#!/usr/bin/env python3
"""Inventory every pinned upstream applet, including disabled alternatives.

Selected entries are evaluated by the C preprocessor with the real generated
Kconfig header. No host BusyBox executable is built or run for this audit.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DECL = re.compile(r'(?<![A-Za-z0-9_])APPLET(?:_ODDNAME|_NOEXEC|_NOFORK|_SCRIPTED)?\(([^)]*)\)')
DIRECTORIES = {'BB_DIR_ROOT': '/', 'BB_DIR_BIN': '/bin',
               'BB_DIR_SBIN': '/sbin', 'BB_DIR_USR_BIN': '/usr/bin',
               'BB_DIR_USR_SBIN': '/usr/sbin'}


def declarations(source):
    """Use exactly the source depth consumed by gen_build_files.sh."""
    rows, fragments = [], []
    for path in sorted(set(source.glob('*/*.c')) | set(source.glob('*/*/*.c'))):
        for number, line in enumerate(path.read_text(encoding='latin-1').splitlines(), 1):
            if not line.startswith('//applet:'):
                continue
            expression = line[len('//applet:'):]
            fragments.append(expression)
            match = DECL.search(expression)
            if match:
                fields = [s.strip() for s in match[1].split(',')]
                if len(fields) not in (3, 5):
                    raise ValueError(f'Unrecognized applet declaration: {path}:{number}')
                name, main = fields[0], fields[0] if len(fields) == 3 else fields[1]
                directory = fields[1] if len(fields) == 3 else fields[2]
                rows.append({'name': name, 'main': main, 'location': DIRECTORIES[directory],
                             'source': str(path.relative_to(source)), 'line': number,
                             'condition': expression})
            elif expression and not expression.startswith(('#', '/*')):
                raise ValueError(f'Unaccounted applet declaration: {path}:{number}')
    # Upstream treats embed/* as scripted applets when embedding is enabled.
    for path in sorted((source / 'embed').glob('*')):
        if path.is_file():
            rows.append({'name': path.name, 'main': 'scripted', 'location': '/usr/bin',
                         'source': str(path.relative_to(source)), 'line': 1,
                         'condition': 'IF_FEATURE_SH_EMBEDDED_SCRIPTS'})
            fragments.append('IF_FEATURE_SH_EMBEDDED_SCRIPTS(APPLET_SCRIPTED(' +
                             path.name + ',scripted,BB_DIR_USR_BIN,BB_SUID_DROP,scripted))')
    return rows, fragments


def selected_entries(source, fragments):
    prefix = '''#include "autoconf.h"
#define APPLET(n,l,s) ENTRY(#n,#n,#l)
#define APPLET_ODDNAME(n,m,l,s,h) ENTRY(#n,#m,#l)
#define APPLET_NOEXEC(n,m,l,s,h) ENTRY(#n,#m,#l)
#define APPLET_NOFORK(n,m,l,s,h) ENTRY(#n,#m,#l)
#define APPLET_SCRIPTED(n,m,l,s,h) ENTRY(#n,#m,#l)
'''
    text = subprocess.check_output(
        ['cc', '-E', '-P', '-x', 'c', '-I', str(source / 'include'), '-'],
        input=prefix + '\n'.join(fragments) + '\n', text=True)
    entries = re.findall(r'ENTRY\("([^"]+)","([^"]+)","([^"]+)"\)', text)
    return {(n, m, DIRECTORIES[d]) for n, m, d in entries}


def inventory(source, built=False):
    lock = json.loads((ROOT / 'config/sources.json').read_text())['busybox']
    actual = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'],
                                     text=True).strip()
    if actual != lock['commit']:
        raise ValueError('BusyBox source pin mismatch')
    rows, fragments = declarations(source)
    selected = selected_entries(source, fragments)
    grouped = {}
    for row in rows:
        grouped.setdefault(row['name'], []).append(row)
    applets = []
    for name, variants in sorted(grouped.items()):
        enabled = [v for v in variants if (v['name'], v['main'], v['location']) in selected]
        unique = {(v['main'], v['location']) for v in enabled}
        if len(unique) > 1:
            raise ValueError('Ambiguous configured applet: ' + name)
        row = enabled[0] if enabled else variants[0]
        applets.append({
            'name': name, 'location': row['location'], 'enabled': bool(enabled),
            'status': ('built but not guest-tested' if built else 'enabled; build not verified')
                      if enabled else 'pending native audit',
            'missing_interfaces': [],
            'reason': '' if enabled else
                'Not enabled in the initial multicall profile; native interface audit required.',
            'verification': [], 'variants': variants,
        })
    config = source / '.config'
    return {'schema': 1, 'busybox': lock,
            'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest(),
            'scope': 'complete upstream name inventory; native feasibility audit incomplete',
            'applets': applets}


def check_reference(result, reference):
    saved = json.loads(reference.read_text())
    keys = ('name', 'location', 'enabled', 'variants')
    project = lambda value: [{key: row[key] for key in keys} for row in value['applets']]
    if saved['busybox'] != result['busybox'] or saved['config_sha256'] != result['config_sha256'] or project(saved) != project(result):
        raise ValueError('Versioned applet/config manifest differs; review and regenerate it')


def write_outputs(result, output):
    output.mkdir(parents=True, exist_ok=True)
    (output / 'applets.json').write_text(json.dumps(result, indent=2) + '\n')
    enabled = [a for a in result['applets'] if a['enabled']]
    (output / 'applets.list').write_text(''.join(a['name'] + '\n' for a in enabled))
    (output / 'applets.list-full').write_text(''.join(
        a['location'].lstrip('/') + '/' + a['name'] + '\n' for a in enabled))
    (output / 'applets.md').write_text(
        '# Pinned BusyBox applet inventory\n\n' + result['scope'] + '.\n\n' +
        '| Applet | Install directory | Enabled | Status |\n| --- | --- | --- | --- |\n' +
        ''.join(f"| `{a['name']}` | `{a['location']}` | {a['enabled']} | {a['status']} |\n"
                for a in result['applets']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--busybox', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--check-reference', type=Path)
    args = parser.parse_args()
    result = inventory(args.busybox.resolve())
    if args.check_reference:
        check_reference(result, args.check_reference)
    write_outputs(result, args.output)
    print(f"Accounted for {len(result['applets'])} upstream names; " +
          f"{sum(a['enabled'] for a in result['applets'])} enabled")


if __name__ == '__main__':
    main()

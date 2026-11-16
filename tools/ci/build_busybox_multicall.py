#!/usr/bin/env python3
"""Build upstream multicall dispatch with native Newlib (never host libc).

The reduced profile is a migration step for #17, not a complete distribution.
The legacy ash and three-applet build paths remain independent regressions.
"""
import argparse
import hashlib
import json
import re
import shlex
import shutil
import subprocess
from pathlib import Path

from newlib_full_link import audit
from busybox_inventory import inventory, write_outputs, check_reference

ROOT = Path(__file__).resolve().parents[2]
OPTIONS = (
    'BUSYBOX', 'FEATURE_INSTALLER', 'STATIC', 'ASH', 'SH_IS_ASH',
    'ASH_OPTIMIZE_FOR_SIZE', 'ASH_INTERNAL_GLOB', 'ASH_ALIAS', 'ASH_ECHO',
    'ASH_PRINTF', 'ASH_TEST', 'ASH_GETOPTS', 'FEATURE_SH_MATH',
    'TRUE', 'FALSE', 'ECHO', 'PRINTF', 'TEST', 'TEST1',
    'BASENAME', 'DIRNAME', 'PWD', 'CAT', 'LS', 'GREP', 'EGREP', 'FGREP',
    'HEAD', 'TAIL', 'WC', 'TEE', 'CUT', 'TR', 'SORT', 'UNIQ', 'CMP',
    'MKDIR', 'RM', 'MV', 'SED', 'AWK', 'FEATURE_AWK_LIBM',
    'FEATURE_FANCY_HEAD', 'FEATURE_WC_LARGE', 'FEATURE_SORT_BIG', 'FEATURE_ALLOW_EXEC',
)
EXTRA_SOURCES = (
    'libbb/appletlib.c', 'coreutils/true.c', 'coreutils/false.c',
    'coreutils/basename.c', 'coreutils/dirname.c', 'coreutils/pwd.c',
    'libbb/full_write.c',
    'libbb/xreadlink.c',
    'libbb/xgetcwd.c', 'libbb/concat_path_file.c', 'libbb/concat_subpath_file.c',
    'libbb/printable_string.c', 'libbb/getopt32.c', 'libbb/single_argv.c',
    'libbb/llist.c', 'libbb/xatonum.c', 'coreutils/cat.c', 'coreutils/ls.c',
    'libbb/bb_cat.c', 'libbb/copyfd.c', 'libbb/wfopen_input.c',
    'libbb/mode_string.c', 'libbb/human_readable.c',
    'libbb/xrealloc_vector.c', 'libbb/recursive_action.c',
    'libbb/common_bufsiz.c', 'libbb/auto_string.c',
    'findutils/grep.c', 'libbb/xregcomp.c', 'libbb/get_line_from_file.c',
    'libbb/fclose_nonstdin.c', 'libbb/wfopen.c',
    *('coreutils/' + name + '.c' for name in
      ('head','tail','wc','tee','cut','tr','sort','uniq','mkdir','rm','mv')),
    'editors/cmp.c', 'editors/sed.c', 'editors/awk.c',
    'libbb/make_directory.c', 'libbb/remove_file.c', 'libbb/copy_file.c',
    'libbb/xfuncs_printf.c', 'libbb/fflush_stdout_and_exit.c',
    'libbb/perror_nomsg_and_die.c', 'libbb/ask_confirmation.c', 'libbb/inode_hash.c',
    'coreutils/libcoreutils/cp_mv_stat.c', 'libbb/last_char_is.c', 'libbb/getopt_allopts.c',
    'libbb/get_line_from_file.c',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('kernel', 'busybox', 'reports'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--newlib-source', type=Path, required=True)
    parser.add_argument('--update-profile', action='store_true',
                        help='regenerate the reviewed config/inventory after changing OPTIONS')
    args = parser.parse_args()
    kernel, busybox, reports = (p.resolve() for p in
                               (args.kernel, args.busybox, args.reports))
    out = reports / 'busybox-multicall'
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'config/sources.json').read_text())
    revision = subprocess.check_output(
        ['git', '-C', busybox, 'rev-parse', 'HEAD'], text=True).strip()
    if revision != lock['busybox']['commit']:
        raise SystemExit('BusyBox source pin mismatch')

    for source in (busybox, kernel, args.newlib_source):
        if subprocess.run(['git', '-C', source, 'diff', '--quiet', 'HEAD']).returncode:
            raise RuntimeError('Refusing modified dependency source: ' + str(source))
    with (out / 'build.log').open('w') as log:
        def run(command, **kwargs):
            command = list(map(str, command))
            log.write('$ ' + shlex.join(command) + '\n')
            log.flush()
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                           check=True, **kwargs)

        run(['make', '-C', busybox, 'allnoconfig'])
        config = busybox / '.config'
        text = config.read_text()
        for name in OPTIONS:
            old = '# CONFIG_' + name + ' is not set'
            if old not in text and 'CONFIG_' + name + '=y' not in text:
                raise RuntimeError('Missing config symbol: ' + name)
            text = text.replace(old, 'CONFIG_' + name + '=y')
        for name in ('ASH_BASH_COMPAT',):
            text = re.sub(r'(?m)^CONFIG_' + name + r'=y$', '# CONFIG_' + name + ' is not set', text)
        config.write_text(text)
        run(['make', '-C', busybox, 'oldconfig'], input='\n' * 10000, text=True)
        # Kconfig emits wall-clock comments; strip only that generated date so
        # the reviewed config and manifest hash are reproducible across runs.
        config.write_text(re.sub(r'(?m)^# (?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) [^\n]*\n', '', config.read_text()))
        if args.update_profile:
            (ROOT / 'config/busybox-multicall.config').write_bytes(config.read_bytes())
        if config.read_bytes() != (ROOT / 'config/busybox-multicall.config').read_bytes():
            raise RuntimeError('Regenerated Kconfig differs from versioned native profile')
        selected = set(config.read_text().splitlines())
        for name in OPTIONS:
            if 'CONFIG_' + name + '=y' not in selected:
                raise RuntimeError('Kconfig rejected: ' + name)
        run(['make', '-C', busybox, 'prepare'])
        # These are host generators only, never linked into the target.
        run(['make', '-C', busybox, 'applets/usage', 'applets/applet_tables'])
        run(['make', '-C', busybox, 'include/usage_compressed.h',
             'include/applet_tables.h'])
        # `prepare` may run silentoldconfig and restore the date comment.
        config.write_text(re.sub(r'(?m)^# (?:Mon|Tue|Wed|Thu|Fri|Sat|Sun) [^\n]*\n', '', config.read_text()))
        shutil.copytree(busybox / 'include', out / 'include', dirs_exist_ok=True)
        platform = (busybox / 'include/platform.h').read_text()
        for name in ('HAVE_MNTENT_H', 'HAVE_SYS_STATFS_H', 'HAVE_NET_ETHERNET_H',
                     'HAVE_DEV_FD', 'HAVE_UNLOCKED_LINE_OPS', 'HAVE_UNLOCKED_STDIO'):
            platform = platform.replace('#define ' + name + ' 1',
                                        '/* Native Newlib: unavailable ' + name + ' */')
        # Shadow the header rather than editing the pinned source.
        (out / 'include/platform.h').write_text(platform)
        newlib = reports / 'newlib-full'
        runtime = reports / 'newlib-target'
        flags = ['gcc-14', '-m64', '-Os', '-std=gnu11', '-ffreestanding',
                 '-nostdinc', '-fno-builtin', '-fno-pie', '-fno-pic',
                 '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
                 '-D_GNU_SOURCE', '-DSA_RESTART=2', '-DBB_VER="1.36.1"',
                 '-DBB_EXTRA_VERSION=""', '-Werror=implicit-function-declaration',
                 '-Werror=int-conversion', '-I' + str(out / 'include'),
                 '-I' + str(ROOT / 'ports/newlib/ash/include'),
                 '-I' + str(kernel / 'runtime/abi'),
                 '-I' + str(busybox / 'include'),
                 '-isystem', str(newlib / 'include'), '-isystem',
                 subprocess.check_output(['gcc-14', '-print-file-name=include'],
                                         text=True).strip(),
                 '-include', str(busybox / 'include/autoconf.h'),
                 '-include', str(ROOT / 'ports/newlib/multicall/native_posix.h')]
        # Newlib's generic unknown-elf profile omits its existing BSD regex
        # implementation. Compile the pinned upstream sources, not host regex.
        newlib_source = args.newlib_source.resolve()
        newlib_revision = subprocess.check_output(
            ['git', '-C', newlib_source, 'rev-parse', 'HEAD'], text=True).strip()
        if newlib_revision != '8ba4275b83ec27529f67e0d477611fa6d8d6e6bd':
            raise RuntimeError('Newlib source pin mismatch')
        regex_objects = []
        for unit in ('regcomp', 'regexec', 'regerror', 'regfree', 'collate', 'collcmp'):
            obj = out / ('newlib-' + unit + '.o')
            run(flags + ['-I' + str(newlib_source / 'newlib/libc/locale'),
                         '-c', newlib_source / ('newlib/libc/posix/' + unit + '.c'),
                         '-o', obj])
            regex_objects.append(obj)
        units = list(dict.fromkeys(json.loads(
            (ROOT / 'config/ash-sources.json').read_text()) + list(EXTRA_SOURCES)))
        sources = [busybox / unit for unit in units]
        sources += [ROOT / 'ports/newlib/ash/libbb_compat.c',
                    ROOT / 'ports/unix/fnmatch.c',
                    ROOT / 'ports/newlib/multicall/libbb_native.c']
        objects = []
        for index, source in enumerate(sources):
            obj = out / (str(index) + '.o')
            extra = ['-Dmain=p9_busybox_main'] if source.name == 'appletlib.c' else []
            if source.name == 'appletlib.c':
                # Upstream installer otherwise returns success after every
                # unsupported link attempt. Fail at the primitive boundary,
                # including recursively dispatched `busybox busybox --install`.
                dispatch = source.read_text()
                old = '\t\t\tbb_simple_perror_msg(fpc);'
                if dispatch.count(old) != 1:
                    raise RuntimeError('Upstream installer layout changed')
                dispatch = dispatch.replace(old, '\t\t\tbb_simple_perror_msg_and_die(fpc);')
                source = out / 'appletlib-native.c'
                source.write_text(dispatch)
            if source.name == 'make_directory.c':
                text = source.read_text()
                old = '\t\t\t\torg_mask = umask(0);'
                if text.count(old) != 1:
                    raise RuntimeError('Upstream mkdir layout changed')
                text = text.replace(old,old + '\n\t\t\t\tif (org_mask == (mode_t)-1) { fail_msg = "apply creation mask to"; break; }')
                source = out / 'make_directory-native.c'
                source.write_text(text)
            if source.name == 'sed.c':
                text = source.read_text()
                old = '\t\t\tfchmod(nonstdoutfd, statbuf.st_mode);\n\t\t\tfchown(nonstdoutfd, statbuf.st_uid, statbuf.st_gid);'
                if text.count(old) != 1:
                    raise RuntimeError('Upstream sed metadata layout changed')
                text = text.replace(old, '\t\t\tif (fchmod(nonstdoutfd, statbuf.st_mode) < 0 || fchown(nonstdoutfd, statbuf.st_uid, statbuf.st_gid) < 0)\n\t\t\t\tbb_simple_perror_msg_and_die(*argv);')
                source = out / 'sed-native.c'
                source.write_text(text)
            if source.name == 'libbb_compat.c':
                extra = ['-DPLAN2001_MULTICALL=1']
            run(flags + extra + ['-c', source, '-o', obj])
            objects.append(obj)
        applets = inventory(busybox)
        if args.update_profile:
            (ROOT / 'config/busybox-applets.json').write_text(json.dumps(applets, indent=2) + '\n')
        check_reference(applets, ROOT / 'config/busybox-applets.json')
        table = (busybox / 'include/applet_tables.h').read_text()
        table = table.split('const char applet_names[]',1)[1].split(';',1)[0]
        table_names = re.findall(r'^"([^"\n]+)" "',table,re.M)
        if table_names != [a['name'] for a in applets['applets'] if a['enabled']]:
            raise RuntimeError('Upstream applet table/inventory mismatch')
        write_outputs(applets, out)
        paths = ['/bin/busybox'] + [a['location'] + '/' + a['name']
                                    for a in applets['applets'] if a['enabled']]
        aliases = out / 'aliases.c'
        aliases.write_text('#include <string.h>\n#include <stddef.h>\n'
            'static const char *const paths[] = {\n' +
            ',\n'.join(json.dumps(path) for path in paths) + ',NULL};\n'
            'const char *p9_multicall_path(unsigned i) { return i < '
            + str(len(paths)) + ' ? paths[i] : NULL; }\n'
            'const char *p9_multicall_resolve(const char *path) {\n'
            'for(unsigned i=0;paths[i];++i) if(!strcmp(path,paths[i])) '
            'return "/bin/busybox"; return NULL; }\n')
        aliases_obj = out / 'aliases.o'
        run(flags + ['-c', aliases, '-o', aliases_obj])
        main_obj = out / 'main.o'
        run(flags + ['-c', ROOT / 'ports/newlib/multicall/main.c', '-o', main_obj])
        archive = out / 'busybox.a'
        archive.unlink(missing_ok=True)
        run(['ar', 'rcs', archive, *objects, *regex_objects])
        elf = out / 'busybox.elf'
        run(['gcc-14', '-m64', '-nostdlib', '-static', '-no-pie',
             '-Wl,--wrap=longjmp', '-Wl,--gc-sections', '-Wl,--build-id=none',
             '-Wl,-z,noexecstack', '-Wl,-T,' + str(ROOT / 'ports/newlib/full/user-newlib.ld'),
             '-o', elf, kernel / 'build/gcc/mpy/mpyobj/rt_startup.o',
             runtime / 'startup.o', main_obj, aliases_obj,
             *[runtime / (x + '.o') for x in
               ('syscalls', 'process', 'inherit', 'file_state', 'fd_table', 'posix', 'stdio_process',
                'dirent', 'ash_posix', 'pipe', 'pvd', 'uxfs', 'uxobject', 'objtail', 'uxshell', 'uxservice',
                'uxfd', 'project_native', 'project_record', 'rt_stubs-newlib')],
             '-Wl,--start-group', archive, newlib / 'libc.a', newlib / 'libm.a',
             '-lgcc', '-Wl,--end-group'])
        result = audit(elf, out, ['_start', 'p9_busybox_main', 'ash_main',
                                  'find_applet_by_name', 'execve', 'waitpid'])
        applets = inventory(busybox)
        write_outputs(applets, out)
        (out / 'expected_applets.h').write_text(
            '#define EXPECTED_APPLETS ' + json.dumps((out / 'applets.list').read_text()) +
            '\n#define EXPECTED_APPLETS_FULL ' + json.dumps((out / 'applets.list-full').read_text()) +
            '\nstatic const char *const expected_names[] = {' +
            ','.join(json.dumps(a['name']) for a in applets['applets'] if a['enabled']) +
            '};\n')
        probe = out / 'guest-probe.o'
        run(flags + ['-I' + str(out), '-I' + str(ROOT / 'ports/newlib/full'),
                     '-I' + str(kernel / 'runtime/abi'), '-c',
                     ROOT / 'tests/guest/busybox_multicall.c', '-o', probe])
        # Reuse the exact native runtime link, substituting only the driver main.
        link = ['gcc-14', '-m64', '-nostdlib', '-static', '-no-pie',
                '-Wl,--wrap=longjmp', '-Wl,--gc-sections', '-Wl,--build-id=none',
                '-Wl,-z,noexecstack', '-Wl,-T,' + str(ROOT / 'ports/newlib/full/user-newlib.ld'),
                '-o', out / 'guest-probe.elf', kernel / 'build/gcc/mpy/mpyobj/rt_startup.o',
                runtime / 'startup.o', probe, aliases_obj,
                *[runtime / (x + '.o') for x in
                  ('syscalls', 'process', 'inherit', 'file_state', 'fd_table', 'posix', 'stdio_process',
                   'dirent', 'ash_posix', 'pipe', 'pvd', 'uxfs', 'uxobject', 'objtail', 'uxshell', 'uxservice',
                   'uxfd', 'project_native', 'project_record', 'rt_stubs-newlib')],
                '-Wl,--start-group', newlib / 'libc.a', newlib / 'libm.a',
                '-lgcc', '-Wl,--end-group']
        run(link)
        audit(out / 'guest-probe.elf', out, ['_start', 'execve', 'fork', 'waitpid'])
        write_outputs(inventory(busybox, built=True), out)
        (out / 'busybox.config').write_bytes(config.read_bytes())
        (out / 'LICENSE.busybox').write_bytes((busybox / 'LICENSE').read_bytes())
        (out / 'manifest.json').write_text(json.dumps({
            'artifact': result, 'busybox': lock['busybox'],
            'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest(),
            'kernel': subprocess.check_output(
                ['git', '-C', kernel, 'rev-parse', 'HEAD'], text=True).strip(),
            'newlib': '8ba4275b83ec27529f67e0d477611fa6d8d6e6bd',
            'compiler': subprocess.check_output(['gcc-14', '--version'], text=True).splitlines()[0],
            'newlib_libc_sha256': hashlib.sha256((newlib / 'libc.a').read_bytes()).hexdigest(),
            'newlib_libm_sha256': hashlib.sha256((newlib / 'libm.a').read_bytes()).hexdigest(),
            'userland_commit': subprocess.check_output(['git', '-C', ROOT, 'rev-parse', 'HEAD'], text=True).strip(),
            'userland_worktree_dirty': bool(subprocess.check_output(
                ['git', '-C', ROOT, 'status', '--porcelain'], text=True).strip()),
            'guest_execution': 'NOT RUN', 'hardware': 'NOT RUN', 'arm64': 'NOT RUN',
            'installed_aliases': paths,
        }, indent=2) + '\n')
        print(elf)


if __name__ == '__main__':
    main()

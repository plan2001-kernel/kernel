#!/usr/bin/env python3
"""Repeated warm-cache F0 image vs ordinary Linux files experiment.

The compare workload is file-I/O replay, NOT GCC/Make on F0. No cache eviction.
Results include F0 volume/index opening and each ordinary file's open/close.
"""
import os
from pathlib import Path
import shutil
import statistics
import time

import ci

ROUNDS_READ = 9
ROUNDS_MIXED = 7


def warm_run(backend, image, manifest):
    # A complete traversal before every timed trial establishes warm file data,
    # index, inodes and dentries as far as the Linux page cache permits.
    p = ci.run([ci.F0, 'replay', backend, ci.ROOT, image, manifest], capture=True)
    return ci.parse(p.stdout)


def recorded_run(backend, image, manifest, label, round_number):
    p = ci.run([ci.F0, 'replay', backend, ci.ROOT, image, manifest], capture=True)
    m = ci.parse(p.stdout)
    m['round'] = round_number
    print(f'WARM_{label}:', m, flush=True)
    return m


def summarize(label, rows):
    assert len(rows) == 2
    ms = {}
    reads = {}
    for backend in ('ext4', 'f0'):
        b = rows[backend]
        if len(b) < 3:
            raise RuntimeError(f'too few samples: {backend}')
        ds = {r['digest'] for r in b}
        if len(ds) != 1:
            raise RuntimeError(f'checksum differs within backend {backend}')
        ms[backend] = statistics.median(r['ms'] for r in b)
        reads[backend] = max(r['read_bytes'] for r in b)
        print(f'{label} {backend}: median_ms={ms[backend]:.3f} '
              f'min_ms={min(r["ms"] for r in b):.3f} '
              f'max_ms={max(r["ms"] for r in b):.3f} '
              f'max_proc_read_bytes={reads[backend]} '
              f'median_proc_write_bytes={statistics.median(r["write_bytes"] for r in b):.0f}')
    if rows['f0'][0]['digest'] != rows['ext4'][0]['digest']:
        raise RuntimeError(f'{label} F0/ext4 checksum mismatch')
    print(f'{label} ext4_over_f0={ms["ext4"]/ms["f0"]:.3f}x '
          f'NO_STORAGE_READS_RECORDED={"YES" if all(r["read_bytes"]==0 for b in rows.values() for r in b) else "NO"}')


def read_only():
    manifest = ci.BASE / 'read-only.txt'
    manifest.write_text(ci.IMPORT.read_text())
    # All source and larger blob files, not just paths with one namespace.
    # Preheat both backends before measuring, then again immediately before
    # every timed run. This deliberately measures a warm-cache workload.
    warm_run('ext4', ci.BASE_IMG, manifest)
    warm_run('f0', ci.BASE_IMG, manifest)
    rows = {'ext4': [], 'f0': []}
    for round_number in range(ROUNDS_READ):
        order = ('ext4', 'f0') if round_number % 2 == 0 else ('f0', 'ext4')
        for backend in order:
            warm_run(backend, ci.BASE_IMG, manifest)
            rows[backend].append(recorded_run(backend, ci.BASE_IMG, manifest, 'READ', round_number))
    summarize('READ_ONLY', rows)


def mixed_io():
    rows = {'ext4': [], 'f0': []}
    for round_number in range(ROUNDS_MIXED):
        order = ('f0', 'ext4') if round_number % 2 == 0 else ('ext4', 'f0')
        for backend in order:
            # Same persisted input contents and no pre-existing output for both.
            # Reset is outside timed interval. Copying F0 input image also
            # populates its page cache, like a warm source project on disk.
            shutil.rmtree(ci.ROOT / 'out', ignore_errors=True)
            (ci.ROOT / 'out').mkdir()
            trial = ci.BASE / 'warm-trial.f0'
            if trial.exists():
                trial.unlink()
            if backend == 'f0':
                shutil.copyfile(ci.BASE_IMG, trial)
                # Read the whole source set to warm image-backed data pages;
                # do NOT replay writes in the warmup.
                ci.run([ci.F0, 'verify', ci.ROOT, trial, ci.IMPORT], capture=True)
            else:
                # Preheat identical inputs and name metadata before timed run.
                warm_run('ext4', ci.BASE_IMG, ci.IMPORT)
            rows[backend].append(recorded_run(backend, trial, ci.MANIFEST, 'MIXED', round_number))
            if trial.exists():
                trial.unlink()
    summarize('MIXED_IO', rows)
    print('NOTE MIXED: writes are buffered; neither backend pays a final fsync.')
    print('NOTE MIXED: F0 trial image copy and ext4 output reset are outside timed replay.')


def make_workflow():
    # Real make always uses the normal ext4 tree; there is no GCC-on-F0 adapter.
    ci.run(['make', '-j4'], cwd=ci.ROOT, capture=True)
    app = ci.run([ci.ROOT / 'app'], capture=True).stdout.strip()
    print('REAL_MAKE_FIRST_PASS: PASS program_output=' + app)
    for scenario, command, n in [('NOOP', ['make', '-j4'], 7),
                                  ('FORCED_REBUILD', ['make', '-B', '-j4'], 3)]:
        values = []
        for i in range(n):
            start = time.monotonic()
            ci.run(command, cwd=ci.ROOT, capture=True)
            ms = (time.monotonic() - start) * 1000
            values.append(ms)
            print(f'REAL_MAKE_WARM_{scenario}: round={i} elapsed_ms={ms:.3f}')
        print(f'REAL_MAKE_WARM_{scenario}_MEDIAN_MS={statistics.median(values):.3f}')
    print('REAL_MAKE_F0: NOT RUN (gcc cannot read F0 image without adapter).')


def main():
    print('MODE: WARM CACHE; NO drop_caches AND NO posix_fadvise(DONTNEED)', flush=True)
    ci.build()
    ci.make_data()
    ci.create_image()
    ci.verify_recovery()
    backing = ci.run(['findmnt', '-T', ci.ROOT, '-n', '-o', 'FSTYPE,SOURCE'],
                     capture=True, check=False)
    print('BACKING FILESYSTEM:', backing.stdout.strip() if backing.returncode == 0 else 'unknown')
    make_workflow()
    read_only()
    mixed_io()
    print('TEST PASS: F0 data and ordinary files matched for both workloads.')
    print('NOT MEASURED: end-to-end Make with F0, native Plan2001 F0 driver, physical laptop.')
    print('LIMITATION: F0 uses direct C calls and a Linux image-backed pread/pwrite, '
          'while ordinary files use Linux open/read/close syscalls.')


if __name__ == '__main__':
    main()
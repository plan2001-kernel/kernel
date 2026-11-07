# Disposable QEMU/OVMF native GE/O testing

This page documents the two owner-sanctioned host-side QEMU scripts. They
validate the current canonical amd64 kernel, the GCC-built MicroPython ELF
stored in an O volume, and real GE/O persistence. They are **not** a general
regression framework, a build prerequisite, or a hardware installer.

- `tools/qemu-o-boot.sh` — one bounded boot of a fresh disposable O image.
- `tools/qemu-o-persist.sh` — two boots against the same disposable GPT disk
  and the same writable O partition.

Both scripts build or reuse only the normal canonical artifacts, create only
regular files under `build/qemu-o/` (or `--output-dir`), and never open a
physical block device. There is no `--device` option and no installation or
`sudo` path.

## Prerequisites

Host packages and tools:

- `qemu-system-x86_64` with KVM support (QEMU 10.x was used for the recorded
  run); `/dev/kvm` must be readable and writable for the default mode.
- OVMF 4 MiB firmware. The defaults are
  `/usr/share/OVMF/OVMF_CODE_4M.fd` and
  `/usr/share/OVMF/OVMF_VARS_4M.fd`; override with `OVMF_CODE`/`OVMF_VARS`.
- `mtools` (`mformat`, `mcopy`, `mmd`) for the FAT ESP.
- Python 3 (the scripts use it for the GPT and for QEMU monitor keyboard
  injection).
- The normal native build dependencies: GCC 14, Clang 19/LLD, GNU make and
  glibc.

If KVM is unavailable, pass `--tcg`. TCG is a slow software fallback and is
supported by the scripts; use a longer `--timeout` (for example 240 seconds).
`--interactive` additionally requires a working graphical display for the GTK
window; the default headless path needs no display.

## Build the canonical artifacts

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-efi-gcc.sh -j 8
tools/build-mpy-gcc.sh -j 8
tools/build.sh
```

The scripts reuse these artifacts when present. `--rebuild` runs all four
commands first; `--no-build` refuses to run without them. The scripts never
use a 9front VM, Ken C, `mk`, an old fixture image, or a second build system.

## One boot

```sh
tools/qemu-o-boot.sh
```

The default writes to `build/qemu-o/boot/`. A useful explicit invocation:

```sh
tools/qemu-o-boot.sh --output-dir build/qemu-o/boot --timeout 120
```

It creates a fresh bare O image with `tools/gcc-ovol-ream.sh --image` (the
real `ge/format/ream.c::reamo()` formatter plus the same-version
`mpy-cross`), seeds `tools/host/odemo.py` and the current
`build/gcc/micropython.elf`, and places the O image in a real GPT partition
next to a FAT EFI System Partition in `disk.img`. The guest is booted by the
generated partition's unique GUID (`PARTUUID`) through the ordinary
`oboot=server` / `obootrepl=1` path.

A PASS requires all of the following in the sanitized serial log:

- the UEFI loader runs and closes the ELF kernel file;
- the kernel reaches `KERNEL-READY` and `o: O volume ... opened without FS`;
- `o: OEXECUTE oid=0x4f45584543000003` loads the MicroPython ELF;
- the fresh Space has no graph, runs `init.run()` with result `1`, commits
  `init` version 1, and reports `init: shared Counter n 1`;
- `PLAN2001-O: BOOT SUCCESS`, `PLAN2001-O: GE/O COMMIT SUCCESS` and
  `PLAN2001-O: REPL READY` (or `SYSTEM IDLE` with `--no-repl`);
- no `panic`, `suicide`, `BOOT FAILED`, `bad disk`, `error syncing:`,
  `cache flush:` or other I/O failure;
- `oinspect` confirms OID `0x4f45584543000003` still holds the exact bytes of
  the build's `micropython.elf`.

Useful options:

```text
--mib N               O volume MiB (default 129, minimum 129)
--controller virtio    default; virtio-blk-pci-non-transitional (sdF0)
--controller ahci      diagnostic comparison only; see below
--tcg                  software fallback instead of KVM
--no-repl              boot with -N and require SYSTEM IDLE
--interactive          GTK window; held QOB_HOLD seconds (default 15)
--udp6-peer HOST:QEMU   attach virtio to a local UDP-encapsulated Ethernet peer;
                       HOST is the peer's UDP port, QEMU the local port
--jobs N               canonical build parallelism
-h, --help             full option list
```

Logs and generated files:

```text
build/qemu-o/boot/disk.img        disposable GPT disk (ESP + O partition)
build/qemu-o/boot/o.img           bare fresh O image before insertion
build/qemu-o/boot/disk.layout     partition offsets and sector counts
build/qemu-o/boot/disk.partuuid   generated O partition GUID
build/qemu-o/boot/plan9.ini       exact guest configuration
build/qemu-o/boot/serial-boot.log raw serial output
build/qemu-o/boot/boot-boot.log   sanitized serial output
build/qemu-o/boot/qemu-boot.log   QEMU stderr/stdout
build/qemu-o/boot/report.txt      PASS/FAIL report with hashes and milestones
build/qemu-o/boot/o-after.json    read-only oinspect result
```

Build outputs under `build/` are disposable and can be regenerated.

The optional UDP6 peer is intended for focused guest/host datagram
experiments. It changes only the disposable QEMU network attachment; the
default remains `-nic none`. QEMU encapsulates Ethernet frames in UDP to the
peer at `127.0.0.1:HOST`; it binds `127.0.0.1:QEMU` for the reverse direction.
The peer must process actual IPv6/NDP/UDP frames, not pretend that IPv4 or
host forwarding is IPv6. Do not report packet exchange unless both peer and
guest logs show the received payloads and source metadata.

## Two-boot persistence

```sh
tools/qemu-o-persist.sh
```

The default output is `build/qemu-o/persist/`. The script creates **one**
fresh disposable GPT disk/O image, boots it, and then boots the same
`disk.img` again without recreating or reseeding the O partition. The default
check uses the existing persistent `odemo` Counter:

- boot 1: `space: O: init has no graph`, `init: run() -> 1`,
  `space: O: committed init version 1`, `init: shared Counter n 1`;
- boot 2: `space: O: restored init version 1`,
  `space: O: init restored (odemo), no module initialization`,
  `init: run() -> 2`, `space: O: committed init version 2`,
  `init: shared Counter n 2`.

The script also requires:

- the same `PARTUUID` and the same O-volume byte offset in both boots;
- the disk SHA-256 changes after boot 1 and changes again after boot 2;
- `oinspect` reports the O Space version advancing (`N` increases) and the
  same MicroPython object bytes in both boots;
- no panic, bad-disk, flush or sync error.

Logs are `serial-boot1.log`, `serial-boot2.log`, `boot-boot1.log`,
`boot-boot2.log`, the per-boot `o-boot1.*`/`o-boot2.*` images and JSON, and
`report.txt` under `build/qemu-o/persist/`.

Options mirror the single-boot script, plus REPL input/expected text:

```text
--repl                use the native REPL (-I) on both boots
--boot1-input FILE    type FILE into boot 1's native PS/2 REPL
--boot2-input FILE    type FILE into boot 2's native PS/2 REPL
--boot1-expect TEXT   require TEXT in boot 1's serial log
--boot2-expect TEXT   require TEXT in boot 2's serial log
```

The input is injected through the QEMU HMP monitor with the canonical
`p9console.c` Finnish keyboard mapping. The harness waits for the REPL to
return to its prompt and for the final prompt to be the tail of the serial
stream before evaluating `--expect`, so an earlier prompt during a multi-line
input cannot hide a still-running final commit. Use an expected result token
that does not appear in the input itself.

`--boot1-input`/`--boot2-input` support persistence checks for variables,
functions and aliases. The N13 integration branch ran such a two-boot check;
see [N13-INTEGRATION](N13-INTEGRATION.md). Its result is not a claim about
the current `main` branch until that work is reviewed and merged.

The current verified input checks are ordinary REPL evaluation and a plain
REPL assignment across two boots:

```sh
printf 'print(6*7)\n' >/tmp/repl-print.py
tools/qemu-o-boot.sh --repl-input /tmp/repl-print.py --expect '^42'

printf 'x = 12345\n' >/tmp/repl-set.py
printf 'print(x)\n' >/tmp/repl-get.py
tools/qemu-o-persist.sh --boot1-input /tmp/repl-set.py \
  --boot2-input /tmp/repl-get.py --boot2-expect '^12345'
```

The second command verifies that boot 2 restores the REPL assignment from the
same GE/O partition. It does not verify function/alias persistence.

## Controller choice and the `sdE0: bad disk` observation

The default and verified controller is virtio 1.0
(`virtio-blk-pci-non-transitional`, guest unit `sdF0`). The recorded runs read
and committed to the O partition through `sdvirtio10`, including
`sdvirtio: flush PASS`.

`--controller ahci` is kept for diagnosis and comparison only. On this
QEMU/q35 host it reaches the AHCI unit `sdE0`, identifies the disk and reads
the GPT and O partition, but repeated runs were **intermittent**: some boots
completed, while others failed on O block I/O with `sdE0: bad disk`. The same
GPT layout, partition offsets, generated `PARTUUID` handling and identical O
image bytes pass reliably through virtio. This reproduces the earlier
observation and localizes it to the AHCI/QEMU timing/attachment path rather
than the GE/O engine. Do not use the AHCI option as the supported success
path, and do not weaken the error checks to hide it.

The disk is created with an inline, verified GPT writer: protective MBR,
primary and backup headers and entry arrays, a standard ESP type and the
Plan2001-O O partition type. There is no dependency on `sfdisk`, `parted` or
`gdisk`. The O partition's unique GUID is generated fresh for each disposable
disk and written consistently into the GPT and `plan9.ini`.

## Safety and exit status

- The scripts never call `sudo`, never write a physical partition, never
  touch p5, ESP, GRUB, firmware variables or UEFI settings, and never use a
  whole-disk operation.
- Output paths under `/dev`, `/proc` or `/sys` are rejected. Existing target
  names that are not plain regular files (including symlinks) are rejected
  before any write. The scripts have no device-selection option. Both
  refusals were exercised: `--output-dir /dev` and a symlinked `o.img` target
  fail before writing.
- `0` means PASS. `1` means FAIL or a blocked prerequisite; the reason and
  serial tail are printed and the report is kept. `2` means usage error.
  QEMU failure, timeout, panic, `bad disk`, flush/sync I/O errors, a missing
  GE/O commit, or an absent second-boot confirmation are never reported as
  PASS.

# Plan2001-O

<p align="center">
  <img src="assets/plan2001-o-mascot.svg" alt="Plan2001-O mascot: a rabbit playing the banjo" width="280">
</p>

**Project mascot:** the banjo-playing rabbit.

**Plan2001-O is an experimental operating system for the amd64 architecture.**

Plan2001-O is a small, byte-oriented operating-system proof of concept:
persistent Space occurrences (*olios*), contextual objects, observations and,
eventually, native relations, built on the existing Plan2001/9front code and
conventional amd64 hardware. amd64 is the only supported and intended target.

The [Finnish manifesto](docs/OLIO-OBJEKTI-MANIFESTI.md) keeps the philosophical
meaning of *olio* and *object* separate from this implementation. The
[general software specification](docs/GENERAL-SOFTWARE-SPECIFICATION.md)
describes the practical computer. This PoC deliberately uses ordinary bytes,
64-bit OIDs and conventional amd64 hardware; the manifesto does not mandate
exotic storage or hardware.

NS2–NS4 object namespaces and kernel-authorized GE relation queries are described
in [NS integration](docs/NS-INTEGRATION.md), with machine-readable results in
[NS verification](docs/NS-VERIFICATION.json). The weekend test harnesses are
retained in the separate companion bundle (`../Plan2001-O-weekend-tests/plan2001`);
they are not build or boot prerequisites of this canonical tree.

Optional [CBOR and O-Envelope v1](docs/CBOR-O-ENVELOPE.md) encode application
payloads without changing GE/O storage or namespace authorization. Its host and
native suites, including the independent `cbor2` reference check, live in the
same companion bundle.

[UJ1 Aino user journey](docs/UJ1-USER-JOURNEY.md) integrates native editing,
TinyCC compilation, isolated TCP6/HTTP processes, publication and reboot
persistence. Its userspace programs and two-boot harness are in the companion
bundle; per-case results are in [UJ1 verification](docs/UJ1-VERIFICATION.json).

## Current system

One amd64 `pc64` configuration, booted from a normal ESP through GRUB:

```text
UEFI -> GRUB -> Clang 19/LLD PE32+ EFI loader (bootx64.efi)
     -> GCC 14 ELF kernel (9pc64.elf, no embedded interpreter)
     -> native GE/O volume on partition 5, selected by PARTUUID
     -> OVOLOPEN / OSPACEOPEN / OEXECUTE
     -> GCC 14 MicroPython ELF stored in GE/O at OID 0x4f45584543000003
     -> restored init Space, init.run(), GE/O commit, interactive REPL
```

- **Kernel:** GCC 14, built as an ELF64 `ET_EXEC` image. The kernel contains no
  MicroPython bytes.
- **NS1 process namespaces:** the existing Pgrp/OContext lifecycle is audited,
  with a kernel-group initialization race corrected and native SMP regressions
  for sharing, copies, empty views, authorization, OSPAWN and exec. See
  [NS1 design and tests](docs/NS1-PROCESS-NAMESPACES.md).
- **Loader:** Clang 19/LLD PE32+ EFI application. It reads `plan9.ini` and the
  kernel from its own ESP directory.
- **MicroPython:** an independently built GCC 14 freestanding ELF, stored byte
  for byte as a normal O object. The kernel loads it through native `OEXECUTE`.
- **GE relations R1:** the `ge` module creates persistent content/tag objects
  and directed OID relations, with multi-tag AND queries. Same-OID/content
  persistence is verified across two real VM boots. See
  [GE relations R1](docs/GE-RELATIONS-R1.md).
- **TinyCC experiment:** native userspace C/assembler/linker port, verified
  compiling and executing real Space 2 objects inside QEMU. See
  [TinyCC and Space 2](docs/TINYCC-SPACE2.md) for the bounded port and test.
- **Storage:** native GE/O v1, reamed by the repository's real GE/O reamer
  (`ge/format/ream.c::reamo()`) built as a Linux GCC host program, not by a
  Linux header writer or a simplified Python image writer. The `.mpy` seed
  is compiled on Linux by the same-version `mpy-cross`. `ge/format/` now
  contains only the eight O formatter/seed units and their two headers; the
  inherited GEFS FS/9P server sources have been removed.
- **Runtime:** `runtime/` contains the live GCC libc, crypto, IPv4, AML and FIS
  members used by the kernel, MicroPython or the formatter, plus the kernel
  AES-GCM subset, GE relations and optional CBOR library. Unused 9front
  sources, Ken C amd64 assembly, `ucd` generators/data and mkfiles have been
  removed. `runtime/abi/sys.h` adds the weekend slots 101–113; earlier numbers
  and layouts are unchanged.
- **Canonical boot:** `oboot=server`. The kernel bootstrap does not run `oinit`
  and does not mount a filesystem root. `oinit` and `kbdfs` are historical
  bootstrap helpers; the canonical boot path does not require them and the
  source tree no longer carries their 9front userland sources.

The N10.1 GCC kernel and Clang UEFI loader were booted on the physical Dell
Precision 7520 from the single `Plan2001-O` GRUB entry and reached an
interactive `>>>` MicroPython REPL. The earlier pre-cleanup binary also
demonstrated PS/2 keyboard input, `dir()`, `import space`, `space.info()` and
real GE/O commits. After N11.HW1 installed that configuration, the owner
reported three physical boots with a working REPL and a REPL-assigned
variable that survived across them (owner-reported, not an agent-read log).
N11.7 was verified only in QEMU on a fresh disposable O image; it has not yet
been installed or booted on the physical machine.

## The `plan9.ini` contract

`plan9.ini` is a line-oriented file. **Every line, including the last, must end
in LF (`0x0a`).** There is no implicit final line.

The canonical content is:

```ini
bootfile=/EFI/plan2001-gcc/9pc64.elf
oboot=server
obootvol=PARTUUID=<live-p5-partuuid>
obootrepl=1
*ncpu=1
```

The EFI loader rejects a nonempty final line that lacks LF and prints exactly
`plan9.ini: missing final newline`; it does not silently drop the last setting
or fall back to another CPU count or boot profile. `tools/gcc-o-install.sh`
writes the file directly with a trailing LF and verifies the staged and
installed bytes (including the final byte) before reporting success.

### Optional boot selectors and CPU placement

The default line set above needs no selectors. The installer and kernel also
accept these optional lines:

```ini
obootexec=0x0          # canonical MicroPython, or a published 64-bit OID
obootcpu=2             # exact logical CPU for the first user process
*ncpu=4                # requested maximum number of logical CPUs
```

- `obootexec` absent or `0x0` selects the canonical MicroPython executable
  `0x4f45584543000003` in the existing `init` Space. **`0x0` is a bootstrap
  selector alias, not a stored object at OID 0.** A nonzero value selects that
  published executable by its real `(Space, OID)` identity; a malformed,
  overflowing, missing or non-executable value is a hard boot error with no
  silent fallback to MicroPython. Canonical MicroPython keeps its
  `micropython -O <volume> -S init -I|-N` arguments; another executable
  receives the generic `oexec <volume> init` convention.
- `obootcpu` is a kernel `Mach.machno` index (0 is the boot CPU), not a
  firmware APIC ID or a physical core number. When present it is a strict
  single-CPU **affinity** for the first user process and survives `OEXECUTE`.
  It is not an exclusive CPU reservation: interrupts, kernel work and other
  processes may still share that CPU. The index must be below `*ncpu`, online,
  and past its per-CPU scheduler handoff; an offline, out-of-range or
  not-ready CPU is rejected instead of being clamped or moved to CPU 0.
- `*ncpu` requests a maximum of 1..128 logical CPUs (`MAXMACH`); it does not
  prove that every requested CPU came online. `tools/gcc-o-install.sh` rejects
  `--ncpu` outside 1..128, `--obootcpu` outside 0..127 and `--obootcpu >=
  --ncpu` before any ESP or GRUB access.

The boot log reports the actual CPU at the first userspace dispatch, for
example `n13: firstuser pid=... cpu=2 requested=2`, so the effective placement
is directly observable. The same online-and-scheduler-ready rule gates the
exact CPU requested by `OSPAWN`; see [N13-INTEGRATION](docs/N13-INTEGRATION.md)
for its grant, view, budget and network contracts.

## Build

Host tools: GCC 14, Clang 19 and LLD (`clang-19`, `lld-link-19`), Python 3,
GNU make and glibc. The GE/O formatter and the `.mpy` compiler are ordinary
Linux host programs; no 9front VM, Ken C `6c`/`6l` or Plan 9 `mk` is used.

```sh
# independent native artifacts
tools/build-gcc.sh --kernel -j 24       # build/gcc/9pc64.elf
tools/build-efi-gcc.sh -j 24            # build/gcc/bootx64.efi
tools/build-mpy-gcc.sh -j 24            # build/gcc/micropython.elf

# native Linux host preparation tools: the real ge/format reamer and mpy-cross
tools/build.sh                          # WHAT=kernel is accepted as an alias
```

The kernel build checks that no `mpelf`/`mpelf_end` payload is present. The
loader build verifies the PE32+ EFI subsystem. These are normal build commands,
not a test framework.

## Published source

The complete canonical source tree is published to `plan2001.com` at the end of
**every** task round, including failed or incomplete rounds. Publication is
never gated on a build or test result; the honest result is recorded as session
metadata instead. Run `make pub` from the repository root, or use the
`EXIT`-trap wrapper `tools/host/session-publish.sh` so a failing command still
triggers publication.

`make pub` creates a deterministic, content-addressed snapshot and transfers it
over SSH to the Ubuntu host. The host-side helper verifies the previous
snapshot, every file hash, the manifest and the archive before activating the
new snapshot with atomic renames. `CURRENT` switches only after verification,
the previous complete snapshot is always retained, and the MCP source service
(`https://plan2001.com/mcp`) exposes the new version through `kernel_status`,
`kernel_history`, `kernel_manifest`, `kernel_read`, `kernel_search` and
`kernel_archive_chunk`. A failed check or transfer leaves the previous snapshot
active and queues a retry. See
[docs/DEVELOPMENT-WORKFLOW.md](docs/DEVELOPMENT-WORKFLOW.md) for the binding
rules.

## Ream, seed and install

Reaming/seed and ESP installation are separate operations.

```sh
# Fresh O image, exact p5 logical size, through the real GE/O reamer.
tools/gcc-ovol-ream.sh --image build/fresh-p5.img 15276

# Read-only identity/size/mount/holder preflight.
tools/gcc-ovol-ream.sh --plan --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/fresh-p5.img

# Authorized partition-only write, fsync, full readback and read-only inspect.
sudo -n tools/gcc-ovol-ream.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/fresh-p5.img
```

The device is never guessed. The tool requires partition 5, a matching live
PARTUUID, the expected p5 size, an unmounted partition and no holders. It writes
only that partition; it never touches the GPT, ESP, other partitions, firmware
or the old demo.

Install or update the ESP files and the single GRUB entry:

```sh
# read-only plan: exact bytes, final-byte check and intended GRUB changes
tools/gcc-o-install.sh --plan --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>

# create the ESP directory if needed, or update an existing one
sudo -n tools/gcc-o-install.sh --bootstrap --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
sudo -n tools/gcc-o-install.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>

# read-only check of installed bytes, plan9.ini and the one GRUB entry
tools/gcc-o-install.sh --verify --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
```

The one visible GRUB entry is **`Plan2001-O`** (id `plan2001-o`); it chainloads
`/EFI/plan2001-gcc/BOOTX64.EFI`. The historical `42_plan2001` and
`43_plan2001_o` scripts are made non-executable, so later `update-grub` runs
cannot bring their entries back; their files and old ESP directories are left
on disk. `44_plan2001_gcc` is the canonical single-entry script. The installer
generates and validates a candidate `grub.cfg` before replacing the active
one, keeps Debian as the default (`set default="0"`), preserves every
non-Plan2001 entry, and never changes `/etc/default/grub`, Debian's own GRUB
scripts or the UEFI boot order.

After reaming p5, install the ESP files again. Reaming does not modify the ESP,
and ESP installation does not format or write p5.

## Retained tools

The short `tools/*` names are compatibility wrappers; implementations live in
`tools/host/`.

```text
tools/build-gcc.sh            GCC kernel build
tools/build-efi-gcc.sh        Clang/LLD EFI loader build
tools/build-mpy-gcc.sh        GCC MicroPython ELF build
tools/build.sh                native Linux formatter + mpy-cross host build
tools/gcc-ovol-ream.sh        ream/seed image and write/verify p5
tools/mkovol                  GE/O image formatter/seeder
tools/oinspect.py             read-only O volume inspection
tools/gefslog                 read-only GE/O analysis library used by oinspect
tools/gcc-o-install.sh        canonical ESP/GRUB/plan9.ini installer
tools/qemu-o-boot.sh          one disposable QEMU/OVMF native GE/O boot
tools/qemu-o-persist.sh       two-boot same-disk GE/O persistence check
```

See [REPOSITORY-LAYOUT](docs/REPOSITORY-LAYOUT.md) for the source tree and
[QEMU-O-TESTING](docs/QEMU-O-TESTING.md) for the two sanctioned QEMU scripts.
The repository intentionally retains **no other test harnesses, broad QEMU
smoke suites, fixture binaries/generators or debug boot selectors**. The
sanctioned scripts build or reuse the canonical artifacts, write only
disposable regular files under `build/`, and are not build or boot
prerequisites. Do not add other test infrastructure to the tree.

## Documentation

- [STATUS](docs/STATUS.md): current verification and the single set of commands.
- [SYSCALL-T8-MULTIPROCESS](docs/SYSCALL-T8-MULTIPROCESS.md): T8 multi-process
  MicroPython result (PASS, 2026-11-06).
- [ROADMAP-T8-T16](docs/ROADMAP-T8-T16.md): T8–T13 recorded PASS within their
  documented gates; T13 is merged (emulated gate only, physical NIC NOT RUN).
  T14 userspace IPv6, T15 provisioned ESP and T16 device work remain PLANNED.
  See [SYSCALL-LEDGER](docs/SYSCALL-LEDGER.md) for the accepted T9 audit.
- [REPOSITORY-LAYOUT](docs/REPOSITORY-LAYOUT.md): the host source layout and
  its distinction from future OID-addressed source objects.
- [O-FORMAT](docs/O-FORMAT.md): implemented GE/O storage and native contract.
- [KERNEL-API-ABI](docs/KERNEL-API-ABI.md): kernel API/ABI boundaries.
- [OBJECT-MODEL](docs/OBJECT-MODEL.md): object/file/Python boundaries.
- [GENERAL-SOFTWARE-SPECIFICATION](docs/GENERAL-SOFTWARE-SPECIFICATION.md):
  practical implementation design.
- [PRODUCTION-BOOT-AND-AUTHORITY](docs/PRODUCTION-BOOT-AND-AUTHORITY.md):
  proposed server/embedded profile and authority boundaries.
- [OLIO-OBJEKTI-MANIFESTI](docs/OLIO-OBJEKTI-MANIFESTI.md): philosophical manifesto.
- [LEGACY-DEPENDENCIES](docs/LEGACY-DEPENDENCIES.md): inherited dependency inventory.
- [GCC-PORT](docs/GCC-PORT.md) and [GCC-HW-BOOT](docs/GCC-HW-BOOT.md): GCC build
  and physical-boot howto.
- [QEMU-O-TESTING](docs/QEMU-O-TESTING.md): the two sanctioned disposable
  QEMU/OVMF native GE/O boot and persistence scripts.

## Planned userspace ESP with external provisioning

The [ESP-SA-BUNDLE specification](docs/ESP-SA-BUNDLE.md) is the normative
network-security target. T14 first demonstrates userspace IPv6/ICMPv6/Neighbor
Discovery over the merged T13 raw Ethernet/Device ABI/ObjectFIFO path. Future
T15 adds one two-host ESP SA pair (IPv6 NH=50), AES-128-GCM-16, preferably
transport mode; protected echo suffices. UDP/TCP are independent objectives.

A versioned bounded GE/O object is supplied to both machines through an
external authenticated/confidential channel. It carries two directional
keys and salts (four secrets, 40 bytes), separate SPIs, IPv6 selectors,
profile, generation, lifetime and policy. Local OIDs may differ and are not
capabilities; local FIFO delivery is not secure interhost transport. No key
exchange or distribution infrastructure is part of the PoC.

IKEv2 is removed entirely from the active and future target: no handshake,
PSKs, UDP/500 service, IKE daemon or deferred migration. The historical
[MINIMAL-IKEV2](docs/MINIMAL-IKEV2.md) report remains NON-TARGET evidence.
Existing kernel ESP/AES-GCM and its old control service have not been removed
by this docs task and do not demonstrate userspace ESP. Native IPv6 startup
and OBJ TCP/UDP opens were disabled in T13.

Require tag verification, anti-replay, fresh unique material for every new
session, no nonce reuse or consumed-bundle reactivation across restart, and
fail closed without plaintext fallback. Manual AES-GCM provisioning is
experimental; RFC 4106's automatic-key-management requirement prevents a
full-compliance or production-security claim. T14/T15 implementation remains
PLANNED. Here ESP means IPsec, not the UEFI EFI System Partition.

## Limits

A file is an O object's finite byte sequence; it needs no directory hierarchy,
pathname or Unix `stat` record. GE/O currently stores versioned opaque byte
values and a flat `init` catalog. The Python persistent graph is still committed
as one `OID_GRAPH` object, not as independently indexed native relations.
General executable-object loading, native IPC/remote-object protocol,
encrypted storage, verified boot and production authority are not implemented.
Inherited `eve`/`hostowner` behavior and the legacy namespace/`Dev` machinery
remain where still linked; see [LEGACY-DEPENDENCIES](docs/LEGACY-DEPENDENCIES.md).

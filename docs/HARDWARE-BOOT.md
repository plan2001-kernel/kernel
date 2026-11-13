# AMD64 UEFI physical-hardware boot: three REPLs and the kernel prompt

This guide installs and verifies the AMD64 Plan2001-O kernel, its UEFI loader,
and an optional boot-process manifest on a real machine. The target result is
three independent MicroPython `-R` REPL sessions plus the kernel-owned
`kernel> ` command prompt on the persistent SystemConsole.

QEMU is not physical-hardware acceptance. At the time of writing, the QEMU
three-REPL and command-prompt regression has been run locally and is wired
into CI; **no physical-hardware run is recorded in this repository**. Do not
close a hardware-acceptance issue without attaching the machine identity,
revision, serial/framebuffer evidence and the actual result, including any
unsupported hardware.

## Scope and limits

- Boot manifest and executables are **not cryptographically authenticated**.
  Installer and `--verify` compare copied bytes for integrity; they are not a
  signature or verified-boot claim.
- `required=true` in the manifest is currently a diagnostic label. It does
  not create dependencies, supervise processes, or restart them.
- The kernel command prompt is read-only: `help`, `status`, `sessions` and
  `dmesg`. There is no shell, arbitrary memory access, device write command or
  userland dependency.
- Physical keyboard input currently uses the PS/2 controller with scan-code
  set 1. Some firmware provides PS/2 emulation for USB keyboards; a USB-only
  machine with no usable emulation is not supported by this console path.
- The installer protects the ESP, GRUB and GE/O partition, but the GE/O volume
  itself is never formatted or written by this procedure.

## Prerequisites on a Debian host

Install the native AMD64 build, EFI, QEMU and image-inspection tools used by
the project. The exact package names can vary by release; at minimum the
project expects GCC 14 (or a working `gcc`), binutils, Clang/LLD, `file`,
`mtools`, `ovmf`, `qemu-system-x86`, Python 3, and the host GE/O tools.

Build the canonical artifacts from the checkout:

```sh
CC=gcc-14 tools/build-gcc.sh --kernel -j 8
tools/build-efi-gcc.sh -j 8
tools/build-mpy-gcc.sh -j 8
tools/build.sh
```

The installer expects `build/gcc/9pc64.elf`, `build/gcc/bootx64.efi` and the
host tools above. Do not install a kernel that still contains the embedded
MicroPython ELF; the installer rejects that condition.

## Identify the GE/O partition (no writes)

Use the actual device names and PARTUUIDs on the target machine:

```sh
lsblk -o NAME,PATH,FSTYPE,PARTUUID,MOUNTPOINTS,SIZE
blkid
```

Validate all of the following before proceeding:

1. The chosen partition is the intended existing GE/O volume.
2. The installer's `--partuuid` resolves to exactly that partition.
3. The partition is the project's supported p5 layout and size; the current
   installer checks this explicitly.
4. The partition is not mounted and has no holders.
5. The MicroPython executable OID `0x4f45584543000003` is present on that
   volume. A read-only inspection can be made without running guest code:

```sh
sudo -n python3 tools/oinspect.py <GE_O_DEVICE> |
  python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["objects"].get("4f45584543000003"))'
```

Substitute the identified partition path. Never format the GE/O volume for
this procedure. If the OID is missing, stop: the manifest would start a
non-executable entry.

## Manifest and loader configuration

The ESP layout used by the installer is:

```text
<ESP>/
  EFI/plan2001-gcc/BOOTX64.EFI
  EFI/plan2001-gcc/9pc64.elf
  EFI/plan2001-gcc/plan9.ini
  processes.conf
```

Example `processes.conf` with three independent REPLs (use the real PARTUUID;
do not keep the placeholder):

```ini
version=1

[process.replA]
source=geo:PARTUUID=<GE_O_PARTUUID>:OID=0x4f45584543000003
owner=replA
args=-R
required=true

[process.replB]
source=geo:PARTUUID=<GE_O_PARTUUID>:OID=0x4f45584543000003
owner=replB
args=-R
required=true

[process.replC]
source=geo:PARTUUID=<GE_O_PARTUUID>:OID=0x4f45584543000003
owner=replC
args=-R
required=true
```

The file must be LF-terminated, contain no CR or NUL bytes, be at most
16383 bytes, start with `version=1`, and contain at least one
`[process.NAME]` entry. The installer checks these loader/kernel constraints
and rejects a manifest whose `geo:` PARTUUID differs from `--partuuid`.

The installer generates this `plan9.ini` (do not add `obootexec` or
`obootrepl` for the manifest demonstration):

```ini
bootfile=/EFI/plan2001-gcc/9pc64.elf
oboot=server
obootvol=PARTUUID=<GE_O_PARTUUID>
bootmanifest=/processes.conf
*ncpu=2
```

Three processes do not require three CPU cores. Use a tested supported
`*ncpu` value for the machine; on a two-CPU configuration the manifest may
pin two processes and let the scheduler place the third.

## Backup before the first install

Back up the ESP and the GRUB configuration outside the ESP:

```sh
sudo -n cp -a /boot/efi /var/backups/plan2001-esp-$(date -u +%Y%m%dT%H%M%SZ)
sudo -n cp -a /boot/grub/grub.cfg /var/backups/grub.cfg.pre-plan2001
```

Record the current UEFI boot entry selected by the firmware. The installer
does not reorder UEFI boot entries. It adds one GRUB menu entry and leaves
Debian and other non-Plan2001 GRUB entries unchanged.

## Read-only plan

Run the plan first. It reads the ESP and GRUB state but writes only its report
under `build/gcc/o-install`:

```sh
tools/gcc-o-install.sh --plan \
  --device <GE_O_DEVICE> \
  --partuuid <GE_O_PARTUUID> \
  --bootmanifest /path/to/processes.conf \
  --ncpu 2
```

Review the printed `plan9.ini`, manifest bytes, hashes, GRUB entry and
preflight report before any write.

## Install or bootstrap, then verify

Use `--bootstrap` only when the ESP directory does not exist yet; use
`--install` when it does. Both paths stage the exact bytes, back up every ESP
file they replace, install the ESP artifacts first, and only then update GRUB.
On any failure the installer restores the backed-up ESP files and the previous
GRUB files.

```sh
sudo -n tools/gcc-o-install.sh --install \
  --device <GE_O_DEVICE> \
  --partuuid <GE_O_PARTUUID> \
  --bootmanifest /path/to/processes.conf \
  --ncpu 2

tools/gcc-o-install.sh --verify \
  --device <GE_O_DEVICE> \
  --partuuid <GE_O_PARTUUID> \
  --bootmanifest /path/to/processes.conf \
  --ncpu 2
```

`--verify` must receive the same selector flags as the original install,
including `--bootmanifest`; it compares the installed `plan9.ini` and
`processes.conf` byte-for-byte with the intended staged bytes and checks the
single canonical GRUB entry. Avoid manual post-install edits of these files,
because they would fail `--verify`.

## Physical acceptance procedure

1. Select the new `Plan2001-O (<kernel build stamp>)` GRUB entry (or the
   corresponding UEFI chainload entry) and boot it.
2. Press `Ctrl+Alt+F1`. The kernel-owned SystemConsole must show
   `kernel> `. Run `help`, `status` and `sessions`. `sessions` must list three
   distinct live PIDs for `replA`, `replB` and `replC`.
3. `Ctrl+Alt+F2`: REPL A accepts `print(111)` and prints `111`.
4. `Ctrl+Alt+F3`: REPL B accepts `print(222)` and prints `222`, with no A
   output.
5. `Ctrl+Alt+F4`: REPL C accepts `print(333)` and prints `333`, with no A/B
   output.
6. Switch repeatedly between F2/F3/F4. Each REPL keeps its own history and
   input; switching must not merge output or change process lifetimes.
7. In REPL B press `Ctrl+D`. Verify A and C still accept commands. Return to
   F1; `sessions` must show B as `exited`, and `dmesg` must contain the B exit
   diagnostic. The `kernel> ` prompt must remain interactive.
8. Verify that unavailable or unsupported slots and an absent keyboard do not
   panic the kernel. Record whether keyboard input used PS/2, firmware PS/2
   emulation, or a newly added USB path.
9. Reboot into the original OS using the original boot entry and confirm that
   recovery works.

Record the machine identity, firmware version, exact Git revision, kernel
build stamp, serial/framebuffer evidence, any failure modes and the final
result. A QEMU PASS alone does not satisfy physical acceptance.

## Rollback and recovery

Automatic rollback runs only for failures during `--install`/`--bootstrap`.
If the machine fails after a completed install, restore the pre-install state
manually:

```sh
sudo -n cp -a /var/backups/grub.cfg.pre-plan2001 /boot/grub/grub.cfg
```

Restore the saved ESP directory or the individual `EFI/plan2001-gcc/*` and
`processes.conf` files from the ESP backup, then select the original UEFI or
GRUB boot entry. The installer's own backups are under
`build/gcc/o-install/esp-backup` with a restore list in
`build/gcc/o-install/esp-restore.txt`.

## Verification status

- QEMU SystemConsole regressions: `tools/host/bootmanifest/run-console-sessions.sh`
  (two REPLs) and `tools/host/bootmanifest/run-console-prompt.sh` (three REPLs
  plus the kernel command prompt). The second is also wired into
  `.github/workflows/kernel-console.yml`.
- Physical AMD64 UEFI hardware: **NOT RUN** for this revision. Record the
  machine identity, exact revision, boot/diagnostic logs, failure modes and
  final result on the acceptance issue or PR. A QEMU PASS does not close the
  physical-hardware requirement.

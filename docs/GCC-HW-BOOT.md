# Physical GCC O-server boot — operator runbook

This is the real-hardware entry for the Plan 2001-kernel GCC kernel, Clang/LLD
loader and GCC MicroPython object. It is a normal native GE/O boot, not a
probe or diagnostic profile.

> **2026-11-02 kernel-only candidate and MPY REPL switch:** kernel
> `06549dd0...`, stamp `2026-11-02 08:57 UTC`; loader `b8cfa2c1...`. The
> kernel-only `plan9.ini` (`a8857dde...`, 113 bytes, no
> `obootexec`/`obootrepl`) was physically booted twice on `kuha` with
> `PLAN2001 KERNEL READY`, read-only p5 open and normal Debian recovery.
> After that PASS the operator requested the normal MicroPython REPL; the
> current ESP `plan9.ini` is the explicit compatibility profile
> (`f911d327...`, 154 bytes, `obootexec=0x4f45584543000003`,
> `obootrepl=1`). That MPY path passed QEMU with the exact p5 object; its
> physical boot is not yet observed. See
> `build/hw-kuha-20261102T083935Z/KUHA-HW-BOOT-REPORT.md`.

## Target and entry

- Host: Dell Precision 7520 (`kuha`).
- ESP: `/boot/efi` (vfat), FAT UUID `ADC3-AA1E`.
- The only Plan2001 GRUB entry: title **`Plan 2001-kernel (<kernel UTC build
  stamp>)`**, currently `Plan 2001-kernel (2026-10-29 15:35 UTC)`, stable id
  `plan2001-o`, script `/etc/grub.d/44_plan2001_gcc`. The installer derives
  the title stamp from the kernel ELF, not from the installation clock.
- Chainloader: `/EFI/plan2001-gcc/BOOTX64.EFI`.
- ESP directory: `/boot/efi/EFI/plan2001-gcc/` containing `9pc64.elf`,
  `BOOTX64.EFI` and `plan9.ini`.
- O volume: partition 5, opened by its live PARTUUID. Verify the live PARTUUID
  and size; do not reuse a remembered value blindly.
- Debian remains the GRUB default (`set default="0"`).

The historical `42_plan2001` and `43_plan2001_o` GRUB scripts are kept on
disk but made non-executable, so later `update-grub`/kernel updates cannot add
their entries back. Their old ESP directories (`EFI/plan2001/`,
`EFI/plan2001-o/`) are not modified and are not menu recovery entries.

## Installed state

The ESP currently contains the matching N11.HW4 (main `fd2441c0`) GCC
kernel, Clang/LLD loader and LF-terminated `plan9.ini`, built (canonical
tools, `-j 8`) and installed/verified 2026-10-29. N11.HW3 (`33fd190`) is
superseded; kernel-relevant source is unchanged between the two commits (only
documentation and `tools/opencode` changed), so N11.HW4 is a rebuild and
reinstall rather than a kernel change. Kernel, EFI loader and MicroPython
builds all passed; no QEMU boot was needed or run.

```text
9pc64.elf     955,232 bytes  85f90c23bd1c4c74ed8664591e4cd0daac395be331f7619b5d19a9536948fc74
BOOTX64.EFI    22,016 bytes  6e6aa89d41bf727b2f3d93a309cd852365bc9f8fc88c0e388df99749784ade34
plan9.ini         125 bytes  6b16494b3b0d90b7b195fa235905225b329085b44f1b3ce84f377c9c7bdca9fa
```

The kernel ELF (`build/gcc/9pc64.elf`) is ELF64 x86-64 `ET_EXEC`, statically
linked, and carries exactly one build stamp, `2026-10-29 15:35 UTC`. The
loader (`build/gcc/bootx64.efi`) is a PE32+ EFI application, and
`build/gcc/micropython.elf` is byte-identical to the ELF stored on p5. The
single GRUB entry is `Plan2001-O (2026-10-29 15:35 UTC)` (stable id
`plan2001-o`). `plan9.ini` ends in `0a` and names the live p5 PARTUUID
`a91d7405-f2d0-4269-82b4-f528ac65b185`; all three ESP files were read back
byte-identical to the build outputs.

p5 (`/dev/nvme0n1p5`, 16,018,046,976 bytes, unmounted, no holders) was
**reused, not reamed**: read-only `oinspect` confirmed valid primary and
backup O superblocks (O v1), Space `N = 17`, OID `0x4f45584543000003`
version 1 holding 458,648 bytes with SHA-256
`0c1d01af9f31d20f200ecd3cc6309cdf910efe1b7638648782ba13987ae54bb8` and
runtime `mp-1.27.0-mpy6.3`. Persisted REPL state was preserved.

`--plan`, `--install` and `--verify` all completed successfully
(`--verify` = PASS), with evidence under `build/n11-hw4/` and
`build/gcc/o-install/` (excluded from source publication).

Status: **BUILD PASS** and **ESP/GRUB INSTALL+VERIFY PASS**.
**PHYSICAL BOOT NOT YET OBSERVED.**

History: N11.HW3 replaced the old untimestamped N11.HW2 ESP state
(`9pc64.elf` `b1a4c769...`, `BOOTX64.EFI` `139e218d...`, title
`Plan2001-O`). The historical N11.HW1 instalment was the N11.6
kernel/loader (`9pc64.elf` 992,120 bytes `8157e6f5...`, `BOOTX64.EFI`
`254e97eb...`); it is no longer the installed ESP state.

The operator selects the single `Plan2001-O (2026-10-29 15:35 UTC)` entry
and checks the kernel build line, GE/O `OEXECUTE`, REPL and keyboard; this
document does not claim HW BOOT PASS until that is owner-observed.

The loader hash is for the currently installed PE image; the COFF timestamp
makes each new link build-specific. The last byte of `plan9.ini` is `0a`.

`/boot/grub/grub.cfg` contains exactly one user-visible Plan2001 line:

```text
menuentry 'Plan 2001-kernel (2026-10-29 15:35 UTC)' --id 'plan2001-o' {
	...
	chainloader /EFI/plan2001-gcc/BOOTX64.EFI
}
```

All non-Plan2001 menu entries are unchanged, including the Debian entries and
the advanced-options submenu (the installer compared them byte-for-byte). N10.1
did not write p5; N11.HW1 historically reamed p5 from
`build/hw-n11-6-p5.img` (SHA-256
`6020bd4c96d2d0d160d38215e9ea523144cb1665f41d2aaf059ed5eb6a94962c`) with the
N11.6 MicroPython ELF. **N11.HW2** then reamed the verified physical p5 again
from `build/hw-n11-8-5-p5.img` (SHA-256
`8a632f01dc5d04951fcbd18da6bc5310ee866877ae35bcdfb4587b5fdcab6900`), so it
then held the freshly built MicroPython ELF (458,648 bytes, SHA-256
`0c1d01af...`) at OID `0x4f45584543000003`, with the matching
`mp-1.27.0-mpy6.3` seed. **N11.HW3** and **N11.HW4** did not ream p5: that
same compatible content was verified read-only and reused each time.

The **pre-cleanup** binary was booted physically and reached the interactive
MicroPython REPL, with `dir()`, `import space`, `space.info()` and GE/O commits.
The **N10.1 GCC kernel / Clang UEFI loader** was then booted physically from
the single `Plan2001-O` GRUB entry and reached the interactive MicroPython
REPL (owner-observed). The historical **N11.6/N11.HW1** instalment was
reported by the owner as three successful physical boots with a persistent REPL
variable. The **N11.HW2** combination (main `f116b82`) was installed and
verified but was never observed booting. The **N11.HW3** timestamped
combination was superseded on the ESP by N11.HW4; no agent-verified N11.HW3
hardware boot is recorded. **N11.HW4 is not yet physically booted**.
Cross-reboot persistence of a newly assigned REPL variable remains **not
documented**.

## Build and install

Build the artifacts first:

```sh
tools/build-gcc.sh --kernel -j 24
tools/build-efi-gcc.sh -j 24
tools/build-mpy-gcc.sh -j 24
# host formatter + mpy-cross when the O image is to be reamed/seeded:
WHAT=kernel tools/build.sh
```

For a full refresh, ream p5 with the current MicroPython first, verify its
readback, and only then install the ESP files, so the boot never pairs a new
kernel with an old interpreter. N11.HW4 did **not** do this: it reused the
compatible p5 content.

```sh
tools/gcc-ovol-ream.sh --image build/<o-image> 15276
sudo -n tools/gcc-ovol-ream.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid> --image build/<o-image>
```

```sh
# read-only plan: exact bytes, final-byte check and intended GRUB operations
tools/gcc-o-install.sh --plan --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>

# create the ESP directory if needed, or update an existing one
sudo -n tools/gcc-o-install.sh --bootstrap --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
sudo -n tools/gcc-o-install.sh --install --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>

# read-only check: ESP hashes, plan9.ini bytes, one GRUB entry, 42/43 inactive
tools/gcc-o-install.sh --verify --device /dev/nvme0n1p5 \
  --partuuid <live-p5-partuuid>
```

The installer:

1. installs and reads back `9pc64.elf`, `BOOTX64.EFI` and `plan9.ini`, and
   re-reads the installed kernel to require its SHA-256 and embedded build
   stamp to match the intended ELF before any GRUB change;
2. makes only the known legacy Plan2001 scripts (`42_plan2001`,
   `43_plan2001_o`) non-executable;
3. writes the canonical `44_plan2001_gcc` with the single
   `Plan2001-O (<kernel UTC build stamp>)` entry;
4. generates a **candidate** `grub.cfg` with `grub-mkconfig`, validates it
   (`grub-script-check`, one Plan2001 entry, right title/id/chainloader,
   `set default="0"`, non-Plan2001 entries unchanged), and only then replaces
   the active configuration;
5. rolls back only the GRUB files it changed if the candidate or post-check
   fails.

`--install` requires the GCC ESP directory to exist; `--bootstrap` may create
it. A second install is idempotent and does not add entries. The installer
requires partition 5, the matching live PARTUUID, the expected size
(`16,018,046,976` bytes), an unmounted partition and no holders. It does not
format or write p5, does not destroy the old ESP directories, does not change
`/etc/default/grub`, Debian's own GRUB scripts or the UEFI boot order, and
does not reboot the machine.

The installer records pre/post GRUB scripts, modes, hashes and logs under
`build/gcc/o-install/`.

## `plan9.ini` contract

The installed file is exactly:

```ini
bootfile=/EFI/plan2001-gcc/9pc64.elf
oboot=server
obootvol=PARTUUID=<live-p5-partuuid>
obootrepl=1
*ncpu=1
```

The final byte must be LF (`0x0a`). The installer verifies the staged and
installed bytes with an exact-file comparison, SHA-256, size and a final-byte
check. The EFI loader also enforces the rule: a nonempty final line without LF
prints `plan9.ini: missing final newline` and stops instead of silently
dropping the setting.

The canonical default has no selectors and `*ncpu=1`. Optional selectors can
be added without changing the default bytes:

```ini
obootexec=0x0
obootcpu=2
*ncpu=4
```

- `obootexec=0x0` (or an absent line) resolves to the canonical MicroPython
  OID `0x4f45584543000003` in the `init` Space; `0x0` is a selector alias, not
  a stored OID. A nonzero value must name a real published executable; a bad,
  missing or incompatible object stops the boot with no MicroPython fallback.
  Canonical MicroPython uses `micropython -O <volume> -S init -I|-N`; other
  executables get `oexec <volume> init`.
- `obootcpu` is an exact kernel `Mach.machno` index, validated against
  `*ncpu`, actual online state and per-CPU scheduler readiness. It pins the
  first user process (surviving `OEXECUTE`) but does not reserve the CPU from
  interrupts or other processes. Invalid or offline selectors are errors; they
  are never clamped or moved to CPU 0.
- `*ncpu` caps the enabled logical CPUs. The installer accepts `--ncpu`
  1..128 and `--obootcpu` 0..127 and rejects `--obootcpu >= --ncpu` before
  touching the ESP. The kernel's `MAXMACH` is 128; a larger `*ncpu` is clamped
  there, which is why preflight does not allow it.
- The boot log prints `n13: firstuser pid=... cpu=N requested=M`, showing the
  actual CPU of the first userspace dispatch. `OSPAWN` uses the same
  scheduler-ready validation for a child's exact CPU.

See the [README](../README.md) and
[N13-INTEGRATION](N13-INTEGRATION.md) for the full placement and spawn
contracts.

## Kernel build identity

Each kernel build generates `build/gcc/gen/kbuildid.h` exactly once, before
any unit is compiled. The header defines the concatenated identity:

```text
PLAN2001-O-KERNEL-BUILD:YYYY-MM-DD HH:MM UTC
```

The magic is `PLAN2001-O-KERNEL-BUILD:` and the timestamp is captured in UTC
from `datetime.now(timezone.utc)`, or from `SOURCE_DATE_EPOCH` when that
environment variable is intentionally supplied (parsed as UTC). The full
string is linked into the kernel's read-only data. `ouser.c` holds it in one
`static const char` array and, immediately before the final userspace
transition, prints:

```text
Plan 2001-kernel kernel build: YYYY-MM-DD HH:MM UTC
```

This is a build-time constant only: there is no syscall, RTC read or runtime
clock involved. Because every kernel build recompiles the units and relinks,
the ELF carries the new stamp; the only way to repeat a stamp is to pin
`SOURCE_DATE_EPOCH`.

The installer does not trust a clock or a sidecar. It extracts the identity
from the actual kernel ELF, requires exactly one match, validates the calendar
value with GNU `date -u`, and derives the GRUB title from it. `--install` and
`--bootstrap` re-read the installed ESP kernel and require the same stamp and
hash before changing GRUB; `--verify` performs the same stamp/hash comparison
and requires the installed `grub.cfg` and canonical 44 script to contain
`menuentry 'Plan2001-O (<stamp>)' --id 'plan2001-o'`.

## Operator boot

1. Reboot and select the single `Plan2001-O (2026-10-29 15:35 UTC)` entry at
   GRUB. The installed state is the N11.HW4 timestamped kernel; the kernel
   prints the same `2026-10-29 15:35 UTC` stamp. Do not change the firmware
   boot order; Debian remains the default if no key is pressed.
2. Expect loader text, the O-volume open, an `OEXECUTE` of OID
   `0x4f45584543000003`, `init.run()`, a GE/O commit and `REPL READY`,
   followed by the `>>>` prompt on the framebuffer/keyboard.
3. Run `dir()`, `import space` and `space.info()` to confirm the same behavior
   observed on the pre-cleanup binary.
4. Record the visible result. Do not label the N11.HW4 build PASS until it is
   actually observed.

Cross-reboot persistence of a newly assigned REPL variable is **not yet
documented**. If testing it, assign the variable, commit, power-cycle without
re-reaming, and check the value on the next boot.

## Recovery

At GRUB, select Debian (the default) to return to Linux. The old Plan2001 demo
is deliberately not a GRUB recovery entry; its ESP directory remains on disk
but is unused. The installer records the previous GRUB scripts, modes and
configuration under `build/gcc/o-install/`.

## Guardrails

- Never ream or write a whole disk. Only the verified partition 5 is a target;
  N10.1 did not write it; N11.HW3 and N11.HW4 only read it; the separately
  authorized N11.HW1 and N11.HW2 preparations reamed only p5 through the
  hardened `--install` path.
- Never change the Debian default, Debian GRUB scripts, firmware BootOrder or
  other partitions as part of an install.
- Reaming p5 and installing the ESP/GRUB entry are separate actions.
- Keep exactly one Plan2001 entry: `Plan2001-O (<kernel UTC build stamp>)` /
  `plan2001-o`.
- The repository retains no diagnostic runbooks, checkpoint tables or test
  harnesses; use this normal boot and the installer evidence.

# Experimental AMD64 ash hardware boot

This builds a disposable GPT disk image with an EFI system partition and GE/O
partition. A trusted F5 supervisor starts genuine BusyBox 1.36.1 ash linked
statically to Newlib and the Plan2001 userspace adapters. The builder writes
regular files only; it never selects or overwrites a physical disk.

Prerequisites: build the native AMD64 kernel, EFI loader, MicroPython/startup,
formatter and mpy-cross in the kernel tree. Build Newlib using
`tools/ci/build_newlib_libc.sh`. The kernel needs the normal alarm-service
startup fix (kernel PR 64). Source inputs stay outside userland. Then run:

```sh
bash tools/ci/build_hw_boot.sh KERNEL_SOURCE BUSYBOX_SOURCE NEWLIB_REPORTS FRESH_OUTPUT
```

The builder refuses an existing output directory and publishes
`plan2001-ash-amd64.img`, its gzip copy, source archives and `SHA256SUMS` only after the interactive QEMU test
passes on virtio and emulated SATA/AHCI. Physical hardware remains untested.
BusyBox is GPLv2: the bundle includes its license and exact source
commit; use `config/sources.json` and the recorded config to obtain/rebuild
corresponding source. See `docs/BUILD.md` for dependency preparation.

Use x86-64 UEFI with Secure Boot disabled and at least two logical CPUs.
The firmware must load EFI/BOOT/BOOTX64.EFI. The kernel must also recognize
the device holding the GE/O partition. Successful firmware USB boot alone
is not proof that kernel USB mass storage works. Use a dedicated test disk
on a supported controller. Emulated AHCI is tested; a physical controller
and USB mass storage are separate hardware checks.

Writing the image overwrites the selected disk and its partition table.
After independently identifying a disposable test disk, an example is:

```sh
sudo dd if=FRESH_OUTPUT/plan2001-ash-amd64.img of=/dev/disk/by-id/REPLACE_WITH_TEST_DISK bs=4M conv=fsync status=progress
```

Boot the disk, then press Ctrl+Alt+F5. Look for PLAN2001-ASH-READY and `ash$`.
Try `echo hello`, `pwd`, `cd /tmp`, `echo data > /tmp/a`,
`read x < /tmp/a; echo $x`, and
`echo piped | while read x; do echo $x; done`.
Ctrl+Alt+F1 returns to the separate kernel console. `exit` ends the F5 session.

Input currently has no terminal echo or line editor; type commands and press Enter.

This is an ash/builtin test profile, not a full Unix distribution. External
coreutils, networking, POSIX PTYs/job control, Unix identity/permissions and
persistent file-service inheritance are not supplied. `/tmp` and the projected
filesystem are volatile and reset on reboot. Do not interpret the GE/O-backed
executable image as persistent Unix file storage. An actual hardware run is
NOT RUN by the automated builder; retain serial output and record the exact
machine, firmware and storage controller when reporting hardware results.

# Native upstream BusyBox multicall: initial profile

This is the first implementation slice of [userland #17](https://github.com/plan2001-kernel/userland/issues/17).
It introduces a genuine upstream dispatcher, an immutable virtual executable
projection and reproducible full-release name inventory. **It does not complete
#17 or establish that every feasible upstream applet has been enabled.**

The profile includes 30 names: `[`, `ash`, `awk`, `basename`, `cat`, `cmp`,
`cut`, `dirname`, `echo`, `egrep`, `false`, `fgrep`, `grep`, `head`, `ls`,
`mkdir`, `mv`, `printf`, `pwd`, `rm`, `sed`, `sh`, `sort`, `tail`, `tee`,
`test`, `tr`, `true`, `uniq`, `wc`.
Upstream features are selectively configured; this is not the all-features Linux
configuration. The older ash-only and three-applet artifacts remain separate.

## Build and run

Use the dependencies in BUILD.md and HARDWARE-BOOT.md, including GCC 14, QEMU,
OVMF, mtools, Python/Pillow and the Newlib build dependencies. Paths below are
fresh disposable sibling directories outside the userland source tree.

```sh
python3 tools/ci/fetch_sources.py --lock config/sources.json --dest ../deps --reports ../reports
bash tools/ci/build_newlib_libc.sh ../newlib ../reports
(cd ../deps/kernel && CC=gcc-14 tools/build-gcc.sh --kernel -j 4)
(cd ../deps/kernel && CC=gcc-14 tools/build-efi-gcc.sh -j 4)
(cd ../deps/kernel && CC=gcc-14 tools/build-mpy-gcc.sh -j 4)
(cd ../deps/kernel && tools/build.sh)
bash tools/ci/build_multicall_image.sh ../deps/kernel ../deps/busybox ../newlib ../reports ../multicall-image
(cd ../multicall-image && sha256sum -c SHA256SUMS)
```

Every CI run resolves current kernel main. The resolved SHA, immutable BusyBox
1.36.1 SHA, Newlib 4.6.0 SHA, config hash and target ELF hash are recorded. Native
objects use `-nostdinc`, target Newlib headers, `-nostdlib`, explicit native startup
and syscall stubs, and a static target linker script. No Linux libc or CRT is
linked. Newlib's existing BSD regex sources are compiled explicitly because its
generic unknown-elf libc build omits them. Their original copyright notices are
preserved in the bundled Newlib sources. Host programs only generate upstream
applet tables and usage data; none are passed off as guest executables.

For just the native driver after building Newlib and the kernel:

```sh
python3 tools/ci/newlib_full_link.py --kernel ../deps/kernel --reports ../reports
python3 tools/ci/build_busybox_multicall.py --kernel ../deps/kernel --busybox ../deps/busybox --newlib-source ../newlib --reports ../reports
bash tools/ci/guest_busybox_multicall.sh ../deps/kernel ../reports/busybox-multicall ../fresh-guest-reports
```

## Interactive console

Use Finnish `keymap=fi` and QEMU `-k fi` when testing with a Finnish keyboard.
Win+F1 selects SystemConsole and Win+F5 selects the Unix shell. This shortcut
requires kernel PR #66 for Win console shortcuts and stacked kernel PR #67
(tested commit `9ac37be`) for native 1024-Handle inheritance. This integration gate
uses that kernel branch; moving-main compatibility is not yet established.
The console acceptance test uses the new shortcut, so that PR must be integrated
before the moving-main interactive CI gate can pass.

Ctrl+D on an empty input line produces one EOF read, allowing `cat` to finish
and ash to read the next command. On a nonempty line it flushes the pending text
without adding a newline; press Ctrl+D again on the now-empty line to finish.
This is userland canonical input handling, not complete termios or job control.

## Executable projection and authorization

One canonical `/bin/busybox` registration holds the delegated executable volume
Handle and GE/O selector. The generated immutable table maps only enabled upstream
install paths to that registration. It consumes no per-applet PVD, UXFS or native
ObjectSpace nodes and no per-applet executable registrations. An alias name alone
has no effect without the registration. Native SPAWN still validates the Handle,
its rights and the selected executable. No OID-based opening is added.

The supervisor already delegates an execute-only volume and passes the selected
child OID as an argument. The multicall entry validates that explicit launch grant
and binds its own selected object to `/bin/busybox`, then runs upstream interactive
ash. Subsequent fork/exec inherits the registry through the existing native context.
The path resolver uses the current PVD cwd; it preserves the caller's argv[0].
Other native POSIX applications need the same generated `aliases.o` linked into
their runtime to expose this catalog; the legacy ash-only profile has no catalog.
Actual ash builtins remain; basename and absolute commands exercise external exec.

Aliases are virtual **regular execute-only entries**, not POSIX symlinks.
`stat` and `lstat` agree and share the target identity; enumeration dynamically
allocates a bounded snapshot large enough for the generated table. `access(X_OK)`
selects a registered executable; the native loader remains the execution check.
Reading, replacing, removing or renaming a projected executable fails with EACCES.
Mutable hard/symlink installation is unsupported: `busybox --install` returns an
error, even though upstream's installer feature is needed for `--list-full`.
This avoids upstream's success exit code after failed link creation.

`busybox --list` and `--list-full` use upstream tables. The builder independently
compares those names with preprocessed source declarations; the driver compares
both outputs byte-for-byte with the inventory. The canonical busybox entry is in
addition to the applet list, as with a normal BusyBox installation.

## File and text tools

`head`, `tail`, `wc`, `tee`, `cut`, `tr`, `sort`, `uniq`, `cmp`, `sed` and
`awk` are upstream applets using the native fd/file/stream layer. Awk math
uses target Newlib libm. `system`, `popen` and `pclose` use native fork, pipes,
exec and exact-child wait; system exit status is preserved. No host shell or
vfork is used. Source builds avoid the BusyBox shell's private `math.h`
shadowing target Newlib's standard math header.

`mkdir` creates individual directories; `rm -r` now removes empty directories
from both UXFS and PVD and reuses freed PVD slots. Fixed bootstrap view
anchors are protected with EBUSY so saved manifests remain restorable. `mv` supports regular-file
renames and updates open source descriptions. Overwriting an open destination
or unlinking an open file returns EBUSY until detached inode lifetime exists.

The versioned `config/busybox-native-limitations.json` supplies per-applet
missing interfaces and restrictions. In particular `mkdir -p`/`-m` and `sed -i`
are not implemented by this profile. They fail with errors; the in-place sed
failure leaves the original content intact. Stream editing and redirection
remain usable. Directory moves and Unix permission/ownership preservation are
also unsupported.

The volatile UXFS service supports 2048 metadata nodes and a shared 8 MiB
content arena. Files grow into the available arena instead of reserving a
fixed per-file buffer. Offset I/O and 4 KiB RPC transfers avoid whole-file
stack buffers. Deleted/truncated content is compacted and reused. The
pointer-free shared snapshot fits the native 16 MiB region ceiling. Manifest
v2 uses the expanded inventory; the original 64-node v1 remains readable. SPAWN and RFORKEX
share the native 1024-Handle capacity, based on Linux's initial RLIMIT_NOFILE
soft limit. The C inheritance plan has the same capacity. External pipelines
preserve stdin, stdout, stderr and other open descriptors without requiring
input redirection or merging stderr. The console regression runs
`cat koe.txt | awk '{print $0}'` and a three-external-command pipeline. A
separate native test leaves fourteen pipe descriptors open across fork/exec,
exercising more than sixteen native grants. The Unix fd and child tracking tables each have 1024 entries. Their actual
use remains subject to native Handle and memory availability. The QEMU
regression writes a 256 KiB file, creates 100 directories and 100 files,
and reads/counts them through real fork/exec and a shell pipeline.

## Evidence and limits

The native driver runs each enabled applet through real fork/exec/wait, captures
its pipe output and checks its exit code. It refuses PASS if any enabled name lacks
a command test. It checks unknown applets, invalid regex, missing files, alias
metadata and write denial, unregistered name denial, directory enumeration, PATH,
relative exec, file creation, cat, grep and a pipeline in upstream ash. More than
four aliases use one registration. Additional guest tests derive non-executable
and revoked volume grants, require native exec rejection, and verify that the
parent's independent executable grant remains usable. The driver stays alive after the completion
marker because voluntarily exiting the kernel's boot process causes a panic.

The image builder requires interactive PS/2/VGA F5 tests on virtio and separately
attempts AHCI compatibility. `verification.json` records AHCI PASS or FAIL; an AHCI
failure is not a virtio or hardware pass. AHCI acceptance requires a kernel whose
SATA retry loop yields to its asynchronous recovery worker. Record the exact
kernel SHA for each integration run. Static checks require AMD64 ET_EXEC, native startup, exactly two PT_LOADs,
no PT_INTERP/PT_DYNAMIC and no unresolved symbols. Reports distinguish PASS, FAIL
and NOT RUN. Physical hardware and ARM64 remain NOT RUN.

The versioned `config/busybox-applets.json` and full
`config/busybox-multicall.config` must match the regenerated source/config table.
`applets.json` accounts for 434 upstream names, including alternative main
functions and disabled source conditions. Source enumeration follows upstream's
`gen_build_files.sh`, including embedded script names. Disabled names currently
say **pending native audit**. They must not be represented as impossible, tested,
or implemented. A standalone config inventory likewise does not claim a build
has happened. The guest result upgrades only enabled names after all driver tests
pass, with the corresponding log path as evidence.

Remaining work for #17 includes applet-specific dependency/semantics audits for
disabled names, further userspace adapters and applet families (including cp, touch,
find, xargs, tar and broader file metadata support), a guest test with more than
64 enabled aliases, broader
terminal/network/device facilities. Native process/fd/grant, argument and file
size bounds still apply; compilation is never evidence of working services.

Distributable image bundles contain BusyBox, Newlib, kernel and userland sources,
GPL license, complete config, manifest, build instructions and checksums.

## Framed-object integration

The integration build provisions MicroPython ELF, MPY modules, the native
console supervisor and the genuine BusyBox ELF as raw-first framed GE/O
objects. Native ELF/MPY loaders require a valid footer, expose only its raw
executable span and reject unframed/corrupt input explicitly. The host
`object_format.py inspect` command and native `space.ometa(oid)` read metadata;
`space.oraw(oid)` reads only raw content under the same volume capability.

UXFS v3 publishes raw file bytes, profile CBOR and footer in one version.
Its framed manifest includes directory metadata and is committed after
successor files are staged. Object-owned names and known creation instants
survive namespace rename. Different OIDs may share the same object name.
Opaque and unknown metadata is preserved; unsupported chmod/chown/xattr/ACL
mutation remains an error. Older unframed manifests are rejected explicitly,
so provision a fresh image rather than silently reformat an existing disk.

The hardware bundle contains a complete UEFI/GPT boot image, a raw `p5.img`
GE/O volume, separately inspectable `micropython.p2o` and `busybox.p2o`, source
archives, provenance and SHA256SUMS. Physical hardware is a user-run check,
not inferred from QEMU. The multicall shell currently uses volatile shared
files; the separate persistent-service image exercises durable UXFS through
its restricted RPC clients. Both use the same framed object contract.

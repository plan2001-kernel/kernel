# Native upstream BusyBox multicall: initial profile

This is the first implementation slice of [userland #17](https://github.com/plan2001-kernel/userland/issues/17).
It introduces a genuine upstream dispatcher, an immutable virtual executable
projection and reproducible full-release name inventory. **It does not complete
#17 or establish that every feasible upstream applet has been enabled.**

The initial profile includes 16 names: `[`, `ash`, `basename`, `cat`, `dirname`,
`echo`, `egrep`, `false`, `fgrep`, `grep`, `ls`, `printf`, `pwd`, `sh`, `test`, `true`.
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
failure is not a virtio or hardware pass. The local kernel main tested here reports
`sdE0: bad disk` on AHCI before shell startup. No kernel changes are included. Static checks require AMD64 ET_EXEC, native startup, exactly two PT_LOADs,
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
disabled names, further userspace adapters and applet families (including sed,
awk, find, sort, xargs, tar and file mutation tools), a guest test with more than
64 enabled aliases, broader
terminal/network/device facilities. Native process/fd/grant, argument and file
size bounds still apply; compilation is never evidence of working services.

Distributable image bundles contain BusyBox, Newlib, kernel and userland sources,
GPL license, complete config, manifest, build instructions and checksums.

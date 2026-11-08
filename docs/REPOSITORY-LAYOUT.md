# Repository layout

This tree now describes Plan 2001-kernel's own parts, not a complete 9front
distribution. The host directory names are a development and export view only:
they are **not** a GE/O namespace, and source paths are not object identity.
Persistent identity remains `(SpaceID, OID)` in a GE/O volume.

```text
boot/efi/          Clang/LLD UEFI loader (PE32+ entry, plan9.ini parsing)
kernel/core/       processes, VM, syscalls, core services, shared early boot
kernel/amd64/      amd64 port, pc64 configuration data, GNU asm, GCC startup
kernel/arm64/gcc/  experimental AArch64 entry, vectors, boot memory and boot-only main
kernel/drivers/    USB and UART drivers; amd64-specific drivers stay in kernel/amd64/
kernel/net/        native IPv6-only network protocols
kernel/compat/     GCC/Plan 9 language compatibility header
ge/storage/        native kernel GE/O engine (B-tree, arena, log, obj, Space)
ge/format/         native O v1 reamer/seed writer sources for the host formatter
micropython/       MicroPython 1.27.0 sources and the upstream mpy-cross front end
runtime/libc/      live Plan 9 C subset for the GCC kernel, MicroPython and formatter
runtime/abi/       syscall numbers and the syscall ABI header
runtime/{ip,crypto,mp,aml,fis}/  only members used by the kernel, MicroPython or formatter
include/           shared headers for the amd64 kernel, MicroPython and formatter
tools/host/        host-side build, image, install and formatter implementations
tools/host/format-o/  Linux entry point for ge/format's real reamer
tools/host/ge-compat/ small Plan 9 libc/process/file shim for the host formatter
micropython/mpy-cross/  upstream v1.27.0 .mpy compiler front end
```

## Build ownership

`tools/host/build-kernel.py` compiles the explicit `KERNEL_SOURCES`
manifest and links `build/gcc/9pc64.elf`. There is no mkfile parsing and no
second compile-unit list. `tools/host/build-mpy-gcc.py` has explicit
MicroPython and `runtime/libc` member manifests and writes
`build/gcc/micropython.elf`; `tools/host/build-format-o.sh` has an explicit
fmt manifest. `tools/host/build-efi-gcc.sh` builds `build/gcc/bootx64.efi`
with Clang/LLD. The experimental ARM64 tree has its own explicit manifests:
`tools/host/build-arm64.py` links `build/arm64/9karm64.elf` and
`tools/host/build-efi-arm64.sh` builds `build/arm64/bootaa64.efi`.
The thin `tools/*` entry points keep the previously documented command names.
`kernel/arm64/gcc/` is not part of the amd64 kernel manifest; see
[ARCHITECTURE-SUPPORT](ARCHITECTURE-SUPPORT.md) for its scope.

## N11.3 canonical kernel boundary

`kernel/core/legacyexec.c`, `kernel/core/legacypath.c` and
`kernel/core/rebootcmd.c` are gone. `OEXECUTE` reaches the native
`sysproc.c::execo()` path; the historical pathname
`EXEC`/`OPEN`/`CREATE`/`REMOVE`, path-based `STAT`/`WSTAT`, `FD2PATH`,
`BIND`/`MOUNT`/`CHDIR` and old stat syscall numbers are RESERVED in
`runtime/abi/sys.h`. The build generator emits a `nil` `systab` entry and sets
`sysreserved=1` for each, and `sysproc.c::dosyscall()` rejects such a call as a
plain `Ebadarg` without a note; there is no `sysunsupported()` handler.
`kernel/core/chan.c` keeps generic reference counting, `kstrcpy`/`kstrdup`,
`validname` and the `chandevreset` hook, but no `namec`/`walk`/`parsename` chain
or Chan close/dispatch machinery. N11.7D.2 later removed `devattach` itself;
N11.8.2 removed `cclose` and N11.8.3 removed the image/fault Chan path and
`ccloseq`. The canonical amd64 build has no `Chan`/`Path` pointer signatures or
`Dev`/`Walkqid` types (see `kernel/core/portdat.h` and `kernel/core/sd.h`).
`ge/storage/`
remains the native GE/O engine; its inherited DMOBJ stream state is removed,
while the on-disk Kobj/version/snapshot/arena/log code is unchanged.

## N11.4 MicroPython boundary

`micropython/p9file.c` and `micropython/boot.py` are gone. Pathname
`open()` is not registered in the builtins or `io` module, while
`io.StringIO`/`io.BytesIO` and the stream machinery stay. `main.c` keeps
`-c`, `-O`/`-E`/`-S`/`-N`/`-I`, native `p9_space_boot/report/commit` and the
native REPL; the file argument, `-B` and fd-console fallback are removed.
`modplan9.c` keeps fd/process/timing operations but no pathname
open/create/remove, mount, stat/dirread, exec or sync exports.
`p9space.c`/`p9image.c` are unchanged, so the native Space, Environment,
`OID_GRAPH` and `.mpy` paths are the same.

## N11.6 O-only Space boundary

The Python `space` module is O-only. `space.open` reaches `ospace_open`;
`space.define`, `mkenv`, `create`, `view`, `View` and `upgrade` are gone,
together with the `/usr/$user/lib` and `/usr/$user/space` file backend, the
V1–V4 version images, the V5/DMOBJ store and the V7 world/rebinding/
migration code. `p9image.c` keeps the active OID_GRAPH serializer and
restorer (`p9_image_commit`, `p9_image_restore_store`, dirty/OID maps,
roots, identities, classes/functions and Environment/MPY links). The stored
OID_GRAPH tags and header bytes are unchanged; there is no file/namespace
compatibility layer.

## N11.7 legacy-removal boundary

`kernel/core/cache.c` and the inherited `Mntcache` read-ahead layer are gone;
`page.c` keeps clean idle-image reclamation through `imagereclaim()`. The
never-generated `initcode.i`/`initcode[]` build remnant and the reference-less
`emptystr`/`eqqid`/`canmount`/`devpower`/`devconfig` helpers are removed.
MicroPython's required port symbols `mp_lexer_new_from_file` and
`mp_import_stat` remain, but without the dead pathname `open`/`read`/`stat`
bodies. Seven orphan `runtime/libc/9sys` units (`access`, `dirread`,
`dirstat`, `dirwstat`, `dirfwstat`, `getuser`, `nulldir`) and the unused
`include/9p.h`, `auth.h`, `draw.h`, `mouse.h` headers are removed; `fcall.h`
and `runtime/abi/sys.h` remain.

`Rgrp`, `Egrp` and `Pgrp` remain where the canonical amd64 build still links
them; `Qid` remains only for GEFS formatting. N11.8.5 later removed the
remaining `Dev`/`Chan`/`Walkqid` declarations and the empty `devtab`, so the
older retention sentences below describe only the pre-N11.8.5 state. N11.8.4 removed
`Path` and the full `Chan`/`Dev`/`Walkqid` layouts. The legacy FD core and `Fgrp` are gone (N11.8.2/N11.8.3); only
the historical syscall slots remain, with their canonical validation and
errors. `PIPE`/`syspipe` and the separate native handle pipe remain, as do
the IPv6-only sources. No network, storage-format or syscall ABI was changed. The historical N11.7D Dev-operation audit removed
nothing; N11.7D.2 then removed the 17 unreachable `dev.c`
directory/lifecycle operations and their declarations, keeping `mkqid`,
`devno`, `todinit` and the `closeproc` worker. N11.8.1 later removed the
unreachable legacy `syspipe()`→`pipechanpair()` FD-creation island together
with `newchan()`, `mkqid()`, `openmode()` and `pipechanpair()` and their
prototypes; `devno()` remained (called by `pgrp.c::devmask()`) until N11.8.5,
and the native handle pipe is unchanged; N11.8.3 later removed the orphan
Chan `closeproc` worker and its close queue. Any cleanup is assessed only against
the current amd64 build, its userspace and tools and their real callers.

## N11.8.2 FD success-path and Fgrp boundary

N11.8.2 removes the uninstantiable legacy FD success layer and decouples
`Fgrp` from `Chan` ownership. `kernel/core/sysfile.c` no longer contains
`newfd`, `fdtochan`, `fdclose`, FD allocation/growth, Chan read/write/seek/
stat dispatch or directory-stat serialization; the syscall slots, argument
validation order, `PIPE` `Ebadsharp` behavior and native `IOWRITE` remain.
`struct Fgrp` in `kernel/core/portdat.h` is now only `{ Ref; }`: `fd`,
`flag`, `nfd`, `maxfd`, `exceed` and `DELTAFD` are gone, while `dupfgrp`
still allocates and can fail and `closefgrp` still refcounts and frees.
`Proc.fgrp` is a presence token; `Proc.closingfgrp`, `forceclosefgrp`, the
`interrupted()` force-close call and the `execcommit` close-on-exec FD loop
are removed. `cons.c::pprint` no longer probes stderr fd 2 and returns 0
without output, its canonical no-FD result. The `chan.c` helpers `isdir`
and `dirchanstat` and their prototypes are removed with the FD I/O layer.

Retained because the compiled image/fault path still needs them:
`Chan`, `Path`, `Dev`, `Qid`, `Image`, `ccloseq`, `chanfree`,
`pathclose`, `chanpath`, `chanclose`, `closeproc`, `validname`,
`validnamedup`, `attachimage` and `eqchantdqid` (`segment.c::putimage`
calls `ccloseq`; `fault.c::pio` reads `image->c`). `Pgrp`, `Rgrp`, `Egrp`
and the native O/USB/Pipe/kbd/UART handle paths are unchanged. `RFORK`
presence/inheritance semantics are preserved: `RFFDG` on an absent group
is `Ebadarg`, `RFCFDG` creates an independent present group when absent,
`RFFDG` copies a present group, no-flag forks inherit presence, and
non-`RFPROC` recovery, `RFPROC` parent/child assignment and combined flag
validation are unchanged. The canonical kernel build/link passed (123/123
selected, 125/125 full closure, 0 undefined symbols) and one disposable
QEMU `oboot=server` boot reached `OEXECUTE`, `init.run()`, GE/O commit,
`REPL READY` and `>>>`; no physical p5/ESP write was made. N11.8.3 later
retired the token type and replaced it with `Proc.fdgrp`; see below.

## N11.8.3 Fgrp retirement

The inherited `Fgrp` type is gone from the canonical amd64 process model.
`struct Proc` carries a process-owned `Ref *fdgrp` instead: an explicitly
named legacy RFORK FD-group presence token (`nil` = absent) that owns no FD
table or other resource. `kernel/core/pgrp.c` provides `newfdgrp()` (fallible
`malloc(sizeof(Ref))`, `ref=1`) and `closefdgrp()` (decref/free), replacing
`dupfgrp`/`closefgrp`; `portfns.h` carries the new prototypes and `portdat.h`
the `Proc.fdgrp` field, with no `Fgrp` declaration remaining.
`sysproc.c::sysrfork` preserves combined-flag validation, `RFFDG`-on-absent
`Ebadarg`, fresh independent groups for `RFFDG`/`RFCFDG`, shared-pointer
`incref` inheritance for no-flag children, `waserror` rollback and close
ordering, while `proc.c::pexit` releases the token and `ouserproc` starts it
absent.

The same pass removed the dead file-backed `Image -> Chan` island:
`attachimage` (its sole creator, with no canonical caller), `newimage`,
`putimage`, the image idle/cache/reclaim functions, `data2txt`/`txt2data`,
`fault.c::pio`/`faulterror`, `Segment.image`/`fstart`/`flen`,
`Page.image`/`daddr`, `PGHASH`, the `Image` type and the unused Image pool/
`conf.nimage` budget. With the image close path gone, `ccloseq`, `chanfree`,
`pathclose`, `chanclose`, `closeproc`/`clunkq`, `chanalloc`, `chanpath`,
`eqchantdqid` and `chandevinit` had no compiled amd64 user and were removed.
`chan.c` retains `incref`/`decref`, `kstrcpy`/`kstrdup`,
`chandevreset`/`chandevshutdown`, `isfrog` and `validname`/`validnamedup`.
`Chan`/`Path` remain only as opaque pointer types for compiled SD/LED
signatures; no code reads their fields. Native O pages stay resident;
COW, page allocation, reclaim/wait and non-swap behavior are unchanged, and a
missing `SG_TEXT`/`SG_DATA` page is a fail-closed invariant panic. The
canonical kernel build/link passed (123/123 selected, 125/125 full closure,
0 undefined symbols) and one disposable QEMU `oboot=server` boot reached
`OEXECUTE`, `init.run()`, GE/O commit, `REPL READY` and `>>>`; no physical
p5/ESP write was made. The QEMU smoke does not exercise `rfork` edge cases or
memory-pressure behavior.

## N11.8.4 namespace declaration contraction

`kernel/core/portdat.h` no longer defines `struct Chan`, `struct Path`, the
full `struct Dev` callback table or `struct Walkqid`. At that point `Chan` and
`Walkqid` remained as incomplete pointer types used only by compiled SD/LED/
network signatures, and `struct Dev` was only `{ int dc; }` for
`dev.c::devno()` and `sdcore.c::sdlegacydev()`; N11.8.5 removed all of them
(see below). `Path` is gone. The
`CHANINVALID` sentinel and the unused channel-flag enum were removed; `Qid`
remains from `<fcall.h>`. `chan.c` lost the empty `chandevshutdown()`, and
`kernel/amd64/main.c` lost its call in the already-`panic`-unreachable reboot
tail. `chandevreset()` still calls `todinit()` at the same boot point, and
`incref`/`decref`, `kstrcpy`/`kstrdup`, `isfrog` and
`validname`/`validnamedup` are unchanged. `devno()`/`devmask()` and the
`Pgrp` device mask were unchanged at that point; N11.8.5 later removed
`devno()` while preserving the canonical mask effects. No
`sd.h`/`led.h`/`netif.h` change was needed then. In `fault.c`, a missing resident `SG_TEXT`/`SG_DATA` page now takes a
bounded `Faultnopage` user-fault/`Eio` path instead of `panic()`, while
memory-pressure `Faultretry`, lazy fill, resident COW and non-swap behavior
are unchanged. Canonical kernel build/link passed (123/123 selected, 125/125
full closure, ABI PASS, 0 undefined symbols; 959,624-byte ELF, SHA-256
`87377864b6184a2bec94e2a395d5d0260f542d65e3bd1248f22ac9d8487b0bb5`) and one
disposable QEMU `oboot=server` boot reached `OEXECUTE`, `init.run() -> 2`,
GE/O commit, `REPL READY` and `>>>` without panic; malicious `SEGFREE` was
not exercised.

## N11.8.5 Dev registry and legacy #S subview removal

`kernel/amd64/pc64` has no `dev` rows, and
`tools/host/build-kernel.py::make_pc64()` now emits no `Dev` extern or
`Dev* devtab[]`; the regenerated `build/gcc/gen/pc64.c` therefore has no legacy
Dev registry. `kernel/core/dev.c` (whose only content was `devno()`) is no
longer in the authoritative `KERNEL_SOURCES` manifest and is retired.
`kernel/core/portdat.h` no longer declares `Dev`, `devtab[]`, `Chan` or
`Walkqid`. `pgrp.c::devmask()` no longer calls `devno()`: in the canonical
no-Dev profile `invert == 0` leaves the inherited mask untouched and
`invert == 1` sets all 256 bits, exactly the old empty-`devtab` `RFNOMNT`
effect, with the same `Pgrp` write lock and ownership. `devmasked()`,
`pgrpcpy()` and `rfork` error ordering are unchanged. `sdcore.c` dropped
`sdlegacydev()` and now reports `native-only` after initialization; controller
discovery, the exactly-once guard, `SDifc` calls and native block/flush paths
are unchanged.

The legacy `#S` virtual-subfile metadata is gone. `SDfile`, `SDrw`,
`SDunit.efile/nefile`, `sdaddfile()`, NVMe `readsmart`, AHCI `ahciledr/w` and
`led.c:ledr/ledw` had no canonical caller once the #S adapter was absent and
were removed; `ledname`/`name2led` remain as generic IBPI policy. Actual AHCI
enclosure LED hardware control (`ledkproc`, `blink`, `blinkesb`, `ledstate`)
and NVMe controller initialization/identify/`nvmerctl` remain. `Qid` remains
only in `kernel/core/lib.h` for the GEFS formatter (`ge/storage/dump.c`);
no canonical driver uses it as directory identity. Unbuilt legacy arm64 sources still mention the removed
`Dev`/`Chan`/`Walkqid` types and are outside the AMD64 build scope; these are
distinct from the actively developed `kernel/arm64/gcc/` boot foundation.

Network drivers are likewise detached from directory Qid. `kernel/net/ip.h`
no longer has `Proto.qid`; `ipcore.c::Fsproto()` no longer writes `QTDIR`/
`QID(...)` and no longer includes `ipdevqid.h`, which is retained only as
dormant #I compatibility source. The conversation bound is the explicit
numeric `Maxconv = 4096`, identical to the legacy `Maskconv + 1`; protocol
indices, registration order and conversation allocation are unchanged.
`kernel/core/netif.h` lost the #I Qid-directory enum, `NETTYPE`/`NETID`/
`NETQID` and the orphan `netifinit`/`netif*`/`activemulti` declarations;
`ip.h` lost orphan `ifclog*`, `routewrite` and `chandial` declarations. Live
`Fs`/`Proto`/`Conv`/`Netif`/`Netfile`/`Queue`/Ethernet/IPv6 and netlog remain.

## Native host preparation

`tools/host/build-format-o.sh` compiles the real O reamer
(`ge/format/ream.c::reamo`, with the surviving GE arena/log/B-tree block/
hash/pack/load primitives) as a normal Linux GCC program. `ge/format`
contains only the eight units and two headers that this O v1 formatter
needs; the former GEFS FS/9P server units are gone. `tools/host/ge-compat`
supplies only the Plan 9 libc, error-stack, synchronization and
file-descriptor glue the O-only call graph needs; there is no 9P server,
FS root, adm tree or user table in the host formatter. The formatter
operates on a caller-selected regular image, never on a block device.

`tools/host/build-mpy-cross.sh` builds the repository's MicroPython
1.27.0 `py` sources with the upstream v1.27.0 `mpy-cross` front end as a
Linux process. `tools/host/oseed.py` hashes the source bytes, compiles
the `.mpy`, and writes the seed directory consumed by the reamer. No
Ken C `cc`/`6c`/`6l`, Plan 9 `mk`, 9front VM or tar-as-virtual-disk step
is part of this path, and the formatter is not replaced by a simplified
Python image writer. `runtime/libc` is now the live subset compiled by
GCC: the 75 explicit MicroPython archive members plus the formatter and
kernel members. Unused inherited C sources, Ken C amd64 assembly, `ucd`
generators/data and 9front mkfiles are gone; the retained
`runtime/{ip,crypto,mp,aml,fis}` files are the members actually
live-linked by the kernel or MicroPython (not the whole old libraries).

## Future OID source objects

Eventually source files become GE/O byte objects addressed by OID, with
dependencies expressed as native relations. This layout does not implement
that repository, an OID manifest schema, a compilation graph or new kernel
interfaces. The current paths are a conventional host-side export only.

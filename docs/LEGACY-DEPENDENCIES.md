# Legacy dependency inventory — Chan, paths and namespaces

**Status:** historical analysis reference; the N10 source move removed the inherited `sys/src` userland tree, so paths below describe provenance and conversation history, not current files.

**N11.7 current state (2026-10-28):** `kernel/core/cache.c` and the
inherited `Mntcache` read-ahead layer are gone. `cinit` had no caller, so
`Chan.mcp`, `copen/cread/cwrite/cupdate/ctrunc/cclunk` and `fscache` were
unreachable; the only `pagereclaim(fscache)` call was an always-nil no-op.
Clean idle-image reclamation remains in `page.c::reclaim` via
`imagereclaim()`; `ge/storage/cache.c` is the separate active GE/O cache.
N11.7 also removed reference-less `emptystr`/`eqqid`/`canmount`/`devpower`/
`devconfig`, the dead pathname bodies behind MicroPython's
`mp_lexer_new_from_file`/`mp_import_stat`, the orphan `9sys` directory/access
units, and unused `9p.h`/`auth.h`/`draw.h`/`mouse.h`. At that point the active
`Chan`/`Dev`/fd/`Fgrp`/`Pgrp` machinery, `sysfstat`/`sys_fstat`, PIPE/
PIPENEW and IPv6-only network sources remained. The canonical
`pc64` config registers no `dev` rows; that alone is not treated as proof that
a shared type or helper is dead. N11.7D.2 later removed the unreachable
directory-helper family, N11.8.1 removed the unreachable Chan-backed pipe
creation island, N11.8.2 removed the uninstantiable FD success layer and
reduced `Fgrp` to a presence token, and N11.8.3 retired `Fgrp` and the dead
file-backed `Image -> Chan` island (see below).

**N11.7D Dev-operation audit (2026-10-28, historical):** that pass removed
nothing. No `dev.c` definition or `portfns.h` declaration was removed. Its
no-removal result is kept as history and does not describe the current tree;
the then-applicable AMD64-only scope decision superseded its former retention rationale based
on dormant non-target sources.

**N11.7D.2 Dev-operation removal (2026-10-28):** removed the 17 unreachable
`dev.c` operations `devdir`, `devgen`, `devclone`, `devwalk`, `devstat`,
`devdirread`, `devpermcheck`, `devopen`, `devattach`, `devcreate`,
`devremove`, `devwstat`, `devbread`, `devbwrite`, `devreset`, `devinit` and
`devshutdown`; `dev.c` went from 428 to 26 lines and 19 to 2 function
definitions. At that point `mkqid` (called by `pipe/pipecore.c`) and `devno`
(called by `pgrp.c`/`sysfile.c`) remained; N11.8.1 later removed `mkqid` and
the pipe creation island, leaving `devno` for `pgrp.c::devmask()`. Active
`Dev` dispatch remains in `chan.c`/`sysfile.c`/`fault.c`/`cons.c`. `portfns.h` lost 17 prototypes and
`Devgen devgen`; `portdat.h` lost the `Dirtab` typedef/struct, the `Devgen`
typedef and `DEVDOTDOT`; the orphan `newpath` helper and prototype were also
removed. `chan.c` dropped three empty `devtab` reset/init/shutdown loops
while retaining `todinit()` and the `closeproc` worker. The canonical generated `devtab` is `{ nil }`; dormant
`kernel/arm64/devrtc.c` is the only remaining source that names the removed
helpers and is not in `KERNEL_SOURCES`. The kernel manifest remains 123
selected / 125 full-closure source units. Kernel build/link passed; a fresh
QEMU `oboot=server` boot reached `OEXECUTE`, `init.run()`, `OSPACECOMMIT`,
`REPL READY` and `>>>`; no physical p5/ESP write was made.

**N11.8.1 Chan/FD pipe-creation seam removal (2026-10-28):** removed the
provably unreachable legacy FD-backed pipe island from canonical amd64.
`syspipe()` retains its ABI, argument decoding, `validaddr`/`evenaddr` order,
`-1` output writes and observable `Ebadsharp`, but no longer calls
`devno('|')`, `pipechanpair()` or `newfd2()`. Removed `newfd2()` and
`openmode()` from `sysfile.c`; `pipechanpair()`/private `pipeendpoint()` from
`pipe/pipecore.c`; `newchan()` from `chan.c`; `mkqid()` from `dev.c`; and the
four matching `portfns.h` prototypes (`newchan`, `mkqid`, `openmode`,
`pipechanpair`). The native `pipecorepair()`/`pipecoreread()`/
`pipecorewrite()`/`pipecoreclose()` queue implementation and the `syspipe.c`
`PIPENEW`/`PIPEREAD`/`PIPEWRITE`/`PIPECLOSE` handle path were unchanged at that point; phase 3 (2026-11-05) later retired those numbers 71-74 and 100 in favour of the OBJ* ABI.
`devno()` remains for `pgrp.c::devmask()`. `Fgrp`, fd tables, `Chan`, `Path`,
`Dev`, `newfd`/`sysdup`, `fdtochan`/`fdclose`, `cclose`/`ccloseq`/
`chanfree`/`pathclose`/`closeproc`, process exit/rfork/exec teardown and
`Proc.fgrp` remain. Remaining live roots: `sysfile.c` fd paths (`newfd` via
`sysdup`, `fdtochan`, `fdclose`, read/write/stat/seek); `pgrp.c`
`dupfgrp`/`closefgrp`/`forceclosefgrp`; `sysproc.c` `sysrfork`/
`execcommit`; `proc.c` exit teardown; `cons.c` diagnostic
`up->fgrp->fd[2]` probe. Build/link and one disposable QEMU `oboot=server`
boot passed; no physical p5/ESP write was made. N11.8.2 later removed the FD
success layer and the `Fgrp` fields/close queue described here; see below.

**N11.8.2 FD success-layer removal and Fgrp decoupling (2026-10-28):** the
canonical amd64 process still has no producer of a first live FD
(`ouserproc` sets `up->fgrp = nil`; `RFCFDG` creates only an empty group;
`open`/`create`/`pipe` cannot create one). `sysfile.c` lost `newfd`,
`fdtochan`, `fdclose`, `unlockfgrp`, `growfd`, `findfreefd`, the Chan
read/write/seek/stat dispatch, directory-stat serialization, and the
`dirname`/`dirsetname`/`pathlast`/`packoldstat` helpers. `dup`, `close`,
`read`, `pread`, `write`, `pwrite`, `seek`, `oseek`, `fstat` and `_fstat`
keep their syscall slots, argument decoding, pre-FD pointer/alignment checks
and canonical `Ebadfd`; `PIPE` keeps `Ebadsharp`; `IOWRITE` is unchanged.
`struct Fgrp` is reduced to `{ Ref; }`; `fd`, `flag`, `nfd`, `maxfd`,
`exceed` and `DELTAFD` are gone. `dupfgrp` still allocates a failable
presence token and `closefgrp` still refcounts/frees it, preserving
`RFFDG`/`RFCFDG`, absent-versus-present, shared-group and teardown semantics.
`forceclosefgrp`, `Proc.closingfgrp`, the `interrupted()` force-close call
and the `execcommit` close-on-exec FD loop are removed. `cons.c::pprint`
returns 0 without output, the exact canonical no-stderr-FD-2 result.
`isdir`/`dirchanstat` and their prototypes are removed; the now-uncalled
`cclose` is also removed while `ccloseq` remains. Retained because the
compiled image/fault path still uses them: `Chan`, `Path`, `Dev`, `Qid`,
`Image`, `ccloseq`, `chanfree`, `pathclose`, `chanpath`, `chanclose`,
`closeproc`, `validname`, `validnamedup`, `attachimage` and `eqchantdqid`
(`segment.c::putimage` calls `ccloseq`; `fault.c::pio` reads `image->c`). `Pgrp`, `Rgrp`, `Egrp` and the native O/USB/Pipe/kbd/UART
handle tables are unchanged. Kernel build/link and one disposable QEMU
`oboot=server` boot passed; no physical p5/ESP write was made. The QEMU smoke
does not exercise `rfork`/exec/error-order edge cases; those are preserved
by the unchanged flag validation and control flow. N11.8.3 later removed the
`Fgrp` type/API, the `Image -> Chan` island and the orphan Chan close
lifecycle described here; see below.

**N11.8.3 Fgrp retirement and dead Image/Chan removal (2026-10-28):** the
`Fgrp` type and `dupfgrp`/`closefgrp` API are gone from canonical amd64.
`Proc.fdgrp` is a `Ref *` legacy-RFORK FD-group presence token (`nil` =
absent) with fallible `newfdgrp()`/`closefdgrp()`; `RFFDG`/`RFCFDG`, the
absent-group `Ebadarg`, independent allocations, shared-pointer `incref`,
`waserror` rollback and `pexit` release are unchanged. The only Image
creator, `attachimage(Chan*)`, had no canonical caller; `execo`/`ouserproc`
load resident pages directly. Removed `attachimage`, `newimage`, `putimage`,
`busyimage`/`idleimage`, `pagereclaim`/`imagereclaim`/`imagecached`,
`cachepage`/`uncachepage`/`lookpage`/`cachedel`, `data2txt`/`txt2data`,
`fault.c::pio`/`faulterror`, `Segment.image`/`fstart`/`flen`,
`Page.image`/`daddr`, `PGHASH`, the `Image` type and the unused Image pool/
`conf.nimage` allocation budget. After the image close path was gone,
`ccloseq`, `chanfree`, `pathclose`, `chanclose`, `closeproc`/`clunkq`,
`chanalloc`, `chanpath`, `eqchantdqid` and `chandevinit` had no genuine
compiled amd64 user and were removed. `chan.c` retained `incref`/`decref`,
`kstrcpy`/`kstrdup`, `chandevreset` (`todinit`), `isfrog` and
`validname`/`validnamedup` (N11.8.4 later removed the then-empty
`chandevshutdown`). At that point `Chan`/`Path` remained only as opaque
pointer types for compiled SD/LED signatures; no code read their fields.
Native O text/data pages remain resident; COW, page allocation, reclaim/wait
and non-swap behavior are unchanged. At that point a missing
`SG_TEXT`/`SG_DATA` page took an explicit fail-closed panic instead of
dereferencing a nil `Image.c`; N11.8.4 replaced that with a bounded user-fault
path (see below). Kernel build/link and one disposable QEMU `oboot=server`
boot passed; no physical p5/ESP write was made. The QEMU smoke does not
exercise `rfork` edge cases or memory-pressure behavior.

**N11.8.4 declaration contraction and resident-page fault safety
(2026-10-28):** `kernel/core/portdat.h` no longer defines `struct Chan`,
`struct Path`, the full legacy `struct Dev` (`attach/walk/stat/open/create/
close/read/bread/write/bwrite/remove/wstat/power/config`) or `struct
Walkqid`; the `Path` typedef and the unused channel-flag enum
(`COPEN`/`CMSG`/`CCEXEC`/`CFREE`/`CRCLOSE`/`CCACHE`) and `CHANINVALID`
sentinel were also removed. At that point `Chan` and `Walkqid` remained only
as incomplete pointer types used by the orphan `sd.h`/`led.h`/`netif.h`
signatures (N11.8.5 removed those signatures and typedefs), and `struct Dev`
was only `{ int dc; }`. That single field was then required because
generated `pc64.c` emitted `Dev* devtab[] = { nil, };` and both
`dev.c::devno()` and `sdcore.c::sdlegacydev()` scanned the table and read
`dc`; `devno()` then backed `pgrp.c::devmask()`, whose
`RFNOMNT`/empty-table semantics are unchanged. N11.8.5 removed the registry,
`devno()` and that scan entirely (see below). `Qid` remains in
`kernel/core/lib.h` for the live GEFS formatter use. `chan.c` lost the empty
`chandevshutdown()`; `chandevreset()` still
calls `todinit()` at the same boot point, and `incref`/`decref`,
`kstrcpy`/`kstrdup`, `isfrog` and `validname`/`validnamedup` remain for live
callers. No `sd.h`/`led.h`/`netif.h` argument list changed.

In `kernel/core/fault.c` the missing-resident-page panic was replaced by a
distinct `Faultnopage` (`-2`) result. User `SEGFREE` can `nil` a resident
`SG_TEXT`/`SG_DATA` PTE slot through `mfreeseg()` (which still skips
`SG_PHYSICAL`/`SG_FIXED`/`SG_STICKY`), and the next access now reaches
`fixfault()`, which releases the segment qlock and returns `Faultnopage`
without allocating or filling a page. `fault()` restores
`up->psstate`/`up->insyscall`; when a kernel-context error label exists it
raises `Eio`, otherwise it returns `-1` so `faultamd64()` takes the ordinary
`faultnote()`/failed-user-fault path. `Faultnopage` never enters the
memory-pressure `Proc_exitme`/`Proc_exitbig` retry switch; that switch and
the three `newpage`/`ptealloc` `Faultretry` (`-1`) returns are unchanged.
Lazy `SG_BSS`/`SG_STACK` fill, resident `SG_DATA` COW, `PTERONLY` text,
physical/fixed/sticky handling, `putmmu` flags, OEXECUTE residency and the
absence of swap/pager/zero-fill are unchanged. Kernel build/link passed and
one disposable QEMU `oboot=server` boot reached `OEXECUTE`, `init.run()`,
GE/O commit, `REPL READY` and `>>>` without panic; malicious `SEGFREE` was
not executed.

**N11.8.5 Dev registry and legacy driver-Qid removal (2026-10-28):** the
canonical `kernel/amd64/pc64` has no `dev` rows, and
`tools/host/build-kernel.py::make_pc64()` now emits no `Dev` extern or
`Dev* devtab[]`; the regenerated `build/gcc/gen/pc64.c` contains no legacy
registry. `kernel/core/dev.c` (whose only content was `devno()`) is removed
from `KERNEL_SOURCES` and from the source tree. `portdat.h` no longer
declares `Dev`, `devtab[]`, `Chan` or `Walkqid`, and `portfns.h` no longer
declares `devno()`. `Qid` remains in `kernel/core/lib.h`; it is used only by
the GEFS formatter (`ge/storage/dump.c::Qconv`) and is not a driver
directory identity.

`pgrp.c::devmask()` no longer calls `devno()`. For the canonical no-Dev
profile this is observably exact: a non-inverting call leaves the inherited
mask untouched, and an inverting call (`RFNOMNT`) sets all 256 bits, which is
what the old empty-`devtab` loop computed (`0 ^ 0xFF`). The `Pgrp` write
lock, `pgrpcpy()`, `devmasked()`, `RFNOMNT`/`RFNAMEG`/`RFCNAMEG` flags and
`rfork` error ordering are unchanged. `sdcore.c` dropped `sdlegacydev()` and
prints `native-only`; controller discovery, the exactly-once guard, `SDifc`
operations, cache flush and native raw-block/GE/O paths are unchanged.

The legacy #S virtual-subfile metadata is gone: `sdaddfile()`, `SDfile`,
`SDrw`, `SDunit.efile`/`nefile`, NVMe `readsmart()`, AHCI `ahciledr()`/
`ahciledw()` and `led.c::ledr()`/`ledw()` had no canonical caller without the
#S adapter. Actual AHCI enclosure LED hardware control (`ledkproc`, `blink`,
`blinkesb`, `ledstate`) and NVMe controller initialization/identify/
`nvmerctl` remain; `ledname()`/`name2led()` remain as generic IBPI policy.

Network driver identity is no longer Qid-based. `ip.h::Proto.qid` and the
`Fsproto()` `QTDIR`/`QID(...)` writes are removed; `ipcore.c` no longer
includes `ipdevqid.h`, which is now explicitly dormant and included by no
compiled source. The conversation bound is the plain numeric
`Maxconv = 4096` (identical to the legacy `Maskconv + 1`); protocol indices,
`Fsproto()` registration order, duplicate-protocol rejection and conversation
slot allocation are unchanged. `netif.h` lost the #I Qid-directory enum,
`NETTYPE`/`NETID`/`NETQID` and the orphan `netifinit`/`netif*`/`activemulti`
prototypes; `ip.h` lost the orphan `ifclog*`, `routewrite()` and `chandial()`
prototypes. Live `Fs`/`Proto`/`Conv`/`Netif`/`Netfile`/`Queue`/Ethernet/IPv6
interfaces and netlog are retained.

Kernel build/link passed (122/122 selected, 124/124 full closure, ABI PASS,
0 undefined symbols; `build/gcc/9pc64.elf`, 955,200 bytes, SHA-256
`b1a4c76936d780a214c456ad2d97d62e98d57506de49f390b67a1b0d73745c8d`). One
disposable QEMU `oboot=server` boot reached `sd: controllers initialized (2)
native-only`, `OEXECUTE`, `init.run()`, GE/O commit, `REPL READY` and `>>>`
without panic. The QEMU smoke does not prove physical AHCI/NVMe SMART/LED
handling or IPv6 traffic.

**N11.6 current state (2026-10-28):** the Python Space API is O-only.
`space.define/mkenv/create/view/View/upgrade` and the V1–V7 path/file
backend are gone; `space.open` reaches only `ospace_open`. The V7
upgrade/world/symtab code and the unused V1–V4 restore/check code are
removed from `p9image.c`/`p9space.h`. The active OID_GRAPH serializer and
restorer remain, including its stored V5/V7 header bytes and
`O_FUNN`/`O_CLASSN` tags. There is no pathname/file Space fallback.

**N11.5 current state (2026-10-28):** the Space V1–V7 path/file backend
in `micropython/p9space.c` and the V7 upgrade/view world in
`micropython/p9image.c` remain intentionally. `space.open/create/define/
mkenv/view/upgrade` are still public exports; their non-O branches still
enter the path backend, and `space.view`/`space.upgrade` still call the
upgrade/world code. Removing those functions would require changing a
visible Python call or its error, so N11.5 removed only `p9_space_isopen`
(no caller), a stale `p9_world_env` prototype and an unused local. The
active OID_GRAPH/O path and `p9image.c` are unchanged.

**N11.4 current state (2026-10-28):** MicroPython no longer exports the
pathname file model. `p9file.c` (Python `open()`/`FileIO` on Plan 9 fds),
the built-in `boot.py`, `-B`, the file-argument path and the
`fd2path`/`/dev/consctl`/fd-console fallback are removed. `io.StringIO`,
`io.BytesIO` and the stream machinery remain. `modplan9.c` keeps
`read/write/close/seek/dup/pipe/alarm/sleep/getpid/rfork/wait/exits` and
drops `open/create/remove/bind/mount/unmount/chdir/fd2path/stat/wstat/
dirread/sync/spawn/exec` plus their dead helpers/constants. The core still
requires `mp_lexer_new_from_file` and `mp_import_stat` callbacks; they are
retained in `main.c` but their pathname reads/stat now fail through the
N11.3 syscall rejections. `p9space.c`/`p9image.c` are unchanged: the
`ovol>=0` O paths are active and the V1–V7 path/file backends are
audit-only until a later, separately decided removal.

**N11.3 current state (2026-10-28):** the canonical kernel no longer compiles
`kernel/core/legacyexec.c`, `kernel/core/legacypath.c` or
`kernel/core/rebootcmd.c`, and no pathname `namec`/`walk`/`parsename`
implementation remains in `chan.c`. Those handlers are gone from
`KERNEL_SOURCES`; the generated syscall table keeps their ABI numbers and
points them at `sysunsupported()`. `sysfstat`/`sys_fstat`, the legacy FD
syscall slots (success paths removed by N11.8.2), `Chan`/`Dev`, the
`Proc.fdgrp` presence token, `validname`/`validnamedup` (USB, environment and
`SEGATTACH` names) and the `Path`/`chanpath` machinery remain; `devattach`
itself was removed by N11.7D.2. The
historical E12 sections below describe the earlier incremental state, not
the current link set.

**Status (original):** current analysis reference after E12.20 separated the old
directory/user metadata from the on-disk GE/O packer and closed E12 for the
canonical O-only claim. The `pc64` GEFS archive has 15 modules; `user.c`, the
directory codecs and all inherited FS/9P/Fid/Conn/DMOBJ adapters are
source-preserved but outside the canonical O-only target. Native GE/O
storage, transactions, sync/epoch, SpaceID/OID and `OEXECUTE` remain. This is
not an implementation claim and does not authorize deleting working source.

The longer-term architectural goal is **compiler-driven elimination of
inherited `Chan`, pathname, `slash`, `Fgrp`, `Rgrp`, `Egrp` and namespace
dependencies from the canonical kernel**, with a source-level dependency
inventory and a tested replacement for each removed use. E12.1 removed
`Proc.dot`; E12.2 removed `Proc.slash` and the inherited kernel `root` `Dev`;
E12.3 removed the canonical swap `Dev`, pager/pageout and fake `devtab[0]`
closer convention; E12.4 removed the legacy root/init/GEFS-mount bootstrap
and unregistered the canonical `mnt` Dev; E12.5 unlinked `devmnt` and removed
the `Pgrp` mount lists plus Chan mount/union traversal; E12.6 deleted the
inert mount types/fields and made pipe creation pathless; E12.7 removed the
dead mount access modes and the `nomount` walk flag, and stopped direct pipe
endpoints from inventing pathnames; E12.8 moved pathname exec and
open/create/remove into separate linked units; E12.9 moved GEFS-local pipe
creation to the existing `pipechanpair()` factory and kernel `ksetenv` to
direct `Egrp` mutation without `namec`/`Chan`; E12.10 moved `mkpipe`/
`gefspost`/`gefsserve` and `loadfs` into source-preserved legacy units and
removed only those units from the canonical GEFS archive/link; E12.11 moved
the `runfs` 9P pipeline and DMOBJ `objread` adapter into new units that remain
compiled and archived, without unlinking any 9P or object-engine code yet;
E12.12 stopped native-O `runmutate`/`runread`, gated legacy `objresume`, and
moved the Fid clunk/pending/abort adapter to `legacyobjctl.c`; E12.13 moved
the shared reply path to `legacyreply.c`, excluded `legacy9p.c` from canonical
`pc64`, and isolated the DMOBJ writer in `legacyobjwrite.c`; E12.14 moved
`newconn`/`putconn` to `legacyconn.c` and `objresume`/`objsweep` to
`legacyobjrecover.c`, breaking the direct `obj.c`→`objpending` edge; E12.15
moved `runmutate`/`runread` to `legacyfsworkers.c` with two narrow dispatchers
and moved `gettx` to `legacyobjtx.c`; E12.16 moved the request/Fid/auth owner
to `legacyfsrequests.c`, made `gestart` O-only, and excluded
`legacyfsworkers.c` from canonical `pc64`.

The canonical `oboot=server` path never called `namec` and is unchanged; the
legacy absolute-path boot, mount and REPL paths are now explicit failures.
Some of these structures also serve non-filesystem functions; for example
rendezvous uses `Rgrp`, and process/fd machinery is not reducible to pathname
lookup. Nothing in this document proves that a listed structure can be removed
wholesale.

## E12.1–E12.16 process, root, VM, mount and call-site state

- **Absent `Proc.dot` (cwd).** There is no process current-directory channel.
  `syschdir` fails with `Ebadarg`; a relative `namec` name fails with
  `Ebadarg` before constructing or walking a `Chan`. After E12.5
  `/proc/N/ns` is an empty file: it no longer lists mount/bind entries and
  does not emit the leading `cd` line. `/proc/N/fd` still enumerates and locks
  the descriptor table but does not emit a cwd header. These are explicit
  unsupported legacy operations, not a silent synthetic root.
- **Absent `Proc.slash` (root).** E12.2 removed the member from `struct Proc`.
  `proc.c` (`kproc`) and `sysproc.c` (`sysrfork`) no longer inherit a root;
  `userinit`/`proc0` no longer creates one. `namec`'s absolute case now fails
  with `absolute path: no process root inherited`; direct `#device`
  attachment is independent of a process root and remains. The canonical
  `oboot=server` process has no root, calls no pathname operation, and is
  unaffected.
- **Root `Dev` and `bootdir` removed.** The `pc64` configuration no longer
  registers `root` and no longer embeds `bootdir` payloads (`kbdfs`, `oinit`,
  `ohandle`, `oexec`). `devroot.c` is not linked into the canonical amd64
  kernel. Consequently `#/`, `rootreset`, the in-memory `/boot` tree
  and `addbootfile` are not linked into the canonical kernel. `oinit` remains
  a buildable bootstrap helper; the `ohandle`/`oexec` test programs and the
  `ktest`/syscall harnesses were removed in N9.8. N11.8.4 removed the former
  `CHANINVALID` unbound-channel state along with `struct Chan`; N11.8.5
  removed the empty `devtab[]`, `struct Dev`, `devno()` and the
  `Chan`/`Walkqid` typedefs entirely from the canonical build.
- **No swap (E12.3).** The canonical `pc64` config no longer registers `swap`,
  so `devswap.c` is not compiled or linked and the fields, pager and PTE
  encoding it required were removed from the shared canonical headers:
  `swapimage`, `Swapalloc`, `PG_ONSWAP`, `onswap`, `swapaddr`,
  `putswap`/`dupswap`/`swapcount`, `Segment.swapped`, `Page.refage`,
  `conf.nswap` and `conf.nswppo`. `devswap.c` remains on disk as dead
  historical source for out-of-scope configurations and no longer compiles
  against current headers; canonical `pc64` has no swap device, anonymous
  pageout, swap-in fault path or swap-backed PTE tag. Clean page-cache and
  idle-image reclamation was retained in `page.c` as a non-swapping
  `reclaimer`; it never writes user pages to backing storage. `newpage`
  attempts that reclamation before blocking, and true exhaustion blocks for
  memory to be freed rather than panicking or invoking an OOM killer.
  `Proc.noswap` is retained only as a last-free-page allocation priority and
  `SG_NOSWAP` as an inert no-image marker; neither activates swap. Only the
  amd64 `pc64` kernel is in scope; swap is absent there.
- **Unbound `Chan` state (E12.3; superseded by N11.8.4).** The sentinel and
  its `cclose`/`closeproc`/`devmnt`/`sysremove` handling described here no
  longer exist: N11.8.4 removed `struct Chan`, the channel-flag enum and
  `CHANINVALID`. `Chan` is now only an incomplete pointer type in dormant
  SD/LED/network signatures; no canonical code creates or inspects one.
- **Legacy root/init bootstrap removed.** The canonical `pc64` `init0()`,
  `proc0()`, `userinit()` and embedded `initcode.i` first-process setup are
  gone. The canonical boot is the single native O-server path: the embedded
  `obootoid` loader uses `OVOLOPEN`/`OSPACEOPEN`/`OEXECUTE` on the GE/O
  volume named by `obootvol=PARTUUID=...`. A non-server `oboot=` value is
  rejected; there is no `probe`/`user`/`oid`/`mp` dispatcher. `rootmount()`,
  `gefsroot()`, `scandisks()`, `gevolume()`, `isgefs()`, `gptpart()` and the
  historical `getest`/`otest`/`oobj` probe wrappers were removed from the
  former `devgefs.c`; the file was renamed `sysgefs.c` and G1 deleted its
  `#G` Dev. `init=/boot/oinit`, `init=/boot/mp`, `root=` and absolute-path
  recovery flows remain unsupported; the former disposable ESP/O REPL disk
  generator and its launcher were removed. Native
  `OVOLOPEN`, `OSPACE*`, `OEXECUTE`, `PARTUUID=`/SD block access and GEFS
  shutdown hooks are retained for the canonical amd64 kernel.
- **Mount transport unlinked, mount namespace removed (E12.5).** `mnt` is not
  in the canonical `dev` config and `devmnt.c` is not compiled or linked, so
  `devtab` had 13 entries at that E12.5 baseline (G1 now leaves 11), `devno('M')` is negative, and the image has no
  `mntattach`/`mntauth`/`mntversion`/`mntrah*`/`muxclose` symbols. `sysbind`,
  `sysmount`, `sys_mount` and `sysunmount` fail with explicit unsupported
  errors; `sysfversion`/`sysfauth` fail with an explicit "9P mount transport
  not supported" error before touching an fd or user buffer. `cache.c` mount
  read-ahead was removed; N11.7 later removed that unused file entirely (see
  the N11.7 note above), while clean idle-image reclamation remains in
  `page.c`. `canmount()` was reference-less and was removed by N11.7;
  `devmasked()` and `devmask()` reject
  negative/out-of-range indices, so `RFNOMNT` cannot index `devmask[-1]` and
  no new authority is granted. `Pgrp` no longer carries
  `mntorder`/`mntordertail`/`mnthash`, and `pgrpcpy()` copies only the device
  mask. `chan.c` no longer defines or traverses
  `cmount`/`cunmount`/`findmount`/`domount`/`undomount`/`createdir`/
  `newmhead`; `sysfile.c` no longer has `unionread` or the directory-union
  rewrite path; `/proc/N/ns` is an empty file.
- **Inert mount state deleted (E12.6).** E12.6 removed `Mount`/`Mhead`/
  `Mntrah`/`Mntrpc`/`Mnt` types and incomplete tags, `putmhead`,
  `mntattach`/`mntauth`/`mntversion`/`muxclose` prototypes, the `Chan.umh`/
  `umqlock`/`mux`/`mchan`/`mqid` fields, `Path.mtpt`/`mlen`/`malen` mount
  history, and `cclose`'s 9P `L'M'` deferred-close branch. `Path.s`/`len`/
  `alen`/`ref` and ordinary `namec`/`walk`/`pathlast`/`fd2path` remain. The
  generic close queue was retained while `ccloseq` was used by `segment.c`;
  N11.8.2 removed the `pgrp.c` force-close use and the now-uncalled `cclose`,
  and N11.8.3 removed `ccloseq` with the dead image-close path.
  `Chan.mcp`/`Mntcache` were retained at
  E12.6 but removed by N11.7; `Chan.nrock`/`mrock` and all live fd/device
  fields are retained.
  `devmnt.c` and `devshr.c` remain unlinked historical sources; they now
  reference removed symbols and do not compile against current headers.
- **Pathless pipe creation and identity (E12.6/E12.7/E12.9/E12.10).** `syspipe` no
  longer calls `namec("#|")`, `cclone`, or `walk("data"/"data1")`. It
  validates `devno('|')` and the inherited `Pgrp` device mask
  (`Ebadsharp`/`Enoattach`), then uses `pipechanpair()` in `devpipe.c` to
  allocate the two existing `Pipe` queues and two data-endpoint `Chan`s
  directly. E12.7 removed the synthetic `Path.s = "#|/data"`/`"#|/data1"`
  construction from this direct factory: direct endpoints now have
  `Chan.path == nil`, identified only by `NETQID`/`Qid`, endpoint index, `aux`
  and `Pipe.ref`/`qref`. E12.9 replaced GEFS-local `mkpipe`/`pipeend` with
  the same helper after repeating `devno('|')`/`devmasked(up->pgrp, t)`; GEFS
  no longer creates a `#|` root `Chan` or walks `data`/`data1`. Each successful
  output owns one opened endpoint, while `Pipe.ref`/`qref` stay owned by
  `pipechanpair`; on failure its local `Chan`/queue unwind runs and GEFS
  outputs are not assigned. E12.10 then moved `mkpipe`/`gefspost` into
  unlinked `gefs/legacyserve.c`, so those definitions are source-retained but
  not canonical archive or link members. Legacy `pipeattach`/`pipewalk` still
  produce real path metadata for direct `#device` callers, but no canonical
  GEFS pipe path uses them. `sysfd2path` fails explicitly with
  `"fd2path: channel has no path"` for a pathless fd and closes its
  `fdtochan` reference; `/proc/N/fd` uses `chanpath()` so nil paths are
  diagnostic text rather than a dereference. The helper adds no Device, OID,
  namespace or generic pathless API. Canonical `syspipe` was compiled then;
  N11.8.1 removed this `pipechanpair`/`pipeendpoint` factory from canonical
  amd64. GEFS `gefsserve` is not linked, and the boot smoke exercises
  neither.
- **`namec`/`walk` mount-mode cleanup (E12.7).** The unreachable `Abind`,
  `Amount` and `Aunmount` `namec` branches, the inner `Aunmount` open path
  and the `Aunmount` device-mask exemption were removed. Every `#device`
  attach validates `devno()` and `devmasked(up->pgrp, t)`; unknown access
  modes now fail `Ebadarg` instead of panicking. `walk()` lost its unused
  `nomount` argument; its only canonical callers are in `chan.c`. Live
  `namec` modes remain `Aopen`/`Aaccess`/`Acreate`/`Aremove` (`legacypath.c`,
  `legacyexec.c`, `sysfile.c` access checks, `rebootcmd.c`, and `kern.c`'s
  GEFS raw/halted branches). E12.9 removed the `devenv.c` and GEFS `#|`
  callers; E12.10 moved `gefspost`'s srv create to unlinked
  `legacyserve.c`. `Atodir` remains only as a retained enum/handling case
  with no compiled caller. The retained `portdat.h` enum values
  `Abind`/`Amount`/`Aunmount` are unreachable but harmless.
- **Legacy exec/path units separated (E12.8).** `sysexec` now lives in
  `port/legacyexec.c` and still obtains the executable and optional `#!`
  interpreter via `namec(..., Aopen, OEXEC, ...)`, using file-backed
  `attachimage`. `execo`, `segload`, `execargcopy` and the native O path stay
  in `sysproc.c` and use no `Chan`/name lookup. `execparse` and `execcommit`
  are now shared kernel-global helpers. `sysopen`, `syscreate` and
  `sysremove` moved intact to `port/legacypath.c`; `sysfile.c` retains the
  descriptor/IPC core (`newfd`/`newfd2`, `fdtochan`, `fdclose`, `openmode`,
  `syspipe`, `sysdup`, read/write, fd stat and device masks). Both units were
  linked in canonical `pc64`; E12.8 removed nothing. (N11.8.1 later removed
  `newfd2` and `openmode`; N11.8.2 then removed `newfd`, `fdtochan`,
  `fdclose` and the FD success paths; the syscall slots, `syspipe` and the
  `sysdup` failure path remain.) Their runtime behavior
  was not exercised by the smoke gate.
- **GEFS pipe and kernel Egrp call sites migrated (E12.9).** GEFS-local
  `mkpipe` used `pipechanpair()` after checking `devno('|')` and
  `devmasked(up->pgrp, t)`; `pipeend` and both `#|` `walk` calls were removed.
  E12.10 moved that historical `mkpipe`/`gefspost`/`gefsserve` code to
  unlinked legacy sources; G1 deleted those sources. The live native engine
  has no GEFS `namec` use: `gflushdev` uses the bounded block callback and
  `gefshalted` only records the halted/read-only state. `gefspost`'s srv
  create is no longer present. Kernel
  `ksetenv` now writes the selected `Egrp` through a
  driver-local helper using `envhash`/`lookupname`, the existing write lock,
  slot/hash/`PATH`/version rules and `envrealloc` accounting. It does not
  build `#e[c]/name`, call `namec`, or use a `Chan`; existing values are
  replaced with truncate semantics and new values use the old `envcreate`
  insertion rules. Name validation, `Maxvalsize`/`Maxenvsize`, device
  registration/mask checks and the old `#ec` `iseve`/shared-Egrp policy are
  retained, with missing `up`/`pgrp`/`egrp` failing closed. The legacy
  `#e`/`#ec` `envattach`/`envwalk`/`envopen`/`envcreate`/`envread`/`envwrite`/
  `envdevtab` exports remain for other callers. The compiled `ksetenv`
  callers are `bootargs.c` (`setconfenv`, no `pc64` caller), `rebootcmd.c`
  and `pc/screen.c`; none was exercised by the canonical smoke.
- **Dormant GEFS 9P/FS entry units excluded (E12.10).** `mkpipe`, `gefspost`
  and `gefsserve` moved from `gefs/kern.c` to new `gefs/legacyserve.c`;
  `loadfs` moved from `gefs/load.c` to new `gefs/legacyload.c`. Both files
  stay in the source tree as historical units, but canonical `pc64` filters
  them from its `GEFS` object list before compilation and archive creation.
  The final archive is created from exactly 18 members:
  `g_avl, g_blk, g_cache, g_check, g_cons, g_ctl, g_dump, g_error, g_fs,
  g_hash, g_kern, g_load, g_o, g_obj, g_pack, g_snap, g_tree, g_user`;
  `g_legacyserve` and `g_legacyload` are absent. The mkfile also does
  `rm -f gefs.a6` before `ar vu`, so an incremental rebuild cannot retain an
  obsolete excluded member.
  Source-level `gefsserve` has no caller; `loadfs`'s only caller was
  `gefsserve`. `newgfd` and `launch` remain in `kern.c` because live
  `geopen0`/`gestart` use them; they now have external linkage and
  `legacyserve.c` declares them locally. `gfns.h` retains the harmless
  historical `loadfs` declaration; a declaration is not a caller and no dummy
  implementation was added. The live native chain
  `sysovolopen -> ovolopenblk -> oopenblk -> geopenblk -> loadge -> gestart`
  is unchanged, as are GE object/arena/recovery/B-tree functions and
  `gefsshutdown`. `gefs/fs.c` is still compiled and linked: it mixes native
  sync/mutator/sweeper code with 9P `Conn`/`Fid` handlers, so E12.10 does not
  claim GEFS 9P or `Chan` dependencies are gone. The canonical smoke neither
  compiles nor executes `legacyserve.c`/`legacyload.c`.
- **First GEFS 9P and DMOBJ read separation (E12.11).** `fshangup`,
  `respond`, `readmsg`, `fsversion`, `fsflush` and `runfs` moved from
  `gefs/fs.c` to new `gefs/legacy9p.c`; `fsauth` and `fsclunk` remain in
  `fs.c` with external linkage and local declarations in the moved unit.
  `newconn` and `putconn` remain in `fs.c`: moving them would expose
  `clunkfid`, `putfid`, dent/mount teardown and `chsend(fs->admchan, ...)`,
  so Agent A stopped at this documented PARTIAL seam rather than globalizing
  a large static set. `emit` and `objread` moved from `gefs/obj.c` to new
  `gefs/legacyobjread.c`. New private `gefs/objint.h` contains the single
  canonical `Ohdr`/`Oent`/`Otx` layouts, the read-state enum, shared `Obj*`
  size constants and prototypes for `gettx`/`txroot`/`gethdr`/`keyver`; those
  helpers became external only because the moved real reader uses them.
  `objwrite`, `objclunk`, `objabortfid`, `objpending`, `objresume`,
  `objsweep` and all native `ospace*`/`onative*` code remain in `obj.c`.
  The E12.11 archive is created from 20 members, including
  `g_legacy9p` and `g_legacyobjread`; `g_fs`, `g_obj`, `g_kern`, `g_load`
  and `g_o` remain. Nothing new is excluded. At E12.11 the canonical boot
  started `runmutate`/`runread`/`runsweep`/`runtasks`/`runsync` but not
  `runfs`; it did not exercise DMOBJ streaming or the moved `objread` path.
  E12.12 later stopped the native-O `runmutate`/`runread` launches.
- **Native-O worker gate and DMOBJ Fid close separation (E12.12).** In
  `gefs/kern.c:gestart`, `Ovolflag` native O now sets `wrchan = nil`,
  `rdchan = nil`, `nreaders = 0` and does not launch `runmutate`/`runread` or
  count them in `nworker`. It still allocates `admchan`, initializes
  `syncq[]`, assigns arena sync queues and launches `runsweep` (id 1),
  `runtasks` and `runsync` (id 2). Non-O FS keeps the prior reader count,
  channel sizes, launch order and epoch IDs. Source proof: `wrchan` has only
  `legacy9p.c:runfs` producers and `runmutate` consumer; `rdchan[]` has only
  `runfs` producers and `runread` consumers; `runfs` is started only by
  excluded `gefsserve`. `ctl.c`/`cons.c`/`putconn` feed `admchan`, not these
  request queues.
  In `fs.c:runsweep`, the one-shot first-`AOsync` `objresume` call is gated
  to non-O volumes. Native O therefore avoids `getmount("main")` and DMOBJ
  pending-mark recovery but retains its normal `AOsync`/`gesync`, read-only,
  error-stack and `AOhalt` handling; native `begin`/`W>N` recovery is
  unchanged. `objclunk`, `objpending` and `objabortfid` moved from `obj.c`
  to new `legacyobjctl.c`; `objabortfid` uses the narrow `objaborttx`
  pass-through to the unchanged private `abort1`. `objwrite`, `objresume`,
  `objsweep`, `objread` and all native `ospace*` code remain in place.
  The E12.12 archive has 21 members, including `g_legacy9p`,
  `g_legacyobjctl` and `g_legacyobjread`; no unit is newly excluded.
- **GEFS runfs exclusion and DMOBJ writer isolation (E12.13).** `respond`
  and `fshangup` moved unchanged from `legacy9p.c` to new
  `legacyreply.c`. `respond` remains global via `gfns.h` because `fs.c`,
  `runsweep` and `ctl.c` still call it; `fshangup` is globally defined in
  `legacyreply.c` with a private prototype in `legacy9p.c`. `respond`
  refcount (`putconn`), `wrlk` serialization, flush-table tag lock release,
  hangup handling and message free order are preserved.
  Canonical `pc64` now excludes `legacy9p.c` in addition to
  `legacyserve.c`/`legacyload.c`. Its private `readmsg`/`fsversion`/
  `fsflush`/`runfs` have no canonical caller: `runfs`'s only caller is
  excluded `gefsserve`. The file remains in the source tree for a historical build that
  also compiles `legacyreply.c`.
  `objwrite` moved from `obj.c` to new `legacyobjwrite.c`. The transaction
  helpers were renamed to private `objtxbegin`/`objtxputchunk`/`objtxaddent`/
  `objtxcommit`/`objtxabort`/`objtxflush`/`objtxend` and declared only in
  `objint.h`; `Objtxmax` has one shared definition there. The parser body,
  record protocol, transaction limits and native `ospace*` behavior are
  otherwise unchanged. `objresume`, `objsweep`, `objaborttx`,
  `legacyobjread.c`, `legacyobjctl.c`, native OID/version/transaction code and
  direct native recovery remain.
  The E12.13 archive has 22 members: `g_fs`, `g_obj`, `g_kern`, `g_load`,
  `g_o`, `g_legacyreply`, `g_legacyobjwrite`, `g_legacyobjread` and
  `g_legacyobjctl` are present; `g_legacy9p`, `g_legacyserve` and
  `g_legacyload` are absent. `fs.c` still compiles its `Fid`/`Conn` handlers,
  `newconn`/`putconn`, `runmutate`/`runread` definitions and administrative
  sweep, so this is not a 9P-free or filesystem-free claim.
- **Conn lifecycle and DMOBJ recovery separation (E12.14).** `newconn` and
  `putconn` moved to new `gefs/legacyconn.c`. The exact `fidtab[]` drain loop
  remains in linked `fs.c` as `legacyconnfidcleanup(Conn*)`: bucket lock,
  temporary fid ref, `wlock`, `clunkfid`, `wunlock`, `putfid`, then deferred
  `Amsg` to `admchan`. `putconn` preserves final-ref checking, `connlk`/
  `fs->conns`, fd close order, wbuf free, helper call and final `free(c)`.
  `cons.c` still reads `fs->conns`; no Conn construction remains in `fs.c`.
  `objresume`/`objsweep` and legacy-only `txdrain`/`drainrange`/`dropall`
  moved to new `gefs/legacyobjrecover.c`. Shared `sweep`/`addmsg` became
  private `objtxsweep`/`objtxaddmsg` with unchanged bodies and call sites;
  `Objyield` has one shared private definition in `objint.h`. Native `obj.c`
  no longer directly references `objpending`, `objresume`, `objsweep` or
  `getmount`; `legacyobjrecover.c` reaches `objpending` through `gfns.h` and
  `getmount` through `fs.c`. Native O's `runsweep` still skips legacy
  `objresume`; `AOsync`/`AOhalt`/`gesync`, epochs and native `W>N` recovery
  are unchanged. The E12.14 archive has 24 members, including
  `g_legacyconn` and `g_legacyobjrecover`; `g_legacy9p`,
  `g_legacyserve` and `g_legacyload` remain absent. All legacy object units
  remain linked because `fs.c` still dispatches QTOBJ and administrative
  sweep.
- **Worker and Fid transaction separation (E12.15).** `runmutate`/`runread`
  moved from `fs.c` to new `legacyfsworkers.c`, preserving `chrecv`, `mutlk`,
  epoch/`epochclean`, `snap.dirty`, `waserror`/`poperror`, deferred `Amsg`
  `admchan` delivery and handler semantics. `fs.c` keeps `migrateusers`, the
  `fs*` handlers and exactly two linked dispatch seams:
  `legacyfsmutate(Fmsg*, int, Amsg**)` and `legacyfsread(Fmsg*, int)`.
  `rerror` moved beside `respond`/`fshangup` in linked `legacyreply.c`; the
  external prototype is used by `fs.c` handlers and `runsweep`.
  `gettx(Fid*)` moved from `obj.c` to new `legacyobjtx.c`; its only callers are
  `legacyobjread.c`/`legacyobjwrite.c`. The `objaborttx` wrapper was removed;
  `legacyobjctl.c` calls `objtxabort` directly. Native `obj.c` now has no Fid
  constructor or wrapper, while `txroot`/`txupsert`/`Otx.mnt` conditional
  support remains for the linked legacy engine.
  Native O still does not launch `runmutate`/`runread`; non-O `gestart` still
  references them. `runsweep`/`runtasks`/`runsync`, `admchan`, epochs,
  native sync and `gesync` are unchanged. The E12.15 archive has 26 members,
  including `g_legacyfsworkers` and `g_legacyobjtx`; `g_legacy9p`,
  `g_legacyserve` and `g_legacyload` remain absent. The moved worker and
  Fid-transaction runtime is not exercised by the canonical O smoke.
- **Request/Fid ownership split and O-only startup (E12.16).** All legacy 9P
  request handlers, `legacyfsmutate`/`legacyfsread`,
  `legacyconnfidcleanup`, and the supporting Fid/auth/Dent helpers moved from
  `fs.c` to new `legacyfsrequests.c`. `fs.c` retains native
  `sync`/`gesync`/`runsweep`/`runtasks`, channels/epochs, `migrateusers`,
  `freeamsg` and shared tree/Mount helpers used by the native sweeper.
  `gestart` now rejects non-`Ovolflag` volumes before worker allocation and
  starts only `runsweep`/`runtasks`/`runsync`; `wrchan`/`rdchan` remain nil.
  `legacyfsworkers.c` stays in the source tree but is excluded from canonical `pc64`
  because source proof shows no compiled caller of `runmutate`/`runread`.
  The E12.16 archive has 26 members, including `g_legacyfsrequests`, and
  excludes `g_legacyfsworkers`, `g_legacy9p`, `g_legacyserve` and
  `g_legacyload`. `fs.c:runsweep` still links `rerror/respond`, `freeamsg`,
  `objresume/objsweep` and administrative cases, so the reply/DMOBJ cluster
  is not removed. The canonical O smoke cannot validate the moved request/Fid
  code or any historical non-O worker runtime.
- **Native sweep and shutdown isolation (E12.17).** `fs.c:runsweep` now reads
  `Osweepmsg` from `fs->osweepchan` and dispatches only `OsweepSync` and
  `OsweepHalt`; the message is freed on normal, error and unknown-op paths.
  The real sync body (oldhd, log compression, epochs, `mutlk`/`synclk`, arena
  loops, old-head freeing, `fs->snap.dirty`, `rdonly`, `Syncout`) and the halt
  three-epoch/`sync`/`gefshalted` path are unchanged. `runtasks` enqueues
  `OsweepSync`; `kern.c:gefsshutdown` enqueues `OsweepHalt`; `gestart`
  creates `osweepchan` and sets `admchan = nil` on canonical O. Native
  startup/sweep/shutdown contain no `Amsg`, `Fmsg`, `Fid`, `rerror/respond`,
  `objresume/objsweep` or `clunkdent` reference. `freeamsg` and `migrateusers`
  moved to `legacyfsrequests.c`; `walk1` stayed in `fs.c` because linked
  `user.c` calls it. `snapfs`/`sweeptree`/`setconf` moved to new
  `legacyfsadmin.c`; `snapmsg`, `freeblks` and the inline
  `AOsnap`/`AOrclose`/`AOclear`/`AOsetcfg`/`AOclrcfg`/`AOobj*` sweep cases
  were removed. The E12.17 archive has 27 members, including
  `g_legacyfsadmin`, and still excludes `g_legacyfsworkers`, `g_legacy9p`,
  `g_legacyserve` and `g_legacyload`. The dormant legacy admin/request/object
  runtime is not exercised by the O smoke.
- **Legacy GEFS adapter archive/link cut (E12.18).** Canonical `pc64` now
  excludes `ctl.c`, `cons.c`, `legacyfsadmin.c`, `legacyfsrequests.c`,
  `legacyreply.c`, `legacyconn.c`, `legacyobjread.c`, `legacyobjwrite.c`,
  `legacyobjctl.c`, `legacyobjrecover.c` and `legacyobjtx.c` from the GEFS
  archive, in addition to `legacyserve.c`, `legacyload.c`, `legacy9p.c` and
  `legacyfsworkers.c`. The measured archive has exactly 16 members:
  `g_avl`, `g_blk`, `g_cache`, `g_check`, `g_dump`, `g_error`, `g_fs`,
  `g_hash`, `g_kern`, `g_load`, `g_o`, `g_obj`, `g_pack`, `g_snap`,
  `g_tree`, `g_user`. `g_pack`/`g_user` stay because native directory packing
  calls `uid2user` and `user.c:loadusers` calls `walk1`. Final-kernel
  symbol-string inspection found no inherited `respond/rerror/newconn/
  putconn/legacyconnfidcleanup/gettx/objread/objwrite/objsweep/objresume/
  writectl/readstatus/runcons` symbols. Native `struct Otx` dropped the
  legacy `Amsg *abort` field; `objtxbegin` no longer preallocates it and
  `ospaceclose` no longer frees it. `objtxabort`, Kobj N/K/W/pin, OID/version
  state and Ospace commit/abort remain. The excluded legacy sources still
  reference `tx->abort` and are intentionally outside the canonical build.
  `Otx.mnt`, `txroot`/`txupsert`, `fs.c:sync`'s `Mount` loop and the shared
  mount helpers remain for E12.19.
- **Native O Mount/type cleanup (E12.19).** `fs.c:sync` keeps the barrier,
  `mutlk`/epoch, `dlsync`, arena/log packing, superblock and `wrwait` order
  but no longer walks `fs->mounts`. The mount-only `upsert`, `drain`,
  `loadautos`, `mountlookup`, `getmount` and `clunkmount` helpers are gone.
  `snap.c:delsnap` keeps persistent label/ref/pred/succ updates,
  `reclaimblocks` and return behavior while dropping the in-memory mount-edge
  loop; `opentree` keeps disk deserialization and drops only the mount-tree
  reuse fast path. `Otx` is tree-only: `mnt` removed, `txroot` returns
  `tx->tree`, `txupsert` always applies the native `rdonly` guard and
  `btupsert(tx->tree,...)`, and `ospacetx` no longer sets `mnt`. Removed from
  `gdat.h`: `Amsg`, `Fmsg`, `Fid`, `Conn`, `Dent`, `Cron`, `Mount`, the `AO*`
  and `DFmnt` enum values, and `Gefs.mountlk/mounts/snapmnt/connlk/conns/
  wrchan/admchan/rdchan/nreaders`. Removed from `gfns.h`: the excluded-only
  mount/9P/DMOBJ/runfs prototypes. Removed from `kern.c`: obsolete field
  assignments; `blk.c` dropped the orphaned `DFmnt` case. `Chan`,
  `Osweepmsg`/`osweepchan`, `Gefs.snap`, Arena/Dlist/Tree/Scan/Syncq,
  `nworker`, `nprocs`, `walk1`, `user.c`, `pack.c` and all on-disk formats
  remain. `gefs.h` still carries the now-unused `Kmount`/`Mount` alias.
- **Final directory-metadata separation and E12 closure (E12.20).** `pack.c`
  retains only the on-disk GE/O packer (`packsb`/`unpacksb`,
  `packarena`/`unpackarena`, `packbp`/`unpackbp`, `packtree`/`unpacktree`,
  `packsnap`/`packlbl`, `kv2dlist`/`dlist2kv`, `tree2kv`/`retag2kv`/
  `lbl2kv`); the old directory codecs (`unpackstr`, `packstr`, `dir2kv`,
  `packdkey`, `unpackdkey`, `packsuper`, `packdval`, `kv2dir`,
  `dir2statbuf`, `kv2statbuf`, `kv2qid`, `fillxdir`) and `fs.c:walk1` are
  removed. `tree.c` no longer implements `Owstat`/`statupdate`, so an
  unexpected operation reaches `apply()`'s existing fail-closed `fatal`;
  `dump.c` renders Kent as a raw hex legacy diagnostic. `gdat.h` drops
  `Xdir`, `User`, `Gefs.userlk/users/nusers/nprocs` and the dead
  `Xdirsz/Wstatmax/Nfidtab/Ndtab/Max9p/Maxuname/Maxtag` constants;
  `gfns.h` drops the directory/user prototypes; `gefs.h` drops
  `Kmount`/`Mount` and `DMOBJ`/`QTOBJ`. `user.c` is excluded from the
  canonical archive. Persistent key/opcode enum values, superblock/Tree/log/
  arena/object layouts, Kobj N/K/W/pin, OID/version and Ospace behavior are
  unchanged. The E12.20 archive has exactly 15 members; E12 is closed for the
  canonical O-only claim.
- **Remaining linked dependencies.** `Chan`, `namec`, `walk`, `Fgrp`
  descriptors, `Rgrp` rendezvous, `Egrp` values/driver exports and the 13
  registered `Dev`s remain; `devmnt` no longer links. `namec`/`walk` remain
  for direct `#` attach, pathname-based exec (`legacyexec.c`),
  open/create/remove (`legacypath.c`), stat metadata paths and `kern.c`'s
  GEFS raw/halted branches. A later explicitly approved stage can decide
  whether to unlink `legacyexec.c`/`legacypath.c`, isolate the remaining
  GEFS raw/halted service, or replace their byte-source/lookup with existing
  native operations. The exec seam is executable/interpreter byte
  acquisition, separate from `execparse`, segment/stack setup,
  `execcommit()` and `execregs()`.

## External Chan audit

A project-owner audit artifact, `Plan2001-O_Chan-riippuvuudet(1).txt`, is an
external lexical/structural scan, **not** the complete inventory and not
current HEAD:

- Pinned source commit: `19de707bc6f88d259d4cc1a75179b69aec7393cf`.
- Target: amd64 `pc64`.
- Artifact: 2,928,946 bytes / 41,191 lines; SHA-256
  `7b9680cd2523c8e16562feedd27478e733860db44da362d0d6abfbd593f5db93`.
- Reported totals: 207 C/header files, 229 source files, 3,447 functions, 151
  active C files, 29 `Dev` registrations, 27 devices with local trees, 2,895
  dependency occurrences in 620 functions, 16,248 resolved direct call sites,
  431 indirect call sites and 1,117 macro call sites.

Its limitations matter: it is not a typed or preprocessed C AST; indirect and
macro callbacks remain unresolved; preprocessor branches are retained; build
inclusion is not linker proof; token and call counts overlap. The GEFS-local
`Chan`, `Mount` and `Path` macro aliases are not the kernel types (`Kchan`
denotes the latter in GEFS code). Revalidate source locations on current HEAD.
A detailed snapshot, if needed for automatic transformation, belongs outside
auto-read documentation with its exact bytes and SHA-256; a fresh HEAD scan is
preferable to treating the older snapshot as current ground truth.

## Historical generated analysis

The source-level operation-map analyser and its test were removed from the
canonical tree in N9.8. Their generated outputs under the ignored `build/`
tree are historical evidence only, not source contracts. Do not reintroduce
them as a repository test or build prerequisite.

The E11.5/E12.0 build-source inventories are ignored working evidence under
`build/verification/e115/` and `build/verification/e120/` (TSV plus summary);
they are regenerable from the canonical compile log, not tracked contracts.
E12.1 evidence is under `build/verification/e121/` (`references.tsv`,
`gates.txt`, `slash-blocker.txt` and gate logs). E12.2–E12.20 acceptance
evidence is historical `build/amd64/session.log`; the compile log also shows
`rm -f gefs.a6`, the final 15-member `ar vu` list
(`g_avl`, `g_blk`, `g_cache`, `g_check`, `g_dump`, `g_error`, `g_fs`,
`g_hash`, `g_kern`, `g_load`, `g_o`, `g_obj`, `g_pack`, `g_snap`, `g_tree`)
and the absence of `g_user`, `g_ctl`, `g_cons`, `g_legacyfsadmin`,
`g_legacyfsrequests`, `g_legacyreply`, `g_legacyconn`,
`g_legacyobjread`, `g_legacyobjwrite`, `g_legacyobjctl`,
`g_legacyobjrecover`, `g_legacyobjtx`, `g_legacyfsworkers`, `g_legacy9p`,
`g_legacyserve` and `g_legacyload`. The gates cover only the canonical
`oboot=server` build and boot, not non-O startup, the excluded
request/Fid/admin/object/user runtime, Conn/Fid teardown, legacy
`EXEC`/`OPEN`/`CREATE`/`REMOVE`, dormant `gefsserve`/`loadfs` or their
raw/srv `namec` helpers, excluded `runfs` 9P serving, DMOBJ
streaming/close/write/recovery, the excluded `objread`, `gettx(Fid*)` or
`objclunk`/`objabortfid` paths, `syspipe`/GEFS pipe I/O, `ksetenv`/`#e`
driver behavior, Egrp rfork/concurrency, `fd2path`/`/proc/N/fd`, direct
`#device` behavior, old syscalls, power-loss durability, memory-exhaustion,
probe or out-of-scope configuration behavior. E12.20's kernel SHA-256 is
`a706e5d27ec02948f3f3e764ababd802e3440ec1d359bf0673277897da4932ee`.

## Representative chains and responsibilities

The following source-checked examples show why a symbol's presence is not a
runtime claim and why removal must preserve non-path behaviour.

1. **GE/O volume discovery: native-only after G1.** `sysovolopen`
   (`ge/storage/sysgefs.c`) accepts only `PARTUUID=...` and reaches
   `ovolopenblk` → `oblockopenpart` → `sdunititer` and native SD block access
   → `oopenblk`; the engine loads through the bounded native read/write/flush
   callback. This is the active `oboot=server` path. Other specifications are
   rejected; there is no `gevolume`, `namec`, `scandisks`, device path, root
   mount or `Chan` fallback, and the old `sys/src/9/gefs/` legacy sources, removed from the compact N10 tree are
   deleted. Any further work must preserve volume-spec parsing, GPT lookup,
   O-profile/version validation, GE instance start and the process-local O
   handle lifetime.
2. **SD partition dispatch.** `devsd` read/write calls `sdbio`, which checks
   partition bounds and media-change state, translates offsets into block
   numbers and calls `(*unit->dev->ifc->bio)`. A replacement must preserve
   partition offset/length and sector-size translation, removable-media locking,
   the raw/SCSI request paths and controller DMA/interrupt lifetime.
3. **Keyboard input.** `kbdread`/`kbdpoll` handle scancode reads through the
   8042 controller. Preserve the 8042 lock, interrupt versus polling mode,
   scancode queue ownership, LED writes and the `iseve` check in `kbdopen`.
4. **Console and UART.** `conswrite` reaches `putstrn0`, then the
   `screenputs` hook or `uartputs` and the `PhysUart.putc` callback. Preserve
   console ownership, `serialoq`/`kprintoq` queues, `kmesg` ring accounting,
   escape-sequence handling and the polling fallback used by `iprint`/panic.
5. **Ethernet transmit.** `etherwrite` treats non-data writes as control and
   frames data through `etheroq` -> `ethermux` -> queue write to the
   `Ether.transmit` callback. Preserve framing, MTU checks, bypass/bridge/
   promiscuous modes, queue ownership and kick semantics.
6. **Dynamic Qid and command dispatch.** `devsd` has no static `Dirtab` for its
   unit/partition tree; `sdgen` and read/write switch on packed `QID(dev, unit,
   part, type)` values. `devcons` and `devvga` also use `Cmdtab` control
   dispatch. Preserve the packed encoding, version checks, dynamic directory
   generation and the difference between local `Dev` dispatch and mounted 9P
   transport. Local dispatch is not 9P.
7. **Userspace `kbdfs` boundary.** The canonical kernel no longer embeds
   `/$objtype/bin/aux/kbdfs` in a `bootdir`; `kbdfs` is a userspace 9P console
   helper, not a kernel driver, and still builds as an ordinary program.
   Kernel endpoints are `#c` (`devcons`), `#b` (`devkbd`, pc64), `#t`
   (`devuart`), `screenputs` and the `PhysUart` callbacks. Removing the
   embedding did not remove those kernel endpoints.
8. **Remaining linked inherited services after G1 (historical).** At that
   point the canonical kernel registered 11 `Dev`s:
   `arch pnp env pipe proc rtc draw mouse vga uart kbd`. The later N11 layout
   retires the `dev` front doors; the current `pc64` configuration has no
   `dev` rows, and N11.8.5 removed the generated `devtab`, the shared
   `Dev`/`Chan`/`Walkqid` types and the `devno()` path from the canonical
   build. After N11.8.2 the process/fd core no longer contains FD success
   dispatches.
   USB is native (R4); GE/O has no Dev after G1. `devmnt` is neither compiled nor linked and its transport
   symbols are absent. The generic path/fd syscall machinery remains linked;
   `Pgrp` mount lists, Chan union traversal and inert mount types/fields are
   gone. `syspipe` remains linked, but after N11.8.1 it no longer creates
   `Chan` endpoints: it validates and returns `Ebadsharp`, while the native
   `PIPENEW` handle path created pathless queues without `Chan` (retired with numbers 71-74 in phase 3); historical
   GEFS `mkpipe` is only in unlinked `legacyserve.c`. The legacy FD syscalls
   keep their pre-FD checks but now return `Ebadfd` after N11.8.2. `fd2path`
   rejects pathless fds explicitly; legacy
   `#device` callers still have real paths. `legacypath.c`
   (`OPEN`/`CREATE`/`REMOVE`), `legacyexec.c` (`EXEC`), stat paths and
   `kern.c`'s GEFS raw/halted branches still use `namec`; `ksetenv` no longer
   does. The GEFS FS/9P/Fid/Conn/DMOBJ adapters, `ctl.c`, `cons.c`, `user.c`,
   the directory codecs and `walk1` are source-preserved but excluded from
   the canonical archive/link. `gefs/obj.c` keeps the native transaction
   engine; the legacy object, reply and Conn implementations are not linked.
   `gestart` is O-only; native O launches only `runsweep`/`runtasks`/
   `runsync` over `osweepchan`. Console, SD, Ethernet and IP have native
   cores; that does not make the whole kernel free of unrelated `Chan` or
   pathname code.

## E12 closure and post-E12 focus

**E12 is CLOSED** for the canonical O-only claim. The final canonical
`pc64` GEFS archive has 15 members: `g_avl`, `g_blk`, `g_cache`, `g_check`,
`g_dump`, `g_error`, `g_fs`, `g_hash`, `g_kern`, `g_load`, `g_o`, `g_obj`,
`g_pack`, `g_snap`, `g_tree`. Excluded source modules are `user.c`, `ctl.c`,
`cons.c`, `legacyfsadmin.c`, `legacyfsrequests.c`, `legacyreply.c`,
`legacyconn.c`, `legacyobjread/write/ctl/recover/tx.c`, `legacyserve.c`,
`legacyload.c`, `legacy9p.c` and `legacyfsworkers.c`.

Final native state:
- `fs.c` runs native `sync`/`gesync`/`runsweep`/`runtasks` over the private
  `Osweepmsg`/`osweepchan` queue. No `Gefs.mounts` traversal remains.
- `pack.c` keeps the on-disk GE/O encoders (`packsb`/`unpacksb`,
  `packarena`/`unpackarena`, `packbp`/`unpackbp`, `packtree`/`unpacktree`,
  `packsnap`/`packlbl`, `kv2dlist`/`dlist2kv`, `tree2kv`/`retag2kv`/
  `lbl2kv`); the directory codecs and `walk1` are gone.
- `obj.c` is tree-only: `Otx.mnt` is gone, `txroot` is `tx->tree`, and
  `txupsert` is the native `rdonly`/`btupsert` path. `objtx*`, Kobj N/K/W/pin
  and Ospace semantics remain.
- `tree.c` has no `Owstat`/`statupdate`; an unexpected op hits the existing
  fail-closed `fatal`. `dump.c` shows Kent as raw hex.
- `Amsg`, `Fmsg`, `Fid`, `Conn`, `Dent`, `Cron`, `Mount`, `Xdir`, `User`,
  `AO*`, `DFmnt`, old `Gefs` service fields and the `Kmount`/`Mount`,
  `DMOBJ`/`QTOBJ` macros are removed. `Chan`/`Osweepmsg`/`osweepchan` remain.

Persisted key/opcode numeric values, superblock/Tree/log/arena/object
layouts, Kobj N/K/W/pin, OID/version rules and Ospace commit/abort are
unchanged, so E12.19 O volumes remain readable.

**Not claimed:** a globally VFS-free 9front kernel, migration of every
`#device`, native per-object graph indexing, exhaustive regression or
power-loss proof, or a complete desktop. Post-E12
development should focus on native GE/O, Space, OID and API features rather
than further inherited-kernel archaeology.

**Required proof for any “no Chan” claim:** trace the actually called functions
in the measured path and account for `Chan`, `Qid`, `namec`, walk, `Dev`
dispatch, blocking/locks and authorization. An operation can be Chan-free
without the whole driver or kernel becoming Chan-free.

## Related contracts

- [KERNEL-API-ABI.md](KERNEL-API-ABI.md) defines the kernel API/ABI vocabulary
  and the migration procedure for new native operations.
- [O-FORMAT.md](O-FORMAT.md) is authoritative for the implemented GE/O format,
  native O calls and handle lifetime.
- [STATUS.md](STATUS.md) records what has actually been verified and what was
  not re-run.

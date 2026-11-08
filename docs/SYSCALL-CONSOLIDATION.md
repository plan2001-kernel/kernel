# Syscall consolidation (2026-11-08)

Source baseline: GitHub main `b558d1a97788bbc2694b4ce3c87440511a83514a`.
This branch changes the amd64 experimental ABI. Historical numbers stay
allocated and reject with `Ebadarg` when retired; they are not reused.
`runtime/abi/sys.h` marks retired numbers, and both kernel dispatch generation
and MicroPython stub generation read that one status source. A missing active
handler or an implemented reserved handler fails the build.

| Dispatch | Before | After |
| --- | ---: | ---: |
| Named numbers | 135 | 135 |
| Active | 62 | 33 |
| Reserved | 73 | 102 |

The 29 removed entries are `SYSR1`, `_ERRSTR`, `_FSESSION`, `FAUTH`,
`FVERSION`, `SEGBRK`, `SEGATTACH`, `SEGDETACH`, `SEGFREE`, `SEGFLUSH`,
`RENDEZVOUS`, `SEMACQUIRE`, `SEMRELEASE`, `TSEMACQUIRE`, `RTCGET`, `RTCSET`,
`OSPACEGET`, `OSPACEPUT`, `OSPACECOMMIT`, `OSPACECREATE`, `OBIND`, and
`USBOPEN` through `USBEPNAME` (eight entries). Their unused handlers,
semaphore bookkeeping, old USB handle table and legacy fd user bindings are
removed. The direct `OSPACEOPEN/CLOSE` handles remain for the native namespace
view and bootstrap; ordinary Space payload read/write/create/commit uses OBJ.
The direct `OEXECUTE` bootstrap and volume open remain separate operations.

The object router no longer accepts `ObjOpTimeNow=27`,
`ObjOpErrstrExchange=29`, `ObjOpGrant=9`, or `ObjOpCreatePersist=12`.
Time and error exchange use direct `_NSEC` and `ERRSTR`. Capability copies
use `OBJOPEN(ObjOpenHandle)`; persistent creation uses
`OBJCREATE(ObjKindGe)`. Numeric operation values remain assigned but fail as
unsupported. `obj_grant` and `obj_create_persist` are userland convenience
functions over the remaining operations. `space.hput/hcommit/hcreate` were
removed; the normal Space persistence path already uses OBJ.

USB enumeration, endpoint ownership and control now use the common Vhand table.
`plan9.obj_usb_open(2184)` requests `Vmeta|Vdelegate|Vcreate|Vcontrol` from
the trusted bootstrap process. The default open is metadata-only. A control
endpoint uses `plan9.obj_usb_ctl` over `ObjOpDeviceCtl`, whose USB branch
requires `Vcontrol`, bounds the in/out buffer to 4096 bytes, and calls the
same `usbcore_ioctl` provider. Common Device session clear-halt remains its
own operation on the same router, selected by Handle kind. `plan9.usb_*`
legacy bindings are removed; `plan9.obj_usb_*` is the public family.

The retained direct calls are small process-local operations (`BRK_`, sleep,
notes, rfork, exits, alarm, time, error) plus native volume/bootstrap,
environment and namespace operations. The five OBJ calls manage live resources.
The deprecated native TCP/UDP provider tags remain rejected, and the native
network stack remains disabled. `ENVGET/SET/DELETE/LIST` are distinct bounded
operations; `ON*` direct calls manage the persistent namespace view, while
`OBJBIND` manages Vhand bindings. These are remaining design boundaries,
not aliases to a single operation.

Verification on the agent's Debian 13 x86_64 executor: canonical GCC kernel,
MicroPython, EFI loader and host formatter builds passed; ABI/layout and link
checks passed. Disposable QEMU/TCG normal boot passed with GE/O commit and REPL.
The focused USB gate passed 70 sequential assertions, including four new
authorization/length negatives, after replacing legacy bindings with OBJ.
The raw Ethernet gate also passed 55 assertions with an external peer.
The retired-number REPL probe exercises all 29 removed syscall numbers and
four OBJ operations. Physical devices were not tested.

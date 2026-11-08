# Object ABI migration roadmap

> **Network status correction (2026-11-08):** the dated UDP/TCP provider
> descriptions below are migration history. T13 disabled native IPv6 startup
> and OBJ TCP/UDP opens; retained symbols are not a supported network service
> or a compatibility obligation. The current raw Device ABI is documented in
> [DEVICE-ABI](DEVICE-ABI.md). Future userspace IPv6 and externally provisioned
> ESP are PLANNED; [ESP-SA-BUNDLE](ESP-SA-BUNDLE.md) defines the target without
> adding a syscall/provider or proving an implemented protocol service.

**Status:** native Pipe/UART/KBD provider and USB metadata-service candidate,
2026-11-05 (task USB `2de71075e8c54f44afef60a4792857a9`, following the
Pipe/UART/KBD round `4900c68069db4b41aab89e4ebca79b3a`, the UDP6 round
`b91ee58f883e4f14a550e9b6157190e1` and the capability-fix round
`9b85e5c5208a4a3b87ec2f22e12443f7`).
The retired compatibility families are gone; the canonical **object** boundary
is the five universal operations. **Interpretation note (2026-11-06): the older
phrase “the canonical kernel boundary is exactly five operations/syscalls” is
superseded for the total syscall ABI.** The five OBJ operations are the general
object boundary, not the whole kernel interface. T7 restored direct
`BRK_(24)`, `SLEEP(17)`, `NOTIFY(28)`, `NOTED(29)`, `RFORK(19)`,
`OTERMINATE(93)` and `EXITS(8)` where an object round trip was not justified;
the current observed table is 135 named / 62 active / 73 reserved. See
[ROADMAP-T8-T16](ROADMAP-T8-T16.md). The historical counts below are evidence
for their rounds, not a target or quota. The Pipe/UART/KBD round moved native Pipe, UART and
PS/2 keyboard behind that boundary as `VkindPipe`, `VkindUart` and `VkindKbd`.
The 2026-11-05 retirement round now removes the `PIPENEW/READ/WRITE/CLOSE`,
`PIPEINHERIT`, `UART*` and `KBD*` handler symbols: their 16 numbers (71-85 and
100) are permanently RESERVED with `nil` dispatch entries. The USB metadata
round adds only the USB service object and its `usbcore_list()` snapshot; it
does not unify endpoint handles. UDP6 is an earlier round's provider. No
syscall was added or renumbered.
See [KERNEL-API-ABI](KERNEL-API-ABI.md), [OBJECT-MODEL](OBJECT-MODEL.md),
[O-FORMAT](O-FORMAT.md) and [STATUS](STATUS.md).

## Console IOWRITE retirement (2026-11-05, partial round)

This round retires the `IOWRITE` (62) compatibility entry point.  The native
console is now an object provider: `OBJOPEN(ObjOpenConsole)` returns a
process-local `VkindConsole` Handle carrying only `Vwrite`, authorized by the
same `Proc.iomask` (or an explicit stdout Pipe) that `IOWRITE` used, and
`OBJINVOKE(Write)` calls the shared kernel `iowritek()` writer.  MicroPython's
`p9console.c` opens the stdout and diagnostic Handles and uses `obj_write`;
it no longer references the `iowrite` stub (the generated reserved stub
remains for the number, with `nil` dispatch).  No number was renumbered or
added: the table is 135 named / 65 active / 70 reserved.

Verified: kernel build PASS (132/132 selected, 134/134 full closure, ABI
PASS, link PASS, 0 undefined symbols); MicroPython build PASS (246 units,
0 FAIL, no undefined symbols).  Disposable KVM QEMU PASS
(`build/qemu-o/console-obj5/`): the REPL printed `CONSOLE STILL OK` through
the OBJ console Handle, and `plan9.sysraw(62)` returned `OSError: bad arg in
system call` with no fatal note.  The earlier `console-obj2` run also printed
`IOWRITE RETIRED 42`.  This is an intermediate step only; the five-call
boundary is **not** reached: 61 other numbers remain active (process lifecycle,
memory, time, RTC/env, namespace, USB legacy, OSPACE*/OVOLOPEN/OEXECUTE/OSPAWN).

## Capability-fix candidate (2026-11-05)

The reviewed patch (SHA-256
`78886b5f019c82db6fe59b9a3a0c41e934706b6af1000fed20c59e80ccfcd68f`)
was applied to canonical snapshot `f3daebf4...` after correcting one
transported line split in `vobjcreatege`. No number was added or renumbered:
the table is 135 named / 87 active / 48 reserved, with `OBJ*` 136-140 active.

- **One binding entry.** `OBJBIND` no longer keeps a generic table plus a
  native mirror. It installs one pgrp/OContext entry containing the
  attenuated `Ohand` (for GE) and the `Vhand`. COPY/SHARE replace the role,
  EXCLUSIVE rejects an existing role, and REMOVE clears both at one
  linearization point. `OBJOPEN(ObjOpenBinding)` reads the same entry.
- **Revocation lineage.** Derived `Ohand` copies pin their origin and
  serialize admission/revocation on the origin gate. `oauthorize` and
  `nsreadable` reject a derived capability whose origin is revoked;
  `sysonmount` now performs the same origin check. Rights are always an
  intersection with the source, so open, copy, bind and grant cannot amplify.
- **CreatePersist.** The returned Handle is reserved and installed before
  `ospacecreate` publishes the initial bytes; the operation returns the OID
  and closes the Handle. A full Handle table therefore fails without a
  durable OID allocation or a leaked Handle. `ospacecreate` itself commits
  the initial bytes, as before.
- **ObjectFIFO.** A sent message records the sender's rights ceiling; the
  receiver's Handle gets that ceiling intersected with the subscription.
  Queue mutation is bounded, TEE/drop-oldest/revoke remain unchanged in
  semantics, and the receive path reserves the result Handle before dequeue
  so no fallible publication happens after removing a message. Subscription
  Handles are rejected by OPEN, BIND, GRANT and OSPAWN.
- **Lifetime fixes.** FIFO teardown now clears each subscription's `fifo`
  pointer while holding the FIFO lock; the previous omission left a dangling
  pointer for a still-live subscription Handle. `vobjcreatege` and
  `vobjopenbinding` fail cleanly on a missing Handle table. RFNOMNT blocks
  namespace extension but no longer blocks role removal.

Executed evidence is summarized in [STATUS](STATUS.md) and
[TEST-RESULTS.json](TEST-RESULTS.json); the local logs are under
`build/qemu-o/task-cap-*` and `build/qemu-o/task-tcp*`.

## Canonical boundary now

```text
object:  CREATE(136)  OPEN(137)  CLOSE(138)  BIND(139)  INVOKE(140)
libc:    obj_create  obj_open  obj_close  obj_bind  obj_invoke
         obj_read/write/receive/publish/subscribe/accept/grant/revoke/list
         obj_listen/connect/udp6_addr/udp6_open/udp6_sendto/udp6_recvfrom
```

This block lists the **object boundary only**. Direct process-local syscalls
restored by T7 (`BRK_`, `SLEEP`, `NOTIFY`, `NOTED`, `RFORK`, `OTERMINATE`,
`EXITS`) remain part of the total syscall ABI. UDP6 is an object provider, not a
second object system: `OBJOPEN
(ObjOpenUdp6)` binds one `VkindUdp6` Vobj, and `OBJINVOKE
(SendTo/RecvFrom)` transfers one complete datagram. The address lookup is
`OBJINVOKE(ObjOpNet6Addr=24)` with handle 0 and the same boot/permit gate;
`NET6ADDR/OPEN/RECVFROM/SENDTO/CLOSE` (116-120) are RESERVED and dispatch to
nothing.

`runtime/abi/obj.h` owns the versioned `ObjCall` request/response and the
provider/kind/operation/mode enums. `kernel/core/sysobj.c` validates and
routes; `kernel/core/sysvobj.c` holds the shared provider core;
`ge/storage/sysgefs.c` owns the persistent O Space and OID providers. The libc
adapters in `runtime/libc/port/oobject.c` add no kernel entry point:
read/write, publish/subscribe/receive and accept/connect are all userland
packing over `OBJINVOKE`.

## Retired numbers (reserved forever)

No number was renumbered or reused. A raw call to a reserved number finds a
`nil` `systab` entry and `sysreserved[number] == 1`; `dosyscall` returns
`Ebadarg` without posting a fatal note and without touching any old handler.

| Family | Numbers | State |
| --- | --- | --- |
| `TCP6LISTEN/CONNECT/ACCEPT/READ/WRITE/CLOSE` | 108-113 | RESERVED, no handler |
| `VOBJCREATE/READ/INFO/CLOSE` | 121-124 | RESERVED, no handler |
| `VOBJFIFOCREATE/SUB/SEND/RECV/STAT` | 125-129 | RESERVED, no handler |
| `VOBJGRANT/REVOKE/IO/LIST/OPEN/ACCEPT` | 130-135 | RESERVED, no handler |
| Pipe: `PIPENEW/PIPEREAD/PIPEWRITE/PIPECLOSE` | 71-74 | RESERVED, no handler |
| Keyboard: `KBDOPEN/KBDREAD/KBDCLOSE/KBDLEDS` | 75-78 | RESERVED, no handler |
| UART: `UARTCOUNT/UARTOPEN/UARTREAD/UARTWRITE/UARTIOCTL/UARTSTATUS/UARTCLOSE` | 79-85 | RESERVED, no handler |
| One-shot Pipe inheritance: `PIPEINHERIT` | 100 | RESERVED, no handler |
| UDP6: `NET6ADDR/OPEN/RECVFROM/SENDTO/CLOSE` | 116-120 | RESERVED, no handler |
| Process: `RFORK`, `OTERMINATE` | 19, 93 | ACTIVE direct historical calls (T7) |
| Process: `_WAIT`, `AWAIT`, `OSPAWN` | 36, 47, 92 | RESERVED, no handler; typed `ObjOpWait=35` / `ObjOpOspawn=33` |
| Pathname/FS: `BIND`, `CHDIR`, `EXEC`, `_MOUNT`, `OPEN`, `_STAT`, `CREATE`, `FD2PATH`, `REMOVE`, `_WSTAT`, `_FWSTAT`, `UNMOUNT`, `STAT`, `WSTAT`, `FWSTAT`, `MOUNT` | 2,3,7,13,14,18,22,23,25,26,27,35,42,44,45,46 | RESERVED, no handler |
| Always-failing FD: `CLOSE`, `DUP`, `_FSTAT`, `_READ`, `OSEEK`, `_WRITE`, `PIPE`, `SEEK`, `FSTAT`, `PREAD`, `PWRITE` | 4,5,11,15,16,20,21,39,43,50,51 | RESERVED, no handler |

`OVOLOPEN` (54), `OSPACEOPEN/GET/PUT/COMMIT/CLOSE/CREATE` (55-61),
`OEXECUTE` (60), `EXITS` (8) and the namespace/OQUERY families keep their
numbers and implementations. `RFORK` 19, `OTERMINATE` 93, `EXITS` 8,
`BRK_` 24, `SLEEP` 17, `NOTIFY` 28 and `NOTED` 29 are direct historical
calls again (T7); their former `ObjOp` variants are retired.
`ObjOpOspawn=33` and `ObjOpWait=35` remain the typed operations for the
still-reserved `OSPAWN` 92, `_WAIT` 36 and `AWAIT` 47. The canonical
MicroPython module keeps the old Python method names (`vobjcreate`,
`tcp6_listen`, `pipenew`, `uart_open`, `kbd_leds`, ...) only as adapters over
OBJ*; they do not call the retired numbers.

## Process lifecycle (T6/T7, 2026-11-06)

`RFORK(19)`, `OTERMINATE(93)` and `EXITS(8)` are direct historical calls
again (T7): `kernrfork`, `kernoterminate` and `pexit(status,1)` remain the one
shared core each. `_WAIT(36)`, `AWAIT(47)` and `OSPAWN(92)` remain reserved;
the typed operations are `ObjOpOspawn=33` and `ObjOpWait=35`, which call the
same `kernospawn`/`pwait` cores. `ObjOpOspawn` needs a `VkindSpace` Handle
with the `Vexecute` right; `Vexecute` maps to the existing Ohand `Oexecute`
gate and is never amplified. `ObjWait` is a bounded typed record that keeps
the exact status string. The former `ObjOpRfork=32`, `ObjOpOterminate=34` and
`ObjOpExit=36` providers are retired and reject a raw `OBJINVOKE`. See
[SYSCALL-SIMPLE-RESTORE](SYSCALL-SIMPLE-RESTORE.md) and the historical
[SYSCALL-T6-PROCESS](SYSCALL-T6-PROCESS.md).

## Pipe/UART/KBD retirement (phase 3, 2026-11-05)

The phase-3 task removes the 16 native compatibility entry points without
renumbering or adding a syscall. Two typed `OBJINVOKE` operations replace the
two behaviours that were not already provider operations:

- `ObjOpPipeInherit=22` is `OBJINVOKE` on a Pipe Handle with `arg0` carrying
  the legacy bits 1 (read) and 2 (write). The mapped `Vread`/`Vwrite` set
  must be a subset of the Handle's rights and arms exactly one next
  `RFORK(RFPROC)`. `vobjpipeprocfork()` still copies only armed endpoints,
  preserves slot and generation, and consumes the mark. `OSPAWN` keeps its
  own `pipes` list and never consumes this mark.
- `ObjOpUartCount=23` is the handleless discovery call (caller passes handle
  0) and returns `uartnports()`, the same index space accepted by
  `OBJOPEN(ObjOpenUart)`. It replaces the old `UARTCOUNT` without a Handle.
- `ObjOpKbdLeds=19` keeps its per-Handle `Vcontrol` form, and additionally
  accepts handle 0 as the eve-only global LED control that does not claim the
  PS/2 scan-code controller. That preserves the historical `KBDLEDS`
  authority while the per-Handle form remains available.

The old `sys*` handlers are deleted from `syspipe.c`, `syskbd.c` and
`sysuart.c`; those files keep only the RFORK/exec/exit, OSPAWN and bootstrap
output hooks. The `PIPENEW/READ/WRITE/CLOSE`, `PIPEINHERIT`, `UART*` and
`KBD*` numbers now emit `nil` `systab` entries and `sysreserved=1`, so a raw
call returns `Ebadarg` without posting a fatal note and without reaching any
old implementation. The internal `vobj*` and `pipecore`/`uartn*`/`kbd*` driver
interfaces are unchanged.

## Persistent GE/O through the five calls

`OBJOPEN(ObjOpenSpace)` applies the same bootstrap/volume authorization as
`OSPACEOPEN` and returns a process-local `VkindSpace` Handle. It opens a
bounded Space name, not a path.

`OBJCREATE(space=<Space Handle>, flags=ObjKindGe, data, len)` allocates a
never-before-committed persistent OID, publishes the initial bytes and commits
through the existing GE/O v1 transaction path. It is an explicit persistent
kind; `ObjKindBytes/Fifo` remain volatile and are never promoted or cloned.

`OBJOPEN(ObjOpenGeSpace, {space, reserved=0, oid})` reopens an existing OID
through the authorized Space Handle. `OBJINVOKE(Read/Write)` maps to the
existing `osreadhand`/`oswritehand` gate and O Space handle; `OBJINVOKE
(CreatePersist)` is the Space-handle allocation operation; `OBJINVOKE
(Commit)` commits the Handle's pending writes. Identity remains
`(SpaceID, OID)`; an OID or numeric Handle in another process grants nothing.

`OBJBIND` and the native O view now use one pgrp/OContext entry. A GE binding
stores both the attenuated `Ohand` and the `Vhand`; COPY and SHARE replace the
role at one linearization point, EXCLUSIVE rejects an existing role, and the
remove path clears the entry. There is no second mirror table and therefore no
partial-mirror rollback state. `OSPAWN` COPY/SHARE, attenuation and revocation
observe that same entry.

## Remaining legacy GE operations and why

`OVOLOPEN`, `OSPACEOPEN`, `OSPACEGET`, `OSPACEPUT`, `OSPACECREATE`,
`OSPACECOMMIT`, `OSPACECLOSE`, `OEXECUTE`, the native `OBIND/OLOOKUP/...`
namespace family and `OQUERY` are retained. They are still the tested
implementation of volume/space selection, the boot bootstrap, the native
namespace and the protected hashtag/relation queries. The OBJ* Space/GE path
routes through the same `Ohand`/`Ohtab` gates; it does not remove the native
calls until their complete behavior and security coverage is demonstrated.
`OBJOPEN(ObjOpenGe)` still accepts an existing native O Space Handle for
callers that already hold one. UDP6 is no longer on this legacy list: its
endpoint state and address lookup are both owned by the object core through
`ObjOpenUdp6`, `ObjOpNet6Addr`, `SendTo/RecvFrom` and `OBJCLOSE`.

T1 (2026-11-05) has already moved MicroPython's GE/O **read** paths to the
OBJ boundary: `p9space.c` uses `OBJOPEN(ObjOpenSpace)`,
`OBJOPEN(ObjOpenGeSpace)`, `OBJINVOKE(ObjOpRead)` and `OBJCLOSE`, with the
published O version in `ObjCall.aux`. `OSPACEGET` remains active only for the
`obootoid.elf` fallback (T10) and for other not-yet-migrated consumers;
`OSPACEPUT`/`OSPACECOMMIT`/`OSPACECREATE` and the legacy write Handle remain
for T2. The write and commit semantics were deliberately not changed.

## UDP6 provider (2026-11-05)

- `runtime/abi/obj.h` adds `ObjOpenUdp6=8`, `ObjOpSendTo=14`,
  `ObjOpRecvFrom=15`, `ObjOpNet6Addr=24`, `ObjUdp6SendReq`,
  `ObjUdp6RecvHdr`, `OBJ_NET6_ADDRLEN=16` and the shared 1232-byte payload
  bound. `runtime/abi/vobj.h` adds `VkindUdp6` and the internal `VopenUdp6`
  tag.
- `kernel/core/sysnet6.c` owns one UDP6 provider over `nativeudp6.c`, not a
  second endpoint table. `OBJOPEN` installs a `Vhand` with default rights
  `Vread|Vwrite|Vmeta|Vdelegate`; `Vread` is receive, `Vwrite` is send, and
  requesting any other bit is `Eperm`. The 8-endpoint per-process limit is
  unchanged. `ObjOpNet6Addr` copies the link-local address under the same
  `obootstrap`/`net6permit` gate without opening an endpoint or reserving a
  port.
- The former `NET6*` handlers (116-120) are retired: `nil` dispatch,
  `sysreserved=1`, no linked `sys*` symbol, raw call `Ebadarg`. Exec and
  process exit release UDP6 Handles through `vobjprocexit`; OSPAWN is the
  only way to delegate one, with `Vdelegate` and a non-growing rights subset.
- `runtime/libc/port/oobject.c` adds `obj_udp6_addr`, `obj_udp6_open`,
  `obj_udp6_open_rights`, `obj_udp6_sendto` and `obj_udp6_recvfrom`.
  MicroPython exposes `obj_udp6_addr`, `obj_open_udp6`,
  `obj_udp6_sendto` and `obj_udp6_recvfrom`; its historical `net6_*`
  names are userland adapters over the same four wrappers and `obj_close`.

## Verification in this round (phase 4, 2026-11-05)

- Canonical kernel build PASS (132/132 selected, 134/134 full closure, ABI
  PASS, link PASS, 0 undefined symbols; `build/gcc/9pc64.elf`, 1,149,936
  bytes, SHA-256
  `9f7a8219505bd17a937e3dc586a139073868fbd9c3afc20c2edfadd69ecffd39`).
  MicroPython build PASS (246 units, 0 FAIL, no undefined symbols;
  `build/gcc/micropython.elf`, 533,672 bytes, SHA-256
  `ef521c803e72ff1894b9f61e0cfcd7ffa3842c13e8db302483c91ead8732384c`).
- Static retirement check PASS (`build/qemu-o/phase4-static/check.txt`):
  the table is 135 named / 66 active / 69 reserved; all five NET6 names have
  `nil` dispatch and `sysreserved=1`; no `sysnet6*` handler symbol is linked;
  `vudp6addr` is linked; `modplan9.o` has no raw `net6*` relocation and
  references only `obj_udp6_addr/open/sendto/recvfrom` and `obj_close`.
- Guest/host QEMU PASS (`build/qemu-o/phase4-udp6c/`, REPL `PHASE4 PASS`,
  peer `UDP6 PEER PASS`): a real IPv6 peer received `NET6-PING` from guest
  port 4444 and `OBJ-PING` from port 4445; the guest received both replies
  with the peer address and source port 40000. `obj_udp6_addr` and the
  `net6_addr` adapter returned the same 16-byte link-local address. Read-only
  Handle send and `GRANT` amplification were denied (`Eperm`); a second close
  and a stale send returned `Ebadarg`; raw calls 116–120 each returned
  `Ebadarg`; no panic, bad-disk, flush or sync error. The peer script, REPL
  input and both logs are retained in that directory.
- Earlier retained evidence, not rerun this round: Object ABI self-test
  PASS 11/0 (`build/qemu-o/abi-selftest-final8/`), two-boot disposable QEMU
  persistence PASS (`build/qemu-o/ge-persist-final3/`), and the 1232-byte
  datagram boundary PASS (`build/qemu-o/task-udp6-big/`). The
  receive-buffer-too-small path remains implemented and reviewed but not
  executed, because the MicroPython binding always supplies full capacity.

## Native Pipe, UART, KBD and USB service providers (2026-11-05)

This is the canonical contract for the three native byte/control providers
and the USB metadata service. They share the one process-local `Vhand` table,
rights model, generation checks and `vobjprocexit` teardown. There is no
`Pipehtab`, `Uarthtab` or `Kbdhtab`, and no parallel user-space Handle table.
The USB **endpoint** handles are different: the legacy 16-slot `Usbhtab` in
`kernel/core/sysusb.c` remains for `USBEPOPEN`/`USBREAD`/`USBWRITE` and is not
merged into `Vhand` in this step.

### ABI surface

| Item | Value / type | Meaning |
| --- | --- | --- |
| `ObjKindPipe` | 3 | `OBJCREATE` one blocking byte-stream pair |
| `VkindPipe`, `VkindUart`, `VkindKbd`, `VkindUsb`, `VkindUsbEp` | 6, 7, 8, 9, 10 | Handle identity kinds |
| `ObjOpenUart`, `ObjOpenKbd`, `ObjOpenUsb` | 10, 11, 12 | `OBJOPEN` providers |
| `ObjOpenPipe` | 9 | informational provider tag; not an open provider |
| `ObjOpPipePeer` | 16 | install and return the second Pipe end |
| `ObjOpUartCtl`, `ObjOpUartStatus` | 17, 18 | typed UART control and status |
| `ObjOpKbdLeds` | 19 | privileged keyboard LED update |
| `ObjOpUsbList` | 20 | bounded USB endpoint metadata snapshot |
| `ObjOpUsbEpOpen` | 21 | open one USB endpoint through a `Vcreate` service Handle |
| `ObjOpenUartReq` | `{int port; int mode; uint flags; uint reserved;}` | UART open request; `flags`/`reserved` zero |
| `ObjUsbEpReq` | `{int devno; int epno; int kind; int mode; uint flags; uint reserved;}` | endpoint open request; kinds DATA(0)/CTL(1), modes OREAD/OWRITE/ORDWR; `flags`/`reserved` zero |
| `ObjUsbEntry` | `Usbentry` layout | `ObjOpUsbList` record; byte-compatible with `include/usbctl.h` |
| `Uartcfg` | existing native structure | `ObjOpUartCtl` input, exactly `sizeof(Uartcfg)` |
| `OBJ_STREAM_MAXIO` | 65536 | per-read/write Pipe and UART byte bound |
| `OBJ_KBD_MAXREAD` | 4096 | per-read keyboard scan-code bound |
| `OBJ_UART_STATMAX` | 65536 | per-status UART byte bound |
| `OBJ_USB_LISTMAX` | 256 | per-call USB record bound; matches legacy `USBLIST` |

Pipe defaults are `Vread|Vwrite|Vmeta|Vdelegate`. UART defaults follow the open
mode: `OREAD` gives `Vread|Vmeta|Vcontrol|Vdelegate`, `OWRITE` gives
`Vwrite|Vmeta|Vcontrol|Vdelegate`, and `ORDWR` gives both data rights plus
`Vmeta|Vcontrol|Vdelegate`. Keyboard defaults are
`Vread|Vmeta|Vdelegate|Vcontrol`. A caller may request a subset; a request
with any bit outside the provider default is `Eperm`. `ObjOpenKbd` accepts no
rights request (`Eperm` if nonzero). `obj_open(ObjOpenHandle)` can attenuate a
Pipe, UART or KBD Handle like any other capability.

### Pipe

`OBJCREATE(kind=ObjKindPipe)` creates one native pair through
`pipecorepair()` and installs **end 0 only**; the second end is pinned by
end 0 until `OBJINVOKE(ObjOpPipePeer)` installs it. The peer call requires
`Vdelegate`, returns a new process-local Handle, and gives the peer the same
rights as end 0. If end 0 is closed first, the still-pinned peer is released,
so no second table and no leaked endpoint remain. There is no third
`PipePeer` call: a non-end-0 Handle returns `Ebadarg`, and an end-0 Handle
whose peer was already installed returns the kernel error
`Pipe: peer already opened`.

Data semantics are the native blocking byte stream: writing end's bytes make
them readable from the opposite end (`write(end 0) -> read(end 1)`, and the
reverse). It is not an ObjectFIFO, an object-message queue, a file descriptor
or a decoded event stream. `OBJINVOKE(Read/Write)` and the legacy
`PIPEREAD/PIPEWRITE` both call the same `pipecore` with `Vread`/`Vwrite`
checked against the Handle. The compatibility calls keep their historical
`Ebadarg` for a rights violation; the generic `OBJINVOKE` path reports
`Eperm`. Zero-length I/O returns 0. All byte buffers are staged in kernel
memory so no user pointer is held across a blocking queue operation.

Inheritance is explicit, one-shot and lossless:

- `OBJINVOKE(ObjOpPipeInherit)` on a Pipe Handle accepts only legacy bits 1
  (read), 2 (write) and 3 (both) in `arg0`, maps them to `Vread`/`Vwrite`,
  requires the mapped rights to be a subset of the Handle, and arms one
  pending `RFPROC` selection. It is the replacement for the retired
  `PIPEINHERIT` number 100.
- `RFORK(RFPROC)` copies only armed Pipe Handles into the child's otherwise
  empty `Vhand` table, preserving slot and generation, and consumes the mark.
  Unmarked endpoints (including standard output) do not cross silently.
- `OSPAWN` uses only this call's validated `pipes` list; each `(handle,
  rights)` right must be a non-amplifying subset. `stdoutpipe` is accepted
  only when the target Handle is an installed Pipe endpoint with `Vwrite`.
- `exec` and process exit release every Handle through `vobjprocexit`; a
  child's end therefore closes normally without a second owner table.

### UART

`OBJOPEN(ObjOpenUart)` requires `iseve()` exactly as the retired `UARTOPEN`
did. `port` is the discovery-local `ObjOpUartCount` index. `mode` must be `OREAD`,
`OWRITE` or `ORDWR`; the mode fixes both the native open mode and the default
rights. Console and special ports are rejected by the existing
`uartnuserport` gate (`Eperm`); an unknown index is `Enodev`. A failed native
open raises before any Handle is installed, and a failed Handle install closes
the native open, so error paths do not leak a port reference.

`OBJINVOKE(Read/Write)` uses `Vread`/`Vwrite` and the native
`uartnread`/`uartnwrite`, rejecting a direction the open mode does not permit
(`Ebadusefd`). `OBJINVOKE(UartCtl)` requires `Vcontrol` and passes exactly one
writable `Uartcfg` to `uartnctl`. `OBJINVOKE(UartStatus)` requires `Vmeta`
and returns the native status bytes bounded by `OBJ_UART_STATMAX`.
`OBJINVOKE(ObjOpUartCount)` is the handleless discovery call and adds no
Handle; the retired `UARTCOUNT` and `UARTOPEN/READ/WRITE/IOCTL/STATUS/CLOSE`
numbers dispatch to nothing.

The QEMU test exposed a pre-existing read-only-memory fault in
`uartenable()`: it passed string literals (`"l8"`, `"s1"`, `"pn"`, `"b9600"`)
to `uartctl()`, which tokenizes its argument in place. Under the GCC amd64
build those literals are in read-only memory, so opening a port not already
configured by the boot console faulted the kernel. This round changes only
those four arguments to writable local buffers in
`kernel/drivers/uart/uartcore.c`; UART semantics, limits and permission gates
are unchanged.

### Keyboard

`OBJOPEN(ObjOpenKbd)` requires `iseve()` and the exclusive
`kbdscancodeacquire()` claim. A second claim is `EBUSY`; an unavailable PS/2
path is reported as `ENODEV`-class error; no Handle is installed on either
failure. `OBJINVOKE(Read)` returns raw PS/2 scan-code bytes, never a decoded
`KeyEvent`, and is bounded by `OBJ_KBD_MAXREAD`.
`OBJINVOKE(ObjOpKbdLeds)` requires `iseve()`; handle 0 is the eve-only
global LED control that does not claim the controller, and a nonzero KBD
Handle also needs `Vcontrol`. LED state is a shared hardware control, not
scan-code authority. `OBJCLOSE` and process exit release the single claim.
The retired `KBDOPEN/KBDREAD/KBDCLOSE/KBDLEDS` numbers dispatch to nothing;
MicroPython keeps the names as OBJ adapters.

### USB service and endpoint open

`OBJOPEN(ObjOpenUsb)` calls the existing `usbcore_init()` and returns a
process-local `VkindUsb` Handle with default rights `Vmeta|Vdelegate`. The
call accepts no request body (`req == NULL` and `len == 0`) and `rights == 0`
selects the metadata-only default. Requesting `Vcreate` requires the trusted
`eve`/bootstrap user; any bit outside `Vmeta|Vdelegate|Vcreate` is `Eperm`.
`OBJOPEN(ObjOpenHandle)`, `GRANT` and `OSPAWN` only copy or attenuate
existing rights, so they cannot add `Vcreate`. A process may hold at most
`Vusbslotmax=4` USB service Handles.

`OBJINVOKE(ObjOpUsbList)` requires `Vmeta` on the Handle, not merely a numeric
Handle or OID. It snapshots the current `usbcore_list()` table into
`ObjUsbEntry` records, which are byte-compatible with the legacy `Usbentry`.
`outlen` is a byte capacity and must be a multiple of `sizeof(ObjUsbEntry)`;
the record count is clamped to `OBJ_USB_LISTMAX=256` and then to the current
total. A capacity above the bound is accepted and clamped before the provider
copies records, matching the `obj_usb_list()` adapter; only the copied range is
address-validated and written. The result is the number of copied records.
`out == NULL` or `outlen == 0` is the legacy `USBLIST` size query and returns
the total endpoint count without copying. An invalid buffer range or a
non-multiple `outlen` is `Ebadarg`; no partial copy is published.

`OBJINVOKE(ObjOpUsbEpOpen)` opens exactly one endpoint through a `VkindUsb`
Handle carrying `Vcreate`. The request is `ObjUsbEpReq` with `devno`, `epno`,
`kind` (DATA 0 / CTL 1) and `mode` (OREAD/OWRITE/ORDWR); `flags` and `reserved`
must be zero. There is no caller-supplied rights mask. DATA calls the existing
`usbcore_dataopen()` path; CTL references the endpoint without a data open, as
the legacy `usbepinstall()` did. The result is a new process-local
`VkindUsbEp` Handle whose rights are derived from the request:
`Vmeta|Vdelegate` plus `Vread`/`Vwrite` from the mode; CTL also gets
`Vcontrol`. A process may hold at most `Vusbepslotmax=16` USB endpoint
Handles. `OBJCLOSE` (or exec/exit) drops the endpoint exactly once when the
last reference is gone: DATA calls `usbcore_dataclose()` then
`usbcore_putep()`, CTL only `usbcore_putep()`. Data I/O, `USBIOCTL` and
control transfer remain unimplemented; no endpoint READ/WRITE/ioctl route is
added.

The legacy `USBOPEN/USBLIST/USBEPOPEN/USBEPNAME/USBREAD/USBWRITE/USBIOCTL/
USBCLOSE` syscalls 63-70 are unchanged and still use the separate 16-slot
`Usbhtab` with `USBSVC`/`USBDATA`/`USBCTL` kinds for endpoint handles. This
step unifies the metadata service and adds an object endpoint Handle, but it
does not move legacy `USBEPNAME`, data I/O or `USBIOCTL` into the object
provider. Closing a service or endpoint Handle is `OBJCLOSE`; a second close,
a stale generation or a wrong kind is `Ebadarg`. Exec and exit release both
`VkindUsb` and `VkindUsbEp` Handles through `vobjprocexit`; endpoint creation
does not allocate a second Handle table.

### Results and errors

- `OBJCREATE(ObjKindPipe)` returns the end-0 Handle; its optional `ObjId` is
  the scope-qualified volatile identity of that end.
- `ObjOpPipePeer`, `ObjOpenUart`, `ObjOpenKbd` and `ObjOpenUsb` return a
  Handle, not an OID. `ObjOpUartStatus` returns a byte count.
  `ObjOpUsbList` returns the copied record count, or the total on a size
  query. `ObjOpUartCtl` and `ObjOpKbdLeds` return 0.
- `ObjOpUsbEpOpen` returns a new endpoint Handle, not an OID. Its result is
  not a data capability until a later round adds I/O.
- Stale generation, wrong kind, wrong table slot and a second close return
  `Ebadarg`. Missing rights on the generic path return `Eperm`; the UART data
  direction mismatch returns `Ebadusefd`. Non-eve UART/KBD open and LED
  control, and console/special UART opens, return `Eperm`. Unknown UART
  indices and absent PS/2 hardware return `Enodev`. A USB rights request
  outside `Vmeta|Vdelegate|Vcreate`, a metadata or attenuated Handle without
  `Vcreate` opening an endpoint, and a missing `Vmeta` on `ObjOpUsbList`
  return `Eperm`; a malformed or inaccessible list buffer, a non-multiple
  `outlen`, and an invalid endpoint number/kind/mode return `Ebadarg`.
- Quota failures are kernel errors before installation: Pipe
  `Vpipeendmax=32` total endpoints per process, UART `Vuartslotmax=16`
  Handles per process, KBD `Vkbdslotmax=4` Handles per process, USB service
  `Vusbslotmax=4` Handles per process and USB endpoint
  `Vusbepslotmax=16` Handles per process. The global `Vhandmax=96` slot table
  is shared with bytes/FIFO/TCP/GE/Space/UDP6/USB.

### Pipe/UART/KBD retirement verification (2026-11-05)

- Kernel PASS (`--kernel`, 132/132 selected, 134/134 closure, ABI PASS, link
  PASS, no undefined symbols): `build/gcc/9pc64.elf`, 1,154,216 bytes,
  SHA-256 `c705bc55764a474e502e06a45e38070f662e56f12686e1e02a2683cbb7ad936e`.
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols,
  `build/gcc/micropython.elf`, 533,512 bytes, SHA-256
  `2cf35f5dd197692d351997bb27cac92d111f24e5501e37156cd8a54c76bb6ef2`.
- Static retirement PASS: 135 named / 71 active / 64 reserved; every one of
  the 16 new reserved names has a `nil` `systab` entry and `sysreserved=1`,
  and none of `syspipenew`/`syspiperead`/`syspipewrite`/`syspipeclose`/
  `syskbdopen`/`syskbdread`/`syskbdclose`/`syskbdleds`/`sysuartcount`/
  `sysuartopen`/`sysuartread`/`sysuartwrite`/`sysuartioctl`/
  `sysuartstatus`/`sysuartclose`/`syspipeinherit` exists in the linked ELF.
- Disposable KVM QEMU PASS (`build/qemu-o/sysretire-2/`), REPL input
  `RETIRE-OK`: OBJ Pipe create/peer, both-direction data and close;
  historical Python adapters still move data; `ObjOpPipeInherit` +
  `RFORK(RFPROC)` child read `FORK-PIPE`; OSPAWN explicit list child read
  `SPAWN`; `ObjOpUartCount` returned 2 and UART control/status/data worked
  through both OBJ and the historical adapters; invalid port, bad Handle and
  console-port opens were denied; KBD OBJ/legacy open denied while the REPL
  held the claim, LED on a pipe Handle and on a guessed Handle denied, and
  the handle-0 global LED control succeeded without claiming the controller.
- Raw-call PASS: `plan9.sysraw(n)` for each of 71-85 and 100 returned the
  kernel `Ebadarg` ("bad arg in system call") with no fatal note and no old
  handler. No panic, bad-disk, flush or sync error occurred.

### Verification in this round

- Kernel PASS (`--kernel`, 132/132 selected, 134/134 closure, ABI PASS, link
  PASS, no undefined symbols): `build/gcc/9pc64.elf`, 1,154,904 bytes,
  SHA-256 `5e098a298b569c888772cfa4deec699dcb7a3e21f2e05435eba8639cdd216014`.
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols,
  `build/gcc/micropython.elf`, 528,488 bytes, SHA-256
  `bf723c8e672e556b90ffbbe2f74fbee821ed40cfcd29abf2e2889a8b9d306f6d`.
- Disposable KVM QEMU PASS (`build/qemu-o/task-pipe2-5/`):
  OBJ Pipe create/peer, both-direction byte transfer, duplex, info kind and
  default rights; peer second-open and end-1 peer denied; attenuated write and
  `GRANT` amplification denied; mixed OBJ-write/legacy-read; legacy
  `PIPENEW/READ/WRITE/CLOSE`; stale read and double close denied; explicit
  UART control/status/data both through OBJ and legacy and cross-route on the
  free port; invalid UART port, console-port open and wrong-handle controls
  denied; KBD OBJ and legacy open denied while the REPL console holds the
  single PS/2 claim, and LED on a non-KBD Handle denied.
- Cross-process Pipe regression PASS: `PIPEINHERIT` + `RFPROC` child read an
  explicitly inherited endpoint and reported `FORK-PIPE`; a second MicroPython
  child started by OSPAWN read only the explicitly listed Pipe endpoint and
  reported `SPAWN`. No unmarked Handle crossed either path.
- UART was exercised on the enumerated free second port, not on physical
  external hardware; the kernel rodata fault above was reproduced first and
  fixed in this round.

Executed evidence is in `build/qemu-o/task-pipe2-5/` (serial log and report)
and the UART isolation runs `task-uart-only1` (fault reproduced before the
fix) and `task-uart-only3` (open PASS after the fix).

### USB metadata verification (2026-11-05)

- Kernel PASS: `build/gcc/9pc64.elf`, 1,155,048 bytes, SHA-256
  `f1f50750398df486253a99d4f7cfec349befee95279dd81ccca9cf16f8de9a43`.
  MicroPython PASS: 246 units, 0 FAIL, no undefined symbols,
  `build/gcc/micropython.elf`, 533,128 bytes, SHA-256
  `8426c0fd5ece411feefc40df10e60ba7d592a72552a1caf2cb5450557c2d542d`.
- Disposable KVM QEMU PASS (`build/qemu-o/task-usb-5/`): 20/20 assertions.
  The boot had zero USB endpoints. `OBJOPEN(ObjOpenUsb)` returned a Handle;
  `ObjOpUsbList` returned total 0 with an empty snapshot; the size query
  (`out == NULL`/count 0), a 300-record request clamped to 256 and a
  1-record request all agreed with the total; the same call through legacy
  `USBOPEN/USBLIST/USBCLOSE` produced the same total and record count.
  A guessed numeric Handle, a byte-object Handle and a `Vdelegate`-only
  attenuated Handle were denied; a request for `Vread` at open was denied;
  Read/Write on the USB Handle were denied (no data path); double close and
  stale close were denied. The legacy `USBEPOPEN` call returned cleanly
  (`Ebadarg` in the zero-endpoint environment) and an invalid kind was
  denied; no panic, bad-disk or sync error occurred.
- The QEMU boot used `--no-build --obootexec 0x4f45584543000003` with the
  current kernel and MicroPython ELF; the REPL input is in the task log.

### USB endpoint-open verification (2026-11-05)

- Canonical kernel PASS: 132/132 selected, 134/134 full closure, ABI PASS,
  link PASS, no undefined symbols (`build/gcc/9pc64.elf`). MicroPython PASS:
  246 units, 0 FAIL, no undefined symbols (`build/gcc/micropython.elf`).
  Exact sizes and hashes are recorded in `docs/TEST-RESULTS.json`.
- Disposable KVM QEMU PASS (`build/qemu-o/task-usbep-1/`): 32/32 assertions.
  The environment again had zero USB endpoints, so successful DATA/CTL open,
  close and endpoint release are **NOT RUN**. Executed:
  - metadata Handle default `Vmeta|Vdelegate`, kind `VkindUsb`;
  - metadata, `Vdelegate`-only and `OBJOPEN(ObjOpenHandle)` copies denied
    `ObjOpUsbEpOpen` with `Eperm`;
  - `OBJOPEN(ObjOpenHandle)` and `GRANT` could not add `Vcreate` (`Eperm`);
  - an eve Handle opened with `Vcreate|Vmeta|Vdelegate` passed the creation
    gate and reached the endpoint lookup, failing with `Ebadarg` because no
    endpoint existed; invalid kind, mode, device and endpoint numbers also
    returned `Ebadarg`;
  - direct `ObjOpUsbList` byte capacities of 1, 256 and 300 records returned
    normally, while a non-multiple 257-record+1 capacity was rejected with
    `Ebadarg` (the pre-fix bound would have rejected 300 records);
  - legacy `USBOPEN/USBLIST/USBCLOSE` still worked and legacy `USBEPOPEN`
    denied invalid kind / absent endpoint without a panic; double close of
    service Handles returned `Ebadarg`.
- No emulated or physical USB endpoint was available, so the successful
  endpoint-open, endpoint-rights observation on a live Handle, dataclose/
  putep release and CTL reference path remain **NOT RUN**, not PASS.

### Not run in the metadata round

- Reading actual keyboard scan codes and releasing the claim from a second
  claimant: the native REPL console owns the exclusive PS/2 claim, so only
  the safe `EBUSY` path was executed. LED hardware behavior is likewise
  implemented and reviewed but **NOT RUN**.
- UART receive with a real external peer, parity/baud error injection,
  hot-unplug and unprivileged-user denial (no non-eve harness is retained).
- USB listing with one or more real/emulated endpoints: the QEMU environment
  had zero endpoints, so record contents, the 256-record clamp with a full
  table and a nonempty-name copy were reviewed but **NOT RUN**.
- USB endpoint open, data transfer, control transfer and `USBIOCTL`
  unification: explicitly outside this step. The legacy endpoint-handle path
  returned a clean error with no endpoints; it was not exercised with a
  present device.
- `exec`/`exit` endpoint release observation across a live multi-process
  session, allocation-exhaustion injection, multi-CPU stress and guessed or
  recycled Handle attacks. These remain **NOT RUN**, not PASS.

## Not run

IPv6 TCP HTTP, two-process TCP/ObjectFIFO handoff, namespace
COPY/SHARE/revocation campaigns, multi-CPU stress, allocation-exhaustion
injection, PID-reuse/guessed-Handle attacks, external IPv6 and physical
hardware/disk writes are **NOT RUN by design** for this task: the canonical
tree retains no companion TCP peer or multi-process harness, and adding a
broad test tree is outside the sanctioned QEMU policy. They are not claimed as
PASS.

For UDP6 specifically, these are implemented but **NOT RUN** this round:
denial of `OBJOPEN(ObjOpenUdp6)` in an unprivileged child that has no boot or
`net6permit` gate (no Python/OSPAWN harness is retained), explicit OSPAWN
delegation of a UDP6 Handle, process-exit/exec endpoint release observation,
the receive-buffer-too-small path through a raw `OBJINVOKE` call, and any
multi-CPU or allocation-exhaustion campaign. The OBJ close/rights behavior,
the retired-number `Ebadarg` checks and a real peer send/receive were
executed in phase 4.

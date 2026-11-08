# Common message-device ABI — T12 USB and T13 Ethernet

T13 raw Ethernet is specified below and validated in
[T13-ETHERNET-RESULT](T13-ETHERNET-RESULT.md). Its emulated integration gate
passes 55 checks; the same kernel passes a fresh 66-check T12 USB regression.

Historical T12 status (2026-11-07): T12 integration gate PASS in the emulated endpoint
environment (66/66 executed checks). Physical hardware is NOT RUN; source
publication is BLOCKED. Execution evidence and
limits are recorded in [T12-DEVICE-RESULT](T12-DEVICE-RESULT.md). The initial
representative integration is a native USB control endpoint over `usbcore`.
Ethernet, USB bulk/interrupt/isochronous and Wi-Fi are not integrated by T12.
This is a byte-oriented amd64 PoC, not a universal packet schema.

## T16 verification scope (2026-11-08)

The later T16 assignment consolidates the **existing USB control-endpoint and
raw Ethernet** contract only, with T11 ObjectFIFO evidence. There is **no
Wi-Fi** family/driver/firmware/service/test requirement; any later Wi-Fi work
needs a separate assignment. T14/T15 and TCP/UDP are not prerequisites.
Family-specific completion, transfer bounds and control remain explicit;
a generic FIFO is optional distribution for synchronous USB, not an extra
completion framework. Historical T12/T13 results above are not fresh T16 tests.

## Common identity and authority

The existing five `OBJ*` syscalls carry this ABI. No syscall is added or
renumbered. New OPEN providers are `ObjOpenDevices=15` and `ObjOpenDevice=16`;
INVOKE operations are DeviceList=37, DeviceSend=38, DeviceRecv=39 and
DeviceCtl=40. Existing ABI versions and operations retain their layout and
meaning. Device records have their own version, `OBJ_DEVICE_ABI_VERSION=1`.

`OBJOPEN(ObjOpenDevices, NULL, 0, rights)` creates a `VkindDevices=13` catalog
Handle. Default rights are `Vmeta|Vdelegate`; the existing `iseve()` bootstrap
policy alone allows requesting `Vcreate`. This is the same transitional
policy as the object USB service (`ObjOpenUsb`), not the proposed production
authority model. The separate legacy USB syscall table is not thereby brought
under the new catalog policy; see the T16 legacy inventory.
There is no ambient privilege in a numeric selector or volatile identity.

`DeviceList` requires `Vmeta` and returns an array of 32-byte
`ObjDeviceEntry` records. A zero byte capacity returns the current count;
otherwise the result is the number copied, up to the caller's capacity.
The maximum capacity is 128 records (USB endpoints and Ethernet NICs).
Membership is a snapshot, not a reservation: detach or a competing open may
occur before use. Family 1 selects USB device/endpoint; family 2 selects
Ethernet controller/zero. USB maxsend is 4104, maxrecv 4096; Ethernet maxima
are complete L2 frame sizes. Unused record fields are zero.
Future providers must retain explicit payload and metadata semantics.

`OBJOPEN(ObjOpenDevice)` takes the 24-byte `ObjOpenDeviceReq`: version, family,
catalog Handle, two provider-specific selectors and a zero reserved field.
The catalog must carry `Vcreate`. The USB core revalidates the endpoint's type,
mode, attachment and exclusive DATA claim. A metadata-only catalog cannot open
a device, and an attenuated catalog cannot amplify its rights.

An open returns a `VkindDevice=14` session Handle with a fresh boot-scoped
volatile identity `(incarnation, space, oid)`. The selected USB device number
is not that identity, an OID, a pathname or a capability. Rights default to
`Vsend|Vrecv|Vmeta|Vcontrol|Vdelegate` for USB (206); Ethernet adds
`Vsubscribe` (238). A nonzero request must be a subset of the family ceiling.
`ObjOpenHandle`, Grant and explicit OSPAWN inheritance reuse existing Handle
semantics. Generic ObjectFIFO delivery retains its own restricted rights mask;
it does not convey all operational Device rights. Catalogs may be closed after
opening a session.
New catalog/session opens reject admission when the process already holds
four catalog or sixteen device Handles respectively. These are OPEN checks,
not hard alias limits: Grant/OpenHandle/OSPAWN use the shared Handle table
and can create further aliases subject to its 96-slot bound. The existing
global object/payload bounds also apply. Do not infer extra device authority
from an alias or confuse an open-admission count with all-handle quotas.

## Common invocation; provider-specific transfer

`DeviceSend` requires `Vsend`. It takes one bounded provider message in
`ObjCall.in/inlen`; out/outlen, arg0 and arg1 must be zero. The kernel validates
and copies the complete request before invoking the provider, so concurrent
changes to a user buffer cannot change the validated transfer. The kernel
retains no user buffer, pointer or borrowed object after return.

Ethernet SEND/RECV semantics are defined in the T13 section below. The
following synchronous completion contract is **USB family 1 only**, not a
universal completion schema.

### USB control transfer and one pending completion

`Vsend` permits submission; it does not authorize arbitrary USB controls.
On downstream endpoints, only this conservative standard read-only allowlist
can omit `Vcontrol`: device GET_DESCRIPTOR (`bmRequestType=0x80`, request 6),
GET_STATUS (0x80/0x81/0x82, request 0, value 0, length 2), GET_CONFIGURATION
(0x80, request 8, value/index 0, length 1) and GET_INTERFACE (0x81, request 10,
value 0, length 1). All other requests require both `Vsend` and `Vcontrol`,
including OUT, vendor IN and class IN. An IN direction is not proof of absence
of side effects. Every native root-hub request requires `Vcontrol`: even
GET_STATUS can finish an earlier port reset in `rhubwrite()`. This check occurs
before provider invocation and denial does not poison the session.
The initial USB message is exactly the eight USB setup bytes followed by
exactly `wLength` bytes for an OUT request; an IN request has setup bytes only.
The original USB byte order and request fields are preserved. `wLength` is at
most 4096. An IN transfer collects its entire response before returning;
a short USB response is a complete message and its actual length is preserved.
An OUT transfer completes with an empty bytes response. The native USB/HCI
code performs the setup/data/status stages and retains its existing finite
control-transfer timeout. Success is provider completion, not merely submission.
The provider may emulate root-hub port controls; this is reported separately
from a transfer to a peripheral on the external USB bus.

A successful send returns a positive, monotonically increasing session-local
sequence, bounded to signed 63 bits. It publishes exactly one pending immutable
completion. The session owns that response until receive or final close.
A second send or device control before receiving returns `Einuse` without
performing another transfer. This one-slot synchronous transaction state is
not a FIFO, TEE mechanism or new queue framework.

`DeviceRecv` requires `Vrecv`. It takes no input or scalar arguments and an
exactly 16-byte `ObjDeviceResult` output: sequence, family and actual response
length. Its return value is a new process-local bytes Handle carrying
`Vread|Vmeta|Vdelegate`. The bytes are an immutable complete response; a later
send cannot overwrite them. Ordinary byte reads may inspect that object in
pieces without changing the message's boundary or ownership. The object can
be published through the existing ObjectFIFO; the FIFO parses no device data.
The response does not confer device control or access to the device session.

Receive is nonblocking. With no completion it raises `device: no completion`.
If a new Handle cannot be installed, the pending completion remains available
for retry. Response storage and its global payload charge are trimmed to the
actual response size. Short replies expose no uninitialized tail. A caller
must close each received bytes Handle using ordinary `OBJCLOSE`.

## Device-specific control

`DeviceCtl` carries a family-specific operation in arg0. IN and arg1 are zero;
OUT is permitted only for a defined bounded result. Authorization belongs to
the operation: Ethernet status (2) requires Vmeta and exact 88-byte output,
Ethernet detach (3) requires Vcontrol and no output. USB clear-halt (1) requires
Vcontrol, with no input/output. USB control operation 1 requests the existing
`usbcore_ioctl(USBclrhalt)` behavior: mark the endpoint for halt recovery on its
next transfer. It does not add global USB administration, arbitrary ioctls,
endpoint creation, enumeration or a reset interface. Other controls return
`Ebadctl`. Control is serialized with transfers and requires an empty completion
slot and an unfailed session. USB setup requests themselves remain explicit
USB messages and obey the SEND authorization rule above.

## Validation, errors and teardown

The normal ObjCall version/flags/rights/reserved validation applies. Catalog
and open records have exact lengths, bounded counts and zero reserved fields;
all buffer addresses/directions are checked. Unknown family/version, invalid
message lengths, mismatched setup/data lengths, wrong Handle kind and stale
Handles fail. Rights come from the kernel Handle, never a caller's claimed
ObjCall rights. Unsupported messages cannot fall through to a stream read or
write path.

- `Ebadarg`: malformed requests, unsupported device/version, wrong kind or
  invalid/stale Handle (the existing Handle lookup contract).
- `Eperm`: insufficient catalog/session rights or requested amplification.
- `Einuse`: exclusive endpoint already open, or pending completion not received.
- `Enomem`: bounded object/payload allocation failure; normal Handle-table
  exhaustion retains its existing `vobj: too many handles` text.
- `Ebadctl`: unknown device-specific control.
- USB detach, transfer timeout, stall and other HCI errors propagate the native
  error text. Failed send creates no new completion or sequence. Hardware may
  already have seen a request, so error is not a promise of rollback and the
  kernel never retries it implicitly. After any error once a transfer has
  entered usbcore (including a post-transfer allocation failure), the shared
  session is latched failed. Further SEND and DeviceCtl on every alias return
  `device: transfer failed; close and reopen`. RECV still reports no completion.
  INFO and CLOSE remain available. Drop all session aliases and explicitly
  reopen to release HCI transaction state and reacquire the endpoint; a detached
  endpoint cannot reopen. No successful hardware rollback is claimed.

The session has one QLock for complete transactions, with a live Vhand reference
pinning the object and endpoint throughout a blocking provider operation.
Aliases reference the same session, exclusive DATA claim, sequence and pending
completion. Closing one alias leaves the others usable. Final close, exec or
process exit releases the pending response, closes the native USB DATA claim
exactly once and drops the endpoint reference. Already received bytes objects
have independent lifetimes. Closing the catalog does not revoke existing
sessions. Generic Revoke remains FIFO-only; this USB session has no device-wide revoke
or hotplug notification. Ethernet separately supports explicit detach (3),
not a new generic device revoke operation. Detachment is detected by usbcore
on the next operation, and close still releases the held reference.

USB provider work runs in syscall/process context. This T12 slice introduces
no IRQ callback, per-device FIFO, filesystem, `Chan` bridge or fd layer. A direct
synchronous usbcore call is simpler for one exclusive control transaction;
ObjectFIFO remains available when actual message distribution is required.
The Ethernet IRQ-safe handoff and subscribers belong to T13, not this change.

## Userspace

C adapters are declared in `runtime/abi/obj.h` and implemented in
`runtime/libc/port/oobject.c`. MicroPython's `plan9` module exposes:

- `obj_devices_open(rights=0)`
- `obj_device_list(catalog, capacity=128)` (capacity 0 returns the count)
- `obj_device_open(catalog, family, selector0, selector1, rights=0)`
- `obj_device_send(device, message)` (USB sequence; Ethernet accepted length)
- `obj_device_recv(device)` (USB bytes Handle/completion; Ethernet FIFO/0/2/0)
- `obj_device_ctl(device, operation)`

Use `obj_info`, `obj_read`, `obj_grant`, `obj_publish` and `obj_close` on the
corresponding existing objects. The adapters pack arguments only; the kernel
performs authorization and validation.

## Reproducible validation

The focused source and host entry point are tracked under `tools/host/t12/`.
Run `bash tools/host/t12/run-device-checks.sh` after the four canonical builds;
see [T12-DEVICE-RESULT](T12-DEVICE-RESULT.md) for exact environment settings.
It seeds the check module through mkovol's existing SOURCE argument and uses
the sanctioned QEMU O-boot runner with a disposable xHCI/mouse device. This is
an explicitly authorized focused test, not a new kernel probe or boot profile.

`plan9.obj_usb_ctl(handle, operation, writable_buffer)` sends a bounded
IN/OUT USB administration request through `OBJINVOKE(ObjOpDeviceCtl)` on a
`ObjOpenUsb` service or control-endpoint Handle. It requires `Vcontrol` on the
Handle. A default metadata service Handle cannot invoke control or open an
endpoint; trusted bootstrap may request `Vcreate|Vcontrol` explicitly. Endpoint
rights are derived from its kind and mode. `ObjOpDeviceCtl` continues to use
operation 1 for USB clear-halt on a common Device session; the Handle kind
selects the provider contract. The old USBOPEN..USBCLOSE numbers are reserved.
The `usbcore` enumeration and detach operations remain synchronous, with the
same 4096-byte bound. This administration path is separate from the common
message-device transfer path above.


## T13 raw Ethernet provider

Family 2 selects a physical NIC by `(selector0=controller number, selector1=0)`.
Catalog authority is unchanged: Vcreate is required to open a session; no
numeric selector grants access. One physical NIC has one RX sink/session;
explicit Handle aliases share it. Catalog records remain 32 bytes/version 1;
maxsend/maxrecv are complete L2 frame limits. The common catalog bound is now
128 entries; the USB payload limit remains 4096, while the shared message
bound is 16384. Actual NIC limits are also enforced (virtio: 60..1514 bytes).
Frames include destination/source MAC, EtherType and padding, without FCS.
No Ethernet/IP schema or filter is imposed on the frame bytes.

Ethernet rights are Vsend|Vrecv|Vmeta|Vsubscribe|Vcontrol|Vdelegate (default
238). Vsend authorizes raw TX. SEND returns the full accepted length or an
error, never a short success or a completion sequence. Acceptance means
ethercore copied/enqueued the frame; it is not a hardware completion. Link
down, full/closed TX queue, allocation and frame-size errors are explicit.
Virtio negotiates link status and handles configuration changes in its IRQ.

RECV requires **both Vrecv and Vsubscribe**. It returns an ordinary FIFO
capability and `(sequence=0, family=2, length=0)`, not a frame completion.
That capability has Vsubscribe|Vmeta plus only the device Handle's Vcontrol
and Vdelegate. It never has Vsend. Generic `obj_subscribe(fifo,12,capacity)`
and `obj_receive(subscription,timeout)` then receive complete immutable bytes
objects; default receive grants Vread|Vmeta, without delegation. Independent
processes can receive a device capability with rights 44 and create their
own subscriptions; neither the device nor FIFO rights can amplify.

Each frame has a separate 24-byte versioned little-endian ObjEtherRxMeta
envelope: version:u32, controller:u32, length:u32, reserved-zero:u32,
arrival-sequence:u64. The object bytes remain exactly the L2 frame. Sequence
is callback arrival order within the session; gaps are possible after drops.
No GE/O OID is allocated for incoming frames. Handle values are process-local;
volatile object identity, immutable bytes and physical controller identity
remain distinct.

DeviceCtl operation 2 returns the fixed 88-byte ObjEtherStatus under Vmeta.
`obj_device_ether_info(device)` packs it as `(version, controller, minframe,
maxframe, mac, link, closed, arrivals, published, handoff_drops, publish_drops,
tx_accepted, highwater, handoff_capacity, handoff_queued)`. `published` counts
successful FIFO publications (including zero subscribers), handoff_drops
counts callback rejection, and publish_drops counts worker allocation/FIFO
failures; generic FIFO drops count per-subscriber overflow separately.
Operation 3 requires Vcontrol and explicitly detaches the session. Closing
one alias keeps it alive; final close/exit performs detach, drains pending
handoff Blocks, closes FIFO queues and releases the NIC sink. Received bytes
remain valid. Revoke acts on the FIFO, not the physical device; after revoke,
RX publication fails safely until detach/close. Detach is idempotent; repeated
user FIFO revoke retains the existing error. A detached session cannot send
or obtain a new RX FIFO; a new session may reacquire the physical NIC.

IRQ handoff capacity is 64 Blocks. Only the worker allocates immutable
objects or enters ObjectFIFO publication. There is one existing ObjectFIFO
per session, with its existing independent subscriber queues, not another
queue family. Ownership, concurrent teardown and executed limits are detailed
in [T13-ETHERNET-RESULT](T13-ETHERNET-RESULT.md).

## T16 common-contract consolidation

[T16-DEVICE-RESULT](T16-DEVICE-RESULT.md) supplies the source-backed common /
provider / legacy matrix and fresh verification. Both families use one VkindDevice,
Vhtab/Vhand, ordinary identity/refcounts, Grant/OpenHandle/OSPAWN attenuation,
OBJCLOSE and process-exit cleanup. Provider state is the USB pending reply/Ep
or Ethernet Veth/FIFO, not an independent capability framework. Actual data
is copied and never translated between USB setup bytes and Ethernet frames.

Receiving a new USB bytes object or Ethernet FIFO uses an explicit provider
result policy; it is not an alias grant of the device's rights. USB response
137 is read/meta/delegate on **content only**, never device authority.
Ethernet FIFO receives subscribe/meta plus only caller-held delegate/control,
with no send right. Generic FIFO reception intersects the sender's ceiling;
default received frame/content Handles have read/meta. Operational session
attenuation/delegation is through Grant/OpenHandle/OSPAWN. Generic FIFO
publication is not a promise to transfer every device I/O/control right;
its existing ceiling does not propagate those operational device bits.

T12's downstream fixture uses the `ObjOpenUsb` service and control-endpoint
Handles for synchronous `usbcore` new-device/address/detach administration,
then uses the common Device session for all peripheral data and completions.
Both paths share the USB core's exclusive endpoint claim and HCI state.
Control requires `Vcontrol` on its own Handle. The old USB syscall table and
its separate Handle table are retired; the focused gate now exercises this
OBJ route directly.

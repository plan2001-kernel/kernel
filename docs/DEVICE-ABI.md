# T12 — common message-device ABI

Status (2026-11-07): T12 integration gate PASS in the emulated endpoint
environment (66/66 executed checks). Physical hardware is NOT RUN; source
publication is BLOCKED. Execution evidence and
limits are recorded in [T12-DEVICE-RESULT](T12-DEVICE-RESULT.md). The initial
representative integration is a native USB control endpoint over `usbcore`.
Ethernet, USB bulk/interrupt/isochronous and Wi-Fi are not integrated by T12.
This is a byte-oriented amd64 PoC, not a universal packet schema.

## Identity and authority

The existing five `OBJ*` syscalls carry this ABI. No syscall is added or
renumbered. New OPEN providers are `ObjOpenDevices=15` and `ObjOpenDevice=16`;
INVOKE operations are DeviceList=37, DeviceSend=38, DeviceRecv=39 and
DeviceCtl=40. Existing ABI versions and operations retain their layout and
meaning. Device records have their own version, `OBJ_DEVICE_ABI_VERSION=1`.

`OBJOPEN(ObjOpenDevices, NULL, 0, rights)` creates a `VkindDevices=13` catalog
Handle. Default rights are `Vmeta|Vdelegate`; the existing `iseve()` bootstrap
policy alone allows requesting `Vcreate`. This is the same transitional
policy as the native USB service, not the proposed production authority model.
There is no ambient privilege in a numeric selector or volatile identity.

`DeviceList` requires `Vmeta` and returns an array of 32-byte
`ObjDeviceEntry` records. A zero byte capacity returns the current count;
otherwise the result is the number copied, up to the caller's capacity.
The maximum capacity is 64 records, matching the current USB core's endpoint
bound. Membership is a snapshot, not a reservation: detach or a competing
open may occur before the selected device is opened. The initial catalog
lists live USB control endpoints only, with family 1, device/endpoint numbers,
maximum send 4104 bytes and maximum receive 4096 bytes. Unused fields are zero.
Future providers must retain explicit payload and metadata semantics.

`OBJOPEN(ObjOpenDevice)` takes the 24-byte `ObjOpenDeviceReq`: version, family,
catalog Handle, two provider-specific selectors and a zero reserved field.
The catalog must carry `Vcreate`. The USB core revalidates the endpoint's type,
mode, attachment and exclusive DATA claim. A metadata-only catalog cannot open
a device, and an attenuated catalog cannot amplify its rights.

An open returns a `VkindDevice=14` session Handle with a fresh boot-scoped
volatile identity `(incarnation, space, oid)`. The selected USB device number
is not that identity, an OID, a pathname or a capability. Rights default to
`Vsend|Vrecv|Vmeta|Vcontrol|Vdelegate`; a nonzero request must be a subset.
`ObjOpenHandle`, Grant, explicit OSPAWN inheritance and ObjectFIFO delegation
reuse existing Handle semantics. Catalogs may be closed after opening a session.
There are at most four catalog and sixteen device Handles per process, plus
the existing 96-Handle and global object/payload bounds.

## Complete-message transfer and completion

`DeviceSend` requires `Vsend`. It takes one bounded provider message in
`ObjCall.in/inlen`; out/outlen, arg0 and arg1 must be zero. The kernel validates
and copies the complete request before invoking the provider, so concurrent
changes to a user buffer cannot change the validated transfer. The kernel
retains no user buffer, pointer or borrowed object after return.

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

`DeviceCtl` requires `Vcontrol`. `arg0` is a provider-specific control operation;
all buffers and arg1 must be zero. USB control operation 1 requests the existing
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
sessions. Generic Revoke remains FIFO-only; device-wide revocation and USB
hotplug notification are not implemented. Detachment is detected by usbcore
on the next operation, and close still releases the held reference.

All provider work runs in syscall/process context. This T12 slice introduces
no IRQ callback, per-device FIFO, filesystem, `Chan` bridge or fd layer. A direct
synchronous usbcore call is simpler for one exclusive control transaction;
ObjectFIFO remains available when actual message distribution is required.
The Ethernet IRQ-safe handoff and subscribers belong to T13, not this change.

## Userspace

C adapters are declared in `runtime/abi/obj.h` and implemented in
`runtime/libc/port/oobject.c`. MicroPython's `plan9` module exposes:

- `obj_devices_open(rights=0)`
- `obj_device_list(catalog, capacity=64)` (capacity 0 returns the count)
- `obj_device_open(catalog, family, selector0, selector1, rights=0)`
- `obj_device_send(device, message)` (returns completion sequence)
- `obj_device_recv(device)` (returns bytes Handle, sequence, family, length)
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

`plan9.usb_ioctl(handle, operation, writable_buffer)` is a bounded buffer-packing
adapter over the existing legacy USBIOCTL syscall, used only to prepare and
remove the downstream device through normal USB administration. Its 4096-byte
buffer cap and the legacy kernel's direction, Handle and operation validation
remain in effect. It adds no controller logic or new syscall, and does not
change legacy USB authorization. Peripheral data transfer, completion and
rights checks then use the T12 session operations. Root-hub emulation and
actual peripheral HCI data are reported separately.

# Common device authority and provider semantics

This document defines the device-opening contract in `runtime/abi/obj.h`. It
is a lasting interface rule, not a per-test report.

## One catalog authority chain

Every supported physical device family uses the same two operations:

```
OBJOPEN(ObjOpenDevices, rights)
  -> OBJINVOKE(ObjOpDeviceList)        bounded discovery records
  -> OBJOPEN(ObjOpenDevice, catalog Handle, family,
             selectors, versioned options, attenuated rights)
  -> armed catalog Handle also accepts global USB administration
  -> provider-specific READ/WRITE/SEND/RECV/INVOKE operations
```

- A catalog Handle must carry `Vcreate` for every device open. A USB CTL
  endpoint reference additionally requires `Vcontrol`, matching its
  administration role.
- `iseve()` may decide which rights an initial catalog grant contains. It is
  not consulted again when a delegated Handle is used.
- Selectors (UART port, USB device/endpoint, NIC controller) are boot-local
  hints. Knowing a selector, device number or endpoint number grants nothing.
- Open rights are attenuated from the family's grantable set. `OBJOPEN` and
  `OBJGRANT` can only narrow the rights of an existing Handle.
- Options are a bounded, versioned `ObjDeviceOpenOptions` record. The kernel
  validates option version, option length, flags, mode/kind and selectors
  before acquiring a port, the PS/2 controller, a NIC or a USB endpoint.

## Families and preserved semantics

| Family | Open input | Required behavior |
| --- | --- | --- |
| `ObjDeviceUart` | `mode` = `OREAD`/`OWRITE`/`ORDWR` | Authorized bounded byte-stream READ/WRITE; `Vread`/`Vwrite`; `UartCtl` needs `Vcontrol`, status/info need `Vmeta`. No artificial frames. |
| `ObjDeviceKbd` | `flags` = `OBJ_DEVICE_KBD_EXCLUSIVE` | Exclusive PS/2 scan-code claim; READ returns raw scan codes; LED control needs `Vcontrol`. |
| `ObjDeviceEthernet` | no options | Raw L2 SEND of complete frames; SEND returns accepted/enqueued bytes. RECV returns the existing ObjectFIFO capability. |
| `ObjDeviceUsbControl` | no options | Synchronous control transfer; exactly one session-owned pending immutable completion, not an Ethernet FIFO and not a bulk API. |
| `ObjDeviceUsbEndpoint` | `kind` = data/CTL, `mode` | DATA performs the existing exclusive endpoint data open; CTL references the endpoint for provider administration. No USB bulk data API is implied. |

Global USB administration (`USBIOCTL`) remains provider-specific. It is
reachable only through a catalog Handle carrying `Vcontrol`, and endpoint
administration only through a catalog-opened CTL endpoint reference carrying
`Vcontrol`. It is synchronous and copies any IN/OUT buffer for the duration
of the call only.

The Ethernet receive path publishes immutable byte objects through
ObjectFIFO, with ordinary subscription rights and frame boundaries. The USB
control path publishes at most one non-queued completion; the two are not
interchangeable transport models.

## Reserved historical identifiers

`ObjOpenUart` (10), `ObjOpenKbd` (11) and `ObjOpenUsb` (12) are reserved slots
with no provider. `ObjOpUsbList` (20) and `ObjOpUsbEpOpen` (21) are reserved
operations with no handler. `VkindUsb` (9) is a reserved object-kind number.
UART, KBD and USB are opened only through `ObjOpenDevices`/`ObjOpenDevice`;
their metadata is ordinary `ObjOpDeviceList` output.

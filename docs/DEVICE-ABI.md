# Common device authority and provider semantics

This document records the current AMD64 device-opening contract in
`runtime/abi/obj.h` and authority rules to retain during migration. The
[target architecture](ARCHITECTURE.md) governs future transport.

## One catalog authority chain

Every supported physical device family uses the same two operations:

```
OBJOPEN(ObjOpenDevices, rights)
  -> OBJINVOKE(ObjOpDeviceList)        bounded discovery records
  -> OBJOPEN(ObjOpenDevice, catalog Handle, family,
             selectors, versioned options, attenuated rights)
  -> armed catalog Handle also accepts global USB administration
  -> provider-specific READ/WRITE/SEND/RECV object operations
  -> IOCTL(device Handle, operation, IoctlReq*, size) for control/status
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
| `ObjDeviceUart` | `mode` = `OREAD`/`OWRITE`/`ORDWR` | Authorized bounded byte-stream READ/WRITE; `Vread`/`Vwrite`. `IOCTL(IoctlUartConfig)` needs `Vcontrol`; `IOCTL(IoctlUartStatus)` needs `Vmeta`. No artificial frames. |
| `ObjDeviceKbd` | `flags` = `OBJ_DEVICE_KBD_EXCLUSIVE` | Exclusive PS/2 scan-code claim; READ returns raw scan codes; `IOCTL(IoctlKbdLeds)` needs `Vcontrol`. |
| `ObjDeviceEthernet` | no options | Current raw L2 SEND accepts complete frames and returns accepted/enqueued bytes. Current `ObjOpDeviceRx` returns an ObjectFIFO receive-channel capability; `IOCTL(IoctlEtherStatus/Detach)` needs `Vmeta`/`Vcontrol`. This receive transport is a Phase 6 removal target. |
| `ObjDeviceUsbControl` | no options | Synchronous control transfer; exactly one session-owned pending immutable completion, not an Ethernet FIFO and not a bulk API. |
| `ObjDeviceUsbEndpoint` | `kind` = data/CTL, `mode` | DATA performs the existing exclusive endpoint data open; CTL references the endpoint for provider administration. No USB bulk data API is implied. |

Global USB administration (`IoctlUsbGlobal`) is provider-specific. It is
reachable only through a catalog Handle carrying `Vcontrol`, and endpoint
administration (`IoctlUsbEndpoint`) only through a catalog-opened CTL endpoint
reference carrying `Vcontrol`. It is synchronous and copies any IN/OUT buffer
for the duration of the call only. `IoctlUsbClearHalt` is the synchronous
USB-control status operation and separately requires `Vcontrol`.

In the current ABI, device payload transport is not IOCTL: UART bytes,
Ethernet frames and USB transfer requests/completions use object
READ/WRITE/SEND/RECV operations. The target may change the transport while
keeping device configuration/control separate from payload.
A USB control *transfer* is transport with its own send/completion sequence;
the naming of "control transfer" does not make it an IOCTL.

**CURRENT IMPLEMENTATION:** Ethernet receive publishes immutable byte objects
through ObjectFIFO with subscription rights and frame boundaries. USB control
publishes at most one synchronous, non-queued completion. Neither establishes
ObjectFIFO as a target device/IPC transport.

**TARGET:** Migrate Ethernet receive and structured device events to
capability-mapped memory publication plus WAIT/WAKE notification after those
mechanisms exist. A userspace service defines framing, buffering, fan-out and
backpressure; retain checked device ownership and control authority. Console
input can use the same primitives while the kernel retains early-boot and
panic output. Phase 6 deletes the obsolete FIFO provider, ABI, adapters and
tests after the replacement device/console path works.

## Current reserved identifiers

`ObjOpenUart` (10), `ObjOpenKbd` (11) and `ObjOpenUsb` (12) are currently
reserved slots with no provider. `ObjOpUsbList` (20) and `ObjOpUsbEpOpen`
(21) currently have no handler. `VkindUsb` (9) is currently reserved.
These numbers are not retained as a target ABI compatibility requirement.
Currently UART, KBD and USB are opened through
`ObjOpenDevices`/`ObjOpenDevice`; their metadata is
`ObjOpDeviceList` output.

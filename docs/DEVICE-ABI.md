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
| `ObjDeviceEthernet` | no options | Raw L2 frame transport is a pair of capability regions. `ObjOpDeviceRx` returns one read-only `VkindRegion` Handle carrying an `ObjRingHeader` with complete L2 frame slots; `ObjOpDeviceTx` returns a region Handle whose unique writable mapping is the authorized publisher and whose consumer is the kernel. `IOCTL(IoctlEtherStatus/Detach)` needs `Vmeta`/`Vcontrol`. |
| `ObjDeviceUsbControl` | no options | Synchronous control transfer; exactly one session-owned pending immutable completion, not an Ethernet FIFO and not a bulk API. |
| `ObjDeviceUsbEndpoint` | `kind` = data/CTL, `mode` | DATA performs the existing exclusive endpoint data open; CTL references the endpoint for provider administration. No USB bulk data API is implied. |

Global USB administration (`IoctlUsbGlobal`) is provider-specific. It is
reachable only through a catalog Handle carrying `Vcontrol`, and endpoint
administration (`IoctlUsbEndpoint`) only through a catalog-opened CTL endpoint
reference carrying `Vcontrol`. It is synchronous and copies any IN/OUT buffer
for the duration of the call only. `IoctlUsbClearHalt` is the synchronous
USB-control status operation and separately requires `Vcontrol`.

In the current ABI, device payload transport is not IOCTL: UART bytes and
USB transfer requests/completions use object READ/WRITE/SEND/RECV operations.
Ethernet frames use the transmit and receive capability regions described
below. The target may change the transport while keeping device
configuration/control separate from payload.
A USB control *transfer* is transport with its own send/completion sequence;
the naming of "control transfer" does not make it an IOCTL.

**CURRENT IMPLEMENTATION:** Ethernet receive publishes complete L2 frames into
a kernel-written capability region; userspace maps it read-only. Ethernet
transmit uses the mirror contract: the authorized userspace publisher holds
the unique writable mapping, writes complete immutable frames into the fixed
64-slot ring, publishes `header.write_seq` with release ordering and calls
`REGIONWAKE(base, OBJ_RING_PUBOFF, ...)`. The kernel consumer validates each
slot, copies the frame into a NIC-owned `Block`, passes it to the existing L2
driver, publishes `header.ack_seq` with release ordering and wakes
`REGIONWAIT` observers of `OBJ_RING_ACKOFF`. A publisher must not reuse a slot
before it is acknowledged; the ring is full when
`write_seq - ack_seq == slots`, and an overrun loses the older slots that the
kernel skips. The kernel does not accept a userspace frame pointer and there is
no per-frame payload-copy syscall. Device `SEND` remains USB-only.

Unmapping the transmit writer stops publication; a later authorized mapping may
resume and continues the ring sequence while the device remains open. Explicit
detach joins both workers and releases the kernel's region references; a
reopened device receives fresh RX/TX regions, and Handles to the old regions
are orphaned (no consumer remains). Revocation blocks new mappings but does not
invalidate an installed writer mapping.

The NIC-facing receive and transmit paths validate only Ethernet frame
length/bounds, slot ownership and device state. They do not inspect or filter
EtherType, so IPv4 (`0x0800`), IPv6 (`0x86DD`), ARP (`0x0806`), VLAN-tagged and
experimental frames are preserved unchanged (FCS excluded) and no protocol
policy or L3 parsing exists in the kernel.

Frame fan-out, IP, IPv6, ARP and all upper-layer policy belong to userspace.
USB control publishes at most one synchronous, non-queued completion. Console
sessions use two such regions per session (owner-written input and
client-written output), with diagnostics distinguished by a slot flag. The
obsolete FIFO, Pipe and kernel IPv6/ARP/ESP providers are deleted.

## Current reserved identifiers

`ObjOpenUart` (10), `ObjOpenKbd` (11) and `ObjOpenUsb` (12) are currently
reserved slots with no provider. `ObjOpUsbList` (20) and `ObjOpUsbEpOpen`
(21) currently have no handler. `VkindUsb` (9) is currently reserved.
These numbers are not retained as a target ABI compatibility requirement.
Currently UART, KBD and USB are opened through
`ObjOpenDevices`/`ObjOpenDevice`; their metadata is
`ObjOpDeviceList` output.

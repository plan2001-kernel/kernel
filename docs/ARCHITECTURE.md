# Plan 2001-kernel: architecture

Plan 2001-kernel is an experimental operating-system kernel, not a production
compatibility platform. The architecture is deliberately open to multiple
instruction-set architectures.

## Architectural invariants

- Persistent data is stored in GE/O Spaces, identified by SpaceID and local
  object identifier (OID). A persistent occurrence is not a file path.
- A runtime object, a process-local Handle/capability, a persistent OID, and a
  physical device identity are distinct concepts.
- Hierarchical filesystem paths, Plan 9 Chan/fid/qid, and 9P are **not** the
  native kernel object interface. A hierarchy may be a userspace view, not
  the persistent identity model.
- The system-call boundary is intentionally small and coherent, **not** an
  exercise in reaching a predetermined syscall count. Use object operations
  where identity, capabilities, ownership, or lifetime have object semantics.
  Keep direct scalar calls where an object would only add ceremony.
- ObjectFIFO is the generic device-frame/IPC channel. It preserves message
  boundaries and capability checks; it is not a filesystem pipe.
- The current proof of concept uses MicroPython as its primary userspace
  program. Additional userspace facilities are not prerequisites for basic
  kernel progress.

## Architecture status

**AMD64:** The current full-system implementation includes UEFI boot,
scheduler, virtual memory, GE/O, object/system-call interfaces, device support,
and MicroPython startup. Its existing build and test paths remain applicable.

**ARM64/AArch64:** Active early-boot port. The repository contains an ARM64
ELF kernel, UEFI loader, shared BootInfo v1 validation, exception/EL1 entry,
early mappings, PL011 console, GICv2/GICv3 timer interrupts, and an early
physical-page allocator. Its QEMU `virt` boot was exercised with both GICv2 and
GICv3 on 2026-11-08. ARM64 does **not** yet implement the full AMD64 scheduler,
virtual memory, GE/O, syscalls, userspace, or device/networking stack. Physical
ARM64 hardware was not validated by that QEMU test.

This describes implementation maturity, **not** an AMD64-only policy. Shared
boot changes must consider both architectures; architecture-specific changes
must not invent cross-architecture feature parity.

## Implementation sources

- `kernel/core/`: core kernel and shared early boot.
- `kernel/amd64/`, `kernel/arm64/gcc/`: architecture-specific code.
- `boot/efi/`: UEFI loader sources.
- `ge/storage/`: GE/O storage engine.
- `runtime/abi/sys.h`: declared kernel syscall ABI and status.
- `micropython/`: current userspace interpreter.
- `kernel/drivers/`: device integrations.

**The current source is authoritative** where these notes and actual code
disagree. Record measured behavior separately from desired design.

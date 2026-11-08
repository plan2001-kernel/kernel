# Plan 2001-kernel

Plan 2001-kernel is an **experimental object-oriented operating-system
kernel** under development for AMD64 and ARM64. Persistent GE/O storage
uses SpaceIDs and OIDs rather than a native hierarchical filesystem.
Object handles and ObjectFIFO provide capability-aware runtime and device
interfaces. MicroPython is the primary userspace proof of concept.

The current **AMD64** kernel has a working full-system path, including GE/O
and MicroPython. The **ARM64** implementation has an early UEFI/QEMU boot
foundation, shared BootInfo handoff, console, memory initialization and timer
interrupts, but is not yet a full-system port. Neither architecture is an
exclusive long-term target.

## Build and test

For the current AMD64 implementation (requires suitable GCC/binutils and
Clang/LLD toolchains):

```sh
tools/build-gcc.sh --kernel -j 8
tools/build-efi-gcc.sh -j 8
tools/build-mpy-gcc.sh -j 8
tools/qemu-o-boot.sh
```

For the experimental AArch64 early-boot implementation:

```sh
tools/build-arm64.sh -j 8
tools/host/build-efi-arm64.sh -j 8
tools/qemu-arm64-boot.sh
QEMU_ARM64_GIC=3 tools/qemu-arm64-boot.sh
```

These commands require appropriate host dependencies. A build or QEMU
early-boot PASS is not a claim of physical-hardware validation.

## Current documentation

- [Architecture and architecture maturity](docs/ARCHITECTURE.md)
- [GE/O persistent object storage](docs/OBJECT-STORAGE.md)
- [Kernel interface and ObjectFIFO principles](docs/KERNEL-INTERFACE.md)
- [Development workflow](docs/DEVELOPMENT-WORKFLOW.md)

Read [AGENTS.md](AGENTS.md) for coding instructions. Source code and tests
are the authority for implemented behavior. Earlier experiments, T-stage
audit logs and removed proposals are available in Git history, not in the
working documentation set.

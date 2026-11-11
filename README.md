# Plan 2001-kernel

<p align="center">
  <img src="assets/plan2001-o-mascot.svg" alt="Plan 2001-kernel mascot: a rabbit playing the banjo" width="280">
</p>

Plan 2001-kernel is an **experimental object-oriented operating-system
kernel** under development for AMD64 and ARM64. Persistent GE/O storage
uses SpaceIDs and OIDs rather than a native hierarchical filesystem.
The [target architecture](docs/ARCHITECTURE.md) assigns each object one
immutable named owner. Registered programs own their executable and data;
launching one creates an independent process under the program's owner.
Owners offer data to other owners through the existing region-based IPC,
not ownership or direct access to their objects. The AMD64 implementation
enforces immutable `OwnerID` and registered-program launch; existing Handles
and region-scoped WAIT/WAKE remain addressing, attenuation and publication
mechanisms, never cross-owner authority. MicroPython is the userspace proof
of concept.

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
- [Kernel interface: target and current status](docs/KERNEL-INTERFACE.md)
- [Development workflow](docs/DEVELOPMENT-WORKFLOW.md)
- [Olio ja objekti: tietoteoreettinen manifesti (suomeksi)](docs/OLIO-OBJEKTI-MANIFESTI.md)

Read [AGENTS.md](AGENTS.md) for coding instructions. Source code and tests
are the authority for implemented behavior. Earlier experiments, T-stage
audit logs and removed proposals are available in Git history, not in the
working documentation set.

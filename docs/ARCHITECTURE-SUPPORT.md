# Architecture support

Plan2001-O has one supported target and one experimental boot foundation.
The distinction is deliberate: a compiling or booting port is not a second
operating system.

## amd64: supported target

The canonical `pc64`/`9pc64` configuration is the only supported Plan2001-O
target. It is the tree that contains the scheduler, VM, GE/O, native object
syscalls, Device ABI, networking and MicroPython execution. Its boot chain is:

```text
UEFI -> GRUB -> bootx64.efi -> 9pc64.elf -> OEXECUTE -> MicroPython REPL
```

Build and current verification commands are in [STATUS](STATUS.md). The amd64
boot ABI, device manifests and syscall ABI are not changed by the experimental
ARM64 work.

## ARM64/AArch64: experimental boot foundation

The ARM64 tree is a bounded integration proof, not a port of the amd64
system. Its purpose is to show that the existing Plan2001 Boot ABI v1 and a
shared early-boot module can be reused by a second ISA, without restoring
Plan 9 interfaces, path semantics or 9front build tools.

What exists today:

- A freestanding AArch64 ELF kernel, `build/arm64/9karm64.elf`, linked at
  `KZERO + 0x40100000` (`0xffffffffc0100000`, physical `0x40100000`).
- A Clang/LLD UEFI loader, `build/arm64/bootaa64.efi`, built from the same
  `boot/efi` sources as the amd64 loader. The shared ELF loader now selects
  the ELF machine by architecture, checks the entry with an architecture
  hook, and performs ARM64 cache maintenance on the loaded ranges.
- EL2-to-EL1 entry handling, AArch64 exception vectors, a 16 KiB early stack,
  a 39-bit VA translation regime with a 2 GiB identity map and a 2 GiB
  high-half (`KZERO`) map. RAM is mapped Normal write-back; the QEMU virt
  GIC/PL011/RTC window at `0x08000000..0x0a000000` is Device-nGnRnE.
- A PL011 console, GICv2/GICv3 detection and the EL1 physical timer.
- A page-granular physical allocator over the memory map: it clamps RAM to
  the mapped window, reserves the kernel image and the BootInfo blob to whole
  pages, returns zeroed 4 KiB pages and keeps a LIFO free list. It tracks
  allocated pages, so a page can be freed only once, and it refuses to report
  a free range that overlaps the kernel or the blob. A boot self-test
  allocates, inspects, frees and re-allocates 16 pages.
- The shared `include/oplanboot.h` / `kernel/core/oplanboot.c` module, which
  validates the BootInfo blob, classifies its memory map, computes RAM, and
  prints the architecture-independent handoff. The same module is compiled
  into the amd64 kernel and called from `kernel/amd64/main.c`.
- The ARM64 entry uses the existing BootInfo v1 convention: `X0` is the
  physical address of the blob, `X1` is zero. It validates the blob as ARM64
  before printing any success marker.

The tested boot sequence on QEMU `virt` is:

```text
PLAN2001-O: ARM64 kernel: entry
PLAN2001-O: ARM64: BootInfo at 0x000000005c75e000
PLAN2001-O: shared boot: BootInfo v1 accepted
PLAN2001-O: shared boot: arch ARM64
PLAN2001-O: shared boot: RAM 506 MiB
PLAN2001-O: ARM64 MEM: 506 MiB free
PLAN2001-O: ARM64 MEM: allocator self-test passed
PLAN2001-O: ARM64: GICv2
PLAN2001-O: ARM64: CNTFRQ 62500000 Hz
PLAN2001-O: ARM64 BOOT: shared kernel initialization reached
PLAN2001-O: ARM64 timer: 3 ticks, interrupts working
```

Build and boot:

```sh
tools/build-arm64.sh -j 8          # build/arm64/9karm64.elf
tools/host/build-efi-arm64.sh -j 8 # build/arm64/bootaa64.efi
tools/qemu-arm64-boot.sh           # builds both, boots QEMU virt, checks markers
```

`tools/qemu-arm64-boot.sh` uses the local AArch64 EDK2 firmware and QEMU's
`vvfat` drive for a disposable ESP under `build/arm64/esp`. Set
`QEMU_ARM64_NOBUILD=1` to boot existing artifacts, `QEMU_ARM64_FIRMWARE` to
name another firmware file, or `QEMU_ARM64_TIMEOUT` to change the bounded
timeout. It writes only under `build/arm64`.

## ARM64: not implemented

The following are **not** implemented or claimed for ARM64:

- a general ARM64 port of the amd64 kernel: no scheduler, VM, processes,
  syscalls, GE/O, Device ABI, ObjectFIFO or namespaces; there is a physical
  page allocator, not a virtual-memory system, kernel heap or pager;
- MicroPython or other userspace execution, and no ELF program loader beyond
  the kernel entry path;
- networking, USB, storage, PCI enumeration, framebuffer or input drivers;
- SMP, GICv3 LPIs/MSIs, ACPI, device-tree interpretation, PSCI, secure
  monitor or virtualization support;
- KVM, hardware-backed testing or physical ARM64 hardware validation;
- any claim that the ARM64 boot marker is a working Plan2001-O system.

The two architectures share the BootInfo ABI and the `oplanboot` early-boot
logic only. They do not share a compiled kernel image, a device manifest or a
boot policy. The amd64 build remains the canonical artifact and is verified
separately.

## Verification record (2026-11-08)

Executed in the repository worktree with the local host toolchain:

| Check | Result |
| --- | --- |
| ARM64 kernel compile/link (`tools/build-arm64.sh`) | PASS |
| ARM64 UEFI loader compile/link (`tools/host/build-efi-arm64.sh`) | PASS |
| QEMU `virt` boot, shared BootInfo, page allocator, marker and timer (`tools/qemu-arm64-boot.sh`) | PASS |
| amd64 kernel compile/link (`tools/build-gcc.sh --kernel`) | PASS on a Debian x86-64 GCC host in the earlier round; NOT RUN on this AArch64 host, which has no x86-64 GCC/binutils cross toolchain |
| amd64 boot | NOT RUN in this round |
| Physical ARM64 hardware | NOT RUN |

The earlier amd64 compile/link was run with a Debian
`gcc-x86-64-linux-gnu` cross compiler because that test host was AArch64. It
exercised the same `tools/host/build-kernel.py` manifest, all 136 closure
units, the ABI/register checks and the final link. In this round the only
available `gcc` is Clang 21 targeting AArch64: a `--target=x86_64-linux-gnu`
attempt cannot express the kernel's global `r14`/`r15` register variables and
has no x86-64 system `ld`, so the amd64 build was not re-run here. The amd64
source and manifest are unchanged by the ARM64 page allocator. No amd64 boot
is claimed from either build alone.

/* Plan2001-O experimental ARM64 boot kernel.
 *
 * VMA is the kernel's high-half address; LMA/p_paddr is the physical
 * address the UEFI loader copies it to.  The EFI loader checks
 * p_vaddr == p_paddr + KZERO, as for the AMD64 kernel.
 *
 * 0x40100000 is 1 MiB into QEMU virt's RAM (0x40000000).  The loader keeps
 * its BootInfo blob at or above 0x41000000, so a kernel smaller than the
 * remaining 15 MiB cannot overlap it.
 */
OUTPUT_FORMAT(elf64-littleaarch64)
OUTPUT_ARCH(aarch64)
ENTRY(_efi64)
KZERO = 0xffffffff80000000;
KTZERO = 0xffffffffc0100000;
SECTIONS {
  . = KTZERO;
  .text : AT(ADDR(.text) - KZERO) {
    *(.text.entry)
    *(.text*)
    *(.rodata*)
  }
  etext = .;
  . = ALIGN(4096);
  .data : AT(ADDR(.data) - KZERO) { *(.data*) }
  edata = .;
  . = ALIGN(4096);
  .bss (NOLOAD) : AT(ADDR(.bss) - KZERO) {
    __bss_start = .;
    *(.bss*) *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }
  . = ALIGN(8);
  end = .;
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment*) *(.gnu*) }
}

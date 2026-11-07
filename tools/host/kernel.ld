/* Plan2001-O N9.3 GCC AMD64 kernel link.  VMA is the high-half kernel
 * address; LMA/p_paddr is the physical load address used by the UEFI loader. */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_efi64)
KZERO = 0xffffffff80000000;
KTZERO = 0xffffffff80110000;
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
  .bss (NOLOAD) : AT(ADDR(.bss) - KZERO) { *(.bss*) *(COMMON) }
  . = ALIGN(8);
  end = .;
  /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) *(.gnu*) }
}

/* Plan2001 native AMD64 executable: two PT_LOAD segments exactly.
 * Symbols and memory ordering match the pinned kernel's user.ld.
 * Keep Newlib's init/fini arrays in the writable LOAD segment.
 */
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x200000;
    .text : {
        *(.text*)
        *(.rodata*)
        *(.gcc_except_table*)
    } :text
    . = ALIGN(0x1000);
    .data : {
        *(.data*)
        *(.got*)
        *(.sdata*)
        . = ALIGN(8);
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
        KEEP(*(.ctors*))
        KEEP(*(.dtors*))
    } :data
    .bss (NOLOAD) : {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    } :data
    . = ALIGN(16);
    end = .;
    _end = .;
    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
        *(.gnu.build.attributes*)
    }
}

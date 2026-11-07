/*
 * Plan2001-O N9.6 freestanding GCC user executable layout.
 *
 * Two PT_LOAD entries only, matching the kernel's fixed TSEG/DSEG/BSEG
 * model: read/execute text at UTZERO, then read/write data.  BSS is the
 * p_memsz tail of the data segment.  No PT_INTERP and no PT_DYNAMIC.
 * The entry contract is documented in startup.S.
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
	} :text
	. = ALIGN(0x1000);
	.data : {
		*(.data*)
	} :data
	.bss (NOLOAD) : {
		*(.bss*)
		*(COMMON)
	} :data
	end = .;
	/DISCARD/ : {
		*(.note*)
		*(.comment)
		*(.eh_frame*)
	}
}

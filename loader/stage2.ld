/* Link script for the loader's second stage; BASE comes from --defsym. */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
	. = BASE;

	.text : { *(.text.start) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	. = ALIGN(16);
	_gp = . + 0x7ff0;
	.data : { *(.data .data.*) *(.sdata .sdata.*) }
	. = ALIGN(4);
	__bss_start = .;
	.bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) }
	. = ALIGN(4);
	__bss_end = .;

	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) *(.gnu.attributes) }
}

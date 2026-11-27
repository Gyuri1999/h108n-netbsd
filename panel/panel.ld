/* Static Linux/MIPS executable for h108panel: one loadable segment. */
OUTPUT_ARCH(mips)
ENTRY(__start)

SECTIONS
{
	. = 0x00400000 + SIZEOF_HEADERS;

	.text : { *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	. = ALIGN(16);
	.data : { *(.data .data.*) *(.sdata .sdata.*) }
	. = ALIGN(16);
	.bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) }

	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) *(.gnu.attributes) }
}

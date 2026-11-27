/*
 * Link address.  0x80000000 is where the stock Linux kernel runs; the
 * bootloader itself lives near 0x81E00000, so stay well below it.
 * LOAD_BASE can be overridden from the Makefile once the bootloader's
 * load/application addresses have been read from its console.
 */
OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
	. = DEFINED(LOAD_BASE) ? LOAD_BASE : 0x80000000;

	.text : { *(.text.start) KEEP(*start.o(.text)) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) }
	. = ALIGN(16);
	_gp = . + 0x7ff0;
	.data : { *(.data .data.*) *(.sdata .sdata.*) }
	. = ALIGN(4);
	__bss_start = .;
	.bss : { *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON) }
	. = ALIGN(4);
	__bss_end = .;
	. = ALIGN(16);
	. += 0x4000;
	__stack_top = .;

	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) *(.gnu.attributes) }
}

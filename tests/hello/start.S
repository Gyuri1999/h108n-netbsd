/*
 * Minimal entry point for the ZTE H108N (Realtek RTL8672, Lexra RLX5281,
 * big-endian MIPS-I).  Loaded to RAM by the bootloader and entered at _start.
 */
	.set	noreorder
	.set	noat
	.text
	.globl	_start
	.ent	_start
_start:
	/* Interrupts off, kernel mode (R3000-style status register). */
	mfc0	$t0, $12
	li	$t1, ~0x3f
	and	$t0, $t0, $t1
	mtc0	$t0, $12
	nop

	/* Clear .bss */
	la	$t0, __bss_start
	la	$t1, __bss_end
1:	sltu	$t2, $t0, $t1
	beqz	$t2, 2f
	nop
	sw	$zero, 0($t0)
	b	1b
	addiu	$t0, $t0, 4

2:	la	$sp, __stack_top
	la	$gp, _gp
	jal	main
	nop

3:	b	3b
	nop
	.end	_start

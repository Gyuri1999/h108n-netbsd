/*
 * Assembly-only serial test for the ZTE H108N (RTL8672, big-endian MIPS-I).
 * Needs only the assembler and linker, so it can be tried before the C
 * compiler is ready.  Position independent apart from the string address,
 * which is taken relative to the entry point, so it runs wherever the
 * bootloader puts it.
 *
 * UART: 16550A at 0xB8002100, registers 4 bytes apart; THR at +0, LSR at
 * +0x14, each in the first (most significant) byte of its word.
 */
	.set	noreorder
	.set	noat
	.text
	.globl	_start
_start:
	/* Find our own address without relying on the link address. */
	bal	1f
	nop
1:	move	$s0, $ra		/* s0 = address of label 1 */

	/* Interrupts off (R3000-style status register). */
	mfc0	$t0, $12
	li	$t1, ~0x3f
	and	$t0, $t0, $t1
	mtc0	$t0, $12
	nop

	lui	$s1, 0xb800
	ori	$s1, $s1, 0x2100	/* s1 = UART base */

	addiu	$a0, $s0, msg - 1b
	bal	puts
	nop

	/* Then one dot roughly every second, forever. */
loop:	li	$t2, 0x02000000
2:	addiu	$t2, $t2, -1
	bnez	$t2, 2b
	nop
	li	$a0, '.'
	bal	putc
	nop
	b	loop
	nop

/* putc: a0 = character.  Clobbers t0. */
putc:
3:	lbu	$t0, 0x14($s1)		/* LSR */
	andi	$t0, $t0, 0x20		/* THR empty? */
	beqz	$t0, 3b
	nop
	jr	$ra
	sb	$a0, 0($s1)		/* THR (delay slot) */

/* puts: a0 = NUL-terminated string.  Clobbers t0, t3, t4. */
puts:
	move	$t4, $ra
	move	$t3, $a0
4:	lbu	$a0, 0($t3)
	beqz	$a0, 5f
	nop
	bal	putc
	nop
	b	4b
	addiu	$t3, $t3, 1
5:	jr	$t4
	nop

msg:	.asciz	"\r\nhello from H108N bare metal (asm)\r\n"
	.align	2

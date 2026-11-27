/*
 * Common entry code for the loader stages.  Entered with interrupts off
 * and up to four arguments in a0-a3, which are passed on to MAIN.
 *
 *   -DMAIN=name     C function to call
 *   -DSTACK=addr    optional: set the stack pointer first (stage 2b);
 *                   without it the caller's stack pointer is kept (2a).
 */
	.set	noreorder
	.set	noat
	.section .text.start, "ax", @progbits
	.globl	_start
	.ent	_start
_start:
	move	$s0, $a0
	move	$s1, $a1
	move	$s2, $a2
	move	$s3, $a3
#ifdef STACK
	li	$sp, STACK
#endif

	la	$t0, __bss_start
	la	$t1, __bss_end
1:	sltu	$t2, $t0, $t1
	beqz	$t2, 2f
	nop
	sw	$zero, 0($t0)
	b	1b
	addiu	$t0, $t0, 4

2:	la	$gp, _gp
	move	$a0, $s0
	move	$a1, $s1
	move	$a2, $s2
	jal	MAIN
	move	$a3, $s3

3:	b	3b
	nop
	.end	_start

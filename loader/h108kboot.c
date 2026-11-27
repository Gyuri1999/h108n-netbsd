/*
 * h108kboot: boot a compressed kernel from the router's stock Linux.
 *
 * The module carries two blobs: the second-stage loader (stage2a.bin, which
 * in turn carries stage 2b) and the kernel as a raw DEFLATE stream.  Loaded with insmod, it
 *
 *   1. finds the physical page behind every page of the compressed kernel
 *      by probing the TLB (the module lives in vmalloc space, so its pages
 *      are scattered), and checks each answer by comparing contents;
 *   2. puts a small mover, the second stage and that page list into one
 *      kmalloc buffer, which is reachable without the TLB;
 *   3. turns interrupts off, flushes the caches and jumps to the mover.
 *
 * From there on Linux is dead: the mover copies stage 2a to its fixed
 * address; see loader.h for what the stages do from there.
 * Nothing touches the flash or the stock boot loader; if any check in
 * step 1 fails the module returns an error and Linux carries on.
 *
 * Built without kernel headers; the module format details are the ones
 * documented in h108boot.c.
 */
#include "loader.h"

extern int printk(const char *fmt, ...);
extern void *__kmalloc(unsigned long size, unsigned int flags);

#define GFP_KERNEL	0xd0u
#define PAGE_SIZE	4096ul
#define PAGE_MASK	(~(PAGE_SIZE - 1))
#define KSEG0		0x80000000ul

#ifndef LOAD_ADDR
#define LOAD_ADDR	0x80010000ul
#endif
#ifndef ENTRY_ADDR
#define ENTRY_ADDR	LOAD_ADDR
#endif

int init_module(void);
void cleanup_module(void);

__asm__(
"	.section .rodata\n"
"	.balign 4\n"
"stage2_start:\n"
"	.incbin \"" STAGE2 "\"\n"
"	.balign 4\n"
"stage2_end:\n"
"payload_start:\n"
"	.incbin \"" PAYLOAD "\"\n"
"payload_end:\n"
"	.balign 4\n"
"	.text\n"
"	.set push\n"
"	.set noreorder\n"
"	.set noat\n"
"	.balign 4\n"
/*
 * mover(a0 = second stage, a1 = its length, a2 = params, a3 = stack)
 * Position independent.  Copies stage 2a to STAGE2A_ADDR, flushes
 * both caches and enters it with a0 = params.
 */
"mover_start:\n"
"	lui	$t1, 0x804c\n"
"	move	$t3, $t1\n"
"1:	lw	$t0, 0($a0)\n"
"	addiu	$a0, $a0, 4\n"
"	sw	$t0, 0($t1)\n"
"	addiu	$a1, $a1, -4\n"
"	bgtz	$a1, 1b\n"
"	addiu	$t1, $t1, 4\n"
"	mfc0	$t0, $20\n"
"	nop\n"
"	ori	$t0, $t0, 0x200\n"
"	xori	$t2, $t0, 0x200\n"
"	mtc0	$t2, $20\n"
"	nop\n"
"	nop\n"
"	mtc0	$t0, $20\n"
"	nop\n"
"	nop\n"
"	ori	$t0, $t0, 0x2\n"
"	xori	$t2, $t0, 0x2\n"
"	mtc0	$t2, $20\n"
"	nop\n"
"	nop\n"
"	mtc0	$t0, $20\n"
"	nop\n"
"	nop\n"
"	move	$sp, $a3\n"
"	jr	$t3\n"
"	move	$a0, $a2\n"
"mover_end:\n"
"	.set pop\n"
);
extern const unsigned char stage2_start[], stage2_end[];
extern const unsigned char payload_start[], payload_end[];
extern const unsigned char mover_start[], mover_end[];

static unsigned int
irq_save(void)
{
	unsigned int sr;

	__asm__ volatile("mfc0 %0, $12\n\tnop" : "=r"(sr));
	__asm__ volatile("mtc0 %0, $12\n\tnop\n\tnop" : : "r"(sr & ~1u));
	return sr;
}

static void
irq_restore(unsigned int sr)
{
	__asm__ volatile("mtc0 %0, $12\n\tnop\n\tnop" : : "r"(sr));
}

/*
 * Ask the (R3000-style) TLB which physical page backs a mapped kernel
 * address.  Touching the page loads its entry; the probe then finds it.
 * Returns the KSEG0 address of the page, or 0.
 */
static unsigned long
page_to_kseg0(unsigned long va)
{
	int attempt;

	va &= PAGE_MASK;
	for (attempt = 0; attempt < 64; attempt++) {
		unsigned int sr, hi, idx, lo = 0;

		sr = irq_save();
		(void)*(volatile unsigned char *)va;
		__asm__ volatile(
		"	.set push\n"
		"	.set noreorder\n"
		"	mfc0	%[hi], $10\n"
		"	nop\n"
		"	andi	%[idx], %[hi], 0xfc0\n"
		"	or	%[idx], %[idx], %[va]\n"
		"	mtc0	%[idx], $10\n"
		"	nop\n"
		"	nop\n"
		"	tlbp\n"
		"	nop\n"
		"	nop\n"
		"	mfc0	%[idx], $0\n"
		"	nop\n"
		"	bltz	%[idx], 1f\n"
		"	nop\n"
		"	tlbr\n"
		"	nop\n"
		"	nop\n"
		"	mfc0	%[lo], $2\n"
		"	nop\n"
		"1:	mtc0	%[hi], $10\n"
		"	nop\n"
		"	nop\n"
		"	.set pop\n"
		: [hi] "=&r"(hi), [idx] "=&r"(idx), [lo] "+r"(lo)
		: [va] "r"(va));
		irq_restore(sr);

		/* found, and the entry is valid (V is bit 9 of EntryLo) */
		if ((int)idx >= 0 && (lo & 0x200) != 0)
			return KSEG0 | (lo & PAGE_MASK);
	}
	return 0;
}

static int
same_bytes(const unsigned char *a, const unsigned char *b, unsigned long n)
{
	while (n-- > 0)
		if (*a++ != *b++)
			return 0;
	return 1;
}

static void
copy_bytes(unsigned char *dst, const unsigned char *src, unsigned long n)
{
	while (n-- > 0)
		*dst++ = *src++;
}

static void
cctl_pulse(unsigned int bit)
{
	unsigned int v;

	__asm__ volatile("mfc0 %0, $20\n\tnop" : "=r"(v));
	v |= bit;
	__asm__ volatile("mtc0 %0, $20\n\tnop\n\tnop" : : "r"(v ^ bit));
	__asm__ volatile("mtc0 %0, $20\n\tnop\n\tnop" : : "r"(v));
}

int
init_module(void)
{
	const unsigned long plen = (unsigned long)(payload_end - payload_start);
	const unsigned long s2len = (unsigned long)(stage2_end - stage2_start);
	const unsigned long mlen = (unsigned long)(mover_end - mover_start);
	const unsigned long first = (unsigned long)payload_start & PAGE_MASK;
	const unsigned long off = (unsigned long)payload_start & ~PAGE_MASK;
	const unsigned long npages = (off + plen + PAGE_SIZE - 1) / PAGE_SIZE;
	struct loader_params *p;
	unsigned long *pl, i;
	unsigned char *buf;
	void (*mover)(unsigned long, unsigned long, unsigned long, unsigned long);

	if (plen > GATHER_MAX) {
		printk("<0>h108kboot: compressed kernel too large (%lu > %lu)\n",
		    plen, GATHER_MAX);
		return -27;			/* -EFBIG */
	}

	buf = __kmalloc(mlen + s2len + sizeof(*p) + npages * sizeof(*pl) + 64,
	    GFP_KERNEL);
	if (buf == 0)
		return -12;			/* -ENOMEM */
	if (((unsigned long)buf >> 29) != 4) {	/* must be a KSEG0 address */
		printk("<0>h108kboot: unexpected buffer address %p\n", buf);
		return -5;
	}

	p = (struct loader_params *)(buf + mlen + s2len);
	pl = (unsigned long *)(p + 1);

	for (i = 0; i < npages; i++) {
		const unsigned long va = first + i * PAGE_SIZE;
		const unsigned long ka = page_to_kseg0(va);

		if (ka == 0 ||
		    !same_bytes((const unsigned char *)va,
		    (const unsigned char *)ka, PAGE_SIZE)) {
			printk("<0>h108kboot: cannot locate page %lu "
			    "(va %08lx -> %08lx), giving up\n", i, va, ka);
			return -5;		/* -EIO */
		}
		pl[i] = ka;
	}

	p->pagelist = pl;
	p->len = plen;
	p->off = off;
	p->load = LOAD_ADDR;
	p->entry = ENTRY_ADDR;

	copy_bytes(buf, mover_start, mlen);
	copy_bytes(buf + mlen, stage2_start, s2len);

	printk("<0>h108kboot: %lu bytes in %lu pages located, leaving Linux\n",
	    plen, npages);

	(void)irq_save();
	/* Get the buffer into RAM and drop stale instructions for it. */
	cctl_pulse(0x200);
	cctl_pulse(0x002);

	mover = (void *)buf;
	mover((unsigned long)(buf + mlen), s2len, (unsigned long)p,
	    STAGE2A_STACK);
	for (;;)
		continue;
}

void
cleanup_module(void)
{
}

/* Module metadata, laid out to match the stock kernel's struct module. */
struct h108_module {
	unsigned char	pad0[0x0c];
	char		name[0xbc - 0x0c];
	int		(*init)(void);
	unsigned char	pad1[0x114 - 0xc0];
	void		(*exit)(void);
	unsigned char	pad2[0x11c - 0x118];
};

struct h108_module __this_module
    __attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = "h108kboot",
	.init = init_module,
	.exit = cleanup_module,
};

static const char mi_license[]
    __attribute__((section(".modinfo"), used, aligned(1))) = "license=GPL";
static const char mi_vermagic[]
    __attribute__((section(".modinfo"), used, aligned(1))) =
    "vermagic=2.6.30.9-g597a8e2d-dirty mod_unload RLX 32BIT ";

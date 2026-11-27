/*
 * h108boot: use the router's stock Linux (2.6.30.9, Realtek RSDK) as a boot
 * loader.  Loaded with insmod, it either just prints a line (-DSTAGE=1) or
 * takes over the machine and jumps to an embedded payload.  Nothing here
 * touches the flash or the stock bootloader; a power cycle undoes everything.
 *
 * Built without kernel headers.  What the kernel needs from a module was
 * read out of the stock modules in /lib/modules:
 *   - .modinfo with a matching vermagic string (no symbol versions are used)
 *   - .gnu.linkonce.this_module: 0x11c bytes, name at 0x0c,
 *     init pointer at 0xbc, exit pointer at 0x114
 *
 * STAGE=1  print a line from init_module and return.
 * STAGE=2  jump to a position-independent payload where it sits (no copy).
 * STAGE=3  copy the payload to LOAD_ADDR and jump to ENTRY_ADDR.
 *
 * Everything after "taking over" runs through KSEG1 (uncached), so no cache
 * flushing is needed and stale Linux code in the caches cannot be executed.
 */

extern int printk(const char *fmt, ...);
extern void *__kmalloc(unsigned long size, unsigned int flags);

#define GFP_KERNEL	0xd0u		/* __GFP_WAIT | __GFP_IO | __GFP_FS */
#define KSEG1(p)	((unsigned long)(p) | 0x20000000ul)

#ifndef STAGE
#define STAGE 1
#endif
#ifndef LOAD_ADDR
#define LOAD_ADDR 0xa0000000ul
#endif
#ifndef ENTRY_ADDR
#define ENTRY_ADDR LOAD_ADDR
#endif

int init_module(void);
void cleanup_module(void);

#if STAGE >= 2
/*
 * Payload and a position-independent copy-and-jump trampoline.
 * tramp(dest, src, len, entry): copy len bytes (multiple of 4), jump to entry.
 */
__asm__(
"	.section .rodata\n"
"	.balign 4\n"
"payload_start:\n"
"	.incbin \"" PAYLOAD "\"\n"
"	.balign 4\n"
"payload_end:\n"
"	.text\n"
"	.set push\n"
"	.set noreorder\n"
"	.balign 4\n"
"tramp_start:\n"
"1:	beqz	$a2, 2f\n"
"	nop\n"
"	lw	$t0, 0($a1)\n"
"	addiu	$a1, $a1, 4\n"
"	sw	$t0, 0($a0)\n"
"	addiu	$a0, $a0, 4\n"
"	b	1b\n"
"	addiu	$a2, $a2, -4\n"
"2:	jr	$a3\n"
"	nop\n"
"tramp_end:\n"
"	.set pop\n"
);
extern const unsigned char payload_start[], payload_end[];
extern const unsigned char tramp_start[], tramp_end[];

static void
copy_uncached(volatile unsigned char *dst, const unsigned char *src,
    unsigned long n)
{
	while (n-- > 0)
		*dst++ = *src++;
}

static void
irq_off(void)
{
	unsigned int sr;

	__asm__ volatile("mfc0 %0, $12" : "=r"(sr));
	sr &= ~0x3fu;			/* R3000-style IEc/KUc stack */
	__asm__ volatile("mtc0 %0, $12\n\tnop" : : "r"(sr));
}
#endif

int
init_module(void)
{
#if STAGE == 1
	printk("<0>h108boot: hello from a module built with the NetBSD toolchain\n");
	return 0;
#else
	unsigned long plen = (unsigned long)(payload_end - payload_start);
	unsigned long tlen = (unsigned long)(tramp_end - tramp_start);
	unsigned char *buf, *ubuf;
	void (*tramp)(unsigned long, unsigned long, unsigned long, unsigned long);

	buf = __kmalloc(tlen + plen, GFP_KERNEL);
	if (buf == 0)
		return -12;		/* -ENOMEM */
	ubuf = (unsigned char *)KSEG1(buf);

	/* Written through KSEG1 so the bytes are in RAM, not only in cache. */
	copy_uncached(ubuf, tramp_start, tlen);
	copy_uncached(ubuf + tlen, payload_start, plen);

	printk("<0>h108boot: stage %d, payload %lu bytes at %p, leaving Linux\n",
	    STAGE, plen, ubuf + tlen);

	irq_off();
	tramp = (void *)ubuf;
#if STAGE == 2
	tramp(0, 0, 0, (unsigned long)(ubuf + tlen));
#else
	tramp(LOAD_ADDR, (unsigned long)(ubuf + tlen), plen, ENTRY_ADDR);
#endif
	for (;;)
		continue;
#endif
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
	.name = "h108boot",
	.init = init_module,
	.exit = cleanup_module,
};

static const char mi_license[]
    __attribute__((section(".modinfo"), used, aligned(1))) = "license=GPL";
static const char mi_vermagic[]
    __attribute__((section(".modinfo"), used, aligned(1))) =
    "vermagic=2.6.30.9-g597a8e2d-dirty mod_unload RLX 32BIT ";

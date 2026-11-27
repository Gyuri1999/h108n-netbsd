/*
 * Loader stage 2a for the ZTE H108N.
 *
 * Entered from the Linux module's mover with interrupts off.  It runs at
 * STAGE2A_ADDR, inside the old Linux kernel image, and makes the loader
 * independent of where Linux happened to put things:
 *
 *   1. gather the compressed kernel from its scattered pages into the
 *      start of the old Linux image;
 *   2. move it to just below STAGE2B_ADDR, at the top of RAM;
 *   3. put stage 2b (carried inside this image) at STAGE2B_ADDR and jump.
 *
 * The parameter block and the page list live in memory that step 2 may
 * overwrite, so everything needed later is copied out of them first.
 */
#include "bare.h"
#include "loader.h"

__asm__(
"	.section .rodata\n"
"	.balign 4\n"
"stage2b_start:\n"
"	.incbin \"" STAGE2B "\"\n"
"	.balign 4\n"
"stage2b_end:\n"
"	.text\n"
);
extern const unsigned char stage2b_start[], stage2b_end[];

void	stage2a_main(struct loader_params *);

static void
copy(unsigned char *dst, const unsigned char *src, unsigned long n)
{
	while (n-- > 0)
		*dst++ = *src++;
}

void
stage2a_main(struct loader_params *p)
{
	const unsigned long len = p->len;
	const unsigned long load = p->load;
	const unsigned long entry = p->entry;
	unsigned char *gather = (unsigned char *)GATHER_ADDR;
	unsigned char *high;
	unsigned long done, a;

	WDTCNR = WDT_STOP;

	puts_("\nh108boot 2a: gathering ");
	puthex(len);
	puts_(" bytes\n");

	if (len > GATHER_MAX) {
		puts_("h108boot 2a: compressed kernel too large\n");
		for (;;)
			continue;
	}

	/* 1. scattered pages -> start of the old Linux image */
	for (done = 0, a = p->off; done < len; ) {
		const unsigned char *page =
		    (const unsigned char *)p->pagelist[a >> 12];
		unsigned long chunk = 4096 - (a & 0xfff);

		if (chunk > len - done)
			chunk = len - done;
		copy(gather + done, page + (a & 0xfff), chunk);
		done += chunk;
		a += chunk;
	}

	/* 2. -> top of RAM, ending right below stage 2b */
	high = (unsigned char *)(STAGE2B_ADDR - ((len + 15) & ~15ul));
	copy(high, gather, len);

	/* 3. stage 2b to its own address */
	copy((unsigned char *)STAGE2B_ADDR, stage2b_start,
	    (unsigned long)(stage2b_end - stage2b_start));

	puts_("h108boot 2a: moved to ");
	puthex((unsigned long)high);
	puts_(", starting stage 2b\n");
	uart_drain();

	cache_flush_all();
	((void (*)(unsigned long, unsigned long, unsigned long, unsigned long))
	    STAGE2B_ADDR)((unsigned long)high, len, load, entry);
	for (;;)
		continue;
}

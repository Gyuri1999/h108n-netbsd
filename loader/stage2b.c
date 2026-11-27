/*
 * Loader stage 2b for the ZTE H108N.
 *
 * Runs at STAGE2B_ADDR near the top of RAM, with the compressed kernel in
 * one piece right below it.  Inflates the kernel to its link address,
 * flushes the caches and jumps to it.  The kernel may be as large as the
 * space between its load address and the compressed data.
 */
#include "bare.h"
#include "inflate.h"
#include "loader.h"

void	stage2b_main(unsigned long, unsigned long, unsigned long,
	    unsigned long);

struct src {
	const unsigned char *p, *end;
};

static int
src_getc(void *arg)
{
	struct src *s = arg;

	if (s->p >= s->end)
		return -1;
	return *s->p++;
}

void
stage2b_main(unsigned long src, unsigned long len, unsigned long load,
    unsigned long entry)
{
	struct src s;
	long n;

	WDTCNR = WDT_STOP;

	puts_("h108boot 2b: inflating ");
	puthex(len);
	puts_(" bytes to ");
	puthex(load);
	puts_("\n");

	s.p = (const unsigned char *)src;
	s.end = s.p + len;
	n = inflate_raw((unsigned char *)load, src - load, src_getc, &s);
	if (n < 0) {
		puts_("h108boot 2b: inflate failed, code ");
		puthex((unsigned long)n);
		puts_(" at input offset ");
		puthex((unsigned long)(s.p - (const unsigned char *)src));
		puts_("\n");
		for (;;)
			continue;
	}

	puts_("h108boot 2b: kernel is ");
	puthex((unsigned long)n);
	puts_(" bytes, jumping to ");
	puthex(entry);
	puts_("\n");
	uart_drain();

	cache_flush_all();
	((void (*)(long, long, long, long))entry)(0, 0, 0, 0);
	for (;;)
		continue;
}

#ifndef BARE_H
#define BARE_H

/*
 * Bare-metal helpers shared by the loader stages: polled serial output,
 * the watchdog, and the Lexra cache control register.
 */

#define UART_BASE	0xB8002100u
#define UART_REG(n)	(*(volatile unsigned char *)(UART_BASE + 4 * (n)))
#define UART_THR	UART_REG(0)
#define UART_LSR	UART_REG(5)
#define LSR_THRE	0x20
#define LSR_TEMT	0x40

#define WDTCNR		(*(volatile unsigned int *)0xB800311Cu)
#define WDT_STOP	0xA5000000u

static inline void
putc_(int c)
{
	while ((UART_LSR & LSR_THRE) == 0)
		continue;
	UART_THR = (unsigned char)c;
}

static void __attribute__((unused))
puts_(const char *s)
{
	while (*s != '\0') {
		if (*s == '\n')
			putc_('\r');
		putc_(*s++);
	}
}

static void __attribute__((unused))
puthex(unsigned long v)
{
	int i;

	for (i = 28; i >= 0; i -= 4)
		putc_("0123456789abcdef"[(v >> i) & 0xf]);
}

static inline void
uart_drain(void)
{
	while ((UART_LSR & LSR_TEMT) == 0)
		continue;
}

/* Lexra cache control: an operation starts when its bit goes 0 -> 1. */
static inline void
cctl_pulse(unsigned int bit)
{
	unsigned int v;

	__asm__ volatile("mfc0 %0, $20\n\tnop" : "=r"(v));
	v |= bit;
	__asm__ volatile("mtc0 %0, $20\n\tnop\n\tnop" : : "r"(v ^ bit));
	__asm__ volatile("mtc0 %0, $20\n\tnop\n\tnop" : : "r"(v));
}

/* Write everything back to RAM and drop all cached instructions. */
static inline void
cache_flush_all(void)
{
	cctl_pulse(0x200);
	cctl_pulse(0x002);
}

#endif

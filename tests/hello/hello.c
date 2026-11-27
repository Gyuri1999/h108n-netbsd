/*
 * Serial "hello" for the ZTE H108N.  Proves three things before any NetBSD
 * code is involved: the load/entry address, the UART address, and that the
 * patched compiler produces code this Lexra core can run.
 *
 * UART: 16550A at 0xB8002100, registers 4 bytes apart (found in the stock
 * bootloader and Linux kernel).  On this big-endian SoC each register is the
 * top byte of its 32-bit word, i.e. the byte at the word's own address.
 */
#define UART_BASE	0xB8002100u
#define UART_REG(n)	(*(volatile unsigned char *)(UART_BASE + 4 * (n)))
#define UART_THR	UART_REG(0)
#define UART_LSR	UART_REG(5)
#define LSR_THRE	0x20

/* Watchdog control register (address and values read from the stock kernel). */
#define WDTCNR		(*(volatile unsigned int *)0xB800311Cu)
#define WDT_STOP	0xA5000000u

static void
putc_(int c)
{
	while ((UART_LSR & LSR_THRE) == 0)
		continue;
	UART_THR = (unsigned char)c;
}

static void
puts_(const char *s)
{
	while (*s != '\0') {
		if (*s == '\n')
			putc_('\r');
		putc_(*s++);
	}
}

static void
puthex(unsigned int v)
{
	int i;

	for (i = 28; i >= 0; i -= 4)
		putc_("0123456789abcdef"[(v >> i) & 0xf]);
}

/* Packed struct forces unaligned access: with stock GCC this is lwl/lwr. */
struct __attribute__((packed)) unaligned {
	unsigned char pad;
	unsigned int word;
};

static unsigned int
read_prid(void)
{
	unsigned int v;

	__asm__ volatile("mfc0 %0, $15" : "=r"(v));
	return v;
}

int
main(void)
{
	volatile struct unaligned u;
	unsigned int n;

	puts_("\nhello from H108N bare metal\n");
	puts_("cpu prid: 0x");
	puthex(read_prid());
	puts_("\n");

	u.word = 0x12345678;
	puts_("unaligned load: 0x");
	puthex(u.word);
	puts_(u.word == 0x12345678 ? " ok\n" : " WRONG\n");

	/*
	 * Stop the watchdog the stock Linux leaves running, otherwise the
	 * board resets about a second after we take over.  The stock kernel
	 * writes 0xA5000000 here in one place and 0x00E00000 (enable) in two
	 * others; 0 is its reboot path.
	 */
	puts_("watchdog reg before: 0x");
	puthex(WDTCNR);
	WDTCNR = WDT_STOP;
	puts_(" after: 0x");
	puthex(WDTCNR);
	puts_("\n");

	for (n = 0;; n++) {
		volatile unsigned int d;

		puts_("tick ");
		puthex(n);
		puts_("\n");
		for (d = 0; d < 1000000; d++)
			continue;
	}
}

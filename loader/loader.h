#ifndef LOADER_H
#define LOADER_H

/*
 * Memory plan of the kernel loader (32 MB of RAM at 0x80000000).
 *
 * The old Linux kernel image occupies 0x80002000..0x804e0000.  Nothing that
 * Linux allocated at run time lives inside it, so once Linux is stopped it
 * is the one region that is known not to hold the loader's own data (the
 * compressed kernel sits in scattered pages elsewhere).
 *
 *   stage 2a  runs inside that region.  It gathers the compressed kernel
 *             into the start of the region, then moves it, together with
 *             stage 2b, to the top of RAM.  After that everything below
 *             is free.
 *   stage 2b  runs at the top of RAM and inflates the kernel to its link
 *             address; the kernel may now be larger than the old Linux
 *             image without running over the loader.
 */
#define GATHER_ADDR	0x80010000ul	/* compressed kernel, gathered */
#define STAGE2A_ADDR	0x804c0000ul	/* must match the mover in h108kboot.c */
#define STAGE2A_STACK	(STAGE2A_ADDR + 0x20000ul - 32)
#define STAGE2B_ADDR	0x81f00000ul
#define STAGE2B_STACK	(STAGE2B_ADDR + 0x10000ul - 32)

/* Largest compressed kernel that fits between GATHER_ADDR and stage 2a. */
#define GATHER_MAX	(STAGE2A_ADDR - GATHER_ADDR)

/* Handed from the Linux module to stage 2a. */
struct loader_params {
	unsigned long	*pagelist;	/* KSEG0 address of each payload page */
	unsigned long	len;		/* compressed length in bytes */
	unsigned long	off;		/* offset of the data in the first page */
	unsigned long	load;		/* where the kernel image goes */
	unsigned long	entry;		/* where to jump */
};

#endif

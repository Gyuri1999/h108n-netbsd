#ifndef INFLATE_H
#define INFLATE_H

/* Negative return values of inflate_raw() */
#define INFLATE_EINPUT	(-1)	/* compressed data ended early */
#define INFLATE_EDATA	(-2)	/* compressed data is not valid */
#define INFLATE_EOUTPUT	(-3)	/* output buffer too small */

/* Returns the next compressed byte (0..255), or -1 when there is none. */
typedef int (*inflate_getc_t)(void *);

/* Decode a raw DEFLATE stream; returns the output size or a negative error. */
long	inflate_raw(unsigned char *out, unsigned long outlen,
	    inflate_getc_t get, void *arg);

#endif

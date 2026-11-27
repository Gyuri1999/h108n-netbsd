/*
 * Small freestanding decoder for raw DEFLATE streams (RFC 1951).
 *
 * Input comes one byte at a time from a callback, so the compressed data
 * does not have to be contiguous in memory; output goes to one flat buffer.
 * Huffman codes are decoded bit by bit from canonical tables: slow, tiny,
 * and without any large lookup tables.  No libc, no globals.
 */
#include "inflate.h"

#define MAXBITS		15
#define MAXLCODES	286
#define MAXDCODES	30
#define FIXLCODES	288

struct huff {
	short count[MAXBITS + 1];	/* number of codes of each length */
	short symbol[FIXLCODES];	/* symbols ordered by code */
};

struct inf {
	inflate_getc_t get;
	void *arg;
	unsigned char *out;
	unsigned long outlen, outcnt;
	unsigned long bitbuf;
	int bitcnt;
	int err;
};

static int
getbits(struct inf *s, int need)
{
	unsigned long val = s->bitbuf;

	while (s->bitcnt < need) {
		int c = s->get(s->arg);

		if (c < 0) {
			s->err = INFLATE_EINPUT;
			return 0;
		}
		val |= (unsigned long)c << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = val >> need;
	s->bitcnt -= need;
	return (int)(val & ((1UL << need) - 1));
}

/* Build canonical decoding tables; returns >0 if the code is incomplete. */
static int
construct(struct huff *h, const short *length, int n)
{
	short offs[MAXBITS + 1];
	int symbol, len, left;

	for (len = 0; len <= MAXBITS; len++)
		h->count[len] = 0;
	for (symbol = 0; symbol < n; symbol++)
		h->count[length[symbol]]++;
	if (h->count[0] == n)
		return 0;		/* no codes at all: legal */

	left = 1;
	for (len = 1; len <= MAXBITS; len++) {
		left <<= 1;
		left -= h->count[len];
		if (left < 0)
			return left;	/* over-subscribed */
	}

	offs[1] = 0;
	for (len = 1; len < MAXBITS; len++)
		offs[len + 1] = offs[len] + h->count[len];
	for (symbol = 0; symbol < n; symbol++)
		if (length[symbol] != 0)
			h->symbol[offs[length[symbol]]++] = (short)symbol;

	return left;
}

static int
decode(struct inf *s, const struct huff *h)
{
	int code = 0, first = 0, index = 0, len;

	for (len = 1; len <= MAXBITS; len++) {
		int count = h->count[len];

		code |= getbits(s, 1);
		if (s->err)
			return -1;
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	s->err = INFLATE_EDATA;
	return -1;
}

static void
codes(struct inf *s, const struct huff *lencode, const struct huff *distcode)
{
	static const short lens[29] = {
		3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const short lext[29] = {
		0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const short dists[30] = {
		1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
		8193, 12289, 16385, 24577 };
	static const short dext[30] = {
		0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

	for (;;) {
		int symbol = decode(s, lencode);
		unsigned long dist;
		int len;

		if (s->err)
			return;
		if (symbol < 256) {
			if (s->outcnt == s->outlen) {
				s->err = INFLATE_EOUTPUT;
				return;
			}
			s->out[s->outcnt++] = (unsigned char)symbol;
			continue;
		}
		if (symbol == 256)
			return;			/* end of block */

		symbol -= 257;
		if (symbol >= 29) {
			s->err = INFLATE_EDATA;
			return;
		}
		len = lens[symbol] + getbits(s, lext[symbol]);

		symbol = decode(s, distcode);
		if (s->err)
			return;
		if (symbol >= 30) {
			s->err = INFLATE_EDATA;
			return;
		}
		dist = (unsigned long)dists[symbol] + getbits(s, dext[symbol]);
		if (s->err)
			return;
		if (dist > s->outcnt) {
			s->err = INFLATE_EDATA;
			return;
		}
		if (s->outcnt + len > s->outlen) {
			s->err = INFLATE_EOUTPUT;
			return;
		}
		while (len-- > 0) {
			s->out[s->outcnt] = s->out[s->outcnt - dist];
			s->outcnt++;
		}
	}
}

static void
stored(struct inf *s)
{
	unsigned int len, nlen;

	s->bitbuf = 0;
	s->bitcnt = 0;
	len = getbits(s, 8);
	len |= (unsigned int)getbits(s, 8) << 8;
	nlen = getbits(s, 8);
	nlen |= (unsigned int)getbits(s, 8) << 8;
	if (s->err)
		return;
	if ((len ^ nlen) != 0xffff) {
		s->err = INFLATE_EDATA;
		return;
	}
	if (s->outcnt + len > s->outlen) {
		s->err = INFLATE_EOUTPUT;
		return;
	}
	while (len-- > 0) {
		int c = s->get(s->arg);

		if (c < 0) {
			s->err = INFLATE_EINPUT;
			return;
		}
		s->out[s->outcnt++] = (unsigned char)c;
	}
}

static void
fixed(struct inf *s)
{
	struct huff lencode, distcode;
	short lengths[FIXLCODES];
	int symbol;

	for (symbol = 0; symbol < 144; symbol++)
		lengths[symbol] = 8;
	for (; symbol < 256; symbol++)
		lengths[symbol] = 9;
	for (; symbol < 280; symbol++)
		lengths[symbol] = 7;
	for (; symbol < FIXLCODES; symbol++)
		lengths[symbol] = 8;
	construct(&lencode, lengths, FIXLCODES);

	for (symbol = 0; symbol < MAXDCODES; symbol++)
		lengths[symbol] = 5;
	construct(&distcode, lengths, MAXDCODES);

	codes(s, &lencode, &distcode);
}

static void
dynamic(struct inf *s)
{
	static const short order[19] = {
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	struct huff lencode, distcode;
	short lengths[MAXLCODES + MAXDCODES];
	int nlen, ndist, ncode, index, err;

	nlen = getbits(s, 5) + 257;
	ndist = getbits(s, 5) + 1;
	ncode = getbits(s, 4) + 4;
	if (s->err)
		return;
	if (nlen > MAXLCODES || ndist > MAXDCODES) {
		s->err = INFLATE_EDATA;
		return;
	}

	for (index = 0; index < ncode; index++)
		lengths[order[index]] = (short)getbits(s, 3);
	for (; index < 19; index++)
		lengths[order[index]] = 0;
	if (s->err)
		return;

	/* The code length code must be complete. */
	if (construct(&lencode, lengths, 19) != 0) {
		s->err = INFLATE_EDATA;
		return;
	}

	index = 0;
	while (index < nlen + ndist) {
		int symbol = decode(s, &lencode);
		int len, repeat;

		if (s->err)
			return;
		if (symbol < 16) {
			lengths[index++] = (short)symbol;
			continue;
		}
		len = 0;
		if (symbol == 16) {
			if (index == 0) {
				s->err = INFLATE_EDATA;
				return;
			}
			len = lengths[index - 1];
			repeat = 3 + getbits(s, 2);
		} else if (symbol == 17) {
			repeat = 3 + getbits(s, 3);
		} else {
			repeat = 11 + getbits(s, 7);
		}
		if (s->err)
			return;
		if (index + repeat > nlen + ndist) {
			s->err = INFLATE_EDATA;
			return;
		}
		while (repeat-- > 0)
			lengths[index++] = (short)len;
	}

	if (lengths[256] == 0) {		/* no end-of-block code */
		s->err = INFLATE_EDATA;
		return;
	}

	/* Incomplete codes are only allowed when they have a single code. */
	err = construct(&lencode, lengths, nlen);
	if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) {
		s->err = INFLATE_EDATA;
		return;
	}
	err = construct(&distcode, lengths + nlen, ndist);
	if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) {
		s->err = INFLATE_EDATA;
		return;
	}

	codes(s, &lencode, &distcode);
}

long
inflate_raw(unsigned char *out, unsigned long outlen, inflate_getc_t get,
    void *arg)
{
	struct inf s;
	int last;

	s.get = get;
	s.arg = arg;
	s.out = out;
	s.outlen = outlen;
	s.outcnt = 0;
	s.bitbuf = 0;
	s.bitcnt = 0;
	s.err = 0;

	do {
		int type;

		last = getbits(&s, 1);
		type = getbits(&s, 2);
		if (s.err)
			break;
		switch (type) {
		case 0:
			stored(&s);
			break;
		case 1:
			fixed(&s);
			break;
		case 2:
			dynamic(&s);
			break;
		default:
			s.err = INFLATE_EDATA;
			break;
		}
	} while (!last && !s.err);

	return s.err ? (long)s.err : (long)s.outcnt;
}

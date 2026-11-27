/*
 * h108panel: a small web panel for the stock Linux of the ZTE ZXHN H108N.
 *
 * Runs on the router itself, from RAM (/var), next to the vendor software.
 * It serves one page and a small API on port 8672:
 *
 *   GET  /api/status   Wi-Fi tables, DHCP clients, interface counters,
 *                      system figures and the DNS block list.  No password
 *                      needed; key and passphrase fields are never included.
 *   POST /api/set      change a short, fixed list of settings (Wi-Fi, and
 *                      the DHCP server switch); /api/wifi is the same call
 *   POST /api/dns      add or remove a name on the DNS block list, or set
 *                      the DNS server that other names are forwarded to
 *
 * Changes need the panel password, which is read at start from the first
 * line of /var/h108panel.key (4 to 64 characters; the owner asked for short
 * ones to be accepted).  Without that file the panel is read-only.
 *
 *   h108panel -d       development mode: also allows reading any settings
 *                      table (/api/table?name=X) and a few fixed files
 *                      (/api/file?name=X) without a password.  Secrets are
 *                      filtered there as well.  Not for everyday use.
 *
 * Settings go through the vendor tool, "sendcmd 1 DB p|set|save".  DNS
 * blocking uses the hosts file of the vendor's dnsmasq, /var/tmp/hosts: a
 * line "0.0.0.0 name" makes the router answer 0.0.0.0 for that name.  The
 * vendor daemon rewrites that file now and then, so a helper process puts
 * our lines back when they go missing.  Forwarding works the same way: the
 * vendor's dnsmasq takes its servers from /var/tmp/default_resolv.conf and
 * accepts a plain "nameserver ADDRESS" line there (checked on the device).
 *
 * No C library: the program talks to the kernel directly (Linux, MIPS o32)
 * so that it can be built with the NetBSD cross compiler used for the rest
 * of this project.  One request per connection, one connection at a time.
 */

#define PORT		8672
#define SENDCMD		"/bin/sendcmd"
#define BUSYBOX		"/bin/busybox"
#define KEYFILE		"/var/h108panel.key"
#define BLOCKFILE	"/var/h108panel.block"
#define HOSTSFILE	"/var/tmp/hosts"
#define RESOLVFILE	"/var/tmp/default_resolv.conf"
#define UPFILE		"/var/h108panel.upstream"	/* address we forward to */
#define RESOLVORIG	"/var/h108panel.resolv.orig"	/* vendor file, as found */
#define BLOCK_MAX	(12 * 1024)

/* Linux/MIPS o32 system call numbers */
#define SYS_exit	4001
#define SYS_fork	4002
#define SYS_read	4003
#define SYS_write	4004
#define SYS_open	4005
#define SYS_close	4006
#define SYS_execve	4011
#define SYS_dup2	4063
#define SYS_setsid	4066
#define SYS_wait4	4114
#define SYS_nanosleep	4166
#define SYS_accept	4168
#define SYS_bind	4169
#define SYS_listen	4174
#define SYS_send	4178
#define SYS_setsockopt	4181
#define SYS_socket	4183

#define O_WRONLY	0x0001
#define O_CREAT		0x0100		/* MIPS numbering */
#define O_TRUNC		0x0200

#define AF_INET		2
#define SOCK_STREAM	2		/* MIPS numbering */
#define SOL_SOCKET	0xffff
#define SO_REUSEADDR	0x0004
#define SO_RCVTIMEO	0x1006
#define SO_SNDTIMEO	0x1005
#define MSG_NOSIGNAL	0x4000

typedef unsigned long size_t;

long	raw_syscall(long n, long a0, long a1, long a2, long a3, long a4);
long	raw_pipe(int *fds);
void	cmain(long *sp);

__asm__(
"	.text\n"
"	.set	push\n"
"	.set	noreorder\n"
"	.globl	__start\n"
"	.ent	__start\n"
"__start:\n"
"	move	$4, $29\n"		/* a0 = &argc */
"	li	$8, -8\n"
"	and	$29, $29, $8\n"
"	addiu	$29, $29, -32\n"
"	jal	cmain\n"
"	nop\n"
"	.end	__start\n"
/* raw_syscall(n, a0, a1, a2, a3, a4): returns the result, or -errno */
"	.globl	raw_syscall\n"
"	.ent	raw_syscall\n"
"raw_syscall:\n"
"	move	$2, $4\n"
"	move	$4, $5\n"
"	move	$5, $6\n"
"	move	$6, $7\n"
"	lw	$7, 16($29)\n"
"	lw	$8, 20($29)\n"
"	addiu	$29, $29, -32\n"
"	sw	$8, 16($29)\n"
"	syscall\n"
"	addiu	$29, $29, 32\n"
"	beqz	$7, 1f\n"
"	nop\n"
"	negu	$2, $2\n"
"1:	jr	$31\n"
"	nop\n"
"	.end	raw_syscall\n"
/* raw_pipe(fds): pipe() returns its two descriptors in v0 and v1 */
"	.globl	raw_pipe\n"
"	.ent	raw_pipe\n"
"raw_pipe:\n"
"	addiu	$29, $29, -32\n"
"	sw	$4, 24($29)\n"
"	li	$2, 4042\n"
"	syscall\n"
"	lw	$8, 24($29)\n"
"	addiu	$29, $29, 32\n"
"	bnez	$7, 1f\n"
"	nop\n"
"	sw	$2, 0($8)\n"
"	sw	$3, 4($8)\n"
"	move	$2, $0\n"
"1:	jr	$31\n"
"	nop\n"
"	.end	raw_pipe\n"
"	.set	pop\n"
/* the page, built in */
"	.section .rodata\n"
"page_start:\n"
"	.incbin	\"" PAGE "\"\n"
"page_end:\n"
"	.text\n"
);
extern const char page_start[], page_end[];

#define SC(n, a, b, c, d, e) \
	raw_syscall((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e))

/* --- the few libc routines needed (the compiler may also call these) --- */
void *
memcpy(void *d, const void *s, size_t n)
{
	char *dd = d;
	const char *ss = s;

	while (n-- > 0)
		*dd++ = *ss++;
	return d;
}

void *
memset(void *d, int c, size_t n)
{
	char *dd = d;

	while (n-- > 0)
		*dd++ = (char)c;
	return d;
}

static size_t
slen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

static int
seq(const char *a, const char *b)
{
	while (*a != '\0' && *a == *b)
		a++, b++;
	return *a == *b;
}

static int
starts(const char *s, const char *prefix)
{
	while (*prefix != '\0')
		if (*s++ != *prefix++)
			return 0;
	return 1;
}

static const char *
find(const char *hay, const char *needle)
{
	for (; *hay != '\0'; hay++)
		if (starts(hay, needle))
			return hay;
	return 0;
}

/* --- small file helpers --- */
static long
slurp(const char *path, char *buf, size_t size)
{
	long fd, n, total = 0;

	fd = SC(SYS_open, path, 0, 0, 0, 0);
	if (fd < 0) {
		buf[0] = '\0';
		return -1;
	}
	while ((size_t)total < size - 1) {
		n = SC(SYS_read, fd, buf + total, size - 1 - (size_t)total, 0, 0);
		if (n <= 0)
			break;
		total += n;
	}
	buf[total] = '\0';
	SC(SYS_close, fd, 0, 0, 0, 0);
	return total;
}

static int
spit(const char *path, const char *buf, size_t n)
{
	long fd = SC(SYS_open, path, O_WRONLY | O_CREAT | O_TRUNC, 0644, 0, 0);

	if (fd < 0)
		return -1;
	while (n > 0) {
		long r = SC(SYS_write, fd, buf, n, 0, 0);

		if (r <= 0)
			break;
		buf += r;
		n -= (size_t)r;
	}
	SC(SYS_close, fd, 0, 0, 0, 0);
	return n == 0 ? 0 : -1;
}

/* --- output buffer for one response --- */
static char out[56 * 1024];
static size_t outlen;

static void
putn(const char *s, size_t n)
{
	while (n-- > 0 && outlen < sizeof(out) - 1)
		out[outlen++] = *s++;
}

static void
put(const char *s)
{
	putn(s, slen(s));
}

/* a JSON string, from n bytes of arbitrary text */
static void
putjson(const char *s, size_t n)
{
	static const char hex[] = "0123456789abcdef";

	put("\"");
	for (; n > 0; n--, s++) {
		unsigned char c = (unsigned char)*s;

		if (c == '"' || c == '\\') {
			putn("\\", 1);
			putn(s, 1);
		} else if (c == '\n') {
			put("\\n");
		} else if (c < 0x20 || c == 0x7f) {
			put("\\u00");
			putn(&hex[c >> 4], 1);
			putn(&hex[c & 15], 1);
		} else {
			putn(s, 1);
		}
	}
	put("\"");
}

static void
send_all(int fd, const char *p, size_t n)
{
	while (n > 0) {
		long r = SC(SYS_send, fd, p, n, MSG_NOSIGNAL, 0);

		if (r <= 0)
			return;
		p += r;
		n -= (size_t)r;
	}
}

static void
respond(int fd, const char *status, const char *ctype, const char *body,
    size_t len)
{
	static char head[256];
	size_t hl = 0;

#define H(s) do { const char *q = (s); while (*q != '\0' && hl < sizeof(head) - 1) head[hl++] = *q++; } while (0)
	H("HTTP/1.0 ");
	H(status);
	H("\r\nContent-Type: ");
	H(ctype);
	H("\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n");
#undef H
	send_all(fd, head, hl);
	send_all(fd, body, len);
}

static void
respond_json(int fd, const char *status)
{
	respond(fd, status, "application/json", out, outlen);
}

static void
respond_error(int fd, const char *status, const char *msg)
{
	outlen = 0;
	put("{\"error\":");
	putjson(msg, slen(msg));
	put("}");
	respond_json(fd, status);
}

/* --- running other programs --- */
static char cmdbuf[40 * 1024];

/* Run path with argv, capture its output in cmdbuf; returns bytes or -1. */
static long
run(const char *path, char *const argv[])
{
	static char *const envp[] = { "PATH=/bin:/sbin:/usr/bin:/usr/sbin", 0 };
	int fds[2], status = 0;
	long pid, n, total = 0;

	if (raw_pipe(fds) != 0)
		return -1;
	pid = SC(SYS_fork, 0, 0, 0, 0, 0);
	if (pid < 0) {
		SC(SYS_close, fds[0], 0, 0, 0, 0);
		SC(SYS_close, fds[1], 0, 0, 0, 0);
		return -1;
	}
	if (pid == 0) {
		SC(SYS_dup2, fds[1], 1, 0, 0, 0);
		SC(SYS_dup2, fds[1], 2, 0, 0, 0);
		SC(SYS_close, fds[0], 0, 0, 0, 0);
		SC(SYS_close, fds[1], 0, 0, 0, 0);
		SC(SYS_execve, path, argv, envp, 0, 0);
		SC(SYS_exit, 127, 0, 0, 0, 0);
	}
	SC(SYS_close, fds[1], 0, 0, 0, 0);
	while (total < (long)sizeof(cmdbuf) - 1) {
		n = SC(SYS_read, fds[0], cmdbuf + total,
		    sizeof(cmdbuf) - 1 - (size_t)total, 0, 0);
		if (n <= 0)
			break;
		total += n;
	}
	cmdbuf[total] = '\0';
	SC(SYS_close, fds[0], 0, 0, 0, 0);
	SC(SYS_wait4, pid, &status, 0, 0, 0);
	return total;
}

/* Fields that must never leave the router through an open call. */
static int
is_secret(const char *name, size_t n)
{
	static const char *const words[] = { "Key", "Passphrase", "Secret",
	    "Password", "Passwd", "PIN", "UserName", "Username", 0 };
	size_t i, j;

	for (i = 0; words[i] != 0; i++) {
		size_t wl = slen(words[i]);

		for (j = 0; j + wl <= n; j++)
			if (starts(name + j, words[i]))
				return 1;
	}
	return 0;
}

/*
 * "sendcmd 1 DB p TABLE" prints
 *	<Tbl name="T" RowCount="n"> <Row No="0"> <DM name="F" val="V"/> ...
 * Turn that into  "T":[{"F":"V",...},...]
 */
static void
table_json(const char *table)
{
	char *argv[] = { SENDCMD, "1", "DB", "p", (char *)table, 0 };
	const char *p;
	int rows = 0, fields = 0, inrow = 0;

	putjson(table, slen(table));
	put(":[");
	if (run(SENDCMD, argv) > 0) {
		for (p = cmdbuf; *p != '\0'; p++) {
			if (starts(p, "<Row ")) {
				if (inrow)
					put("}");
				put(rows++ > 0 ? ",{" : "{");
				inrow = 1;
				fields = 0;
			} else if (inrow && starts(p, "<DM name=\"")) {
				const char *name = p + 10, *ne, *val, *ve;

				for (ne = name; *ne != '\0' && *ne != '"'; ne++)
					continue;
				if (!starts(ne, "\" val=\""))
					continue;
				val = ne + 7;
				for (ve = val; *ve != '\0' && *ve != '"' &&
				    *ve != '\n'; ve++)
					continue;
				if (is_secret(name, (size_t)(ne - name)))
					continue;
				if (fields++ > 0)
					put(",");
				putjson(name, (size_t)(ne - name));
				put(":");
				putjson(val, (size_t)(ve - val));
				p = ve;
			}
		}
		if (inrow)
			put("}");
	}
	put("]");
}

static void
file_json(const char *key, const char *path)
{
	static char buf[BLOCK_MAX + 1024];	/* the block list is the largest */
	long n = slurp(path, buf, sizeof(buf));

	putjson(key, slen(key));
	put(":");
	putjson(buf, n > 0 ? (size_t)n : 0);
}

/* --- DNS block list --- */
static char blocklist[BLOCK_MAX];	/* names, one per line */
static char hostsbuf[BLOCK_MAX + 8192];
static char hostsnew[2 * BLOCK_MAX + 8192];

static int
valid_name(const char *s)
{
	size_t n = slen(s), i;
	int dot = 0;

	if (n < 3 || n > 100 || s[0] == '.' || s[0] == '-' || s[n - 1] == '.')
		return 0;
	for (i = 0; i < n; i++) {
		char c = s[i];

		if (c == '.') {
			if (s[i + 1] == '.')
				return 0;
			dot = 1;
		} else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		    c == '-' || c == '_')) {
			return 0;
		}
	}
	return dot;
}

/* is `name` one of the lines of blocklist? */
static int
blocked(const char *name)
{
	size_t nl = slen(name);
	const char *p = blocklist;

	while (*p != '\0') {
		const char *e = p;

		while (*e != '\0' && *e != '\n')
			e++;
		if ((size_t)(e - p) == nl && starts(p, name))
			return 1;
		p = (*e == '\n') ? e + 1 : e;
	}
	return 0;
}

static void
dnsmasq_reload(void)
{
	char *argv[] = { "killall", "-HUP", "dnsmasq", 0 };

	run(BUSYBOX, argv);
}

/*
 * Rewrite the hosts file: keep every line that is not a "0.0.0.0 ..."
 * line (those belong to the vendor daemon), then add ours.
 * Returns 1 if the file was changed.
 */
static int
hosts_apply(void)
{
	size_t o = 0;
	const char *p;

	slurp(BLOCKFILE, blocklist, sizeof(blocklist));
	slurp(HOSTSFILE, hostsbuf, sizeof(hostsbuf));

	for (p = hostsbuf; *p != '\0'; ) {
		const char *e = p;

		while (*e != '\0' && *e != '\n')
			e++;
		if (!starts(p, "0.0.0.0 ") && e > p &&
		    o + (size_t)(e - p) + 1 < sizeof(hostsnew)) {
			memcpy(hostsnew + o, p, (size_t)(e - p));
			o += (size_t)(e - p);
			hostsnew[o++] = '\n';
		}
		p = (*e == '\n') ? e + 1 : e;
	}
	for (p = blocklist; *p != '\0'; ) {
		const char *e = p;

		while (*e != '\0' && *e != '\n')
			e++;
		if (e > p && o + (size_t)(e - p) + 10 < sizeof(hostsnew)) {
			memcpy(hostsnew + o, "0.0.0.0 ", 8);
			o += 8;
			memcpy(hostsnew + o, p, (size_t)(e - p));
			o += (size_t)(e - p);
			hostsnew[o++] = '\n';
		}
		p = (*e == '\n') ? e + 1 : e;
	}
	hostsnew[o] = '\0';
	if (seq(hostsnew, hostsbuf))
		return 0;
	spit(HOSTSFILE, hostsnew, o);
	dnsmasq_reload();
	return 1;
}

static int
valid_ip(const char *s)
{
	int parts = 0, digits = 0, val = 0;

	for (;; s++) {
		if (*s >= '0' && *s <= '9') {
			val = val * 10 + (*s - '0');
			if (++digits > 3 || val > 255)
				return 0;
		} else if (*s == '.' || *s == '\0') {
			if (digits == 0)
				return 0;
			parts++;
			digits = val = 0;
			if (*s == '\0')
				break;
		} else {
			return 0;
		}
	}
	return parts == 4;
}

/*
 * Point the vendor's dnsmasq at the server named in UPFILE.  The vendor
 * file is saved once, before the first change, so that turning this off
 * can put it back.  Returns 1 if the file was changed.
 */
static int
upstream_apply(void)
{
	static char want[64], have[512], orig[512];
	char ip[32];
	long n;
	size_t i, o = 0;

	n = slurp(UPFILE, ip, sizeof(ip));
	for (i = 0; n > 0 && i < (size_t)n && ip[i] != '\n' && ip[i] != '\r'; i++)
		continue;
	ip[n > 0 ? i : 0] = '\0';
	if (!valid_ip(ip))
		return 0;			/* not managing the file */

	memcpy(want, "nameserver ", 11);
	o = 11;
	memcpy(want + o, ip, slen(ip));
	o += slen(ip);
	want[o++] = '\n';
	want[o] = '\0';

	n = slurp(RESOLVFILE, have, sizeof(have));
	if (n >= 0 && seq(have, want))
		return 0;
	if (slurp(RESOLVORIG, orig, sizeof(orig)) < 0)
		spit(RESOLVORIG, have, n > 0 ? (size_t)n : 0);
	spit(RESOLVFILE, want, o);
	dnsmasq_reload();
	return 1;
}

/* Stop forwarding: put the vendor's file back as we found it. */
static void
upstream_off(void)
{
	static char orig[512];
	long n = slurp(RESOLVORIG, orig, sizeof(orig));

	spit(UPFILE, "", 0);
	if (n >= 0) {
		spit(RESOLVFILE, orig, (size_t)n);
		dnsmasq_reload();
	}
}

/* The vendor daemon regenerates these files; put our lines back. */
static void
keeper(void)
{
	struct { long sec, nsec; } ts;

	for (;;) {
		ts.sec = 20;
		ts.nsec = 0;
		SC(SYS_nanosleep, &ts, 0, 0, 0, 0);
		hosts_apply();
		upstream_apply();
	}
}

/* --- changes --- */
static char panel_key[72];		/* empty: read-only mode */
static int debug_mode;

struct setting {
	const char *param;		/* name in the request */
	const char *table, *row, *field;
	int kind;			/* 0: 0/1   1: number lo..hi   2: text */
	int lo, hi;
};

static const struct setting settings[] = {
	{ "enable",   "WLANCfg",  "0", "Enable",             0, 0, 1 },
	{ "ssid",     "WLANCfg",  "0", "ESSID",              2, 1, 32 },
	{ "hidden",   "WLANCfg",  "0", "ESSIDHideEnable",    0, 0, 1 },
	{ "maxusers", "WLANCfg",  "0", "MaxUserNum",         1, 1, 32 },
	{ "autochan", "WLANBase", "0", "AutoChannelEnabled", 0, 0, 1 },
	{ "channel",  "WLANBase", "0", "Channel",            1, 1, 13 },
	{ "wifipass", "WLANPSK",  "0", "KeyPassphrase",      2, 8, 63 },
	{ "dhcp",     "DHCPSHostCfg", "0", "ServerEnable",   0, 0, 1 },
	{ 0, 0, 0, 0, 0, 0, 0 }
};

static int
valid(const struct setting *s, const char *v)
{
	size_t n = slen(v), i;
	long num = 0;

	if (s->kind == 2) {
		if (n < (size_t)s->lo || n > (size_t)s->hi)
			return 0;
		for (i = 0; i < n; i++)		/* printable ASCII only */
			if (v[i] < 0x20 || v[i] > 0x7e || v[i] == '"' ||
			    v[i] == '\\' || v[i] == '<' || v[i] == '>' ||
			    v[i] == '&')
				return 0;
		/* the vendor tool prints "***" for hidden values: never store that */
		for (i = 0; i < n && v[i] == '*'; i++)
			continue;
		return i < n;
	}
	if (n == 0 || n > 3)
		return 0;
	for (i = 0; i < n; i++) {
		if (v[i] < '0' || v[i] > '9')
			return 0;
		num = num * 10 + (v[i] - '0');
	}
	return num >= s->lo && num <= s->hi;
}

/* constant-time comparison of the supplied key with ours */
static int
key_ok(const char *given)
{
	size_t i, n = slen(panel_key), g = slen(given);
	unsigned char diff = (unsigned char)(n ^ g);

	for (i = 0; i < n; i++)
		diff |= (unsigned char)(panel_key[i] ^ given[i < g ? i : 0]);
	return diff == 0 && n == g;
}

/* decode one application/x-www-form-urlencoded value in place */
static void
urldecode(char *s)
{
	char *o = s;

	for (; *s != '\0'; s++) {
		if (*s == '+') {
			*o++ = ' ';
		} else if (*s == '%' && s[1] != '\0' && s[2] != '\0') {
			int h = 0, i;

			for (i = 1; i <= 2; i++) {
				char c = s[i];

				h <<= 4;
				if (c >= '0' && c <= '9')
					h |= c - '0';
				else if ((c | 32) >= 'a' && (c | 32) <= 'f')
					h |= (c | 32) - 'a' + 10;
				else
					h = 0x100;	/* not hex: reject below */
			}
			*o++ = (h > 0 && h < 0x100) ? (char)h : '?';
			s += 2;
		} else {
			*o++ = *s;
		}
	}
	*o = '\0';
}

/* split "a=b&c=d" in place; calls fn(name, value) for each pair */
static void
each_pair(char *body, void (*fn)(char *, char *))
{
	char *p = body;

	while (*p != '\0') {
		char *name = p, *val, *end;

		for (end = p; *end != '\0' && *end != '&'; end++)
			continue;
		p = (*end == '&') ? end + 1 : end;
		*end = '\0';
		for (val = name; *val != '\0' && *val != '='; val++)
			continue;
		if (*val != '=')
			continue;
		*val++ = '\0';
		urldecode(val);
		fn(name, val);
	}
}

static int n_applied, n_bad;

static void
set_one(char *name, char *val)
{
	const struct setting *s;

	for (s = settings; s->param != 0; s++)
		if (seq(s->param, name))
			break;
	if (s->param == 0)
		return;			/* not something we change */
	if (n_applied + n_bad > 0)
		put(",");
	putjson(name, slen(name));
	if (!valid(s, val)) {
		put(":\"invalid\"");
		n_bad++;
		return;
	}
	{
		char *argv[] = { SENDCMD, "1", "DB", "set", (char *)s->table,
		    (char *)s->row, (char *)s->field, val, 0 };

		run(SENDCMD, argv);	/* output goes to cmdbuf, not to `out` */
	}
	put(":\"set\"");
	n_applied++;
}

static void
do_wifi(int fd, char *body)
{
	outlen = 0;
	n_applied = n_bad = 0;
	put("{\"results\":{");
	each_pair(body, set_one);
	put("},\"saved\":");
	if (n_applied > 0) {
		char *argv[] = { SENDCMD, "1", "DB", "save", 0 };

		run(SENDCMD, argv);
		put("true");
	} else {
		put("false");
	}
	put("}");
	respond_json(fd, n_bad > 0 && n_applied == 0 ? "400 Bad Request" : "200 OK");
}

static void
dns_one(char *name, char *val)
{
	size_t bl, vl, i;

	for (i = 0; val[i] != '\0'; i++)	/* names are case-insensitive */
		if (val[i] >= 'A' && val[i] <= 'Z')
			val[i] = (char)(val[i] + 32);
	if (seq(name, "upstream")) {
		if (n_applied + n_bad > 0)
			put(",");
		put("\"upstream\"");
		if (seq(val, "off")) {
			upstream_off();
			put(":\"off\"");
		} else if (valid_ip(val) && !seq(val, "0.0.0.0")) {
			spit(UPFILE, val, slen(val));
			upstream_apply();
			put(":\"set\"");
		} else {
			put(":\"invalid\"");
			n_bad++;
			return;
		}
		n_applied++;
		return;
	}
	if (!(seq(name, "add") || seq(name, "del")))
		return;
	if (n_applied + n_bad > 0)
		put(",");
	putjson(val, slen(val));
	if (!valid_name(val)) {
		put(":\"invalid\"");
		n_bad++;
		return;
	}
	bl = slen(blocklist);
	vl = slen(val);
	if (seq(name, "add")) {
		if (blocked(val)) {
			put(":\"already\"");
		} else if (bl + vl + 2 > sizeof(blocklist)) {
			put(":\"full\"");
			n_bad++;
			return;
		} else {
			memcpy(blocklist + bl, val, vl);
			blocklist[bl + vl] = '\n';
			blocklist[bl + vl + 1] = '\0';
			put(":\"added\"");
		}
	} else {
		char *p = blocklist;
		int gone = 0;

		while (*p != '\0') {
			char *e = p;

			while (*e != '\0' && *e != '\n')
				e++;
			if ((size_t)(e - p) == vl && starts(p, val)) {
				char *from = (*e == '\n') ? e + 1 : e;

				memcpy(p, from, slen(from) + 1);
				gone = 1;
				break;
			}
			p = (*e == '\n') ? e + 1 : e;
		}
		put(gone ? ":\"removed\"" : ":\"absent\"");
	}
	n_applied++;
}

static void
do_dns(int fd, char *body)
{
	outlen = 0;
	n_applied = n_bad = 0;
	slurp(BLOCKFILE, blocklist, sizeof(blocklist));
	put("{\"results\":{");
	each_pair(body, dns_one);
	put("}");
	if (n_applied > 0) {
		spit(BLOCKFILE, blocklist, slen(blocklist));
		hosts_apply();
	}
	put("}");
	respond_json(fd, n_bad > 0 && n_applied == 0 ? "400 Bad Request" : "200 OK");
}

/* --- one request --- */
static char req[8192];

static const char *
header(const char *name)
{
	const char *p = req;

	while ((p = find(p, "\n")) != 0) {
		p++;
		if (starts(p, name)) {
			p += slen(name);
			while (*p == ' ')
				p++;
			return p;
		}
		if (*p == '\r' || *p == '\n')
			return 0;	/* end of headers */
	}
	return 0;
}

/* Copy the query value after `prefix` (up to the space) if it is a plain word. */
static int
query_word(const char *prefix, char *dst, size_t size)
{
	const char *p = req + slen(prefix);
	size_t i = 0;

	if (!starts(req, prefix))
		return 0;
	for (; *p != ' ' && *p != '\0'; p++) {
		if (i >= size - 1 || !((*p >= 'a' && *p <= 'z') ||
		    (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')))
			return 0;
		dst[i++] = *p;
	}
	dst[i] = '\0';
	return i > 0;
}

/* POST with the panel password and our own page's marker; returns the body */
static char *
authorized_body(int fd, char *body, long total)
{
	static char keybuf[80];
	const char *h;
	size_t clen = 0, i;
	long n;

	if (panel_key[0] == '\0') {
		respond_error(fd, "403 Forbidden",
		    "read-only: no password file on the router");
		return 0;
	}
	h = header("X-Panel-Key:");
	for (i = 0; h != 0 && h[i] != '\0' && h[i] != '\r' && h[i] != '\n' &&
	    i < sizeof(keybuf) - 1; i++)
		keybuf[i] = h[i];
	keybuf[i] = '\0';
	if (h == 0 || !key_ok(keybuf)) {
		respond_error(fd, "401 Unauthorized", "wrong panel password");
		return 0;
	}
	/* a form on another site cannot set this header */
	h = header("X-Requested-With:");
	if (h == 0 || !starts(h, "h108panel")) {
		respond_error(fd, "400 Bad Request", "bad request");
		return 0;
	}
	h = header("Content-Length:");
	for (; h != 0 && *h >= '0' && *h <= '9' && clen < 4096; h++)
		clen = clen * 10 + (size_t)(*h - '0');
	if (clen == 0 || clen > 2048) {
		respond_error(fd, "400 Bad Request", "bad length");
		return 0;
	}
	while ((size_t)(req + total - body) < clen &&
	    total < (long)sizeof(req) - 1) {
		n = SC(SYS_read, fd, req + total,
		    sizeof(req) - 1 - (size_t)total, 0, 0);
		if (n <= 0)
			break;
		total += n;
	}
	req[total] = '\0';
	if ((size_t)(req + total - body) < clen)
		return 0;
	body[clen] = '\0';
	return body;
}

static void
handle(int fd)
{
	static const struct { const char *name, *path; } files[] = {
		{ "hosts", HOSTSFILE },
		{ "resolv", "/var/tmp/resolv.conf" },
		{ "defresolv", "/var/tmp/default_resolv.conf" },
		{ "netdev", "/proc/net/dev" },
		{ "arp", "/proc/net/arp" },
		{ "route", "/proc/net/route" },
		{ "meminfo", "/proc/meminfo" },
		{ "mounts", "/proc/mounts" },
		{ 0, 0 }
	};
	long n, total = 0;
	char *body = 0;
	char word[40];
	int i;

	while (total < (long)sizeof(req) - 1) {
		n = SC(SYS_read, fd, req + total,
		    sizeof(req) - 1 - (size_t)total, 0, 0);
		if (n <= 0)
			break;
		total += n;
		req[total] = '\0';
		if ((body = (char *)find(req, "\r\n\r\n")) != 0) {
			body += 4;
			break;
		}
	}
	if (body == 0)
		return;

	outlen = 0;
	if (starts(req, "GET / ") || starts(req, "GET /index.html ")) {
		respond(fd, "200 OK", "text/html; charset=utf-8", page_start,
		    (size_t)(page_end - page_start));
		return;
	}
	if (starts(req, "GET /api/status ")) {
		put("{\"writable\":");
		put(panel_key[0] != '\0' ? "true" : "false");
		put(",\"debug\":");
		put(debug_mode ? "true" : "false");
		put(",");
		table_json("WLANCfg");
		put(",");
		table_json("WLANBase");
		put(",");
		table_json("DHCPSHostInfo");
		put(",");
		table_json("DHCPSHostCfg");
		put(",");
		file_json("blocklist", BLOCKFILE);
		put(",");
		file_json("upstream", UPFILE);
		put(",");
		file_json("resolv", RESOLVFILE);
		put(",");
		file_json("netdev", "/proc/net/dev");
		put(",");
		file_json("uptime", "/proc/uptime");
		put(",");
		file_json("loadavg", "/proc/loadavg");
		put(",");
		file_json("meminfo", "/proc/meminfo");
		put(",");
		file_json("version", "/proc/version");
		put("}");
		respond_json(fd, "200 OK");
		return;
	}
	if (debug_mode && query_word("GET /api/table?name=", word, sizeof(word))) {
		put("{");
		table_json(word);
		put("}");
		respond_json(fd, "200 OK");
		return;
	}
	if (debug_mode && query_word("GET /api/file?name=", word, sizeof(word))) {
		for (i = 0; files[i].name != 0; i++) {
			if (seq(files[i].name, word)) {
				put("{");
				file_json(word, files[i].path);
				put("}");
				respond_json(fd, "200 OK");
				return;
			}
		}
	}
	if (starts(req, "POST /api/wifi ") || starts(req, "POST /api/set ")) {
		if ((body = authorized_body(fd, body, total)) != 0)
			do_wifi(fd, body);
		return;
	}
	if (starts(req, "POST /api/dns ")) {
		if ((body = authorized_body(fd, body, total)) != 0)
			do_dns(fd, body);
		return;
	}
	respond_error(fd, "404 Not Found", "not found");
}

static void
say(int fd, const char *s)
{
	SC(SYS_write, fd, s, slen(s), 0, 0);
}

void
cmain(long *sp)
{
	long argc = sp[0];
	char **argv = (char **)(sp + 1);
	struct { unsigned short family, port; unsigned int addr; char pad[8]; } sa;
	struct { long sec, usec; } tv = { 5, 0 };
	int one = 1;
	long s, c, n;

	debug_mode = argc > 1 && seq(argv[1], "-d");

	/* the panel password: first line of the key file, 4..64 characters */
	n = slurp(KEYFILE, panel_key, sizeof(panel_key));
	for (c = 0; c < n && panel_key[c] != '\n' && panel_key[c] != '\r'; c++)
		continue;
	panel_key[c > 0 ? c : 0] = '\0';
	if (c < 4 || c > 64)
		panel_key[0] = '\0';

	s = SC(SYS_socket, AF_INET, SOCK_STREAM, 0, 0, 0);
	if (s < 0)
		SC(SYS_exit, 1, 0, 0, 0, 0);
	SC(SYS_setsockopt, s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	memset(&sa, 0, sizeof(sa));
	sa.family = AF_INET;
	sa.port = PORT;		/* big-endian machine: already network order */
	if (SC(SYS_bind, s, &sa, sizeof(sa), 0, 0) < 0 ||
	    SC(SYS_listen, s, 4, 0, 0, 0) < 0) {
		say(2, "h108panel: cannot listen (already running?)\n");
		SC(SYS_exit, 1, 0, 0, 0, 0);
	}

	/* carry on in the background so the shell that started us returns */
	if (SC(SYS_fork, 0, 0, 0, 0, 0) != 0) {
		say(1, "h108panel: listening on port 8672");
		say(1, panel_key[0] != '\0' ? ", changes allowed" : ", read-only");
		say(1, debug_mode ? ", development mode\n" : "\n");
		SC(SYS_exit, 0, 0, 0, 0, 0);
	}
	SC(SYS_setsid, 0, 0, 0, 0, 0);

	hosts_apply();
	upstream_apply();
	if (SC(SYS_fork, 0, 0, 0, 0, 0) == 0) {
		SC(SYS_close, s, 0, 0, 0, 0);
		keeper();
	}

	for (;;) {
		c = SC(SYS_accept, s, 0, 0, 0, 0);
		if (c < 0)
			continue;
		/* a stalled client must not hold up everyone else */
		SC(SYS_setsockopt, c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		SC(SYS_setsockopt, c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		handle((int)c);
		SC(SYS_close, c, 0, 0, 0, 0);
	}
}

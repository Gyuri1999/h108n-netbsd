#!/usr/bin/env python3
"""Scan a big-endian 32-bit MIPS ELF for instructions outside MIPS-I.

The Lexra RLX5281 runs MIPS-I (minus lwl/lwr/swl/swr). Anything newer —
MIPS-II (ll/sc, sync, trap, *.fmt branches-likely), MIPS-III 64-bit ops,
MIPS32 clz/mul/movz/movn, MIPS32r2 ext/ins/rotr — is illegal on it. The
original port only grepped for the four unaligned loads/stores; this checks
the whole non-MIPS-I set and reports which function each hit lands in, so
inert dispatch-table entries (e.g. in bds_emul.S) can be told apart from
instructions on a live path.

Usage: scan-nonmips1.py <elf> [--fail] [--allow FUNC ...]
  --fail        exit 1 if any non-allowed hit remains
  --allow FUNC  treat hits inside FUNC as known-inert (e.g. the bds_emul.S
                delay-slot dispatch table, which is only reached if such an
                instruction appears in a delay slot — impossible in MIPS-I code)
"""
import bisect
import struct
import sys

argv = sys.argv[1:]
ALLOW = set()
args = []
i = 0
while i < len(argv):
    if argv[i] == "--allow" and i + 1 < len(argv):
        ALLOW.add(argv[i + 1]); i += 2; continue
    if not argv[i].startswith("--"):
        args.append(argv[i])
    i += 1
FAIL = "--fail" in argv
f = open(args[0], "rb").read()
assert f[:4] == b"\x7fELF" and f[4] == 1 and f[5] == 2, "need a 32-bit big-endian ELF"
be = ">"
e_shoff, = struct.unpack(be + "I", f[32:36])
e_shentsize, e_shnum, e_shstrndx = struct.unpack(be + "HHH", f[46:52])

secs = []
for i in range(e_shnum):
    o = e_shoff + i * e_shentsize
    nm, typ, fl, addr, off, size, link, info, al, ent = \
        struct.unpack(be + "IIIIIIIIII", f[o:o + 40])
    secs.append(dict(typ=typ, fl=fl, addr=addr, off=off, size=size, link=link, ent=ent))

syms = []
for s in secs:
    if s["typ"] == 2:  # SYMTAB
        st = secs[s["link"]]
        for j in range(s["size"] // s["ent"]):
            o = s["off"] + j * s["ent"]
            nn, val, sz, info, oth, sh = struct.unpack(be + "IIIBBH", f[o:o + 16])
            if nn and (info & 0xf) == 2:  # STT_FUNC
                e = f.index(b"\0", st["off"] + nn)
                syms.append((val, f[st["off"] + nn:e].decode("latin1")))
syms.sort()
saddr = [a for a, _ in syms]
def func_of(a):
    i = bisect.bisect_right(saddr, a) - 1
    return syms[i][1] if i >= 0 else "?"

# SPECIAL (op 0) function codes that are NOT MIPS-I
SPECIAL_BAD = {
    0x01: "movci", 0x0a: "movz", 0x0b: "movn", 0x0f: "sync",
    0x14: "dsllv", 0x16: "dsrlv", 0x17: "dsrav",
    0x1c: "dmult", 0x1d: "dmultu", 0x1e: "ddiv", 0x1f: "ddivu",
    0x2c: "dadd", 0x2d: "daddu", 0x2e: "dsub", 0x2f: "dsubu",
    0x30: "tge", 0x31: "tgeu", 0x32: "tlt", 0x33: "tltu",
    0x34: "teq", 0x36: "tne",
    0x38: "dsll", 0x3a: "dsrl", 0x3b: "dsra",
    0x3c: "dsll32", 0x3e: "dsrl32", 0x3f: "dsra32",
}
SPECIAL2 = {0x00: "madd", 0x01: "maddu", 0x02: "mul", 0x04: "msub",
            0x05: "msubu", 0x20: "clz", 0x21: "clo"}

def classify(w):
    op = (w >> 26) & 0x3f
    fn = w & 0x3f
    if op == 0x00:
        return SPECIAL_BAD.get(fn)
    if op == 0x1c:
        return SPECIAL2.get(fn, "special2")
    if op == 0x1f:
        return "special3(ext/ins/rotr/...)"
    if op in (0x30, 0x34, 0x38, 0x3c):   # ll, lld, sc, scd
        return {0x30: "ll", 0x34: "lld", 0x38: "sc", 0x3c: "scd"}[op]
    if op in (0x1a, 0x1b):               # ldl/ldr (64-bit unaligned)
        return "ldl/ldr"
    if op in (0x2c, 0x2d):               # sdl/sdr
        return "sdl/sdr"
    if op == 0x37 or op == 0x3f:         # ld / sd (64-bit)
        return "ld/sd"
    return None

hits = {}
for s in secs:
    if not (s["fl"] & 0x4):   # SHF_EXECINSTR
        continue
    data = f[s["off"]:s["off"] + s["size"]]
    for k in range(0, len(data) - 3, 4):
        w, = struct.unpack(be + "I", data[k:k + 4])
        c = classify(w)
        if c:
            hits.setdefault(c, {}).setdefault(func_of(s["addr"] + k), 0)
            hits[c][func_of(s["addr"] + k)] += 1

if not hits:
    print("CLEAN: no non-MIPS-I instructions in executable sections.")
    sys.exit(0)

total = sum(sum(fns.values()) for fns in hits.values())
live = 0   # hits outside the allow-listed (known-inert) functions
print(f"NON-MIPS-I INSTRUCTIONS: {total} in {args[0]}")
for c in sorted(hits):
    fns = hits[c]
    n = sum(fns.values())
    bad = sum(k for fn, k in fns.items() if fn not in ALLOW)
    live += bad
    where = ", ".join(f"{fn}×{k}" for fn, k in sorted(fns.items(), key=lambda x: -x[1])[:6])
    flag = "" if bad == 0 else "  <== LIVE"
    print(f"  {c:28s} {n:4d}   ({where}){flag}")
if ALLOW:
    print(f"\nallow-listed (inert): {', '.join(sorted(ALLOW))}")
    print(f"non-MIPS-I on live paths: {live}")
sys.exit(1 if (FAIL and live) else 0)

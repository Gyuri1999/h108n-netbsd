# tools

## scan-nonmips1.py

Audits a 32-bit big-endian MIPS ELF for instructions outside **MIPS-I**, the
ISA of the Lexra RLX5281. The original port only grepped the kernel for the
four unaligned load/stores (`lwl/lwr/swl/swr`); this checks the whole
non-MIPS-I set — MIPS-II (`ll/sc`, `sync`, traps), MIPS-III 64-bit ops,
MIPS32 (`clz/mul/movz/movn`), MIPS32r2 (`ext/ins/rotr`) — and reports which
function each hit lands in.

```sh
python3 tools/scan-nonmips1.py path/to/netbsd            # report
python3 tools/scan-nonmips1.py path/to/netbsd --fail \
        --allow mips_emul_branchdelayslot                # CI gate
```

The current `RTL8672` kernel is MIPS-I clean except the `bds_emul.S`
delay-slot dispatch table (`mips_emul_branchdelayslot`), whose 64-bit / movz /
trap entries are **inert**: each is only executed if that instruction appears
in a branch delay slot, which cannot happen in MIPS-I code. `--allow` marks
that function known-inert so the gate fails only on a *new* leak onto a live
path. CI runs this on every build.

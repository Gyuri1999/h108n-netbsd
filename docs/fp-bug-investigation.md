# Floating-point `nan` — investigation status (resume here)

`sysctl vm.loadavg` prints `0.00 nan nan` on the H108N. This is the running
log of narrowing it down, and where to pick up next time.

## Bottom line (what we know)

**Runtime `cvt.d.w` (int → double) is broken on this Lexra build; compile-time
double constants are fine.** That is the cause of the `nan`.

Everything else was ruled out:
- **Not libgcc.** The soft-float routines are MIPS-I clean (CI audit, 0 hits in
  `__divdf3`, `__floatsidf`, `__muldf3`, `__fixdfsi`, `__adddf3`).
- **Not an illegal instruction in the FP path.** The whole `RTL8672` kernel is
  MIPS-I clean except the inert `bds_emul.S` delay-slot dispatch table
  (`mips_emul_branchdelayslot`), which is never reached by MIPS-I code. See
  `tools/scan-nonmips1.py` and the CI gate.
- **Userland is fine** (it runs: `init`, `sh`, ~25 tools), so it is effectively
  MIPS-I; the FP values go through the kernel FP emulator (`options FPEMUL`,
  hard-float userland, `MKSOFTFLOAT` is only default-yes on MIPS64).

## The evidence (on real hardware)

NetBSD 11 booted on the device (serial console) and `usr/bin/fptest` (added to
the ramdisk) printed the raw IEEE-754 bits of each operation:

```
cvt7    bits=0000000000000007 (int)=7      (double)7       runtime  -> WRONG (raw int)
cvt3    bits=0000000000000003 (int)=3      (double)3       runtime  -> WRONG (raw int)
cvt1    bits=3ff0000000000000 (int)=0      (double)1       constant -> 1.0  CORRECT
div3.5  bits=0000000000000000 (int)=0      7.0/2.0                  -> WRONG
mul14   bits=0000000000000000 (int)=0      7.0*2.0                  -> WRONG
add8    bits=3ff0000000000000 (int)=0      7.0+1.0                  -> 1.0 (const dominates)
load    bits=0000000000000000 (int)=0      3.0/2048.0               -> WRONG
```

Reading:
- `cvt1` is `(double)1` with a literal, so the compiler constant-folds it and
  emits `l.d` of a ready-made `1.0` (`0x3ff0000000000000`) — **correct**. This
  proves loading double constants and storing/printing doubles all work.
- `cvt7`/`cvt3` are `(double)n` with `volatile int n`, so they run `cvt.d.w` at
  runtime. The result is the **raw integer bit pattern**, i.e. the conversion
  did not happen — `cvt.d.w` left the integer in the low word, exponent 0.
- `(int)d` (i.e. `cvt.w.d`) is also wrong: it returns the low word of the
  double (0 for `1.0`, 7 for the `0x…0007` garbage), so the FP↔int conversions
  are the broken piece, not add/mul/div themselves.

So: **the `cvt.*` (format-conversion) emulation path is not converting at
runtime.** Constants dodge it; everything that needs a real int→double breaks.

## Where to look next

`sys/arch/mips/mips/fp.S` in the fork (`Gyuri1999/src` @ `h108n-lexra`):

1. `cvt_d_w:` (int→double) and `cvt_w_d:`/`cvt_w_s:` (double/single→int). The
   `cvt_d_w` zero-shortcut works (loadavg[0] = `0.00`); the non-zero
   normalization path is the suspect. Check the manual leading-zero / shift
   path (the `#else` branch used when `__mips == 1`) and `result_fs_d` /
   `get_fs_int` operand-and-result movement.
2. The **FMT dispatch** at `mips_emul_fp`: `cvt.d.w` has FMT = W, dispatched via
   `fmt_tbl` → `single_fixed_op` → `func_single_fixed_tbl`. Verify the W-format
   path reaches `cvt_d_w` and isn't landing on `ill`/the wrong table on this
   build.
3. Whether `cvt.d.w` even traps to the emulator on the Lexra, vs. executing as
   a no-op on the core. (The CPU is classified `MIPS_SOFT`; confirm CU1 stays
   disabled so cp1 ops fault into `mips_fpu_intr` → `mips_emul_fp`.)

A good next experiment: extend `usr.bin/fptest` to print the bits of
`cvt.w.d`, a bare `mtc1`/`mfc1` round-trip, and a single `cvt.d.w` in isolation,
then rebuild (below) and re-read on the device.

## How to resume on the device

Serial: `/dev/cu.PL2303G-USBtoUART80` @ 115200 8N1 (GND/TX/RX, no VCC).
`screen /dev/cu.PL2303G-USBtoUART80 115200`.

1. Build the loader (with the latest ramdisk): run the **full** CI workflow
   (`gh workflow run full.yml`) and download the `h108nb-ko` artifact, or build
   locally with `FULL=1 REPO=$PWD sh build/build-all.sh`. Put `h108nb.ko` in a
   tftp dir and serve it: `python3 loader/tftpd.py <dir> 6969`.
2. Root on stock Linux: `nc 192.168.1.254 23` (admin user/pass).
3. `tftp -g -r h108nb.ko -l /var/h108nb.ko <your-ip> 6969` then
   `insmod /var/h108nb.ko`. NetBSD comes up on serial.
4. `fptest` and `sysctl vm.loadavg` to check.
5. `reboot` returns to stock firmware to load a new build.

Nothing here writes to flash; a power cycle always restores the factory
firmware.

## What this session added

- `tools/scan-nonmips1.py` + CI gate (full non-MIPS-I audit, not just the 4
  unaligned loads/stores).
- `tests/fp/fp.c` (host-readable isolation test) and `usr.bin/fptest` in the
  ramdisk (on the fork).
- `build/` reproducible build (`build-all.sh`, Dockerfile) and the `build` /
  `full` CI workflows. The NetBSD port now lives as commits on the fork
  `Gyuri1999/src` @ `h108n-lexra`, not as a patch set.

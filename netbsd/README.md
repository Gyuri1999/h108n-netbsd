# NetBSD patches

Three patches against the `netbsd-11` branch of NetBSD
(base commit `4299530ca04c23eff6e740a3d97d29bca16c22fe`).

| Patch | What it changes |
|---|---|
| `0001-gcc-mips-add-mlexra.patch` | Adds `-mlexra` to NetBSD's GCC 12: the compiler stops using `lwl`/`lwr`/`swl`/`swr` |
| `0002-netbsd-rtl8672-lexra-port.patch` | Lexra CPU support, byte-wise string routines, the `evbmips/rtl8672` board port, kernel configs, ramdisk recipe |
| `0003-tools-keep-DEVELOPER_DIR-on-macOS.patch` | Build fix for macOS hosts only; see `docs/building.md` |

## What is in the port patch

**Shared MIPS code**

- `sys/arch/mips/include/cpuregs.h`, `mips/mips_machdep.c`: the RLX5281
  (PRID implementation `0xdc`) as a MIPS-I CPU.
- `sys/arch/mips/mips/cache_lexra.c`, `include/cache_lexra.h`, `mips/cache.c`:
  cache operations through CP0 register 20.
- `sys/arch/mips/mips/trap.c`: the `TRAP_SIGDEBUG` register dump now
  compiles on 32-bit kernels, and stops after six reports.

**String routines** (selected with `-D__LEXRA__`)

- `common/lib/libc/arch/mips/string/bcopy.S` (kernel and libc `memcpy`,
  `memmove`, `bcopy`)
- `lib/libc/arch/mips/string/bcmp.S`, `bzero.S`

**Board port**, `sys/arch/evbmips/rtl8672/`

| File | |
|---|---|
| `machdep.c` | start-up, early console, watchdog, reboot |
| `rtl8672_intr.c` | interrupt controller, finds the timer's CPU line at boot |
| `rtl8672_clock.c` | `delay()` calibrated against the on-chip timer |
| `rtl8672_bus.c`, `rtl8672_dma.c` | bus space and DMA tags |
| `mainbus.c`, `com_mainbus.c`, `autoconf.c` | device attachment |
| `rtl8672reg.h` | register addresses, with what was verified on hardware |

**Kernel configurations**, `sys/arch/evbmips/conf/`

- `RTL8672`: serial console only, stops at `root device:`.
- `RTL8672_MD`: the same with a 3 MB memory disk root and fault reporting.

**Ramdisk**, `distrib/evbmips/rtl8672ramdisk/`: `init`, `sh` and about 25
tools in one crunched binary.

## Licensing

These patches modify NetBSD and GCC source files and are covered by the
licenses of those files (BSD-style for NetBSD, GPL for GCC). New files in
the port follow NetBSD's 2-clause BSD convention.

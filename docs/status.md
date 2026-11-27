# Status and open problems

## What has been seen working on the device

- The kernel boots, identifies the CPU, calibrates its delay loop and takes
  clock interrupts at 100 Hz. The clock advances at the right rate.
- The serial console works for output and input, including the kernel
  debugger (`ddb`).
- With a memory disk root, `init` starts a root shell. `ls`, `ps`, `df`,
  `mount`, `sysctl`, shell arithmetic, and writing files on the memory disk
  all behave.
- A loop of 150 iterations that each started two programs (300 process
  creations) ran to completion without a fault.
- `reboot` shuts down cleanly and resets the board through the watchdog.

## Open problems

**A crash that was seen once.** On the first boot with a memory disk, about
31 seconds after start, `init` began to fault in an endless loop
(`Process (pid 1) got sig 11`, repeated with the same timestamp). It has
not happened again in later boots with the same user programs, and its
cause is unknown. The memory-disk kernel is built with `TRAP_SIGDEBUG` so
that the next occurrence reports the faulting address and registers.

**Floating point.** `sysctl vm.loadavg` prints `nan nan nan`. The board has
no FPU and the kernel emulates floating point, so something on that path
gives wrong results. Narrowed on hardware to the runtime `cvt.d.w`
(int→double) emulation — constants are fine, the conversion is not. Full
investigation and how to resume: [fp-bug-investigation.md](fp-bug-investigation.md).

**No real-time clock.** Time starts from the file system's time stamp.

**Nothing is persistent.** There is no flash driver. The root file system
lives in RAM and the system is loaded from the stock Linux on every boot.

**No network.** There is no Ethernet driver, so NetBSD on this board talks
to the outside only through the serial port.

**Cache operations are coarse.** Every cache request flushes the whole
cache. Correct, but slow.

**Interrupt routing is copied, not understood.** The routing registers are
written with the vendor kernel's values, and the timer's CPU line is found
at boot by experiment. If another device ends up on the same line as the
timer, the interrupt levels will be wrong.

## What was checked for forbidden instructions

The kernel, the loader, the panel, the ramdisk binary and the static and
shared C library were disassembled and searched for `lwl`, `lwr`, `swl` and
`swr`: none. The rest of the installed base system was built with the same
flags but was **not** checked program by program.

## What would come next

1. An Ethernet driver for the on-chip MAC.
2. A driver for the SPI flash, for a persistent root and a real install.
3. Finding the cause of the `init` fault and of the floating point results.
4. A time counter based on the second on-chip timer.

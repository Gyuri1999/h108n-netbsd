# Hardware notes

There is no public documentation for this SoC. Everything below was read
out of the vendor boot loader and Linux kernel in a flash backup, and the
entries marked **verified** were then confirmed on the device by code from
this repository. Treat the rest as "what the vendor kernel does".

Board: ZTE ZXHN H108N, hardware "V2.5", firmware `V2.5.0_WindT13`.

## CPU

| | |
|---|---|
| Core | Lexra RLX5281 (Realtek), big-endian |
| PRID | `0x0000dc01` (**verified**) |
| Instruction set | MIPS-I, plus MIPS16, **without** `lwl`/`lwr`/`swl`/`swr` |
| TLB | 64 entries, R3000 style (**verified**: the loader probes it) |
| Caches | 16 KB instruction, 8 KB data |
| FPU | none |
| CP0 count/compare | none |

Things that do not behave like a real R3000:

- **No unaligned load/store instructions.** Code must not contain them at
  all; there is no emulation.
- **Cache control** goes through CP0 register 20. An operation starts when
  its bit goes from 0 to 1 (**verified**):

  | Bit | Operation |
  |---|---|
  | `0x002` | invalidate the instruction cache |
  | `0x200` | write back and invalidate the data cache |

- **`bc0f`/`bc0t`** must not be used. NetBSD's MIPS-I `wbflush()` loops on
  `bc0f`, waiting for an external write buffer, and never returns here.

## Memory map

| Address | What |
|---|---|
| `0x80000000` | 32 MB RAM (KSEG0), also at `0xA0000000` uncached |
| `0xB8002100` | UART, 16550A (**verified**) |
| `0xB8003000` | Interrupt controller |
| `0xB8003100` | Timers and watchdog |
| `0xB8001200` | SPI flash controller |
| `0xB8020000` | USB OHCI |
| `0xB8021000` | USB EHCI |
| `0xB9000000` | PCIe (the Wi-Fi card, an RTL8192E) |
| `0xBD000000` | 8 MB SPI flash, memory mapped |

## UART

16550A, registers 4 bytes apart. On this big-endian SoC each register is
the first byte of its 32-bit word, so plain byte accesses at `base + 4*n`
work (**verified**). 115200 baud. The reference clock is not documented;
the NetBSD port derives it from the divisor the boot loader left behind and
gets about 197 MHz.

## Interrupt controller

| Register | Address | |
|---|---|---|
| GIMR | `0xB8003000` | mask, one bit per source |
| GISR | `0xB8003004` | pending sources |
| IRR0..3 | `0xB8003008`..`0xB8003014` | routing, 4 bits per source |

The vendor kernel writes `0x22222202`, `0x65232227`, `0x22222222` and
`0x22222222` to the routing registers. The layout of those registers was
not worked out; the NetBSD port writes the same values and then finds out
by experiment which CPU line the timer arrives on. On the device that is
interrupt mask `0x8000`, CPU line 7 (**verified**).

| Source | Bit |
|---|---|
| Timer 0 | 8 |
| USB | 10 |
| UART | 13 |
| Ethernet | 15 |

## Timer

| Register | Address | |
|---|---|---|
| TC0DATA | `0xB8003100` | reload value, shifted left by 8 |
| TC1DATA | `0xB8003104` | |
| TC0CNT | `0xB8003108` | |
| TC1CNT | `0xB800310C` | |
| TCCNR | `0xB8003110` | `0x80000000` enable timer 0, `0x40000000` auto-reload |
| TCIR | `0xB8003114` | `0x80000000` interrupt enable, `0x20000000` pending (write 1 to clear) |
| CDBR | `0xB8003118` | clock divisor, shifted left by 16 |

The bus clock is 200 MHz: the vendor kernel prints `Div=1000, Data=2000` at
100 Hz. With those values NetBSD's clock advances at the right rate
(**verified** against wall-clock time).

## Watchdog

`0xB800311C`. The stock Linux leaves it running, and it resets the board
about a second after Linux stops servicing it.

| Value | Effect |
|---|---|
| `0xA5000000` | stop the watchdog (**verified**) |
| `0x00000000` | reset the board immediately (**verified**; this is NetBSD's reboot) |
| `0x00E00000` | what the vendor kernel writes to enable it |

## Flash

8 MB SPI NOR, memory mapped at `0xBD000000`. See
[flash-backup.md](flash-backup.md) for the partition layout. There is no
NetBSD driver for it.

## Network

One Ethernet MAC with four LAN ports (PHYs 0 to 3), no external switch
chip. ADSL shows up in Linux as `nas*` interfaces. Wi-Fi is an RTL8192E on
PCIe with four virtual interfaces. None of these have NetBSD drivers.

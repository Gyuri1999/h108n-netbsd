# The serial console

NetBSD on this router has no network driver yet, so the serial port is its
only console. It is also the only place where you see the boot loader and
the early kernel messages.

## Settings

| | |
|---|---|
| Speed | 115200 baud |
| Format | 8 data bits, no parity, 1 stop bit |
| Flow control | none |
| Levels | 3.3 V TTL (not RS-232) |

## Wiring

Use a 3.3 V USB serial adapter and connect **three wires only**:

| Adapter | Router |
|---|---|
| GND | GND |
| RX | TX |
| TX | RX |

**Do not connect the adapter's VCC pin.** The router has its own power
supply.

The location and order of the pins on the board are not documented here:
this repository was written from the software side, on a unit that was
already wired. Find ground with a multimeter against the shield of a
connector, and identify the router's TX as the pin that shows activity
while it boots.

If you see garbage where text should be, with many bytes replaced by the
same wrong character, check the ground wire first. That was the cause on
the development unit: with a poor ground, about every second byte arrived
as `0xFD`.

## Connecting

```sh
screen /dev/tty.usbserial-XXXX 115200
```

Only one program can hold the port. `dashboard/server.py` in this
repository opens it too, so close `screen` before starting the dashboard.

## What you see on a normal boot

```
Booting
Press '1' to enter BOOT console...
Press '2' to enter DEBUG mode......
Found image at 0xbd080000
Linux version 2.6.30.9-... (gcc version 4.4.7 (Realtek RSDK-1.5.8p1 Build 1050) )
CPU revision is: 0000dc01
icache: 16kB/32B, dcache: 8kB/32B, scache: 0kB/0B
Timer: Div=1000, Data=2000
...
ZXHN H108N
Login:
```

The `Login:` prompt here takes different credentials from telnet (see
[getting-root.md](getting-root.md)). You do not need it.

## The boot loader console

Pressing `1` in the first second gives the Realtek loader prompt:

```
<RTL867X>help
help
info
reboot
run [app addr] [entry addr]
r [addr]
w [addr] [val]
d [addr] <len>
resetcfg
mac ["clear"/"osk"/mac address]
bootline
entry [address]
load [address]
xmodem [address]
tftp [ip] [server ip] [file name]
web
flashsize ...
memsize ...
uart [0(enable)/1(disable)]
```

`info` is safe and useful:

```
Entry Point: 0x80000000
Load Address: 0x80000000
Application Address: 0xBFC20000
```

**Be careful with the rest.** What was found out the hard way:

- `xmodem [address]` is a **firmware upgrade** command. It received a
  208-byte test file, tried to parse it as a firmware image and rejected it
  with `upgradekey is err!`. A file that passed that check would have been
  written to flash.
- `tftp` is very likely the same.
- `d 80000000 40` printed `0xC0000000:` and hung the loader until a power
  cycle. Addresses are not interpreted the way the help text suggests.
- `load`, `entry`, `bootline`, `mac`, `flashsize`, `memsize`, `uart` and
  `resetcfg` look like commands that store settings in flash. They were
  never run.

This is why the project boots through Linux instead: see
[boot-chain.md](boot-chain.md).

## On the NetBSD side

NetBSD takes over the same UART at the same speed. The console needs no
login: `init` starts a root shell directly. At the `root device:` prompt
(a kernel without a memory disk) and at the `db>` prompt of the kernel
debugger, input works as well.

Typing `reboot` in NetBSD resets the board through the watchdog and the
factory firmware starts again.

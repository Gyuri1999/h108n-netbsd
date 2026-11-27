# Getting a root shell on the stock firmware

Everything in this project starts from a root shell on the router's own
Linux. On this firmware (`V2.5.0_WindT13`) telnet gives you one directly.

## What you need

- The router with its factory firmware, powered on.
- A computer connected to one of the router's LAN ports. The router's
  default address is `192.168.1.254`.
- The router's administrator user name and password (the same ones you use
  for its web interface).

## Log in

```sh
telnet 192.168.1.254
```

macOS no longer ships `telnet`. `nc` works just as well:

```sh
nc 192.168.1.254 23
```

Enter the administrator user name and password at the `Login:` and
`Password:` prompts. You land in a BusyBox shell:

```
BusyBox v1.01 (2019.06.21-04:33+0000) Built-in shell (ash)
Enter 'help' for a list of built-in commands.

#
```

With `nc` your terminal echoes what you type, so the password is visible on
your screen while you type it.

## Check that it is root

There is no `id` on this firmware. Ask the kernel instead:

```sh
cat /proc/self/status
```

`Uid: 0 0 0 0` means root. `ls -l` shows an odd owner name instead of
`root`; that is only how the vendor filled in `/etc/passwd`.

## Things to know

- **One session at a time.** A second connection is refused with
  `The connect number is limited`. If an old session is stuck, power cycle
  the router.
- **The serial console is not the same login.** It asks for a user name and
  password too, and on the unit this was developed on the telnet credentials
  were rejected there. You do not need a serial login for anything here:
  the boot loader console and NetBSD's console need none.
- **The shell is minimal.** There is no `dd`, `grep`, `head`, `md5sum` or
  `dmesg`. What you do have: `cat`, `ls`, `ps`, `mount`, `ifconfig`,
  `tftp`, `wget`, `insmod`, `rmmod`, `lsmod`, `kill`, `killall`, `reboot`.
- **`/var` is a 12 MB RAM disk** and the only place you can write. It is
  emptied on every boot.

## Useful first commands

```sh
cat /proc/cpuinfo      # system type RTL8672, cpu model 56321 (0xdc01)
cat /proc/mtd          # flash partitions
cat /proc/interrupts   # timer 8, serial 13, eth0 15
cat /proc/iomem
```

Next: [back up the flash](flash-backup.md) before doing anything else.

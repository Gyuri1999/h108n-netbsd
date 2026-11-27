# Backing up the flash

Do this before anything else. Nothing in this project writes to the flash,
but a full image is the only way back if something ever does, and it is
also where every hardware fact in this repository came from.

**Keep the backup private.** It contains your Wi-Fi keys, your ISP user
name and password, and the vendor's firmware. Do not publish it.

## The flash layout

From a [root shell](getting-root.md):

```
# cat /proc/mtd
dev:    size   erasesize  name
mtd0: 00800000 00001000 "whole_flash"
mtd1: 00020000 00001000 "bootloader"
mtd2: 00060000 00001000 "userconfig"
mtd3: 00240000 00001000 "kernel1"
mtd4: 00540000 00001000 "filesystem1"
```

`mtd0` is the whole 8 MB chip, so one file is enough.

## Copy it into RAM

There is no `dd`; `cat` does the job. `/var` is a 12 MB RAM disk, so an
8 MB image fits:

```sh
cat /dev/mtdblock0 > /var/flash-backup.bin
ls -l /var/flash-backup.bin
```

The size must be exactly `8388608` bytes.

## Get it off the router

The router has a TFTP client. Run a TFTP server that accepts uploads on
your computer, on a port above 1024 so that it needs no administrator
rights (6969 is used throughout this project), then on the router:

```sh
tftp -p -l /var/flash-backup.bin -r flash-backup.bin <your-pc> 6969
```

Use the full form with `-l` and `-r`; the short form without `-r` timed out
on this BusyBox.

`loader/tftpd.py` in this repository is read-only on purpose (it only
serves files to the router), so it cannot receive the backup. Use any
TFTP server that supports write requests.

Afterwards remove the copy from the router's RAM:

```sh
rm /var/flash-backup.bin
```

## Check it

On your computer:

```sh
ls -l flash-backup.bin        # 8388608 bytes
xxd -s 0x80000 -l 64 flash-backup.bin
```

At offset `0x80000` you should see the vendor's image header, which starts
with `99 99 99 99 44 44 44 44 55 55 55 55 aa aa aa aa` and contains the
firmware version string a little further on.

The router has no `md5sum`, so there is no checksum to compare against.
Reading the flash twice and comparing the two files on your computer is a
reasonable substitute.

## What is inside

| Offset | Size | Contents |
|---|---|---|
| `0x000000` | 128 KB | Boot loader (second stage LZMA-compressed at `0x6020`) |
| `0x020000` | 384 KB | User configuration |
| `0x080000` | 2.25 MB | Vendor header, then the Linux kernel, LZMA-compressed from `0x80100` |
| `0x2C0000` | 5.25 MB | Root file system, squashfs 4.0 with LZMA |

`unsquashfs` reads the root file system directly once you cut it out:

```sh
dd if=flash-backup.bin of=rootfs.sqsh bs=65536 skip=44
unsquashfs -d rootfs rootfs.sqsh
```

## Restoring

This project never had to restore a flash image, so there is no tested
procedure here. The boot loader has firmware upgrade commands, but they
check a vendor key and were not explored. If the boot loader itself is ever
damaged, the realistic way back is an external SPI programmer and this
backup.

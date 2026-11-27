# Loader

Boots a kernel on the H108N from the router's own Linux, without touching
the flash or the stock boot loader. The design is described in
[`docs/boot-chain.md`](../docs/boot-chain.md).

| File | |
|---|---|
| `h108kboot.c` | Linux kernel module: locates the compressed kernel's pages through the TLB and leaves Linux |
| `stage2a.c` | gathers the compressed kernel and moves it, with stage 2b, to the top of RAM |
| `stage2b.c` | inflates the kernel to its link address and jumps |
| `inflate.c`, `inflate.h` | small raw DEFLATE decoder, no libc, input through a callback |
| `stage_start.S`, `stage2.ld`, `loader.h`, `bare.h` | entry code, link script, memory plan, serial and cache helpers |
| `build_kboot.sh` | wraps a NetBSD kernel into `tftp/h108nb.ko` |
| `mkpayload.py` | compresses a kernel image to a raw DEFLATE stream |
| `h108boot.c`, `build.sh` | the three small bring-up modules and their build |
| `tftpd.py` | minimal read-only TFTP server for feeding files to the router |

## Build

```sh
TOOLS=/path/to/netbsd/tools/bin sh build_kboot.sh /path/to/netbsd.md
```

## Serve and load

```sh
python3 tftpd.py tftp 6969
```

```sh
# on the router
tftp -g -r h108nb.ko -l /var/h108nb.ko <your-pc> 6969
insmod /var/h108nb.ko
```

The modules are built without kernel headers and match one specific vendor
kernel (`2.6.30.9-g597a8e2d-dirty`). On any other kernel `insmod` should
refuse them because of the version string, but do not count on that.

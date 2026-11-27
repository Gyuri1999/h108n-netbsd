# Building

Everything is cross-built with NetBSD's own `build.sh`. The steps below
were run on macOS; Linux and the BSDs work the same way, usually with less
trouble.

## 1. Get NetBSD and apply the patches

```sh
git clone --depth 1 --branch netbsd-11 https://github.com/NetBSD/src
cd src
git apply /path/to/this/repo/netbsd/patches/0001-gcc-mips-add-mlexra.patch
git apply /path/to/this/repo/netbsd/patches/0002-netbsd-rtl8672-lexra-port.patch
```

The patches were made against commit
`4299530ca04c23eff6e740a3d97d29bca16c22fe` of `netbsd-11`. A newer tip of
the branch may need small adjustments.

On macOS use a case-sensitive volume for the source and object trees.
`0003-tools-keep-DEVELOPER_DIR-on-macOS.patch` is only needed if you have
to steer the host compiler with `DEVELOPER_DIR`; see the note at the end.

## 2. Tools

```sh
./build.sh -U -j8 -m evbmips-eb -O ../obj -T ../tools tools
```

This builds the cross compiler with the new `-mlexra` option. Check it:

```sh
../tools/bin/mipseb--netbsd-gcc --target-help | grep lexra
```

## 3. Kernel

```sh
./build.sh -U -u -j8 -m evbmips-eb -O ../obj -T ../tools kernel=RTL8672
```

`RTL8672` stops at the `root device:` prompt, which is enough for a first
test. The result is `../obj/sys/arch/evbmips/compile/RTL8672/netbsd`.

It is worth checking that no forbidden instruction slipped in:

```sh
../tools/bin/mipseb--netbsd-objdump -d ../obj/sys/arch/evbmips/compile/RTL8672/netbsd \
    | grep -c -E '	(lwl|lwr|swl|swr)	'      # must print 0
```

## 4. Userland and ramdisk

The whole base system has to be built with the Lexra flags, because libc
contains assembly that would otherwise use the missing instructions:

```sh
./build.sh -U -u -j8 -m evbmips-eb -O ../obj -T ../tools \
    -V CPUFLAGS="-mlexra -D__LEXRA__" \
    -V MKCXX=no -V MKMAN=no -V MKDOC=no -V MKINFO=no -V MKHTML=no -V MKNLS=no \
    -V MKKERBEROS=no -V MKLDAP=no -V MKPAM=no -V MKYP=no -V MKLLVM=no \
    -V MKX11=no -V MKGDB=no -V MKGCCCMDS=no -V MKBINUTILS=no -V MKRUMP=no \
    distribution
```

With that many parts switched off, the very last step (`checkflist`, a
consistency check of the file lists) fails. Everything is built and
installed by then, so the failure can be ignored for this purpose.

Then the ramdisk and the kernel that holds it:

```sh
(cd etc && ../../tools/bin/nbmake-evbmips-eb obj MAKEDEV)
(cd distrib/evbmips/rtl8672ramdisk && ../../../../tools/bin/nbmake-evbmips-eb obj dependall)

./build.sh -U -u -j8 -m evbmips-eb -O ../obj -T ../tools kernel=RTL8672_MD

K=../obj/sys/arch/evbmips/compile/RTL8672_MD
cp $K/netbsd $K/netbsd.md
../tools/bin/mipseb--netbsd-mdsetimage -v $K/netbsd.md \
    ../obj/distrib/evbmips/rtl8672ramdisk/ramdisk.fs
```

`build.sh` rewrites the `nbmake-evbmips-eb` wrapper each time it runs, and
the wrapper remembers the `-V` settings of that run. Build the ramdisk
right after the `distribution` step, before another `build.sh` call without
`CPUFLAGS` replaces the wrapper.

## 5. The loader module

```sh
cd /path/to/this/repo/loader
TOOLS=/path/to/tools/bin sh build_kboot.sh /path/to/obj/sys/arch/evbmips/compile/RTL8672_MD/netbsd.md
```

This compresses the kernel, builds the two loader stages and produces
`tftp/h108nb.ko`. It refuses to continue if the kernel is too large or if
any loader object contains a forbidden instruction.

## 6. Boot it

On your computer, serve the file:

```sh
python3 loader/tftpd.py loader/tftp 6969
```

On the router, from a [root shell](getting-root.md):

```sh
tftp -g -r h108nb.ko -l /var/h108nb.ko <your-pc> 6969
insmod /var/h108nb.ko
```

Watch the [serial console](serial-console.md):

```
h108boot 2a: gathering 0019e3dc bytes
h108boot 2a: moved to 81d61c20, starting stage 2b
h108boot 2b: inflating 0019e3dc bytes to 80010000
h108boot 2b: kernel is 005c9710 bytes, jumping to 80010000
[   1.0000000] RTL8672: UART clock 197222400 Hz
...
```

## The smaller tests

Before a whole kernel, the board was brought up with three tiny modules:

```sh
cd loader
TOOLS=/path/to/tools/bin sh build.sh     # h108s1.ko, h108s2.ko, h108s3.ko
```

See [boot-chain.md](boot-chain.md) for what each one proves.

## Note for macOS

If the host compiler on your Mac only works with the Command Line Tools
selected (for example because `xcodebuild` crashes), export

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
```

before `build.sh` and apply `0003-tools-keep-DEVELOPER_DIR-on-macOS.patch`.
Some host tools are built with a cleared environment (`env -i`), which
would otherwise drop the variable again.

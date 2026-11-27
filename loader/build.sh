#!/bin/sh
# Build the three loader modules into ./tftp, ready to be fetched by the router.
#   h108s1.ko  stage 1: prints one line and returns (checks the module format)
#   h108s2.ko  stage 2: jumps to the position-independent assembly test
#   h108s3.ko  stage 3: copies the C test to 0xA0000000 and jumps to it
set -eu
cd "$(dirname "$0")"
: "${TOOLS:?set TOOLS to the bin directory of the NetBSD cross tools}"
X=$TOOLS/mipseb--netbsd-
HELLO=../tests/hello
CFLAGS="-Os -ffreestanding -fno-pic -mno-abicalls -mlong-calls -G 0 -msoft-float \
 -march=mips1 -mlexra -EB -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables \
 -Wall -Wextra"
mkdir -p tftp obj

# Assembly test: position independent, link address does not matter.
${X}as -EB -march=r3000 -non_shared -o obj/hello_asm.o $HELLO/hello_asm.S
${X}ld -EB -Ttext 0x80000000 -e _start -o obj/hello_asm.elf obj/hello_asm.o
${X}objcopy -O binary -j .text obj/hello_asm.elf obj/hello_asm.bin

# C test linked to run uncached from the start of RAM.
HC="-Os -ffreestanding -fno-pic -mno-abicalls -G 0 -msoft-float -march=mips1 -mlexra -EB -Wall -Wextra"
${X}gcc $HC -c -o obj/start.o $HELLO/start.S
${X}gcc $HC -c -o obj/hello.o $HELLO/hello.c
${X}ld -nostdlib -static -T $HELLO/hello.ld --defsym LOAD_BASE=0xa0000000 \
    -o obj/hello_k1.elf obj/start.o obj/hello.o
${X}objcopy -O binary obj/hello_k1.elf obj/hello_k1.bin

${X}gcc $CFLAGS -DSTAGE=1 -c -o tftp/h108s1.ko h108boot.c
${X}gcc $CFLAGS -DSTAGE=2 -DPAYLOAD='"obj/hello_asm.bin"' -c -o tftp/h108s2.ko h108boot.c
${X}gcc $CFLAGS -DSTAGE=3 -DPAYLOAD='"obj/hello_k1.bin"' -c -o tftp/h108s3.ko h108boot.c

for m in tftp/h108s1.ko tftp/h108s2.ko tftp/h108s3.ko obj/hello_k1.elf; do
	if ${X}objdump -d "$m" | grep -E '	(lwl|lwr|swl|swr)	' >/dev/null; then
		echo "ERROR: $m contains lwl/lwr/swl/swr" >&2; exit 1
	fi
done
ls -l tftp/*.ko
echo "ok: no lwl/lwr/swl/swr in any image"

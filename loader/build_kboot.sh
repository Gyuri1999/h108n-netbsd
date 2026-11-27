#!/bin/sh
# Wrap a NetBSD kernel into tftp/h108nb.ko, the Linux module that boots it.
# usage: TOOLS=<tooldir>/bin build_kboot.sh path/to/netbsd
set -eu
cd "$(dirname "$0")"
: "${TOOLS:?set TOOLS to the bin directory of the NetBSD cross tools}"
X=$TOOLS/mipseb--netbsd-
KERNEL=${1:?usage: build_kboot.sh path/to/netbsd}
CF="-Os -ffreestanding -fno-pic -mno-abicalls -G 0 -msoft-float -march=mips1 -mlexra -EB \
 -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Wall -Wextra"
# Addresses from loader.h
GATHER=0x80010000; S2A=0x804c0000; S2B=0x81f00000
mkdir -p tftp obj

# Kernel: flat image starting at its load address, then raw DEFLATE.
LOAD=0x$(${X}readelf -l -W "$KERNEL" | awk '$1 == "LOAD" { print substr($3, 3); exit }')
ENTRY=$(${X}readelf -h "$KERNEL" | awk '/Entry point/ { print $4 }')
END=0x$(${X}nm "$KERNEL" | awk '$3 == "end" { print $1 }')
${X}objcopy -O binary "$KERNEL" obj/netbsd.bin
python3 -I mkpayload.py obj/netbsd.bin obj/netbsd.z
echo "kernel: load $LOAD entry $ENTRY end $END"

# Stage 2b (inflater) at the top of RAM.
${X}gcc $CF -DMAIN=stage2b_main -DSTACK="($S2B + 0x10000 - 32)" -c -o obj/start2b.o stage_start.S
${X}gcc $CF -c -o obj/stage2b.o stage2b.c
${X}gcc $CF -c -o obj/inflate.o inflate.c
${X}ld -nostdlib -static -T stage2.ld --defsym BASE=$S2B \
    -o obj/stage2b.elf obj/start2b.o obj/stage2b.o obj/inflate.o
${X}objcopy -O binary obj/stage2b.elf obj/stage2b.bin

# Stage 2a (gather and relocate) inside the old Linux image; carries 2b.
${X}gcc $CF -DMAIN=stage2a_main -c -o obj/start2a.o stage_start.S
${X}gcc $CF -DSTAGE2B='"obj/stage2b.bin"' -c -o obj/stage2a.o stage2a.c
${X}ld -nostdlib -static -T stage2.ld --defsym BASE=$S2A \
    -o obj/stage2a.elf obj/start2a.o obj/stage2a.o
${X}objcopy -O binary obj/stage2a.elf obj/stage2a.bin

# Size checks: compressed kernel must fit the gather area, and the
# inflated kernel (with bss) must end below the compressed copy.
python3 -I - "$END" "$LOAD" <<PYEOF
import os, sys
end, load = int(sys.argv[1], 16), int(sys.argv[2], 16)
gather, s2a, s2b = $GATHER, $S2A, $S2B
z = os.path.getsize('obj/netbsd.z')
high = s2b - ((z + 15) & ~15)
print('compressed %d KB of %d KB gather room; kernel end %#x, compressed copy at %#x, gap %d KB'
      % (z // 1024, (s2a - gather) // 1024, end, high, (high - end) // 1024))
ok = z <= s2a - gather and end < high and os.path.getsize('obj/stage2a.bin') < 0x10000
sys.exit(0 if ok else 1)
PYEOF

${X}gcc $CF -mlong-calls -DLOAD_ADDR=${LOAD}ul -DENTRY_ADDR=${ENTRY}ul \
    -DSTAGE2='"obj/stage2a.bin"' -DPAYLOAD='"obj/netbsd.z"' \
    -c -o tftp/h108nb.ko h108kboot.c

for m in tftp/h108nb.ko obj/stage2a.elf obj/stage2b.elf; do
	if ${X}objdump -d "$m" | grep -E '	(lwl|lwr|swl|swr)	' >/dev/null; then
		echo "ERROR: $m contains lwl/lwr/swl/swr" >&2; exit 1
	fi
done
ls -l obj/stage2a.bin obj/stage2b.bin tftp/h108nb.ko
echo "ok: no lwl/lwr/swl/swr in loader"

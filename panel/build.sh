#!/bin/sh
# Build h108panel, the web panel for the router's stock Linux, and put it
# where the TFTP server (../loader/tftpd.py) can serve it.
set -eu
cd "$(dirname "$0")"
: "${TOOLS:?set TOOLS to the bin directory of the NetBSD cross tools}"
X=$TOOLS/mipseb--netbsd-
CF="-Os -ffreestanding -fno-pic -mno-abicalls -G 0 -msoft-float -march=mips1 -mlexra -EB \
 -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -fno-stack-protector -Wall -Wextra"
mkdir -p obj ../loader/tftp
${X}gcc $CF -DPAGE='"panel.html"' -c -o obj/panel.o panel.c
${X}ld -nostdlib -static -T panel.ld -o obj/h108panel.elf obj/panel.o
${X}strip -o obj/h108panel obj/h108panel.elf
if ${X}objdump -d obj/h108panel.elf | grep -E '	(lwl|lwr|swl|swr)	' >/dev/null; then
	echo "ERROR: h108panel contains lwl/lwr/swl/swr" >&2; exit 1
fi
cp obj/h108panel ../loader/tftp/h108panel
ls -l ../loader/tftp/h108panel
echo "ok: no lwl/lwr/swl/swr"

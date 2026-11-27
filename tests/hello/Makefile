# Bare-metal serial test for the ZTE H108N, built with the NetBSD cross
# toolchain (patched GCC with -mlexra).
# Set TOOLS to the bin directory of the NetBSD cross tools.
TOOLS	?= $(error set TOOLS to the bin directory of the NetBSD cross tools)
CROSS	?= $(TOOLS)/mipseb--netbsd-
LOAD_BASE ?= 0x80000000

CFLAGS	= -Os -ffreestanding -fno-pic -mno-abicalls -G 0 -msoft-float \
	  -march=mips1 -mlexra -EB -Wall -Wextra
LDFLAGS	= -nostdlib -static -T hello.ld --defsym LOAD_BASE=$(LOAD_BASE)

all: hello.bin check

start.o: start.S
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<

hello.o: hello.c
	$(CROSS)gcc $(CFLAGS) -c -o $@ $<

hello.elf: start.o hello.o hello.ld
	$(CROSS)ld $(LDFLAGS) -o $@ start.o hello.o

hello.bin: hello.elf
	$(CROSS)objcopy -O binary $< $@

# Fail if any instruction the Lexra core lacks made it into the image.
check: hello.elf
	@if $(CROSS)objdump -d $< | grep -E '\s(lwl|lwr|swl|swr)\s'; then \
		echo "ERROR: unaligned load/store instructions present"; exit 1; \
	else echo "ok: no lwl/lwr/swl/swr"; fi

clean:
	rm -f *.o hello.elf hello.bin

.PHONY: all check clean

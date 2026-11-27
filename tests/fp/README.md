# Floating-point isolation test

`sysctl vm.loadavg` prints `nan nan nan`. This tiny program narrows the cause
to a single soft-float operation, using only integer `printf`.

Build it with the cross toolchain (same flags as the base system) and run it
on the device over the serial console. Then check whether libgcc is clean:

```sh
TOOLS=../../nb/tools/bin
$TOOLS/mipseb--netbsd-gcc -O -mlexra -D__LEXRA__ -o fp fp.c
$TOOLS/mipseb--netbsd-objdump -d fp | grep -E '\t(lwl|lwr|swl|swr)\t'   # should be empty
# and the soft-float library it links against:
$TOOLS/mipseb--netbsd-objdump -d <path>/libgcc.a | grep -cE '\t(lwl|lwr|swl|swr)\t'
```

Expected program output: `idiv=3500 back=7 mul=6`. Anything else means the
soft-float routines are miscompiled for the Lexra core (likely libgcc built
without `-mlexra`).

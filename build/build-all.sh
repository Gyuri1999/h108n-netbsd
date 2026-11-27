#!/bin/sh
# Reproducible build of the H108N/Lexra NetBSD port.
#
# Clones the NetBSD fork branch that carries the Lexra patches as commits,
# cross-builds the toolchain and the RTL8672 kernel, and checks that no
# forbidden Lexra instruction (lwl/lwr/swl/swr) slipped into the kernel.
#
# With FULL=1 it also builds the memory-disk kernel (RTL8672_MD) and, if a
# checkout of this repo is given in REPO, the loader module (tftp/h108nb.ko).
#
# Runs on Linux (incl. CI and the Docker image here) and on the BSDs. On
# macOS use a case-sensitive volume for WORK; Linux is case-sensitive already.
#
# Env knobs (all optional):
#   FORK_URL  NetBSD fork (default https://github.com/Gyuri1999/src.git)
#   BRANCH    branch with the port    (default h108n-lexra)
#   WORK      build directory         (default ./nb)
#   JOBS      parallelism             (default: CPU count)
#   FULL      1 = also ramdisk + MD kernel + loader (default 0)
#   REPO      path to a checkout of h108n-netbsd (needed for the loader)
set -eu

FORK_URL="${FORK_URL:-https://github.com/Gyuri1999/src.git}"
BRANCH="${BRANCH:-h108n-lexra}"
WORK="${WORK:-$PWD/nb}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
FULL="${FULL:-0}"
REPO="${REPO:-}"

SRC="$WORK/src"; OBJ="$WORK/obj"; TOOLS="$WORK/tools"
TAB=$(printf '\t')
mkdir -p "$WORK"

if [ ! -d "$SRC/.git" ]; then
  echo ">>> cloning $FORK_URL @ $BRANCH (depth 1)"
  git clone --depth 1 --branch "$BRANCH" "$FORK_URL" "$SRC"
fi
cd "$SRC"

echo ">>> [1/4] cross toolchain"
./build.sh -U -j"$JOBS" -m evbmips-eb -O "$OBJ" -T "$TOOLS" tools

echo ">>> [2/4] -mlexra present in the cross gcc?"
"$TOOLS/bin/mipseb--netbsd-gcc" --target-help 2>/dev/null | grep -i lexra \
  || { echo "FAIL: -mlexra option missing from the cross compiler"; exit 1; }

echo ">>> [3/4] RTL8672 kernel"
./build.sh -U -u -j"$JOBS" -m evbmips-eb -O "$OBJ" -T "$TOOLS" kernel=RTL8672
K="$OBJ/sys/arch/evbmips/compile/RTL8672/netbsd"

echo ">>> [4/4] forbidden-instruction check (must be 0)"
n=$("$TOOLS/bin/mipseb--netbsd-objdump" -d "$K" \
      | grep -c -E "${TAB}(lwl|lwr|swl|swr)${TAB}" || true)
echo "forbidden instructions in kernel: $n"
[ "$n" = "0" ] || { echo "FAIL: forbidden Lexra instruction in the kernel"; exit 1; }
echo ">>> OK: clean kernel at $K"

if [ "$FULL" != "1" ]; then
  echo ">>> done (tools + RTL8672 kernel). Set FULL=1 for ramdisk + loader."
  exit 0
fi

echo ">>> [full] userland distribution with Lexra flags"
./build.sh -U -u -j"$JOBS" -m evbmips-eb -O "$OBJ" -T "$TOOLS" \
    -V CPUFLAGS="-mlexra -D__LEXRA__" \
    -V MKCXX=no -V MKMAN=no -V MKDOC=no -V MKINFO=no -V MKHTML=no -V MKNLS=no \
    -V MKKERBEROS=no -V MKLDAP=no -V MKPAM=no -V MKYP=no -V MKLLVM=no \
    -V MKX11=no -V MKGDB=no -V MKGCCCMDS=no -V MKBINUTILS=no -V MKRUMP=no \
    distribution || echo "(checkflist may fail with these MK* off; everything is built — continuing)"

echo ">>> [full] ramdisk + RTL8672_MD kernel"
( cd etc && "$TOOLS/bin/nbmake-evbmips-eb" obj MAKEDEV )
( cd distrib/evbmips/rtl8672ramdisk && "$TOOLS/bin/nbmake-evbmips-eb" obj dependall )
./build.sh -U -u -j"$JOBS" -m evbmips-eb -O "$OBJ" -T "$TOOLS" kernel=RTL8672_MD
KM="$OBJ/sys/arch/evbmips/compile/RTL8672_MD"
cp "$KM/netbsd" "$KM/netbsd.md"
"$TOOLS/bin/mipseb--netbsd-mdsetimage" -v "$KM/netbsd.md" \
    "$OBJ/distrib/evbmips/rtl8672ramdisk/ramdisk.fs"
echo ">>> OK: memory-disk kernel at $KM/netbsd.md"

if [ -n "$REPO" ] && [ -d "$REPO/loader" ]; then
  echo ">>> [full] loader module h108nb.ko"
  ( cd "$REPO/loader" && TOOLS="$TOOLS/bin" sh build_kboot.sh "$KM/netbsd.md" )
  echo ">>> OK: loader at $REPO/loader/tftp/h108nb.ko"
else
  echo ">>> skipping loader (set REPO=/path/to/h108n-netbsd to build it)"
fi
echo ">>> full build done"

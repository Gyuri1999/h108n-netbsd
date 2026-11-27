# Reproducible build

One script, [`build-all.sh`](build-all.sh), rebuilds the port from nothing:
it clones the NetBSD fork branch that carries the Lexra changes as commits
([`Gyuri1999/src` @ `h108n-lexra`](https://github.com/Gyuri1999/src/tree/h108n-lexra)),
cross-builds the toolchain and the `RTL8672` kernel, and fails if any
forbidden Lexra instruction (`lwl`/`lwr`/`swl`/`swr`) is in the kernel.

## With Docker (nothing to install)

```sh
docker build -t h108n-build build/
docker run --rm -v "$PWD/out:/work" h108n-build
```

The clean kernel ends up at `out/obj/sys/arch/evbmips/compile/RTL8672/netbsd`.

Full build (also the memory-disk kernel and the loader module):

```sh
docker run --rm -e FULL=1 -e REPO=/repo \
    -v "$PWD/out:/work" -v "$PWD:/repo" h108n-build
# -> loader/tftp/h108nb.ko
```

## Without Docker (Linux, macOS, BSD)

```sh
WORK=./nb sh build/build-all.sh          # tools + RTL8672 kernel + checks
FULL=1 REPO="$PWD" WORK=./nb sh build/build-all.sh   # + ramdisk + loader
```

On macOS use a case-sensitive volume for `WORK` (Linux is already
case-sensitive). The toolchain build is the long part; the kernel is quick.

## Knobs

| Env | Default | |
|---|---|---|
| `FORK_URL` | `https://github.com/Gyuri1999/src.git` | NetBSD fork |
| `BRANCH` | `h108n-lexra` | branch with the port |
| `WORK` | `./nb` | build directory |
| `JOBS` | CPU count | parallelism |
| `FULL` | `0` | `1` = ramdisk + MD kernel + loader |
| `REPO` | — | path to this repo (for the loader) |

## CI

[`.github/workflows/build.yml`](../.github/workflows/build.yml) runs the same
script on every push: it builds the toolchain (cached) and the `RTL8672`
kernel, runs the forbidden-instruction check, and uploads the kernel as an
artifact. See the badge in the top-level README.

The build matches the manual steps in [`docs/building.md`](../docs/building.md).

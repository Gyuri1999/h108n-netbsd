# Reproducible build environment for the H108N/Lexra NetBSD port.
#
#   docker build -t h108n-build build/
#   docker run --rm -v "$PWD/out:/work" h108n-build          # tools + kernel
#   docker run --rm -e FULL=1 -v "$PWD/out:/work" \
#              -v "$PWD:/repo" -e REPO=/repo h108n-build       # + ramdisk + loader
#
# Results land in ./out (the mounted /work): ./out/obj/... and, with FULL=1,
# ./loader/tftp/h108nb.ko in the mounted repo.
FROM debian:12-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential git ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
COPY build-all.sh /usr/local/bin/build-all.sh
RUN chmod +x /usr/local/bin/build-all.sh

# WORK defaults to the mounted /work so artifacts persist on the host.
ENV WORK=/work
ENTRYPOINT ["/usr/local/bin/build-all.sh"]

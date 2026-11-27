#!/usr/bin/env python3
"""Compress a raw kernel image into a raw DEFLATE stream for the loader."""
import sys
import zlib

src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
c = zlib.compressobj(9, zlib.DEFLATED, -15)
out = c.compress(data) + c.flush()
open(dst, 'wb').write(out)
print('%s: %d -> %d bytes (%.0f%%)' % (src, len(data), len(out), 100.0 * len(out) / len(data)))

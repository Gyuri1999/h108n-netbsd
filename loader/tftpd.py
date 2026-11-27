#!/usr/bin/env python3
"""Minimal read-only TFTP server for feeding files to the router.

Serves files from one directory on an unprivileged UDP port (default 6969),
octet mode, 512-byte blocks.  Only plain file names are accepted, so nothing
outside the directory can be read.  Usage: tftpd.py DIR [PORT] [BIND_ADDR]
"""
import os
import socket
import struct
import sys

RRQ, DATA, ACK, ERROR = 1, 3, 4, 5


def serve_file(peer, data, bind):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((bind, 0))
    s.settimeout(2.0)
    nblocks = (len(data) // 512) + 1
    for n in range(1, nblocks + 1):
        chunk = data[(n - 1) * 512:n * 512]
        pkt = struct.pack('>HH', DATA, n & 0xffff) + chunk
        for _ in range(8):
            s.sendto(pkt, peer)
            try:
                reply, addr = s.recvfrom(1024)
            except socket.timeout:
                continue
            if addr == peer and reply[:4] == struct.pack('>HH', ACK, n & 0xffff):
                break
        else:
            s.close()
            return False
    s.close()
    return True


def main():
    root = os.path.realpath(sys.argv[1])
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 6969
    bind = sys.argv[3] if len(sys.argv) > 3 else '0.0.0.0'
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((bind, port))
    print('tftpd: serving %s on %s:%d' % (root, bind, port), flush=True)
    while True:
        pkt, peer = sock.recvfrom(1024)
        if len(pkt) < 4 or struct.unpack('>H', pkt[:2])[0] != RRQ:
            sock.sendto(struct.pack('>HH', ERROR, 4) + b'read only\0', peer)
            continue
        name = pkt[2:].split(b'\0')[0].decode('ascii', 'replace')
        path = os.path.join(root, name)
        if os.path.basename(name) != name or not os.path.isfile(path):
            print('tftpd: %s asked for %r: refused' % (peer[0], name), flush=True)
            sock.sendto(struct.pack('>HH', ERROR, 1) + b'not found\0', peer)
            continue
        with open(path, 'rb') as f:
            data = f.read()
        ok = serve_file(peer, data, bind)
        print('tftpd: %s got %s (%d bytes): %s'
              % (peer[0], name, len(data), 'ok' if ok else 'FAILED'), flush=True)


if __name__ == '__main__':
    main()

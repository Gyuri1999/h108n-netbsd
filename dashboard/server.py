#!/usr/bin/env python3
"""Local dashboard server for the ZTE H108N running NetBSD.

Owns the router's serial console (the router has no network under NetBSD
yet) and exposes it to a web page on this machine only:

  - everything the router prints is recorded and streamed to the page;
  - the boot stage is followed from the text (boot loader, Linux, loader
    stages, NetBSD, shell);
  - when a NetBSD shell is up, a few read-only commands are run in the
    background to fill the system cards; their output is kept out of the
    visible console.

Python standard library only.  usage: server.py [PORT] [SERIAL_DEVICE] [LOG]
"""
import calendar
import json
import os
import random
import re
import select
import string
import sys
import termios
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8672
DEVICE = sys.argv[2] if len(sys.argv) > 2 else '/dev/cu.PL2303G-USBtoUART80'
LOGFILE = sys.argv[3] if len(sys.argv) > 3 else os.path.join(HERE, 'console.log')
MAXBUF = 2 * 1024 * 1024

STAGES = ['off', 'bootloader', 'linux', 'loader', 'netbsd', 'shell']

# First match wins; checked against each completed line of console text.
STAGE_RULES = [
    (re.compile(r'^panic:|kernel: .* trap|Process \(pid 1\) got sig'), 'trouble'),
    (re.compile(r"^Booting$|Press '1' to enter BOOT console"), 'bootloader'),
    (re.compile(r'^Linux version |ZXHN H108N$|^Login:'), 'linux'),
    (re.compile(r'^h108boot'), 'loader'),
    (re.compile(r'The NetBSD Foundation|^\[ +\d+\.\d+\] NetBSD \d'), 'netbsd'),
    (re.compile(r'^NetBSD on the ZTE ZXHN H108N'), 'shell'),
]


class Router:
    def __init__(self):
        self.lock = threading.Lock()          # buffers and state
        self.cmdlock = threading.Lock()       # one shell exchange at a time
        self.fd = None
        self.error = None
        self.visible = bytearray()            # console as shown on the page
        self.base = 0                         # offset of visible[0]
        self.capture = None                   # bytearray while a hidden command runs
        self.partial = b''
        self.stage = 'unknown'
        self.trouble = None
        self.stage_at = {}
        self.last_rx = 0.0
        self.last_user = 0.0
        self.info = {}
        self.info_at = 0.0
        self.boot_id = 0
        self.dmesg_boot = -1
        self.log = open(LOGFILE, 'ab')
        self.open()

    # --- serial port -----------------------------------------------------
    def open(self):
        try:
            fd = os.open(DEVICE, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            a = termios.tcgetattr(fd)
            a[0] = 0
            a[1] = 0
            a[3] = 0
            a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            a[4] = a[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, a)
            self.fd, self.error = fd, None
        except OSError as e:
            self.fd, self.error = None, str(e)

    def write(self, data):
        if self.fd is None:
            return False
        try:
            for i in range(0, len(data), 16):     # the router has a small FIFO
                os.write(self.fd, data[i:i + 16])
                time.sleep(0.02)
            return True
        except OSError as e:
            self.error = str(e)
            return False

    def reader(self):
        while True:
            if self.fd is None:
                time.sleep(2)
                self.open()
                continue
            try:
                r, _, _ = select.select([self.fd], [], [], 0.5)
                data = os.read(self.fd, 4096) if r else b''
            except BlockingIOError:
                data = b''
            except OSError as e:
                self.error = str(e)
                try:
                    os.close(self.fd)
                except OSError:
                    pass
                self.fd = None
                continue
            if data:
                self.feed(data)

    def feed(self, data):
        self.log.write(data)
        self.log.flush()
        with self.lock:
            self.last_rx = time.time()
            if self.capture is not None:
                self.capture += data
            else:
                self.visible += data
                if len(self.visible) > MAXBUF:
                    cut = len(self.visible) - MAXBUF // 2
                    del self.visible[:cut]
                    self.base += cut
            if self.capture is not None:
                # Output of our own background commands (dmesg replays the
                # whole boot) must not be mistaken for something happening now.
                self.partial = b''
                return
            lines = (self.partial + data).split(b'\n')
            self.partial = lines.pop()[-400:]
            for raw in lines:
                self.classify(raw.decode('latin1').strip('\r\x00 '))

    def classify(self, line):
        for rx, stage in STAGE_RULES:
            if rx.search(line):
                if stage == 'trouble':
                    self.trouble = line[:160]
                else:
                    self.set_stage(stage)
                return

    def set_stage(self, stage):
        if stage == self.stage:
            return
        if stage == 'bootloader':             # a fresh boot starts here
            self.boot_id += 1
            self.stage_at = {}
            self.trouble = None
            self.info = {}
        self.stage = stage
        self.stage_at[stage] = time.time()

    # --- shell exchanges -------------------------------------------------
    def run(self, cmd, timeout=6.0, hidden=True):
        """Run one command at the NetBSD shell and return its output, or
        None if the shell did not answer.  The end marker is typed with a
        quote in the middle, so the echo of the command line never matches
        the marker that the shell prints."""
        tag = ''.join(random.choice(string.ascii_lowercase[2:]) for _ in range(10))
        typed = '%s; echo __en""d_%s__\r' % (cmd, tag)
        marker = ('__end_%s__' % tag).encode()
        with self.cmdlock:
            with self.lock:
                if hidden:
                    self.capture = bytearray()
                start = self.base + len(self.visible)
            self.write(typed.encode())
            deadline = time.time() + timeout
            out = None
            while time.time() < deadline:
                time.sleep(0.05)
                with self.lock:
                    buf = bytes(self.capture) if hidden else \
                        bytes(self.visible[max(0, start - self.base):])
                i = buf.find(marker)
                if i >= 0:
                    out = buf[:i]
                    break
            time.sleep(0.25)                  # let the prompt arrive
            with self.lock:
                if hidden:
                    leftover = bytes(self.capture)
                    self.capture = None
                    if out is None and leftover.strip():
                        # nothing answered: show what came back instead of hiding it
                        self.visible += leftover
            if out is None:
                return None
            text = out.decode('latin1').replace('\r', '')
            # drop the echoed command line
            return text.split('\n', 1)[1] if '\n' in text else ''

    def poller(self):
        time.sleep(1.5)
        while True:
            try:
                self.poll_once()
            except Exception as e:            # keep the thread alive
                self.error = 'poller: %s' % e
            time.sleep(1.0)

    def poll_once(self):
        now = time.time()
        with self.lock:
            stage, quiet = self.stage, now - self.last_rx
            idle_user = now - self.last_user
            boot, due = self.boot_id, now - self.info_at
        if stage == 'unknown' and quiet > 3:
            # Started while the router was already up: is a shell there?
            if self.run('echo', timeout=3) is not None:
                with self.lock:
                    self.set_stage('shell')
            else:
                with self.lock:
                    self.stage = 'silent'
            return
        if stage != 'shell' or quiet < 2 or idle_user < 6 or due < 12:
            return
        info = {}
        out = self.run('sysctl -n kern.ostype kern.osrelease hw.model '
                       'hw.machine_arch hw.physmem hw.pagesize kern.boottime; date +%s')
        if out is None:
            with self.lock:
                self.info_at = now - 6      # try again a little later
            return
        v = [l.strip() for l in out.strip().split('\n')]
        if len(v) >= 8:
            info.update(ostype=v[0], osrelease=v[1], model=v[2], arch=v[3])
            try:
                info['physmem'] = int(v[4])
                # "sysctl -n kern.boottime" prints seconds since the epoch;
                # accept the date form too in case that ever changes.
                if v[6].isdigit():
                    boot_t = int(v[6])
                else:
                    boot_t = calendar.timegm(
                        time.strptime(v[6], '%a %b %d %H:%M:%S %Y'))
                info['uptime'] = max(0, int(v[7]) - boot_t)
                info['router_time'] = int(v[7])
            except ValueError:
                pass
        out = self.run('df -k /')
        if out:
            for l in out.strip().split('\n')[1:]:
                f = l.split()
                if len(f) >= 6 and f[1].isdigit():
                    info['disk'] = dict(dev=f[0], total=int(f[1]), used=int(f[2]),
                                        avail=int(f[3]), cap=f[4])
        out = self.run('ps -ax')
        if out:
            procs = []
            for l in out.strip().split('\n')[1:]:
                f = l.split(None, 4)
                if len(f) == 5 and f[0].isdigit():
                    procs.append(dict(pid=int(f[0]), stat=f[2], time=f[3], cmd=f[4]))
            # leave out the ps we ran ourselves
            info['procs'] = [p for p in procs if not p['cmd'].startswith('ps -ax')]
        if self.dmesg_boot != boot:
            out = self.run('dmesg', timeout=10)
            if out:
                self.dmesg = self.parse_dmesg(out)
                self.dmesg_boot = boot
        info.update(getattr(self, 'dmesg', {}))
        with self.lock:
            self.info = info
            self.info_at = time.time()

    @staticmethod
    def parse_dmesg(text):
        d = {}
        pats = dict(
            kernel=r'\] (NetBSD \S+ \(\S+\) #\d+: .*)$',
            cpu=r'cpu0: (.*CPU.*?) \(0x[0-9a-f]+\)',
            icache=r'cpu0: (\S+ \S+ Instruction cache)',
            dcache=r'cpu0: (\S+ \S+(?: \S+)? Data cache)',
            total_kb=r'total memory = (\d+) KB',
            avail_kb=r'avail memory = (\d+) KB',
            timer=r'rtl8672: (timer on CPU interrupt mask \S+)',
            root=r'\] (root on \S+)',
        )
        # The message buffer survives warm reboots, so it can hold several
        # boots: later lines override earlier ones.
        for l in text.split('\n'):
            for k, p in pats.items():
                m = re.search(p, l)
                if m:
                    d[k] = int(m.group(1)) if k.endswith('_kb') else m.group(1).strip()
        return d

    # --- page API --------------------------------------------------------
    def snapshot(self, since):
        with self.lock:
            end = self.base + len(self.visible)
            if since < self.base or since > end:
                since = max(self.base, end - 20000)
            chunk = bytes(self.visible[since - self.base:])
            now = time.time()
            return dict(
                stage=self.stage, trouble=self.trouble, boot=self.boot_id,
                stage_at={k: int(now - v) for k, v in self.stage_at.items()},
                serial_ok=self.fd is not None, error=self.error,
                quiet=round(now - self.last_rx, 1) if self.last_rx else None,
                info=self.info, info_age=int(now - self.info_at) if self.info_at else None,
                log=chunk.decode('latin1'), next=end, device=DEVICE)

    def user_send(self, text):
        with self.lock:
            self.last_user = time.time()
        with self.cmdlock:                    # never interleave with a poll
            return self.write(text.encode('latin1', 'replace'))


ROUTER = None
ALLOWED_ORIGINS = ('http://127.0.0.1:%d' % PORT, 'http://localhost:%d' % PORT)


class Handler(BaseHTTPRequestHandler):
    server_version = 'h108dash/1'

    def log_message(self, *a):
        pass

    def reply(self, code, body, ctype='application/json'):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path, _, query = self.path.partition('?')
        if path in ('/', '/index.html'):
            with open(os.path.join(HERE, 'index.html'), 'rb') as f:
                return self.reply(200, f.read(), 'text/html; charset=utf-8')
        if path == '/api/poll':
            m = re.search(r'since=(\d+)', query)
            return self.reply(200, ROUTER.snapshot(int(m.group(1)) if m else 0))
        self.reply(404, {'error': 'not found'})

    def do_POST(self):
        # This server can type into a root shell: only this page may ask.
        origin = self.headers.get('Origin')
        if origin is not None and origin not in ALLOWED_ORIGINS:
            return self.reply(403, {'error': 'forbidden'})
        if self.headers.get('Content-Type', '').split(';')[0] != 'application/json':
            return self.reply(415, {'error': 'json only'})
        try:
            n = int(self.headers.get('Content-Length', '0'))
            req = json.loads(self.rfile.read(min(n, 65536)) or b'{}')
        except (ValueError, json.JSONDecodeError):
            return self.reply(400, {'error': 'bad request'})
        if self.path == '/api/send':
            text = str(req.get('text', ''))[:2000]
            if req.get('enter'):
                text += '\r'
            return self.reply(200, {'ok': ROUTER.user_send(text)})
        if self.path == '/api/refresh':
            with ROUTER.lock:
                ROUTER.info_at = 0
                ROUTER.last_user = 0
            return self.reply(200, {'ok': True})
        self.reply(404, {'error': 'not found'})


def main():
    global ROUTER
    ROUTER = Router()
    threading.Thread(target=ROUTER.reader, daemon=True).start()
    threading.Thread(target=ROUTER.poller, daemon=True).start()
    httpd = ThreadingHTTPServer(('127.0.0.1', PORT), Handler)
    print('dashboard: http://127.0.0.1:%d  serial: %s  (%s)' %
          (PORT, DEVICE, 'open' if ROUTER.fd is not None else ROUTER.error), flush=True)
    httpd.serve_forever()


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Turtles in Time PC port: code server for online play.

The host asks for a code ("K7QX"), the other player enters it, and from then on this server passes
the game's packets between the two. Nothing else: it keeps no logs of play and stores nothing on
disk. Only paired players' packets are relayed, at a limited rate, so it can't be used as an open
relay.

    python3 tmnt_relay.py [--port 7846] [--bind 0.0.0.0]

Protocol (UDP, first byte = type):
  client -> server   H "TMNR" ver            host: give me a code (repeat until answered)
                     J "TMNR" ver CODE       join the game with this code
                     K                       keepalive while waiting
                     R payload               pass payload to the other player
                     Q                       leaving
  server -> client   C CODE                  your code (host)
                     P                       paired: the other player is there
                     R payload               from the other player
                     E text                  error (unknown code, game full, version)
"""
import argparse
import secrets
import socket
import sys
import time

MAGIC = b"TMNR"
VERSION = 1
CODE_CHARS = "ACDEFHJKMNPRTWXY3479"   # no look-alikes (0/O, 1/I, 5/S, 8/B, ...)
IDLE_WAITING = 60                    # seconds a host may wait without a keepalive
IDLE_PAIRED = 60                     # seconds without traffic before a game is dropped
MAX_SESSIONS = 200
MAX_PER_IP = 4
RATE = 600                           # packets per second per player (the game sends ~60)
MAX_PACKET = 1400


class Session:
    def __init__(self, code, host):
        self.code = code
        self.host = host
        self.guest = None
        self.last = time.monotonic()


def log(*a):
    print(time.strftime("%Y-%m-%d %H:%M:%S"), *a, flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", type=int, default=7846)
    ap.add_argument("--bind", default="0.0.0.0")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))
    sock.settimeout(1.0)
    log(f"listening on udp {args.bind}:{args.port}")

    by_code = {}     # code -> Session
    by_addr = {}     # (ip, port) -> Session
    rate = {}        # addr -> [window start, count]

    def drop(s, why):
        by_code.pop(s.code, None)
        for a in (s.host, s.guest):
            if a and by_addr.get(a) is s:
                del by_addr[a]
        log(f"game {s.code} closed ({why})")

    def send(addr, data):
        try:
            sock.sendto(data, addr)
        except OSError:
            pass

    last_sweep = time.monotonic()
    while True:
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            data = None
        except OSError:
            continue
        now = time.monotonic()

        if now - last_sweep > 5:   # forget idle games
            last_sweep = now
            for s in list(by_code.values()):
                if now - s.last > (IDLE_PAIRED if s.guest else IDLE_WAITING):
                    drop(s, "idle")
            for a in [a for a, (t, _) in rate.items() if now - t > 10]:
                del rate[a]
        if not data or len(data) > MAX_PACKET:
            continue

        r = rate.setdefault(addr, [now, 0])
        if now - r[0] >= 1:
            r[0], r[1] = now, 0
        r[1] += 1
        if r[1] > RATE:
            continue

        t = data[:1]
        s = by_addr.get(addr)
        if t == b"R":
            if s and s.guest:
                s.last = now
                send(s.guest if addr == s.host else s.host, data)
        elif t == b"K":
            if s:
                s.last = now
        elif t == b"H":
            if data[1:5] != MAGIC:
                continue
            if data[5:6] != bytes([VERSION]):
                send(addr, b"EUPDATE THE GAME")
                continue
            if s and s.host == addr:              # repeated request: same code
                s.last = now
                send(addr, b"C" + s.code.encode())
                continue
            if s:
                drop(s, "host started again")
            if len(by_code) >= MAX_SESSIONS or sum(1 for x in by_code.values() if x.host[0] == addr[0]) >= MAX_PER_IP:
                send(addr, b"ESERVER FULL")
                continue
            code = "".join(secrets.choice(CODE_CHARS) for _ in range(4))
            while code in by_code:
                code = "".join(secrets.choice(CODE_CHARS) for _ in range(4))
            s = Session(code, addr)
            by_code[code] = s
            by_addr[addr] = s
            log(f"game {code} hosted")
            send(addr, b"C" + code.encode())
        elif t == b"J":
            if data[1:5] != MAGIC:
                continue
            if data[5:6] != bytes([VERSION]):
                send(addr, b"EUPDATE THE GAME")
                continue
            code = data[6:10].decode("ascii", "replace").upper()
            g = by_code.get(code)
            if not g:
                send(addr, b"EUNKNOWN CODE")
                continue
            if g.guest and g.guest != addr:
                send(addr, b"EGAME IS FULL")
                continue
            if g.host == addr:
                send(addr, b"ETHAT IS YOUR OWN CODE")
                continue
            if s and s is not g:
                drop(s, "player joined another game")
            if not g.guest:
                log(f"game {code} joined")
            g.guest = addr
            g.last = now
            by_addr[addr] = g
            send(addr, b"P")
            send(g.host, b"P")
        elif t == b"Q":
            if s:
                other = s.guest if addr == s.host else s.host
                if other:
                    send(other, b"EOTHER PLAYER LEFT")
                drop(s, "player left")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)

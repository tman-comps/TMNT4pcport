"""Build the recompiler's code map (map.json) from coverage logs plus static tracing.

Turtles in Time dispatches almost everything through jump tables and pushed return addresses, so
a purely static trace finds only a few percent of the code. The ground truth is what the game
actually executed: the coverage logs written by the port's own core (tmnt_tool ... cdl:FILE),
one byte per 24-bit address:
  0x01 exec m=1  0x02 exec m=0  0x04 exec x=1  0x08 exec x=0  0x10 exec emulation mode

1. every executed instruction start becomes a map entry, with the M/X state it ran with
2. from each of them (and from the vectors) the code is followed statically: fall-through,
   branches both ways, JSR/JSL/JMP targets. Static decoding stops at anything already in the map,
   so it can never displace executed code.

Static guesses are harmless for correctness: the generated code evaluates operand widths at
runtime and every static goto is guarded by the real PC (see recomp.py), so a wrong guess only
costs a trip through the dispatcher or the interpreter.

usage: cdlmap.py rom.sfc out_map.json hints.txt cdl_file [cdl_file ...]
       (a cdl argument may also be a directory: every *.cdl in it)
hints.txt (optional, '-' for none):  entry <pc24> <m> <x>  |  data <pc24> <len hex>
writes out_map.json and prints a short coverage report per bank
"""
import glob
import json
import os
import sys
from collections import deque

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from op65816 import OPS, oplen  # noqa: E402

ROM = open(sys.argv[1], "rb").read()
OUT = sys.argv[2]
HINTS = sys.argv[3]
NB = len(ROM) // 0x8000

files = []
for a in sys.argv[4:]:
    files += sorted(glob.glob(os.path.join(a, "*.cdl"))) if os.path.isdir(a) else [a]

# ---- merge the logs into one byte per ROM offset (LoROM: banks 00-7D and 80-FF, $8000-$FFFF)
cdl = bytearray(len(ROM))
for f in files:
    d = open(f, "rb").read()
    for hb in range(0x100):
        if hb in (0x7E, 0x7F):
            continue
        b = hb & 0x3F if hb < 0x40 or 0x80 <= hb < 0xC0 else hb & 0x3F
        if b >= NB:
            continue
        seg = d[(hb << 16) + 0x8000:(hb << 16) + 0x10000]
        if not any(seg):
            continue
        base = b * 0x8000
        for i, v in enumerate(seg):
            if v:
                cdl[base + i] |= v


def to_off(a):
    bank, addr = a >> 16, a & 0xFFFF
    if bank in (0x7E, 0x7F) or addr < 0x8000:
        return None
    b = bank & 0x3F
    return b * 0x8000 + (addr - 0x8000) if b < NB else None


def to_pc(off):
    return 0x800000 | ((off // 0x8000) << 16) | 0x8000 | (off % 0x8000)


datarange = set()
entries = []
if HINTS != "-" and os.path.exists(HINTS):
    for line in open(HINTS):
        f = line.split("#")[0].split()
        if not f:
            continue
        if f[0] == "entry":
            entries.append((int(f[1], 16), int(f[2]), int(f[3])))
        elif f[0] == "data":
            o = to_off(int(f[1], 16))
            datarange.update(range(o, o + int(f[2], 16)))

code, owner, labels, conflicts = {}, {}, {}, []


def add(off, m, x, src):
    n = oplen(ROM[off], m, x)
    if off in code:
        return code[off]["len"] == n
    if off + n > len(ROM) or any(o in owner for o in range(off, off + n)) or off in datarange:
        conflicts.append([off, src])
        return False
    code[off] = {"len": n, "m": m, "x": x, "pb": to_pc(off) >> 16, "states": [[m, x]], "src": src}
    for o in range(off, off + n):
        owner[o] = off
    return True


# 1) executed instructions first, in address order; a byte inside an earlier executed
#    instruction that was also executed (overlapping code) keeps the earlier one
execd = [o for o in range(len(ROM)) if cdl[o] & 0x1F]
for o in execd:
    v = cdl[o]
    m = 1 if (v & 0x01 or v & 0x10) else 0
    x = 1 if (v & 0x04 or v & 0x10) else 0
    add(o, m, x, "cdl")
n_exec = len(code)

# 2) static extension
ENDERS = {"rts", "rtl", "rti", "jmp", "jml", "bra", "brl", "stp", "brk", "cop", "wdm"}
q = deque()
for o in execd:
    if o in code:
        c = code[o]
        q.append((o, c["m"], c["x"]))
for pc, m, x in entries:
    q.append((to_off(pc), m, x))
    labels[to_off(pc)] = "manual"
v = lambda a: ROM[to_off(0x00FF00 | a)] | ROM[to_off(0x00FF00 | a) + 1] << 8  # noqa: E731
for name, a in (("reset", 0xFC), ("nmi", 0xEA), ("irq", 0xEE)):
    t = v(a)
    if t >= 0x8000:
        labels[to_off(t)] = name
        q.append((to_off(t), 1, 1))
seen = set()
while q:
    off, m, x = q.popleft()
    while off is not None and (off, m, x) not in seen:
        seen.add((off, m, x))
        if off not in code and not add(off, m, x, "static"):
            break
        c = code[off]
        m, x = (c["m"], c["x"]) if c["src"] == "cdl" else (m, x)
        op = ROM[off]
        mn, mode = OPS[op]
        n = c["len"]
        arg = int.from_bytes(ROM[off + 1:off + n], "little") if n > 1 else 0
        pc = to_pc(off)
        pb = pc & 0xFF0000
        if mn == "rep":
            m = 0 if arg & 0x20 else m
            x = 0 if arg & 0x10 else x
        elif mn == "sep":
            m = 1 if arg & 0x20 else m
            x = 1 if arg & 0x10 else x
        elif mn == "plp":
            break   # flags unknown from here on; executed code past it is already in the map
        nxt = off + n if (off + n) // 0x8000 == off // 0x8000 else None
        if mode in ("rel", "rel16") and mn != "per":
            disp = arg - 0x100 if (mode == "rel" and arg >= 0x80) else arg
            if mode == "rel16" and arg >= 0x8000:
                disp = arg - 0x10000
            t = to_off(pb | ((pc + n + disp) & 0xFFFF))
            if t is not None:
                labels.setdefault(t, "loc")
                q.append((t, m, x))
            if mn in ("bra", "brl"):
                break
        elif mn in ("jsr", "jmp") and mode == "abs":
            t = to_off(pb | arg)
            if t is not None:
                labels.setdefault(t, "sub" if mn == "jsr" else "loc")
                q.append((t, m, x))
            if mn == "jmp":
                break
        elif mn in ("jsl", "jml") and mode == "absl":
            t = to_off(arg)
            if t is not None:
                labels.setdefault(t, "long" if mn == "jsl" else "loc")
                q.append((t, m, x))
            if mn == "jml":
                break
        elif mn in ENDERS or mode in ("absi", "absxi", "absil") and mn != "jsr":
            break
        off = nxt

print("logs:", len(files))
print("executed instructions:", n_exec, " static extension:", len(code) - n_exec, " rejected:", len(conflicts))
for b in range(NB):
    s = range(b * 0x8000, (b + 1) * 0x8000)
    ex = sum(1 for o in s if cdl[o] & 0x1F)
    st = sum(1 for o in s if o in code)
    if st:
        print(f"  bank {0x80 + b:02X}: executed {ex:5d}  in map {st:5d}")
for c in code.values():
    del c["src"]
json.dump({
    "code": {str(k): v for k, v in sorted(code.items())},
    "labels": {str(k): v for k, v in sorted(labels.items())},
    "conflicts": conflicts[:2000],
    "indirect": [], "tables": {}, "callers": {},
}, open(OUT, "w"), separators=(",", ":"))

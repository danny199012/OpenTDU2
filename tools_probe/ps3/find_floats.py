#!/usr/bin/env python3
"""Locate named float constants in the EBOOT.ELF data segments.

Step 1 of the "find the code by its constants" method (see
C:/OpenTDU2/docs/re/ps3/constant_pools.md): the PC spec names the constants of
the laws (1e-5, 300.0, 100.0, 0.8, the RK4 weights ...), so find where each one
sits in the PS3 build and then xref that address.

Writes port/out/float_consts.json: { "label": [vaddr, ...] }

Usage:
  python tools/find_floats.py                 # the default set
  python tools/find_floats.py 0.16666667 2.5  # extra values to look for
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, "game", "EBOOT.ELF")
OUT = os.path.join(ROOT, "port", "out", "float_consts.json")

SEGS = ((0x10000, 0x108C508, 0x0), (0x10A0000, 0xB7430, 0x1090000))

# label -> the float the PC spec names
DEFAULTS = {
    "1/6": 1.0 / 6.0,
    "1/3": 1.0 / 3.0,
    "1/2": 0.5,
    "2/3": 2.0 / 3.0,
    "5/6": 5.0 / 6.0,
    "1e-5": 1e-5,
    "1e-6": 1e-6,
    "0.01": 0.01,
    "0.1": 0.1,
    "0.8": 0.8,
    "1.0": 1.0,
    "5.0": 5.0,
    "100.0": 100.0,
    "300.0": 300.0,
    "1e5": 1e5,
    "1e6": 1e6,
    "1e7": 1e7,
    "19000.0": 19000.0,
    "16000.0": 16000.0,
    "-9.81": -9.81,
}


def main():
    wanted = dict(DEFAULTS)
    for a in sys.argv[1:]:
        wanted[a] = float(a)
    data = open(ELF, "rb").read()
    hits = {}
    for label, value in wanted.items():
        pat = struct.pack(">f", value)
        for va0, fsz, off in SEGS:
            seg = data[off:off + fsz]
            start = 0
            while True:
                i = seg.find(pat, start)
                if i < 0:
                    break
                hits.setdefault(label, []).append(va0 + i)
                start = i + 1
    with open(OUT, "w") as fh:
        json.dump(hits, fh)
    for label in sorted(hits, key=lambda k: -len(hits[k])):
        vs = hits[label]
        print(f"{label:10s} {len(vs):5d}  {[hex(v) for v in vs[:6]]}")
    print(f"-> {OUT}")


if __name__ == "__main__":
    main()

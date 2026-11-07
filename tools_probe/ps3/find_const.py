#!/usr/bin/env python3
"""Find the code that uses a data address, following one level of indirection.

PS3 constants are often reached indirectly: the address of a float constant sits
in a 4-byte pointer slot (in .rodata / the TOC area), and code loads the *slot*.
So `abs_refs.py` finds nothing for the constant itself. This tool:

  1. scans both PT_LOAD segments for 4-byte big-endian occurrences of the target
     (the candidate pointer slots),
  2. looks each slot up in port/out/abs_refs.json (direct absolute references),
  3. prints the referencing functions.

Usage:
  python tools/find_const.py 0xf120f0 [0xf7a1e4 ...] [--slots]
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, "game", "EBOOT.ELF")
ABS = os.path.join(ROOT, "port", "out", "abs_refs.json")

SEGS = ((0x10000, 0x108C508, 0x0), (0x10A0000, 0xB7430, 0x1090000))


def main():
    args = [a for a in sys.argv[1:]]
    show_slots = "--slots" in args
    if show_slots:
        args.remove("--slots")
    refs = json.load(open(ABS)) if os.path.exists(ABS) else {}
    data = open(ELF, "rb").read()

    for a in args:
        target = int(a, 16)
        pat = struct.pack(">I", target)
        slots = []
        for va, fsz, off in SEGS:
            seg = data[off:off + fsz]
            start = 0
            while True:
                i = seg.find(pat, start)
                if i < 0:
                    break
                slots.append(va + i)
                start = i + 4
        print(f"=== {a}: {len(slots)} 4-byte slot(s) ===")
        if show_slots:
            print(f"    slots: {[hex(s) for s in slots[:12]]}")
        found = {}
        for s in slots:
            for site, fn in refs.get(f"{s:08X}", []):
                found.setdefault(fn, []).append((s, site))
        if not found:
            print("    no absolute reference to any slot (TOC-relative or deeper)")
        for fn, lst in sorted(found.items()):
            print(f"    func_{fn:08X}: {len(lst)} site(s), e.g. slot {hex(lst[0][0])} @ {hex(lst[0][1])}")


if __name__ == "__main__":
    main()

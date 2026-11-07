#!/usr/bin/env python3
"""Single-pass index of ABSOLUTE 32-bit address materializations in EBOOT.ELF.

`addr_refs.py` only looks for TOC-relative pairs (`addis rT, r2, HA`), but the
PS3 build materializes almost every address absolutely (`addis rT, r0, HI` +
`addi/ori rT, rT, LO`) -- see the README's "key facts". This tool indexes the
absolute form for the WHOLE text segment in one pass, so a query is a dict
lookup instead of a 17M-word rescan (which is why `xref_addr.py` times out on
the 1.8 GB lifted tree).

Writes port/out/abs_refs.json:
    { "00F120F0": [[site_va, func_start], ...], ... }

Usage:
  python tools/abs_refs.py                      # build the index
  python tools/abs_refs.py 0xf120f0 0xf7a1e4    # build if needed, then query
  python tools/abs_refs.py --near 0xf120f0 0x80 # query a window around an addr
"""
import bisect
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ELF = os.path.join(ROOT, "game", "EBOOT.ELF")
OUT = os.path.join(ROOT, "port", "out", "abs_refs.json")
FUNCS = os.path.join(ROOT, "port", "out", "EBOOT.functions.json")

CODE_VA = 0x10000
CODE_SIZE = 0x108C508
# Accepted target range. The constants/strings live in .rodata, which is inside
# the FIRST PT_LOAD segment (0x10000..0x10A0508) after the code, not in the
# second one -- so the lower bound must sit below 0xf120f0 / 0xfb0000, the pools
# this project actually queries. 0xE00000 is comfortably inside .rodata and
# above the last code.
DATA_LO, DATA_HI = 0xE00000, 0x1200000
TOC = 0x11141C8                            # r2 at entry (see NOTES.md stage 1)

# opcodes that write their RT from a single source register (break an addis chain)
CLOBBERS = {7, 8, 10, 11, 12, 13, 14, 15, 20, 21, 23, 24, 25, 26, 27, 28, 29}

# d-form loads (op, RT, RA, D): the low half of the address lives in D.
DFORM_LOADS = {
    32, 33,          # lwz, lwzu
    34, 35,          # lbz, lbzu
    40, 41,          # lhz, lhzu
    42, 43,          # lha, lhau
    48, 49,          # lfs, lfsu
    50, 51,          # lfd, lfdu
    58, 59,          # ld, ldu
}


def sext16(v):
    return v - 0x10000 if v & 0x8000 else v


def build():
    data = open(ELF, "rb").read()
    n = CODE_SIZE // 4
    words = struct.unpack(f">{n}I", data[:n * 4])
    funcs = json.load(open(FUNCS))
    starts = sorted(int(f["start"], 16) for f in funcs)

    def owner(va):
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else 0

    refs = {}
    for i in range(n):
        w = words[i]
        if (w >> 26) != 15:
            continue
        ra = (w >> 16) & 0x1F
        rt = (w >> 21) & 0x1F
        if ra == 0:                      # absolute: addis rT, r0, HI
            hi = (w & 0xFFFF) << 16
        elif ra == 2:                    # TOC-relative: addis rT, r2, HA
            hi = TOC + ((w & 0xFFFF) << 16)
            hi = hi - 0x100000000 if hi & 0x80000000 else hi
        else:
            continue
        for j in range(i + 1, min(i + 11, n)):
            w2 = words[j]
            op2 = w2 >> 26
            if op2 in (14, 24) and ((w2 >> 16) & 0x1F) == rt and ((w2 >> 21) & 0x1F) == rt:
                lo = w2 & 0xFFFF
                val = (hi + (lo if lo < 0x8000 else lo - 0x10000)) & 0xFFFFFFFF if op2 == 14 \
                    else (hi | lo) & 0xFFFFFFFF
                if DATA_LO <= val < DATA_HI:
                    refs.setdefault(f"{val:08X}", []).append([CODE_VA + 4 * j, owner(CODE_VA + 4 * i)])
                break
            # d-form load with the low half in the displacement: lis rT,HI; lfs f,f_LO(rT)
            if op2 in DFORM_LOADS and ((w2 >> 16) & 0x1F) == rt and ((w2 >> 21) & 0x1F) == rt:
                val = (hi + sext16(w2 & 0xFFFF)) & 0xFFFFFFFF
                if DATA_LO <= val < DATA_HI:
                    refs.setdefault(f"{val:08X}", []).append([CODE_VA + 4 * j, owner(CODE_VA + 4 * i)])
                break
            if op2 in CLOBBERS and ((w2 >> 21) & 0x1F) == rt:
                break
    with open(OUT, "w") as fh:
        json.dump(refs, fh)
    print(f"{len(refs)} distinct absolute addresses -> {OUT}")
    return refs


def main():
    args = sys.argv[1:]
    near = None
    if "--near" in args:
        k = args.index("--near")
        near = (int(args[k + 1], 16), int(args[k + 2], 16))
        del args[k:k + 3]
    if "--rebuild" in args:
        args.remove("--rebuild")
        refs = build()
    else:
        refs = json.load(open(OUT)) if os.path.exists(OUT) else build()

    if near:
        lo, span = near
        keys = sorted(int(k, 16) for k in refs)
        sel = [k for k in keys if lo <= k < lo + span]
        print(f"{len(sel)} referenced addresses in [{lo:#x}, {lo+span:#x})")
        for k in sel:
            fn = sorted({f for _, f in refs[f'{k:08X}']})
            print(f"  {k:#010x}: {len(refs[f'{k:08X}'])} site(s) in {[hex(f) for f in fn]}")
        return

    for a in args:
        key = f"{int(a, 16):08X}"
        sites = refs.get(key)
        if not sites:
            print(f"{a}: no absolute reference")
            continue
        fns = {}
        for site, fn in sites:
            fns.setdefault(fn, []).append(site)
        print(f"{a}: {len(sites)} site(s), {len(fns)} function(s)")
        for fn, s in sorted(fns.items()):
            print(f"    func_{fn:08X}: {len(s)} site(s) {[hex(x) for x in s[:4]]}")


if __name__ == "__main__":
    main()

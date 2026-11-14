#!/usr/bin/env python3
"""Read the Apr 2012 dev prototype (TestDrive2DEV.exe, Hidden Palace) with capstone.

Unlike TestDrive2.exe its .text is NOT encrypted (no SecuROM), so the whole PC
engine is readable. Different build from retail: addresses do not carry over,
match by strings / constants / vtable slot. See docs/re/dev_build_physics.md.

  set TDU2_DEV_EXE=E:\\Documents\\TDU2+Dev+Build\\TDU2 Dev Build\\TestDrive2DEV.exe
  python tools/devbuild_disasm.py --str PacejkaConfig        # string VA + code xrefs + function start
  python tools/devbuild_disasm.py --dis 0x10122f0 120        # disassemble
  python tools/devbuild_disasm.py --vtable 0x155a630         # code pointers of a vtable
  python tools/devbuild_disasm.py --callers 0x10122f0        # e8/e9 call sites
  python tools/devbuild_disasm.py --float 0.28               # float constant + xrefs
"""
import argparse
import os
import re
import struct

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

EXE = os.environ.get("TDU2_DEV_EXE", r"E:\Documents\TDU2+Dev+Build\TDU2 Dev Build\TestDrive2DEV.exe")
B = 0x400000
pe = pefile.PE(EXE)
d = open(EXE, "rb").read()
md = Cs(CS_ARCH_X86, CS_MODE_32)
secs = [(s.VirtualAddress + B, max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
T0, TEND = 0x401000, 0x107B000


def off(va):
    for v, n, ro, rs in secs:
        if v <= va < v + n:
            return ro + va - v
    raise KeyError(hex(va))


def va_of(o):
    for v, n, ro, rs in secs:
        if ro <= o < ro + rs:
            return v + o - ro
    return None


text = d[off(T0):off(T0) + (TEND - T0)]


def func_start(va):
    o = off(va)
    for i in range(0x4000):
        a = o - i
        if d[a - 1] in (0xCC, 0xC3, 0xC2) and d[a] not in (0xCC,):
            return va - i
    return None


def refs_dword(v, code_only=True):
    p = re.escape(struct.pack("<I", v))
    if code_only:
        return [T0 + m.start() for m in re.finditer(p, text)]
    return [va_of(m.start()) for m in re.finditer(p, d)]


def callers(t):
    out = []
    for m in re.finditer(rb"[\xe8\xe9]", text):
        if m.start() + 5 > len(text):
            continue
        a = T0 + m.start()
        rel = struct.unpack("<i", text[m.start() + 1:m.start() + 5])[0]
        if (a + 5 + rel) & 0xFFFFFFFF == t:
            out.append(a)
    return out


def dis(va, n=60):
    o = off(va)
    for i in list(md.disasm(d[o:o + n * 8], va))[:n]:
        print("%08x  %s %s" % (i.address, i.mnemonic, i.op_str))


def vtable(va):
    out, a = [], va
    while True:
        v = struct.unpack("<I", d[off(a):off(a) + 4])[0]
        if not (T0 <= v < TEND):
            return out
        out.append(v)
        a += 4


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--str")
    ap.add_argument("--dis", nargs="+")
    ap.add_argument("--vtable")
    ap.add_argument("--callers")
    ap.add_argument("--float", type=float)
    a = ap.parse_args()
    if a.str:
        for m in re.finditer(re.escape(a.str.encode()), d):
            va = va_of(m.start())
            xs = refs_dword(va) if va else []
            print(hex(va or 0), [(hex(x), hex(func_start(x) or 0)) for x in xs[:8]])
    if a.dis:
        dis(int(a.dis[0], 16), int(a.dis[1]) if len(a.dis) > 1 else 60)
    if a.vtable:
        for i, v in enumerate(vtable(int(a.vtable, 16))):
            print(i, hex(i * 4), hex(v))
    if a.callers:
        print([(hex(c), hex(func_start(c) or 0)) for c in callers(int(a.callers, 16))])
    if a.float is not None:
        p = re.escape(struct.pack("<f", a.float))
        for m in re.finditer(p, d):
            va = va_of(m.start())
            if va and va >= 0x107C000:
                print(hex(va), [(hex(x), hex(func_start(x) or 0)) for x in refs_dword(va)[:8]])

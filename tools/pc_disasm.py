#!/usr/bin/env python3
"""Disassemble the readable half of TestDrive2.exe (x86-32) with capstone.

The encrypted band (0x401000..0x6D1000, SecuROM) is NOT decrypted or bypassed;
this tool only reads the bytes as they sit in the file, so addresses in that
band disassemble to garbage. Use docs/re/ps3 for that code.

  pip install capstone pefile
  python tools/pc_disasm.py 0x8720e0 120        # disassemble 120 instructions
  python tools/pc_disasm.py --vtable 0xf3ad6c 40 # dump a vtable (code / ENC / data tags)
  python tools/pc_disasm.py --xref-dword 0x54bc70 # file locations holding a dword
"""
import argparse
import os
import re
import struct

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

EXE = os.environ.get(
    "TDU2_EXE",
    r"C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2\TestDrive2.exe",
)
BASE = 0x400000
ENC_LO, ENC_HI = 0x401000, 0x6D1000

pe = pefile.PE(EXE)
data = open(EXE, "rb").read()
md = Cs(CS_ARCH_X86, CS_MODE_32)


def off(va):
    return pe.get_offset_from_rva(va - BASE)


def rd32(va):
    o = off(va)
    return struct.unpack("<I", data[o:o + 4])[0]


def tag(w):
    if ENC_LO <= w < ENC_HI:
        return "ENC"
    if ENC_HI <= w < 0xE41000:
        return "code"
    return ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("addr", nargs="?")
    ap.add_argument("count", nargs="?", type=int, default=60)
    ap.add_argument("--vtable", action="store_true")
    ap.add_argument("--xref-dword", action="store_true")
    a = ap.parse_args()
    va = int(a.addr, 16)
    if a.xref_dword:
        pat = struct.pack("<I", va)
        pos = 0
        while True:
            i = data.find(pat, pos)
            if i < 0:
                break
            pos = i + 1
            try:
                print("file 0x%x  va 0x%x" % (i, pe.get_rva_from_offset(i) + BASE))
            except Exception:
                print("file 0x%x" % i)
    elif a.vtable:
        for k in range(a.count):
            w = rd32(va + 4 * k)
            print("0x%x +0x%02x  0x%08x %s" % (va + 4 * k, 4 * k, w, tag(w)))
    else:
        code = data[off(va):off(va) + a.count * 16]
        for n, i in enumerate(md.disasm(code, va)):
            if n >= a.count:
                break
            print("%x: %s %s" % (i.address, i.mnemonic, i.op_str))


if __name__ == "__main__":
    main()

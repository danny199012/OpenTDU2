#!/usr/bin/env python3
"""Dump tables from the game database (db_data.cpr / db_<lang>.cpr).

Mirrors source/core/xmb.cpp + source/core/crypto/cpr.cpp (the XTEA key is the
documented DAT_00f763bc one). Examples:

  python tools_probe/db_probe.py tables
  python tools_probe/db_probe.py dump DBSurgeryCost
  python tools_probe/db_probe.py dump DBCarShop --rows 5
  python tools_probe/db_probe.py loc surgery
  python tools_probe/db_probe.py loc bandage
"""
import struct
import sys

KEY = (0x4FE23C4A, 0x80BAC211, 0x6917BD3A, 0xF0528EBD)
DB = r"C:\TDU2-Gamedata\TestDrive2\euro\bnk\database"


def decrypt_cpr(data):
    assert data[:4] != b"XMBF", "already plain"
    n = (len(data) - 8) // 8
    out = bytearray()
    for i in range(n):
        c0, c1 = struct.unpack_from("<II", data, i * 8)
        k0, k1 = struct.unpack_from("<II", data, (i + 1) * 8)
        s = 0xC6EF3720
        for _ in range(32):
            k1 = (k1 - ((((k0 >> 5) ^ (k0 << 4)) + k0) ^ (KEY[(s >> 11) & 3] + s))) & 0xFFFFFFFF
            s = (s + 0x61C88647) & 0xFFFFFFFF
            k0 = (k0 - ((((k1 >> 5) ^ (k1 << 4)) + k1) ^ (KEY[s & 3] + s))) & 0xFFFFFFFF
        out += struct.pack("<II", c0 ^ k0, c1 ^ k1)
    return bytes(out)


PRIM_SIZE = {1: 1, 2: 1, 6: 1, 3: 2, 7: 2, 5: 8, 9: 8, 11: 8,
             0: 4, 4: 4, 8: 4, 10: 4, 12: 4, 13: 4}


class Xmb:
    def __init__(self, path):
        data = open(path, "rb").read()
        if data[:4] != b"XMBF":
            data = decrypt_cpr(data)
        assert data[:4] == b"XMBF", data[:4]
        self.data = data
        self.str_off, self.type_off, self.data_off = struct.unpack_from("<III", data, 8)
        self.root_type = struct.unpack_from("<I", data, 0x18)[0] // 2
        self.types = []  # (name, is_composite, size, prim, fields)
        remaining = 0
        cur = 0
        for off in range(self.type_off, self.data_off - 7, 8):
            tw, no = struct.unpack_from("<II", data, off)
            if remaining > 0:
                tw, no = no, tw  # LE field records are swapped
            if tw == 0x41414141 or self.str_off + no >= self.type_off:
                break
            name = self.cstr(self.str_off + no)
            if remaining > 0:
                self.types[cur][4].append((name, (tw & 0x7FFFFFFF) // 2, bool(tw & 0x80000000)))
                # field records occupy a slot (type refs are record indices)
                self.types.append((name, False, 0, 0, None))
                remaining -= 1
                continue
            hi, lo = tw >> 16, tw & 0xFFFF
            num_fields = (lo >> 4) & 0x7FF
            if (lo & 0xF) == 0 and num_fields > 0:
                size = hi * 2 + (1 if lo & 0x8000 else 0)
                self.types.append([name, True, size, 0, []])
                remaining = num_fields
                cur = len(self.types) - 1
            else:
                prim = lo & 0x7FFF
                self.types.append((name, False, PRIM_SIZE[prim], prim, None))
                cur = len(self.types) - 1
        # resolve field offsets
        for t in self.types:
            if not t[1]:
                continue
            off = 0
            for i, (fname, ftype, farr) in enumerate(t[4]):
                t[4][i] = (fname, ftype, farr, off)
                off += 8 if farr else self.types[ftype][2]

    def cstr(self, off):
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("utf-8", "replace")

    def u(self, fmt, off):
        return struct.unpack_from(fmt, self.data, self.data_off + off)[0]

    def value(self, type_index, off, depth=0, indent=""):
        name, comp, size, prim, fields = self.types[type_index]
        if comp:
            out = []
            for fname, ftype, farr, foff in fields:
                foff_total = off + foff
                if farr:
                    count = self.u("<I", foff_total)
                    aoff = self.u("<I", foff_total + 4)
                    rows = []
                    for i in range(count):
                        rows.append(self.value(ftype, aoff + i * self.types[ftype][2], depth + 1))
                    out.append(f"{fname}[{count}]")
                    for r in rows:
                        out.append("  " + r.replace("\n", "\n  "))
                else:
                    out.append(f"{fname}={self.value(ftype, foff_total, depth + 1)}")
            return "\n".join(out)
        if prim == 12:  # string
            so = self.u("<I", off)
            return repr(self.cstr(self.data_off + so))
        if prim == 10:
            return f"{self.u('<f', off):g}"
        if prim == 11:
            return f"{self.u('<d', off):g}"
        if prim in (1,):
            return str(bool(self.u("<B", off)))
        if prim in (2,):
            return str(self.u("<b", off))
        if prim in (3,):
            return str(self.u("<h", off))
        if prim in (4,):
            return str(self.u("<i", off))
        if prim in (5,):
            return str(self.u("<q", off))
        if prim in (6,):
            return str(self.u("<B", off))
        if prim in (7,):
            return str(self.u("<H", off))
        if prim in (8,):
            return str(self.u("<I", off))
        if prim in (9,):
            return str(self.u("<Q", off))
        return f"<prim {prim}>"


def type_of_array(xmb, root_type_index, array_name):
    for fname, ftype, farr, foff in xmb.types[root_type_index][4]:
        if fname == array_name and farr:
            return ftype
    return None


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "tables"
    xmb = Xmb(DB + r"\db_data.cpr")
    root = xmb.root_type
    root_fields = xmb.types[root][4]
    if cmd == "tables":
        for fname, ftype, farr, foff in root_fields:
            cnt = xmb.u("<I", foff) if farr else 1
            print(f"{fname}{'[]' if farr else ''} rows={cnt} type='{xmb.types[ftype][0]}'")
    elif cmd == "dump":
        table = sys.argv[2]
        max_rows = int(sys.argv[sys.argv.index("--rows") + 1]) if "--rows" in sys.argv else 10**9
        for fname, ftype, farr, foff in root_fields:
            if fname != table or not farr:
                continue
            count = xmb.u("<I", foff)
            aoff = xmb.u("<I", foff + 4)
            row_size = xmb.types[ftype][2]
            print(f"{table}: {count} rows, row size {row_size}, type '{xmb.types[ftype][0]}'")
            for i in range(min(count, max_rows)):
                print(f"-- [{i}]")
                print(xmb.value(ftype, aoff + i * row_size))
    elif cmd == "loc":
        pattern = sys.argv[2].lower()
        loc = Xmb(DB + r"\db_us.cpr")
        # db_<lang>: DBData { Line db_data[] { Int32 HashCode; String Text; } }
        for fname, ftype, farr, foff in loc.types[loc.root_type][4]:
            if fname != "db_data" or not farr:
                continue
            count = loc.u("<I", foff)
            aoff = loc.u("<I", foff + 4)
            row_size = loc.types[ftype][2]
            hits = 0
            for i in range(count):
                off = aoff + i * row_size
                line_id = loc.u("<i", off)
                so = loc.u("<I", off + 4)
                text = loc.cstr(loc.data_off + so)
                if pattern in text.lower():
                    print(f"{line_id}: {text}")
                    hits += 1
            print(f"-- {hits} of {count} lines matched '{pattern}'")


if __name__ == "__main__":
    main()

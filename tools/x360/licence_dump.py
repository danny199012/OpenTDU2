#!/usr/bin/env python3
"""licence_dump.py - dump the TDU2 racing-school licences with their tests.

Reads the game data directly (no game run needed):
  - db_data.cpr   -> DBLicense rows (licence -> ordered challenge files, classes)
                     DBCarPhysicsData rows (car id -> name/category)
  - db_<lang>.cpr -> the localization strings (HashCode -> Text)
  - Euro/bnk/ig2/<name>.ig2 -> the test files (MDSC keys: timG/CtDw/VMod/dbna...,
                     ENTI entities: CHK2 gates, DEC2 decor, VEHI traffic)

CPR = XTEA keystream (key in TestDrive2.exe DAT_00f763bc); XMB = the Eden
"XMBF" container (docs/formats.md, source/core/xmb.cpp).

Usage:
  python tools/x360/licence_dump.py <gamedata> [lang]
e.g.
  python tools/x360/licence_dump.py C:/TDU2-Gamedata/TestDrive2 us
"""
import math
import struct
import sys
from pathlib import Path

KEY = (0x4FE23C4A, 0x80BAC211, 0x6917BD3A, 0xF0528EBD)


def xtea_decipher(block, key=KEY):
    v0, v1 = block
    total = 0
    delta = 0x61C88647
    total = 0xC6EF3720
    for _ in range(32):
        v1 = (v1 - ((((v0 >> 5) ^ (v0 << 4)) + v0) ^ (key[(total >> 11) & 3] + total))) & 0xFFFFFFFF
        total = (total + delta) & 0xFFFFFFFF
        v0 = (v0 - ((((v1 >> 5) ^ (v1 << 4)) + v1) ^ (key[total & 3] + total))) & 0xFFFFFFFF
    return v0, v1


def decrypt_cpr(data):
    if len(data) < 16:
        raise ValueError("cpr too small")
    num = (len(data) - 8) // 8
    out = bytearray(num * 8)
    for i in range(num):
        cur = struct.unpack_from("<2I", data, i * 8)
        nxt = struct.unpack_from("<2I", data, (i + 1) * 8)
        dec = xtea_decipher(nxt)
        out[i*8+0] = (cur[0] ^ dec[0]) & 0xFF
        out[i*8+1] = ((cur[0] ^ dec[0]) >> 8) & 0xFF
        out[i*8+2] = ((cur[0] ^ dec[0]) >> 16) & 0xFF
        out[i*8+3] = ((cur[0] ^ dec[0]) >> 24) & 0xFF
        out[i*8+4] = (cur[1] ^ dec[1]) & 0xFF
        out[i*8+5] = ((cur[1] ^ dec[1]) >> 8) & 0xFF
        out[i*8+6] = ((cur[1] ^ dec[1]) >> 16) & 0xFF
        out[i*8+7] = ((cur[1] ^ dec[1]) >> 24) & 0xFF
    return bytes(out)


class Xmb:
    """Minimal XMB reader: enough to walk one root array of flat rows."""

    def __init__(self, data):
        if data[:4] != b"XMBF":
            raise ValueError("not XMB")
        self.d = data
        self.big = data[8] == 0 and data[9] == 0
        self.str_off = self.u32(0x08)
        self.type_off = self.u32(0x0C)
        self.data_start = self.u32(0x10)
        self.root_type = self.u32(0x18) // 2
        self.types = []          # dicts: name, composite, primitive, size, fields
        self._parse_types()

    def u16(self, off):
        return struct.unpack_from(">H" if self.big else "<H", self.d, off)[0]

    def u32(self, off):
        return struct.unpack_from(">I" if self.big else "<I", self.d, off)[0]

    def _name(self, name_off):
        pos = self.str_off + name_off
        if pos >= self.type_off:
            return ""
        end = self.d.find(b"\0", pos, self.type_off)
        return self.d[pos:end].decode("latin1")

    def _parse_types(self):
        remaining = 0
        comp = 0  # index of the last real (non field-record) type
        for off in range(self.type_off, self.data_start - 7, 8):
            tw = self.u32(off)
            no = self.u32(off + 4)
            if remaining and not self.big:
                tw, no = no, tw
            if tw == 0x41414141 or self.str_off + no >= self.type_off:
                break
            if remaining:
                field = {"name": self._name(no), "type": (tw & 0x7FFFFFFF) // 2,
                         "array": bool(tw & 0x80000000), "offset": 0}
                self.types[comp]["fields"].append(field)
                # a field record occupies one index in the type table too
                self.types.append({"name": field["name"], "composite": False,
                                   "primitive": 0, "size": 0, "fields": []})
                remaining -= 1
                continue
            hi, lo = tw >> 16, tw & 0xFFFF
            num_fields = (lo >> 4) & 0x7FF
            t = {"name": self._name(no), "composite": False, "primitive": lo & 0x7FFF,
                 "size": 0, "fields": []}
            if (lo & 0xF) == 0 and num_fields:
                t["composite"] = True
                t["size"] = hi * 2 + (1 if lo & 0x8000 else 0)
                remaining = num_fields
            else:
                t["size"] = {1: 1, 2: 1, 6: 1, 3: 2, 7: 2, 5: 8, 9: 8, 11: 8}.get(t["primitive"], 4)
            self.types.append(t)
            comp = len(self.types) - 1

        # resolve field offsets
        for t in self.types:
            if not t["composite"]:
                continue
            off = 0
            for f in t["fields"]:
                f["offset"] = off
                off += 8 if f["array"] else self.types[f["type"]]["size"]

    # --- value access (offsets are data-section relative) ---
    def _abs(self, off):
        return self.data_start + off

    def read_u32(self, off):
        return self.u32(self._abs(off))

    def read_int(self, off, typ):
        t = self.types[typ]
        a = self._abs(off)
        fmt = {0: "b", 1: "?", 2: "b", 3: "h", 4: "i", 5: "q", 6: "B", 7: "H",
               8: "I", 9: "Q", 10: "f", 11: "d"}[t["primitive"]]
        if self.big:
            fmt = ">" + fmt
        else:
            fmt = "<" + fmt
        v = struct.unpack_from(fmt, self.d, a)[0]
        if t["primitive"] == 10:
            v = round(v, 4)
        return v

    def read_str(self, off):
        s = self.read_u32(off)
        a = self._abs(s)
        end = self.d.find(b"\0", a)
        return self.d[a:end].decode("latin1")

    def row(self, off, typ):
        """One object's scalar fields as {name: value} (arrays kept raw)."""
        t = self.types[typ]
        out = {}
        for f in t["fields"]:
            fo = off + f["offset"]
            if f["array"]:
                count = self.read_u32(fo)
                arr_off = self.read_u32(fo + 4)
                elem = self.types[f["type"]]
                if elem["composite"]:
                    out[f["name"]] = [self.row(arr_off + i * elem["size"], f["type"]) for i in range(count)]
                else:
                    vals = []
                    for i in range(count):
                        vals.append(self.read_str(arr_off + i * elem["size"])
                                    if elem["primitive"] == 12
                                    else self.read_int(arr_off + i * elem["size"], f["type"]))
                    out[f["name"]] = vals
            else:
                ft = self.types[f["type"]]
                if ft["composite"]:
                    out[f["name"]] = self.row(fo, f["type"])
                elif ft["primitive"] == 12:
                    out[f["name"]] = self.read_str(fo)
                else:
                    out[f["name"]] = self.read_int(fo, f["type"])
        return out

    def root_array(self, field_name):
        t = self.types[self.root_type]
        f = next(f for f in t["fields"] if f["name"] == field_name and f["array"])
        count = self.read_u32(f["offset"])
        arr = self.read_u32(f["offset"] + 4)
        return [self.row(arr + i * self.types[f["type"]]["size"], f["type"]) for i in range(count)]


def load_loc(gamedata, lang="us"):
    path = Path(gamedata) / "euro" / "bnk" / "Database" / f"db_{lang}.cpr"
    x = Xmb(decrypt_cpr(path.read_bytes()))
    rows = x.root_array("db_data")
    return {r["HashCode"]: r["Text"] for r in rows}, x


def load_db(gamedata):
    path = Path(gamedata) / "euro" / "bnk" / "Database" / "db_data.cpr"
    return Xmb(decrypt_cpr(path.read_bytes()))


def parse_ig2(path):
    """MDSC keys + ENTI entity census of one .ig2 (big endian)."""
    d = path.read_bytes()
    if d[:4] != b"IGE2":
        raise ValueError("not IGE2")
    vals = {}
    gates = []
    decors = []
    vehi = 0
    ent_sizes = {}
    off = struct.unpack_from(">I", d, 4)[0]
    f32 = lambda p: struct.unpack_from(">f", d, p)[0]
    while off + 8 <= len(d):
        fourcc = d[off:off + 4]
        size = struct.unpack_from(">I", d, off + 4)[0]
        if size < 8 or off + size > len(d):
            break
        body = off + 8
        bsize = size - 8
        if fourcc == b"MDSC" and bsize >= 16:
            p = 16
            while p + 8 <= bsize:
                k = d[body + p:body + p + 4]
                if not all(0x20 <= c < 0x7F for c in k):
                    p += 4
                    continue
                vsize = (d[body + p + 6] << 8) | d[body + p + 7]
                if p + 8 + vsize > bsize:
                    break
                raw = d[body + p + 8:body + p + 8 + vsize]
                if vsize == 4:
                    vals[k.decode()] = struct.unpack(">I", raw)[0]
                elif vsize == 8:
                    vals[k.decode()] = (struct.unpack(">I", raw[:4])[0] << 32) | struct.unpack(">I", raw[4:])[0]
                p += 8 + vsize
        elif fourcc == b"ENTI" and bsize >= 4:
            count = struct.unpack_from(">I", d, body)[0]
            p = 4
            for _ in range(count):
                if p + 8 > bsize:
                    break
                tag = d[body + p:body + p + 4]
                esize = struct.unpack_from(">I", d, body + p + 4)[0]
                if esize < 8 or p + esize > bsize:
                    break
                ent_sizes[tag.decode()] = ent_sizes.get(tag.decode(), 0) + 1
                if tag == b"CHK2" and esize in (0x68, 0x7C):
                    m = body + p + 8 + (4 if esize == 0x68 else 24)
                    tail = m + 48 + 16
                    kind, side = struct.unpack_from(">iI", d, tail)[0], struct.unpack_from(">I", d, tail + 4)[0]
                    width = f32(tail + 8)
                    gates.append({"kind": kind, "side": side, "width": round(width, 1),
                                  "pos": (round(f32(m + 12), 1), round(f32(m + 28), 1), round(f32(m + 44), 1))})
                elif tag == b"VEHI":
                    vehi += 1
                elif tag == b"DEC2" and esize in (0x54, 0x68):
                    typ = struct.unpack_from(">I", d, body + p + esize - 8)[0]
                    decors.append(typ)
                p += esize
        off += size
    return vals, gates, decors, vehi, ent_sizes


def fmt_ms(ms):
    if not ms:
        return "-"
    s = ms / 1000.0
    return f"{int(s // 60)}:{s % 60:04.1f}"


def main():
    gamedata = sys.argv[1] if len(sys.argv) > 1 else "C:/TDU2-Gamedata/TestDrive2"
    lang = sys.argv[2] if len(sys.argv) > 2 else "us"
    loc, _ = load_loc(gamedata, lang)
    db = load_db(gamedata)

    # car id -> name (DBCarPhysicsData: manufacturer + model + version)
    clean = lambda t: (t or "").replace("##", "").rstrip()
    cars = {}
    for row in db.root_array("DBCarPhysicsData"):
        ident = row.get("_id_car")
        version = clean(row.get("_version_name"))
        model = clean(row.get("_model_name"))
        brand = clean(row.get("_manufacturer_name"))
        name = " ".join(p for p in (brand, model, version) if p)
        cars[ident] = name or f"car {ident}"

    licences = db.root_array("DBLicense")
    ig2dir = Path(gamedata) / "euro" / "bnk" / "ig2"

    print("=" * 100)
    print("DBLicences (isachampionship = 0) and their tests")
    print("=" * 100)
    for row in licences:
        if row.get("_isachampionship"):
            continue
        island = "Ibiza" if row.get("_island") == 0 else "Hawai"
        cats = [row.get(f"_vehiclecategory_{i}") for i in (1, 2)]
        cats = "/".join(str(c) for c in cats if c not in (None, 0, ""))
        order = row.get("_order")
        champ = row.get("_championshipname_hc") or ""
        print(f"\n--- {champ} (island {island}, order {order}, classes {cats}) ---")
        for i in range(1, 9):
            fname = row.get(f"_challengefile_{i}")
            if not fname or fname == "##":
                continue
            stem = Path(str(fname)).stem
            f = ig2dir / (stem.lower() + ".ig2")
            if not f.exists():
                print(f"  {i}. {stem}: file missing")
                continue
            vals, gates, decors, vehi, ents = parse_ig2(f)
            name = loc.get(vals.get("dbna"), f"?{vals.get('dbna')}")
            kinds = {}
            for g in gates:
                kinds[g["kind"]] = kinds.get(g["kind"], 0) + 1
            slalom = sum(1 for g in gates if g["kind"] == 1)
            road = sum(1 for g in gates if g.get("bRoad")) if False else 0
            print(f"  {i}. {stem.upper():9s} '{name}'  target {fmt_ms(vals.get('timG'))}  "
                  f"limit {fmt_ms(vals.get('CtDw'))}  car {vals.get('VMod')} "
                  f"({cars.get(vals.get('VMod'), '?')})")
            print(f"      gates {len(gates)} kinds {kinds}  decor {len(decors)}  "
                  f"traffic {vehi}  entities {ents}")
            print(f"      desc: {loc.get(vals.get('dbd1'), '')[:90]}")
            print(f"      other keys: " + " ".join(
                f"{k}={v}" for k, v in sorted(vals.items())
                if k not in ("timG", "CtDw", "VMod", "dbna", "dbd1")))

    print()
    print("=" * 100)
    print("Car table entries used (VMod space)")
    print("=" * 100)


if __name__ == "__main__":
    main()

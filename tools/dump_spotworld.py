#!/usr/bin/env python3
"""Dump <game>/euro/bnk/scenarics/spotworld.bin (the challenge table, docs/re/map_spots_spec.md sections 2-3, 12).

Layout [DATA, decoded from the dev build's writer 0x895890 + the file]:
  "SPTW" v3, then 33 "CONT" blocks; each block holds records:
  LE header: name[8] (NUL padded), x y z (float), u32 id, u32 flags, float, then an "IGE2" BE blob whose "MDSC" chunk is a
  property list: key[4] 03 sub 00 size value(size bytes, big endian).
Usage: python tools/dump_spotworld.py [path] [--csv]
"""
import re, struct, sys, collections
path = next((a for a in sys.argv[1:] if not a.startswith("--")), r"C:\TDU2-Gamedata\TestDrive2\euro\bnk\scenarics\spotworld.bin")
d = open(path, "rb").read()

def props(o):
    m = o + 0x1c
    size, cnt = struct.unpack(">II", d[m + 4:m + 12])
    p = m + 24
    ent = {}
    for _ in range(cnt):
        if p + 8 > len(d): break
        k = d[p:p + 4].decode("latin1"); sub = d[p + 5]
        sz = struct.unpack(">H", d[p + 6:p + 8])[0]
        ent[k] = (sub, d[p + 8:p + 8 + sz]); p += 8 + sz
    return ent

def i(e):
    if e is None: return None
    sub, v = e
    if len(v) == 4: return struct.unpack(">i", v)[0]
    if len(v) == 8: return struct.unpack(">Q", v)[0]
    return v.hex()

def s(e):
    return e[1].split(b"\0")[0].decode("latin1") if e else ""

recs = []
for mt in re.finditer(rb"IGE2", d):
    o = mt.start()
    if d[o + 0x1c:o + 0x20] != b"MDSC": continue
    h = o - 0x28  # LE header precedes the blob: name[8] xyz id flags float = 8+12+4+4+4 = 32 bytes, then "2EGI"+ver
    pr = props(o)
    # name / position: search back for the 8-char name that precedes the floats
    q = o - 0x20
    name = d[q:q + 8].split(b"\0")[0].decode("latin1")
    x, y, z = struct.unpack("<fff", d[q + 8:q + 20])
    rid, flags = struct.unpack("<II", d[q + 20:q + 28])
    recs.append(dict(off=o, name=name, x=x, y=y, z=z, id=rid, flags=flags, p=pr))

if "--csv" in sys.argv:
    print("name,x,y,z,id,island,level,gmth,gmpm,mission,desc,nblp,cops,fee,reward")
    for r in recs:
        p = r["p"]
        print(",".join(str(v) for v in (r["name"], round(r["x"], 1), round(r["y"], 1), round(r["z"], 1), r["id"], i(p.get("isla")), i(p.get("leme")),
              i(p.get("gmth")), i(p.get("gmpm")), s(p.get("mnme")), s(p.get("DesC")), i(p.get("nblp")), i(p.get("cops")), i(p.get("fee1")), i(p.get("rewm")))))
else:
    print(len(recs), "records")
    c = collections.Counter((i(r["p"].get("isla")), i(r["p"].get("gmth"))) for r in recs)
    for k, v in sorted(c.items(), key=lambda kv: (str(kv[0][0]), str(kv[0][1]))): print("island", k[0], "gmth", k[1], "x", v)
    for r in recs[:6]:
        print(r["name"], round(r["x"]), round(r["y"]), round(r["z"]), r["id"], r["flags"], s(r["p"].get("mnme")), s(r["p"].get("DesC")))

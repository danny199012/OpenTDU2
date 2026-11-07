"""Dump the instance lists of a TDU2 `.pmi` and check their record arithmetic.

One `.pmi` per island sector, five per sector bank (group 5). This is the file
that says *what* is placed in the world; the models themselves live in
`commonworld.bnk` (library.3dd/.3dg) and `worldactors.bnk`.

    otdu2_bnk_tool extract <sector.bnk> <dir>     # writes Sector-X-Y-SX-SY-*.pmi
    python tools_probe/pmi_probe.py <dir>/Sector-3-3-0-0-S.pmi [-v]

subtype  list  type      content                          drawn by
   10      V   1         vegetation: palms, olives...      EdenWorld
   13      O   1         objects: houses, walls, lamps     EdenWorld
   50      S   3         guard rails SF_GLISS/SF_GLISO     EdenWorld (24th session)
   14      A   2/4       road signs, sign text             EdenWorld
   24      F   0         empty in every sector checked     -

A type-3 record carries `extraSize / 64` bone matrices (D3D row major, model
space). That must equal the palette size of the model's primitive - the high byte
of its PRIM type and its BONE section in the .3dg - and the renderer applies them
*before* the instance matrix: v * Bone_i * World (docs/re/pmi_spec.md 1.5).
"""
import struct
import sys

K_POSITION = 0.0156252384185791
K_POSITION_OFFSET = 128.0
K_SCALE = 6.103608757257462e-05
K_ANGLE = 9.587526437826455e-05
K_BOX = 0.0625009536743164
K_BOX_OFFSET = 1024.0
LISTS = {"V": "V vegetation (type 1)", "O": "O objects (type 1)", "S": "S skinned static (type 3)",
         "A": "A actors (type 2/4)", "F": "F (empty)"}


def hash_name(value):
    out = ""
    for i in range(8):
        c = (value >> (i * 8)) & 0xFF
        if c == 0:
            break
        out += chr(c) if 32 <= c < 127 else "?"
    return out


def parse(data, verbose=False):
    """Returns (header, [mob]); raises ValueError when the file does not add up."""
    if len(data) < 0x1D0 or data[0x0C:0x10] != b".PMI" or data[0x10:0x14] != b"MOBA":
        raise ValueError("not a .PMI 2.1 file with a MOBA section")
    num_mob, height_min, height_max = struct.unpack_from("<Iff", data, 0x20)
    header = {"num_mob": num_mob, "height_min": height_min, "height_max": height_max,
              "cells": sum(data[0x30:0x30 + 409])}
    offset = 0x10 + struct.unpack_from("<I", data, 0x1C)[0]  # the MOBA size keeps its header
    mobs = []
    for _ in range(num_mob):
        if data[offset:offset + 4] != b"MOB.":
            raise ValueError(f"MOB. magic missing at 0x{offset:x}")
        size, = struct.unpack_from("<I", data, offset + 8)
        hashcode, = struct.unpack_from("<Q", data, offset + 0x10)
        count, extra, unknown = struct.unpack_from("<HHH", data, offset + 0x18)
        ptype = data[offset + 0x1E]
        stride = 0x20 + ((extra + 15) & ~15)
        if size < 0x20 + count * stride or offset + size > len(data):
            raise ValueError(f"MOB. size {size} does not hold {count} x {stride}")
        mob = {"type": ptype, "count": count, "extra": extra, "unknown": unknown,
               "hash": hashcode, "records": []}
        for i in range(count):
            record = offset + 0x20 + i * stride
            w = struct.unpack_from("<16H", data, record)
            height = height_min + w[1] * (height_max - height_min) / 65535.0
            mob["records"].append({
                "position": (w[0] * K_POSITION - K_POSITION_OFFSET, height,
                             w[2] * K_POSITION - K_POSITION_OFFSET),
                "scale": (w[3] * K_SCALE, w[4] * K_SCALE, w[5] * K_SCALE),
                "rotation": (w[6] * K_ANGLE, w[7] * K_ANGLE, w[8] * K_ANGLE),
                "cell": w[9] & 0x1FF, "level": w[9] >> 9,
                "box_min": tuple(w[10 + k] * K_BOX - K_BOX_OFFSET for k in range(3)),
                "box_max": tuple(w[13 + k] * K_BOX - K_BOX_OFFSET for k in range(3)),
                "bones": extra // 64 if ptype == 3 else 0,
            })
        mobs.append(mob)
        offset += size
    if offset != len(data) and verbose:
        print(f"  note: parsed to 0x{offset:x}, the file is 0x{len(data):x}")
    return header, mobs


def report(path, verbose):
    data = open(path, "rb").read()
    header, mobs = parse(data, verbose)
    stem = path.replace("\\", "/").rsplit("-", 1)[-1].split(".")[0].upper()
    label = LISTS.get(stem, "?")
    print(f"{path}  [{label}]  {len(data)} bytes, {header['num_mob']} model(s), "
          f"height {header['height_min']:.1f}..{header['height_max']:.1f}")
    total = 0
    for mob in mobs:
        total += mob["count"]
        bones = f", {mob['extra'] // 64} bones" if mob["type"] == 3 else ""
        print(f"  type {mob['type']} x{mob['count']:<5} '{hash_name(mob['hash'])}' "
              f"({mob['hash']:016x}) extra {mob['extra']}{bones}")
        if verbose:
            for i, record in enumerate(mob["records"][:3]):
                p = record["position"]
                print(f"    [{i}] pos ({p[0]:8.2f} {p[1]:8.2f} {p[2]:8.2f}) "
                      f"scale {record['scale'][0]:.3f} "
                      f"rot {[round(r * 57.29578, 1) for r in record['rotation']]} "
                      f"cell {record['cell']} stored {tuple(round(v, 2) for v in record['box_min'])}")
    print(f"  {total} instance(s)")


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    if not args:
        print(__doc__)
        sys.exit(1)
    for arg in args:
        report(arg, "-v" in sys.argv)

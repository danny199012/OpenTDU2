#!/usr/bin/env python3
"""Builds the second tuner-mod shop's interior: itnr_b.bnk, the retail USED-CAR
DEALER room (icdc_wld.bnk, "U.S. Cars (north)" / World classic) re-skinned as a
dark neon workshop. Reads the user's own game data, writes only into the mods
overlay (inside the enabled tuner-garage package):

  python tools/mod/make_revyard_interior.py --game C:\\TDU2-Gamedata\\TestDrive2 \\
         --userdata C:\\temp\\otdu2-win-userdata
  # -> <userdata>/mods/packages/tuner-garage/Euro/Bnk/interior/itnr_b.bnk

InteriorWalk resolves ETNR_<L> to itnr_<l>.bnk (ETNR_A keeps the tuning-shop
based itnr_a), retypes the room's hostess desk (selectable type 1) to the
tuner seller (23), forces the island night weather, and lays the props of
interior/props_itnr_b.txt (make_revyard_props.py) over the room.

What is patched (decoded from the bank, docs/state session 45):
  marble_02_d     256x256   the floor        -> dark tiles with glowing seams
  bricks          512x512   the brick wall   -> the original, darkened and cooled
  stone_wall_d / concrete_d / y_concrete01_d / planks_02_d / ceiling_2_d /
  carpet_02_d / metal_sheet_d   -> the originals, darkened and cooled
  newlogosquare   128x128   the "US Cars" sign -> the neon shop sign
  glow_neons_d / halo02_d   the lamp fixtures -> neon colours
  lm_icdc_wld (the baked lightmap) and cubecar (the glass backdrop) dimmed,
  and every material's Ambient / Diffuse scaled down.
The swapped textures are uncompressed sRGB A8R8G8B8 .2db (format 31), the
pattern make_tuner_interior.py ships.
"""
import argparse
import math
import os
import struct
import sys

SHOP_NAME = ("REV", "YARD")
NEON_A = (255, 60, 200)   # magenta
NEON_B = (60, 220, 255)   # cyan

TOOLS = os.path.dirname(os.path.abspath(__file__))           # tools/mod
REPO = os.path.dirname(os.path.dirname(TOOLS))
sys.path.insert(0, os.path.join(REPO, "tools", "x360"))
sys.path.insert(0, TOOLS)

import eden_bank          # noqa: E402
import build_bank         # noqa: E402
import make_tuner_interior as mt   # noqa: E402  (shared: png_to_2db, darken_materials, dxt_darken, pc_dxt_decode)

GAME_BANK = os.path.join("euro", "bnk", "interior", "icdc_wld.bnk")
DARKEN = 0.68

# Originals that keep their detail: bank entry -> (multiplier r,g,b)
TINTS = {
    "bricks.2db": (0.34, 0.30, 0.40),
    "stone_wall_d.2db": (0.45, 0.45, 0.55),
    "concrete_d.2db": (0.45, 0.45, 0.55),
    "y_concrete01_d.2db": (0.30, 0.31, 0.40),
    "planks_02_d.2db": (0.28, 0.28, 0.36),
    "ceiling_2_d.2db": (0.26, 0.26, 0.34),
    "carpet_02_d.2db": (0.55, 0.50, 0.65),
    "metal_sheet_d.2db": (0.55, 0.55, 0.65),
    "windows_d.2db": (0.70, 0.75, 0.95),
    "marbre.2db": (0.28, 0.28, 0.38),
    "floor01_d.2db": (0.28, 0.28, 0.38),
}
LIGHTMAP_SCALES = {"lm_icdc_wld.2db": (0.70, 0.72, 0.88)}
CUBEMAP = "cubecar.2db"
CUBEMAP_SCALES = (0.30, 0.34, 0.45)


# ---------------------------------------------------------------------------
# generated art (PIL)
# ---------------------------------------------------------------------------

def _font(size):
    from PIL import ImageFont
    for name in ("arialbd.ttf", "arial.ttf", "segoeui.ttf"):
        try:
            return ImageFont.truetype("C:/Windows/Fonts/" + name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def make_floor(size):
    """Dark concrete tiles with a faint glowing cyan seam and a magenta cross
    line (the retail floor: beige tiles in a grey cross grid)."""
    import random
    from PIL import Image, ImageDraw, ImageFilter
    rnd = random.Random(11)
    img = Image.new("RGB", (size, size), (26, 27, 33))
    px = img.load()
    for y in range(size):
        for x in range(size):
            v = rnd.randint(-5, 5)
            px[x, y] = (26 + v, 27 + v, 33 + v)
    glow = Image.new("RGB", (size, size), (0, 0, 0))
    d = ImageDraw.Draw(glow)
    half = size // 2
    for pos, col, w in ((0, NEON_B, 4), (half, NEON_A, 3)):
        d.line([(pos, 0), (pos, size)], fill=col, width=w)
        d.line([(0, pos), (size, pos)], fill=col, width=w)
    glow = glow.filter(ImageFilter.GaussianBlur(3))
    core = Image.new("RGB", (size, size), (0, 0, 0))
    d = ImageDraw.Draw(core)
    for pos in (0, half):
        d.line([(pos, 0), (pos, size)], fill=(230, 235, 255), width=1)
        d.line([(0, pos), (size, pos)], fill=(230, 235, 255), width=1)
    img = Image.blend(img, glow, 0.55)
    from PIL import ImageChops
    img = ImageChops.add(img, core.point(lambda v: v // 3))
    return img


def make_sign(size):
    """The neon shop sign: SHOP_NAME on black, cyan frame."""
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (size, size), (6, 6, 10, 255))
    big, small = SHOP_NAME
    f1 = mt._fit_font([big], 100, size - 30)
    f2 = mt._fit_font([small], 90, size - 30)
    d = ImageDraw.Draw(img)
    mt._glow_text(img, (20, 14), big, f1, NEON_A)
    mt._glow_text(img, (16, size // 2 + 4), small, f2, NEON_B)
    mt._glow_line(img, [(6, 6), (size - 7, 6), (size - 7, size - 7), (6, size - 7), (6, 6)], NEON_B, 3)
    return img.convert("RGB")


def make_fill(colour):
    from PIL import Image
    return Image.new("RGB", (64, 64), colour)


# ---------------------------------------------------------------------------

def tint(blob, mult):
    from PIL import ImageChops, Image
    im, w, h = mt.pc_dxt_decode(blob)
    return ImageChops.multiply(im.convert("RGB"), Image.new("RGB", im.size, tuple(int(255 * m) for m in mult)))


def build(game, userdata):
    src = os.path.join(game, GAME_BANK)
    if not os.path.isfile(src):
        raise SystemExit("no %s (check --game)" % src)
    bank = eden_bank.parse_file(src, eden_bank.PC)
    entries = {e["filename"].lower(): e for e in bank["entries"]}

    def payload(entry):
        return bank["data"][entry["offset"]:entry["offset"] + entry["size"]]

    def size_of(key):
        return struct.unpack_from("<HH", payload(entries[key]), 0x28)[0]

    swaps = {}

    def swap(key, img):
        if key not in entries:
            print("  ! '%s' not in the bank (skipped)" % key)
            return
        swaps[key] = mt.png_to_2db(bytes(payload(entries[key])), img.convert("RGB"))
        print("  %s replaced" % key)

    swap("marble_02_d.2db", make_floor(size_of("marble_02_d.2db")))
    swap("newlogosquare.2db", make_sign(size_of("newlogosquare.2db")))
    swap("glow_neons_d.2db", make_fill(NEON_A))
    swap("halo02_d.2db", make_fill(NEON_B))
    for key, mult in TINTS.items():
        if key in entries:
            swap(key, tint(bytes(payload(entries[key])), mult))
        else:
            print("  ! '%s' not in the bank (skipped)" % key)

    lms = {}
    for key, scales in LIGHTMAP_SCALES.items():
        if key in entries:
            lms[key] = mt.dxt_darken(bytes(payload(entries[key])), scales)
            print("  %s darkened" % key)
    if CUBEMAP in entries:
        lms[CUBEMAP] = mt.dxt_darken(bytes(payload(entries[CUBEMAP])), CUBEMAP_SCALES)
        print("  %s darkened" % CUBEMAP)

    out_entries = []
    for entry in bank["entries"]:
        name = entry["filename"].lower()
        data = swaps.get(name)
        if data is None:
            data = lms.get(name)
        if data is None:
            data = bytes(payload(entry))
        if name.endswith(".2dm"):
            count, _ = mt.darken_materials(bytearray(data), DARKEN)  # counted only; patched below
            buf = bytearray(data)
            mt.darken_materials(buf, DARKEN)
            data = bytes(buf)
            print("  darkened %s: %d materials (x%.2f)" % (entry["filename"], count, DARKEN))
        out_entries.append({"path": entry["path"], "data": data,
                            "subtype": entry["subtype"], "group": entry["group"]})

    directory_order = [eden_bank.decode_bank_path(encoded)
                       for encoded in build_bank.eden_bank_directory_order(bank)]
    new_bank = build_bank.build_bank(out_entries, creation_date=bank["header"]["creationDate"],
                                     bank_type=bank["header"]["type"], directory_order=directory_order)
    re_parsed = eden_bank.parse(bytes(new_bank), eden_bank.PC)
    assert len(re_parsed["entries"]) == len(bank["entries"]), "entry count changed"
    for key in swaps:
        entry = next(e for e in re_parsed["entries"] if e["filename"].lower() == key)
        blob = re_parsed["data"][entry["offset"]:entry["offset"] + entry["size"]]
        assert struct.unpack_from("<I", blob, 0x3C)[0] == 31, "%s: not the uncompressed format" % key
    print("  self-check: %d entries, %d swapped textures" % (len(re_parsed["entries"]), len(swaps)))

    out_dir = os.path.join(userdata, "mods", "packages", "tuner-garage", "Euro", "Bnk", "interior")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "itnr_b.bnk")
    with open(out_path, "wb") as f:
        f.write(new_bank)
    print("wrote %s (%.1f MB, was %.1f MB)" % (out_path, len(new_bank) / 1e6, os.path.getsize(src) / 1e6))


def main():
    ap = argparse.ArgumentParser(description="Build the second tuner shop's interior (itnr_b.bnk).")
    ap.add_argument("--game", default=r"C:\TDU2-Gamedata\TestDrive2", help="the game root (read only)")
    ap.add_argument("--userdata", default=r"C:\temp\otdu2-win-userdata")
    args = ap.parse_args()
    build(args.game, args.userdata)


if __name__ == "__main__":
    main()

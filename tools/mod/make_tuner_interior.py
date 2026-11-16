#!/usr/bin/env python3
"""Builds the tuner-garage mod's own interior bank: the retail tuning-shop
interior (itun_a.bnk), re-skinned dark with neon fixtures and graffiti, under
a NEW name (itnr_a.bnk) so the shared vanilla bank - which every retail ETUN_*
shop walks into - stays untouched.

Reads the user's own game data (never the repo, never the game folder for
writing) and writes only into the mods overlay, inside the enabled
tuner-garage package:

  python -I tools/mod/make_tuner_interior.py --game C:\\TDU2-Gamedata\\TestDrive2 \\
         --userdata C:\\temp\\otdu2-win-userdata
  # -> <userdata>/mods/packages/tuner-garage/Euro/Bnk/interior/itnr_a.bnk

InteriorWalk's ETNR rule prefers itnr_a.bnk when it exists (fallback itun_a).

What is patched (the interior's decode lives in docs/state session 42):
  wall_d      256x256  the walls - dark workshop wall with spray tags
  poster2_d   1024x512 the big wall poster sheet - the neon signage mural
                       with the garage's name
  flare1_d / flare2_d / flare3_d / glow_d   the room's light fixtures -
                       recoloured to the neon palette
  tole_d      256x128  the roof deck - darkened
  marble_floor_d 512x512 the floor - dark tiles
and every material's Ambient / Diffuse colour is scaled down (the room's
brightness otherwise comes from the renderer's hard-coded daylight scene
constants). InteriorWalk also forces the island's night weather preset for
ETNR rooms, which flips the baked NIGHTLMAP to its night half and raises the
neon strength.

The swapped textures are uncompressed sRGB A8R8G8B8 .2db (format 31, full mip
chain) - the pattern make_eden_island.py ships; the engine loads it (no DXT
encoder exists in the repo, and none is needed).

Our own art is generated with PIL into the package folder
(resources/mods/packages/tuner-garage/interior/) and committed; --art
regenerates it. The name on the sign and the neon palette are the two
constants below.
"""
import argparse
import math
import os
import struct
import sys

# The re-theme: the sign's name and the neon palette (magenta / cyan).
SIGN_NAME = "TUNER GARAGE"
NEON_A = (255, 60, 200)   # magenta
NEON_B = (60, 220, 255)   # cyan

TOOLS = os.path.dirname(os.path.abspath(__file__))           # tools/mod
REPO = os.path.dirname(os.path.dirname(TOOLS))               # the repo root
sys.path.insert(0, os.path.join(REPO, "tools", "x360"))

import eden_bank          # noqa: E402  (the bank reader)
import build_bank         # noqa: E402  (the bank writer)
import eden_2dm           # noqa: E402  (the material reader)

GAME_BANK = os.path.join("Euro", "Bnk", "interior", "itun_a.bnk")
ART_DIR = os.path.join(REPO, "resources", "mods", "packages", "tuner-garage", "interior", "art")
# The room's material library and the wall-library entries (their .2dm payloads).
ROOM_2DM = "itun_a_sc_65bb36e110a8780b.2dm"
DARKEN = 0.55             # the Ambient / Diffuse scale of every room material

# The texture swaps: bank entry file name -> our art PNG in the art folder.
SWAPS = {
    "wall_d.2db": "wall_graffiti.png",
    "poster2_d.2db": "poster_neon.png",
    "flare1_d.2db": "flare_neon_a.png",
    "flare2_d.2db": "flare_neon_b.png",
    "flare3_d.2db": "flare_neon_a.png",
    "glow_d.2db": "glow_neon.png",
    "tole_d.2db": "roof_dark.png",
    "marble_floor_d.2db": "floor_dark.png",
    "trame_d.2db": "grid_neon.png",
    "nuancier_c.2db": "nuancier_dark.png",
    "tv1_d.2db": "screen_neon.png",
    "steer_d.2db": "frame_neon_a.png",
    "dashboard_d.2db": "frame_neon_b.png",
    "board_d.2db": "frame_neon_c.png",
    "jante_d.2db": "frame_neon_d.png",
    "jante2_d.2db": "frame_neon_a.png",
    "disc2_d.2db": "frame_neon_e.png",
    "pneu_d.2db": "frame_neon_f.png",
    "coversmag_d.2db": "frame_neon_g.png",
}


# ---------------------------------------------------------------------------
# the art (PIL; the PNGs land in the package and are committed)
# ---------------------------------------------------------------------------

def _font(size):
    from PIL import ImageFont
    for name in ("arialbd.ttf", "arial.ttf", "segoeui.ttf"):
        try:
            return ImageFont.truetype("C:/Windows/Fonts/" + name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def _glow_text(img, xy, text, font, colour):
    """Neon lettering: blurred halos, then the core, then a white-hot centre."""
    from PIL import Image, ImageDraw, ImageFilter
    for radius, alpha in ((9, 110), (5, 160), (2, 220)):
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).text(xy, text, font=font, fill=colour + (alpha,))
        layer = layer.filter(ImageFilter.GaussianBlur(radius))
        img.alpha_composite(layer)
    ImageDraw.Draw(img).text(xy, text, font=font, fill=tuple(min(255, c + 60) for c in colour) + (255,))
    ImageDraw.Draw(img).text(xy, text, font=font, fill=(255, 255, 255, 235))


def _glow_line(img, points, colour, width):
    """A neon tube: soft coloured halo strokes under a bright core."""
    from PIL import Image, ImageDraw, ImageFilter
    for radius, alpha, w in ((8, 90, width + 10), (3, 170, width + 3)):
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).line(points, fill=colour + (alpha,), width=w, joint="curve")
        layer = layer.filter(ImageFilter.GaussianBlur(radius))
        img.alpha_composite(layer)
    ImageDraw.Draw(img).line(points, fill=tuple(min(255, c + 70) for c in colour) + (255,),
                             width=width, joint="curve")


def make_wall(path):
    """256x256, tileable: dark workshop wall, faint scuffs, two small tags
    kept clear of the tile edges so the repeat stays clean."""
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (256, 256), (26, 28, 34))
    px = img.load()
    for y in range(256):
        for x in range(256):
            n = ((x * 7 + y * 13) % 11) - 5
            px[x, y] = (26 + n, 28 + n, 34 + n)
    draw = ImageDraw.Draw(img, "RGBA")
    for y in range(0, 256, 32):  # panel seams
        draw.line((0, y, 256, y), fill=(16, 17, 22, 255), width=2)
    for _sx, _sy, _r, _c in ((40, 60, 26, (58, 62, 74)), (200, 180, 34, (52, 56, 68))):  # scuffs
        draw.ellipse((_sx - _r, _sy - _r, _sx + _r, _sy + _r), fill=_c + (90,))
    # two spray tags (random polylines), magenta + cyan, away from the edges
    img = img.convert("RGBA")
    _glow_line(img, [(60, 130), (78, 108), (100, 128), (122, 106)], NEON_A, 3)
    _glow_line(img, [(150, 190), (168, 170), (190, 190), (206, 172)], NEON_B, 2)
    img.convert("RGB").save(path)


def make_poster(path):
    """1024x512: the signage mural - near-black panel, neon tube frame, the
    garage's name in neon script, a cyan ground strip."""
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (1024, 512), (14, 15, 20, 255))
    draw = ImageDraw.Draw(img, "RGBA")
    for x in range(0, 1024, 4):  # subtle wall-panel striping
        draw.line((x, 0, x, 512), fill=(18, 19, 26, 255))
    # the tube frame
    _glow_line(img, [(36, 36), (988, 36)], NEON_A, 5)
    _glow_line(img, [(36, 476), (988, 476)], NEON_B, 5)
    _glow_line(img, [(36, 36), (36, 476)], NEON_B, 3)
    _glow_line(img, [(988, 36), (988, 476)], NEON_A, 3)
    # the name, two lines, magenta over cyan
    font_big = _font(96)
    font_small = _font(64)
    _glow_text(img, (110, 120), "TUNER", font_big, NEON_A)
    _glow_text(img, (110, 240), "GARAGE", font_big, NEON_B)
    _glow_text(img, (112, 372), "parts  -  fits  -  paint", font_small, NEON_A)
    # a graffiti scribble over the corner (our tag)
    _glow_line(img, [(760, 400), (800, 360), (850, 410), (900, 370), (950, 400)], NEON_B, 4)
    img.convert("RGB").save(path)


def make_flare(path, size, colour):
    """A neon lamp sprite like the originals: mostly BLACK with one bright
    radial blob in the centre ~45% of the tile - the flare quads are larger
    than the lamp, so a full-tile glow reads as a giant solid rectangle."""
    from PIL import Image
    w, h = size
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    cx, cy = w / 2.0, h / 2.0
    radius = min(w, h) * 0.24
    for y in range(h):
        for x in range(w):
            d = math.hypot(x - cx, y - cy) / radius
            if d >= 1.0:
                continue
            a = max(0.0, 1.0 - d)
            px[x, y] = (colour[0], colour[1], colour[2], int(255 * (a * a)))
    img.save(path)


def make_grid(path):
    """512x512, tileable: the colour-display wall (the retail orange perforated
    grid) as a dark pegboard with magenta / cyan glowing holes."""
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (512, 512), (16, 17, 22))
    draw = ImageDraw.Draw(img, "RGBA")
    step = 32
    for gy in range(0, 512, step):
        for gx in range(0, 512, step):
            draw.ellipse((gx + 10, gy + 10, gx + 22, gy + 22), fill=(8, 9, 12, 255))
            if (gx // step + gy // step) % 5 == 0:
                draw.ellipse((gx + 12, gy + 12, gx + 20, gy + 20),
                             fill=NEON_A + (200,))
            elif (gx // step * 3 + gy // step) % 7 == 0:
                draw.ellipse((gx + 12, gy + 12, gx + 20, gy + 20),
                             fill=NEON_B + (200,))
    img.save(path)


def make_nuancier(path):
    """256x256: the paint-swatch wall panel as a dark chart with neon strips."""
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (256, 256), (18, 19, 25))
    draw = ImageDraw.Draw(img, "RGBA")
    for i in range(8):
        colour = NEON_A if i % 2 == 0 else NEON_B
        y = 16 + i * 30
        draw.rectangle((16, y, 240, y + 18), outline=(34, 36, 46, 255), width=2)
        draw.rectangle((20, y + 4, 60 + i * 20, y + 14), fill=colour + (170,))
    img.save(path)


def make_roof(path):
    """256x128: dark corrugated roof deck (tileable stripes)."""
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (256, 128), (20, 21, 26))
    draw = ImageDraw.Draw(img)
    for x in range(0, 256, 16):
        draw.line((x, 0, x, 128), fill=(30, 32, 40), width=6)
        draw.line((x + 8, 0, x + 8, 128), fill=(12, 13, 17), width=3)
    img.save(path)


def make_screen(path):
    """256x128: the wall screens as dark tuner-shop displays - a cyan grid and
    the name, lit."""
    from PIL import Image, ImageDraw
    img = Image.new("RGBA", (256, 128), (10, 12, 18, 255))
    draw = ImageDraw.Draw(img, "RGBA")
    for x in range(0, 256, 24):
        draw.line((x, 0, x, 128), fill=(22, 40, 52, 255))
    for y in range(0, 128, 24):
        draw.line((0, y, 256, y), fill=(22, 40, 52, 255))
    font = _font(30)
    _glow_text(img, (18, 44), "TUNER", font, NEON_B)
    img.convert("RGB").save(path)


def make_frame(path, size, colour, label):
    """The framed wall pictures (the retail shop's product-photo sheets:
    steering wheels, rims, discs, tyres, magazines) as dark neon-framed
    panels with the label lit."""
    from PIL import Image, ImageDraw
    w, h = size
    img = Image.new("RGBA", (w, h), (13, 14, 19, 255))
    draw = ImageDraw.Draw(img, "RGBA")
    draw.rectangle((4, 4, w - 5, h - 5), outline=colour + (255,), width=4)
    font = _font(min(30, h // 3))
    _glow_text(img, (16, h // 2 - 14), label, font, colour)
    _glow_line(img, [(w - 40, 16), (w - 16, h - 20)],
               NEON_B if colour == NEON_A else NEON_A, 3)
    img.convert("RGB").save(path)


def make_floor(path):
    """512x512, tileable: dark chequer-plate tiles with grout lines."""
    from PIL import Image, ImageDraw
    img = Image.new("RGB", (512, 512), (24, 26, 32))
    draw = ImageDraw.Draw(img)
    for ty in range(4):
        for tx in range(4):
            x0, y0 = tx * 128, ty * 128
            tint = 4 * ((tx + ty) % 2)
            draw.rectangle((x0 + 2, y0 + 2, x0 + 126, y0 + 126), fill=(26 + tint, 28 + tint, 34 + tint))
            draw.rectangle((x0 + 2, y0 + 2, x0 + 126, y0 + 126), outline=(14, 15, 20), width=3)
            for i in range(6):  # chequer notches
                draw.line((x0 + 16 + i * 18, y0 + 108, x0 + 34 + i * 18, y0 + 90), fill=(38, 41, 50), width=3)
    img.save(path)


def make_art(force):
    os.makedirs(ART_DIR, exist_ok=True)
    jobs = [
        ("wall_graffiti.png", lambda p: make_wall(p)),
        ("poster_neon.png", lambda p: make_poster(p)),
        ("flare_neon_a.png", lambda p: make_flare(p, (128, 128), NEON_A)),
        ("flare_neon_b.png", lambda p: make_flare(p, (256, 128), NEON_B)),
        ("glow_neon.png", lambda p: make_flare(p, (256, 128), NEON_A)),
        ("roof_dark.png", lambda p: make_roof(p)),
        ("floor_dark.png", lambda p: make_floor(p)),
        ("grid_neon.png", lambda p: make_grid(p)),
        ("nuancier_dark.png", lambda p: make_nuancier(p)),
        ("screen_neon.png", lambda p: make_screen(p)),
        ("frame_neon_a.png", lambda p: make_frame(p, (128, 128), NEON_A, "GT-R")),
        ("frame_neon_b.png", lambda p: make_frame(p, (256, 128), NEON_B, "STI")),
        ("frame_neon_c.png", lambda p: make_frame(p, (256, 256), NEON_B, "RX-7")),
        ("frame_neon_d.png", lambda p: make_frame(p, (512, 128), NEON_A, "RAYS  TE37")),
        ("frame_neon_e.png", lambda p: make_frame(p, (256, 256), NEON_A, "BRAKE")),
        ("frame_neon_f.png", lambda p: make_frame(p, (256, 64), NEON_B, "TYRES")),
        ("frame_neon_g.png", lambda p: make_frame(p, (256, 256), NEON_A, "MAGS")),
    ]
    made = 0
    for name, job in jobs:
        path = os.path.join(ART_DIR, name)
        if force or not os.path.isfile(path):
            job(path)
            made += 1
    print("art: %d of %d images (re)generated in %s" % (made, len(jobs), ART_DIR))
    for name, _job in jobs:
        assert os.path.isfile(os.path.join(ART_DIR, name)), name


# ---------------------------------------------------------------------------
# the .2db writer (the crop_map_2db pattern: uncompressed sRGB A8R8G8B8)
# ---------------------------------------------------------------------------

def png_to_2db(source_entry_bytes, img):
    """An uncompressed sRGB A8R8G8B8 .2db cloned from the original texture's
    0x50-byte header (the name hash at +0x20 stays - the texture keeps its
    identity), resized to the original dimensions, full mip chain. An RGBA
    image keeps its alpha (the lightmaps' alpha is load-bearing); RGB is
    written opaque."""
    from PIL import Image
    w, h = struct.unpack_from("<HH", source_entry_bytes, 0x28)
    if img.size != (w, h):
        img = img.resize((w, h), Image.LANCZOS)
    img = img.convert("RGBA")
    payload = bytearray()
    level = img
    mw, mh = w, h
    mips = 0
    while True:
        raw = level.tobytes()  # RGBA
        row = bytearray(mw * mh * 4)
        for p in range(mw * mh):
            r, g, b, a = raw[p * 4: p * 4 + 4]
            row[p * 4: p * 4 + 4] = bytes((b, g, r, a))  # A8R8G8B8 little endian
        payload += row
        mips += 1
        if mw == 1 and mh == 1:
            break
        mw, mh = max(1, mw // 2), max(1, mh // 2)
        level = level.resize((mw, mh), Image.LANCZOS)
    header = bytearray(source_entry_bytes[:0x50])
    struct.pack_into("<HH", header, 0x28, w, h)
    struct.pack_into("<BB", header, 0x2E, mips, mips)
    struct.pack_into("<I", header, 0x3C, 31)  # VF_SRGB_A8R8G8B8
    total = 0x50 + len(payload)
    struct.pack_into("<I", header, 0x08, total)
    struct.pack_into("<II", header, 0x18, total - 0x10, total - 0x10)
    return bytes(header) + bytes(payload)


def darken_materials(data, scale):
    """Scales every material's Diffuse (+0xb0) and Ambient (+0xc0) float4 of a
    .2dm payload in place (formats.md: per-MAT colour floats; eden_2dm.parse
    gives each MAT's payload offset). The room's brightness otherwise comes
    from the renderer's hard-coded daylight scene constants."""
    mats = eden_2dm.parse(bytes(data), "pc")
    patched = 0
    for mat in mats:
        if mat.get("payload_offset") is None:
            continue
        for off in (0xB0, 0xC0):
            x, y, z, w = struct.unpack_from("<4f", data, mat["payload_offset"] + off)
            struct.pack_into("<4f", data, mat["payload_offset"] + off,
                             x * scale, y * scale, z * scale, w)
            patched += 1
    return len(mats), patched


# The lightmaps carry the retail room's baked look (its bright spots - the
# product frames, the warm floor pool - are IN the baked night map), so the
# re-skin dims them too. Scale per channel: dimmer and cooler.
LIGHTMAP_SCALES = {
    "lm_itun_night.2db": (0.42, 0.46, 0.60),
    "lm_itun.2db": (0.55, 0.58, 0.68),
}
# The glass boards' shared backdrop (the reflections are the retail showroom
# photos) - dimmed to a cool dark.
CUBEMAP = "cubecar.2db"
CUBEMAP_SCALES = (0.30, 0.34, 0.45)


def pc_dxt_decode(blob):
    """Decodes a PC DXT1/DXT5 .2db mip0 to an RGBA image: the payload wrapped
    in a DDS header, handed to PIL's own DXT decoder."""
    import io
    from PIL import Image
    fmt = struct.unpack_from("<I", blob, 0x3C)[0]
    w, h = struct.unpack_from("<HH", blob, 0x28)
    fourcc = b"DXT5" if fmt in (0x37, 55) else b"DXT1"
    block = 16 if fourcc == b"DXT5" else 8
    size = ((w + 3) // 4) * ((h + 3) // 4) * block
    dds = bytearray(128)
    dds[0:4] = b"DDS "
    struct.pack_into("<I", dds, 4, 124)
    struct.pack_into("<I", dds, 8, 0x1007)  # CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE
    struct.pack_into("<I", dds, 12, h)
    struct.pack_into("<I", dds, 16, w)
    struct.pack_into("<I", dds, 20, size)
    struct.pack_into("<I", dds, 28, 1)
    struct.pack_into("<I", dds, 76, 32)
    struct.pack_into("<I", dds, 80, 0x4)  # DDPF_FOURCC
    dds[84:88] = fourcc
    struct.pack_into("<I", dds, 108, 0x1000)
    return Image.open(io.BytesIO(bytes(dds) + blob[0x50:0x50 + size])).convert("RGB"), w, h


def _dds_image(data, offset, w, h, fourcc, size):
    """PIL-decodes a raw DXT mip sitting at data[offset:+size] (a DDS header
    wrapped around the blocks)."""
    import io
    from PIL import Image
    dds = bytearray(128)
    dds[0:4] = b"DDS "
    struct.pack_into("<I", dds, 4, 124)
    struct.pack_into("<I", dds, 8, 0x1007)  # CAPS | HEIGHT | WIDTH | PIXELFORMAT | LINEARSIZE
    struct.pack_into("<I", dds, 12, h)
    struct.pack_into("<I", dds, 16, w)
    struct.pack_into("<I", dds, 20, size)
    struct.pack_into("<I", dds, 28, 1)
    struct.pack_into("<I", dds, 76, 32)
    struct.pack_into("<I", dds, 80, 0x4)  # DDPF_FOURCC
    dds[84:88] = fourcc
    struct.pack_into("<I", dds, 108, 0x1000)
    return Image.open(io.BytesIO(bytes(dds) + data[offset:offset + size])).convert("RGBA")


def _colour_block_encode(px, scales):
    """The 8-byte DXT colour block of one 4x4 RGBA block, its RGB first scaled
    (the alpha block beside it stays byte for byte - dimming does not touch
    alpha). RGB565 endpoints at the max / min luma texel + 2-bit indices into
    the 4-colour palette."""
    scaled = [(min(255, int(p[0] * scales[0])), min(255, int(p[1] * scales[1])),
               min(255, int(p[2] * scales[2]))) for p in px]

    def rgb565(c):
        return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3)

    lumas = [p[0] * 2 + p[1] * 3 + p[2] for p in scaled]
    hi = max(range(16), key=lambda i: lumas[i])
    lo = min(range(16), key=lambda i: lumas[i])
    c0 = rgb565(scaled[hi])
    c1 = rgb565(scaled[lo])
    if c0 < c1:
        hi, lo = lo, hi
        c0, c1 = c1, c0
    r0, g0, b0 = scaled[hi]
    r1, g1, b1 = scaled[lo]
    pal = [(r0, g0, b0), (r1, g1, b1),
           ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3),
           ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3)]
    idx_bits = 0
    for i, p in enumerate(scaled):
        idx = min(range(4), key=lambda k: (p[0] - pal[k][0]) ** 2 + (p[1] - pal[k][1]) ** 2 +
                  (p[2] - pal[k][2]) ** 2)
        idx_bits |= idx << (2 * i)
    return struct.pack("<HHI", c0, c1, idx_bits)


def dxt_darken(blob, scales):
    """Dims a PC DXT1/DXT5 .2db with its structure byte layout untouched: for
    every mip of every face (faces run face major, each with its full mip
    chain, formats.md 2DB) the texels are decoded, their RGB scaled, and the
    DXT colour blocks re-encoded to the same format - alpha blocks stay as
    they are. The loader sees the identical structure with dimmer texels."""
    fmt = struct.unpack_from("<I", blob, 0x3C)[0]
    w, h = struct.unpack_from("<HH", blob, 0x28)
    faces = max(1, blob[0x2C])
    fourcc = b"DXT5" if fmt in (0x37, 55) else b"DXT1"
    is_dxt5 = fourcc == b"DXT5"
    block = 16 if is_dxt5 else 8
    out = bytearray(blob)
    data = bytes(blob)
    chain = []
    mw, mh = w, h
    while True:
        chain.append((mw, mh, ((mw + 3) // 4) * ((mh + 3) // 4) * block))
        if mw == 1 and mh == 1:
            break
        mw, mh = max(1, mw // 2), max(1, mh // 2)
    face_size = sum(size for _w, _h, size in chain)
    for face in range(faces):
        base = 0x50 + face * face_size
        off = 0
        for mw, mh, size in chain:
            # Cube maps pack their smallest mips in a shared tail (the DX9
            # convention): a mip that overruns the payload keeps the original
            # bytes - at 16x16 and below it is invisible.
            if mw >= 4 and mh >= 4 and base + off + size <= len(data):
                img = _dds_image(data, base + off, mw, mh, fourcc, size)
                px = img.load()
                bw, bh = mw // 4, mh // 4
                for by in range(bh):
                    for bx in range(bw):
                        block_px = [px[bx * 4 + i, by * 4 + j] for j in range(4) for i in range(4)]
                        cb = _colour_block_encode(block_px, scales)
                        dst = base + off + (by * bw + bx) * block + (8 if is_dxt5 else 0)
                        out[dst:dst + 8] = cb
            off += size
    return bytes(out)


# ---------------------------------------------------------------------------
# the bank
# ---------------------------------------------------------------------------

def build(game, userdata):
    src = os.path.join(game, GAME_BANK)
    if not os.path.isfile(src):
        raise SystemExit("no %s (check --game)" % src)
    bank = eden_bank.parse_file(src, eden_bank.PC)
    entries = {e["filename"].lower(): e for e in bank["entries"]}

    def payload(entry):
        return bank["data"][entry["offset"]:entry["offset"] + entry["size"]]

    from PIL import Image
    swaps = {}
    for entry_name, art_name in SWAPS.items():
        key = entry_name.lower()
        if key not in entries:
            print("  ! '%s' not in the bank (skipped)" % entry_name)
            continue
        art_path = os.path.join(ART_DIR, art_name)
        if not os.path.isfile(art_path):
            raise SystemExit("missing art '%s' (run with --art)" % art_path)
        img = Image.open(art_path).convert("RGB")
        original = bytes(payload(entries[key]))
        swaps[key] = png_to_2db(original, img)
        w, h = struct.unpack_from("<HH", original, 0x28)
        print("  %s -> %s (%dx%d, uncompressed sRGB)" % (art_name, entry_name, w, h))

    lms = {}
    for entry_name, scales in LIGHTMAP_SCALES.items():
        key = entry_name.lower()
        if key not in entries:
            print("  ! '%s' not in the bank (skipped)" % entry_name)
            continue
        lms[key] = dxt_darken(bytes(payload(entries[key])), scales)
        print("  %s darkened (%.2f/%.2f/%.2f)" % (entry_name, *scales))
    if CUBEMAP.lower() in entries:
        lms[CUBEMAP.lower()] = dxt_darken(bytes(payload(entries[CUBEMAP.lower()])), CUBEMAP_SCALES)
        print("  %s darkened (the glass boards' backdrop)" % CUBEMAP)

    darkened = []
    out_entries = []
    for entry in bank["entries"]:
        name = entry["filename"].lower()
        data = swaps.get(name)
        if data is None:
            data = lms.get(name)
        if data is None:
            data = bytes(payload(entry))
        if name.endswith(".2dm"):
            count, patched = darken_materials(bytearray(data), DARKEN)
            data = bytes(data)
            darkened.append("%s: %d materials" % (entry["filename"], count))
        out_entries.append({"path": entry["path"], "data": data,
                            "subtype": entry["subtype"], "group": entry["group"]})
    for line in darkened:
        print("  darkened %s (x%.2f)" % (line, DARKEN))

    directory_order = [eden_bank.decode_bank_path(encoded)
                       for encoded in build_bank.eden_bank_directory_order(bank)]
    new_bank = build_bank.build_bank(out_entries,
                                     creation_date=bank["header"]["creationDate"],
                                     bank_type=bank["header"]["type"],
                                     directory_order=directory_order)

    # self-check: the output re-parses, same entry count, patches present
    re_parsed = eden_bank.parse(bytes(new_bank), eden_bank.PC)
    assert len(re_parsed["entries"]) == len(bank["entries"]), "entry count changed"
    for entry_name, art_name in SWAPS.items():
        key = entry_name.lower()
        entry = next(e for e in re_parsed["entries"] if e["filename"].lower() == key)
        blob = re_parsed["data"][entry["offset"]:entry["offset"] + entry["size"]]
        fmt = struct.unpack_from("<I", blob, 0x3C)[0]
        assert fmt == 31, "%s: format %d, expected the uncompressed 31" % (entry_name, fmt)
    print("  self-check: %d entries, %d swapped textures, materials darkened" %
          (len(re_parsed["entries"]), len(swaps)))

    out_dir = os.path.join(userdata, "mods", "packages", "tuner-garage", "Euro", "Bnk", "interior")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "itnr_a.bnk")
    with open(out_path, "wb") as f:
        f.write(new_bank)
    print("wrote %s (%.1f MB, was %.1f MB)" %
          (out_path, len(new_bank) / 1e6, os.path.getsize(src) / 1e6))
    print("the garage uses it on the next game start (InteriorWalk prefers itnr_a.bnk); "
          "delete the file to fall back to the vanilla interior")


def main():
    ap = argparse.ArgumentParser(description="Build the tuner garage's own interior bank (itnr_a.bnk).")
    ap.add_argument("--game", default=r"C:\TDU2-Gamedata\TestDrive2", help="the game root (read only)")
    ap.add_argument("--userdata", default=r"C:\temp\otdu2-win-userdata",
                    help="the -userdata folder whose mods overlay gets the bank")
    ap.add_argument("--art", action="store_true", help="regenerate the PNG art even if it exists")
    args = ap.parse_args()
    make_art(args.art)
    build(args.game, args.userdata)


if __name__ == "__main__":
    main()

"""Reads a texture dump of the 360 recomp (SDK patch 0013: --texture_dump_dir) - see the recomp's TEXTURE_DUMP.md.

  python -I tools/x360/tex_convert.py report <dump dir>              summary of textures.csv (sizes, formats, cube / array / 3D)
  python -I tools/x360/tex_convert.py convert <dump dir> <out dir> [--min 256] [--limit N] [--only list.txt (file names)]
                                                                      PNG of the base level of each 2D texture (DXT1/3/5, DXN, 8888, 8, 565...)

The .bin is the guest's raw bytes: big-endian, 32x32-block tiled (Xenos), base level first then the mip chain. Format names are the
SDK's FormatInfo names (matched by substring). UNVERIFIED until run on a real dump: the tiling and swap code follows Xenia's
texture_util and has not been checked against this game's data yet.
"""
import csv
import os
import struct
import sys
from collections import Counter, defaultdict

import numpy as np


def read_rows(folder):
    with open(os.path.join(folder, "textures.csv"), newline="") as f:
        return list(csv.DictReader(f))


def report(folder):
    rows = read_rows(folder)
    print(len(rows), "distinct textures")
    by_dim = Counter(r["dimension"] for r in rows)
    print("dimensions:", dict(by_dim))
    by_fmt = Counter(r["format"] for r in rows)
    print("formats:", dict(by_fmt.most_common()))
    big = [r for r in rows if r["dimension"].startswith("2D") and int(r["width"]) >= 1024 and int(r["height"]) >= 512]
    print("large 2D textures (>= 1024 x 512) - candidates for sprite sheets / atlases:", len(big))
    for r in sorted(big, key=lambda r: -int(r["width"]) * int(r["height"]))[:40]:
        print("  %-60s %sx%s %s mips %s layers %s" % (r["file"], r["width"], r["height"], r["format"], r["mips"], r["depth_or_layers"]))
    stacked = [r for r in rows if int(r["depth_or_layers"]) > 1]
    print("textures with layers (array / cube / 3D):", len(stacked))
    sizes = Counter((int(r["width"]), int(r["height"])) for r in rows)
    print("most common sizes:", sizes.most_common(12))


def swap(data, endian):
    a = np.frombuffer(data, dtype=np.uint8).copy()
    n = len(a) - (len(a) % 4)
    a = a[:n]
    if endian == 1:  # 8in16
        a = a.reshape(-1, 2)[:, ::-1].reshape(-1)
    elif endian == 2:  # 8in32
        a = a.reshape(-1, 4)[:, ::-1].reshape(-1)
    elif endian == 3:  # 16in32
        a = a.reshape(-1, 2, 2)[:, ::-1, :].reshape(-1)
    return a


def tiled_offset_2d(x, y, width_blocks, log2_bpp):
    macro = (((x >> 5) + (y >> 5) * (width_blocks >> 5)) << (log2_bpp + 7))
    micro = (((x & 7) + ((y & 0xE) << 2)) << log2_bpp)
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


def untile(data, w_blocks, h_blocks, bytes_per_block, tiled):
    if not tiled:
        return data[: w_blocks * h_blocks * bytes_per_block].reshape(h_blocks, w_blocks, bytes_per_block)
    log2_bpp = bytes_per_block.bit_length() - 1
    aw = (w_blocks + 31) & ~31
    ah = (h_blocks + 31) & ~31
    ys, xs = np.mgrid[0:h_blocks, 0:w_blocks]
    offs = np.vectorize(lambda x, y: tiled_offset_2d(int(x), int(y), aw, log2_bpp))(xs, ys)
    out = np.zeros((h_blocks, w_blocks, bytes_per_block), np.uint8)
    for i in range(bytes_per_block):
        idx = offs + i
        valid = idx < len(data)
        out[..., i][valid] = data[idx[valid]]
    return out


def rgb565(v):
    r = ((v >> 11) & 31) * 255 // 31
    g = ((v >> 5) & 63) * 255 // 63
    b = (v & 31) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.uint8)


def decode_dxt_color(blocks, dxt1):
    c0 = blocks[..., 0].astype(np.uint16) | (blocks[..., 1].astype(np.uint16) << 8)
    c1 = blocks[..., 2].astype(np.uint16) | (blocks[..., 3].astype(np.uint16) << 8)
    bits = blocks[..., 4].astype(np.uint32) | (blocks[..., 5].astype(np.uint32) << 8) | (blocks[..., 6].astype(np.uint32) << 16) | (blocks[..., 7].astype(np.uint32) << 24)
    p0, p1 = rgb565(c0).astype(np.int32), rgb565(c1).astype(np.int32)
    pal = np.zeros(blocks.shape[:2] + (4, 4), np.int32)
    pal[..., 0, :3], pal[..., 1, :3] = p0, p1
    four = (c0 > c1) | (not dxt1)
    pal[..., 2, :3] = np.where(four[..., None], (2 * p0 + p1) // 3, (p0 + p1) // 2)
    pal[..., 3, :3] = np.where(four[..., None], (p0 + 2 * p1) // 3, 0)
    pal[..., :, 3] = 255
    if dxt1:
        pal[..., 3, 3] = np.where(four, 255, 0)
    h, w = blocks.shape[:2]
    img = np.zeros((h * 4, w * 4, 4), np.uint8)
    for py in range(4):
        for px in range(4):
            sel = (bits >> (2 * (py * 4 + px))) & 3
            img[py::4, px::4] = pal[np.arange(h)[:, None], np.arange(w)[None, :], sel.astype(np.int64)]
    return img


def decode_alpha_block(blocks, interp):
    h, w = blocks.shape[:2]
    out = np.zeros((h * 4, w * 4), np.uint8)
    if not interp:  # DXT3: 4 bit alpha
        for i in range(16):
            byte = blocks[..., i // 2]
            a = (byte >> 4) if (i & 1) else (byte & 15)
            out[(i // 4)::4, (i % 4)::4] = a * 17
        return out
    a0, a1 = blocks[..., 0].astype(np.int32), blocks[..., 1].astype(np.int32)
    bits = np.zeros(blocks.shape[:2], np.uint64)
    for i in range(6):
        bits |= blocks[..., 2 + i].astype(np.uint64) << np.uint64(8 * i)
    lut = np.zeros(blocks.shape[:2] + (8,), np.int32)
    lut[..., 0], lut[..., 1] = a0, a1
    for k in range(1, 7):
        lut[..., k + 1] = np.where(a0 > a1, ((7 - k) * a0 + k * a1) // 7, np.where(k < 5, ((5 - k) * a0 + k * a1) // 5, 0))
    lut[..., 7] = np.where(a0 > a1, lut[..., 7], 255)
    lut[..., 6] = np.where(a0 > a1, lut[..., 6], 0)
    for i in range(16):
        sel = ((bits >> np.uint64(3 * i)) & np.uint64(7)).astype(np.int64)
        out[(i // 4)::4, (i % 4)::4] = np.take_along_axis(lut, sel[..., None], 2)[..., 0].astype(np.uint8)
    return out


def decode(row, data):
    fmt = row["format"].upper()
    w, h = int(row["width"]), int(row["height"])
    tiled = int(row["tiled"]) == 1
    raw = swap(data, int(row["endian"]))
    if "DXT1" in fmt or "DXT2_3" in fmt or "DXT3" in fmt or "DXT4_5" in fmt or "DXT5" in fmt or "DXN" in fmt:
        bpb = 8 if "DXT1" in fmt else 16
        wb, hb = (w + 3) // 4, (h + 3) // 4
        blocks = untile(raw, wb, hb, bpb, tiled)
        if bpb == 8:
            return decode_dxt_color(blocks, True)[:h, :w]
        if "DXN" in fmt:
            r = decode_alpha_block(blocks[..., 0:8], True)
            g = decode_alpha_block(blocks[..., 8:16], True)
            return np.stack([r, g, np.zeros_like(r), np.full_like(r, 255)], -1)[:h, :w]
        img = decode_dxt_color(blocks[..., 8:16], False)
        img[..., 3] = decode_alpha_block(blocks[..., 0:8], "DXT4_5" in fmt or "DXT5" in fmt)
        return img[:h, :w]
    if "8_8_8_8" in fmt:
        px = untile(raw, w, h, 4, tiled)
        return np.stack([px[..., 1], px[..., 2], px[..., 3], px[..., 0]], -1) if "A" not in fmt else px  # ARGB bytes -> RGBA
    if fmt.endswith("_8") or fmt == "K_8":
        px = untile(raw, w, h, 1, tiled)[..., 0]
        return np.stack([px, px, px, np.full_like(px, 255)], -1)
    return None


def convert(folder, out, minimum=256, limit=1 << 30, only=None):
    from PIL import Image

    os.makedirs(out, exist_ok=True)
    done = skipped = 0
    for row in read_rows(folder):
        if only is not None and row["file"] not in only:
            continue
        if not row["dimension"].startswith("2D") or max(int(row["width"]), int(row["height"])) < minimum or done >= limit:
            continue
        with open(os.path.join(folder, row["file"]), "rb") as f:
            data = f.read()
        img = decode(row, data)
        if img is None:
            skipped += 1
            continue
        Image.fromarray(img, "RGBA").save(os.path.join(out, row["file"].replace(".bin", ".png")))
        done += 1
    print("converted", done, "skipped (format not handled)", skipped)


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "report":
        report(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "convert":
        mn = int(sys.argv[sys.argv.index("--min") + 1]) if "--min" in sys.argv else 256
        lim = int(sys.argv[sys.argv.index("--limit") + 1]) if "--limit" in sys.argv else 1 << 30
        only = set(open(sys.argv[sys.argv.index("--only") + 1]).read().split()) if "--only" in sys.argv else None
        convert(sys.argv[2], sys.argv[3], mn, lim, only)
    else:
        print(__doc__)

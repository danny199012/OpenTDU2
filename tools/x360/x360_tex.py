"""Decode TDU2 Xbox 360 2DB textures (tiled, 16-bit swapped DXT) from a bank to PNG."""
import struct, sys, os, io, hashlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x360_bank import entries

def tiled_offset(x, y, width, log_bpp):
    # XGAddress2DTiledOffset (block coordinates; width in blocks), as in Xenia.
    aligned_w = (width + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aligned_w >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7)
            + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp

def decode(blob, fourcc):
    w = int.from_bytes(blob[0x28:0x2A], "big")
    h = int.from_bytes(blob[0x2A:0x2C], "big")
    block = 8 if fourcc == b"DXT1" else 16
    log_bpp = 3 if block == 8 else 4
    data = blob[0x1000:]
    bw, bh = max(1, w // 4), max(1, h // 4)
    out = bytearray(bw * bh * block)
    for y in range(bh):
        for x in range(bw):
            src = tiled_offset(x, y, bw, log_bpp) * block
            chunk = data[src:src + block]
            if len(chunk) < block:
                continue
            # 16-bit endian swap
            sw = bytearray(block)
            sw[0::2] = chunk[1::2]
            sw[1::2] = chunk[0::2]
            dst = (y * bw + x) * block
            out[dst:dst + block] = sw
    dds = bytearray(128)
    dds[0:4] = b"DDS "
    struct.pack_into("<IIIIIII", dds, 4, 124, 0x1007, h, w, len(out), 0, 1)
    struct.pack_into("<II4s", dds, 76, 32, 4, fourcc)
    struct.pack_into("<I", dds, 108, 0x1000)
    from PIL import Image  # only the PNG path needs PIL
    return Image.open(io.BytesIO(bytes(dds) + bytes(out))), w, h

if __name__ == "__main__":
    bank, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    d = open(bank, "rb").read()
    for hsh, blk, off, size in entries(d):
        blob = d[off:off + size]
        if b"BD2." not in blob[:16]:
            continue
        name = bytes(reversed(blob[0x20:0x28])).split(b"\0")[0]
        name = "".join(chr(c) if 48 <= c < 123 else f"{c:02x}" for c in name)
        fmt = blob[0x2E]
        digest = hashlib.sha1(blob[0x1000:]).hexdigest()[:12]
        for fourcc in ((b"DXT5", b"DXT1") if fmt == 6 else (b"DXT1", b"DXT5")):
            try:
                img, w, h = decode(blob, fourcc)
                path = os.path.join(outdir, f"{name}_{fourcc.decode()}.png")
                img.save(path)
                print(f"{name:14s} {w}x{h} fmt {fmt:#x} {fourcc.decode()} sha {digest} -> {path}")
            except Exception as ex:
                print(name, fourcc, "failed", ex)

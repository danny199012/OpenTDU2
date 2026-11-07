"""Restore a texture cut from the retail game: take it from a 360 prototype bank and write a PC .2db
that OpenTDU2 loads in place of the retail one (source/render/texture_cache.cpp, "restored content").

    python restore_texture.py <360 bank> <TEXTURE NAME> <retail PC .2db (template)> <userdata> <bank name>

e.g. the Q7 Inuit's grey dials:
    python restore_texture.py C:/TDU2-Proto/dec2010/Euro/Bnk/vehicules/interiors/q7_inuit_i.bnk Q7_INT2 ^
        C:/TDU2-Proto/retail/q7_inuit_i/q7_int2.2db C:/temp/otdu2-win-userdata q7_inuit_i
-> <userdata>/restored/q7_inuit_i/q7_int2.2db

PC .2db [DATA, retail q7_int2.2db]: 0x20 bytes of file / BMAP section header (u32 total size at 0x08,
section sizes at 0x18 / 0x1C = total - 0x10), the 0x30-byte Texture2DBHeader at 0x20 (NumMipmap 0x2E,
NumPackedMipmap 0x2F), texels from 0x50. The 360 texture's mip 0 is untiled and swapped (x360_tex);
its mip tail is not decoded, so the restored file holds mip 0 only (NumMipmap 1).
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from x360_bank import entries
from x360_tex import tiled_offset


def untile_mip0(blob):
    w = int.from_bytes(blob[0x28:0x2A], "big")
    h = int.from_bytes(blob[0x2A:0x2C], "big")
    block = 8 if blob[0x2E] == 0x05 else 16  # 0x05 DXT1, 0x06 DXT5 [DATA: decoded]
    log_bpp = 3 if block == 8 else 4
    data = blob[0x1000:]
    bw, bh = max(1, w // 4), max(1, h // 4)
    out = bytearray(bw * bh * block)
    for y in range(bh):
        for x in range(bw):
            src = tiled_offset(x, y, bw, log_bpp) * block
            chunk = data[src:src + block]
            dst = (y * bw + x) * block
            out[dst:dst + block:2] = chunk[1::2]
            out[dst + 1:dst + block:2] = chunk[0::2]
    return w, h, block, bytes(out)


def build_pc_2db(template_bytes, pixels, mip_count=1):
    """A PC .2db holding one mip: the retail template's 0x50-byte header, the
    given mip 0 pixels (untiled, little endian) and one mip in the header.

    The header layout (PC .2db): 0x20 bytes of file / BMAP section header (u32
    total size at 0x08, section sizes at 0x18 / 0x1C = total - 0x10), the
    0x30-byte Texture2DBHeader at 0x20 (NumMipmap 0x2E, NumPackedMipmap 0x2F),
    texels from 0x50.
    """
    tpl = bytearray(template_bytes[:0x50])
    tpl[0x2E] = mip_count
    tpl[0x2F] = mip_count
    total = 0x50 + len(pixels)
    struct.pack_into("<I", tpl, 0x08, total)
    struct.pack_into("<II", tpl, 0x18, total - 0x10, total - 0x10)
    return bytes(tpl) + pixels


def main():
    bank_path, name, template_path, userdata, bank_name = sys.argv[1:6]
    d = open(bank_path, "rb").read()
    blob = None
    for _, _, off, size in entries(d):
        b = d[off:off + size]
        if b"BD2." in b[:16] and bytes(reversed(b[0x20:0x28])).split(b"\0")[0] == name.encode():
            blob = b
    if blob is None:
        raise SystemExit(f"{name} not found in {bank_path} (long names are folded: use x360_tex.py to list them)")
    w, h, block, mip0 = untile_mip0(blob)
    tpl = open(template_path, "rb").read()
    tw, th = struct.unpack_from("<HH", tpl, 0x28)
    if (tw, th) != (w, h):
        raise SystemExit(f"template is {tw}x{th}, the prototype texture {w}x{h}")
    out_dir = os.path.join(userdata, "restored", bank_name)
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, os.path.basename(template_path).lower())
    open(out, "wb").write(build_pc_2db(tpl, mip0))
    print(f"{name}: {w}x{h} mip 0 ({len(mip0)} bytes) -> {out}")


if __name__ == "__main__":
    main()

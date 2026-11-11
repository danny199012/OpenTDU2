"""Bisect the 'Eden island' islet out of Hawaii into a standalone island tree.

The round crater islet off Hawaii's SE corner (world ~64,050, 44,360; area
10-7, sectors 3-1 / 3-2 / 4-2 - docs/re/eden_island_spec.md 1) is fully
modeled terrain with waterfalls, a footbridge, ferry props and a baked road,
but it has no road-graph nodes and no spots. This tool re-homes it as its own
island (`islands/eden/`) in the mods overlay, under a 1 x 1 area grid:

    python -I tools/mod/make_eden_island.py ^
        --gamedata C:/TDU2-Gamedata/TestDrive2 ^
        --userdata C:/temp/otdu2-win-userdata

then drive it (headless evidence: add -screenshot <file> -screenshotframe N):

    <exe> -game <gamedata> -userdata <userdata> -gm freeride -island eden -skipcine -tp 2950,1350

What it writes (never into the game folder - modding.md 4):

  islands/eden/level/worldconfig.cpr          new, XTEA-CBC (<Area NbColumns="1" NbRows="1"/>)
  islands/eden/level/eden.bnk                 from hawai.bnk: entries renamed, road IA dropped;
                                              island heightfield flattened to sea with the islet
                                              re-sampled from the area sheet (retail leaves the
                                              islet void at this LOD, eden_island_spec.md 1);
                                              -c map cropped to the area, sea-masked by the area
                                              heightfield, re-encoded A8R8G8B8 with a mip chain
  islands/eden/level/area-0/area-0-0/
    area-0-0.bnk                              from area-10-7.bnk: entries renamed; heightfield
                                              flattened outside the islet rect (drops O'ahu's
                                              severed coastline); -o.3dd: sector-group node
                                              hashcodes renamed Sector-10-7-* -> Sector-0-0-* and
                                              the non-shipped sectors' CITY impostors sunk 500 m
                                              (the ocean tiles of every group stay)
    sector-0-0-3-1.bnk (+ 3-2, 4-2)           from sector-10-7-*.bnk: entries renamed (sector
                                              content is index-local, collision area-local -
                                              eden_island_spec.md 2 facts 2-4)
  islands/eden/level/commonworld.bnk          verbatim copy (the PMI library)
  islands/eden/scenarics/packs/worldactors.bnk  verbatim copy
  islands/eden/fx/game_weathers.xmb           verbatim copy

Loader facts this encodes are cited in eden_island_spec.md 2 (verified against
source/world/eden_world.cpp, source/render/3dd.cpp and
source/physics/vehicle_collision.cpp). The XTEA key and CBC layout are from
source/core/crypto/cpr.cpp; the encrypt side is the decipher's exact inverse
and is validated against the source island's own worldconfig.cpr by round-trip.
"""
import argparse
import io
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(HERE, "..", "x360")))

import eden_bank  # noqa: E402  (bank reader + path decoding)
from build_bank import build_bank  # noqa: E402  (the PC bank writer)

try:
    sys.path.insert(0, r"C:\Users\cuz_c\AppData\Roaming\Python\Python314\site-packages")
    from PIL import Image
except ImportError:
    Image = None

K_CPR_KEY = (0x4FE23C4A, 0x80BAC211, 0x6917BD3A, 0xF0528EBD)  # DAT_00f763bc
MASK = 0xFFFFFFFF
DELTA_DEC = 0x61C88647  # the decipher's step (-delta)

SECTOR_SIZE = 768.0
SECTORS_PER_AREA = 8
AREA_SIZE = SECTOR_SIZE * SECTORS_PER_AREA
HF_SIZE = 257  # heightfield vertices per side

SEA_ISLAND = -100.0  # the island-grid's sea placeholder
SEA_AREA = -10.0     # the area-grid's sea placeholder


def _fcc(text):
    return struct.unpack("<I", text.encode("ascii"))[0]


T_3DD = _fcc(".3DD")
T_3DG = _fcc(".3DG")
T_HASH = _fcc("HASH")
T_INSA = _fcc("INSA")
T_OBJA = _fcc("OBJA")
T_HIE = _fcc("HIE.")
T_DXVB = _fcc("DXVB")


def log(msg):
    print(msg, flush=True)


# ---------------------------------------------------------------- identifiers

def identifier64(text):
    """GetIdentifier64bit (source/shared.h:96): 8 additive slots, uppercased."""
    slots = [0] * 8
    for i, ch in enumerate(text.encode("ascii", "strict")):
        if 0x61 <= ch <= 0x7A:  # > '`' && < '{' -> upper
            ch -= 0x20
        slots[i & 7] = (slots[i & 7] + ch) & 0xFF
    return sum(slot << (i * 8) for i, slot in enumerate(slots))


def hash_to_name(hashcode):
    out = ""
    for i in range(8):
        c = (hashcode >> (i * 8)) & 0xFF
        if c == 0:
            break
        out += chr(c) if 32 <= c < 127 else "?"
    return out


# ---------------------------------------------------------------------- XTEA

def xtea_encipher(v0, v1):
    """Exact inverse of XTEADecipher (core/crypto/cpr.cpp:10): the decipher
    walks sum from 0xC6EF3720 upward by DELTA_DEC; the encipher walks it back."""
    key = K_CPR_KEY
    s = (0xC6EF3720 + 32 * DELTA_DEC) & MASK
    for _ in range(32):
        f = ((((v1 >> 5) ^ ((v1 << 4) & MASK)) + v1) & MASK) ^ ((key[s & 3] + s) & MASK)
        v0 = (v0 + f) & MASK
        s = (s - DELTA_DEC) & MASK
        f = ((((v0 >> 5) ^ ((v0 << 4) & MASK)) + v0) & MASK) ^ ((key[(s >> 11) & 3] + s) & MASK)
        v1 = (v1 + f) & MASK
    return v0, v1


def xtea_decipher(v0, v1):
    key = K_CPR_KEY
    s = 0xC6EF3720
    for _ in range(32):
        f = ((((v0 >> 5) ^ ((v0 << 4) & MASK)) + v0) & MASK) ^ ((key[(s >> 11) & 3] + s) & MASK)
        v1 = (v1 - f) & MASK
        s = (s + DELTA_DEC) & MASK
        f = ((((v1 >> 5) ^ ((v1 << 4) & MASK)) + v1) & MASK) ^ ((key[s & 3] + s) & MASK)
        v0 = (v0 - f) & MASK
    return v0, v1


def decrypt_cpr(blob):
    """DecryptCPR: file = [8-byte IV][blocks]; p_i = c_i XOR D(c_{i+1})."""
    if len(blob) < 16 or (len(blob) - 8) % 8 != 0:
        raise SystemExit(f"cpr: bad size {len(blob)}")
    num = (len(blob) - 8) // 8
    words = struct.unpack(f"<{2 * (num + 1)}I", blob[: 8 + num * 8])
    out = bytearray()
    for i in range(num):
        d0, d1 = xtea_decipher(words[2 * i + 2], words[2 * i + 3])
        out += struct.pack("<2I", words[2 * i] ^ d0, words[2 * i + 1] ^ d1)
    return bytes(out)


def encrypt_cpr(plain, iv=b"\0" * 8):
    """Inverse CBC: c_{i+1} = E(c_i XOR p_i); output = IV + blocks. The reader
    takes strnlen, so zero padding is fine."""
    if len(plain) % 8:
        plain += b"\0" * (8 - len(plain) % 8)
    out = bytearray(iv)
    prev = struct.unpack("<2I", iv)
    for i in range(0, len(plain), 8):
        p = struct.unpack("<2I", plain[i:i + 8])
        e0, e1 = xtea_encipher(prev[0] ^ p[0], prev[1] ^ p[1])
        out += struct.pack("<2I", e0, e1)
        prev = (e0, e1)
    return bytes(out)


def selftest_xtea(gamedata, island):
    """Decrypt the source island's worldconfig.cpr, re-encrypt with its own IV,
    demand byte-identical (validates the cipher against retail data)."""
    path = os.path.join(gamedata, "Euro", "Bnk", "islands", island, "level", "worldconfig.cpr")
    blob = open(path, "rb").read()
    plain = decrypt_cpr(blob)
    text = plain.split(b"\0")[0].decode("ascii", "replace")
    if encrypt_cpr(plain, iv=blob[:8]) != blob:
        raise SystemExit("XTEA self-test FAILED (re-encrypt mismatch)")
    log(f"XTEA self-test OK: {island} worldconfig = {text!r}")


def read_worldconfig(gamedata, island):
    """(columns, rows) of the source island, from its decrypted worldconfig."""
    path = os.path.join(gamedata, "Euro", "Bnk", "islands", island, "level", "worldconfig.cpr")
    text = decrypt_cpr(open(path, "rb").read()).split(b"\0")[0].decode("ascii", "replace")
    values = {}
    for name in ("NbColumns", "NbRows"):
        at = text.find(name)
        if at < 0:
            raise SystemExit(f"worldconfig of {island}: no {name}")
        at = text.find('"', at)
        values[name] = int(text[at + 1: text.find('"', at + 1)])
    return values["NbColumns"], values["NbRows"]


# ------------------------------------------------------------- render files

def walk_sections(data, want_magic):
    """RenderFile::VisitSections: {u32 type, u16, u16, u32 size, u32 dataSize}
    records; a section's children live in [offset+dataSize, offset+size).
    Yields (type, header_offset, payload_offset, payload_size, container_type)
    where container_type is the top-level section above this one."""
    def visit(begin, end, container):
        offset = begin
        while offset + 0x10 <= end:
            sec_type = struct.unpack_from("<I", data, offset)[0]
            size = struct.unpack_from("<I", data, offset + 8)[0]
            data_size = max(struct.unpack_from("<I", data, offset + 12)[0], 0x10)
            if size < 0x10 or offset + size > end:
                return
            yield sec_type, offset, offset + 0x10, size - 0x10, container
            if size > data_size:
                yield from visit(offset + data_size, offset + size,
                                 sec_type if container is None else container)
            offset += size

    if struct.unpack_from("<I", data, 0x0C)[0] != want_magic:
        raise SystemExit(f"not a {want_magic:#010x} payload")
    yield from visit(0x10, min(len(data), struct.unpack_from("<I", data, 0x08)[0]), None)


def entry_data(bank, entry):
    return bank["data"][entry["offset"]: entry["offset"] + entry["size"]]


def rename_entry(entry, old_prefix, new_prefix):
    """The entry's path with the filename's leading name swapped (keeps
    -o/-c/... suffixes and any directory components). Case-insensitive match;
    None when the name does not carry the prefix."""
    dir_name, _, filename = entry["path"].rpartition("\\")
    stem, dot, ext = filename.rpartition(".")
    if not dot:
        stem, ext = filename, ""
    if not stem.lower().startswith(old_prefix.lower()):
        return None
    new_stem = new_prefix + stem[len(old_prefix):]
    new_filename = new_stem + ((dot + ext) if dot else "")
    return (dir_name + "\\" + new_filename) if dir_name else new_filename


def patch_2db_name_hash(data, new_name):
    """The 0x50-byte 2db header carries GetIdentifier64bit(name) at +0x20."""
    out = bytearray(data)
    struct.pack_into("<Q", out, 0x20, identifier64(new_name))
    return bytes(out)


# ------------------------------------------------------- heightfields (3dg)

def heightfield_streams(data):
    """Locate the DXVB of a 257x257 heightfield in a .3dg payload: returns
    (num_vertex, stream0_offset, stream1_offset). Layout per
    source/render/3dg.cpp: semantics [{1, 0x0F R32 height}, {1, 0x0B half4
    normal}], stream 0 then stream 1 after the 0x30 DXVB header."""
    for sec_type, hdr, _pay, _psz, _cont in walk_sections(data, T_3DG):
        if sec_type != T_DXVB:
            continue
        num_vertex = struct.unpack_from("<I", data, hdr + 0x10)[0]
        if num_vertex != HF_SIZE * HF_SIZE:
            continue
        pos_count, pos_fmt = data[hdr + 0x14], data[hdr + 0x15]
        nrm_count, nrm_fmt = data[hdr + 0x16], data[hdr + 0x17]
        if (pos_count, pos_fmt, nrm_count, nrm_fmt) != (1, 0x0F, 1, 0x0B):
            raise SystemExit(f"unexpected heightfield layout {pos_count}x{pos_fmt:#x} / {nrm_count}x{nrm_fmt:#x}")
        base = hdr + 0x30
        return num_vertex, base, base + num_vertex * 4
    raise SystemExit("no 257x257 heightfield DXVB found")


def read_heights(data):
    num, s0, _ = heightfield_streams(data)
    return list(struct.unpack_from(f"<{num}f", data, s0))


def patch_island_heightfield(data, area_data, keep_rect):
    """eden_island_spec.md 3: flatten every island-grid cell to sea, then
    re-sample the islet cells from the (already flattened) area sheet - retail
    leaves the islet void at this LOD. keep_rect = eden-space world rect
    (x0, z0, x1, z1) to preserve. Kept cells take the area sheet's height and
    normal at the same spot; flattened cells take a retail sea vertex's normal."""
    num, s0, s1 = heightfield_streams(data)
    heights = read_heights(data)
    sea_vertex = next(v for v in range(num) if heights[v] <= SEA_ISLAND)
    sea_normal = bytes(data[s1 + sea_vertex * 8: s1 + sea_vertex * 8 + 8])

    a_num, a_s0, a_s1 = heightfield_streams(area_data)
    area_heights = struct.unpack_from(f"<{a_num}f", area_data, a_s0)

    out = bytearray(data)
    x0, z0, x1, z1 = keep_rect
    patched = kept = 0
    for j in range(HF_SIZE):
        world_z = j * 384.0
        for i in range(HF_SIZE):
            v = j * HF_SIZE + i
            world_x = i * 384.0
            if x0 <= world_x <= x1 and z0 <= world_z <= z1:
                ax = max(0, min(HF_SIZE - 1, round(world_x / 24.0)))
                az = max(0, min(HF_SIZE - 1, round(world_z / 24.0)))
                av = az * HF_SIZE + ax
                struct.pack_into("<f", out, s0 + v * 4, area_heights[av])
                nrm = a_s1 + av * 8
                out[s1 + v * 8: s1 + v * 8 + 8] = area_data[nrm: nrm + 8]
                kept += 1
            else:
                struct.pack_into("<f", out, s0 + v * 4, SEA_ISLAND)
                out[s1 + v * 8: s1 + v * 8 + 8] = sea_normal
                patched += 1
    final = struct.unpack_from(f"<{num}f", out, s0)
    log(f"  island heightfield: {patched} cells -> sea, {kept} islet cells kept "
        f"(heights {min(final):.1f}..{max(final):.1f})")
    return bytes(out)


def flatten_area_heightfield(data, keep_rect, margin=192.0):
    """The copied area sheet still holds the SE tip of O'ahu; eden ships no
    mainland sectors, so flatten everything outside the islet rect (+ margin)
    to the area sheet's own sea placeholder, normals following."""
    num, s0, s1 = heightfield_streams(data)
    heights = read_heights(data)
    sea_vertex = next(v for v in range(num) if heights[v] <= SEA_AREA)
    sea_normal = bytes(data[s1 + sea_vertex * 8: s1 + sea_vertex * 8 + 8])
    out = bytearray(data)
    x0, z0, x1, z1 = keep_rect
    x0 -= margin; z0 -= margin; x1 += margin; z1 += margin
    patched = 0
    for j in range(HF_SIZE):
        for i in range(HF_SIZE):
            v = j * HF_SIZE + i
            wx, wz = i * 24.0, j * 24.0
            if x0 <= wx <= x1 and z0 <= wz <= z1:
                continue
            if heights[v] <= SEA_AREA:
                continue
            struct.pack_into("<f", out, s0 + v * 4, SEA_AREA)
            out[s1 + v * 8: s1 + v * 8 + 8] = sea_normal
            patched += 1
    log(f"  area heightfield: {patched} mainland cells -> sea")
    return bytes(out)


# ------------------------------------------------------- the area -o scene

def patch_area_far_objects(data, old_area, new_area, shipped, sink_y=-500.0):
    """Two byte-in-place patches to the area -o.3dd (eden_island_spec.md 2
    fact 5 + the sink rule):

    1. every HASH entry inside INSA containers whose hashcode equals
       GetIdentifier64bit('Sector-<old>-<sx>-<sy>') becomes the hash of
       'Sector-<new>-<sx>-<sy>' - a 3dd node's NAME is its hashcode
       (Render3DD::parse: node.Name = HashToName(node.Hashcode)), and
       EdenWorld::loadArea looks the groups up by SectorName;
    2. instances under the non-shipped sector groups (O'ahu's CITY impostors,
       standing on terrain that is not shipped) get their matrix translation
       Y set to sink_y. OCEAN* tiles stay (they are the sea).

    Returns (data, hashcodes_patched, impostors_sunk)."""
    out = bytearray(data)
    old_to_new = {}
    group_sector = {}
    for sy in range(SECTORS_PER_AREA):
        for sx in range(SECTORS_PER_AREA):
            old_h = identifier64(f"Sector-{old_area}-{sx}-{sy}")
            new_h = identifier64(f"Sector-{new_area}-{sx}-{sy}")
            old_to_new[old_h] = new_h
            group_sector[new_h] = (sx, sy)

    insa_hashes = []  # (offset of the u64, hashcode, INS-record offset)
    obj_names = {}    # OBJA HASH-entry offset -> object name
    hie_nodes = []    # (HIE section offset, instance offset, parent HIE offset)
    for sec_type, hdr, pay, psz, cont in walk_sections(data, T_3DD):
        if sec_type == T_HASH and cont == T_INSA:
            for i in range(psz // 16):
                base = pay + i * 16
                insa_hashes.append((base, struct.unpack_from("<Q", data, base)[0],
                                    struct.unpack_from("<I", data, base + 8)[0]))
        elif sec_type == T_HASH and cont == T_OBJA:
            for i in range(psz // 16):
                base = pay + i * 16
                obj_names[base] = hash_to_name(struct.unpack_from("<Q", data, base)[0])
        elif sec_type == T_HIE:
            hie_nodes.append((hdr, struct.unpack_from("<I", data, pay + 4)[0],
                              struct.unpack_from("<I", data, pay + 12)[0]))

    patched = 0
    for base, old, _ptr in insa_hashes:
        if old in old_to_new:
            struct.pack_into("<Q", out, base, old_to_new[old])
            patched += 1

    inst_hash = {ptr: struct.unpack_from("<Q", out, base)[0] for base, _old, ptr in insa_hashes}
    hie_by_off = {}
    inst_to_hie = {}
    children = {}
    for hie_off, inst, parent in hie_nodes:
        hie_by_off[hie_off] = (inst, parent)
        inst_to_hie[inst] = hie_off
        children.setdefault(parent, []).append(hie_off)

    def descendants(group_hie):
        stack, seen = [group_hie], set()
        while stack:
            h = stack.pop()
            if h in seen:
                continue
            seen.add(h)
            stack.extend(children.get(h, ()))
        return seen

    sunk = 0
    for hie_off, inst, _parent in hie_nodes:
        hashcode = inst_hash.get(inst)
        if hashcode not in group_sector:
            continue
        if group_sector[hashcode] in shipped:
            continue
        for h in descendants(hie_off):
            i_inst, _ = hie_by_off[h]
            if i_inst == 0:
                continue
            ip = i_inst + 0x10  # INS record payload: matrix, ..., numLods @+0x4E, LOD0 @+0x50
            obj_ptr = struct.unpack_from("<I", out, ip + 0x50)[0]
            name = obj_names.get(obj_ptr, "")
            if name and not name.upper().startswith("OCE"):
                struct.pack_into("<f", out, ip + 52, sink_y)  # matrix[13] = translation Y
                sunk += 1
    log(f"  area -o.3dd: {patched} sector-group hashcodes renamed, {sunk} far objects sunk")
    return bytes(out), patched, sunk


# ------------------------------------------------------------- the map (2db)

def decode_dxt1(data):
    """PC .2db DXT1 mip0 -> PIL RGB, via a wrapped DDS (Pillow decodes DXT1)."""
    w, h = struct.unpack_from("<HH", data, 0x28)
    texels = data[0x50: 0x50 + (w * h) // 2]
    hdr = bytearray(128)
    struct.pack_into("<I", hdr, 0, 0x20534444)
    struct.pack_into("<I", hdr, 4, 124)
    struct.pack_into("<I", hdr, 8, 0xA1007)
    struct.pack_into("<I", hdr, 12, h)
    struct.pack_into("<I", hdr, 16, w)
    struct.pack_into("<I", hdr, 20, (w * h) // 2)
    struct.pack_into("<I", hdr, 28, 1)
    struct.pack_into("<I", hdr, 76, 32)
    struct.pack_into("<I", hdr, 80, 0x4)
    hdr[84:88] = b"DXT1"
    struct.pack_into("<I", hdr, 108, 0x1000)
    return Image.open(io.BytesIO(bytes(hdr) + texels)).convert("RGB")


def crop_map_2db(source_2db, crop_px, map_world_w, map_world_h, area_heights, out_name):
    """Crop the source island map to the area's pixel rect, resize to 1024^2,
    re-encode as uncompressed sRGB A8R8G8B8 (format 31, full mip chain). The
    crop is sea-masked by the flattened area heightfield, erasing O'ahu's
    severed coastline from the new island's map: a pixel whose world position
    reads sea on the area sheet and looks like terrain on the retail map gets
    the map's own median sea colour."""
    img = decode_dxt1(source_2db)
    src_w, src_h = img.size
    x0, y0, x1, y1 = crop_px
    crop = img.crop((x0, y0, x1, y1))
    cw, ch = crop.size
    size = 1024
    crop = crop.resize((size, size), Image.LANCZOS)
    px = crop.load()
    metres_per_px_x = map_world_w / src_w
    metres_per_px_z = map_world_h / src_h

    def area_cell(sx, sy):
        wx = (x0 + (sx / size) * (x1 - x0)) * metres_per_px_x
        wz = (y0 + (sy / size) * (y1 - y0)) * metres_per_px_z
        ax = max(0, min(HF_SIZE - 1, round(wx / 24.0)))
        az = max(0, min(HF_SIZE - 1, round(wz / 24.0)))
        return az * HF_SIZE + ax

    sea_samples = []
    for sy in range(0, size, 16):
        for sx in range(0, size, 16):
            if area_heights[area_cell(sx, sy)] <= SEA_AREA:
                sea_samples.append(px[sx, sy])
    if not sea_samples:
        raise SystemExit("map crop: no open-sea pixels in the crop rect")
    sea_samples.sort(key=sum)
    sea = sea_samples[len(sea_samples) // 2]

    masked = 0
    for sy in range(size):
        for sx in range(size):
            if area_heights[area_cell(sx, sy)] <= SEA_AREA:
                r, g, b = px[sx, sy]
                if g > r + 12 and g > b + 12:  # the retail map's green terrain tint
                    px[sx, sy] = sea
                    masked += 1

    payload = bytearray()
    level = crop
    mw = mh = size
    mips = 0
    while True:
        raw = level.tobytes()  # RGB
        row = bytearray(mw * mh * 4)
        for p in range(mw * mh):
            r, g, b = raw[p * 3: p * 3 + 3]
            row[p * 4: p * 4 + 4] = bytes((b, g, r, 255))  # A8R8G8B8 little endian
        payload += row
        mips += 1
        if mw == 1 and mh == 1:
            break
        mw, mh = max(1, mw // 2), max(1, mh // 2)
        level = level.resize((mw, mh), Image.LANCZOS)

    header = bytearray(source_2db[:0x50])
    struct.pack_into("<HH", header, 0x28, size, size)
    struct.pack_into("<BB", header, 0x2E, mips, mips)
    struct.pack_into("<I", header, 0x3C, 31)  # VF_SRGB_A8R8G8B8
    struct.pack_into("<Q", header, 0x20, identifier64(out_name))
    total = 0x50 + len(payload)
    struct.pack_into("<I", header, 0x08, total)
    struct.pack_into("<II", header, 0x18, total - 0x10, total - 0x10)
    log(f"  map: crop {cw}x{ch} px -> {size}^2, {mips} mips, {masked} land pixels masked to sea")
    return bytes(header) + bytes(payload)


# ----------------------------------------------------------------- assembly

def read_bank(path):
    return eden_bank.parse_file(path, eden_bank.PC)


def retitle_entries(bank, renames):
    """build_bank entries with the given name prefixes swapped; renamed .2db
    payloads get their header name hash patched to match."""
    entries = []
    for entry in bank["entries"]:
        data = entry_data(bank, entry)
        new_path = new_name = None
        for old_prefix, new_prefix in renames:
            candidate = rename_entry(entry, old_prefix, new_prefix)
            if candidate:
                new_path = candidate
                new_name = new_prefix + entry["name"][len(old_prefix):]
                break
        if new_path is None:
            new_path, new_name = entry["path"], entry["name"]
        elif entry["ext"] == "2db":
            data = patch_2db_name_hash(data, new_name)
        entries.append({"path": new_path, "data": data,
                        "subtype": entry["subtype"], "group": entry["group"],
                        "date": bank["header"].get("creationDate", 0)})
    return entries


def write_bank(path, entries, creation_date=0):
    data = build_bank(entries, creation_date=creation_date)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(data)
    check = eden_bank.parse_file(path, eden_bank.PC)
    if len(check["entries"]) != len(entries):
        raise SystemExit(f"bank writer self-check failed: {path} has "
                         f"{len(check['entries'])} of {len(entries)} entries")
    log(f"  wrote {path} ({len(entries)} entries, {len(data):,} bytes)")


def main():
    ap = argparse.ArgumentParser(description="bisect the Eden island islet into its own island tree")
    ap.add_argument("--gamedata", required=True, help="the game data root (TestDrive2)")
    ap.add_argument("--userdata", help="the user data folder; writes <userdata>/mods/Euro/Bnk/islands/<name>")
    ap.add_argument("--outdir", help="alternative output root (default: <userdata>/mods)")
    ap.add_argument("--source-island", default="hawai")
    ap.add_argument("--island", default="eden")
    ap.add_argument("--area", default="10-7", help="the source area holding the islet")
    ap.add_argument("--sectors", default="3-1,3-2,4-2", help="the source sectors to ship")
    ap.add_argument("--islet-rect", default="2568,1032,3216,2256",
                    help="islet+companion rect inside the area (x0,z0,x1,z1), metres "
                         "(covers both islets; the crater islet is 2568..3168 x 1032..1680)")
    ap.add_argument("--skip-xtea-selftest", action="store_true")
    args = ap.parse_args()

    if Image is None:
        raise SystemExit("Pillow is required (map crop): pip install pillow")

    ax, ay = (int(v) for v in args.area.split("-"))
    new_area = "0-0"
    new_ax, new_ay = 0, 0
    shipped = set()
    for token in args.sectors.split(","):
        sx, sy = (int(v) for v in token.split("-"))
        shipped.add((sx, sy))
    islet_rect = tuple(float(v) for v in args.islet_rect.split(","))

    src_level = os.path.join(args.gamedata, "Euro", "Bnk", "islands", args.source_island, "level")
    src_root = os.path.join(args.gamedata, "Euro", "Bnk", "islands", args.source_island)
    out_root = os.path.join(args.outdir or os.path.join(args.userdata, "mods"),
                            "Euro", "Bnk", "islands", args.island)
    out_area_dir = os.path.join(out_root, "level", f"area-{new_ax}", f"area-{new_area}")

    if not args.skip_xtea_selftest:
        selftest_xtea(args.gamedata, args.source_island)
    columns, rows = read_worldconfig(args.gamedata, args.source_island)

    # ---- the area bank ----------------------------------------------------
    area_dir = os.path.join(src_level, f"area-{ax}", f"area-{ax}-{ay}")
    area_bank_path = os.path.join(area_dir, f"area-{ax}-{ay}.bnk")
    log(f"source area: {area_bank_path}")
    area_bank = read_bank(area_bank_path)
    area_3dg_entry = next(e for e in area_bank["entries"]
                          if e["name"] == f"area-{ax}-{ay}" and e["ext"] == "3dg")
    area_3dg_flat = flatten_area_heightfield(entry_data(area_bank, area_3dg_entry), islet_rect)
    area_heights = read_heights(area_3dg_flat)

    area_entries = retitle_entries(area_bank, [(f"area-{ax}-{ay}", f"area-{new_area}")])
    for entry in area_entries:
        filename = entry["path"].rpartition("\\")[2].lower()
        if filename == f"area-{new_area}.3dg":
            entry["data"] = area_3dg_flat
        elif filename == f"area-{new_area}-o.3dd":
            entry["data"], _p, _s = patch_area_far_objects(
                entry["data"], f"{ax}-{ay}", new_area, shipped)
    write_bank(os.path.join(out_area_dir, f"area-{new_area}.bnk"), area_entries,
               area_bank["header"].get("creationDate", 0))

    # ---- the sector banks -------------------------------------------------
    for token in args.sectors.split(","):
        sx, sy = (int(v) for v in token.split("-"))
        src_name = f"sector-{ax}-{ay}-{sx}-{sy}"
        new_name = f"sector-{new_area}-{sx}-{sy}"
        sector_path = os.path.join(area_dir, f"{src_name}.bnk")
        if not os.path.exists(sector_path):
            log(f"  sector {src_name}: not present (pure ocean), skipped")
            continue
        sector_bank = read_bank(sector_path)
        sector_entries = retitle_entries(sector_bank, [(src_name, new_name)])
        write_bank(os.path.join(out_area_dir, f"{new_name}.bnk"), sector_entries,
                   sector_bank["header"].get("creationDate", 0))

    # ---- the island bank --------------------------------------------------
    island_bank_path = os.path.join(src_level, f"{args.source_island}.bnk")
    log(f"source island bank: {island_bank_path}")
    island_bank = read_bank(island_bank_path)
    old_island = args.source_island
    new_island = args.island

    island_entries = retitle_entries(island_bank, [
        (f"{old_island}-o", f"{new_island}-o"),
        (old_island, new_island),
    ])
    # no road IA files: the islet has no graph; its baked road still drives
    island_entries = [e for e in island_entries
                      if not e["path"].rpartition("\\")[2].lower().endswith(".ia")]
    for entry in island_entries:
        filename = entry["path"].rpartition("\\")[2].lower()
        if filename == f"{new_island}.3dg":
            entry["data"] = patch_island_heightfield(entry["data"], area_3dg_flat, islet_rect)
        elif filename == f"{new_island}-c.2db":
            source_entry = next(e for e in island_bank["entries"]
                                if e["ext"] == "2db" and e["name"].lower() == f"{old_island}-c")
            map_world_w = columns * AREA_SIZE
            map_world_h = rows * AREA_SIZE
            map_px = 1024  # the source map texture is 1024^2
            crop = (round(ax * AREA_SIZE / map_world_w * map_px),
                    round(ay * AREA_SIZE / map_world_h * map_px),
                    round((ax + 1) * AREA_SIZE / map_world_w * map_px),
                    round((ay + 1) * AREA_SIZE / map_world_h * map_px))
            entry["data"] = crop_map_2db(entry_data(island_bank, source_entry), crop,
                                         map_world_w, map_world_h, area_heights, f"{new_island}-c")
    write_bank(os.path.join(out_root, "level", f"{new_island}.bnk"), island_entries,
               island_bank["header"].get("creationDate", 0))

    # ---- verbatim support files -------------------------------------------
    for src, dst in [
        (os.path.join(src_level, "commonworld.bnk"),
         os.path.join(out_root, "level", "commonworld.bnk")),
        (os.path.join(src_root, "scenarics", "packs", "worldactors.bnk"),
         os.path.join(out_root, "scenarics", "packs", "worldactors.bnk")),
        (os.path.join(src_root, "fx", "game_weathers.xmb"),
         os.path.join(out_root, "fx", "game_weathers.xmb")),
    ]:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        log(f"  copied {dst} ({os.path.getsize(dst):,} bytes)")

    # ---- worldconfig.cpr ---------------------------------------------------
    # the retail shape (hawai's decrypted worldconfig), sized 1 x 1: the port
    # reads only NbColumns/NbRows (eden_world.cpp:727-754) but the zoom /
    # centre rows keep the map screen's retail feel
    config = (
        '<?xml version="1.0" encoding="iso-8859-1"?>\r\n'
        f'<WorldConfig version="1" level="{new_island.capitalize()}">\r\n'
        '  <Area NbColumns="1" NbRows="1" />\r\n'
        '  <MapRotation>0</MapRotation>\r\n'
        '  <MapInclination Zoom="Island">0</MapInclination>\r\n'
        '  <MapInclination Zoom="Far">20</MapInclination>\r\n'
        '  <MapInclination Zoom="Medium">30</MapInclination>\r\n'
        '  <MapInclination Zoom="Close">40</MapInclination>\r\n'
        '  <MapInclination Zoom="Very Close">50</MapInclination>\r\n'
        '  <MapDistanceAddedToZoom Zoom="Island" Screen="4/3">60000</MapDistanceAddedToZoom>\r\n'
        '  <MapDistanceAddedToZoom Zoom="Island" Screen="16/9">35000</MapDistanceAddedToZoom>\r\n'
        '  <MapCenterCorrection Screen="4/3">\r\n    <X>1000</X>\r\n    <Z>-1500</Z>\r\n  </MapCenterCorrection>\r\n'
        '  <MapCenterCorrection Screen="16/9">\r\n    <X>1000</X>\r\n    <Z>-600</Z>\r\n  </MapCenterCorrection>\r\n'
        '</WorldConfig>'
    ).encode("ascii")
    out_cfg = os.path.join(out_root, "level", "worldconfig.cpr")
    os.makedirs(os.path.dirname(out_cfg), exist_ok=True)
    open(out_cfg, "wb").write(encrypt_cpr(config))
    log(f"  wrote {out_cfg} ({config.decode()!r} encrypted)")

    cx = (islet_rect[0] + islet_rect[2]) / 2
    cz = (islet_rect[1] + islet_rect[3]) / 2
    log(f"\ndone. Drive it:  <exe> -game {args.gamedata} -userdata {args.userdata} "
        f"-gm freeride -island {new_island} -skipcine -tp {cx:.0f},{cz:.0f}")
    log(f"the islet fills area {new_area} around world ({cx:.0f}, {cz:.0f}); "
        f"the crater peak is ~109 m above the -1.8 m sea.")


if __name__ == "__main__":
    main()

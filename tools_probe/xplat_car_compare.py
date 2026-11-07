"""Cross-platform car asset comparison (PC vs Xbox 360 prototype vs PS3).

One car bank and everything it carries: the asset list, the geometry (primitives,
vertices, indices and the *real* triangle count), the materials and the textures -
measured on all three platforms and printed side by side.

    python tools_probe/xplat_car_compare.py --car california
    python tools_probe/xplat_car_compare.py --car 458_italia --interior
    python tools_probe/xplat_car_compare.py --selftest

Where the data comes from (`[DATA]`, all read-only):

    PC   <pc>/Euro/Bnk/Vehicules/{high,interiors}/<car>.bnk - little endian,
         payloads stored plain, the engine reads these directly.
    360  the Aug 2011 / Dec 2010 prototype bigfiles (bigfileN.map + bigfileN.big).
         A payload is LZX-compressed as a chain of blocks; `xzxd.exe` (the 360
         recomp's tool) decodes it. Big-endian `BANK` container.
    PS3  <ps3>/bigfileN.{big,map} - the same big-endian `BANK` container, but the
         payload is `[plain bank metadata][144-byte chunk header ('HDR!')][64 KB
         chunks]`; only the chunks the packer stored verbatim are readable as-is
         (docs/re/xplat_car_compare.md). So the PS3's asset list, per-asset sizes
         and the geometry *counts* are readable today; the chunked payload bytes
         need the chunk decompressor.

The readers it orchestrates live outside this repo: `eden_bank.py` (GLM's
`tools/x360`) and `tdu2_bigfile.py` + `xzxd.exe` (the 360 recomp's tools). Both
locations are CLI-overridable, and nothing is written outside `--work`.
"""
import argparse
import importlib.util
import os
import struct
import subprocess
import sys

FOURCC_HASH = 0x48534148
FOURCC_GEOM = 0x4D4F4547
FOURCC_PRIM = 0x4D495250
FOURCC_3DG = 0x4744332E

DEFAULT_X360_TOOLS = r"C:\OpenTDU2-glm\tools\x360"
DEFAULT_RECOMP_TOOLS = (r"E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump"
                        r"\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools")
DEFAULT_PC = r"C:\TDU2-Gamedata\TestDrive2"
DEFAULT_PS3 = r"C:\TDU2-PS3-Recomp\game\USRDIR"
DEFAULT_360 = (r"C:\Users\cuz_c\Downloads"
               r"\Test_Drive_Unlimited_2_(Aug_8,_2011_prototype)")

SOURCES = ("pc", "360", "ps3")
PATHS = {}
TOOLS = {}


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def platform_order(platform):
    return "<" if platform == "pc" else ">"


def hash_name(value):
    """The 8-slot identifier as text (lossy past 8 characters, as the format is)."""
    out = ""
    for i in range(8):
        c = (value >> (i * 8)) & 0xFF
        if c == 0:
            break
        out += chr(c) if 32 <= c < 127 else "?"
    return out


def sections(data, order, magic):
    """Eden section tree walk: yields (type, header, payload, payload_size, depth)."""
    def visit(begin, end, depth):
        offset = begin
        while offset + 0x10 <= end:
            size = struct.unpack_from(order + "I", data, offset + 8)[0]
            if size < 0x10 or offset + size > end:
                return
            data_size = max(struct.unpack_from(order + "I", data, offset + 12)[0], 0x10)
            yield (struct.unpack_from(order + "I", data, offset)[0], offset,
                   offset + 0x10, size - 0x10, depth)
            if size > data_size:
                for section in visit(offset + data_size, offset + size, depth + 1):
                    yield section
            offset += size

    if len(data) < 0x10 or struct.unpack_from(order + "I", data, 0x0C)[0] != magic:
        return
    file_size = min(struct.unpack_from(order + "I", data, 0x08)[0], len(data))
    for section in visit(0x10, file_size, 0):
        yield section


def geometry(blob, order, read_indices=True):
    """Per geometry: [prims, vertices, indices, triangles, restarts, skipped].

    A primitive header is 0x40 bytes: +0x00 index-block offset (absolute in the
    .3dg), +0x04 vertex-block offset, +0x10 material index, +0x12 primitive type
    (a byte pair: 4 = triangle strip on the PC/360, 3 = triangle list on the
    PS3), +0x1C numVertex, +0x28 numIndex. The index block it points at starts
    with a 0x20-byte `DXIB` header (numIndex at +0x10) then u16 indices; the 360
    breaks strips with 0xFFFF, the PC joins them with degenerate triangles - so
    the real triangle count is counted by walking the indices and skipping both,
    never by dividing. `read_indices=False` keeps the prim/vertex/index *counts*
    without touching the index buffers (what the PS3 needs while its chunked
    payloads are undecoded).
    """
    name_by_offset = {}
    for stype, _h, poff, psize, _d in sections(blob, order, FOURCC_3DG):
        if stype == FOURCC_HASH:
            for i in range(psize // 16):
                value = struct.unpack_from(order + "Q", blob, poff + i * 16)[0]
                name_by_offset[struct.unpack_from(order + "I", blob, poff + i * 16 + 8)[0]] = hash_name(value)

    result = {}
    current = None
    for stype, hoff, poff, psize, _d in sections(blob, order, FOURCC_3DG):
        if stype == FOURCC_GEOM:
            current = name_by_offset.get(hoff)
            result.setdefault(current, [0, 0, 0, 0, 0, 0])
        elif stype == FOURCC_PRIM and current is not None and psize >= 0x40:
            ib = struct.unpack_from(order + "I", blob, poff + 0x00)[0]
            nv = struct.unpack_from(order + "I", blob, poff + 0x1C)[0]
            ni = struct.unpack_from(order + "I", blob, poff + 0x28)[0]
            ptype = blob[poff + 0x12]           # 4 = triangle strip, 3 = triangle list
            row = result[current]
            row[0] += 1
            row[1] += nv
            row[2] += ni
            if read_indices and ib and ib + 0x20 + ni * 2 <= len(blob):
                indices = struct.unpack_from(order + "%dH" % ni, blob, ib + 0x20)
                row[4] += indices.count(0xFFFF)
                step = 3 if ptype == 3 else 1
                for i in range(0, ni - 2, step):
                    a, b, c = indices[i], indices[i + 1], indices[i + 2]
                    if a == 0xFFFF or b == 0xFFFF or c == 0xFFFF or a == b or b == c or a == c:
                        row[5] += 1
                    else:
                        row[3] += 1
            elif ptype == 3:
                # a list needs no index buffer walk: every three indices are a triangle
                row[3] += ni // 3
    return result


def totals(geoms):
    out = [0] * 6
    for row in geoms.values():
        for i in range(6):
            out[i] += row[i]
    return out


def extract(source, name):
    """(entry, payload bytes) for `name` in a source's bigfiles, or (None, None)."""
    folder = PATHS[source]
    wanted = BIGFILE.big_hash(name)
    for index in range(4):
        map_path = os.path.join(folder, "bigfile%d.map" % index)
        if not os.path.exists(map_path):
            continue
        _header, entries = BIGFILE.parse_map(map_path)
        hit = next((e for e in entries if e["hash"] == wanted), None)
        if hit is None:
            continue
        with open(os.path.join(folder, "bigfile%d.big" % index), "rb") as handle:
            handle.seek(hit["offset"])
            payload = handle.read(hit["comp"] or hit["uncomp"])
        return hit, payload
    return None, None


def decode_360(payload, uncompressed):
    """A 360 payload's LZX chain -> the plain bank (xzxd.exe, the recomp's tool)."""
    raw = os.path.join(PATHS["work"], "_xplat_360.raw")
    out = os.path.join(PATHS["work"], "_xplat_360.bnk")
    with open(raw, "wb") as handle:
        handle.write(payload)
    result = subprocess.run([os.path.join(TOOLS["recomp"], "xzxd.exe"), raw, out,
                             str(uncompressed)], capture_output=True)
    if result.returncode != 0:
        raise SystemExit("xzxd failed: %s" % result.stderr.decode(errors="replace")[:200])
    with open(out, "rb") as handle:
        return handle.read()


def ps3_layout(payload):
    """A PS3 payload is `[plain bank metadata][144-byte 'HDR!' chunk header][chunks]`.

    Returns {"plain": True} for a payload the packer stored whole (comp == uncomp),
    else the header fields, the per-chunk stored sizes and the shift a verbatim
    payload sits at (the chunk header is not part of the bank image).
    """
    order = ">"
    header_at = struct.unpack_from(order + "I", payload, 0x44)[0]   # the data section
    if header_at + 16 > len(payload):
        return {"plain": True}
    header_size, chunk_size, field = struct.unpack_from(order + "III", payload, header_at)
    magic = payload[header_at + 12:header_at + 16]
    if magic not in (b"!RDH", b"HDR!") or not 0x20 <= header_size <= 0x1000:
        return {"plain": True}
    sizes = []
    offset = header_at + 16
    while offset + 4 <= header_at + header_size - 4:
        sizes.append(struct.unpack_from(order + "I", payload, offset)[0])
        offset += 4
    return {"plain": False, "header_at": header_at, "header_size": header_size,
            "chunk_size": chunk_size, "field": field, "magic": magic,
            "sizes": sizes, "data_at": header_at + header_size}


MAGIC_KINDS = {0x4444332E: "3dd", 0x4744332E: "3dg", 0x4D44322E: "2dm", 0x4244322E: "2db"}


def measure(bank, platform, verbatim_shift=0, image_size=None, counts_only=False):
    """Everything one bank carries on one platform (via GLM's eden_bank reader).

    `verbatim_shift` is the PS3's chunk header size: on that platform a payload
    the packer stored verbatim sits `header_size` bytes further along than the
    bank image says, and a payload inside a compressed chunk is not readable at
    all - so every entry is validated by its own fourcc before it is used.
    `image_size` is the bank image the entry table describes (the PS3 payload is
    shorter than that, and eden_bank drops entries that do not fit).
    """
    order = platform_order(platform)
    image_size = image_size or len(bank)
    padded = bank if image_size <= len(bank) else bank + bytes(image_size - len(bank))
    parsed = EDEN_BANK.parse(padded, EDEN_BANK.PC if platform == "pc" else EDEN_BANK.X360)
    result = {"platform": platform, "size": len(bank), "image_size": image_size,
              "entries": parsed["entries"], "geoms": {}, "textures": [],
              "available": 0, "counts_only": counts_only}
    for entry in parsed["entries"]:
        at = entry["offset"] + verbatim_shift
        if not entry["offset"] or at + 0x10 > len(bank):
            continue
        blob = bank[at:at + min(entry["size"], len(bank) - at)]
        kind = MAGIC_KINDS.get(struct.unpack_from(order + "I", blob, 0x0C)[0])
        if kind is None:
            continue        # inside a compressed chunk (or past the end)
        result["available"] += 1
        if kind == "3dg":
            result["geoms"].update(geometry(blob, order, read_indices=not counts_only))
        elif kind == "2db":
            w, h = struct.unpack_from(order + "HH", blob, 0x28)
            result["textures"].append((entry["filename"], w, h, entry["size"]))
    return result


def report(result, title):
    print("\n== %s [%s]  %d bytes" % (title, result["platform"], result["size"]))
    print("   %d assets, %d payloads the tool understands (3dd/3dg/2dm/2db)" % (
        len(result["entries"]), result["available"]))
    geoms = result["geoms"]
    if geoms:
        prims, verts, indices, tris, restarts, skipped = totals(geoms)
        if result["counts_only"]:
            print("   geometry: %d primitives, %d vertices, %d indices -> %d triangles" % (
                prims, verts, indices, tris))
            print("             (the PS3's primitives are triangle lists, so the count comes")
            print("              from the primitive headers; the index buffers themselves are")
            print("              inside compressed chunks)")
        else:
            print("   geometry: %d primitives, %d vertices, %d indices -> %d real triangles" % (
                prims, verts, indices, tris))
            print("             (%d strip restarts, %d degenerate/restart indices skipped)" % (
                restarts, skipped))
        for name, row in sorted(geoms.items(), key=lambda kv: -kv[1][2])[:5]:
            print("        %-16s %3d prims %7d verts %7d idx %7d tris" % (
                name, row[0], row[1], row[2], row[3]))
    else:
        print("   geometry: payload chunks not decoded (counts only, see below)")
    if result["textures"]:
        print("   textures: %d, %d bytes" % (len(result["textures"]),
                                             sum(t[3] for t in result["textures"])))
        for name, w, h, size in result["textures"]:
            print("        %-28s %5dx%-5d %8d B" % (name, w, h, size))
    else:
        print("   textures: %d (sizes only, no decodable payload)" % sum(
            1 for e in result["entries"] if e["ext"] == "2db"))
        for entry in sorted((e for e in result["entries"] if e["ext"] == "2db"),
                            key=lambda e: e["offset"]):
            print("        %-28s %8d B" % (entry["filename"], entry["size"]))
    return result


def compare_car(car, interior):
    names = [("body", "euro/bnk/vehicules/high/%s.bnk" % car)]
    if interior:
        names.append(("interior", "euro/bnk/vehicules/interiors/%s_i.bnk" % car))
    for label, name in names:
        print("\n" + "=" * 78)
        print("%s: %s" % (label, name))
        print("=" * 78)
        for source in SOURCES:
            if source == "pc":
                path = os.path.join(PATHS["pc"], name.replace("/", os.sep))
                if not os.path.exists(path):
                    print("\n== pc: %s missing" % path)
                    continue
                with open(path, "rb") as handle:
                    report(measure(handle.read(), "pc"), os.path.basename(path))
                continue
            entry, payload = extract(source, name)
            if entry is None:
                print("\n== %s: not in this archive" % source)
                continue
            print("\n== %s  stored %d B, uncompressed %d B" % (
                source, entry["comp"], entry["uncomp"]))
            if source == "360":
                if entry["comp"] != entry["uncomp"]:
                    payload = decode_360(payload, entry["uncomp"])
                report(measure(payload, "360"), "360 (LZX-decoded)")
                continue
            layout = ps3_layout(payload)
            if layout.get("plain"):
                print("   the packer stored this payload whole (no chunk header)")
                report(measure(payload, "360"), "ps3 (plain)")
            else:
                padding = entry["comp"] - layout["data_at"] - sum(layout["sizes"])
                print("   container: %d-byte '%s' chunk header at %d, chunk size %d, %d chunks"
                      % (layout["header_size"], layout["magic"].decode("latin-1"),
                         layout["header_at"], layout["chunk_size"], len(layout["sizes"])))
                print("              %d B of chunks + %d B padding, header field %#x"
                      % (sum(layout["sizes"]), padding, layout["field"]))
                print("   readable here: the plain bank metadata and every payload the packer")
                print("   stored verbatim (the .3dd, the .3dg's GEOA -> the prim/vertex/index")
                print("   counts). The index buffers, textures and materials are inside")
                print("   compressed chunks - the PS3 chunk decompressor is not decoded yet.")
                report(measure(payload, "360", layout["header_size"],
                               image_size=entry["uncomp"], counts_only=True),
                       "ps3 (metadata + verbatim chunks)")


def selftest():
    """No game data: the identifier text, the section walk and the triangle count."""
    print("xplat_car_compare selftest")
    assert hash_name(0x0032544E495F3751) == "Q7_INT2", hash_name(0x0032544E495F3751)
    print("  identifier -> text        ok ('Q7_INT2')")

    indices = struct.pack("<6H", 0, 1, 2, 2, 3, 4)
    index_block = b"DXIB" + b"\0" * 12 + struct.pack("<I", 6) + b"\0" * 12 + indices
    prim = bytearray(0x40)
    struct.pack_into("<I", prim, 0x00, 0x200)   # index block, absolute in the .3dg
    struct.pack_into("<H", prim, 0x12, 4)       # triangle strip
    struct.pack_into("<I", prim, 0x1C, 5)       # 5 vertices
    struct.pack_into("<I", prim, 0x28, 6)       # 6 indices -> 2 triangles, 2 degenerate
    body = bytearray(0x400)
    body[0x00:0x10] = struct.pack("<HHIII", 3, 4, 0, 0x300, FOURCC_3DG)
    body[0x10:0x20] = struct.pack("<IIII", FOURCC_HASH, 0, 0x20, 0x10)
    body[0x20:0x30] = struct.pack("<QII", 0x00524C5F59444F42, 0x30, 0)   # 'BODY_LR'
    body[0x30:0x40] = struct.pack("<IIII", FOURCC_GEOM, 0, 0x70, 0x10)
    body[0x40:0x50] = struct.pack("<IIII", FOURCC_PRIM, 0, 0x50, 0x40)
    body[0x50:0x90] = bytes(prim)
    body[0x200:0x200 + len(index_block)] = index_block
    stats = geometry(bytes(body), "<")
    assert stats == {"BODY_LR": [1, 5, 6, 2, 0, 2]}, stats
    print("  geometry walk             ok (1 prim, 5 verts, 6 indices -> 2 triangles, 2 skipped)")
    print("  selftest passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--car", default="california", help="car bank name (default california)")
    parser.add_argument("--interior", action="store_true", help="also compare <car>_i.bnk")
    parser.add_argument("--pc", default=DEFAULT_PC)
    parser.add_argument("--ps3", default=DEFAULT_PS3)
    parser.add_argument("--x360", default=DEFAULT_360)
    parser.add_argument("--x360-tools", default=DEFAULT_X360_TOOLS)
    parser.add_argument("--recomp-tools", default=DEFAULT_RECOMP_TOOLS)
    parser.add_argument("--work", default=os.environ.get("TEMP", "."))
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    global PATHS, TOOLS, BIGFILE, EDEN_BANK
    PATHS = {"pc": args.pc, "360": args.x360, "ps3": args.ps3, "work": args.work}
    TOOLS = {"x360": args.x360_tools, "recomp": args.recomp_tools}
    if args.selftest:
        selftest()
        return 0
    for label, path in (("recomp tools", os.path.join(args.recomp_tools, "tdu2_bigfile.py")),
                        ("x360 tools", os.path.join(args.x360_tools, "eden_bank.py"))):
        if not os.path.exists(path):
            raise SystemExit("%s not found: %s (use --recomp-tools / --x360-tools)" % (label, path))
    BIGFILE = load_module("tdu2_bigfile", os.path.join(args.recomp_tools, "tdu2_bigfile.py"))
    EDEN_BANK = load_module("eden_bank", os.path.join(args.x360_tools, "eden_bank.py"))
    compare_car(args.car, args.interior)
    return 0


BIGFILE = None
EDEN_BANK = None

if __name__ == "__main__":
    sys.exit(main())





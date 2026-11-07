"""Assemble the cut Audi Q7 Inuit interior as a playable PC bank.

The Xbox 360 prototypes of TDU2 (Dec 2010 + Aug 2011, byte-identical) carry the
real Inuit interior - the white-leather cabin with the walnut `WOOD_INUIT`
trim and grey `Q7_INT2` dials (a player saw it in the Dec 2010 build; see
`docs/re/CROSSBUILD_PLAN.md` and C:\\TDU2-Proto\\q7_inuit_beta). Retail
`q7_inuit_i.bnk` was repurposed as the Q7 V12's cabin: same geometry part
names, none of the Inuit materials.

This script builds the hybrid bank the CROSSBUILD_PLAN prescribed: the
prototype's scene and materials (.3dd / .2dm, byte-swapped to the PC layout by
`eden_render_file.swap_to_pc`) over the retail PC geometry (.3dg) and the
shared textures, plus the two prototype-only textures decoded to PC .2db
(grey dials, wood trim - mip 0, sRGB formats so the retail shaders sample them
as they sample the retail ones). No prototype geometry is used: the packed
360 .3dg vertex format is not decoded (CROSSBUILD_PLAN "Not ported").

The result loads through the mod overlay - copy nothing into the game data:

    python -I make_q7_inuit.py <userdata>            # -> <userdata>/mods/Euro/Bnk/...
    python -I make_q7_inuit.py <userdata> --out f    # write to a file instead
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_bank
import eden_bank
import eden_render_file
from eden_2dm import get_identifier_64bit, parse as parse_2dm
from restore_texture import untile_mip0

_PROTO_BANK = r"C:\TDU2-Proto\q7_inuit_beta\q7_inuit_i_xbox360_dec2010.bnk"
_RETAIL_BANK = r"C:\TDU2-Gamedata\TestDrive2\Euro\Bnk\vehicules\interiors\q7_inuit_i.bnk"

# The retail .2db used as the 0x50-byte header template (512x512 sRGB DXT5).
_TEMPLATE = "q7_int2.2db"
# sRGB DXT1: the retail enums are the proto's + the sRGB offset (23 -> 51) -
# the retail shaders sample sRGB, so the hybrid textures keep retail's formats.
_SRGB_DXT1 = 51

# The geometry is retail's, so its primitive material indices are retail's -
# the scene keeps the retail bindings and the Inuit materials are substituted
# by role: the V12's carbon trim becomes wood, its dark paint panels become
# the Inuit white, its black leather becomes the Inuit light leather. Every
# target exists in the prototype's .2dm (verified by the self-check below).
MATERIAL_MAP = {
    "Carbon": "wood",
    "Paint": "inuit",
    "C1_L": "leather",
    "C2_L": "leather",
    "A_C3_Mo": "moquette",
    "Roof": "moquette",
    "A_C1_Al": "aluminium",
    "A_C1_P": "plastic_white",
    "Dot": "plastic",
    "Plastic_Bruit": "plastic",
}


def _build_pc_2db(template_bytes, pixels, width, height, fmt, hashcode):
    """A PC .2db with a cloned retail header, patched for the given texture
    (restore_texture.build_pc_2db generalized: dimensions, format and name)."""
    tpl = bytearray(template_bytes[:0x50])
    struct.pack_into("<Q", tpl, 0x20, hashcode)
    struct.pack_into("<HHH", tpl, 0x28, width, height, 1)  # Width, Height, Depth
    tpl[0x2E] = 1  # NumMipmap: the 360 mip tail is not decoded
    tpl[0x2F] = 1  # NumPackedMipmap
    struct.pack_into("<I", tpl, 0x3C, fmt)
    total = 0x50 + len(pixels)
    struct.pack_into("<I", tpl, 0x08, total)
    struct.pack_into("<II", tpl, 0x18, total - 0x10, total - 0x10)
    return bytes(tpl) + pixels


def make(userdata=None, out_path=None):
    proto = eden_bank.parse_file(_PROTO_BANK, eden_bank.X360)
    retail = eden_bank.parse_file(_RETAIL_BANK, eden_bank.PC)
    proto_entry = {e["filename"]: e for e in proto["entries"]}
    retail_entry = {e["filename"]: e for e in retail["entries"]}

    def payload(bank, entry):
        return bank["data"][entry["offset"]:entry["offset"] + entry["size"]]

    # 1. The prototype's materials, byte-swapped to the PC layout, and the
    #    retail scene with its material banks re-pointed at the Inuit set.
    materials = bytearray(payload(proto, proto_entry["q7_inuit_i.2dm"]))
    eden_render_file.swap_to_pc(materials, eden_render_file.FOURCC_2DM)
    scene = bytearray(payload(retail, retail_entry["q7_inuit_i.3dd"]))
    _substitute_scene_materials(scene, materials)

    # 2. The two prototype-only textures as PC .2db (mip 0, retail formats).
    template = payload(retail, retail_entry[_TEMPLATE])
    wood_hash = get_identifier_64bit("wood_inuit")
    w, h, block, wood_mip0 = untile_mip0(bytes(payload(proto, proto_entry["wood_inuit.2db"])))
    wood_2db = _build_pc_2db(template, wood_mip0, w, h, _SRGB_DXT1, wood_hash)
    w2, h2, block2, dials_mip0 = untile_mip0(bytes(payload(proto, proto_entry["q7_int2.2db"])))
    tw, th = struct.unpack_from("<HH", template, 0x28)
    if (w2, h2) != (tw, th):
        raise SystemExit("proto q7_int2 is %dx%d, the template %dx%d" % (w2, h2, tw, th))
    dials_2db = _build_pc_2db(template, dials_mip0, w2, h2,
                              struct.unpack_from("<I", template, 0x3C)[0],
                              get_identifier_64bit("q7_int2"))

    # 3. The entries: prototype scene/materials/textures over retail geometry.
    # Paths (authoring metadata) stay retail's - a PC bank; the wood texture
    # gets the Inuit maps folder the prototype itself used.
    kinds = {"q7_inuit_i.3dd": scene,
             "q7_inuit_i.2dm": materials,
             "q7_inuit_i.3dg": payload(retail, retail_entry["q7_inuit_i.3dg"]),
             "q7_int.2db": payload(retail, retail_entry["q7_int.2db"]),
             "q7_int_ts.2db": payload(retail, retail_entry["q7_int_ts.2db"]),
             "q7_glass_i.2db": payload(retail, retail_entry["q7_glass_i.2db"]),
             "q7_int2.2db": dials_2db,
             "wood_inuit.2db": wood_2db}
    wood_path = retail_entry["q7_inuit_i.3dd"]["path"].replace(
        "q7_inuit_i.3dd", "maps\\wood_inuit.2db")
    entries = []
    for filename, data in kinds.items():
        source = retail_entry[filename] if filename in retail_entry else None
        path = source["path"] if source else wood_path
        subtype, group = (source["subtype"], source["group"]) if source else \
            (retail_entry[_TEMPLATE]["subtype"], retail_entry[_TEMPLATE]["group"])
        entries.append({"path": path, "data": data, "subtype": subtype, "group": group})

    # 4. The retail directory order, the wood texture added to the maps folder.
    directory_order = [eden_bank.decode_bank_path(encoded)
                       for encoded in build_bank.eden_bank_directory_order(retail)]
    directory_order.append(wood_path)

    bank = build_bank.build_bank(entries, creation_date=retail["header"]["creationDate"],
                                 bank_type=retail["header"]["type"],
                                 directory_order=directory_order)

    # 5. Self-checks: the bank parses; the materials bind; the scene stays
    #    inside the material set.
    parsed = eden_bank.parse(bank, eden_bank.PC)
    got = {e["filename"]: (e["kind"], e["size"]) for e in parsed["entries"]}
    assert len(parsed["entries"]) == 8, got
    assert got["wood_inuit.2db"][0] == "2db" and got["q7_inuit_i.3dd"][0] == "3dd", got
    mats = parse_2dm(materials, "pc")
    by_hash = {m["hash"]: m for m in mats}
    wood = by_hash.get(get_identifier_64bit("wood"))
    assert wood is not None, "the swapped .2dm has no Wood material"
    detail = [s for s in wood["slots"] if s["slot_type_name"] == "DETAIL"]
    assert detail and detail[0]["texture_hash"] == wood_hash, \
        "Wood DETAIL does not bind wood_inuit (%r)" % detail
    for offset, material_hash in _scene_material_entries(scene):
        assert material_hash in by_hash, \
            "scene references material %016x not in the .2dm" % material_hash

    # 6. Write it - into the mod overlay unless --out.
    if out_path is None:
        out_path = os.path.join(userdata, "mods", "Euro", "Bnk", "vehicules",
                                "interiors", "q7_inuit_i.bnk")
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as handle:
        handle.write(bank)
    print("q7 inuit hybrid bank: %d entries, %d bytes -> %s" % (
        len(entries), len(bank), out_path))
    print("  wood_inuit %dx%d DXT1 (sRGB %d) %d bytes, grey dials %dx%d, Inuit materials on retail geometry" % (
        w, h, _SRGB_DXT1, len(wood_mip0), w2, h2))
    return out_path


def _substitute_scene_materials(scene, materials):
    """Rewrite the retail scene's MBNK material hashes through MATERIAL_MAP,
    in place. The prototype's .2dm must end up covering every reference."""
    by_name = {m["name"].lower(): m["hash"] for m in parse_2dm(materials, "pc")}
    remap = {}
    for retail_name, proto_name in MATERIAL_MAP.items():
        proto_hash = by_name.get(proto_name)
        assert proto_hash is not None, "the proto .2dm has no %r material" % proto_name
        remap[get_identifier_64bit(retail_name)] = proto_hash
    covered = {m["hash"] for m in parse_2dm(materials, "pc")}
    patched = 0
    for offset, material_hash in _scene_material_entries(scene):
        if material_hash in remap:
            struct.pack_into("<Q", scene, offset, remap[material_hash])
            patched += 1
        else:
            assert material_hash in covered, \
                "scene material %016x is in neither the proto .2dm nor the map" % material_hash
    return patched


def _scene_material_entries(scene):
    """Every MBNK material hashcode as (file offset, u64)."""
    import eden_3dd

    entries = []
    for section_type, _header, payload_offset, payload_size, _depth in eden_3dd._sections(scene, "pc"):
        if section_type != eden_3dd.FOURCC_MBNK:
            continue
        for i in range(payload_size // 16):
            entries.append((payload_offset + i * 16,
                            struct.unpack_from("<Q", scene, payload_offset + i * 16)[0]))
    return entries
    """The MBNK material hash lists of every node, as raw u64s."""
    import eden_3dd

    hashes = []
    mbnk_offsets = []
    for section_type, _header, payload_offset, payload_size, _depth in eden_3dd._sections(scene, "pc"):
        if section_type == eden_3dd.FOURCC_MBNK:
            mbnk_offsets.append((payload_offset, payload_size))
    for payload_offset, payload_size in mbnk_offsets:
        count = payload_size // 16
        hashes.append([struct.unpack_from("<Q", scene, payload_offset + i * 16)[0]
                       for i in range(count)])
    return hashes


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[2] == "--out":
        make(out_path=sys.argv[3])
    elif len(sys.argv) >= 2:
        make(userdata=sys.argv[1])
    else:
        raise SystemExit("usage: make_q7_inuit.py <userdata> [--out <file.bnk>]")

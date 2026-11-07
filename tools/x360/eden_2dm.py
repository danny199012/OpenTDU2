"""TDU2 .2dm (Eden material library) reader and texture-slot patcher.

Port of `source/render/2dm.cpp` / `2dm.h` for the **PC** layout (little
endian) plus the **Xbox 360** prototype variant (the same structure, every
field big endian - .2dm keeps its size across platforms, only the bytes are
swapped, see `tools/x360/README.md`). A bank entry's payload starts with the
RenderFile container (header + section tree); each `MAT.` section payload is
one material:

    +0x000 u64  Hashcode
    +0x010 char Name[32]
    +0x030 render states (16 bytes)
    +0x0b0 float4 Diffuse/Ambient/Specular/Emissive
    +0x0fc u8   NumTextureSlots
    +0x100 TextureSlot[i] (0x20 bytes): { u64 SlotType; u64 TextureHashcode;
                                          ...; u8 AddressU @0x18; u8 AddressV @0x19 }
    +0x300 u64  ShaderSet GUID
    +0x340 ParameterDesc[N] + values

Textures are referenced by 64-bit hashcode (`GetIdentifier64bit`, ported from
`source/shared.h`: uppercase characters accumulated into 8 byte slots, char i
and i+8 ADDED together for longer names). `patch_texture` rewrites a slot's
`TextureHashcode` in place - same size, so a patched .2dm drops back into a
bank with no repacking.
"""
import struct

FOURCC_2DM = 0x4D44322E  # MakeFourCC('.', '2', 'D', 'M')
FOURCC_MAT = 0x2E54414D  # MakeFourCC('M', 'A', 'T', '.')
FOURCC_UVA = 0x2E415655  # MakeFourCC('U', 'V', 'A', '.')

_HEADER_SIZE = 0x10
_SECTION_SIZE = 0x10
_RENDER_STATES_OFFSET = 0x30
_TEXTURE_SLOTS_OFFSET = 0x100
_TEXTURE_SLOT_SIZE = 0x20
_PARAMETERS_OFFSET = 0x340


def make_fourcc(a, b, c, d):
    return (a & 0xFF) | ((b & 0xFF) << 8) | ((c & 0xFF) << 16) | ((d & 0xFF) << 24)


def get_identifier_64bit(name):
    """Port of GetIdentifier64bit (source/shared.h)."""
    slots = [0] * 8
    for i, ch in enumerate(name.encode("ascii", "replace")):
        c = ch
        if 0x60 < c < 0x7B:
            c -= 0x20
        slots[i & 7] = (slots[i & 7] + c) & 0xFF
    return sum(slot << (i * 8) for i, slot in enumerate(slots))


def identifier_to_string(hashcode):
    out = ""
    for i in range(8):
        c = (hashcode >> (i * 8)) & 0xFF
        if c == 0:
            break
        out += chr(c)
    return out


def _visit_sections(data, platform):
    """Yield (section_type, section_offset, section_size, payload_offset,
    payload_size) following RenderFile::VisitSections."""
    order = "<" if platform == "pc" else ">"

    def read_u32(off):
        return struct.unpack_from(order + "I", data, off)[0]

    def visit_range(begin, end):
        offset = begin
        while offset + _SECTION_SIZE <= end:
            size = read_u32(offset + 8)
            if size < _SECTION_SIZE or offset + size > end:
                return
            data_size = max(read_u32(offset + 12), _SECTION_SIZE)
            yield (read_u32(offset), offset, size, offset + _SECTION_SIZE,
                   size - _SECTION_SIZE)
            if size > data_size:
                for section in visit_range(offset + data_size, offset + size):
                    yield section
            offset += size

    if len(data) < _HEADER_SIZE:
        return
    magic = read_u32(0x0C)  # RenderFile::Header: Magic is the last u32
    if magic != FOURCC_2DM:
        raise ValueError("not a .2dm payload (magic %08x at +0x0C)" % magic)
    header_size = min(read_u32(0x08), len(data))  # Header::Size
    for section in visit_range(_HEADER_SIZE, header_size):
        yield section


def _is_parameter_desc(data, off, order):
    name = struct.unpack_from(order + "Q", data, off)[0]
    ptype = struct.unpack_from(order + "H", data, off + 8)[0]
    size = struct.unpack_from(order + "I", data, off + 12)[0]
    return name != 0 and 1 <= ptype <= 4 and size == 0x10


def parse(data, platform="pc"):
    """Parse a .2dm payload. Returns a list of materials:
    {hash, name, payload_offset, slots: [{index, slot_type, slot_type_name,
    texture_hash, texture_name, hash_offset}], params: {name: (x,y,z,w)}}.
    `hash_offset` is the file offset of the slot's TextureHashcode u64, so
    `patch_texture` can rewrite it in place."""
    order = "<" if platform == "pc" else ">"
    materials = []
    for section_type, _off, _size, payload_offset, payload_size in _visit_sections(data, platform):
        if section_type != FOURCC_MAT or payload_size < _PARAMETERS_OFFSET:
            continue
        p = payload_offset
        material = {
            "hash": struct.unpack_from(order + "Q", data, p)[0],
            "name": data[p + 0x10:p + 0x10 + 32].split(b"\0")[0].decode("latin-1"),
            "payload_offset": p,
            "slots": [],
            "params": {},
        }
        num_slots = data[p + 0xFC]
        for i in range(min(num_slots, 8)):
            slot = p + _TEXTURE_SLOTS_OFFSET + i * _TEXTURE_SLOT_SIZE
            slot_type = struct.unpack_from(order + "Q", data, slot)[0]
            texture_hash = struct.unpack_from(order + "Q", data, slot + 8)[0]
            if slot_type == 0:
                continue
            material["slots"].append({
                "index": i,
                "slot_type": slot_type,
                "slot_type_name": identifier_to_string(slot_type),
                "texture_hash": texture_hash,
                "texture_name": identifier_to_string(texture_hash),
                "hash_offset": slot + 8,
            })
        offset = p + _PARAMETERS_OFFSET
        limit = p + payload_size
        while offset >= p + 0x330 and offset + 16 <= limit and offset < p + _PARAMETERS_OFFSET + 0x80 \
                and not _is_parameter_desc(data, offset, order):
            offset += 16
        names = []
        while offset + 16 <= limit and _is_parameter_desc(data, offset, order):
            names.append(struct.unpack_from(order + "Q", data, offset)[0])
            offset += 16
        for name in names:
            if offset + 16 > limit:
                break
            value = struct.unpack_from(order + "4f", data, offset)
            material["params"][identifier_to_string(name)] = value
            offset += 16
        materials.append(material)
    return materials


def patch_texture(data, material, slot_type_name, new_hash, platform="pc"):
    """Rewrite `material`'s slot `slot_type_name` texture hashcode in place.
    Returns the old hashcode."""
    order = "<" if platform == "pc" else ">"
    wanted = get_identifier_64bit(slot_type_name)
    for slot in material["slots"]:
        if slot["slot_type"] == wanted:
            old = slot["texture_hash"]
            struct.pack_into(order + "Q", data, slot["hash_offset"], new_hash)
            slot["texture_hash"] = new_hash
            slot["texture_name"] = identifier_to_string(new_hash)
            return old
    raise KeyError("material '%s' has no '%s' slot" % (material["name"], slot_type_name))


def _safe(text):
    return text.encode("ascii", "backslashreplace").decode("ascii")


def dump(data, platform="pc"):
    for material in parse(data, platform):
        slots = ", ".join("%s='%s' (%016x)" % (s["slot_type_name"], s["texture_name"],
                                               s["texture_hash"])
                          for s in material["slots"]) or "(none)"
        print("  '%s' (%016x)  shader %016x  slots: %s" % (
            _safe(material["name"]), material["hash"],
            struct.unpack_from("<Q" if platform == "pc" else ">Q", data, material["payload_offset"] + 0x300)[0],
            _safe(slots)))


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("usage: eden_2dm.py <file.2dm> [pc|360]")
        raise SystemExit(2)
    path = sys.argv[1]
    plat = sys.argv[2] if len(sys.argv) > 2 else ("360" if path.endswith("_360.2dm") else "pc")
    with open(path, "rb") as handle:
        dump(handle.read(), plat)

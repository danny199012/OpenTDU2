"""TDU2 .3dg (Eden geometry library) primitive reader - PC and 360.

Only the section/PRIM-header walk of `source/render/3dg.h` (the geometry
library of a scene: GEOA { HASH + GEOM -> PRIM }), not the vertex data - the
360 prototype vertex buffers are packed (CROSSBUILD_PLAN "Not ported") but the
primitive headers are plain:

    PRIM payload (0x40): +0x00 u32 pIndexBuffer; +0x04 u32 pVertexBuffer;
                         +0x10 u16 MaterialIndex (the instance's MBNK list);
                         +0x12 u16 PrimitiveType; +0x1C u32 NumVertex;
                         +0x28 u32 NumIndex
    HASH: { u64 Hashcode (also the .3dd object hashcode); u32 pGeometry; u32 }[]

`primitive_materials` returns {geometry name: [MaterialIndex per PRIM]} - what
`make_q7_inuit.py --material-diff` joins against the two scenes' MBNK lists to
derive the retail -> prototype material pairing per primitive.
"""
import struct

FOURCC_3DG = 0x4744332E  # MakeFourCC('.', '3', 'D', 'G')
FOURCC_HASH = 0x48534148  # 'HASH'
FOURCC_GEOM = 0x4D4F4547  # 'GEOM'
FOURCC_PRIM = 0x4D495250  # 'PRIM'


def hash_to_name(hashcode):
    out = ""
    for i in range(8):
        c = (hashcode >> (i * 8)) & 0xFF
        if c == 0:
            break
        out += chr(c) if 32 <= c < 127 else "?"
    return out


def _sections(data, platform):
    order = "<" if platform == "pc" else ">"

    def visit(begin, end, depth):
        offset = begin
        while offset + 0x10 <= end:
            size = struct.unpack_from(order + "I", data, offset + 8)[0]
            if size < 0x10 or offset + size > end:
                return
            data_size = max(struct.unpack_from(order + "I", data, offset + 12)[0], 0x10)
            section_type = struct.unpack_from(order + "I", data, offset)[0]
            yield section_type, offset, offset + 0x10, size - 0x10, depth
            if size > data_size:
                for section in visit(offset + data_size, offset + size, depth + 1):
                    yield section
            offset += size

    if len(data) < 0x10:
        return
    magic = struct.unpack_from(order + "I", data, 0x0C)[0]
    if magic != FOURCC_3DG:
        raise ValueError("not a .3dg payload (magic %08x)" % magic)
    file_size = min(struct.unpack_from(order + "I", data, 0x08)[0], len(data))
    for section in visit(0x10, file_size, 0):
        yield section


def primitive_materials(data, platform="pc"):
    """{geometry name: [MaterialIndex, ...] in PRIM order}."""
    order = "<" if platform == "pc" else ">"
    name_by_geom_offset = {}
    for section_type, _header, payload_offset, payload_size, _depth in _sections(data, platform):
        if section_type != FOURCC_HASH:
            continue
        for i in range(payload_size // 16):
            hashcode = struct.unpack_from(order + "Q", data, payload_offset + i * 16)[0]
            pointer = struct.unpack_from(order + "I", data, payload_offset + i * 16 + 8)[0]
            name_by_geom_offset[pointer] = hash_to_name(hashcode)

    result = {}
    current = None
    for section_type, header_offset, payload_offset, payload_size, _depth in _sections(data, platform):
        if section_type == FOURCC_GEOM:
            current = name_by_geom_offset.get(header_offset)
            result.setdefault(current, [])
        elif section_type == FOURCC_PRIM and current is not None and payload_size >= 0x40:
            result[current].append(struct.unpack_from(order + "H", data, payload_offset + 0x10)[0])
    return result


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("usage: eden_3dg.py <file.3dg> [pc|360]")
        raise SystemExit(2)
    plat = sys.argv[2] if len(sys.argv) > 2 else "pc"
    with open(sys.argv[1], "rb") as handle:
        data = handle.read()
    for name, materials in primitive_materials(data, plat).items():
        print("%-16s %2d prims  materials %s" % (name, len(materials), materials))

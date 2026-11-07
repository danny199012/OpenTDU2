"""TDU2 .3dd (Eden scene description) reader for PC and 360 payloads.

Port of `source/render/3dd.cpp`. A .3dd is a RenderFile container whose
sections are:

    MBNA: MBNK sections = material banks { u64 MaterialHashcode; u64 pad }[]
    OBJA: HASH { u64 Hashcode; u32 pObject; u32 pad }[] + OBJ. (one per object;
          the object hashcode is also the hashcode of its .3DG geometry)
    INSA: HASH + INS. instances:
          +0x00 float[16] local matrix (D3D row-major, row vectors)
          +0x44 u32 pMaterialBank (file offset of a MBNK section)
          +0x4e u16 numLOD, +0x50 u32 pObjectHashEntry, +0x54 + k*0x14
          { u32 pObjectHashEntry; f32 distance } per LOD
    HIEA: HASH + HIE. hierarchy nodes { u32; u32 pInstance; u32 flags; u32 pParent }
    REAL: relocations (not read by the engine)

The **Xbox 360** variant is the same structure big endian (see
`tools/x360/README.md`) - `eden_render_file.swap_to_pc` converts a 360 .3dd
(or .2dm) to the PC layout in place, which is what `make_q7_inuit.py` feeds
the hybrid bank.
"""
import struct

FOURCC_3DD = 0x4444332E  # MakeFourCC('.', '3', 'D', 'D')
FOURCC_MBNA = 0x414E424D  # 'MBNA'
FOURCC_OBJA = 0x414A424F  # 'OBJA'
FOURCC_INSA = 0x41534E49  # 'INSA'
FOURCC_HIEA = 0x41454948  # 'HIEA'
FOURCC_HASH = 0x48534148  # 'HASH'
FOURCC_INS = 0x2E534E49  # 'INS.'
FOURCC_HIE = 0x2E454948  # 'HIE.'
FOURCC_MBNK = 0x4B4E424D  # 'MBNK'

_INS_MATERIAL_BANK_OFFSET = 0x44
_INS_NUM_LODS_OFFSET = 0x4E
_INS_OBJECT_ENTRY_OFFSET = 0x50
_INS_LOD_OFFSET = 0x54
_INS_LOD_STRIDE = 0x14


def make_fourcc(a, b, c, d):
    return (a & 0xFF) | ((b & 0xFF) << 8) | ((c & 0xFF) << 16) | ((d & 0xFF) << 24)


def hash_to_name(hashcode):
    out = ""
    for i in range(8):
        c = (hashcode >> (i * 8)) & 0xFF
        if c == 0:
            break
        out += chr(c) if 32 <= c < 127 else "?"
    return out


def _sections(data, platform):
    """Walk the section tree like RenderFile::VisitSections (read in the
    file's own endianness). Yields (section_type, header_offset, payload_offset,
    payload_size, depth)."""
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
    if magic != FOURCC_3DD:
        raise ValueError("not a .3dd payload (magic %08x)" % magic)
    file_size = min(struct.unpack_from(order + "I", data, 0x08)[0], len(data))
    for section in visit(0x10, file_size, 0):
        yield section


def parse(data, platform="pc"):
    """Port of Render3DD::parse (without the X mirror, which is a runtime
    choice). Returns a list of nodes:
    {hash, name, object_hash, object_name, materials: [names], lods}."""
    order = "<" if platform == "pc" else ">"
    instance_hashes = {}  # INS. payload offset -> hashcode
    hash_entries = {}     # OBJA HASH entry offset -> hashcode
    hierarchy = []        # (header offset, payload offset)

    current_container = 0
    for section_type, header_offset, payload_offset, payload_size, depth in _sections(data, platform):
        if depth == 0:  # container: MBNA / OBJA / INSA / HIEA
            current_container = section_type
        if section_type == FOURCC_HASH:
            count = payload_size // 16
            for i in range(count):
                entry = payload_offset + i * 16
                hashcode = struct.unpack_from(order + "Q", data, entry)[0]
                pointer = struct.unpack_from(order + "I", data, entry + 8)[0]
                if current_container == FOURCC_INSA:
                    instance_hashes[pointer] = hashcode
                elif current_container == FOURCC_OBJA:
                    hash_entries[entry] = hashcode
        elif section_type == FOURCC_HIE:
            hierarchy.append((header_offset, payload_offset))

    sources = []
    for header_offset, payload_offset in hierarchy:
        # HIE. payload: { u32; u32 pInstance; u32 flags; u32 pParentNode }
        sources.append((header_offset,
                        struct.unpack_from(order + "I", data, payload_offset + 4)[0],
                        struct.unpack_from(order + "I", data, payload_offset + 12)[0]))
    if not sources:
        # Simple scenes (rims...) have no HIEA: every instance is a root node.
        sources = sorted(((off, off, 0) for off in instance_hashes),
                         key=lambda source: source[0])

    header_to_index = {source[0]: i for i, source in enumerate(sources)}
    nodes = []
    for header_offset, instance_offset, _parent in sources:
        node = {
            "hash": instance_hashes.get(instance_offset, 0),
            "name": "",
            "object_hash": 0,
            "object_name": "",
            "materials": [],
            "lods": 0,
            "parent": -1,
        }
        node["name"] = hash_to_name(node["hash"])
        payload = instance_offset + 0x10
        if payload + 0x60 > len(data):
            continue
        material_bank_offset = struct.unpack_from(order + "I", data, payload + _INS_MATERIAL_BANK_OFFSET)[0]
        object_entry_offset = struct.unpack_from(order + "I", data, payload + _INS_OBJECT_ENTRY_OFFSET)[0]
        num_lods = struct.unpack_from(order + "H", data, payload + _INS_NUM_LODS_OFFSET)[0]
        node["lods"] = num_lods
        node["object_hash"] = hash_entries.get(object_entry_offset, 0)
        node["object_name"] = hash_to_name(node["object_hash"])
        if material_bank_offset and material_bank_offset + 0x10 <= len(data) \
                and struct.unpack_from(order + "I", data, material_bank_offset)[0] == FOURCC_MBNK:
            bank_size = struct.unpack_from(order + "I", data, material_bank_offset + 8)[0]
            for i in range((bank_size - 0x10) // 16):
                material_hash = struct.unpack_from(
                    order + "Q", data, material_bank_offset + 0x10 + i * 16)[0]
                node["materials"].append(hash_to_name(material_hash) or "%016x" % material_hash)
        node["parent"] = header_to_index.get(_parent, -1) if _parent else -1
        nodes.append(node)
    return nodes


def dump(data, platform="pc"):
    nodes = parse(data, platform)

    def walk(index, depth):
        node = nodes[index]
        print("  %s%-16s obj %-16s lods %d  materials: %s" % (
            "  " * depth, node["name"] or "(root)", node["object_name"] or "-",
            node["lods"], ", ".join(node["materials"]) or "-"))
        for child in range(len(nodes)):
            if nodes[child].get("parent") == index and child != index:
                walk(child, depth + 1)

    for index in range(len(nodes)):
        parent = nodes[index].get("parent", -1)
        if parent < 0:
            walk(index, 0)


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("usage: eden_3dd.py <file.3dd> [pc|360]")
        raise SystemExit(2)
    plat = sys.argv[2] if len(sys.argv) > 2 else "pc"
    with open(sys.argv[1], "rb") as handle:
        dump(handle.read(), plat)

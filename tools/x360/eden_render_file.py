"""Byte-swap an Xbox 360 Eden render file (.3dd / .2dm) to the PC layout.

The two platforms store these files with the same sizes and the same section
tree; every 16/32/64-bit scalar is big endian on the 360 (`tools/x360/README.md`).
`swap_to_pc` rewrites a 360 payload in place into the little-endian PC layout,
so a prototype's .3dd / .2dm can drop into a PC bank (`make_q7_inuit.py`).

Only the fields the engine reads are swapped - byte arrays (material names,
render states, shader permutation switches) are endian-neutral and are left
alone, and unknown payload regions are left untouched rather than guessed:

  - every section: header { u32 Type; u16 vMaj; u16 vMin; u32 Size; u32 DataSize }
  - .3dd: HASH { u64; u32; u32 }[], INS. float[16] + the fields 3dd.cpp reads,
    HIE. { u32 x4 }, MBNK { u64; u64 }[], REAL u32[] (OBJ. is unread - left as is)
  - .2dm: MAT. hashcode, the float4 colours, the texture slots' two u64s, the
    shader GUID, ParameterDesc { u64; u16; u16; u32 } + float4 values
    (MTPA is unread - left as is)
"""
import struct

FOURCC_3DD = 0x4444332E  # MakeFourCC('.', '3', 'D', 'D')
FOURCC_2DM = 0x4D44322E  # '.2DM'
FOURCC_INS = 0x2E534E49  # 'INS.'
FOURCC_MAT = 0x2E54414D  # 'MAT.'
FOURCC_HASH = 0x48534148  # 'HASH'
FOURCC_MBNK = 0x4B4E424D  # 'MBNK'
FOURCC_UVAA = 0x41415655  # 'UVAA'

_INS_NUM_LODS_OFFSET = 0x4E
_INS_LOD_OFFSET = 0x54
_INS_LOD_STRIDE = 0x14
_MAT_PARAMETERS_OFFSET = 0x340


def _sections(data, magic):
    """Walk the section tree reading big endian. Yields (type, header_offset,
    payload_offset, payload_size, has_children) - the payload is the DataSize
    region only, children live between DataSize and Size."""
    def visit(begin, end):
        offset = begin
        while offset + 0x10 <= end:
            size = struct.unpack_from(">I", data, offset + 8)[0]
            if size < 0x10 or offset + size > end:
                return
            data_size = max(struct.unpack_from(">I", data, offset + 12)[0], 0x10)
            section_type = struct.unpack_from(">I", data, offset)[0]
            has_children = size > data_size
            payload_size = (data_size if has_children else size) - 0x10
            yield section_type, offset, offset + 0x10, payload_size, has_children
            if has_children:
                for section in visit(offset + data_size, offset + size):
                    yield section
            offset += size

    if len(data) < 0x10:
        return
    if struct.unpack_from(">I", data, 0x0C)[0] != magic:
        raise ValueError("not a %08x payload (magic %08x)" % (
            magic, struct.unpack_from(">I", data, 0x0C)[0]))
    file_size = min(struct.unpack_from(">I", data, 0x08)[0], len(data))
    for section in visit(0x10, file_size):
        yield section


def _mat_parameter_descs(data, payload_offset, payload_size):
    """Offsets of the ParameterDesc records with their two big-endian u16s
    (Type, Flags), detected on the ORIGINAL (big endian) bytes - mirroring
    2dm.cpp's IsParameterDesc scan."""
    descs = []
    offset = payload_offset + _MAT_PARAMETERS_OFFSET
    limit = payload_offset + payload_size
    while offset >= payload_offset + 0x330 and offset + 16 <= limit \
            and offset < payload_offset + _MAT_PARAMETERS_OFFSET + 0x80:
        name = struct.unpack_from(">Q", data, offset)[0]
        ptype = struct.unpack_from(">H", data, offset + 8)[0]
        size = struct.unpack_from(">I", data, offset + 12)[0]
        if name != 0 and 1 <= ptype <= 4 and size == 0x10:
            break
        offset += 16
    while offset + 16 <= limit:
        name = struct.unpack_from(">Q", data, offset)[0]
        ptype = struct.unpack_from(">H", data, offset + 8)[0]
        size = struct.unpack_from(">I", data, offset + 12)[0]
        if not (name != 0 and 1 <= ptype <= 4 and size == 0x10):
            break
        descs.append((offset - payload_offset,
                      (ptype, struct.unpack_from(">H", data, offset + 10)[0])))
        offset += 16
    return descs


def _swap_u32_range(data, start, end):
    for offset in range(start, end - 3, 4):
        value = struct.unpack_from(">I", data, offset)[0]
        struct.pack_into("<I", data, offset, value)


def _swap_u64(data, offset):
    """Reverse one 64-bit field (a per-u32 swap would leave the two halves
    exchanged)."""
    high = struct.unpack_from(">I", data, offset)[0]
    low = struct.unpack_from(">I", data, offset + 4)[0]
    struct.pack_into("<II", data, offset, low, high)


def swap_to_pc(data, magic):
    """Convert a big-endian .3dd/.2dm payload to the PC layout, in place.
    Returns data (the same bytes object)."""
    order_fixes = []  # (absolute offset, big-endian u16 to write little endian)

    # Collect the section list on the original bytes first: swapping the file
    # header below would break the walk's own magic/size reads.
    sections = list(_sections(data, magic))

    # File header: u16 maj, u16 min, u32 flags, u32 size, u32 magic.
    struct.pack_into("<HHII", data, 0x00,
                     struct.unpack_from(">H", data, 0)[0],
                     struct.unpack_from(">H", data, 2)[0],
                     struct.unpack_from(">I", data, 4)[0],
                     struct.unpack_from(">I", data, 8)[0])
    struct.pack_into("<I", data, 0x0C, magic)

    for section_type, header_offset, payload_offset, payload_size, has_children \
            in sections:
        # Section header.
        struct.pack_into("<IHHII", data, header_offset,
                         struct.unpack_from(">I", data, header_offset)[0],
                         struct.unpack_from(">H", data, header_offset + 4)[0],
                         struct.unpack_from(">H", data, header_offset + 6)[0],
                         struct.unpack_from(">I", data, header_offset + 8)[0],
                         struct.unpack_from(">I", data, header_offset + 12)[0])
        if payload_size <= 0 or has_children:
            continue

        if section_type == FOURCC_INS:
            # float[16] matrix, then the fields 3dd.cpp reads. The LOD table's
            # { u32 pObjectHashEntry; f32 distance } per 0x14 entry.
            _swap_u32_range(data, payload_offset, payload_offset + 0x44)
            _swap_u32_range(data, payload_offset + 0x44, payload_offset + 0x4C)
            num_lods = struct.unpack_from(">H", data, payload_offset + _INS_NUM_LODS_OFFSET)[0]
            order_fixes.append((payload_offset + _INS_NUM_LODS_OFFSET, num_lods))
            _swap_u32_range(data, payload_offset + 0x50, payload_offset + 0x54)
            for lod in range(min(num_lods, 3)):
                entry = payload_offset + _INS_LOD_OFFSET + lod * _INS_LOD_STRIDE
                _swap_u32_range(data, entry, entry + 8)
        elif section_type == FOURCC_MAT:
            descs = _mat_parameter_descs(data, payload_offset, payload_size)
            # The material hashcode at +0 is NOT a number on either platform:
            # it is the 8-slot identifier in memory order (char 0 first - e.g.
            # 'Mirror_I' holds "MIRROR_INT"'s fold), identical bytes on both
            # platforms, so it must not be reversed. The colours, the texture
            # slots' two u64s and the shader GUID are real numbers.
            _swap_u32_range(data, payload_offset + 0xB0, payload_offset + 0xF0)
            _swap_u64(data, payload_offset + 0x300)
            num_slots = data[payload_offset + 0xFC]
            for slot in range(min(num_slots, 8)):
                base = payload_offset + 0x100 + slot * 0x20
                _swap_u64(data, base)
                _swap_u64(data, base + 8)
            # Parameter descriptors { u64; u16; u16; u32 } and their float4 values.
            param_values = payload_offset + _MAT_PARAMETERS_OFFSET + len(descs) * 16
            for desc, u16_values in descs:
                base = payload_offset + desc
                _swap_u64(data, base)
                for u16_offset, value in zip((8, 10), u16_values):
                    struct.pack_into("<H", data, base + u16_offset, value)
                _swap_u32_range(data, base + 12, base + 16)
            values_end = payload_offset + payload_size
            _swap_u32_range(data, param_values, values_end)
        else:
            # .3dd HASH { u64; u32; u32 }[], MBNK { u64; u64 }[] and HIE. /
            # REAL u32 arrays are numbers. The .2dm's own HASH (the MATA
            # material index) and UVAA id lists hold string-order identifiers
            # like the MAT hashcode - nothing reads them, leave them. OBJ. and
            # MTPA are not read by the engine - swap them as u32 words (byte
            # arrays inside would need a real decoder).
            if magic == FOURCC_3DD and section_type in (FOURCC_HASH, FOURCC_MBNK, FOURCC_UVAA):
                record = 16
                count = payload_size // record
                for i in range(count):
                    base = payload_offset + i * record
                    _swap_u64(data, base)
                    _swap_u32_range(data, base + 8, base + record)
            elif magic == FOURCC_2DM and section_type in (FOURCC_HASH, FOURCC_UVAA):
                pass
            else:
                _swap_u32_range(data, payload_offset, payload_offset + payload_size)

    for offset, value in order_fixes:
        struct.pack_into("<H", data, offset, value)
    return data


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 4 or sys.argv[3] not in ("3dd", "2dm"):
        print("usage: eden_render_file.py <in.3dd|in.2dm> <out> <3dd|2dm>")
        raise SystemExit(2)
    with open(sys.argv[1], "rb") as handle:
        payload = bytearray(handle.read())
    swap_to_pc(payload, FOURCC_3DD if sys.argv[3] == "3dd" else FOURCC_2DM)
    with open(sys.argv[2], "wb") as handle:
        handle.write(payload)
    print("%s: swapped to PC layout, %d bytes" % (sys.argv[2], len(payload)))

"""TDU2 .bnk (Eden resource bank) writer - the PC (`KNAB`) container.

The first bank writer in the repo (the engine and `tools/bnk_tool` only read);
inverse of `eden_bank.py` / `source/filesystem/bank.cpp`. Layout measured off
the retail banks (see the reader's docstring):

    0x00 { u32 headerSize (0x40); u32 CRC }
    0x08 BankHeader (0x40): magic `KNAB`, flags, version, type, sizes,
         alignments, NumEntries, creation date, section offsets
    0x48 "STNICC2000 RULEZPADDING " filler (0x18)
    sections, each prefixed { u32 size; u32 CRC } (content at +8), 32-aligned:
      Table     (NumEntries + 1) x 0x14 { i32 offset; i32 size; i32 date;
                                              i32 CRC; i32 alignment }
      Types     BankType { i16 subType; i16 group } per entry
      Directory compressed path tree (`encode_directory`)
      Indexes   u8/u16/u32 per entry: directory order -> entry index
    Data      the payloads, 16-aligned, at the offsets the table gives

The section CRCs (and the per-entry CRCs) are zlib crc32 here - the engine
never verifies them and Eden's own algorithm is not reproduced; every other
byte of the layout is faithful. `--roundtrip` rebuilds an existing PC bank and
compares every entry (path, size, payload bytes) plus the directory bytes:

    python -I build_bank.py --roundtrip <bank.bnk>
"""
import struct
import sys
import zlib

from eden_bank import parse_file, decode_bank_path, PC

_MAGIC = b"KNAB"
_FILLER = b"STNICC2000 RULEZPADDING "
_HEADER_SIZE = 0x40
_SECTION_ALIGNMENT = 0x20
_DATA_ALIGNMENT = 0x10


def _align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def _directory_components(path):
    """Decoded bank path `dir\\name.ext` -> the tree components
    (`dir`, `.ext`, `name`): a component starting with '.' is the extension of
    the file that follows (the reader's decode_bank_path in reverse)."""
    parts = path.split("\\")
    filename = parts[-1]
    dot = filename.rfind(".")
    name = filename[:dot] if dot >= 0 else filename
    ext = filename[dot:] if dot >= 0 else ""
    return parts[:-1] + ([ext] if ext else []) + [name]


def _build_tree(paths):
    """The path tree as {"name", "children"} nodes, files as leaves, in
    insertion order (the retail trees list files in their own order)."""
    root = {"name": "", "children": []}
    nodes = {(): root}

    def node_at(components):
        if components in nodes:
            return nodes[components]
        parent = node_at(components[:-1])
        node = {"name": components[-1], "children": []}
        parent["children"].append(node)
        nodes[components] = node
        return node

    for path in paths:
        node_at(tuple(_directory_components(path)))
    return root


def _write_node(out, node):
    """One directory node: [escape] length, child-count varint, name, children
    (directories); or length, name (files). Mirrors the reader."""
    name = node["name"].encode("latin-1")
    if node["children"]:
        length = len(name)
        if length < 0x80:
            out.append((-length) & 0xFF)
        else:
            out.append(0x80)
            out.append(length - 0x80)
        count = len(node["children"])
        while True:
            byte = count & 0x7F
            count >>= 7
            if count:
                out.append(byte | 0x80)
            else:
                out.append(byte)
                break
        out.extend(name)
        for child in node["children"]:
            _write_node(out, child)
    else:
        length = len(name)
        if length < 0x7F:
            out.append(length)
        else:
            out.append(0x7F)
            out.append(length - 0x7F)
        out.extend(name)


def encode_directory(root):
    """The compressed path tree bytes from a built tree, byte-compatible with
    the retail banks (verified by --roundtrip)."""
    out = bytearray()
    for child in root["children"]:
        _write_node(out, child)
    out.append(0)  # the reader stops at the 0 byte
    return bytes(out)


def _tree_leaf_order(root):
    """The decoded bank paths in tree (directory) order."""
    order = []

    def walk(node, prefix):
        for child in node["children"]:
            if child["children"]:
                walk(child, prefix + child["name"] + "\\")
            else:
                order.append(decode_bank_path(prefix + child["name"]))

    walk(root, "")
    return order


def build_bank(entries, creation_date=0, bank_type=0, directory_order=None):
    """Pack `entries` ({path, data, subtype, group, date?}) into a PC bank.
    `directory_order` optionally gives the paths in the original tree order
    (from the source bank's directory), so a rebuild is byte-identical.
    Returns the bank bytes."""
    num_entries = len(entries)
    order = list(directory_order) if directory_order else [entry["path"] for entry in entries]
    assert sorted(order) == sorted(entry["path"] for entry in entries), \
        "directory_order must be a permutation of the entry paths"
    root = _build_tree(order)
    directory = encode_directory(root)

    # Layout: header + filler, then the four 32-aligned sections, then the
    # 16-aligned payloads.
    offset = _align(0x48 + len(_FILLER), _SECTION_ALIGNMENT)
    table_offset = offset
    offset = _align(offset + 8 + (num_entries + 1) * 0x14, _SECTION_ALIGNMENT)
    types_offset = offset
    offset = _align(offset + 8 + num_entries * 4, _SECTION_ALIGNMENT)
    directory_offset = offset
    offset = _align(offset + 8 + len(directory), _SECTION_ALIGNMENT)
    indexes_size = num_entries if num_entries < 0x100 else (
        num_entries * 2 if num_entries < 0x10000 else num_entries * 4)
    indexes_offset = offset
    offset = _align(offset + 8 + indexes_size, _DATA_ALIGNMENT)
    data_offset = offset

    # Payloads, then the table records (the last record is the data section).
    payloads = []
    records = []
    for entry in entries:
        data = entry["data"]
        records.append((offset, len(data), entry.get("date", creation_date),
                        zlib.crc32(data) & 0xFFFFFFFF, _DATA_ALIGNMENT))
        payloads.append(data)
        offset = _align(offset + len(data), _DATA_ALIGNMENT)
    records.append((offset, 0, 0, 0, _DATA_ALIGNMENT))
    end_of_data = offset

    # Directory order -> table order (the reader's resolveIndex).
    table_index = {entry["path"]: i for i, entry in enumerate(entries)}
    index_bytes = bytearray()
    for path in _tree_leaf_order(root):
        index = table_index[path]
        if num_entries < 0x100:
            index_bytes.append(index)
        elif num_entries < 0x10000:
            index_bytes.extend(struct.pack("<H", index))
        else:
            index_bytes.extend(struct.pack("<I", index))

    file_size = _align(end_of_data, _DATA_ALIGNMENT)

    # BankHeader (offsets relative to the struct at file 0x08).
    header = bytearray(0x40)
    header[0:4] = _MAGIC
    struct.pack_into("<i", header, 0x08, 0)                    # Version
    struct.pack_into("<h", header, 0x0C, bank_type)            # Type
    struct.pack_into("<i", header, 0x10, file_size)            # Size
    struct.pack_into("<i", header, 0x14, end_of_data)          # SizeWithoutPadding
    struct.pack_into("<i", header, 0x18, _SECTION_ALIGNMENT)
    struct.pack_into("<i", header, 0x1C, _DATA_ALIGNMENT)
    struct.pack_into("<i", header, 0x20, num_entries)
    struct.pack_into("<i", header, 0x24, creation_date)
    struct.pack_into("<i", header, 0x28, table_offset)
    struct.pack_into("<i", header, 0x2C, types_offset)
    struct.pack_into("<i", header, 0x30, directory_offset)
    struct.pack_into("<i", header, 0x34, indexes_offset)
    struct.pack_into("<I", header, 0x38, 0)                    # reversed indexes: unused
    struct.pack_into("<I", header, 0x3C, data_offset)

    out = bytearray()
    out.extend(struct.pack("<II", _HEADER_SIZE, zlib.crc32(bytes(header)) & 0xFFFFFFFF))
    out.extend(header)
    out.extend(_FILLER)
    out.extend(b"\0" * (table_offset - len(out)))

    table = bytearray()
    for rec_off, rec_size, rec_date, rec_crc, rec_align in records:
        table.extend(struct.pack("<iiiIi", rec_off, rec_size, rec_date, rec_crc, rec_align))
    out.extend(struct.pack("<II", len(table), zlib.crc32(bytes(table)) & 0xFFFFFFFF))
    out.extend(table)
    out.extend(b"\0" * (types_offset - len(out)))

    types = bytearray()
    for entry in entries:
        types.extend(struct.pack("<hh", entry.get("subtype", 0), entry.get("group", 0)))
    out.extend(struct.pack("<II", len(types), zlib.crc32(bytes(types)) & 0xFFFFFFFF))
    out.extend(types)
    out.extend(b"\0" * (directory_offset - len(out)))

    out.extend(struct.pack("<II", len(directory), zlib.crc32(directory) & 0xFFFFFFFF))
    out.extend(directory)
    out.extend(b"\0" * (indexes_offset - len(out)))

    out.extend(struct.pack("<II", len(index_bytes), zlib.crc32(bytes(index_bytes)) & 0xFFFFFFFF))
    out.extend(index_bytes)
    out.extend(b"\0" * (data_offset - len(out)))
    for data in payloads:
        out.extend(data)
    out.extend(b"\0" * (file_size - len(out)))
    return bytes(out)


def eden_bank_directory_order(bank):
    """A parsed bank's encoded paths in directory (tree) order - the order the
    retail trees list files in, for byte-identical rebuilds."""
    import eden_bank

    header = bank["header"]
    data = bank["data"]
    directory, directory_size = 0, 0
    for field, mask in ((0x38, None),):
        directory = struct.unpack_from("<i", data, field)[0]
        directory_size = struct.unpack_from("<I", data, directory)[0]
    end = directory + 8 + min(directory_size, len(data) - directory - 8)
    paths = eden_bank._parse_directory(data, directory + 8, end)
    return paths


def _roundtrip(path):
    """Rebuild an existing PC bank and compare entries + directory bytes."""
    import eden_bank

    bank = parse_file(path, PC)
    entries = [{"path": entry["path"],
                "data": bank["data"][entry["offset"]:entry["offset"] + entry["size"]],
                "subtype": entry["subtype"], "group": entry["group"]}
               for entry in bank["entries"]]
    directory_order = [decode_bank_path(encoded)
                       for encoded in eden_bank_directory_order(bank)]
    rebuilt = build_bank(entries, creation_date=bank["header"]["creationDate"],
                         bank_type=bank["header"]["type"], directory_order=directory_order)
    again = eden_bank.parse(rebuilt, PC)

    problems = 0
    if len(again["entries"]) != len(bank["entries"]):
        print("FAIL: entry count %d != %d" % (len(again["entries"]), len(bank["entries"])))
        problems += 1
    for original, copy in zip(bank["entries"], again["entries"]):
        if original["path"] != copy["path"] or original["size"] != copy["size"]:
            print("FAIL: entry %r (%d) != %r (%d)" % (original["path"], original["size"],
                                                      copy["path"], copy["size"]))
            problems += 1
        elif bank["data"][original["offset"]:original["offset"] + original["size"]] != \
                again["data"][copy["offset"]:copy["offset"] + copy["size"]]:
            print("FAIL: payload bytes differ for %r" % original["path"])
            problems += 1
    # The directory tree must round-trip byte for byte.
    directory_offset = struct.unpack_from("<i", rebuilt, 0x38)[0]
    directory_size = struct.unpack_from("<I", rebuilt, directory_offset)[0]
    original_offset = struct.unpack_from("<i", bank["data"], 0x38)[0]
    original_size = struct.unpack_from("<I", bank["data"], original_offset)[0]
    new_directory = rebuilt[directory_offset + 8:directory_offset + 8 + directory_size]
    old_directory = bank["data"][original_offset + 8:original_offset + 8 + original_size]
    if new_directory != old_directory:
        print("NOTE: directory bytes differ (valid but not identical):\n  old %s\n  new %s" %
              (old_directory.hex(), new_directory.hex()))
    print("%s: %d entr(ies) rebuilt into %d bytes, %d problem(s)" % (
        path, len(entries), len(rebuilt), problems))
    return problems


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--roundtrip":
        raise SystemExit(1 if _roundtrip(sys.argv[2]) else 0)
    raise SystemExit("usage: build_bank.py --roundtrip <bank.bnk>")

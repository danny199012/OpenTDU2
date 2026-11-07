"""TDU2 .bnk (Eden resource bank) reader for both platforms.

Port of `source/filesystem/bank.cpp` for the retail **PC** banks (little endian,
magic `KNAB`) plus the **Xbox 360** prototype variant (big endian, magic `BANK`).
The two share the header, the section prefixes, the compressed path tree and the
index table - only the entry table records differ - so every asset of a 360
prototype bank can be named exactly like the PC one (that is what makes the
content scan in `content_scan.py` able to say *which* asset changed).

Layout (file offsets; the same on both platforms, big endian on the 360):

    0x00 u32   header section size (0x40)
    0x04 u32   header section CRC
    0x08 BankHeader (0x40 bytes, `BankHeader` in bank.h)
    0x48 "STNICC2000 RULEZPADDING " filler
    sections, each prefixed { u32 size; u32 CRC }, content at SectionOffset + 8:
      Table     NumEntries + 1 records (the last one is the data section)
      Types     BankType { i16 subType; i16 group } per entry
      Directory compressed path tree (`decode_bank_path`)
      Indexes   u8/u16/u32[NumEntries]: directory order -> entry index
    Data        the payloads, at the offsets the table records give

Table record:
    PC  0x14 bytes { i32 offset; i32 size; i32 date; i32 CRC; i32 alignment }
        (0x10 in TDU1-style banks: no alignment field)
    360 0x14 bytes { u64 hash; u32 block; u32 offset; u32 size }

The 360 record's hash is the asset's 64-bit identifier, not the name; the name
comes from the directory, as on the PC.
"""
import struct

PC = "pc"
X360 = "360"

_MAGIC = {PC: b"KNAB", X360: b"BANK"}
# Payload magics sit at payload + 0x0C, byte-reversed between the platforms
# (".2DM" on the PC is "MD2." on the 360).
_KINDS = ((b".3DD", "3dd"), (b".3DG", "3dg"), (b".2DM", "2dm"), (b".2DB", "2db"))


def _fmt(platform):
    return "<" if platform == PC else ">"


def i32_at(data, off, platform):
    return struct.unpack_from(_fmt(platform) + "i", data, off)[0]


def u32_at(data, off, platform):
    return struct.unpack_from(_fmt(platform) + "I", data, off)[0]


def kind_at(data, offset):
    """Resource kind of the payload at `offset`: 3dd / 3dg / 2dm / 2db / xmb /
    wav / the first four bytes in hex."""
    magic = data[offset + 0x0C:offset + 0x10]
    for raw, name in _KINDS:
        if magic in (raw, raw[::-1]):
            return name
    head = data[offset:offset + 0x10]
    if head[:4] == b"XMBF":
        return "xmb"
    if head[0x08:0x0C] in (b"WAVE", b"EVAW"):
        return "wav"
    return head[:4].hex()


def kind(blob):
    """Resource kind of a payload given as bytes (see kind_at)."""
    return kind_at(blob, 0)


def payload(bank, entry):
    """The bytes of an entry (sliced on demand: a bank's buffer is kept whole)."""
    return bank["data"][entry["offset"]:entry["offset"] + entry["size"]]


def decode_bank_path(encoded):
    """`dir\\.ext\\file` (the bank's internal layout) -> `dir\\file.ext`.

    Port of DecodeBankPath (FUN_0062220a in TDU1): a component that starts with
    '.' is the extension of the file that follows, not a directory.
    """
    result = []
    pending = ""
    for component in encoded.split("\\"):
        if component.startswith("."):
            pending = component
            continue
        if component:
            result.append(component)
    return "\\".join(result) + pending


def _parse_directory(data, start, end):
    """The path tree -> the list of encoded paths, in directory order."""
    paths = []

    def node(p, prefix):
        if p is None or p >= end:
            return None
        length = struct.unpack_from("b", data, p)[0]
        p += 1
        if length < 0:
            name_length = -length
            if length == -0x80:
                name_length = data[p] + 0x80
                p += 1
            children = 0
            shift = 0
            while True:
                byte = struct.unpack_from("b", data, p)[0]
                p += 1
                children |= (byte & 0x7F) << shift
                shift += 7
                if byte >= 0:
                    break
            name = data[p:p + name_length].decode("latin-1")
            p += name_length
            child_prefix = prefix + name + "\\"
            for _ in range(children):
                p = node(p, child_prefix)
            return p
        name_length = length
        if length == 0x7F:
            name_length = data[p] + 0x7F
            p += 1
        name = data[p:p + name_length].decode("latin-1")
        p += name_length
        paths.append(prefix + name)
        return p

    p = start
    while p is not None and p < end and data[p] != 0:
        p = node(p, "")
    return paths


def _section(data, offset, platform):
    """{ u32 size; u32 CRC } prefix at `offset`, content at offset + 8."""
    if offset < 8 or offset + 8 > len(data):
        return None, 0
    return offset + 8, u32_at(data, offset, platform)


def parse(data, platform):
    """Parse a bank. Returns {header, entries}; each entry is
    {name, filename, ext, path, size, offset, subtype, group, data}."""
    if len(data) < 0x48:
        raise ValueError("truncated bank")
    if data[0x08:0x0C] != _MAGIC[platform]:
        raise ValueError("not a %s bank (magic %r at 0x08)" % (platform, data[0x08:0x0C]))

    header = {
        "platform": platform,
        "flags": u32_at(data, 0x0C, platform),
        "version": u32_at(data, 0x10, platform),
        "type": struct.unpack_from(_fmt(platform) + "h", data, 0x14)[0],
        "size": i32_at(data, 0x18, platform),
        "sectionAlignment": i32_at(data, 0x20, platform),
        "dataAlignment": i32_at(data, 0x24, platform),
        "numEntries": i32_at(data, 0x28, platform),
        "creationDate": i32_at(data, 0x2C, platform),
    }
    num_entries = header["numEntries"]
    if num_entries < 0:
        raise ValueError("negative entry count")

    table, table_size = _section(data, i32_at(data, 0x30, platform), platform)
    types, _ = _section(data, i32_at(data, 0x34, platform), platform)
    directory, directory_size = _section(data, i32_at(data, 0x38, platform), platform)
    indexes, _ = _section(data, i32_at(data, 0x3C, platform), platform)
    if table is None:
        raise ValueError("no entry table")

    stride = 0x14
    if num_entries > 0 and table_size // (num_entries + 1) == 0x10:
        stride = 0x10  # TDU1-style table, no alignment field

    entries = [{"name": "", "filename": "", "ext": "", "path": "",
                "size": 0, "offset": 0, "alignment": 0, "kind": "?",
                "subtype": header["type"], "group": 0}
               for _ in range(num_entries)]
    for i, entry in enumerate(entries):
        record = table + i * stride
        if platform == PC:
            offset = i32_at(data, record, platform)
            size = i32_at(data, record + 4, platform)
            if stride == 0x14:
                entry["alignment"] = i32_at(data, record + 16, platform)
        else:
            # 360 records are { u32 offset; u32 size; u64 unknown; u32
            # alignment }: the same offset/size pair as the PC's, so entry i is
            # record i on both platforms (verified: the payload's own size field
            # and the 2DB name hashcode both agree with the directory name).
            offset = u32_at(data, record, platform)
            size = u32_at(data, record + 4, platform)
            entry["alignment"] = u32_at(data, record + 16, platform)
        if 0 < offset and 0 < size and offset + size <= len(data):
            entry["offset"] = offset
            entry["size"] = size
            entry["kind"] = kind_at(data, offset)
        if types is not None and types + i * 4 + 4 <= len(data):
            entry["subtype"] = struct.unpack_from(_fmt(platform) + "h", data, types + i * 4)[0]
            entry["group"] = struct.unpack_from(_fmt(platform) + "h", data, types + i * 4 + 2)[0]

    if directory is not None and indexes is not None:
        end = directory + min(directory_size, len(data) - directory)
        paths = _parse_directory(data, directory, end)
        for order, encoded in enumerate(paths):
            if order >= num_entries:
                break
            if num_entries < 0x100:
                entry_index = data[indexes + order]
            elif num_entries < 0x10000:
                entry_index = struct.unpack_from(_fmt(platform) + "H", data, indexes + order * 2)[0]
            else:
                entry_index = u32_at(data, indexes + order * 4, platform)
            if entry_index >= num_entries:
                continue
            entry = entries[entry_index]
            entry["path"] = decode_bank_path(encoded)
            entry["filename"] = entry["path"].split("\\")[-1]
            dot = entry["filename"].rfind(".")
            if dot >= 0:
                entry["name"] = entry["filename"][:dot]
                entry["ext"] = entry["filename"][dot + 1:].lower()
            else:
                entry["name"] = entry["filename"]

    return {"header": header, "entries": entries, "data": data}


def parse_file(path, platform):
    with open(path, "rb") as handle:
        return parse(handle.read(), platform)


if __name__ == "__main__":
    import sys
    platform = X360 if sys.argv[1] == "360" else PC
    bank = parse_file(sys.argv[2], platform)
    header = bank["header"]
    print("%s: %d entries, created %d, data alignment %d" % (
        sys.argv[2], header["numEntries"], header["creationDate"], header["dataAlignment"]))
    for entry in bank["entries"]:
        print("  %-34s %9d  %s" % (entry["filename"], entry["size"], entry["kind"]))

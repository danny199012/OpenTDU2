#!/usr/bin/env python3
"""Dump an Eden key map .xmb (ARRAY_OF_Key records with ActionName/ChannelName)."""
import struct
import sys


def main(path):
    data = open(path, "rb").read()
    assert data[:4] == b"XMBF"
    str_off, type_off, data_off = struct.unpack_from("<III", data, 8)

    def cstr(off):
        end = data.index(b"\0", off)
        return data[off:end].decode("utf-8", "replace")

    # Walk the type table to find the composite with an ActionName field.
    # Types: u32 TypeWord, u32 NameOffset records; composite records are
    # followed by one {NameOffset, TypeWord} (LE swap) record per field.
    p = type_off
    types = []  # (name, fields[(name, typeword)])
    while p + 8 <= data_off:
        w1, name_off = struct.unpack_from("<II", data, p)
        lo = w1 & 0xFFFF
        hi = (w1 >> 16) & 0xFFFF
        is_composite = (lo & 0xF) == 0 and lo != 0
        num_fields = (lo >> 4) & 0x7FF
        name = cstr(str_off + name_off)
        if is_composite and num_fields > 0 and hi > 0:
            fields = []
            ok = True
            q = p + 8
            for i in range(num_fields):
                if q + 8 > data_off:
                    ok = False
                    break
                fname_off, fw = struct.unpack_from("<II", data, q)
                fields.append((cstr(str_off + fname_off), fw))
                q += 8
            if ok:
                types.append((name, fields, p))
                p = q
                continue
        p += 8
    key_type = None
    for name, fields, pos in types:
        fnames = [f[0] for f in fields]
        if "ActionName" in fnames:
            key_type = (name, fields)
            print(f"Key struct '{name}': {fields}")
            break
    if key_type is None:
        print("no ActionName struct found; types:")
        for name, fields, pos in types:
            print("  ", name, [f[0] for f in fields])
        return

    # The root data starts at data_off; find the array of Key structs.
    # Struct size: hi of the TypeWord: for the Key composite, byte size =
    # hi*2 (+1 if lo & 0x8000). Recompute from the type word.
    # Simpler: brute force — the root is ARRAY_OF_Key {count, offset}.
    root_ref = struct.unpack_from("<I", data, 0x18)[0]
    # root type fields: find type at index root_ref>>1 by order in the table
    # (types list order == table order for composites only; primitives were
    # skipped, so index lookup is unreliable). Brute force: scan the data
    # section start for {count, offset} that yields plausible strings.
    count, aoff = struct.unpack_from("<II", data, data_off)
    base = data_off + aoff
    # struct size guess from type word of the Key type
    # find its TypeWord again
    for name, fields, pos in types:
        fnames = [f[0] for f in fields]
        if "ActionName" in fnames:
            w1 = struct.unpack_from("<I", data, pos)[0]
            lo = w1 & 0xFFFF
            hi = (w1 >> 16) & 0xFFFF
            size = hi * 2 + (1 if (lo & 0x8000) else 0)
            break
    print(f"array count {count}, struct size {size}")
    for i in range(count):
        rec = base + i * size
        values = {}
        for j, (fname, fw) in enumerate(fields):
            is_array_field = (fw & 0x80000000) != 0
            lo = fw & 0xFFFF
            prim = lo & 0x7FFF
            off = 0 if j == 0 else None
            # field offsets: sequential packed; need offsets from table.
            pass
        # offsets unknown here: fall back to printing the whole record's
        # leading string pointers in field order using packed layout from the
        # schema dumped by otdu2_bnk_tool instead.
    # --- simpler: rely on sequential u32 fields (ActionName, ChannelName,
    # ChannelNameAND are strings = u32 offsets; ints between). Print the raw
    # dwords and resolve strings.
    for i in range(count):
        rec = base + i * size
        dwords = struct.unpack_from("<" + "I" * (size // 4), data, rec)
        parts = []
        for d in dwords:
            if 0 < d < len(data) and data[data_off + d] in range(0x20, 0x7F):
                parts.append(cstr(data_off + d))
            else:
                parts.append(str(d if d < 0x7FFFFFFF else -1))
        print(" | ".join(parts))


if __name__ == "__main__":
    main(sys.argv[1])

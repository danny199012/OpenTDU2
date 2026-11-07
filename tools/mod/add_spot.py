#!/usr/bin/env python3
"""Add a spot (a building placement) to an island through the mods overlay.

Why this exists: OpenTDU2's `spots.bin` lists every building of an island (one
0x110-byte record each: `u64 id`, `char bank[8]`, `vec3 position`, `f32`,
`f32 heading`, the category byte at +0x100 - docs/re/spot_exteriors_spec.md §1).
The port resolves every read through the **mods overlay** first
(`<user data>/mods/<game path>`, `-mods <dir>`, `OTDU2_MODS_DIR`, `-nomods`;
source/filesystem/gs_file.h), so a copy of `spots.bin` in the mods folder with one
extra record adds a building - the game data is only ever *read*.

The exterior of a spot is `islands/<island>/exterior/<bank lowercased>.bnk`; the
port looks for that file through the overlay too, so `--bank` drops a bank of our
own in there.

Verified (docs/re/modding.md): `SpotExteriors: EGAS_A at (12665, 16698): 2 models,
1 placements` with the record appended here and a copied bank, no engine change.

The id is `GetIdentifier64bit` (source/shared.h): 8 byte slots, uppercase(c) added
into slot `i & 7` - it is lossy, so any unique name works.
"""
import argparse
import os
import shutil
import struct
import sys

RECORD = 0x110      # bytes per spot record
HEADER = 0x20       # records start here
SIZE_AT = 0x14      # u32: bytes of the file after its first 16 bytes


def ident64(name):
    slots = [0] * 8
    for i, ch in enumerate(name):
        c = ord(ch)
        if 0x60 < c < 0x7B:
            c -= 0x20
        slots[i & 7] = (slots[i & 7] + c) & 0xFF
    out = 0
    for i in range(8):
        out |= slots[i] << (i * 8)
    return out


def read_records(data):
    size = struct.unpack_from('<I', data, SIZE_AT)[0]
    count = min(size, len(data) - HEADER) // RECORD
    out = []
    for i in range(count):
        o = HEADER + i * RECORD
        bank = data[o + 8:o + 16].split(b'\0')[0].decode()
        out.append({
            'index': i,
            'id': struct.unpack_from('<Q', data, o)[0],
            'bank': bank,
            'pos': struct.unpack_from('<3f', data, o + 0x10),
            'heading': struct.unpack_from('<f', data, o + 0x20)[0],
            'category': data[o + 0x100],
        })
    return out, size


def main():
    ap = argparse.ArgumentParser(description='Add a spot through the mods overlay.')
    ap.add_argument('--name', required=True, help='building name, 8 chars (the bank field), e.g. EGAS_A')
    ap.add_argument('--x', type=float, required=True)
    ap.add_argument('--z', type=float, required=True)
    ap.add_argument('--y', type=float, default=None,
                    help='height; default = the nearest existing spot (the terrain height there)')
    ap.add_argument('--heading', type=float, default=0.0, help='radians')
    ap.add_argument('--category', type=int, default=4,
                    help='the spots.bin category byte at +0x100 (map icon/tab; 4 = shop, 8 = the E*_ shop family)')
    ap.add_argument('--bank', default=None,
                    help='a .bnk to copy to islands/<island>/exterior/<name>.bnk (our own building, or a stand-in)')
    ap.add_argument('--island', default='ibiza')
    ap.add_argument('--game', default=r'C:\TDU2-Gamedata\TestDrive2', help='the game root (read only)')
    ap.add_argument('--mods', default=None, help='default: <user data>/mods of the -userdata folder you play with')
    args = ap.parse_args()

    if len(args.name) > 8:
        sys.exit('--name must be 8 characters or fewer (the record holds 8)')
    game_bank = os.path.join(args.game, 'Euro', 'Bnk')
    src = os.path.join(game_bank, 'islands', args.island, 'level', 'spots.bin')
    if not os.path.isfile(src):
        sys.exit('no spots.bin at %s (check --game)' % src)
    mods = args.mods or r'C:\temp\otdu2-cline-userdata\mods'
    mod_bank = os.path.join(mods, 'Euro', 'Bnk')

    data = bytearray(open(src, 'rb').read())
    if data[:4] != b'.SPT':
        sys.exit('%s is not a spot list' % src)
    records, size = read_records(data)
    print('spots.bin: %d records' % len(records))
    if any(r['bank'].lower() == args.name.lower() for r in records):
        sys.exit("'%s' is already in the list" % args.name)

    y = args.y
    if y is None:
        nearest = min(records, key=lambda r: (r['pos'][0] - args.x) ** 2 + (r['pos'][2] - args.z) ** 2)
        y = nearest['pos'][1]
        print('height %.1f taken from the nearest spot (%s)' % (y, nearest['bank']))

    template = next((r for r in records if r['category'] == args.category), records[0])
    record = bytearray(data[HEADER + template['index'] * RECORD:HEADER + (template['index'] + 1) * RECORD])
    spot_name = '%s_%d' % (args.name, int(args.x))
    struct.pack_into('<Q', record, 0, ident64(spot_name))
    record[8:16] = args.name.encode().ljust(8, b'\0')
    struct.pack_into('<3f', record, 0x10, args.x, y, args.z)
    struct.pack_into('<f', record, 0x20, args.heading)
    record[0x100] = args.category

    out = bytearray(data[:HEADER + size])
    out += record
    struct.pack_into('<I', out, SIZE_AT, size + RECORD)

    level_dir = os.path.join(mod_bank, 'islands', args.island, 'level')
    os.makedirs(level_dir, exist_ok=True)
    dst = os.path.join(level_dir, 'spots.bin')
    open(dst, 'wb').write(out)
    print('wrote %s: %d records (was %d)' % (dst, len(records) + 1, len(records)))
    print("spot '%s' id 0x%X at (%.1f, %.1f, %.1f) heading %.3f category %d" %
          (spot_name, ident64(spot_name), args.x, y, args.z, args.heading, args.category))

    if args.bank:
        ext_dir = os.path.join(mod_bank, 'islands', args.island, 'exterior')
        os.makedirs(ext_dir, exist_ok=True)
        bank_dst = os.path.join(ext_dir, args.name.lower() + '.bnk')
        shutil.copyfile(args.bank, bank_dst)
        print('wrote %s' % bank_dst)
    else:
        print('no --bank: the spot will be listed but nothing is drawn there')


if __name__ == '__main__':
    main()

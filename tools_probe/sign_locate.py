"""Where is the direction sign that says X: text + world placement of RS_Dir*.

The direction signs of an island are placed by the sector `.pmi` "A" lists
(docs/re/pmi_spec.md 2.2): every sign is one **type-4** record, and that single
record carries both the placement (position / rotation words, 1.3) and the plate
text payload the port composes onto the blank plates (docs/re/sign_text_spec.md
2). So the text and the world position come from the same record - this probe
lists them together, which is what a "where does the game put this sign" answer
needs.

    python tools_probe/sign_locate.py [C:\\TDU2-Gamedata\\TestDrive2]
                                      [-island ibiza] [-match Centre,Platja]
                                      [-spawn [m]] [-csv _signs.csv] [-quiet]

  -match a,b   only the signs whose decoded plate rows contain one of the terms
               (case and accent insensitive substring, e.g. `platja` matches
               "PLATJA" / "Platja")
  -spawn [m]   also print the FREERIDE command line that parks the car m metres
               in front of the plate, facing it (default 15, the `-atsign`
               distance; the plate faces the sign's transformed -Z, so the
               spawn is `sign - forward * m` with `forward = (sin yaw, cos yaw)`)
  -csv path    write every listed sign as CSV (scratch file, `_*` is ignored)
  -quiet       drop the side / variant / hash line under each sign

Positions are world metres in the game's raw D3D frame
(`sectorOrigin + local`, pmi_spec 3): the same numbers `-tp x,z` takes and the
coords overlay (F6) shows. Yaw is the type-4 record's Y rotation in degrees
(0 = +Z, 90 = +X), the heading `-heading <deg>` takes.

Actor names: the type-4 MOB hashcode is the actor **entity** hash of
`worldactors.bnk` `entities.xmb` (`__Hc` -> `__Name`, the map the game resolves
signs through); a name like `RS_DirL_D_act` gives the side and the plate layout
variant letter. Signs whose hash is not in the map (or is not an `RS_Dir*`
actor: speed plates, STOP...) are still listed, tagged `?`.

Read-only: nothing is written except the optional CSV.
"""
import glob
import math
import os
import struct
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sign_probe import load_bank  # noqa: E402  (the KNAB reader of the probes)

K_POSITION = 0.0156252384185791  # pmi_spec 1.3, 0xf4ae38
K_POSITION_OFFSET = 128.0        # 0xf49e08
K_ROTATION = 9.5875264e-05       # 2 pi / 65535, 0xf4ae40
K_SECTOR = 768.0                 # eden_world.h kSectorSize
K_SECTORS_PER_AREA = 8


# Row-leading glyph codes that are not letters: the retail face stamps an icon
# at the start of the row. Each code prefixes exactly one destination word over
# the whole Ibiza census (2247 records [DATA]), so the letter is that
# destination's pictogram - the beach umbrella of the "Platja" row is 0x4b.
ROW_ICONS = {0x48: 'port', 0x49: 'oldtown', 0x4a: 'airport', 0x4b: 'beach'}


# Distance bytes extend the digit charset: 0x1b..0x24 = 0..9, 0x25..0x2a =
# 10..15 (sign_text.cpp decodeCharset). The 10..15 reading was confirmed
# against the island: see the decodeCharset comment there.
def decode_charset(byte):
    if byte == 0:
        return ' '
    if byte <= 0x1A:
        return chr(ord('A') + byte - 1)
    if byte <= 0x24:
        return chr(ord('0') + byte - 0x1B)
    if byte <= 0x2A:
        return '1' + str(byte - 0x25)  # 10..15
    if byte <= 0x44:
        return chr(ord('a') + byte - 0x2B)
    if byte == 0x47:
        return '\u00f1'
    if byte == 0x50:
        return '\u00c0'
    return None


def decode_window(window):
    out = []
    for byte in window:
        char = decode_charset(byte)
        if char is not None:
            out.append(char)
        elif byte in ROW_ICONS:
            out.append(f'[{ROW_ICONS[byte]}] ')
        else:
            out.append(f'[byte {byte:02x}]')
    return ''.join(out).strip()


def decode_distance(byte):
    return decode_charset(byte) if 0x1B <= byte <= 0x2A else ''


# Row windows per payload size (sign_text.cpp decodePayload switch).
ROW_WINDOWS = {
    19: [(0, 18, 0)],
    20: [(0, 18, 18)],
    22: [(0, 20, 20)],
    37: [(0, 18, 0), (18, 18, 0)],
    39: [(0, 18, 18), (19, 18, 37)],
    43: [(0, 20, 20), (21, 20, 42)],
    55: [(0, 18, 0), (18, 18, 0), (36, 18, 0)],
    58: [(0, 18, 18), (19, 18, 37), (38, 18, 56)],
    64: [(0, 20, 20), (21, 20, 42), (42, 20, 63)],
}


def decode_rows(payload):
    """[(name, distance)] of a type-4 payload (sign_text_spec 2)."""
    size = len(payload)
    if size < 2:
        return []
    code = payload[0]
    body = payload[1:]
    rows = []
    for offset, window, distance_offset in ROW_WINDOWS.get(size, [(0, size - 1, 0)]):
        if offset + window > len(body):
            continue
        name = decode_window(body[offset:offset + window])
        distance = decode_distance(body[distance_offset]) if 0 < distance_offset < len(body) else ''
        if name:
            rows.append((name, distance))
    # Faces without trailing distance bytes carry it in the layout code.
    if size in (19, 37, 55) and rows and not rows[0][1] and 0x1B <= code <= 0x2A:
        rows[0] = (rows[0][0], decode_distance(code))
    return rows


# ------------------------------------------------------- A .pmi type-4 records
def pmi_type4_records(data):
    """(hashcode, words[16], heightMin, heightMax, payload) of every type-4
    record of a `-a.pmi` (pmi_spec 1.2 / 1.3)."""
    if len(data) < 0x1D0 or data[0x0C:0x10] != b'.PMI':
        return
    num_mob, hmin, hmax = struct.unpack_from('<Iff', data, 0x20)
    off = 0x10 + struct.unpack_from('<I', data, 0x1C)[0]
    for _ in range(num_mob):
        if data[off:off + 4] != b'MOB.':
            return
        size, = struct.unpack_from('<I', data, off + 8)
        hashcode, = struct.unpack_from('<Q', data, off + 0x10)
        count, extra, _unk = struct.unpack_from('<HHH', data, off + 0x18)
        ptype = data[off + 0x1E]
        stride = 0x20 + ((extra + 15) & ~15)
        if ptype == 4 and extra > 0:
            for i in range(count):
                rec = off + 0x20 + i * stride
                words = struct.unpack_from('<16H', data, rec)
                payload = bytes(data[rec + 0x20:rec + 0x20 + extra])
                yield hashcode, words, hmin, hmax, payload
        off += size


def sector_origin(bank_path):
    """World origin of `sector-<ax>-<ay>-<sx>-<sy>.bnk` (eden_world.cpp
    getSectorOrigin: the rows are staggered like bricks)."""
    parts = os.path.splitext(os.path.basename(bank_path))[0].split('-')
    if len(parts) != 5 or parts[0].lower() != 'sector':
        return None, None
    try:
        ax, ay, sx, sy = (int(p) for p in parts[1:])
    except ValueError:
        return None, None
    x = (ax * K_SECTORS_PER_AREA + sx) * K_SECTOR + (K_SECTOR * 0.5 if sy & 1 else 0.0)
    return x, (ay * K_SECTORS_PER_AREA + sy) * K_SECTOR


def record_placement(words, hmin, hmax, origin_x, origin_z):
    """(x, y, z, yaw) of one type-4 record, world metres / radians."""
    return (origin_x + words[0] * K_POSITION - K_POSITION_OFFSET,
            hmin + words[1] * (hmax - hmin) / 65535.0,
            origin_z + words[2] * K_POSITION - K_POSITION_OFFSET,
            words[7] * K_ROTATION)


# ------------------------------------------------------------ actor __Hc map
def load_actor_names(root, island):
    """{__Hc: __Name} of worldactors.bnk entities.xmb (the RS_Dir* actors)."""
    bank_path = os.path.join(root, 'Euro', 'Bnk', 'islands', island, 'scenarics', 'packs', 'worldactors.bnk')
    if not os.path.isfile(bank_path):
        return {}
    try:
        from db_probe import Xmb
    except Exception:
        return {}
    entities = None
    for path, data in load_bank(bank_path).items():
        if os.path.basename(path).lower() == 'entities.xmb':
            entities = data
            break
    if not entities:
        return {}
    # db_probe.Xmb takes a path: hand it the extracted bytes through a scratch
    # file next to the probes (`_*` is gitignored).
    scratch = os.path.join(os.path.dirname(os.path.abspath(__file__)), '_sign_entities.xmb')
    with open(scratch, 'wb') as f:
        f.write(entities)
    try:
        xmb = Xmb(scratch)
    except Exception:
        return {}
    finally:
        try:
            os.remove(scratch)
        except OSError:
            pass

    arrays = []

    def visit(type_index, offset, depth=0):
        if depth > 8:
            return
        name, comp, size, prim, fields = xmb.types[type_index]
        if not comp or not fields:
            return
        for fname, ftype, is_array, foffset in fields:
            if is_array:
                arrays.append((fname, ftype, xmb.u('<I', offset + foffset),
                               xmb.u('<I', offset + foffset + 4)))
            elif xmb.types[ftype][1]:
                visit(ftype, offset + foffset, depth + 1)

    visit(xmb.root_type, 0)
    names = {}
    for fname, row_type, count, array_offset in arrays:
        if 'Entity__Actor' not in fname:
            continue
        row_fields = {f[0]: f[3] for f in xmb.types[row_type][4] or []}
        if '__Hc' not in row_fields or '__Name' not in row_fields:
            continue
        row_size = xmb.types[row_type][2]
        for i in range(count):
            row = array_offset + i * row_size
            hashcode = xmb.u('<Q', row + row_fields['__Hc'])
            names[hashcode] = xmb.cstr(xmb.data_off + xmb.u('<I', row + row_fields['__Name']))
    return names


# --------------------------------------------------------------------- main
def normalize(text):
    decomposed = unicodedata.normalize('NFD', text)
    return ''.join(c for c in decomposed if unicodedata.category(c) != 'Mn').lower()


def row_text(rows):
    return ' / '.join(f'{name} {distance}'.strip() for name, distance in rows)


def describe(sign):
    """`RS_DirL_D_act` -> side L, variant d; `RS_TdirIb_B_act` -> variant b, no
    side (the TdirIb* panels are not handed, sign_text_spec 0)."""
    parts = sign['actor'].split('_')
    if len(parts) < 3:
        return 'unknown actor'
    side = parts[1][-1].upper() if parts[1][-1] in 'LR' else '-'
    return f'side {side}  variant {parts[2].lower()}'


def collect(level, actor_names):
    """Every type-4 sign record of the island's sector banks, in sector order."""
    found = []
    sectors = 0
    for area_dir in sorted(glob.glob(os.path.join(level, 'area-*'))):
        for sub_dir in sorted(glob.glob(os.path.join(area_dir, 'area-*'))):
            for bank_path in sorted(glob.glob(os.path.join(sub_dir, '*.bnk'))):
                origin_x, origin_z = sector_origin(bank_path)
                if origin_x is None:
                    continue
                try:
                    entries = load_bank(bank_path)
                except Exception:
                    continue
                sectors += 1
                for path, data in entries.items():
                    if not path.endswith('-a.pmi'):
                        continue
                    for hashcode, words, hmin, hmax, payload in pmi_type4_records(data):
                        rows = decode_rows(payload)
                        if not rows:
                            continue
                        x, y, z, yaw = record_placement(words, hmin, hmax, origin_x, origin_z)
                        found.append({
                            'sector': os.path.splitext(os.path.basename(bank_path))[0],
                            'x': x, 'y': y, 'z': z, 'yaw': math.degrees(yaw) % 360.0,
                            'actor': actor_names.get(hashcode, ''), 'hash': hashcode, 'rows': rows,
                        })
    return found, sectors


def main():
    arguments = list(sys.argv[1:])
    root = r'C:\TDU2-Gamedata\TestDrive2'
    if arguments and not arguments[0].startswith('-'):
        root = arguments.pop(0)
    island = arguments[arguments.index('-island') + 1] if '-island' in arguments else 'ibiza'
    terms = None
    if '-match' in arguments:
        terms = [normalize(t) for t in arguments[arguments.index('-match') + 1].split(',') if t]
    spawn_distance = None
    if '-spawn' in arguments:
        index = arguments.index('-spawn')
        spawn_distance = 15.0
        if index + 1 < len(arguments) and not arguments[index + 1].startswith('-'):
            spawn_distance = float(arguments[index + 1])
    csv_path = arguments[arguments.index('-csv') + 1] if '-csv' in arguments else None
    quiet = '-quiet' in arguments

    level = os.path.join(root, 'Euro', 'Bnk', 'islands', island, 'level')
    if not os.path.isdir(level):
        print(f'no level folder: {level}')
        return 1

    actor_names = load_actor_names(root, island)
    print(f'=== {island}: direction signs of the sector A lists (type 4) ===')
    print(f'  actor map: {len(actor_names)} entities.xmb actors')
    found, sectors = collect(level, actor_names)
    print(f'  {sectors} sector banks scanned, {len(found)} type-4 records with text')

    matches = found
    if terms is not None:
        matches = [f for f in found if any(t in normalize(row_text(f['rows'])) for t in terms)]
    print(f'  {len(matches)} listed' + (f' matching "{arguments[arguments.index("-match") + 1]}"' if terms else '') + '\n')

    for sign in matches:
        print(f'  [{sign["actor"] or "?"}] ({sign["x"]:.1f}, {sign["y"]:.1f}, {sign["z"]:.1f})  '
              f'yaw {sign["yaw"]:.0f}  {sign["sector"]}')
        print(f'      "{row_text(sign["rows"])}"')
        if not quiet:
            print(f'      {describe(sign)}  hash {sign["hash"]:016x}')
        if spawn_distance is not None:
            forward_x = math.sin(math.radians(sign['yaw']))
            forward_z = math.cos(math.radians(sign['yaw']))
            print(f'      -gm freeride -island {island} -tp '
                  f'{sign["x"] - forward_x * spawn_distance:.1f},{sign["z"] - forward_z * spawn_distance:.1f} '
                  f'-heading {sign["yaw"]:.1f}')

    if csv_path:
        with open(csv_path, 'w', encoding='utf-8') as f:
            f.write('sector,x,y,z,yaw,actor,rows\n')
            for sign in matches:
                f.write(f'{sign["sector"]},{sign["x"]:.2f},{sign["y"]:.2f},{sign["z"]:.2f},'
                        f'{sign["yaw"]:.1f},{sign["actor"]},"{row_text(sign["rows"])}"\n')
        print(f'\n  wrote {csv_path}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

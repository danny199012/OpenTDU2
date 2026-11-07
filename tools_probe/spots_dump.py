import struct, os, sys, collections

BASE = r'C:\TDU2-Gamedata\TestDrive2\Euro\Bnk\islands'

def sectors_present(island, pos):
    # 3x3 sector check around the spot (EdenWorld::getSectorAt: 16x16 areas
    # of 16 sectors, sector = 768 m; approximated like the viewer).
    ax = int(pos[0] // (768 * 16)); ay = int(pos[2] // (768 * 16))
    x = int((pos[0] % (768 * 16)) // 768); y = int((pos[2] % (768 * 16)) // 768)
    cx = ax * 16 + x; cy = ay * 16 + y
    n = 0
    for row in range(cy - 1, cy + 2):
        for col in range(cx - 1, cx + 2):
            if row < 0 or col < 0:
                continue
            a_x, a_y, s_x, s_y = col // 16, row // 16, col % 16, row % 16
            p = os.path.join(BASE, island, 'level', f'sector-{a_x}-{a_y}-{s_x}-{s_y}.bnk')
            if os.path.exists(p):
                n += 1
    return n

for island in ['ibiza', 'hawai', 'casino']:
    path = os.path.join(BASE, island, 'level', 'spots.bin')
    if not os.path.exists(path):
        print(f'{island}: no spots.bin')
        continue
    data = open(path, 'rb').read()
    if len(data) < 0x20 or data[0x10:0x14] != b'SPTA':
        print(f'{island}: bad spots.bin')
        continue
    size, = struct.unpack_from('<I', data, 0x14)
    spots = []
    off = 0x20
    while off + 0x110 <= len(data) and off < 0x20 + size:
        sid, = struct.unpack_from('<Q', data, off)
        bank = data[off + 8:off + 16].split(b'\0')[0].decode('latin1')
        pos = struct.unpack_from('<3f', data, off + 0x10)
        heading, = struct.unpack_from('<f', data, off + 0x20)
        spots.append((bank, pos, heading))
        off += 0x110
    print(f'\n=== {island}: {len(spots)} spots ===')
    groups = collections.defaultdict(list)
    for bank, pos, heading in spots:
        prefix = bank.split('_')[0] if '_' in bank else bank[:4]
        groups[prefix].append((bank, pos, heading))
    for prefix in sorted(groups):
        lst = groups[prefix]
        print(f'  {prefix}: {len(lst)}')
        for bank, pos, heading in sorted(lst)[:40]:
            covered = sectors_present(island, pos)
            print(f'    {bank:24s} ({pos[0]:9.1f}, {pos[1]:7.1f}, {pos[2]:9.1f}) hdg {heading:5.2f} sectors {covered}/9')

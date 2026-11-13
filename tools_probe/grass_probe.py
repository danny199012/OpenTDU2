# probe: decode the per-sector grass grids (.pgr).
# Container: magic "RLE." at 0x0c, section "DATA" at 0x10, { u32 version, u32 dsize, u32 0 },
# payload: u16 gridW, u16 gridH, u32 gridLog2, then gridH u32 row offsets (file-absolute),
# each row = RLE (u8 count, u8 value) pairs summing to gridW cells.
# Values look like GrassType ids >> 2 (11 -> 44 PinsSauvages_OffRoad_2Medium), 255 = none.
# Usage:
#   python -I tools_probe/grass_probe.py <file.pgr> [--render out.png] [--rows N] [--abs|norel]
import sys, struct, zlib
from collections import Counter

def decode(path, render_path=None, row_limit=0, relative=False):
    d = open(path, 'rb').read()
    magic, sec = d[0x0c:0x10], d[0x10:0x14]
    ver, dsize, zero = struct.unpack_from('<III', d, 0x14)
    gw, gh, log2 = struct.unpack_from('<HHI', d, 0x20)
    print('%s: %d bytes magic=%s sec=%s ver=%d grid=%dx%d log2=%d' % (
        path, len(d), magic, sec, ver, gw, gh, log2))
    table = 0x28
    rows = []
    hist = Counter()
    base = 0x20  # offsets are relative to the payload start (file 0x20) - grass_spec.md 2
    n = min(gh, row_limit) if row_limit else gh
    incomplete = 0
    for r in range(n):
        o, = struct.unpack_from('<I', d, table + r * 4)
        o += base
        cells = []
        while len(cells) < gw and o + 2 <= len(d):
            c, v = d[o], d[o + 1]
            o += 2
            cells.extend([v] * (c + 1))  # a run is count + 1 cells (every row sums to gridW)
        if len(cells) < gw:
            incomplete += 1
            rows.append(None)
            continue
        rows.append(cells[:gw])
        hist.update(cells[:gw])
    ok = n - incomplete
    print('  rows decoded: %d/%d incomplete (relative=%s)' % (ok, n, relative))
    if hist:
        print('  value histogram (top 12): %s' % ', '.join(
            '0x%02x(x%d -> id %d)' % (v, c, v * 4 if v not in (0, 255) else -1)
            for v, c in hist.most_common(12)))
    if render_path and ok == n and n:
        render(rows, gw, render_path)
    return rows

def render(rows, w, out):
    h = len(rows)
    PALETTE = {
        0: (30, 30, 30),      # none?
        255: (10, 10, 10),    # sentinel
        1: (60, 120, 40), 2: (80, 160, 50), 3: (100, 190, 60),
        8: (120, 220, 70), 10: (160, 200, 60), 11: (200, 120, 40),
        16: (220, 220, 90), 24: (230, 160, 60), 28: (240, 100, 60),
    }
    def colour(v):
        if v in PALETTE:
            return PALETTE[v]
        x = (v * 2654435761 + 1) & 0xffffffff
        return (x & 255, (x >> 8) & 255, (x >> 16) & 255)
    raw = bytearray(w * h * 3)
    for y, row in enumerate(rows):
        base = y * w * 3
        for x, v in enumerate(row):
            r, g, b = colour(v)
            i = base + x * 3
            raw[i:i + 3] = bytes([r, g, b])
    from PIL import Image
    img = Image.frombytes('RGB', (w, h), bytes(raw))
    img.save(out)
    print('rendered %s (%dx%d)' % (out, w, h))

if __name__ == '__main__':
    args = sys.argv[1:]
    out = None
    rows = 0
    rel = False
    if '--render' in args:
        i = args.index('--render'); out = args[i + 1]; del args[i:i + 2]
    if '--rows' in args:
        i = args.index('--rows'); rows = int(args[i + 1]); del args[i:i + 2]
    if '--rel' in args:
        rel = True; args.remove('--rel')
    decode(args[0], out, rows, rel)

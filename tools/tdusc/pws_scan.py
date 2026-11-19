#!/usr/bin/env python3
"""Scan a TDUSC .PWS world tile for embedded GDeflate streams (docs/re/tdusc_pws_format.md).

A PWS is a container: plain structured sections (index arrays, repeated 256-byte records) with GDeflate
TileStreams embedded (header `04 fb <u16 tiles>`). This lists every candidate and decodes it.
    python pws_scan.py <file.PWS> [--out DIR] [--max-bytes N]
"""
import argparse, os, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gdeflate

def candidates(d):
    for m in re.finditer(rb'\x04\xfb([\x01-\xff])\x00', d):
        o = m.start()
        tiles = d[o + 2]
        if o + 8 + 4 * tiles > len(d):
            continue
        word = struct.unpack_from('<I', d, o + 4)[0]
        if (word & 3) != 1 or (word >> 20):
            continue
        offs = struct.unpack_from('<%dI' % tiles, d, o + 8)
        if offs[0] == 0 or o + 8 + 4 * tiles + offs[0] > len(d) + 64:
            continue
        if any(offs[i] >= offs[i + 1] for i in range(1, tiles - 1)):
            continue
        yield o, tiles, (word >> 2) & 0x3ffff, offs

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pws'); ap.add_argument('--out'); ap.add_argument('--max-bytes', type=int, default=200000)
    a = ap.parse_args()
    d = open(a.pws, 'rb').read()
    ok = bad = 0
    for o, tiles, last, offs in candidates(d):
        end = o + 8 + 4 * tiles + (offs[-1] if tiles > 1 else 0) + offs[0]
        if end - o > a.max_bytes:
            print('%#x tiles=%d comp~%d SKIP(big)' % (o, tiles, end - o)); continue
        try:
            r = gdeflate.decompress(d[o:end])
        except Exception as e:
            bad += 1; print('%#x tiles=%d FAIL %s' % (o, tiles, e)); continue
        ok += 1
        print('%#x tiles=%d comp=%d -> %d  %s' % (o, tiles, end - o, len(r), r[:16].hex(' ')))
        if a.out:
            os.makedirs(a.out, exist_ok=True)
            open(os.path.join(a.out, '%08x.bin' % o), 'wb').write(r)
    print('ok', ok, 'bad', bad)

if __name__ == '__main__':
    main()

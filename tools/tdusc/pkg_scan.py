#!/usr/bin/env python3
"""Read-only lister for TDUSC `.PKG` archives (docs/re/tdusc_pkg_format.md).

Scans a package for `PKGB` records and writes a CSV summary (offset, kind, sizes, header fields) into an
output folder you point it at. It never writes into the game folder and never extracts asset data; with
`--payloads` it additionally decodes each LZ4 record in memory to read its `PA__` header.

    python pkg_scan.py --pkg "<TDUSC>/WIN32/PKG/CHUNK_0/CHUNK_0_PART0005.PKG" --out C:/temp/tdusc-scan --limit 2000

With `--toc` it decodes the package's table of contents (a 32-byte repeating text key masks it; the key is a
property of the file format, recovered from the zero-filled cells of the table itself) and writes
`<part>_toc.csv` with the path, absolute offset, frame size, decoded size, kind, timestamp and hash of every
entry. Entries: u32 pathLen, path, then 88 bytes {u32 flag, u64 offset, u64 FILETIME, u64 frameSize,
u64 decodedSize, u32 kind(2 = LZ4, 0 = stored), 16 byte hash, 32 zero bytes}.

Formats [DATA, session 58]:
  * records start at `PKGB` (4 bytes) immediately before a 4 KiB page boundary; the gaps are padded with 0x30.
  * `PKGB` + LZ4 frame (04 22 4d 18) -> RIFF `PA__` { `Head` (30 bytes) , `Data` }   geometry / buffers
  * `PKGB` + `PATX-SP2` `ENDI` ... 'PC' 9 + a mip table                               textures
"""
import argparse
import csv
import mmap
import os
import struct
import sys

PAGE = 4096


def lz4_block(src):
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        tok = src[i]
        i += 1
        ll = tok >> 4
        if ll == 15:
            while True:
                b = src[i]
                i += 1
                ll += b
                if b != 255:
                    break
        out += src[i:i + ll]
        i += ll
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8)
        i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        st = len(out) - off
        for k in range(ml):
            out.append(out[st + k])
    return bytes(out)


def lz4_frame(buf, pos):
    """Decode one LZ4 frame at pos (the 04 22 4d 18 magic). Returns (data, end position)."""
    flg = buf[pos + 4]
    p = pos + 6
    if flg & 0x08:
        p += 8
    if flg & 0x01:
        p += 4
    p += 1
    out = bytearray()
    while True:
        bs = struct.unpack_from('<I', buf, p)[0]
        p += 4
        if bs == 0:
            break
        raw = bs & 0x80000000
        bs &= 0x7fffffff
        blk = bytes(buf[p:p + bs])
        p += bs
        if flg & 0x10:
            p += 4
        out += blk if raw else lz4_block(blk)
    if flg & 0x04:
        p += 4
    return bytes(out), p


TOC_KEY = b"oL7bFfNRojSzr4y7t6s7wWMIA0w03zDz"
TOC_BYTES = 8 << 20


def read_toc(path):
    """Decode a package's masked table of contents. Returns [(path, flag, offset, filetime, frame, size, kind, hash)]."""
    with open(path, 'rb') as fh:
        raw = fh.read(TOC_BYTES)
    key = (TOC_KEY * (len(raw) // 32 + 1))[:len(raw)]
    plain = bytes(a ^ b for a, b in zip(raw, key))
    out = []
    pos = 0
    marker = b'WIN32' + bytes([92])
    while True:
        pos = plain.find(marker, pos)
        if pos < 0:
            break
        n = struct.unpack_from('<I', plain, pos - 4)[0] if pos >= 4 else 0
        if 8 <= n <= 300 and all(32 <= c < 127 for c in plain[pos:pos + n]) and pos + n + 56 <= len(plain):
            t = pos + n
            flag, off, ft, fs, ds, kind = struct.unpack_from('<IQQQQI', plain, t)
            out.append((plain[pos:pos + n].decode(), flag, off, ft, fs, ds, kind, plain[t + 44:t + 60].hex()))
            pos = t
        else:
            pos += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pkg', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--limit', type=int, default=0, help='stop after N records')
    ap.add_argument('--toc', action='store_true', help='decode the table of contents instead of scanning records')
    ap.add_argument('--payloads', action='store_true', help='decode LZ4 records to read the PA__ header')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    name = os.path.splitext(os.path.basename(a.pkg))[0]
    if a.toc:
        entries = read_toc(a.pkg)
        path = os.path.join(a.out, name + '_toc.csv')
        with open(path, 'w', newline='') as f:
            wr = csv.writer(f)
            wr.writerow(['path', 'flag', 'offset', 'filetime', 'frame_bytes', 'decoded_bytes', 'kind', 'hash'])
            for e in entries:
                wr.writerow([e[0], e[1], hex(e[2]), e[3], e[4], e[5], e[6], e[7]])
        print('%d toc entries -> %s' % (len(entries), path))
        return
    rows = []
    with open(a.pkg, 'rb') as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        pos = 0
        while True:
            pos = mm.find(b'PKGB', pos)
            if pos < 0:
                break
            head = mm[pos + 4:pos + 16]
            if head[:4] == b'\x04\x22\x4d\x18':
                kind = 'lz4/PA__'
                info = ''
                size = 0
                if a.payloads:
                    try:
                        data, end = lz4_frame(mm, pos + 4)
                        hs = struct.unpack_from('<I', data, 16)[0]
                        info = 'form=%s head=%s data=%d' % (data[8:12].decode('latin1'),
                                                           data[20:20 + hs].hex(), len(data))
                        size = len(data)
                    except Exception as e:  # a false 'PKGB' hit inside data
                        pos += 4
                        continue
                rows.append((pos, kind, size, info))
            elif head[:8] == b'PATX-SP2':
                w, h = struct.unpack_from('<II', mm, pos + 0x30)
                rows.append((pos, 'patx-sp2', 0, 'w=%d h=%d' % (w, h)))
            else:
                pos += 4
                continue
            pos += 4
            if a.limit and len(rows) >= a.limit:
                break
    path = os.path.join(a.out, name + '_records.csv')
    with open(path, 'w', newline='') as f:
        wr = csv.writer(f)
        wr.writerow(['offset', 'kind', 'decoded_bytes', 'info'])
        for r in rows:
            wr.writerow([hex(r[0])] + list(r[1:]))
    print('%d records -> %s' % (len(rows), path))


if __name__ == '__main__':
    sys.exit(main())

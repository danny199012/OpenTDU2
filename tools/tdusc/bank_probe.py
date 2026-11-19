#!/usr/bin/env python3
"""Read-only lister for TDUSC `.bank` sound files (docs/re/tdusc_sound_banks.md).

Each TDUSC bank is a legacy FMOD Designer event file (RIFF `FEV`) with one FMOD Studio 2.x
`FSB5` (version 1) audio bank embedded somewhere inside it. The FSB5 layout (per vgmstream
`src/meta/fsb5.c`):

  * 0x04 version (0 or 1; TDUSC uses 1), 0x08 subsongs, 0x0C sample-header (entry table) size,
    0x10 name-table size, 0x14 sample-data size, 0x18 codec (0x0F = Vorbis, 0x10 = FADPCM, ...),
    0x3C + sample_header_size = name table (u32 relative offsets, then NUL-terminated names),
    entries themselves start at 0x3C and are variable size: u64 mode then chunks while bit 0 is set:
    num_samples = mode>>34 & 0x3FFFFFFF, data_offset = (mode>>7 & 0x07FFFFFF) << 5,
    channels = mode>>5 & 3 (1/2/6/8), rate = mode>>1 & 0x0F into the FMOD rate table
    (0=4000 ... 8=44100, 9=48000, 10=96000), chunks can override channels (0x01) and rate (0x02).
    chunk word: type = >>25 & 0x7F, size = >>1 & 0xFFFFFF, continue = & 1 (0x03 = loop info,
    0x0B = vorbis setup id + seek table).

Sanity: base header (0x3C) + entries + names + data == the whole FSB5 region; everything before it
is the FEV event data, everything after nothing (the FSB5 runs to end of file).

    python bank_probe.py --bank "<TDUSC>/WIN32/Sounds/VEHICLES/PERF_RC_AUDI_ETRON_GTR_AI.bank"
    python bank_probe.py --bank "<TDUSC>/WIN32/Sounds" --out C:/temp/tdusc-banks   # whole folder -> CSV
"""
import argparse
import csv
import mmap
import os
import struct
import sys

RATE_TABLE = [4000, 8000, 11000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 96000]

CODECS = {
    0x01: 'PCM8', 0x02: 'PCM16', 0x03: 'PCM24', 0x04: 'PCM32', 0x05: 'PCMFLOAT',
    0x06: 'GCADPCM', 0x07: 'IMAADPCM', 0x08: 'VAG', 0x09: 'HEVAG', 0x0A: 'XMA',
    0x0B: 'MPEG', 0x0C: 'CELT', 0x0D: 'AT9', 0x0E: 'XWMA', 0x0F: 'VORBIS',
    0x10: 'FADPCM', 0x11: 'OPUS',
}

CHUNK_NAMES = {0x01: 'channels', 0x02: 'rate', 0x03: 'loop', 0x04: 'comment',
               0x06: 'xma-seek', 0x07: 'dsp-coefs', 0x0B: 'vorbis-setup',
               0x0D: 'peak-volume', 0x0F: 'opus-size'}


def parse_fsb5(buf, base):
    """Parse one FSB5 (version 0/1) audio bank at `base`. Returns a dict or raises ValueError."""
    if buf[base:base + 4] != b'FSB5':
        raise ValueError('no FSB5 magic')
    version, subsongs, entry_bytes, name_bytes, data_bytes, codec = struct.unpack_from('<IIIIII', buf, base + 4)
    if version not in (0, 1):
        raise ValueError('FSB5 version %d' % version)
    hdr = 0x3C if version == 1 else 0x40
    end = base + hdr + entry_bytes + name_bytes + data_bytes
    if end > len(buf):
        raise ValueError('FSB5 extends past end of file (entries+names+data=%d)' %
                         (entry_bytes + name_bytes + data_bytes))
    out = {'base': base, 'version': version, 'subsongs': subsongs, 'codec': CODECS.get(codec, hex(codec)),
           'entry_bytes': entry_bytes, 'name_bytes': name_bytes, 'data_bytes': data_bytes,
           'runs_to_eof': end == len(buf), 'entries': [], 'names': []}
    names = None
    if name_bytes:
        start = base + hdr + entry_bytes  # name table: u32 offsets are relative to its start
        offs = struct.unpack_from('<%dI' % subsongs, buf, start) if subsongs else ()
        name_end = start + name_bytes
        names = []
        for rel in offs:
            p = start + rel
            if p >= min(name_end, len(buf)):
                names.append(None)
                continue
            s = buf[p:name_end].split(b'\x00', 1)[0]
            names.append(s.decode('latin1') if s else None)
    pos = base + hdr
    offset = 0
    for i in range(subsongs):
        if pos + 8 > base + hdr + entry_bytes:
            break
        mode, = struct.unpack_from('<Q', buf, pos)
        pos += 8
        samples = (mode >> 34) & 0x3FFFFFFF
        data_offset = ((mode >> 7) & 0x07FFFFFF) << 5
        channels = (1, 2, 6, 8)[(mode >> 5) & 3]
        rate = RATE_TABLE[(mode >> 1) & 0x0F] if (mode >> 1) & 0x0F < len(RATE_TABLE) else 0
        loop = None
        while mode & 1 and pos + 4 <= base + hdr + entry_bytes:
            word, = struct.unpack_from('<I', buf, pos)
            ctype, csize = (word >> 25) & 0x7F, (word >> 1) & 0xFFFFFF
            pos += 4
            if pos + csize > base + hdr + entry_bytes:
                break
            if ctype == 0x01 and csize >= 1:
                channels = buf[pos]
            elif ctype == 0x02 and csize >= 4:
                rate, = struct.unpack_from('<I', buf, pos)
            elif ctype == 0x03 and csize >= 8:
                ls, le = struct.unpack_from('<II', buf, pos)
                loop = (ls, le + 1)
            pos += csize
            if not word & 1:
                break
        entry = {'index': i, 'id': i, 'samples': samples, 'offset': data_offset, 'prev_offset': offset,
                 'channels': channels, 'rate': rate, 'loop': loop,
                 'name': names[i] if names and i < len(names) else None}
        offset = data_offset
        out['entries'].append(entry)
    out['names'] = [n for n in (names or []) if n]
    return out


def scan_bank(path):
    """Open one .bank, locate the FEV wrapper and the embedded FSB5. Returns (info dict or None, fev dict)."""
    fh = open(path, 'rb')
    try:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        fev = {'riff': mm[:4] == b'RIFF' and mm[8:12] == b'FEV ', 'fsb5_offset': None}
        if not fev['riff']:
            fev['riff'] = mm[:4] == b'RIFF'
            return None, fev
        pos = mm.find(b'FSB5')
        fev['fsb5_offset'] = pos if pos >= 0 else None
        if pos < 0:
            return None, fev
        info = parse_fsb5(mm, pos)
        info['fev_prefix'] = pos
        info['bank_bytes'] = len(mm)
        return info, fev
    finally:
        fh.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--bank', required=True, help='a .bank file, or a folder of them')
    ap.add_argument('--out', help='write a summary CSV here (folder scan)')
    ap.add_argument('--limit', type=int, default=0, help='print at most N entries per bank')
    a = ap.parse_args()
    if os.path.isdir(a.bank):
        rows = []
        for dirpath, dirs, files in os.walk(a.bank):
            for f in sorted(files):
                if not f.lower().endswith('.bank'):
                    continue
                path = os.path.join(dirpath, f)
                try:
                    info, fev = scan_bank(path)
                except (ValueError, struct.error) as e:
                    print('%-60s ERROR: %s' % (os.path.relpath(path, a.bank), e))
                    continue
                rel = os.path.relpath(path, a.bank)
                if info is None:
                    print('%-60s FEV only (no audio data)' % rel)
                    rows.append([rel, fev['riff'], '', 0, '', '', '', 0, 0])
                    continue
                total = sum(e['samples'] for e in info['entries'])
                secs = total / max(info['entries'][0]['rate'] or 1, 1) if info['entries'] else 0
                print('%-60s FSB5 v%d %-7s sounds=%-4d %8.1f s  fev_prefix=%d' %
                      (rel, info['version'], info['codec'], info['subsongs'], secs, info['fev_prefix']))
                rows.append([rel, fev['riff'], info['codec'], info['subsongs'], info['data_bytes'],
                             info['name_bytes'], '; '.join(info['names'][:8]), total, int(secs)])
                if a.limit:
                    for e in info['entries'][:a.limit]:
                        nm = (' "' + e['name'] + '"') if e['name'] else ''
                        lp = (' loop %d-%d' % e['loop']) if e['loop'] else ''
                        print('    [%2d] %6d Hz  %dch  %9d samples%s%s' %
                              (e['index'], e['rate'], e['channels'], e['samples'], nm, lp))
        if a.out:
            os.makedirs(a.out, exist_ok=True)
            path = os.path.join(a.out, 'banks.csv')
            with open(path, 'w', newline='') as f:
                wr = csv.writer(f)
                wr.writerow(['bank', 'fev', 'codec', 'sounds', 'data_bytes', 'name_bytes', 'first_names',
                             'total_samples', 'seconds'])
                wr.writerows(rows)
            print('%d banks -> %s' % (len(rows), path))
    else:
        info, fev = scan_bank(a.bank)
        if not fev['riff']:
            print('not a RIFF/FEV file')
            return 1
        if info is None:
            print('FEV wrapper without embedded FSB5 audio (%d bytes)' % fev['bank_bytes'] if 'bank_bytes' in fev else '')
            return 0
        print('FEV prefix: %d bytes; FSB5 v%d %s at 0x%x; sounds=%d names=%d data=%d bytes' %
              (info['fev_prefix'], info['version'], info['codec'], info['base'],
               info['subsongs'], len(info['names']), info['data_bytes']))
        for e in info['entries'][:a.limit or info['subsongs']]:
            nm = ('  "' + e['name'] + '"') if e['name'] else ''
            lp = ('  loop %d-%d' % e['loop']) if e['loop'] else ''
            print('  [%2d] %6d Hz  %dch  %9d samples%s%s' % (e['index'], e['rate'], e['channels'], e['samples'], nm, lp))
    return 0


if __name__ == '__main__':
    sys.exit(main())

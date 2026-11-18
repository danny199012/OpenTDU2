#!/usr/bin/env python3
"""A small pure-Python GDeflate (DirectStorage) decoder, for reading the owner's own TDUSC mesh blobs.

GDeflate is Microsoft / NVIDIA's published GPU-friendly DEFLATE variant (Apache-2.0; reference decoder and
format notes: github.com/microsoft/DirectStorage, GDeflate/README.md and shaders/GDeflate.hlsl). This file is a
sequential re-implementation of that shader's algorithm - 32 interleaved bitstreams ("lanes") decoded in
rounds - written from the published description. It is slow (Python) and meant for tooling and verification;
an engine loader would port it to C++.

Stream = TileStream header { u8 id (4), u8 magic (id ^ 0xff), u16 numTiles, u32 { tileSizeIdx:2 (1),
lastTileSize:18, reserved:12 } }, then u32 tileOffsets[numTiles], then the tile data. Tile 0 starts at data offset 0,
tile i > 0 at tileOffsets[i], and the LAST tile's size is tileOffsets[0]. A tile decodes to 65536 bytes
(the last one to lastTileSize, 0 meaning 65536).
"""
import struct

TILE = 65536
LANES = 32
BASE_DIST = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073,
             4097, 6145, 8193, 12289, 16385, 24577, 32769, 49153]
EXTRA_DIST = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14]
BASE_LEN = [0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195,
            227, 3, 0, 0]
EXTRA_LEN = [0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 16, 0, 0]
LANE4ID = [3, 17, 15, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 14, 16, 18, 0, 1, 2]
BASE_REP = [1, 3, 3, 11]
XLEN_REP = [0, 2, 3, 7]


def build(lengths):
    """Canonical Huffman table (puff style) from a list of code lengths."""
    count = [0] * 16
    for l in lengths:
        count[l] += 1
    count[0] = 0
    offs = [0] * 17
    for i in range(1, 16):
        offs[i + 1] = offs[i] + count[i]
    symbols = [0] * max(1, sum(count))
    for s, l in enumerate(lengths):
        if l:
            symbols[offs[l]] = s
            offs[l] += 1
    return count, symbols


def decode(tbl, bits):
    """bits: the stream's next bits, LSB first. Returns (symbol, length); a bad code returns (0, 1)."""
    count, symbols = tbl
    code = first = index = 0
    for l in range(1, 16):
        code |= (bits >> (l - 1)) & 1
        c = count[l]
        if code - c < first:
            return symbols[index + (code - first)], l
        index += c
        first += c
        first <<= 1
        code <<= 1
    return 0, 1


class Lanes:
    """The 32 per-lane bit buffers and the shared refill pointer."""

    def __init__(self, data):
        n = (len(data) + 3) // 4
        self.words = list(struct.unpack('<%dI' % n, data + b'\0' * (n * 4 - len(data))))
        self.words += [0] * 64
        self.buf = [self.words[t] for t in range(LANES)]
        self.cnt = [32] * LANES
        self.base = LANES

    def refill(self, p):
        n = 0
        for t in range(LANES):
            if p[t] and self.cnt[t] < 32:
                w = self.words[self.base + n] if self.base + n < len(self.words) else 0
                self.buf[t] |= w << self.cnt[t]
                self.cnt[t] += 32
                n += 1
        self.base += n

    def eat(self, n, p):
        """n: per-lane bit counts (list) or an int; p: per-lane predicate."""
        for t in range(LANES):
            if p[t]:
                k = n[t] if isinstance(n, list) else n
                self.buf[t] >>= k
                self.cnt[t] -= k
        self.refill(p)


def fixed_tables():
    lit = [8] * 144 + [9] * 112 + [7] * 24 + [8] * 8
    return build(lit), build([5] * 32)


def unpack_code_lengths(br, hlit, hdist, hclen):
    vals = [0] * LANES
    p = [t < hclen for t in range(LANES)]
    for t in range(hclen):
        vals[t] = br.buf[t] & 7
    br.eat(3, p)
    cl = [vals[LANE4ID[s]] for s in range(19)]
    tbl = build(cl)
    count = hlit + hdist
    lens = [0] * (count + 320)
    base_offset = 0
    lastlen = 0
    while True:
        syms = []
        ls = []
        ns = []
        xs = []
        for t in range(LANES):
            bits = br.buf[t] & 0x3fff
            sym, l = decode(tbl, bits)
            idx = 0 if sym <= 15 else sym - 15
            n = BASE_REP[idx] + ((bits >> l) & ((1 << XLEN_REP[idx]) - 1))
            syms.append(sym)
            ls.append(l)
            ns.append(n)
            xs.append(XLEN_REP[idx])
        codelens = []
        prev = None
        for t in range(LANES):
            if syms[t] == 16:
                cl_t = lastlen if prev is None else prev
            else:
                cl_t = syms[t] if syms[t] <= 15 else 0
                prev = cl_t
            codelens.append(cl_t)
        lastlen = codelens[31]
        running = base_offset
        offs = []
        for t in range(LANES):
            offs.append(running)
            running += ns[t]
        eat_n = [0] * LANES
        p = [False] * LANES
        for t in range(LANES):
            if offs[t] < count:
                p[t] = True
                eat_n[t] = ls[t] + xs[t]
                if codelens[t]:
                    for k in range(ns[t]):
                        if offs[t] + k < len(lens):
                            lens[offs[t] + k] = codelens[t]
        br.eat(eat_n, p)
        base_offset = running
        if base_offset >= count:
            break
    return lens[:hlit], (lens[hlit:hlit + hdist] + [0] * 32)[:32]


def compressed_block(br, lit_tbl, dist_tbl, out, dst):
    def round_decode(use_dist):
        syms = []
        lens = []
        for t in range(LANES):
            bits = br.buf[t] & 0x7fffffff
            s, l = decode(dist_tbl if use_dist[t] else lit_tbl, bits)
            syms.append(s)
            lens.append(l)
        return syms, lens

    def translate(syms, lens, isdist, p):
        values = []
        eat_n = [0] * LANES
        pp = [False] * LANES
        for t in range(LANES):
            s = syms[t]
            bits = br.buf[t] & 0xffffffff
            if isdist[t]:
                s = min(s, 31)
                base, n = BASE_DIST[s], EXTRA_DIST[s]
            elif s >= 256:
                i = min(s - 256, 31)
                base, n = BASE_LEN[i], EXTRA_LEN[i]
            else:
                base, n = 1, 0
            values.append(base + ((bits >> lens[t]) & ((1 << n) - 1)))
            if isdist[t] or p[t]:
                pp[t] = True
                eat_n[t] = lens[t] + n
        br.eat(eat_n, pp)
        return values

    def write_output(dst, offset, dist, length, byte, iscopy):
        for t in range(LANES):
            if not iscopy[t] and length[t] != 0:
                if dst + offset[t] < len(out):
                    out[dst + offset[t]] = byte[t] & 0xff
        for t in range(LANES):
            if iscopy[t]:
                pos = dst + offset[t]
                d = max(dist[t], 1)
                for i in range(length[t]):
                    q = pos + i
                    if q < len(out):
                        src = pos - d + (i % d)
                        out[q] = out[src] if 0 <= src < len(out) else 0

    def scan(values):
        res = []
        s = 0
        for v in values:
            res.append(s)
            s += v
        return res, s

    nodist = [False] * LANES
    syms, lens = round_decode(nodist)
    eob_lane = next((t for t in range(LANES) if syms[t] == 256), None)
    oob = [eob_lane is not None and t > eob_lane for t in range(LANES)]
    value = translate(syms, lens, nodist, [not o for o in oob])
    length = [0 if oob[t] else value[t] for t in range(LANES)]
    offset, _ = scan(length)
    iscopy = [s > 256 for s in syms]
    byte = list(syms)
    while eob_lane is None:
        syms, lens = round_decode(iscopy)
        eob_lane = next((t for t in range(LANES) if syms[t] == 256 and not iscopy[t]), None)
        oob = [eob_lane is not None and t > eob_lane for t in range(LANES)]
        value = translate(syms, lens, iscopy, [not o for o in oob])
        write_output(dst, offset, value, length, byte, iscopy)
        _, total = scan([offset[31] + length[31]]) if False else (None, offset[31] + length[31])
        dst += total
        length = [0 if (iscopy[t] or oob[t]) else value[t] for t in range(LANES)]
        offset, _ = scan(length)
        iscopy = [s > 256 and not ic for s, ic in zip(syms, iscopy)]
        byte = list(syms)
    syms, lens = round_decode([True] * LANES)
    iscopy = [iscopy[t] and not oob[t] for t in range(LANES)]
    dist = translate(syms, lens, iscopy, [False] * LANES)
    write_output(dst, offset, dist, length, byte, iscopy)
    return dst + offset[31] + length[31]


def decode_tile(data, out_size):
    br = Lanes(data)
    out = bytearray(out_size)
    dst = 0
    while True:
        header = br.buf[0] & 0xffffffff
        done = header & 1
        btype = (header >> 1) & 3
        only0 = [t == 0 for t in range(LANES)]
        br.eat(3, only0)
        if btype == 2:
            hlit = ((header >> 3) & 31) + 257
            hdist = ((header >> 8) & 31) + 1
            hclen = ((header >> 13) & 15) + 4
            br.eat(14, only0)
            lit, dist = unpack_code_lengths(br, hlit, hdist, hclen)
            dst = compressed_block(br, build(lit), build(dist), out, dst)
        elif btype == 1:
            lit, dist = fixed_tables()
            dst = compressed_block(br, lit, dist, out, dst)
        elif btype == 0:
            size = br.buf[0] & 0xffff
            br.eat(16, only0)
            full, rem = divmod(size, LANES)
            for _ in range(full):
                for t in range(LANES):
                    if dst + t < len(out):
                        out[dst + t] = br.buf[t] & 0xff
                br.eat(8, [True] * LANES)
                dst += LANES
            if rem:
                p = [t < rem for t in range(LANES)]
                for t in range(rem):
                    if dst + t < len(out):
                        out[dst + t] = br.buf[t] & 0xff
                br.eat(8, p)
                dst += rem
        else:
            raise ValueError('bad block type')
        if done:
            break
    return bytes(out)


def decompress(stream):
    ident, magic, tiles = struct.unpack_from('<BBH', stream, 0)
    if ident != 4 or magic != (ident ^ 0xff):
        raise ValueError('not a GDeflate stream')
    word = struct.unpack_from('<I', stream, 4)[0]
    last = (word >> 2) & 0x3ffff
    offs = struct.unpack_from('<%dI' % tiles, stream, 8)
    data = stream[8 + 4 * tiles:]
    out = bytearray()
    for i in range(tiles):
        start = 0 if i == 0 else offs[i]
        end = offs[i + 1] if i < tiles - 1 else start + offs[0]
        size = TILE if (i < tiles - 1 or last == 0) else last
        out += decode_tile(data[start:end] + b'\0' * 128, size)
    return bytes(out)

"""Entries of an Xbox 360 TDU2 bank whose table the recomp tool misses: after the
64-byte header + STNICC padding, count records of 20 bytes
{u64 hash; u32 block; u32 offset; u32 size} (big endian), found by scanning
for the first record whose offset/size fit the file."""
import struct, sys, re

def entries(d):
    count = struct.unpack(">I", d[0x28:0x2C])[0]
    for start in range(0x40, 0x200, 4):
        recs = []
        ok = True
        for k in range(count):
            o = start + k * 20
            h, blk, off, size = struct.unpack(">QIII", d[o:o+20])
            if not (0 < off < len(d) and 0 < size <= len(d) - off):
                ok = False
                break
            recs.append((h, blk, off, size))
        if ok and recs:
            return recs
    return []

def kind(blob):
    head = blob[:16]
    for magic, name in ((b"DD3.", "3dd"), (b"GD3.", "3dg"), (b"MD2.", "2dm"), (b"BD2.", "2db"), (b"XMBF", "xmb")):
        if magic in head:
            return name
    return head[:8].hex()

def names(blob, minlen=4):
    return sorted(set(m.group(0).decode() for m in re.finditer(rb"[A-Za-z][A-Za-z0-9_]{%d,31}" % (minlen - 1), blob)))

if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    for h, blk, off, size in entries(d):
        blob = d[off:off+size]
        print(f"{h:016X} off {off:#9x} size {size:9d} {kind(blob)}")

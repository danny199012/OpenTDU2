import struct, sys
d = open(sys.argv[1], 'rb').read()
hdr = struct.unpack_from('<III', d, 0)
xmb_end = 12 + hdr[2]
carsz = struct.unpack_from('<I', d, xmb_end)[0]
p = xmb_end + 4 + carsz
mat = 105
n = 94 if len(sys.argv) < 3 else int(sys.argv[2])
prev = None
for i in range(n):
    c = d[p + i * mat: p + (i + 1) * mat]
    x, y, z = struct.unpack_from('<fff', c, 9)
    w = struct.unpack_from('<f', c, 21)[0]
    hdr4 = c[:4].hex()
    if i < 4 or i % 10 == 0 or i == n - 1:
        print(i, hdr4, round(x, 1), round(y, 2), round(z, 1), round(w, 3))
print('block end', p + n * mat, 'file', len(d))

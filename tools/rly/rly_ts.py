import struct, sys
d = open(sys.argv[1], 'rb').read()
hdr = struct.unpack_from('<III', d, 0)
xmb_end = 12 + hdr[2]
carsz = struct.unpack_from('<I', d, xmb_end)[0]
p = xmb_end + 4 + carsz + 94 * 105 + 561 * 11
print('timestep block at', p)
vals = [struct.unpack_from('<II', d, p + i * 8) for i in range(6)]
print(vals)
fl = [struct.unpack_from('<ff', d, p + i * 8) for i in range(6)]
print(fl)
tot = 0.0
for i in range(1681):
    tot += struct.unpack_from('<f', d, p + i * 8)[0]
print('sum first floats', tot)
tail = d[hdr[1]:hdr[1] + 64]
print('after recording block:', tail.hex(' '))

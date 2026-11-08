# probe: minimal D3D9 vertex-shader bytecode disassembler (vs_2/3), enough to
# trace how the grass VS builds its output positions.
# usage: python tools_probe/dxso_dis.py <file.dxso>
import struct, sys

OPS = {
    0: 'nop', 1: 'mov', 2: 'add', 3: 'mad', 4: 'mul', 5: 'rcp', 6: 'rsq',
    7: 'dp3', 8: 'dp4', 9: 'min', 10: 'max', 11: 'slt', 12: 'sge', 13: 'exp',
    14: 'log', 15: 'lit', 16: 'dst', 17: 'lrp', 18: 'frc', 19: 'm4x4',
    20: 'm4x3', 21: 'm3x4', 22: 'm3x3', 23: 'm3x2', 24: 'call', 25: 'callnz',
    26: 'loop', 27: 'ret', 28: 'endloop', 29: 'label', 30: 'dcl', 31: 'pow',
    32: 'crs', 33: 'sgn', 34: 'abs', 35: 'nrm', 36: 'sincos', 37: 'rep',
    38: 'endrep', 39: 'if', 41: 'else', 42: 'endif', 43: 'break', 44: 'breakc',
    45: 'mova', 46: 'defb', 47: 'defi',
}
REGS = {
    0: 'r', 1: 'v', 2: 'c', 3: 'a0', 4: 'oPos', 5: 'oD', 6: 'oT', 7: 'i',
    8: 'b', 9: 'L', 10: 's', 11: 'c', 12: 'c', 13: 'c', 18: 'c', 19: 'c', 20: 'c',
}

def reg_name(token):
    rtype = ((token >> 28) & 0x7) | (((token >> 11) & 0x3) << 3)
    reg = token & 0x7FF
    return '%s%d' % (REGS.get(rtype, 't%d' % rtype), reg)

def parse_ctab(data, off, dwords):
    # CTAB payload: magic, Size, Creator, Version, NumConstants @+16,
    # constantInfoOffset @+20 (the translator's pTable = magic + 4).
    b = data[off:off + dwords * 4]
    if b[:4] != b'CTAB':
        return
    num = struct.unpack_from('<I', b, 16)[0]
    info_off = struct.unpack_from('<I', b, 20)[0]
    print('CTAB: %d constants (info @0x%x)' % (num, info_off))
    for i in range(num):
        p = 4 + info_off + i * 20
        nameOff, rset, rindex, rcount = struct.unpack_from('<IHHH', b, p)
        end = b.find(b'\0', nameOff)
        name = b[nameOff:end].decode('latin1') if 0 < nameOff < len(b) else '?'
        print('  set=%d index=%d count=%d  "%s"' % (rset, rindex, rcount, name))
SWIZ = 'xyzw'

def param(token, is_dest):
    mods = ''
    if not is_dest:
        if (token >> 24) & 1:
            mods = '-'
        smod = (token >> 25) & 7
        if smod == 1: mods += 'b'
        elif smod == 2: mods += 'x2'
        elif smod == 3: mods += '-x2'
        elif smod == 4: mods += 'd'
        elif smod == 5: mods += 's'
        elif smod == 6: mods += '-'
        swiz = ''.join(SWIZ[(token >> (16 + 2 * i)) & 3] for i in range(4))
        return '%s%s.%s' % (mods, reg_name(token), swiz)
    mask = ''
    for i, ch in enumerate('xyzw'):
        if (token >> (16 + i)) & 1:
            mask += ch
    return '%s.%s' % (reg_name(token), mask or 'xyzw')

USAGES = ['position', 'blendweight', 'blendindices', 'normal', 'psize',
          'texcoord', 'tangent', 'binormal', 'tessfactor', 'positiont', 'color', 'fog', 'depth', 'sample']

def dis(path):
    d = open(path, 'rb').read()
    ver, typ = struct.unpack_from('<HH', d, 0)
    print('%s: vs_%d_%d, %d bytes' % (path, (ver >> 8) & 0xFF, ver & 0xFF, len(d)))
    off = 4
    while off + 4 <= len(d):
        ins, = struct.unpack_from('<I', d, off)
        if (ins & 0xFFFF) == 0xFFFE:  # comment token: skip (ins >> 16) dwords
            dw = ins >> 16
            payload = d[off + 4:off + 4 + dw * 4]
            if payload[:4] == b'CTAB':
                parse_ctab(d, off + 4, dw)
            else:
                print('%4d: comment (%d dwords)' % (off, dw))
            off += 4 * (1 + dw)
            continue
        if (ins & 0xFFFF) == 0xFFFF:  # end token
            print('%4d: end' % off)
            break
        op = ins & 0xFFFF
        length = (ins >> 24) & 0xFF
        if length == 0:
            length = 1
        name = OPS.get(op, 'op%d' % op)
        if op == 81:  # def: dest + 4 floats
            toks = struct.unpack_from('<5I', d, off + 4)
            vals = struct.unpack_from('<4f', d, off + 8)
            print('%4d: def %s = (%.3f, %.3f, %.3f, %.3f)' % (off, reg_name(toks[0]), *vals))
            off += 4 * (1 + length)
            continue
        if op == 82:  # defi: dest + 4 ints
            toks = struct.unpack_from('<5I', d, off + 4)
            vals = struct.unpack_from('<4i', d, off + 8)
            print('%4d: defi %s = %s' % (off, reg_name(toks[0]), vals))
            off += 4 * (1 + length)
            continue
        comment = (ins & 0x10000000) != 0  # _c co-issued
        if op == 30:  # dcl
            decl, = struct.unpack_from('<I', d, off + 4)
            usage = decl & 0xF
            index = (decl >> 16) & 0xF
            ptok, = struct.unpack_from('<I', d, off + 8)
            mask = ''
            if ((ptok >> 28) & 0x7) | (((ptok >> 11) & 0x3) << 3) in (0, 1):
                for i, ch in enumerate('xyzw'):
                    if (ptok >> (16 + i)) & 1:
                        mask += ch
            u = USAGES[usage] if usage < len(USAGES) else str(usage)
            print('%4d: dcl %s%d -> %s%s' % (off, u, index, reg_name(ptok), '.' + mask if mask else ''))
            off += 4 * (1 + length)
            continue
        if op in (46, 47):  # defb / defi
            vals = struct.unpack_from('<4i', d, off + 8)
            print('%4d: %s %s %s' % (off, name, param(struct.unpack_from('<I', d, off + 4)[0], True), vals))
            off += 4 * length
            continue
        toks = struct.unpack_from('<%dI' % length, d, off + 4)
        args = []
        for k in range(0, length):
            is_dest = k == 0 and op not in (27, 28, 41, 42, 43)
            args.append(param(toks[k], is_dest))
        print('%4d: %s%s %s' % (off, name + '_c' if comment else name, '' if op in (27, 28, 41, 42) else '', ', '.join(args)))
        off += 4 * (1 + length)

dis(sys.argv[1])

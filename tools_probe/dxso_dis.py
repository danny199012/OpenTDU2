# probe: D3D9 (SM1) vertex/pixel-shader bytecode disassembler, enough to
# trace how the grass VS builds its output positions.
# The decode follows the vendored DXVK decoder exactly
# (third_party/dxvk/src/dxso/dxso_{enums,decoder}.{h,cpp}) - these files ARE
# stock SM1 (an earlier draft of this tool numbered the ops from a
# half-remembered table and dropped SUB, shifting every mnemonic from index 3
# up; the dcl usage nibble is bits 0-3 and the index bits 16-19, and the CTAB
# offsets are relative to the struct start after the fourcc).
# Length field: bits 24-27 = tokens AFTER the opcode token (a def prints with
# length 5 at a 24-byte stride; ParseSamplerTypes in the runtime translator
# advances `4 + length*4` the same way).
# usage: python tools_probe/dxso_dis.py <file.dxso>
import struct, sys

OPS = {
    0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp',
    7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min', 11: 'max', 12: 'slt', 13: 'sge',
    14: 'exp', 15: 'log', 16: 'lit', 17: 'dst', 18: 'lrp', 19: 'frc',
    20: 'm4x4', 21: 'm4x3', 22: 'm3x4', 23: 'm3x3', 24: 'm3x2', 25: 'call',
    26: 'callnz', 27: 'loop', 28: 'ret', 29: 'endloop', 30: 'label', 31: 'dcl',
    32: 'pow', 33: 'crs', 34: 'sgn', 35: 'abs', 36: 'nrm', 37: 'sincos',
    38: 'rep', 39: 'endrep', 40: 'if', 41: 'ifc', 42: 'else', 43: 'endif',
    44: 'break', 45: 'breakc', 46: 'mova', 47: 'defb', 48: 'defi',
    64: 'texcoord', 65: 'texkill', 66: 'tex', 78: 'expp', 79: 'logp',
    80: 'cnd', 81: 'def',
}
# ops whose first token is a destination register (DXVK's default class:
# token 0 = dest, the rest sources; everything else prints sources only)
DEST_OPS = set(range(1, 25)) | {26, 30, 35, 36, 37, 46}
COMP = ['?', 'gt', 'eq', 'ge', 'lt', 'ne', 'le', '?']  # D3DSI_COMPARISON bits 16-23 (low nibble used)

REGS = {
    0: 'r', 1: 'v', 2: 'c', 3: 'a0', 4: 'oPos', 5: 'oD', 6: 'oT', 7: 'i',
    8: 'b', 9: 'L', 10: 's', 11: 'c', 12: 'c', 13: 'c', 18: 'c', 19: 'c', 20: 'c',
}
SWIZ = 'xyzw'

def reg_type(token):
    return ((token >> 28) & 0x7) | (((token >> 11) & 0x3) << 3)

def reg_name(token):
    return '%s%d' % (REGS.get(reg_type(token), 'x%d' % reg_type(token)), token & 0x7FF)

def param(token, is_dest):
    if is_dest:
        mask = ''.join(ch for i, ch in enumerate('xyzw') if (token >> (16 + i)) & 1)
        return '%s.%s' % (reg_name(token), mask or 'xyzw')
    mods = ''
    if token & 0x01000000:
        mods += '-'            # D3DSPSM_NEG
    smod = (token >> 24) & 0xF
    if smod == 2:
        mods += 'b'            # bias
    elif smod == 3:
        mods += 'bx2'
    elif smod == 4:
        mods += 'x2'
    elif smod == 5:
        mods += 'dz'
    elif smod == 6:
        mods += 'dw'
    elif smod == 7:
        mods += 'comp'
    swiz = ''.join(SWIZ[(token >> (16 + 2 * i)) & 3] for i in range(4))
    return '%s%s.%s' % (mods, reg_name(token), swiz)

def read_type(b, offset):
    return struct.unpack_from('<6H I', b, offset)  # class, type, rows, cols, elements, members, memberInfo

def type_registers(t):
    per = 1
    if t[0] == 2:
        per = t[2]  # matrix rows
    elif t[0] == 3:
        per = t[3]  # matrix columns
    return per * max(t[4], 1)

def read_string(b, sOff):
    end = b.find(b'\0', sOff)
    return b[sOff:end].decode('latin1') if 0 < sOff < len(b) else '?'

def parse_ctab(data, off, dwords):
    # _D3DXSHADER_CONSTANTTABLE starts after the fourcc; every offset inside
    # is relative to that struct start (DXVK's DxsoCtab does the same).
    b = data[off + 4:off + dwords * 4]
    if data[off:off + 4] != b'CTAB':
        return
    num = struct.unpack_from('<I', b, 12)[0]
    info_off = struct.unpack_from('<I', b, 16)[0]
    print('CTAB: %d constants' % num)
    for i in range(num):
        p = info_off + i * 20
        nameOff, rset, rindex, rcount, _res, tOff = struct.unpack_from('<I HHHH I', b, p)
        name = read_string(b, nameOff)
        print('  set=%d index=%d count=%d  "%s"' % (rset, rindex, rcount, name))
        if tOff == 0 or tOff + 16 > len(b):
            continue
        t = read_type(b, tOff)
        if t[5] == 0:
            continue
        reg = 0
        for j in range(t[5]):
            mOff = t[6] + j * 8
            mnameOff, mTOff = struct.unpack_from('<II', b, mOff)
            mt = read_type(b, mTOff)
            n = type_registers(mt)
            print('    c%d..c%d  %s.%s' % (rindex + reg, rindex + reg + n - 1, name, read_string(b, mnameOff)))
            reg += n

USAGES = ['position', 'blendweight', 'blendindices', 'normal', 'psize',
          'texcoord', 'tangent', 'binormal', 'tessfactor', 'positiont',
          'color', 'fog', 'depth', 'sample']

def dis(path):
    d = open(path, 'rb').read()
    ver, = struct.unpack_from('<I', d, 0)
    kind = 'ps' if (ver >> 16) == 0xFFFF else 'vs'
    print('%s: %s_%d_%d, %d bytes' % (path, kind, (ver >> 8) & 0xFF, ver & 0xFF, len(d)))
    off = 4
    while off + 4 <= len(d):
        ins, = struct.unpack_from('<I', d, off)
        if ins == 0xFFFF:
            print('%4d: end' % off)
            break
        if (ins & 0xFFFF) == 0xFFFE:  # comment: skip (ins >> 16) dwords
            dw = (ins >> 16) & 0x7FFF
            payload = d[off + 4:off + 4 + dw * 4]
            if payload[:4] == b'CTAB':
                parse_ctab(d, off + 4, dw)
            else:
                text = payload.rstrip(b'\0').decode('latin1', 'replace')
                print('%4d: comment %d dwords %s' % (off, dw, text if text.isprintable() else ''))
            off += 4 * (1 + dw)
            continue
        op = ins & 0xFFFF
        length = (ins >> 24) & 0x0F
        name = OPS.get(op, 'op%d' % op)
        flags = ''
        if ins & 0x10000000:
            flags += ' p'       # predicated (DXVK reads bit 28)
        if ins & 0x40000000:
            flags += ' _c'      # co-issued
        if op == 81:            # def: dest + 4 floats
            toks = struct.unpack_from('<5I', d, off + 4)
            vals = struct.unpack_from('<4f', d, off + 8)
            print('%4d: def %s = (%.4g, %.4g, %.4g, %.4g)' % (off, reg_name(toks[0]), *vals))
            off += 4 * (1 + length)
            continue
        if op == 47:            # defb: dest + 1 dword
            toks = struct.unpack_from('<2I', d, off + 4)
            print('%4d: defb %s = %d' % (off, reg_name(toks[0]), struct.unpack_from('<I', d, off + 8)[0]))
            off += 4 * (1 + length)
            continue
        if op == 48:            # defi: dest + 4 ints
            toks = struct.unpack_from('<5I', d, off + 4)
            vals = struct.unpack_from('<4i', d, off + 8)
            print('%4d: defi %s = %s' % (off, reg_name(toks[0]), vals))
            off += 4 * (1 + length)
            continue
        if op == 31:            # dcl: semantic token + dest token
            decl, ptok = struct.unpack_from('<2I', d, off + 4)
            usage = decl & 0xF
            index = (decl >> 16) & 0xF
            u = USAGES[usage] if usage < len(USAGES) else str(usage)
            extra = ''
            if reg_type(ptok) == 10:  # sampler: texture type in bits 24-27
                extra = ' %s' % ('?', '?', '2d', 'cube', 'volume', '?', '?', '?')[ (decl >> 24) & 7 ]
            print('%4d: dcl_%s%d %s%s' % (off, u, index, reg_name(ptok), extra))
            off += 4 * (1 + length)
            continue
        toks = struct.unpack_from('<%dI' % length, d, off + 4) if length else ()
        args = []
        if op in (41, 45) and length >= 2:  # ifc / breakc: comparator in bits 16-23
            args.append('%s' % COMP[(ins >> 16) & 0x7])
        for k in range(length):
            args.append(param(toks[k], op in DEST_OPS and k == 0))
        print('%4d: %s%s %s' % (off, name, flags, ', '.join(args)))
        off += 4 * (1 + length)

dis(sys.argv[1])

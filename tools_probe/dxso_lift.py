# probe: symbolic SM1 (D3D9) shader lift - transcribes a .dxso into SSA
# pseudo-C with the CTAB struct-member names substituted for c-registers.
# Shares the token decode of tools_probe/dxso_dis.py (see the header there);
# masked writes expand per component, dp3/dp4/nrm/... broadcast their scalar.
# usage: python tools_probe/dxso_lift.py <file.dxso> [--frame|--scene|--mat]
import struct, sys

OPS = {
    0: 'nop', 1: 'mov', 2: 'add', 3: 'sub', 4: 'mad', 5: 'mul', 6: 'rcp',
    7: 'rsq', 8: 'dp3', 9: 'dp4', 10: 'min', 11: 'max', 12: 'slt', 13: 'sge',
    14: 'exp', 15: 'log', 16: 'lit', 17: 'dst', 18: 'lrp', 19: 'frc',
    20: 'm4x4', 25: 'call', 28: 'ret', 32: 'pow', 33: 'crs', 35: 'abs',
    36: 'nrm', 37: 'sincos', 38: 'rep', 46: 'mova', 81: 'def',
}
DEST_OPS = set(range(1, 25)) | {26, 30, 35, 36, 37, 46}
REGS = {0: 'r', 1: 'v', 2: 'c', 3: 'a0', 4: 'oPos', 5: 'oD', 6: 'oT', 9: 'L',
        10: 's', 11: 'oC', 12: 'oDepth', 13: 'c', 14: 'c', 15: 'c', 16: 'c'}
SWIZ = 'xyzw'
SCALARS = {'dp3', 'dp4', 'rsq', 'rcp', 'exp', 'log', 'nrm', 'frc', 'sincos'}
# ops that write a fresh scalar value (not a component-wise op of the sources)
COMPOW = {'mov', 'add', 'sub', 'mad', 'mul', 'min', 'max', 'slt', 'sge', 'cnd', 'lrp', 'pow', 'crs', 'abs'}

def reg_type(token):
    return ((token >> 28) & 0x7) | (((token >> 11) & 0x3) << 3)

def reg_num(token):
    return token & 0x7FF

def swiz(token):
    return ''.join(SWIZ[(token >> (16 + 2 * i)) & 3] for i in range(4))

def dest_mask(token):
    return ''.join(ch for i, ch in enumerate(SWIZ) if (token >> (16 + i)) & 1) or 'xyzw'

def smod(token):
    s = (token >> 24) & 0xF
    return {0: '', 1: '-', 2: 'b', 3: '-b', 4: 'x2', 5: '-x2', 6: 'comp',
            7: 'x2', 8: '-x2', 9: 'dz', 10: 'dw', 12: 'abs'}.get(s, '')

def parse_ctab(data, off, dwords):
    b = data[off + 4:off + dwords * 4]
    if data[off:off + 4] != b'CTAB':
        return {}
    # member name -> 'c<index>' (the register offset the runtime feeds)
    names = {}
    num = struct.unpack_from('<I', b, 12)[0]
    info_off = struct.unpack_from('<I', b, 16)[0]
    for i in range(num):
        p = info_off + i * 20
        nameOff, rset, rindex, rcount, _res, tOff = struct.unpack_from('<I HHHH I', b, p)
        end = b.find(b'\0', nameOff)
        cname = b[nameOff:end].decode('latin1')
        if tOff == 0 or tOff + 16 > len(b):
            continue
        cls, _typ, rows, cols, elems, members, mOff = struct.unpack_from('<6H I', b, tOff)
        if members == 0:
            names['c%d' % rindex] = cname
            continue
        reg = 0
        for j in range(members):
            mnOff, mTOff = struct.unpack_from('<II', b, mOff + j * 8)
            mcls, _mt, mrows, mcols, melems, _mm, _mo = struct.unpack_from('<6H I', b, mTOff)
            per = mrows if mcls == 2 else (mcols if mcls == 3 else 1)
            n = per * max(melems, 1)
            end = b.find(b'\0', mnOff)
            mname = b[mnOff:end].decode('latin1')
            for k in range(n):
                names['c%d' % (rindex + reg + k)] = '%s.%s%d' % (cname, mname, k)
            reg += n
    return names

def lift(path):
    d = open(path, 'rb').read()
    ver, = struct.unpack_from('<I', d, 0)
    kind = 'ps' if (ver >> 16) == 0xFFFF else 'vs'
    print('// %s: %s_%d_%d' % (path, kind, (ver >> 8) & 0xFF, ver & 0xFF))
    names = {}
    off = 4
    lines = []
    while off + 4 <= len(d):
        ins, = struct.unpack_from('<I', d, off)
        if ins == 0xFFFF:
            break
        if (ins & 0xFFFF) == 0xFFFE:
            dw = (ins >> 16) & 0x7FFF
            if d[off + 4:off + 8] == b'CTAB':
                names = parse_ctab(d, off + 4, dw)
            off += 4 * (1 + dw)
            continue
        op = ins & 0xFFFF
        length = (ins >> 24) & 0x0F
        toks = struct.unpack_from('<%dI' % length, d, off + 4) if length else ()
        off += 4 * (1 + length)

        def ref(tok, comp=None):
            base = '%s%d' % (REGS.get(reg_type(tok), 'x%d' % reg_type(tok)), reg_num(tok))
            nm = names.get(base)
            sw = swiz(tok)
            m = smod(tok)
            parts = []
            for i, ch in enumerate(sw):
                ref_c = '%s.%s' % (nm or base, ch)
                parts.append(ref_c)
            if comp is not None:
                sel = parts[SWIZ.index(comp)] if comp in SWIZ else parts[0]
            else:
                sel = '%s.%s' % (nm or base, sw)
            return m + sel if m else sel

        if op == 81:  # def
            vals = struct.unpack_from('<4f', d, off - 16)
            reg = 'c%d' % (toks[0] & 0x7FF)
            print('// def %s = (%.4g, %.4g, %.4g, %.4g)  [%s]' %
                  (reg, *vals, names.get(reg, reg)))
            continue
        if op == 31:  # dcl
            decl, ptok = toks
            usage = decl & 0xF
            if reg_type(ptok) == 10:  # sampler: texture type in bits 24-27
                print('// dcl_sampler %s%d (%s)' % ('s', reg_num(ptok),
                      {2: '2d', 3: 'cube', 4: 'volume'}.get((decl >> 24) & 0xF, '?')))
            else:
                u = {0: 'position', 1: 'blendweight', 2: 'blendindices', 3: 'normal', 5: 'texcoord',
                     6: 'tangent', 7: 'binormal', 10: 'color'}.get(usage, str(usage))
                print('// dcl_%s%d %s%d' % (u, (decl >> 16) & 0xF, REGS.get(reg_type(ptok), 'x'), reg_num(ptok)))
            continue
        if op == 66:  # texld: dst, src, sampler
            dst, src, samp = toks
            dname = '%s%d' % (REGS.get(reg_type(dst), 'x%d' % reg_type(dst)), reg_num(dst))
            sname = 's%d' % reg_num(samp)
            for ch in dest_mask(dst):
                print('%s.%s = sample(%s, %s)' % (dname, ch, sname, ref(src)))
            continue

        name = OPS.get(op, 'op%d' % op)
        if name in ('nop', 'call', 'ret'):
            continue

        if op == 37:  # sincos: dst.x = cos(src.w), dst.y = sin(src.w)
            dst, src = toks
            print('// sincos')
            for ch in dest_mask(dst):
                if ch == 'x':
                    val = 'cos(%s)' % ref(src, 'w')
                elif ch == 'y':
                    val = 'sin(%s)' % ref(src, 'w')
                else:
                    continue
                print('%s%d.%s = %s' % (REGS.get(reg_type(dst), 'x'), reg_num(dst), ch, val))
            continue
        if op == 20:  # m4x4: dst = pos * c[4n..4n+3] (row-major dp4s)
            dst, src = toks
            base = reg_num(src) & 0x7FF
            rows = ' + '.join('%s.%s * %s' % (ref(src, ch), ch, names.get('c%d' % (base + i), 'c%d' % (base + i)))
                              for i, ch in enumerate(SWIZ))
            for ch in dest_mask(dst):
                print('%s%d.%s = dp4row%d(%s)' % (REGS.get(reg_type(dst), 'x'), reg_num(dst), ch, SWIZ.index(ch), rows))
            continue

        dsttok = toks[0] if op in DEST_OPS else None
        srcs = toks[1:] if dsttok is not None else toks
        if dsttok is None:
            continue

        dname = '%s%d' % (REGS.get(reg_type(dsttok), 'x%d' % reg_type(dsttok)), reg_num(dsttok))
        mask = dest_mask(dsttok)
        if name in SCALARS:
            if name == 'nrm':
                s0 = ref(srcs[0])
                expr = 'normalize(%s)' % s0
            elif name == 'sincos':
                expr = 'sincos(%s)' % ref(srcs[0], 'w')
            elif name in ('dp3', 'dp4'):
                expr = 'dot(%s, %s)' % (ref(srcs[0]), ref(srcs[1]))
            else:
                expr = '%s(%s)' % (name, ref(srcs[0]))
            for ch in mask:
                print('%s.%s = %s' % (dname, ch, expr))
            continue
        # component-wise
        def comp_expr(ch):
            idx = SWIZ.index(ch)
            def take(tok):
                tok_sw = swiz(tok)
                c = tok_sw[idx] if len(tok_sw) == 4 else (tok_sw[min(idx, len(tok_sw) - 1)])
                return ref(tok, c)
            s = [take(t) for t in srcs]
            if name == 'mov':
                return s[0]
            if name == 'mad':
                return '%s * %s + %s' % (s[0], s[1], s[2])
            if name == 'lrp':
                return 'lerp(%s, %s, %s)' % (s[2], s[1], s[0])
            if name == 'cnd':
                return '(v0.r > 0.5 ? %s : %s)' % (s[0], s[1])
            sym = {'add': '+', 'sub': '-', 'mul': '*', 'min': ' min ', 'max': ' max ',
                   'slt': ' < ', 'sge': ' >= ', 'pow': ' pow ', 'crs': ' crs ', 'abs': ' abs '}[name]
            return '(%s%s%s)' % (s[0], sym, s[1])
        for ch in mask:
            print('%s.%s = %s' % (dname, ch, comp_expr(ch)))

lift(sys.argv[1])

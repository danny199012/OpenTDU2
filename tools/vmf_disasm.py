"""Minimal AVM1 disassembler for Eden .vmf (SWF tags, 'FMV' header). Scratch tool."""
import struct, sys

NAMES = {0x04:'NextFrame',0x05:'PrevFrame',0x06:'Play',0x07:'Stop',0x0a:'Add',0x0b:'Subtract',0x0c:'Multiply',0x0d:'Divide',
0x0e:'Equals',0x0f:'Less',0x10:'And',0x11:'Or',0x12:'Not',0x13:'StringEquals',0x14:'StringLength',0x15:'StringExtract',
0x17:'Pop',0x18:'ToInteger',0x1c:'GetVariable',0x1d:'SetVariable',0x20:'SetTarget2',0x21:'StringAdd',0x22:'GetProperty',
0x23:'SetProperty',0x24:'CloneSprite',0x25:'RemoveSprite',0x26:'Trace',0x27:'StartDrag',0x28:'EndDrag',0x29:'StringLess',
0x30:'RandomNumber',0x34:'GetTime',0x3a:'Delete',0x3b:'Delete2',0x3c:'DefineLocal',0x3d:'CallFunction',0x3e:'Return',
0x3f:'Modulo',0x40:'NewObject',0x41:'DefineLocal2',0x42:'InitArray',0x43:'InitObject',0x44:'TypeOf',0x45:'TargetPath',
0x46:'Enumerate',0x47:'Add2',0x48:'Less2',0x49:'Equals2',0x4a:'ToNumber',0x4b:'ToString',0x4c:'PushDuplicate',0x4d:'StackSwap',
0x4e:'GetMember',0x4f:'SetMember',0x50:'Increment',0x51:'Decrement',0x52:'CallMethod',0x53:'NewMethod',0x54:'InstanceOf',
0x55:'Enumerate2',0x60:'BitAnd',0x61:'BitOr',0x62:'BitXor',0x63:'BitLShift',0x64:'BitRShift',0x65:'BitURShift',0x66:'StrictEquals',
0x67:'Greater',0x68:'StringGreater',0x69:'Extends',0x81:'GotoFrame',0x83:'GetURL',0x87:'StoreRegister',0x88:'ConstantPool',
0x8a:'WaitForFrame',0x8b:'SetTarget',0x8c:'GoToLabel',0x94:'With',0x96:'Push',0x99:'Jump',0x9a:'GetURL2',0x9b:'DefineFunction',
0x9d:'If',0x9e:'Call',0x9f:'GotoFrame2',0x8e:'DefineFunction2',0x8f:'Try',0x2a:'Throw',0x2b:'CastOp',0x2c:'ImplementsOp',0x9:'StopSounds',0x8:'ToggleQuality'}

def cstr(b, p):
    e = b.index(0, p); return b[p:e].decode('latin1'), e + 1

def dis(b, start, end, out, ind):
    pool = []
    p = start
    while p < end:
        op = b[p]; p += 1
        if op == 0:
            break
        ln = 0
        if op >= 0x80:
            ln = struct.unpack_from('<H', b, p)[0]; p += 2
        body = b[p:p+ln]; q = p; p += ln
        n = NAMES.get(op, 'op%02x' % op); arg = ''
        if op == 0x88:
            c = struct.unpack_from('<H', body, 0)[0]; r = 2; pool = []
            for _ in range(c):
                s, r = cstr(body, r); pool.append(s)
            arg = '%d entries' % c
            out.append(ind + 'ConstantPool ' + repr(pool)); continue
        if op == 0x96:
            r = 0; items = []
            while r < len(body):
                t = body[r]; r += 1
                if t == 0: s, r = cstr(body, r); items.append(repr(s))
                elif t == 1: items.append(repr(struct.unpack_from('<f', body, r)[0])); r += 4
                elif t == 2: items.append('null')
                elif t == 3: items.append('undef')
                elif t == 4: items.append('r%d' % body[r]); r += 1
                elif t == 5: items.append(str(bool(body[r]))); r += 1
                elif t == 6:
                    d = body[r:r+8]; items.append(repr(struct.unpack('<d', d[4:]+d[:4])[0])); r += 8
                elif t == 7: items.append(str(struct.unpack_from('<i', body, r)[0])); r += 4
                elif t == 8: i = body[r]; r += 1; items.append(repr(pool[i]) if i < len(pool) else 'c%d' % i)
                elif t == 9: i = struct.unpack_from('<H', body, r)[0]; r += 2; items.append(repr(pool[i]) if i < len(pool) else 'c%d' % i)
                else: items.append('?'); break
            arg = ', '.join(items)
        elif op == 0x83:
            a, r = cstr(body, 0); t, r = cstr(body, r); arg = repr(a) + ' ' + repr(t)
        elif op == 0x8c or op == 0x8b:
            arg = repr(cstr(body, 0)[0])
        elif op == 0x81:
            arg = str(struct.unpack_from('<H', body, 0)[0])
        elif op in (0x99, 0x9d):
            arg = '%+d' % struct.unpack_from('<h', body, 0)[0]
        elif op == 0x87:
            arg = 'r%d' % body[0]
        elif op == 0x9f:
            arg = 'play=%d' % (body[0] & 1)
        elif op == 0x9b:
            nm, r = cstr(body, 0); c = struct.unpack_from('<H', body, r)[0]; r += 2; ps = []
            for _ in range(c):
                s, r = cstr(body, r); ps.append(s)
            sz = struct.unpack_from('<H', body, r)[0]
            out.append(ind + 'function %s(%s) {' % (nm, ', '.join(ps)))
            dis(b, p, p + sz, out, ind + '  '); out.append(ind + '}'); p += sz; continue
        elif op == 0x8e:
            nm, r = cstr(body, 0); c = struct.unpack_from('<H', body, r)[0]; r += 2; regs = body[r]; r += 1; r += 2; ps = []
            for _ in range(c):
                reg = body[r]; r += 1; s, r = cstr(body, r); ps.append('r%d:%s' % (reg, s))
            sz = struct.unpack_from('<H', body, r)[0]
            out.append(ind + 'function2 %s(%s) {' % (nm, ', '.join(ps)))
            dis(b, p, p + sz, out, ind + '  '); out.append(ind + '}'); p += sz; continue
        out.append(ind + '%05x %s %s' % (q, n, arg))

def tags(b, p, end):
    while p + 2 <= end:
        h = struct.unpack_from('<H', b, p)[0]; p += 2
        code = h >> 6; ln = h & 0x3f
        if ln == 0x3f:
            ln = struct.unpack_from('<I', b, p)[0]; p += 4
        yield code, p, ln
        if code == 0: return
        p += ln

def walk(b, p, end, path, out):
    frame = 0; label = None
    for code, o, ln in tags(b, p, end):
        if code == 1: frame += 1
        elif code == 43: label = cstr(b, o)[0]; out.append('%s f%d label %s' % (path, frame, label))
        elif code == 12:
            out.append('%s f%d DoAction:' % (path, frame)); dis(b, o, o + ln, out, '    ')
        elif code == 59:
            sid = struct.unpack_from('<H', b, o)[0]
            out.append('%s InitAction sprite %d:' % (path, sid)); dis(b, o + 2, o + ln, out, '    ')
        elif code == 39 or code == 127:
            # DefineSpriteEden (127) carries an extra u32 before its tags.
            sid = struct.unpack_from('<H', b, o)[0]
            walk(b, o + (8 if code == 127 else 4), o + ln, 'S%d' % sid, out)
        elif code == 56:
            c = struct.unpack_from('<H', b, o)[0]; r = o + 2
            for _ in range(c):
                i = struct.unpack_from('<H', b, r)[0]; s, r = cstr(b, r + 2); out.append('export %d %s' % (i, s))
        elif code == 34:
            bid = struct.unpack_from('<H', b, o)[0]; aoff = struct.unpack_from('<H', b, o + 3)[0]
            if aoff:
                r = o + 3 + aoff
                while True:
                    nxt, cond = struct.unpack_from('<HH', b, r)
                    out.append('button %d cond %04x:' % (bid, cond))
                    dis(b, r + 4, (r + nxt) if nxt else (o + ln), out, '    ')
                    if not nxt: break
                    r += nxt
        elif code == 26:
            flags = b[o]; r = o + 3
            if flags & 2: r += 2
            if flags & 4:  # matrix: skip bits
                pass
            if flags & 0x20:
                pass
            if flags & 0x80:
                out.append('%s f%d PlaceObject2 with clip actions (not decoded)' % (path, frame))

if __name__ == '__main__':
    b = open(sys.argv[1], 'rb').read()
    p = 0x13
    if b[0x12]:
        n = struct.unpack_from('<I', b, p)[0]; p += 4 + n * 4
    out = []
    walk(b, p, len(b), '/', out)
    open(sys.argv[2], 'w', encoding='utf8').write('\n'.join(out))
    print(len(out), 'lines')

def edittexts(b):
    p = 0x13
    if b[0x12]:
        n = struct.unpack_from('<I', b, p)[0]; p += 4 + n * 4
    res = []
    def rec(p, end, path):
        for code, o, ln in tags(b, p, end):
            if code == 39 or code == 127:
                rec(o + (8 if code == 127 else 4), o + ln, 'S%d' % struct.unpack_from('<H', b, o)[0])
            elif code == 37:
                cid = struct.unpack_from('<H', b, o)[0]
                body = b[o:o+ln]
                # variable name: find printable runs
                import re
                ss = [m.group().decode() for m in re.finditer(rb'[ -~]{2,}', body[20:])]
                res.append((path, cid, ss))
    rec(p, len(b), '/')
    return res

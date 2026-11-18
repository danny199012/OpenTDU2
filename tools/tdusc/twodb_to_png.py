import struct,sys,io
from PIL import Image
FMT={51:b'DXT1',50:b'DXT1',55:b'DXT5',54:b'DXT3',53:b'DXT3',52:b'DXT1'}
def conv(p,out=None):
    d=open(p,'rb').read()
    h=d[0x20:0x50]
    w,hh,dep=struct.unpack('<HHH',h[8:14]);mips=h[14];fmt=struct.unpack('<I',h[0x1C:0x20])[0]
    info=(w,hh,mips,fmt,len(d)-0x50)
    cc=FMT.get(fmt)
    if not cc:return info,None
    hdr=b'DDS '+struct.pack('<7I',124,0x1007|0x20000|0x80000,hh,w,max(w*hh//2,16),0,mips)+b'\0'*44+struct.pack('<2I4s5I',32,4,cc,0,0,0,0,0)+struct.pack('<5I',0x1000,0,0,0,0)
    im=Image.open(io.BytesIO(hdr+d[0x50:]));im.load()
    if out:im.convert('RGBA').save(out)
    return info,im
if __name__=="__main__":
    for p in sys.argv[1:]:
        i,im=conv(p,p.replace('.2db','.png'));print(p,i)

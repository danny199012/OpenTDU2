import os, sys
sys.path.insert(0, r'E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools')
import tdu2_bigfile as T
from tdu2_extract_pc import xor_decode

base = r'C:\Program Files (x86)\Steam\steamapps\common\Test Drive Unlimited 2'
out_root = r'C:\TDU2-Gamedata\TestDrive2'
table = T.load_names(r'E:\Xbox 360 Games\Torrent\Minerva_Myrient\Redump\Microsoft - Xbox 360\Test-Drive-Unlimited-2-Recomp\tools\FileNames.list')

pairs = [
    ('TDU2DLC2v026', 'TDU2DLC2v026'), ('TDU2DLC2v028', 'TDU2DLC2v028'),
    ('TDU2DLC2v031', 'TDU2DLC2v031'), ('TDU2DLCEXPLOv017', 'TDU2DLCEXPLOv017'),
    ('TDU2DLCEXPLOv023', 'TDU2DLCEXPLOv023'), ('TDU2DLCEXPLOv024', 'TDU2DLCEXPLOv024'),
    ('TDU2DLCEXPLOv027', 'TDU2DLCEXPLOv027'), ('TDU2v082', 'TDU2v082'),
    ('TDU2v083', 'TDU2v083'), ('TDU2v086', 'TDU2v086'), ('TDU2v097', 'TDU2v097'),
]

for map_name, big_name in pairs:
    mp = os.path.join(base, map_name + '.map')
    bp = os.path.join(base, big_name + '.big')
    d = open(mp, 'rb').read()
    big_size = os.path.getsize(bp)
    chosen = None
    for endian, layout in (('>', '360'), ('<', 'pc')):
        try:
            info, entries = T._parse_map_as(d, mp, endian, layout)
        except T._MapLayoutError:
            continue
        fits = sum(1 for e in entries if 0 <= e['offset'] and e['offset'] + e['uncomp'] <= big_size)
        if fits == len(entries) and entries:
            chosen = (endian, layout, info, entries)
            break
    if chosen is None:
        print(f'{map_name}: no layout fits, skipping')
        continue
    endian, layout, info, entries = chosen
    named = [(table.get(e['hash']), e) for e in entries]
    missing = sum(1 for n, e in named if n is None)
    done = 0
    with open(bp, 'rb') as fh:
        for name, e in named:
            fh.seek(e['offset'])
            data = xor_decode(fh.read(e['uncomp']))
            rel = (name or '_unresolved/' + f"{e['hash']:016x}.bin").replace('/', os.sep)
            p = os.path.join(out_root, rel)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, 'wb').write(data)
            done += 1
    print(f'{map_name} [{layout}/{endian}]: {done} files ({missing} unresolved)')

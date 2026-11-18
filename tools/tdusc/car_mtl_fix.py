#!/usr/bin/env python3
"""Post-step for blender_car_split.py: make the exported part .mtl files loadable by the port.

The prop loader's no-texture TD_Gouraud variant is flat white and ignores Kd, so every part gets a
tiny solid-colour map_Kd PNG; glass is alpha blended (`d` < 1 in the mtl AND alpha in the PNG).
The tyres share one baked atlas, `<prefix>tire.png` (copy it beside the OBJs first).
Usage: python car_mtl_fix.py <mods>/carswap [prefix=lambo_]
"""
import glob, os, re, sys
from PIL import Image

folder = sys.argv[1]
prefix = sys.argv[2] if len(sys.argv) > 2 else 'lambo_'
SOLID = {'body': (150, 10, 12, 255), 'glass': (20, 32, 42, 95), 'rimF': (185, 188, 194, 255),
         'rimR': (185, 188, 194, 255), 'interior': (22, 22, 24, 255)}
for mtl in glob.glob(os.path.join(folder, prefix + '*.mtl')):
    part = os.path.basename(mtl)[len(prefix):-4]
    t = open(mtl).read()
    t = re.sub(r'^Ka .*$', 'Ka 1.0 1.0 1.0', t, flags=re.M)
    t = re.sub(r'^Kd .*$', 'Kd 1.0 1.0 1.0', t, flags=re.M)
    if part.startswith('tire'):
        png = prefix + 'tire.png'
    else:
        png = prefix + part + '.png'
        if part in SOLID and not os.path.exists(os.path.join(folder, png)):
            Image.new('RGBA', (8, 8), SOLID[part]).save(os.path.join(folder, png))
    if part == 'glass':
        t = re.sub(r'^d .*$', 'd 0.35', t, flags=re.M)
    if 'map_Kd' not in t:
        t = t.rstrip() + '\nmap_Kd %s\n' % png
    open(mtl, 'w').write(t)
    print(os.path.basename(mtl), '->', png)

#!/usr/bin/env python3
"""Post-step for blender_car_split.py: make the exported part .mtl files loadable by the port.

The prop loader's no-texture TD_Gouraud variant is flat white and ignores Kd, so every part gets a
tiny solid-colour map_Kd PNG; glass is alpha blended (`d` < 1 in the mtl AND alpha in the PNG).
Usage: python car_mtl_fix.py <mods>/props [prefix=tdusc_lambo_]   (the tire PNG is baked separately)
"""
import os, re, sys
from PIL import Image
props = sys.argv[1]
prefix = sys.argv[2] if len(sys.argv) > 2 else 'tdusc_lambo_'
PNG = {'body': (150, 10, 12, 255), 'glass': (20, 32, 42, 95), 'rim': (185, 188, 194, 255),
       'interior': (22, 22, 24, 255)}
for part, rgba in PNG.items():
    mtl = os.path.join(props, prefix + part + '.mtl')
    if not os.path.exists(mtl):
        continue
    png = os.path.join(props, prefix + part + '.png')
    if not os.path.exists(png):
        Image.new('RGBA', (8, 8), rgba).save(png)
    t = open(mtl).read()
    t = re.sub(r'^Ka .*$', 'Ka 1.0 1.0 1.0', t, flags=re.M)
    t = re.sub(r'^Kd .*$', 'Kd 1.0 1.0 1.0', t, flags=re.M)
    if part == 'glass':
        t = re.sub(r'^d .*$', 'd 0.35', t, flags=re.M)
    if 'map_Kd' not in t:
        t = t.rstrip() + '\nmap_Kd %s%s.png\n' % (prefix, part)
    open(mtl, 'w').write(t)
t = os.path.join(props, prefix + 'tire.mtl')
if os.path.exists(t):
    s = open(t).read()
    s = re.sub(r'^Ka .*$', 'Ka 1.0 1.0 1.0', s, flags=re.M)
    s = re.sub(r'^Kd .*$', 'Kd 1.0 1.0 1.0', s, flags=re.M)
    if 'map_Kd' not in s:
        s = s.rstrip() + '\nmap_Kd %stire.png\n' % prefix
    open(t, 'w').write(s)

#!/usr/bin/env python3
"""Make a vinyl-pack skeleton for one car from its exported OBJ.

  1. export the car:   opentdu2.exe -gm assetviewer -exportcar <FileName>  (headless)
                       -> <userdata>/export/<FileName>/<FileName>_ext.obj
  2. run this tool:    python tools/mod/vinyl_template.py <path to *_ext.obj> --id <_id_car> --out <mods>/stickers

It writes <out>/<car>_vinyl/ with
  pack.ini                 vinyl=1, cars=<id>  (edit name / price)
  _template_<car>.png      the body paint UV layout, 1024x1024 (NOT a sticker: delete or
                           keep it out of the folder before shipping - any *.png in the
                           folder is a livery entry)
and moves the template to <out>/<car>_vinyl_template.png so the pack folder stays clean.

Paint your livery over the template at 1024x1024 (the canvas size), hide the template layer, save
as a PNG with alpha into the pack folder. Pixel (x, y) = UV0 (x/1024, y/1024), V downward: the
canvas the game composites is exactly that texture space (docs/re/sticker_mod_packs.md).
Only the materials that carry the sticker slot are drawn: 'Paint' and 'Paint_SK'
(docs/re/stickers_spec.md section 1: Paint_LR and the numbered Paint_NN variants have none).
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

SIZE = 1024


def read_paint_triangles(path):
    vts, tris = [], []
    paint = False
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("vt "):
                _, u, v = line.split()[:3]
                vts.append((float(u), float(v)))
            elif line.startswith("usemtl "):
                name = line.split(None, 1)[1].strip()
                paint = name.lower() in ("paint", "paint_sk")
            elif paint and line.startswith("f "):
                idx = [int(p.split("/")[1]) - 1 for p in line.split()[1:] if "/" in p and p.split("/")[1]]
                for k in range(1, len(idx) - 1):
                    tris.append((idx[0], idx[k], idx[k + 1]))
    return vts, tris


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("obj")
    ap.add_argument("--id", type=int, required=True, help="DBCarPhysicsData._id_car (the shop log prints it: 'car 387 bound')")
    ap.add_argument("--out", required=True, help="the <mods>/stickers folder")
    ap.add_argument("--name", default=None, help="pack title (default: the car name)")
    a = ap.parse_args()

    car = os.path.basename(a.obj).replace("_ext.obj", "")
    vts, tris = read_paint_triangles(a.obj)
    if not tris:
        sys.exit("no Paint triangles in " + a.obj)

    img = Image.new("RGBA", (SIZE, SIZE), (20, 20, 24, 255))
    d = ImageDraw.Draw(img, "RGBA")
    pts = [(u * SIZE, v * SIZE) for u, v in vts]
    # Coverage as an opaque fill (12k triangles overlap: translucent fills saturate), then only the
    # island outlines - the edges used by exactly one triangle - so the seams stay readable.
    for t in tris:
        d.polygon([pts[i] for i in t], fill=(55, 70, 100, 255))
    edges = {}
    for t in tris:
        for k in range(3):
            e = tuple(sorted((t[k], t[(k + 1) % 3])))
            edges[e] = edges.get(e, 0) + 1
    for (i, j), n in edges.items():
        if n == 1:
            d.line([pts[i], pts[j]], fill=(255, 210, 90, 255), width=2)
    # a 128px grid and the centre cross, to place things by eye
    for g in range(0, SIZE + 1, 128):
        d.line([(g, 0), (g, SIZE)], fill=(255, 255, 255, 40))
        d.line([(0, g), (SIZE, g)], fill=(255, 255, 255, 40))
    d.rectangle((0, 0, SIZE - 1, SIZE - 1), outline=(255, 255, 255, 255))

    pack = os.path.join(a.out, car.lower() + "_vinyl")
    os.makedirs(pack, exist_ok=True)
    ini = os.path.join(pack, "pack.ini")
    if not os.path.exists(ini):
        with open(ini, "w") as f:
            f.write("name=%s\nprice=2000\nvinyl=1\ncars=%d\n" % (a.name or (car + " livery"), a.id))
    tpl = os.path.join(a.out, car.lower() + "_vinyl_template.png")
    img.save(tpl)
    used = [pts[i] for t in tris for i in t]
    print("paint triangles: %d  uv range u %.3f..%.3f v %.3f..%.3f" % (
        len(tris), min(p[0] for p in used) / SIZE, max(p[0] for p in used) / SIZE,
        min(p[1] for p in used) / SIZE, max(p[1] for p in used) / SIZE))
    print("pack folder:", pack)
    print("template:   ", tpl)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generates the second tuner shop's own props (OBJ + MTL + PNG, Blender
convention: Y up, -Z forward, metres) into the tuner-garage package:

  resources/mods/packages/tuner-garage/interior/props/*.{obj,mtl,png}

Parts are made by us (docs/TUNER_MOD_PLAN.md rule), so nothing here comes from
the game files. The room places them from interior/props_itnr_b.txt through the
shared OBJ route (source/game/spots/obj_mesh.cpp):

  lift_2post   two-post car lift: base plate, two posts, four arms, top beam
  tire_stack   a stack of four tyres
  toolbox      a red roll cabinet with drawers
  drum         an oil drum with a coloured band
  rack         a steel shelf with crates (tyres / parts)
  neon_bar     a bright tube light (magenta / cyan) in a bracket

  python tools/mod/make_revyard_props.py
"""
import math
import os

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "resources", "mods", "packages", "tuner-garage", "interior", "props")


class Mesh:
    """Vertex / uv / normal soup grouped by material."""

    def __init__(self):
        self.v, self.vt, self.vn = [], [], []
        self.faces = {}  # material -> [[(v, vt, vn) * 3 or 4]]

    def _add(self, lst, item):
        lst.append(item)
        return len(lst)

    def quad(self, mat, pts, normal, uvs=((0, 0), (1, 0), (1, 1), (0, 1))):
        n = self._add(self.vn, normal)
        idx = []
        for p, uv in zip(pts, uvs):
            idx.append((self._add(self.v, p), self._add(self.vt, uv), n))
        self.faces.setdefault(mat, []).append(idx)

    def box(self, cx, cy, cz, sx, sy, sz, mat, uv_scale=1.0):
        """Axis-aligned box centred at (cx, cy, cz); size (sx, sy, sz)."""
        x0, x1 = cx - sx / 2, cx + sx / 2
        y0, y1 = cy - sy / 2, cy + sy / 2
        z0, z1 = cz - sz / 2, cz + sz / 2
        s = uv_scale
        self.quad(mat, [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)], (0, 0, 1),
                  ((0, 0), (s, 0), (s, s), (0, s)))
        self.quad(mat, [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1),
                  ((0, 0), (s, 0), (s, s), (0, s)))
        self.quad(mat, [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0),
                  ((0, 0), (s, 0), (s, s), (0, s)))
        self.quad(mat, [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0),
                  ((0, 0), (s, 0), (s, s), (0, s)))
        self.quad(mat, [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0),
                  ((0, 0), (s, 0), (s, s), (0, s)))
        self.quad(mat, [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0),
                  ((0, 0), (s, 0), (s, s), (0, s)))

    def cylinder(self, cx, cy, cz, r, h, mat, seg=20, cap_mat=None):
        """Vertical cylinder, base centre (cx, cy, cz), height h."""
        cap_mat = cap_mat or mat
        for i in range(seg):
            a0 = 2 * math.pi * i / seg
            a1 = 2 * math.pi * (i + 1) / seg
            x0, z0 = cx + r * math.cos(a0), cz + r * math.sin(a0)
            x1, z1 = cx + r * math.cos(a1), cz + r * math.sin(a1)
            am = (a0 + a1) / 2
            nrm = (math.cos(am), 0, math.sin(am))
            # counter-clockwise seen from outside
            self.quad(mat, [(x1, cy, z1), (x0, cy, z0), (x0, cy + h, z0), (x1, cy + h, z1)], nrm,
                      (((i + 1) / seg, 0), (i / seg, 0), (i / seg, 1), ((i + 1) / seg, 1)))
            # top cap fan (as quads degenerate to triangles)
            self.quad(cap_mat, [(cx, cy + h, cz), (x0, cy + h, z0), (x1, cy + h, z1), (cx, cy + h, cz)], (0, 1, 0),
                      ((0.5, 0.5), (0.5 + 0.5 * math.cos(a0), 0.5 + 0.5 * math.sin(a0)),
                       (0.5 + 0.5 * math.cos(a1), 0.5 + 0.5 * math.sin(a1)), (0.5, 0.5)))
            self.quad(cap_mat, [(cx, cy, cz), (x1, cy, z1), (x0, cy, z0), (cx, cy, cz)], (0, -1, 0),
                      ((0.5, 0.5), (0.5 + 0.5 * math.cos(a1), 0.5 + 0.5 * math.sin(a1)),
                       (0.5 + 0.5 * math.cos(a0), 0.5 + 0.5 * math.sin(a0)), (0.5, 0.5)))

    def write(self, name, materials):
        """materials: {mat name: (Kd rgb, texture file or None)}."""
        with open(os.path.join(OUT, name + ".mtl"), "w") as f:
            for mat, (kd, tex) in materials.items():
                f.write("newmtl %s\nKd %.3f %.3f %.3f\n" % (mat, *kd))
                if tex:
                    f.write("map_Kd %s\n" % tex)
                f.write("\n")
        with open(os.path.join(OUT, name + ".obj"), "w") as f:
            f.write("mtllib %s.mtl\n" % name)
            for v in self.v:
                f.write("v %.4f %.4f %.4f\n" % v)
            for t in self.vt:
                f.write("vt %.4f %.4f\n" % t)
            for n in self.vn:
                f.write("vn %.4f %.4f %.4f\n" % n)
            for mat, faces in self.faces.items():
                f.write("usemtl %s\n" % mat)
                for face in faces:
                    # drop a repeated (degenerate) last vertex: a fan triangle
                    if len(face) == 4 and face[0] == face[3]:
                        face = face[:3]
                    f.write("f " + " ".join("%d/%d/%d" % c for c in face) + "\n")


# ---------------------------------------------------------------------------
# textures (PIL)
# ---------------------------------------------------------------------------

def make_textures():
    from PIL import Image, ImageDraw
    import random
    rnd = random.Random(7)
    size = 128

    def save(img, name):
        img.save(os.path.join(OUT, name))

    # hazard yellow / black stripes (lift posts, base plate edges)
    img = Image.new("RGB", (size, size), (235, 190, 20))
    d = ImageDraw.Draw(img)
    for k in range(-size, size * 2, 32):
        d.polygon([(k, 0), (k + 16, 0), (k + 16 - size, size), (k - size, size)], fill=(25, 25, 25))
    save(img, "hazard.png")

    # brushed steel
    img = Image.new("RGB", (size, size), (120, 124, 130))
    d = ImageDraw.Draw(img)
    for y in range(size):
        v = 105 + rnd.randint(-14, 14)
        d.line([(0, y), (size, y)], fill=(v, v + 3, v + 8))
    save(img, "steel.png")

    # rubber with a tread hint
    img = Image.new("RGB", (size, size), (22, 22, 24))
    d = ImageDraw.Draw(img)
    for x in range(0, size, 8):
        d.rectangle([x, 0, x + 3, size], fill=(34, 34, 36))
    d.rectangle([0, 56, size, 70], fill=(40, 40, 44))
    d.text((8, 58), "WANGAN  RADIAL", fill=(210, 210, 215))
    save(img, "rubber.png")

    # red roll cabinet with drawers
    img = Image.new("RGB", (size, size), (170, 24, 28))
    d = ImageDraw.Draw(img)
    for i in range(6):
        y0 = 6 + i * 20
        d.rectangle([6, y0, size - 7, y0 + 16], fill=(150, 18, 22), outline=(60, 6, 8))
        d.rectangle([size // 2 - 14, y0 + 6, size // 2 + 14, y0 + 9], fill=(200, 200, 205))
    save(img, "cabinet.png")

    # oil drum (blue with a neon magenta band)
    img = Image.new("RGB", (size, size), (28, 50, 120))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 52, size, 66], fill=(255, 60, 200))
    for y in (20, 100):
        d.line([(0, y), (size, y)], fill=(18, 30, 80), width=3)
    save(img, "drum.png")

    # crate / part boxes
    img = Image.new("RGB", (size, size), (150, 110, 60))
    d = ImageDraw.Draw(img)
    for y in range(0, size, 16):
        d.line([(0, y), (size, y)], fill=(110, 78, 40), width=2)
    d.rectangle([2, 2, size - 3, size - 3], outline=(90, 62, 30), width=3)
    save(img, "crate.png")

    # neon tubes (flat saturated, bright)
    for name, colour in (("neon_magenta.png", (255, 90, 220)), ("neon_cyan.png", (90, 235, 255))):
        img = Image.new("RGB", (16, 16), colour)
        save(img, name)

    # dark painted metal (brackets, frames)
    img = Image.new("RGB", (size, size), (34, 36, 42))
    save(img, "darkmetal.png")


# ---------------------------------------------------------------------------
# the props
# ---------------------------------------------------------------------------

def lift_2post():
    m = Mesh()
    # base plate between the posts, hazard edge
    m.box(0, 0.03, 0, 3.3, 0.06, 1.1, "steel")
    m.box(0, 0.065, 0.55, 3.3, 0.02, 0.06, "hazard", uv_scale=8)
    m.box(0, 0.065, -0.55, 3.3, 0.02, 0.06, "hazard", uv_scale=8)
    for sx in (-1, 1):
        # post + foot + hazard collar
        m.box(sx * 1.55, 1.2, -0.35, 0.22, 2.4, 0.22, "darkmetal")
        m.box(sx * 1.55, 0.5, -0.35, 0.24, 1.0, 0.24, "hazard", uv_scale=2)
        m.box(sx * 1.55, 0.1, -0.35, 0.5, 0.12, 0.6, "steel")
        # carriage + two swing arms (long + short) reaching in
        m.box(sx * 1.55, 0.55, -0.35, 0.30, 0.3, 0.30, "steel")
        m.box(sx * 1.2, 0.55, 0.05, 0.12, 0.10, 0.9, "steel")
        m.box(sx * 1.2, 0.55, -0.7, 0.12, 0.10, 0.6, "steel")
        m.box(sx * 1.2, 0.62, 0.45, 0.22, 0.10, 0.22, "rubber")   # pad
        m.box(sx * 1.2, 0.62, -0.95, 0.22, 0.10, 0.22, "rubber")
    # top beam
    m.box(0, 2.35, -0.35, 3.35, 0.16, 0.2, "darkmetal")
    m.write("lift_2post", {
        "steel": ((0.6, 0.6, 0.62), "steel.png"),
        "darkmetal": ((0.3, 0.3, 0.34), "darkmetal.png"),
        "hazard": ((1, 1, 1), "hazard.png"),
        "rubber": ((0.4, 0.4, 0.42), "rubber.png"),
    })


def tire_stack():
    m = Mesh()
    for i in range(4):
        m.cylinder(0, i * 0.24, 0, 0.34, 0.24, "rubber", seg=20)
        m.cylinder(0, i * 0.24 + 0.005, 0, 0.17, 0.23, "darkmetal", seg=14)   # the hub opening reads dark
    m.write("tire_stack", {
        "rubber": ((1, 1, 1), "rubber.png"),
        "darkmetal": ((0.3, 0.3, 0.34), "darkmetal.png"),
    })


def toolbox():
    m = Mesh()
    m.box(0, 0.55, 0, 0.7, 1.0, 0.5, "cabinet")
    m.box(0, 1.07, 0, 0.74, 0.05, 0.54, "steel")
    for sx in (-1, 1):
        for sz in (-1, 1):
            m.cylinder(sx * 0.28, 0, sz * 0.18, 0.04, 0.05, "darkmetal", seg=8)
    m.write("toolbox", {
        "cabinet": ((1, 1, 1), "cabinet.png"),
        "steel": ((0.6, 0.6, 0.62), "steel.png"),
        "darkmetal": ((0.3, 0.3, 0.34), "darkmetal.png"),
    })


def drum():
    m = Mesh()
    m.cylinder(0, 0, 0, 0.3, 0.88, "drum", seg=20, cap_mat="steel")
    m.write("drum", {
        "drum": ((1, 1, 1), "drum.png"),
        "steel": ((0.6, 0.6, 0.62), "steel.png"),
    })


def rack():
    m = Mesh()
    for sx in (-1, 1):
        for sz in (-1, 1):
            m.box(sx * 0.9, 1.0, sz * 0.3, 0.06, 2.0, 0.06, "darkmetal")
    for y in (0.15, 0.85, 1.55):
        m.box(0, y, 0, 1.86, 0.05, 0.66, "steel")
    # crates / part boxes
    for i, (x, y, w, d) in enumerate(((-0.45, 0.35, 0.7, 0.45), (0.4, 0.35, 0.8, 0.4),
                                      (-0.5, 1.05, 0.6, 0.4), (0.3, 1.05, 0.9, 0.42),
                                      (0.0, 1.75, 1.2, 0.45))):
        m.box(x, y, 0, w, 0.36, d, "crate")
    m.write("rack", {
        "steel": ((0.6, 0.6, 0.62), "steel.png"),
        "darkmetal": ((0.3, 0.3, 0.34), "darkmetal.png"),
        "crate": ((1, 1, 1), "crate.png"),
    })


def neon_bar(name, tex):
    m = Mesh()
    m.box(0, 0, 0, 2.0, 0.07, 0.07, "tube")
    m.box(-1.02, 0, 0, 0.05, 0.14, 0.12, "frame")
    m.box(1.02, 0, 0, 0.05, 0.14, 0.12, "frame")
    m.write(name, {
        "tube": ((1, 1, 1), tex),
        "frame": ((0.3, 0.3, 0.34), "darkmetal.png"),
    })


def main():
    os.makedirs(OUT, exist_ok=True)
    make_textures()
    lift_2post()
    tire_stack()
    toolbox()
    drum()
    rack()
    neon_bar("neon_bar_magenta", "neon_magenta.png")
    neon_bar("neon_bar_cyan", "neon_cyan.png")
    print("props written to", OUT)


if __name__ == "__main__":
    main()

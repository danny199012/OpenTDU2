"""Hong Kong Island (from the owner's own TDUSC install) as a third island mod.

Terrain-only first pass (docs/re/tdusc_pws_format.md, docs/re/hongkong_island_mod.md):

    1. python tools/tdusc/pws_island.py --levels "<TDUSC>/WIN32/LEVELS/HONGKONG" --out C:/temp/hk_tiles
    2. python -I tools/mod/make_hongkong_island.py --gamedata C:/TDU2-Gamedata/TestDrive2 ^
           --userdata C:/temp/otdu2-win-userdata --tiles C:/temp/hk_tiles
    3. <exe> -game C:/TDU2-Gamedata/TestDrive2 -userdata C:/temp/otdu2-win-userdata -gm freeride -island hongkong -skipcine -tp 5632,7936

It writes <userdata>/mods/Euro/Bnk/islands/hongkong/ (never the game folder): a 3 x 3 area island (18.4 km), the
heightfield of every sector that holds land (3 m grid) + every area (24 m grid) + the island (384 m grid), colour maps
generated from the heights (hillshade + elevation palette), no objects, no roads. Donors from Hawaii:
sector-6-4-6-7 (a bare terrain sector), area-6-4, hawai.bnk (island ocean), commonworld / worldactors / weathers.

World mapping [INF, chosen]: Eden x = HK x + 9472, Eden z = 6912 - HK y (TDUSC y is north, Eden +z is south, so north is up on
the map), heights - 1.8 m (TDUSC sea = 0, the donor island ocean plane = -1.8 m).
"""
import argparse
import glob
import os
import shutil
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_eden_island as me  # noqa: E402

HK_X0 = -9472.0          # TDUSC world x of Eden x = 0
HK_Y_NORTH = 6912.0      # TDUSC world y of Eden z = 0
HEIGHT_SHIFT = -1.8      # TDUSC sea level 0 -> the donor ocean plane
SEA_FLOOR = -40.0
COLS = ROWS = 3
AREA = me.AREA_SIZE
SECTOR = me.SECTOR_SIZE
HF = me.HF_SIZE


# ------------------------------------------------------------------ the TDUSC height raster (1 m)

class HeightRaster:
    """All decoded tiles at 1 m. Row 0 = y_min, column 0 = x_min (TDUSC axes, y north)."""

    def __init__(self, tile_dir):
        tiles = []
        for f in glob.glob(os.path.join(tile_dir, "*.npy")):
            x0, y0 = (int(v) for v in open(f[:-4] + ".org").read().split())
            tiles.append((x0, y0, f))
        if not tiles:
            raise SystemExit(f"no tiles in {tile_dir}")
        self.x0 = min(t[0] for t in tiles)
        self.y0 = min(t[1] for t in tiles)
        w = max(t[0] for t in tiles) + 512 - self.x0
        h = max(t[1] for t in tiles) + 512 - self.y0
        a = np.full((h, w), np.nan, np.float32)
        for x0, y0, f in tiles:
            t = np.load(f)                                # 1024 x 1024 at 0.5 m, row = y
            q = t.reshape(512, 2, 512, 2)
            fin = np.isfinite(q)
            cnt = fin.sum(axis=(1, 3))
            s = np.where(fin, q, 0).sum(axis=(1, 3))
            m = np.where(cnt > 0, s / np.maximum(cnt, 1), np.nan).astype(np.float32)
            r, c = y0 - self.y0, x0 - self.x0
            sub = a[r:r + 512, c:c + 512]
            ok = np.isfinite(m)
            sub[ok] = m[ok]
        self.a = a
        self.h, self.w = a.shape
        print(f"  raster {w} x {h} m, {np.isfinite(a).mean() * 100:.1f} % with data", flush=True)
        # a coarse, fully filled copy (16 m) for the holes and the open sea
        b = 16
        hh, ww = h // b, w // b
        blk = a[:hh * b, :ww * b].reshape(hh, b, ww, b)
        fin = np.isfinite(blk)
        cnt = fin.sum(axis=(1, 3))
        sm = np.where(fin, blk, 0).sum(axis=(1, 3))
        coarse = np.where(cnt > 0, sm / np.maximum(cnt, 1), np.nan).astype(np.float32)
        known = np.isfinite(coarse)
        cur = np.where(known, coarse, SEA_FLOOR)
        for _ in range(400):                              # diffuse the coast values into the unknown
            p = np.pad(cur, 1, mode="edge")
            avg = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:]) * 0.25
            cur = np.where(known, coarse, avg)
        # unknown cells never rise above the sea floor cap
        cur = np.where(known, cur, np.minimum(cur, -8.0))
        self.coarse = cur
        self.cb = b

    def sample(self, x, y):
        """TDUSC world (x, y) arrays -> height, bilinear on the 1 m raster, coarse fill where there is no data."""
        u = np.asarray(x, np.float64) - self.x0 - 0.5
        v = np.asarray(y, np.float64) - self.y0 - 0.5
        inside = (u >= 0) & (v >= 0) & (u < self.w - 1) & (v < self.h - 1)
        uc = np.clip(u, 0, self.w - 1.001)
        vc = np.clip(v, 0, self.h - 1.001)
        i0 = uc.astype(np.int64)
        j0 = vc.astype(np.int64)
        fu = (uc - i0).astype(np.float32)
        fv = (vc - j0).astype(np.float32)
        a = self.a
        h00, h10, h01, h11 = a[j0, i0], a[j0, i0 + 1], a[j0 + 1, i0], a[j0 + 1, i0 + 1]
        val = (h00 * (1 - fu) + h10 * fu) * (1 - fv) + (h01 * (1 - fu) + h11 * fu) * fv
        # coarse fill: nearest 16 m cell
        cj = np.clip(((np.asarray(y) - self.y0) // self.cb).astype(np.int64), 0, self.coarse.shape[0] - 1)
        ci = np.clip(((np.asarray(x) - self.x0) // self.cb).astype(np.int64), 0, self.coarse.shape[1] - 1)
        fill = self.coarse[cj, ci]
        out = np.where(np.isfinite(val) & inside, val, fill)
        return out.astype(np.float32)


class World:
    """Eden-frame height lookups."""

    def __init__(self, raster):
        self.r = raster

    def height(self, ex, ez):
        return self.r.sample(np.asarray(ex) + HK_X0, HK_Y_NORTH - np.asarray(ez)) + HEIGHT_SHIFT

    def grid(self, ex0, ez0, step, n):
        """n x n heights, rows = z, cols = x (the heightfield vertex order j * n + i)."""
        xs = ex0 + np.arange(n) * step
        zs = ez0 + np.arange(n) * step
        gx, gz = np.meshgrid(xs, zs)
        return self.height(gx, gz)


# ------------------------------------------------------------------ normals / 3dg patching

def normals_from_heights(h, step):
    dz, dx = np.gradient(h.astype(np.float64), step)     # axis 0 = z (j), axis 1 = x (i)
    n = np.stack([-dx, np.ones_like(dx), -dz], -1)
    n /= np.linalg.norm(n, axis=2)[..., None]
    out = np.zeros(h.shape + (4,), np.float16)
    out[..., :3] = n.astype(np.float16)
    return out


def patch_heightfield(data, heights, step):
    num, s0, s1 = me.heightfield_streams(data)
    assert num == HF * HF and heights.shape == (HF, HF)
    out = bytearray(data)
    out[s0:s0 + num * 4] = np.ascontiguousarray(heights, "<f4").tobytes()
    out[s1:s1 + num * 8] = np.ascontiguousarray(normals_from_heights(heights, step)).tobytes()
    return bytes(out)


# ------------------------------------------------------------------ colour maps (DXT1 + mips, donor layout)

def shade_colour(h, step, sea=HEIGHT_SHIFT):
    """h: (n, n) Eden heights -> uint8 RGB (n, n, 3), elevation palette x hillshade. Rows = z (south), cols = x (east)."""
    dz, dx = np.gradient(h.astype(np.float64), step)
    nrm = np.stack([-dx, np.ones_like(dx), -dz], -1)
    nrm /= np.linalg.norm(nrm, axis=2)[..., None]
    light = np.array([-0.45, 0.75, -0.45])               # from the north-west, high
    light /= np.linalg.norm(light)
    shade = np.clip(nrm @ light, 0, 1) / light[1]
    shade = np.clip(0.45 + 0.65 * shade, 0.25, 1.25)
    slope = np.sqrt(dx * dx + dz * dz)
    above = h - sea

    def lerp(a, b, t):
        t = np.clip(t, 0, 1)[..., None]
        return np.array(a, np.float64) * (1 - t) + np.array(b, np.float64) * t

    land = lerp((156, 152, 140), (84, 118, 58), above / 28.0)           # quay / city -> green
    land = lerp(land, (54, 92, 44), (above - 40) / 200.0)               # forest
    land = lerp(land, (128, 116, 98), (above - 330) / 160.0)            # high rock
    land = lerp(land, (122, 112, 102), (slope - 0.55) / 0.5)            # steep faces
    depth = np.clip(-above / 45.0, 0, 1)
    sea_c = lerp((92, 150, 172), (12, 42, 88), depth)
    beach = np.clip((above + 1.5) / 1.5, 0, 1)[..., None]               # a thin lighter rim at the waterline
    col = np.where((above > 0.1)[..., None], land * shade[..., None], sea_c * (0.9 + 0.1 * shade[..., None]))
    col = col * (1 - 0.0 * beach)
    return np.clip(col, 0, 255).astype(np.uint8)


def encode_dxt1(img):
    """uint8 (H, W, 3), H and W multiples of 4 -> DXT1 bytes (bounding-box endpoints, 4-colour mode)."""
    h, w, _ = img.shape
    blk = img.reshape(h // 4, 4, w // 4, 4, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 16, 3).astype(np.int32)
    lo = blk.min(axis=1)
    hi = blk.max(axis=1)

    def to565(c):
        return ((c[:, 0] >> 3) << 11) | ((c[:, 1] >> 2) << 5) | (c[:, 2] >> 3)

    c0, c1 = to565(hi), to565(lo)
    swap = c0 < c1
    c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
    same = c0 == c1                                        # flat block: c0 == c1 would select the 3-colour mode
    c0 = np.where(same & (c0 < 0xFFFF), c0 + 1, c0)

    def from565(c):
        r = ((c >> 11) & 31) * 255 // 31
        g = ((c >> 5) & 63) * 255 // 63
        b = (c & 31) * 255 // 31
        return np.stack([r, g, b], -1)

    p0, p1 = from565(c0), from565(c1)
    pal = np.stack([p0, p1, (2 * p0 + p1) // 3, (p0 + 2 * p1) // 3], 1)      # (N, 4, 3)
    d = ((blk[:, :, None, :] - pal[:, None, :, :]) ** 2).sum(-1)               # (N, 16, 4)
    idx = d.argmin(-1).astype(np.uint32)
    bits = np.zeros(len(idx), np.uint32)
    for k in range(16):
        bits |= idx[:, k] << (2 * k)
    out = np.zeros((len(idx), 3), "<u4")
    out[:, 0] = (c0.astype(np.uint32) | (c1.astype(np.uint32) << 16))
    out[:, 1] = bits
    rec = np.zeros(len(idx), dtype=[("c", "<u4"), ("b", "<u4")])
    rec["c"] = out[:, 0]
    rec["b"] = out[:, 1]
    return rec.tobytes()


def dxt1_chain(img, payload_len):
    """Mip chain down to 2 x 2 like the donor (payload size asserted)."""
    parts = []
    cur = img
    while True:
        h, w, _ = cur.shape
        if h >= 4 and w >= 4:
            parts.append(encode_dxt1(cur))
        else:
            parts.append(encode_dxt1(np.pad(cur, ((0, 4 - h), (0, 4 - w), (0, 0)), mode="edge")))
        if h <= 2 or w <= 2:
            break
        cur = (cur.reshape(h // 2, 2, w // 2, 2, 3).astype(np.uint16).sum(axis=(1, 3)) // 4).astype(np.uint8)
    data = b"".join(parts)
    if len(data) != payload_len:
        raise SystemExit(f"mip chain {len(data)} != donor payload {payload_len}")
    return data


def replace_2db(donor_2db, rgb, new_name):
    """Same header (size / mips / format 0x33) and name hash patched; DXT1 payload from rgb."""
    out = bytearray(donor_2db)
    w, h = struct.unpack_from("<HH", donor_2db, 0x28)
    assert rgb.shape[:2] == (h, w), (rgb.shape, w, h)
    payload = dxt1_chain(rgb, len(donor_2db) - 0x50)
    out[0x50:] = payload
    struct.pack_into("<Q", out, 0x20, me.identifier64(new_name))
    return bytes(out)


# ------------------------------------------------------------------ assembly

def entry_named(entries, suffix):
    for e in entries:
        if e["path"].rpartition("\\")[2].lower().endswith(suffix.lower()):
            return e
    raise KeyError(suffix)


def build_sector(world, donor_bank, donor_name, ax, ay, sx, sy, colour):
    name = f"sector-{ax}-{ay}-{sx}-{sy}"
    corner_x = (8 * ax + sx) * SECTOR + (SECTOR / 2 if sy % 2 else 0.0)
    corner_z = (8 * ay + sy) * SECTOR
    heights = world.grid(corner_x, corner_z, 3.0, HF)
    entries = me.retitle_entries(donor_bank, [(donor_name, name)])
    e = entry_named(entries, f"{name}.3dg")
    e["data"] = patch_heightfield(e["data"], heights, 3.0)
    empty = entry_named(entries, f"{name}-O.pmi")["data"]
    entry_named(entries, f"{name}-V.pmi")["data"] = empty           # no donor vegetation
    if colour:
        sc = shade_colour(world.grid(corner_x, corner_z, 1.5, 512), 1.5)
        ec = entry_named(entries, f"{name}-c.2db")
        ec["data"] = replace_2db(ec["data"], sc, f"{name}-c")
        es = entry_named(entries, f"{name}-s.2db")
        flat = np.zeros((512, 512, 3), np.uint8)
        flat[..., 0] = 255                                           # R: lit, G: no street lamps, B: no grass
        es["data"] = replace_2db(es["data"], flat, f"{name}-s")
    return name, entries, heights


def main():
    ap = argparse.ArgumentParser(description="TDUSC Hong Kong Island as a terrain-only island mod")
    ap.add_argument("--gamedata", required=True)
    ap.add_argument("--userdata", required=True)
    ap.add_argument("--tiles", default="C:/temp/hk_tiles")
    ap.add_argument("--island", default="hongkong")
    ap.add_argument("--source-island", default="hawai")
    ap.add_argument("--donor-area", default="6-4")
    ap.add_argument("--donor-sector", default="6-7")
    ap.add_argument("--max-sectors", type=int, default=0, help="debug: stop after N sectors")
    ap.add_argument("--no-colour", action="store_true", help="keep the donor colour maps")
    ap.add_argument("--land-margin", type=float, default=384.0, help="ship sectors with land within this distance (m)")
    args = ap.parse_args()
    colour = not args.no_colour

    dax, day = (int(v) for v in args.donor_area.split("-"))
    dsx, dsy = (int(v) for v in args.donor_sector.split("-"))
    src_root = os.path.join(args.gamedata, "Euro", "Bnk", "islands", args.source_island)
    src_level = os.path.join(src_root, "level")
    out_root = os.path.join(args.userdata, "mods", "Euro", "Bnk", "islands", args.island)
    me.selftest_xtea(args.gamedata, args.source_island)

    print("loading the TDUSC height tiles ...", flush=True)
    world = World(HeightRaster(args.tiles))

    # which sectors carry land: a coarse test on a 48 m grid of the sector footprint + margin
    shipped = []
    for ay in range(ROWS):
        for ax in range(COLS):
            for sy in range(8):
                for sx in range(8):
                    cx = (8 * ax + sx) * SECTOR + (SECTOR / 2 if sy % 2 else 0.0)
                    cz = (8 * ay + sy) * SECTOR
                    m = args.land_margin
                    g = world.grid(cx - m, cz - m, 48.0, int((SECTOR + 2 * m) / 48.0) + 1)
                    if g.max() > HEIGHT_SHIFT + 1.0:
                        shipped.append((ax, ay, sx, sy))
    print(f"{len(shipped)} of {COLS * ROWS * 64} sectors carry land", flush=True)
    if args.max_sectors:
        shipped = shipped[:args.max_sectors]

    donor_area_dir = os.path.join(src_level, f"area-{dax}", f"area-{dax}-{day}")
    donor_sector_name = f"sector-{dax}-{day}-{dsx}-{dsy}"
    donor_sector = me.read_bank(os.path.join(donor_area_dir, donor_sector_name + ".bnk"))
    donor_area = me.read_bank(os.path.join(donor_area_dir, f"area-{dax}-{day}.bnk"))

    # ---- sectors
    for n, (ax, ay, sx, sy) in enumerate(shipped):
        name, entries, _ = build_sector(world, donor_sector, donor_sector_name, ax, ay, sx, sy, colour)
        path = os.path.join(out_root, "level", f"area-{ax}", f"area-{ax}-{ay}", name + ".bnk")
        data = me.build_bank(entries, creation_date=donor_sector["header"].get("creationDate", 0))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "wb").write(data)
        if n % 25 == 0:
            print(f"  sector {n + 1}/{len(shipped)} {name} ({len(data):,} bytes)", flush=True)

    # ---- areas (24 m heightfield, 1024^2 colour map, far objects sunk)
    for ay in range(ROWS):
        for ax in range(COLS):
            aname = f"area-{ax}-{ay}"
            entries = me.retitle_entries(donor_area, [(f"area-{dax}-{day}", aname)])
            heights = world.grid(ax * AREA, ay * AREA, 24.0, HF)
            e = entry_named(entries, f"{aname}.3dg")
            e["data"] = patch_heightfield(e["data"], heights, 24.0)
            o = entry_named(entries, f"{aname}-o.3dd")
            o["data"], _p, _s = me.patch_area_far_objects(o["data"], f"{dax}-{day}", f"{ax}-{ay}", set())
            if colour:
                ec = entry_named(entries, f"{aname}-c.2db")
                ec["data"] = replace_2db(ec["data"], shade_colour(world.grid(ax * AREA, ay * AREA, 6.0, 1024), 6.0),
                                         f"{aname}-c")
            path = os.path.join(out_root, "level", f"area-{ax}", aname, aname + ".bnk")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            open(path, "wb").write(me.build_bank(entries, creation_date=donor_area["header"].get("creationDate", 0)))
            print(f"  area {aname}", flush=True)

    # ---- the island bank (ocean + 384 m heightfield + the whole-island map)
    island_bank = me.read_bank(os.path.join(src_level, f"{args.source_island}.bnk"))
    old, new = args.source_island, args.island
    entries = me.retitle_entries(island_bank, [(f"{old}-o", f"{new}-o"), (old, new)])
    entries = [e for e in entries if not e["path"].rpartition("\\")[2].lower().endswith(".ia")]
    ie = entry_named(entries, f"{new}.3dg")
    ih = np.full((HF, HF), -100.0, np.float32)
    n_in = int(round(COLS * AREA / 384.0)) + 1
    ih[:n_in, :n_in] = world.grid(0.0, 0.0, 384.0, n_in)
    ie["data"] = patch_heightfield(ie["data"], ih, 384.0)
    if colour:
        ic = entry_named(entries, f"{new}-c.2db")
        ic["data"] = replace_2db(ic["data"], shade_colour(world.grid(0.0, 0.0, COLS * AREA / 1024.0, 1024), COLS * AREA / 1024.0),
                                 f"{new}-c")
    path = os.path.join(out_root, "level", f"{new}.bnk")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(me.build_bank(entries, creation_date=island_bank["header"].get("creationDate", 0)))
    print(f"  island bank {path}", flush=True)

    # ---- verbatim support files + worldconfig
    for src, dst in [
        (os.path.join(src_level, "commonworld.bnk"), os.path.join(out_root, "level", "commonworld.bnk")),
        (os.path.join(src_root, "scenarics", "packs", "worldactors.bnk"),
         os.path.join(out_root, "scenarics", "packs", "worldactors.bnk")),
        (os.path.join(src_root, "fx", "game_weathers.xmb"), os.path.join(out_root, "fx", "game_weathers.xmb")),
    ]:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
    config = (
        '<?xml version="1.0" encoding="iso-8859-1"?>\r\n'
        f'<WorldConfig version="1" level="{new.capitalize()}">\r\n'
        f'  <Area NbColumns="{COLS}" NbRows="{ROWS}" />\r\n'
        '  <MapRotation>0</MapRotation>\r\n'
        '  <MapInclination Zoom="Island">0</MapInclination>\r\n'
        '  <MapInclination Zoom="Far">20</MapInclination>\r\n'
        '  <MapInclination Zoom="Medium">30</MapInclination>\r\n'
        '  <MapInclination Zoom="Close">40</MapInclination>\r\n'
        '  <MapInclination Zoom="Very Close">50</MapInclination>\r\n'
        '  <MapDistanceAddedToZoom Zoom="Island" Screen="4/3">60000</MapDistanceAddedToZoom>\r\n'
        '  <MapDistanceAddedToZoom Zoom="Island" Screen="16/9">35000</MapDistanceAddedToZoom>\r\n'
        '  <MapCenterCorrection Screen="4/3">\r\n    <X>0</X>\r\n    <Z>0</Z>\r\n  </MapCenterCorrection>\r\n'
        '  <MapCenterCorrection Screen="16/9">\r\n    <X>0</X>\r\n    <Z>0</Z>\r\n  </MapCenterCorrection>\r\n'
        '</WorldConfig>'
    ).encode("ascii")
    open(os.path.join(out_root, "level", "worldconfig.cpr"), "wb").write(me.encrypt_cpr(config))
    print("done. Drive it:  <exe> -game", args.gamedata, "-userdata", args.userdata,
          "-gm freeride -island", new, "-skipcine -tp 5632,7936", flush=True)


if __name__ == "__main__":
    main()

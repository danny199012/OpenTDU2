# Island road data (Ibiza_ROAD_MAP.IA, Ibiza_ROAD_DETAIL.IA)

Tags as in CLAUDE.md. Both are entries of `islands/<island>/level/<island>.bnk`.

## ROAD_MAP.IA `[DATA]`
`RI41DAEH` header, then `EDON` nodes (40 B: +4 id (1-based), +12 x, +16 y (unused, 0 after load), +20 z) and `DAOR`
roads (48 B: +4 id, +28 / +32 the two node ids). Ibiza: 856 nodes, 1392 roads, 1350 resolve to two real nodes. The
word at node +8 holds the link count in its low bytes (0x303 = 3 links); the word at road +12 has small flag values
(0, 1, 2, 256, 512, ...) `[INF]` road class, not decoded.

### The road +12 field (the class byte)
It is **two independent `u8`s** (`+12` and `+13`, each small) - the `+12` u16 reads `0x0000..0x0404`, i.e. two
fields of the same shape, `[INF]` a road property per direction / per end, not decoded. Their values grow with the
island: **Ibiza 0..4** (link counts by `+12`: 728 / 246 / 184 / 97 / 95), **Hawai 0..11**, Casino only 0. The two
bytes are *not* independent of the geometry in the way a plain class would be: `+12 = 0` is the only value whose
links form long connected chains (84.7 / 73.1 km components; every other value breaks into 3-10 km pieces), the
mean edge length peaks on value 2 (873 m, one 11.5 km link = the ring road) and is shortest on 3/4 (400 m).

The retail map colours a road by its **AIRL type** - `GamePC.cpr` `[HudMap]` (docs/re/freeride_spec.md 4): 0 highway
29/180/215 w4, 1 secondary 0/255/255 w3, 2 road 0/255/255 w3, **3 offroad 243/196/6 w3**, 4 circuit 102/102/204 w4,
5 wip, 6 tunnel 37/82/129; a locked (undiscovered) road 140/140/140, the driven trace 254/216/197/200 w2 and the GPS
path 255/102/0 w5. Which value of the road map is the offroad one is **not decoded**: `MapScreen::kRoadClassDirt = 3`
(the offroad slot of that table) is the default, `-dirtclass <n>` switches it and `-logroads` prints the histogram.
Checked and rejected as identifiers: the island colour map (`<island>-c.2db`) draws no roads at all (on-road vs
40 m off-road pixels differ by <1/255), the sector `.rd` splines carry no class (`SR20` blocks are only
`u32 nPoints, 0, 1` + `n` x `(x, y, z, 1)`), and the `ATED` detail blocks repeat the same `+12` pair in their header
word 2 (257 / 256 / 514 / 512 / 768).

### Dirt roads: the class byte is NOT known to be the type (2026-11-14, `main`)
The satnav colours a driven road by this byte (`3` = yellow), but nothing proves `+12 == 3` is a dirt track. Tests, all negative:
- `-scanroadsurface` (ray down at each link's middle): every class, **including 3**, hits the `-R.shk` road collision (Ibiza class 3: 96 of 97 `road`, friction 0.7 on all) - so a dirt track is not
  distinguished by `Road` vs `Terrain` nor by the Havok body friction.
- `-logroadfields` / `-logroads`: `+12` / `+13` are two bytes each 0..4 on Ibiza (0..9 / 0..11 on Hawai); `+8` looks like (flag, lane count 1..5); `+24 / +36 / +40 / +44` are almost always 0.
  **Not** the node slot index: `(node, slot)` pairs clash 40 % of the time for both readings (`slot test` lines).
- Hawai has 58 % of its links in class 0 and only 164 of 2827 in class 3, which does not look like the island with the big dirt region.
The retail physics does know the ground (`FUN_00ba81a0`: material ids `1, 0x3d, 0x3e` = dirt), but our Havok reader keeps no per-triangle material ids (`HavokCollision` has none): reading them from
the `hkpStorageExtendedMeshShape` subparts is the way to classify a link from data. Until then the yellow roads are a guess. Debug flags: `-scanroadsurface`, `-logroads`, `-logroadfields`.

**Per-triangle material ids (2026-11-14, later) [DATA].** `HavokCollision::TriMaterial` now carries each triangle's mesh material (`hkpStorageExtendedMeshShape` `MeshSubpartStorage`
`m_materialIndices` @`+0x2c` (u8 per triangle) -> `m_materials` @`+0x38` (u32) [INF layout; it yields real values, not noise]), `VehicleWorldCollision::Hit::Material` reports it.
Triangle counts on Ibiza: road meshes `0x0:419757 0x8:15600 0x9:2542 0x3f:14034 0x40:13977 0x41:234317 0x43:3588 ...`, static meshes `0x0:498411 0x1:23311 0x2:22237 0x3:136564 ...`
(Hawai alike, bigger). The retail table (`FUN_00ba81a0`) calls ids `1, 0x3d, 0x3e` dirt and `2, 4, 5, 0xb..0xd, 0x2b..0x2e, 0x42` grass. **Result: under the middle of every road link (all classes) the road
mesh is material `0` (a few `8`)** - class 3 included (96 of 97 links `R0`) - so the collision does **not** mark dirt tracks either; `0x3f / 0x40 / 0x41 / 0x43` are meshes beside the links (shoulders?).
Material `1` (dirt) exists only on static meshes (23311 triangles) and under 7 class-2 links. The dirt / grass / sand distinction of a track therefore lives in the **terrain paint** (the sector colour
maps carry the sand / mud textures, surface_effects_spec.md 1), not in the road graph or the road collision. Still undecoded: which `DAOR` byte, if any, is the AIRL type. Next lead: the dev exe's
`HudMapRoads` draw code (`0xc9e584`..) and how it picks the colour per road (it reads the road list from a resource, not `ROAD_MAP.IA` [INF]).

**ROAD_DETAIL sub-block headers vs class (2026-11-14, `-logroadfields`).** The 5 header words of each record's first sub-block (`0x60007, 18, 257, 1, 0xc90204`...) crossed with the `DAOR +12` class on Ibiza and
Hawai: nothing separates a class (word 0 high half `3` is the only thing classes 3 / 4 share, but classes 0..2 have it too; word 2 = `0x100 / 0x101 / 0x200 / 0x202 / 0x300`; the low half of word 4 =
`0x203 / 0x204 / 0x302 / 0x4 / 0x5 / 0x104` is spread over every class). GLM's dev-exe read says the satnav colour is a 7-entry table at `0x1687174` indexed by a type byte at road piece `+0x11` filled from a
runtime `RoadNetworkPoly` resource, not by `ROAD_MAP.IA` [CODE-dev, per the owner's relay; not re-checked]. The commonworld bank has road texture families `crossroad_color_{circuit,country,goudron,national,town}` /
`crossroad_detail_{country,oldtown,town}` - a likely naming of the road types (country = dirt track) [INF]; the type per link may come from the road mesh textures of the sectors.

## ROAD_DETAIL.IA `[DATA]`
`DI41DAEH` header, then **`ATED`** records, one per road (1392, id = the DAOR id): `+4 id, +8 N` (9 on road 1),
`N` byte offsets from the record start, then N sub-blocks of 5 header words (`0x60007, 18, 257, 1, 0xc90204` ...
`[INF]` flags) and float triples `(x, y, z)` with a 0 word after groups of points. Concatenated, the triples of a record
are the **curve of the road** (road 1: 96 points over ~1020 m); the first / last point lie within ~10 m of the
road's two nodes (checked on roads 1-12, orientation either way). Stray float triples can match the filter: the
loader (`MapScreen::loadRoads`) rejects points more than 250 m from the previous one. `FTED` (791) and `ETED` (64)
records follow (junction / area data, not decoded).
Not decoded: lanes, speed limits, signals (there is no signal data found in either file).

## Used by
- `MapScreen`: `getRoadShapes()` (curve per link, ends on the nodes), explored tracking (distance to the curve) and drawing.
- `Traffic`: lanes follow the curve (`docs/re/traffic_spec.md`).

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

# Hong Kong Island as a third island mod (terrain-only, session 62)

Tags: `[CODE]` port source, `[DATA]` game files, `[INF]` chosen / inferred. Source data: the owner's own TDUSC install (`docs/re/tdusc_pws_format.md`).
Nothing from TDUSC is in the repo; the mod is generated into `<userdata>/mods/Euro/Bnk/islands/hongkong/` (never the game folder).

## Build and run

```
python tools/tdusc/pws_island.py --levels "E:/SteamLibrary/steamapps/common/TDUSC/WIN32/LEVELS/HONGKONG" --out C:/temp/hk_tiles --jobs 18   # 43 min, 2.4 GB
python tools/mod/make_hongkong_island.py --gamedata C:/TDU2-Gamedata/TestDrive2 --userdata C:/temp/otdu2-win-userdata --tiles C:/temp/hk_tiles   # 1m45s, 388 MB
build-main\bin\opentdu2-main.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm freeride -island hongkong -skipcine -nopad -tp 5632,7936
build-main\bin\opentdu2-main.exe ... -gm worldviewer -island hongkong -nopanel -campos 14848,520,7600 -camyaw 0 -campitch -25   # aerial (F-key list has "Hong Kong")
```
(`python`, not `python -I`: the tool needs numpy from the user site.) `-weather 00` is night, `12` midday; the default `14` looks washed out (below).

## What it writes [CODE loader facts from eden_island_spec.md, DATA donors from Hawaii]

* `level/worldconfig.cpr` - **3 x 3 areas** (18.4 km), XTEA-CBC like Eden's.
* **269 of 576 sectors** (those with land within 384 m): donor `sector-6-4-6-7` (a bare terrain sector: 793 KB heightfield `.3dg`, empty `-O/-A/-F/-S.pmi`) renamed to
  `sector-<ax>-<ay>-<sx>-<sy>`; its **257x257 height stream and half4 normal stream are replaced** (3 m grid); `-V.pmi` (donor vegetation) replaced by the empty `-O.pmi`.
* **9 areas**: donor `area-6-4`; 24 m heightfield; `-o.3dd` hashcodes renamed and every non-ocean far object sunk (`patch_area_far_objects(.., shipped=set())`).
* **island bank**: donor `hawai.bnk` (ocean `-o`, no road IA); 384 m heightfield (-100 outside the 18.4 km box).
* colour maps (`-c.2db`, sectors 512^2, areas / island 1024^2) **generated**: elevation palette x hillshade, DXT1 with the donor's exact mip layout (payload size asserted); `-s` maps = R 255 / G 0 / B 0.
* verbatim from Hawaii: `commonworld.bnk`, `worldactors.bnk`, `game_weathers.xmb`.
* No road graph, spots, objects, grass, traffic, ambience.

## Facts established on the way

* **Heightfield vertex normals [DATA, verified against donor data]:** `n = normalize(-dh/dx, 1, -dh/dz)` written as half4 (w = 0); correlation with finite differences 1.000 on all three axes, grid `i` = x, `j` = z, spacing 3 / 24 / 384 m. R32 height is absolute world y.
* The port does not use the 3dd `HMAP` for the terrain (it draws the 257^2 grid and reads the heights for collision), so patching the two streams is enough.
* **DXT1 `.2db`** (format `0x33`): header `0x50`, payload = mip chain down to **2 x 2** (sector 512^2: 174,768 B; area / island 1024^2: 699,056 B).
* **World mapping [INF, chosen]:** Eden `x = HK x + 9472`, `z = 6912 - HK y` (TDUSC y is north, Eden +z is south, so north is up on the map), heights `- 1.8 m` (TDUSC sea 0 -> the donor ocean plane -1.8 m, `eden_island_spec.md` fact 6). Sea fill for tile gaps: diffused coast values capped at -8 m.

## Verified (headless)

* Boot log: `hongkong: 3 x 3 areas (worldconfig.cpr)`, `9 areas loaded`, `9 sectors loaded around (5632, 7936)`, `island ocean bounds (-98304 -2 -98304)`, `WORLD_VIEWER: hongkong ready ... island heightfield yes`. No parse errors (only the optional grass / clouds / ambience files are missing, like Eden).
* FREERIDE `-tp 5632,7936`: the car stands on the quay platform with hills behind it (collision from the heightfield works). World viewer aerials show the coastline, bays and islets, and the ridge road of tile `X63_Y42` as real relief. Screenshots are kept locally (TDUSC-derived art stays out of the repo): `_hk1.png`, `_hk4.png`, `_hk5.png`, `_hk7.png`.
* Source change: `gm_world_viewer.{h,cpp}` - `kIslands` gains `"hongkong"`, label "Hong Kong".

## Driving tips

* **Use `-tp x,y,z`** with a real height (the x,z form puts the car at y = 0, inside any hill: it was thrown through the clouds). A flat hill spot: `-tp 14792,191,6868` (tile `X63_Y42`); the quay: `-tp 5632,3,7936`.

## Open / next

* ~~Daytime look washed out~~ **fixed**: the terrain `.2dm` stores the donor's colour / shadow map name hashes (`0x158` / `0x178`); renaming the textures without patching them left the maps unresolved (white terrain, with the donor maps too). `patch_2dm_hashes` now rewrites them (sector, area, island). Verified: real Hawaii at the same weather / camera is green and detailed (`_hw1.png`), Hong Kong now green with coast, bays and the ridge road (`_hk8.png`), and a free-ride drive up a grass hillside in daylight (`_hk9.png`, `-weather 12`).
* Colour maps are a flat palette: no roads / buildings painted; TDUSC's real textures are in the PKGs (PTX decode exists) but nothing maps a texture to a tile yet.
* No road graph: GPS, traffic, the minimap roads, spots and races need one (roads exist only as ridges in the heightfield). `add_spot.py` and the Eden notes (s.4) apply.
* No buildings: the PWS data after the 33x33 grid is undecoded; HK's buildings / props are the next big piece.
* Spawn / HUD: the zone label says "Ibiza Area 1"; the map screen needs `worldconfig` map-centre tuning for an 18 km island.
* The 61 pure-sea sectors are absent by design; the area mesh (24 m) shows there.
* Sea floor outside the data is a diffused fill; the sector terrain at the data's edge may show a seam underwater.

## Spawn / HUD / map tuning (session 62)

* **Start point**: `-island hongkong` with no `-tp` / `-home` / save now starts at the Central quay `(5632, 3, 7936)`, heading 0 (`gm_freeride.cpp`, log `FREERIDE: hongkong start point`).
  The mod has no spots / airport; y must be a real height (y 0 is inside terrain). `-tp x,y,z` still overrides.
* **Zone badge**: with no `cruisingzones.bin` the HUD showed the hardcoded "Ibiza Area 1"; an island that is not ibiza / hawai / casino now names itself
  (`hongkong` -> "Hong Kong Island", others -> the capitalised folder name) (`freeride_hud.cpp`, the `findNearest` fallback).
* **Map screen** (`-autoplay "400:press=map"`): opens without errors and draws the island from the island colour map (the shaded elevation palette), the whole 18 km island fills the frame and the car marker
  `MapScreen: car at (5632, 7936)` lands on the west bay. The centre cursor shows the red "not allowed" marker (no road edges / spots on the island). No road, spot or bookmark data exists, so the
  map has no overlays; the zone, travel and licence code paths still assume ibiza / hawai (`map_screen.cpp` `roadIsland == "hawai"` tests), so travel to / from Hong Kong is not wired.
* Minimap is black (it draws road edges, which this island does not have).

## Island travel (session 62)

* **Cheats panel** (F4, "travel - now on <island>"): lists `Go to hongkong` when `islands/hongkong/level/worldconfig.cpr` is installed (the other islands always). `travelToIsland` uses
  the island mod's built-in start (`ModIslandStart`, the Central quay) where there is no `EAIR` airport spot, then restarts the mode through the story handoff, exactly like Ibiza <-> Hawai.
* **Test hook** `-travelto <island>`: fires once on the 200th frame of the run. Verified headless: `-island ibiza -travelto hongkong` logs `travel ibiza -> hongkong ... (5632.0, 3.0, 7936.0)`,
  then `island 'hongkong', spawn overridden`; `-island hongkong -travelto ibiza` arrives at the Ibiza airport (16731.5, 20.3, 27506.4) with the "Ibiza Area 2" badge.
* **Map button (F7)**: with the world map open, **F7** (keyboard) travels Ibiza / Hawai -> Hong Kong when the mod is installed, and Hong Kong -> Ibiza. Verified headless:
  `-autoplay "300:press=map;500:key=F7"` logs `map island trip (F7) ibiza -> hongkong` / `hongkong -> ibiza`. No pad button (the retail pop-up has one slot).
* **Not wired** (retail pop-up): the map's airport pop-up button (`isld`) still toggles the original Ibiza / Hawai pair (the pop-up exists only on the airport spots, which Hong Kong has none of), so there is no
  map-screen button for Hong Kong yet. The trip back to Hong Kong from the map would need the pop-up's label (`m_gotohawai` / `m_gotoibiza`) extended to a third island.

## Box buildings (session 62d)

`python tools/tdusc/plm_footprints.py ...` then `python tools/mod/make_hongkong_buildings.py --userdata C:/temp/otdu2-win-userdata --tiles C:/temp/hk_tiles` extrudes the PLM footprints
(`docs/re/tdusc_pws_format.md` 2m) into box buildings: 724 `ManualBuildingPoly` + the building-like `exclusion` polygons, 25..8000 m2, on land (ground >= 0.5 m on the island's own height raster).
Result: **1,742 buildings, 87 props (one OBJ per 512 m cell, 27k triangles)** in `<userdata>/mods/props/hk_b_<i>_<j>.obj` + `props.txt` lines (`hongkong hk_b_i_j x 0 z 0`; the script replaces only its own lines).
* Heights are **[INF]** (the data has none): tower 110 m, `TOP` 30 m, school 18 m, else by area (9 / 22 / 45 m), +-25 % jitter. Base is sunk 3 m. Facade = generated 64 px window texture (3 tints) with UVs (6 m x 3.5 m per window).
* **Trap:** the props shader's no-texture path is flat white whatever `Kd` says (`obj_mesh.cpp`); a `map_Kd` texture is needed for colour.
* Visual only: props have no collision. Verified headless: `-gm worldviewer -island hongkong -campos 8280,120,5200 -camyaw 180 -campitch -8 -weather 12` shows the Central skyline (screenshot kept locally, TDUSC-derived).
* Not done: collision, real heights / parcel meshes, interiors, roads.

### Building collision (session 62d)

* `props.txt` gains a **`solid`** column: the prop's OBJ triangles (world space, X mirrored like the draw) become static car collision (`ModProps::Prop::SolidVertices/Indices`,
  `VehicleWorldCollision::addStaticTriangles`, called from `GMFreeRide::ensureModPropsLoaded`). Two-sided, friction 0.7, not road. `make_hongkong_buildings.py` writes `solid` on every building.
* Verified: log `VehicleWorldCollision: 32 / 98 / 19 ... static prop triangles added` (one line per prop), and `-gm freeride -island hongkong -tp 3192,46,3380 -logdrive` leaves the car resting at
  y = 40.98 on a roof at 40.87 instead of falling to the ground. Walls were not driven into (not tested).

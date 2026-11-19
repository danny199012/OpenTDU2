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

# Eden island — the round islet off Hawaii's SE coast, bisected into its own island

Tags: `[CODE]` readable exe / port code, `[DATA]` game files, `[INF]` inferred.

The community-named "Eden island" (after Eden Studios): a round crater islet
off the south-east tip of Hawaii (O'ahu), detached from the mainland, reachable
in retail only by glitching, present in TDU1 too. Nothing is ever sent there:
it has no road-graph nodes, no spots, no game content — but it *is* fully
modeled terrain with waterfalls, a footbridge, ferry props and a road mesh.
This spec documents what is in the data, and how `tools/mod/make_eden_island.py`
bisects it into a standalone `islands/eden/` tree in the mods overlay.

## 1. The islet in the data

Measured from `islands/hawai/level/hawai.bnk` (the minimap `hawai-c.2db` and
the island heightfield `hawai.3dg`), `area-10/area-10-7/area-10-7.bnk` (24 m
heightfield) and the sector banks:

- **Position**: world x 64,008..64,608, z 44,040..44,688 — the SE corner of
  Hawaii, area **10-7**, sectors **10-7-3-1, 10-7-3-2** (+ 10-7-4-2 sliver;
  10-7-4-1 does not exist — pure ocean has no sector bank).
- **Shape**: ~600 x 650 m, peak **109 m**; a volcano cone with a **flooded
  crater** — the interior dips to -5.4 m (below the -1.8 m Hawaii water plane)
  inside a ~73 m rim. A smaller companion islet (~340 x 170 m, max 22.7 m)
  sits 1 km south of it.
- **Painted on the retail minimap** (`hawai-c.2db`): it draws with its
  shallows, so the map screen shows it (the owner's screenshot; nothing
  reachable points there).
- **Not in the road graph**: zero `EDON` nodes and zero `ATED` curves within
  1.2 km of (64,300, 44,360) [DATA Hawai_ROAD_MAP.IA / Hawai_ROAD_DETAIL.IA].
  Its road(s) exist only as baked mesh (`TD_Road` in the sector `-o` scenes)
  + road collision (`-R.shk`).
- **LOD quirk**: the island-level heightfield (384 m grid) is **-100 (void)**
  under the whole islet — retail never LODed it in. It exists only at area
  (24 m) and sector (3 m) level. At the far-distance LOD the islet vanishes
  (retail behaviour).

### What the sectors carry

`sector-10-7-3-1.bnk` (2.9 MB) is the rich one:

| content | evidence |
|---|---|
| waterfalls | ~16 `*SCADE_` nodes with `WATERFAL` materials in `-o.3dd`; `waterfall_0.uva`, `waterfall_color_2.uva`, `waterfall_normal1_1.uva` in the bank [DATA] |
| footbridge | `SB_FBRID` node + `sb_fbrid_b.2db` / `sb_fbrid_lodc.2db` |
| ferry props | `FU_LINEA` node (`FU_L_FE` / `FU_FERRY` materials); `-O.pmi` places `FU_CATAM` x3 (catamaran) and `FU_MONO` x5 (mono hull) — **boats moored at the islet** |
| cliffs / vegetation | `R_FALB` x23, `R_FALC` x158, `R_ROCA`, `MASSIF_I`, `SB_ALAEA`... |
| terrain + grass + minimap tile | `.3dd/.3dg/.2dm/-c/-s`, `.pgr`, `.flg`, `.shk` + `-R.shk`, `.psa` |

Sector heights range -9.4..105.2 m (the `-O.pmi` payload bounds).

## 2. Verified loader facts the bisect relies on

All read from the port (`source/world/eden_world.cpp`, `source/render/3dd.cpp`,
`source/physics/vehicle_collision.cpp`), session 2026-11-11:

1. **An island is a folder name.** `islandFolder = "Euro/Bnk/islands/<name>/level/"`
   (`eden_world.cpp:573`); FREERIDE takes `-island <string>` unvalidated; the
   world viewer rejects unknown names (`gm_world_viewer.h:42` `kIslands`).
2. **Bank entries are fetched by (case-insensitive) name**, `Bank::findEntry`
   (`bank.cpp:254`) — so every entry inside a copied bank must be renamed:
   `sector-10-7-3-1.*` → `sector-0-0-3-1.*`, `area-10-7.*` → `area-0-0.*`,
   `hawai.*` → `eden.*`. The `.2db` texture header carries a name hash at +0x20
   (`GetIdentifier64bit(name)`) — patched too.
3. **Sector world positions come from the indices, not baked data**
   (`eden_world.h:14-31`): corner X = (8 ax + sx) * 768 (+ 384 when sy is odd),
   Z = (8 ay + sy) * 768. PMI instances are sector-local (quantised 1/64 m over
   [-128, 896)) and the sector `-o` objects are sector-local
   (`eden_world.h` layout comment). Renaming `10-7-3-1` → `0-0-3-1` relocates
   the whole sector content ~62 km north-west, intact.
4. **Collision is the same**: `.shk` entries are read by extension, in the
   **area-local frame**, translated by the area origin derived from the new
   indices (`vehicle_collision.cpp:304-340`). Library instances are looked up
   as `sector-<ax>-<ay>-<sx>-<sy>-v/-o/-s.pmi` (`vehicle_collision.cpp`
   `addLibraryInstances`) — renamed with the bank.
5. **The area `-o` scene hangs its ocean tiles / far objects under dummy nodes
   named `Sector-<ax>-<ay>-<sx>-<sy>`** — `loadArea` looks them up by
   `SectorName` to hide an area tile when its sector is loaded
   (`eden_world.cpp:1956-1985`). Node *names* in a 3dd are derived from the
   node hashcode (`node.Name = HashToName(node.Hashcode)`, `3dd.cpp:102`), and
   `findNode` hashes the query the same way (`3dd.cpp:170`). So the rename is
   a **hashcode patch**: every HASH entry inside the INS containers whose u64
   equals `GetIdentifier64bit("Sector-10-7-sx-sy")` becomes the hash of
   `Sector-0-0-sx-sy` (byte-in-place, sizes unchanged; `3dd.cpp:52-66`).
6. **Sea level is data-driven, not a name switch**: the ocean base level is
   `islandOcean.getBoundsMin().y` (`eden_scene_view.cpp:312` — "0 on Ibiza /
   Casino, -1.8 on Hawai" comes from the mesh). Copy Hawaii's island `-o`
   and the new island gets the -1.8 m plane for free (its crater floods at
   exactly retail depth). No code change.
7. **`worldconfig.cpr` is XTEA-CBC** with the DAT_00f763bc key
   (`core/crypto/cpr.cpp:7`): file = `[8-byte IV][ciphertext blocks]`,
   plaintext<sub>i</sub> = c<sub>i</sub> XOR XTEADecipher(c<sub>i+1</sub>).
   Encryption is the exact inverse; the reader takes `strnlen`, so zero
   padding. It is *optional* (`eden_world.cpp:576-582` falls back to 7x6 —
   wrong for a 1x1 island, so the tool writes one).
8. **Missing island files degrade**: traffic returns false on an empty road
   graph (`traffic.cpp::load`), spots.bin / cruisingzones / worldactors /
   ambience / chaseconfig all warn-and-continue. Casino runs as the smallest
   complete island, so the floor is proven.
9. **Shared banks are per-island paths**: `level/commonworld.bnk` (the library
   every PMI references, 65 MB), `scenarics/packs/worldactors.bnk` (road-sign
   actors, 2.8 MB), `fx/game_weathers.xmb` (weather presets, 956 KB) — the
   tool copies them from the source island.
10. **Heightfields are found structurally**: the loader takes the primitive
    with `NumRawVertex == 257*257` from `<name>.3dg` through the scene's
    material hash (`eden_world.cpp:1537-1568`) — the copied and patched
    island/area/sector 3dg just works renamed.

## 3. The bisect recipe (`tools/mod/make_eden_island.py`)

Reads the user's own game data, writes `<userdata>/mods/Euro/Bnk/islands/eden/`
(the overlay shadows the game tree; the game folder is never written —
`modding.md` 4). Steps, all verified against the formats above:

| output | from | transform |
|---|---|---|
| `level/worldconfig.cpr` | new | `<Area NbColumns="1" NbRows="1"/>`, XTEA-CBC encrypted |
| `level/eden.bnk` | `hawai.bnk` | entries renamed `hawai`→`eden`; **no** road IA files, no spots; `-c` colour map cropped to the source area's minimap rect and re-encoded A8R8G8B8 with a mip chain; island heightfield (`hawai.3dg`): all cells flattened to -100 (sea), then the islet cells re-sampled **from the area heightfield** so the islet finally exists at the island LOD |
| `level/area-0/area-0-0/area-0-0.bnk` | `area-10-7.bnk` | entries renamed `area-10-7`→`area-0-0`; `-o.3dd` HIE hashcodes patched `Sector-10-7-*`→`Sector-0-0-*` (fact 5) |
| `level/area-0/area-0-0/sector-0-0-3-1.bnk` (+ 3-2, 4-2) | `sector-10-7-*.bnk` | entries renamed (facts 2-4); nothing else to patch |
| `level/commonworld.bnk` | hawai | verbatim copy (65 MB) |
| `scenarics/packs/worldactors.bnk` | hawai | verbatim copy |
| `fx/game_weathers.xmb` | hawai | verbatim copy |

Deliberately absent: `_ROAD_MAP.IA` / `_ROAD_DETAIL.IA` (the islet has no
graph; the baked road still drives), `spots.bin` (no spots on the islet —
`add_spot.py` can add some later), `cruisingzones.bin`, `aiconfig.cpr`,
`chaseconfig.cpr` (warn-and-continue), `vehicules/traffic`, `sound/ambience`.

Code change on the port side: the world viewer's `kIslands` list gains
`"eden"` (`gm_world_viewer.h:42`) — everything else already treats an island
as a folder. (Sea level needs nothing, fact 6.)

## 4. After the bisect — expanding the island

The point of the exercise: the crater rim road, the waterfalls and the moored
catamarans are already there; what is missing is *reasons to go*. In order of
cost:

1. **Spots** (`tools/mod/add_spot.py`, works today): a dock spot by the ferry
   moorings, a lookout on the crater rim — icons appear on the map, discovery
   by driving near works (`map_spots_spec.md`).
2. **Props** (`<mods>/props.txt` OBJ path, works today): a small "Eden
   Studios" monument / garage — the nod to the creators the owner asked for.
3. **A real road graph**: author `EDON`/`DAOR` nodes + `ATED` curves over the
   crater rim (the parsers accept generated files, `roads_spec.md`) — GPS,
   traffic and the road-explore systems wake up.
4. **New terrain / buildings at scale**: the DEM+OSM generator plan
   (the `map_gen` discussion) applies unchanged to this 6 x 6 km canvas.

## 6. Built and verified (session 2026-11-11, build 17 bed87e8+dirty)

`tools/mod/make_eden_island.py` runs end to end (XTEA self-test first: decrypt
the source island's own worldconfig.cpr, re-encrypt with its IV, demand
byte-identical). Output tree at `<userdata>/mods/Euro/Bnk/islands/eden/`,
~81 MB total (mostly the verbatim commonworld.bnk copy). Verified headless:

- boot: `EdenWorld: eden: 1 x 1 areas (worldconfig.cpr)`, `1 areas loaded`,
  `island ocean bounds (-98304 -2 -98304) - (165888 -2 153600)` — the -2 m
  plane is the copied Hawaii water level (fact 6), flooding the crater at
  retail depth;
- sectors: 3-1 **75,042 collision triangles / 400 boxes / 370 capsules /
  523 library instances**, 3-2 4,503 triangles / 135 instances, 4-2
  13,409 / 44; the 61 pure-ocean sector banks are absent and skipped;
- the baked road works: FREERIDE at the crater shows the "Return to the
  road" prompt (`docs/images/eden_island_crater_drive.webp`);
- world viewer: Eden button + F8 (label fix), aerials
  `docs/images/eden_island_aerial.webp` (islet + moored catamaran from the
  south) and `eden_island_crater.webp` (cone, summit road, crater bridge,
  dock buildings, causeway). The minimap evidence pair:
  `eden_island_minimap.webp` (retail map, SE corner) /
  `eden_island_heightfield.webp` (the flattened area sheet's crater).

Fixed on the way — **`tools/x360/build_bank.py` wrote bank payloads
back-to-back while its table carried 16-aligned offsets**, so every entry
after the first payload of 4 mod 16 read from the wrong offset ("failed to
parse" for sector 3-1's -o/PMI/shk; the empty island-ocean bounds came from
the new 4-mod-16 `eden-c.2db` shifting everything after it in `eden.bnk`).
The writer now pads each payload to the alignment (no bytes change for
16-aligned sizes, so the byte-identical `--roundtrip` property holds —
re-verified on retail banks carrying unaligned PMI/SHK/UVA payloads:
area-3-3 and sector-10-7-3-1, 0 problems).

Also landed in the world viewer: `kIslands` gains "eden"
(`gm_world_viewer.h:42`), the island button label + F-key hint, the F-key
switch loop now covers all four islands (was hard-bound to 3), and `-nopanel`
starts with the F2 panel closed (headless screenshots).

Known cosmetics, none blocking: the zone label reads "Ibiza: Area 1" (the HUD
prefix special-case, freeride_hud.cpp), the map screen is the blurry 94x114 px
crop (spec 5), and the weather preset shipped is Hawaii's.

## 7. Open items

- The minimap crop is 94 x 114 retail pixels upscaled to 1024² — blurry on
  the map screen. A sharper map wants compositing the sector `-c` maps
  (higher resolution) or painting one.
- The island `-o` ocean tiles copied from Hawaii span the whole 67 x 55 km
  retail grid; only the part over eden's 6 x 6 km world matters, the rest
  draws (and is then hidden) harmlessly. A trimmed ocean mesh is a cleanup,
  not a requirement.
- TDU1's version of the island (the owner: "it was in TDU1 too") is not
  compared here; the retail TDU2 sectors are the source of truth.
- The `.pgr` grass grids come along untouched; they decode with the same
  open row-order question as every retail sector (`grass_spec.md` 7) — no
  worse than retail, and the islet cells rendered correctly in the port's
  retail Hawaii runs.

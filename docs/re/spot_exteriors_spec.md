# Spot exteriors: the island's buildings (dealerships, shops, houses)

Tags: [CODE] readable exe, [DATA] game files, [INF] inferred.

## 1. Data

- `islands/<island>/level/spots.bin`: `.SPT`, `SPTA`, `u32 size` at +0x14, then 0x110-byte records from +0x20
  (`u64 id`, `char bank[8]` at +0x08, `vec3 position` at +0x10, `f32` at +0x1c, `f32 heading` at +0x20, the
  category byte at +0x100). Ibiza 79 records, Hawai 144, Casino 4 [DATA].
- `islands/<island>/exterior/<bank name lowercased>.bnk`: the building itself — Scenery models,
  `Entity__Actor__Mesh` / `__Interior` in the bank frame, `Entity__Actor` at
  `entityTRS * rootLocal^-1` (spec_pool_interior 6), plus `entities.xmb`, `2dm` / `3dg` / `2db` entries.
  Every spot of this copy has its exterior bank (Ibiza 79/79, Hawai 144/144) [DATA].
- The record's position is the building's **origin**: the showroom / shop / house is centred on it. The
  footprints read from the models' bounds (logged by `SpotExterior::load`) are 40-100 m across
  (ECD_FERR 60 x 47 m, ETUN_A 43 x 42 m, ECDC_EUR 61 x 102 m) [DATA].
- Several spots may share one bank (the same building type at several places): one `SpotExterior` per bank,
  one placement per spot [INF].

## 2. Streaming (source/game/spots/spot_exteriors.{h,cpp})

`SpotExteriors` owns the spot list and the loaded banks:

- `load(island)` / `setSpots(list)` — parses spots.bin (`LoadSpotList`), resolves the exterior path.
- `tick(view, position, bEnabled)` — loads the banks whose spots are within `kLoadDistance` (1500 m) and
  drops the ones beyond `kUnloadDistance` (2000 m, hysteresis). The list is only re-evaluated when the
  position moved more than `kRetestDistance` (10 m) or the enable flag changed. Returns true when a bank was
  dropped: the view's lightmap list holds material pointers, so it is cleared and the survivors re-registered
  (and the shadow caster cache dropped) — a caller that registered lightmapped models of its own must
  re-register them (FREERIDE's house: `FirstOutro::registerLightmaps`).
- `excludeBank(path)` — a bank another owner already draws. FREERIDE excludes the house bank
  (`islands/ibiza/exterior/eph_l3di.bnk`, loaded by `FirstOutro` for C_FIRST_OUTRO): the texture cache owner
  and the lightmap entries belong to that owner, and a second load would release them from under it.
- `draw(renderer)` — every bank at its spot frames `T(position) R_y(heading)`.

## 3. Users

- **FREERIDE** (`gm_freeride.cpp`): `spotExteriors.load(island)` at enter, `tick` every frame around the car
  (or around the map camera when the 3D map view is up), drawn from the scene view's draw callback for every
  pass except `ProjectorActors` (so the buildings are lit, cast shadows and reflect). `-noexteriors` turns
  them off. The touring `launch/*.bat` starts use `-snaproad 150`: the spot position is the building's origin,
  so the car is parked on the nearest road **outside** it.
- **WORLD_VIEWER** (`gm_world_viewer.cpp`): the same streamer; the panel lists the spot list and its
  `-spot` / `-noexteriors` switches are unchanged.

## 4. Not done

- **No collision**: the buildings are drawn but the car drives through them (the exterior banks carry the
  house `.shk` collision used by FirstOutro — `findEntriesByExtension("shk")` — but the other banks' shape
  was not checked). The spawn inside a showroom is therefore drivable out of.
- **Interiors**: entering a dealership opens the native `DealerMenu` (car shop) from the prompt, not the
  retail shop interior; the shop / house interiors (`interior/<bank>.bnk`) are only used by the story house.
- **LOD**: the `_low` exterior banks (`ecd_ferr_low.bnk`, 10-34 KB) are not used; the full bank is loaded at
  every distance.
- The category byte of spots.bin (map icon / tab) is read by `MapScreen` only; the buildings are placed from
  the position + heading alone.

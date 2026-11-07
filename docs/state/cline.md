# Session handoff — `cline` (Cline workspace, branch `cline`)

**Only the Cline agent writes this file.** The other agents have their own shard
(`docs/state/main.md`, `docs/state/glm.md`), so two branches never edit the same
handoff file — that is what kept stopping the three-branch merge in
`E:\OpenTDU2-main\update-and-rebuild.bat` (`docs/STATE.md` was edited by both
sides of 32 of the last 38 merges).

* **Append the new entry at the top**, directly under the rule, newest first.
  Shape: the `## Session <date> (<ordinal>, `cline`): <one-line result>` block of
  `CLAUDE.md` §"The session-entry template" (the shape matters more than the
  length). The short `- Newest (...)` bullet form is fine for a small session.
* **Do not** edit `docs/state/main.md` or `docs/state/glm.md`, and **do not**
  append to `docs/STATE.md`: that file is the frozen history up to session 62
  (2026-11-01) plus the shared `## Where we are` block.
* Read order for a fresh agent: `CLAUDE.md` → this file → `docs/state/main.md` →
  `docs/state/glm.md` → `docs/STATE.md` for the deep history.

## Session 2026-11-03 (eighty-eighth, `cline`): world foliage verified — the V/O `.pmi` vegetation draws, and the old LOD0-only gap is closed (LOD1/2 impostors load and switch by distance)

The owner: *"can we try get world foiliage working grass, flowers etc..."*. The vegetation was
not actually broken: the V/O `.pmi` lists stream and draw with their textures, and the "bare"
look of the Ibiza spawn is the retail data (its terrain material's only detail layers are
`d1_cliff_rocks` / `d2_earth`).

### What was verified (headless, build 413)

- `-logmaterials` on a FREERIDE run at the spawn: every foliage family draws with a bound
  texture — grass `Set_Grass` (`TD_Ground`, `grassh` + `grassh_01n`) and `herbe_01`;
  flowers `V_BFlow` (`TD_VegetationSPR`, `v_bflow`, 2976 indices), `SF_PFlow_Spr`
  (`pinkflow_d`), `V_Lierre_PFlow`; bushes `V_BushA/BushC_Spr`, `V_Chen_B_H`; trees
  `V_Chen_C_Spr`, `V_Oliv_A_Spr`, `V_HPin_A/E_Spr`, trunks `V_TreeBark_B`, `V_TreeBranch_A`.
  No `NONE` slot on any of them.
- The sector carries them: `Sector-5-0-5-6-V.pmi` = 904 instances (`V_OLIV_*` 405,
  `V_CHEN_C/D/E` 352, `V_HPIN_*` 125, `V_FIG_*` 22), `-O.pmi` = 2043 (bushes, rocks, walls);
  the sector resolves 2967 instances in total (`-verbose`).
- Green-coverage A/B (Pillow, strict `g > r+12 and g > b+12`): Ibiza FREERIDE spawn 0.21 %,
  the **retail** `docs/images/retail-pause-freeride.png` 0.24 % (the same arid look), the
  port's own `docs/images/freeride.png` 0.27 %. Hawai `ETUN_G` (`-gm WORLD_VIEWER -island
  hawai -spot ETUN_G -weather 18`) **7.71 %** (the reference `world_hawai.png` is 12.2 %),
  the casino reference 9.16 %. The same pipeline renders lush vegetation where the data has it.

### What changed (the one real gap: `pmi_streaming_spec` §9 #3, LOD0 only)

- `source/render/3dd.{h,cpp}` — every INS. LOD's `pObjectHashEntry` is resolved to its OBJA
  geometry hash (`SceneNode::LodObjectHashcodes`); `ObjectHashcode` stays LOD0.
- `source/render/eden/eden_model.{h,cpp}` — `setLoadLods(true)` (the world library only)
  loads LOD1/LOD2 geometry; `Item::Lod`; `getNodeItemRange(node, lod)`; `nodeLodItems`;
  `draw()`, `drawNode()`, `drawSkinned()` and the RT BLAS creation stay LOD0-only, so cars /
  props / avatars are untouched.
- `source/world/eden_world.cpp` — `libraryModel.setLoadLods(!-nolodgeometry)`; `drawInstances`
  picks `lod = first k with d² < lod[k] · 3.160494 · F` (last level
  `F = max(SET_GLOBAL_LOD_FACTOR, 1)`, thresholds scaled by the Draw Distance setting; a
  level that did not load falls back to LOD0) and draws that level's item range.
- `README.md` — the `-nolodgeometry` A/B row; `docs/re/pmi_streaming_spec.md` §9 #3 marked
  implemented.

### Verified

- `library 2635 items (LOD0 1921, LOD1 656, LOD2 58)` (`-verbose`).
- `-logmaterials` on Hawai: `TD_Impostor` materials with bound textures (`V_Palm_B_H-I`
  `v_palm_b_h-i`, `V_Palm_C_H-I`, `V_Palm_H_H-I`, `V_Arbu_A-D_H-I`, `V_Plum_F_H-I`,
  `V_Thes_Aa/Ba_H-I`, `V_SchG_H-I`) and `TD_HardwareInstance` (`Obj_L_Rocks` `rock_low`).
- No regression: Hawai `ETUN_G` strict green 7.73 % before, 7.71 % after; the run loads the
  9 sectors in 10.7 s and logs nothing beyond the single frame-1 hitch (453 ms).
- `-vk_enable_validation_layer` was not rerun this session (the change adds buffers and a
  draw-range selection; pipeline creation is untouched) — first check for the next session.

### Next

- The Ibiza intro-house `-spot EPH_L3DI` world-viewer shot is green-free (0.06 %) because the
  spot camera faces the house / ground, not the beach grass — the flowers/grass are in the
  world's O.pmi there (the FREERIDE log draws them). A "grass and flowers" reference shot
  needs a camera on the spot's beach side; not a rendering bug.
- The owner's visual A/B: the free ride spawn vs `-nolodgeometry` (distant palms / olives
  should switch to their `TD_Impostor` quads instead of the full meshes).



The owner: *"is it possible we can start adding more of the audio work please — also traffic, horns, ocean,
rain etc... I want more ambiance in the world ... in terms of audio as well what can we do to enhance it
stuff maybe like 5.1/7.1 support ... could we maybe implement some dualsense features at all like audio to
play through the speaker maybe limited to a dialog / led to flash red & blue if chased or haptic feedback"*.

**What is in** (`docs/re/ambience_audio_spec.md` is the new reference):

1. **`source/audio/ambience_audio.{h,cpp}`** — the island beds of
   `islands/<island>/sound/ambience/ambiences.xmb`. The file's 16 `Environment` records (day / night wav
   names, `Volume`, `Pitch`) load lazily and crossfade over 2.5 s. The record is picked from the world
   [INF - the retail zone lookup is [ENC]]: `Sea` within 140 m of a probed waterline (leaving at 210 m),
   `Mountain` > 160 m over the sea plane, `Cliff` > 70 m, `City` / `Residential` from the live traffic
   count, `Plain` otherwise; a roof over the listener ducks the bed to 40 %. A **shore layer** plays the
   `Sea` record under any other bed, 0 at 320 m -> 0.55 at 25 m, so a coastal road always has waves.
   Day / night from the game clock (7 -> 20.5), a record without a night wav falls back to day.
   Client `AmbienceInternal` (bfx 10, 2-input: an island bed is not a point source).
2. **`source/audio/traffic_audio.{h,cpp}`** — the ambient cars' engines and horns from the generic car
   bank `sound/vehicles/generic_high_high.bnk` (the traffic banks are model-only): up to 10 engine loops
   on the spatial `OtherVehicles` client (bfx 32), pitch / gain from speed, distance 10 -> 150 m, Doppler
   from the car's velocity, a police car 1.25x louder; 3 horn one-shot slots. `Traffic` raises a
   `HornEvent` when a car is held up by the player (within 16 m ahead, both nearly stopped, 6-12 s
   apart) and hands over an `AudioAgent` snapshot (`appendAudioAgents`, stable `AudioId`, `countNear`).
3. **The player's horn** — `VehicleAudio::setHorn` plays the car's own `CarVSTConfig` `Horn`
   (`PanelID 7` -> `EngineFx`, the `carhorn_loop_*` sample) while `IA_CarHorn` is held.
4. **5.1 / 7.1** — the routing matrices were already used; this adds the control and the diagnosis:
   `GSAudio::setSpeakerLayout` (2 / 6 / 8 / 0 = device), `-speakers`, `speaker_layout` in `options.ini`,
   the layout in the open log line and a **warning when the mix is wider than the device** instead of a
   silent fold-down. Verified: `-speakers 6` opens `48000 Hz, 6 channels, 5.1`, `-speakers 8` `7.1`.
5. **`source/input/gamepad_output.{h,cpp}`** — `SDL_RumbleGamepad` road feel (speed x surface + engine
   load, smoothed; a traffic crash punches), `SDL_SetGamepadLED` blue / red at 1.4 Hz while
   `ChaseMode` is `Chase`, idle colour off (`-dsled r,g,b`), `-nods` / `dualsense = 0` off. A real
   DualSense was detected on this machine (`rumble yes`) and the light bar accepted the writes.
   **The pad speaker is not possible through SDL3 3.4** (the PS5 driver writes the audio report bytes but
   exposes no PCM API, and the port has no raw HID path): `GamepadOutput::playSpeaker` keeps the hook and
   logs once why it is a no-op. Adaptive triggers are likewise not in SDL3 (only Xbox trigger rumble).
6. **`FREERIDE` debug window (F1)** — the speaker layout, the bed and its shore distance, the traffic
   voice / horn counts, the pad capabilities and two live checkboxes (ambience, pad extras) that save to
   `options.ini`.

**Verified**: build clean; `-gm FREERIDE -no3d -skipcine -exitframe 1500 -logambience` logs
`bed 'Plain' (day, shore 165 m, 0 cars, ground 5 m)` -> `shore layer in at 165 m` -> `Residential` ->
`City` with **no warning**; `-ambience Sea`, `-ambience City -clock 23.00` decode `a_daysandcoast.wav` /
`a_nightcityibiza.wav`; `-noambience -notrafficaudio -nods -speakers 2` runs quiet. Not done: the police
siren (no sample in any readable bank), the retail zone volumes, the ambiences.xmb reverb presets.

## Session 2026-11-03 (eighty-sixth, `cline`): the map reopened with no icons, then "texture corruption" and a device-lost — three lifetime bugs

The owner: *"i was driving around ... i spawned at audi dealer ship drove around a little bit and
entered when i entered i bought a car and tried to use the car option but that doesn't work anyway
when i got outside i opened the map and no icons showed up at all and then got odd textures being
stamped on the map and then it hard crashed luckily the new guardrails killed it ... sometimes icons
are there and sometimes there not mostly happens if you open map some where and drive a little
reopen and the icons seem to never show back up"*. They were on **build 416 (`acaf285`)
· main**, 3840x2160, and the run log they left (`E:\OpenTDU2-main\opentdu2.log`) is the evidence
for everything below — no guessing was needed.

### The three bugs, in the order the log shows them

1. **The map's own sector streaming never dropped the renderer material caches.** `EdenWorld::unloadFar`
   frees the sector's textures and its models, and `eden_world.h` says the caller **must** call
   `EdenRenderer::clearShadowCasterCache()` — `streamWorld`, the world viewer, the intro cinematic and
   the chase all do. The map's per-frame streaming (`gm_freeride.cpp`, the `mapScreen.getZoomLevel() >= 1`
   branch) called `unloadFar` bare, so the caster / Z-pass caches kept material *copies* whose texture
   pointers had been freed. Reproduced: `-vk_enable_validation_layer` + the two-open map script gave
   **19 violations**, the first exactly after `1 sectors unloaded`:
   `vkUpdateDescriptorSets(): pDescriptorWrites[6].pImageInfo[0].imageView Invalid VkImageView`, then
   `vkCmdDrawIndexed(): ... Binding 13, variable "t0_2d" ... imageView VkImageView 0x0 that is invalid or
   has been destroyed`. On the GPU that is a descriptor pointing at recycled memory — the owner's
   "odd textures being stamped", and the GPU reset that followed: their log's last frames are
   `STALL: no frame for 5.1 s (last frame 34982, phase 'update')`, a 9.85 s update hitch and
   `Vulkan device lost (VkResult -4) in vkWaitForFences`. Fixed by guarding the call exactly like
   `streamWorld` does; the same script now reports **0** violations (166 sector unloads, 0 stalls),
   leaving only the known shutdown-time `vkDestroyDevice(): 2 leaked objects` note.
2. **The spot icons were built once per *session*, not once per *open*.** `MapScreen::open()` replays
   the root timeline (`map_open`) and `close_free` is a frame that drops `/pack`, so the `pointNNN`
   clones die with it — but `bSpotsBuilt` stayed true, and `buildSpots()` is gated on it
   (`openTicks == 45 && !bSpotsBuilt`). The owner's log has two `MapScreen: open label 'map_open'`
   (lines 1468 and 80102) and **one** `74 spot icons created`. `open()` now clears `bSpotsBuilt` so
   the clones are rebuilt on every open: the same two-open run logs `75 spot icons created` twice and
   the reopened map draws its POI tiles (`docs/images/world_map_icons_reopen_s86.png`).
3. **A texture-cache owner is keyed by bank path, and two views can hold the same path.** A shop
   interior is its own `EdenSceneView` with its own `EdenWorld`, initialised with the same island
   (`InteriorWalk::enter(spot, island)`), so it loads the *same* `commonworld.bnk`, `ibiza.bnk`,
   `weatherfx.bnk`, `worldactors.bnk`, area and sector banks as the free ride world — one cache owner,
   one set of GPU textures (`loadBankTextures` even skips the hashes already there). Leaving the
   interior then called `releaseOwner` on each of them and **destroyed textures the free ride world
   still drew with**, `ibiza.bnk` (whose `ibiza-c` bake is the flat map's island image and
   `EdenWorld::pIslandColorMap`) included. The owner's run shows the interior world streaming at
   `EdenWorld: 9 sectors loaded around (11665, 17437)` (line 2617) right after entering `ECD_AUDI`, and
   the map/corruption/crash comes after leaving it. `OwnerTextures` now carries a `References` count
   (`loadBankTextures` ++, `releaseOwner` --); the textures go with the **last** holder. Verified with
   `-verbose -interior ECD_AUDI` + back: **39** owners are now released with holders left, including
   `Euro/Bnk/islands/ibiza/level/ibiza.bnk' released, 1 holder(s) left` and
   `Euro/Bnk/fx/weatherfx.bnk' released, 3 holder(s) left` — under the old code every one of those
   destroyed the shared textures. The map opened after the interior is whole
   (`docs/images/world_map_after_interior_s86.png`).

### The island holes (tunnels / bridges) — measured, then fixed

The owner: *"it's ok if you can't reproduce i just need you to check island for holes I believe may be near
tunnels/bridges"*. `-scantunnels` now measures carve **coverage** as well as the terrain crossing: over a
carved cell the heightfield is not ground any more (`getGroundHeight` answers through `SampleCarveGround`
only), so a carved cell with no up-facing tunnel / bridge surface over it is a hole — the sheet is drawn
without depth write over nothing (see-through) and the car falls through it.

- **Bug found by the measurement: the carve-ground sampler mixed coordinate spaces.**
  `SampleCarveGround` took world x / z but compared them against the **sector-local** collision soup
  (`EdenModel::load` builds it from the node transforms; the sector is drawn with `Translation(Origin)`
  on top). The 8 m bucket key subtracted `Origin` (local) and the triangle test did not, so on any sector
  with a non-zero origin — i.e. every one — the point test matched nothing. Ibiza measured
  **10209 of 10209 carved cells uncovered**. Fixed (one `localX` / `localZ` conversion, height still
  world); the same scan then read **6153 covered / 4056 uncovered**, so ~60 % of the gaps were the sampler.
- **The remaining 4056 cells are real voids**: `PruneUncoveredCarveCells` (new, after `BuildCarveGround`
  at sector load) unmarks a carved cell that has no floor under it, so it keeps its opaque terrain — the
  pre-carve look at that cell, and ground the car can stand on. `-noprunecarve` restores the holes for the
  A/B. Ibiza after: **6153 carved cells, 0 uncovered**; with `-noprunecarve`: 10209 / 3419.
- **Reproduced before the prune, gone after**: `-tp 22803,14,22443 -heading 0` (the biggest gap cluster,
  `sector-3-3-5-5` `?????W_T`) — with `-noprunecarve` the car sits on grass at frame 40 and is **under the
  island** looking up at the sea at frame 300; on this build it stands.
- **The Ibiza gap inventory** (world x, z; `-noprunecarve` numbers, worst 8 m cluster in brackets):
  `sector-3-3-5-5` 1288/1871 (22803x22443), `sector-3-4-0-1` 473/1187 (19260x25740),
  `sector-3-3-3-4` 447/1391 (21228x22131 — the session-42 underpass), `sector-3-3-5-4` 261/728
  (22875x22227), `sector-3-4-0-2` 247/744 (18876x26460), `sector-3-0-3-7` **210/210** (21156x5388),
  `sector-3-3-4-7` 171/888 (21915x23811), `sector-3-4-1-0` 93/498, `sector-1-2-6-5` **83/96** (11139x16131
  — the `BP_PONTA` bridge ~1.1 km from `ESTOL_B`, the used-car dealer), `sector-3-3-3-5` 101/2013,
  `sector-3-0-2-7` 45/50, `sector-3-3-4-5` 0 (the only fully covered one).
- **Trade-off, for the owner's eye**: at `sector-3-0-3-7` the carve is now a no-op (carve-on vs
  `-nocarve` differ by mean 0.000, 0 px > 24) — those cells had no floor at all, so the hill stays solid
  instead of showing a hole. A bore whose floor lives in a *neighbouring* sector (the footprints span
  300-400 m) or whose road triangles the up-normal test rejects would read as plugged rather than open;
  `-noprunecarve` is the A/B if a tunnel mouth looks wrong.

### Ruled out (with the test that ruled it out)

- **The purchase / `switchCar` model swap** — not it *this time*: the owner's log shows the default
  delivery path (`FREERIDE: delivering 'Q7_V12' to 'PosCarParking' ...` then `car at the delivery
  position (the model change needs -switchcar)`), so no runtime car model change happened. That path's
  device wedge (`gm_freeride.cpp`'s KNOWN ISSUE comment) is still open and is the reason "Get into the
  car" cannot hand the car over — see below.
- **VRAM** — `vram: 2442 MB used / 52000 MB budget` in every perf line of their run; the map never
  approached the budget.

### Owner items that are *not* fixed here

- **"i bought a car and tried to use the car option but that doesn't work"** — the dealer entry
  *Get into the car* (`DealerMenu::eEntry::EnterCar`) only steps to the podium camera (`eStep::View`);
  it never puts you in the car, and `deliverBoughtCar` → `switchCar` deliberately does **not** change
  the driven car (`-switchcar` is opt-in) because replacing it at runtime wedges the device. Retail's
  behaviour needs that wedge solved first; the option should not pretend otherwise until it can.
- **"holes in the geometry ... i thought it was transparent i fell through"** — measured and fixed in this
  session, see *The island holes* above (`-noprunecarve` is the A/B). The remaining question is visual,
  not structural: whether a bore whose floor lives in a neighbouring sector now reads as plugged.

**Note on how this session ended**: the image reader died part-way through (every file, including
shrunken crops and JPEGs, came back "invalid or exceeds size limit" for files that had read fine an hour
earlier — the owner has the same symptom on Cline Pass and OpenRouter, so it is a supplier-side image
cap, not the model). The tunnel work was finished on numbers instead, and the rule is now written down
for every agent: `CLAUDE.md` §8 trap 14 and `CLINE.md` "When vision fails" — park the frame, finish on
counts/diffs/logs, name the gap, do not guess.


## Session 2026-11-03 (eighty-fifth, `cline`): the brake-disc glow is a rotor ring again, and the rotor + caliper sit on the wheel

The owner, with a close-up of a glowing California wheel: *"try fix the heat emission from brake discs
but also the brake caliper looks wrong it doesn't look accurate"*.

**Reproduced** — `-gm FREERIDE -car California -skipcine -nopad -fixedtimestep -night
-brakeheattest 100 -autoplay "8:look=1,0;45:shot=..." -exitframe 50`: the orange filled the whole
barrel with a ~27 px checkerboard, spilled over the tyre sidewall and into the arch, and the caliper
read as a flat grey slab because the glow was drawn *in front of* it. The caliper and the model's own
rotor were also 0.120 m under the wheel centre (see below).

### What changed

- **`resources/shaders/brakedisc.frag`** (regenerated header) — the face is an **annulus**: the
  friction ring the pads sweep (`0.40 .. 0.965` of the radius) glows, the hat stays at 10 %, so the
  disc reads as a ring with a dark bell instead of a flat orange plate. The `16 x 16` hash grain (the
  checkerboard in the owner's close-up) is gone: a few percent of angular streak plus a 160-cell
  speckle. The spill shape is an annulus too, not a `(1-r2)^2` ball.
- **`source/render/eden/eden_surface_fx.cpp`** — the spill quad moved **into the wheel plane**
  (2.0 cm outboard of the hub, `0.95 x radius`, alpha 0.30). It used to be a 0.62 m billboard
  `0.9 x radius` = **19 cm outboard of the hub**, i.e. in front of the spokes and the tyre; that is
  what tinted the whole wheel and the caliper orange.
- **`source/game/cinematic/cinematic_car.cpp`** — the wheel lift to the physics tyre radius
  (`D_RIM_xx` + tyre) now takes the corner's **brake assembly** with it: `BRAKE_<corner>` (the rotor,
  a sibling of the rim under `DISC_xx`) and `STIR_<corner>` (the caliper, beside `DISC_xx` under the
  upright), matched by exact name. On the California the whole brake used to hang 0.120 m under the
  wheel centre, at the bottom of the rim, while the glow quad was drawn at the physics hub — the
  caliper was never concentric with the disc it was supposed to straddle.
- Docs: `docs/re/surface_effects_spec.md` 7.3/7.4, `PROGRESS.md`, this entry, and the A/B screenshot
  `docs/images/surface_fx_brake_glow_before_after.png` (same camera, pre-fix binary vs now).

### Verified

- The A/B at the same camera: wheel region mean **11.24**, **3,716 px > 24** against the pre-fix
  binary; the tyre, the arch and the spokes are no longer orange (`surface_fx_brake_glow_before_after.png`).
- The glow still draws: against `-nobrakeheat`, wheel region mean 5.02, **1,206 px > 24** (the ring
  only — the old effect changed 5,302 px because it flooded the wheel).
- `CinematicCar: brake node 'BRAKE_FL' lifted 0.120 m with the wheel 'D_RIM_FL'` — 8 lines, the four
  rotors and the four calipers, same lift as the rim.
- `-logbrakeheat` untouched: the heat law and its numbers are unchanged (no code touched).

### Ruled out (with the test that ruled it out)

- **"The caliper is the wrong colour — the paint row's `_callipers` is not applied."** No:
  `-logpaint` on the dealer preview logs `CarPaint: California: 23 row(s), ... callipers ''` (the 16M
  row is `'##'`), and `california.bnk`'s `california_color.2dm` has no `Cal_*` material at all, so
  `FUN_00c5bb40` would change nothing here. The grey caliper is the model's own material, not a
  missing paint. (`applyDatabasePaint` still does not write `_callipers` for the cars that do have
  one — see next.)
- **"The glow is drawn additively."** No, it has been blended since s79; the flood was the billboard's
  placement and alpha, not the blend mode.

### Not done / next

- **The caliper does not steer with the front wheels.** `STIR_xx` is a sibling of `DISC_xx` under the
  upright, while the port steers `D_RIM_xx` only (`GMFreeRide::updateWheels`), so a held lock leaves
  the caliper straight. The node's world basis is mirrored on the right-hand corners (the `FORK_xx`
  basis is `-x` there and `D_RIM_xx` compensates with a `-z` scale, which `STIR_xx` does not), so the
  steer angle needs `sign(det(D_RIM) * det(STIR))` — start at `gm_freeride.cpp` `setupVehicle`.
- `_callipers` is still only logged: `CinematicCar::applyDatabasePaint` paints `Paint*` /
  `Paint2*` and stops. Cars with a real id (the Brera's `Cal_G_R`) keep the model's caliper colour.
- The discs do not light the road (no projector/point light) — unchanged.

## Session 2026-11-03 (eighty-fourth, `cline`): the caravan report — the entry shot plays once, the island can be streamed around a living room, and the outdoor garage walk is parked with its cause named

The owner: *"the caravan is in it's 1st house you get given but when inside the exterior
didn't seem to be loaded and can't walk to the garage outside from it also everytime you
enter it seems to replay the caravan scene it's ok if this can't be fixed due to lack of
visión however"* — three symptoms, three different causes (`docs/re/houses_spec.md` 9).

### What changed (build 408)

- **`source/game/spots/interior_walk.{h,cpp}`** — the house entry cinematic played on
  **every** room load (`scene.playMain( introName )`); it now plays once per house and
  profile. Key = the room bank's name (`lph_l0c` / `gph_l0c`), kept in
  `PlayerProfile::SeenHouseIntros` and saved as `house_intros_seen` (`player_profile.{h,cpp}`).
  `-replayhouseintro` forces one, `-nointeriorintro` still disables all, shop interiors keep
  replaying theirs (unchanged). `enterHouse` also gained the **island** parameter and, behind
  the new opt-in **`-houseworld`**, streams the island around the living room — the house
  path never passed an island while the shop path always did, which is exactly why the
  exterior was missing inside.
- **`source/game/freeride/gm_freeride.cpp`** — the caller passes `islandName`.
- Docs: `docs/re/houses_spec.md` 9.1-9.3, PROGRESS.md (two new rows + the caravan row),
  README flag table.

### Verified (build 408 `build 408 (e2e047d+dirty) · cline`)

- In and out of the caravan twice (`-storystep 10 -home -fixedtimestep`, validate / back /
  validate): first entry `InteriorWalk: 'EPH_L0C_1254I' (interior/lph_l0c.bnk): cinematics …
  C_INTRO` + `Profile: saved … (house intro seen)`; second entry
  `InteriorWalk: 'lph_l0c': 'C_FIRST_INTRO_I' already played - straight to the walk
  (-replayhouseintro replays it)`. `profile.sav` carries `house_intros_seen = lph_l0c`.
- The caravan's entry shot is **`C_FIRST_INTRO_I`** (the bank also has `C_INTRO`; the name
  scan takes the first match) — the log line names it, so this is not a guess.
- `-houseworld`: `InteriorWalk: house 'EPH_L0C_1254I' -> living room, island 'ibiza' (world
  streamed around the room)` and the house's sector banks/textures load
  (`sector-1-2-5-5`, `sector-1-2-6-5` …) instead of `island '' (closed room: no world -
  try -houseworld)`.
- Living-room selectables logged (they answer the third symptom): `ExitDoor` type 0,
  `Media` 57 `td_media`, `Garage` type 5 at (-10.87, 1.16, -1.71), `Diary` 9, `Dressing` 10.

### Ruled out (with the test that ruled it out)

- **Not a missing asset**: `interior/lph_l0c.bnk` is complete — 204 entries, 49 entities in
  10 class arrays, 3 entity cameras (`cam_entree_garage` at (-6.40, 1.60, -1.56)), collision
  `lph_l0c_sc_…shk: 39 triangle(s), 33 box(es), 5 capsule(s), 40 body(ies)`, 4 models / 66
  meshes / 31,274 verts (ASSET_VIEWER). The exterior bank draws too (s82).
- **Not the spot grant**: step 10 owns the caravan (s83) and `-home` spawns at its entrance;
  the enter tile and the walk both work.
- **The garage is not "outside" in the data**: it is `gph_l0c`, a separate room bank loaded
  by the living room's `Garage` volume; the caravan plot has no walkable outdoor surface in
  the port (walk collision = the loaded bank's `.shk` only).

### Not done / next

- **9.2 needs the owner's eyes** (the one thing this session cannot verify headlessly):
  run `-storystep 10 -home -houseworld` and say whether the view out of the caravan's
  windows is right. If yes, make `-houseworld` the default for living rooms (one line in
  `InteriorWalk::enterHouse`).
- **9.3 is a feature, not a fix**: walking out to the garage needs walk-capable world
  collision (`WalkMode` has only the bank `.shk`; the island has `VehicleWorldCollision`).
  First cheap check: whether `lph_l0c`'s own `.shk` already covers a patio (33 boxes) — if
  it does, the plot could be added to the walkable set without new collision code.

## Session 2026-11-03 (eighty-third, `cline`): story step 10 hands the caravan over as the home house, step 9 routes you there, and `-storystep` reaches 0..13

The owner: *"Based on the progress.md I want you to select a few things you want to work on please and will need you to work on them autonomous if possible if vision starts to become a issue document and park it and move on to something else."* The two owner reports stay where session 82 left them (both need the owner's build id / a location, so there is nothing to test headlessly yet); this session takes the board's own next step - **"step 10 must grant the caravan as home"** - and makes it a playable beat.

### What changed

- **`source/game/house_shop.{h,cpp}`** - `GrantHouse( spotName, bSetHome )`: the story's own house grant (no sale, no realtor stock needed). Adds the DBHouse `_spotname` to `PlayerProfile::OwnedHouses`, keeps `NbHouses` in step, optionally sets `HomeHouse`, logs the grant, and returns true only when the profile actually changed (so the caller knows a save is needed). Idempotent, so it is safe to call on every entry.
- **`source/game/freeride/gm_freeride.cpp`** - `ApplyStoryGrants()`: at `StoryStep >= 10` it grants `EPH_L0C_1254I` ("Caravan", 2 slots) as the home house and saves (`story step 10: the caravan as home`). Called from `enter()` (a save already past step 10 gets it) and right after the cheat panel sets a step. `startDriving()` adds the **step 9 "Go Home"** beat: the goal text is the step's own name and the GPS target is the caravan's own `FindSpotEntrance` point; the tick's `takeArrival()` then advances 9 -> 10, grants, and saves.
- **`source/tdu_instance.cpp`** - `-storystep` now maps 0..13 (the retail step table of `docs/re/tess_garage_spec.md` 1.2). 9..13 have no ported cinematic/mission yet, so they start in FREERIDE and log the step's own name (12/13 add "pass -island hawai") instead of the old `not implemented (0..8)` warning.

### Verified

- `build-cline.ps1` exit 0.
- **Step 10 grant, from a save already at step 10** (`-storystep 10 -home`): `House: granted 'Caravan' (spot 'EPH_L0C_1254I', 2 slot(s)) as the home house, 2 house(s)` -> `Profile: saved ... (story step 10: the caravan as home)` -> `FREERIDE: -home 'EPH_L0C_1254I', entrance at (10513.7, 7.4, 15743.2)`; the saved profile then carries `houses_owned = EPH_L3DI_5067I,EPH_L0C_1254I` and `home_house = EPH_L0C_1254I`.
- **Step 9 -> 10, the whole beat** (`-storystep 9 -tp 10513.7,7.4,15743.2 -gpsdrive 10513.7,15743.2,25` - the virtual car drives the route): `FREERIDE: story step 9 'Go Home': GPS to the caravan (10514, 15743), route 55 m` -> `FREERIDE: -gpsdrive to 10514,15743 at 25.0 m/s, route 55 m` -> `House: granted 'Caravan' ... as the home house` -> `Profile: saved ... (story step 10: the caravan as home)` -> `FREERIDE: story step 9 -> 10 'Home sweet home : Caravan', the caravan is your home`; `stopped after 7.1 s: 1600 beats, 0 stalls`. The saved profile: `story_step = 10`, `houses = 2`, `houses_owned = EPH_L3DI_5067I,EPH_L0C_1254I`, `home_house = EPH_L0C_1254I`.
- **Step table boundary** (`-storystep 13 -island hawai`): `-storystep 13 'Igloo First Intro': no ported cinematic/mission yet, starting in FREERIDE (pass -island hawai)` then the Hawai world loads; 560 beats, 0 stalls.

### Ruled out (with the test that ruled it out)

- **The grant is not lost on exit.** The first version granted in memory only (nothing triggered `SaveProfile`) and the saved profile still had `home_house = EPH_L3DI_5067I`; `GrantHouse`'s "profile changed" return + the explicit `SaveProfile` fixed it - the re-run's save carries the caravan.
- **`-tp` at the caravan entrance does not arrive on its own**: `setTarget` snaps to the nearest *road node*, 55 m of route away, so the 40 m arrival never fires. The `-gpsdrive` virtual car is what closes it (and is the cheap way to test any arrival beat headlessly).

### Not done / next

- **Steps 4-8 and 11-13 have no mission**: the next story work is step 4 "Driving Tess" (the `TESSEVEN` mission) or the retail presentation of step 9 (a phone call + goal, not decoded).
- **Remaining shop menus** are now named precisely in `PROGRESS.md`: surgery has `docs/re/surgery_spec.md` + `DBSurgeryCost` (the biggest; phase 1 = the native menu), club (`ECH_*`/`ENC_*`) and airport (`EAIR_*`) have no spec - the airport would need a runtime island switch (the port only reads `-island` at startup).
- **The two owner reports are still `Open`**, blocked on the same two things: the build id (map-credits look-back flicker) and a location/spot (graffiti). Nothing new to test headlessly; the `-logmapzoom` diagnostic and the LOD-alias scan stay where session 82 left them.

---



The owner: *"we may need to watermark the runs with build ids that way we can see what build it was
caught and how then we test it again in newer build to see if fixed ... I think we may need to come up
with a rules.md moving forward to improve work efficiency so everyone is always synced and work that is
WIP needs to be flagged ... a progress status on the github ... it would show us whats done, needs work
not implemented or started."* This session builds that workflow. The two open reports (map-credits
look-back flicker, graffiti on buildings) stay **Open** — see "Not done / next" for the exact state.

### What changed

- **`source/debug/build_stamp.{h,cpp}` (new)** — a 5x7 bitmap stamp burnt into every saved
  test/debug screenshot (PNG and PPM) at the bottom-right: `BUILD 406 5C3E76D+DIRTY CLINE`.
  Independent of the UI watermark and of any graphics setting; skipped by `-nowatermark` and for the
  photo camera's player photos (`saveScreenshot`, the "Take photo" feature).
- **`cmake/StampBuildInfo.cmake`** — the stamp now carries the branch:
  `OTDU_BUILD_BRANCH` + `OTDU_BUILD_FULL_STRING` = `build N (hash[+dirty]) · branch`. Consumed by
  the banner, window title, watermark, screenshot log line and `check-build.ps1`.
- **`source/debug/watermark_overlay.{h,cpp}`** — the build id is drawn top-right **always**
  (`-tester <name>` only adds the player name; `-nowatermark` hides the overlay for A/B runs). One
  `Run: build … · branch (name built <timestamp>)` line is logged every run.
- **`source/tdu_instance.cpp`** — the window title always ends with the build stamp.
- **`source/render/vulkan/render_device.cpp`** — the screenshot path expands `{build}` / `{hash}` /
  `{branch}`, the stamp is burnt in, and the saved line logs the build id next to the path.
- **`tools/launcher/launcher_ui.cpp`** — the watermark switch is relabelled: it adds the player name;
  the build id cannot be turned off. Help text updated.
- **`RULES.md` (new)** — the workflow contract: sync before work, build id in every report, WIP
  flagged, session start/end checklists, merge order, the status-board law, the bug-report shape.
- **`PROGRESS.md` (new)** — the status board (Done / WIP / Partial / Not started / Blocked / Open)
  with owner, evidence and next step per area, plus the WIP block every agent reads first.
- **`tools_probe/status_report.py` (new)** — reports/validates `PROGRESS.md`; `--check` fails on a
  WIP row without owner or next step, or an illegal status; `--summary` feeds the Actions page.
- **`.github/workflows/status.yml` (new)** — publishes the board to the Actions summary and runs the
  check on every push/PR. **`.github/ISSUE_TEMPLATE/{bug_report,feature_request}.yml` (new)** —
  the bug form *requires* the build id. **`.github/pull_request_template.md` (new)** — the
  sync/status/evidence checklist.
- **`check-build.ps1`** — now also prints the exe's build stamp (read from
  `<build>/generated/build_info.h`), the sync state (`behind/ahead origin/<branch>`, `behind
  origin/main`), the last session entry and the current WIP block; `-RequireSynced` makes the
  session-start gate fail when behind.
- **`CLAUDE.md` / `CLINE.md` / `WORKSPACES.md` / `README.md`** — read order and references updated
  (RULES.md + PROGRESS.md first), the workflow section and the new CLI behaviour documented.
- **`source/game/map_screen.cpp`** — `-logmapzoom` diagnostic kept from the flicker investigation
  (prints transition / zoom level / wheel / trigger values every 0.5 s). It pinned a test-timing
  fact: a zoom input before the map's 1 s open transition is ignored, which is why the first headless
  credits runs never opened them.

### Verified

- **Build**: `build-cline.ps1` exit 0 (build 406). `check-build.ps1`:
  `build stamp: build 406 (5c3e76d+dirty) · cline`, `0 behind, 0 ahead of origin/cline`,
  last session entry and the 3 WIP rows.
- **Stamp in the image**: `-gm WORLD_VIEWER … -autoplay "120:shot=…/_stamp-{build}.ppm"` ->
  `_stamp-406.ppm`, log `Screenshot saved to '…/_stamp-406.ppm' [build 406 (5c3e76d+dirty) · cline]`.
  Diff against the same frame with `-nowatermark`: the bottom-right box appears, and an ASCII view
  of the region reads `BUILD 406 5C3E76D+DIRTY CLINE`. The PNG path writes through the same code
  (`_stamp.png` saved, same log line).
- **Run line**: `Run: build 406 (5c3e76d+dirty) · cline (no tester name built Nov  3 2026 03:12:45)`;
  with `-nowatermark` the same line ends `(watermark off)`.
- **Board**: `python tools_probe/status_report.py --check` -> `OK: every WIP row has an owner and a
  next step; every status is legal.` (19 Done / 4 Partial / 2 WIP / 4 Not started after this entry).
- **Caravan (the owner's third ask earlier in the session)**: `EPH_L0C_1254I` (DBHouse row 50,
  "Caravan", $28k, level 0, 2 slots, realtor `ERE_B_1264I` which *is* one of Ibiza's two `ERE_B`
  spots). Its exterior bank `eph_l0c.bnk` draws (`SpotExterior: 4 set models, 4 props, local bounds
  (-21.4,-0.4,-9.9)-(12.6,5.0,9.0)`, `SpotExteriors: EPH_L0C at (10490,15750): 8 models`) — the
  silver trailer is visible in the day shot. With the house owned, the living room `lph_l0c` lists
  `Garage` type 5, pressing A loads `gph_l0c` and places the player's cars (Charger SRT8, Viper
  SRT10, Mustang Fastback on D_CAR_01..03); `-housegarage` goes straight there. So the exterior and
  the garage work on this branch; what does **not** exist is the story grant (step 10 "Home sweet
  home: Caravan") and the purchase flow was not re-tested end to end this session.

### Ruled out (with the test that ruled it out)

- **Map-credits flicker is not reproduced by the credits themselves on build 406**: 640x360
  `-fixedtimestep`, map -> wheel zoom-out at level 0 -> credits open with the words visible at frame
  560 (`_words2.ppm`, the scroll text at the bottom) -> Back -> Back -> drive with `hold=accel` +
  `hold=backview`; 241 consecutive frames captured across the close (620..860) plus 120 after
  (800..919): the minimum mean luminance after the map is 97.7 (`-meankan`), no black frame, no
  VUIDs. A `-vk_enable_validation_layer` run of the same script was still in progress at write time.
- **The credits need ~3 s before the first text is on screen** (`creditsY 860 -> 720` at 45 px/s),
  so a "words came up" repro needs the longer run above.
- **A too-early zoom input is not a bug**: `-logmapzoom` shows `transition < 1` swallows the wheel /
  trigger until the 1 s open transition completes. The first two headless credits runs failed for
  this reason, not because the map is broken.
- **The graffiti is not the broken LOD alias rule on the caravan**: `eph_l0c.bnk` logs one alias
  (`lm_gph_l0c_lod -> lm_eph_l0c`), but the same world-viewer frame with `-uselodtextures` is
  pixel-identical (0 px > 8) — the aliased texture is not on a visible surface there. A broader scan
  (`_lod_alias_scan.py`) shows 67 aliases in the exterior/interior banks, many with a 3-4 char shared
  prefix, so the rule is still suspect for other buildings; needs the owner's location.

### Not done / next

- **Ask the owner for the build id on both reports** (the new watermark makes it visible in the
  screenshot). Then:
  - flicker: reproduce with a real pad (L1 is `IA_CarBackView`) and the credits words up; A/B
    `-noblur`, `-noshadow`, `-nort`, and add a one-shot log of the first frame whose mean luminance
    is ~0 (the screenshot path is too slow at 60 Hz to catch every frame). Candidate leads:
    `ChaseCamera::update` ground clamp using the heightfield (`getHeight`) while the look-back eye
    leads the car, and the map's sector unload/reload around `mapScreen.close()`.
  - graffiti: a spot/coordinate; then `-uselodtextures` A/B and `-verbose` ("texture … borrows
    from …") at that location. Next code lead: `TextureCache::findForOwner`'s global hash fallback
    can bind a same-named texture from another bank.
- **Story step 10** should grant `EPH_L0C_1254I` as the home (and `HomeHouse`), which makes the
  caravan reachable in a normal playthrough without the realtor; the purchase flow at `ERE_B_1264I`
  should be re-tested (walk to `Realtor_area`, buy, then enter the living room and the garage).
- **Merge this branch** so main gets the watermark/RULES/PROGRESS; the new `status.yml` runs on
  main only after the merge.



## Session 2026-11-03 (eighty-first, `cline`): the owner's main tree gets unstuck - the origin/main integration resolved and pushed, and the teleporter gets a destinations row

The owner: *"in -main I used the update and rebuild but it looks like i'm missing some stuff. also
could we add for teleporter cheats a few destinations in the menu?"*

### What was wrong (the "missing stuff")
- **`E:\OpenTDU2-main` was stuck mid-merge, not stale.** Its `update-and-rebuild.bat` had fetched
  origin/cline (`d65fca8`) and stopped at **step [4/5]** with `.git\MERGE_HEAD` present - 20 conflicts
  in 7 files: `.github/workflows/windows.yml`, `freeride_hud.{h,cpp}`, `gm_freeride.cpp`,
  `eden_scene_view.h`, `eden_surface_fx.{h,cpp}`. The rebuild step never ran, so the exe there still
  predates all six cline commits after `865a955` (the casino interior included: it was uncommitted
  here until now).
- **Cause: origin/cline did not contain origin/main.** origin/main is 41 commits ahead of the last
  cline merge base, so the "merge origin/main, then origin/glm, then origin/cline" order produced a
  main-vs-cline conflict on exactly the files both lines worked on (the glm satnav/puddles/rain versus
  the cline HUD/perf/brake work).
- **Fix: the integration is done in the cline tree and pushed.** `git merge origin/main` here, the
  seven files resolved, then `push origin cline` (`5d553f1`). origin/cline is now a descendant of
  origin/main *and* origin/glm, so the owner's next `update-and-rebuild.bat` aborts its stuck merge
  and merges cline cleanly; its only own commit on top of `e046580` is the progress-tracking one.

### How the merge was resolved
- `freeride_hud.{h,cpp}`: **main's retail satnav wins** (the map renders into
  `/HUD/minimap/mmap_lure` under the movie, the movie's own cursor + GPS readout, the near-car spec
  card, the popup auto-close fix) and the cline pieces that main lacks are kept (`HudQuality <= 1`
  simple-HUD gate, the enter-card depths, `-dumphud`). `ClipToRect` (Sutherland-Hodgman) restored:
  the cline perf rewrite had deleted it and main's minimap still needs it.
- `gm_freeride.cpp`: the cline **arrest shot** and main's **`-cameramode` / windscreen droplets** both
  wired (the camera block needed its closing brace); the puddle height query and `-nobrakeheat` both
  on; `setBrakeGlowEnabled` next to `setPuddlesEnabled` / `setHeightQuery`.
- `eden_surface_fx.{h,cpp}`: the brake disc glow keeps its pipeline, now created **per render pass**
  beside particle/decal (main's `VUID-vkCmdDraw-renderPass-02684` fix) - `PassPipelines.brakeGlow`,
  destroyed with the map; puddles + light pools kept; the stats line reports particles, decals,
  puddles and the hottest disc.
- `eden_scene_view.h`: the cline `LightmapMaterial` owner table and main's `EdenRain` / `EdenWindscreen`
  members in one block.
- `.github/workflows/windows.yml`: main's clang-cl + Ninja workflow (the cline side was the older
  Visual Studio one).
- **A trap worth knowing:** splicing the file with PowerShell (`Get-Content` / `Set-Content`) is
  lossy on this tree - the non-ASCII comments came back double-encoded (`—` as three characters).
  They are repaired in the follow-up commit; use the editor tool or Python for a whole-file splice,
  never `Get-Content | Set-Content`.

### The teleporter destinations
- **`source/game/cheats.{h,cpp}`**: a **destinations row** above the spot list, one button per
  landmark of the island's own `spots.bin`: **Airport** (`EAIR_*`), **Casino** (`EGAMHA`),
  **Ferrari dealer** (`ECD_FERR`), **Any dealer** (`ECD_*`), **Tuning shop** (`ETUN_A`),
  **Car wash** (`ECW_A`), **Race school** (`ERS_A`), **Clothes shop** (`EAS_A`).
  `Cheats::setSpots` resolves every prefix against the island's spots and silently skips one the
  island lacks, so the row is the same on Ibiza (79 spots, 8 destinations) and Hawai (144, 8);
  `teleportToSpot` is now the one path the row, the list (click / double click) and the
  "Teleport to the spot" button use.
- **`-tpdestination <name>`**: the same button from the command line (exact name first, then the
  first name containing it), resolved on the first `setSpots`; it is how a headless run proves the
  row lands on a real place.

### Verified
- `build-cline.ps1` exit 0 (after one `-Reconfigure`: the merge brought new source files, the CMake
  glob check fired).
- Headless FREERIDE, 400 frames: exit 0, **0 stalls**, `EdenSurfaceFx: ready`, `SurfaceFx: first
  light pool drawn (1 front, 0 rear cones)`, 79 spots.
- `-tpdestination Airport` -> `Cheats: -tpdestination 'Airport' -> EAIR_A (16780, 27512)` then
  `FREERIDE: teleport to (16780.2, 20.9, 27512.4), heading -110 deg`; on Hawai
  `-tpdestination Casino` -> `EGAMHA (23449, 43355)`.
- The panel screenshot shows the row (Airport / Casino / Ferrari dealer / Any dealer, Tuning shop /
  Car wash / Race school / Clothes shop) - scratch `_tp_panel.ppm`, not committed.

---

## Session 2026-11-03 (eightieth, `cline`): the casino interior - the hall, the slots, the roulette room, and the Audi on the car machine, all offline

The owner: *"can we look into having the casino interior implemented and make sure the slots and the
roulette and other stuff have there true interior and assets also textures ... I want it to have more
offline functionality ... buying and selling chips off line, buying drinks offline, slots I want offline
also and chance to win the Audi car offline (from slots) also the clothes win (on slots) the store and
wardrobe need hooking up"*.

### What changed
- **The retail casino is a set of interior banks, and they are now walked**: `icashal.bnk` (the hall,
  50 MB), `icasslo1.bnk` (38 slot machines, 55 MB), `icasrou1.bnk` (5 roulette tables),
  `icaspok1.bnk` (5 poker tables), `icasvip.bnk`, `icasdr.bnk` (dressing) and `ishop_ca.bnk` (the
  boutique). Each has its own `entities.xmb` with the doors / machines / tables as data.
- **`source/game/casino/casino_interior.{h,cpp}`** (new) - the room machine: loads a room as a closed
  interior (`IntroCinematic` + `WalkMode`), plays its `C_LIFE*` background cinematics, injects the walk
  selectables (the machines use their retail `SelectableZone` volume matrices, the roulette / poker
  tables a 1.6 m box, the hall's cashier / bar a 1.2 m box) and reports the retail
  `SelectableString` vocabulary (`cslo`, `crou`, `cpok`, `sitgo`, `cvip`, `shop`, `avat`, `outdoor`,
  `chal`, `sm_car` ...).
- **`GMCasino`** - a new interior-first floor: the hall's doors walk between the rooms, the machines /
  tables open the native games with their own type, the cashier and the bar have screens, the boutique
  opens `ClothesMenu` (brand 0x57 CASINO) and the dressing room the wardrobe. `-nocasinointerior`
  keeps the old native floor list; `-casinoroom` / `-casinoselect` / `-casinoprize` are test hooks.
- **The six retail slot machines** (`CasinoSlotMachineTypeDesc`): Car (100 a spin, 1 line, 6 symbols),
  Clothes (10 / 1 / 4), Poker (5 / 1 / 52 cards), Hawai (15 / 3 / 8), Classic (2 / 3 / 8), HighLife
  (10 / 9 / 8). Wager = `Price x bet level`, each line pays `paytable x bet x Price / lines`, so the
  96.16% paytable holds on every machine (measured 97.17% on 50 000 spins).
- **The prizes**: 3 of the car machine's top symbol pays the **Audi R8 V10** (`DBRewardCar CASINO_CAR_1`,
  car 432) into the garage + the `Car` unlockable, then 250 000 chips; the clothes machine pays the
  first casino outfit the player does not own + `cloth_set_won`. Verified end to end (`cars = 2`,
  `casino_car_won = 1` in `profile.sav`; the clothes run logged `Trousers` id 580).
- **Offline wallet loop**: the cashier buys 1 000 / 10 000 / 100 000 chips and sells 1 000 / all;
  the bar sells the 25 `DBCasinoUnlockable` cocktails for $25 (`PlayerProfile::CasinoDrinks`, new save
  key `casino_drinks`); roulette tables take their `CasinoRouletteTypeDesc` minimum.
- **The 2D layer after the walk movie**: `INTRO.vmf` leaves the 2D canvas at its own size and the
  clip / stencil state set, so the native header vanished over the interior - `GMCasino::draw` now
  calls `clearClipRect` / `setStencil(SM_None)` / `begin(1280, 720)` after the scene draws.
- **Docs**: `docs/re/casino_spec.md` §9 (the room table, the six machines, the prizes, the cashier /
  bar / boutique / wardrobe and what still needs the server).

### Verified
- `-gm casino` renders the hall (screenshot `_hall4.png`), `-casinoroom slots` the slots room with its
  machine walls (`_casino_slots.png`), `-casinoroom roulette -casinoselect 2` the roulette room with
  the table's felt over it and `CasinoRoulette: table minimum 25, chip 25`.
- `-casinoselect 1` in the slots room opened `SlotMachineClothes` (10 chips a spin, 1 line, 4 symbols).
- `-slotselftest 50000`: 97.17% measured against 96.16% analytic, wallet exact.
- `-casinoprize car`: car 432 in `cars =`, `casino_car_won = 1`, saved. `-casinoprize clothes`: `Trousers`.

### What is next
- The Sit & Go poker room offline (AI opponents, the `sitgo` door already points at it).
- The wardrobe's avatar mesh swap (the list and the ownership are wired).
- The VIP prizes (`CASINO_CAR_VIP`) and an offline route to the `DBClubRewardCars` club cars
  (championship with an entry fee, hard but beatable AI) - the owner's ask, not started.

---



The owner: *"can we implement the feature while driving and braking hard
causes tire marks and also causes break discs to light up from heat this was a
feature in the original game"*.

### What changed
- **`source/physics/vehicle_dynamics.{h,cpp}`** - `getBrakeTorque(i)`: the pad torque of one wheel,
  `BrakingPosition (+0x1c) x MaxBrakingAction x (front ? _brake_balance : 1 - _brake_balance)`, the same
  per-axle limit `updateBrakeControllers` applies (spec 6.4). The surface effects read it.
- **`source/render/eden/eden_surface_fx.{h,cpp}`** - the two features:
  * **hard-braking tyre marks.** `WheelContact` gained `BrakeSlip` (the longitudinal force as a share of
    `Load x SurfaceFriction`: 0.45 starts a mark, 0.85 is full), because the ABS *holds the slip ratio
    down* (0.1..0.25 on a full dry stop, under the 0.35 skid threshold) - the owner's hard stops left
    no mark on tarmac. A `skid > 0.35` now lays the road decal at `3.0 x skid` per metre (a continuous
    black line) and its alpha is at least `0.80 x brakeSlip`.
  * **brake disc heat.** A `BrakeDisc` per wheel (pose, temperature, glow). Heat in = the pad power
    (`BrakeTorque x v / wheelRadius`) / capacity (700 J/K front, 300 J/K rear - a 380 / 330 mm disc),
    out = Newton + a T^4 stand-in, faster with the air flow. Glow from 260 to 780 C, clamped at 1050.
    Drawn in the same `Effects` pass: the disc face in the wheel plane (the rim's spokes are
    depth-tested **in front of it**, so they are silhouetted - the original's look) and a camera-facing
    halo, **blended, not additive** (an additive orange over the silver rim added up past white in the
    first cut), colour `(0.55,0.03,0.005)` red -> `(1.0,0.38,0.04)` orange, never white.
- **`resources/shaders/brakedisc.frag`** + `source/render/shaders/builtin/brakedisc_frag.h` (generated,
  `python tools/compile_shaders.py brakedisc.frag`).
- **`source/game/freeride/gm_freeride.cpp`** - `tickSurfaceFx` feeds the pad torque / brake slide and the
  disc pose (the hub with the suspension travel, in the wheel's steering frame; `+x` is the car's left,
  so the outward axle is `+x` on the left wheels). `setupVehicle` and `teleportTo` now call
  `EdenSurfaceFx::clear()` - the old car's (or old spot's) dust, tracks and disc heat do not follow.
- **`source/physics/vehicle_data.cpp`** - `-dumpcars` also dumps `_brakes_power_front` / `_rear` and
  `_brake_balance`.
- **Docs**: `docs/re/surface_effects_spec.md` 7 (the whole feature, with the laws and the constants),
  `README.md` flags, the two screenshots below.

### Verified
- **The heat law** (`-logbrakeheat -fixedtimestep -autoplay "30:hold=accel;250:release=accel;260:hold=brake;380:release=brake"`):
  `BrakeHeat: 35 C` cold, then `737 C/0.92 603 C/0.66 558 C/0.57 528 C/0.52` at the end of one hard stop
  from ~110 km/h, then `716 -> 591 -> 556` after the release (**it cools**).
- **The glow draws** (A/B against `-nobrakeheat`, same drive, shot at 378): the wheel region differs by a
  mean of 5.2 with **5,302 px > 24**. `docs/images/surface_fx_brake_disc.png` (night, both wheels).
- **The marks** (`-logsurfacefx`): `206 decals, ground tarmac` after the brake; A/B against
  `-surfacefxscale 0` gives a road-region mean of 8.1 and **14,215 px > 24**.
  `docs/images/surface_fx_brake_marks.png`.
- **No regression**: `-surfacefxtest` still gives `186 particles, 483 decals, ground dirt` (the spec's
  numbers), and `EdenSurfaceFx: ready (3000 particles, 1024 decals)`.

### Ruled out (with the test that ruled it out)
- **"An additive glow will look right on any surface."** No: over the silver rim the additive orange
  added up past 1.0 in every channel and the wheels came out **white** (the owner: *"blown out white
  rather than the orange to reddish glow"*). The A/B proved the quad was in the right place (5k px
  changed); only the blend was wrong. Blended now.
- **"The rear discs heating at 35 C during acceleration is the brake."** No: that is the **traction
  control** (`updateBrakeControllers` raises the driven wheels' `BrakingPosition` when they spin), so
  the heat model is reading the pads correctly - it is the pads being applied.
- **"`_brake_balance` is 0 / the front brakes do nothing."** `-dumpcars` on the California:
  `brakes 15000/90 balance 60` - the front axle gets 60 % of 15000 N m, which is why the front discs
  run ~200 C hotter than the rear on the same stop.

### Not done / next
- **The AI traffic cars do not glow and leave no marks** (the same `traffic.cpp` gap as the spray,
  spec 5.6): they would need the same `emitWheel` calls from their own wheel state.
- **The disc face is edge-on in the chase view** - the halo is what shows there. A car parked with the
  camera orbited (or the photo camera) is the view where the spokes are silhouetted.
- **The discs do not light the road** (no projector / point light): the glow is a blended quad only.

---

## Session 2026-11-02 (seventy-eighth, `cline`): Hawaii audit - the police car is a police car again, its one tunnel crossing is carved, and the sea's tide works on Hawaii and Casino

The owner: *"check out hawaii island to make sure police are using correct cars
and if there's bridges or tunnels need to look see if they are filled with dirt
etc... (This has been an ibiza issue) also make sure the sea changes etc... work
on both Hawaii and casino island"*.

### What changed
- **`source/game/freeride/traffic.cpp`** - **a roof light bar alone no longer makes a car a police car.**
  The Hawai fire van (`pompier1`, id 2016) carries the same `Gyro` lens and `FA_G` ring quads as
  `police1`, and `Type::bPolice` was `PoliceBar.isPoliceBar()`. So `-trafficparkpolice` parked a **fire
  truck** (type 10, lift 0.269, before the fix a red pump ladder truck) and `Traffic::setPoliceLights`
  lit the fire van's bar in a chase. The island's `aiconfig.cpr` `POLICE_CAR_ID` now decides:
  `bPolice = bHasBar && policeCarId != 0 && type.Id == policeCarId`; the park hook picks that id first
  (falls back to any bar type) and logs the id. Every bar-equipped non-police type now keeps its bar
  **dark** (`applyOff`, quads hidden) instead of keeping the data's all-lit GLOSSFCT, and only the
  police car sweeps. The log now names the winning type
  (`'police1' is the island police car (POLICE_CAR_ID 2032)`).
- **`source/render/eden/eden_ocean.{h,cpp}`, `source/render/eden/eden_scene_view.cpp`** - **the sea's base
  plane is the island's, not 0.** The loaded island ocean mesh is y = **-1.8 on Hawai** (0 on Ibiza /
  Casino; `ocean_spec` 5.3 - the port logged it as -2 because the bounds line prints `%.0f`).
  `EdenSceneView::initialize` now passes `world.getIslandBoundsMin().y` to
  `EdenOcean::setWaterBaseLevel`; `renderReflection` mirrors about `base + tide`, and `OTDU_OCEAN_LOG`
  prints the absolute level and the base. The `TD_Ocean` meshes receive only the tide offset (their data
  already sits on the island plane), so the sea is not lowered twice.

### Verified
- **Data**: Hawai `aiconfig.cpr` `POLICE_CAR_ID = 2032` ->
  `islands/hawai/vehicules/traffic/police1.bnk` (`police1`, 5.40 m, lift 0.232); `chaseconfig.cpr`
  CopCar level Low = 2032. Ibiza: 3009 -> `ibiza_police.bnk`.
- **Hawaii police**: `-trafficpark -trafficparkpolice` now parks **type 18 (id 2032, lift 0.232)** - the
  white Hawai police sedan with its roof bar (`docs/images/hawaii-police-car-s78.png`). Ibiza parks type 8
  (id 3009, `ibiza_police`). `-police` starts the chase with "2 cops (traffic police model)"; the back
  view at frame 300 shows the Hawai police car behind the California with headlights and roof bar, and the
  arrest sequence fires (`docs/images/hawaii-chase-cop-s78.png`).
- **Tunnels / bridges, Hawai**: `-scantunnels` -> **2860 sectors scanned, 3476 banks missing, 1516
  keyword nodes, 1 with terrain inside** - the exact result of session 56: `TUNSOR`,
  `sector-8-5-4-6`, x 52213..52257, y -19..8, z 35277..35338, band 33 of 33, terrain h 2.7..5.3. The carve
  fix is active on it: `-verbose` at the spot logs **"sector-8-5-4-6: 65 heightfield cells carved into
  tunnel / bridge volumes"**. No `pont` (72 nodes), `viad` (0) or `bridge` (1) node has terrain inside.
- **Tunnels / bridges, Casino**: `-scantunnels` -> 6 sectors scanned, 6330 banks missing, 1 keyword node
  (`?W_TUNNE`, all 160 samples *below* the mesh), **0 with terrain inside**.
- **Sea, Hawai / Casino**: ocean materials set up for all three levels on both (kind 0 sector / 1 area /
  2 island; colour maps `hawai-c` / `casino-c`). `OTDU_OCEAN_LOG=1` -> Hawai
  `sea level -1.715 m (base -1.800, shore swell 12 cm)`, Casino `(base +0.000)`. Wave drift (two frames
  3 s apart, `-notide`): Hawai **384,574 px > 8 (233,400 > 24, mean 38.4)**, Casino **448,394 > 8
  (318,149 > 24, mean 42.9)**. Tide at a shore camera (fixed timestep, same frame, `-notide` vs
  `-tide 20`): Hawai cliff `-tp 36859,9210 -heading 0` **56,082 px > 8 / 35,525 > 24** in the lower half
  (`docs/images/hawaii-tide-s78.png`); Casino bay (world viewer `-campos 2642,40,2600 -camyaw 200`)
  **37,209 > 8 / 7,392 > 24** (`docs/images/casino-tide-s78.png`).
- **Stability**: Hawai FREERIDE 1,200 frames with traffic, 0 stalls (worst gap 2.9 s at frame 0),
  `_hawaii_drive.ppm`; two builds exit 0.

### Ruled out (with the test that ruled it out)
- **"Hawaii's police car is the wrong model."** The bank drawn is `police1` (the white Hawai police
  sedan); the `Dodge_Charger` line is only the DB `_car_file_name` label of `chaseconfig.cpr` CarId 2032.
  The `-police` chase + park shot show the police model.
- **"Hawaii's water plane is missing / the mesh is at y = 0."** The island ocean bounds print -2
  (`%.0f`) but the real plane is **-1.8**, exactly `ocean_spec` 5.3. Only the reflection/tide base was
  still 0; fixed above.
- **"The sea does not change on Hawaii / Casino."** Both animate (wave A/B above); a sea-only view is
  pixel-identical with and without the tide because the plane is infinite and no waterline is in frame -
  an earlier 0-px result at an offshore camera, not a dead sea. The shore cameras move 35k / 7.4k px > 24.
- **"Hawaii has a dirt-filled tunnel like the old Ibiza one."** The single Hawaii hit is the TUNSOR
  crossing under a hillside (the terrain above it carries a residential road at y ~11.8), and its 65
  carved cells are the same class session 56 fixed; no bridge hits.

### Not done / next
- A drive-through of `TUNSOR` (its portals were not found by eye at x 52213..52257 / z 35277..35338 -
  the land side is a hill at y ~1.8-11.8, the sea side is water) would be the final visual check; the
  carve log already proves the fix runs on the sector.
- The reflection base now follows the island mesh; a mirror A/B with `-showsearefl` on the Hawai
  coastline would confirm the alignment visually.
- Sea life (`ocean_spec` 11.3), lane changes / overtaking: unchanged.

---

## Session 2026-11-02 (seventy-seventh, `cline`): the cockpit needles finally move, and the retail camera shake / speed blur are live

The owner: *"I noticed the needles weren't fully implemented if you can do them
things you mentioned about the needles and camera data etc… would be great"*.

### What changed
- **`source/game/freeride/freeride_hud.{h,cpp}`** — the `AddHud` parser now
  normalises the needle tables: points outside `[val_min, val_max]` are dropped
  (California `interior_kmh` carries the mph 200 point `(320, 282)` in its
  0..300 range) and the rest are sorted, so the map is a real piecewise line.
  `updateGauges` maps the display speed through **`interior_mph` / `interior_kmh`**
  (the player's unit system) and `interior_rpm`; the angles are exposed as
  `getInteriorSpeedNeedleAngle()` / `getInteriorRpmNeedleAngle()`.
- **`source/game/cinematic/cinematic_car.{h,cpp}`** — at load the interior bank's
  needle mesh is located (`ND_SPD` when it carries geometry, else the `ND_SPD_1`
  pose; same for rpm — the R8's `_1` node is the mesh, the California has a
  separate child) and its rest pose cached. `setDashboardNeedles` rotates the
  mesh about its **local Z** from that pose (`rest * R_z(angle)`), which is the
  calibrated sweep (the needle points at 9 o'clock for +90°). New calibration
  flags `-needletest <node>:<x|y|z>:<deg>` and `-needlepose <pose>:<mesh>`;
  `-logcar` dumps the `ND_` interior nodes with parent / transform / bounds.
- **`source/game/freeride/gm_freeride.cpp`** — feeds the two angles to the car
  every frame after `hud.tick`, and pushes the camera's speed blur to the view
  (`view.setSpeedBlur`, `-noblur` A/B).
- **`source/game/freeride/chase_camera.{h,cpp}`** — parses and applies the
  retail `shake_*` data: the slow→fast and max→max2 speed ramps, the
  `shake_rpm_purcent` red-line ramp and the x/y magnitude + period sinusoids
  (eye, or aim point when `shake_on_target`; zero-length ramps are disabled).
  `-logcameras` now prints `shake` and `blur` once a second. It also parses
  `blur_int_speed_slow/fast` + `blur_int_slow/fast` and exposes
  `getSpeedBlur()`.
- **`source/render/eden/eden_scene_view.{h,cpp}`** — `setSpeedBlur(0..1)`; the
  final blit / upscale passes take it as a push constant (`* 0.03` UV).
- **`resources/shaders/blit.frag` / `upscale.frag`** — an 8-tap radial smudge
  toward the screen centre, 25 % at the centre / 100 % at the edges
  (`blur_dist_att_type 1`), zero when the strength is 0 (plain copy / upscale).
  Rebuilt through `tools/compile_shaders.py` (glslangValidator).
- **`docs/re/needles_spec.md`** (new) — the node layouts, the tables, the driver,
  the shake / blur laws, the calibration tools and the open items.

### Verified
- `build-cline.ps1` → **build exit 0** (the two shader headers regenerated:
  `blit_frag.h` 2,676 bytes, `upscale_frag.h` 14,224 bytes).
- California interior: `CinematicCar: cockpit needles speed node 14, rpm node 10`;
  idle moves the rpm needle (204 changed pixels in the tach region), accelerating
  moves both (535 speed / 583 rpm). R8: nodes 14 / 11, 420 / 509 changed pixels.
  `-needletest ND_SPD:z:90` moves the California needle from the bottom to
  9 o'clock; `-needlepose ND_SPD_2:ND_SPD` moves it by the 84.5° second pose.
- Camera shake (California chase 20, `-logcameras`): 0.00 at rest, 0.14 at
  21 km/h, 0.42 at 52, 0.49 at 73, 1.00 at 100 (rpm near the red line); the
  cockpit mode 23 ships no shake data and stays at 0.00.
- Speed blur: 0.02 at 21 km/h, 0.10 at 52, 0.18 at 73; on/off at the same
  frame, edge Laplacian variance **542 (on) vs 565 (off)**, 103k pixels differ.
- Units still work through the pause row; needles follow `interior_mph` after
  the imperial flip (the same `SpeedForDisplay` value as the digits).

### Next
- `slip_shake_*` (the slide tremor) and the `blur_ha_*` / `blur_va_*` movement
  blur (needs a velocity buffer).
- The `ND_x_2` second pose is unused; if it is a car's mph 0 pose, switch the
  base per unit system.
- The R8's only gameplay camera is cockpit (23/24); a true chase entry is data
  work, not code.

---

## Session 2026-11-02 (seventy-sixth, `cline`): the pause menu finally has a speed-unit row, the whole car roster was speed-audited (and three defect classes fixed), and the first drivable motorbike

The owner: *"I believe we use kmph I want in the pause menu to able to switch to
mph. But I also want to make sure car data for speeds etc… are close as possible
as while driving something feels off in California it read over 200 and sense of
speed and speed itself felt off  can we improve this preferably for all vehicles
and can we also start work looking into motorbikes?"*

### What changed
- **`source/game/advanced_graphics.cpp`** — the native **Advanced settings**
  page (the page the pause menu opens with Y / Triangle from Options → Video or
  Controls, or F2) has a new second row **Speed units** (km/h / km ↔ mph /
  miles). It applies live (HUD, GAUGES digits, GPS / detector lines, fuel
  range) and is saved when the page closes, like every other row of that page;
  the retail page's Cancel preserves the unit (the native page has already
  written it). The Video-page hint now says "Advanced settings (speed units,
  graphics)".
- **`source/physics/vehicle_dynamics.cpp`** — the rated top speed is now a
  governor: `edVehicle +0x1f0 MaxVelocity = _max_theorical_speed / 3.6` caps the
  forward speed after integration (only forward; reverse keeps its own limits).
  Without it the torque curves ran the Mustang GT to **342 km/h against 250**,
  the Charger SRT8 to 327 against 265, the 2-Eleven to 253 against 225. Also:
  the wheel ground ray has a 15 cm search margin, the chassis sphere floor is
  clamped to +2 cm above the origin, and the per-axle load / inertia share is
  divided by the number of wheels on the axle (a bike gets the whole share).
- **`source/physics/vehicle_data.cpp`** — data sanity for corrupt rows:
  suspension travel clamped to 5..30 cm (612 Sessanta `_suspension_length_front`
  is 1100 mm), `_lift_drag_ratio` clamped to 0..20 (R8 V10 is 200), bike
  detection (`_vehiclecategory` 18..22), bike suspension normalized to a 6 cm
  sag and 100 % throttle, and the Diavel torque curves / `.dhk` used when the
  bike row points at the generic `default_bike.bnk` placeholder.
- **`source/game/freeride/gm_freeride.cpp`** — **`-vehcheck`**: audits every
  valid DB car (`-car X` for one) on the flat plane and logs 0-100 / 0-200 / top
  speed against the DB; `BuildDatabaseWheels` is shared with `setupVehicle`.
  **`-bike`**: drives the cut Ducati Diavel model with the `Default_Bike` row.
- **`source/game/cinematic/cinematic_car.{h,cpp}`** — `Desc::ModelBank` (model
  bank ≠ physics row), wheel hubs lifted to the physics tire radius (the R8's
  `D_RIM_` sits 0.168 m under a 0.324 m tire), the **two-wheel** ground fit for
  bikes, and `-logcar` now dumps the `ND_` cockpit needle nodes.
- **`source/game/shop/gm_shop.cpp`** — `/:IsBike` is derived from
  `_vehiclecategory` 18..22 instead of hard-coded false.
- **`docs/re/vehicle_physics_spec.md` §7.2**, **`docs/re/bikes_spec.md`** (new)
  and **`docs/re/weather_clock_units_spec.md` §1** — the measurements, the bike
  data/prototype and the unit-row path.

### Verified
- `build-cline.ps1` → **build exit 0**.
- Vehtest before/after: Mustang_GT top **342.1 → 250.0** (DB 250), Charger_SRT8
  **326.8 → 265.0** (DB 265), 2_Eleven **253.4 → 225.0** (DB 225), Viper
  **336.7 → 325.0** (DB 325), California 309.7 (DB 310); R8 V10 **0.2 → 325.0**
  and 0-100 4.10 (DB 4.00), R8 Spyder 313.0 / 4.13, 612 Sessanta 302.6 / 4.93.
- `-vehcheck` (whole roster, ~8 s): **137 cars, 36 cut rows without a bank, 113
  within 1 % of the quoted top speed**, 69 within 0.30 s of 0-100. Remaining
  low outliers are the placeholder rows and hypercars whose quoted speed is not
  reachable with their torque/gearing (CCXR 309.6 vs 417, Type_D 217.5 vs 261).
- Units through the real pause path (`pause → right x5 → validate → down x4 →
  validate → down → validate → Y → down → right → back`): `Advanced settings:
  units imperial (mph / mi) (saved on close)`, `options.ini` written with
  `units = 1`, screenshot `_units2.png` shows the row on the Video page, and a
  restart logs `FREERIDE: units imperial (mph / mi)`. The F2 path the same
  (`_units3.png`). A driving A/B at 65 km/h reads `041` + `310.1 mi` imperial
  against `066` + `499.1 km` metric (`_hud_units_ab.png`).
- Bike: `-bike` loads the Diavel (two `D_RIM_` wheels, 17" rims, tyre r 0.300),
  stands on its wheels, drives and turns (`_bike.png` / `_bike_lean.png` in the
  scratch root, gitignored). `-vehtest -bike` is stable, no wheelie, 0-100 22 s
  / top 197 km/h — the placeholder row's first gear runs to 197 km/h at the
  13 000 rpm limiter; a real Diavel row is the next step (`docs/re/bikes_spec.md`
  §3).

### Next
- A real `DBCarPhysicsData` row for the Diavel (the placeholder row's numbers
  cannot reach the claimed 332 km/h / 4.0 s); re-read the drive-train ratio
  path first (adding the primary reduction ×1.8 made the bike slower).
- The cockpit gauge needles (`D_ND_SPD` / `D_ND_RPM` are in the **interior**
  bank, not the car model): the R8 cockpit shot shows the dials dead at 0 while
  the HUD reads the speed. `-logcar` now dumps the `ND_` nodes; the needle
  tables (`interior_kmh` / `interior_mph`) are already parsed in `FreeRideHud`.
- Speed-based camera feel: the retail camera data has `shake_*` /
  `blur_int_*` curves (CamerasInGame.xmb) that the port does not apply;
  the FOV lerp is the only speed cue today.

---

## Session 2026-11-02 (seventy-fifth, `cline`): the stray beige box on the driving HUD, the fuel bar moved under the odometer, and a fuel law that no longer reads 71,435 km

The owner's ask, quoted: *"I want you to try and fix some of the UI issues we have I know your sometimes hit with
vision issues so if it becomes to much bench it and possibly find something you can work on such as setting up the
races, quests or if we go to a petrol station we can refuel also improve fuel logic and maybe move the placeholder
fuel UI to just underneath the Milage near speedometer or just above it which you think is best."*

**Reproduced** — the HUD at the Ferrari dealership, 1280x720:
```
-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -carlod 4
-width 1280 -height 720 -exitframe 320 -autoplay "240:shot=_hud_before.ppm"
```
Two defects on the frame: a **solid beige rectangle (224, 219, 209) at canvas 150..330 x 690..720** (bottom left,
under the minimap), and the fuel bar at the top right reading **`Tank 78 L 71435.0 km`** — a full 78 L tank with a
71,435 km range.

### What changed
- `source/game/freeride/freeride_hud.cpp` — **the beige box is `HUD.vmf /HUD/enter`'s own prompt bar**: char 231,
  placed at **depth 1 with no name**, so no path could reach it. It is anchored to the element's parked origin
  (stage -459, 229) while its shape (char 229) is a 179x34 bar at local (615, 576) — i.e. it lands at the bottom
  left, far from the tile the port moves. `showEnterSpot` now hides it every frame with the new
  `FlashPlayer::setInstanceVisible( "/HUD/enter", 1, false )`, and `setWorldMarkers` does the same on each
  `wm<N>` clone (a clone carries the bar too).
- `source/game/freeride/freeride_hud.cpp` — **the empty dark panel in the middle of the screen** was the same
  element's card (the dark panel + the button glyph, unnamed children at depths 4 / 5 / 9 / 10 / 13). The port
  passes an empty prompt text while the car is moving (`bSlow ? pNearAction : ""`), so the tile marked the spot
  but the card stayed with nothing in it. `showEnterSpot` now shows the card only when the prompt text is set.
- `source/game/freeride/freeride_hud.cpp` — **the world markers were never placed**: `duplicateMovieClip` puts the
  copy in the source's *parent*, so the clones are `/HUD/wm<N>` siblings of the element, not `/HUD/enter/wm<N>`.
  Every `setClipVisible` / `gotoLabel` / `setClipTransform` on the old path failed silently, leaving the clones
  stacked at the element's own position. The paths are fixed, and the element is hidden while no spot is shown
  (`tick`), which is safe now that the markers are siblings.
- `source/flash/flash_player.{h,cpp}` — `setInstanceVisible( clipPath, depth, bVisible )`: the display list is keyed
  by depth, so an unnamed child needs a depth-based setter (`setClipVisible` resolves names only).
- `source/game/freeride/freeride_hud.cpp` — `drawFuel` moved from the top right (792, 112) to **under the odometer
  box of the gauge** (1030..1175 x 686..712, centred on the odometer's 1040..1165 x 662..684): a 54x9 fill bar on
  the left, `Tank 78 L` and `497.2 km` on the right. Under the mileage reads as part of the instrument cluster;
  above it there is only the 8 px gap between the dial's bottom (654) and the odometer box (662).
- `source/game/freeride/freeride_hud.cpp` — `drawGeneralClips` (new): `drawClipOnly` ignores a clip's own
  visibility, so `/db_progress` (the level-up popup) painted its parked beige card every frame. It is now drawn
  only while `levelUpStage_ > 0`, `/pop_tutorial` only while a hint is open. `/helpmore` and `/help_system` (the
  frontend's help bar / help popup, root clips the port never plays the `default` frame of) are hidden at load.
- `source/game/freeride/fuel.{h,cpp}` — **the consumption law**. `_gas_cons_min` is **0.1 on 266 of the 278 rows**
  (`-dumpcars`), a placeholder; the old law used it at 0 km/h, which is where the 71,435 km came from. The curve is
  now `avg x 1.2` at 0, `avg` at 60, `min` at 90, `max` at 200, with a min below a quarter of `avg` treated as
  unset (`avg x 0.7`) and a 0 max as `avg x 2.5`. `RangeKm` is read off a distance-weighted average
  (`AverageConsumption`, 1.5 km time constant) and capped at 5000 km. v-power burns 3 % leaner
  (`FuelGradeConsumptionFactor`). The load log prints the row and the curve.
- `source/game/freeride/gm_freeride.cpp` — **the `Breakdown` line never reached the screen**: it was set in the fuel
  block (line ~2884) and `tickWorldSpots` (line ~3272) clears the prompt every frame. It is now set after
  `tickWorldSpots`, and only when no spot prompt is up. The station pump also needs the car **under 20 km/h**
  (`bSlow`) to open, like the shops.
- `source/game/freeride/freeride_hud.{h,cpp}` — `hasPrompt()`; `-loghudregion` (a probe that logs every instance of
  the HUD / GENERAL / GAUGES movies whose origin or shape box lands in a stage region — this is what named the
  beige box; the stage is 1280x960 drawn at y -120 in screen ratio mode 1, so canvas y 690 is stage y 810).

### Verified
```
-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -carlod 4
-width 1280 -height 720 -exitframe 420 -autoplay "240:hold=accel;400:shot=_hud_after3.ppm"
```
- the beige box is gone: `(160,700)` is now `(38, 37, 32)` (road), was `(224, 219, 209)`; the fuel bar reads
  `Tank 78 L / 497.2 km` under the odometer (`docs/images/fuel-hud-s75.png`).
- the empty enter card is gone while driving: 18,225 pixels changed in the panel region (canvas 540..820 x
  500..720) against the before frame, and the region now reads road colours (12..26) instead of the card's
  `(47, 53, 59)`. Stopped at the spot the card and the `Enter` prompt are still there (136 bright pixels in the
  text area, max 255; 0 while driving). The final frame is `docs/images/hud-fixes-s75.jpg`.
- `Fuel: 'California' tank 78.0 L, row 0.1/13.1/0.0 l/100 km -> city 15.7, 60 13.1, 90 9.2, 200 32.8, grade 0`.
- `-fuel 0.02` + `hold=accel`: consumption follows the curve (15.7 -> 14.8 -> 13.5 -> 11.4 -> 10.0 -> 16.4),
  `Fuel: tank empty after 149 m - Breakdown`, and the `Breakdown` line is on screen
  (`docs/images/fuel-breakdown-s75.png`).
- FUEL1 station prop, `A` twice: `Fuel: pump screen at FUEL1 (tank 20.0 / 78.0 L, grade 0)` — the screen shows
  `20.0 / 78.0 L  127 km`, `Fill: 58.0 L`, `$86` — then `Fuel: filled 58.0 L Shell basic for $86.42 (money
  4996305)` (`docs/images/fuel-pump-s75.png`).
- `build-cline.ps1` -> **build exit 0** (5 incremental builds; only pre-existing warnings).

### Ruled out (with the test that ruled it out)
- **`/helpmore` (the frontend help bar) is the beige box**: hiding it changed nothing — the box survived, and the
  `-loghudregion` probe found no instance of the HUD / GENERAL / GAUGES movies in the region until the probe's
  stage region was corrected for screen ratio mode 1 (canvas y 690 = stage y 810, not 920).
- **A world marker clone**: the box stayed at the same canvas position with `-heading 90` and on Hawaii
  (`-island hawai -tp 11241.4,32482.8`), so it is a fixed element, not a projected one.
- **`/db_progress`**: gating it on `levelUpStage_ > 0` (kept — it was a real bug) did not remove the box.
- **`/HUD/compteur_int` (the HUD movie's own gauge copy) is the empty panel**: hiding it is pixel-identical
  (0 changed pixels), so the probe's per-instance `visible` flag is not the effective visibility — the parent
  `/HUD/compteur_int` is hidden and its children only *look* visible. The probe reports the instance's own flag,
  not the parent chain; read it with that in mind.
- **`/helpmore` / `/db_progress` / the marker clones are the empty panel**: hiding `/HUD/enter` outright changed
  21,899 pixels, which is what finally pointed at the element.

### Not done / next
- The probe (`-loghudregion x0,x1,y0,y1`) reports an instance's own `visible`, not the parent chain — a child of a
  hidden clip still prints `visible 1`. If another stray element shows up, run it and then hide the parent.
- The enter element's card depths (4 / 5 / 9 / 10 / 13) are the ones the movie places at its `on` frame; a
  different HUD state could place others.
- Races / quests were not touched this session (the fuel work was the concrete ask). `RaceEvents` already builds 8
  starts from the road graph (`source/game/freeride/race_events.cpp`); the next step there is the DBEvent reward
  rows and the `challeng.bnk` screen.
- The grades still have no performance effect (v-power only burns 3 % leaner).

---

## Session 2026-11-02 (seventy-fourth, `cline`): night windows and ground lighting — the frame `UserVector1` was hardcoded to day

The owner's report, quoted: *"I was driving night time just now and were missing a hell of a lot non of
buildings seem to be lit no windows etc.... have lighting at night time and noticed I think our lamps are using
wrong colour light projection it looke more yellow"* (the screenshot did not come through). Also asked why
TDUWorld runs the same game at 4K 130-160 fps when this port cannot — answered in
`docs/re/quality_profiles.md` §5b (TDUWorld is the original engine: instancing / batching / LOD; the port makes
~25k individual draw submissions and is CPU-bound, so resolution/AA are not the lever).

### What changed
- `source/render/eden/eden_scene_view.cpp` — **the actual night switch**: `EdenSceneParams::UserVectors[1]` was
  hardcoded to `(1, 0, 0, 0)`, but its `.y` is the night lights factor and its `.w` is the TD_Building window
  switch (the `vs_92` shader compares `.w` against `cF0_def.z = 0.5` and only then writes the window output).
  It is now `(1, nightWeight, 0, nightWeight)` from `spot_exterior_lightmap_day_night.y` (fallback: weather
  hour, lit 20:00-06:00). Day is unchanged.
- `source/game/assetviewer/gm_asset_viewer.cpp` — the same vector, so previews match the game.
- `source/world/eden_world.{h,cpp}` — model load/unload callbacks; `source/render/eden/eden_scene_view.{h,cpp}`
  registers the streamed sector/area models' NIGHTLMAP materials and unregisters them on unload (they were
  never registered before: only cinematic sets and spot exteriors were). `-lognightlmaps` dumps them; the
  exterior cap is 512 for a loaded world (the retail 63 was a spot-scene array).
- `docs/re/night_lights_camera_glass_spec.md` §5 — the shader evidence, the NIGHTLMAP dead end, and the
  remaining lamp-light work. Screenshots: `docs/images/night-fix-ab-s73.png` (before / `.y` / `.y + .w`) and
  `docs/images/night-windows-s73.png` (tuning shop, rows of lit windows).

### Verified
```
-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -night
-width 1280 -height 720 -exitframe 300 -autoplay "240:shot=...ppm" -log _night.log
```
- weather at 00:00: `FREERIDE: lmap ext (0.000 1.000) int (0.000 1.000)`.
- building spot: walls/ground gain the night bake and one window strip lights (before/after mean abs diff
  **2.35/255**, 135k pixels > 8); the full tuning-shop street shows **rows of lit windows**.
- day remains `(1,0,0,0)`, so the day frame is unchanged; no crash, no assert in the runs.

### Ruled out (with the test that ruled it out)
- **"The world is missing its NIGHTLMAP update"**: 175 streamed TD_Building materials carry NIGHTLMAP and all
  had `u6 = 0` (the old exteriors-only filter required `u6 = 1`, so none were registered) — but registering
  them and setting `(0,1)` is **pixel-identical**, and the TD_Building CTAB has no NIGHTLMAP constant. It is
  data plumbing, not the switch.
- **`MATPARAM.x` on window materials** (`-windowtest`): pixel-identical. The window shader reads the frame
  `UserVector1`, not the material parameter.

### Not done / next
- **Lamp colour** still needs the owner's screenshot (or the spot + `-clocktime`). The lamp head is the unlit
  `TD_Lamp` quad (`sf_light_c`); retail's `streetlamps.xml` `#f8d6a6` colour and light pools are not
  implemented. Compare against `docs/images/night-windows-s73.png`.
- **TD_ShadowBox light pools** (car headlights / lamp ground pools) remain skipped: the world-view-proj and
  ground-projection constants are not written (`docs/re/night_lights_camera_glass_spec.md` §4).
- Performance: see the new `docs/re/quality_profiles.md` §5b and §6 — instancing / merged static batches /
  LOD1+ impostors are what close the gap to the original engine.

---


## Session 2026-11-02 (seventy-third, `cline`): FREERIDE 41 -> 35 ms (the minimap was 11.5), the opening scene culled, and a 4K 60 fps quality profile

The owner's ask: the game cannot hold 60 fps at 4K on a 5070 Ti / i9-10850K, the opening scene should be
faster, everything should document the numbers for another agent to review, and a **profile system** should let
a Steam Deck / Legion Go drop resolution and settings for the best playable experience. Scoped to the measured
frame rather than guesses: the port is CPU-recording-bound (35 ms CPU vs 2.5 ms GPU), so the work went into the
per-frame CPU costs first. The full brief is `docs/re/quality_profiles.md`; the history is `docs/PERF_PLAN.md` §0.

### What changed
- `source/game/freeride/freeride_hud.cpp` — **the minimap was the FREERIDE frame**: `drawMinimap` ran
  Sutherland-Hodgman clipping over every road triangle in a 570 m square every frame (**11.5 ms**, measured with
  the new `-loghudtimes`; the HUD / GAUGES Flash movies are only 0.4). It now draws the roads through a GPU
  scissor (`Renderer2D::setClipRect`) and rebuilds the map vertex list at ~20 Hz, cached between rebuilds.
- `source/render/renderer_2d.{h,cpp}` — `setClipRect` / `clearClipRect` (canvas rect -> Vulkan scissor; flushes
  the batch on a change), used by the minimap and available to any 2D owner. `begin` / `beginRect` reset it.
- `source/game/game_options.{h,cpp}` — the quality profiles: `quality_profile`, `hud_quality`,
  `ocean_reflections`, `draw_distance`, `shadow_distance`; `ApplyQualityProfile()` (High / Medium / Low /
  **60 fps** / Steam Deck / Legion Go) and `-profile <name>` plus the individual overrides. All are written to
  `options.ini`.
- `source/game/advanced_graphics.cpp` — the **Quality profile** row (0..6) at the top of the page; picking one
  applies the preset and `ApplyVideoOptions()`, editing any other row makes it Custom. New rows: HUD detail,
  Water reflections, Draw distance, Shadow distance.
- `source/world/eden_world.cpp` — `drawInstances` scales each instance's draw distance by `DrawDistance`
  (the strongest draw-call lever; 50 % in the handheld profiles).
- `source/render/eden/eden_scene_view.cpp` — the water mirror pass honours `OceanReflections`
  (0 = render once, 1 = retail every frame, N = every Nth); the first frame always renders so a static
  reflection has content.
- `source/render/eden/eden_shadows.cpp` — `ShadowDistance` scales `MaxCastDistance`; kept as a knob but see
  "Ruled out" (no effect at the benchmark spawn).
- `source/render/eden/eden_renderer.{h,cpp}` — `isSphereVisible(center, radius)` frustum test.
- `source/game/avatarchoice/intro_cinematic.cpp` — `drawSets` culls the 31 PNJs, the player, the car and the
  showroom/display cars against each pass's frustum (shadow slices, ocean mirror and projectors included);
  `playingOrder()` is hoisted out of the per-prop loop.
- `README.md`, `docs/PERF_PLAN.md`, `docs/re/quality_profiles.md` — the flags, the profile table, the measured
  numbers and the remaining structural work.

### Verified
```
-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -carlod 4
-autoplay "60:hold=accel" -width 1280 -height 720 -logscenetimes -loggputimes -exitframe 480 -log _perf.log
```
- default 720p: **41-42 ms -> 34.8 ms** (24 -> 29 fps); `main` 11.0 -> 2.2 ms, HUD minimap 11.5 -> 0.6-1.7,
  GPU unchanged at ~2.5 ms. `-no3d` HUD-only frame 12.6 -> **2.9 ms**.
- `-profile low` 720p **10.5 ms (96 fps)**; `-profile low` at **3840x2160** (70 % = 2688x1512)
  **10.9-11.1 ms (90-92 fps)**; `-profile 60` at **3840x2160 native 13.3-15.1 ms (66-75 fps)**;
  `-profile medium` 23.0-28.2 ms (35-44 fps); `-profile steamdeck` 1280x800 **9.6-10.2 ms (99-104 fps)**;
  `-profile legiongo` 1920x1200 **11.5-12.8 ms (78-87 fps)** — the last two on this PC, not on the devices.
- Opening scene `-intro` 720p: 8.2 ms (121 fps) after the culling; the fixed-timestep frame-400 screenshot
  against the before-build one is **pixel-identical (mean 0, max 0)**, so no avatar vanished.
- Draw counts (`-drawfrom 0 -drawcount 1000000`): **6,449** in the main pass, ~4-5k in prepass / shadow /
  ocean, ~330-817 in the rest — the reason High stays ~35 ms at 4K while the GPU idles.

### Ruled out (with the test that ruled it out)
- **"The Flash HUD is the cost"** — `-loghudtimes`: HUD/GAUGES 0.4 ms, minimap 11.5 ms.
- **"Lower the shadow distance for Medium"** — `-shadowdistance 50`: shadows 6.7 ms before and after; the three
  slice submissions are the cost, not the 736 m range.
- **"Render scale helps the CPU"** — Low at 720p (70 %) and at 4K (70 %) are both ~10.5-11 ms; resolution only
  moves GPU time.

### Not done / next
- **High is still ~35 ms at 4K.** It needs fewer draw submissions: instancing the `.pmi` instances / merging
  static sector geometry by material, and loading the game's LOD1+ / `TD_HardwareInstance` / `TD_Impostor`
  meshes (`docs/PERF_PLAN.md` P3). Start at `docs/re/quality_profiles.md` §6.
- **Shadow slices** (3 world re-submissions): a 2-slice mode or casters every other frame while resolving the
  screen shadow every frame should save ~2-3 ms.
- **Z pre-pass** (7 ms) cannot go without changing how the shadow resolve gets receiver depth (previous-frame
  depth or a deferred shadow pass).
- **Handheld hardware untested**: run `-profile steamdeck` / `-profile legiongo` on the Deck and the Legion Go;
  if they miss 60, lower `draw_distance` first (CPU), then `render_scale` (GPU).

---


## Session 2026-11-02 (seventy-second, `cline`): the UI viewer + the UI element label file - the frontend's flash display list as data

The owner's ask, quoted: *"we're missing a lot of UI elements so what I was
thinking maybe we can make a tool similar to how asset viewing etc... works but
mostly for inspecting each UI element like pause screens and in game menu's ...
if we could do this we may be able to fix a mass majority of UI stuff and down
the line also maybe be able to recreate near 1:1 replicas of the UI"*. Scoped with
them: **Ibiza first** (Hawaii is not reachable yet) and **main.vmf first** (the
pause menu is the one frontend screen the port already drives by hand, so the tool
is checkable against known ground truth). They also asked for the findings in a
standalone doc for another agent - that is `docs/re/ui_viewer.md`.

### What changed
- `source/game/uiviewer/gm_ui_viewer.{h,cpp}` - **`GM_UiViewer`** (`-gm uiviewer` /
  `-uiviewer`): the frontend movies' display lists as data. Panels: movies
  (FrontEnd/HiRes), the display list (path, type, character, depth, frame,
  visibility, labelled `*`), the selected element (its clip's timeline **labels as
  clickable `gotoLabel` buttons**, plus the label/note editor) and the movie drawn
  live with **click-to-select** and a highlight box on the selection.
- `source/game/ui_labels.{h,cpp}` - the UI element label file, mirroring
  `game/asset_labels.{h,cpp}`: `docs/re/ui_elements.txt`, TAB separated, keyed on
  `<movie .vmf>` + `<clip path>` (the identity the runtime itself takes).
- `docs/re/ui_elements.txt` - seeded with **only** the keys
  `game/pause_menu.cpp` already resolves by name (16 of them).
- `docs/re/ui_viewer.md` - the spec, the verified numbers, the state-machine dumps
  and what is next.
- `docs/images/ui_viewer.png`, `docs/images/ui_viewer_log_in2.png`.
- `source/game/gm_base.{h,cpp}` - `GM_UiViewer` + `"UI_VIEWER"` (appended last so
  the existing mode values do not move); `source/tdu_instance.cpp` - the factory
  case and the `-uiviewer` shortcut.
- `launch/17_UiViewer.bat`.

The click test reuses the runtime's own composition rule rather than inventing
one: `world = parentMatrix * instance.Matrix` (`drawClip`), then
`world.inverse()` on the point in root twips, compared against the character's
bounds - shapes and edit texts authored, sprites the union of their children,
buttons the union of their **hit records**. Bitmaps are shape fills in this
runtime, not display list characters `[CODE]`.

### Verified
```
-gm uiviewer -lintui
  UI lint: 16 label(s) over 1 movie(s), 0 problem(s) - every key is a display list path

-gm uiviewer -dumpui
  UiViewer: main.vmf: 1280x960 stage (px, 1 px = 20 twips), 210 frames @ 30 fps,
            5068 tag(s), 525 bitmap(s)
  UiViewer: --- main.vmf: 110 element(s) ---

-gm uiviewer -goto "/:pause_stop" -dumpui
  UiViewer: jumped '/' to 'pause_stop'
  UiViewer: --- main.vmf: 965 element(s) ---

-gm uiviewer -goto "/:pause_stop;/pause_menu/menu_sq:stop" -clip /pause_menu/menu_sq/sq3 \
    -autoplay "70:shot=..." -exitframe 95
  UiViewer: selected '/pause_menu/menu_sq/sq3' (sprite, character 18565, depth 82,
            frame 0/208, visible, bounds known)
```
The `-clip` selection loads its label from the file into the editor (screenshot
shows `entry square 3` / `photo`), so the whole flow - select, name, save - is
proven headlessly.

### Ruled out (with the test that ruled it out)
- The first `-lintui` reported **all 16** keys missing, including
  `/pause_menu/menu_sq`, which `pause_menu.cpp` definitely finds. Cause: the lint
  walked the display list **straight after load**, and a frontend movie builds its
  UI from the actions of its first frames, so it was empty. Fixed by settling 30
  frames first, then by walking every root-timeline label too.
- `-goto` in `enter()` gave `no label 'open' on '/pause_menu'`: the nested clip
  does not exist until the root has run. Fixed by deferring `-goto` to the first
  settled frame and retrying it; it now takes a `;`-separated chain.
- `/pause_menu/*` keys are **not** wrong, only state-gated: `/` in `pause_stop`
  produces them (110 -> 965 elements). Not a data problem.

### Not done / next
- **The pause screen previews mostly black.** `PauseMenu::layoutEntries()` writes
  the square row's `_x`/`_y` every frame because MAIN.vmf's own `setSquareMenu()`
  returns early, so the viewer shows the authored art but not the imposed row. The
  display list is unaffected (correct and complete).
- **The recorder (`-logui`)** is the single highest-value next step: hook
  `FlashPlayer::placeObject` / `removeObject` / `gotoLabel` behind a flag and log
  `(frame, path, event)` while the real game runs, which finds the elements that
  only exist on a later frame.
- Then the bulk inventory pass over the other movies (`hud`, `gauges`, `general`,
  `map`, `shopsacar`, the 9 `cas_*` screens, ...), then the dial restyle of
  `/HUD/ig_menu`, then a real ignition state (`ap.bEngineOn` today is only an
  audio duck flag).
- Start at `docs/re/ui_viewer.md` §5 and §6; `-lintui` is the accuracy gate for
  every label added.

---

## Session 2026-11-01 (seventy-first, `cline`): the tuning shop sells rim sizes, and the tires get the matching profile

The owner: *"Would it be possible we could implement custom rim sizes as a mechanical option in game but make sure the tires can get a profile for it to match?"*

### What changed
- `source/physics/vehicle_data.{h,cpp}` — the rim-size model: `RimProfile`, `GetStockRimInfo` (DBCarPhysicsData `_id_car` -> `_default_rims` -> DBRims), `GetRimProfile` (plus sizing: **the rolling radius stays the stock one**, sidewall = stock radius - rim radius, aspect = sidewall / width) and `GetRimSizeRange` (stock +-3", clamped to the 13"..24" tire meshes and to a 25 mm / 20 % sidewall floor). The stock size is always valid.
- `source/game/player_profile.{h,cpp}` — `OwnedCarRimSizes` (`_id_car` -> inches), saved as `car_rim_sizes = <id>:<inch>,...`.
- `source/game/cinematic/cinematic_car.{h,cpp}` — `Desc::RimDiameterFront/Rear` (0 = the DB diameter). The tire model is loaded with a `GeometrySetup` that remaps **every** base `T_13P`..`T_24P` mesh to bead = the rim lip (`diameter / 2 + 2.17 cm`, the port's normalized rim 0.212 m) and tread = `Desc.TireRadius` (radial map `r -> a r + b`, normals by the inverse transpose `n_yz / a`), so any diameter can be selected live. `setRimDiameters` reselects the `T_<d>P` nodes. The rim draw scale now **replaces** the baked `D_RIM_xx` radial scale when the diameter differs (divides the baked 1.24 out); the old code only recomputed it for a baked scale of 1, and multiplying a custom scale onto 1.24 compounded to a ~27" rim.
- `source/game/freeride/tuning_menu.{h,cpp}` — a fourth row **"Rim size"**: up/down selects, left/right previews the range, validate fits it ($8k [INF], free back to stock), the row and the message show the derived tire (`21" 245/30`).
- `source/game/freeride/gm_freeride.cpp` — the profile's size is applied at spawn / car switch (`ApplyProfileRims`) and **live** when the panel closes (`applyTuning` -> `car.setRimDiameters`; no model reload, so none of the `-switchcar` device wedge). Test hooks: `-rims <inch>` (freeride + title scene) and `-tuningmenu`.
- `source/render/eden/eden_model.cpp` — the bank `load()` convenience overload dropped the `geometrySetup` argument (passed `nullptr`); fixed so the tire hook actually runs.

### Verified
- `build-cline.ps1` exit 0.
- Menu (`-gm FREERIDE -skipcine -no3d -tuningmenu -autoplay "30:press=down;40:press=down;50:press=down;60:press=right;70:press=right;80:press=validate;110:shot=...;130:press=back;..."`): `Tuning: shop for car 387 'California' ... rims 19" of 16..22` -> `Tuning: car 387 rims 21" (21" 245/30) for $8000` -> `car_rim_sizes = 387:21` in `profile.sav` -> `CinematicCar: rims 21" / 21", tire nodes 28 / 28` -> `FREERIDE: 'California' rims 21" applied live`. Panel screenshot: `_rimmenu.png` (scratch, `_*` ignored).
- The saved profile at spawn: `FREERIDE: 'California' on its 21" rims (tuning shop)` (`_rimprofile.log`).
- The geometry: `-rims 21` logs `rim 21" drawn at lip 0.288 m (multiplier 1.097 on the baked 1.240)` and the T_21P remap `bead 0.296 -> 0.288, tread 0.363 -> 0.339` (the 0.339 m stock radius). The title Touareg (`-cine 2 -rims 22`): `rim 22" drawn at lip 0.301 m (multiplier 1.145 on the baked 1.240)`, T_22P `bead 0.309 -> 0.301, tread 0.373 -> 0.365`. The stock/22 title frames differ in **1428 pixels, all in the wheel band (y 460-570)**.
- Physics untouched: `-vehtest` with the feature built: **0-100 4.52 s, top 309.7 km/h, 100-0 34.4 m**, the documented baseline (the rolling radius is what the profile preserves).

### Ruled out / notes
- The rear axle is drawn at the front rolling radius (the retail/edVehicle rule: `LoadVehicleSetup` uses the front `DBRims` values on both axles), so a staggered rear reads thinner; the menu shows the front profile only.
- The remap also applies to stock cars: their visual tire now sits at the DB radius (e.g. the One-77's `T_20P` shrinks ~16 mm), i.e. it no longer sinks into the road. If a car's baked `D_RIM_xx` scale is not the 0.212 m + 2.17 cm formula, the draw now trusts the diameter; check it with `-logcar`.
- The `$8k` rim price is [INF] (the retail pricing table is encrypted).

---

## Session 2026-11-01 (seventieth, `cline`): cars drive the right-hand lane, the far sea stops popping and the surf runs up the beach

The owner: *"cars are driving wrong side of the road ... the recent water work doesn't seem to fix the sea near beaches
the coastal stuff looks static and was hoping we can try enhance the sea more so it goes back and forth on beach I just
did a quick check as well and there's a oddity the new sea renders but it keeps reverting and popping in and out think
some overlap from water texture that maybe used for the foam which hopefully we can simulate and add coastals etc...?"*

Specs: `docs/re/traffic_spec.md` "Lane side", `docs/re/ocean_spec.md` 11.4, `docs/re/ocean_fix_spec.md` 3.

### What changed
- `source/game/freeride/traffic.cpp` — **the lane was on the wrong side**. The Eden camera is right-handed
  (`core/math.h` `LookAt` uses `s = forward x up`; `walk_controller.cpp` and `chase_camera.cpp` both say screen right
  is -X at +Z), so right of a heading `(tx, tz)` is `(-tz, tx)`. The traffic used `(tz, -tx)` = the driver's LEFT.
  Fixed `pathAhead`'s lane offset (both the on-link and the past-the-node target), the right-turn indicator test,
  `-trafficpark`'s columns and the near-miss probe. No density / model change.
- `source/game/freeride/chase_mode.cpp` — the chase cops had the same `(tz, -tx)` lane offset (`kCopLaneOffset`);
  fixed, together with the ground-sample right axis and its cross order so the slope roll stays correct.
- `source/world/eden_world.{h,cpp}` — **the far sea**. `area-X-Y-o` is a 768 m ocean tile per sector cell under dummy
  nodes `Sector-ax-ay-sx-sy`; the branch loaded the area objects and set their TD_Ocean materials up every frame but
  **never drew them**, while `updateOceanVisibility` hid the island `AREA-x-y` tile for any loaded area with area
  ocean and guessed each area tile's sector from its centre. The sea beyond the loaded 3 x 3 sector ring fell through
  to sky / the -100 m island heightfield and popped as the ring moved. `draw` now draws the area `-o` objects
  (`-noareaobjects` is the A/B); `loadArea` records each item's sector group (`ItemSectors`, including the odd-row
  previous-area column 7); an area ocean tile is hidden only when its own sector is loaded **and has a TD_Ocean tile
  of its own** (otherwise the area tile is the only water there). Non-ocean group items hide under their loaded
  sector. `loadArea` also sets `bOceanVisibilityDirty`.
- `source/render/eden/eden_renderer.cpp` — the transparency of 11.2 put the sea in the distance-sorted blended flush,
  where the shore foam (also blended) and the water are at the same distance and swapped order as the camera moved:
  the water painted over the foam and the surf popped. Every TD_Ocean draw now joins the water bucket even when it
  blends: opaque sea bed -> water -> foam and the other blended draws.
- `source/render/eden/eden_ocean.{h,cpp}` — **shore swell**: a 9.2 s surf cycle, `level = water_tide cm *
  (0.80 sin θ + 0.20 sin 2θ)`; the reflection mirrors about `y = level` instead of a fixed 0. `OTDU_OCEAN_LOG` logs
  the live level once per second.
- `source/render/eden/eden_model.{h,cpp}` — `setOceanYOffset` lifts every TD_Ocean item by the level;
  `setFoamOffset` moves the retail TD_backwash `Ecume` strip along the beach. The strip's footprint is fixed, so a
  purely vertical move would only float it: `loadSector` computes the beach gradient under it from the sector
  heightfield at +-96 m (the strip sits over the sea-bed heightfield; the shore only shows at that scale), clamped to
  1:40. `foamOffset = FoamTideVector * level + (0, level, 0)`.
- `source/render/eden/eden_scene_view.cpp` — `world.setSeaLevelOffset( ocean.getWaterLevel() )` every frame (0 when
  the ocean is off).
- `source/game/game_options.{h,cpp}`, `source/game/advanced_graphics.cpp` — `water_tide` (0..20 cm, default 12) in
  `options.ini`, the Advanced page row **Shore swell**, and `-tide <cm>` / `-notide` overrides.
- Docs: README CLI rows (`-tide`, `-notide`, `-noareaobjects`), the three specs above.

### Verified
- Ibiza FREERIDE `-tp 7954,18154 -heading 103 -clocktime 14:00 -trafficnear`: 11 cars, **worst lane error 0.20 m**
  (the logs before the fix showed 0.9-2.8 m), 0 contacts, 880 beats, 0 stalls. Hawai `-island hawai -tp
  36148,8922 -snaproad`: 5 cars, lane 0.08 m, 1 200 beats, 0 stalls.
- Far sea, `-campos 36070,260,5960 -camyaw 90 -campitch -15`, fixed timestep, frame 60: `-noareaobjects` against the
  new run = **82 428 px above 24/255 (mean 1.91)** in the horizon band y 658..1181; at the largest difference pixel
  (18, 669) the new run is sea `[16,25,34]`, `-noareaobjects` is sky `[255,255,218]`.
  `docs/images/ocean-far-sea-ab-s70.png` (top: new, bottom: -noareaobjects).
- Surf, `-campos 36070,12,5960 -camyaw 90 -campitch -15`, fixed timestep, frame 105: `-tide 20` (level +0.142 m)
  against `-notide` = **232 061 px above 24/255 (mean 1.71)**; the retail foam run-up is 13.6 m per m of sea level on
  `sector-5-0-6-7` (Ibiza) and 12.1 m/m on Hawai `sector-5-1-7-3`; the foam band's own UV animation changes
  **46 910 px (mean 11.7)** between two still-water frames 3 s apart, so it is alive.
  `docs/images/ocean-shore-surf-s70.png`.
- `OTDU_OCEAN_LOG=1` prints the area groups (`area-2-0: 68 items, 68 sector groups`, ...), the foam gradients and the
  live sea level. World viewer run 480 beats / FREERIDE runs 0 stalls; only the known Havok / flash warnings.
### Ruled out (with the test that ruled it out)
- "the retail foam is dead / not animated" -> with the water still, two frames 3 s apart differ by 46 910 px in the
  foam band (the `ecume_0.uva` animation is bound and running); the static look was the sea drawn over it.
- "the popping is z-fighting of the coplanar ocean levels" -> the near-camera water is unchanged between the new and
  old visibility; the band that changed is far beyond the sector ring, where the missing area tiles showed sky
  (`[255,255,218]`) instead of sea (`[16,25,34]`).
- "the lane error is just corner cutting" -> with the corrected right vector the worst error at the same density drops
  from 0.9-2.8 m to 0.20 m (Ibiza) / 0.08 m (Hawai).

### Not done / next
- **Hawai's retail water base**: `ocean_spec` 5.3 says the reflection plane is -1.8 m on Hawai (the port's mesh is
  y = 0 and the tide now oscillates around 0). The island name is in `EdenSceneView::initialize`; add
  `EdenOcean::setWaterBaseLevel( -1.8f )` there, use `base + waterLevel` in `renderReflection` and offset the ocean
  items by the base too.
- The foam runs on one gradient per strip (`EdenWorld::loadSector` `FoamTideVector`); a curved shore would need a
  per-vertex height field. The tide is a global plane, not a wave-by-wave run-up shape.
- Lane changes / overtaking and the retail traffic signal timing are still open (unchanged); sea life (`ocean_spec`
  11.3) unchanged.
- The area `-o` far objects now draw too (per group). No duplicate far prop was seen in the 1 200-beat Hawai run;
  keep an eye on long drives.

---

## Session 2026-11-01 (sixty-ninth, `cline`): the launcher — a real front end for the port (TDU2 artwork, `.iso` install, changelog updater, player name, online account, the watermark kept)

The owner: *"I Want you to make fully fledged launcher please it must use a nice GUI with TDU2 artwork point to a .iso
and it will extract/unpack the game I want a built in updater which shows a changelog also have a player's name which
is used for saves also could do with an online username section for when network stuff is implemented also in -glm
there is a temporary launcher you can use a build from also keep the watermark stuff thanks."*

The GLM tester launcher (`tools/launcher/main.cpp` at `c89b9ef`) was the starting point: its build stamp
(`cmake/BuildInfo.cmake` + `StampBuildInfo.cmake`), its watermark (`source/debug/watermark_overlay.*` + the
`tdu_instance.cpp` hooks) and its "find the game exe beside me" rule were brought into `cline` unchanged in intent.
Everything else on the exe is new.

### What changed

- **Build stamp** — `cmake/BuildInfo.cmake` / `cmake/StampBuildInfo.cmake` (build-time `git rev-list --count HEAD` +
  hash + dirty, `<build>/generated/build_info.h`), included from `CMakeLists.txt`. The game banner, the tester window
  title and the launcher all read `OTDU_BUILD_*`.
- **Watermark** — `source/debug/watermark_overlay.{h,cpp}` (top-right, `NoInputs`, translucent), wired into
  `tdu_instance.cpp`: `WatermarkOverlay::Initialize()` beside `CoordsOverlay::SetVisible`, `Draw()` beside
  `CoordsOverlay::Draw()`, and the tester name in the window title. `-tester <name>` draws
  `<name> · build N (hash)` + the compile timestamp; `-nowatermark` suppresses the draw and keeps the log line.
- **Player name now reaches the save** — after `LoadProfile()` in `tdu_instance.cpp`, `-playername` overwrites
  `GetPlayerProfile().Name` (so the launcher's Player page wins over an older `profile.sav`; `SaveProfile` writes it
  back as `name`). `-onlineusername` / `-onlineserver` log one line (`Online: account '...' (networking not
  implemented)`) and nothing else; the password never leaves `launcher.ini`.
- **`tools/launcher` — the GUI** (`otdu2_launcher`, WIN32, ImGui core + the Win32/DX11 backends compiled into the
  target, no `otdu_engine` link, 3.7 MB with the artwork embedded):
  - `main.cpp` — Win32 + D3D11 host, DPI awareness, work-area clamp, `--page`, and the headless `--selftest` /
    `--iso-info` / `--check-update` / `--launch-test` modes.
  - `launcher_ui.cpp` — sidebar (Play / Game data / Updates / Player / Online, watermark switch, build stamp) and the
    five pages: hero artwork + PLAY + launch options; the `.iso` picker/inspect/extract with progress and cancel; the
    updates page with changelog; the player page (name, the `profile.sav` it found, saves/mods folders); the online
    account. Fonts are Segoe UI baked at the monitor DPI and every layout constant goes through `S()`
    (`gUiScale`), with `ImGuiStyle::ScaleAllSizes` — the 225 % 4K desktop that first showed the bug now lays out
    correctly.
  - `iso_archive.{h,cpp}` — a read-only **ISO 9660 + Joliet (+ Rock Ridge `NM`, multi-extent files)** and **plain
    UDF 2.01-2.60** reader (anchor, VDS, partition + LVD, FSD, FE/EFE, short/long/embedded ADs, FID walk). UDF with
    a **metadata partition** (2.50, Blu-ray) and `extended_ad` are refused with a named reason, not guessed at.
  - `game_installer.{h,cpp}` — `FindGameRoot` (a folder containing `Euro/Bnk`, any depth; `bigfile_*.map` = the
    packed retail layout, named and refused; installer discs recognised), free-space check, per-file copy with
    progress/cancel and a size-based resume.
  - `launcher_updater.{h,cpp}` + `launcher_http.*` + `launcher_json.*` — git checkout mode (`fetch origin`,
    `log HEAD..origin/<branch>` as the changelog, `merge --ff-only` when on that branch and clean) and packaged mode
    (GitHub releases / commits, release notes as the changelog, `.zip` asset → `tar.exe` staging →
    `apply-update.cmd` that waits for the launcher's PID, backs the exe up and restarts it).
  - `launcher_settings.*` — `launcher.ini` beside the exe, rewritten as **UTF-16 with a BOM** so a non-ASCII player
    name and a non-Latin path survive (the Win32 profile API keeps the file's encoding).
  - `launcher_artwork.*` + `launcher.rc.in` + `artwork/otdu2.ico` — six of the repo's own gameplay captures embedded
    as RCDATA and decoded through WIC into D3D11 textures; the icon is generated from the same Ibiza art.
  - `launcher_util.*` — UTF-8/UTF-16, paths, the game-exe probes (`opentdu2.exe` / `opentdu2-<workspace>.exe` beside
    the launcher, then `..\build-*\bin`), `CreateProcess` with captured output, and the **one** function that builds
    the game's command line (used by PLAY and by the self test so they cannot drift).
- `tools/CMakeLists.txt` — the `otdu2_launcher` target, `configure_file(launcher.rc.in)` with absolute artwork paths,
  the ImGui sources/backends, `windowscodecs`/`winhttp`/`d3d11`/`d3dcompiler` links, `build_info` dependency.
- `README.md` — a **Launcher (Windows)** section (pages, ISO reader scope, updater modes, identity, headless modes,
  the screenshot), two CLI rows (`-tester`/`-nowatermark`, `-onlineusername`/`-onlineserver`), the `-playername` row
  updated, the tools tree. `docs/re/launcher.md` — the design/behaviour reference (files, pages, ISO limits, updater
  apply paths, `launcher.ini` keys, gotchas).


### Verified

- `build-cline.ps1 -Reconfigure` exit 0; `build-cline\bin\otdu2_launcher.exe` (3,740,160 bytes) and the game exe
  rebuilt with the new stamp.
- `--selftest` **PASS**: UTF-8 round trip, JSON (`\u` escape), `launcher.ini` round trip with `Ø`/`✓`, the `Euro\Bnk`
  rule, and the PLAY command line with/without the watermark.
- **ISO, real data**: `--iso-info` on `C:\TDU2-PS3-Recomp\iso\TDU2_decrypted.iso` (9,642,967,040 B, UDF 2.50 +
  ISO 9660) — `ISO 9660 + Joliet`, label `TDU2`, 35 entries / 9.0 GB, game root `PS3_GAME/USRDIR`, read
  `PS3_GAME/PARAM.SFO` (1,312 B) back; **SHA-256 identical to 7-Zip's extraction**
  (`46B21C0DFD1D9D10DB54B2D10B086E4873EF46459B697C26D664767BB405CB88`).
- **Updater, git mode**: `--check-update` → `Git checkout`, 22 commits on `origin/main` with subjects, `available:
  yes`, `can apply: no` and the correct reason (uncommitted changes in this tree).
- **End to end**: `--launch-test` started `opentdu2-cline.exe` with exactly the launcher's arguments (`-game`,
  `-playername Daniel`, `-tester Daniel`, `-onlineusername Daniel`, `-onlineserver ...`), exit 0, and the game log
  holds `Tester: 'Daniel' (watermark on)` and `Online: account 'Daniel', server play.opentdu2.net:4000`.
- **Watermark render A/B** (`-fixedtimestep`, frame 120, 3840x2160): `-tester Daniel` vs `-tester Daniel
  -nowatermark` — a control region is bit-identical (mean diff 0.0) while the top-right differs in exactly the
  watermark panel's box (crop bbox 231,27 → 593,130); the log keeps `Tester:` with the watermark off.
- **UI**: screenshots of all five pages (Play: `docs/images/launcher_s69.png`); the 225 % DPI layout was fixed after
  the first capture (text overflowed its boxes) and re-captured.

### Not done / next

- **Packed retail**: `bigfile_*.map` + `TDU2v*.big` unpacking is not implemented — the page names the layout and
  stops (`Euro/Bnk` is the supported layout).
- **UDF 2.50 metadata partition** (PS3/Blu-ray UDF tree) and **installer-cab** extraction are out of scope for this
  reader; hybrid discs still work through ISO 9660.
- The packaged-release **apply** path cannot run end to end until a GitHub release with a `.zip` asset exists; its
  parts (WinHTTP download, `tar.exe` staging, the apply script) are in place, and the download/staging path is what
  the self test exercises.
- Networking is still storage-only (Online page → `-onlineusername`/`-onlineserver` → one log line).
- Packaging a tester zip (launcher + game exe) is still manual.


## Session 2026-11-01 (sixty-eighth, `cline`): the sea moves (a travelling swell) and the water is see-through, ready for sea life

The owner: *"please can we enhance the sea etc... water source doesn't seem to be moving when I just tried it also could
we improve the quality of the water to be a little more transparent I want to eventually add some sea life to the world
I believe the original lacked specially for such destinations of ibiza and hawaii?"*

Spec: **`docs/re/ocean_spec.md` §11** (the two enhancements, the numbers, and the fish assets the game does have).

### What changed
- `source/render/eden/eden_ocean.{h,cpp}` — **travelling swell**. The retail surface only morphs its noise volume
  (§3.4: one slice per second, 32 s loop) while the wave UV stays fixed at the world position, so at mid-day the sea
  reads as a still sheet. The vertex shader already multiplies a world-space offset into the wave UV (`ShiftWorld`, the
  floating origin, always 0 in retail and unused in this port), so the port puts a wind drift there
  (`waveSpeed` 1.5 m/s at `waveAngle` 35°, wrapped to one low-res noise tile = 66.7 m, exactly ten high-res tiles, so
  both octaves stay continuous). `-nowavedrift` / `-wavespeed <m/s>` / `-wavedir <deg>`.
- `source/render/eden/eden_ocean.cpp` — **water clarity**. `ps_0` always writes alpha 1, so `SRC_ALPHA` cannot make the
  sea see-through; the material now blends with the D3D9 constant blend factor (`SrcBlend 14` / `DstBlend 15`,
  `EdenMaterial::BlendFactor` → `VkPipelineColorBlendState::blendConstants`) and drops the depth write, which moves the
  water out of the opaque group into the distance-sorted blended flush (after the sea bed).
- `source/render/eden/eden_renderer.{h,cpp}` — `EdenMaterial::BlendFactor` + `blendFactorByte()` (quantized into the
  pipeline cache key, `PipelineKey::Words[5]` bits 16-23), `GetBlendFactor` cases 14 / 15.
- `source/game/game_options.{h,cpp}`, `source/game/advanced_graphics.cpp` — `water_clarity` 0..20 (0 = the opaque retail
  water, 20 = alpha 0.5) default 4, an **Advanced settings** row ("Water clarity"), saved in `options.ini`;
  `-waterclear <0..20>` overrides it for a run, `-nowaterclarity` gives the retail water for an A/B.
- `source/render/eden/eden_scene_view.cpp` — `-showseanmap`: blit the ocean's wave normal map (NoiseNMap 256²), the
  twin of `-showsearefl`.
- `tools/sob_tool/main.cpp` — `-members`: print the constant members `EdenRenderer::fillConstants` matches against
  (that is how the `matParam__array2` / `matParam__array4` / `ShiftWorld` binding of the noise + ocean shaders was
  confirmed).
- `README.md` — the two new option rows.

### Verified (headless, screenshots + measured pixels)
- The noise pipeline itself was **fine**: `-showseanmap` at 1280x720, frames 300 vs 1500 -> **289 826 px above 8/255**;
  the 256² NoiseNMap is a real, animated wave normal map. `OTDU_OCEAN_LOG=1` prints the constants every TD_Ocean kind
  receives and they are the shipped weather's (`NMapLR (0.015 1.000) NMapHR (0.150 1.000) specPower 20.0 sun 0.913
  fresnel (0.300 30.000 0.500)`, colour maps `sector-3-3-5-6-c` / `area-2-0-c` / `ibiza-c`).
- **Swell**: World viewer at `-campos 22450,6,23500 -camyaw 90 -campitch -12` (over the Ibiza bay; the sea there comes
  from `-dumpheights`, sector-3-3-5-6: z 23217..23808, x 22305..23040), 2560x1440, same frame: drift vs `-nowavedrift`
  -> **747 070 px above 8/255 (52 % of the water region), mean 13.96/255**. Two frames 2 s apart in one run: drift
  14.12, still 13.56 (the retail morph alone is subtle; the drift is what reads as motion).
- **Clarity**: same camera and frame, `-waterclear 0` vs `-waterclear 20` -> **1 183 604 px (82 %), mean 27.11/255**, and
  the water gets *darker* (95,105,65 -> 84,93,59): it blends with the sea bed behind it, not with the sky.
- FREERIDE 220 frames: no new warnings, 0 stalls (the Havok / flash / hitch lines are the pre-existing ones).

### Ruled out (with the test that ruled it out)
- "the wave normal map is broken / not regenerating" -> `-showseanmap` shows it changing every frame, and
  `td_noisecompose` / `td_noiseassembly` / `TD_ocean_nmap_from_hmap` all load (`EdenOcean: compose td_noisecompose
  vs0/ps0, assemble ...` in the verbose log).
- "the ocean constants are not bound" -> `-members` on `TD_Ocean.sob` shows `ShiftWorld` / `matParam` /
  `reflMat__float4x4` as real CTAB members, and `OTDU_OCEAN_LOG=1` logs the weather's own values into them.
- "the water is opaque because of `SrcBlend 2 / DstBlend 1`" -> the pipeline log line
  `Eden pipeline TD_Ocean vs2/ps0 (Ocean): ... blend 2/1 op 1` is `ONE / ZERO`, i.e. opaque; the new path asks for 14/15.
- A same-camera frame pair can be **bit identical** (max 0) when nothing in view animates (open sea at 60 m, weather
  14): the far water is dominated by the static colour map + a 256² reflection, so the retail animation is invisible
  there - that is the owner's "it doesn't move".

### Not done / next
- **Sea life exists in the data** (`docs/re/ocean_spec.md` §11.3): `Euro/Bnk/interior/gph_l3a.bnk` holds
  `fish_01/02/04/05` + `skinned_fish_03` (3DD/3DG/2DM/XSB), the swim cycles `fish_slow/medium/fast.anm`, the atlas
  `poissons_cmap.2db`, the kelp art `algues_hautes` / `algues_oedogonium`, and the retail aquarium cinematic
  `gph_l3a_sc_M_LIFE_FISH.cin`. The next step is `source/game/freeride/sea_life.{h,cpp}` modelled on `birds.cpp`, but
  drawing the fish as **skinned meshes** (`S_USESKINNING`) below the water plane (0 m Ibiza, **-1.8 m Hawai**, §5.3) and
  letting the new clarity show them.
- The clarity is a *uniform* alpha (no depth falloff): at the default 4 the horizon keeps 10 % of the sky. A depth-based
  fade needs our own ocean shader (the retail `ps_0` has no depth term) - the follow-up if 4K looks washed out.
- The drift uses `ShiftWorld`, which retail reserves for the floating origin; if the port ever adds one, the drift must
  move to a dedicated constant (`matParam.w` is free) or be added to the origin.

---

## Session 2026-11-01 (sixty-seventh, `cline`): aeroplanes that land, roll and take off, and flocks of birds in the sky

The owner: *"I want the world to be more alive could we add airplane logic and random birds etc... flying in the
skies and stuff the assets should be somewhere best check the asset tool I want planes to be flying into the airport
landing and taking off and stuff and if airport has terminals have a airplane parked up to a terminal so when we add
the airport it looks more realistic as I believe there may be cutscenes"*.

Spec: **`docs/re/aircraft_birds_spec.md`** (the data, the flight law, the three traps, the verification numbers, the
six next steps). This entry is the summary.

### What changed
- **`source/game/freeride/aircraft.{h,cpp}`** (new) - `Aircraft`: the island's four `Entity__AircraftPath` loops of
  `scenarics/packs/world_aircrafts.bnk`, resampled (straight on a runway segment, Catmull-Rom in the air), each flown
  by the library aircraft `aircrafts.bnk` names (`FU_AIR_B` 14 m private jet, `FU_AIR_C` 68 m airliner - both
  instance nodes of `commonworld.bnk`'s `library.3dd`). A plane runs at `SpeedOnGround` on a segment that joins two
  ground points and `SpeedOnAir` elsewhere, so each loop reads as **landing -> runway roll -> take-off**; heading from
  a 45 m look-ahead, pitch from the climb, bank from `tan(bank) = v yawRate / g`.
- **`source/game/freeride/birds.{h,cpp}`** (new) - `Birds`: the five sprites of `fx/details/birds.bnk` (noir, faucon,
  mouette, pigeon, moineau) flown as five wandering flocks of 5..9, camera-facing quads through the port's own
  `resources/shaders/bird.vert` / `bird.frag` (+ their generated `source/render/shaders/builtin/bird_*.h`), in the
  scene's `ePass::Effects`. Heavy rain grounds them.
- **`source/world/eden_world.{h,cpp}`** - `findLibraryNode`, `getLibraryNodeBounds`, `drawLibraryNode`: the shared
  library scene as a drawable object by hashcode (the aircraft use it, nothing is uploaded per plane).
- **`source/game/freeride/gm_freeride.{h,cpp}`** - owns both, ticks them with the traffic, draws the aircraft in the
  scene passes and the birds in the effects pass; `-noaircraft` / `-logaircraft` / `-aircraftpark` /
  `-aircraftspeed <x>` and `-nobirds` / `-logbirds` / `-birdsnear` / `-birdsfront`.
- **`README.md`** - the two new flag rows.

### Verified (headless, screenshots committed)
- `Aircraft: type 0 'FU_Air_B' -> library node 617` / `type 1 'FU_Air_C' -> library node 609`;
  `Path01 point 0 ... terrain 20.1 -> ground`, `Path02 point 0 ... -> air`;
  `Aircraft: ibiza: 4 routes, 2 aircraft types, 11 planes (1 on a runway), view 9000 m`.
- `-aircraftspeed 40`, 12 s:
  `Aircraft: 11 planes on 4 routes, 2 on the ground, 5 take-off / landing since the last line` - the loops really
  land and take off.
- `-aircraftpark` vs `-aircraftpark -noaircraft`, same frame: **mean pixel difference 30.0, 237,667 pixels above
  8/255**; `docs/images/aircraft_parked_terminal.png` is the 747 Paradise Airline airliner standing on the Ibiza
  apron in front of the car.
- The airport bank's own **parked plane already worked**: `SpotExterior: eair_a.bnk: 2 set models, 1 props` ->
  `docs/images/airport_apron_parked_planes.png` (two airliners at the terminal, `-noaircraft`, so it is the bank's
  `CF_Plane` prop, not the new system).
- `Birds: ibiza: 5 species ... in 5 flocks`, `Birds: 35 drawn of 35`; `-birdsfront` vs `-nobirds`, same frame:
  **5302 pixels above 8/255 in the sky** (a block map of the difference shows isolated birds, not a rectangle);
  `docs/images/birds_sky.png`.
- Frame cost unchanged (`[perf] frame 120: avg 18.3 ms` with and without).

### Ruled out (with the test that ruled it out)
- `-birdsscreen` (a temporary 3 x 3 m sprite 5 m down the view axis) drew a huge bird at the centre: the pipeline,
  the texture set and the shader were fine, so "no birds visible" was **placement**, not rendering.
- `params.CameraDirection` and `EdenRotationFromQuaternion(camera.Rotation) * (0,0,1)` both disagree with the drawn
  view direction (debug log: view `(0, -0.07, 1)` while the flocks went `+X`). The look direction now comes from
  `-row 2` of `params.View`, recorded in `Birds::draw` for the next tick.
- A pipeline created with the default `RenderPass` never appears in the `Effects` pass: `desc.RenderPass` must be
  `device.getCurrentRenderPass()` (the pass draws into the scene render target).

### Not done / next
- **The airport cinematics exist and are unused**: `eair_a.bnk` has `eair_a_sc_M_LANDED` / `_M_TAKE_OFF` /
  `_M_INTRO_E_1` / `_M_INTRO_E_2` `.cin` + `.sce`, `cf_plane_takeoff.anm` / `cf_landed.anm` and the two
  `m_*_CINEMATIC.wav`. Point `CINVIEWER` (`gm_cin_viewer.cpp`) at this bank next - that is the owner's "there may be
  cutscenes", and it is the retail airport flow.
- The engine sounds (`boeing_747_idle.wav` / `_in_flight.wav`, named by `SoundOnGround` / `SoundOnAir`) are unplayed.
- `EdenSurfaceFx` still creates its pipeline with the default render pass - check whether its quads reach the scene
  target at all (the same one-line fix as the birds).

### The owner's "the updater was fine but launch bats run an old build" (same session)

Not a bug in the game: `E:\OpenTDU2-main`'s `update-and-rebuild.bat` merged `origin/main` and `origin/glm`, then
**stopped on the `origin/cline` merge with three real conflicts** (`source/world/eden_world.cpp`,
`source/game/freeride/gm_freeride.cpp`, `source/game/freeride/freeride_hud.cpp`) - and its build step is *after* the
merges, so it never ran: the exe stayed at 09:06 while the sources were merged at 13:43. The merge base was
`5df65cf`, i.e. the hub had never merged sessions 64-67, so cline dragged four sessions in at once.

**Resolved in the hub tree** (merge `e0fda68`, then `build-main\bin\opentdu2-main.exe` 13:53, 74 files rebuilt,
exit 0) with these decisions - the same ones a future merge will need:
- `eden_world.cpp`: **union** - glm's `getGroundHeight` (tunnel carve ground) *and* the cline
  `findLibraryNode` / `getLibraryNodeBounds` / `drawLibraryNode` are different functions at the same spot.
- `freeride_hud.cpp`: **HEAD (hub)** - glm's session 62 made the satnav the retail screen and deleted the native
  outline + GPS bar; the cline side only reformatted that bar (`FormatDistance`), so the deletion wins.
- `gm_freeride.cpp` weather: **cline** - `weatherCycle.load()` replaces the old -weather / -night / nearest-to-the-PC
  -clock picking and handles those flags itself.
- `gm_freeride.cpp` pause clock: **hub** - glm removed `PauseMenu::setClock` (`refreshClock` reads the PC clock), so
  the *two* other cline `setClock` calls (the cheats weather pick and the weather tick) were removed too, and the
  cycle's clock is logged instead. **The pause screen therefore shows the PC time; the weather cycle's clock is the
  lighting clock** - they agree unless `-clocktime` / `-clockrate` is used. Re-unifying them (a `setClock` the pause
  screen honours when the cycle is pinned) is the open follow-up.

Tools added so this cannot look "fine" again: **`check-build.ps1`** (repo root) reports the merge state, which exe a
launch would run and whether it is older than the newest source; **`build.ps1`** now prints the exe path, timestamp and
size, and warns `STALE BUILD` when a source is newer. `WINDOWS_SETUP.md` has the two causes (and `E:\OpenTDU2`, the
old `win` hub, has its own unfinished merge and a 10/31 exe - launching from *that* folder is the second way to get an
old build).

---

## Session 2026-11-01 (sixty-sixth, `cline`): wheel spray, tyre-track decals and a ground that gets wet and dries — the particle layer the plans kept naming as missing

The owner: *"I was wondering if we can improve the water, sand, dirt etc... I believe it's just mostly a
texture is it possible we can have a particle system made and we can leave decal trails etc... I was hoping
when driving on sand, mud it would splash up a little bit under the wheels and make it more like a
material so it can potentially be affected by rain and sun so mud would dry up and get wet?"*

Spec: **`docs/re/surface_effects_spec.md`** (the data hooks, the law, what is verified, the six next
steps). This entry is the summary.

### What changed
- `source/render/eden/eden_surface_fx.{h,cpp}` (new) — `EdenSurfaceFx`: a 3000-slot CPU particle pool of
  camera-facing quads (gravity, drag, growth, quadratic alpha fade) and a 1024-slot ring buffer of ground
  quads for the tyre tracks, both drawn depth tested / no depth write / alpha blended. Plus
  `eGroundMaterial` (tarmac, dirt, sand, grass, snow) and `GroundWetness`: rain soaks the ground, the sun
  dries it, dirt is the slowest to dry (mud) and the fastest to hold water.
- `resources/shaders/surfacefx.vert`, `surfacefx.frag`, `surfacedecal.frag` (new) + their generated
  `source/render/shaders/builtin/*.h` (`python tools/compile_shaders.py surfacefx.vert surfacefx.frag
  surfacedecal.frag`).
- `source/render/gs_render.{h,cpp}` — owns the system, `getSurfaceFx()`.
- `source/render/eden/eden_scene_view.{h,cpp}` — new `ePass::Effects`, called inside the blended pass
  after `flushDeferred()` and before the haze; off unless `setEffectsPass(true)` (only FREERIDE feeds the
  effects, and the other owners draw their actors in every pass).
- `source/game/freeride/gm_freeride.{h,cpp}` — `tickSurfaceFx()`: the weather preset's
  `rain_ground_global_intensity` / `sun_diffuse` drive the wetness, every wheel's contact point / normal /
  slip / surface drives the emission, `ClassifyGround()` turns the car data's tyre surface (+ the beach
  height test) into the ground material. The `Effects` pass draws it.
- `README.md` — the new flags in the CLI table.

### Verified (all headless, screenshots committed)
- `EdenSurfaceFx: ready (3000 particles, 1024 decals)` (`-logsurfacefx`).
- Splashes under the wheels: `SurfaceFx: 193 particles`, `docs/images/surface_fx_splash.png`
  (`-surfacefxtest`).
- Tracks behind the car: `SurfaceFx: 183 particles, 543 decals, ground dirt`,
  `docs/images/surface_fx_tracks.png`.
- A dry road throws nothing and a slipping tyre still leaves a skid: `0 particles, 8 decals, ground
  tarmac` on the plaza.
- Rain soaks: `-weather 14 -intempery 3` took tarmac `0.09 -> 0.83`, dirt `0.03 -> 0.54`, sand `0.76`.
- A dry sky dries it, mud last: `-surfacefxwet 100` took tarmac `1.00 -> 0.66`, dirt `1.00 -> 0.92 (mud)`,
  sand `0.75`, snow `0.95`.

### Ruled out / not done
- The **terrain shader does not see the wetness yet**: wet sand and dark mud *on the ground itself* need
  the `TD_HeightMap` permutation to take a `Wetness` parameter (the `RainSpecs` path is the model).
  `GroundWetness` is the value to feed it.
- **Rain drops in the air** are still not drawn — but the particle layer now exists, so drops are a second
  emitter on the same pool (half a session).
- The beach test is a height heuristic (`< 2.5 m` of terrain): if a beach sprays green, the next step is
  the sector colour map averaged at load (`FindColorMap`). `ClassifyGround()` is the one place to change.
- The AI cars (`traffic.cpp`) leave nothing; they need the same `emitWheels` calls from their own state.
- **Not a retail behaviour**: the original has no particle layer (`.prt` not parsed) and no per-material
  wetness, so every rate and colour here is the port's `[INF]`, documented and in one table each.

---



The owner: *"cars don't actually project light yet at night and street lights seem to also stay on during
the day but with street lights I believe there maybe 2 sources per 1 light … the california in 1 camera view
a lot of it's detail seems wrong or low poly but when you switch to in car cam it seems the true detail …
in car view glass looks dirty/dark and it's hard to see out … the cam doesn't sit correctly behind the car
and when driving up hills the cam seems to get lower"*.

**Street lamps**: `-logmaterials` names them — the head is a **TD_Lamp** unlit quad (`SF_Light_Lamp`,
`SF_Instc_B_Ligh`, `SF_Instc_B_Patc`, guid `c913741cbe17490d`, texture `sf_light_c`) and there is a second
additive `FX_LIGHT#1` sprite (TD_Sprite, `light_fx`) at the same post. Both glow at any hour: the retail
lamp manager (`fx/streetlamps.xml` lights, on from dusk) has no port equivalent. `EdenRenderer::bNightLights`
(new) is fed by `EdenSceneView::drawObjects` from the weather hour (lit when `Hour < 6 || >= 20`, the car
headlights' rule); TD_Lamp is skipped by day and the `FX_LIGHT` sprite is skipped always (`-lampflares` back,
`-lampday` keeps the lamps lit). Verified headless: day logs all three glows + the sprite skipped, night only
the sprite.

**Chase camera**: the eye height ran along world up, so on a slope the body pitched above a camera that
stayed vertically 1.5 m over the car origin. It now runs along the car's own up axis, followed with the
heading's time constant (and re-seeded on `reset`); `-logcameras` prints `eye above car / ground / up` once
a second (flat ground: 1.50 m above the car, 1.69 m of ground clearance, up (0,1,0)). The offsets are the
car's data (`camerasingame.xmb` mode 20: pos -6.45 / 1.50, target 1.50 / 0.91).

**Cockpit glass**: the exterior `Glass` carries `DIRTCOLOR DIRT_SCRATCH` (the streaks) and the black `G_I`
base — retail swaps in the interior bank's `Glass_I` (TD_CarGlassDirt) inside the car, which the port does
not instance. In the inside views (the existing `< 5 m` LOD test) the windscreen + side glass nodes
(`GLASS_F`, `WND_L1`, `WND_R1`; 3 found at load) are now hidden, so the driver sees out; `-keepglass` for
the A/B. Verified: `CinematicCar: glass nodes 3`, mode 23 `camera 1.13 m, LOD 4`.

**Not fixed, with the reason**: the car's **headlight / brake light pools** are `TD_ShadowBox` projected
boxes whose shader the port skips (`docs/re/car_lights_spec.md` 2.4) — the projection constant block has to
be written in the renderer first. And the California's **"low poly" chase view** is not an LOD bug: the port
draws `BODY_HR` (the high body) and hides `BODY_MR`/`BODY_LR` exactly like retail LOD 3; what reads as lost
detail is the black glass hiding the cabin (`-carlod 4` shows the full interior, `-keepglass` the glass art).
Both are written up in **`docs/re/night_lights_camera_glass_spec.md`** §4-5 as the next steps.

### Verified
- `build-cline.ps1` → **build exit 0** (3 incremental builds).
- Day / night lamp logs (above), `CinematicCar: glass nodes 3 (GLASS_F / WND_L1 / R1)`, `CinematicCar:
  camera 1.13 m, LOD 4` for mode 23, `ChaseCamera: mode 20 eye above car 1.50 m, ground 1.69, up (0.00
  1.00 -0.00)`.
- No screenshot A/B this session beyond the cockpit/chase shots taken for the diagnosis (the image reader
  stopped taking files mid-session); the owner should eyeball the lamps at `-weather 14` vs `-weather 21.35`.

### Next
- `TD_ShadowBox` projection constants → the headlight pools (renderer session).
- Instance `Glass_I` and swap it like `FUN_00c53d10` instead of hiding the glass.
- The `?A_` lamp glow meshes of every car are hidden by the LOD code: showing them at night is a cheap
  stand-in for the missing light pools.

---

## Session 2026-11-01 (sixty-fourth, `cline`): the weather finally moves (cloud, rain, wet ground), the readouts speak mph as well as km/h, and the odometer counts the road

The owner: *"could we implement the Imperial and metric system for cars kmph and mph also I think the
miles counter is un accurate … I asked about clouds and weather and stuff but never seen a cloud or it
rain … could it date check and in december we could maybe add new objects and snow … fireworks on 5th
november"*.

**The weather root cause is a data read, not the renderer**: `islands/ibiza/fx/game_weathers.xmb` has
24 times of day × **7 intemperies** each — 0 is a 16-parameter base block (tunnel / headlight values
only), 1-2 are the dry sets, **3 / 5 / 6 are the rain sets** (`rain_ground_global_intensity 1`,
`_wet 0.6-0.7`, `_puddle_height 0.62`, `rain_splash_max`, `rain_droplets_max`, their own
`clouds_cloud3dname`), 4 is the drying one — and `eden_weather.cpp` loaded **only intemperies 0 and 1**
(`j < numIntemperies && j < 2`). Every consumer (`EdenWorld::updateWetGround`, `drawClouds`, the sky /
fog / sun constants) was already wired to the preset; there was simply never a rainy preset in it.

### What changed
- **`render/eden/eden_weather.{h,cpp}`**: the loader keeps every intempery (`variants`, with
  `findVariant(name, n)` / `getIntemperyCount(name)`); `presets` still holds the old merge (base + set 1)
  so the title screen, the world viewer and the test drive look exactly as before.
- **`game/freeride/weather_cycle.{h,cpp}`** (new): the in-game clock (machine local time at start,
  `-clocktime HH:MM`, `-clockrate x`, 1 game s per real s by default) picks the `game_weathers` entry
  nearest below it (sun / ambient / fog / clouds follow), and a small state machine walks the
  intemperies — dry 8-16 game min, cloud 4-9, rain / storm 2-6, rain decaying through cloud
  (`[INF]`: the retail scheduler is not decoded). `-weather <name>` / `-night` / `-intempery n` pin one,
  the cheats panel's weather combo pins too. The pause clock follows the cycle.
- **`game/units.{h,cpp}`** (new): `IsImperialUnits` / `SpeedForDisplay` / `KmForDisplay` /
  `FormatDistance`. `GameOptions::Units` (0 metric, 1 imperial) is saved in `options.ini` (`units`),
  `-imperial` / `-metric` override it for a run, the cheats panel (F4) row **mph / miles** flips and saves
  it. Applied to the GAUGES movie (speed digits, `/:SpeedUnit` — which the port never set — and
  `/:Km` / `/:Centaine` in miles), the port's placeholder gauge (digits + unit + odometer), the GPS and
  detector lines (`850 m` / `12.3 km` / `2,800 ft` / `7.6 mi`) and the fuel range.
- **`physics/vehicle_dynamics.cpp`**: the odometer was `|velocity| * h` — it counted the vertical bounce
  of a bump, a spin in place, the body's motion in mid air and a chassis depenetration push. It now
  accumulates the **horizontal position delta** of the tick (a wheel-based counter's measure) with a
  5 m-per-tick guard so a teleport / reset cannot book kilometres. The DRIVE log now prints `odo`.
- **`eden_world.cpp`**: one line when the wet ground first comes on (`EdenWorld: wet ground on
  (intensity …)`), so a headless run can tell the rain state reached the renderer.
- **`docs/re/weather_clock_units_spec.md`** (new): the data table, the cycle, the unit map, the odometer
  measure, and the honest plan for snow / Christmas / fireworks (see below).

### Verified
- `build-cline.ps1` → **build exit 0** (4 incremental builds; only pre-existing warnings).
- Pinned rain (`-weather 14 -intempery 3`): `WeatherCycle: pinned '14' intempery 3 at 14:00`,
  `EdenWorld: wet ground on (intensity 1.00, wet 0.60, puddle 0.62)`, no `no rain permutation` warning;
  the screenshot `_rain.png` (scratch) shows the overcast sky and the wet reflective road in
  Bienvenidos, with the placeholder gauge reading `000` over `km/h`.
- Running clock (`-clockrate 60`): `WeatherCycle: clock 00:00, weather '00' intempery 1, rate 60.00`
  then `weather state 1 -> …` and `FREERIDE: clock 00:0x, weather '00' intempery 1 (rain 0.00)` as the
  states move; `-imperial` → `FREERIDE: units imperial (mph / mi)`.
- Odometer: a scripted `-autoplay "200:hold=accel;900:release=accel"` drive went from
  `pos (36008.46, 5814.22)` to `(35927.29, 5785.19)` = **86.2 m straight line**, odometer **88.3 m**
  (the small arc), while the first seconds — the car standing still with the body bouncing
  `y 5.54 → 5.51` — kept `odo 0.0 m` (the old measure added that bounce).

### Next
- Falling **rain drops** still are not drawn (the `rain_droplets_*` / `rain_splash_*` shader work and a
  particle layer); the rain states give the wet road, the puddles, the ripples and the darker sky.
- **Snow / Christmas / fireworks** (the owner's other ask): the calendar and the scheduling are cheap,
  the visible parts are three pieces of engine work that do not exist — a particle layer (fireworks,
  rain drops), a snow ground cover (the wet-ground pattern in the shaders) and tyre-track decals; the
  port also has no port-native prop mesh for decorations (README "Modding"). Phased plan in
  `docs/re/weather_clock_units_spec.md` §4.
- The odometer is still per session: the retail profile's per-car `OwnedCar::Km` is where it belongs
  once the driven car is mapped to its garage record.

---

## Session 2026-11-01 (sixty-third, `cline`): the session handoff is one file per agent, and the update script can no longer wedge

The owner: *"Is some of these change conflicts happen to be from .md files by chance? If so what if we
made seperate docs ... each agent get there own .md and anything else to prevent these issue
reoccuring as it is tending to hinder the through put of the project as I'm trying to test"*.

**Measured first** (`git rev-list --merges` over the last 38 merges of both trees: the files both sides
of a merge changed = the conflict risk set): `docs/STATE.md` **32 / 38** (84 %), `README.md` 11 / 38,
`source/game/freeride/gm_freeride.cpp` 21 / 38, `traffic.cpp` 5 / 38 — and the conflict that stopped the
script was `docs/STATE.md` (+ `README.md` for the cline merge); **no code conflict**. So the recurring
pain was documentation only: every session *prepends* to the one handoff file.

### What changed
- **`docs/state/main.md`, `docs/state/glm.md`, `docs/state/cline.md`** — one handoff shard per agent,
  single writer per file. `docs/STATE.md` keeps the deep history (up to session 62) and the shared
  `## Where we are` block, and is **frozen**: it says so at the top, with the read order
  (CLAUDE.md → your shard → the other two → STATE.md).
- `CLAUDE.md` — reading order, the "write it down" rule and the session-entry template now point at the
  shard of the branch you are on; the union/rerere note is in §4.
- `.gitattributes` — `docs/state/*.md merge=union` as a safety net (an accidental cross-edit still merges
  instead of stopping the owner's build; it is not a licence to edit another agent's shard).
- `.gitattributes` — `docs/STATE.md`, `docs/SURVEY.md`, `README.md` stay `merge=union` (session 62).

### Verified
Their tree (`E:\OpenTDU2-main`): the wedged `origin/glm` merge was aborted, all three branches merged
with 0 conflicts, `build exit: 0`, `SUCCESS: OpenTDU2-main is up to date and rebuilt`; a second run
merged the follow-up commits live (`Auto-merging CLAUDE.md / docs/STATE.md`, `ninja: no work to do`).
A throwaway 3-branch repo (bare origin + main/glm/cline + a deliberately wedged merge) showed the
script aborting the stale merge, unioning the logs with both entries, and still stopping on a real
content conflict with the file named.

### Next
- The three shards start empty: the next session on each branch prepends its entry there.
- Optional, not done: split the README CLI table per subsystem (`docs/cli/*.md`) — it is the last
  co-edited documentation hotspot (11 / 38) and is covered by union for now.


# Session handoff — `main` (the one tree)

One directory, one repo, one agent at a time (`WORKSPACES.md`): **every agent
appends its entry at the top of this file**, newest first, in the

## Session 2026-11-21 (`main`, Veyron rims: checked, no defect reproduced)

**Root cause found (owner's reference screenshot):** the car model's `WLR_xx` nodes (material `WLR_FR_01`, alpha-tested flat disc of the tyre's radius, textured with the LOD wheel picture) are the *low-resolution wheel* of the far LODs. The LOD0 draw never hid them, so a grey disc sat over the real rim (Veyron SS: orange ring + stubs only; the Grand Sport had the same disc, its alpha just let more through). `cinematic_car.cpp` now hides `WLR_*` nodes with the other LOD duplicates (`BODY_MR/LR`, `HARDTOP_*`); `-keepwlr` = old. Verified `docs/images/veyron-wlr-rims-fixed.webp`: SS shows the 10-fork... multi-spoke orange wheel with Bugatti hub badge, drilled rotor and BUGATTI caliper; the Grand Sport its own white/silver wheel. Ruled out first: rotor depth (`-rimx` shifts only moved the rim), tyre disc `T_F_01` (`-notire` left the grey disc), kept-geometry spoke/rotor depth analysis. New diagnostics: `-logrimmat` (rim items/materials, tyre items, brake/wheel items, DBRims row), `-photoatrel`. Only cars with `WLR_` nodes are affected (Veyrons found; California has none).

**Correction (owner):** my test cars were wrong, not the dealer. `-car` kept the California's rims (code comment said so), so every shot above showed the wrong wheels. Fixed: `-car` now takes the car's `_default_rims` DBRims row (log `-car 'X' takes its database rims ...`; `-oldcarrims` = old). Veyron Super Sport now draws the orange thin-spoke rim (`bugatti/veyron_super_sport_f_01`). The dealer/drive path already used the DB rims. The rim-defect check above is void for the Super Sport; re-check the face between spokes (dark disc visible) against retail.

Owner list item "odd rim issue with Bugatti Super Sport, possibly other cars": **not reproduced, nothing changed in the rim code**. The owner's screenshot text was lost to a context cut, so the exact oddity is unknown.

### What changed
- `gm_freeride.cpp`: `-photoatrel` makes `-photoat x,y,z,yaw,pitch,fov` an offset from the car instead of a world position, and the pose is logged (`FREERIDE: photoat car ... eye ...`). Car front is -z, left side -x looks at yaw 90.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -clocktime 12:00 -car Veyron_Super_Sport|Veyron_Grand_Sport -photoatrel -photoat "-4,0.34,-1.325,90,0,18"` (rear hub z +1.381): rims centred in the tyres on all four wheels, five spokes, hub cap present, rear rim larger than front (correct for the car). `-norim` / `-notire` A/B: the dark disc behind the spokes is the baked wheel-well/brake art, not a missing mesh.

### Not done / next
- The owner's frame is needed (which car, which wheel, which view). Suspects left: kaleidoscope-like env reflection on the spokes at close range, the brake disc being a flat bake (no caliper mesh), Grand Sport rims drawn with the Super Sport colours. Use the command above with the owner's car and compare.

## Session 2026-11-20 (`main`, scene editor M5 first slice — the bank writers + the native spots export)

The scene editor roadmap's M5 opening ("bnk_tool pack + the `.3dd/.3dg/.2dm/.2db` writers, validated by byte-compare round-trips, then scene→bank/spots/pmi export"): the write side into the engine, the byte-compare validation, and the first export route (spots; the `.pmi` patch and the composed `.3dd` bank build stay open).

### What changed
- **`source/filesystem/bank_write.{h,cpp}`** (new): the BNK packer, the C++ port of `tools/x360/build_bank.py` with the layout laws the Python tool never faced (it only compared entries): per-file section/data/file alignments out of the source header (icd_ferr packs sections 4-aligned with NO filler; frontend banks 32-align with the filler), the gap filler = **"STNICC2000 RULEZPADDING DATAS...-OR"** restarting at EVERY gap (sections and payload gaps alike; the 24-char header string continues past itself — the retail gaps read "...RULEZPADDING DATA"), the last payload UNPADDED (the sentinel sits at its last byte), the file padded to a 2048 multiple with the stream tail (the streamed texture region after the last payload; the header's SizeWithoutPadding points INTO it — copied, not recomputed), the retail CRCs copied on rebuild (Eden's own algorithm is not reproduced; zlib crc32 otherwise — the engine never verifies). The directory tree: retail trees may list a directory name TWICE (commonworld's `objects` holds `vegetations` twice) so `CaptureBankDirectoryTree` captures the raw node structure verbatim; fresh packs build the tree from decoded paths. Traps hit on the way: `Bank::parseDirectory` returns the INTERNAL path form (`dir\.ext
ame` — decode before rebuilding); the node law is length → **count → name** (build_bank.py and bank.cpp agree; my first capture read name → count and mangled every tree).
- **`source/render/render_file_write.{h,cpp}`** (new): the RenderFile container writer/parse pair — sections with payload / children / trailing split per section; the DataSize field is measured from the SECTION START (child region at DataSize — my first rebuild put it after the header and every payload differed at byte 28).
- **`tools/bnk_tool/main.cpp`**: `pack <folder> <out.bnk>` (folder tree → bank, parse-back checked), `roundtrip <bank>` (whole-file rebuild + compare; entries + directory as the fallback bar), `roundtripfile <payload>` (the RenderFile rebuild + compare). `Bank::parseDirectory` made public static.
- **`source/game/scene_editor/scene_export.{h,cpp}`** (new) + `export_spots` (the editor command + the Export panel button): the scene's spot / interior placements append as real 0x110-byte records to the overlay's spots.bin; the referenced interior banks copy to the overlay's `exterior/<bank field>.bnk` (the field = the interior stem truncated to 8). Records UPSERT (the record name = "SN" + the object id — position-independent, so a moved building rewrites its record). The list's own law kept: the retail size field = fileSize − 0x10 (a 16-byte tail after the records), so records count off the FILE length and the tail moves to the end on append.

### Verified (headless, build 394)
- **Byte-identical whole-file rebuilds of nine retail banks** of every layout family: Intro (frontend, 12 entries), commonworld (478), icd_ferr (interior, 158), eph_l3di (exterior, 125), worldactors (scenarics pack, 714), ias_a (interior, 130), area-2-4 (7), sector-2-4-3-3 (20), 250_gto (vehicle, 15). Known limitation: banks without a directory section (`avatar/config/pnj.bnk`) rebuild with one invented — the reader parses both.
- **218 payloads byte-identical** through `roundtripfile` (icd_ferr 125 + eph_l3di 81 + 250_gto 12: `.2dm/.3dd/.3dg/.2db/.XSB`, cube-map textures included).
- `pack` a folder → parse-back OK (`sub.bin` sub-paths right).
- **The native route end to end**: the editor (`export_spots`, 2 placements) → FREERIDE with NO editor: "Mod overlay: ...spots.bin", "81 spots, 81 with an exterior bank", `ECD_AUDI at (11665, 17437): 3 placements` (the island's 2 + the scene's 1 at 12680), `icd_ferr at (12620, 16700): 4 models, 1 placements` from the copied bank; screenshot landed (`_m5_freeride_native.png`).
- Re-export with the building moved 20 m: still 81 records, the scene record rewritten in place (upsert).
- Data trap documented: a hand-edited overlay spots.bin from an earlier session (the retail size-field law ignored) poisoned the first export — the export now counts records off the file length, keeps the tail, and refuses junk.

### Not done / next
- **M5 next**: the `.pmi` patch (library-node placements into the sector banks — the RenderFile writer is the tool for it) and the composed `.3dd` bank build (a scene compiled into one `exterior/<name>.bnk`; OBJ props authored to `.3dg/.2dm/.2db`).
- M6 polish (3-axis gizmo, material editing); the human UI pass + the roads feel-check in play.

## Session 2026-11-20 (`main`, brake / tail lamps: lopsided glow anchor + rear pool on the real lamps)

Owner: brake lights "don't seem correctly aligned" (Veyron Super Sport at night: white glow cores between the lamps, a hard red rectangle on the road shifted to one side).

### What changed
- `cinematic_car.cpp` `collectLampAnchors`: [DATA] the Veyron's `?A_S_RL_` glow mesh spans x -0.61..0.23 (twin `?A_S_RR_` 0.30 wide), so its centre landed at x 0.21 instead of 0.47. A left/right pair whose model-space centres are not mirror images now takes the compact side's anchor mirrored (log `CarLight: lopsided glow`). `-noglowmirror` = old.
- `gm_freeride.cpp` + `CinematicCar::getTaillampPositions`: the rear brake / reverse ground pool is centred between the real stop lamps with a width from their span (it was a fixed point 1.6 m behind the car origin). `-oldrearpool` = old.

### Verified
- `-car Veyron_Super_Sport -clocktime 23:00 -logcarlights`: `lopsided glow S rear left: centre x 0.21 -> 0.47`. A/B frame pair (reversing, white pool) `docs/images/rear-lamps-pool-ab.webp`: new = compact pool centred behind the car, old = wide band.

### Not done / next
- The owner's exact frame (braking, red pool, Veyron Grand Sport rear view) was not reproduced: my test car reversed, and the lamp cores themselves were not seen close up (low camera). Check: brake from speed, `-photoat` behind the car, with and without `-noglowmirror -oldrearpool`. Other cars with lopsided glow meshes get the same fix automatically; only this one was measured.

## Session 2026-11-20 (`main`, pool water: the dark blob was the walker's own head)

Owner: "odd pool reflection behaviour in pools" (dark blob with hair texture in the water of a house pool, terrain-like shapes).

**Reproduced**: `-gm FREERIDE -skipcine -nopad -fixedtimestep -enterhouse EPH_L0C_1254I -nointeriorintro -walkat -6.6,1.8,-0.3,0 -autoplay "100:look=0,60;500:shot=..."` (caravan tub). `docs/images/pool-refraction-walker-before.webp`.

### What changed
- `intro_cinematic.cpp` `setMirrorPlayer`: a first-person walker no longer feeds the pool's REFRACT layer (`view.setRefractionActors(false)`); the intro's swimmers keep it. `-refractwalker` = old behaviour.
- Debug flags: `-showrefract` (blit the GSReflection RT instead of the scene), `-noreflactors` (skip the actor mirror layers), `-logprojectors` (node world bounds per projector).

### Verified
- `-showrefract` before the fix: the RT held the basin + the walker's head seen from inside (teeth / tongue) + a ladder. With `-norefractactors` the head is gone; with the fix the pool shows only water + basin (`docs/images/pool-refraction-walker-after.webp`).

### Ruled out
- `-noactorreflect`-style actor mirror layer (`-noreflactors`): identical frame. Forcing the refraction layer two-sided: identical frame. Not the projectors' geometry (POOL basin bounds y 7.53..8.37 match the tub).

### Not done / next
- The owner's shot was a house with an in-ground pool (dining table, ladder, lifebuoy) and also showed green terrain-like shapes in the water; the tub repro shows no terrain, so that second symptom may be the same head/body (other avatar parts) or an EPH_L3DI-specific thing - recheck with `-enterhouse EPH_L3DI_5067I` and `-showrefract`.

## Session 2026-11-20 (`main`, clouds composited over foliage / markers - first fix, unverified by eye)

Owner (8 screenshots): clouds paint over trees, 3D markers and lamp glows; also reported, **not touched**: furniture / car collision in dealerships and house props (chairs through a table), Veyron Grand Sport rim + glass oddities, pool reflection (dark blob + terrain in the water), brake lights / glow misaligned.

### What changed
- Cause [INF]: the cloud composite ran after `flushDeferred`, depth-tested against a copy of the *opaque* depth; blended draws (foliage cards, markers, glows) write no depth, so clouds covered them. `eden_renderer.{h,cpp}`: `tagSkyDraws` / `flushDeferredSky` - dome, moon, stars and halo are tagged and flushed alone; `eden_scene_view.cpp` now does sky -> cirrus -> clouds -> all other blended draws -> haze. `-oldcloudorder` = old behaviour (A/B).

### Verified
- Build 391 links clean. A/B `-tp 29794,2258` night shot, new vs old: mean pixel diff 0.0 - **no cloud overlapped any blended draw in that frame, so the fix is not proven**.

### Not done / next
- Check by eye: needs a frame with cloud blobs behind a tree canopy or marker (the owner's shots at X 29794 Z 2258 had them); run with and without `-oldcloudorder`.
- Pool water, brake-light alignment, dealership/house prop collision (the interior walk has none for scene/prop objects), Veyron rim/glass: each needs its own repro.

## Session 2026-11-20 (`main`, wind turbine rotors on Ibiza)

Owner: wind turbines drawn as tower + nacelle with no blades (photo-camera shot at X 30493 Y 363 Z 4307 - **Ibiza**, not Hawai; Y is the photo camera's height).

### What changed
- `source/world/eden_world.cpp` / `.h`: each sector bank nests a `<sector>_sc` bank [DATA]. Its `entities.xmb` holds `Entity__Actor__RotatingActor` (`orient_pos_scale` = quaternion + position + scale in sector-local coordinates, `RotationSpeed` 3.0 rad/s for `FU_EoleA_act*`, 1.2 for `FU_EoleD_act*`, `WindCoeff`/`MinAngle`/`MaxAngle`/`Pause` all 0) and the rotor model `decors/ibiza/objects/furnitures/fu_eole/fu_eolea_act_<hash>.3DD/.3DG/.2DM` (one node `FU_Eole`, a flat 46 x 43 m disc, thin along its local Y = the shaft axis). The tower + nacelle (`FU_EOLEA`, materials `FU_Eole_Pill/Concret/Metal/Door`) are the only turbine pieces in the sector `-o` bank, so the port never had the blades. Now `loadSector` loads the nested bank, builds one model per rotor entity, and `drawSectors` draws it at the entity pose spun about its local Y (`world * T*R * spin(Y) * S`; matrices are column-vector, `A*B` applies B first). Phase = index * 1.7 rad [INF]. Speed is the entity's own RotationSpeed [DATA], clock is wall time.
- Flags: `-norotors` (A/B), `-logeole` (prints `ROTOR` entity rows and `ROTORITEM` model bounds), `-rotortest` (identity orientation, a debug aid).

### Verified
- `-gm FREERIDE -island ibiza -tp 30493,4307 -skipcine -nopad -photoat 30493,363,4307,75,-10,60 -autoplay "400:shot=..."`: all four visible turbines now carry three blades (`docs/images/wind-turbine-rotors-ibiza.webp`; before: bare nacelles, identical camera). 6 rotor entities in sector-0-3-4-7.

### Ruled out
- Not a world instance (only `EL_AG_*`/`EL_MP_*` pole models) and not culled/hidden: no blade geometry exists in the sector objects; `-lognearinst` / `-lognearmesh` show only `FU_EOLEA` items 1-5.

### Not done / next
- Hawai has `fu_eole` references too (sector-6-2-1-3): not looked at, the same loader should handle it. Night shading of the blades not compared with retail. Rotor orientation handedness verified by eye on Ibiza only; spin direction unknown [INF]. Other `Entity__Actor__*` classes of the `_sc` banks (the nested banks' other props) are still ignored.

## Session 2026-11-20 (`main`, scene editor M4 — interior-bank composition + walk-mode entry)

The scene editor roadmap's M4 ("interior-bank composition + walk-mode entry"), continuing M3 (roads, last commit). Task: place the game's own walkable rooms in a scene, compose furniture into them, and walk them first person to feel the composition.

### What changed
- **`source/game/scene_editor/scene_interior.{h,cpp}`** (new): the interior-bank asset layer. `ListBanks` (the game data's `interior/*.bnk` stems, the `__fr/__ge/__it/__sp/__us` locale clones filtered; 104 on this copy), `ResolveBankPath` (the three ref forms: the stem `icd_ferr`; the shop spot `ECD_FERR` through `InteriorWalk::InteriorBankFor`; the house `EPH_L2E_0345I` through `HouseBankFor`, `:garage` suffix for the garage bank — a bank only counts when the file exists), `LoadWalkData` (the bank's `.shk` collision via `LoadHavokCollision` + the retail walk start: `Entity__FirstPerson { StartCamera = Entity__Camera __Hc }` — `cam_entree` in every bank checked, spot_interiors_spec.md 2).
- **`gm_scene_editor.{h,cpp}`**: `refreshSpotRecords` now builds records for spot AND interior objects and resolves **parent chains** (`doc.resolveWorlds`; position + heading extracted from the composed matrix — the doc's own yaw law, so unparented objects behave exactly as before; pitch / roll / scale do not survive a spot record). An interior record's `ExteriorPath` points into `interior/` — the room draws through the unchanged `SpotExteriors` streaming path (lightmaps / overrides / distances = the retail machinery). The **walk**: `enterWalk(id?, atBankLocal?, yawLocal?)` / `exitWalk` / `tickWalk` — the walked room's placement frame is the SpotExteriors frame law (T·R_y), the walk runs bank-local (`WalkController`) with the `WalkMode::collide` law mirrored (0.3 m sphere `ResolveSphereHavokCollision`, `FindHavokFloor` step 0.4 / drop 2.0 + the 8-ray ring, freeze off-floor), start eye = the bank's FirstPerson camera dropped to the floor (+1.6, walk_mode.cpp's start law; no camera in the bank → the collision bounds centre; overrides via the command). Streaming follows the walk eye (`streamWorld`/`streamExteriors` take a center); Esc/F6 exits, the fly camera parks at the eye; F6 with no interior placement walks the scene grounds (terrain / road floors via `getSurfaceHeight`, step / drop bounded, no walls). The walk dies on island switch / walked-object delete. `refreshAssets` lists the interior banks (the browser's new tab).
- **Grass clearings** (`source/world/eden_world.{h,cpp}`): `GrassClearing` rects (world XZ) + `addGrassClearing` / `clearGrassClearings`; the grass emit's five-probe law skips any card probing inside one. The editor re-derives the set from the loaded interior banks' render bounds × placement frames every stream pass (`refreshGrassClearings`) — a scene room in a field no longer grows tall grass through its floor.
- **`scene_cmd.cpp`**: `enter_walk` (fields `id?` — defaults to the selection when it is an interior, else the nearest placement; bank-local `pos?` + `yaw?` overrides — returns the walk note + world `eye`), `exit_walk`, `status` gained `walk_active` / `walk_interior`, `teleport` ends the walk first. **`scene_editor_ui.cpp`**: the interior asset tab, the "walk inside (F6)" properties button, the walk status / hint lines. **`scene_objects.cpp`**: the `interior` kind skips silently (it draws through the streamer; was an "unknown kind" warn).

### Verified (headless, build 388, live server from Python)
- `add interior icd_ferr` at the ETUN_A field → `SpotExteriors: icd_ferr at (12640, 16700): 4 models, 1 placements` + `SpotExterior: Euro/Bnk/interior/icd_ferr.bnk: 2 set models, 2 props`; the room draws (`_m4_shot_field.png`) and the walk starts at `cam_entree` (eye bank-local (−8.19, 1.60, 1.77), floor snapped) inside the showroom — red hall, atrium, swatch wall (`_m4_shot_walk.png`).
- Chair `FU_Chair_act` attached to the interior at parent-local (2, 0, 3); save → reload → `enter_walk` round-trips (`_m4_shot_walk2.png`).
- Group yaw 90 + `ECD_FERR` (spot-ref form) attached → `1 scene spots appended`; `enter_walk` eye `[12641.8, 11.6, 16708.2]` = the hand-computed +90° composition (a sign flip would give `[12638.2, 16691.8]`) — the record frame and the walk frame agree with the drawn frames (`_m4_shot_group90.png`).
- Grass A/B: the first run grew flowering grass through the showroom floor; after `addGrassClearing` the floor is clean.
- `list_assets kind=interior`: 104 stems, `icd_ferr` / `lph_*` present.
- Note for the next session: the batch `-editorscript` runs all commands before the first frame, so multi-shot screenshot sequences drive better through the live server (the M2 route).

### Not done / next
- M5 native export (`bnk_tool pack` + the `.3dd/.3dg/.2dm/.2db` writers, byte-compare round-trips, scene→bank/spots/pmi); M6 polish (3-axis gizmo, material editing); the human UI pass + the roads feel-check in play.
- The interior walk has no scene-object collision (the attached furniture does not push the walker — the .shk only) and the grounds walk has no wall push; the retail walk (avatar / cinematics / shop menus) stays the game modes' route on purpose.
- Scene records are append-only for a session (`appendSpots` has no removal): a removed interior's record keeps streaming until relaunch — the documented M1 law, acceptable for a dev tool; a `SpotExteriors` removal path is the clean fix if it bothers the composing flow.

## Session 2026-11-20 (`main`, LOGIN skips the New profile dialog when a profile exists)

Owner (recording of the retail game): with an existing profile retail loads straight in; the port always asked for details.

- `source/game/bootmenu/gm_login.cpp` (`LS_Profile`): when `IsProfileLoaded()` and the profile has a name (and no `-newprofile` / `-newgame`... only `-newprofile` skips the skip), press start goes straight to `LS_SaveCheck` (continue to FREERIDE if story step >= 1, else the no-data popup). The dialog is first-run only. `-playername` is applied after `LoadProfile()` in `tdu_instance.cpp`, so it renames the existing profile and still pre-fills the dialog on a first run.
- Verified headless (`-no3d -fixedtimestep -autoplay "300:press=validate"`): empty userdata -> `New profile screen (nickname pre-filled '')`; `profile.sav` name only -> `profile 'Tester' found - New profile dialog skipped`; with `-playername Zed` -> `'Zed'` skipped; story_step 1 -> skipped + `progression found -> continue` + FREERIDE; first run + `-playername Zed` -> dialog pre-filled 'Zed'.
- Not done: the owner's video was not frame-compared (retail title -> loading timing); a retail "profile selection" page for several profiles does not exist here.

## Session 2026-11-20 (`main`, shift reach on every car)

Owner (from the 360-recomp video): retail shows no shift on the Audi R8, a reach to the lever on an older red manual car; "the shift animation on every car".

### What changed
- `seated_avatar.cpp`: when the car has no `GEAR_LR` node the right-hand lever goal is `HAND_LR_R` + (-0.16, -0.16, -0.01) in car space [INF], x mirrored by the seat side. `-nogearfallback` = old behaviour. `-logshiftnodes` logs per car: GEAR_LR present + position, IKOffsetDB Gear offset, HAND/FOOT/BACK nodes, seat position, any node named GEAR/SHIFT/LEVER/KNOB/PADDLE. `CinematicCar::findNodeNames`. `-listcars` (gm_freeride) prints the PC-valid DB car names (173).
### Data [DATA]
- Of 122 cars that loaded in the sweep: 50 have `GEAR_LR` (all with IKOffsetDB Gear offset 0 or small), **72 have none - including the California** (so the port never reached there); HAND/FOOT/BACK nodes exist on all. IKOffsetDB Gear offset is non-zero on 40 cars (20 without a node) - it is an offset on the node, not a position. Two cars (117, 374) carry a node called plain `GEAR` (not the lever node).
- gear - seat (car space) median (-0.35, 0.32, 0.47) std 0.11 / 0.07 / 0.08; gear - HAND_LR_R median (-0.16, -0.16, -0.01) std 0.10 / 0.06 / 0.05 (the one used). Driver seat x > 0 on 49 of 50.
### Verified
- California side view from the passenger side, `-shifttest 1 -shifttestat 80`: with the fallback the right hand leaves the wheel, with `-nogearfallback` it stays: `docs/images/driver-shift-fallback-s69.webp`.
### Not done / next
- How retail decides lever vs hands-on-wheel (R8) is not found (no DB gearbox field / node difference read); every car animates now. The mirror / door hides the lever from the driver side; a cleaner overhead shot needs a better `-photoat` pose. Offset is [INF] per car: a car-by-car check of the lever reach in play is owed.
- Worked in a worktree (`C:\OpenTDU2-shift`, build dir `build`) because another session's uncommitted scene_editor work broke the main build; delete it with `git worktree remove ../OpenTDU2-shift`.

## Session 2026-11-20 (`main`, finger layers hand_size / hand_open)

Owner: "Add the finger layers hand_open and hand_size" (and offered to record the 360 recomp for a video comparison; also reported wind turbines without blades - not touched here).

- **Trap fixed**: `avatarsdriver.bnk` holds same-named clips of other sets - `findEntry("hand_size.anm")` returned `animsike\man\hand_size.anm` (18 tracks, none on the car driver's skeleton). The loader now takes the entry under `anims\car\` (`seated_avatar.cpp`, `loadClip`). The car hand_size has 41 tracks, both hands' fingers, 1.0 s.
- **hand_size [DATA]**: t 0..0.5 an open flat hand (57 deg mean finger rotation away from turn-mid), 0.75 -> 37 deg, 1.0 -> 24 deg: a closing sweep, so the sample time = grip size. The game's time is not decoded [INF]: constant 1.0 (the closest to the turn.xan fist; `-handgrip <0..1>` to look, `-nohandsize` off). Effect at the rim is small (mean pixel diff 0.16 vs off): the turn.xan fist already is that grip.
- **hand_open**: 21 right-hand finger tracks, one pose; blended in with the shift ramp (`-nohandopen` off). Not visible in the shots (the right hand is the far hand, no camera angle sees it) - NOT verified visually; the later gear_seq_up finger tracks also overwrite part of it.
- Evidence `docs/images/driver-fingers-s68.webp`. `-logdriveclips` prints the hand_size sweep numbers.
- Next: a camera on the right hand during a shift (`-shifttest 1 -shifttestat 80`) to see hand_open; the owner's 360-recomp video would settle the grip time / angle map.

## Session 2026-11-20 (`main`): scene editor M3 - spline roads

Task: the approved plan's M3 - draw roads as splines in the scene editor, play them in FREERIDE.

### What changed
- **`source/game/spots/mod_roads.{h,cpp}`** (the mod_props sibling): `ModRoads::load` reads `<mods>/roads/<island>.json` (or a scene document), `loadDocument` rebuilds; `BuildCenterline` (Catmull-Rom, 2 m samples, closed loops), `BuildRoadObjText` (8 m ribbon, distance UVs, OBJ text in the AUTHORING space - the loader mirrors X), `BuildRoadSolid` (world-space up-facing soup); `draw` (identity frame) / `registerCollision` (`VehicleWorldCollision::addStaticTriangles`) / `registerSurfaces` (EdenWorld). `ModRoadsDefaultTexture` generates `<mods>/props/asphalt_road.png` once (own art; `SaveImagePng` added to `render/image_file.*`).
- **EdenWorld**: `addRoadSurfaceTriangles` (world-level 8 m-bucket soup; `getSurfaceHeight` / `getDriveHeight` sample it - a scene road is a surface like a retail one) and `GrassRoadIndex::merge` + `appendRoadShapes` (the mod roads' centerlines join the retail graph's grass suppression).
- **Editor**: a Roads panel (new road / points at focus / width / lanes / closed / collide / delete), road commands (`add_road`, `add_road_point` [snapped], `set_road`, `remove_road[_point]`, `export_roads`, `load_roads`, `bake_road`); a fresh document imports the island's roads file at load; `exportRoadsFile` writes the roads JSON; `bakeRoadToProps` writes `props/road_<id>.obj+mtl` + a `solid` props.txt line.
- **FREERIDE**: `ModRoads modRoads` loaded in `ensureModPropsLoaded` (draw in the scene callback, collision, surfaces, grass); `-nomodroads` = the A/B.
- Docs: `docs/re/scene_editor.md` 3 rewritten (implemented), README rows.

### Verified (headless, build 386)
- Editor: scripted road (`add_road` + 3 snapped points + `export_roads`) draws as a continuous terrain-following ribbon - top-down `_road_top2.png` (build 386). The first run showed FRAGMENTS of ribbon: the spline interpolated y between control points while the heightfield rolls - the ribbon dipped under the terrain between points. Fixed by conforming the samples (lift to terrain +5 cm; a bridge's explicit height stays). The bake OBJ was inspected (`v -12609.000 9.710 16600.000` - authoring space, ground heights).
- FREERIDE with the exported roads file: `ModRoads: 1 road(s), 184 surface triangles`, `EdenWorld: mod road surface registered (184 triangles, 49 buckets)`, collision added; the car teleported onto the strip stands on the ribbon (`_road_freeride4.png`), 0 stalls.

### Ruled out
- Drawing the ribbon at control-point heights only (the fragments above) - terrain conformance is required for ground roads.

### Not done / next
- Road drive-feel, lane markings and the grass-suppression A/B: owner in play (logs + screenshots are the headless evidence).
- M4 interiors; M5 native export (`bnk_tool pack` + the .3dd/.3dg/.2dm/.2db writers + scene->bank); traffic/GPS graph (ROAD_MAP writers) stays open.

## Session 2026-11-20 (`main`): the scene editor, M1 + M2 (owner request: "a tool like blender but easy to use, headless, AI can use it; design buildings, interiors, props from what's in the game; attach/detach; map editor with spline roads")

Task: the approved plan's first two milestones — the in-engine scene editor shell + the AI/headless command interface. M3 (spline roads), M4 (interiors), M5 (native bank export) follow.

### What changed
- **New `source/game/scene_editor/`** (the mode `-gm SCENE_EDITOR`, factory + enum + `launch/18_SceneEditor.bat`):
  - `scene_json.{h,cpp}` — minimal JSON parse/write in-repo (no third-party dep; pretty files, one-line compact for the protocol).
  - `scene_doc.{h,cpp}` — the document (`<mods>/scenes/<name>.scene.json`): objects (group / library / actor / spot / prop with pos + yaw/pitch/roll + scale + visible + parent), overrides (node / material hide), roads (M3 shape), parent-chain world resolution, VFS load / mods-root save.
  - `scene_objects.{h,cpp}` — instantiates the document: library nodes via `EdenWorld::drawLibraryNode` (nothing uploaded, shared model), actors via `getActorModelPtr`, props via `LoadObjDraws`; world AABBs for picking; draw in the scene view's callback next to the exteriors.
  - `gm_scene_editor.{h,cpp}` — the WORLD_VIEWER skeleton (streaming, weather, islands F5-F8) + editor: LMB pick / ground-plane drag (RMB looks), undo / redo (document snapshots), duplicate / delete, snap-ground, asset browser cache, scene-spot records appended to the streamer.
  - `scene_editor_ui.cpp` — the panels (Scene / Assets / Outliner / Properties / World pick-detach / Export).
  - `scene_cmd.{h,cpp}` — the command interpreter + `-editorscript` batch (VFS path or plain file) + the live server (one client, JSON lines, 127.0.0.1 only, main-thread execution between frames; winsock2, `ws2_32` linked on WIN32).
- **EdenWorld extensions** (`world/eden_world.{h,cpp}`): public `getLibraryModel / getLibraryNodes / getActorNames / getActorModelPtr`, `raycastObjects` (exact sector picking through the collision copies), and the visibility-override sets `addHiddenNode / addHiddenMaterial / clearHiddenOverrides` — applied at library / sector / area model load (a hidden node hides its whole subtree) and guarding `drawLibraryNode` (drawNodeItems does not test nodeVisible).
- **EdenModel**: shared `EdenModelApplyVisibility` (nodes + subtree, material items) in `render/eden/eden_model.{h,cpp}`.
- **SpotExterior(s)**: `applyVisibilityOverrides` (the banks' models get the sets; the editor re-applies per frame) and **`appendSpots`** — records added WITHOUT the `setSpots` unload (see Ruled out), an already-loaded bank just gains a frame.
- Docs: `docs/re/scene_editor.md` (the format + routes), `docs/studio_ai_api.md` (every command + a verbatim session), README rows (flags, tool table, source map), PROGRESS.md.

### Verified (headless, build 384, RTX 5070 Ti, `-userdata C:\OpenTDU2\_test_userdata` so the overlay never touches the game data)
- Scripted run (`-editorscript`): status / list_assets (library 399 names, actors 135 entities, spots 79 banks) → `add` spot + actor + library → `snap_ground` (heightfield hit) → `save_scene` → reload via `load_scene` + `status` (`num_objects 3`). The scene JSON is human-readable and round-trips.
- The placed `EAS_B` streams and draws: `SpotExteriors: 1 scene spots appended` → `EAS_B at (12630, 16698): 2 models, 1 placements`. **A/B**: `_ed_shot5.png` (with scene: the MYWEAR building) vs `_ed_shot6_ab.png` (empty field, no scene), same camera.
- **Live server**: Python TCP session ran status → add → look_at → hide_node → save_scene → screenshot → status; every response JSON, the screenshot saved (`_ed_server_shot.png` — the AI-placed building framed by the AI-aimed camera).
- WORLD_VIEWER untouched (the skeleton is shared code, the mode is separate); full engine regression = the same runs' logs (0 stalls, no VUID lines).

### Ruled out
- **`SpotExteriors::setSpots` as the scene-placement path — segfault.** It `unload()`s the loaded banks; the view's lightmap list then holds dangling material pointers and the next draw crashed. `appendSpots` (no unload, frames appended for loaded banks) replaced it; the crash is gone (the full placement run exits 0).
- ImGuizmo as the gizmo: dropped for M1 — the projection conventions would have needed empirical flipping and a new FetchContent dep risks the offline `OTDU_DEPS_DIR` builds. The screen-space pick + ground-plane drag + numeric inputs cover placement; a 3-axis gizmo can come back in M6 with the exact view/projection now proven.

### Not done / next
- M3 spline roads (the tool + `mod_roads` runtime + collision + grass suppression + OBJ bake export).
- M4 interiors; M5 `bnk_tool pack` + `.3dd/.3dg/.2dm/.2db` writers + scene→bank export; M6 polish (3-axis gizmo, material editing).
- The editor panels are untested interactively (all verification headless): a human pass over the UI is still owed.

## Session 2026-11-20 (`main`, clip-driven seated driver)

Task: replace the procedural driver IK (hand goals on the wheel + 0.6 s reach to GEAR_LR) by the game's own driving clips.

### What changed
- `seated_avatar.cpp/.h`: loads `turn.anm`, `hand_open`, `gear_seq_up/down` (`-w` for the woman), `gear_lever_up/down` from `avatarsdriver.bnk`. Layer 0: `turn.anm` sampled at `(0.5 + 0.5 * clamp(wheel/1.5708)) * 1.04 s` over the base pose (the wheel angle comes from `GMFreeRide::updateSteeringWheel`, positive = left, unchanged). Layer 5: a gear change plays `gear_seq_up` (both directions, reversed for down) 1.0 s with 0.2 s ramps, blended onto the arm tracks, then the layer-6 IK runs after the clips (right-hand goal -> Gear node blended by the ramp + the clip's hand lift). `-noclipdrive` = the old procedural path (kept as fallback).
- `eden_animation.h`: `getLoopDuration / getTrackCount / getTrackBone / hasTrack`.
- Flags: `-logdriveclips` (durations, tracks, hand positions along each clip), `-shifttest <+1/-1> [-shifttestat N]` (plays a shift on update N; photo mode freezes updates ~91, so N=80 catches the reach), `-logshift`.

### Data [DATA] (`-logdriveclips`, driver_seat_spec 1.4)
- turn.anm = ONE lock-to-lock sweep, 1.04 s: t=0 right lock (right hand at the rim bottom), 0.5 straight (= stance_turn), 1.0 left lock (right hand at the top); the rim covers only +-90 deg, the game's wheel goes +-270 deg, so beyond the clip lock the hand IK carries the hands.
- gear_seq_up (1.0 s): RIGHT arm + fingers, hand at (-0.55, 0.14, -0.12) with a 6 cm lift and back; gear_seq_down: the same for the LEFT arm mirrored (+0.55) = the right-hand-drive twin. So up / down are NOT shift directions; a LHD car uses seq_up for both [INF].
- gear_lever_up / down (0.2 s): finger / wrist only, hand near the pelvis (partial clips) - unused for now.
- hand_open: 21 finger tracks, dur 0 loop 0.2.

### Verified
- Side view of the cabin (profile `avatar_config = AVT_Male` in `C:	emp\otdu2-clip-userdata`): `docs/images/driver-clips-s68.webp` - wheel 0 / +1.5 / -1.5 rad and mid shift. Clip vs `-noclipdrive` at -1.5: mean pixel diff 7.0 (poses differ). `-logshift`: blend 0 -> 1 -> 0 over 1.0 s, lift 0 -> 0.059 m.

### Ruled out
- The 2012 dev build has no readable driving code: its avatar class (`anim_blend_drive` loader at 0x8c5bc5, array +0x250 x 0x18) is the BIKE avatar; the car weights / timings are not in it. The angle -> clip-time map and the ramps are [INF].

### Not done / next
- Finger layers (hand_size, hand_open at the gear lever), gear_lever_* use, an owner look: the shift motion is subtle (the Gear node is near the wheel). Built and tested in a git worktree `C:\OpenTDU2-clip` (branch `clipdrive`) because the main tree's untracked `scene_editor/` WIP did not compile; merged into main afterwards.

## Session 2026-11-20 (`main`): fourth Ibiza OIL station wired; `-lognearinst`

Owner: found another petrol station (F6 X 20338.6 Z 20153.4), wants it wired, shutters removed, lighting and better textures; unsure where the others are.

### What changed
- `service_stations.cpp` kBuiltIn: `OIL Station (Area 2)` at (20338, 20153). Log: 4 built-in stations on ibiza.
- `eden_world.cpp`: `-lognearinst x,z,r` logs every sector instance near a point (hash/name, box).

### Ruled out
- The station building is NOT a world instance: only vegetation (V_PALM/V_BUSH) and low walls (SF_WALE*) within 70 m. It is baked in the sector object geometry, so shutters / lights / textures need item-level work on that mesh (find the REPAIR shutter materials, hide or swap them), not an instance swap.

### Update (same day): shutters opened
- Station = sector Objects node `SB_GAZS_`; materials SB_GazSB_tole / Wall / Detail (signs, canopy stripe) / Ground / Metal / Instc (**the graffiti roller shutters**) / SB_ShopingB (glass shop front). Found with `-lognearmesh x,z,r` and A/B `-hidemat <name>`.
- `eden_world.cpp draw`: once per sector, the Instc material is replaced by the ShopingB one (lit glass fronts instead of shutters); `-noopenbays` restores. Shot: `docs/images/station-open-bays-s67.webp`. Applies to every SB_GAZS building.
- Ruled out: `EdenMaterial::Emissive` on the TD_Building shader (no visible change); the material has no WeatherEmissive param.

- **Forecourt lights** (`gm_freeride.cpp`, next to the traffic light appends): at night, 3x3 grid of warm glow heads (`EdenLampGlowFrame`) + ground pools (`EdenHeadlightFrameCones`, horizontal Direction + PoolAhead/Along/Across, because EdenSurfaceFx only draws a pool from the cone's horizontal heading) around each listed station within 150 m. `ServiceStations::Station::LightOffset` = canopy centre from the entry (Area 2: +10.5, +14, measured from a top-down `-photoat`). Cone Position is put 1 m below ground so the pool's surface query (starts +4.5 m) lands under the canopy roof, not on it. `-nostationlights` removes. Shot: `docs/images/station-lights-s67.webp`.
- Trap: headless `-tp x,z` leaves the car at y 1.9 until it settles; pass `-tp x,y,z` (y 56) or the light grid is built at the wrong height.
- Other stations' LightOffset from top-down `-photoat` shots at the `SB_GazSB_tole` centres (ground y 140 / 70 / 71.5): Area 1 (+3, +22), Car Wash (-2, -22), East (+17, 0). Checked in low-angle night shots: the glow heads and pools sit under each canopy (eyeballed to ~2 m).

- **Station scan** (`-logstations`, one run per candidate sector; raw-string prefilter of the 1165 sector banks found 57 with `SB_Gaz*`): 13 OIL stations (`SB_GazSB_*`, the shuttered type with pumps) on Ibiza, none on Hawai. 9 new ones added to `ServiceStations` (13 total), centre of the Metal canopy item, LightOffset 0 (not measured). A second type, `SB_Gazs_A_*` (wood / stone, `SB_GazS_Volet`), has ~44 small roadside shacks; two looked at from the air are tiny buildings, pumps not confirmed, NOT wired.
- Night emissive textures: TD_Building has a night window term (slot 6 `gWindowMapTexSampler` x lum(base) x UserVector1.w) - night_lights_camera_glass_spec.md; so a lit version needs a second texture in that slot, not a shader change. Not tried on the station materials.

- **Slot-6 night window test** (`-nightwindowtest`, `-logstationmats`): station materials (`SB_GazSB_*`) run TD_Building vs0/36/52/76, none the window permutation. Only the shacks' `SB_GazS_Volet` has slot 6 + vs92/ps512 (spec 1=2 2=1 3=1 6=1 36=1 40=1). Moving `SB_GazSB_Metal` onto that spec (getProgram -> `TD_Building vs92/ps512`) with the Detail sign texture in slot 6 renders without error, but NO visible glow at night (mean pixel diff small, bbox over the canopy only): the window term samples a window UV the station meshes probably do not carry [INF, not checked]. Next: dump Metal's vertex UV streams (`EdenMesh` / `-loguvrange`) and the vs92 inputs to see which UV set `gWindowMap` reads; if the meshes have none, a glow needs a re-exported mesh or the port-native glow quads instead.

- **Tarmac** (owner: add road and floor to the stations): (1) `SB_GazSB_Ground` (TD_Ground pad, dirt art) gets a generated asphalt in its COLOR slot (`GetStationAsphalt`, `station_drives.cpp`; `-nostationfloor`). (2) The dirt access track is terrain, so a tarmac strip (procedural OBJ via `BuildObjDraws`, TD_Gouraud, 7.2 m wide, height from `surfaceHeight` + 0.07, built when the car is within 250 m, `ServiceStations::tickDrives/drawDrives`) follows `Station::DrivePath`. Only Area 2 has a path: traced from a top-down shot (scale calibrated against the strip itself, 4.3 px/m at 1280 wide, NOT 5.2). Its two ends are the entrance and exit; the track runs N-S past the pad, not to the highway. `-stationdrivegeneric` = a straight connector to the nearest map road (`MapScreen::getRoadPointNear`), off by default because it cut across the verge. `-nostationdrive` removes. Shot `docs/images/station-tarmac-drive-s67.webp`. The texture is plain grey grain, no markings.
- The other 12 stations still need their own traced `DrivePath` (top-down `-photoat x,200,z,0,-89,60` at the station, trace the dirt band).

### Not done / next
- Texture pass (needs a light/glow prop mechanism; TD_Building params unknown). Other SB_Gazs_A_* station variants not checked for shutters.
- Other stations: need F6 positions (or a scan of sector object materials for the pump / REPAIR textures).

## Session 2026-11-20 (`main`, evening): steering wheel direction fixed, East Coast station moved to the owner's F6

Owner: "steering wheel and animations are reversed turning left wheel turns right"; found a petrol station at X 23281 Z 19374 (= the scanned `East Coast` one, 34 m off its centre, so it sat outside the 35 m prompt radius).

- `updateSteeringWheel`: target is now `+getSteerAngle()*9` (was `-`). `getSteerAngle` is positive = left (`-logwheel` `steerleft` -> road +0.53). Cockpit California at `-wheeltest +0.8 / -0.8`: positive turns the wheel counter-clockwise as the driver sees it (the Engine Start button goes from the lower left to the lower right spoke) = a LEFT turn. The earlier note "positive turned it clockwise" was wrong or from behind. The hands follow the same angle.
- `ServiceStations`: East Coast entry = (23281, 19374.5), lights still at the scanned centre via LightOffset (22, -26).
- Not done: the real driving clips (`driveiklayer0..6.bas`: turn.xan, hand_open, gear_seq_up/down, gear_lever_*) are still not played; the gear reach is the procedural 0.6 s IK move. The 2012 dev build (`E:\Documents\TDU2+Dev+Build`) has readable code for it.

## Session 2026-11-20 (`main`, later): other exterior banks checked for the car wash's set-vs-collision offset - none has it

`-logbankoffsets` (`gm_freeride.cpp`) prints `BANKOFFSET <bank> render (...) coll (...) centre delta size delta` per streamed building. Swept via `-tpdestination` (Ferrari dealer, Any dealer, Tuning shop, Race school, Clothes shop, Airport, Casino) on Ibiza + Hawai: dealers (ecd_ferr / audi / bri2 / ital), houses (eph_l0c / l1bi / l1c / ephot), shops, edi, ere, enc_pac, ech, ehd: render and collision bounds agree to 0.0 m in x and z. Differences exist only where the collision is smaller than the render (egamha -3.5 x -5.7, etnr / etun 1 m, elandcaa, airports' extra runway collision) - bounds, not a rigid shift. The wash (-1.5, 0, -16) offset is specific to `ecw_`. Not swept: every house variant / the Hawai-only banks not near those destinations.

## Session 2026-11-20 (`main`, car wash alignment solved: the set is 16 m off its own collision)

Owner (build 373 shot): the planter should sit where the bushes are, the wash hangs over the road and needs pulling back, the entrance is where the semicircle enter/exit arrows are.

- **Cause [DATA]**: the bank's Scenery set has no placement; its render bounds (x -19.3..23.1, z -21.9..33.9) are NOT where the bank's own Havok collision (`ecw_ai_sc_*.shk`, lot walls x -20.7..21.6, z -38.2..17.6), the props and the washing scene put the lot. A search over the 4 orientations x translations (34 structural .shk boxes vs the set's vertices, `-logspotcollbounds` + `-dumpwashverts` + `-washfloor`) picks identity with **(-1.5, 0, -16.0)** (mean distance 0.7 m); rot180 / mirrors fit worse. spots.bin also holds a second placement per record (+0x2C, +0x38: the road access point, ~24-27 m on the local +Z side) - not used yet.
- **Fix**: `WashSetLocal()` (`spot_exterior.h/.cpp`): the Scenery sets of `/ecw_` banks are drawn at `frame * Translation(-1.5, 0, -16)` (`SpotExterior::draw`, the floor collision via `getSetLocal()`, and `IntroCinematic` for the washing scene). Props / collision / car / cameras stay on the spot frame. `-nowashrot` = old placement. The forecourt plate is no longer cut (it lies 6 m clear of the road now): `-dropwashstrip` restores the old strip cut. `-washshift x,z` (spot_exteriors.cpp) still moves the whole spot (default 0).
- **Verified** (`docs/images/carwash-aligned-s66.webp`, `carwash-aligned-all-s66.webp`): top-down at Ibiza wash 2 / 1 / 3 and Hawai 1 / 3: the lot is clear of the road, the entry / exit pockets meet its front; the drive-in now ends with the car IN the bay facing out, a tiled wall on its left and the canopy above (the owner's 360 shot of the loop). Not verified: the planters vs the bushes at ground level; the other Hawai washes; whether other exterior banks (dealers, houses) have the same set-vs-collision offset (`-logspotcollbounds` shows the bounds for any bank).
- Test hooks: `-atwashidx`, `-atwashlocal`, `-photoat`, `-logspotcollbounds`, `-dumpwashverts`.

## Session 2026-11-20 (`main`, photo DoF + Character, Android in CI)

- **Depth of field**: `resources/shaders/photodof.frag` (+ generated `photodof_frag.h`, `tools/compile_shaders.py photodof.frag`): the port's own depth-driven gather blur, NOT the original "crappy" ps_1 (that one only moves the LUT's luminance index, no visible blur, postfx_spec 2.5). Depth = gViewPos B/A decoded as postfx_spec 6.2; `EdenSceneView::applyPhotoDof` runs after the height fog on the finished image (`PhotoGrade::DofIntensity/DofDistance`): distance slider 0..1 -> focus 2..200 m (0.5 = 20 m), intensity -> up to 16 px at 1080 lines. Trap fixed: `bDofApplied` must be cleared BEFORE `applyHeightFog` (the fog reads `finishedSceneColor()` - last frame's DoF output fed back and the image smeared more every frame). `-photodofdebug` shows decoded depth (white = 50 m), `-photodofdebug2` the blur radius.
- **Character Off** hides the seated driver and passenger (`gm_freeride.cpp`, `photoCamera.isCharacterShown()`); `-photoopt` takes an 8th value (character 0/1). NOT verified visually: the test profile has no avatar (needs `avatar_config = AVT_Male`), the diff was 0.
- **CI**: the Windows workflow has failed since run 311 (`558bd6a`); a clean local clone of HEAD (LLVM 23.1.2, same cmake line as CI) builds fine, so it is the runner toolchain - the log's first `error:` line is needed (the API gives no log without a login). New `android` job in `.github/workflows/windows.yml`: Gradle `assembleDebug` on ubuntu -> `opentdu2-android-b<commit count>.apk`, attached to the auto prerelease by `release-main` (a failed phone build does not block the Windows zip). Untested until it runs on GitHub.

## Session 2026-11-20 (`main`, photo Options -> post pass)

- `PhotoGrade` (`eden_postfx.h`), set each tick by `PhotoCamera` while it is open, read in `EdenPostFx::applyPhotoshop`: Bloom intensity scales `photoshop_bloom_intensity` (x value/0.2), Contrast scales the overlay layer opacity (x value/0.5, capped 1), Red/Green/Blue add (value-0.5)*2 to the midtone color balance [INF: retail mapping not readable; neutral at the defaults]. `-photoopt bloom,dofI,dofD,contrast,r,g,b` presets the values (test).
- Verified: pixel-diff vs default at `-photoat 19090,150,18210,60,-5,60`: red 1.0 -> (5.5, 0, 0), blue 1.0 -> (0, 0, 4.8), contrast 1.0 -> (8.1, 7.5, 6.9), bloom 1.0 -> (1.7, 1.4, 0.8). Looked at the shots: tints are visible and per channel.
- NOT done: the two Depth of field rows (still stored only) and the Character row. DoF = the `edrender_pfx_apply_lut` ps_1 "crappy" variant (item 25d6c5d0fd095995, spec[32]=1) with sD = the depth target decoded as in postfx_spec 6.2 and params focus / OoF blurred lum; needs a second apply material in `EdenPostFx` plus the scene depth bound.

## Session 2026-11-20 (`main`, later): Hawai car washes checked

All five Hawai washes top-down (`-island hawai -atwash -atwashidx 0..4 -photoat ...,0,-89,60`, `docs/images/carwash-floor-hawai-s66.webp`): the floor draws, the road strip is cut, no road is covered. The roof canopy touches / hangs a little over the road edge at washes 1 and 5 (building geometry). Owner confirmed (build 362 shots) every wash had the same bare-floor issue.

## Session 2026-11-20 (`main`, car wash true floor back)

Owner (photo of wash 2 from above): the wash lot shows bare gravel; most of the carwash's true floor is missing; asks to check the other washes for road overlap.

- Cause: commit 0177afb hid the whole forecourt plate item (ecw set, mat 2) because its front lip lies over the road band. `-logwashplate` dumps its triangles [DATA]: the plate is a polygon at y 0.0 (= road height) with a 0.2 m rim; only the lip with all vertices z >= 26.3 (bank-local) lies over the road.
- Fix: `EdenModel::setTriangleDrop` (per-triangle drop before upload); `spot_exterior.cpp` draws the plate and drops the flat triangles (y -0.3..0.3) with min z >= 26.3 on ecw banks. `-hidewashplate` = the old whole-item hide, `-keepwashplate` unchanged meaning (no filter).
- Verified top-down (`-atwash -atwashidx N -photoat x,y,z,0,-89`): `docs/images/carwash-floor-wash2-s66.webp` (the dark paved lot, blue border, road clear), `carwash-floor-wash1-3-s66.webp`. Washes 1 and 3 (Ibiza): floor correct; the roof canopy still hangs ~3 m over the road edge (the building's own roof, retail geometry). Hawai washes NOT checked.
- Not done: the sink-under-floor case is still unreproduced (see the entry below); with the floor drawn at y 0 = road height there should be nothing to sink under.

## Session 2026-11-20 (`main`, car wash floor report): car under the bay floor at wash 2 (19122.7, 18162.4) - NOT reproduced

Owner (build 362 `842836a+dirty`): teleported (cheat row "Car wash") to X 19128.36 Z 18167.76 HDG 225.3 and the car sat ~0.5 m below the bay's concrete floor (screenshot), "collision is broke".

- Test hooks added (`gm_freeride.cpp`): `-atwashidx <n>`, `-atwashinside`, `-atwashlocal x,z` (wash-frame spawn), `-atwashlift` (spawn 40 m up so the car lands after the exterior collision streams in).
- `SpotExterior::collectFloorTriangles` + `-washfloor` (opt-in): the up-facing triangles of the ecw sets between local y -0.3..0.6 (928 triangles) added to the vehicle collision. Default OFF: in this tree the car already rests at Y 139.97 (spot Y 139.9) with or without it, so it is not shown to change anything.
- NOT reproduced: at the owner's exact world coordinates this tree puts the car OUTSIDE the tunnel, on sand beside it (`-atwash -atwashidx 1 -atwashlocal -7.4,-2.5`), while the owner's screenshot has the car inside the tunnel there. Their build is `842836a`, mine `27386d9`: the spot frame / building placement may differ between the two trees. Next: the owner runs `check-build.ps1` + pulls, then `-tpdestination "Car wash"`; if still sunk, send the log line `building collision ... ecw_a` and `-logdrive` Y.

## Session 2026-11-20 (`main`, photo mode UI): the 360 photo-mode screens redrawn natively, free camera kept

Owner: wants the real photo-mode UI (360 shots: Move/Zoom/Tilt/Rotate top bar, Take a Photo/Back/Options bottom bar, reticle, Options panel) but the camera must stay free (no locked default radius/height); also: the car wash at the Ibiza forecourt overlaps the road and the car parks in the wrong place (360 shots of the tile-wall bay).

### What changed
- `photo_camera.{h,cpp}`: `drawUi` draws the retail layout from the reference shots (`docs/images/photomode-ui-s66.webp`, `photomode-options-s66.webp`); X (keyboard O) opens the Options panel (Bloom, DOF intensity/distance, Contrast, R/G/B, Character On/Off; D-pad/arrows move + adjust, A/B close). Values are stored only, NOT applied to the post pass [INF]. Controls follow the retail bar: LS move along the view (free flight, no limits), RS rotate, LT/RT zoom (continuous), LB/RB tilt, D-pad up/down or Q/E vertical. `-photoat x,y,z,yawDeg,pitchDeg[,fovDeg]` opens the camera at a pose after 90 frames (test hook). `gm_freeride.cpp` now calls `photoCamera.drawUi`.
- The retail movie (`photomode_menu` in MAIN.vmf, `phmo:*` / `phop:*`) is still not driven: the UI is native, matched to the shots.

### Verified
- `-gm FREERIDE -island ibiza -atwash -photoat "8142,149.5,26281,-167,-5,60" -autoplay "230:press=opt1;260:shot=..."` shows the bars, reticle and Options panel (docs/images). Not verified: pad input (keyboard/headless only); glyph shapes are approximations (no icon art).

### Car wash: NOT fixed - findings
- `-photoat "8132,200,26228,0,-89,60"` (`carwash-topdown-s66.webp`): same building layout as the owner's 360 top-down (bays / arch / pump canopy; rotated 90 deg), no mirror. The bay canopy and the building end sit where the road continues; in the 360 the road passes clear of the lot. The earlier "retail-accurate placement" conclusion (spec 11) was reached without a 360 reference and may not hold: the 360 road geometry could differ from this PC (TDU World) install.
- The scene car ends at local (1.47, -17.5) = the data pose (matches C_INTRO_E_1 end), but in the top-down that is ~9 m beyond the arch mouth, outside it, with the wall on the car's right (`carwash-park-s66.webp`); the 360 shot has the car in the bay with a tiled wall on its left.
- Next: the owner says which wash (3 on Ibiza) the 360 shot is, then compare the road meshes (sector roads vs the bank) at that spot and check whether the exterior set is x-mirrored against the scene frame (`gm_cin_viewer` / `IntroCinematic` spot frame).

## Session 2026-11-20 (`main`, later 3): the REAL pool ring (Entity__Actor__RotatingActor 'Accessories')

Owner showed the 360 recomp ring (a yellow inflatable ring with a red underside, floating in the caravan tub).

- It IS in the PC data: the exterior bank's `eph_l0c_sc_4BFB820359AB7775.3DD/.3DG` (materials `waterwing`, node `BOUEE`) is the mesh of the `Entity__Actor__RotatingActor` 'Accessories' placed at the pool centre (-6.51, 0.97, 2.61) (spot frame), RotationSpeed 2.2, WindCoeff 0, Min/MaxAngle 0..0, Pause 0 [DATA]. The loader never read that array.
- `intro_cinematic.cpp`: `ArrayOf_Entity__Actor__RotatingActor` is now a prop array; `Prop::bRotating` + `RotAngle` turn it about its own Y; its mesh is off the origin, so it circles the pool. RotationSpeed read as **degrees/s** (164 s lap) [INF: the unit - if the 360 ring moves faster, it is `-ringspeed <x>`]. The port-invented ring is retired when the real one loads (`-fakering` brings it back; `-noring` hides both).
- Verified: build 364 headless, the ring crosses the top of the frame between frames 200 and 500 (`-ringspeed 40`). Not compared against the 360 shot side by side (the walk start moved after the collision fix).

## Session 2026-11-20 (`main`, later 2): the yard walk went through the caravan, the pool tub and the garage - the exterior shell was never in the body's frame

Owner: "all properties need outside collision fixing even caravan as can go through the walls and garage"; the real ring should be in the 360 data.

### Cause [measured]
`IntroCinematic::collectExteriorCollisionTriangles` appended the exterior sets' triangles in the SPOT frame (bounds (-21..12, -0.4..5, -10..9)) while `InteriorWalk`'s world hook resolves the body in the ISLAND frame (the spot is at (10490, 15750)): the exterior shell (caravan walls, garage, pool tub) was ~10 km from the body and never collided. Only the sector statics (`VehicleWorldCollision`) ever pushed.

### Fix
`intro_cinematic.cpp`: the collected vertices go through `spotOrigin`. `-extcollisionlocal` = the old frame (A/B). `interior_walk.cpp` logs `exterior collision bounds`.

### Verified
`-enterhouse EPH_L0C_1254I -nointeriorintro -walkat -6.6,1.8,-2.5,<yaw> -logwalk -autoplay "60:hold=walk"`: yaw 0 old frame walks through the pool tub to z 17.2, new frame stops at the tub wall z -0.09; yaw 90 / 180 / 270 all stop within a few metres. The default start still walks (in the room).

### Not done
- The exterior bank's own Havok collision (`eph_l0c_sc_*.shk` / `.dhk`, 45 KB) is not used for the yard: the render triangles stand in (one-sided/holes possible). Next step: load it with `LoadHavokCollision` in the yard hook setup and merge (frame: spot).
- Other properties were not walked; the same hook serves all `bWorld` houses, so they should be fixed too - walk the garage door / terrace houses (EPH_L3DI) to confirm.
- The 360 ring: the 360 `eph_l0c.bnk` (bigfile1, 2.2 MB vs 4.2 MB on PC) extracts but is not a plain BANK (`tdu2_bank.py`: no magic at offset 8, compressed?). Not decoded.

## Session 2026-11-20 (`main`, property walk fixes): vegetation alpha, the glass-to-roof push, pool water is not ground, wading

Owner: vegetation slightly transparent; properties: out-of-bounds, walked into glass and was pushed onto the roof, walk on pool water (wants swimming / getting wet / water reacting), a ring circling the caravan's small pool.

### What changed
- `eden_renderer.cpp` `EffectiveAlphaRef`: TD_Vegetation* alpha-test reference x0.55 (`-vegalpha <scale>`, 1 = data). Ibiza parked shot: mean pixel diff 2.7 vs 1.0. [INF: the leaf-card alpha mips lose coverage]
- `interior_walk.cpp` world hook: the push can lift the body at most 2 cm (glass met at an edge resolved upward onto the roof) [INF, not reproduced].
- `intro_cinematic.cpp`: the pool water is its own exterior model (`POOL` node under `REFRACT`, a TD_Water quad); it no longer counts as ground or collision (`-solidwater` = the old law); `getWaterSurfaceHeight`. `WalkController::setSpeedScale`: wading slows the walk to 50 % at 1 m of water (`-nowade`). Log `InteriorWalk: wading in the pool`. `-lognodes` dumps the exterior nodes + `POOLWATER` bounds.

### Not done / next
- **The caravan pool is an above-ground tub with a wooden rim** (`-enterhouse EPH_L0C_1254I -nointeriorintro -walkat -6.5,1.8,2.5,0`): the exterior shell blocks entry, so wading was NOT exercised; a deep in-ground pool (house with a terrace pool, e.g. EPH_L3DI) is the test.
- Wet avatar / ripples around the character: not started (needs a water-interaction pass; nothing in the engine does it yet).
- **The pool ring (built, later in the session)**: no ring object exists in the data, so `IntroCinematic::tickPoolRing` builds a port-invented red/white torus (`BuildObjDraws`, in-memory OBJ + 2x2 colour textures) on the pool water model's bounds (centre, top y) and drifts it round at ~0.9 m/s with a bob and slow spin. `-noring`, `-logring`, `-ringlift <m>`, `-ringat x,y,z` (test). Checked: caravan pool (`-enterhouse EPH_L0C_1254I -nointeriorintro -walkat -6.6,1.8,-0.3,0 -autoplay "100:look=0,60;500:shot=..."`) ring seen at frame 500, out of frame at 200 / 800. It is unlit-flat (the TD_Gouraud variant ignores the lighting here).
- Glass push and out-of-bounds: need the owner's property + spot (a repro command).

## Session 2026-11-20 (`main`, later): clouds fixed, the steering wheel turns, the driver's hands follow, gear-shift reach

Owner: clouds rock back and forth and sit too low; "implement the avatar's driving animations (gears, steering wheel)"; thinks the Android is draw-call bound.

### What changed
- Clouds (`ccd4301`): the shader offset the deck by wind*time, so the gust pulses rocked it. The drift is integrated on the CPU (`WindModel::driftXZ`) and passed as an offset; deck raised to 5500-6800 m (rain 3200-4500, cirrus 9500-10200).
- `gm_freeride.cpp` `resolveSteeringWheel` / `updateSteeringWheel`: the cockpit wheel is **`STEER_HR` of the INTERIOR model** (`STEER_LR` of the exterior has a mesh too); both are turned about the local axis along the column (auto-picked, log `FREERIDE: steering wheel node`), road angle x9, +-270 deg, 60 ms smoothing. `-nosteerwheel` = off, `-wheeltest <rad>` forces an angle (negative = left, MEASURED: positive turned it clockwise), `-logwheel`.
- `seated_avatar.cpp` `setDriving`: both hand IK goals are rotated with the wheel (`-nosteerhands` = off); a gear change sends the right hand to `GEAR_LR` (+ IKOffsetDB node 7) and back over 0.6 s (`-noshift` = off) [INF timings: layer-5 clips gear_seq_up/down not decoded].

### Verified
- Cockpit shots at `-wheeltest 0 / 0.6 / -2.5`: the wheel hub rotates. The seated driver's arm moves in the chase shot (`-wheeltest -2.5`).

### Not verified / next
- The hands were not seen up close, and the gear reach was never looked at (needs a side view of the cabin; test profile needs `avatar_config = AVT_Male`). The real clips (`turn.xan` layer 0 weight, `hand_open`, `gear_seq_*`) are not played: next step is layer 0/4/5 of `driveiklayer*.bas` in `SeatedAvatar::update`.
- Draw calls: NOT investigated on the phone. `-logdrawfamilies` prints the per-shader-family draws every 120 frames; the overlay's "draw calls" is the LAST scene only (0 in the menu). Needs a log from the phone in a city.

## Session 2026-11-20 (`main`): Android performance — the 16x anisotropic filter nobody set, a 7.3x texture problem, and the ARM build

Owner: *"we started doing android builds and anyway shape or form you can think of ways to get the game to run well and look good […] not sure if arm optimisations would help and if so maybe looking at rpcs3"* (test device: Honor
Magic 8 Pro, SD 8 Elite Gen 5 / Adreno 840, 12 GB).

Full write-up: **`docs/android_perf.md`** (instruments, levers, the texture survey, the next steps in order).

### What changed

- **`-aniso <1|2|4|8|16>` + texture filtering in the quality profiles**
  (`game_options.h/.cpp`): `TextureQuality` (the retail Video row, 1 trilinear /
  2 4x / 3 16x) was applied but **no profile set it**, so the phone ran 16x
  anisotropic filtering on every texture — the worst case for a driving game
  (road + terrain at a grazing angle). High keeps 16x, Medium/Deck/Legion/60 fps
  4x, **Low 1x**; `-aniso` overrides for a run (`GameOptions::AnisoOverride`,
  not saved); `ApplyVideoOptions` now logs the level in the `Graphics:` line.
  The Android launcher's Phone preset passes `-aniso 1` (`otdu_launcher.cpp`).
- **Advanced settings page: a `Texture filtering` row** (`advanced_graphics.cpp`,
  index 10, live: `ApplyVideoOptions` → `setMaxAnisotropy` +
  `refreshSamplers`), so the setting can be flipped on the phone and watched.
- **`-texcap <px>`** (`texture_cache.cpp`, `createFrom2DB`): drops the mip levels
  above the cap before decoding/uploading (`firstMip`, cube layer stride kept
  correct). Measured: FREERIDE spawn VRAM 1695 → 1567 MB at 512, pixels
  indistinguishable (0.35 / 0.25 / 0.16 mean abs).
- **`DecodeBlockCompressed` rewritten** (`2db.cpp`): table-driven endpoint
  expansion, packed RGBA palette, an interior-block fast path that writes a 16-byte
  row at a time. **Bit-identical** to the old decoder (DXT1/3/5, 14 size classes
  including non-multiples of 4, harness `_bctest.cpp`), 3.4 → 2.6 ms per 1024²
  DXT1+DXT5 pair (2.3 → 3.1 GB/s decoded).
- **`SDL_DelayPrecise` frame limiter** (`tdu_instance.cpp`): the old loop spun a
  big core for the last 2 ms and `SDL_Delay(1)` oversleeps on Android.
- **ARM build settings** (`CMakeLists.txt`, `if( ANDROID )`):
  `-march=armv8.2-a+dotprod+fp16 -ffp-contract=fast -fno-math-errno` (NDK default
  is armv8-a from 2014), `-flto=thin`; opt-outs `-DOTDU_ANDROID_BASELINE_ARMV8A`,
  `-DOTDU_ANDROID_ARM_V9` (armv9.2-a+i8mm) and `-DOTDU_ANDROID_LTO`.

### Verified

- Desktop build clean (`cmake --build build-main`, 43 targets) and a FREERIDE run
  with `-aniso 1 -texcap 512 -showfps -loggputimes`: `Graphics: … texture filter
  1`, `Texture cache: -texcap 512, dropping mips above it (e.g. 'herb_01'
  1024x1024 -> 512x512)`, `[perf] vram: 1567 MB` (1695 MB without).
- Pixel A/B at frame 240 (`_aniso16_ref.ppm` vs `_aniso1_texcap512.ppm`, both
  1280x720 with the same overlay): mean abs diff 0.82 / 0.67 / 0.47;
  `-aniso 1` alone 0.75 / 0.62 / 0.42; `-texcap 512` alone 0.35 / 0.25 / 0.16.
  Screenshots `docs/images/android_aniso1_texcap512.png`, `…_texcap512.png`.
- **APK**: `gradlew assembleDebug` green; the .so inside `app-debug.apk` is 13.06 MB
  (was 13.49 MB), ninja's FLAGS line carries `-O3 -DNDEBUG
  -march=armv8.2-a+dotprod+fp16 -ffp-contract=fast -flto=thin`, and
  `llvm-nm -D` still shows `JNI_OnLoad` + every `Java_org_libsdl_app_*` export
  (the LTO question that would have broken the app silently).
- Texture survey of 500 banks / 2961 textures: widths 64…2048 (only 5 at 2048,
  568 at 1024); stored 528.8 MB BC → 3834.8 MB RGBA8, **7.3x**.

### Not done / next

- **Nothing measured on the phone yet** — the build is there (build 357) and the
  next step is the owner's `-showfps -loggputimes` run, read against
  `docs/android_perf.md` §5.
- **ASTC/ETC2 transcoding** is the structural fix for the 7.3x (§3 of the doc).
- `vkCmdClearAttachments` / mid-pass barriers / unnecessary depth stores (GMEM
  round trips on Adreno), an A/B of the parallel secondary-CB path
  (`-noparallelrecord`), and moving texture decode off the main thread.



## Session 2026-11-19 (`main`): Android port — arm64 APK builds + the integrated launcher start screen

Owner asked to start Android builds (test device: Honor Magic 8 Pro, SD 8 Elite Gen 5 / Adreno 840,
Gamesir controller) with the launcher working for touch + controller.

### What changed
- **Build plumbing**: `android/` Gradle project (AGP 8.13.0 / Gradle 8.14.3 / NDK 28.2 / SDK cmake
  3.31.1, `org.opentdu2.game`, minSdk 29, targetSdk 36, arm64-v8a only) driving the repo-root
  CMakeLists through `externalNativeBuild`; `build_android.ps1` mirrors build.ps1 (deps cache
  `C:\otdu2-deps`, `OTDU_RELEASE_TAG`). Root CMakeLists: `if( ANDROID )` builds `opentdu2` as a
  **SHARED** lib (`libopentdu2.so`, SDL3-static inside — the Java side must NOT load "SDL3"),
  skips `add_subdirectory(tools)`; DLSS/XeSS SDK checks now WIN32-guarded (a local dlss-sdk
  checkout broke the Android configure). `android/local.properties` = `sdk.dir` (gitignored); SDK
  installed to `C:\Android\Sdk` (cmdline-tools + platform-36 + build-tools 36 + NDK r28c), JAVA =
  the Studio JBR 21 pinned in `android/gradle.properties`.
- **Portability fixes** (also un-breaks the red Linux CI): `fsr3_upscaler.cpp` wrapped in
  `#if defined( _WIN32 )` + a stub `#else` (the plugin DLL is Windows-only; FSR1 stays);
  `xess_upscaler.cpp` guard `&& defined( _WIN32 )`; `shared.h` gains `OTDU_ANDROID` + maps
  `_stricmp`/`_strnicmp` to the portable spellings (`interior_walk.cpp` etc. use the MSVC names
  directly — Linux never compiled them); **ogg `config_types.h` is generated for non-MSVC** in
  Dependencies.cmake (os_types.h includes it off-Windows; desktop MSVC typedefs inline, so this
  only bit on Android); `d3d12_device_stubs.cpp` links the empty-on-Android D3D12 backend's 9
  referenced symbols; `main.cpp` includes `<SDL3/SDL_main.h>` on Android; `gs_imgui.cpp` gains
  `/system/fonts` font candidates.
- **Integrated launcher screen** (`source/launcher/otdu_launcher.{h,cpp}`, the owner picked
  "integrated start screen"): runs **before any game data** — its own SDL window + RenderDevice +
  TextureCache + Renderer2D + ImGui (no GSConfig/GSFile) — Play / Game Data / Settings pages in
  the desktop launcher's theme (dark navy + orange accent, cards, sidebar), density-scaled fonts
  (`FontScaleMain`, PushFont sizes are logical px — do NOT wrap them in `S()`, it squares the
  density), touch via SDL's touch→mouse, gamepad nav (`NavEnableGamepad` + focus rects). The Game
  Data page is a real folder browser (std::filesystem over shared storage, `Euro/Bnk` validation
  mirroring `GSFile::HasBankFolder`), All-Files-Access checked/requested over JNI
  (`MainActivity.hasAllFilesAccess`/`requestAllFilesAccess`); settings persist to
  `<SDL_GetPrefPath>/launcher_android.ini`. PLAY sets `OTDU2_GAME_DIR`, hands the window to the
  engine (`adoptBootstrapWindow`, `createWindow()` skipped when one exists) and the normal init
  runs → title screen. Desktop can test it with `-launcher` (Android runs it always,
  `-nolauncher` skips).
- SDL3 vendored Java (`org.libsdl.app`, pinned release-3.4.20 from the deps cache) +
  `MainActivity` (`getLibraries()` returns only `"opentdu2"`).

### Verified
- `build_android.ps1` → `android/app/build/outputs/apk/debug/app-debug.apk` (31.5 MB
  libopentdu2.so, ELF arm64, dex + manifest + icon) — the whole engine compiles under NDK clang
  (SDL3, Jolt, volk, VMA, DXSO, opus/opusfile, imgui).
- Desktop `build.ps1` clean (0 errors) + headless boot exit 0 with all services (the
  `pWindow == nullptr` guard changes nothing for the normal path) + DLSS plugin loads.
- Launcher screen renders on desktop (`-launcher`, screenshots `_bootlauncher_play.png`):
  sidebar nav, hero, status card, disabled PLAY, folder button — correct at density 2.25 after
  fixing the PushFont double-scale. Font/theme/scale numbers from `opentdu2.log`.

### Not done / next
- **On-device test is the open item** (adb is blocked in the agent harness): owner installs the
  APK, copies `C:\TDU2-Gamedata\TestDrive2` to `/storage/emulated/0/TDU2`, grants
  All-Files-Access, picks the folder in GAME DATA, PLAY → title screen; Gamesir must be in its
  Android/S-mode for SDL3 to see it.
- Phase 2 (owner asked for it): auto-updates like the Windows launcher — libcurl + the GitHub
  releases API, APK download + install intent, a **committed release keystore** (needed so
  updates install over each other), `android.yml` CI. Touch tuning of the launcher (hit targets,
  the folder browser on a phone) happens with the owner's on-device feedback.
- ImGui page-switch could not be driven headless (synthetic mouse/keyboard never reached the SDL
  window through Windows DPI virtualization) — the two other pages are unverified by eye; they
  share the widgets the Play page proved.












## Session 62e (`win`): vpos solved; road prop exporter written, but ROADBUILDSTEP is not the road surface

Owner: "yes go ahead with the road prop".

### What changed
- `tools/mod/make_hongkong_roads.py` - turns `C:/temp/hk_roads/*.npz` (decoded `ROADBUILDSTEP` meshes) into one OBJ per 512 m cell (vertex-clustered, upward winding, no collision since the terrain has it). 2.5 m grid -> 383 cells, 1.55 M triangles, 42 MB.

### Verified
- The props load and draw in `-gm worldviewer -island hongkong` (`-campos 14607,170,6600 -camyaw 0 -campitch -50`).
- [DATA] but the source itself is scattered blobs, not ribbons: a plot of one raw tile (X63_Y42, 1660 triangles) is two irregular patches ~15 m across. The overview "dotted chains" are the same patches. So ROADBUILDSTEP is a set of road-build pads/junction patches, **not the road surface**; shipping it would put grey blobs on the terrain. I removed the generated props from the userdata overlay; the exporter stays.

### Spline edges exported (late 62e)
- `level_mesh.py --all --kind SPLINEMESHSKINNERBUILDSTEP --out C:/temp/hk_spl` finished: 387 tiles (6 files skipped: invalid GDeflate / vpos past the blob; `run_tile` now skips a bad file and resumes finished tiles).
- `make_hongkong_roads.py --roads C:/temp/hk_spl --pattern "*SPLINEMESH*.npz" --prefix hk_e_ --grid 1.0 --max-edge 6 --colour 200,200,195` -> 426 cells, 3.3 M triangles, 130 MB. Grid 0.3 gives 16.3 M triangles / 618 MB (too heavy). Loads (513 props), 8 ms frames, no texture warnings.
- [DATA] Result is **fragments, not continuous lines**: at 1 m the ~0.3 m strips collapse, and the decode itself is dashed (parts dropped, LOD0 only). Not a usable road layer yet. In-game map image (owner) shows the real network (cyan tarmac, yellow dirt) at ~17 m/px; a fit of it to the island using the spline points scored 0.077 vs 0.034 base - unconfirmed.
- Next: find why parts drop (meshlet split vs vcount, per-part k) before more export tuning; or trace the map image once registered.

### Not done / next
- Find the real road surface meshes: other mesh names in the tile SCENE files (list names per tile with `level_mesh.parse_named_meshes`, look for spline / lane / asphalt), or the PLM entity transforms that place kit pieces.
- The k exponent source, `vnor`, dealership / garage kit assembly and traffic lights are still open.

## Session 62d (`win`): Hong Kong footprints decoded, box buildings placed as props

Task: "roads and buildings from TDUSC". PLM point ids are direct indices into a float3 table at 0x452e59 (`tools/tdusc/plm_footprints.py`; 97.9 % of 4-point polygons are rectangles).
`tools/mod/make_hongkong_buildings.py` extrudes 1,742 footprints into box buildings (87 props, window texture, heights inferred) - verified in the world viewer over Central.
Trap: props shader ignores `Kd` without `map_Kd`. Not done: collision, real parcel meshes (placement unknown), interiors, roads (no source found). Details: `docs/re/tdusc_pws_format.md` 2m, `hongkong_island_mod.md`.

## Session 62c (`win`): Hong Kong spawn / zone badge / map tuned

Task: "Tune spawn/HUD/map". `-island hongkong` starts at the Central quay (5632, 3, 7936); the zone badge reads "Hong Kong Island" (no more "Ibiza Area 1");
the map screen opens and draws the island with the car marker. Verified headless (`-gm freeride -island hongkong -newdrive -autoplay "400:press=map;700:shot=..."`).
Not done: road / spot overlays on the map, island travel to / from Hong Kong, a real minimap (all need a road graph, see `docs/re/tdusc_pws_format.md` 2d-2l).
Files: `source/game/freeride/gm_freeride.cpp`, `freeride_hud.cpp`, `docs/re/hongkong_island_mod.md`.

## Session 62b (`win`): Hong Kong roads found in the PKG level meshes (decoder partial)

Task: continue the Hong Kong mod. The data after the PWS heightfield is not roads; roads are `ROADBUILDSTEP` P3D meshlet meshes per tile in the PKG.
`tools/tdusc/level_mesh.py` decodes them (u8 meshlet indices, unorm16 positions, Actor translation); verified on X63_Y42 against the heightfield.
Ruled out: the PWS stream tail as a road/building store (mixed half blocks, not identified).
Next: per-mesh Actor translation, then road mask into the colour map. See `docs/re/tdusc_pws_format.md` 2d.

## Session 2026-11-19 (62, `main`): TDUSC Hong Kong island mod - the terrain loads and drives (3 x 3 areas, 269 sectors)

Owner: "With TDUSC can we try get hong kong island to be our 3rd map? as part of a mod?" Plan: PWS decode -> heightfield/meshes to OBJ -> terrain-only `islands/hongkong` -> objects/roads/minimap.
Result: the PWS is not one opaque compressed body. It is plain sections plus **1,364 embedded GDeflate streams per tile** (84 x 21,053 B + 1,280 x 12,861 B), decoded with the existing `gdeflate.py`. `tools/tdusc/pws_scan.py` (new) decodes all of them: X75_Y26 and X57_Y58 -> `ok 1364 bad 0`. Spec: `docs/re/tdusc_pws_format.md`.
Later in the session: stream layout decoded - 256 B header (2 oriented boxes, z up) + 33x33 float heightfield per 16 m cell; `tools/tdusc/pws_height.py` stitched 941 cells of `X63_Y42` (512 m, z 58-282 m) into a seamless heightmap (a local render, not committed (TDUSC art stays out of the repo; regenerate with `pws_height.py --png`)). Then: tile placement solved (x,y = 256*idx - 11008). **The tile is a depth-first quadtree of 1,364 streams (4+16+64+256+1024 nodes), each a 33x33 heightfield (rows top-down in y); parents = children decimated exactly (diff 0.0).** `tools/tdusc/pws_island.py` decoded all 589 tiles (43 min, 18 jobs): 103 km2, z -99..552 m, and the stitched map is unmistakably Hong Kong Island (roads show in the hillshade). My earlier "11 % coverage / other stream kinds" note was wrong (retracted in the spec). Open: the data after the grid, roads/nav/buildings/textures, the Eden terrain writer (`.3dg` 257x257 per island/area/sector). **Then built the mod:** `tools/mod/make_hongkong_island.py` (heightfield + normals patched into Hawaii donor sector / area / island banks, generated DXT1 colour maps, 3 x 3 areas, 1m45s, 388 MB into `<userdata>/mods/.../islands/hongkong`); loads headless (`9 sectors loaded around (5632, 7936)`), the car stands on the quay, world-viewer aerials show the coast and the ridge relief. `gm_world_viewer` gains "hongkong". Spec `docs/re/hongkong_island_mod.md`. Fixed the washed-out terrain: the `.2dm` holds the donor's -c / -s texture name hashes (`0x158`/`0x178`), now patched (`patch_2dm_hashes`); a daylight drive up a green hillside verified (`-tp 14792,191,6868 -weather 12`; use x,y,z, not x,z). Open: roads / GPS, buildings (PWS data after the grid), TDUSC textures, spawn / map tuning. Command: `-gm freeride -island hongkong -skipcine -tp 5632,7936`.

## Session 2026-11-19 (61f, `main`): TDUSC Huracan runs in the game as a car swap, straight from the install

`tools/tdusc/swap_pack.py` (new): `car_assemble.py` output -> `<mods>/carswap/` (body / glass / rim+tyre per axle, `carswap.txt`, a PNG per material). Run:
`build-mainin\opentdu2-main.exe -game C:\TDU2-Gamedata\TestDrive2 -mods C:	emp\otdu2-tdusc-mods2 -gm FREERIDE -island ibiza -skipcine -nopad -fixedtimestep -car 458_italia -tp 12640,10.5,16686`.
Verified: 80 body draws + 9 + 9 wheel draws, offset (-0.002, 0.045, -0.122), all four wheels flipped; rear and side screenshots show the green Performante, tricolore stripe,
calipers, wheels in the arches. Nothing from the rip FBX is used any more. Next: the `_I_` interior P3D, other wheel designs, other TDUSC cars (script is brand-generic apart from
the `LP640_` wheel-name rule), driving check, lights.

## Session 2026-11-19 (61e, `main`): TDUSC wheels placed + the 'paint variants' were the tricolore stripe

`tools/tdusc/car_assemble.py` (new): HR body + 4 wheels (mesh = right wheel, left = mirror) at the LR `Fork_*` anchors, lathe tyre added (the P3Ds have none),
-> `<stem>_assembled.obj/.mtl` + `_wheels.json`. Correction to 61d: Body parts `C1_PAINT_RED/WHITE/GREEN` are the side stripe, not alternatives - no selection needed.
`[INF]`: tyre size and hub height z = tyre radius (R 0.285 front fits the arch top 0.53-0.59). Not checked in Blender / game lighting; spin axis = X.
Next: use the assembled pack in a carswap (`docs/state/main.md` s58 format: body / glass / wheels split), `vtan`, dirt-mask format, wheel designs 02-06.

## Session 2026-11-19 (61d, `main`): TDUSC PMA parameters decoded - paint colour, metalness, gloss in the MTL

PMA record = `u32 nameLen, name, u32 type (5 float / 8 RGBA / 2 texture), value`; colours are linear (C1_PAINT = green, `_RED` = red). `psi_mtl.parse_params` +
`.mtl` `Kd` / `Pm` / `Pr` / `d`. A colour render of HR is a green Performante with the tricolore stripe. Spec 7.5. Caveat: the exporter emits all paint variants
(`C1_PAINT`, `_RED`, `_WHITE`, `_GREEN` parts overlap) - the game picks one. Next: pick one variant / drop hidden ones, wheels at `Fork_*`, `vtan`, dirt-mask format.

## Session 2026-11-19 (61c, `main`): TDUSC PSI / PMA parsed - the OBJ gets per-part materials and textures

`tools/tdusc/psi_mtl.py` (new) + `p3d_to_obj.py`: PSI gives node name -> PMA per part (k-th pair = part k), PMA gives texture slots; the exporter writes
`usemtl`, a `.mtl` and `tex/*.png|dds`. HR: 34 materials, 19 textured; the plate / tail lamps / door label render in the right places. Spec 7.4.
Next: decode PMA's binary parameters (ExternalColor, Gloss, Metalness) for paint colour; wheels at the `Fork_*` anchors; `vtan`; the dirt-mask format.

## Session 2026-11-19 (61b, `main`): TDUSC UVs decoded - `vuvs` = unorm16 pairs, one per UV set

`p3d_to_obj.py` now writes `vt` (UV0). Stride is `nUV * 4` B (the 'zeros' were a wrong stride on LR); unorm16 beats snorm / half on edge-length
smoothness. Verified by a textured render of LR with BODY_LR (rear detail lands on the rear) - only a partial look: mesh -> texture needs `PMA`.
Next: parse `PMA` (material -> texture slots) + `PSI`, then write per-part materials (.mtl) so the OBJ shows textured; `vtan` still undecoded.

## Session 2026-11-19 (61, `main`): TDUSC Huracan - node transforms applied AND the textures convert to DDS (53 of 53)

The owner: "do both" - apply the node transforms, and textures to DDS (TDUSC is the Steam install on E:, part of our mod).

### What changed
- `tools/tdusc/p3d_to_obj.py` - `parse_nodes()` reads the scene graph (two tables, one pre-order id space, 4x4 row-major local matrices,
  parent = deepest node whose id range holds the child) and the exporter instances each mesh at its node's world matrix; `--local` = old output.
- `tools/tdusc/ptx_to_dds.py` (new) - `PATX-SP2` -> DX10 DDS (mip 0) + optional PNG preview; `--all <prefix>` converts a folder.
- `docs/re/tdusc_pkg_format.md` sections 7.2 (scene graph) and 9 (PTX layout, formats); `C:/temp/tdusc-evidence/tdusc_huracan_*.png`.

### Verified
- HR with the graph applied: bbox (-1.11, -2.41, 0)..(1.11, 2.11, 1.03) = the LR header box to 2 cm; `--local` had z down to -0.43. Door / mirror /
  window / wiper / bumper nodes land on the body (`C:/temp/tdusc-evidence/tdusc_huracan_hr_nodes.png` vs `_local.png`). Tree: 62 nodes, `Root` nDesc = 61.
- PTX mip entries are GDeflate streams; 53 of 53 Huracan textures convert, no error. Looked at: `_DIFF` 8 bpp = **BC3** (logo sheet, dashboard,
  BODY atlas clean), `_NRM` = **BC5 signed**, 4 bpp `_DIFF` / `_IOR` = BC1, wheel `_AO` greyscale.

### Ruled out
- BC7 for the 8 bpp diffuse: the same bytes decode to noise, BC3 is clean. BC5 unorm / BC3 / BC7 for `_NRM`: noise; signed BC5 is right.
- The header carries no pixel-format field (identical `hdr` words on BC3, BC5S and BC1 files): the format is bpp + file-name convention.

### Not done / next
- **Dirt masks** (`*_MASK_DIRT`, 8 bpp, 5 files) decode to noise under BC3 / BC5 / BC5S / BC7: format unknown, not needed for the look.
- Only mip 0 is written (the packed tail of mips 128 and below, 0x5e10 bytes, is not unpacked).
- **UVs**: `vuvs` (8 B / vertex) is still undecoded, so the OBJ has no texture coordinates - the next step to see the car textured
  (try 2 x half2 / unorm16 against the DIFF atlas). `vtan` too. Then `PMA` (mesh -> texture slots) and a carswap pack.
- Wheels: `W_*.P3D` are separate files; place them at the LR `Fork_FL/RL` anchors (mirror for the right side). `Fork_*` is in LR only.
- Not pushed.

## Session 2026-11-18 (60, `main`): TDUSC Huracan - decoded: GDeflate decoder works, the car's meshes export to OBJ

The owner: "Decode a car (Huracan)". Spec section 7 of `docs/re/tdusc_pkg_format.md`; reader `tools/tdusc/tdusc_read.py`.

### What changed
- `tools/tdusc/tdusc_read.py` (ls / info / strings / export by game path; `export` writes only into a folder the owner names).
- `docs/re/tdusc_pkg_format.md` sections 7-8.

### Verified
- `GRAPH\CARS\LAMBORGHINI\HURACAN` = 194 files; `PSI` lists mesh -> material paths; `P3D` plain section lists per-mesh bounding
  boxes, vertex formats (`TANBNORMPOS_UV0_UV1_`: vtan 4 B, vnor 8 B, vpos 12 B, vuvs 8 B), node names and a collision hull.
- The blob header is GDeflate: id 4 / magic 0xfb / tiles / 64 KiB tiles / cumulative tile ends; decompressed size matches the main
  header on 13/13 files (e.g. W_R_03 = 3 x 65536 + 9884 = 206,492).

### Ruled out
- raw deflate, zlib, gzip, LZ4 block, zstd (+ magicless), brotli, lzma raw, bz2 on the blob: all fail.
- DirectStorage via ctypes (`DStorageCreateCompressionCodec(1, ...)`) -> `E_NOTIMPL`: my guessed interface id (or the call) is wrong;
  11,019 GUID-like constants in the DLLs scanned, none accepted.

### Later in s60: the geometry decodes
- The owner approved fetching Microsoft's open-source GDeflate source (sparse clone of `microsoft/DirectStorage` `GDeflate/`, 247 KB,
  into the scratchpad, nothing run from it). `tools/tdusc/gdeflate.py` is my pure-Python port of `shaders/GDeflate.hlsl`'s algorithm
  (32 lanes, per-lane 64-bit buffers, shared refill pointer, Deflate64 tables, stored blocks per the shader).
- `tools/tdusc/p3d_to_obj.py`: Huracan LR -> 5 parts / 841 triangles, bbox exactly the header's, a recognisable Performante in Blender;
  HR -> 38 meshes / 90 parts / 55,217 triangles in 4.5 s; wheel `W_F_01` -> 8 parts / 7,670 triangles. Renders are outside the repo
  (`C:\temp\tdusc-export`). Needs numpy for the OBJ writer (`python` without `-I`).
- Ruled out earlier in the session: the ctypes DirectStorage route (`E_NOTIMPL`), the "second mask key" (hexdump overlap illusion).

### Not done / next (updated)
- Apply the **node transforms** (the table after the mesh entries: names `D_CAR`, `Fork_FL`, `Engine`, doors ... with matrices) so doors,
  wheels and suspension land in place; decode `vuvs` (8 B: likely 2 x half2 or unorm16) and `vtan`; the `PMA` material layout and
  `PTX` -> DDS for the look.
- Then a TDUSC car -> a carswap pack (`docs/state/main.md` s58): body / glass / wheels from the decoded parts, no re-capturing needed.

### Superseded next-steps (kept for the record)
- **A GDeflate CPU decoder** (Microsoft publishes the format and a reference decoder; needs fetching or writing), then: split the decoded
  blob by the stream offsets, build an OBJ of the Huracan LR body, view it in Blender, then HR / wheels, `PMA` + `PTX` for the look.
- `PTX` -> DDS, the PMA parameter layout, PWS compression.

## Session 2026-11-18 (59, `main`): TDUSC .PKG format - records decode AND the name table is solved: 154,623 named files

The owner installed TDUSC from Steam and asked to "dig through the game ... mod pack ... clothes, cars, islands,
physics". TDUSC is **not Unreal** (own engine, no .pak; the earlier FModel suggestion was wrong). The owner confirmed no
DRM / anti-cheat (SARD was scrapped) and approved decoding the masked part. Spec: `docs/re/tdusc_pkg_format.md`.

### What changed
- `docs/re/tdusc_pkg_format.md` - the layout found so far. `tools/tdusc/pkg_scan.py` - a read-only lister (CSV into a folder
  the owner names; never writes to the game folder, never extracts asset data).

### Verified
- `python tools/tdusc/pkg_scan.py --pkg <CHUNK_0_PART0005.PKG> --out C:\temp\tdusc-scan --limit 60 --payloads` decodes 60/60
  LZ4 `PA__` records; `--pkg <CHUNK_2_PART0001.PKG>` lists 99 `PATX-SP2` textures (47 x 1024^2, 20 x 4096^2, 17 x 2048^2 ...).
- Not encrypted: the records are plain LZ4 frames / raw BC1 texture data; a part is mostly 0x30 padding.

### Later in s59: the TOC is solved
- The name table at the start of each part is XOR-masked with the 32-byte text key `oL7bFfNRojSzr4y7t6s7wWMIA0w03zDz`
  (period 32 from offset 0, same in all 23 parts, read off the zero cells of the table). Entries: `u32 pathLen`, path, 88-byte tail
  `{u32 flag, u64 offset, u64 FILETIME, u64 frameBytes, u64 decodedBytes, u32 kind, 16-byte hash, 32 zero}`. Validated: 300/300
  offsets of CHUNK_0 part 5 hit `PKGB`, frame / decoded sizes match exactly. `pkg_scan.py --toc` -> 154,623 entries, 53.1 GB
  decoded, all 23 parts (CSV into `C:\temp\tdusc-scan`, not the repo).
- The game's contents (by extension): 69,755 PTX textures, 30,184 PMA materials, 20,557 PSI, 18,951 P3D models, 8,301 PACLOG,
  3,035 PAN animations, 395 PSK skeletons; folders: `GRAPH\CARS` 22,176 (45 brands incl. LAMBORGHINI: Centenario, Diablo VT,
  Huracan ...), `GRAPH\CHARACTERS` 8,114 (AVATAR male 5,297 / female 1,971 / NPC 819), `LEVELS\IBIZA` + `HONGKONG` by TILE_X, `GRAPH\PROPS`
  8,016, `GUI\LIVERYEDITOR` 1,774.
- Open: one PAN (EMOTE_MICHELIN_FLAG) fails the pure-python LZ4 decode (multi-block detail?); the PSI / P3D / PMA / PSK formats are
  not read yet; the PWS compression is unidentified.

### Ruled out
- "The PKGs are encrypted / one XOR key": the first 2 MB (the table of contents) is masked, the content is not; the
  self-similarity peak is the 160-byte row, not a key period.

### Not done / next
- ~~Solve the masked TOC~~ DONE (above). Next: the `P3D` model + `PSI` + `PMA` formats of one car (e.g. `GRAPH\\CARS\\LAMBORGHINI\\HURACAN`), `PTX` -> DDS.
- PWS compression (header offsets known, body entropy 7.8-7.95).
- PATX -> DDS (confirm BC1 vs BC3/BC7 per header), `PA__` vertex-buffer strides, then cars (a PA__ group + textures).
- Nothing extracted or committed from the game; the owner decides what a mod pack may carry.

## Session 2026-11-18 (58, `main`): a ripped TDUSC Lambo is a DRIVEABLE car (visual swap on a donor) - no hosting, no assets in the repo

The owner: "make it a driveable vehicle, we just can't host it" (the car from `E:\TDUSC Rips\Cars\Lambo fixed.fbx`).
**Design [INF]**: a retail car stays the *donor* for physics, sound, gearing, collision and the wheel nodes; the mod
only swaps what is DRAWN. No Lamborghini exists in this install (no licence), so the donor is the **Ferrari 458 Italia**
(`458_italia`, mid-engine, 2.77 m wheelbase). The swapped car drives exactly like the 458 and looks like the Lambo.

### What changed
- `source/game/freeride/car_swap.{h,cpp}` - the data-driven loader: `carswap/carswap.txt` in the mods overlay
  (`[swap:<donor _car_file_name>]`, `body = <obj>`, `wheel_front/rear = <rim obj> <tire obj>`, four `wheel_pos_*`
  engine-space centres), OBJs from `carswap/` via `LoadObjDraws` (TD_Gouraud), `-nocarswap` disables it.
- `source/game/cinematic/cinematic_car.{h,cpp}` - `setVisualSwap`: the car hides every retail item
  (`ForceHiddenItems`), draws the swap's body / glass / interior on `BODY_HR` as `nodeT * inverse(restT)` (so roll / pitch
  follow), and each wheel at OUR wheel centre with the donor node's rotation (spin + steer, scale normalised) plus its
  travel from the apply pose; the engine-positive-x side turns the mesh 180 deg like the retail rims. The model is aligned
  to the donor at apply time: our wheel centroid -> the donor wheel nodes' centroid (origin + ride height). Applied lazily
  on the first `draw()` (the renderer exists then); cleared in `unload()`. Retail lights / glow nodes still draw (hidden items
  only - the lamp glow stays; the headlights glow on the Lambo).
- `tools/tdusc/blender_car_split.py` - now exports into `<mods>/carswap/` + writes `carswap.txt`; **levels the rip**
  (yaw 3.31 deg, roll -1.82 deg measured from the four rim centres) and exports one centred rim + tire per axle.
  `tools/tdusc/car_mtl_fix.py` - the solid-colour PNG + `d`/alpha fix for the new names.

### Verified (build 293)
- `-car 458_italia` (freeride, `-island ibiza`): log `CarSwap: 1 swap(s)`, the body / interior / glass / wheel OBJs load,
  the car is the Lambo with the 458's yellow gauges; side view shows spoked wheels, glass, the lit headlights.
- Driven headless: `-car 458_italia -snaproad 150 -logdrive -autoplay "40:hold=accel;..."` -> 83.7 km/h, gear 2, steer
  +-0.527, `contacts 1111` (screenshot `_drv1`, not committed: ripped art).
  Run: `opentdu2-main.exe -game ... -mods C:\temp\otdu2-tdusc-mods -gm FREERIDE -island ibiza -skipcine -nopad -fixedtimestep
  -car 458_italia -tp 12640,10.5,16686`.

### Wheel check (same session, build 294) - fixed two bugs, now verified
- **Ride height**: aligning our wheel centroid to the donor's wheel-node y sank the car 17 cm (the nodes' y is in a
  body-lifted space). Now `swapOffset.y = tireRadius - our mean wheel y` (log `aligned, offset (-0.047, 0.018, -0.019)`).
- **Wheel side**: the donor's wheel nodes all carry the retail convention "local +x is OUTWARD" (rest axle x +0.87 on
  the positive side, -0.87 on the negative: `CarSwap: wheel 'D_RIM_FL' side 1 rest axle x 0.87 -> flipped`, all four), and our
  mesh's outer face is -x, so **every** wheel is turned 180 degrees about the vertical. The rule is geometric (flip when
  `-row0.x` of the rest pose points the wrong way for the side), not by name. First tries that did NOT work, with the test
  that ruled them out: flip only the positive side -> one side shows the inside of the tyre; no flip -> both sides wrong; strip a
  mirror (negative determinant) -> all four determinants are +1.00, there is no reflection.
- Verified headless: `-car 458_italia -snaproad 150 -autoplay "30:hold=accel;60:look=1,0;200:shot=...;203:shot=..."`: 73 km/h,
  both sides show the spoked rims facing out, the front wheel is steered with `hold=steerleft`, and the spoke pattern
  differs between two shots 3 frames apart (the wheels SPIN). Screenshots not committed (ripped art).

### Ruled out
- Matrix order: a post-multiplied flip (`wheelT * Flip`) like the retail rim scale; a pre-multiplied flip would turn the
  wheel's world position, not the mesh.
- The first spawn at `-tp` hit a tree (0 km/h, wheels spinning) - add `-snaproad 150`, it is not a swap bug.

### Not done / next
- Not swapped: shadows / reflection passes and the damage / paint / doors / lights models (the retail light glow still
  draws; nothing deforms); the paint colour (flat red PNG - `TD_CarPaint` needs the retail cube map); the garage / dealer
  lists still show the 458. Every 458 in the world (traffic, showrooms) gets the swap too - scope it later by owner car id.
- `CarSwaps::unloadMeshes()` is not yet called at shutdown.

## Session 2026-11-18 (57, `main`): TDUSC bring-your-own props - a ripped Lambo renders in Ibiza (tools only, no assets)

The owner: "a mod that, using the same bring-your-own-legally-owned-games rules, adds stuff from TDUSC into our
game ... we're still not hosting". glm had already written `tools/tdusc/glb_props.py` (Ibiza.glb scene rip ->
the OBJ props route); this session took `E:\TDUSC Rips\Cars\Lambo fixed.fbx` through the same route.
**No TDUSC data is in the repo** - the tools read the owner's files and write into a mods folder
(`C:\temp\otdu2-tdusc-mods`); no screenshots of ripped art are committed either.

### What changed
- `tools/tdusc/glb_props.py` (glm, committed here) - glTF scene capture -> `props/*.obj` + `props.txt` lines; repairs
  the rip's broken BIN chunk / u16-as-u32 index accessors / wrong index offsets, dedupes the double captures.
- `tools/tdusc/blender_car_split.py` - Blender headless (`blender -b --python ... -- 1.0`): imports the car FBX,
  dedupes the triple capture, splits body / glass / rims / tires (by material + poly count), **squashes Z by 0.565**,
  rotates +X-front to -Y, centres, grounds on the tire minimum, writes UVs for the tires and exports 4 OBJs.
- `tools/tdusc/twodb_to_png.py` - TDU2 `.2db` (DXT1/3/5) -> PNG, for the game's own tire maps in `common_car.bnk`.

### Verified
- Run: `opentdu2-main.exe -game ... -mods C:\temp\otdu2-tdusc-mods -gm FREERIDE -island ibiza -skipcine -nopad
  -fixedtimestep -tp 12640,10.5,16686 -heading 0 -autoplay "300:shot=..." -exitframe 330`; log
  `ModProps: 'tdusc_lambo_*' ... shader TD_Gouraud` for all four, tire texture `assigned=1`.
- **The rip is vertically stretched ~1.75x** [DATA]: rims 0.94 m tall (real 0.53), car 1.98 m (real 1.14). After
  `Z *= 0.565` the car is 4.68 x 2.35 x 1.12 m = an Aventador SVJ. Visually checked next to the stock California.
- TDU2's tire maps (`ts_tire_s/_v/_cgv`, `common_car.bnk`) are **normal maps / masks**, not colour; the tread atlas is
  lit from `ts_tire_s` into a dark colour map (sidewall rings are procedural [INF]).

### Ruled out
- `TD_CarPaint2` on the body -> renders **black** (it needs the retail cube map / paint params).
- `Kd` in the `.mtl` does nothing: the loader's no-texture `TD_Gouraud` variant is flat white (`obj_mesh.cpp` ~l.361), so
  every part needs a (solid-colour) `map_Kd` PNG. Blender writes `Ka 1 1 1`; set `Ka`/`Kd` to 1 when a texture is used.

### Not done / next
- Tread / sidewall never inspected closer than ~10 m (a few pixels): check with `-tp 12636,10.5,16690`.
- Body is 199k tris (decimate before real use); flat red, no gloss.
- **Glass done (later in s57)**: `obj_mesh.cpp` now alpha-blends a material whose `.mtl` has `d < 1` (or `Tr`): SRCALPHA /
  INVSRCALPHA, no depth write, two sided (the rain materials' bytes). TD_Gouraud takes alpha from the **texture**, so the glass
  PNG must carry alpha too (`tools/tdusc/car_mtl_fix.py` writes both). `blender_car_split.py` now also splits a dark `interior`
  group (56 meshes inside the cabin box [INF]). Verified: the cabin and the trees read through the roof (`-tp 12637,10.5,16690`,
  build 292); judged at ~10 m only. No reflections (that is the CarPaint cube-map blocker).
- Drivable: needs a "visual swap" car (clone an existing DB car's physics, mount these OBJ parts on its wheel
  anchors from `anchors.txt`), the same shape as the tuner parts. FModel exports of an installed TDUSC would add textures.

## Session 2026-11-18 (56, `main`): tuner garage - card UI, spoiler finishes, exhaust pop flames

The owner: "give it a proper UI ... exhaust tips customizable chrome, burnt, gold etc... look good as if they
belong in the game ... options for spoiler like painted, carbon fibre etc ... also want flames to come from
custom exhausts for the pops".

### What changed
- `tuner_menu.*` rewritten: a stack of **cards** (neon-edged header with shop / car / wallet, Spoiler, Spoiler
  finish, Exhaust tips, Tip finish, Pop flames) with price / FITTED / PICKED pills, colour **swatch strips** for
  every finish and flame style (the swatch is the finish's own average pixel colour; the Painted one is the car's
  body colour), keycap hints. Up/Down = card, Left/Right = option, A/Enter = fit / pick, B/Esc = leave.
- Finishes are per slot now: `[finish:<id>]` takes `slot = spoiler|exhaust` and `paint = body`; saved as
  `<slot>_finish` (`exhaust_finish` is unchanged, so old saves keep working). 11 spoiler finishes ship: Painted
  (body colour), Gloss Black, Pearl White, Race Red, Racing Blue, Candy Orange, Primer Grey, Twill / Forged /
  Blue / Gold Carbon (new textures `paint_gloss.png`, `carbon_twill.png`, `carbon_forged.png`, generated by
  `tools/mod/make_spoiler_tex.py`). The "Painted" texture is baked from `CinematicCar::getBodyPaintColour()` when
  applied: repaint the car, then re-enter the garage / respawn to re-match.
- **Pop flames** (port-native [INF]; nothing in the original draws exhaust fire): `[flame:<id>]` blocks (Classic
  Orange, Blue LPG, Violet, Toxic Green, Magnesium White), `tip = x y z` lines on each exhaust part, saved as
  `exhaust_flames = <id>|off` (absent = first style). `VehicleAudio::takeFiredPops()` reports each pop the audio
  fires; `GMFreeRide::updateTunerFlames` spits a muzzle flash, hot core, coloured tongue, falling sparks and soot
  from every tip via `EdenSurfaceFx::emitBurst`. Only a fitted tuner exhaust spits (the stock pipe does not).
  `CinematicCar::get/setTunerAttachmentTips` give the world-space pipe mouths.
- `-tunerfit` now takes `<slot>_finish` / `exhaust_flames` keys too and the pops follow the same loadout.
- Loader fix: a CRLF `tuner.txt` left a `
` in `mesh = wing_gt.obj` (OBJ never found); the parser strips it.

### Verified (build 290, headless)
- `-car GTR -tunermod -tunerfit "exhaust=exhaust_quad_ti;exhaust_finish=blueburnt;spoiler=wing_gt;spoiler_finish=s_painted;exhaust_flames=orange"`
  + `100:hold=accel;260:release=accel` : log `Tuner: N pop flame(s) 'orange' from 4 tip(s)` on every pop;
  `docs/images/tuner_flames_s56.webp` (painted wing matches the body, blue-burnt quad tips, orange fireballs).
- Garage: `-interior ETNR_A -nointeriorintro -walkaim Hostess_area` + a press script buys Dual Element, picks
  Race Red, buys Burnt Twin, picks Burnt Titanium, picks Magnesium White flames: log lines
  `spoiler_finish 's_red'`, `exhaust_finish 'burnt'`, `exhaust_flames 'white'`; `docs/images/tuner_menu_s56.webp`.
- Trap: the test userdata (`C:	emp\otdu2-win-userdata\mods\packages	uner-garage`) holds a COPY of the
  package - copy the repo's `resources/mods/packages/tuner-garage` over it after editing `tuner.txt`.

### Not done / next
- Flame size / colour are tuned by eye on one car in the chase view; by ear + by eye in the cockpit view
  unverified. No flames in the garage room itself (the room scene has no surface-fx pass): a "test fire" on the
  flame card is the next step (`tuner_menu.cpp`, emit at the room car's `getTunerAttachmentTips`).
- Finish / flame prices: all free. Spoiler finishes are not on the Impreza / Delta spoilers' stock wings (they
  only dress the tuner wing mesh).
- Body kit, neon, plates still "coming soon".

## Session 2026-11-18 (`main`, 55): tuner mod - tip finishes (chrome / burnt / gold... per exhaust)

The owner: "upon buying can it be possible to choose how they look between chrome, burnt
etc... love this burnt look on exhausts but want to all each exhaust to be able to have a
different colour look to it or even if we can also maybe apply a custom colour to it would be
cool in case people want a gold tipped exhaust" (reference photo: deep blue rim over a gold
bore).

### What changed
- `tuner.txt` - `[finish:<id>]` blocks (name, texture, optional `tint = r g b` baked into the
  pixels at load): Burnt Titanium, Blue Burnt (new `titanium_blue.png`, the photo's look),
  Chrome, Carbon, Gold (chrome.png tinted), Copper Burnt (tinted titanium), Ceramic Black and
  Gunmetal (tinted `ceramic.png`).
- `tuner_parts.*` - the finish catalog, `findFinish`, the tinted-texture cache
  (`finishTexture`, GPU-owned, freed in `unloadMeshes`), `applyPart(..., pFinishId)` and
  `applyFinish` (re-colour a fitted part in place), `applySaved` reads the `exhaust_finish`
  key of the same profile map (unknown ids skip like unknown part ids).
- `cinematic_car.*` - `setTunerAttachmentTexture` (the attachment's material is a per-car
  copy, so two cars can wear one mesh in different looks).
- `tuner_menu.*` - the **Tip finish** row under the exhaust row: Left / Right cycles (mesh
  default first), the room car previews live, validate saves free into
  `OwnedCarTunerParts[..].Parts["exhaust_finish"]`; close() reverts finish preview before part
  preview (the part revert re-applies the finish).

### Verified (build 289, headless)
- Gold and Blue Burnt quad on the GTR (`docs/images/tuner_finish_gold_s55.webp`,
  `tuner_finish_blueburnt_s55.webp`); the menu run buys Dual Chrome, cycles four finishes
  live (one texture load per finish in the log) and saves - profile.sav
  `car_tuner_parts = 403:exhaust=exhaust_dual;exhaust_finish=gold;spoiler=wing_street`
  (`tuner_finish_menu_s55.webp`); a plain respawn wears the saved gold; a bogus finish id
  logs `not in the catalog (kept as data, not drawn)`.

### Not done / next
- Finishes are free (the part's price covers them) - per-finish pricing if the owner wants;
  no finish preview swatch in the row (name text only); the asset viewer's authoring loop
  does not cycle finishes yet.

## Session 2026-11-18 (`main`, 54): tuner mod - exhaust tips rebuilt, five new parts

The owner: "in the tuner mod please could you start adding more items also add some polish to
the exhausts I Had claude work on some but think they could do with a little something they
look a little odd".

### What changed
- `tools/mod/make_gt_wing.py` - the tip builder: cylindrical UVs (u around the tube, v along
  the pipe; the old planar map put the burnt gradient ACROSS each tip - the session 47
  "coloured egg" blobs), a rolled rim (the lip curls out and back onto the bore), a cone
  throat closing dark and deep, a winding self-check per quad; chrome / titanium textures
  repainted to the layout, `carbonsteel.png` added; five new meshes (`exhaust_n1`,
  `exhaust_carbon`, `exhaust_quad_ti`, `exhaust_bigbore`, `wing_blade`).
- `resources/mods/packages/tuner-garage/tuner/tuner.txt` - five catalog rows (15 parts),
  `exhaust_oval` offset y 0.25, Impreza `exhaust_offset = 0 0.26 -0.03`.
- `_session/preview_part.py` (scratch) - offline painter's-algorithm OBJ preview, so part
  looks iterate without a game run.

### Verified (build 288, headless)
- Catalog: `Tuner: 15 parts, 7 tuner cars`; the Wangan Works menu lists and prices the new
  sets, the room car previews live (`docs/images/tuner_menu_s54.webp`).
- All 8 tip sets on the GTR: chrome sets read as pipes with rims and dark bores in the
  diffuser cutouts (`tuner_exhaust_quad_s54.webp`), burnt titanium is muted violet-blue rim /
  gold bore, no more candy-purple (`tuner_exhaust_burnt_s54.webp`, `tuner_exhaust_oval_s54.webp`).
- Second body: the Impreza takes `align = rear` tips + the per-car exhaust lift
  (`tuner_exhaust_impreza_s54.webp`); the Evo Blade replaces the GTR's stock lip and the
  Impreza's stock STI wing via the existing hide boxes (`tuner_blade_carbon_s54.webp`).

### Not done / next
- The single oval still floats mid-bumper on the GTR (no centre cutout on that body - style,
  the material is right now); pops audio still by ear unverified; body kit / neon rows remain
  "coming soon".

## Session 2026-11-18 (`main`, 53): TDU Media - music, forecast schedule, News and DLC channels

The owner: "yes please do the tv music, forecast icons and the news and dlc channel thanks".

### What changed
- `source/game/freeride/weather_cycle.{h,cpp}` - `ScheduledIntempery` / `RetailBlock` / `RetailHour`; the retail clock follows the schedule (`setIsland`), `gm_freeride.cpp` passes the island.
- `source/game/spots/interior_walk.{h,cpp}` - TV music, forecast icons from the schedule, News + DLC channel lists (`buildNews`, `updateMediaNews`), toast, `-dumpmediacmd`, `-probedlc`.
- `docs/re/houses_spec.md` 15.1.

### Verified (build 288, headless, 0 VUIDs)
- music log `FrontEndMusic: 'tdmedia'`; forecast rain/cloud/moon icons; News list + details + tick + delete + tab switch; DLC 15 cars / 24 clothes, Buy toast.

### Ruled out
- Setting the news line variables on the content clip: nothing shows; the text fields' `../../../:news_title` is the line clip.
- Forcing no key for tab change (`sendKeyTo /lists/tabs`): no effect; `/lists:changeTab` works.

### Not done / next
- DLC Packs / Furniture / Stickers tabs have no data rows; `emptyListText`; the Hsys help channel; the schedule changes the freeride weather behaviour (the old random walk is only for test clocks) - the owner should confirm the new rhythm feels right.

## Session 2026-11-18 (`main`, 52): closet avatar camera + live clothes, Leaderboards offline panel

The owner: "Both please" (Leaderboards and the closet camera).

### What changed
- `source/game/walkmode/walk_mode.{h,cpp}`, `walk_controller.h` - `startSelfView( scene, bFront )`, collision-probed closet camera, `sweep`.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - `refreshPlayerAvatar` (retire, don't free).
- `source/game/spots/interior_walk.{h,cpp}` - `came` -> self view, pick refreshes the avatar, Leaderboards `drawLeaderPanel`, `-logdiary`.
- `docs/re/houses_spec.md` 16.

### Verified (build 287, headless)
- closet: avatar in third person facing the camera wearing the cowboy hat; picking a hat rebuilds it; `-vk_enable_validation_layer` 0 VUIDs. Leaderboards: 3 injected records listed (restored the profile after).

### Ruled out
- One 2.2 m collision jump: hops over thin walls, camera ended in the next room (looked at a wall); 0.25 m steps fixed it.
- First yaw sign had the avatar's back to the camera.

### Not done / next
- Per-category closet zoom; forecast icons / News / DLC channels of TDU Media; TV music; the Leaderboards records are our own list (no retail data to read).

## Session 2026-11-18 (`main`, 51): TDU Media found and wired - the house TV

The owner: "Find TDU Media". It is `tdmedia.bnk` / TDMEDIA.vmf, a second Flash movie behind the `td_media` selectable.

### What changed
- `source/game/spots/interior_walk.{h,cpp}` - `openMedia` / `closeMedia` / `routeMediaInput` / `updateMediaWeather` (own `FlashPlayer`, `/:db_tdmedia`, weather tile labels), `-dumpmedia`, `-logmedia`.
- `docs/re/houses_spec.md` 15.

### Verified (build 286, headless)
- A on the TV opens it; Weather channel shows Ibiza + Hawaii outlines, sun/sun/moon by game hour; B / B closes and the walk resumes. `docs/images/house_tdu_media_weather.webp`.

### Not done / next
- Forecast icons are always clear (WeatherCycle is FREERIDE-only and random); News / DLC channels empty offline; the TV music (`tdumedia.wav`); Leaderboards lists; closet avatar camera.

## Session 2026-11-18 (`main`, 50): house Diary - photo Zoom / Delete + offline MyTDULife

The owner: "both please" (MyTDULife and photo zoom/delete).

### What changed
- `source/game/spots/interior_walk.{h,cpp}` - Zoom (answer to the movie's `sndp:wrong`, native full-screen, Left/Right/A/B), Delete (Y twice, file moves to `photos/deleted/`), `drawLifePanel` (MyTDULife offline summary), `AlbumTexture` cache.
- `docs/re/houses_spec.md` 14.1.

### Verified (build 285, headless)
- zoom log `My Photos: zoom 0 ...`, delete log `deleted ...photo_002.png (ok)` and the file in `photos/deleted`; shots `house_my_photos_zoom.webp`, `house_mytdulife_offline.webp` (Level 0 Inglorious matches the pause menu).

### Ruled out
- Forcing the slot frame to `taken` / `taken_on` made Zoom reachable but wrecked the slot art (thumbnails shrank) - reverted.

### Not done / next
- Leaderboards (Instant / Multiplayer lists) and the `td_media` selectable (no media label in HOME.vmf - which movie is TDU Media?); closet avatar camera; MyTDULife is a native panel, not the retail book.

## Session 2026-11-18 (`main`, 49): house Diary - the My Photos page is filled

The owner: "work on some of the screens ... start implementing TDU Media, TDU House and TDU Life as well as walk in closets for houses". Survey: the closet (clothes worn on the avatar) and MyTDUHouse (furniture, colours, walls, floors) were already in (houses_spec 10-13); what was empty is the rest of the Diary.

### What changed
- `source/game/spots/interior_walk.{h,cpp}` - `updateAlbum`: My Photos shows `<userdata>/photos/photo_*.png` in the retail book (lure bitmap override, captions, pager); `phpa` / `phph` / `phsp:phcl` handled; `-dumphomeclips <cmd>` / `-dumphomedepth`.
- `source/flash/gs_flash.cpp` - `-dumplabelslate`.
- `docs/re/houses_spec.md` 14.

### Verified (build 283+, 0 crashes)
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -enterhouse EPH_L3DI_5067I -walkaim diary -autoplay "300:press=validate;440:press=down;460:press=down;480:press=validate;700:shot=..."`: 6 test PNGs -> page 1/2 four thumbnails + name; page 2 photos 5-6. `docs/images/house_my_photos_album.webp`, `house_my_photos_page2.webp`.

### Not done / next
- MyTDULife is a Back-only wait screen (`mylf:self`/`mylf:wait`): needs its protocol decoded (dump `-dumphomeclips mylf:wait`) and an offline stats fill (money, level, cars, houses from the profile).
- Photos: Zoom / Delete. Leaderboards lists. `td_media` selectable ("This is not wired yet", interior_walk.cpp ~1062) - HOME.vmf has no media label; find which movie TDU Media is.
- Closet: `came` avatar camera still ignored.

## Session 2026-11-18 (`main`, 48): mod sticker packs + full-car vinyl packs

The owner: "make custom menus using the real UI ... more stickers in the packs ... a unique Vinyl pack ... full car like nfsu2 ... a sticker marketplace ... a tool for pure custom vinyl/sticker creations" - started with (a) mod sticker packs and (b) full-car vinyl packs.

### What changed
- `source/game/freeride/sticker_bank.{h,cpp}` - `<mods>/stickers/<id>/pack.ini` + PNGs become extra catalog banks (`mod_<id>`); `name/price/mono/vinyl/cars` keys; textures owned and destroyed by the catalog; `-nomodstickers`. `docs/re/sticker_mod_packs.md` is the spec.
- `source/game/freeride/sticker_shop_ui.cpp` - vinyl entries place at Scale 1 / aspect-corrected to cover the canvas; packs with a `cars` list are hidden for other cars (`bankFitsCar`).

### Verified
- Build 276 clean. `-stickertest mod_testlivery,0` with a 1024^2 stripe vinyl: log `mod pack ... [vinyl]`, `1 image(s)`, canvas bound on 2 paint materials; `docs/images/sticker_mod_vinyl_test.webp` shows the stripe + tint on the California.

### Not done / next
- Not driven through the SHOPSCAR sticker shop UI (grid / buy) - run `-atspot` sticker shop with a pack in `<userdata>/mods/stickers` and check the grid shows it. Only 15 entries per pack show (`kGridSquares`).
- Vinyl UV templates (`car_obj_export` is the starting point), a packaged/hashed pack archive (marketplace), then the tuner UI and auctions.

## Session 2026-11-18 (`main`, 48): car viewer + OBJ exporter + the tuner authoring loop

Owner: "I had a tool made for openTDU2 ... I was hoping we can improve it somehow and add a car viewer and exporter I want to make cars more modular ... some cars have spoilers already GTR as example but need a method to remove it without destroying the car and so we can swap the spoilers also need something for exhaustes this is for our customization mod".

### What changed
- `source/render/eden/eden_model.*` — `setKeepParsedGeometry`: the parsed .3dg (every primitive's MeshVertex streams + indices) survives load; items record their source primitive (`SourceGeometryHash` / `SourcePrimitive`). Tools-only (one extra geometry copy per model).
- `source/game/cinematic/cinematic_car.*` — `Desc::bKeepPrimitiveData` (no `clearPrimitiveData` after the wipers: the viewer's live hide-box edits re-run `collectTunerMasks` via the new public `refreshTunerMasks`), `Desc::bKeepParsedGeometry`, the `TunerMask` struct + `getTunerMasks()` made public, accessors (`getFileName`, `getInteriorModel` / `getRimModel` / `getRimRearModel` / `getTireModel`).
- `source/game/freeride/tuner_parts.*` — the viewer's live edits: `setRuntimeHideBoxes` / `setRuntimeSlotOffset` (runtime overrides `getHideBoxes` / `offsetFor` return first), and `hide_tris_<slot> = <item>: <tri>,...` parsed into `TunerCar::HideTris`.
- `source/game/cinematic/cinematic_car.cpp` `collectTunerMasks` — groups by box OR exact tri list (runtime + file).
- `source/game/assetviewer/car_obj_export.{h,cpp}` — NEW: the whole assembly (ext / interior / rims / tire) as OBJ + MTL + `anchors.txt` under `<userdata>/export/<car>/`, the round-trip law below.
- `source/game/assetviewer/gm_asset_viewer.*` — the Cars panel: car list (279 rows, display names + tuner badges), `-car` / `-cartab` / `-exportcar <name|*>` / `-objroundtrip` flags, the loaded car drawn in the 3D view (BODY_HR camera fit, static CUBECAR env - the IntroCinematic pattern - without it the paint draws black), node picking, the tuner authoring loop (fit live, hide-box overlay + masked-triangle highlight, drag-edit boxes / offsets through the runtime overrides with live `refreshTunerMasks`, copy `tuner.txt` lines to the clipboard + log).

### Verified (headless, build 276)
- Export GTR: 129 ext items / 29,978 tris + interior 39,479 + rims 1,216 x2 + tire 19,427, `anchors.txt` (bounds, node translations, the tuner data).
- **Round-trip**: `-objroundtrip .../GTR_ext.obj` re-imports through the game's own OBJ loader -> bounds delta 0.0000 PASS. The law: engine x negated + faces reversed on write (the involution of obj_mesh.cpp's import mirror), positions car space, hide boxes stay ITEM space (anchors.txt states the conversion).
- Viewer: `-gm assetviewer -car GTR -tunermod -imguiscale 1.0` - the GT-R renders (env-cube paint), the `hide_spoiler` box wireframed exactly over the stock boot lip (`docs/images/car_viewer_gtr_maskbox.webp`).
- `hide_tris_`: GTR box = 656 maskable; adding `hide_tris_spoiler = 7: 0,1,2` -> 659. (Test line removed after.)
- Garage regression: `-tunerfit "spoiler=wing_gt;exhaust=exhaust_quad"` - same maskable counts as session 47, the A/B pair `docs/images/tuner_ab_stock_s48.webp` / `tuner_ab_fitted_s48.webp` (stock lip + round tips vs GT wing + quad tips, no doubles). The stock part still comes back on unfit (index-range mask, untouched mesh data).

### Ruled out / notes
- The stale copy of `tuner.txt` in the test userdata (`<userdata>/mods/packages/tuner-garage/`) shadowed the repo package for the first runs - anchors.txt faithfully showed the stale data. Re-copied the package; the launcher's install does this for the owner.
- The viewer panels use `DpiScaled` positions: at this machine's DPI scale the right-side windows land off a 1280x720 backbuffer (pre-existing; `-imguiscale 1.0` for the test shots). Car mode now draws only the Cars panel + the 3D view so both fit.

### Not done / next
- Triangle-click mask painting (the runtime-override plumbing is ready; needs a triangle-exact raycast on the kept primitive vertices) - `docs/re/car_viewer_spec.md` 7.
- Texture export (the MTL carries flat Kd; the art stays in the banks).
- `-exportcar *` re-exports cars that share a file name (harmless, slow).

## Session 2026-11-18 (`main`, 47): tuner mod - six spoilers, four exhaust-tip sets, exhaust pops

Owner: "the tuner mod, can you add more content to it, more spoilers, exhaust tips, and work on the enhanced audio if we can for exhaust pops etc."

### What changed
- **Slots**: `CinematicCar` holds one attachment per slot (`setTunerAttachment(slot, ...)`, `clearTunerAttachment(slot)`); `TunerParts::applySaved` dresses a car from its whole saved loadout (replaces the three hard-coded "spoiler" blocks in gm_freeride.cpp / interior_walk.cpp). Slots: `spoiler`, `exhaust`.
- **Menu** (`tuner_menu.*`): one row per slot the car takes. Up / Down (D-pad up / down) pick the row, Left / Right cycle its parts, validate buys.
- **Parts** (`tools/mod/make_gt_wing.py`, own art): spoilers `wing_swan` (14,500), `wing_dual` (11,800), `wing_drift` (16,500); exhaust `exhaust_dual` (3,200), `exhaust_quad` (5,400), `exhaust_oval` (4,600), `exhaust_burnt` (6,800) with new `chrome.png` / `titanium.png`. All 7 tuner cars take the exhaust slot.
- **`align = rear`** (tuner.txt): exhaust offsets are relative to the car's own body bounds (`getBodyBounds`: z past the rear face, y above the body bottom), so one entry fits every car length.
- **Audio** (`vehicle_audio.*`): `setTunerPops(strength)`. Three synthesized pop samples (noise crack + falling thump, [INF] no retail pop sample is used), played on the ExhaustPipe client (BackFiring panel 2) at the car: 1-4 pops on a throttle lift above 3,200 rpm plus a ~1 s overrun crackle, 1-2 pops on an upshift under throttle, 1-3 on a downshift. Strength = the car's `pops =` (tuner.txt, default 1) x 1.0 with an exhaust fitted / 0.6 stock; 0 for non-tuner cars or the mod off. Set from `GMFreeRide::applyTunerParts`.

### Verified
- GTR wear runs (`-car GTR -tp 12615,16658 -heading 90`, userdata copy with `car_tuner_parts = 403:exhaust=...;spoiler=...`): all 7 new meshes load and draw - `docs/images/tuner_swan_quad_s47.webp`, `tuner_dual_oval_s47.webp`, `tuner_drift_burnt_s47.webp`.
- `-logtuner` drive (hold / release accel at 6,500 rpm): `VehicleAudio: tuner pops xN` lines on shifts, on the lift and as overrun crackle at throttle 0.
- Build OK (build 269+).

### Not verified / ruled out
- **Nobody listened to the pops** - the sample levels, pitch and rate are by-ear guesses; tune `buildPopClips` / `kPopMinRpm` / the burst counts in vehicle_audio.cpp. A wrong sound is a data-sized fix.
- The garage menu rows were not screenshotted (it compiles and the part logic is exercised by the wear runs; walk into ETNR_A and press Up / Down to check the row highlight).
- ~~Stock tips / spoiler lip showing under the new parts~~ fixed in the follow-up below.
- Flames and turbo flutter / blow-off not done.
- The installed copy in `<userdata>/mods/packages/tuner-garage` is a copy: after editing the repo package, re-run the launcher install (or copy `tuner/`) before testing.

### Follow-up (same session): the stock spoiler and exhaust tips are replaced, not doubled
Owner: "detach the spoiler under the custom one; if the car already has a quad exhaust, remove and replace it."
- **Exhaust**: the car's own `EXHAUST` node (the stock tips, [DATA] `-logcaritems`) is hidden while an exhaust part is fitted and shown again on stock (`CinematicCar::applyTunerMasks`).
- **Spoiler**: the GTR's boot lip is triangles of the body `Paint` item. `collectTunerMasks` (at load, mod on, car has `hide_spoiler` boxes in tuner.txt) reorders that item's index buffer as [exhaust box][rest][spoiler box] and `EdenModel::setItemMesh` re-points the item at the contiguous range, so a fitted spoiler drops the lip. Box for the GTR: `hide_spoiler = -0.95 0.935 -2.30 0.95 1.05 -1.97` (548 triangles). Find boxes with `-dumptunermask` (prints every rear triangle centre) + `-logtuner` (counts). The model X is mirrored on screen.
- Chrome texture darkened (the tips rendered white).
- Verified: `docs/images/tuner_stock_parts_hidden_s47.webp` (swan neck + quad on the GTR: no lip under the wing, no stock tips); lip and tips come back with stock.
- **Only the GTR has a `hide_spoiler` box** - the other six tuner cars still show their stock spoiler (if any) under a fitted wing. Each is one data line after a `-dumptunermask` run (`-car <name>`). Their stock tips already hide (every car checked has `EXHAUST`? - only the GTR was looked at).

### Follow-up 2 (same session): the other tuner cars measured
- `-dumptunermask` now dumps for every car (`MASKCAR` = has an EXHAUST node, `MASKTRI <car> ...` = triangle centres of the rear half). `-tunerfit "spoiler=wing_gt;exhaust=exhaust_quad"` fits parts on the driven car without touching the save (test flag).
- **370Z**: no stock spoiler; wing deck `spoiler_offset = 0 0.86 -1.85`. **Impreza WRX STI 2007**: stock wing is body Paint above the 0.86 m deck - `hide_spoiler = -0.80 0.872 -2.10 0.80 1.30 -1.65` (785 triangles), `spoiler_offset = 0 0.86 -1.95`. **Delta Integrale Evo 2**: hatchback, no stock spoiler; wing on the hatch roof edge `spoiler_offset = 0 1.12 -1.40`. All three have an EXHAUST node (stock tips hide).
- `align = rear` now uses `CinematicCar::getRearBounds` (BODY_HR plus the separate `?UMP_R_H` rear bumper node): on the 370Z / Delta / Impreza the bumper sticks out past the body bounds and the tips sat inside it.
- Evidence: `docs/images/tuner_four_cars_s47.webp` (GTR, 370Z, Impreza, Delta with a ducktail + quad tips).
- **350Z_CPE, RX8_Cpe, Escort_RS_Cosworth have NO model in this install** (`CinematicCar: '<car>' not available, using the California`): they are in the DB and tuner.txt but cannot be measured or shown - they draw as the California. Not removed from the catalog.
- Impreza: the lower rear bumper has a hole in the middle with or without parts (the car's own render, not the mod).

### Next
- Listen, then tune the pops. Then flames (a sprite at the exhaust per pop, position = the `align = rear` tip) and a body kit slot.

## Session 2026-11-18 (`main`): full garages block dealer purchases and offer the sell list, cars move between garages, the for-sale house card

Owner: the three open items (buying a second house moves no cars; no screen to move a car between garages; the card of an unowned house uses the realtor layout) + "if you have 1 house with 2 slots and it is full, cars can't be bought from a dealership and it offers to sell a vehicle" (360 shots: "Not enough space in the garage(s) / Would you like to sell one or several vehicles?" Yes/No, then the trade-in list with Status / Category / Vehicles / Price columns, "You need: One spot in your garage", Select / Sell / Back).

### What changed
- `house_shop.{h,cpp}`: `GarageTotalCapacity()` (sum of `DBHouse._nbcarslots` of the owned houses), `GarageFreePlaces()` (capacity - `OwnedCars`; -1 = no limit when no house is owned), `MoveCarBetweenGarages()`.
- `gm_shop.cpp` `buyOrEvaluate` writes the SHOPSCAR `/:EnoughMoney` cases read from `TestMoney` (shopscar.vmf 0x11e48) [DATA]: 0 = poor but cars to sell, 2 = garages full + cars to sell, 3 = poor, 4 = full and nothing to sell. The movie raises its own dialogs `em_0` / `em_2` / `em_3` (texts "Not enough space in the garage(s)" / "Would you like to sell one or several vehicles?"); no purchase happens while the garages are full. `dealer_menu.cpp` (native dealer) refuses the same way.
- **The trade-in list** (`/buy/reprise`, shopscar.vmf S573): the movie builds `list = ASFlashList(...)` - a native class of the exe. `flash_player.cpp` now gives that call a small object (cursor + view size, `GetViewLineSelected`, `GetViewSize`, `OnRollOver`) and `FlashPlayer::getListSelection / setListSelection / getListGeneration / sendKeyTo` expose it. `gm_shop.cpp` drives the list: rows = owned cars (class badge from `_vehiclecategory`, tuning level icon, name, price), Up / Down move + scroll (5 view lines), A ticks (`shca:addv`), X (`opt1` / key X) = Sell (`/:BuyOK` etc. as `X()` reads them), the confirm text fills `§NB_VEHICLES§` / `§PRICE§`, `shca:selv` sells the cars (profile money, `OwnedCars`, paint / rims / stickers / tuning dropped, garages normalised), then the purchase evaluates again. The stats panel follows the cursor car. Text fields of a line are on `line<N>/:name` / `/:price` (a text char's `../` is the clip holding it).
- Sale value = 75 % of the dealer price [INF: the retail valuation is not read; a model no dealer sells has no price and its row is locked].
- The dealer boxes show **Yes / No**: `/:YesNo` was copied as the string "true" (the movie wants a boolean) - fixes the "OK only" box (also noted in tuning_ui_spec).
- `estate_menu.cpp`: buying a house places the cars that stood in no garage into the new one (`HouseGarageCars`); selling a house is refused when the cars no longer fit in the other houses (the movie's `m_NoGarPlaces`), and its cars move out.
- **Move a vehicle**: `garage_move_menu.{h,cpp}` - native panel (the retail page was not found: the SHOPSCAR / Home garage pages only hold the "Select vehicle / Move vehicle" hint bar) [INF]. In a house garage interior press **X (pad) / M (keyboard)**: pick a car, pick the house (shows `Garage n / capacity`), A moves it (`house_garages` saved). The room shows the moved cars when it loads again (a live re-place left the old cars standing).
- `map_screen.cpp`: an unowned `EPH_*` house opens the movie's `forsale` card (was `estates`, the realtor list): capacity, level stars, `/:HousePrice` (the card shows no price field).

### Verified (profile with the Caravan = 2 places, 2 cars; `-gm SHOP -shopspot ECD_AUDI_1276I -shopaim 6 -money 500000`, the Audi buy key recipe)
- Log: `eval Audi R8 V10 FSI Quattro: the garages are full (2 car(s) in 2 place(s)) -> EnoughMoney 2`, dialog `em_2` with the 360's texts and Yes / No (`docs/images/garage_full_dialog.webp`); Yes -> `sell list opened with 2 car(s)` (`dealer_sell_list.webp`: tick, `You need:` text clears, GT-R stats on the right), X -> confirm "Confirm sale of 1 vehicles for $54,000" (`dealer_sell_confirm.webp`), A -> `sold 1 car(s) for $54000 (money now $554000, 1 car(s) left, 1 free place(s))`, then `eval ... <= money 554000`.
- A purchase with free places still works (`bought Audi R8 ... car 442`).
- Move panel: `-gm FREERIDE -skipcine -island ibiza -atspot EPH_L0C_1254I -housegarage` + autoplay `500:press=validate;900:key=M;970:press=validate;1040:press=validate` -> `Garage: car 387 moved from 'EPH_L0C_1254I' to 'EPH_L3DI_5067I'`, profile `house_garages = EPH_L0C_1254I=403|EPH_L3DI_5067I=387` (`garage_move_panel.webp`); the map home card of the Caravan then lists only the GT-R (1 / 2).
- `-mapcardicon estates,10 -mapcardextend` (`map_forsale_card.webp`): the for-sale card with Garage capacity 2 and 1 star.

### Ruled out
- `fake`-driven movie cursor (the movie's Up / Down only runs while `_root.fake` is set: the retail exe never sets it for the list) -> the game moves the cursor itself.
- `ui.sendKey( 13 )` reaches the shop's walk A handler too (all visible key buttons get it): the list uses `sendKeyTo( "/buy/reprise", key )`.

### Not done / next
- Not checked with vision: the scroll of a list longer than 5 rows (code path only), the em_0 (poor, cars to sell) flow, the scroll bar lift (not moved), the "Select" hint label in the list bar is covered by the walk-pad icon (the movie's own).
- The retail valuation of a car and its car-move screen are still unread; the move panel is native.
- The story (non-free-ride) shop has no garage limit.

## Session 2026-11-18 (`main`): per-house garages, garage cars wear stickers + spoiler, the map's home card

Owner: "garage cars stickers and spoilers; a car with a spoiler / stickers must be remembered; check owned properties remember the cars in the house's garage" (360 shots: the map card of Fontanelles house / Caravan: picture, Garage 2/2, level stars, Vehicles list with class badges).

### What changed
- **Per-house garages.** `PlayerProfile::HouseGarages` (house spot -> car ids, saved as `house_garages = SPOT=id,id|SPOT=...`). `HouseGarageCars( spot )` (`house_shop.cpp`) assigns every owned car that is in no garage to the home house first, then the other owned houses, up to `DBHouse._nbcarslots` (`HouseGarageCapacity`), drops sold cars / unowned houses, and remembers the result (log `Garage: car 387 goes to the garage of ...`). `InteriorWalk::placeGarageCars` shows THIS house's cars only (it showed every owned car in any house).
- **Garage cars wear their own look** (`placeDisplayCars( ids, bOwned )`): ordered / custom paint, rims, the tuner mod's spoiler, and a sticker canvas per car (`GarageStickers`, composited and bound in `InteriorWalk::renderOffscreen`).
- **Bug**: `TunerParts::isTunerCar` was asked before `load()` (also in `dressRoomCar`), so no spoiler was ever drawn on room cars; fixed in both.
- **Map home card** (`map_screen.cpp`): an owned `EPH_*` spot uses the movie's `home` kind - title = the house name, the house's `realtorscr/<house>/img_ext` picture (loaded for owned houses), `/:nb_cars` of `/:nb_garage_places`, `/:HouseLevel` (level + 1 stars), `/:car<N>_gp` (class) / `_car_name`.

### Verified (profile `persist2`: cars 387 + 403, stickers on 387, `car_tuner_parts 403:spoiler=wing_ducktail`)
- `-atspot EPH_L3DI_5067I -housegarage`: both cars in the garage, the California green with its stickers; with `-tunermod` vs `-notunermod` the GT-R shows a ducktail (26 changed px at its tail); profile now holds `house_garages = EPH_L3DI_5067I=387,403`.
- `-mapcardicon estates,18 -mapcardextend`: "Sa Cala house", picture, Garage 2 / 6, 3 stars, California (A3) and GT-R (A2) (`docs/images/map_home_card.webp`).

### Not done / next
- Buying a second house does not move cars; no UI to move a car between garages (retail has the car-move screen, not read). The unowned `forsale` card still uses the realtor layout. The class badge digit comes from `VehicleClassName( _vehiclecategory )` as in the dealer card (not compared with the 360's "A2" for the GT-R / R8).

## Session 2026-11-18 (`main`): stickers and the other car places

Owner: "check the sticker layouts and the other car places".

### What changed
- `interior_walk.cpp` `placeDisplayCars( ids, bOwned )`: the HOME GARAGE put the owned cars on the showroom's rotating paint rows (`i % numPaints`); owned cars now wear their ordered paint (else the model's own colour), custom paint and rim size.
- `gm_cin_viewer.{h,cpp}` (the car wash scene, `C_WASHING`): the scene car got the ordered paint only; it now also wears the custom paint, rims and the sticker layout (own `StickerCanvas`, bound in `drawOffscreen`). Log `CINVIEWER: scene car 387: custom paint 1, rims 21, stickers 3`.

### Verified (profile `persist2`: California 387, custom green paint, 21" rims, 3 stickers; headless)
- Tuning shop (`-island hawai -atspot ETUN_A_3136`): green car with rims on the lift; pixel diff against the same profile without stickers = 6,182 px on the car, the stickers are dark marks on the rear deck. Sticker shop (`ESTICK_B_8570`): car on the turntable, stickers present (diff 766 px at distance).
- Home garage (`-island ibiza -atspot EPH_L3DI_5067I -housegarage`): the green California in D_CAR_01. Car wash (`ECW_A_3764` on Hawai): green car, `sticker canvas bound on 2 paint material(s)`.

### Not done / next
- Garage cars wear no stickers (one canvas texture per driven car; each garage car needs its own canvas) and no tuner spoiler. The dealer test drive uses the dealer's car by design; the photographer uses the live car. Story cutscene cars (gm_cin_viewer with `bPlayerCar` false, gm_cine) unchanged.

## Session 2026-11-18 (`main`): hairdresser cancel / beard page checked, saved looks and the car in shops audited

Owner: "check the hairdresser cancel and beard page work; then check every bit of detail is remembered in a shop (haircut, clothing, face...) and in car places (sticker shop, tuner) - when walking in you should see your car".

### What changed
- `hair_ui.cpp`: the **beard page's Style row** sends a bare `hadr:edit:pilo:type` (no `scne` / `scpr`), which was ignored - the type never changed. Now handled (`bTypeRow`). Cancel (red-X icon -> "Would you like to cancel?" -> `hadr:edit:canc`) reverts the preview to the worn look (log `preview hair -1` = the preset hair). A page-level Back (`hadr:edit:maha:canc`) keeps the pending change and price in the square menu, as the movie intends [INF].
- `eden_avatar.cpp`: `IsPlayerConfig` - a profile with an empty `avatar_config` (FREERIDE started directly) is the `AVT_Male_Intro` the rooms fall back to, so its worn clothes / face edits / tints now apply (they were silently skipped). `-logworn` prints player / worn counts.
- `gm_freeride.{h,cpp}` `dressRoomCar`: ONE place that puts the driven car's paint, custom paint, saved rim size and tuner-mod spoiler on a shop room car, used by the sticker shop, the tuner garage and the tuning shop. `bindTuneRoomCar`: the tuning shop's car is on the lift when you walk in (it used to appear only after the movie's page opened); sticker canvas bound to the tune / tuner room cars too.

### Verified
- Crafted profile (`C:	emp\otdu2-persist`: worn hair 261, beard 241, glasses 307, hat 419, top 163, bottoms 133, tints 6, 49 face params, age / freckle 0.9, all in `clothes_owned`): the hairdresser and clinic head views wear all of it (`-logworn`: `6 worn`, sunglasses, cap, vest, freckled skin visible).
- `-island hawai -atspot ETUN_A_3136` + validate: car on the lift, log `room car 'California' wears ... rims 21`; `ESTICK_B_8570`: the car on its turntable after turning in the room.

### Not verified / next
- Stickers on the tuning / tuner room cars (profile had none); beard page's "Cut" label stays "Clean shaven" after a style change (the movie re-reads it only on a cut move); worn clothes are dropped on load when not in `clothes_owned` (by design?). Other places that need the car (car wash, photographer, dealer test) not audited.

## Session 2026-11-18 (`main`): hairdresser pages redraw the hair live

Owner: yes to "make the haircut pages redraw the hair live".

### What changed
- `hair_ui.{h,cpp}`: `setLookSink` + `previewLook()` - after every type / cut / colour change and on Cancel the working look (hair id + colour, facial hair id + colour) goes to the sink; `-` log `HairUi: preview hair ...`.
- `gm_freeride.cpp`: the sink puts the look into `profile.WornClothes` (parts 3 / 9) and `HairColor` / `BeardColor` for ONE `scene.setPlayerAvatar( ... )` (the avatar reads the profile only at load), then restores the profile - nothing unpaid is ever stored.

### Verified
- `-interior EHD_B -autoplay "400:press=validate;480:press=validate;...press=down/right..."`: log `preview hair 259 colour 0` -> `261` -> `colour 1`; the avatar shows "Shaved head 2", tint 2, price `$250` (`docs/images/hairdresser_live_preview.webp`).

### Not done / next
- Cancel's revert path and the facial-hair page (Pilosity) were not driven headlessly; each preview reloads the avatar (no cost seen, not profiled). The preview rebuilds the avatar, so a pending surgery face edit is not involved (separate room).

## Session 2026-11-18 (`main`): the hairdresser uses the same head-orbit camera

Owner: "make the hairdresser use the same head orbit camera ... avatar sat in a chair, pan to the front and the back, not all the way round" (360 frame: profile, portrait wall behind, the barber leaning in).

### What changed
- `interior_walk.cpp` `playHeadView( distance )`: the surgeon's C_HEAD + orbit code moved into one helper; `hadr:open` calls it too (`ihd_b.bnk` has its own C_HEAD with an `Avatar` actor). The orbit (right stick / mouse, +-166 deg) already runs for any room whose main cinematic is C_HEAD.
- `IntroCinematic::setHeadOrbit( on, distance )`: optional camera-to-head distance. The hairdresser's C_HEAD camera sits 2.6 m from the head (the clinic's 0.8 m), the 360 frame needs about 1.3 m (head ~51% of the frame height, fov 0.384 rad), so the hairdresser orbits at 1.4 m [INF]. `-logorbit` prints the distance; `-logmain` now prints the fov.

### Verified
- `-interior EHD_B -autoplay "400:press=validate;470:shot=..."` + `look=1,0` / `look=-1,0`: profile view like the 360 frame, then the front, then behind the head (`docs/images/hairdresser_head_view.webp`).
- Ruled out: `cam_haidresser` is the reception desk camera, not the chair view.

### Not done / next
- The hairdresser's haircut pages do not yet redraw the hair live in this view (`HairUi` is not wired to `setPlayerFaceEdit`-style updates); the barber's idle pose differs from the retail frame.

## Session 2026-11-18 (`main`): surgery sliders redraw the face live + the head camera swings round (owner video)

Owner: "make the sliders redraw the face live. You sit on the bench and can pan the camera left and right in a semi circle" (video `2026-11-18 12-46-15.mp4`: profile -> front orbit of the head, black tee, sliders change the face as they move).

### What changed
- `eden_avatar.{h,cpp}` `setFaceEdit`: copies the 49 FaceParamArray bytes (the FaceFX evaluation reads `getFaceParams()` every frame) and writes age / freckle into the TD_Skinface materials. `intro_cinematic.h` `setPlayerFaceEdit` forwards it to the player avatar.
- `surgery_ui.{h,cpp}`: `setFaceSink` callback, fired on bind, every slider update, Random and Cancel (cancel restores the baseline face). `gm_freeride.cpp` wires it to `interior.getScene().setPlayerFaceEdit`.
- Head orbit: `IntroCinematic::setHeadOrbit/addHeadOrbit` rotate the C_HEAD camera about the avatar's `Head` bone (yaw clamped to +-1.45 rad ~ 166 deg); `InteriorWalk::tick` feeds it from the mouse X / right stick X while the surgeon page is up. RS right = behind the head, RS left = front.

### Verified
- `-interior ESUR_A -autoplay "400:press=validate;480:press=validate;560:press=validate;660..:press=right|left"`: log `SurgeryUi: HeadDepth = 15` vs `= 0`, price `$100`, mean pixel difference of the head region between the two runs 8.0 (identical timing).
- `look=1,0` / `look=-1,0` held: the head stays centred while the camera goes from profile to behind / to the front (frames in the session).

### Not done / next
- Default view: the retail one starts nearer the profile and the avatar wears a black tee (ours: olive shirt from the intro preset). Orbit speed / limits are INF. Hair salon could reuse the orbit. Eyes colour / eyebrow type choice boxes not wired.

## Session 2026-11-18 (`main`): surgery shows the head close-up (360 comparison)

Owner: "check surgery, see if it shows this camera preview" (360 recomp shots: head close-up on the green striped wall + the Head page list).

### What changed
- `interior_walk.cpp` (`surg:cine:intr`): plays the bank's `C_HEAD` as the main cinematic, drops the walk camera override, `clearMirrorPlayer()` (the walk's body is mirror-only, which hid the avatar) and `useMainCamera()`; `wmon` (menu closed) restarts the walk. `-nosurgeryview` = A/B, `-surgcam <Entity__Camera>` test hook.
- `intro_cinematic.{h,cpp}`: `useMainCamera()` (C_HEAD has CinProps 0x52 = NoCamera but carries a camera track at (-27.1, 6.98, -5.05) [DATA]), `getEntityCameraNames()`, `-logmain` (cinematic props + object names), `-logroomcams`.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -interior ESUR_A -autoplay "400:press=validate;440:shot=..."`: log `surgeon view C_HEAD -> 1`, head close-up with the menu over it (`docs/images/surgery_head_closeup.webp`); cancel gives `wmon` and the walk resumes (A reopens it).

### Ruled out
- The room camera `Cam_Hostess` is the surgeon's seat view, not the head shot.

### Not done / next
- The 360 frame shows no bamboo plant at the right and a black tee (ours: olive shirt from the intro preset, bamboo in view): the retail view may orbit (RS Camera) or use C_360 / a different time offset. Slider changes redrawing the face live not looked at.

## Session 2026-11-18 (`win`): every map shop card shows its building picture

Owner: "implement all the shop previews" (360 sample: clothes card, B-Miami).

### What changed
- `source/game/map_screen.cpp` `loadRealtorPreviews` / `fillSpotCard` / `bindRealtorPictures`: every `mapscreens/spots/<full spots.bin name>.bnk` is hashed (`GetIdentifier64bit` of the upper-case stem) = the spot record id [DATA], loaded, and bound to `/tool_tip/infos/picture/lure_map` for any card kind (dealers, clothes, tuning, sticker, hair, wash, surgery, photo, casino, school...). Banks whose picture is named after another bank (`ecd_bri1_1470i` holds `ecd_bri1_5203i`) fall back to the bank's own `.2db`.

### Verified
- `-mapcardicon clothes|cardealer|tuning -mapcardextend`: pictures draw (clothes = same building as the 360 shot); log `73 of 73 pictures loaded` (was 21).

### Not done / next
- Done in a follow-up: the clothes card's rows (Hats / Upper / Lower / Sunglasses, page 2 shoes / gloves) and "Clothes bought" now count the shop's brand stock (DBCarShop._clothesshopbrand) for the player's sex at the current area level; `/:Is<Part>` flags + `/:Nb<Part>Bought|Total` (map.vmf `setClothes`). Verified with `-mapcardicon clothes -mapcardextend` (0/9 at area level 0; the 360's 5/100 is a late-game profile). Page 2 (shoes, gloves) not looked at.

## Session 2026-11-18 (`main`, 70): the real-estate card - one agency's houses, with pictures

The owner (retail screenshot of the "Real estate" popup): "certain properties are locked to a location, can we implement it with the previews also".

### What changed
- **The lock is DBHouse._realtor** [DATA] (each house names its agency, e.g. `ERE_B_2236`); the in-world estate menu already filtered by it, the map card did not. `map_screen.cpp` `fillSpotCard` now fills the movie's `setRealtor` data (read from the AVM1 with `tools/vmf_disasm.py` on the extracted `map.vmf`): `/:House<i>_name / _price / _gCapacity / _level / _state`, `/:NbHouses` (the agency's count, not the island's) and `NbHousesBought`; cheapest first [INF], stars = `_level + 1` (the stars clip's frame n shows n-1 stars [INF]).
- `FlashPlayer`: a `Math` object (`ceil floor round abs sqrt min max pow sin cos atan2`) - `setRealtor` computes its page count with `Math.ceil( NbHouses / nbLines )` and the runtime had none, so `nb_pages` stayed undefined. After `SetInfos` the card calls `tool_tip.initList()` (page counter, folio arrows), Left / Right page the list (SWF keys 1 / 2: `cardPage`, D-pad left / right).
- **Previews** [DATA]: `frontend/allres/realtorscr/<house spot>.bnk` (`img_ext`, `img_gar`, `img_lvg`) and `frontend/allres/mapscreens/spots/<realtor>.bnk` (the agency building). Created at spot load (`loadRealtorPreviews`, 21 textures on Ibiza, a texture created mid-frame crashed the driver in the phone-call work), bound each frame to `/tool_tip/infos/txt/pages/house<slot>/lure/picture` by the slot's own `id` variable (the movie scrolls the slots) and `/tool_tip/infos/picture/lure_map` (agency). The card title is "Real estate" like the retail screenshot.

### Verified
- `-mapcardicon estates,19 -unlockallspots -autoplay "200:press=map;360:press=validate;480:shot=..."`: "Real estate", the agency building, Sant Jordi residence $110,000 and Sant rafael hacienda $118,000 with thumbnails, 1 star each, folio 1/5; after `press=right` page 2 (Porroig house, Santa Eulària residence) with its own thumbnails. Matches the owner's screenshot.
- Dev hooks added: `-dumpmapclips <path>` (clip tree of the map movie at tick 480).

### Not done / next
- The thumbnail is `img_ext`; `img_gar` / `img_lvg` (garage, living room) are for the retail realtor screen, not the card. Houses on Hawaii and the other agencies were not screenshotted. "Houses bought" counts the owned houses of the agency; the tester profile owns one (the retail capture shows 0). The order of the list (price ascending) is a guess.

## Session 2026-11-18 (`main`, 69): shops stock by area level

The owner: "do the next progression slice" (the open item of 68: "New items available" was told, not enforced).

### What changed
- `DBClothe._min_arealevel` (0..4: 280 / 206 / 56 / 39 / 23 items [DATA]) is read into `ClotheItem::MinAreaLevel`. `skill_levels`: `SetCurrentAreaLevel` / `IsStockedAtAreaLevel`; `gm_freeride.cpp` keeps the level of the zone under the car (DBAreaLevel from the zone's ALP, see 68). The clothes shop's brand stock (`interior_walk.cpp`) and the hairdresser's cuts (`hair_ui.cpp`) list only items the current area level stocks. Tuners have no such field in the data read so far [INF: the reward text names them too].

### Verified
- Fresh profile: `FREERIDE: area level here 1, 209 of 327 clothes stocked`; with Ibiza Area 2 seeded to 950 ALP (roads + 20 events): `level 3, 304 of 327`. The shop screens themselves were not driven.

### Not done / next
- A worn item above the level disappears from the hairdresser's list; tuner stock; the retail `NewAreaTitle` popup page; whether the shop's own zone (not the car's) should decide [INF: the car is at the door].

## Session 2026-11-18 (`main`, 68): area levels (DBAreaLevel) and duels

The owner: "please do Duels and DB Arealevel".

### What changed
- **DBAreaLevel** (`skill_levels.{h,cpp}`: `GetAreaLevels`, `GetAreaPoints`, `FindAreaLevel`): the area level points (ALP) of a zone are the Cruising points of the DBRewardXP events named `CRUISING_<ISLAND>_..._AREA<n>` (roads, wrecks, events won) times the times earned [INF: DBAreaLevel's 200..1700 scale fits the ~1200 points those events pay per area; the dev exe loader `0x49a1c0` reads `_alpneeded`, the earner was never found]. `_island` 0 = Ibiza, 1 = Hawai, -1 = the default table [INF]. `gm_freeride.cpp`: crossing a level announces "<zone>: Familiar - New items available" on the pop_news banner.
- **Duels** (`gm_freeride.{h,cpp}`: `DuelSpot`, `loadDuelSpots`, `startDuel`, a prompt branch beside the championships): DBEvent `_eventtype` 11 rows (DUELC4A, B4A, A5A, A2A, B3A) at their spotworld position, the event tile within 40 m; A starts `ig2/duel*.ig2` through `startLicenseTest` with one rival (`_pnj1`) in a car of the duel's class (`_championship` is a vehicle category: 17 = C4 [DATA]). First place pays `RACING_DUEL_<class>` (+60 / 120 / 160 / 200 Racing) and the banner "Duel won against <rival>"; a won duel says so on a second visit. Open once a championship of the class is complete [INF], `-opendevduels` skips that.

### Verified
- Seeded profile (`done_CRUISING_IBIZA_ROADS_AREA1_10/25/50` = 190 ALP) + `-awardevent CRUISING_IBIZA_ROADS_AREA1_75`: `area level up Ibiza Area 1 300 points -> level 2 'Familiar'` and the banner.
- `-tp 11576,15490 -opendevduels -opponentdifficulty 0 -licensewarp -autoplay "200:press=validate"`: "licence test duelc4a ... 1st of 2", `Skills: event 'RACING_DUEL_C4' ... +60`, `done_RACING_DUEL_C4 = 1` saved. Without the warp the rival wins and nothing is paid.

### Not done / next
- The shops do not yet gate their stock by area level ("New items available" is told, not enforced); the retail popup page `NewAreaTitle` is not driven. The duel cinematics (`C_DUEL_*_INTRO|WIN|LOOSE`, `DUEL_WIN_<class>`), the duel prize car and the real unlock rule are not done; the Hawai duels (A5 / A2 / B3 spots) were not run.

## Session 2026-11-18 (`main`, 67): the retail "discovered" banner

The owner sent the retail screenshot: a bottom banner "Clothes Shop discovered: Hiro Hisaishi".

### What changed
- `freeride_hud.{h,cpp}`: `showDiscovery( text )` drives GENERAL.vmf `/pop_news` (`/:NewsTxt`, label `open`, then `close` after 5 s) [CODE-dev 0x7d1130, the news queue: labels `open` / `open_little` / `close` / `close_little`]; `/pop_news` is drawn while the notice is up.
- `map_screen.cpp` `spotDisplayName`: the retail strings db_data 4863 Car Dealer, 4864 Bike Dealer, 4867 Tuner, 4868 Clothes Shop, 4869 Car Wash, 4870 Real estate agent, 4871 Stickers Shop (all "... discovered: §SHOPNAME§"), 4872 Cosmetic Clinic, 4873 Hairdresser [DATA]; kinds without a string (airport, club, casino, racing school) show nothing. Classic dealer (4865 / 4866) is not told apart [INF].

### Verified
- `-tpdestination "Ferrari dealer"`: banner "Car Dealer discovered: Ferrari" at the bottom, same bar and icon as the owner's screenshot (`_a.png`, not committed).

## Session 2026-11-18 (`main`, 66): "<building> discovered" notice

The owner: "when close to a shop or dealership it says what shop it is and is now discovered".

### What changed
- `map_screen.{h,cpp}`: `spotDisplayName` (shop / house name from the data, else the card's kind title: "Ferrari dealership", clothes shop, tuning shop, car wash, Racing School, casino...) and `takeDiscoveries()`; `discoverNear` queues a name per newly discovered non-event spot (the 150 m radius is unchanged).
- `gm_freeride.cpp`: each name shows on the goal banner as "<name> discovered" (`hud.showGoal`; the tip popup was tried first and fought the other hints).

### Verified
- `-tpdestination "Ferrari dealer" -autoplay "220:shot=..."` on a fresh scratch profile: log `FREERIDE: discovered 'Ferrari dealership'`, screenshot shows the banner "Ferrari dealership discovered".

### Not done / next
- The banner is the retail goal style, not a verified copy of the retail discovery toast [INF]. Remaining progression slices are large: duels (`RACING_DUEL_*`), `DBAreaLevel` earner, `SOCIAL_CLUB_LVL*`. The `TUTO_*` DBRewardXP rows pay 0 points, so they were skipped.

## Session 2026-11-18 (`main`, 65): haircut / surgery XP and per-area event-win XP

The owner: "do the next progression slice".

### What changed
- `player_profile.{h,cpp}`: `NbHaircuts` (saved as `nb_haircuts`). `hair_ui.cpp` `pay()` bumps it and calls `AwardCollectionMilestones( "COLLECTION_HAIRCUTS", n )` (2 / 5 / 10 -> 20 / 50 / 80 points [DATA]) [CODE-dev 0xba0ef0: counter + `COLLECTION_HAIRCUTS_%d`]. The map's Hairdresser popup `NbHairs` shows it.
- `surgery_ui.cpp`, `surgery_menu.cpp`: each operation pays `COLLECTION_SURGERY` (25 pts, `_nbtimesallowed` 4) [INF: once per operation].
- `gm_freeride.cpp` (instant-race finish, `takeFinished`): a won race pays `CRUISING_<ISLAND>_EVENT_AREA<n>` (20 wins Ibiza / 15 Hawaii per area [DATA]); name from the dev exe `0x99cc90` (`CRUISING_%s_EVENT_AREA%d`), `[INF]` n = the cruising zone (`GetCruisingZones().find`) where the race ends.

### Verified
- `-awardmilestone COLLECTION_HAIRCUTS,5` pays `_2` (+20) and `_5` (+50); the profile saves `nb_haircuts`.
- `-racestart 0 -racetest -exitframe 1500` (scratch profile): "finished in 3.07 s" then `Skills: event 'CRUISING_IBIZA_EVENT_AREA1' ... +25 points`.

### Not done / next
- The hairdresser and surgery purchases were not driven through the Flash UI headlessly (only the milestone rows). Championship / duel wins do not count as events (not read). `DBAreaLevel`, duels `RACING_DUEL_*`, `SOCIAL_CLUB_LVL*` and the story-step cases are still open. The retail `COLLECTION_HAIRCUTS` counts purchases (dev exe), not distinct haircuts.

## Session 2026-11-18 (`main`, 64): photograph challenges (the 20 viewpoints per island) built

### What changed
- **Data found** [DATA]: `euro/bnk/scenarics/photospot.xml` has 40 `VIEWPOINT`s (island 0 Ibiza / 1 Hawaii, 20 each): `pos_vp` (camera), `pos_spot` (subject), `car_pos`, `ideal_angle_*` / `ideal_zoom` with `tol_*`, `start_hour..end_hour`; `hashcode` = `DBPhotograph._hashcode` (name, `_money` 5000, `_playercar`, `_weather`, `_dayperiod`).
- `source/game/freeride/photo_spots.{h,cpp}` (new): loads both and `match()`es a shot. `gm_freeride.cpp`: `judgePhoto` runs after each shutter press of the existing pause-menu `PhotoCamera`; a shot within 60 m of a viewpoint, subject within 15 deg of the view axis, inside the hour window = done: `+_money`, `CRUISING_PHOTOS_<ISLAND>` XP (20 allowed), `BeenInPhotograph`, all 20 -> `PHOTOGRAPH_END_*` call; `tickPhotoHint` shows "Photo spot: <name>" within 100 m of an untaken one. Saved as `photos_taken`. `map_screen.cpp` Photographer popup shows taken / 20. `phone_calls.cpp`: `PHOTOGRAH` nudge on Ibiza while none taken.
- `[INF]` The retail angle/zoom tolerance convention (`ideal_angle_x/y`, `ideal_zoom`) is not decoded, so the 15 deg axis test replaces it; `_weather`, `_playercar` and `_dayperiod` are not enforced.

### Verified
- `-phototest 1` (frames spot 1 exactly) on a named scratch profile: `Photo: 'Ibiza's docklands' taken (+$5000, 1 of 20 on ibiza)`, XP `CRUISING_PHOTOS_IBIZA` +40, saved `money`, `photos_taken`, `phone_state_BeenInPhotograph`; a second run logs `was already taken`. **Not** verified with the real camera by hand (pad / screen), nor the map popup text on screen.

### Not done / next
- Decode the angle/zoom tolerances (compare `ideal_*` with the viewpoint->subject vector to find the convention); enforce weather / car; spawn the requested car at `car_pos`; the Hawaii island in a run (only Ibiza exercised).

## Session 2026-11-18 (`main`, 63): panorama XP ruled out as a trigger; the real feature is DBPhotograph

Tried to pay `CRUISING_PHOTOS_<ISLAND>` from the discovery code's "panorama nodes". `-logdiscovery`: the 2 Ibiza nodes are `D_SCENE` at (0,0,0) and `PLANE_01` at (0,19,-20) - a scene root and a plane, not viewpoints. Code reverted, nothing shipped.
- Lead: `DBPhotograph` (-dumpdbtable) has rows with `_name_hc` ("Bienvenidos"), `_description_hc`, `_money 5000`, `_weather`, `_dayperiod`, `_position`, `_playercar`, `_specialitem`, `_champ1`: photo-challenge spots (Mikey's `PHOTOGRAH` / `PHOTOGRAPH_END_*` calls count them). Next step: read the table fully, find where `_position` maps to a world spot (spots.bin `photograph` kind), build the photo challenge (reach spot in the weather/time-of-day, take the photo), then `CRUISING_PHOTOS_*` and the two phone calls follow.

## Session 2026-11-18 (`main`, 62): championship points table - the medal now follows a real rank

### What changed
- Points per place **10 8 6 4 3 2 1 0** [CODE-dev `0xa3aa70`, jump table `0xa3aab0`; ties share the first place of the tie, `0xa3aad0`, not built]. The ranking `0xa3b6e0` sums each race's points per racer over the championship's races and sorts descending; the medal reads the player's index (`0xa3c8f0`).
- `gm_freeride.cpp`: each championship race with opponents stores its places' points (`PlayerProfile::ChampRacePoints`, saved as `champ_points`); a **finished** race now counts even when beaten [INF - the retail ranks on points so a championship cannot need every win]; completion ranks the player by the summed points (races without opponents: 10 for the player, 0 for the rest [INF]) and pays bronze/silver/gold accordingly (3rd/2nd/1st). Log: `championship '<name>' points: player N, rank R`.
- `player_profile.{h,cpp}`: the `champ_points` save/load.

### Verified
- Build links; a seeded `champ_points` line survives a run. The race-to-completion path was **not** driven headlessly (it needs finishing a whole championship).

### Not done / next
- Tie handling; the speed-mode races; prize money now pays on a beaten finish too (first finish) - check against the 360 if that is wrong.

## Session 2026-11-18 (`main`, 61): championship medal XP (RACING_CHAMP_BRONZE/SILVER/GOLD) wired from the dev exe's rule

### What changed
- `gm_freeride.cpp` (championship-complete block) pays `RACING_CHAMP_<medal>_<class>[H]` [CODE-dev `0xa3c8f0`/`0xa3ce64`]: the final rank (index in the 8-id sorted array) 1st/2nd/3rd -> medal 3/2/1; medal n pays BRONZE.. up cumulatively, each once. `H` = the `_order 2` championship. Class name = `VehicleClassName(VehicleCategory[0])`; cups are skipped (they keep `RACING_CUP_*`).
- `[INF]` The port completes a championship only when all races are won, so the rank is the last race's place = 1st: it always pays all three. No championship points table is kept.

### Verified
- Event names resolve: `-awardevent RACING_CHAMP_BRONZE_A4H` logs `skill 0 +45 points`. The completion path itself was not driven headlessly (needs winning a whole championship).

### Not done / next
- A real points table (non-winning completions give silver/bronze); `spec skills_spec.md` still lists the medal rows as dark.

## Session 2026-11-18 (`main`, 60): phone-call progression rules from the dev exe's trigger table (6 more calls fire)

The owner: *"with dev build can we try do all progression stuff"* — scoped (asked) to the phone-call triggers first; the rest of the progression backlog is untouched.

### What changed
- `source/game/freeride/phone_calls.cpp` `checkNudges` — added, in the retail order of `SCPhoneCalls 0x9a2c30` [CODE-dev]: `CALL_C4_NOT_PASSED` (Classic school's first licence started, not obtained), `RS_ASPHALT` (first Asphalt licence obtained, next not started), `TUNER_CALL`, and the championship nudges `NEW_ASPHALT_CAR`/`ASPHALT_CHAMP`, `NEW_SUV_CAR`/`DIRT_CHAMP`, `CLASSIC_CHAMP` (family unfinished; new-car call while no car of `_vehiclecategory` 1..7 / 9..12 is owned). Conditions are [INF] readings of the state objects; the 15 min play gate and TUNER_CALL's rule are chosen.

### Verified
- Scratch profile (`C:	emp\otdu2-phone-userdata`, story_step 12, play time 3000 s, shops visited) with `-gm FREERIDE -skipcine -no3d -nopad -fixedtimestep -exitframe 4000`: log `PhoneCalls: ring 'NEW_ASPHALT_CAR' (npc 0x180 ...)`.

### Not done / next
- `EVENT_JANE`, `PHOTOGRAH`/`PHOTOGRAPH_END_*`, `HAWAI_CALL` (no state in the port); `ASPHALT_CHAMP`/`DIRT_CHAMP` path and `CALL_C4_NOT_PASSED` not exercised headlessly. Still open from the backlog: `DBAreaLevel` area points (dev exe loader at `0x49a1c0`, earner not found), medal/duel/photo XP, story-step enter/leave cases.

## Session 2026-11-18 (`main`, 59): the tuning shop runs the RETAIL SHOPSCAR car_tuner screen (default; `-notuneui` = native)

The owner: *"please work on the tuning shop menu ui"*. ETUN_* rooms load `shopscar.vmf` at its `after_mkt` label; `TuneUi`
(`freeride/tune_ui.*`) answers the page: car sheet + gauges, money, the squares, the four level lines (prices, lock states),
the Wheels page (`/:option_name`, `/:cur_op_price`), buy (`kiva`) and rims (`tune:rims:vali`). Protocol in `docs/re/tuning_ui_spec.md`.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -interior ETUN_A -walkaim npc_tuner -logtune -autoplay ...` (needs the 3D pass, no `-no3d`):
  `TuneUi: car 387 accel -> level 1 for $12000 (money now $81450)`, `braking -> level 1`, `rims 21" for $8000`; saved to the profile;
  B -> "Exit Shop?" -> A -> `gofr` -> `FREERIDE: back in the car`. `docs/images/tuning_retail_ui.webp`.
- Controls: D-pad up/down/left/right (arrow keys) pick squares / lines / rims, A (Enter) buys, B (Esc) backs out.

### Preview camera + car on the lift (later, same session)
The owner, from retail 360 screenshots: the car is on the lift; the Rims preview is a second-camera close-up on the wheel that pans left-right-left in a loop (the dealership interior shot does the same); paint orbits the car.
- `GMFreeRide`: at `afmk` the driven car is placed on the room's car place (`InteriorWalk::placeStickerCar`, the sticker/tuner-garage rule) with its paint (`pTuneRoomCar`; `-notunecar` skips). Log: `InteriorWalk: sticker car 387 on D_CAR`.
- While `TuneUi::isRimsPage()`: a **second render into the movie's preview clip** (the dealer's technique: `IntroCinematic::renderLocalInset` -> `RenderTarget` 512x360 -> `FlashPlayer::setBitmapOverride("/menu_rims/lure")`, found with `-dumptuneclips 7`). The camera pans +-0.40 rad (period 8 s, as the dealer's) about the front-left hub (D_RIM_FL 0.826, 0.229, 1.335 [DATA]) at 1.5 m. `GMFreeRide::renderTuneInset`; `-noinset` skips it, the main view stays the room's. New `IntroCinematic::getDisplayCarYaw`. Evidence `docs/images/tuning_wheel_pan.webp` (3 frames of one loop, the panel shows the wheel, the room behind is unchanged).
- The panel is grey-boxed like the dealer's: only the lifted car is drawn on the flat grey backdrop (`renderLocalInset(..., onlyDisplayCar)` -> `insetDisplayCar` in `drawSets`, showcase mode); `-insetfull` shows the whole room again.
- Not done: paint orbit (the tuner has no paint page).
- **Dealer too** (`GMShop`), now matched to the owner's own capture of the same R8 (`Videos/2026-11-18 07-02-01.mp4`, frames read with OpenCV: phase-correlation of the main view, frame-difference of the panel): **wheels + paint main view** = the four-corner box loop, a move every **~5.3 s** (hold 4.0 / move 1.3 s) on BOTH pages (was paint only, 4.0 s); **wheels panel** = left-right-left sweep across the wheel, period **8 s**; **paint panel** = a lap round the car ~1.3 m off the paint (wheel, lamp, grille, ..., tail; ~21 s); **interior main view** = the driver's seat looking at the wheel, swaying left-right (~7.3 s, +-4 deg), **interior panel** = the cabin from the open door, STILL. (My earlier door-gap main view and panel sweep on the interior page were a misreading, reverted.) `docs/images/dealer_interior_vs_video.webp` (video left, ours right). `-noinsetsweep` = the old static panel.
- **Interior colours** (`CinematicCar::applyInterior`; `-loginterior` logs the DBInterior fields, the library entries and the model materials): the DBInterior row is `_interior_color_1/2/3`, `_interior_name`, `_material`, `_price` [DATA]; the ids live in the car's own `<car>_color.2dm` (found by hash). Seat leather `C1_*` <- colour 1, `C2_*` <- colour 2, the couture `A_C3_*` <- colour 3 (was untinted), the trim material `MAT_1` <- the row's `_material` ('R8_V10_Mat_Gris_01' = brushed alu ramps, no weave; 'R8_V10_Mat_Carbon_01' = the CARBON detail texture); `A_C2_al` keeps its metal. Verified on the first four R8 rows (red / black seats, red couture, carbon vs alu).
- **Palette squares** (`GMShop::paintBuySwatches`): the seat colour from the car's library (`CinematicCar::getLibrarySwatch`) instead of grey. The retail square (capture) is colour 1 with colour 3 as a diagonal bottom-right corner when it differs; the corner IS drawn now: `FlashPlayer::setClipCorner` / `clearClipCorner` (`Instance::CornerColor`, a solid triangle over the lower-right half of the clip's first shape, drawn after the tinted square in `drawInstance`), set per square when `_interior_color_3` differs from `_interior_color_1` (`docs/images/dealer_interior_palette_corner.webp`). The textures the interior uses: `r8_v10_int` / `int2` / `int_ts`, `moket2` + `ts_leather3` (leather / alcantara), `couture` + `ts_couture`, `carbon`; there is no separate palette texture, the squares are flat colours from the library materials' diffuse / RAMPCLR.
- Not done: the +20 % lightening in `applyInterior`; other cars' trim material names (`MAT_1` is the R8's); the retail rims panel may be a pre-rendered picture - ours is a live render.
- **Dealer paint page checked against the capture** (`docs/images/dealer_paint_vs_video.webp`, video left / ours right, same page time +2..+22 s): the main-view box loop was in the wrong order and too close. Now **front-left -> rear-left -> rear-right -> front-right**, corners (1.5, 6.3) / (3.0, -5.0) / (-3.0, -5.0) / (-2.6, 5.9) m, eye 1.25 m (the front-left one avoids the palm; the poses at +6 / +10 / +14 / +18 s match the capture closely). The panel orbit ran the wrong way: the capture goes left side -> front (grille at +8.7 s) -> right side -> rear (tail at +19 s), angle `2.6 - 0.30 t`, eye 1.0 m off the body ellipse (0.9 x 2.1 m), fov 0.75. **Prices carry no "+"** in the retail rows / panel ("$880", "$0", "$2,630"): the "+" prefix is dropped. Not matched: the page starts with the capture's first hold ~3.7 s (ours 4.0 s); the capture's car is Phantom Black $0, ours starts on the DB's first row (Brilliant Red +$2,400).
- **Dealer interior page matched to the capture** (`docs/images/dealer_interior_page_vs_video.webp`, video brightened x2.2 left / ours right, page time +1..+7 s): **main view** = the driver's eye close behind the wheel, looking down at the wheel and pedals (eye (0.38, 0.92, -0.1), target (0.48, 0.50, 0.55), fov 1.0; the wheel sits right of centre and fills ~55 % of the frame height), and it **slides left-right while it keeps looking at the wheel** (eye x +-0.14 m, period ~7.3 s): the wheel stays put and the door / windows swing past. (The previous "rotate in place" sway and the farther eye were wrong.) **Panel** = the cabin from the open door, looking across at the wheel / cluster / passenger seat (eye (0.95, 0.95, -0.4), target (0, 0.75, 0.3), fov 0.85), still. Not matched: the capture is darker; the pedals are lower in its frame (the eye is a little farther back / lower there).
### Not done / next
- The part cinematic, 4th level (spec section 4). Hawai tuning shops not walked.

## Session 2026-11-18 (`main`, 58): clothes shop buy works on the retail movie (default; `-noclothesui` = native)

The A refusal = missing `Solded` / `DLC_Locked` / `IsEnoughMoney` (set per item in `updateClosetItem`), then `buit` buys + wears. See `avatar_shops_spec.md` s3.

## Session 2026-11-17 (`main`, 57): clothes shop on the retail movie - browsing (earlier step)

Shop mode reuses the house closet protocol (`clin/neit/prit/seit`) with the brand stock; `-walkaim Seller_area`. A on an item is refused by
the movie (`sndp:wrong`) - next step in `docs/re/avatar_shops_spec.md` s3. Native menu stays the default.

## Session 2026-11-17 (`main`, 56): the hairdresser runs the RETAIL screen

The owner: *"Yes, hairdresser"*. Root cause of the blank hair page: **`GetProperty(_target)` was unimplemented in the AVM1 runtime**
(`flash_player.cpp getProperty` case 11 now returns the clip's slash path) - `setHairMenu` saves `_target`, edits child clips and
`SetTarget2`s back, so `GoToLabel('open')` had hit the root. Then `HairUi` (new `freeride/hair_ui.*`) answers the page: values on
`<page>:open`, `:scne/:scpr` by writing display text into `/:HairCut` / `/:HairColor`, price vars, `vali` pays, `canc` reverts.
Default now (`-nohairui` = native). Profile: `HairColor` / `BeardColor`, `EdenAvatar` applies them + the beard slot (part 9) for the player.
Verified headless (`-interior EHD_B -walkaim hairdresser`, `docs/images/hairdresser_retail_ui.webp`): `paid $250 (hair 261 colour 2)`,
saved `worn_clothes = 3:261`, `hair_color = 2`. Details in `docs/re/avatar_shops_spec.md` section 2.
Not done: Eyebrows page, seeing the new hair on screen (no avatar in the room view), casino salon untested; clothes shop (next), tuning.

## Session 2026-11-17 (`main`, 55): clothes-shop mirror shows the player; hair / clothes on the retail movie reached, not finished

The owner: *"Both if we can [hairdresser + clothes] ... also in clothes shop the mirror doesn't reflect the characters"*.

### What changed
- **Mirror (done)**: `IntroCinematic::setMirrorPlayer` / `updateMirrorPlayer` / `bPlayerMirrorOnly` + `WalkMode::update` feed:
  the first-person walker's avatar is posed (stand idle) at its feet and drawn only in the projector passes.
  Verified in `EAS_A` (diff confined to the mirror quad, `docs/images/clothes_mirror_player.webp`); `-nomirrorplayer` A/B.
- `InteriorWalk::avatarLabel` generalises the avatar-shop mode (`surgeon` / `hairdresser` / `clothes_shop`); hair and clothes are
  opt-in `-hairui` / `-clothesui` (native menus remain default). `HairUi` skeleton (`-loghair` dumps the movie variables).
- Fixed an ordering bug: the entry commands now reach the sink BEFORE the movie's `openMenu*` invoke (its `*:edit:open` fires inside it).
- Findings in `docs/re/avatar_shops_spec.md`: hairdresser square menu opens (needs `/:isWoman`), hair list page stays blank
  after `hadr:edit:maha:open` (variables unknown); clothes shows its title bar, seller not yet selectable by `-walkaim`.

### Not done / next
- Hairdresser rows (DLC / Lock variables), clothes item grid + prices (reuse the house closet's `clin/neit/prit/seit`),
  then make both default. The performance (tuning) shop: SHOPSCAR tuning page, not touched this session.

## Session 2026-11-17 (`main`, 54): the clinic runs the RETAIL SHOPSAVATAR surgeon screen

The owner: *"that isn't the correct menu UI - I would like to try get the true menus if possible"*. The native
list (session 53) is now only the `-nosurgeryui` A/B: an `ESUR*` room loads `shopsavatar.vmf`, goes to its `surgeon` label and
answers its `surg:` FSCommands. Full protocol, variables and the slider-range finding (`/:NbNotches` is the max) are
in `docs/re/surgery_spec.md` section 5. Screenshots `docs/images/surgery_retail_ui.webp`.

### What changed
- `interior_walk.{h,cpp}`: `bAvatarUi` (+ `getAvatarUi()`), the surgeon A (`ItemType('valid')`, bandage gate 4405),
  `surg:cine:intr` -> `openMenuSurgeon`, the dialog box shares the sticker path (`DialogBoxSource` SHOPSAVATAR).
- `freeride/surgery_ui.{h,cpp}` (new): the game's half of the protocol; `gm_freeride.cpp` binds it on the surgeon's A.
- Verified headless (`-gm FREERIDE -skipcine -nopad -fixedtimestep -interior ESUR_A -autoplay ...`): square menu
  (Head / Face / Skin / Random / Cancel / Valid) -> Face -> Eyes sliders move (`EyesOpening = 11, 12, 13`), price
  `$ 100`, confirm dialog, `surg:edit:vali` -> `paid $100`, saved `face_params` slot 10 (C_Eyes_opening) = 20, bandage
  refusal on the next A.

### Not done / next
- Hairdresser (`hairdresser` label, `hadr:*` commands, `/:Lock*`), clothes (`clothes_shop`, `cl_in`, `item_selection_*`), realtor are
  the SAME movie: copy `SurgeryUi` for each. The performance (tuning) shop is SHOPSCAR's tuning page: not decoded.
- Not verified: the edited face on screen; Skin page (Tint / Age / Freckle) and the Head page in the movie; EyesColor
  choicebox; Random; the C_OBSERVE cinematic.

## Session 2026-11-17 (`main`, 53): the cosmetic clinic (surgery) menu - first of the shop-menu pass

The owner's report: *"a few shops may not be using the correct menus - the performance upgrade shop
doesn't have the correct menus, hairdressers, clothes shop, surgery also isn't implemented at all - please
start on those"*.

### Survey (what each shop runs today)
- Car dealer: the retail SHOPSCAR movie (GMShop). Tuning (`ETUN_*`, `tuning_menu.cpp`), paint (`paint_menu`),
  clothes (`ClothesMenu`) and hairdresser (`ClothesMenu` on the `HAIR` brand) are **native stand-in lists**, not
  the retail movies. Surgery had **no branch at all** ("This shop is not open yet.").
- SHOPSAVATAR.vmf (426 frames) is the retail avatar-shop movie: its root has frame labels `scr_open`,
  `surgeon`, `hairdresser`, `photographer`, `clothes_shop` (a `/:fake` page-picker button), plus `surgeon_block`,
  `surgeon_stop`, `hair_dresser_edit/stop` clips [DATA, `-gm uiviewer -movie shopsavatar -dumpui -dumpbuttons`].
  Driving it like GMShop drives SHOPSCAR is the real fix for clothes / hair / surgery; **not started**.

### What changed (surgery, native, surgery_spec.md phases 1-2)
- `source/game/freeride/surgery_menu.{h,cpp}` - `SurgeryMenu`: one row per `DBSurgeryCost` item the avatar can
  apply (39 FaceFX sliders + age + freckles = 43 of the 46; EYE_COLOR / EYE_BROWS_TYPE / TINT are not offered -
  the avatar does not read EyeID / EyebrowsID / tint). Left/Right = one notch (10, of -100..100), A pays every
  changed row's `_cost` once (retail TotalPriceStr / NbNotches idea), B leaves. Keyboard: arrows / Enter / Esc.
- `openSpotShop` (`gm_freeride.cpp`) gained `kind == "surgery"`; bandaged -> text 4405; the hairdresser answers
  7169 while bandaged. `-interior ESUR_A|ETUN*|EHA*` now sets the kind (test hook).
- `PlayerProfile`: `FaceParams[49]`, `FaceAge`, `FaceFreckle`, `NbSurgeries`, `BandageUntil` (profile.sav keys
  `face_params`, `face_age`, `face_freckle`, `nb_surgeries`, `bandage_until`); the map's NbSurgeriesDone reads it.
- `EdenAvatar::load` applies the edit to the player's own config (`bPlayerFace`; `-nosurgery` A/B), and
  `EdenAvatarLibrary::readPresetFace` gives the menu the preset .avt baseline.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -interior ESUR_A -autoplay "400:press=validate;...;495:press=validate"`:
  `Surgery: 'Cosmetic clinic' open, 43 row(s)` then `bought 1 change(s) for $100 (money now $94700 ...)`,
  profile saved; `docs/images/surgery_menu_clinic.webp`.

### Not done / next
- NOT verified: the edited face on screen (the avatar is only visible in cinematics / house closet), the
  bandage duration (600 real seconds is [INF]; retail rule is [ENC]) and bandage head item (DBClothe 313/314).
- Performance (tuning) shop: retail pages not decoded; hairdresser/clothes: drive SHOPSAVATAR (`hairdresser`,
  `clothes_shop` labels) - start at `GMShop`'s SHOPSCAR handling for the pattern.

## Session 2026-11-17 (`main`, 52): ray tracing step 3 completed - the terrain is in the acceleration structures

The owner's other ask ("work on ray tracing also if possible"). `ray_tracing.md`
listed the terrain heightfield as the blocker for step 4 (RT sun shadows): the
level mesh keeps one R32 height per vertex and the shader rebuilds the positions
from grid coordinates, so a BLAS had nothing to read. Solved:

### What changed
- `EdenWorld::buildHeightfieldBlas` / `destroyHeightfieldRt` (eden_world.{h,cpp}):
  when `-rtworld` is on, the heightfield loader builds an XYZ position buffer
  from the heights it already parsed (sector local, the TD_HeightMap vertex
  shader's own law: x = column * Spacing, y = height, z = row * Spacing; the
  draw's World translation carries the sector origin) and a BLAS over the
  shared grid indices - one per loaded sector sheet (up to 131072 tris, 768 m at
  3 m). The existing `RtInstance` capture picks the terrain draw up unchanged.
- **Carve aware**: the BLAS is built from the full grid at load and REBUILT by
  `updateCarveBuffers` from the split list once the carve decides (the quads
  inside tunnels / bridges stay out of the structure like out of the opaque
  draw; the carved immediate draw zeroes `Mesh.BlasAddress` so the capture does
  not duplicate the instance). Exercised at the spawn sector (19 covered-road
  carve volumes) with 0 VUIDs.
- `RayTracing::retireBlas` / `retireTick` (ray_tracing.{h,cpp}, called from
  `GSRender::endFrame`): an unloaded sector's structure is destroyed three
  frames later - the `pRtTlasRetired` shape, because a frame in flight still
  reads it through the TLAS. `destroy()` flushes the ring.
- The sector unload (`unloadFar`) and `EdenWorld::destroy` free the positions
  buffer + retire the structure. The coarse far sheets (area 24 m / island
  384 m) are deliberately NOT included (ray-space z-fighting under the 3 m
  sheet; would need a rebuild hook in updateArea/updateIslandIndices).

- Daylight + other car (owner: "check the satnav in daylight and on another car"): `-weather 12` daylight is fine (roads no longer clip). Of California / 458 / 599 / 370Z / Agera / Camaro only the California and 370Z have a GPS node. The **370Z's quad has transposed UVs** (texture +x runs down the screen, found with `-gpstest` corner markers + `-loggps` UV dump), so its map came out rotated 90 deg and squashed. `CinematicCar::computeGpsLayout` now reads each car's quad UVs at load and matches the 8 transpose/flip layouts (`GpsUvLayout`: California identity, 370Z transpose + flipV, logged as `dash GPS uv layout`); `GpsScreen::setLayout` makes a portrait 256x512 target for transposed cars and `FreeRideHud::drawDashSatnav` pre-rotates the content. `docs/images/dash_satnav_orientation_california_370z.png`. Other GPS cars unchecked (the detector logs NOT matched if a layout is not one of the 8).

### Verified (headless, build 216, 0 VUIDs everywhere)
- The camera probe "down ray" now hits the ground at the drive height (before it
  reported whatever a model provided): `Ray tracing world: down ray -> hit 1,
  t 1.43 m` at the `-tp 21796.3,24188.1` spawn.
- `-rttest` still PASSED (hit / miss / rotated rays).
- `-showaomask` A/B at `35700,3,5600` pitch 4: the mask now shows the terrain
  self-occlusion (ground shadow band under the tree base, the hill fold
  darkening) - `_x/rt_mask_ground.png`; the color view is unchanged at
  `-rtao 16` vs `-rtao 0` (`rt_ao_color.png` / `rt_ao_off.png`).
- Costs (opt-in, `-rtworld` only): the frame-0 startup stall grows
  3.4 s -> 6.5 s (~30 sheets uploaded + built); steady state unchanged
  (13.3 ms avg / 75 fps at the spawn, TLAS 0.4-0.8 ms per frame, 3259 KB at
  696 instances); streaming hitches on new sectors (112-131 ms) comparable to
  the model BLAS builds already there.

### Not done / next
- Step 4 itself (RT sun shadows, `rt_shadow.comp` through the
  `setShadowRealTimeTexture` override) is now unblocked - the next RT session's
  work.
- The owner's nvoglv64.dll access violations (STATE session 23) still keep RT
  non-default.
- The remaining ~40 dev-build light functions (the other session-51 dig) and
  the dashboard intensity wiring are the lighting port work.



## Session 2026-11-18 (`main`, 52): the dash satnav shows in the cockpit (render-to-texture, on by default); dash backlight confirmed

Owner: "add the cockpit view lighting for dash and the satnav for cars with satnavs" (+ "I think it probably uses render to texture").

### What changed
- `source/game/freeride/gm_freeride.cpp` - `-gps` now defaults to 1 (`-gps 0` = off). The session-49 "invisible quad" was not a shader problem: [CODE] TD_GPS.sob's VS (dumped with `otdu2_sob_tool`) is a plain WorldViewProj, `uv * UVOffset.xy`, colour = lerp(ambient, diffuse, N.L) * vertex colour. The quad is a 13 x 7 cm screen at car-local (0, 0.65, 0.14) on the centre stack, below the default cockpit view.
- OWNER CORRECTION ("it's not accurate, it normally renders pretty much 1:1 as the original satnav"): the dash target no longer draws its own map. `FreeRideHud::buildMinimapVertices` (extracted from `drawMinimap`) is shared; `FreeRideHud::drawDashSatnav` draws the HUD minimap's exact content (road graph by discovery - grey / cyan / blue / dirt yellow, off-road trace, orange route, heading-up, car at 62 %) into the `GpsScreen` target at the HUD map-rect scale (190 x 95 logical px, 512x256 target). `GpsScreen::render` takes a draw callback; redraw every 5 frames. Pointer is our own (the HUD's is a movie clip).
- `source/game/cinematic/cinematic_car.cpp` - `-loggps` prints the GPS items (node, bounds, index count).

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 21796.3,24188.1 -snaproad 150 -gpsdrive "24500,25300,20" -autoplay "100:press=camera;130:press=camera;200:look=0.35,0.7;380:shot=..."` : the HUD's satnav content on the California's screen, `docs/images/dash_satnav_california.png`.
- Dash backlight (already wired via `setDashboardLit` / HEADLIGHTSINTENSITY): `-carlights 1` vs `0` cockpit shots differ by 34k pixels, gauges lit blue, `docs/images/dash_backlight_night.png`.

### Not done / next
- Only the California checked; other cars' GPS node placement unchecked. Daylight look unchecked (screen is lit by the scene sun, so noon may clip). No in-car zoom / rotating mode. The cockpit's non-dash surfaces get no cabin light at night (retail has none found yet).

## Session 2026-11-17 (`main`, 51): the dev-build car-lighting dig - the [ENC] lamp functions decoded, the light data found, the dashboard mechanism named

Owner: *"dig the dev build see if you find any car lighting data head lights, tail lights, interior dash
etc"* (before a ray-tracing pass). Research only, no code. **`docs/re/dev_build_car_lights.md`** is the
write-up; `_lights_dig/` (scratch, uncommitted) has the python XMB reader (`xmb.py`, cpr decrypt +
full XMBF walk - the retail `-dumpdbtables` log line truncates at 2 KB, the python dump does not) and
the decoded shader folders.

### Found ([CODE-dev] = TestDrive2DEV.exe via tools/devbuild_disasm.py, [DATA] = game files)
- **DBCarPhysicsData carries 14 light columns** (the `-dumpdbtables` log hides them all but 5):
  front headlight + rear break ("Break", the data's spelling) + rear reverse, each
  projectionangle / apertureanglewidth / apertureangleheight / activatecaustics, plus
  `_headlightsorientation` (10 on every car - pop-ups are NOT in the DB) and `_islightcarwashable`
  (Ferrari-brand cars = the car-wash "light car"). All cars ship the same angles (46/176/63 front,
  69/180|141/86 rear): global defaults, not per-car art.
- **The retail per-frame lamp functions decoded** (car_lights_spec §2.2's [ENC] table):
  dev table 0x19ca470 (initializer 0x1462410, ~50 x 8-byte slots). brake 0x71fdd0 -> GLOSSFCT r,
  reverse 0x720e50 -> g, headlights 0x721ad0 / position 0x721dc0 -> b, blinkers 0x720a50/0x720c40 ->
  1-a - the channel law guessed from the masks is confirmed. Shape: groups {1,0,3,4,6,2,5}, pair
  vector at car+0x783C + g*12 (retail offset confirmed), a per-function state cache (+0xb0 / +0xb4)
  and a force-every-frame debug byte at 0x1660800.
- **The retail head-light projection system named**: "HEADLIGHTS BLIT" + "HeadLightsRT" (RT chain
  created at 0x4fd240, format via a HeadLightsRT config flag) - s15's G/B IS the projected head-light
  buffer (rain_shadow_spec B.7's [INF] now [CODE-dev]); texture ids 13/14/15 = breakcaustics /
  reversecaustics / headlightcaustics (registered at 0x716b77); head-light receiver volume test =
  config globals HeadLightFront/Back/LateralDistance = 120/30/30 m.
- **The interior dashboard light**: TD_DashBoard.sob (caca000000000010) and TD_flashdashboard.sob
  (7dad16b47eb51337) both carry Var_PS `IlumParam` / `IlumColor` / `HeadlightsIntensity` + sample s15;
  the dev collector 0x71a680 gathers the HEADLIGHTSINTENSITY param pointers of exactly those two
  shaders into car+0x77e8. The port already dumps these params (`-logcar`) but nothing drives them -
  that wiring is the port work this dig feeds. Also: a scene object named "DASHBOARD" with an
  ILUMCOLOR param in 8 interior banks; `mng_dashboard` = the Flash GAUGES movie manager (2D overlay).
- Dev-only strings: the HeadLight debug block (beam / front / break / reverse caustics / spot light /
  HeadLightAttributes), HEADLIGHTS SWITCH DISTANCE, "All cars toggle headlights",
  `avatar_car_night_reflection_factor` (unported weather key).

### Not done / next
- The remaining ~40 table slots (0x722310..0x728910, glow-node / flare writers) not decoded one by
  one; the pop-up headlight function (retail FUN_00c67280 twin) not found yet.
- The per-frame writer of car+0x77e8 (dashboard intensity) + the flash trigger not chased to its
  caller - that is step 1 of the port work (write HEADLIGHTSINTENSITY on TD_DashBoard materials in
  CinematicCar instead of the port-native night-dash hack).
- Ray tracing (the owner's other ask) untouched this session - see ray_tracing.md step 4 (RT sun
  shadows, needs the terrain in the structures).




## Session 2026-11-16 (`main`, 50): clouds v8 - discrete puffs, not gathered sheets; offline field replica built

Owner (v7 verdict + two crops): the broken-up cloud in the second crop is the
appealing character, but the deck is still "too gathered"; earlier attempts
"didn't understand". Asked for another go.

### What changed (`resources/shaders/volclouds.frag` + regenerated header)
- **Asymmetric cluster**: the cluster map is sharpened (`smoothstep(0.20, 0.80, cmap)`) and
  applied with a hard carve below its mid (x1.30 - kills the stray-puff confetti in the gaps)
  but a weak lift above (x0.40 at fair weather, blending to x1.0 for storms) - clouds inside a
  region no longer merge into sheets.
- **Convection-cell gate** (the real fix): a 2D slice of uDetail's freq-2 inverted Worley at a
  4200 m tile (~2.1 km cells), `d *= mix(gate, 1.0, storm)`, gate = `smoothstep(0.30, 0.62, cell)`.
  The base Perlin-Worley field is shaped like RIDGES between its Worley cells
  (`GenerateBaseVolume` dips at the cell centres), so no threshold tuning can turn its highs
  into separate clouds - they always reconnect. One cloud per convection cell now; positions and
  interior shape still come from the base noise + cluster map. Fades out for storms
  (`smoothstep(0.55, 0.85, coverage)`), so an overcast deck still forms (verified `-cloudcover 0.86`).
- **Horizon wash starts earlier**: fade from tNear 9 km (was 24 km) - the small-puff band at the
  horizon now dissolves into the haze instead of reading as speckle grain.

### The tool that made this work (new, `_clouds_iter/`, scratch - NOT committed)
- `field_preview.py`: exact numpy replica of `GenerateBaseVolume` / `GenerateDetailVolume` and the
  shader's `density()` (base median 0.463 matches the game log) - renders top-down coverage maps in
  ~30 s instead of a 40 s build+shoot cycle.
- `ray_view.py`: 56-step oblique ray march through the precomputed grid = offline stand-in for the
  march pass (what the sky actually looks like from a ground camera, pitch/yaw configurable).
- Process note: the first three "no visible effect" rounds were self-inflicted - a subtractive
  erosion too weak to reach zero in optically-thick clouds, then a gate whose mix was inverted
  (multiplied by `1-gate`), then a gate tile 10x too small (420 m instead of 4200 m - a no-op,
  median gate 0.99). The `-showcloudalpha` composite debug (mode 1, R = accumulated alpha) and the
  top-down capture from above the slab (`-campos 35700,5000,5600 -campitch -89`) were what
  uncovered each. The game top-down also showed the deck is a THIN translucent layer plus a few
  dense puffs - the "gathered sheets" at ground level are the thin layer optically accumulating at
  grazing angles.

### Verified
- `docs/images/clouds_v8_viewer.png` (pitch 20), `clouds_v8_viewer_high.png` (pitch 38),
  `clouds_v8_drive.png` (freeride 12600,16640 look-up), `clouds_v8_overcast.png` (`-cloudcover 0.86`),
  `clouds_v8_before_v7.png` (the v7 baseline at the same pose for A/B).
- The gathered sheet of v7 (top-centre at the standard viewer pose) is gone: discrete puffs with
  clean blue between; the mid-band strings broke up; horizon band hazier.
- Storm coverage at 0.86 re-gathers and densifies (gate faded, lift back to symmetric) - brighter
  than the real kind-R look because `-cloudcover` does not apply darkness; kind R itself untested.
- Perf: same texture-fetch count as v7 inside the march (+1 cell fetch on the 4 non-detail light
  taps only); run times unchanged (1280 beats ~6 s).

### Not done / next
- Sunset / night colours still unchecked (carried from v5-v7); clouds still absent from the env
  cube / mirrors.
- The thin-layer insight (thin translucent haze between clouds is what merges them at grazing
  angles) suggests a possible next lever if the owner still sees sheets on the road: raise the
  remap steepness (`mix(0.16, 0.22, coverage)` -> ~0.12) or the coverage mapping so marginal
  columns die entirely.
- `_clouds_iter/` contains the replica + all iteration PNGs; safe to delete, or keep `field_preview.py`
  / `ray_view.py` as the starting point for the next clouds pass (they are scratch quality, stdlib+zlib
  only, numpy required).
- NOT committed yet (owner asked for a try, not a commit).


## Session 2026-11-16 (`main`, 49): clouds v7 - broken up, toned down, higher, faster drift

Owner: v6 "blown out and so joined up ... break up more, tone down, maybe higher, add movement".
- Deck 2400-3400 m; coverage `0.38c`; stronger cluster map (gaps) in `volclouds.frag`; sun term x0.42 (was 0.55) and forward-scatter cap 1.3 (less clipping); wind x2 (`eden_scene_view.cpp`: 42 / 21 m/s x cloud3dSpeed).
- Verified: `clouds_v7_viewer.webp`, `clouds_v7_drive.webp`; two drive shots 600 fixed-timestep frames apart differ by (41, 33, 23)/255 in the sky band. Perf and sunset/night unchecked.

## Session 2026-11-16 (`main`, 48): clouds v6 (smaller / fewer / higher) + the phone-call GPS offer layout

Owner: clouds "slightly off ... way too big / too many ... as if clouds are low down"; phone-call accept/decline "maybe inaccurate" with a 360 capture (Tess: portrait + name, text, `A OK, change my GPS target` / `B No thanks, maybe later...`).

### What changed
- Clouds: deck 1900-3000 m (was 1000-2100; rain 1100-2300), shape tile 2100 m (was 2600), detail 210 m, coverage mapping `0.48c` (was 0.8c) in `eden_clouds.cpp` / `eden_scene_view.cpp` / `volclouds.frag`. A smaller tile with the old coverage was too busy (flecks); the final trio gives separate cumulus with blue between (`clouds_v6_*.webp`).
- Phone call (`freeride_hud.cpp`, `phone_calls.cpp`): `/:dialog_pnj` is the **caller name** header (loc 7697 Tess .. 7707 Jane; was showing wrong text), the ringing-phone overlay (`gps_display/phone`) and the small answer icons (`gps_display/buttons`) are hidden once the call is shown, help bar `A_B` with `/:m_Ok` = loc 7449 "OK, change my GPS target" and `/:m_Cancel_` = loc 7450 "No thanks, maybe later...". Only for calls that have a GPS target (`TargetSpot`). Controls unchanged and already matched the 360: A / Enter accepts, B / Esc declines (`-phonecall RS_ASPHALT -autoplay "400:press=validate|back"` logs accepted -> GPS to ERS_B / declined). `docs/images/phone_call_gps_offer.webp`.

### Not done / next
- Calls without a GPS target keep the single-OK bar. Mickey / dispatcher / the ev_bwoman caller have no name id found (header empty). The button glyphs are the PC ones, the 360 uses A/B.
- Not measured: clouds perf; sunset/night cloud colours.

## Session 2026-11-16 (`main`, 47): clouds v5 - real cumulus with blue gaps; the coverage table was inverted

Owner: "look into the clouds again ... they had been looked at a few times and still don't look great".

**Reproduced**: freeride look-up and World Viewer `-island ibiza -campos 35700,80,5600 -campitch 20 -weather 12 -clock 12` showed a near-solid dull-grey deck, smooth lens-shaped blobs and blown-out white "holes", no blue sky, no cumulus shape.

### What changed
- `source/render/eden/eden_clouds.cpp` `thresholdForCoverage`: **the table was indexed by 1-coverage although it is already filled by coverage**, so more coverage gave FEWER clouds (log: 0.5 -> 0.169, 0.86 -> 0.679). Now 0.5 -> 0.513, 0.86 -> 0.154 (`0.8c` mapping). The "overcast"/rain presets were thinner than fair weather before.
- `resources/shaders/volclouds.frag` (+ regenerated `volclouds_frag.h`): large-scale cluster map and per-cloud top height from the same noise volume (flat bases, puffy varying tops, gaps); stronger 4-channel detail erosion; lower light extinction (0.014), higher multi-scatter floor, brighter ambient, HDR sun scaled 0.55 and the HG phase capped (the lit tops clipped to white).

### Verified
- `docs/images/clouds_v5_viewer.webp`, `clouds_v5_drive.webp` (freeride look-up), `clouds_v5_overcast.webp` (`-cloudcover 0.86`), `clouds_v5_rain.webp` (freeride `-intempery 6`, dark storm sky). Drive/overcast captures were taken at the `0.9c` mapping; the final `0.8c` is slightly clearer (World Viewer day shot re-taken).
- Not measured: perf (shader got one more texture fetch per density call, 2 more in the light march) - check the `[perf]` line.

### Not done / next
- `-intempery` has no effect in the World Viewer (weather fixed there), so the rain look was only checked in freeride.
- Sunset / night colours unchecked; still no clouds in the env cube / mirrors.


## Session 2026-11-16 (`main`, 46): the second tuner shop "Rev Yard" - the used-car-dealer room as a dark neon workshop, with props of our own

The owner: *"we made a custom shop using something we already have ... maybe make a unique shop ... use used car dealer interior, add the lifts and a few other things, use stuff from other places for props ... custom cars in the background ... people working on their cars online"*, then *"make it an extra shop"*, *"yes icdc_wld"*, *"reskin and add props"*.

### What changed
- `tools/mod/make_revyard_interior.py` (new) - `itnr_b.bnk` from the retail used-car dealer `icdc_wld.bnk` ("U.S. Cars (north)" / World classic [DATA]). Floor `marble_02_d` -> dark tiles with glowing cyan/magenta seams; brick / concrete / planks / ceiling / carpet / windows darkened and cooled (original detail kept: decode -> multiply); `newlogosquare` "US Cars" -> the neon REV / YARD sign; `glow_neons_d` / `halo02_d` -> neon fills (the light panels); lightmap + cubecar dimmed; every material x0.68. Reuses `make_tuner_interior.py`'s encoder helpers. Bank entry names are not renamed (a plain renamed copy works).
- `tools/mod/make_revyard_props.py` (new) - our own OBJ/MTL/PNG props in `resources/mods/packages/tuner-garage/interior/props/`: `lift_2post`, `tire_stack`, `toolbox`, `drum`, `rack`, `neon_bar_{magenta,cyan}`. **Why generated, not exported from `itun_a`:** the tuning shop's lift is baked into the room scene and `eden_3dg.py` reads primitive headers only (no vertex decode). Parts are ours (plan rule).
- `IntroCinematic::loadRoomProps( list, folder )` (`intro_cinematic.{h,cpp}`) - reads `interior/props_<bank>.txt` (`name ref x y z heading [scale]`, `ref` = `room` or an Entity__PosCam name; bank frame), loads each through the shared OBJ route (`LoadObjDraws`) and draws them in `drawSets` at `spotOrigin * Local` (not in the actors-only pass). `-noroomprops` A/B, `-logroomprops` dumps the PosCam placements (the layout helper). `InteriorWalk::loadRoom` calls it for ETNR rooms.
- `InteriorWalk::InteriorBankFor` - `ETNR_<L>` -> `itnr_<l>.bnk` (fallback `itnr_a`). ETNR rooms retype the hostess desk (selectable type 1, car seller) to 23 (`npc_tuner`) via `IntroCinematic::remapSelectableType`. **ETNR_B is a closed room** (`bStreamWorld` off): it stands on arbitrary Ibiza ground and the streamed island's grass AND a tree grew through the floor (the retail dealers sit on cleared pads). `-nograss` alone cleaned the grass but not the tree.
- `TunerShopName( spotBank )` (`tuner.h`): ETNR_A "Wangan Works", ETNR_B "Rev Yard" - used by the map title, the "needs the Tuner mod" notice, the menu title (`TunerMenu::setShopName`); F4 teleporter row "Rev Yard" (`cheats.cpp`, the Wangan row now keys on `ETNR_A`).
- `add_spot.py` now starts from the overlay's own `spots.bin` (spots stack; before, the second `add_spot` silently dropped the first).
- Spot: `ETNR_B` on Ibiza at (12640, 16640) heading 1.21, installed in BOTH userdata folders (`C:\temp\otdu2-win-userdata` and the AppData pref path, see the session 44 two-userdata trap), exterior = the Wangan Works exterior copy (stand-in).

### Verified
- `-interior ETNR_B -nointeriorintro` (AppData, no `-userdata`): the room loads, 17 props placed, hall + aisle shots `docs/images/revyard_hall_s45.png`, `revyard_aisle_s45.png`. `-walkaim Hostess_area` + `press=validate`: the part menu opens titled "Rev Yard - GT-R" with the car on slot D_CAR_01 wearing its wing (the stock rack prop beside it). F4 list on Ibiza: 10 destinations incl. "Rev Yard".
- Ruled out: grass-only (`-nograss`) is not enough - a world tree stands in the room; the closed room is the fix.

### Not done / next
- Not exercised end to end: the F4 "Rev Yard" teleport + walk-in (the headless `-tpdestination` was consumed by the Hawaii start), the real door-prompt path, the map icon, Hawaii (no ETNR_B there; the room is closed so a Hawaii spot is just one more `add_spot.py` call).
- Spot (12640, 16640) is a guess: the exterior is the small Wangan building and may sit in a hedge or tree - check it from the road (`-tp 12640,16640`).
- Background cars: the room has 8 D_CAR_* PosCams (x +5.56 / -5.95, z -13..13, y 0.29); `InteriorWalk::placeDisplayCars` already parks cars on them. Cars on the lifts need a raised PosCam (the lifts are floor-level props now).
- Online "people working on their cars": nothing exists (no netcode); plan = bay slots filled by scripted NPC cars first.
- Tyre stacks / the wall racks were placed from PosCam offsets and not all seen in a shot; windows show a plain night sky (closed room).
- The launcher-side installer for the tuner mod still does not exist (session 44): a new machine needs `add_spot.py` ETNR_A/ETNR_B, `make_tuner_interior.py`, `make_revyard_interior.py` and the `interior/props` copy.

### Addendum (same session): spot checked from the road, background cars in
- **Spot check:** `-travelisland ibiza -tp 12600,16640`: the car stands on a dirt track in a grass field beside a shed, the **ENTER prompt shows** and `press=validate` walks into Rev Yard through the real door path (17 props, `ETNR_B (interior/itnr_b.bnk)`). The exterior is the small Wangan stand-in among trees: acceptable, not pretty; a proper exterior is open.
- **Background cars:** `InteriorWalk::placeRoomCars()` reads `interior/cars_<bank>.txt` (`PosCam carFileName [paint]`, name -> id through `gpDatabase->findCarPhysics`), called from `GMFreeRide::bindTunerGarage()` (now a wrapper around `bindTunerGarageCar()`) AFTER the player's room car holds its place, so display car 0 stays the player's (placeStickerCar returns display car 0). A place the player's car holds is skipped (`IntroCinematic::isPosCamTaken`). `-noroomcars` A/B. Shipped list: GTR, 370Z, 350Z_CPE, Impreza WRX STI, RX8, Delta Integrale, Escort (the player's GT-R took D_CAR_07 in the test, so the Escort was skipped), GTR. Evidence: `docs/images/revyard_cars_s45.png`, `revyard_cars2_s45.png`. Cars stand at floor level in the bays (not on the lifts: raised PosCams would be needed).
- Still open: online bays (no netcode), cars on lifts, mechanics, the F4 teleport end to end.

### Addendum 2 (same session): the tuner mod installs itself - one-time, in the launcher, no Python
The owner: *"can we make it so this can be done automatically when the mod is turned on, leave it as a 1 time toggle, and disabling still lets you in but not use"* -> option A (port it to C++), *"add a disclaimer before toggling about it may take a short while"*.
- `tools/launcher/tuner_install.{h,cpp}` (new, no engine link, WIC for the PNGs): a C++ port of `add_spot.py` + `make_tuner_interior.py` + `make_revyard_interior.py`. From the player's OWN game files it writes `itnr_a.bnk` (itun_a) and `itnr_b.bnk` (icdc_wld) into the LOOSE `<userdata>\mods\Euro\Bnk\interior\`, the ETNR_A/ETNR_B records into the overlay `spots.bin` of Ibiza / Hawaii (extends an existing overlay list, never twice), and the exteriors (copies of `etun_a.bnk`). **Bank rewrite trick:** only payloads change, so the source bank's table/types/directory/indexes sections are copied byte for byte and only the table records, the data section and the two size fields are rewritten (no directory encoder needed). Textures: our PNG -> uncompressed A8R8G8B8 with a box-filter mip chain; the originals are dimmed IN DXT (colour-block re-encode, `DarkenDxtTexture`) instead of the Python's decode -> multiply -> uncompressed (so `itnr_b.bnk` is 9.9 MB, not 13); `.2dm` Diffuse/Ambient scaled. Rev Yard's generated art now ships as PNG (`interior/art_b/`, `make_revyard_interior.py` `make_art()`).
- **Marker:** `<mods>\tuner-install.txt` (`version=1`, `kTunerInstallVersion`). `SyncTunerRoomFiles` copies the props + `props_*.txt` / `cars_*.txt` into the loose `<mods>\interior\` (always on), and the launcher re-runs it at start once installed (`EnsureBundledMods`), so a refreshed package reaches the game.
- **Launcher UI:** ticking "Tuner garage" the first time opens "Install the Tuner garage" (disclaimer: takes a short while, ~40 MB, once, game files untouched, what switching it off does), then a progress bar on a worker thread (Cancel supported); on success the mod is enabled and saved. `--ask-tuner` opens the dialog at start (test hook); `--install-tuner <game> <userdata> <package>` is the headless run (`--log` report). Screenshot `docs/images/tuner_install_dialog_s46.png`.
- **Mod OFF = locked, not gone:** the spots / banks / props / cars live in the loose folder, so a disabled mod keeps the shops on the map (icon + ENTER prompt no longer gated), walk-in at night, props and cars; only the menu is locked ("<shop> is locked: switch the Tuner garage mod on in the launcher's Mods page."), no room car, parts unapplied (saved parts untouched). `docs/images/tuner_locked_s46.png`.
- **Verified:** headless installs into three fresh userdata folders (~20 s, all PASS); the game loads both C++-built rooms (`-interior ETNR_A` dark/neon identical to the owner's screenshot, ETNR_B with lifts + cars); the OFF run on a fresh folder with an empty `enabled.txt` (`Tuner: the tuner-garage mod is off (vanilla)`): room, 17 props, 8 cars, locked notice.
- **Not done / next:** the install is not yet hooked to the launcher's PLAY for a mod that was already enabled before this existed (the owner's AppData still has the Python-built files; running the dialog or `--install-tuner` on it refreshes them to the C++ build); a distributed zip without the repo's `resources` folder has no art -> the installer reports it; Hawaii has only Wangan Works; the DXT dimmer's decode rounding differs from PIL's (not pixel-identical, looked the same); a failed half-install leaves no marker, so it just offers again.

## Session 2026-11-16 (`main`, 45): the despawn/flicker is FIXED - a shadowed `range` plus the slot-VP reuse

The owner: the perf work caused buildings despawning and trees flickering.

### What changed (`source/world/eden_world.{h,cpp}`, `drawInstances`)
- **Shadowing bug**: the LOD recompute branch declared `auto range = ...` which shadowed the outer
  `range` (0,0) - on every frame an instance's LOD was freshly computed (any moving camera) the
  instance drew NOTHING for that pass. Now assigns the outer `range`. This is the visible
  despawn/flicker (session 44's per-value stamps only fixed the frozen-LOD half).
- **Slot-VP reuse (session 44's open item)**: `instanceFrameVpSerial` bumps whenever the slot VP is
  rewritten; each instance's vis verdict stores `VisSerial` and is reused only when stamp AND serial
  match, so verdicts written under another pass's VP are never reused while the camera is stationary.

### Verified
- Stationary A/B (`-tp 21796.3,24188.1 -snaproad 150 -carlod 4 -clock 02.00 -staticcubemap
  -fixedtimestep`, shot at frame 400): cache on vs `-noinstancecache` mean-diff 0.0 (was ~2 % band);
  on vs on (A/A) 0.0. Moving drive looks right (trees/buildings present), but a 2400-frame
  drive A/B is not usable: Hawaii off-road drives diverge on their own (help popup vs tree crash).
- Not checked: a long paired moving drive by eye.

## Session 2026-11-16 (`main`, 44): "Wangan Works" - the shop gets its name; F4 walks straight in; the flicker root cause is HALF fixed (handoff below)

Task: the owner picked the garage's final name (the plan's open question), asked which shop the
retail game uses for customizations, wanted an F4 teleporter button for it, and reported world
objects / buildings flickering and despawning, worse when the camera pans (suspected session 41).

### The shop name + the customization-shop answer
- Name: **"Wangan Works"** (owner's pick of 4). Wired everywhere player-visible: the neon mural
  (`make_tuner_interior.py` SIGN_NAME + the poster/screen art derive their words from it now, with a
  fit-to-frame font size; `itnr_a.bnk` REGENERATED into the overlay - run
  `python tools/mod/make_tuner_interior.py --art`), the walk-up prompt title
  (`map_screen.cpp` ~1369), the part-menu title (`tuner_menu.cpp`), the F4 landmark row and the
  mod-off refusal (`cheats.cpp`, `gm_freeride.cpp`). Evidence: `docs/images/wangan_works_room.webp`
  (teleport-in shot: the GTR on the lift, the dark room, the "WORKS" wall art visible).
- The retail customization-shop question: retail TDU2 has exactly ONE tuning interior -
  `Euro/Bnk/Interior/itun_a.bnk`, shared by every ETUN_* spot; visual paint/vinyls happen at the
  ESTICK_* sticker shops (`istick_b`). There is no separate "customization shop". The mod's room is
  the generated **`<userdata>/mods/packages/tuner-garage/Euro/Bnk/interior/itnr_a.bnk`** (the
  re-skinned copy of itun_a; delete the file to fall back to the vanilla one). The mod package's
  art sources live in `resources/mods/packages/tuner-garage/interior/art/`.

### F4 teleporter: teleport AND walk straight in
- The F4 panel's destination row already had "Tuner garage" (session 42) but it only dropped you at
  the building. `Cheats::LandmarkDestination` now carries an `EnterPrefix` - the **"Wangan Works"**
  row resolves the first `ETNR_*` spot and asks the mode to walk straight in: `takeTeleport` grew an
  optional `pOutEnterSpot` out-param (also honored by `-tpdestination "wangan works"` - that is the
  headless test path), and `gm_freeride.cpp` (~4609) does `interior.enter(spot)` + `interiorKind =
  "tuner"` + `bindStickerShop()` / `bindTunerGarage()` on the same tick (the `-interior` hook's
  path; the room is a closed one, no world stream, so it loads instantly).
- Verified (Hawai, the save's island; `-car GTR -tpdestination "wangan works" -nointeriorintro`):
  `Cheats: -tpdestination 'wangan works' -> ETNR_A (21160, 11500), walk straight in` ->
  `interior/itnr_a.bnk` -> `sticker car 403 on D_CAR` -> `Tuner: the car is on the room's place` ->
  `FREERIDE: teleport walked straight into 'ETNR_A'`. The GTR wore its saved wing.

### Housekeeping
- `map_screen.cpp` carried three RAW NUL bytes inside `'\0'` char literals (committed long ago) -
  they made the whole file unreadable to text tooling (the Read tool refuses it, grep needs -a).
  Normalized to the `'\0'` escape, byte-identical semantics. Trap for the next agent: bash
  double quotes eat `\\0` into a literal NUL - patch such bytes with `bytes([92, 48])`, not
  backslash literals.

### The flicker / despawn: root cause found, HALF fixed - **handoff for the next agent**
`Sector::InstanceFrameInfo` (session 41's per-instance draw cache, `eden_world.cpp`) used ONE
shared `Stamp` for three cached values, and the loop's order made it wrong: the frustum/occlusion
branch and the TD_Ground branch run FIRST and refresh `info.Stamp` to the new frame's stamp, then
the LOD branch's staleness check (`info.Stamp == stamp && info.bComputed`) passes on a LOD computed
for an older camera. **Every instance's LOD froze at the first value computed for that sector
load**: buildings first seen far stayed coarse/impostor blobs up close, and a frozen level whose
items were not in the streamed model drew nothing (the flicker / despawn). Session 41's "pixel-identical"
purity check compared frame 400 of a short straight drive - too early to cross LOD thresholds.
- **Fixed here**: one stamp per value (`VisStamp` / `GroundStamp` / `LodStamp`; stamp equality IS
  validity - the `bVisFilled` / `bComputed` / `bGroundValid` flags are gone). Per-frame LOD
  recompute while moving is back (with its empty-range fallback walk), within-frame sharing
  (prepass fills, opaque reads) is kept, cross-frame reuse while stationary is kept.
- **NOT fixed (the next agent's work)**: the A/B vs `-noinstancecache` still diverges - paired
  2400-frame drives (720p, `-clock 02.00 -staticcubemap -fixedtimestep`, teleport
  `-tp 21796.3,24188.1 -snaproad 150 -carlod 4`, autoplay `60:hold=accel`): ~2 % of pixels differ
  in a horizon band (far-hill ridge), A/A is pixel-identical. Temporary instrumentation
  (`-loginstances` per-pass decision counters, since removed - re-add it, and do not trust a ninja
  build that did not recompile the file, see the map_screen trap above) localized it to the
  **view-projection slot**: with a STATIONARY camera the frame stamp never bumps, so
  `VisStamp == stamp` stays true forever and a pass whose VP equals the slot reuses verdicts that
  were written by a DIFFERENT pass under the same slot (the slot tracks only the current VP, not
  the VP the stored verdicts were computed with). Instrumented groups (cache ON vs OFF, same VP):
  `frustumCulled 0 (visReused 2151)` vs `frustumCulled 1604 (visComputed 2196)` - the ON run draws
  ~1600 instances the OFF run culls, and the far-slice shadow maps then differ (slice 2: 548 vs 305
  caster draws). Fix direction: key the vis verdicts by the VP they were computed with (store the
  slot VP hash per instance, or per group of passes), not just by the frame stamp. Shadow far-slice
  round-robin lag (`eden_shadows.cpp`) is a separate, deliberate tradeoff - leave it.
- Repro of the user report (pre-fix, gone with the fix): drive a while, buildings stay blobs;
  post-fix drives look right, only the stationary-camera band remains.
- Session 44 follow-up (same day, owner tested and "can't teleport to Wangan - the Tuning shop is
  stock"): **there are TWO userdata folders on this machine and the mod was only installed in
  one.** Normal play (the launcher, or any run without `-userdata`) uses the SDL pref path
  `C:\Users\cuz_c\AppData\Roaming\OpenTDU2\OpenTDU2` (gs_file.cpp: `SDL_GetPrefPath("OpenTDU2",
  "OpenTDU2")`); the agent test runs pass `-userdata C:\temp\otdu2-win-userdata`. The launcher had
  enabled `tuner-garage` in the AppData folder's `enabled.txt` and created the package folder with
  only `mod.json` - no island overlay (no ETNR spots: no Wangan Works destination, the vanilla
  ETUN_A "Tuning shop" was the only tuner-ish place) and no catalog/bank. Installed there now:
  `add_spot.py` ETNR_A for ibiza (12560, 10.55, 16700, heading 1.21) and hawai (21160, 6.99,
  11500, heading 2.59, category 4, `--bank` the win overlay's `etnr_a.bnk`), the `tuner/` catalog
  copied into the AppData package, and `make_tuner_interior.py --userdata <AppData>` regenerated
  `itnr_a.bnk`. Verified WITHOUT `-userdata` (the owner's exact path): 9 quick destinations incl.
  "Wangan Works", `-tpdestination 'wangan works' -> ETNR_A`, the room loads `itnr_a.bnk` from the
  AppData package, GTR on the lift. **The launcher-side installer (spots append + catalog copy +
  bank generation) remains the real follow-up** - a fresh machine hits this exact wall. Also note:
  the vanilla "Tuning shop" F4 row (ETUN_A) is intentionally stock - Wangan Works is its own
  building next to it in the row.


## Session 2026-11-16 (`main`, 43): the tuner garage gets its own dark / neon / graffiti interior

Task: the owner wants the customization shop "pimped out" - darker, a custom name, neon, graffiti
on the walls - **without affecting the shops that share the same interior** (the garage walked
into the shared `interior/itun_a.bnk` every retail ETUN_* tuning shop uses).

### What changed
- `tools/mod/make_tuner_interior.py` (new) - reads the user's own `itun_a.bnk` and writes the
  re-skinned **`itnr_a.bnk`** into the enabled tuner-garage package (`<userdata>/mods/packages/
  tuner-garage/Euro/Bnk/interior/`). 13 texture swaps (art generated with PIL into
  `resources/mods/packages/tuner-garage/interior/art/`: graffiti walls, the "TUNER GARAGE" neon
  mural on the poster sheet, neon-lamp recolors, dark roof / floor / display sheets), every
  room material's Ambient / Diffuse x0.55, the baked lightmaps and the `cubecar` cube map dimmed
  **in place** (a small DXT1/DXT5 colour-block re-encoder - the structure stays byte-identical:
  an uncompressed cube rebuild renders the sampler's white fallback). The repo ships only the
  tool + our art; the game folder is never written [DATA: reads the user's bank].
- `source/game/spots/interior_walk.cpp` - the ETNR rule prefers `interior/itnr_a.bnk`
  (fileExists), falling back to `itun_a`; ETNR rooms force the island night game weather
  ("23.20") so the room draws its night lightmap with the neon strength up.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - `setForcedWeather`: a room-set forced
  preset feeding the same selection `-cineweather` uses.

### Verified
- ETNR_A (Ibiza, `-interior ETNR_A`): the dark garage - neon pegboard arch, neon lamps, framed
  neon art, the neon mural - with the GTR on the lift wearing its wing
  (`docs/images/tuner_garage_dark.webp`). Buy flow green under the reskin (`car_tuner_parts`
  moved to `spoiler=wing_ducktail`).
- **ETUN_A stays vanilla**: still reads `itun_a`, bright orange shop, no neon
  (`docs/images/tuner_garage_etun_vanilla.webp`).
- Hawaii ETNR_A through the real enter-prompt path: same re-skin + menu + preview
  (`docs/images/tuner_garage_hawai_dark.webp`).
- Lightmap findings: the night lightmap's alpha is uniform 255 (not load-bearing) and the arch
  frames' retail photos live in the `cubecar` CUBE MAP (reflected by the glass boards) - both
  now dimmed in place; an uncompressed cube rebuild renders the sampler's white fallback
  [CODE: pickTexture's view-type mismatch, eden_renderer.cpp].

### Not done / next
- True TD_NEONDECAL conversion on the poster material (animated / blinking neon needs
  WeatherEmissive param writes + scene-view registration) - the current glow is the recoloured
  flare fixtures + the art.
- DXT compression for the swapped art (uncompressed swaps are why the bank grew 9.3 -> 17.1 MB).
- A launcher button (or first-run step) to run `make_tuner_interior.py`; per-car offset
  eyeballing remains a data edit.

## Session 2026-11-16 (`main`, 42): tuner-garage mod - OBJ materials no longer render flat white

Task: the handoff's one open bug (obj_mesh / mod_props materials flat white).

### What changed
- `source/game/spots/obj_mesh.cpp` - textured materials now set `desc.Spec[0]=1` (S_NBTEXTURES) and `Spec[1]=1` (S_COORDTEXTURE). An all-zero Spec picks the no-texture TD_Gouraud permutation (the same two bytes `eden_rain.cpp` sets). [CODE]
- Whole mod (uncommitted in session 41): tuner.{h,cpp}, tuner_parts, tuner_menu, obj_mesh, make_gt_wing.py, package `tuner-garage`, spot ETNR_A, profile `car_tuner_parts`.

### Verified
- Wear run (GTR, `-tp 12615,16658 -heading 90`): dark carbon GT wing on the car, `docs/images/tuner_wing_gt_wear.webp`. `-notunermod` A/B: 3,128 pixels differ (wing + map icon gone).
- Buy / save / lift flows were verified in session 42's first half (build 201).

### Ruled out
- assignTexture, ViewType, missing shader params, texture cache fallback - none was the cause; the Spec bytes were.

### Second pass (same session): the skipped checklist, all green
- **Ducktail / Street Lip previews** (Ibiza, menu Left/Right): both meshes load and attach live on the room car (log: `part 'wing_ducktail' mesh loaded`, `attached to 'GTR' node 3`, same for street), money untouched on preview. The GTR body carries its own boot-lip spoiler, so the low lips tuck under it - tuner.txt now has a `[car:GTR]` `spoiler_offset = 0 0.82 -2.05` override (z keeps the ducktail's 0.26 m depth inside the body's -2.317). Offsets are first-pass; per-car eyeballing is a data edit.
- **Non-tuner-car refusal** (starter California at the garage): the hint shows "This is not a tuner car. The garage works on the tuner style: the GT-R, the Zs, the WRX STI, the RX-8..." (`docs/images/tuner_garage_refusal.webp`). `bindTunerGarage` now also gates on `isTunerCar`, so a refused car gets no room car.
- **Unknown-part-id skip**: renamed `wing_gt` to `wing_gt_old` in the installed tuner.txt, wear run -> `Tuner: 'GTR' part 'wing_gt' is not in the catalog (kept as data, not drawn)`, no crash, `car_tuner_parts` intact in profile.sav, catalog restored after.
- **Hawaii spot walk-in** (`-travelisland hawai -tp 21130,11500`, the real enter-prompt path, not `-interior`): the hawai overlay spots.bin record drives it - `discovered 'ETNR_A' (tuner)`, the hawai `etnr_a.bnk` exterior + its 294-triangle collision at (21160, 11500), walk to the npc_tuner seller, menu up with the car on the lift, ducktail previewed (`docs/images/tuner_garage_hawai.webp`). Note: the `-interior` hook resolves the spot too early under `-travelisland` (it grabbed the ibiza record) - the enter-prompt path is the real one and is what was verified.
- **Overlay cleanup**: the session's test leftovers (`mods/props.txt`, `mods/props/cube.*`, `props/red.png`) removed from the userdata; the red test texture was already replaced by the real carbon.png (byte-compared against the repo).

### Not done / next
- Spoiler placement is visual-only (no aero change); the offsets need per-car eyeballing by the owner (data edits).
- Distribution packaging: the overlay spots.bin record still comes from a manual `add_spot.py` run - a launcher-side generator (a C++ port of the append) is the follow-up.
- Next parts per the plan: neon/underglow, number plates (all cars), then the mesh-swap parts (body kits, hoods, seats) - each is "author an OBJ + a tuner.txt row" plus slot-specific logic.

## Session 2026-11-16 (`main`, 41): 4K High 17.9 -> 15.9 ms (56 -> 63 fps) with the image untouched - shadow far-slice round-robin + per-instance per-frame cache

Owner: "What else can we do to improve performance without cutting down the graphics quality I want you to
make more improvements please thanks". Picked up `docs/PERF_PLAN.md` §0a's plan-to-60 list; full write-up in
the new **§0b** there (fresh `-sampleprof` table of everything that is left, with the next steps). Two
levers landed, both CPU-only, both image-identical by construction and by measurement. Baseline on the
session's start tree: frame 600 **17.9 ms (56 fps)** at 4K High (env 2.9 / prepass 6.4 / shadows 4.6 /
opaque 2.2, GPU ~5.5 ms).

### What changed
- `source/render/eden/eden_shadows.{h,cpp}` - **far-slice round-robin** (PERF_PLAN 2 #3): slice 0 (the car's)
  renders every frame, slices 1-2 alternate (`-shadowrate <n>` spaces them out, `-noshadowcache` = the old
  every-slice-every-frame). A skipped slice keeps its map AND its light matrix / texel size / depth range, so
  `resolve` projects into the map that is actually in the target; the whole cache drops on a config change
  (generation counter in `setConfig`/`destroy`) or a sun step > ~1.15 degrees (a weather switch must not show
  a late shadow direction).
- `source/world/eden_world.{h,cpp}` - **per-instance per-frame cache** (`Sector::InstanceFrame`,
  `-noinstancecache` = the A/B): the prepass, the shadow slices and the opaque pass submit the same instances
  3-4x a frame; the LOD pick, the TD_Ground flag and the occlusion verdict are memoized per instance for one
  camera position (stamp = camera + Draw-Distance scale + LOD factor + drawEpoch). The frustum bits belong to
  a projection: non-shadow passes own one VP slot (the prepass fills it, the opaque - same projection, same
  occlusion grid - reads it), shadow-caster passes compute inline and never touch the slot. `drawInstances`
  now takes a non-const `Sector &` (sole caller passes one).

### Verified
- Shadow cache: paired 4K High runs (`_perf_sha_on/off.log`) - **shadows 4.8 -> 3.3 ms, GPU 0.90 -> 0.66**;
  frame-400 pixel diff mean 0.0001/255, 0.001 % of pixels > 2 (max 6) in one 195x348 far-shadow patch (the
  expected staleness); validation run clean of new VUIDs.
- Instance cache: clean paired runs (`-clock 02.00 -staticcubemap`, back to back) - **13.9 -> 12.0 ms
  (72 -> 83 fps)**: prepass 6.6 -> 5.9, shadows 2.8 -> 2.4, opaque 2.2 -> 1.7. Frame-400 diff vs
  `-noinstancecache`: **pixel-identical** (0 differing pixels). A/A runs of one build: pixel-identical.
- Combined, default visuals: **17.9 -> 15.9 ms (63 fps)** at 4K High (`_perf_inst.log`).

### Ruled out (with the test that ruled it out)
- "The image differs" (first cross-build comparison, whole sky): the game clock seeds from the PC wall clock
  and the weather ramp from spawn is mid-flight for the first seconds - a 02:05 run showed a starry night
  where a 02:19 run showed a grey deck. Pin every comparison run with **`-clock 02.00` (dot format - a colon
  parses as :00)**; `-clock` freezes the rate. Cost a confusion cycle; written into PERF_PLAN as a rule.
- A 470x357 diff patch on the car in the first instance-cache A/B: the env-cube refresh timer
  (`refreshTimer += frameDelta`) runs on REAL time, so runs with different frame rates refresh the cube on
  different frames and the paint reflections shift. `-staticcubemap` pins it out; the flag pair is then
  pixel-identical. The cache itself is pure.
- GPU instancing of the .pmi draws (PERF_PLAN 2 #2): parked - it needs the world matrix out of the per-draw
  constants, i.e. a mechanical SPIR-V patch of the 116 shipped shader libraries; retail drew per-instance too.
- `vkGetInstanceProcAddr` at 7.6 % leaf in the profile: symbolization artefact (volk loads device tables
  directly; the samples are loader/driver dispatch). Not a call site.

### Not done / next
- **The grass emit is the next big lever (~22 % of the main thread)**: `buildGrassBatch` 12.2 % +
  road queries ~8 %. On a drive the camera moves > 2 m/frame, so the 140 m square (~139k .pgr cells)
  re-walks every frame. The cheap fix (build margin) is NOT image-identical: the density law's keep
  probability is camera-dependent (the 10-60 m ring). Image-identical option: memoize the road probes
  (`GrassRoadIndex::nearRoad`, `SampleRoadSurface` - pure functions of world x,z, probe points hash-stable
  per cell) in a persistent per-run table. Bigger option: make CPU placement camera-independent and move the
  camera term into the shader (needs the owner's eye).
- Per-draw resolve ~14 % (`fillConstants` 6.6 + `recordDrawImpl` 5.8 + `FindSmall` 2.0): cache the
  world-independent part of the constant block per (program, material, fingerprint), write only the
  world registers per draw.
- **Pre-existing startup VUID found** (one-shot, frame 0): `vkQueueSubmit` expects an image in
  SHADER_READ_ONLY, current UNDEFINED (`_sha_valid.log:501`). Fires with `-noshadowcache` too, and session 36
  measured 0 render VUIDs before clouds v3, so the suspect is the cloud composite's first-frame history
  sample (860ef3b changed that path after my validation). Repro: the §0a command at 720p +
  `-vk_enable_validation_layer`; fix the history reset to render (not sample) on frame 0.

### Addendum (same day): the owner's DeepSeek dev-build dig mapped onto the perf plan
The owner relayed DeepSeek's dev-build findings; captured in `docs/re/pmi_streaming_spec.md` **1.2a** and
PERF_PLAN §0b. Two are actionable: (1) **MASSINSTANCE_VIS_MAX is retail's own visible-instance budget**
(1000/4000 per list, overrun prints `TOO MANY VISIBLE MASS INSTANCES`) and the port does not implement it -
the direct lever on the ~8k draws/frame (`-massinstancevismax` knob proposed, image-affecting, owner judges
the density); (2) **SETINSTANCIATEPMIACTORS\* proves retail hardware-instanced pre-merged actors** - the
`TD_HardwareInstance` shader family (2 VS variants, already in the port) is the first place to check for a
per-instance stream, which would make GPU instancing feasible without patching the other 114 shader
libraries. Also new: `CULLINGCONFIG`'s 4th int is an angle in degrees; Eden's own PerfLog CSV profiled
instance counts against 33.33 ms (draws, not fill rate - consistent with the port's measurements).

## Session 2026-11-16 (`main`, 40): the retail menu / UI sounds play - `fe_snd.bnk` + its `sounds.ini` cue map + the `sndp:` hook

Owner: *"I want you to start addingsound effects for menu's and in game like collisions etc... thanks
you can use dev build if you want to dig for refferences"*.

**Where the sounds were all along.** `Euro/bnk/frontend/sounds/fe_snd.bnk` (178 entries) holds
`sounds.ini` - retail's own name registry - and 177 MS-ADPCM 48 kHz stereo wavs. `sounds.ini` maps the
flash message a movie sends (`sndp:<name>`) to a wav and a volume:
`AddFESound "mopen" "menuOn01.wav" 0.3`, 189 such lines (`mopen`/`mclose`/`move`/`valid`/`wrong`/`back`/
`ttip`/`switch`/`st_*`/`md*`/`rd_*`/`S_*`/`UPMONEY`/`UPBET` ...), plus the `SetMusic` shop-music table
(not used yet). The bank is played on the **"FrontEnd"** bfx client (7): `audiosetup.xmb` points it at the
`FrontEndStereo` pipeline and the `Menu` mix preset keeps it at gain 1 while muting the world; the `Freeride`
preset keeps it at 1 too, so pause-menu cues sit on top of the game without any game-mode switch.

**What was built.** `source/audio/frontend_audio.{h,cpp}`: opens the bank (kept mapped, 4.8 MB), parses
`sounds.ini`'s `AddFESound` lines (cue -> wav + volume, case insensitive), decodes a wav on first use and
plays it non-spatial on client 7; a cue repeated within 40 ms is dropped (the movies fire some twice).
`FlashPlayer::SetSoundCommandCallback` is the movie side: `dispatchFSCommand` feeds every
`FSCommand("sndp:<cue>")` from **every** FlashPlayer (boot menu, pause menu, HUD, map, shops, casino HUDs)
to that one handler and still passes the command to the menu's own callback. `PlayFrontEndCue()` is the
native side, for the menus the port drives itself (their label swaps bypass the movie scripts): the pause
menu's fallback column + its blocked entry, the map's cursor menu, the dealer / estate / fuel / paint /
school / tuning / championship menus, the clothes shop, the advanced-graphics panel. `gm_login`, `gm_shop`,
the casino HUDs and the HUD need nothing - they send keys to their movie, which fires `sndp:` itself.

**Verified** (build 196, headless `-gm freeride`, `-logfrontendaudio`, autoplay pause/right/valid/map):
`FrontEndAudio: 'Euro/Bnk/frontend/sounds/fe_snd.bnk', 189 cues, client 9 (FrontEnd, pipeline 2)`, then
`mopen`->menuOn01 (0.30), `ttip`->toolTip02 (0.20), `move`->move04 (0.10), `valid`->valid01 (0.90),
`mclose`->menuOff01 (0.30), the map's `switch`->trigger, and - from the HUD movie, unprompted - the race
countdown's `three`. The pause menu's own FSCommands (`sndp:mopen`, `sndp:move`, `sndp:valid`, `sndp:ttip`,
`sndp:mclose`) confirm the hook rather than the native fallback is what plays in the movie-driven column.
Collision audio (already in the tree) was re-verified in the same sweep: `CollisionAudio: metal 6/6,
ground 6/6, car 4/4, scrape loops 4`, and a wall hit logged `'carmetal_high03_PHYSIC_SFX' volume 1.01`.

**Round 2 (the owner: "I heard the menu sounds you added ... please continue with the other stuff")**:
the place music and the sticker sounds. `source/audio/frontend_music.{h,cpp}` parses the bank's second
table - `SetMusic "<place key>" "<wav>"` x113, the wavs loose in `Euro/bnk/sound/musics/` - and plays one
looping track on the **"FrontEndMusic"** client (8), which `audiosetup.xmb` puts in the **"Music" mixer**
with the radio (`Menu` 0.2, `SHOP` 0.5, `Freeride` 1). One voice with fade-in 0.5/s / fade-out 1.2/s
(`update()` from the main loop), the wav decoded on switch and the previous clip dropped (a 1.4 MB track is
~25 MB of float frames). Key resolution: the shop interiors' `I` entry first (`ECD_FERR` -> `ECD_FERRI`),
else the spot's own (the clubs / airport / houses have no `I`); a house spot drops its `_<n>` part
(`EPH_L3DI_5067I` -> `EPH_L3DI`). Hooked at `InteriorWalk::loadRoom` (shops at 1.0, house living rooms at
0.8; `GARAGE`/`DRESSING` have no shipped wav so the house track stays in the garage), `InteriorWalk::exit`,
and `GMShop::enter`/`exit` so the retail showroom keeps the dealer's music under the `SHOP` preset. The
sticker editor's native states in `paint_menu.cpp` now play its own `st_*` family: `st_move` on the pack /
grid / placed lists and on a placement's move key, `st_uscal` / `st_dscal` on the scale keys, `st_rotat`
on rotate, `st_apply` on the placement being bought / committed, `pdelete` on a removed sticker, plus
`switch` on a monochrome recolour and `back`/`valid`/`wrong` on the transitions.

**Verified round 2** (build 199): `FrontEndMusic: ... 113 place tracks, client 8 (FrontEndMusic)`;
`-interior ECD_FERR` -> `loaded 'CarDealer.wav' (32.0 s)`, `'ECD_FERRI' -> CarDealer.wav (voice 41)`, then
`'ecd_ferri' at gain 1.00` (the fade landed) and `'ecd_ferri' stopped` on the B that left the room;
`-interior ECH_IA` -> `'ECH_IA' -> ClubIbiza.wav` (the `ECH_IAI` key does not exist); `-enterhouse
EPH_L3DI_5067I` -> `'EPH_L3DI' -> PlayerHouse02.wav (0.80)`; `-gm shop -shopspot ECD_FERR` ->
`'ECD_FERRI' -> CarDealer.wav` in the showroom.

**Not done / next** (single next step: the screens' music): `MAP` / `LOBBY` / `REPLAY` / `EDITOR` /
`TDMEDIA` have `SetMusic` entries and wavs; wire each screen (the map opens and closes from ~8 places in
`gm_freeride.cpp`, and a shop interior's track must come back when the map closes). Then the seller voice
lines (still no data - do not invent audio). Detail: `docs/re/frontend_audio_spec.md`.


Owner: "I want to have volumetric clouds but 3 attempts and still haven't really seen a single cloud ... as for the 2nd attempt we had clouds but were static way to big and a bit of an eye sore to look at".

**Reproduced before changing anything** - build 194 (`fceb7b4`), `_c3_day.ppm` vs `_c2_off.ppm`: mean |difference| in the sky band **0.16/255** (`-novolclouds` A/B). The march itself was drawing (`-showclouds` differed from clouds-off by **92.4**), the composite contributed nothing. The committed v3 evidence images agree with the owner: `clouds_v3_day.webp` and `clouds_v3_overcast.webp` differ by **2.8/255 mean** and the "overcast" shot is blue sky (RGB 68,102,145 at y=40), i.e. v3's "Done" was never on screen.

### What changed (all in `source/render/eden/eden_clouds.cpp`, `eden_scene_view.cpp`, `resources/shaders/volclouds*.frag`, `depthcopy.frag`)

1. **The packed alpha was destroyed by the render target.** The march packs `(alpha, hitDistance)` as two halves *inside* the alpha channel, but the march/history targets were `R16G16B16A16_SFLOAT` - writing a float whose *bits* are two halves converts it as a number, so the composite always decoded `alpha ≈ 0`. Targets are now `R32G32B32A32_SFLOAT` (half res, ~15 MB total).
2. **The packed value was then linearly filtered.** Even with 32F storage, `texture()` on the history interpolates the packed float: the composite drew white speckles over the whole scene. New `sampleAlphaHit()` (both shaders) decodes the 4 nearest texels with `texelFetch` and blends the *decoded* pair.
3. **The composite inherited the D3D flipped viewport.** `resumeScene()` sets `height = -height` for the scene draws; the composite fullscreen pass then mapped texcoord (0,0) to the bottom row, so the layer was upside down (clouds for up-rays drew on the ground, the sky stayed clear) and the depth test compared the wrong rows. The composite now draws with a plain Vulkan viewport and restores the saved one.
4. **The scene-depth copy returned a constant 1.0.** `depthcopy.frag` sampled the depth image with `texture()`; `texelFetch` (1:1 copy) makes it return the real depth. That also repairs the FSR/DLSS depth input, which shared the shader.

Tuning, once it was on screen: coverage is now a **measured quantile of the generated noise** (`thresholdForCoverage`, precomputed from a 32k sample histogram; log `EdenClouds: base noise median 0.463, coverage 0.50 -> threshold ...`) instead of the hand-tuned `mix(0.72, 0.34, ...)` that made the 0.5 default effectively clear; the deck is mapped so 0.5 leaves ~28 % of the field as cloud and 0.86 ~70 % (rays graze the slab, so volume fraction reads much denser). Shape tile 4500 -> **2600 m** (the "way too big" of attempt 2), march reach 16 -> 26 km, grazing fade window 0.10 -> 0.03 (the old one erased the whole horizon band, which is where a 1-2 km deck appears), distance fade moved out, three multi-scatter octaves with a 0.10 transmittance floor (single-scatter `exp()` is ~0 at real cloud depth, so the deck read as flat grey), powder reduced, wind 12 m/s fair-weather / 32 m/s storm (was 2.4 m/s).

### Verified

- Default CLOUD_B day, camera pitched up: `docs/images/clouds_v4_day.webp`; overcast `-cloudcover 0.86`: `clouds_v4_overcast.webp`; rain `-intempery 6`: `clouds_v4_rain.webp`.
- Buildings occlude the deck sharply (`-showcloudalpha`: scene depth varies 0.68-1.00, cloud depth equals the scene on the ground and the alpha is rejected there; sky rows keep alpha 0.5-0.83).
- Drift: two shots 3.3 s apart in one run (`-autoplay 260:shot=...;460:shot=...`) differ by **4.39/255 mean** in the sky band (the static attempt measured 0.1-0.5).
- Cost: `[perf] frame 240` **7.8 ms** with the layer vs **8.6 ms** `-novolclouds`; frame 360 9.1 vs 9.0 ms - within run-to-run noise.
- `-novolclouds` / `-oldclouds` still draw the `clouds.3dd` billboards.

> Build note: every capture above was taken on **build 194 (`fceb7b4+dirty`)**,
> which is the exact source state of commit `860ef3b`; the rebuild after the
> commit only changes the stamp (`195`, `860ef3b+dirty`), not the code.

### Ruled out (with the test)

- "The noise is empty": `-showclouds` was bright; `-showcloudmarchalpha` (`-showcloudhist`) showed the march alpha reaching 1.0.
- "The temporal accumulation eats it": `-cloudnohistory` (history weight 0, reset every frame) changed nothing.
- "The depth test rejects everything": `-noclouddepth` changed nothing before fix 3, and `-clouddepthflip` showed the sample was the wrong row - the viewport, not the depth image.
- "The weather never selects CLOUD_B": the log prints the resolved preset; the default preset is B and its coverage is 0.5.

### Not done / next

- Clouds are still absent from the env cube / planar mirrors (the retail billboards stand in there).
- The sunset silver lining is unverified (the test street hides the sun); check a west-facing shot at `-clock 19` with CLOUD_B.
- The layer is soft at half res by design; a checkerboard/quarter-res option would buy ~0.3 ms but needs a mask pass.
- `-showcloudalpha` / `-showcloudmarchalpha` / `-noclouddepth` / `-clouddepthflip` / `-cloudnohistory` are documented A/Bs, not user flags.


## Session 2026-11-16 (`main`, 38): volumetric clouds v3 - Perlin-Worley 3D noise, temporal accumulation, depth-rejected composite

Owner: "I want you to improve the clouds I want them to be realistic volumetric if possible I had claude try twice and honestly there not that great".

### What changed
- `source/render/eden/eden_clouds.{h,cpp}` (new): `EdenClouds` - CPU generated tileable Perlin-Worley 128³ R8 + Worley 32³ RGBA8 3D textures (multithreaded, the ocean-NoiseSlice pattern), half-res RGBA16F march + history ping-pong targets, pass orchestration, exponential weather smoothing (tau 4 s shape / 6 s slab) that removes the mid-blend string pop of `clouds_cloud3dname`, camera-jump history reset.
- `resources/shaders/volclouds.frag` (rewrite): 3D-texture shape (coverage threshold + height profile) with Worley detail erosion, 56-step march through a generic slab intersection (works from above the deck too, which the old shader did not render at all), 6-tap sun march, two-octave multi-scatter, dual-lobe Henyey-Greenstein, grazing-angle + distance fades. **Fixed attempt 1's ray y-flip** (D3D texcoords, y down): the old pass marched screen-bottom rays upward, so the clouds only ever showed as a band at the horizon. The alpha channel packs `packHalf2x16(alpha, hitT)` for the next two passes.
- `resources/shaders/volclouds_accum.frag` (new): TAA-style accumulation - reprojects the history with the previous camera basis, neighbourhood-clamps colour + alpha, history weight 0.88. This is what removes the ray dither (documented as visible in session 16).
- `resources/shaders/volclouds_composite.frag` (new): samples the history + a copy of the opaque scene depth and rejects the cloud where the scene is nearer: mountains hide the clouds, the clouds stay in front of what is behind them, the car / cockpit (near depth) always cover them. Premultiplied blend, no depth writes.
- `eden_scene_view.*`: march + accumulate in their own "clouds" pass before the opaque pass; scene-depth copy between the passes; composite in the blended pass **after `flushDeferred`** - the sky dome is itself a deferred blended draw, so a composite drawn in the opaque pass (every earlier attempt) was simply painted over by it; weather targets from `clouds_cloud3dname` (A/B/R mapping kept [INF]); `-showclouds` / `-showcloudhist` debug views; stale comments fixed.
- `postfx/fullscreen_pass.*`: optional depth test / write and sampler-address parameters (both defaulted - no existing call site changed).

### Verified
- Overcast `-cloudcover 1.0`: full deck, buildings occlude it sharply (`docs/images/clouds_v3_overcast.webp`).
- Rain `-intempery 6`: dark heavy overcast, wet-road mood (`docs/images/clouds_v3_rain.webp`).
- Default CLOUD_B day (`docs/images/clouds_v3_day.webp`); bonnet camera: the hood rejects the clouds, no X-ray (`docs/images/clouds_v3_bonnet.webp`) - the class of bug that killed attempt 1.
- `-oldclouds` / `-novolclouds` / `-cloudcover` A/Bs all behave; `-clockrate 200` dry-to-night cycling ran with no pops and no device lost.
- Perf `[perf] frame 240`: 6.9 ms with the layer vs 8.3 ms `-novolclouds` (draw 6.6 vs 8.0) - no measurable cost (half-res march + a cheap composite instead of the old full-res heavy-ALU march).

### Ruled out (with the test that ruled it out)
- Depth-tested composite in the opaque pass (three orders tried): invisible in all of them; the `-nosky` A/B showed clouds the moment the dome was gone -> the dome is drawn from `flushDeferred` (eden_renderer.cpp defers every blended material), it paints over anything earlier. Compositing after the flush, against a depth copy taken before the deferred draws, is the position that works.
- "The noise texture is too dark": measured median 0.18 in a Python mirror of the generator, rebalanced to 0.46 - necessary but not the blocker; the y-flip was.

### Not done / next
- Clouds are still absent from the env cube / planar mirrors (car reflections use the retail billboards): `renderEnvironment` would need a low-quality march per face.
- The sunset silver lining is not yet verified on screen (the sun sits behind buildings in the test street); the phase term + physical sun colour are in the shader - check at a west-facing spot at `-clock 19`.
- Rays grazing the slab (the deck edge-on, seen from above) fade out; a mip-filtered detail fetch could bring them back if the owner ever sees it.

## Session 2026-11-16 (`main`, 37): panel feed is a car-only showcase pass (cost 15 ms -> 0.3 ms); progression pages checked

Owner: "what can we do to get that extra cost down ... in the 360 the window render doesn't look like anything else is rendered, floor doesn't match, everything grey - maybe a boxing method for the 2nd camera".

### What changed
- `eden_scene_view.*`: `setShowcaseMode( on, backdrop )` - no world / sky / sun halo / clouds / ocean / haze / rain, no env-cube refresh, shadow casters = the objects only, flat grey clear (0.30, 0.31, 0.33). `intro_cinematic.*`: `renderInset( cam, target, uv, showCar )` draws ONLY that showroom car (`insetShowCar` filter at the top of `drawSets`). `gm_shop.cpp`: the panel pass uses it, and refreshes every 3rd frame (immediately on a page change; `-insetevery` = every frame, `-insetfull` = the old full-scene panel for A/B).
- `pause_menu.cpp`: test hooks `-unlockprogress` (Game Progress entry in free roam) and `-progresstest <n>` (every detail row + ring at n %).

### Verified
- Measured on the Audi buy window paint page: no inset 13.3 ms, full second scene render (`-insetfull`) 28.4 ms, showcase pass every frame 16.9 ms, showcase every 3rd frame 13.6 ms. Panels show the car on plain grey on all three pages (`docs/images/shop_panel_showcase_grey.webp`, `shop_panel_showcase_pages.webp`).
- Progression: `-unlockprogress -progresstest 60` - the Completion page and the Competition / Collection / Discovery pages render with their ring gauges (the ring lights with the percentage); the detail rows in `MAIN.vmf`'s `simple_menu` lines are a percent text + a checkbox only (the AS sets `/:Percent` and `/checkbox`; no bar clip in the line). The level-up popup (`-testlevelup`) and HUD earn bar draw. No missing bar found there.

### Not done / next
- The row percents (`kPercents`) are still 0 except roads: nothing earns Racing School / Championship / Vehicles ... percentages, and the checkboxes never tick. If the owner means a different bar (a screenshot of the 360 screen would say which), start there.
- Interior colours still pale grey (session 34).

## Session 2026-11-16 (`main`, 36): buy-window panel live feed + Flash morph shapes (stat bars)

Owner (360 captures): the buy menu's panel shows a live close-up of the part; stat bars (Acceleration / Speed / Braking) were empty; asked about the road discovery / progression bars. DeepSeek's dev-build dig: the panel is a real-time RTT of the showcase car (`VwPrtDtlsGMShop` at `viewport_details`) with named presets C_WHEELS_V / C_PAINT_V / C_INTERIOR_V - not video.

### What changed
- `source/flash/flash_definitions.*`: **DefineMorphShape (tag 46)** decoded [DATA]: Eden header (id, 2 RECTs, u32 end-edges offset, solid start/end RGBA, 12-byte line styles) + SWF bit-packed start / end edges; interpolated per placement ratio, triangulated and cached per (id, ratio). `flash_player.cpp` draws morphs through `getMorphShape(id, instance.Ratio)`. This was why the shop's stat gauges had no fill (the fill is clipped by a morph mask, char 4975); 337 morph shapes exist in the UI movies. Only solid fills decoded; `-logmorph` dumps tag bytes.
- `gm_shop.cpp/.h`, `intro_cinematic.*`, `eden_scene_view.*`, `flash_player.*`: `IntroCinematic::renderInset` renders the scene a second time from the panel camera into a 512x380 target (`EdenSceneView::getFinalColor` + a 2D quad copy, depth attachment required by the 2D pipeline), bound to `/buy/window_buy/lure` + `/buy/lure` via `setBitmapOverride`, alpha forced with the new `setClipAlpha`. Main vs panel poses per page: wheels = rear-quarter car + wheel; paint = corner orbit + body side; interior = driver's seat + open-door cabin. The wheels page leaves the movie's lure parked off stage, so `setClipTransform(79.3, -57, scale 124.3 / 109)` places it (native paint-page matrix read with `-logpanelmatrix`; y is calibrated, [INF]). `-noinset` = A/B, `-dumpshopclips <depth>` / `-dumpshopat <frame>` dump the shop movie's clips.

### Verified
- Stat bars fill by value (95 / 64 / 95): `docs/images/shop_stat_bars_morph_masks.webp`.
- Panel feed on all three pages: `docs/images/shop_panel_live_feed_wheels.webp`, `shop_panel_live_feed_pages.webp`. Full Audi R8 buy still completes (bought car 442). Validation: 12 -> 0 render VUIDs; one remaining message at exit is a device-shutdown leak (VkBuffer), not from the inset.

### Not done / next
- Road discovery / progression bars: not opened yet; the morph decoder may already fix them - check the pause-menu progression page and the map's discovery bar first.
- Cost: the second scene render doubles the shop's 3D time (`-noinset` to compare); the env cube refreshes twice.
- Interior colours still pale grey (session 34); interior main camera pose is a first guess; the wheel in the wheels panel sits slightly low / right.

## Session 2026-11-16 (`main`, 35): buy window - paint page orbits the car corner to corner, interior page opens the door

Owner (360 recomp captures): the paint view draws a box around the car, holding at each corner then panning; the interior page opens the door by itself and the panel shows a live feed of the view.

- `gm_shop.cpp/.h`: `applyPageCameraPose(dt)` - paint (page 3) orbits 4 corners (1.8 s hold, 2.2 s smoothstep pan about the car centre, timings [INF]); interior (page 4) opens DOOR_L over 0.8 s (`setDoorAngle(0,-0.95)`), closed again by `closePageDoor` on leaving / closing. Evidence: `docs/images/shop_paint_orbit_interior_door.webp` (4 orbit frames + the open-door cabin).
- Not done: (1) the in-panel live feed (retail crops the part view into the panel; ours shows an unrelated shop view there), (2) the stat bars (Acceleration / Speed / Braking, Difficulty stars on the car info panel) and other bar widgets, (3) the road discovery / progression bars, (4) interior colours still read pale grey (see session 34).

## Session 2026-11-16 (`main`, 34): buy-window page cameras + live rims / interior preview landed; hologram double arrow fixed

Picked up GLM's uncommitted work (quota ran out) and the owner's report: "2 arrows instead of 1, one side correct, the other flipped".

### What changed
- `gm_shop.cpp/.h`, `cinematic_car.cpp/.h` (GLM, reviewed + verified here): per-page camera for the buy window (wheels / paint / interior poses around the podium car, re-applied at the end of `tick` because `walk.update` overwrites the camera; the confirm page restores the walk view so C_BUY_CAR_I plays), `CinematicCar::swapRims` (reloads the two rim meshes + diameters on the browsed DBRims row), `CinematicCar::applyInterior` (DBInterior colours onto the `C1_*` / `C2_*` cockpit materials). Poses are port-native [INF] (the dealer banks carry no C_WHEELS_V / C_PAINT_V / C_INTERIOR_V tracks).
- `spot_holograms.cpp/.h`: the frame scenes carry TWO arrow nodes on the one `Arrow_Blue_01` material (`..ROW_FR` front, `..ROW_SI` side). Only the first was hidden and re-aimed, so the second drew as authored = the flipped extra arrow. All arrow nodes are hidden from the group draw now; the front one is redrawn aimed.

### Verified
- Buy run (`-gm SHOP -shopspot ECD_AUDI_1276I -shopaim 6`, the Audi key recipe): log `page camera 2/3/4`, `rims swapped to audi/r8_spyder_f_03`, `interior ... on 1 material(s)`, `bought Audi R8 ... car 442`; `docs/images/shop_page_cameras_rims_paint_interior.webp` (wheel close-up before/after swap, paint 3/4, cabin, confirm).
- Hologram: `-atspot ECD_FERR_3337I -weather 18`, reverse: one arrow leaning at the dealer (`docs/images/spot_hologram_one_arrow.webp`).

### Not done / next
- Interior preview reads pale grey on a "Black Nappa" row: `applyInterior` lightens the swatch 20 % and the `R8_V10_Int_Noir_01` library lookup may be missing (falls back to grey 0.32) - check `FindLibrarySwatch` and drop the lightening for dark ids. The retail apply (sls_amg_int1/int2 map swap) is still undecoded.
- Bikes' `C_PAINT_V_BIKE` view not done. Owner to eyeball the camera poses in play.

## Session 2026-11-16 (`main`, 33): spot holograms rebuilt on the entrance anchor + frame arrow; birds per-island + path flock

The owner (taking over from another vendor): "the 1 thing that really really needs work on both islands is the hologram enter markers for races, shops, dealerships etc... I did try to get claude to really dig in the dev build the data might be there for it." Also: "we need birds and Airplanes flying over I don't think the islands share the same stuff like traffic etc".

### What changed
- `source/game/freeride/gm_freeride.cpp` (`tickWorldSpots`, hologram block) - the plates anchor at the spot's **road-side entrance** now: `FindSpotEntranceById` (spots.bin +0x28/+0x38, the same record the enter press and `-atspot` use) instead of the building point + an invented 12 m eye-nudge + a per-spot-hash 7 m side offset. The old anchor put the plate INSIDE the building (Ferrari dealer Ibiza: no hologram visible from the road at all - the plate stood in the showroom volume; G-Miami: clipped into the canopy). Scale is authored 1.0 (was `1.5*clamp(d/60,1,2.2)` - the retail capture reads the 10 m frame 1:1 against the street). The frame AddOn draws with the plate's heading (the old `+pi` "drawn from behind" flip showed only its pointer cone).
- `source/game/freeride/spot_holograms.h/.cpp` - each frame model finds its `Arrow_*` node (the flat direction pointer; material name probe). `draw()` hides it via `EdenDrawParams::ForceHiddenNodes` and redraws it alone (`EdenModel::drawNode`) with the plate's heading + a 0.61 rad roll toward the building's door (door bearing = building point - entrance point, projected on the plate's right axis; positive RotZ leans the tip LEFT - verified against the Ferrari approach; `-holoarrowflip` = A/B). `-logspotholo` now also dumps materials (shader set) and scene nodes.
- `source/game/freeride/birds.h/.cpp` - flock slot 0 is a **path flock**: spawns 300-500 m AHEAD of the drive at 18-32 m with a heading across the road (log `Birds: flock0 ... (path flock, crossing ahead)`); the other four slots keep the behind/around behaviour. The owner asked for birds "flying over" - they previously spawned mostly behind the camera.

### Decoded (dev build + data)
- `pack_spots.bnk` loads as a scenaric pack: `Scenarics\Packs\pack_spots` beside worldactors + worldlifeset [CODE-dev 0x892250..0x8922d4]; `SpotMesh` is a `SCEntityFactory` entity class (name getter 0x8a5490; ctors 0x8a54a0/0x8a5550). `MngSpots.cpp`/`Spot.cpp`/`Spots.cpp` exist readable in the dev exe only.
- The record's `Type` condition (+144, `{VIRTUAL if; VIRTUAL then;}`) has a `then` word taking 9 values (0x36..0x52 step 4, 0xaa) = the xmb schema's nine `Struct*` classes in order: House, Club, Goodies, Other, Tuning, School, DriveIn, Challenge (0x52: Event/Cup/Championship_*/the multipliers), CarDealer 0xaa (every CD_*/CDC_* plate). Playerhouse/House_To_Buy share 0x36 with Photograph/Surgery/Airport/Used_Car_Dealer/Hair_Stylist/Casino, so the class alone does not pick the plate - the `if` side is NOT decoded (its second word varies per record; the AddOn frames store 64-bit hash pairs from a non-Eden hash function). spots.bin +0x100 (0..0x19) is the map category, not this enum. The port's name-prefix mapping agrees with every decoded value; unchanged.
- Frame scene composition (`-logspotholo`): `Spot_Pulse_*` x2 (UVA pulse) + ring Inside/Outside + `Arrow_*`; the flat arrow's FACE is its own yaw (fixed-yaw sweep: face-on at the camera bearing, edge-on 90 deg off).
- The spots.bin entrance pair explains session 31's oddity: ECD_MERC_7416's BUILDING point sits under the dealer's overhang; its entrance is 52 m away on the forecourt.
- Full notes: `docs/re/spot_holograms_spec.md` section 2.

### Verified
- Ibiza Ferrari dealer (`-atspot ECD_FERR_3337I -weather 18`, reverse 3 s, shoot): plate + frame over the road ahead, arrow leaning at the dealer (`docs/images/spot_hologram_ferrari_day.webp`); night: plate glows white-blue (`spot_hologram_night.webp`).
- Hawai Mercedes dealer (`-atspot ECD_MERC_7416 -island hawai`): same composition, entrance 52 m from the building, "Hawaii Area 3" (`spot_hologram_hawai_dealer.webp`). Log: `1 spot holograms in 300 m (nearest 'CD_MERC' at 52 m)`.
- Hawai event start (`-tp 14781,31560 -island hawai`): the Event hologram 118 m ahead (log; the shot has the usual `-tp` streaming float).
- All 8 AddOn frames render whole (`-spotholotest 56 -spotholocount 8`): vivid rings + pointers.
- Birds/aircraft per-island [DATA]: `islands/<isl>/scenarics/packs/aircrafts.bnk` + `world_aircrafts.bnk` + `fx/details/birds.bnk` exist on BOTH islands; logs: Hawai 4 routes / 3 aircraft types / 11 planes / 5 species; Ibiza 4 routes / 2 types / 11 planes / 5 species. The islands do NOT share them. Showcase `docs/images/aircraft_hawai_airlines.webp`.
- Path flock: `Birds: flock0 at ... 401 m from the camera ... (path flock, crossing ahead)`.
- Builds clean, runs 0 stalls (watchdog lines).

### Ruled out
- The Type-condition offsets (word1/word3) as data pointers: the bytes there are scene/table data unrelated to the condition (counts read as floats/garbage) - they are part of the VIRTUAL encoding, not a link.
- Node hierarchy loss: `EdenModel` draws each node with its authored `World`; both cones in a frame scene are LOD0 authored geometry, not a double draw.
- The arrow's yaw = camera-heading +/- 90: renders edge-on (logged heading -1.34 vs face-on at -77 deg) - the face is the yaw itself.

### Not done / next
- The `if` side of the Type condition (which would replace `HologramIconFor`'s name prefixes with the data rule) and the per-record word: needs the dev exe's SCData VIRTUAL reader (edXMB `CLASS_VIRTUAL`) decoded - start at the `VIRTUAL` string 0x15850fc neighborhood and the class-name switch.
- The arrow lean at other approach sides / for circles-rhombi unverified by eye (one capture only); retail's exact lean angle (mine is 0.61 rad [INF]).
- The plates' night glow strength vs retail: the port's night plate reads bright (the UVA pulse layers tick); an A/B against a retail night capture would settle it.
- `SignificanceLevel` (+56) and `Shape` (+160) per record: not applied (draw distance tier?).
- Birds: visibility in natural play is the owner's look; bird/plane SOUNDS (ambience banks have candidates) unexamined.

## Session 2026-11-15 (`main`, 32): Hawai buy + other shops verified, zone badge fallback fixed, `-atspot`

- Buy: `-gm SHOP -shopspot ECD_MERC_7416 -island hawai -shopaim 3 -money 500000` with the Audi key recipe (`docs/state/main.md` buy test): FSCommands eval / buyc, "bought Mercedes-Benz SLS AMG (car 401) for 244500", profile saved; next FREERIDE "driving the profile's car 401 'SLS_AMG', rims mercedes/sls_amg_f_02 (ordered at the dealer)". (A blind recipe at the wrong `-shopaim` buys nothing; the shop view shows no menu without the presses - same on Ibiza.)
- New hook `-atspot <spots.bin name>` (`gm_freeride.cpp`, via `FindSpotEntrance`): ESTICK_B_8570 (stickers, the retail sticker shop), ETUN_A_3136 (tuning bay), ESTOL_B_2265 (used dealer, NPC scene), EAS_A_1555 (clothes, hostess) all enter with validate on Hawai (`_hs_sheet.png`).
- Bug fixed: `FreeRideHud` zone fallback printed the hardcoded "Ibiza Area 1" while the car was outside every cruising-zone polygon (Hawai clothes shop 10736,35032). `CruisingZones::findNearest` picks the nearest polygon edge; shows "Hawaii Area 2". The badge still keeps the last zone when leaving polygons (retail rule untouched).
- Not verified: other shop types on Hawai (surgery, club, photo, realtor, car wash walk-in), buying clothes / stickers, an on-screen overtake. Oddity unexamined: the car at 44452,29811 sits under the dealer's overhang.

## Session 2026-11-15 (`main`, 31): Hawai dealer entry from FREERIDE verified; Hawai vegetation looks fine

- Verified: `-gm FREERIDE -island hawai -tp 44452,29811`, `press=validate` at frame 260 -> SHOP "Mercedes-Benz (6564), 6 cars ... free-ride visit, loop C_LIFE_HOSTESS", showroom with the hostess drawn. Hawai HUD badge "Hawaii Area 3" there. Oddity: the parked car at that tp sits under the dealer's overhanging first floor with a floating look - not investigated.
- Vegetation: daylight (`-weather 18`) shots at (7038,16850), (39380,20085), (31672,4416) show dense palms / forest / parkland trees and hedges. No change made; the earlier 7.7 % vs 12.2 % green figure was a different viewpoint.
- Not done: buying at a Hawai dealer, clothes / tuning / sticker shops on Hawai, traffic zones / signals, an on-screen overtake.

## Session 2026-11-15 (`main`, 30): Hawai dealers failed to load - ReadSpot read Ibiza's spots.bin only

- Bug: `IntroCinematic::ReadSpot` (`avatarchoice/intro_cinematic.cpp`) opened `islands/ibiza/level/spots.bin` always; every Hawai dealer / shop spot warned "not in hawai's spots.bin" and fell back to defaults (callers: gm_shop x2, gm_testcar, gm_cine). Now searches ibiza, hawai, casino (ids are hashes of unique names).
- Verified: `-gm SHOP -shopspot ECD_MERC_7416 -island hawai` and `ECD_US1_5005`: "SHOP: spot ... type 0x1 at (44452.94, 12.99, 29811.37)" / (31672, 4416), showrooms render (sheet `_sh2_sheet.png`, scratch). NOT verified: the real FREERIDE enter press at a Hawai dealer, buying, shops that are not dealers.
- Overtake chase: the three early overtakes are on Ibiza ~150 m from the start, out of view; a deterministic on-screen shot was not obtained (log now prints the position). Still numeric evidence only.
- Next: PROGRESS row "Hawai: traffic behaviour + dealers"; vegetation survey (Hawai 7.7 % green vs 12.2 % reference), other Hawai shops.

## Session 2026-11-15 (`main`, 29): traffic front-wheel steer verified; traffic overtakes slow cars (owner: "both")

- Steer verified: `-trafficpark -trafficparkface 1 -trafficsteer 25` vs `0` - the vans' front wheels visibly turn (crop in `_sp_crop.png`, scratch). Sign follows the rivals' law, not checked against a real turn.
- New: overtaking (`traffic.cpp`, `traffic.h` Agent::LaneShift / bOvertaking; spec "Lane changes / overtaking"). Hawai 6000-frame run: 48 overtakes, 0 contacts, 0 off-road removals, 0 VUIDs. `-notrafficovertake` = off.
- NOT verified: an overtake on screen; behaviour on dual carriageways / one-way roads (the oncoming lane may not exist - watch `off road` removals when driving other areas); 48 in 6000 frames may be too many. Next: a free camera on an overtaking car (log the agent index, chase it), then tune the trigger; then the real zone lookup / signals.
- Still not done: signal models, zones, Hawai shops / dealers / vegetation survey.

## Session 2026-11-15 (`main`, 28): Hawai traffic survey + front wheels steer (owner: "Hawaii traffic needs work ... pretty much everything")

- Survey (build 179, `-travelisland hawai`, night, 1500 frames): Hawai traffic spawns and drives (29 cars, red-light stops, 0 VUIDs, ~8 ms/frame), vegetation / buildings render. `-island hawai -gm FREERIDE` alone spawns at Ibiza coordinates (grey plane, "Ibiza Area 1", 0 traffic): use `-travelisland hawai` or F4. Gaps still open per `docs/re/traffic_spec.md` "Not done": lane changes / overtaking, the real zone lookup, signal models, front-wheel steer.
- Changed: `traffic.cpp` draw - the front wheel nodes (WLR_FL/FR) steer by atan(2.7 * yawRate / speed), same law as the rivals; `-notrafficsteer` = old.
- Verified: builds, 0 VUIDs. NOT verified visually: no traffic car was in frame in the A/B (`_st_ab.png`, 146 px differ). Next: park a car mid-turn (`-trafficpark`) or tp to a junction and compare with `-notrafficsteer`; then lane changes.
- Shops / dealers / vegetation on Hawai not surveyed.

## Session 2026-11-15 (`main`, 27): events run to their real destination, with the real time limit and rule title (committed, not pushed)

- `RaceEvents::start` loads the event's `.ig2` (gate 0 = start, others = real destination), `CtDw` = time limit (expiry -> "Time is up", no reward, `takeFinished` now reports failure), title = rule shown as the goal. Spec section 13. `-oldraces` restores the random walk.
- Verified: `-racestart N` logs for 6 events (2.5-6.4 km straight, limits 230-240 s); `-tp` + OK at `EVIA1SP1` starts with its 4 gates. NOT verified: finishing a whole event, the HUD route line to the real finish, Hawai.
- Not done: per-kind rules (collisions, tailing, speed traps), `VMod` cars.

## Session 2026-11-15 (`main`, 26): hover card for the event pins (committed, NOT pushed: the owner asked to push once at the end of the dig)

- `SpotCardKind` knows `event`; `fillSpotCard` sets `WhatSpotType = event`, the name (`dbna` loc), `/:GoldReward`, `/:nb_events(_done)`. Verified by `-mapcardicon event` (screenshot `docs/images/map_event_card.png`). Type row empty (source undecoded); done count is always 0.

## Session 2026-11-15 (`main`, 25): challenge pins on the world map's first tab

- Tab 0 now shows an `event` pin per real event start (39 Ibiza / 60 Hawai), discovered by proximity like the buildings; `Spot::bChallenge` keeps them out of enter prompts, HUD building markers and the news badge. `RaceEvents::GetRealStarts` is the shared list. `-nochallengepins` removes them. The icon's bar shows the event's `CtDw` time limit as `MM SS` [INF]. `-dumpmapspot` lists the text fields too.
- Verified: screenshot of the Ibiza map (all discovered) `docs/images/map_challenge_pins_ibiza.png`; a fresh profile driven to `EVIA1SP1`: `discovered 'EVIA1SP1' (event)`, saved, one pin built. NOT verified: Hawai, the pins' hover (no card), clicking OK on one.
- Next: a card for the pins (title = `dbna` loc), per-kind icon frames (the movie has `speed`, `timeattack`, `race`, ...), events from their own `.ig2` gates.

## Session 2026-11-15 (`main`, 24): spotworld.bin decoded - real positions for all 532 challenges; free-ride races use them

- `scenarics/spotworld.bin` = the world index of the `.ig2` challenge files with their start positions (spec section 12). New `source/game/spots/spot_world.{h,cpp}` + `tools/dump_spotworld.py`. Validation: 25 / 25 championship first gates equal the record's position (0 m); 40 / 40 DBEvent rows found.
- `RaceEvents::load` places the instant-challenge starts at the real EV* event positions (39 Ibiza / 60 Hawai) instead of 8 invented ones; `-oldraces` restores the old behaviour. Verified by log on both islands and a started race at `EVIA1SP1`. The old header claim ("event positions unknown, files [ENC]") was false and is fixed.
- NOT done / next: map tab-0 pins from the table; running events from their own `.ig2` gates with per-kind rules (drag, speed trap, time attack ...); the minimap / HUD now shows 39 / 60 race markers instead of 8 (owner should look at this; `-oldraces` is the escape hatch).

## Session 2026-11-15 (`main`, 23): unowned houses draw `forsale`, the seaplane dock draws `casino`

- Retail MAP.vmf's icon clip has a frame for each dev kind string (spec section 11). `_dlc` frames exist only for challenge icons. `map_screen.cpp` buildSpots now draws `forsale` for unowned EPH houses and `casino` for `ELANDCA*` (display frame only; `spot.pIcon` unchanged); `-oldspoticons` restores the old art, `-mapicon <label>` is a test hook.
- Verified: before / after Ibiza map screenshot on the avatar-spots tab (4 176 px differ), frame art sheet in `docs/images/map_icon_frames_house_casino.png`. NOT verified: Hawai, the card text for a for-sale house, and the realtor tile.
- Photo-studio weather / daypart rule: the helper at `0x96ebd0` is challenge status, not weather; nothing applied (the port has no photo-studio gate).

## Session 2026-11-15 (`main`, 22): entering a spot marks it seen; lock texts identified

- `MapScreen::markSpotSeenById` + one call in `gm_freeride.cpp`'s enter press (dev base enter method `0xa7ec80`). Verified with `-tp 27560,8303` at the Ferrari dealer (named scratch profile): `seen (1 in the list)`, `seenspots_ibiza` saved.
- Spec section 10: the lock reasons (loc 6855 no vehicle of the category, 7308 surgery timer, neighbours 6854 / 6856) and which categories lock. Nothing applied: the port has no level / category gate on spots, so there is no lock to show.
- Open: photo-studio weather / daypart rule, level thresholds, `rank` / `done` / `objective` for challenge spots, the `_dlc` frame.

## Session 2026-11-15 (`main`, 21): "new" badge on unseen map icons (dev build seen list)

- `PlayerProfile::SeenSpots` (+ save / load `seenspots_<island>`), `MapScreen::isSpotTracked / isSpotSeen / markSpotSeen`: OK on a spot's pop-up marks it seen (450 cap, houses / home never tracked); `updateSpots` drives the icon's `/spot/news` child for discovered, unseen, tracked spots. Also the pop-up bookmark button now honours the 32 cap (it was still 20).
- Verified headless with a named scratch profile: log `seen (1 in the list)`, badge clears for that id only, `seenspots_ibiza` saved and reloaded; screenshot of the orange "!" badge in `docs/images/map_icon_news_badge.png`. NOT verified: Hawai, other tabs, entering a shop (not hooked), clubs as one flag.
- Behaviour change to know about: every discovered shop icon now shows the badge until it is selected once, so `-cheat allspots` shows badges everywhere.

## Session 2026-11-15 (`main`, 20): dev build KnownSpots decoded - it is the "seen" list, not a discovery gate

- Spec `docs/re/map_spots_spec.md` section 9: the class layout (450 ids + count + club flag), `add` / `isKnown` / `isTrackable`, and the only two live adders (`0xb0d3a0` map select, `0xa7ec80` mode enter). `isKnown` is read only by the icon updater, where `!isKnown` = the `news` bubble. No proximity test exists in this code.
- Code unchanged. The port's proximity-discovery rule is the owner's description of retail and stays; the dev build cannot confirm or refute it (3 months older, or retail hides spots another way).
- Next, if wanted: a separate "visited" list in the profile so the `news` badge can be driven (selected / entered clears it), and the 450 cap.

## Session 2026-11-15 (`main`, 19): map icons show favourite / target state (dev build `0xb19f20`)

- `map_screen.cpp` / `.h`: `updateSpots` drives each visible icon's `/spot/favorite` (bookmarked spot) and `/spot/target` (GPS target within 60 m) children through `gotoLabel`, cached in `spotStatus`; new flag `-dumpmapspot` lists the icon's status children and labels.
- Verified: scratch profile with one bookmark -> log `icon status 'EPH_L3DI': favorite=1` and a green badge on that icon only (pixel diff vs a run without it, `docs/images/map_icon_favorite_badge.png`). NOT verified: the target ring (no headless GPS-target hook), nor on Hawai.
- Not applied: `news` (needs a visited flag), `_dlc` frame (not checked in MAP.vmf), `rank` / `done` for challenges.

## Session 2026-11-15 (`main`, 18): map favourites cap 20 -> 32 from the dev build

- `source/game/map_screen.cpp`: `kMaxBookmarks = 32` (the dev popup code `0xb1dc60` tests the favourite count `< 0x20`); the "Bookmarks limit" toast and `/:CanAddMySpot` both use it. Spec: `docs/re/map_spots_spec.md` section 7.
- Verified: builds; `-mapcardicon cardealer` opens the card path (`card locks for 'ECD_ITAL'`) with no errors. NOT verified: the cap boundary itself (needs 32 bookmarks added; no hook), no screenshot read by eye.
- Not applied: `IsNewSpot` (race-school progress), `IsChaLocked`, `SpotLockedText`, weather / daypart photo locks.

## Session 2026-11-15 (`main`, 17): dev build read for the map-marker code - category table decoded, SpotWorld.bin identified

Owner: "from dev build is it possible to track down how or where the markers are placed for shops, dealerships etc?"

- Static read of `TestDrive2DEV.exe` (no run). `0x970030` is the spot category -> kind-name switch (`[spot+0x79]`, 0..0x19); it matches the port's `spots.bin` table and names the rest. `0xca7350` maps the movie's `/:WhatSpot` to the pin title. Full table and addresses: `docs/re/map_spots_spec.md` section 5.
- `spotworld.bin` is the 532-record **challenge** table (not shops); the writer is `0x895890`. `Level/%s/spots` readers are cutscene loaders (ruled out).
- Not changed in code. Followed up the same day: loader `0x96b050`, the 0x110 record layout, the +0x101 sub-kind (ENC/EDI/EUC = 9 -> no kind in the dev code, so the port's `club` for `ENC_*` is unsupported), the 450-entry KnownSpots cap and the per-icon status calls (section 8; the 51/122 clip pool is the challenge editor's checkpoints, NOT the map pins - corrected) are in map_spots_spec.md sections 5-6. Open: the discovery distance test and the KnownSpots writer. Nothing run, no screenshots.

## Session 2026-11-15 (`main`, 16): volumetric clouds fixed - no more painting over the cockpit, visible at the horizon

Owner (screenshot): clouds showed through the cockpit roof / bonnet and the car interior, barely visible in the chase view unless aimed up.

- **Cause 1**: the pass ran after the haze and found sky pixels through the gViewPos buffer, which is built from the Z pre-pass - the player's car is not in it, so the cockpit read as sky. **Fix**: `drawVolumetricClouds` now runs right after the sky dome in the opaque pass (where the old billboard clouds drew), with no sky mask; the world, the car and its interior draw over it with their depth like any geometry. The `uViewPos` sampler is kept but unused.
- **Cause 2**: slab at 1.7-2.9 km and a fade starting at 6 km meant a ray 10 degrees up was already faded. Now 1.0-2.1 km (Cloud_R 0.7-1.8 km), fade 16..60 km, horizon cut-off at 0.004. Chase view shows clouds overhead and toward the horizon (World Viewer shot at pitch 8 shows banks near the skyline).
- Verified: freeride chase + `-cameramode 23` cockpit at `-weather 12`: no cloud on the roof / dash / buildings. Not verified in the owner's exact bonnet / roof-down case or at night / dusk.

## Session 2026-11-15 (`main`, 15): soft-ground drag (sand / mud / wet grass slow the car)

Owner: "all please" (sand shading, car effects, ocean / rivers).

- **Done**: `GMFreeRide::tickSurfaceFx` now bleeds horizontal speed exponentially by the share of wheels on soft ground, by `ClassifyGround` material: sand 0.16/s (x1.35 wet), dirt 0.03 + 0.22 x (dirt wetness - 0.3) (mud drags most), grass 0.035 + 0.08 x wetness; tarmac none. [INF: the retail tyre sets have no sand / mud rows.] `-nosoftdrag` = off. Verified headless: farm field, full throttle, frame 420: 55 km/h with it vs 61.7 without (the first, stronger rates cost 37% of the distance and left the car stuck, so they were halved).
- **NOT done**: sand shading (dunes / ripples / wet-sand sheen), sand thrown onto the body, the ocean and rivers. The ocean (`eden_ocean.cpp`) already has reflection, drift, clarity and tide; improving it needs eyes on the water and a shader-side change (whitecaps / foam, depth tint) - I could not find a convenient viewpoint of open sea in the headless World Viewer (my Hawai test spot looked at the beach / cave rock), so I did not change it blind. Next step: pick a coastal `-tp` spot (use `-logambience` to find a shore) and A/B `-waterclear`, `-wavespeed` first.

## Session 2026-11-15 (`main`, 14): wet terrain (rain darkens dirt / sand / mud ground)

Owner: "sand and mud" (nicer, interacting with the cars).

- **What**: `surface_effects_spec.md` 5.1 said the terrain shader never sees the wetness. `EdenWorld::terrainWet` (rain_ground_global_intensity, soaks over ~3 s, dries over ~15 s) now scales the heightfield's own light constants per draw (`__DirLight_Diffuse_0` x (1 - 0.45 wet), `__AmbLight_Ambient_0` x (1 - 0.30 wet)) through `EdenDrawParams` - TD_HeightMap honours those overrides. Island terrain, areas and sectors all go through `BuildWetTerrainParams` (eden_world.cpp). `-nowetterrain` = off.
- **Verified**: `-weather 12 -intempery 3 -tpin 19584,24960 -noblades`, 2100 frames: frame luma 59.8 -> 55.3, dirt track and grass visibly darker (`docs/images/wet_terrain_ab.png`). It darkens everything on the terrain sheet equally (no gloss, no per-material difference between sand, dirt and grass).
- **Already there (earlier sessions, unchanged)**: wheel spray / dust particles by ground material, dirt vs mud (wet dirt) splash, tyre-track decals with a mud tread, puddles, mud on the car.
- **Not done**: gloss / sheen on wet ground (needs a spec term the terrain shader lacks), sand-specific shading (dunes, ripples), tracks that deepen with speed or sink the car, mud that slows the car (physics), sand throw onto the car body, and the beach test is still a height heuristic (`ClassifyGround` in gm_freeride.cpp). Water / ocean / rivers: not started.

## Session 2026-11-15 (`main`, 13): grass blades with car interaction

Owner: nicer vegetation, "splines of grass", little blades in certain areas, the car interacting; also water / sand / mud (NOT started, see below).

- **What**: `source/render/eden/eden_blade_grass.{h,cpp}` + `resources/shaders/bladegrass.{vert,frag}` (recompile: `tools/compile_shaders.py bladegrass.vert bladegrass.frag`). `EdenWorld::buildGrassBatch` now also scatters real blades (7-vertex tapered ribbons, 3 curvature rows) in the cell of every grass card within 24 m of the camera (8 per card, <= 24000 blades), tint by the card's atlas class, height from the GrassType. Drawn in the Effects pass (GMFreeRide draw callback). The vertex shader bends each blade: its own lean, two travelling wind waves (amplitude from `wind_continuous_grass_amplitude`), and the car - an oriented-box test on the car footprint lays blades flat under the car and pushes the ones beside it away (`setGrassCar`). Placement reuses the .pgr / road-skip tests, so blades only appear where cards do. Flags: `-noblades`, `-bladedist m` (24), `-bladecount n` (8).
- **Verified**: `-tpin 19584,24960 -weather 12` farm spot: blades vs `-noblades` (`docs/images/grass_blades_ab.png`); driving through it shows the tyre tracks pressed flat behind the car. Cost in that field: draw 14.1 ms -> 15.9 ms (14 blades / 30 m was 21 ms, too dear). No validation errors seen in the log.
- **Not done / next**: (1) the lingering trail is only the instant footprint - a persistent flatten map (springing back over seconds) would be nicer; (2) blades are unshadowed, flat-lit (ambient + 0.36 sun) with a root-to-tip gradient; (3) blades are rebuilt every frame on the CPU (rebuild-per-frame is the existing card cache rule) - a cheaper static emit + GPU culling is the perf lever; (4) wheels do not part the grass separately from the box; (5) **water / sea / rivers, sand and mud improvements are not started** - suggested order: sand (dune shading + wheel track decals already exist in `eden_surface_fx`), mud (wet dirt darkening), then ocean (`eden_ocean.cpp`, `docs/re/ocean_spec.md`).

## Session 2026-11-15 (`main`, 12): collision sounds, thunder + lightning, volumetric clouds

Owner: "volumetric clouds and maybe thunder and lightning ... more sound effects ... collisions ... I don't think have any sound effects. Mostly car engines, horns, dialog the rest seems missing."

### What changed
- **Collision sounds**: `source/audio/collision_audio.{h,cpp}` loads the 107 `*_PHYSIC_SFX.wav` of `Euro/Bnk/fx/physicsfxsetup.bnk` [DATA] (carmetal_low/high, carground, carcar, carmetal_loop ...), plays them on bfx client 21 (PhysicSfx, spatial). `VehicleDynamics` now queues `HitEvent`s (every chassis contact closing > 1.2 m/s: point, speed, slide, floor/wall) and a max wall slide speed (`takeHitEvents`, `takeScrapeSpeed`); GMFreeRide feeds them plus the traffic / opponent contacts (`hitCar`). Wall hit = carmetal (low < 6 m/s, high above), floor = carground, car = carcar, rubbing a wall = carmetal_loop01 with gain / pitch from the slide speed. Which sample the retail game plays per material is `[ENC]`/entities.xmb and not used [INF]. Flags `-nocollisionaudio`, `-logcollisionaudio`.
- **Thunder + lightning**: `game/freeride/lightning_storm.{h,cpp}`; the storm intemperies carry `thunder_off_on`, `thunder_time_spacing_min_max`, `thunder_lightning_duration_min_max`, `thunder_flicker_*` [DATA] and nothing used them. A strike flashes (the preset copy `stormWeather` with ambient / fog / sun / Skydome_sunIntensity lifted, so terrain, sky, clouds flash together) and the island's `eclair_high/mid/low` wavs play after distance / 343 m/s on client 17. Units of the retail keys are not decoded, so durations / intensities are clamped to a believable flash [INF]. `-lightning` forces a storm on any sky, `-nolightning` / `-nothunder`, `-logstorm`.
- **Volumetric clouds**: `resources/shaders/volclouds.frag` (+ generated `volclouds_frag.h`; recompile with `tools/compile_shaders.py volclouds.frag`, needs the Vulkan SDK `glslangValidator` on PATH). Fullscreen premultiplied-alpha pass in `EdenSceneView::drawVolumetricClouds`, right after the haze; only pixels the gViewPos buffer calls sky; 36-step march through a 1.1-1.2 km slab, fbm value noise, 4-tap sun march, HG phase. Cloud_A / B / R of `clouds_cloud3dname` pick coverage and darkness [INF]. Replaces the billboard clouds (`-oldclouds` brings them back, `-novolclouds` = neither, `-cloudcover 0..1`).

### Verified
- `-lightning -logstorm -logcollisionaudio`: `CollisionAudio: metal 6/6, ground 6/6, car 4/4, scrape loops 4`, `LightningStorm: 9 thunder clips`, strikes with thunder `eclair_*` at the computed delay, `CollisionAudio: 'carmetal_high03_PHYSIC_SFX'` on a real wall hit.
- Lightning at night (`-weather 00`): frame luma 47.3 -> 67.1 on the flash frame, then back (blue-white lift visible).
- Clouds: World Viewer `-island ibiza -campos 35700,80,5600 -campitch 25 -weather 12` shows lit cumulus with grey bases; `-novolclouds` shows a clear sky. Freeride perf unchanged at 120 fps.

- **Landings (follow-up)**: `CollisionAudio::land` - when all wheels leave the ground > 0.18 s and touch down at > 2 m/s vertical, `carsuspension01..07` plays, plus carground (dirt) or carleaves (grass) layered by the wheel surface. Wired next to the horn in `gm_freeride.cpp`. Built and a flat drive runs clean; NO jump was driven, so no landing has been heard or logged yet (`-logcollisionaudio` prints it). Still not wired: wood / fence / stone classes (need a contact material id), glass, dialog.

### Not verified / next
- **Nobody has listened**: levels of the collision / thunder sounds are by formula (hit volume 0.35 + v/16, thunder by distance). If hits are too loud or quiet, `collision_audio.cpp` `hitWall/hitFloor/hitCar`.
- Clouds: not in the environment cube / mirrors; coverage per Cloud_A/B/R is a guess; edges show a faint dither (no temporal accumulation); cloud height 1.1-2.9 km fixed. The old `-intempery 3` storm sky shows overcast only in the drive view - look at it from the World Viewer.
- The rest of the audio gap: skid / tyre squeal exists (VehicleAudio Skid layers) but wood / fence / stone / leaves impact classes, glass breaking, suspension landing (`carsuspension01..07`) and pedestrian / dialog ambience are not wired. Next: surface class of the contact (needs the Havok material id) and a landing one-shot from the wheel contact.

### Update (accessories icons, owner's screenshot at spawn, build 146)
- The wrong icons are **GENERAL.vmf's emote-wheel art** [DATA, `-dumpflashart`]: up = cell 684 (hand + sound waves), down = 678 (waving person), left / right = 2698 / 2695 (the "L / R" eye glyphs); the HUD's own cells for the slots are 1003 (roof), 1005 (windows), 1001 (car + arrow). The title "Accessories", the arrows and the layout are the HUD's `ig_menu`, and the owner's wheel shows NO red lock on the roof slot (the retail-correct wheel for this hardtop Porsche shows one) - so it is not the same clip content.
- Re-checked and NOT reproduced: `-gm FREERIDE` with / without `-skipcine`, the LOGIN -> FREERIDE continue path, the owner's options.ini (4K, button_prompts 3), mods, profile, a connected DualSense (no `-nopad`), DLSS on. All show roof(locked) / windows / indicators.
- Open discriminators to ask the owner: numpad 4 (keyboard) vs the pad's D-pad left; before / after the "Enjoy!" popup closes.






### Update (owner tested the gate, "works pretty well"): the cave stays open once the club car is collected
- `updateCaveGate`: when the profile owns car 436 (`OwnedCars`, saved with the garage) the gate opens and never closes again - the rock collision stays off for good, so anyone who earned the secret can drive in whenever they like. `-cavegatealways` keeps the 88 mph rule even for owners. Verified: with the owner's profile (436 owned) the run logs `the cave stays open (the club car was collected)` at once. A new player still needs the 88 mph run; collecting the car (it is added to `OwnedCars` in `updateCaveCar`) opens it the same frame.

## Session 2026-11-15 (`main`, 11): the Hawai cave is an 88 mph gate (Back to the Future)

Owner's idea: the cave's rock only lets the car in if it hits it at 88 mph.

- `gm_freeride.cpp` `updateCaveGate`: on Hawai the cave rock (`VehicleWorldCollision::CaveWalls()`, static triangles ignored inside the cave box 39150..39550 x 20100..20350) is now SOLID by default. Reaching the cave area (39205..39525 x 20140..20330, the owner's photo-shot extent plus a margin) at >= 88 mph (141.6 km/h) opens it: white flash (`caveGateFlash`, drawn like the photo flash), hint "88 mph! The rock lets you through...", log `the cave gate opened`. It stays open while the car is inside the box (+40 m margin) and closes again once it leaves (`the cave gate closed`). A car that starts / loads inside the box finds it open (a save made in the cave). `-nocavegate` = the old always-open easter egg; `-nocavewalls` / `-cavewalls` / the F4 option still force closed / open; `-cavegatespeed km/h` (default 142) and `-logcavegate` (position / speed / state each second) for tests. The per-frame block that used to set CaveWalls on Hawai no longer touches the gate state.
- Verified (state machine only): `-island hawai -tp 39380,20085 -heading 180 ...` logs closed -> `opened at 142 km/h (39344, 20149)` -> open through the cave -> `closed` after leaving the box; with `-cavegatespeed 999` it stays closed. heading 0 faces -z, 180 faces +z.
- NOT verified: that the rock really stops a car below 88 mph. The headless runs reach 300+ km/h (or the sim flips the car) so they tunnel the rock; the owner's earlier note says the car stopped at x ~39272 with walls on. Also unknown where exactly the "wall" is (the 88 mph test counts anywhere in the cave area, not at one wall face). Next: drive it by hand, then tighten the trigger to the real wall.

## Session 2026-11-15 (`main`, 10): windscreen visible in the inside views, sharper / fresher mirror reflections

Owner (with a 360-recomp reference shot): windshields should be visible inside, tinted, with a dirt arch at the top where the wipers do not reach; enhance mirror reflections.

- `cinematic_car.cpp` draw: the windscreen (GLASS_F) is no longer hidden in the inside views; it takes the retail inside glass (Glass_I, TD_CarGlassDirt, the same swap the side windows use [DATA car_glass_spec.md]), which carries the retail tint and the dirt toward the top. `-hidewindscreen` = old. Verified on the owner's data (`-cameramode 23 -clock 14.00`): tinted glass, darker toward the top. NOT compared to the 360 arch shape pixel for pixel - if the arch is missing, the dirt art is Glass_I's own DETAIL2 and the wipers' reach would have to be baked in.
- `eden_scene_view.cpp`: the car environment cube (what the mirrors, paint and glass sample) is 512 per face (was 256; `-envsize n`; +0.6 ms offscreen measured) and, while moving, the one or two faces most behind the view are re-rendered about every 70 ms (`renderEnvironment(..., faceMask)`; the full 6-face refresh stays at 5 Hz) so the mirror image does not trail the car. `-norearfaces` = off.
- A tunnel night shot (cockpit) shows the mirror with the road and a car behind. Not done: a true mirror camera (the retail renders the mirror from a rear camera; the port still samples a cube taken at the car centre, so there is parallax and the mirror's aim can read too far down on a parked car).

## Session 2026-11-15 (`main`, 9): side windows in the inside views, lamp-based headlights, tunnel traffic

### What changed
- **Side windows (cockpit / bonnet)**: `cinematic_car.cpp` draw: the inside views hid every glass node (GLASS_F, WND_L1, WND_R1); now only the windscreen is hidden, the side windows stay and take the inside Glass_I material (a light tint with a reflection - closed glass shows a faint lighter patch, wound down it is clear). `-hidesidewindows` = old. Verified with `-cameramode 23 -accdown` and `look=-1,0` on the owner's data: closed glass visible, open clear, `-hidesidewindows` shows none.
- **Headlights**: the ground pool was ONE 13 x 9 m quad centred 9.5 m ahead of the car's ORIGIN (2 m ahead of the origin + 7.5), whatever the car's length -> it floated off short / long cars. New `EdenAppendHeadBeams` (eden_headlights.cpp): a pool per lamp starting at the real lens ('L' glow anchors: `CinematicCar::getHeadlampPositions`, traffic `type.LampGlows`) plus a long faint spill; traffic brings 3 beams per car (`maxCones` now counts cars). `-oldheadlights` = old. Cars with no 'L' anchors keep the old cone. [INF: pool sizes / intensities, by eye]
- **Traffic in the tunnel**: `traffic.cpp`: the ground ray started 6 m above the car's last height and hit the tunnel roof / hill (a bridge deck overhead likewise), so cars rose onto the hill and "drove through" it. Now 1.6 m above (3 m fallback; spawn from player.y + 2.5). `-oldtrafficground` = old; `-logtrafficy` logs the worst height gap to the player per second. Verified at the tunnel (`-tp 35942,5866 -heading 12.5`, 2400 frames): worst gap 7.9..11.6 m old, 0.1..0.3 m new.

### Not done / next
- Headlights were judged on one night frame only; the pool shapes want a drive at night (traffic and own car). The headlamp lens glow quads are unchanged.

## Session 2026-11-15 (`main`, 8): accessories icons FIXED - the owner's data is a newer HUD.vmf with a bike icon set

- **Cause** [DATA]: the owner plays from `C:\Users\cuz_c\Pictures\TDU 2 Unpacked` (launcher.ini of `E:\opentdu2-windows`), whose `hud.bnk` (4,132,864 bytes) is NOT the `C:\TDU2-Gamedata` one (4,046,848) I had been testing with. Its `ig_menu` `SetIcons` tests `_root.IsBike == False`: with the variable never set the comparison fails and the wheel takes the BIKE branch (labels `taun` horn, `clbr`, `hell` wave, `clbl` = the horn hand, wave person and L / R eye glyphs that look like emotes). The valid_icon popup uses the car labels, so it showed the right icon.
- **Fix**: `FreeRideHud::showCarMenu(..., bIsBike)` sets `/:IsBike` (false for cars, true with `-bike`) before opening. Verified with `-game "C:\Users\cuz_c\Pictures\TDU 2 Unpacked" -accleft`: roof (locked on the hardtop) / windows / indicators.
- Lesson: test against the owner's launcher.ini `game_folder`; two data sets exist on this machine. Other variables the newer HUD / GENERAL read may also be unset (compare the `ConstantPool`s of both bnk versions).
- The bike wheel's up / down entries (horn, wave) have no actions in the port yet.

## Session 2026-11-15 (`main`, 7): drops run down the glass; the accessories icons NOT reproduced

### What changed
- `cinematic_car.cpp` `updateGlassDrops`: drops now run on their own. Down = the world's down in the windscreen uv (the fit's y gradient). Big drops (and ~12 % of the small ones) start to run after 3..13 s (6..26 s), accelerate to 0.16 uv/s, wobble, leave a trail, shrink and die when spent / off the glass; the overlay re-renders at 10 Hz while any drop runs. [INF all constants]
- Verified only by build + a 2000-frame rain run (frames differ); the trails are too small to judge at 1280 x 720 - look at the windscreen in a chase / front view.

### The D-pad-left accessories icons (owner: "they look like emotes: horn hand, wave person")
- **Not reproduced.** `-accleft` in FREERIDE shows roof / windows / indicators correctly at 1280 x 720 and at 3840 x 2160, with the owner's `options.ini` (button_prompts 3, hud_quality 2), the owner's mods (`fuel-system`, `enhanced-damage`, `original-physics`) and a saved profile. The movie script is deterministic [DATA hud.vmf, ig_menu frame 0 `SetIcons`: icon_1..4 `GotoFrame2` the labels hood / clir / wind / clil; icon cells are 46 x 46 atlas cells 1001 / 1003 / 1005 of `page_001`], and the retail art for those cells is the roof / window / car-with-arrow icons.
- Ruled out: pages (hud.bnk has a single page, `page_001`; page names are owner-scoped), mods (no `flash/` overrides), prompt platform, resolution, a pad (`-nopad` only; DualSense not tried).
- Suspect: a state that only a longer session reaches (a stale texture / draw-set after leaving walk mode or a shop; CLAUDE.md trap 13 - run `-vk_enable_validation_layer`), or the connected DualSense. Next: reproduce it, then `-dumphud` on `/HUD/ig_menu`.

## Session 2026-11-15 (`main`, 6): the wipers push the water down the glass (spatial wipe)

### What changed
- `eden_model.*`: `Item::PrimitiveUVs` (kept with the primitive data) so the windscreen's uv can be read.
- `cinematic_car.cpp` `collectGlassFit`: least-squares `uv = A.(|x|, y, z, 1)` over GLASS_F's vertices in car space. **[DATA]** the windscreen uv is mirrored left/right (x does not change it) and uses only a part of the DETAIL2 texture (0.44..0.99 x 0.33..0.99 on the California), so a blade on either side clears the mirrored side too.
- `updateGlassDrops`: a live drop sim (about 300 x wet, beads in over ~1 s). Each wiper blade's pivot->tip segment is mapped to uv; drops it crosses are dragged by the blade's own motion, thinned (a thin film is left), leave a short trail, and die when thin or pushed off the glass. The overlay is re-rendered (256^2 RGBA, new `rain_dyn_N` texture, old one `destroyTexture`d) about 30 Hz while wiping, 3 Hz otherwise. Cars with no blades / no fit keep the 12-level static overlay; `-nodynglass` forces it.
- **Fix (both overlays)**: the stock G_I overlay is a dark tint (alpha ~0.87) over the whole window; the first droplet overlays had none and turned the glass milky white (A/B screenshots, dry 56/50/50 vs 224). They now carry the tint (`FillGlassTint`) and blend the beads over it; beads are also much smaller (0.8..4 px) and fainter.

### Verified
- `-intempery 3 -wipers on -cameramode 20`, backview (front of the car): dry glass dark; wet glass with small beads; sweep frames show the glass clearing progressively. Validation layer: no VUIDs apart from the 6 objects leaked at exit (the cached overlay textures are never destroyed).

### Not done / next
- The blade-pushed water leaves no accumulated bead line at the blade edge, and drops do not run down on their own. Wiper uv mapping is a plane fit (a strongly curved windscreen may drift). Side windows get no sim (they only show the static-style overlay when it rains).

## Session 2026-11-15 (`main`, 5): the glass droplets fade smoothly

- `cinematic_car.cpp`: the droplet overlay is now 12 levels of the SAME 340 drops (fixed seed), each with its own wetness threshold that fades / swells it in over 0.2 of wetness (`MakeRainGlassTexture`), so a level step changes the glass only a little (was 3 levels, hard swap). The wiper no longer zeroes `glassWater`: it eases down 3/s while the blade is across the glass (about 0.3 s). The level line is DEBUG now. [INF all constants]
- Verified: `-intempery 3 -wipers on` log: levels 1,2,3,4 on the way up (about 0.08 water each) and 3,2,1,0 down per sweep. Not looked at in 3D. A true per-pixel crossfade would need a shader term; 12 steps at 60 fps with a slow rise reads as a fade [INF].

## Session 2026-11-15 (`main`, 4): the wipers now clear the outside glass drops (and actually sweep)

### What changed
- `cinematic_car.cpp` `updateWipers`: **bug fix** - a sweep started at `wiperTimer = -1e-6` and finished on the next frame, so the blades never moved (the owner's "wipers to work"); now `-1.0`. The outside-glass water (`glassWater`, new) beads up 0.08/s while it rains (capped by the body's water film), a sweep past 80 % of its angle sets it to 0, and the droplet overlay (`applyGlassWet`, now called from here every frame instead of from `applySurfaceState`) follows it.

### Verified
- `-intempery 3 -wipers on`, 1500 frames: the glass level cycles 1 -> 0 -> 1 -> 0 with each sweep. Not looked at in 3D (blades moving, overlay flicker).

### Not done / next
- Level steps are a hard swap (no fade); a smoother wipe would need a shader term. Sweep period is the existing auto interval (7 s drizzle .. 1.2 s downpour); F6 cycles off / auto / fast.

## Session 2026-11-15 (`main`, 3): rain on the car's glass from outside, cars get wetter over time

### What changed
- `cinematic_car.cpp` `applyGlassWet` (called from `applySurfaceState`): the exterior glass materials' DETAIL2 slot (the RGBA overlay `base = lerp(base, G_I.rgb, G_I.a)`, car_glass_spec.md 2 [SHADER]; the cracked glass swaps there too) gets a procedural droplet overlay (`MakeRainGlassTexture`, 3 densities, 256^2) as the water film passes 0.12 / 0.40 / 0.70. A cracked window keeps its crack texture. `-norainglass` disables.
- `car_dirt.cpp`: `kRainSoak` 0.35 -> 0.06 (about 17 s of rain to soak, was 3 s) so the car visibly gets wetter over time; the paint darkens up to 30 % (was 22 %) and the body reflection gain is 1 + 0.7 wet (was 0.45). [INF all rates]

### Verified
- `-intempery 3 -cameramode 20`, 1500 frames: log shows glass level 1 (water 0.12) -> 2 (0.40) -> 3 (0.70) on 5 glass slots; the wet glossy paint shows in the screenshot. The droplets themselves on the glass were NOT seen (the California's rear window is too small at chase distance): check a coupe from the chase view and the pause-less photo camera up close.

### Not done / next
- Drops are a static overlay (they do not run or evaporate individually); the three levels swap with the water film. The wipers do not clear the outside glass overlay (only the inside screen-space drops).
- The overlay UV is the glass's own: a windscreen with unusual UVs may show stretched beads.

## Session 2026-11-15 (`main`, 2): spec card off, photo-mode head, weather blend, lens droplets

Owner: remove the bottom-right car stats card (predates the dealership UI); headless driver in photo mode; blend weather / day-night better (rain starting slow); droplets and wipers in other views.

### What changed
- `freeride_hud.cpp` `showCarSpecCard`: no-op unless `-speccard` (traffic and showroom card both).
- `gm_freeride.cpp`: the driver's head draws while `photoCamera.isOpen()` (it was hidden for the cockpit mode 23 the photo cam inherits).
- `gm_freeride.cpp` `BlendWeather` + `gm_freeride.h`: a preset change (clock preset or clear/cloud/rain state) now blends 10 s (time of day) / 40 s (state change) through a flat interpolated preset; rain_* / thunder_* keys ease with t^2 when growing (drizzle first), mirrored when fading. `-noweatherblend` = old instant switch. [INF: durations and curves]
- `eden_windscreen.*`, `eden_scene_view.h`, `gm_freeride.cpp`: droplets now also show in chase / bumper views as a "lens" variant (45 % cap, no wet film); cap ramps with rain_alpha (full from 128). `-nolensdrops` = old (cockpit only).

### Verified
- Builds; `-clockrate 600` run 1500 frames, no errors; chase view `-intempery 3` log "droplets on", screenshot shows rain scene with no card. The blend itself and the headed driver were NOT looked at in 3D.

### Not done / next
- Rain on the car's glass seen from outside (needs a glass-material wetness term in the car shader) - not started. Wipers already exist (F6 cycles off/auto/fast, `-wipers`); only the cockpit view shows the blades.
- Lens drops are few (retail cap is 10 x `-dropletscale 4`); raise `-dropletscale` if too faint.

## Session 2026-11-15 (`main`): cars with no chase camera now fall back to the default camera sets

Owner: "I can only go into camera views cockpit and bonnet, no chase or anything else ... dig for the real camera data from the dev build".

### What changed
- `source/game/freeride/chase_camera.cpp` - **[DATA]** 99 of the 163 car sets in `camerasingame.xmb` have no mode 20/21/22 (chase); the club/reward cars (id 436, the cave FXX) have only 23/43/24/44, so the cycle `21 20 23 24 22` collapsed to 2 cameras. **[CODE dev build]** `TestDrive2DEV FUN_0068fed0` looks the mode up in a set and, on a miss, in the default set `0x270e` = **9998** (modes 20 21 22 23); 9999 / 10000 are the other defaults. The port now takes any cycle mode the car lacks from set 9998, then 10000. `-nocamfallback` restores the old behaviour, `-cameracar <id>` loads another car's set, `-dumpcameras` logs every car's modes.

### Verified
- `-cameracar 436 -logcameras`: 2 cameras in the cycle with `-nocamfallback`, 5 (first mode 21) without. Not yet looked at in 3D.

### Not done / next
- The dev build's first key for ids < 9998 is 9999/10000 by `[veh+0x79a0]==2` (0x68fed0): not understood (bike/class?), so I only used it as the second fallback. Chase offsets of 9998 are not scaled to car length - check a long car by eye.

### Update: the cave sector is `sector-6-3-6-5` (Hawai), centre about (42240, 22656)
The bank scan found **`mur_cave.2db` / `mur_cave_n.2db`** ("cave wall") in `islands/hawai/level/area-6/area-6-3/sector-6-3-6-5.bnk` - the only cave-named assets in Hawai. Sector origin = (6*8+6, odd row +half) * 768 = (41856, 22272), size 768 m, centre (42240, 22656). The island map's marker for a car there sits at 0.60 / 0.38 of the island image, the same spot as the player in the owner's video frame (0.60 / 0.35) - so this is the dark patch [INF, two independent hints agree]. `-island hawai -tp 42240,0,22656` lands there (Y 285). Not yet explored in 3D.

### Correction (owner's photo-mode shots, build 131): the cave is at about (39265..39465, 118..135, 20200..20270), sector `sector-6-3-3-2`
The `mur_cave` textures in `sector-6-3-6-5` were a decoy. The owner stood at the dark patch (39265, 118, 20254) and the photo camera shows a real hollow cave inside the rock: rock walls, arches, daylight openings, **no other terrain surfaces** visible (mud / asphalt / dirt patches not seen, only dark rock floor). Sector `sector-6-3-3-2` (-o.3dg 153 KB with `cliff` + `v_ivy` textures, `.shk` collision 171 KB). The car cannot enter: either the cave floor has no drive collision in the port or the mouth is closed; `-tp` always snaps to the terrain under the point (y 105.7), so it cannot place a car inside. Not tested: ghost mode inside (needs the ghost toggled off over the cave floor).

### Update: ghost is now a real no-clip; how the cave is built
- `-cavescan` (down rays through the cave box) shows the cave is a **static rock mesh** (S hits at ~y 130..200: roof and walls) laid over **flat heightfield terrain at ~y 100..121 = the cave floor** (T hits below the S hits). So the cave floor is drivable terrain and only the rock mesh seals it - the 360's map glitch dropped the car's collision for a moment, which is all it took.
- Ghost = **no-clip** (owner's idea; plain collision-off just dropped the car): the car hovers at a held height, throttle / brake set a fly speed, steering turns, **PgUp / PgDn** change height, **Shift** = fast. F4 checkbox or `-ghost`. Verified headless: from the cave mouth (39255, 118, 20254) the car flew east to x 39453 at 252 km/h, height held at 114.7.
- `-cavewalls` (static triangles ignored in the cave box 39150..39550 x 20100..20350) is kept as an F4 option, but the car still stopped at x 39272 with it (something else blocks: likely a road-class triangle or the heightfield), so use no-clip.

### Confirmed (owner, build 135): the cave floor is fully drivable with the cave-walls option
Owner got in by switching the **F4 "Hawai cave" option** on (static rock collision off in the box) with the no-clip off: the floor (the terrain heightfield, y ~119) is fully drivable, shown at (39400, 119, 20152) with a dark cavern, stalactite columns and a lighter ridge. No-clip alone could not get in (spawn near the cave threw the car into the air / dropped it through the floor: the first frames after a ghost toggle reset the velocity with no ground contact). Visible ground surfaces so far: dark rock / cracked mud only; the dev surface patches are not seen. Next: map the cave's extent (F6 marks), look for the dev-test geometry.

### Cave easter egg: open by default on Hawai + cave lighting (owner: "keep it automatic ... add lighting inside")
- The cave-walls option is now **on by default on Hawai** (`-nocavewalls` = the sealed rock again; it is off on the other islands). 
- **Cave light**: within 260 m of the cave centre (39350, 20230) the weather's `sun_ambient` is lifted toward a cool (1.10, 1.18, 1.35) glow, fading in over the last 120 m (`caveWeather` copy in `gm_freeride.cpp`; `-nocavelight` = off). Night (-clock 2.00) inside: mean brightness readable instead of near black; by day it adds a warm lift. The values are [INF] by eye, one night / one day capture; the owner plays with the gamma down, so tune with the F4 weather presets if it is still dark.
- Idea parked: hide something in the cave (a car, a collectible) - needs a spawn point inside; the floor spans roughly x 39250..39500, z 20150..20300 at y ~118..121.

### The cave's hidden club car (owner: "hide a club car; I want all cars obtainable offline")
- `DBClubRewardCars` has 4 rows [DATA]: **280 MF3 Roadster, 400 2-Eleven, 365 Apollo Sport, 436 Ferrari FXX Evoluzione**. The cave holds **436** (the other three are free for the same trick elsewhere; the id is `kCaveCarId` in `GMFreeRide::updateCaveCar`).
- Parked at (39420, 119.7, 20175) (the cave floor, found by a down ray), loaded lazily when the player is within 500 m on Hawai and the car is not owned. Drive within 6 m under 25 km/h and it is added to the garage (`OwnedCars`, `Garage`, `NbCars`, the "Vehicles bought" popup / COLLECTION_CARS_DIF XP, a hint "You found a secret ...", saved) and the parked model is removed. `-nocavecar` = off.
- Verified headless: at 5 m the log reads `the cave car (id 436) was taken`, the save has `cars_owned = 436` and the HUD shows the hint + the +75 pts popup. **Not verified**: the parked FXX is not visible in my 22 m approach shot (dark, behind the player car); its heading / spot may need adjusting by eye. The paint is the default.

### Open: red / white kerb stripes along the roads near the airport (owner, build 135, after returning from Hawai)
Owner's photo-mode shot at (16577, 86, 27637): flat red-white striped kerbs along every road edge by the car park; the 360 shows plain edges (only the red-white barrier arches). **Not reproduced** from the ground on a fresh Ibiza start, nor after `-travelisland hawai` then `-travelisland ibiza` (checked headless at the car park and the airport): the road edges are plain. The ground view only shows the barrier arches. Leads: (1) it appeared **after the Hawai -> Ibiza trip**, so a stale texture binding across the island switch (CLAUDE.md trap 13: a material cache keyed on a hashcode; Hawai has circuit-kerb textures) - owner is trying to reproduce; (2) a far-LOD road material seen only from the air (my no-clip lift shot at +60 m shows the airport's own red dashes at the runway edge). `-ghostlift <m>` (new) raises the no-clip start height for such shots. Next: the owner's exact steps; then `-logmaterials` right after the trip.

### FIXED: red / white kerb stripes (and likely the earlier "texture corruption" after shops) = cross-island texture borrowing
Owner reproduced it (Ibiza fresh = fine; Ibiza -> Hawai -> Ibiza = stripes down every road) and said it also happened after shops. **Reproduced headless** with the new chain `-travelisland hawai,ibiza` (red px in the road crop 2472); `-hidematerial RoadMaterial` removes the stripe (396 px), so a `RoadMaterial*` MARKS slot got the wrong atlas.
- Cause: `TextureCache::findForOwner` borrows a texture the material's own bank lacks from the **newest** owner that has it, and after an island trip the other island's owners are still alive; Hawai's `r_m_00` (road marks atlas, circuit kerbs) shadowed Ibiza's. (CLAUDE.md trap 13's family.)
- Fix: `findForOwner` searches the borrower's **own island** (`islands/<name>/` of the bank path) first, then falls back to the old newest-first. `-noislandborrow` = the old search. After the trip: red px 2472 -> 396 (= the plain road), screenshot clean.
- Not verified: shop / dealership return (same owners machinery but within one island, so this fix does not cover a same-island wrong borrow); Hawai after Ibiza (symmetrical, not run). `-travelisland a,b,...` chains trips for such tests.

## Session 2026-11-15 (`main`, 12): Hawai test cave - ghost mode to get through the wall

Owner found a video (HSG Automotive "TDU2 : Secret cave location on Hawaii"): the cave is behind a rock wall in the **dark patch just right of the yellow dirt track** north-centre of the Hawai map (the map-glitch gets through on the 360). Their frames: player near the top-middle of the island, a looping dirt track, a lake with cyan roads to the south-east.
- Added a debug **ghost mode**: `VehicleWorldCollision::Ghost()` makes `raycast` / `sphereContact` return false (no world, no ground), `-ghost` at start or the F4 checkbox "ghost - drive through the world". The car falls with no ground (tested: y 198 -> -92 in 200 frames), so toggle it off once inside the cave, where its floor has collision. Use with the F4 teleport (x / z, y snaps) and F2 / F6 marks on Hawai.
- Coordinates not pinned: my estimate from the island's road bounding box, (~40000, ~35250), was wrong (no roads there). Next: the owner's F6 mark at the patch. Bank scan for cave / test assets in `islands/hawai` banks was started (`_cavescan.txt`), not read yet.

## Session 2026-11-15 (`main`, 11): travel between islands (F4 panel + the airport pop-up)

Owner: "I tried to teleport to Hawaii; the airport is not wired and F4 has no way to go there" (the secret test cave is supposedly on Hawai).
- F4 panel: a **travel** row, "Go to hawai / casino / ibiza" (the others than the current island). `GMFreeRide::travelToIsland` writes a `StoryHandoff` (Island + the target island's **EAIR\*** spot road-side entrance read from its `spots.bin`) and restarts FREERIDE there; the profile / car carry over. `-travelisland <name>` = the same, 120 frames in (test hook). Verified: Ibiza -> Hawai lands at (7038.8, 1.6, 16850.8) on the road at the Hawai airport, "Hawaii Area 2".
- The map's airport pop-up: `/:CurIsland` + `/:AirportFree` make the movie show **"Go to Hawaii"** as its first button; its FSCommand `isld` travels to the other island of the pair (Ibiza <-> Hawai; the casino island stays an F4 / `-island` trip). No level gate or price offline [INF].
- Side fix: the pop-up's buttons answer a few frames after the key (the movie plays its press animation first), so the map polls for the island trip and for "Go there" every frame; the pop-up's "Go there" is locked when no discovered road is under the spot (`IsTeleportOK` = `getFastTravelPoint`), like the old menu. Before this the pop-up's Go there never teleported (checked: it logged `goin` and nothing else).
- Not done: the retail airport flow (cutscene, plane, the price "first time") - only the instant trip. The test cave: not located; once on Hawai use F2 / F6 to mark coordinates.

## Session 2026-11-15 (`main`, 10): road 827 (code 0x604) is a tunnel, not dirt; the owner's "fell through the world"

Owner, build 128 (the depth bias was still on): the map showed black trees and white tracks, the overlay read `road 827 type 604 w1 21 w2 101 class 0 DIRT` at (20471, 197, 6073) and the car ended up inside a diamond-plate structure on top of a tunnel.
- `0x604` is **not dirt**: road 827 (20504, 6440) is a tunnel. By the `[HudMap]` AIRL table (4 circuit, 6 tunnel) the road type may be the **high byte** of the ROAD_DETAIL code half-word (0x604 -> 6 tunnel, 0x400 -> 4 circuit), which would also explain `0x400 / 0x100` sitting on the small closed circuit [INF: a guess that fits two data points]. The static dirt rule now keeps only `0x0` (the caravan track) and the offroad-school chain (`0x5 / 0x202 / w1 13,14,17`).
- Not reproduced: a car dropped at (20471, 200, 6073) rests at y 198 with all four contacts (a solid surface there), so the car was on the tunnel's roof structure; the depth bias is render-only and does not touch collision. The map's black trees did not reproduce either (close map at (20333, 56, 20182), bias on / off: mean diff 0.07). Both reports were on the bias build; retest on the current build.

## Session 2026-11-15 (`main`, 9): the terrain depth bias is reverted (it caused the blue line / see-through look)

Owner (screenshot at 17308 / 125.6 / 17970.9, build 128): "a weird blue line and it makes things look kind of transparent".
- **Cause: my own session-3 change.** The `TD_HeightMap` depth bias (constant 500000 units on a D32F depth buffer = ~0.03 of depth = metres of world distance at 20-300 m, since depth is ~`1 - near/d`) pushed the far terrain behind the ocean plane / the far sheets, so a bright sliver ran along distant ridges and objects looked see-through. Measured with `-noocean` (sky shows through the gap): **1835 sliver px with the bias, 81 without**; `-nocarve / -noareas / -nolodgeometry` do not change it.
- `eden_renderer.cpp`: `-terrainbias` now defaults to **0** (opt-in). Small values (3000..50000) leave the sliver alone but do not clear the old-town rubble either (mean diff vs the 2M reference 6.6 .. 5.7, none = 6.76), so **the rubble is not a few cm of depth**: it is open again. Lead: the heightfield materials carry States `05 06` (SRCALPHA / INVSRCALPHA blend), i.e. the retail terrain is alpha-masked by its colour map where paved meshes lie; compare the port's TD_HeightMap alpha path. `-logstates` (new) prints each material's raw States bytes.
- Lesson recorded: a constant depth bias on a float depth buffer is a distance-dependent push; judge any such change at far range too, not only at the spot that prompted it.

## Session 2026-11-15 (`main`, 8): the F2 coordinates overlay names the road under the car (to find dirt roads in our own build)

Owner: "I may be best using our version to find these roads - I have all locations and marks, and can test Hawai".
- `MapScreen::describeRoad` + `CoordsOverlay::SetRoadInfo`: with the overlay on (**F2**, or `-coords`) a line under "F6 = mark" reads e.g. `road 1356  type 5  w1 14 w2 202  class 0  DIRT` - the DAOR id, the `ROAD_DETAIL` type code (low half of header word 4), word 1 / 2 and the class byte, and `DIRT` when the map already treats the link as dirt. **F6** appends the same text to the mark line in `_marks.txt`. Works on any island (`-island hawai`); the dirt rule itself is the Ibiza-derived one, so on Hawai read the codes of the tracks you know are dirt and send them (or the `_marks.txt` lines) and I will add them.
- What to do: drive / stand on a known dirt track, note the `type / w1 / w2` and whether it says DIRT; do the same on a tarmac road next to it. A dirt road that does not say DIRT = a code to add; tarmac that says DIRT = a code to remove. Verified headless at the school (`road 1356 ... DIRT`).

## Session 2026-11-15 (`main`, 7): dirt roads anchored on the offroad school (owner's 360 shots)

Owner sent two 360 captures at the offroad racing school (**`ERS_B` at (17164, 18079)**, "Obtain B4 license", the 360 debug overlay read X -29.2 / Z -37.8 at its gate): the dirt roads are **yellow once discovered, thin and lighter grey before**; a short yellow dirt road runs from the school up to the blue main road and on south.
- Nearest `ROAD_DETAIL` records to the school: **1355 / 1356** (810 / 612 m), header `w1 = 0xe`, `w2 = 0x202`, code `0x5`; the same signature runs through ids **1349..1356, 1367, 1368** - a ~5 km chain from (13000, 16500) to the school and on to (20400, 20400) [DATA]. Rule now: `code 0x5 && w2 == 0x202 && w1 in {13, 14, 17}` (`w1 = 15` is the same code in the old town, paved) plus the earlier 17 rare records [INF].
- `MapScreen`: dirt edges draw 0.7 x thinner and, undiscovered, `175/175/165` instead of `140` grey (`-nostaticdirt` = off). Built and run headless; **not verified at close zoom** (the island view is too coarse to see the thin lines) - open the map at the school and compare with the 360 shot.
- The 360 overlay is not world X/Z of the cursor on the map screen (the map shot's X 6.3 / Z -1236 is the map camera); only the in-world shot at the gate is a usable anchor, offset ~ (17193, 18117) [INF, one point].

## Session 2026-11-15 (`main`, 6): a static dirt-road flag found in ROAD_DETAIL (candidate, owner to compare with the 360 map)

Owner: yellow dirt roads show on the 360 map (and our satnav once driven) but not on our map.

- **Finding [DATA + INF]**: the road *type* is in the `ROAD_DETAIL.IA` `ATED` header, **low 16 bits of word 4** of the first sub-block (the `DAOR +12` class byte is not it). Ibiza census: `0x203` 703 roads, `0x204` 222, `0x302` 217 (the ordinary roads), **`0x104` 92 roads = the long trunk roads** (drawn as the red lines in the scratch plot: the highways, AIRL 0), `0x3/0x4/0x5/0x200` = the old town; and **17 rare records `0x0 / 0x100 / 0x400 / 0x604`** (~7 km): the caravan track (ids 1389-1391 at 36037,5769 - the owner's confirmed dirt link), a small closed rally loop at (27300-27800, 11000-11660) and two short coastal tracks. Header word 1 / 2 are cross-section profile ids (0x24..0x2a on the loop).
- `source/game/map_screen.cpp`: `edgeDirtRecord` marks those links; `segDirt` starts all-set for them, so a discovered one draws the retail offroad yellow without having to be driven (`-nostaticdirt` = the old driven-only behaviour). Verified headless with a fully explored profile: yellow on the coast track and the loop; the satnav shares `segDirt`.
- **Not verified / open**: the 360 screenshot's long winding yellow road is not among these 7 km, so either the retail rule is wider (other codes, e.g. the rural `0x104`-adjacent ones, or terrain paint) or that road is on a part of the map I have not matched. Next: ask the owner for the 360 map at a known place (a spot name) and compare; Hawai uses a different code set (not scanned).
- Scratch: `-logroadclass` link numbers are NOT `DAOR` indices (the "links 1023 / 1347" of the earlier notes are 20 km from where they were read), so the older ground-truth lines in `roads_spec.md` are suspect.

## Session 2026-11-15 (`main`, 5): the retail map hover cards (MAP.vmf /tool_tip) on every spot kind

Owner: "add the hover cards and menus" (360 captures: Fearless / Racing School / Audi cards and pop-ups).

### What changed
- `source/game/map_screen.cpp` `updateSpotCard` / `fillSpotCard` / `applyCardLocks`: when the reticle snaps to a spot (`snapSpot`), the root vars of its kind are set (`/:WhatSpot` = carDealer, rschool, estates, home, clothes, tuning, carWash, carPaint, bikeDealer, surgery, hair, photograph, airport, club, gamblingHall, stolenCar; `/:SpotName` title) and `/tool_tip.SetInfos()` + `open` run (the same movie path the championship race bubble uses); the card closes when the reticle leaves or the OK menu opens. `-nospotcard` = off.
- Data per kind [DATA var names from the movie's edit texts]: **car dealer** (`/:nb_cars`, `/:nb_cars_bought`, `/:car<N>_gp|_car_name|_state` from the DBCarShop stock + the owned cars + `_vehiclecategory`), **Racing School** (`/:nb_licences`, `/:nb_licences_done`, `/:li<N>_gp1|_gp2|_state` from `GetSchoolLicenses`: done / current / locked), **estates / home** (house name, `NbHousesBought` / `NbHouses`), **clothes** (shop name + brand logo, `NbClothes` / `NbClothesMax`), **sticker shop**, **car wash** (price), tuning / surgery / hair / photograph / airport / club titles. Titles other than the dealer / house / clothes names are English [INF]: the retail ones are localized strings.
- The extended pop-up (picture slot, Go there / GPS Target / Bookmark it, "Cars for sale" / "Licenses" lists with ticks) is now the **default OK on a spot** (`-nospotpopup` = the old three-row menu): OK extends (`tool_tip.Extend(1)`), Up / Down / OK are sent as **SWF keyPress codes 14 / 15 / 13** (the menu clip's button conditions `0x1c00 / 0x1e00 / 0x1a00` = keyPress 14 Up, 15 Down, 13 Enter; the first try used 38 / 40, which only ever reached the first row), and the movie's FSCommands `goin` / `gotp` (Go there), `targ` (GPS target), `mysp:enbl|dsbl` (bookmark) are handled. Back closes the pop-up first. SetInfos asks the game with `FSCommand bmap:ttip:info`; the locks are answered there.
- Test hooks: `-mapcardicon <icon>[,n]` (the reticle as if on the n-th spot of that icon), `-mapcardextend`, `-logcard`.

### Verified
- Pop-up, headless: Up / Down move the highlight Go there -> GPS Target -> Bookmark it; OK on Go there fires `goin`, on GPS Target `targ` (red route drawn), on Bookmark it `mysp:enbl` ("New favorite place added", yellow diamond, `bookmarks_ibiza` saved).
- Headless `-mapcardicon cardealer / rschool / estates / clothes / tuning / carwash / carpaint / surgery / hair / airport / club / photograph`: every card draws its own layout with its data (screenshots read by eye; the default OK menu still opens and closes; exit 0).
- The extended pop-up for the dealer ("Cars for sale" with the stock) and the school ("Licenses" with the A7/A6 row) look like the owner's 360 captures.

### Not done / next
- The picture slot is empty (the retail shows the spot's photo; the `picture` clip reads a page texture), the "1/7" slash does not render (only "o 7"), the Adrenalin event cards ("Fearless"; the port has no event spots yet), hotspot / duel / championship-diamond hover kinds, `IsSpotLocked` / road-locked texts.

## Session 2026-11-15 (`main`, 4): map accuracy scouting (dirt roads, hover cards, spot pop-ups) - findings only, no code

Owner (retail 360 captures vs ours): yellow dirt roads show on the 360 map and on our satnav but not on our map; the 360 has a hover card per spot ("Fearless / Type Adrenalin / Reward $3,000 / Events succeeded 1/100") and an extended pop-up (picture, Enter / Remove target / Bookmark it, Rules, 1/2 pager; Racing School "Licenses obtained 0/1" + licence list; Audi "Vehicles bought 1/7" + "Cars for sale" list with tick boxes). Ours shows a bare "Target this spot / Bookmark it" menu.

- **The hover cards / pop-ups are already in `MAP.vmf`** (extracted with `otdu2_bnk_tool extract .../frontend/hires/map.bnk`, `tools/vmf_disasm.py` -> `map.txt`): `tool_tip.SetInfos()` (map.txt ~16561) switches on the root `WhatSpot` over ~40 kinds - `carDealer, rschool, estates, clothes, surgery, hair, forsale, home, bikeDealer, stolenCar, carRental, carPaint, tuning, carWash, auctionHall, event, event_dlc, mission_spot, photograph, hotspots, asphalt/dirt/classic(_h), cup(_h), race, timeAttack, speedtrap, speed, eliminator, ftl, kyd, airport, club, friend, player` - and calls per-kind setters `setRealtor`, `setClothes`, `setTuner`, `setAirport`, `setClub`, `setContests`, `setChallenge`, `rschList`, `CarsList`; `Extend(1)` opens the pop-up; `ValidMenu` builds the Enter / Target / Bookmark buttons (`getLockText`: `road_locked` = the "Road locked" box in the capture). The port already drives this for championship races (`MapScreen::showContestRace`: root `WhatSpot` / `SpotName` / `GoldReward` / `Distance` / `NbLaps` / `GameRules`, then `/tool_tip.SetInfos()` + `open`). So the work is **per kind: set the root variables the kind's setter reads, then SetInfos / open / Extend** - not new UI. Not started; the variable list per kind has to be read from each setter (`setRealtor` etc. in map.txt).
- **Dirt roads**: our map colours a road yellow only for segments the player drove (`MapScreen::segDirt`), because the link class byte is not a dirt flag (`docs/re/roads_spec.md`: Ibiza dirt class 2 at two links, but the owner's track at (27021,14752) is class 0; `RoadMaterial` / `crossroad_detail_country` road meshes carry no obvious dirt marker either). The 360 map shows every dirt road in yellow from the start, so the retail data has the flag somewhere I have not found: candidates are the road-mesh material of the link (the sector `RoadMaterial_*` DETAIL `rt_d_0x` vs `r5_d_00`), the terrain paint under it, or a per-link field of `ROAD_MAP` / `ROAD_DETAIL.IA` (`FTED` / `ETED` records, not decoded). Next step: take the two known dirt links (1023, 1347) and (27021,14752), print every field of their DAOR / ATED records and the road mesh material under them, and look for the field that separates them from tarmac.

## Session 2026-11-15 (`main`, 3): "rubble" on the old-town pavement fixed (terrain depth bias); the graffiti is authored data

Owner (screenshot, Ibiza 11395 / 12.22 / 15474, heading 294): "graffiti on building and odd piles of rubble on some pavement I don't think should be there"; the 360 build at the same spot: "no rubble or graffiti".

**Reproduced**
    build-mainin\opentdu2-main.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:	emp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -tp 11395,12.22,15474.08 -heading 294.4 -clock 14.00 -autoplay "300:shot=C:/OpenTDU2/_x.ppm" -exitframe 320
(`-clock 14.00` freezes the clock - without it every run differs by the sun and A/B diffs are meaningless.)

### What changed
- `source/render/eden/eden_renderer.cpp` (`getPipeline`): the **TD_HeightMap** programs (sector 3 m and area 24 m heightfields) get a rasterizer depth bias (constant 500000, slope 2, pushed back) so the paved `TD_Ground` meshes laid over them (`Set_PavementB` cobbles, `SA_CityMeshMate`, plaza slabs) win the depth test. `-terrainbias <n>` overrides, `-noterrainbias` / `-terrainbias 0` = old behaviour. [INF: the retail depth state is not readable; the effect matches the 360 reference.]
- `-logmaterials` now prints each slot's type (`COLOR= DETAIL= DETAIL2= ...`).

### Verified
- `-noterrain` gave clean cobbles (mean frame diff 3.45); `-nocarve/-noroadskip/-noprunecarve/-noprojectors/-nowet/-noareaobjects` = 0.0 diff. A bias on the `TD_Ground` programs made it worse (it moved the cobbles too); on `TD_HeightMap` it clears the rubble: 20000 / 100000 do not, 400000 = 2000000 (diff 0.6). `docs/images/terrain_rubble_before_after.webp` (before | after).
- Two other spots (21449,24598 and 27021,14752, on vs `-noterrainbias`): no visible regression; a kerb strip that was buried now shows. Not run under the validation layer.

### Graffiti - not a bug in the port (probably)
- `-hidematerial` bisect: the wall is **`SB_ai_wall`** (TD_Building ps16): `COLOR=sb_ai_c DETAIL=chaux_d DETAIL2=tagsa_c`. `tagsa_c` (512x512 DXT1, `commonworld.bnk`) is a graffiti atlas; ps16 samples it with Texcoord1.zw and multiplies it into the wall. The `dirt*_d` textures are plain stain gradients, not graffiti. The port renders what the data says (zoomed crop looks like real tags).
- The 360 build shows that wall clean. Either the 360 build drops the tag layer or the PC retail does too; not decided. To test: `-hidematerial SB_ai_wall` hides the whole wall; a `-notags` A/B (DETAIL2 -> white for `tagsa_*`) is the next step if the owner wants parity with the 360.

### Follow-up: `-notags`
- `-notags` drops every `tags*` texture at material load (graffiti atlases; the wall takes the permutation without DETAIL2). Verified at the same spot: tags gone, rest of the frame identical (mean diff 2.69 over the whole frame, all in the wall crop). Default stays **off** (PC data carries the tags); say so if parity with the 360 should be the default.

### Follow-up 2 (owner: "could it be similar to the airport issue?" - yes, a sampler address): the tags now clamp
- `-logaddress` (new) prints each slot's raw 2DM address codes. `tagsa_c` is **(U2,V2)** and `AddressIndex` read 2 as *mirror*; the tag UV set leaves [0,1] (`-edenwrapall` painted whole walls with tags), so mirror repeated the atlas across the wall and roof. As **clamp** it reads the atlas's blank edge: the wall is clean with the layer still on, like the 360 (`-notags` no longer needed for parity).
- `eden_renderer.cpp` `setupMaterial`: `tags*` textures sample clamp/clamp (`-tagsmirror` = the old mirror). Clamping **every** (2,2) texture was tried and **rejected**: the terrain ground detail flattened to a smear (27021,14752). So (2,2) is not clamp in general - it is per family, like the airport's code 3 [INF]; the real mapping is in the encrypted loader.
- Verified: wall at 11395,15474 clean; 27021,14752 and 10490,15732 identical to before (diff 0.0). **Not verified: the roof tags from the owner's photo-mode shot** (needs an elevated camera; the same material family, so expected fixed) and other SB_* walls elsewhere.

### Progression / XP (owner: "start the progression and xp")
- The XP / level framework already existed (`skill_levels.*`, `skills_spec.md`, popups); the gap was the **triggers**. Added `AwardCollectionMilestones` and wired houses, clothes, stickers, furniture, licences (RACING_LICENCE_*) and cups; the two car-count loops now use it. Verified: `-awardmilestone COLLECTION_CLOTHES,12` pays 5 + 10 only; a C4 licence completed from a seeded save pays `RACING_LICENCE_C4` +100 racing and saves it. The list of events still dark is in `docs/re/skills_spec.md`.
- Next: a medal rule for `RACING_CHAMP_*`, the panorama photos, `DBAreaLevel`, then the story steps from the dev exe (`GSPlayerProgress`, the enter/leave cases) - not read yet.

### Not done / next
- Progression / XP / quests (owner: "start doing progression stuff"): nothing started; see the dev-build survey 3.1 (`GSPlayerProgress`, `DBPlayerLevel`, `DBRewardXP`, story-step enter/leave cases) - first step is `docs/re/progression_spec.md` from `TestDrive2DEV.exe`.

## Session 2026-11-15 (`main`, 2): enhanced damage is an opt-in mod; vanilla repair = going home

Owner: "make sure damage is opt in; vanilla has limited damage and no repair method - going to a house auto-repairs the car".

- New launcher mod `enhanced-damage` (`DamageModEnabled()`, `-damagemod` / `-nodamagemod`). **Off (vanilla)**: only the retail damage (zone damage, cracked glass, hanging doors / bonnet); no lost parts or debris (`CarPartRig::setPartLossEnabled`, also for traffic), no crumple (`bAllowCrumple`), no sparks / smoke, no tyre model (`VehicleDynamics::setTireModelEnabled`: grip 1, no wear / burst / rim). **On**: everything of the damage sessions.
- Vanilla repair: entering a house interior repairs the car (damage, zones, tyres) when the fuel-system mod is off (with it on, the pump's paid repair is the route).
- Verified: `-setzone 200,62000,62000 -settire 200,0,0.9,0` : `-nodamagemod` keeps both bumpers and wheels; `-damagemod` loses the bumper and shows debris (screenshots compared). The house auto-repair path was built but not walked in a run.

## Session 2026-11-15 (`main`): fuel is an opt-in mod; retail-style fuel gauge; pump help bar

Owner: "use the retail help bar states for the pump screen; make the fuel UI look like these (dev-build gauge + segmented bar); turn fueling into an opt-in mod through the launcher so vanilla players are unaffected".

### What changed
- **Opt-in mod `fuel-system`**: the launcher's Mods page creates it (`launcher_mods.cpp`, mod.json only, no files). `GSFile::isModEnabled` + `FuelModEnabled()` (`fuel.cpp`; `-fuelmod` / `-nofuelmod` override). Off = vanilla: no burn, no Breakdown, no gauge, no pump prompts (stations, house garage), no map pump icons, no LackOfPetrol. Note: the pump screen also carries Car repair, so with the mod off the station / garage repair is gone too [INF].
- **Fuel HUD** (`freeride_hud.cpp drawFuel`): round dial (arc of 11 ticks E..F, red zone at E, pump glyph, red needle on a grey hub, E / F, litres + range) after the dev-build dial; `-fuelbar` = the segmented digital bar (pump, E, ten cells, F; red when low). All procedural (Renderer2D triangles), no art needed; AlexVII's sheet could replace the glyph later.
- **Pump screen help bar** (`fuel_menu.cpp`): the retail `help` states `gas` / `vpower` / `fill_up` as text: loc 1638 / 1639 for the selected grade, "A / Enter: fill up" button row.
- `-openpump <frame>` test hook.

### Verified
- `-fuelmod -fuel 55`: dial needle at 70 %; `-fuelbar -fuel 14`: 2 red cells; `-nofuelmod`: no gauge. `-openpump 200` screenshots show both help texts. `enabled.txt` with `fuel-system` -> log "fuel-system mod is enabled"; without -> "off (vanilla)". Launcher builds; the Mods page itself not viewed on screen.

### Not done / next
- Real tank burn over a long drive not re-run; the pump icon is procedural (AlexVII's sheet would replace `drawPump`).

## Session 2026-11-14 (`main`, 17th): phone calls playable (owner: "build calls now")

### What changed
- New `source/game/freeride/phone_calls.{h,cpp}` + `FreeRideHud::showPhoneCall/closePhoneCall` + wiring in `gm_freeride.{h,cpp}` + `PlayerProfile::PhoneCallCounts` (saved as `phone_call_<NAME>`). Details and limits in `docs/re/phone_calls_spec.md` "Built". Debug: `-phonecall <NAME>` forces a DBPhoneCall by name.
### Verified
- Build 88, `-gm FREERIDE -skipcine -nopad -fixedtimestep -phonecall REALTOR_CALL -autoplay "400:shot=..."`: the retail popup shows with the voice; `CAR_WASH` hangs up after its voice. Not verified: save round trip across runs, other languages, the nudge firing on its own after 45 s.
### Verified 2026-11-14 (later; the earlier driver crashes were environmental and cleared)
- Accept -> GPS and the portrait preload now RUN OK (build 90: log `accepted -> GPS to 'ERE_B' (estates), 23129 m`, route + 26.2 km on the satnav). It was previously noted as unrun: from ~16:53 every start of any build (also `build-glm`, also a fresh userdata) crashed in `nvoglv64.dll` (0xc0000094 / 0xc0000005). Reboot / driver check, then run the command in phone_calls_spec.md.
### Game clock decoded (same day, latest; answers the "6 game clock" brief below)
- Full writeup in `docs/re/clock_spec.md` [CODE-dev]. The brief's premise was wrong: `FORCED_OFFLINE_DAYLENGTH` never writes `0x16867b4` — that global (init 4) is **`NBGRASSBITMAPS`** (and `0x16867b8` = `NBBUSHBITMAPS`), grass render counts. There is **no index→seconds table**: `FORCED_OFFLINE_DAYLENGTH` stores its arg at `0x19e62a0` (unread in the dev exe; the online weather/daylength sync — its flag byte only gates weather debug dumps; the name table at `0x1664290` is Sunny/Cloudy/Rainny/..., so FORCED_OFFLINE_WEATHER's init 4 = "After_rain").
- The real day length = **`SETDAYDURATION`'s qword `0x164ec30`, default 10800 s = 3 real hours per game day** (7.5 min per game hour). The clock is the edSys2 `SystemTimer` singleton `0x1905ae0`: `_ftime64` OS epoch anchored at `mktime(2007-01-01 local)`, so **time of day = ((epoch − anchor) mod 10800) / 10800 × 24 h** — pure wall-clock function, phase locked (same real moment = same game time).
- `SETTIMER REAL_TIME|FIXED_TIME` (the `REAL_TIME` at 0x51207d) = bit0 of the edSys2 settings byte `+0x4b` (reader not in the dev exe); the working freeze path is the timer's `+0x2C` tick enable (debug toggle 0xc1ab0f). `SetFromHMS` = h*(dur/24)+m*(dur/1440)+s*(dur/86400).
- **The time of day is NOT in the save** (owner's 360 `PLAYERSAVE/DATA` dumped with `otdu2_bnk_tool xmbdump`: `PlayerData.Environment` = `{Variable[] Variables}` holds only `RnkChkC="13"`; no clock field in the schema) — consistent with the wall-clock derivation, and it means the clock always runs in offline free roam.
- Port next step [INF]: `WeatherCycle` should switch from mirroring the PC clock's own time of day 1:1 to the retail formula (§6 of the spec); `-clock hh.mm` = FIXED_TIME (frozen phase). Retail-sun phase check against a retail screenshot at a known wall time would settle the last [INF].
### Game clock: the original 3-hour day (same day, later)
- GLM read the dev exe: the retail day is **10800 real seconds** (SETDAYDURATION initial value; 3 h per game day, 8 days per real day); time of day = ((wall clock - 2007-01-01 00:00 local) mod 10800) / 10800 x 24 h (edSys2 SystemTimer 0x1905ae0, `_ftime64`); the time of day is NOT saved (the 360 save has no clock field); `FORCED_OFFLINE_DAYLENGTH` is only an online weather sync argument (the "index 4" I noted before was NBGRASSBITMAPS). Implemented in `WeatherCycle` (`RetailSecondsOfDay`, rate 8): the pause clock and the lighting follow it; the weather schedule runs on its own never-wrapping `weatherClock` (the old one froze after midnight when a state change was scheduled past 24 h) with the same real-time durations as before. Verified: the in-game clock 19:15 vs 19:16 from an independent formula. `-clocktime` / `-clockrate` / `-clock` still leave the wall clock for tests.
### Save/load restores the drive; game clock research (same day, later)
- 5 save/load: `PlayerProfile::LastDrive` (island, position, heading, car file, roof, windows, damage, mud/dirt/hardness/wetness) is refreshed every free-roam frame and saved with every save (`last_drive = island|x|y|z|yaw|car|hardtop|windows|damage|mud|dirt|hardness|wetness`) and on leaving FREERIDE. `GMFreeRide::enter` puts the car back exactly there (island from the save when no `-island`; the house exit only for a new profile; `-newdrive` forces the old start; `-tp`, `-home`, `-atwash`, a handoff still win). Damage now also survives scene trips (`VehicleSceneState::Damage`, `VehicleDynamics::setDamage`). Verified headless: run 1 drove and quit, `last_drive` written; run 2 without `-tp` logged "the save's drive" and started at the same (34970.4, 9.9, 5477.7), heading -101 deg, damage 65535. NOT verified: other islands, a car other than the starter, the pause-menu save path in a real run.
- Damage accuracy (owner: "doesn't seem accurate"): a 400-frame headless drive took the damage from 0 to 65535 (+7948 at 43 km/h, +1689 ...), i.e. `addImpactDamage` is fed large magnitudes (per contact frame while scraping?). Not calibrated: next step is to log the magnitude per call at gm_freeride.cpp ~5824 and the car-world hits in `VehicleDynamics`.
- 6 game clock: the dev exe has the retail clock commands `FORCED_OFFLINE_DAYLENGTH` (a setter into global `0x16867b4`, initial value 4 = an index, not seconds), `SETDAYDURATION` (registered at `0x519dba`), `REAL_TIME` (`0x51207d`), the `day_time` Flash var (`0xcea476`) and `m_time_scale`; `GamePC.cpr` has none of these (`-dumpgamepc` lists its time keys: only online timers). So the day length is code / table data, not config. Brief for GLM: map index 4 of `FORCED_OFFLINE_DAYLENGTH` (and the other indices) to a day duration in seconds, find where the time of day is stored in the save and whether the clock runs in free roam offline, and what `REAL_TIME` does. The port still reads the PC clock (`WeatherCycle`).
- 2 car wash blocking a road: not reproduced - `-atwash` started in the pre-drive state (the car held by the brake branch at gm_freeride.cpp ~5525), so the test said nothing. Needs the owner's exact symptom (where the car stops: the arch, the booth, the road before it).
### Cut shop types + the nightclubs (2026-11-15, later; owner's AlexVII leads)
- Full writeup in `docs/re/cut_shop_types.md`. The petrol-pump icon = the **cut fuel feature**: the engine has a `gasStation` spot kind + `FSCommand:gaso` (gmeditor.vmf), but no EGAS spots ship, no fuel icon label in MAP.vmf/HUD.vmf, only the garage `gaspump_d` prop. Cut shop types with UI but no spots: **carRental, auctionHall (DLC2 hook), watches, forsale, driveIn**, map filter legend `_AFTER MARKET`; ORC has its own kind + `orcscreen.bnk` UI (`FSCommand:orcp:wmra`).
- The "strip club" behind AlexVII's black box: **no strip club in the data** (dev exe, both decrypted DBs, all 44 UI VMF banks searched). What ships: two **real licensed Ibiza nightclubs** as scenery spots — `enc_pac.bnk` = **Pacha** ("PURE PACHA / TODOS LOS VIERNES" flyer, cherries, sushi lounge) at (23130, 23132) and `enc_priv.bnk` = **Privilege** (blue neon "Privilege" + red spiral logo, "WORLD'S BIGGEST CLUB" banner, merch stand with shirts) at (19905, 17979) — both already stream in the port via SpotExteriors; no interiors, no shop rows, no dancer/stage entities. Plus Hawai-only `edi_b.bnk` = **Ruby's Diner** at 25 spots, icon-less on the map. The collage logos are real per-shop sign textures ("Sea Speed & Sun" = `ers_a/rs_a_board.2db`; the clothes shop is "Hiro Hisaishi"; the hairdresser has `enseigne_wip_d`, a WIP placeholder sign).
### Pump screen swap: not possible as hoped (2026-11-15, later)
- The `gas_station` clip is NOT in the PC MAIN.vmf (a button targets it, nothing places it): only the help-bar states + variables exist. Spec section 8 of fuel_gas_station_spec.md. Native pump screen stays. Done instead: map OK menu blocked when the tank is empty (`MapScreen::setLackOfPetrol`, `-lackofpetrol`). Option left: use the retail `help` states (`gas` / `vpower` / `fill_up`) for the pump screen's prompt bar. The pump icon still needs AlexVII's sheet.
### Retail pump screen found in MAIN.vmf (2026-11-15, dig; swap NOT started)
- `MAIN.vmf` root has a `gas_station` clip (opened by a key button: `gas_station.gotoAndPlay("gas_in")`, key code 103 = 'g' [INF]); texts `_GAS STATION`, `_SHELL BASIC`, `_SHELL V-POWER`, `_FILL`, help `last_help_g` states `gas` / `vpower` / `fill_up`; variables `/:WhatGasType`, `/:WhatGasTypeInTank`, `/:LockVPower`, `/:IsLackOfPetrol`, `/:EnoughMoney`. This contradicts fuel_gas_station_spec.md ("native, not a movie").
- `map.vmf`: `/:LackOfPetrol` = 1 hides the map's OK menu (`menu._visible = 0`) and swaps the help bar - an empty tank blocks Go there / Target. The port does not set it (`bFuelEmpty` exists in gm_freeride.h).
- `FSCommand:gaso` is next to `car_name` / `CarStatus` (Home.vmf): not fuel. `opts:gase:` is Game settings.
- Next: `-dumpgasclips` (added in `PauseMenu::load`) dumps the clip tree; run it with `-dumplabelsclip` (output of the first run not read yet), then drive the page with a MAIN.vmf FlashPlayer: set the variables, `gotoLabel("/gas_station","gas_in")`, answer its FSCommands, keep the repair row native or find its place. Scratch decodes in `_dig/` (ignored).
### Crumple + tyre wear / burst / rims (2026-11-15, latest of all)
- Owner: "make the cars crumple and deform on impact; tire wear, eventually pop, ride on the rim". `docs/re/damage_spec.md` 3f. Crumple = CPU vertex deformation of the driven car (`EdenModel::deformPositions`, `CinematicCar::addDent`, `GMFreeRide::updateCrumple`); tyres = `VehicleDynamics` wear / pressure per wheel (grip, radius, drag), rim-only drawing, sparks. Verified by shots with `-setdent` / `-settire` and a crash; NOT verified: long-drive wear rates, save round trip, traffic crumple (not done: shared models).
### Car damage: bumpers, bonnet, boot and doors come off + sparks / smoke (2026-11-15, latest of all)
- Owner: "enhance car damage as much as possible, bumpers falling off". Details and limits: `docs/re/damage_spec.md` 3e. New `car_debris.{h,cpp}`, `CarPartRig::lostMask/detachPart`, `EdenSurfaceFx::emitBurst`, `-setzone`. Verified with a parked car (parts fly, rear bumper gone); sparks / smoke particles and traffic debris NOT seen yet - first check: `-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 27037,11610 -setzone 250,55000,40000` with the bumper camera.
### Car wash blocking the road: FIXED visually (2026-11-15, latest of all)
- Owner's top-down world-viewer shot confirmed it: the wash bank's flat forecourt ground plate (ecw_a item 2, 42 x 56 m, 0.4 m thick) is drawn over the road along the wash front. `SpotExterior::load` now hides that item for the `ecw_*` banks (`EdenModel::setItemHidden`, rule: height < 0.5 m, x > 40, z > 50); `-keepwashplate` restores it, `-logspotitems` lists a bank's items. Verified top-down at the 3 Ibiza washes: the road is clear (screenshots scratch only). Not checked: Hawai's 5 washes (same bank file, same rule), the forecourt look at ground level, traffic passing the mouth.
### Car wash vs the road, measured (2026-11-15, latest)
- New `-scanwash` (with `-tp x,z` at a wash): samples the road graph within 70 m of each wash and tests a 0.45 m sphere at 0.9 / 1.5 m over the road against the whole collision. All 3 Ibiza washes (27062,11642 / 19123,18162 / 8129,26222): 0 blocked, road top surface +-0.1 m of the road mesh across the whole footprint - **the wash's collision does not stop a car or the traffic graph**. In all three the same link runs parallel to the front at wash-local z 24..28 (x -18..23), i.e. through the front canopy / forecourt (footprint z -22..34): the world-viewer top view (`-gm WORLD_VIEWER -nopanel -campos 27040,150,11650 -campitch -89`) shows the canopy roof and the dark forecourt plate drawn over the whole road width, lane markings hidden. So the "block" is the building drawn over the road (retail placement, car_wash_spec.md 0.4), not collision. Not checked: Hawai's 5 washes (`-island hawai`), what the owner sees in a drive (a stop, a hidden road, traffic through the tunnel).
### Stations, pause spacing, car wash (2026-11-15, later)
- Three built-in OIL stations on Ibiza from the owner's F6 positions (`ServiceStations`, kBuiltIn): Area 1 (18961, 18097), by the car wash (27002, 11622), east (27305, 13962); `<mods>/stations.txt` still adds more. Verified at the car wash one: prompt "Fill / Car repair: <name>" and the pump screen.
- Pause screen "bigger gap" (owner: two pause screens of one run, the second with the icon row and tooltip ~38 stage px higher, the clock and the card unmoved): `PauseMenu` lifts the movie's tab row by `kTabRowLift` once it settles; the lift's base was captured from the row's CURRENT matrix, so a pause that opened while the row still carried the previous lift captured the lifted y as its base and lifted it a second time. Now the base is the movie's own constant (222.8 px = 4456 twips) and the lift is idempotent. `-logpausepos` logs the row y and the lift state. Verified over 7 pause / back / pause cycles (always 4456 -> 3856); the owner's exact trigger (save icon showing) was not reproduced in fixed-step runs.
- Car wash "blocking a road": driving from (27036.6, 11706.6) south at 100 km/h passes the wash portal (27043, 11678) without a stop (z 11623 reached), so it is not a collision there. The owner's exact symptom is still needed (a stop, traffic, the GPS route, the Enter prompt?).
### Bike dealership + the map OK menu (2026-11-15)
- `EUC_BDCL` / `EUC_BDAS` are the bike dealerships (carried as building sites): icon + enter + the `ibd_cla` showroom (empty, no stock). Map OK menu: Go there / Target / Bookmark with the retail wording, bookmarks saved. Details in docs/re/map_spots_spec.md (last section).
### Traffic damage + repair at petrol stations (2026-11-15)
- Traffic cars take zone damage (bumpers, bonnet / boot, per-agent cracked glass; `CarPartRig`) - docs/re/damage_spec.md 3d. Repair row in the pump screen + `<mods>/stations.txt` - docs/re/fuel_gas_station_spec.md 7. Stations are NOT listed by default: needs the owner's positions (F6 marks). Repair price 3 % of the car's price at full damage [INF].
### Owner's 6-point list (same day, later)
- 1 purple road: it was road class 4 drawn as the retail "circuit" purple 102/102/204; dropped (class 4 = the tarmac cyan like the rest; the class byte is not a reliable type anyway).
- 3 dirt track discovered blue: the class byte does NOT identify dirt (the owner's track at (27021, 14752) is class 0, the first one class 2). Ground truth (`-logroadclass` now also logs the ground under the car): **both dirt tracks have bare terrain under them (no road / static mesh), the tarmac has a road mesh**. A driven segment is now flagged dirt when no collision mesh lies over its middle and the ray down lands on the heightfield (`trackCar`'s `isTerrainOnly`), stored per segment (`MapScreen::segDirt`, saved as `exploreddirt_<island>`), drawn yellow on the satnav and the world map. Known weak spot: a few painted-terrain street pieces in the old town can still read as dirt (one yellow dot seen at (22197, 24751)). Discovery also reaches 35 m ahead of the car (a second probe point).
- 4 smashed window repaired by the chase camera: `CinematicCar::applyGlassSwap` restores the clean `glassOriginals` when leaving the cockpit while `appliedDamageState` still said cracked; now reset to -1 so the crack is bound again. NOT done: the "bumper hangs off" report - the port's only damage visual is the cracked glass (`applyDamageState`); no bumper / part code exists, so that is the car model itself or a physics offset, not found.
- 2 car wash blocking a road: `-atwash` spawns the car on the forecourt but it did not move with throttle held (0.1 km/h, wheels on the ground) - NOT diagnosed, no blocking code found in `gm_freeride.cpp` (the prompt is only a hint + Enter). Next: log what the car hits (`collision.sphereContact`) there.
### Road discovery per section (same day, later)
- Owner: the satnav filled way too much; the retail reveals roads a piece at a time. Cause: a whole link (mean 873 m, the ring road 11.5 km) became explored when the car came within 18 m of any part. Now `MapScreen` explores per curve segment (~28 m): `segExplored` / `segLength` / `edgeExploredLength`, `markSegments`, radius 16 m [INF]. `isRoadExplored(edge)` = any segment driven; `isSegmentExplored(edge, k)` drives the drawing (minimap + world map colour per segment, grey for the rest) and `findExploredRoadNear` (fast travel / shops need a driven section). Area percentages and milestones count explored metres.
- Save: `explored_<island>` = fully driven links (old saves load as whole links), new `exploredseg_<island>` = (link << 12 | segment) for the rest (`PlayerProfile::ExploredSegments`; the save skips the segments of full links). Verified headless: fresh profile at the dirt road colours only the driven piece; a hand-written `exploredseg_ibiza` (15 sections of link 1023) loads yellow with the rest grey (screenshots were checked by eye, not committed). NOT verified: the write side (the file was not rewritten in the headless run) and the walking radius against the retail (16 m is a guess).
### Dirt class per island, off-road trace, no auto Return-to-road (same day, later)
- Owner's `-logroadclass` drive: the dirt road at (34999, 5568) is `link 1023 class 2`; Ibiza dirt = class 2 (`MapScreen::DirtClassOfIsland`), Hawai still 3 UNVERIFIED (no class 2 on that island). Verified headless: `-tp 35036,5509` shows the dirt road yellow, class 0 cyan.
- The satnav draws the off-road trace (`FreeRideHud::addTracePoint` / `endTrace`, colour [HudMap] driven trace 254/216/197/200 w2): the path off the road graph from the last on-road point, 4 m steps, last 8000 points, not saved. Screenshot `docs/images/satnav_dirt_offroad_trace.png`.
- The Return-to-road ring no longer appears by itself off the road (owner: "it only works when you hold the button"): `gm_freeride.cpp` shows /HUD/BOT only while the back-on-track button is held (the `-offroadhud` hook still forces it). The log line "return to road indicator shown" is now internal state only. Supersedes the passive-ring behaviour of backontrack_ui_findings.md.
- Hawai: drive a dirt road with `-logroadclass`, send the class. Other classes (0, 1, 3, 4 on Ibiza) are probably road types of their own; their colours are still the class table guess.
### Havok material ids read (same day, later)
- `HavokCollision::TriMaterial` + `Hit::Material` (per-triangle mesh material). Dirt is NOT marked on the road collision: every link's road mesh is material 0 (class 3 too), dirt id 1 is only on static meshes. So the dirt-road classification is still open; the yellow roads remain a guess. Details + counts in roads_spec.md. Next: the dev exe `HudMap` road-colour code (`0xc9e584`..), or the terrain colour map under the link. Debug: `-scanroadsurface` (class x surface, overlap per class, material histogram).
### Trigger rules built + dirt-road findings (same day)
- Phone calls: `checkNudges` runs the retail order (realtor, clothes, hair, surgery, stickers, car wash, treasure hunt, Hawaii schools) off a saved `profile.PhoneState` (play time, `BeenIn*`, Hawaii arrival), set in `openSpotShop`. Delays are [INF]. Verified with a scratch profile (`SURGERY_IBIZA` rings by itself). Missing rules are listed in phone_calls_spec.md "Rules built".
- Dirt roads: **could not be classified from the data.** Road surface and friction do not tell dirt from tarmac (all road collision, friction 0.7), and `DAOR +12 == 3` is unproven (roads_spec.md "Dirt roads"). The yellow dirt roads are still a guess. Next: read per-triangle ground material ids (`1, 0x3d, 0x3e` = dirt) from the Havok `hkpStorageExtendedMeshShape` subparts in `havok_collision.cpp`.
### MngPhone2 / real triggers read (same day)
- `MngPhone2` is only the popup UI. The real trigger table is `SCPhoneCalls` `0x9a2c30` (found via the debug menu's `LAUNCH PHONE CALL` callback): order, conditions, 30 s global gap, `_timer` x 60 per-call retrigger, play-time nudges - all in `docs/re/phone_calls_spec.md` "The real trigger table". Built: gap 30 s + retrigger. Not built: the rest (no play time / houses / car categories in the port). Threshold minutes (`TimeBefore*Call`) not found in the data.
### Not done / next
- Portraits + accept/decline are done (later in the same session; `-phonecall CAR_WASH -autoplay "420:press=validate"` shows Todd's face). Still open: what accepting does (GPS to the shop), the other `BeenIn*` flags set by their shops, the real story conditions (MngPhone2 unread), the chase badge unchecked.

## Session 2026-11-14 (`main`, 16th): satnav roads blue/grey by discovery; phone calls found in the retail data

Owner: "our satnav isn't accurate as discovered roads should be blue" + "dig for the phone calls and anything else (cutscenes, progression)" in the dev build.

### What changed
- `freeride_hud.cpp` `drawMinimap` + `freeride_hud.h` `setRoadMap` + `gm_freeride.cpp`: the in-car satnav drew every road white from the collision triangles and ignored discovery. It now draws the road graph (`MapScreen::getRoadShapes/Classes/Boxes`, `isRoadExplored`, new `getExploredLength` as the redraw key): driven = class colour (tarmac cyan-blue, highway 29/180/215, circuit 102/102/204, dirt 243/196/6), undiscovered = grey 140 [DATA: GamePC.cpr [HudMap]], grey first. The collision-triangle path stays as the fallback when no graph is set.
- `docs/re/phone_calls_spec.md`: **`DBPhoneCall` (33 rows) + voices (`pc_*.wav`) + call art (`frontend/allres/phonecalls`) are in RETAIL data**, not cut; the port just never drives them. Full table in the spec.

### Verified
- Build 84 (`build-main`), `-gm FREERIDE -skipcine -nopad -fixedtimestep -autoplay "500:shot=..."`: the satnav shows a blue driven stretch under the car and grey for the rest (`_sat1.png`, not committed). Not checked: after a long drive, at Hawai, and the 20 Hz rebuild cost (`-loghudtimes`).

### Findings about the dev build
- The dev package's data is byte-identical to retail (dev_build_survey.md 1), so **there is no extra map data in it**: the road graph / classes we already read are the same. What the dev exe adds is *readable code*: `HudMap.cpp` / `HudMapRoads` (`0xc9e584`), `SCPhoneCalls.cpp` (`0x9a3670` ...), `MngPhone2`, `/:UnlockedRoadsPercent`, `/:DelockAllRoads`. The retail discovery radius (we use 18 m `[INF]`) can be read there.

### Not done / next
1. The discovery radius is NOT at `HudMap` `0xc9e584` (that is resource setup) nor at the `RoadLock` / `AreaRoadsUnlocked` xrefs (save-field readers). Find the writer of those fields (callers of `0xa05771` region / `MngRoadNetwork` `0xd07120`) to replace `kExploreRadius` in `map_screen.cpp`.
2. `SCPhoneCalls` xrefs are save serialisation too: the saved fields and a name-based trigger pairing are in `phone_calls_spec.md`; the real triggers are in `MngPhone2` (`0xd20e50`), unread. Then build the phone-call queue.
3. The cop-menu / avatar-clothes leads from the owner's GLM notes are unverified here.

## Session 2026-11-14 (`main`, 15th): launcher update-check rails against the GitHub 403 (owner report)

The owner refreshed the Updates page a few times waiting for a build and got "403": GitHub allows 60 unauthenticated API calls an hour per IP and each check made two (releases + compare). Confirmed live: this connection showed `remaining 0 of 60, resets 16:27:37`.

### What changed (tools/launcher)
- `launcher_http.{h,cpp}`: `HttpGet( url, ua, timeout, ifNoneMatch )` sends `If-None-Match`; `HttpResponse` carries `ETag`, `RateLimitRemaining`, `RateLimitReset`, `RetryAfterSeconds`, `bRateLimited`, `bFromCache`.
- `launcher_updater.cpp` `GitHubGet` (every API call goes through it): an on-disk ETag + body cache (`%TEMP%\opentdu2-update\cache`), a **304 is served from the cache** (GitHub does not count it); on a 403 / 429 rate-limit answer it stores the reset time (`ratelimit.txt`, survives restarts) and **makes no call until then**; the message says "GitHub is limiting update checks until HH:MM" plus a hint that nothing is wrong with the repo. `UpdateRateLimitedUntil()`, `FormatClockTime()`. `OTDU2_GITHUB_API=<base>` overrides the API host (Enterprise / tests).
- `launcher_ui.cpp`: the Check button is disabled while limited ("GitHub limit - again at 16:24") and has a 15 s cooldown ("Check again in N s"); the startup auto-check stays (it is free after the first answer).

### Verified
- Real 403 on this connection: a packaged-style launcher copy reported "until 16:27" and wrote 16:27:37 (the real `X-RateLimit-Reset`); a second run made no call. Fake-API server: run 1 = two 200s, run 2 = two 304s with `If-None-Match`, same result from the cache. UI capture: disabled button with the time.

### Not done
- The Atom feed (`releases.atom`) as a free pre-check, a minimum interval for the startup check across launches, and an optional token setting (5000 / hour) - the ETag cache makes them unnecessary for normal use.


## Session 2026-11-14 (`main`, 14th): launcher - sidebar fits, Updates page rebuilt (owner request, build 81)

The owner: the sidebar is cut off (Online hidden), the Updates page has many scroll bars of which one works, show only the current build and the build being jumped to with dates and the changes, drop the per-release changelogs.

### What changed
- `tools/launcher/launcher_ui.cpp` `DrawSidebar`: the nav height comes from the item count (`kNavItems` x `S(46)` + padding) instead of a fixed `S(250)` (sized for five items, so Mods pushed Online out); a compact banner is used when the window is too short for banner + nav + the pinned profile block.
- `DrawUpdatesPage`: one status card with **CURRENT BUILD** (release string + build time) and **LATEST BUILD** (name + date of the newest release) when an update exists, else "You are on the latest build."; ONE "WHAT'S NEW IN THE LATEST BUILD" list (the compare-API commit subjects, else the notes of every release newer than this one, CI boilerplate and the compare link filtered); the per-release CHANGELOG card is gone. No card on the page has its own scrollbar any more (cards with `bScroll` ignore the mouse wheel, `NoScrollWithMouse`), so the page scrollbar is the only one.
- `main.cpp`: `--page mods`. Test hook `OTDU2_LAUNCHER_FAKE_UPDATE=1` renders a fake "update available" state on the Updates page (layout checks only).

### Verified
- DPI-aware window captures (`_shot_launcher.ps1`, scratch): all six nav items and the pinned profile block fit; Updates (normal and with the fake update) and the Mods page render as described. Not tested against a real packaged-build update (this tree is a git checkout).


## Session 2026-11-14 (`main`, 13th): the dirt-filled tunnel at the free-roam start fixed (owner report, build 81)

The owner: "at start of free roam you turn left and there's a tunnel it's blocked oddly" - parked outside it, F2 position X 35949.07 Y 5.71 Z 5889.96 HDG 5.2.

**Reproduced**: `-tp 35949.07,5.71,5889.96 -heading 5.2 ... hold=accel`: the car stops at z 5945 (y 10.3, launched). The black panel with the two amber lamps is the **hill (terrain heightfield) filling the bore**, the jet fans hang in front of it (`docs/images/tunnel_35949_before_terrain_plug.png`).

### What changed
- `source/world/eden_world.{h,cpp}` `FindRoofedRoadBoxes`: a name-independent carve rule. The keyword rule (`CarveKeyword`: tun / pont / viad / bridge in node names) missed this tunnel: its shell is a *material* (`5067tunnel_mesh`) on the node `BLOCK`, its road a material `RoadTunnelMater` with **its own shader-set id `e8ad93c00455291b`** [DATA] on the node `NETWORK`. Road triangles (that id and the ordinary road id) with the heightfield > 3.5 m above them (centre + 2 corners) are clustered (8 m, 4-connected, >= 6 triangles) into carve boxes that feed both `MarkCarvedCells` and `BuildCarveGround`. `-noroofcarve` is the A/B.
- `-lognodesnear x,z,r` (debug): nodes near a point, the terrain height there and the collision triangles under it (material, shader id, height).

### Verified
- Same drive with the fix: z 6011 at 126 km/h and 137 km/h later, car at road level; `-noroofcarve`: stuck at z 5945, y 10.3 (`docs/images/tunnel_35949_after_roofcarve.png`, a long lit bore with fans).
- `-scantunnels`: the rule now reports the covered roads of this tunnel (sector-5-0-6-7 / 5-1-6-0, ~500 m) and of other tunnels / underpasses across the island (see the log: `covered road (no tunnel node)`); the carve-ground index (road triangles) serves the physics there.

### Not done / next
- Not driven: the other newly flagged covered roads (city sectors 3-3-x, 3-4-x, 4-2-6-7); a false positive would show as a hole in a hill, `-noroofcarve` is the A/B. The owner also reported (build 78 / 81): the save starting at the Tess drive instead of the hotel (Profiles page shows the story step), dealer cars without collision and the exterior glass hidden in cockpit views (by design, `-keepglass`); F2 opens the coordinate overlay and the advanced graphics menu together (noted, not looked at).

## Session 2026-11-14 (`main`, 12th): dev-build survey, car damage decoded + accumulator, original Physics.cpr as a mod

The owner: work on damage; what else can the dev build give (grass / vegetation, traffic, road signs, DLC2 / bikes, cut content, menus, outfits); could it be an opt-in mod in a launcher Mods page.

### What changed
- `docs/re/damage_spec.md` (new): `DBCarDamageData` + the dev `DisplayVehicleDamage` logic (loader `0x751810`, `AddImpact 0x751ba0`, state `0x751a50`, minimum 50, lateral rule, `DamageMap%i` / `DAMAGESCN` render path, `TD_CarDeform_TS`).
- `source/physics/vehicle_data.{h,cpp}` `DamageParams` / `GetDamageParams()`; `vehicle_dynamics.{h,cpp}` `damage` (0..65535), `getDamageState()`, `addImpactDamage()`, fed by `resolveChassisCollision` with magnitude = impulse / dt `[INF]`. Log line `Vehicle damage: +N -> M (scratched|deformed|broken)`. **No visuals yet.**
- `docs/re/dev_build_survey.md` (new) and `docs/re/dev_build_modules.md` (new, 279 source modules grouped).

### Verified
- All 59 comparable `.big` / `.map` / `.cpr` files of the dev package are size-identical to retail: **no new assets in the package**. Only `Physics.cpr` and `GamePC.cpr` differ; the retail install is the TDU World retune, the dev `Physics.cpr` is Eden's original (angular damping, steering lock, ABS / TC / handbrake). Dropped into `<mods>/Physics.cpr` it loads (`Mod overlay: 'Physics.cpr'`); `-vehtest` barely moves (flips 0; lateral g 1.63 / 2.07 / 1.70 vs 1.63 / 2.07 / 1.72).
- Damage run: `-gm FREERIDE -autoplay "60:hold=accel;300:hold=steerleft;..." -exitframe 1300` -> scratched -> deformed -> broken; headless, no screenshot.

### Follow-up (same session, owner said "all"): Mods page, visible damage, traffic feed
- **Mods:** `tools/launcher/launcher_mods.*` + Mods page; `gs_file.cpp` overlay layers (`<mods>/enabled.txt` + `<mods>/packages/<id>/`, name validation rejects `..` and separators). Bundled import mod `original-physics`. Verified: 9-check harness (scan / import / enable / enabled.txt) and the game resolving `Physics.cpr` from the package; `-nomods` disables it. **The page was not viewed on screen** (GUI).
- **Damage visuals:** `CinematicCar::applyDamageState` swaps the glass `DETAIL2` to the retail cracked art from "deformed": log `damage state 2 -> cracked glass (5 slot(s))`, frame `docs/images/damage_cracked_glass_windscreen.png` (starred windscreen).
- **Traffic feed:** a player-traffic crash adds car-car damage (`ScaleCar`, `[INF]` magnitude); compiled, not exercised (no traffic hit in the test drive).
- **Police / traffic AI:** not started beyond the survey - the police chase meter already exists (`ChaseMode`); porting `AiCopMng` / `AiNavPolice` / `AiPoolTraffic` is several sessions.

### Not done / next
- Damage-map render pass + `TD_CarDeform_TS` body deformation, lateral rule, exercising the traffic feed; police and traffic AI from the readable `Ai*` modules; look at the Mods page on screen. The project cannot host Eden assets, only our own or "import from my copy" mods. The dev exe was not run (untrusted download).

## Session 2026-11-14 (`main`, 11th): the Apr 2012 dev prototype as a physics source - suspension law read

The owner found `TestDrive2DEV.exe` (Hidden Palace, Apr 6 2012 prototype) and asked to start with physics.

### What changed
- `docs/re/dev_build_physics.md` (new spec) and `tools/devbuild_disasm.py` (capstone helper: `--str/--dis/--vtable/--callers/--float`). No game code changed.

### Verified
- Dev exe `.text` entropy 5.89, no SecuROM: the encrypted retail band is readable. 278 source paths, 709 RTTI classes.
- Tyre law is the retail one (`D = MaxFriction x Fz`), so load sensitivity is NOT in the Pacejka evaluator.
- Suspension: spring `k x (1 + b x)` matches the port exactly; `0x1012930` (vtable slot 9) is a rest-length solve, probably the retail `[ENC]` 0x54bc70 / 0x54bac0; damper clamps velocity (`V` at +0x80); stabilizer bar `(K dx - C dv) x w`.

### Follow-up (same session): `0x1012130` decoded, damper limit tested
- `V = 2|axis . v_anchor|` (damper velocity limit), `v = -dOffset/dt`, `MaxDamperForce` is diagnostic only; port signs confirmed. New flag `-devdamper` (`vehicle_dynamics.cpp`, off by default) uses it instead of `kMaxCompressionRate`.
- A/B `-vehtest` H3 / Q7_V12 / ML63_AMG: 5 of 6 flip with and without it; lateral 12.29 -> 12.30 m/s2. **The damper is not the rollover lever.** Next: the factor `w` (wheel vfunc +0x40) that scales the total suspension force, then the contact solver.

### Follow-up 2 (same session): `w` chased
- `w` (wheel vfunc +0x40) = **dt** (`0xff4480`: `+0x5a8` Frequency, `+0x5a4 = 1/Frequency`): it only converts the summed suspension force to an impulse, the port's `force x h`. No hidden factor. Flagged "25 % momentum" damper exists but the car ctor clears its flag.
- Found Tire `SetNormalForce` (`0x1002a80`): clamps Fz to 1e5 N and derives effective friction coefficients `min(|F/Fz|, mu_max x FS)` at +0x44/+0x48/+0x4c (solver limits `[INF]`). Next: find who reads them (`0x10012f0`, `0x100c2e0`, `0x100d370`) and with which normal force; spec section 6/6b.

### Follow-up 3 (same session): the wheel pipeline and the solver
- `docs/re/dev_build_physics.md` section 8: wheel update `0x10001c0` / `0xffe7f0`: tyre `NormalForce = max(0, F_sd (n . axis))` with `F_sd = Force - stabilizer bar`, clamp 1e5 N only; impulses `N n dt` (+0x890) and `(Fy lat + Fx long) dt` (+0x8a0) applied at the contact anchor by vfunc +0x6c. The wheel owns `ContactNormalConstraint` / `ContactTangentialConstraint` members: friction is solved as a constraint (limit `mu_eff x N`, `[INF]`).
- New opt-in flags in `vehicle_dynamics.cpp`: `-devtyreload` (no stabilizer bar in the tyre load, projected on the contact normal) and `-devnocap` (no 2x static cap). **A/B: no change in flips** (H3 / Q7_V12 / ML63_AMG 5 of 6 each way; Enzo 1.77 -> 1.74 g).
- Next: the edPhys solver that consumes the two implicit impulses.

### Follow-up 4 (same session): THE ROLLOVER LEVER FOUND - lateral tyre force acts at hub height
- Dev wheel = its own rigid body; tyre impulses go to the wheel at `-n x LoadedRadius` from the hub and reach the chassis through the joint (spec 8b). The solver friction constraint is a low-speed aid only (bit 19, speed <= `TangentialConstraintSolverThreshold`).
- `vehicle_dynamics.cpp`: new opt-in flags `-devhublat` (lateral force at hub height = contact + wheel radius) and `-devhubforce` (lateral + longitudinal). **A/B `-vehtest`: `-devhublat` takes H3 / Q7_V12 / ML63_AMG from 5 of 6 flips to 0 (roll 90 -> 1.3-1.5 deg), lateral g 1.25 -> 1.63 / 1.74 / 1.82 (below DB mu 1.80 / 1.90 / 2.00); whole-roster `-vehcheck` 0-100 identical to baseline (70 / 137 within 0.3 s).** `-devhubforce` also fixes the flips but worsens the roster 0-100 (+0.33 -> +0.44 s mean error), so only the lateral part is proposed.
- **Made the default on the owner's approval** (same session): the lateral force acts at hub height unless `-nohublat` (A/B, old behaviour). Re-verified with no flags: 0 flips on H3 / Q7_V12 / ML63_AMG / Enzo; `-nohublat` gives 5 / 5 / 5 / 0 flips again; roster `-vehcheck` unchanged; an in-game smoke run (throttle / steer / brake, FREERIDE) is clean. **Not yet judged by feel on the DualSense** - that is the owner's check. `-devhubforce` (longitudinal too) stays opt-in because it worsens 0-100.

### Ruled out
- `-devtyreload`, `-devnocap`, `-devdamper`: identical flips -> not the tyre load source, not the load cap, not the damper.
- `-devdamper` identical flips -> not the damper.
- "Lateral grip cheat" (0x552420) is AI opponent tuning, not a player tyre knob.

### Not done / next
- Decode `0x1012130` (origin of `V` and the per-frame `MaxDamperForce`), then the wheel vfunc +0x40 factor `w`; prototype behind a flag and A/B with `-vehtest`. Full list in `docs/re/dev_build_physics.md` section 6. Nothing was run in the game this session (static reading only).

## Session 2026-11-14 (`main`, 10th): the retail 3D spot holograms and the satnav ENTER state

The owner: "there's a bunch of hologram markers in the game for races, houses, dealerships, shops etc.... Just like in this picture can we implement them please and also the map changes when in a certain radius of these and it shows you can enter can this also be implemented as our current implementation isn't correct."

### What changed
- Resumed the parked `wip/spot-holograms` work (cherry-picked `cf853c6`, it built and ran): `source/game/freeride/spot_holograms.{h,cpp}` loads `scenarics/packs/pack_spots.bnk` - 64 models [DATA] (`-logspotholo` lists them): 56 icon plates (`Clothes_Shop`, `Tuner`, `Playerhouse`, `House_To_Buy`, `Realtor`, `CD_<bank>` per dealer, `Event`, `Championship_*`, ...) and 8 backplates (`Square_Blue/DarkBlue/Red_Frame`, `Rhombus_Blue_Frame`, `Circle_Orange/Blue/Green/DarkBlue_Frame` - the plate with the pointer cone). `Placement` gained `Scale`.
- `gm_freeride.cpp` `tickWorldSpots`: every map spot / race start / championship start within 300 m (12 nearest) gets its hologram = a backplate + the icon plate, turned about the vertical axis to face the camera, nudged up to 12 m toward the viewer (the spot is the entrance in the facade - the wall swallowed the plate otherwise) and scaled 1..2.2x with distance. The flat 2D HUD tiles are used only with `-flatmarkers` (or when the pack does not load). `HologramIconFor` / `HologramFrameFor` hold the spot -> model rules.
- The satnav ENTER state (`freeride_hud.{h,cpp}`, `setMinimapEnter`, `getMinimapEnterStrip`): within the enter radius (60 m, `nearRadius`) the map eases to 0.45x metres-per-pixel (`mapMetersPerPixel()`, cache-keyed on the zoom), a dark header band reads ENTER (loc 1703, upper-cased), and the bottom strip carries the spot's tile (the movie's own `/HUD/enter/Spot`) with the prompt bar's button glyph beside it (the movie's platform logic picks the pad glyph). The old top-right card is gone in this mode.

### Verified
- `-tp 11530,4,16850 -heading 125 -autoplay "300:shot=..."`: the Clothes_Shop hologram (blue plate, suit icon, pointer cone) floats over the B-Miami building and the satnav shows ENTER + tile + glyph (`docs/images/spot_hologram_enter.webp`). `-tp 7445,17060 -heading 0`: the `House_To_Buy` rhombus 134 m down the beach. `-logmarkers` prints each hologram's canvas position.

### Follow-up (same session, owner asked for three fixes after a retail comparison)
- Plate size x1.5, scaled about its centre (`Position.y -= (Scale-1)*12`) so it stays at the same height; the movie's bottom strip (#10) and its red area icon / status clips are hidden in ENTER and a plain dark band is drawn (hiding depth 19 / 200 removes the bezel - left alone; the white car icon stays, retail shows a faded one). Not fixed: the pointer cone is on the right, retail has it lower-left - `Placement.bMirror` (negative X scale) makes the plate vanish (mirrored-cull path), left off. Retail screens of each style from the 360 recomp would settle the frame colours too.

### Retail captures from the 360 recomp (owner, 2026-11-14)
- Racing School, Real Estate and shops: the **blue square** (bracket on the right, pointer lower-left, aimed at the building); the owned caravan: the **red square**; an Event: the **circle** with an orange pointer (crescent on the left). Owner's recollection, unproven: clubs red, dealers blue. The plates read small far away and grow near.
- Applied: `HologramFrameFor` follows them; the backplate is now drawn from behind (`-holofront` = old side) which gives the retail bracket / pointer side without a mirrored matrix (the negative-scale attempt vanished). Not done: the pointer does not yet swing toward the entrance (retail aims it at the door); club / dealer colours wait for the proof.

- Second batch (dealers, a casino, a green circle): dealers and the casino are **blue squares** too. The plate floats to one side of the entrance and the pointer hangs on the door side (bracket on the far side): now a per-spot hashed side, 7 m perpendicular to the line of sight, pointer toward the door (checked by eye on the B-Miami shop, `-holoflip` = the other side). Casino / gambling hall no longer green. Unidentified: a green circle with a car + down arrow, and a faint grey square with a white bag-like icon (owner to name them).

- Third batch (map capture): the green circle is the **Keep your distance** multiplayer challenge (`KYD_Coop`), the orange / red circles the speed / race multiplayer ones (`Speed_Multi` / `Race_Multi` / `Radar_Multi`) - online content, no spots yet. An **unpurchased club is blue**; so the rule is red = owned, blue = for sale / shop (`Club` -> blue, `My_Club` -> red once clubs can be owned). The TDU World owned-club check is not possible (server down).

- Fourth batch (owner, 360 recomp, driven to each spot): house for sale (house + $), Car Wash, Clinic (surgery), Stickers shop - all the same plain blue square (grey at night); Online Challenge - the orange flag circle (= `Event`); Community Racing Center - a speech-bubble "S!" plate with no frame (not mapped; model not identified); owner thinks the hair stylist is blue too. A house bought in the recomp kept its blue "$" look - possibly the recomp not refreshing, so red = owned stays based on the caravan only. These agree with the current `HologramFrameFor` (blue default, red `Playerhouse`, circle `Event`); no code change.

- Speech-bubble plate found (`-spotholotest <i> -spotholocount 1 -spotholoy -1`, one model at a time): the **"$!" bubble is model 35 `GamblinHall`** - i.e. the gambling hall's own plate, which the map category 0x06 (`EGAMHA`) already maps to it; the owner saw it beside a "Community Racing Center" sign, so that is probably a gambling hall, not a racing centre. Other icons identified: `KYD_Coop` = car + down arrow (Keep your distance), `FTL_Coop` = green torch, `Race_Multi` = chequered flag, `Speed_Multi` = striped car, `Cup` = trophy, `DLC_Event` / `Event` = bomb-like icons (orange / pale blue), `Spot_Photo` / `Photograph` = camera. `Casino` (40) is likely the cards + dice (the owner's capture); the cards + dice plate was seen at a gambling spot - worth re-checking which of 35 / 40 it is.

- Championship plate (owner's capture "C4", a **blue rhombus**): championships wear `Rhombus_Blue_Frame`, and the plate is `Championship_<Asphalt|Dirt|Classic>_0N` where N is the championship's vehicle class number (the log's "class C4" for the classic series = the capture; A7 asphalt -> `Championship_Asphalt_07`, B4 dirt -> `Dirt_04`). `Championship_Classic_04` is chosen for the Sant Antoni series (log: nearest 'Championship_Classic_04' at 73 m). Not seen drawn in the world: the headless `-tp` to that series' start (12560, 11100) did not stream the ground in 900 frames, so only the model choice is verified.

- The owner's championship-screen captures (Sant Antoni, C4): the movie that draws them is **MAP.vmf** (the world map's own movie; `/championship` Information panel + `/tool_tip` race pop-up), not CHALLENG.vmf - `docs/re/championship_screen_spec.md` has the clip structure, text variables, FSCommands and our gaps; timeline labels of those clips are the next dump.

- **The championship screen is wired**: `MapScreen::openContest` + `GMFreeRide::openChampionshipScreen` open the world map in the retail contest view (header with the C4 diamond, Information panel, the bottom bar with Give up, a race bubble per race, Up/Down + OK enters, Back leaves; `-oldchampmenu` = the native list). Built from the movie's own disassembled AVM1 (`-dumpmapfuncs`, `-dumpclipfuncs`, `-dumpclabels`). 0 validation errors; the race starts from the map. Details and what is left (extended pop-up, pins, results tab, give up): `docs/re/championship_screen_spec.md` 4, `docs/images/championship_screen_map.webp`.

- Championship screen follow-up: the **extended pop-up** (first OK = `tool_tip.Extend(1)`, rules in `/:GMDescription`, second OK enters, Back = `Extend(0)`) and the **race pins** (`/pack/point` clones, selected one ringed) are in. The owner's new captures (pre-race menu "Start / Change vehicle / Leaderboard" with the Restriction / Reward / Distance / Rules panel, the "Change vehicle" list with the Lancia Delta stats, the Solar Crown dialog + camera pans before the start, the grid order: back first, then front) are not implemented - note for the next step. The pale-blue bomb hologram is the offline quest "Drop me at the driving school" (a light-blue circle frame, now `Circle_Blue`); the orange circle is the online challenge.

### [INF] / not decoded (be honest)
- Which spot wears which backplate colour/shape, the plate offset toward the viewer, the distance scaling and the 0.45x zoom are read off one retail capture + colour sense; the `entities.xmb` `Type` conditions that select SpotMesh / SpotMeshAddOn in the retail game are not decoded (raw words in `-logspotholo`). The retail header / strip layout was matched by eye.
- The `Arrow_*` / `Pulse` UVA scroll animation is bound (`bindUVAnimations`) but the plates look static in screenshots; not checked in motion. Not run with the validation layer. Owned-house `My_Club`, `DLC_Event`, `Dual`, `Radar_Multi`, `Speed_Multi`, `Spot_Photo`, `FTL_Coop`, `KYD_Coop`, race checkpoints are unmapped (no spot kind drives them yet).
- The ENTER strip tile is the old 2D art at 50 % (retail's strip icon not separately identified); the road-name label / route line of the retail minimap in this state are not reproduced.

### Next
- Run the headless drive with `-vk_enable_validation_layer`, check the plates' motion, and decode the `Type` conditions (`spot_holograms.cpp`, `TypeRaw`) so each spot picks its frame by data.

## Session 2026-11-14 (`main`, 9th): the Solar Crown arrival - the red carpet and the walk are on the stairs

The owner: "in the scene where you drive tess to the solar crown the walk up the stairs and the carpet aren't aligned correctly".

**Reproduced** (carpet a stair-width left of the house stairs, Tess and the player walking up the carpet's line, off the steps):
    build-main\bin\opentdu2-main.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -storystep 5 -nopad -fixedtimestep -playername Danny -autoplay "100:shot=...;330:shot=..." -exitframe 430

### What changed
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - `Setup::SceneLocalOffset`: the scene frame (`sceneOrigin`) is moved along the spot's local axes while the exterior bank stays at `spotOrigin`; scene-bank props named `door*` (`Prop::bKeepSpotFrame`) stay on the house (they copy the exterior's gate leaves).
- `source/game/cine/gm_cine.cpp` - kind 2 (C_SOLAR_CROWN_E) sets the offset (0, 0, +2.45). `-nosolarcrownoffset` = the A/B.

### Why 2.45 [DATA]
- The `ech_ia` exterior's stair strip, probed with `EdenModel::raycast`, spans spot-local z 0.05..3.55 (centre 1.8; the gate's two leaves are centred on 1.75).
- The scene bank is centred 2.45 m away: 'Carpet' (entities.xmb) at z -0.67 (width 1.8), Tess / the player's tracks z -0.4..-1.6, the two guards at z -1.86 / +1.13, the car's gate entry z -0.6. They agree with each other, not with the house, and the house's heights agree with the tracks in x (the terrace -0.2, the top 0.9) - so it is a pure z offset. The retail shows the carpet up the middle of the stairs.
- The reason the retail frames differ is [ENC]/unknown (the C_SOLAR_CROWN_E Entity__Cinematic sits at (21.29, 0, -48.56) turned 180 degrees; ECH_IA_1243I's second spot transform is local (-81, -2, 2.77)) - the offset is a measured fix [INF], not a decoded rule.

### Verified
- Frames 100 / 250 / 330 / 420: the carpet runs up the middle of the stairs between the lamps, the guards flank it, the pair walks on it, the door poster now shows full-frame (`docs/images/solar_crown_ext_carpet_before_after.webp`, before left / after right).
- Log: `IntroCinematic: scene frame offset (0.00, 0.00, 2.45) from the spot frame`.

### Not done / next
- Not run with the validation layer; the scene's first seconds (the car through the gate) were not looked at after the shift - `-autoplay "20:shot=..."`. The cin_viewer tool plays the same cinematic without the offset.
- The other cameras / shots of C_SOLAR_CROWN_E were only spot-checked at four frames.

## Session 2026-11-14 (`main`, 8th): the upscalers tested in motion (scripted drive)

The owner: "Test each in motion". I cannot watch a live screen, so: the same scripted drive under every upscaler, 3 frames 3 apart while turning at speed, compared to a native 100 % render of the same frames.

### Method
`options.ini` per run (`render_scale` 67 except native = 100; `upscaler` 0 bilinear / 2 FSR 1 / 3 FSR 3 / 4 XeSS / 5 DLSS), `-gm FREERIDE -skipcine -nopad -nowatermark -fixedtimestep -vk_enable_validation_layer -autoplay "200:hold=accel;500:hold=steerleft;800:shot;803:shot;806:shot" -exitframe 815`. Metrics in a scratch script (`_m.py`): PSNR against the native frame, over the full frame, the scenery above the car and the car box; mean absolute change between the first two frames (flicker + real motion).

### Results (mean of 3 frames)
| upscaler | PSNR full | scenery | car | change a-b |
|---|---|---|---|---|
| bilinear | 35.22 | 37.82 | 30.00 | 4.25 |
| FSR 1.0 | 35.24 | 37.82 | 30.01 | 4.54 |
| FSR 3 | 33.78 | 37.03 | 29.70 | 3.89 |
| XeSS | 34.54 | 37.99 | 30.43 | 4.01 |
| DLSS | 34.96 | 37.53 | 30.41 | 4.05 |
(native itself: 5.38). 0 validation errors in all six runs.

### Verified / read honestly
- `docs/images/upscalers-motion-car-sheet.png` (native, bilinear, FSR 1, FSR 3, XeSS, DLSS, same frame): **no ghost trail or smear behind the car or the lamps in any temporal upscaler**; the car body, plate and exhausts hold up, the oncoming car's lights read as native. The per-object velocity pass is doing its job on the car.
- The temporal upscalers are within about 1.5 dB of the cheap ones against a pixel-exact reference, with XeSS / DLSS best on the car box and FSR 3 slightly lowest (PSNR rewards blur, and the runs are not frame-identical: the world state drifts a little between runs, so a sub-pixel shift costs more PSNR than a blur does).

### Not verified
- Real time playback: flicker over many frames, shimmer on grass / foliage, the rain windscreen, the driver / passenger avatars and glass (still camera-only vectors, not in this crop), the cutscenes. These still need eyes on a real screen; the single next check is to run the same autoplay with `-gm FREERIDE` in rain (`-weather`-type flag if any) or at the sticker shop car, and compare the crops.

## Session 2026-11-14 (`main`, 7th): NVIDIA DLSS in the Upscaling filter (optional NGX plugin, restart to apply)

The owner approved downloading the DLSS (NGX) SDK files and running `nvngx_dlss.dll` (this machine has an RTX 5070 Ti).

### What changed
- `source/render/upscalers/dlss_upscaler.{h,cpp}`: NGX Vulkan through the static `nvsdk_ngx_d.lib`: instance / device extension queries (before creation), `NVSDK_NGX_VULKAN_Init_with_ProjectID`, optimal-settings preset pick, `NGX_VULKAN_CREATE_DLSS_EXT1` / `NGX_VULKAN_EVALUATE_DLSS_EXT` on colour + R32F depth + velocity + jitter. Compiled to a stub unless `third_party/dlss-sdk/{include,lib}` exist (CMake `OTDU_HAVE_DLSS`); SDK + DLL gitignored (licence).
- `render_device.cpp`: with `upscaler = 5` / `-dlss` the instance and device add NGX's extensions; the device also enables `shaderStorageImageWrite/ReadWithoutFormat`; render target images gain `TRANSFER_DST`.
- `EdenSceneView::runDlss`; Upscaler 5 in both menus; jitter / velocity run for upscaler >= 3.

### Verified
- `upscaler=5`, 67 %, `-vk_enable_validation_layer`, accel + steerleft, 820 frames: `[dlss] feature requirement: supported ... feature created: render 858x482 -> 1280x720`, **0 VUIDs, 0 [ERROR]** after two fixes the validation named (the clear needed `TRANSFER_DST`; the shaders needed storage-image-without-format). `docs/images/dlss-67pct-driving.png`: clean and sharp, HUD native.

### Not done / next
- Not eyeballed in motion (same open items as the other temporal upscalers: avatar / glass vectors, rain path). The jitter sign check is weak for DLSS (115 vs 109), it is kept the same as FSR 3 / XeSS.
- Quality-mode presets, DLSS frame generation / ray reconstruction, and a live (no restart) switch to XeSS / DLSS are not done.
- The launcher / release could ship the vendor DLLs (their licences allow binary redistribution with conditions): not done, the repo stays free of them.

## Session 2026-11-14 (`main`, 6th): Intel XeSS in the Upscaling filter (optional plugin, needs a restart)

The owner approved the XeSS SDK download (v3.0.2, 77 MB) and running `libxess.dll`.

### What changed
- `source/render/upscalers/xess_upscaler.{h,cpp}`: loads `libxess.dll` by `GetProcAddress` (no import lib), context + `xessVKInit` + `xessVKExecute` on colour + the R32F depth copy + velocity + jitter; compiled to a stub when `third_party/xess-sdk/inc/xess/xess_vk.h` is absent (the SDK is local only, gitignored: Intel's licence).
- `render_device.cpp`: with `upscaler = 4` / `-xess` the instance asks for Vulkan 1.3 (what XeSS needs, if the loader has it), the device adds XeSS's extension + feature chain (`VkPhysicalDeviceFeatures2`, `pEnabledFeatures` null), and the engine's EXT feature structs are folded into XeSS's core 1.2 / 1.3 ones (the validation rule: one struct per promoted feature). A failed `vkCreateDevice` is retried the plain way.
- `EdenSceneView::runXess` + shared `prepareTemporalInputs` (output target + depth copy, also used by FSR 3); Upscaler 4 in both menus; jitter / velocity run for any upscaler >= 3.

### Verified
- `upscaler=4`, 67 %: `[xess] needs Vulkan 1.3 ... device created with the XeSS requirements ... context created: render 858x482 -> 1280x720`, preset 104 (input 854x480), 820 frames driving, **0 validation errors** (the first run had 3 device-creation errors from duplicate promoted-feature structs; fixed by the fold). `docs/images/xess-67pct-driving.png`.
- Jitter sign: static scene, Laplacian variance normal 60.8 vs `-xessjitterflip` 38.1 (FSR 3 71.0), so the unflipped sign is right (same convention as FSR 3).

### Not done / next
- Still not eyeballed in motion; same open items as FSR 3 (avatar / glass vectors, rain path). Choosing XeSS in the menu needs a restart (no live device re-creation); the plain upscale runs until then, with one log line.
- DLSS: NVIDIA's SDK (NGX / Streamline) is the same shape (DLL + Vulkan extensions asked before device creation); needs an RTX card and the user's own download.

## Session 2026-11-14 (`main`, 5th): AMD FSR 3 in the Upscaling filter (optional DLL plugin)

### What changed
- `source/render/upscalers/fsr3_upscaler.{h,cpp}`: loads `amd_fidelityfx_loader_vk.dll` (exe folder, then `third_party/fidelityfx-api/bin`), creates an upscale context (`ffxCreateContextDescUpscale` + the VK backend with our device / `vkGetDeviceProcAddr`) and records `ffxDispatchDescUpscale` into the frame's command buffer (`getCommandBuffer()`, so the renderers drop their cached bindings). Headers vendored in `third_party/fidelityfx-api` (MIT sources; the signed DLLs are gitignored, binary-only licence).
- `EdenSceneView::runFsr3`: colour + an R32F copy of the depth (`depthcopy.frag`: the backend views depth as R32F, our D32S8 image failed validation) + the velocity buffer + jitter; output `pFsrTarget` (window size), presented by `draw()` exactly like FSR 1 (the final pass does no second upscale / sharpen).
- `render_device.cpp`: enables `shaderInt16`, storage image extended formats and `VK_KHR_compute_shader_derivatives` when supported (FSR 3's shaders declare them). `-jitter` / the velocity pass now also turn on whenever `Upscaler == 3`.
- Menu: Upscaling filter has a 4th value, "AMD FSR 3 (temporal)" (F2 Advanced page and the Image settings row).

### Verified
- `options.ini` render_scale=67 upscaler=3, `-gm FREERIDE -skipcine -nopad -nowatermark -fixedtimestep -fsr3debug -vk_enable_validation_layer`, accel + steerleft, 820 frames: `[fsr3] context created: render 858x482 -> 1280x720`, **0 validation errors, 0 [ERROR]**; the frame is clean and sharp (`docs/images/fsr3-67pct-driving.png`, HUD at native resolution).
- Fallback: DLLs moved away -> one `[WARN] ... FSR 3 is unavailable`, bilinear, no crash.
- Validation caught (and I fixed) three things: depth/stencil aspect + R32F view of a D32S8 image, the missing R32F format mapping, and the two missing device capabilities.

### Ruled out
- Passing the combined depth image straight to FSR: invalid views (aspect / format), hence the R32F copy.

### Not done / next
- Not eyeballed in motion on a real screen (only stills): ghosting on the avatar / glass (camera-only vectors), rain / windscreen path, and the reactive / transparency masks (none given) are the first things to look at.
- Quality-mode presets (render scale picked from the mode) and frame generation are not done. The jitter sign matches the SDK's convention by construction; a doubled-edge look would mean flipping `m[2][0]` / `m[2][1]` in `setupScene`.
- XeSS and DLSS: same inputs (colour, depth, velocity, jitter); XeSS has a Vulkan SDK, DLSS needs the user's own NVIDIA DLL.

## Session 2026-11-14 (`main`, 4th): per-object velocity - the car and traffic carry their own motion

### What changed
- `ePass::Velocity` + `EdenSceneView::runObjectVelocity`: the moving meshes (player car, traffic, police, opponents - `gm_freeride.cpp`'s draw callback returns right after them in this pass) are redrawn through a new `EdenRenderer::setDrawHook` into `pObjectVelocityTarget` (RG16F + its own depth) with `velobject.vert/frag`: position stream only, clip position of this frame and of the previous frame (the mesh's previous world + the previous camera, both unjittered); RG = previous uv - current uv.
- The previous world of a mesh is the nearest (< 25 m) world that same `EdenMesh*` had last frame (traffic cars share meshes); wheels are separate meshes with spin in their node transform, so spin is in the vector too.
- `velocity.frag` takes the object vector where the object is the visible surface (linear distance <= scene distance + max(5 cm, 0.4 %)).
- Skipped on purpose: skinned meshes (avatars, hardtop skin), blended and depth-write-off materials (glass), non float position formats. `-noobjvelocity` turns the pass off (A/B), `-logvelocity` prints the tracked mesh count (173 on the opening drive).

### Verified
- `-velcheck` A/B, accel + steerleft, frame 900, mean error in the car box (x 450-830, y 345-585): object pass OFF warped 0.0184 vs raw 0.0139 (the car was warped as static world: worse than doing nothing); ON warped **0.0139** = raw (the car is nearly still on screen: no regression). World box unchanged (warped 0.0057 vs raw 0.0138). 0 VUIDs with `-vk_enable_validation_layer`. `docs/images/velocity-objects-reprojection.png`.

### Not done / next
- The driver / passenger avatars and the hardtop skin are skinned: still camera-only. Glass is skipped (it reads the body's vector behind it once the buffer is dilated by the upscaler).
- Alpha-tested meshes are drawn solid (grilles). Aircraft, pedestrians and the rain / spray effects are not in the pass.
- Next: the FSR 3 glue (FidelityFX API: colour, depth, this buffer, jitter offsets and the MV scale; verify the jitter sign), then XeSS, then DLSS.

## Session 2026-11-14 (`main`, 3rd): the velocity buffer passes its reprojection check

### What changed
- `-velcheck` (`runVelocity`, `velcheck.frag`): keeps last frame's final image, warps it with the velocity buffer and writes into the final image R = |cur - prev(uv+v)|, G = |cur - prev(uv)| (no compensation), B = |cur - prev(uv-v)| (opposite-sign control). Run without `-jitter`.

### Verified
- `-gm FREERIDE -skipcine -nopad -nowatermark -fixedtimestep -velcheck`, mean error over the static scenery above the car (x 330-1000, y 130-430), frame 900: driving straight (`accel`): warped 0.0053 / raw 0.0066 / opposite 0.0089; driving and turning (`accel` + `steerleft`): **warped 0.0075 / raw 0.0150 / opposite 0.0190** - the buffer halves the error and the wrong sign is worse than no compensation, so sign and magnitude are right. `docs/images/velocity-reprojection-check.png` (R warped / G raw / B opposite): the static world is near black; the car's outline and the oncoming car light up, which is exactly the per-object gap.
- Absolute means are the 8-bit swapchain values (not linear); only the ratios matter.

### Not done / next
- Per-object velocity (the car and traffic currently read as static world). Jitter sign vs FSR 3 still unproven. Then the FSR 3 glue.

## Session 2026-11-14 (`main`, 2nd): temporal upscaler groundwork - jitter + camera velocity buffer

Continuation of the FSR work (owner: "Yes, start it").

### What changed
- `eden_scene_view.cpp` `setupScene`: `-jitter` adds a Halton(2,3) sub-pixel offset to `params.Projection` (`m[2][0]`, `m[2][1]`; phases 8*(display/render)^2); the unjittered view / projection are kept in `currentView` / `currentProjection`.
- `runVelocity()` + `resources/shaders/velocity.frag`: from the scene depth rebuild the view position, carry it by `projPrev * viewPrev * inverse(viewCur)` (all unjittered) and write previous uv - current uv into an RG16F target. `-showvelocity` paints it into the final image (`velocityview.frag`), `-logvelocity` logs the jitter.
- Everything is behind flags: default frames are untouched.

### Verified
- Parked: the buffer is uniform grey (|v| < 0.0005). Driving (accel held): `docs/images/velocity-debug-drive.png`, bottom pixels vy < 0 (the road came from nearer the horizon), right edge vx < 0 (outward flow) - the expected signs. 0 VUIDs with `-vk_enable_validation_layer`.

### Not verified / limits
- The magnitude was not checked by a reprojection (warp frame N-1 with the buffer, diff with frame N): that is the first check to do. The car and every moving object read as camera motion only (their own previous transforms are not in the buffer: ghosting on cars until per-object vectors exist). The jitter sign convention vs FSR 3's `jitterOffset` is unproven (flip `m[2][0]` / `m[2][1]` signs in setupScene if the upscaler shows doubled edges).
- Shadow cascades, the occlusion probe and the RT AO use `params.Projection` (jittered under `-jitter`): not audited.

### Next
1. Reprojection test. 2. Per-object velocity: mesh vertex path writes a second colour attachment from previous-frame world matrices (car body / wheels first, then skinned, instanced). 3. FSR 3 via the FidelityFX API (the SDK clone is at third_party/_ffx_tmp if still present) fed colour + depth + this buffer + jitter; then XeSS, then DLSS (optional user-supplied DLL).

## Session 2026-11-14 (`main`): AMD FSR 1.0 in the Upscaling filter

The owner: *"add FSR 3, DLSS and XeSS options ... DLSS and FSR help enhance graphics a bit while also helping some with performance."* Agreed scope: FSR 1.0 now, temporal groundwork after.

### What changed
- `third_party/fidelityfx-fsr1/gpu/` - AMD's `ffx_fsr1.h` + the core headers (MIT; the licence is in each header), copied from FidelityFX-SDK `7539ae8`.
- `resources/shaders/fsr1.hlsl` - EASU and RCAS pixel shaders (glue only). `tools/compile_shaders.py` gained an HLSL path (dxc, entry renamed `main`); outputs `source/render/shaders/builtin/fsr1_{easu,rcas}_frag.h`.
- `eden_scene_view.cpp` `runFsr1()` - at the end of `renderOffscreen`: EASU (render res -> window) into `pFsrEasuTarget`, RCAS into `pFsrTarget` when Sharpness > 0; `draw()` presents it and zeroes the final pass's own upscale / sharpen so nothing is applied twice. Constants are `ffxFsrPopulateEasuConstants` / `FsrRcasCon` evaluated on the CPU.
- `Upscaler = 2` "AMD FSR 1.0" in the F2 Advanced page and the Image settings row (`advanced_graphics.cpp`, `pause_menu.cpp`).

### Verified
- `options.ini` render_scale=67, upscaler 0 / 2 / 2+sharpness 5, `-gm FREERIDE -skipcine -nopad -nowatermark -fixedtimestep`, shot at frame 300: 0 `[ERROR]`, no VUIDs; `docs/images/fsr1-ab-crops.png` (left bilinear, middle FSR, right FSR + RCAS) and `fsr1-67pct.png` (full frame, HUD sharp at native res).

### Ruled out
- Compiling `ffx_fsr1.h` as GLSL: this SDK version ships no `ffx_core_glsl.h`; the HLSL route through dxc works.
- First build logged `vkCreateGraphicsPipelines failed (-13)`: the dxc entry point was named EASU/RCAS, the pipeline expects `main`.

### Not done / next
- FSR 3 / XeSS / DLSS are temporal: need (1) sub-pixel jitter in `params.Projection` (eden_scene_view.cpp:635), (2) a velocity pass (camera motion from depth + previous view-projection, then per-object previous transforms for cars / skinned meshes), (3) the SDK glue. DLSS = optional runtime load of the user's own NVIDIA DLL.
- Per `docs/re/advanced_graphics.md`, the frame was CPU bound at 100 % scale (52 ms at 50 / 100 / 150 %): upscaling raises the frame rate only where the GPU is the limit; the owner's quality point (FSR edges vs bilinear) is visible now.
- Eyeball FSR on motion and in rain (windscreen path) on the owner's machine.

`## Session 2026-11-14 (Driving Tess + the avatar in the car, `main`): step 4 is playable and the driver / Tess are seated

The owner (fresh save at `story_step = 3`, launcher): *"I have no avatar in car ... meant to drive tess to the solar crown but neither of us end up in the car and that mission it self doesn't seem to really happen as there's normally a marker outside and your on a timer to drive there."* Also: odd graffiti on building windows (screenshot, build 43).

**Reproduced** - the old flow ended the dream at step 3 with only the goal "On the road to Solar Crown", a guessed GPS target, no timer / gate / passenger, an empty open convertible:
    build-main\bin\opentdu2-main.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata <ud with the owner's profile.sav> -gm FREERIDE -skipcine -nopad -fixedtimestep -storystep 2 -storytime 3 -clock 3.3 -autoplay "7000:shot=..." -exitframe 7100

### What changed
- `game/freeride/gm_freeride.{h,cpp}` - **`loadOccupants`**: the profile avatar at the driver seat (`SeatedAvatar`, avatarsdriver.bnk, seat IK; GMTestCar's pattern), updated / lit / drawn each frame, head hidden in the cockpit view (camera mode 23), reloaded on a car swap; `-noseated`. Commit `ed0c61b`.
- **`startTessMission` / `tickTessMission` / `finishTessMission`** (inside FREERIDE, so the traffic, weather, night lights stay): story step 4 "Driving Tess" (story_spec 4 of CamilleKMI `335759a`, TESSEVEN, DBEvent 21). `ig2/tesseven.ig2` is read with the existing `Ig2Challenge` ([DATA]: finish gate (9957.2, 9.8, 14856.5) = spec 4.2 #1, `timG` 160 s, `CtDw` 210 s). Goal loc 4032, GPS route + minimap target, the finish gate drawn with `RaceGates` (Finish arch), the HUD countdown (`setTestDrive` + `setCountdown`, the test drive's widget) from `CtDw`, Tess (`STO_Tess_Phone`, DBEvent `_pnj1`) as the passenger, crossing the gate plane (or < 12 m) -> story step 5 -> CINE kind 2 (`C_SOLAR_CROWN_E`). A save at step 3 is upgraded to 4. `-notess` = the old free drive, `-tesstime s`.
- the dream hands over at step 4 (`StoryStepAfter = 4`, the `-nodream` cut too); `-storystep 4` starts FREERIDE (the `GM_Mission` mode is not registered); `game/cine/gm_cine.cpp` step 3 end hands the car to FREERIDE instead of the unregistered `GM_Mission`.

### Verified
- the owner's path headless (FREERIDE step 2 -> dream -> FREERIDE): log `story step 4 Driving Tess: finish gate (9957.2, 9.8, 14856.5) width 7.0, route 3983 m, time limit 210 s (timG 160000 ms, CtDw 210000 ms), passenger Tess`, screenshot: goal text, minimap route, countdown 02:46 top right.
- arrival: `-tp 10006.6,10.2,14906.1 -heading 224.9` + `hold=accel` -> `Driving Tess done with 203.7 s left -> story step 5`, profile saved at 5, `CINE: kind 2, C_SOLAR_CROWN_E`.
- daylight chase shot: the driver's head over the left seat, the cockpit view shows the hands on the wheel; Q7 / H3 / bike load without errors.

### Ruled out
- upstream's whole `GMMission` (a second driving mode of 675 lines duplicating the freeride's driving, without our traffic / night / weather): not ported, only its data and flow; its `IgeFile` was not needed (`Ig2Challenge` parses the file).
- upstream says "no countdown for this event" ([INF], /:ShowCountdown only in state 9); our own files carry `CtDw`, the same key the licence tests use as their time limit, so the countdown follows it. Whether the retail fails at zero is ENC: the port stops the clock at 0 and lets the drive go on.

### Not done / next
- Tess's reactions (`EventReactions` of upstream: toplayer / phone / impact clips and voice lines, 40 s / 25 s timers), the C_TRUCK props at the venue, the results screen, the start gate placement (the car stays where the dream left it), a failure at 0 s (needs the owner's memory of the retail).
- Tess is seated low (only a smudge above the right headrest at night / day, not verified from the side); the steering input does not turn the driver's arms.
- **Graffiti on the building windows: not reproduced** (a hotel spot render and the route start show nothing). Needs a location: F2 (coords overlay) + F6 (writes `_marks.txt`) at the spot; then `-uselodtextures` A/B and `-verbose` there (leads in `docs/state/cline.md`: the LOD alias rule, `TextureCache::findForOwner`'s global hash fallback).

## Session 2026-11-14 (`main`): launcher revamp + profile manager (several saves / avatars)

Owner: "a profile loader for multiple saves ... different avatars and saves ... the update buttons look cut off ... fully revamp the launcher".

### What changed
- Game: `-savefile <path>` (`source/game/player_profile.cpp`) - one profile.sav per profile. LOGIN's save check now requires `StoryStep >= 1` (the New profile dialog writes a step-0 save first).
- Launcher: new `launcher_profiles.{h,cpp}` (scan / create / rename / duplicate / delete-to-trash / backups + restore); Play page = animated hero, glowing PLAY "as <profile>", profile tiles, collapsible launch options; Profiles page replaces Player; sidebar with drawn icons, update badge, active-profile card, page fade-in. Cards auto-size (the cut-off Updates buttons).
- PLAY backs the save up first, then passes `-savefile` / `-playername`.

### Verified
- `otdu2_launcher.exe --selftest` PASS; screenshots of Play / Profiles / Updates (scratch `_ltest`, DPI 225 %); game run with `-savefile` on a populated profile -> `progression found`, on an empty one -> new-game path, twice (no skip after the dialog's step-0 save).

### Not done / next
- Not clicked through live: rename / delete / restore modals, a real PLAY from the launcher, the Online / Game data pages after the card change.
- Pressing OK on the no-data popup still starts AVATAR_CHOICE over the *main* save (backups now cover it).

## Session 2026-11-14 (`main`): LOGIN continues an existing save instead of the "no progression data" popup

Owner: the launcher shows my save, but starting the game keeps showing "No TDU2 progression data could be found".

### What changed
- `source/game/bootmenu/gm_login.cpp` (LS_SaveCheck) - the check always showed the popup, never looking at `profile.sav` (which `LoadProfile()` had already read). Now, when a profile was loaded, it logs `LOGIN: progression found` and goes to FREERIDE (retail FUN_009c9180: loaded progression -> no popup, no AVATAR_CHOICE). `-newgame` forces the old popup. [INF: the port's save is `<userdata>/profile.sav`; the retail backend is [ENC].]
- `player_profile.{h,cpp}` - `IsProfileLoaded()`.

### Verified
- Run with a copy of the real save (story step 3): popup skipped, FREERIDE entered.

### Not done / next
- Pressing OK on the popup (no save) still goes to AVATAR_CHOICE, whose "gain" sets StoryStep=1 and saves over an existing profile.sav - a new game overwrites the old save with no backup.

## Session 2026-11-14 (TDU1 physics recon, `main`): tall-car rollover measured and reproduced; load-sensitive grip stops every flip (probe flag, off by default)

Owner: "port TDU1 physics from OpenTDU - hybrid, keep TDU2 physics". Written permission from the OpenTDU maintainer on their issue thread (no LICENSE file): credit `opentdu`; do not paste their code (`docs/re/tdu1_physics_recon.md` section 0).

- **Recon**: TDU1 tyre = same Pacejka family, but D is load dependent (`FUN_0043ceb0` / `FUN_0043cf50`, per-load peak tables, friction ellipse); ours is `mu x Fz`, linear. Their anti-roll is disabled, no help there.
- **Measured** (`-vehtest`, 19 cars): H3, ML63, Q7, Range Rover, Pekin to Paris flip in 5 of 6 steady corner runs (7 CSR 260 in 2); sports cars never. Mechanism: inner wheels unload in 0.17 s, grip 1.25 g > tipping limit.
- **Harness fix** (`gm_freeride.cpp`): the corner test spawned big-tyre cars 0.3 m in the road; now spawn height follows the radius + 1 s pre-roll (`-vehtestraw` = old).
- **Probe** (`vehicle_dynamics.cpp`): `-tyreloadsens k`, mu x clamp(1 - k (Fz/static - 1), 0.5, 1.25), default off. k 0.2: 27 flips -> 0; sports cars -10 % peak lateral g; 100-0 +4-5 %. [INF] k is not the retail law.
- **Not done / next**: (1) read the PS3 twin of the tyre D (`ps3_enc_twins.json`) to replace k with the real law; (2) check a kerb / bump rollover in-game with `-tyreloadsens 0.2`; (3) decide the default. Not committed.

## Session 2026-11-14 (party shadow flicker, `main`): a world-space shadow bias floor - flips on walls / decks / cloth 24 % -> 3 %

Owner: flickering shadows / lighting, mostly at the intro party (dancing; DJ decks, table cloth, building walls), faint in freeride.

- **Measured** (`-intro -fixedtimestep`, 16 consecutive frames at 2250 / 2000, `|shadows - -noshadows|` lit<->shadow state flips per pixel; `_emo/flip.sh` style, not committed): the shot configs PR / PA / PM (20 m far) give slice 0 a ~4 mm texel, so the config bias (x3 scale) is ~3.6 mm - below the depth error of a grazing wall - and every wall / deck / cloth face flipped between lit and self-shadowed as the dancers' casters changed the map. Pixels with >= 3 flips in 16 frames: **23.9 %**. `-shadowconfig DEFAULT` 5.1 %, `PG` 2.4 %, `-shadowbias 600/1200/2400` 6.6 / 4.1 / 3.1 % (but the shadowed area also halves: the acne counted as shadow).
- **Fix** (`eden_shadows.cpp`): the light-row bias has a floor in world units, `max( 3 x config x texel, 5 cm )`; `-shadowminbias <cm>` (0 = off). Result at 2250: flips **23.9 % -> 2.9 %**, at 2000 3.8 % -> 1.2 %. Character / ground shadows remain (`docs/images/intro_party_shadow_bias_before_after.webp`, left before).
- **FREERIDE**: frame 300 with the floor on vs off differs by mean 0.003 (the FREERIDE config bias is already above 5 cm): no change there. The faint freeride flicker is the animated-caster class of the 2026-10-29 sessions (cloth / vegetation vertex stage vs `TD_ShadowCaster`) - not touched.
- **Not done**: no slope-scaled bias (it would need the shadow shader); the dancers' own caster motion still changes real shadow edges frame to frame (that is animation, not flicker); the 5 cm floor can peter-pan contact shadows by that much; only 2 party windows measured - if a spot still flickers, `-shadowminbias 10` is the next lever.

## Session 2026-11-14 (forehead hair stroke, `main`): the dark hair-like stroke on the character-select face - fixed

Owner: "a weird hair like bug that clips through certain people". Found on the AVATAR_CHOICE male (frame 3100 of `-intro`): a dark curved stroke on the forehead above one eyebrow.

- **Traced** by A/B (`-hidematerial`): not FaceFX (`-nofacefx` identical), not `EYEBROW`, not the other hair layers - it is the `implant_front` / `Implant_front_n` TD_Hair hairline card (`hair_implant_front_nuque`, UV v up to 1.26). The slot is V = mirror (2DM byte 2), already applied since skin_zones_spec 1.7 #2 (wrap = the full black band); the mirrored rows 0.74..1.0 still hold hair texels -> the stroke. `-implantv 0/1/2` experiment: wrap = band, mirror = stroke, **clamp = clean** (reads the transparent bottom row).
- **Fix** (`eden_renderer.cpp` `setupMaterial`): V mirror -> clamp for materials named `*mplant_front*`. `-implantmirror` is the A/B. [EXP: the retail sampler state for the hair layers is [ENC].] Other implant cards (tempes / nuque / pattes / casque) were not changed - none showed a stroke in the frames checked.
- Evidence: `docs/images/intro_forehead_hair_before_after.webp` (left before, right after; the lighting differs because the left frame predates the weather-modifier fix).
- **Not done**: only the select-screen male and the party frames were looked at; other avatars with an affected face (the owner's "certain people") were not enumerated - the F6 marker (`_marks.txt`) next to such a face would pin the next one. **The lighting / shadow flicker was not worked on this session**: with a fixed camera in the intro pool walk (`-walkmode 1 -walkat "5,32.7,0,270"`, 12 consecutive frames) static surfaces are stable (per-pixel std only on animated characters and water), so it needs the owner's spot.

## Session 2026-11-14 (intro party shadows, `main`): the cinematic weather modifiers never applied - fixed

Owner: after the sea sweep the camera goes up to the party and nobody draws shadows until the select-character shot.

- **Cause** (`source/render/eden/eden_weather.cpp`, `WeatherLibrary::loadModifiers`): every one of the 276 entries of `islands/ibiza/fx/cinematic_modifiers.xmb` parsed with **0 values** - the schema accessor `param["param"]` has no type for an `Object` field, so every param was skipped. The base `Intro` weather has `sun_shadow_opacity 0`; the shots' `wmod:Intro_02..15` modifiers (intempery 0) set it to **1.00** (plus 7 more values) - never applied, so the whole party ran at opacity 0. Earlier sessions blamed "the data" (STATE.md session 48) and the shot shadow configs (PG / PR: ruled out again - `-shadowconfig AVATAR_CHOICE` on the party frame changes nothing; `-cineweather Avatar` brought the shadows back, which pointed at the opacity).
- **Fix**: the params are decoded with the raw Object layout the weather library already uses (type index x 2, data offset). Debug: `-logmodifiers` lists each modifier with its value count / opacity.
- **Verified** (build 36, `-intro -nopad -fixedtimestep -logshadows`, frame 1900): ShadowFit opacity now 1.000 for ~2700 of the ~6200 shadow fits (was 0.000 for all); ground / body shadows on the party frame - `docs/images/intro_party_shadows_before.webp` vs `intro_party_shadows_fixed.webp`. The other modifier values also apply now (the party light is warmer), so every cinematic that uses `wmod:` (intro, title scene) shifts toward the retail look.
- **Not done / open**: the sea sweep and shots without a modifier keep the base opacity 0 (retail data: there is no `Intro_01`); no side-by-side with a retail capture; the title scene / other cinematics were not re-checked for the colour shift.

## Session 2026-11-14 (emote wheel, `main`): the retail emote UI replaces the ImGui panel

Owner: "I had emotes implemented but would like the proper UI if possible".

- The wheel is the `/emotes` clip of `GENERAL.vmf` (spec `docs/re/emotes_spec.md` section 5): `emote_wheel.*` drives it (open label, `avan:vali` plays the slot, `emcl` leaves, `wmon` closes), owned by `WalkMode` (`updateEmoteWheel` in the three hosts' `updateEmotes`, `drawEmoteWheel` in the FIRST_WM / InteriorWalk / CasinoInterior draws). ImGui picker removed.
- Controls: X (keyboard / pad emote action) opens; arrows / D-pad / left stick step the ring; Enter / A plays; Back / X cancels; keys 1..8 pick + play.
- Verified (build 35, FIRST_WM): `emop` -> `avan:seta` x2 -> `sndp:valid` -> `avan:vali` -> `wmon` -> `playing 'funny'` -> `self view out`; `docs/images/emote_wheel_garage.webp`, `emote_wheel_funny_playing.webp`.
- Not verified: the casino / freeride interior hosts only compiled (same code path); no pad run. Not done: mouse hover on the ring, hiding the walk HUD panels behind the wheel.

## Session 2026-11-14 (Advanced graphics page, `main`): a retail-template tab for the extra graphics rows

Owner: "expand these menus for graphics or some method that wasn't out of place" -> yes, build it.

- The Audio & Video column already carries a **Stereo** tab (MAIN.vmf `options/relief3d`, command prefix `ster`, locked: Stereo 3D is not implemented) - a clean retail page of 5 rows (3 choice boxes `mode3d` / `anaglyph3d` / `diagonal3d`, 2 sliders `depth3d` 0..15 / `resolution3d` 0..4; the ranges are hard-coded in the page's setParameters). It is now **Advanced graphics** (`PauseMenu::onAdvancedGraphicsCommand`): Sun shadows, Haze, Sun flare (choice), Shadow distance (slider 0..15 -> 50..150 %), Draw distance (slider 0..4 -> 50..150 %); labels / values are root variables (`m_StereoOp3d_`, `m_ModeOp3d_` ..., `/:mode3d` ...), `/:is3DMode` must be true for the page's own Move() (Up / Down), OK saves / Cancel restores like the other pages; LB / RB cycle audio -> video -> image -> advanced. Those 5 rows left the Image settings list, which keeps Gamma + 8 rows (Render scale ... UI layout, FOV scale scrolls in).
- Evidence (build 34, `docs/images/options-advanced-graphics.webp`, `options-image-settings-split.webp`): `stmo:scne` toggles shadows, the sliders log `stds:slid:updt` -> `shadows 0 @ 120 %` / `stre` -> `draw 125 %`, OK -> `options.ini` `shadows = 0`, `shadow_distance = 120`, `draw_distance = 125`.
- Debug: `/options/relief3d` clip dump via `-dumpoptionsall -dumpoptionsclips`. The 5th row's label sits close to the page's bottom mask (retail template). `menu_rows.ini` still drives the video / gamma pages only.

## Session 2026-11-14 (brightness slider + the Image settings overlay, `main`)

Owner: add the brightness slider to the picker; "the Image settings with gamma ... I think there is an overlay issue".

- **Slider**: the picker is now a hue x saturation square (white at the bottom) plus a brightness bar (the retail frames), a generated texture bound to `lure_grad` in 17 variants (the bar's handle); brightness = triggers (accelerate / brake) or Page Up / Down on the active colour; paint (two colours keep their own value) and the sticker colour tool. `docs/images/sticker_ui_brightness_slider.webp`.
- **Overlay root cause** (`pause_menu.cpp` extendOptionsRows): the cloned `bkgd<N>` strips (semi-transparent shadow, a=0.85) were placed at depth 250+slot, ABOVE the game rows at 200+slot, so they washed the rows out (the s76 shots already showed it). They go to 100+slot now. Plus the rows start 13 px lower on the Gamma page so row 1 no longer straddles the board frame. Shots `options-gamma-overlay-fixed.webp`, `options-gamma-rows-inside-panel.webp`; the treadmill still scrolls (`focus slot 7 ... [window scrolled]`). New debug flags `-dumpoptionsclips`, `-hideoptclip <name>`.
- **Expanding the graphics menus** (not done, proposal): a dedicated "Advanced graphics" tab on the Audio & Video column built from the Video page's own template (crisp retail rows, 7 per page) instead of 13 rows crammed under the gamma slider; `menu_rows.ini` already reorders / relabels.

## Session 2026-11-14 (sticker shop car in the room, `main`): the editor shows the car on the shop's turntable, not outside

The owner sent retail video frames: the car stands on the turntable inside the shop (STICKERS / CAR PAINT signs), pack bars read $10,000 / $13,000 and the invoice $1,000 paint + $23,000 stickers + $1,000 labour. Change: `InteriorWalk::placeStickerCar` parks a copy of the player's car on the room's `D_CAR` PosCam (own dealer + custom paint), `GMFreeRide` orbits a scene-local camera around it (`setLocalCamera`), binds the sticker canvas to it and hands it to `StickerShopUi` as the car the Paint page recolours; the outside cut stays only when no car place exists. My Colors now start empty (retail). Pricing already matched (per pack + $1000 labour + $1000 paint). Verified headless: `docs/images/sticker_ui_room_car.webp` (stickers on the room car), `sticker_ui_paint_in_room.webp`. Not done: the retail brightness slider beside the picker, `cam_new_car_*` cameras, the `sti_carready` arrival; the room car does not turn, and the walk's room view shows it parked while you stand at the seller.

## Session 2026-11-14 (sticker shop Paint page, `main`): the retail Paint screen recolours the car - finish, one or two colours, My Colors, paid and saved

The owner said yes to the open piece of the last entry: the Paint entry of the retail stickers menu.

**Reproduced / how to see it**: `build-main/bin/opentdu2-main.exe -game C:/TDU2-Gamedata/TestDrive2 -userdata <seeded profile> -gm FREERIDE -car One_77 -skipcine -nopad -fixedtimestep -interior ESTICK_B -nointeriorintro -walkaim StickersShop_area -autoplay "200:press=validate;340:press=validate;490:press=camera;560:hold=accright;600:release=accright;650:press=opt2;680:press=validate;760:press=validate;..." -exitframe 1400` (A on the seller, A on Paint; `camera` = L1/R1 key 51 = next finish, `accright` = numpad 6 = the stick, `opt2` = Y = bicolour). The California cannot be painted (`_isvalidcarpaintdeluxe` 0): use `-car One_77`.

### What changed
- `source/game/freeride/sticker_shop_ui.{h,cpp}` - `onPaintCommand` answers `stsh:pain:colo:open/vali/canc/clos/bion/biof/bich/type:*` and `pain:myco:*`; `tickPaint` moves the active colour's cursor, tints the picker cubes / My Colors, binds the gradient (the colour tool's helpers now take their root from `colourRoot()`: `/stickers/colors` or `/paint`); the page's two colours + finish live in `paintWork` (restore point `paintBackup`, committed `paintCommitted`); `pay()` saves them, `revert()` / `unbind()` put the car's paint back; the Paint entry unlocks from `_isvalidcarpaintdeluxe`. The finish name goes to `/:ColorType` (found with `-logstickerui`, which dumps `/paint`).
- `source/game/cinematic/cinematic_car.{h,cpp}` - `applyCustomPaint(type, colour1, bBicolour, colour2)`, `restorePaint()` (snapshot of the dealer paint, dropped by `applyDatabasePaint`), `getBodyPaintColour()`: the colors.2dm base material of the finish (`GLOSSY/MATT/METAL/CARBON`, FUN_00c62490's list [CODE]) copied like `applyDatabasePaint`, the colour on `RampClr2` (the body) with a lighter `RampClr1` [INF].
- `player_profile.{h,cpp}` - `OwnedCarCustomPaints` (`car_custom_paints = id:type;bicolour;c1;c2`); `gm_freeride.cpp` - `applyOwnedPaint` puts it over the dealer paint at spawn, the shop gets the car; `gm_shop.cpp` - a dealer purchase drops it.

### Verified (headless, build 31, 0 stalls, plain FREERIDE smoke exit 0)
- The page opens (shots `docs/images/sticker_ui_paint_*.webp`): Color type "Glossy", the cursor on the car's own blue (`body 52,77,111`), the car recolours live as the cursor moves; L1/R1 -> Matt -> Metal; Y -> the second cursor and cube, A -> colour 2, A -> Apply; the cart shows $1,000; Valid & Pay -> `paid 1000 (0 sticker(s) on car 431, custom paint), money now 93000`, `profile.sav` `car_custom_paints = 431:2;1;4282712175;4278218521`; the next FREERIDE logs `'One_77' in its custom paint (type 2, ff45006f + bicolour)` and the bonnet is purple metal.
- B on colour 1 -> `pain:colo:canc` -> the original blue is back, $0; a My Colors slot (X, right x2, A) makes the car red.

### Ruled out
- The page is not base / highlight / stroke (my earlier guess): the movie has one finish selector, one or two colours (bicolour) and My Colors.
- The finish is not on up / down: the keys are 49 / 51 (L1 / R1; `camera` / `backview` actions) - `press=right` did nothing.

### Not done / next
- The colour -> ramp mapping (`RampClr1` lighter by +42 / 0 / 30 / 25 %) is my choice [INF]; the retail one is [ENC] (FUN_008c8d20). The finish names are the base material names; the paint price is the flat $1000. `One_77` has no `Paint2`, so the bicolour only shows on cars that have it (the `DBCarsAndColors` `_color_id_2` cars): not verified on one.
- Unverified by feel: the DualSense (L1 / R1 = finish, left stick = cursor).
- Still open from the stickers entry: `C_NEW_CAR_DESIGN_I`, diary stats, casino / chase packs, DLC popup.

## Session 2026-11-13 (sticker shop UI, `main`): the ESTICK room runs the retail SHOPSCAR stickers page

The owner: *"Can you try implement the real ui for the stickers/vinyl editor in the shop it's only partially implemented with non real ui"* - the port's own `PaintMenu` panel stood in for the movie; `shopscar.vmf` holds the real thing (menu, pack grid, tool strip, colour picker, history, itemized invoice) and nothing answered its `stsh:` FSCommands.

### What changed
- `source/game/spots/interior_walk.{h,cpp}` - an `ESTICK*` room loads `UiMovie = "shopscar"` (`bStickUi`; `-nostickerui` = the old native menu), goes to `st_open`; A on the seller is `ItemType('valid')` (not key 13); `camn` / `wmof` / `wmon` gate the walk; the pad is routed to the movie (L1 / R1 = SWF 49 / 51, X / Y = 120 / 121, history = 6); `dbxo` / `dbxc` / `gofr` handled; every other FSCommand goes to a sink.
- `source/game/freeride/sticker_shop_ui.{h,cpp}` (new) - `StickerShopUi`: the working layout + undo / redo snapshots, the pack list (sold DBStickersBanks), 24 thumbnails through `setBitmapOverride(".../stN/lure")`, the tool values from the left stick, stamp / symmetry / flip, the colour picker (generated gradient on `lure_grad`, dropper cursor, live tint) and My Colors, the invoice (Labour $1000 + unpaid packs), pay / clear / cancel. Spec: `docs/re/stickers_ui_spec.md`.
- `source/flash/flash_player.{h,cpp}` - **opt-in `onEnterFrame` handlers** (`setEnterFrameHandlers`), run in the defining timeline (`Value::pClip` on defined functions): the pack grid's loading spinner waited on one forever.
- `gm_freeride.cpp/.h` - the sink, `bindStickerShop`, the editor's orbit camera (right stick / D-pad / right-mouse drag, wheel zoom) - the interior branch used to return before `view.setCamera`, so the old "cut to the car" never showed it; the movie over the car (`IntroCinematic::drawUi`); the EMOTES debug panel no longer outlives the room.
- `sticker_bank.*`, `player_profile.*` - `StickerPlacement` gained `Alpha`, `bFlip`, `YStretch` (profile line stays backward compatible), the canvas clears when the last sticker goes; `OwnedStickerBanks` (`sticker_banks`) and `StickerMyColors` (`sticker_colors`).

### Verified (headless, builds 27-30, 0 stalls)
- Walk-in from the world (`-tp 18873,26646`, A on the enter tile) and the test hook (`-interior ESTICK_B -walkaim StickersShop_area -nointeriorintro`); shots `docs/images/sticker_ui_*.webp`.
- Pay round trip with a seeded profile (`USERDATA` with `name = Tester`; test runs without a name never save): `paid 6000 (4 sticker(s) on car 387), money now 94000` -> `car_stickers` / `sticker_banks` / `money` in `profile.sav` -> next FREERIDE `'California' wears 4 sticker(s)`; the pack shows its checkbox on re-entry.
- B in the walk -> "Exit the shop?" -> OK -> `gofr` -> back in the car; Cancel -> `stsh:canc` -> same; history Undo removes the stamp; L1 / R1 switch the pack (3 / 15 "Vector/Graphic (monochrome)").

### Ruled out
- `sendKey(13)` on the seller does nothing - the `st_stop` buttons only carry the selection keys; `ItemType('valid')` is the A.
- A solid `setClipColor` alone does not tint the colour cubes: their `color` shape is a fully transparent solid placeholder, it needs the bound white texture too.
- The first dialog press is lost: the GENERAL dialog ignores keys for ~1 s while it animates in (not a bug).

### Not done / next
- **Paint** (`stsh:pain:*`, the base / highlight / stroke colours, bicolour): the Paint entry shows its lock. It reuses the same picker (`/paint/color_box`, `/paint/my_colors_box`); the custom colour needs the TD_CarPaint2 colour params (`paint_spec.md`). Start at `StickerShopUi::onCommand`.
- The retail `C_NEW_CAR_DESIGN_I` car presentation; `EdCarSticked` / `NbStickersBought` diary stats; casino / chase packs; the DLC popup.
- **Unverified by feel:** the pad mapping (left stick edits the tool, right stick / D-pad orbit, L1 / R1 switch tool and pack, Select = history) - a DualSense pass is the next check. The picker cursor offset (`57 + 212 u`) was fitted against screenshots.

## Session 2026-11-13 (grass under the car, `main`): the grass flattens around the player car

The owner: "Yes, fix it" (grass through the car body).

### What changed
- `eden_world.*` - `setGrassCar` + the emitter's box-distance flatten (drop under the car, lower in a 1.6 m ring), `-nograsscar`; `gm_freeride.cpp` feeds the car pose each frame (footprint 2.6 x 1.25 m half extents, generic).

### Verified
- Wheat field A/B `docs/images/grass_car_flatten_before_after.webp`; 0 VUIDs; the flatten is in the once-per-frame emit, no extra cost.

### Not done / next
- Per-car footprints from the DB body size; traffic / rival cars; Hawaii / Casino unlooked; grass size vs distance; retail A/B.

## Session 2026-11-13 (grass density tuning, `main`): continuous fields, cheaper emit

The owner: "Yes, tune it" (grass density / height after the .pgr decode fix).

### What changed
- `eden_world.cpp` - density law (keep probability by distance and class, stride 1, cap 24000; `-grassdensity`, `-grasslattice`); `SampleSectorHeight` (no per-card sector search); `RoadMask` / `GrassRoadIndex::dilated` prefilters for the card-end road probes; one insert per stream per quad; grass skipped in shadow / mirror / probe passes (`-grassshadows`).

### Verified
- Wheat / colza fields continuous (`docs/images/grass_density_law_wheat_before_after.webp`), hillside covered; 0 VUIDs.
- Steady frame at `-tpin 23104,5721`: 14.6 ms new law vs 11.2 ms old lattice (itself ~16 before the emit fixes); scene prepass 14.9 -> 8.9 ms.

### Ruled out
- Shadow / mirror passes were not the cost (skipping them changed nothing); the per-card emit was (getHeight's sector scan, ~50 hash finds per card in the probes).

### Not done / next
- Hawaii / Casino not looked at (`-island hawai` spawned in the void with Ibiza coordinates); the car-flatten (cards through the car body); grass size vs distance; retail A/B of density and heights.

## Session 2026-11-13 (the .pgr grass grid decode, `main`): grass follows the terrain - tracks, roads and plazas carry none

The owner: "Yes, dirt paths" (grass still on dirt paths).

### What changed
- `source/world/eden_world.cpp` - `drawGrass` decodes the `.pgr` correctly: row offsets are relative to the payload start (file 0x20), a run is `count + 1` cells. `-pgrlegacy` = the old reading. `tools_probe/grass_probe.py` fixed the same way.
- Why it was wrong: the row table's sorted offsets start at 0x1008 = table end - 0x20; the earlier code read every row 32 bytes early (misaligned pairs, "shuffled" tables, "(0,x) escapes", "road markers" were all that one error plus the run length).

### Verified
- All 1024 rows of `sector-3-4-1-0.pgr` sum to exactly 1024; the decoded grid overlays the sector colour map (tracks / roads / building pads = 0xff, wheat = 14, colza = 13).
- In-engine farm teleport (`-tpin 19584,24960 -loggrass`): road-skipped 718 -> 1, graph-skipped 822 -> 0 with the fix; `-grassflip` brings 799 / 719 back (the default orientation is right).
- Village spawn with every suppression OFF (`-noroadskip`): legacy decode = grass on the cobbles, fixed decode = clean cobbles (`docs/images/grass_pgr_decode_village_before_after.webp`).

### Ruled out
- The grid orientation: world axes, row 0 at the sector origin (the `-grassflip` A/B above).

### Not done / next
- Grass heights / `_OffRoad_` ladder / density were tuned against the old garbage grid: a retail capture A/B of the placement is the next accuracy step; the per-edge law (`MAX_GRASS_BILLBOARD_PER_EDGE`) is still unread.
- The other islands' `.pgr` (Hawai / Casino) were not visually checked; the container is the same.

## Session 2026-11-13 (grass on paths + far-tree light, `main`): grass cards no longer overhang roads; far trees are no longer black

The owner: "I know grass still sometimes is on pathways and stuff you can do both of these" (both = grass placement and the world vegetation accuracy).

### What changed
- `source/world/eden_world.cpp` - the road-surface / road-graph suppressions probe the card centre + the four quad ends (`-grasscentreonly` = old). `-loggrassroads` logs sample road points (`gm_freeride.cpp`) for `-tpin` probes.
- `eden_scene_view.cpp` / `eden_world.*` - the far-tree impostors' `Ambient_1` = scene `sun_ambient` x `-impostoramb2` (default 1.0); the TD_Impostor VS adds both slots and the port had 0 in the second, so the far forest was near-black. `-logimpostors` also prints sun_ambient / sun_diffuse.

### Verified
- `-loggrass` counts: village 374 -> 418 road skips, road spot 902 -> 998; `docs/images/grass_card_extent_village_before_after.webp`.
- Far trees at `-tpin 5150,26192`: `-noimpostors` removes the dark forest (so it was the impostors), k sweep 0 / 0.5 / 0.8 / 1.1, 17:00 / 23:00 / 07:00 checked, `docs/images/far_trees_impostor_light_before_after.webp`. 0 VUIDs.

### Ruled out
- The dark forest is not the grass (`-nograss` unchanged), not the instances (`-noinstances` removes the forest entirely); `-noimpostors` removes it.

### Not done / next
- Grass on off-graph dirt tracks / forest paths (terrain paint): the colour-map layers are GPU-only; a CPU readback of the sector terrain material mask is the lead (`EdenWorld::drawGrass`, near line 3340).
- Slot 1's retail source (FUN_00d769e0 / FUN_00981600) is untraced; 1.0 x sun_ambient is a visual match, `[INF]`.
- Dawn (07:00) impostors are still dim; mid-distance LOD0 vs impostor brightness at the hand-over was matched by eye only.

## Session 2026-11-13 (grass white bases, `main`): the grass root fringes are shaded now (baked root gradient)

The owner: "fix the worlds vegetation it doesn't look accurate and grass has odd white parts at the bottom".

**Reproduced**: `build-main/bin/opentdu2-main.exe -game C:/TDU2-Gamedata/TestDrive2 -userdata C:/temp/otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -tpin 19584,24960 -clock 12 -autoplay "300:shot=..." -exitframe 330` - blade bases bright white/silver.

### What changed
- `source/render/texture_cache.cpp` - `loadBankTextures` bakes a root-darkening gradient into the DXT5 colour endpoints of the `tallgrass.bnk` atlases (cause: the atlas bases are authored pale; the retail TD_TallGrass PS shades roots via its light gradient, the port's Gouraud path has none). Flags `-grassrootdark <s>` (default 0.5, 1.0 = off), `-nograssroots`.
- grass_spec.md 8, README flags row, PROGRESS tall-grass row.

### Verified
- `docs/images/grass_roots_before_after.webp` (top off, bottom new default); default-spawn village unchanged; 0 VUIDs in both runs.

### Ruled out
- `-grasstallgrass` (the retail shader) also fixes the white but shears/streaks the cards near the camera (car flatten fed with the camera) - not a usable default.

### Not done / next
- "Vegetation accuracy" beyond the grass base is NOT assessed: the `.pmi` trees/bushes (pmi_spec.md) vs retail colour/density, the unverified grass placement grid (grass_spec 7). Next: take comparison shots of a forest + a meadow and name the specific difference with the owner.
- The 0.5 bottom scale and the gradient shape are `[INF]`; the owner's eye on the real build decides.

## Session <date> (<topic>): <one-line result>` shape of `CLAUDE.md`
§"The session-entry template".

* Read order for a fresh agent: your vendor's onboarding (`CLAUDE.md` /
  `CLINE.md`) → this file → `PROGRESS.md` (the `WIP` rows especially) →
  `docs/STATE.md` for the deep history (frozen up to session 62, 2026-11-01).
* `docs/state/glm.md` / `cline.md` are the retired three-tree model's shards,
  kept as history — do not append to them.
* A session that is not pushed did not happen: commit + push `main` at the end
  (RULES §4); a push publishes the auto prerelease the launcher reads.

---

## Session 2026-11-11 (the launcher's update flow, `main`): "Restart to apply" works again (four defects: the xcopy quoting, the self-delete, the console flash, the never-persisted last_tag that offered a fresh install the release it already runs) and updates carry changelogs - a WHAT'S NEW SINCE YOUR BUILD card from the compare API plus real git-log release bodies; CI stamps the release tag into the build

The owner's report (tested on `E:\opentdu2-windows-v0.2.15`, the latest release): *"I ran it and says it's
using build 15 when I download latest release it says restart to apply and upon clicking it closes and looks
like a terminal pops up and instantly closes and nothing else happens"* — and the ask *"is it possible that
changelogs or some of the changelogs can be shown for updates?"*.

### Root cause (four defects, one visible failure)
1. **The apply script's xcopy never copied.** `StartStagedUpdate` wrote `xcopy /E /I /Y "<staged>\*"
   "<dir>\" >nul` — the destination's trailing `\` escapes the closing quote under the Windows argv rules,
   xcopy receives a broken path, its stderr goes to `>nul`, no errorlevel check. The staged files were
   silently never applied.
2. **The self-delete killed the relaunch.** The script's last line was `del "%~f0"`; cmd reads a batch file
   line by line and a delete makes the next read fail — "The batch file cannot be found.", errorlevel 1.
   (The fixture test proved it: any `del "%~f0"` ends the script with exit 1 in this cmd; only the
   `(goto) 2>nul & del "%~f0"` idiom exits clean.)
3. **The terminal flash.** `CreateProcessW(..., DETACHED_PROCESS | CREATE_NO_WINDOW, ...)` — MSDN says
   CREATE_NO_WINDOW is ignored next to DETACHED_PROCESS, so a real console appeared with all the swallowed
   error text and died: the "terminal pops up and instantly closes" the owner saw.
4. **The phantom update offer.** `bAvailable = lastTag.empty() ? true : latestTag != lastTag` and nothing
   ever wrote `[update] last_tag` (read in launcher_settings.cpp:58, saved verbatim, never assigned) — a
   fresh packaged install was told an update exists and was offered *the same release it had just
   downloaded*. The "build 15" display is the commit-count stamp (`git rev-list --count HEAD`), which
   coincides with the tag number only while one release ≈ one commit (v0.2.15 = 4827bfd = commit #15); the
   launcher never showed which release it was.

### What changed
- `launcher_updater.cpp` — the script is generated by the pure `BuildApplyScript` (exported for the
  selftest): `findstr /C:"%PID%"` instead of PATH-resolved `find` (a Git-for-Windows PATH serves GNU find),
  the xcopy destination carries **no** trailing backslash, every step appends to
  `%TEMP%\opentdu2-update\apply.log`, the relaunch runs even on failure (a failed apply must not leave the
  user with nothing), `set OK=1` + `if not "%OK%"=="1" exit /b 1` keeps the script and exits non-zero on
  failure, and the self-delete is the `(goto) 2>nul &` idiom, reached only on success. Spawned with
  `CREATE_NO_WINDOW` alone. The relaunch target is `ExePath()` (a renamed launcher still returns), and
  `StartStagedUpdate` refuses an empty staging dir up front.
- The update decision (`FillFromGitHub`): `UpdateEntry` gains `Commit` (`target_commitish`); equal hash →
  "This launcher is the newest release" and `bAvailable = false`; else the compare API
  (`/compare/<buildHash>...<commit>`) decides via `ahead_by` and its commit list becomes
  `UpdateState.WhatsNew` (subjects capped at 140 chars, 30 max, best-effort — a 404/rate-limit falls back
  to the `last_tag` string compare). The UI persists `last_tag` when a release is staged, and a launcher
  that finds files in `...\staged` at startup re-offers *Restart to apply* (`tag.txt` names the release in
  the Summary) — the owner's current stuck state recovers without re-downloading.
- The Updates page: a **WHAT'S NEW SINCE YOUR BUILD** card under the offer, the check-completion status line
  announces "Update available: <tag> - the Updates page has the changelog.", release bodies render through
  `StripMarkdownNoise` (images dropped, links keep their text, bold/heading/backtick markers off).
- `cmake/StampBuildInfo.cmake` + `windows.yml`: CI sets `OTDU_RELEASE_TAG` (tag pushes stamp their tag,
  main pushes stamp the prerelease tag of that run) → `OTDU_RELEASE_STRING` ("v0.2.18 · build 15
  (4827bfd)") shown by the launcher's title / status line / sidebar / footer / Updates card;
  `tools/CMakeLists.txt` + `launcher.rc.in` parse the tag into the version resource (was a hardcoded
  0.1.0.0; local builds stay 0.2.0). Both release jobs now check out (`fetch-depth: 0`) and write the real
  changelog into the release body — stable: `git log --format='* %s' $(git describe --tags --abbrev=0
  "<tag>^")..<tag>`, prerelease: newest reachable tag..HEAD, capped at 50 lines, `generate_release_notes`
  still appends GitHub's auto block; the prerelease body says "CI run #N" (the third "build N" meaning is
  retired). `main.cpp`: the selftest reports the release tag and `--check-update` prints WhatsNew + the
  newest release notes.

### Verified (build 19 (155ec9f+dirty) · main)
- `--selftest` **PASS** — including the new apply-script end-to-end: the generated script's quoting has no
  `\"` at line ends, a fixture run copies the staged file, replaces the fake launcher exe, creates the
  `.bak`, writes "staged tree copied" into apply.log and self-deletes with exit 0. The failure branch was
  exercised by hand (read-only target exe): "apply FAILED" logged, script kept, exit 1, relaunch attempted
  and its failure logged too.
- `--check-update` A/B against the live GitHub API: the current build reports **"This launcher is the
  newest release: v0.2.18 … available: no"** (the phantom offer is gone — v0.2.18's `target_commitish` is
  this commit); a copy pointed at `ninja-build/ninja` (hash unknowable there) degrades gracefully to
  available-yes via the tag fallback; a **v0.2.15-era worktree build** (hash 4827bfd, my new code copied
  over it) reports available-yes and renders the WhatsNew digest — the FFB wheel, grass, eden island and
  video-settings commits of v0.2.18. `--check-update` on the git-checkout path unchanged ("Up to date with
  origin/main").
- The CI changelog commands run locally: `git describe --tags --abbrev=0 "v0.2.15^"` → v0.2.14; the
  `git log --format='* %s'` ranges produce the bullet bodies. YAML re-checked by reading (no parser on the
  box).

### Not done / next
- The apply flow was never exercised against a REAL install (a GUI click of Restart to apply with a real
  staged release); the fixture + hand-run cover the script mechanics, the owner's next update is the live
  confirmation.
- `bAvailable` for a build NEWER than the newest release (the owner's unpushed local builds): the compare
  resolves, `ahead_by == 0` → nothing offered — correct, but the case is undocumented in the UI.
- Windows: the selftest's fake-exe relaunch flashes a console for a moment (a test artefact; the real
  target is the GUI launcher, which opens no console).

## Session 2026-11-11 (video settings that stick, `main`): the Resolution row works in Borderless / Fullscreen now and the Display mode row shows the mode the window is really in - the Discord user's "windowed keeps reverting back to Borderless" + "dropping resolution … staying at 4k" (RTX 3050 @ 4K); F4 answered (cheats, FREERIDE only; F2 anywhere)

The owner's task (the Discord thread of a tester user, quoted): *"A user with
those specs has had fps issues and some reason windowed keeps reverting please
can we check that the settings are sticking he also mentioned earlier that
dropping resolution didn't look like it was dropping but staying at 4k but
unsure if it is or not"* — RTX 3050 8 GB, 16 GB RAM, Ryzen 5 5600X, 4K monitor.

### Root cause (two bugs + one expectation gap)
1. **The Resolution row was windowed-only.** `ApplyVideoOptions` skips
   `SDL_SetWindowSize` while the window carries `SDL_WINDOW_FULLSCREEN`
   (game_options.cpp), and the swapchain always follows the window's pixel
   size (render_device.cpp `capabilities.currentExtent`). In Borderless the
   window IS the desktop mode → the row changed its label only; in exclusive
   the display mode was picked only inside `ApplyWindowMode`, which early-outed
   on `mode == appliedMode` → the row changed its label only. Either way a 4K
   panel rendered 4K however low the row went — "unsure if it is or not": it
   was not.
2. **The Display mode row lied about `-1`.** A window created from a retail
   fullscreen `SystemPC.cpr` is born BORDERLESS (the `SDL_WINDOW_FULLSCREEN`
   creation flag without a display mode = the borderless fullscreen desktop,
   tdu_instance.cpp createWindow), but `WindowMode = -1` ("not chosen") was
   shown as "Windowed" (extraRowText default case) and its first arrow step
   initialized to EXCLUSIVE (both old fallbacks said fullscreen-exclusive for
   `SetWindowed=false`, contradicting the creation). So the user opened the
   page on "Windowed" (a lie — really Borderless), pressed an arrow, and the
   row next to that lie was "Borderless" — *"it reverts back to Borderless"*.
   (Their file can also hold `window_mode = 1` from any past Borderless
   validation; that boots Borderless forever, by design.)
3. F4 "doesn't do anything": it is the **cheats panel**, wired in FREERIDE and
   casino only (`gm_freeride.cpp:3408`, `gm_casino.cpp:2378`). The graphics
   panel is **F2 anywhere** (`AdvancedGraphics::drawUI` runs globally,
   tdu_instance.cpp:614) or Y / Triangle on the pause screen. No code change.

### What changed
- `source/game/game_options.{h,cpp}` — `EffectiveWindowMode()`: `-1` resolves
  to the mode the window was CREATED with (windowed / borderless desktop);
  `ApplyWindowMode` uses it, keys its no-op on mode AND fullscreen size (a
  Resolution step re-applies), and both fullscreen flavors now call
  `ApplyFullscreenMode` = the display mode closest to `video_width/height`
  (borderless rides a real mode too, so the Resolution row drives the scanout
  there; null mode = desktop only with no size chosen; the picked size is in
  the log line; exclusive with a failed enumeration degrades to the null mode
  and re-applies on the next step instead of latching).
- `source/game/pause_menu.cpp` — the Display mode row's label and first-step
  start from `EffectiveWindowMode()`, so the row never shows a mode the window
  is not in and one press lands on the true neighbour.

### Verified (headless, build 17 (bed87e8+dirty) · main, 4K desktop, scratch userdata per run)
- `window_mode=1` + 1280x720 → `Window mode: borderless fullscreen at 1280x720`
  + `Swapchain created (1280x720, 3 images)` — the row's size now drives
  Borderless (was impossible).
- `window_mode=2` + 1280x720 → `Window mode: exclusive fullscreen at 1280x720`
  + swapchain 1280x720.
- `window_mode=0` + 1280x720 → `Window mode: windowed` on every boot (sticking).
- `window_mode=1` with no size → `Window mode: borderless fullscreen (desktop
  resolution)` + `Swapchain created (3840x2160, 3 images)` — the user's
  Borderless state quantified: full-4K rendering (their FPS cost).
- Run logs `_vmode_{a,a2,b,c,d,e}.log` (scratch, gitignored).

### Ruled out
- The launcher forcing a mode: no `-borderless` / `-fullscreen` / `-w` anywhere
  in `tools/launcher` — the command line is not the revert.
- options.ini being written behind the user's back: a plain boot+quit writes
  no options.ini (verified: none appeared in the scratch userdata); every
  `SaveGameOptions` site serializes the live state (F2 close, units, audio OK,
  video OK/Test, three cheat checkboxes).
- A quality profile touching the mode: `ApplyQualityProfile` never writes
  `WindowMode`.

### Not done / next
- The movie menu stepping (open Video page, arrows on the Display mode row,
  Cancel restore) not driven live headless — the autoplay drive of the retail
  menu is fragile; label + step go through `EffectiveWindowMode` at one site
  each, and the boot matrix above exercises the same resolver. Next: the
  owner/user eyeballs the row on the next prerelease build; if a real revert
  is still seen in play, get the build id + their options.ini `window_mode`.
- The `-borderless` / `-fullscreen` flags force `WindowMode` on every load and
  a save during that run would persist the forced value (dev-flags-only quirk,
  users never pass them — noted, not fixed).
- The user's actual FPS: the fix gives the Resolution row teeth, but a 3050 at
  4K wants `-profile low` / the 60 fps preset or Render scale ~67 % too — say
  so in the reply.

## Session 2026-11-10 (grass on the village cobbles, `main`): three more grass suppressions - the architecture family in the paved index, the ROAD_MAP road-graph width test, the out-of-class .pgr skip - the first-test-drive village renders clean cobbles (docs/images/grass_village_cobble_before/after.png); the .pgr row-table shuffle probed and documented as open

The owner's task: "g_on1.png has grass where it shouldn't on road behind the
car are you able to try fix the grass issue" - the village cobble plaza of
the first test drive, still grassy after the morning's TD_Road suppression.

### Root cause (three layers, all fixed)
1. **Out-of-class `.pgr` values clamped onto a grass type**: the emit took
   any cell value, `Min(v, 24 types - 1)` - the road/path-marker-looking
   cells (`0x59/0x5f/0x60/0x9d` in the spec's sectors, `0x7b/0x7c` here)
   rendered as the last (tallest) family. They skip now, counted as
   "path-marker cells skipped" (187-188 at the village spawn).
2. **The village plaza is not road geometry**: the sector `-o` material
   census (new, `-loggrass`) shows the collision soup carries walls /
   buildings / 'Village_Bandero' in the architecture family
   `183157b81c6cff57` - the plaza streets are terrain paint. That family
   joins `BuildRoadIndex` (up-facing + height-window rules keep walls /
   roofs out automatically; the index grew 1445 -> 3213 triangles here, the
   skip count ~unchanged - the streets really are paint).
3. **The ROAD_MAP graph suppression** (the faithful GRASSASTARMAC shape):
   `EdenWorld::GrassRoadIndex` - the ROAD_DETAIL polylines the map screen
   already parses, as an XZ segment hash built once per island from
   GMFreeRide; a card within `-grassroadwidth` (default 5.5 m) of a
   centreline skips. The village renders clean
   (`docs/images/grass_village_cobble_before/after.png`, same pose); the
   farm keeps its cap (20000 CAPPED, 685 graph-skips, `-grassroadwidth
   5.5`); `-roadskiptol` exposes the below-sheet window (0.05 m default).

### The .pgr deep probe (documented in grass_spec.md 7, open)
The row-offset table is index-shuffled in both probed sectors (~50 %
ascending steps, one `0x1008` entry pointing back into the table) and the
table-order decode is organic on the farm sector but full-width uniform
bands on sector-5-0-6-7 - the bands read as grass stripes on the village.
Sorted storage / reversal / 10-bit bit-reversal score no better on
adjacent-row coherence (the metric saturates: one class > 50 % of both
sectors). The `(0, x)` RLE pairs as escapes fit the missing-cell counts
exactly (farm 97,280 short / 4,955 pairs = 19.6 cells; s5067 15,807 / 709 =
22.3) but decoding them that way breaks row completeness. The retail
anchors point at per-EDGE billboards (`MAX_GRASS_BILLBOARD_PER_EDGE`,
`GRASSASTARMAC`) - the grid may not be a terrain map at all. The port keeps
the table-order placement + today's suppressions; the true layout wants the
PS3 twins or a retail capture.

### Verified (headless, build 16, `-gm FREERIDE -skipcine -nopad
-fixedtimestep -clock 13:00 -loggrass`)
- Village (default spawn, the owner's repro): 482 road-skipped + 189
  graph-skipped + 187 marker cells skipped; the cobble plaza and street
  clean in the shot, planters / verge grass intact.
- Farm (`-tpin 19584,24960`): 20000 cards CAPPED, 615 road + 685
  graph-skips - the fields stay dense.
- 0 errors, 0 stalls on every run.

### Not done / next
- The `.pgr` layout research (grass_spec.md 7) - PS3 twins or a retail
  capture A/B; junction plazas wider than ~11 m still take grass in their
  middle (the width dial is not a pavement mask).
- The roadside `_OffRoad_` grass-shrinking ladder stays open.

## Session 2026-11-10 (wheel force feedback, `main`): `source/input/wheel_ffb.*` - the rim torque on a haptic steering wheel (front-axle lateral load, slip lightness, centring pull, speed damper, crash kick), the port's own composition documented in docs/re/ffb_spec.md; headless drive proves the feed end to end, the feel awaits the owner's wheel

The owner's task: "you can start the FFB work ... it's something we were aiming
on doing before" - the same-day follow-up to GitHub issue #3 ("TDU2 never had
a proper implementation for force feedback on wheels") and the OpenTDU1
licence ask (org #548; TDU1's device layer is the documented *shape*,
unlicensed upstream - **nothing copied**, the composition is ours).

### What changed
- `source/input/wheel_ffb.h/.cpp` (new) - `WheelFFB`, beside GMFreeRide's
  `GamepadOutput`: attaches the SDL haptic device off the open gamepad's
  joystick (SDL 3.4.20 has no haptic-from-gamepad opener -
  `SDL_GetGamepadJoystick` + `SDL_OpenHapticFromJoystick`; DirectInput wheels
  expose it, XInput / DualSense pads never do, which gates the feature
  naturally), drives a `SDL_HAPTIC_CONSTANT` effect (level ±0x7fff x gain,
  INFINITY, updated on a 0.004-of-scale change + a 0.25 s heartbeat) and a
  `SDL_HAPTIC_DAMPER` condition (coefficient = speed norm), a crash `kick()`
  (0.18 s decaying overlay in the torque's own direction), `shutdown()` stops
  the effects (exit, option off, unplug). Flags: `-noffb` (also the
  `options.ini` `ffb` row + the debug panel checkbox "wheel force feedback"),
  `-ffbgain <0.1..4>`, `-ffbinvert`, `-logffb` (attach line + a ~2 Hz state
  line printed device or not).
- `source/game/freeride/gm_freeride.cpp/.h` - the composition, in the
  pad-extras block (the same block that feeds `GamepadOutput`): from
  `VehicleDynamics`'s per-wheel state - the front axle's `LateralForce` (each
  contribution falling to 0.25 by 3x the tyre's own `OptimalSlipAngle` - the
  rim "goes light" past the grip peak), normalized by `Load x SurfaceFriction`
  (grip loss on grass is automatic), the centring term
  (steerAngle/0.6 rad x speedNorm x 0.35 x axle grip), no fight below 2 km/h
  (full at 10), `Damper = speedNorm`; `wheelFfb.kick()` beside the two crash
  `gamepad.pulse()` calls; the `ffb:` line + checkbox in the debug panel;
  `wheelFfb.shutdown()` at terminate. `docs/re/ffb_spec.md` (new) is the
  design record: the TDU1 shape table (§1), the composition (§2), the attach
  law (§3), flags/logs (§4), the evidence split (§5).
- `source/game/game_options.h/.cpp` - the `FFB` field + the `ffb` save row.

### Verified (headless, build 15 (4827bfd+dirty), `-gm FREERIDE -skipcine
-nopad -fixedtimestep -logffb`, accel f60, steer-left hold f280-330,
steer-right f360-410)
- The state line follows the script exactly: damper ramps 0 -> 0.48 with
  speed after the accel hold, torque jumps +0.707 -> the +1.00 rail on the
  steer-left hold (the centring/SAT pull against the turn), settles with
  small counter-swings (-0.135 / +0.216) as the steering recentres, parked
  frames print torque 0.000.
- `-noffb` A/B: zero FFB lines (fully silent); 0 errors, 0 stalls both runs.
- No wheel on the headless runs: every line reads "no device" - the *feel*
  is the owner's test (plug a wheel in; if the pull reads backwards,
  `-ffbinvert`). PROGRESS rows (bug table + feature board), README flags row.

### Not done / next
- Config curves (TDU1's per-input piecewise tables) instead of the fixed
  feel constants - only if the owner wants tuning depth after a first drive.
- FFB in GM_TestCar (only FREERIDE feeds a wheel today; the class lifts out
  in one include + one update call).
- The feel report from the owner's wheel (and any `-ffbinvert` outcome) ->
  `docs/re/ffb_spec.md` 6.
- The licence answers (org #548, Milli) still pending - recorded in this
  morning's docs session (4827bfd).

## Session 2026-11-10 (OpenTDU1 reference re-point + physics/FFB inventory, `main`): the reference clone now tracks the org repo (the fork was 278 commits behind) and docs/re/opentdu1_reuse.md gains §2.11 — upstream's handling physics is done on Jolt (5,874-line physics_vehicle, retail-Havok replay validation, a bike roll-limit constraint) and the full FFB chain is in the clear; both gated on the licence ask (org issue #548, filed by the owner)

The owner's task: the repoint behind issue #3 (silentvelour: drop TDU2
handling, implement TDU1 physics; take OpenTDU's camera; keep TDU2 bikes) and
the TDU Platinum question (can Milli's mod feed physics/camera/refuel).

### What changed
- `C:\temp\opentdu1-ref` re-cloned from the org repo
  `opentestdriveunlimited/OpenTestDriveUnlimited` (shallow, `5487449`) —
  CamilleKMI's personal fork sat 278 commits behind (stale since 2026-09-13);
  the doc now names the org repo as canonical.
- `docs/re/opentdu1_reuse.md`:
  - header: repo + maturity refreshed (FreeRide **done** upstream now;
    physics runs on Jolt — `third_party/JoltPhysics`, CMake
    `OTDU_USE_JOLT`); **licence status recorded**: the org repo ships no
    licence (GitHub `license: null`) — read-and-reimplement only, no
    verbatim port into this GPL-3 tree until org issue #548 (the owner's
    GPL/permission ask, filed 2026-11-10) is answered.
  - new §2.11: the handling inventory (physics_vehicle.cpp 5,874 lines /
    3 stubs; vehicle_simulation.cpp 1,795 / 0; gs_physics.cpp 6,754 / 11;
    physics_vehicle_replay.cpp replays retail Havok snapshots — header 0x10,
    per-vehicle record 0x43b0, wheel stride 0x450 — into Jolt to evaluate
    faithfulness), the bike roll-limit constraint (101 lines, a Jolt
    `TwoBodyConstraint`, `mRollLimit = 0.01f`), the complete FFB chain
    (`input/gs_device.h:132-141` externs; `config/device.cpp:168`
    `FFB_FACTOR`; `gs_physics.cpp` `EvalFFBCurve` 6441 + composition
    6481-6578; `input/gs_device.cpp:356-407` SDL haptic constant + damper),
    and the no-refuel finding (no `fuel`/`refuel` anywhere in OpenTDU1
    source — TDU1's gas stations are unimplemented upstream, so
    fuel_gas_station_spec.md stays the only code path; the Platinum mod's
    reworked refuel is UX reference at most, never code or data in this
    tree, NOTICE rule).
  - ranked list gains 10-12: FFB (gated on the licence), TDU1 handling as
    `-physicstdu1` behind a toggle (gated on the licence + a car-data
    conversion plan), retail-physics replay validation as our
    faithfulness-evidence pattern.

### Not done / next
- The physics / FFB / camera ports wait on org #548 (licence) — nothing else
  blocks them; FFB is the smallest first move, then chase-cam laws, then the
  handling epic with a car-data conversion plan.
- Owner outreach to Milli, no reply yet: the 2026-10-30 DM (still unread),
  the Turboduck forum message, and a Discord DM (2026-11-10, notes the
  earlier attempts and that she is credited regardless of outcome).
  Platinum files stay local-only reference regardless of the answer.

## Session 2026-11-10 (grass through roads + density, `main`): tufts no longer poke through the tarmac (each sector indexes its TD_Road triangles; the emitter skips covered cards) and the grass is 2.2x denser at ~the same frame cost (one emit per frame + a near-first scan + stride 2 / cap 20000)

The owner's task: "Grass/Vegetation needs work During some tests I seen grass
poking through roads noticed at when you can start your 1st test drive of
california also believe grass should be more dense in area's it seems patchy
as if there's not enough" (the California = the Ferrari California, the
first test drive at the Ferrari dealer on Ibiza).

### What changed
- `source/world/eden_world.h/.cpp`:
  - **The road index**: `Sector.RoadTriangles` + `RoadCells` (8 m XZ
    buckets, the carve-ground mechanics on the road ribbons);
    `BuildRoadIndex` at sector load — items whose material resolves
    `kRoadShaderSet` (the TD_Road ribbons of the sector -o, collision kept
    anyway), up-facing triangles only; `SampleRoadSurface` = the
    `SampleCarveGround` query on that index.
  - **The suppression** in the grass emitter: a card whose spot has a road
    surface within `cardHeight + 0.25 m` above the heightfield is skipped
    (the roads ride above the sheet — on embankments by metres — so a card
    rooted under a road poked through the tarmac). Cards well below a road
    (bridge decks) stay. `-noroadskip` = the A/B; `-loggrass` counts the
    skips. Cards can still overlap a road *edge* by up to w/2 (centre-only
    test — the retail roadside answer is the `.pgr` markers +
    `_OffRoad_` ladder, still open).
  - **One emit per frame**: `drawGrass` split into the emit
    (`buildGrassBatch`) and the draw; every pass that calls
    `EdenWorld::draw` (prepass, shadow casters, ocean mirror, env-cube
    faces, main — up to ~8x per frame) used to re-walk the `.pgr` grids
    and re-upload. The passes now re-draw the cached bindings; rebuild on
    frame rollover or > 2 m of camera movement. The upload-failure path
    zeroes the mesh (the copy carried the template's IndexCount).
  - **Near-first scan**: sectors sorted by centre distance, rows and
    columns alternating outward from the camera's lattice lines — the cap
    truncates the faded far ring instead of an arbitrary list-order band
    (before, a capped dense sector never reached the road cells at all:
    0 road-skipped at the farm highway).
  - **Denser defaults**: `-grassstride` 3 → 2 (2.25 m → 1.5 m spacing),
    `-grassmax` 9000 → 20000, streams reserved once per build. The scene
    record is ~flat in the card count now (9000 capped vs 20000 capped
    both ~10-13 ms `-logscenetimes` at the farm teleport).
- `docs/re/grass_spec.md` §6 (the three mechanics + evidence) + §7 (the
  poke-through item built; the `_OffRoad_` roadside ladder stays open).
- README flags row, PROGRESS.md tall-grass row (now `main`).

### Evidence (build 13+dirty, headless)
- `docs/images/grass_road_ferrari_off/on.webp` — the dealer road (the
  owner's repro area): 5163 cards with 0 skipped (poke-through) vs 4957 +
  206 road-skipped (clean tarmac; the counts reconcile exactly).
- `grass_road_highway_off/on.webp` — the autovia embankment: tufts crossed
  the guardrail line onto the shoulder in the off shot; the on shot keeps
  the embankment grass and the tarmac clean (159 skipped while capped).
- `grass_density_before/after.webp` — the farm field, old defaults vs new:
  the patchy dirt fills in.
- Regressions: `-grasstallgrass` renders through the same paths (guid
  cafe8deca8c8caca, 20000 cards, 591 skips), `-nograss` silent, no
  validation VUIDs, no stalls.

### Open / next
- The roadside `_OffRoad_*` ladder + the `.pgr` road markers (the retail
  way to *shrink* grass toward roads; the port deletes the covered cards).
- Retail-capture density A/B (what `TALL_GRASS_MAX_PER_EDGE` really
  generates), the wind time, the car flatten — the spec's §7 as before.

## Session 2026-11-10 (map icons + discovery, `main`): every dealer pin shows its brand (the retail-grabs law) and the map hides undiscovered spots — `KnownSpots` in the profile, All spots cheat, `-cheat allspots`

The owner's task: "Is it possible we can add the icons on map I believe some of the
dealership icons on the map are missing / also we need to also have it so no icons
show on the map as they normally show on map once you've discovered them", with two
retail grabs of the "Spots for Vehicles" tab attached (Ibiza + Hawai).

### What changed
- `docs/re/map_spots_spec.md` (new) — the spots.bin category→icon table, the dealer
  logo law + the retail pick table, the discovery law, the open [ENC]s.
- `source/game/map_screen.cpp/.h` — the dealer pin logo (the 3-prefix hack → the
  retail picks table + the stock law; the brand resolves through
  `DBCarPhysicsData._car_brand` → `DBBrands._logo_global_index`, both read at
  runtime), and the discovery state (`spotDiscovered` per island:
  `discoverNear` 150 m [INF], `buildSpots` clones only known spots,
  `updateSnap` skips unknown ones so fast travel cannot target them;
  `unlockAllSpots`; `-logspots` logs logo + state). `-maptab <n>` now actually
  pins the tab (the movie's open animation reset it before; a test hook).
- `source/game/car_shop.cpp/.h` — `ShopCar.BrandHash` (`_car_brand`; the
  `_manufacturer_name` display strings are not id-safe — the Charger's says
  "Charger").
- `source/game/player_profile.cpp/.h` — `KnownSpots` per island, saved as
  `knownspots_<island> = id,…` (the `ExploredRoads` pattern).
- `source/game/freeride/gm_freeride.cpp` — `discoverNear(carPosition)` beside
  `trackCar`; the All spots cheat handler.
- `source/game/cheats.cpp/.h` — the **All spots** delock row (the retail dev
  menu's shape), `spots: n/m` in the status line, `-cheat allspots`.
- `README.md` flags + feature rows; `PROGRESS.md` World map row.

### Verified (build 12, headless)
- All 20 Ibiza dealer pins resolve to distinct logos matching the owner's retail
  grab, position by position — `docs/images/map_dealer_brands_retail_vs_port.webp`
  (retail over port). Before: only Audi/Ferrari/Mercedes had brands, 17 pins were
  the generic car cell.
- Fresh profile: `75 spots with an icon …, 2 discovered` (the auto-knowns: the
  story house + the hotel), `2 spot icons created (73 undiscovered hidden)`, the
  vehicles tab draws nothing (`docs/images/map_discovery_fresh.webp`).
- Discovery: `-tp 21700,8,24188` (96 m from the Ferrari dealer) → `MapScreen:
  discovered 'ECD_FERR' (cardealer)`, the map shows exactly that pin
  (`docs/images/map_discovery_one_pin.webp`); 296 m out does not fire (the radius
  is real). `-playername` run: `knownspots_ibiza = …` lands in `profile.sav` and a
  later run reloads `3 discovered` (round-trip).
- `-cheat allspots`: `75 spot icons created (0 undiscovered hidden)`, profile
  saved (`cheat: all spots`).

### Ruled out / notes
- The retail per-dealer pick is hand-authored in the encrypted band — "British
  Cars" (6 Astons) shows Lotus, "Asian Cars" (2 Nissans) shows Subaru, "U.S. Cars
  (north)" shows Hummer. The grabs settle 11 of 14 dealer banks; the rest use the
  stock law (most-represented brand, tie → priciest car's brand).
- Hawai's 25 `EDI_B` diners (category 0x08, no prefix match) stayed icon-less
  **faithfully**: MAP.vmf has no diner/food kind. `bikeDealer` /
  `carDealerClassic` kinds exist in the movie but no base-game spot or DBCarShop
  row uses them (bike dealers = DLC data the port does not read).
- The HUD's floating world markers + enter prompts stay unfiltered on purpose:
  driving past a shop is what discovers it (retail shows the enter tile of a
  never-visited dealer).
- An existing profile (pre-feature save) starts with nothing discovered except
  owned houses / home — drive near spots to re-discover, or F4 → All spots.

### Not done / next
- Hawai's own pin layout not screenshot-verified (the owner's Hawai grab shows
  the same 14 dealer banks; the law is island-independent).
- The retail `carDealerClassic` tile art (if it differs from `cardealer`) is
  unused; the port draws `cardealer` + brand for the ECDC classics too.

## Session 2026-11-10 (walk-mode emotes, `main`): the 8 retail avatar emotes play in the self view — X / 1..8 / the EMOTES panel, `-emote <label>` for tests

The owner's task: "implement the characters emotes if possible" (after confirming
`origin` = `https://github.com/danny199012/OpenTDU2`).

### What changed
- `docs/re/emotes_spec.md` (new) — the feature spec: the 16 `em_*.anm` of
  `avatarsdriver.bnk` (`anims\emotes\em_xan`, 8 emotes x sex = the `animAvatar[8..16]`
  slots of `Entity__AvatarOnline`, the "hello(a)" of tess_garage_spec 4.2 is
  `em_helloa-w`), the retail trigger path refs, the port's [INF] choices, the [ENC] gaps.
- `source/game/avatarchoice/intro_cinematic.*` — the clip table (`getPlayerEmotes`, the
  retail slot order), `playPlayerEmote` / `startPlayerSelfView` / `stopPlayerEmote` /
  `setPlayerEmotePlace` + `updateEmotePlayer` (bind -> `co_stand` looped ->
  `BlendPoses` toward the clip, 0.2 s ease each way, the `SeatedAvatar::setOverride`
  pattern), called from `updatePnjs()` in the `bPlayerSeated` chain.
- `source/game/walkmode/walk_mode.*` — `startSelfView` / `endSelfView` / `updateSelfView`
  (the body parks, the camera glides 0.25 s to a 2.2 m / 1.75 m orbit aimed at floor
  + 1.2 m, feeds `setPlayerEmotePlace`; the retail type-10 camera law is [ENC]) and the
  shared `drawEmotePickerUi` (Dear ImGui; the retail wheel movie is undocumented).
- `source/game/firstwalkmode/gm_first_walk_mode.*`, `source/game/spots/interior_walk.*`,
  `source/game/casino/casino_interior.*` + `gm_casino.cpp` — the same emote law in the
  three walk hosts: `IA_Emote` (X) toggles the self view, 1..8 the slots, the view
  returns at the clip end (the retail state 7 -> 4), B in the self view goes back instead
  of leaving the room, A ignored; `-emote <label>` plays on the walk start.
- `source/input/gs_input.*` — `IA_Emote` / `EMOTE_MENU`, default X (the retail action
  0x22 binding is [ENC]; X collides with nothing in the walk contexts, rebindable via
  controls.ini).
- `source/tdu_instance.cpp` — autoplay action `emote` (the `press=` path; autoplay
  `key=X` injects the raw scancode and does not map through the action bindings — a
  real keyboard X does, that was the one test dead end).
- `README.md` flags row, `PROGRESS.md` Done row, `docs/re/casino_spec.md` §8 row (the
  emote half of "no cocktail or emote to *use*" is now built).

### Verified
- `powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1` exit 0 (build 11,
  509d6ce+dirty); check-build: exe newer than every source.
- FIRST_WM headless (`-allowskip -emote hello -logemotes`, autoplay skip + shots):
  `EMOTE: self view in, eye (-7.73, 1.69, 16.32)` -> `EMOTE: playing 'hello'
  (em_hello-m.anm, 4.92 s)` -> `EMOTE: self view out` at the clip end (~frame 417);
  screenshots `docs/images/emote_hello_garage.webp` (the wave, third person) and
  `emote_return_firstperson.webp` (after the return, the A/B).
- Trigger paths: `press=emote` toggles the self view (idle), then autoplay `key=3`
  played `funny` (`em_funny-m.anm, 4.16 s`) — `docs/images/emote_funny_garage.webp`.
- Casino hall (`-gm CASINO -casinoroom hall -skipcine -walkat ... -emote cheers`):
  `playing 'cheers' (em_cheers-m.anm, 4.32 s)` — `docs/images/emote_cheers_casino.webp`.

### Ruled out / notes
- The retail emote wheel is a Flash movie that never shipped readable (`emop`/`emcl`
  reach the manager, the movie is undocumented) — the ImGui panel is the stand-in.
- The retail avatar opacity fade (FUN_00b85460) needs skin-shader alpha the port has no
  path for; the camera glide carries the transition instead.
- The `Emote_11..33` -> slot mapping is not decoded; the unlockables stay inert and the
  emotes are not gated.
- The woman clips are in the bank and the per-sex branch is the SeatedAvatar law, but
  the runs played the default male profile (no CLI flag forces the avatar sex).

### Not done / next
- The cocktails (the other half of casino_spec §8's row) still need the prop work.
- If the owner wants the pad: the emote key is keyboard-only right now (a pad binding
  for IA_Emote wants the owner's button preference).

## Session 2026-11-10 (single-tree migration, `main`): the workflow moved to one directory, one public repo, one line

The owner's decision: agents run one at a time in one shared directory
(`C:\OpenTDU2`), rotating when the 5h / weekly quota runs out, and everything
publishes to the public repo. Done: cloned `origin` =
`https://github.com/danny199012/OpenTDU2` as `C:\OpenTDU2`; cherry-picked
glm's two unpushed commits (the docs/images WebP shrink 218 MB → 22 MB + the
README gallery links); captured the old win tree's uncommitted spot-holograms
feature as `wip/spot-holograms` (compiles clean on top of `main`, never run);
verified the win tip's shop/house world markers and the cline casino cashier
work were already in `main` (the shader-blob drift between the old branches
and `main` is stale, not missing work); `WORKSPACES.md` / `RULES.md` /
`PROGRESS.md` / this handoff rewritten to the single-tree model; pushed as
`4289a76` (the docs landed one push later than the images — the first push
went out before the docs commit was moved onto `main`; two auto prereleases
fired and the launcher sees the newest). Verified: `build-main\bin\opentdu2-main.exe`
exit 0. Not done / next: the old trees (`C:\OpenTDU2-win` / `-glm` / `-cline`)
are disposable — the owner deletes them when ready, never while an agent has
one as its cwd; the owner's `E:` copy still points `origin` at the private
archive (flip it with `git remote set-url origin
https://github.com/danny199012/OpenTDU2.git` when next opened); the private
repo stays frozen (optionally Archive it on GitHub); the RE data files
(`docs/re/ps3/*`, `savegame_schema_360.txt`) live only there and must never be
created in this tree. Next work: whatever the owner brings to the next agent;
the hologram WIP row in `PROGRESS.md` names its own next step.

## Session 2026-11-05 (shop markers, `win`): shops and houses get their retail world marker tile

The owner, with a retail capture of a red house tile floating over a home down the road: *"I'm hoping we can quickly
add these markers there's some for clothes and dealerships I think even champion ships have them etc... But look
slightly different."*

### What changed
- `gm_freeride.cpp` `tickWorldSpots` - every spot with a map icon within 300 m (`MapScreen::forEachSpot`, new) is
  pushed as a world marker, nearest first, after the race / gate / championship markers (8 max); owned houses
  (`EPH`, `IsHouseOwned`) show `home` (the red tile of the capture), the rest their own Spot art. Not during a race /
  licence test, not the spot whose enter element is up. Shop tiles: anchor +14 m, scale `300/d` clamped 150..230 %
  [INF: read off the one capture]; race markers keep their small law.
- `freeride_hud.cpp` `setWorldMarkers` - the clones hide the enter CARD (depths 4/5/9/10/13: every marker drew a
  dark "Enter" panel, the Spot tile sat ~280 px under it) and are offset so the TILE centre lands on the projected
  point (`kMarkerTileDX/DY` = 7 / 280, measured with `-markertest`). `<kind>+<halo>` turns one Spot halo on.
  Dealer tiles bind their brand: `Spot/ic` 'brands', `ic/logo` frame = logo + 1, the `lure` quad = the logo's cell of
  `brands_map` (the HUD movie now adds `LogoTexturePage.bnk` like the map).
- `flash_player.cpp` `drawShape` - a fully transparent SOLID fill takes a bound bitmap override like an empty bitmap
  fill: HUD.vmf's logo `lure` is the solid kind (MAP.vmf's is the bitmap kind), so the logos never drew.
- `MapScreen::forEachSpot` passes the dealer's `LogoIndex` (the map's own rule).

### Verified
- `-markertest home,estates,clothes,carDealer -markerlogo 12|4|62 -no3d`: red home tile, estates (house + hand),
  clothes (suit), dealer tile with the Ferrari / Audi / index-62 logo (`docs/images/shop_markers.webp`, top row).
- `-tp 7445,17060 -heading 0`: `marker 'estates' canvas (637, 296) distance 142 m` - the for-sale tile floats over
  EPH_L1BI (same image, bottom).

### Ruled out
- The capture's pink bracket + red arrow is not a Spot halo: target / hlight / glow / link / news / rank / favorite /
  done all rendered (`-markertest home+<halo>`), none is it (glow is a pink rim, the closest).

### Not done / next
- **The GPS-destination dressing** of the capture (the ring bracket around the tile with the red arrow on it) - most
  likely `/HUD/arrow_gps` (sprite 769: its `arrow` child places 767/744 'wrong' + the red disc 572) or a GPS-target
  state; start with `-dumpbot`'s placement walk on 769 and the map's route destination.
- Size / height are one-capture [INF]; championship starts already had markers (their own family tiles).
- Occlusion: tiles show through buildings (as the race markers always did).

## Session 2026-11-05 (return to road 2, `win`): the red X is the reset cooldown, not "on the road"

The owner: *"I Think the red cross version happens when you try return to the road and then try again straight away
after successfully returned to the road that's how I managed to capture the snippits"*.

### What changed
- `source/game/freeride/gm_freeride.cpp` - BACK_ON_TRACK (DualSense circle / keyboard R, held ~1 s) now works
  on the road too (it used to be refused there, so a car rolled over on a road could not be reset). A reset starts
  `kBackOnTrackCooldown` = 5 s [INF]; a new press during it is refused for that whole hold: the badge's red X
  (ic 'lock' + sndp:wrong), which the movie closes itself. The badge display moved into one place (the indicator
  block): passive off-road, the hold sweep, the refused X - the hold path no longer calls the HUD directly.
- `freeride_hud.cpp` - lock -> on re-settles BOT on 'stop' (the refused X's self-close leaves it on 'default').

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -autoplay "240:hold=backontrack;260:shot=..;330:release=backontrack;
  360:hold=backontrack;372:shot=..;420:shot=..;440:release=backontrack;700:hold=backontrack;720:shot=..;800:release=backontrack"`
  -> `docs/images/rtr_bot_cooldown_sequence.webp`: sweep, reset (`back on track (12.8 m)`), red X at f372 and f420,
  the sweep after the cooldown and a second reset (`back on track (0.3 m)`).

### Not done / next
- The 5 s is a choice; the retail timer is in the encrypted driver (PS3 twin not looked for). Say if it feels long.
- `PoliceParams.CanBackOnTrack` (the reset barred in a police chase) is a readable name in the exe - not wired.

## Session 2026-11-05 (return to road, `win`): the badge is the retail `/HUD/BOT` element - down arrow + road, both states match

The owner, with two retail captures (the blue badge, the red X badge): *"I hope you can find these I believe they
come from seperate places ... would hope it if we can try match how this works correctly"*.

### What changed
- `source/game/freeride/freeride_hud.cpp` `showBackOnTrack` - drives **`/HUD/BOT`** (char 821) instead of
  `/HUD/enter_hold` (char 828). The two are twin elements of `hud.vmf` at the same parking spot; BOT's `ic` draws
  **bitmap 812** (white ring + DOWN arrow + road with lane dashes, transparent interior over the sky sweep 808) -
  the captures' art - where enter_hold's draws 822 (the up-arrow the port showed since glm s70). The label is
  BOT's own field `/:m_BackOnTrack_` (loc 50); `/:IsBOT` is set (BOT's `open` script reads it).
  Two fixes on the way: a show that starts locked now makes the `ic` goto (the cache started on 'lock', so a locked
  first show drew the `on` art), and `lock` re-shows `ic` depth 1, the red disc (the `on` hide outlived the goto).
- `-dumpbot` also prints "who uses bitmap N" (shapes filling 812/822/808/571 -> the sprites placing them -> the root,
  with the placement matrix).
- `source/flash/flash_movie.cpp` / `gs_flash.cpp` - **`-dumpflashart all|<bank>,<bank>`**: every bitmap of every
  loaded UI movie (`all` also loads every `FrontEnd/HiRes/*.bnk`) to `_flashart/<movie>/<id>.png`.

### Verified
- `docs/images/rtr_bot_retail_vs_port.webp` - left the owner's captures, right the port: passive
  (`-gm FREERIDE -skipcine -nopad -fixedtimestep -offroadhud -autoplay "240:shot=..."`) and locked (the same run with
  `240:hold=backontrack;300:shot=...` - the car is on the road, so the hold is rejected). Ring, arrow, road, sky fill,
  red disc, X and label line up with the retail.
- `-botforce lock` at f70 / f100 / f160: the X flashes and the element closes itself (ic f40
  `_parent.gotoAndPlay("close")`, the movie's own law - a rejected press flashes ~0.5 s).

### Ruled out
- Runtime composition / a flip of 822 (the s70 theory): the composed look is authored as one bitmap, 812.
- The art in another bank: all 31 HiRes frontend banks dumped; the RMS match ranks hud 812 first for the blue capture.

### Not done / next
- The owner's `<mods>/flash/fla_0822.png` stand-in no longer shows (it changes only enter_hold) - can be deleted.
- `-dumpflashart all` stops after `log_in2` (the next bank load dies with no log line); `main`/`map`.. were dumped
  by name. Not chased - start at `GSFlash::initialize`'s bank loop with the watchdog log.
- The port keeps the locked badge up while the button is held on-road; retail plays `ic.gotoAndPlay("lock")`, which
  closes itself after ~32 frames. Watch it in game (DualSense circle / keyboard R, held on a road) and say if it
  should only flash once.

## Session 2026-11-05 (school desk, `win`): the racing school opens the retail R_SCHOOL.vmf screen

The owner: *"Yes, school screen"* (the retail school UI instead of the port's panel).

### What changed
- `source/game/freeride/school_screen.{h,cpp}` (new) - R_SCHOOL.vmf driven by its own protocol (licenses_spec 11):
  `open_school` + the tutor pick (`ItemType('valid')`), `rsch:stli` (licence card states / class badges), `rsch:dits`
  (the test list: name, type icon, place, weather, done), `rsch:dtup` (Test's objective + target), `rsch:tspr`
  (start the test), `wmon` / `rsch:exit` (back to the walk).
- `gm_freeride.cpp` - `openSchool()` for the desk, `-schoolmenu` and the result screen's Quit; `-nativeschool`
  keeps the old panel. HUD hidden while it is up.
- `ig2_challenge.*` - VEHI entities counted (the overtaking tests).
- `tools/vmf_disasm.py` - walks `DefineSpriteEden` (tag 127) sprites: R_SCHOOL's menus live there.

### Verified
- `-schoolmenu A -loglicenses`: card "Asphalt Racing School / Asphalt license" with A7 / A6 badges, A -> the 7 A7 tests
  with icons and the objective panel (`docs/images/licence_school_tests.webp`), Down + A -> `rsch:tspr` -> RSA7T02
  starts; B, B -> `camf`, `wmon` -> closed.
- `-licensetest RSA7T01 -licenseforce fail`, menu Quit -> inside ERS_A on the card (`licence_school_card.webp`).

### Not done / next
- The card's player fields (photo, name, number) are empty: trace their variables in R_SCHOOL.vmf (`licence`
  clip, `tools/vmf_disasm.py` output around `SetCurLicence`).
- Weather icon always `sun`; the type icon is [INF] (title words / course).
- Controls (DualSense): left / right on the card, up / down in the list, Cross OK, Circle back.

## Session 2026-11-05 (cones, `win`): there is no traffic cone in the game data - the barrel stays

The owner, after the slalom work: *"Yes, real cones"* (load Hawai's `SF_PLOTS` as the cone).

### What changed
- Nothing in the code (the cone loader was written, tested and reverted). `docs/re/licenses_spec.md` 10 and the
  previous entry's "next" corrected.

### Verified / ruled out
- `SF_PLOTS` (Hawai `commonworld.bnk`) is a material. The library nodes using it, `SF_PLOA` .. `SF_PLOQ`, were loaded
  (EdenModel node filter) and drawn as the RSA7T02 markers: green street **bollards** and bollard rows, not cones
  (`docs/images/licence_hawai_sf_plo_bollards.webp`: SF_PLOC, SF_PLOD, SF_PLOG). Bounds: posts 0.2 x 1.06 m, rows
  2-46 m long.
- All 649 Hawai library node names, the police pack and every non-frontend bank (name scan): no cone.
- So the racing-school barrel (`sf_barel_d`, `RS_Barel_*` materials, the checkpoint pack's only obstacle) stays as
  the slalom pole / cone-line marker - most likely the retail look [INF].

### Not done / next
- Confirm against a retail capture of a slalom test (the 360 build runs: `docs/re/CROSSBUILD_PLAN.md`) whether the
  markers are barrels; if they are something else, its model is not in the PC data.

## Session 2026-11-05 (licences, `win`): slaloms weave, cone lines + racing line drawn, the retail fail / win menu

The owner: *"i was hoping we can fix the licenses solomon is meant to have cones you weave in and out of and stuff
there's also meant to be a racing line which tells you when to slow down as well also can we try find the corrcet ui
menu's also upon failing there should be a menu that pops up with retry and if you win there's a menu for advancing
to next task or returning to the school."*

**Reproduced**: `-licensetest RSA7T02` ("Slaloms"): the airport apron was empty but for barrel pairs centred on a
straight line; holding the throttle straight through passed every gate; the end was a goal line, no menu.

### What changed
- `source/game/freeride/license_result_screen.{h,cpp}` (new) - the retail RESULTS.vmf racing school frames
  (`failed_test` / `success_test` -> A -> `menu_rs`): the game sets `TestName`, `Message`, `final_score`,
  `ref_score`, `LicenceName`, `nb_tests_done/total`, `IsLicensed`, `LockReplay`; the movie's own menu answers
  `rest` (Retry), `nets` (Next test), `quit` (Quit to the racing school); `rpin` (Video Replay) goes back to the
  panel (no replays). Decoded from the movie's scripts (licenses_spec 9).
- `gm_freeride.cpp` - `openLicenseResult` / `tickLicenseResult` / `returnToSchool`: a licence test's end opens the
  screen over the frozen world (HUD hidden); Retry restarts, Next test starts the next one of the licence (the school
  if none / not passed), Quit enters `ERS_<letter>` with the school menu on that licence (`SchoolMenu::openAtTest`).
  `buildCourseMarkers`: all slalom poles, the DEC2 cone lines (one marker / 3 m), the racing line strips and braking
  zones, built once per run. `-licenseforce <pass|fail>` test hook.
- `ig2_challenge.*` - `DEC2` decor parsed (`Ig2Decor`); `Ig2Gate::isSlalom` / `getPassCentre`: a kind 1 record is a
  slalom pole with its 7 m opening on side 1 (+x) / 2 (-x) of the gate frame.
- `license_test.cpp` - a slalom counts only when the car's centre crosses 1 m .. width + 1 m out on the open side
  (straight down the pole line = "Missed a gate"); `getNextGate` points at the opening.
- `race_gates.*` - loads the pack's `Trajectory` strip and `BrakingZone_01` target; draws `Marker`s (poles, cone
  markers, strips stretched along the line) and single-pole gates. `-logracegates` lists every pack actor.
- `tools/vmf_disasm.py` (new) - AVM1 disassembler for the .vmf (FMV) movies: `python tools/vmf_disasm.py in.vmf out.txt`.

### Verified
- Fail: `-licensetest RSA7T02 -licenseforce fail -loglicenses` -> `failed_test`, thumbs-down cart, A -> menu
  "Retry / Next test / Video Replay / Quit to the racing school" (`docs/images/licence_result_failed.webp`,
  `licence_result_menu.png`); Retry -> `FSCommand 'rest'` -> the test restarts.
- Win: `-licensetest RSA7T01 -licenseforce pass` -> the licence panel "A7 1 / 7 Successful / Total"
  (`licence_result_success.png`); Quit -> `FSCommand 'quit'` -> inside `ERS_A`, school menu on Slaloms
  (`licence_result_back_to_school.png`).
- Slalom: `-licensewarp` through the openings passes 12 / 12; `hold=accel` straight from the start ->
  "gate 1 crossed 3.6 m off centre ... Missed a gate" -> the fail screen.
- `FREERIDE: RSA7T02 course: 10 slalom pole(s), 11 cone line(s) / 94 marker(s), racing line 292 strip(s), 3 braking
  zone(s)`; frames `licence_slalom_racing_line.webp`, `licence_cone_lines.png`, `licence_braking_zone.png`.

### Ruled out (with the test that ruled it out)
- A cone mesh in the Ibiza data: case-insensitive scan of every non-frontend bank for cone / plot / pylon names ->
  only `SF_PLOTS` in **Hawai's** `commonworld.bnk` (+ random hits in compressed sector data). The checkpoint pack
  has the racing-school barrel only.
- A path in the .ig2 files: the 58 licence tests hold CHK2 / DEC2 / VEHI entities only; no spline record.
- Drawing the strips along +z: the chevrons pointed back at the car (`_c3z` crop); laid from the far end now.

### Not done / next
- ~~Real cones from Hawai's `SF_PLOTS`~~: those are bollards; the game data has no cone (next entry up).
- **The retail racing line's source** is unknown ([INF] curve through the openings, braking zone where it turns
  > 50 degrees in 20 m). Lead: `ig2/replays/<test>.rly` (XMB `ReplayBaseParams`, matrix chunks: full start position
  then packed deltas). The strips are white; the Arrow_Orange / Arrow_Red UVAs (colour by braking) are not used.
- **Road tests** (RSA7T04 ...) get no racing line (the road graph could carry one).
- **The school screen** is still the native panel; the retail one is `r_school.vmf` (disassemble it with
  `tools/vmf_disasm.py`, same method as section 9).
- The fail cart's text is empty (`openCarts()` sets `Message2 = 'empty'` itself) - check a retail capture.
- Owner check: weave RSA7T02 with the DualSense (Cross = OK on the result screen, Circle = back, D-pad up/down).

## Session 2026-11-05 (cameras, `win`): the photo camera's sticks turn the right way, the chase camera no longer sinks

The owner: "in pause the camera menu the left/right on both sticks seem to be flipped right being left and vice
versa also camera in game chase cam tends to sink sometimes making it hard to see above car".

### What changed
- `source/game/freeride/photo_camera.cpp` - Eden's yaw grows towards +x, the screen's left: the right stick
  now subtracts from the yaw (the mouse already did), and the left stick / A-D move along +right (it moved
  along -right). Right stick right = turn right, left stick right = move right.
- `source/game/freeride/chase_camera.cpp` - the back / forward offsets of the chase eye and target lie in the
  plane square to the (smoothed) car up axis. With the horizontal heading and the tilted up, a nose-down pitch
  took `posOffset x sin(pitch)` off the height. `-logcameras` now also logs every frame with the eye under
  1.2 m above the car ("LOW ...").

### Verified
- Chase: `-gm FREERIDE -tp 11635,13,17477 -heading 90 -fixedtimestep -logcameras -autoplay
  "60:hold=accel;600:release=accel;600:hold=brake;800:release=brake" -exitframe 900` - before: eye above car
  0.72..1.80 m, 12+ LOW frames (`eye y - car y` stayed 1.50 while the up tilted 0.12 forward); after: 1.50..1.53 m,
  0 LOW frames.
- Photo camera: the sign change only, built; **not driven with a pad** (no headless photo-mode hook was run).
  Owner: pause -> camera, push both sticks right.

### Not done / next
- The photo camera was not exercised headlessly; if a direction is still wrong, `photo_camera.cpp` lines 97 /
  124 are the two signs.

## Session 2026-11-05 (Q7 body, `win`): the Audi Q7 draws its whole body again

The owner: "Q7 missing panels" (seen on the wheel shots: bonnet, wings and pillars missing, the seats visible
through the front).

**Reproduced**: `-userdata C:\temp\otdu2-win-wheel-Q7_V12 -playername BuyTest -gm FREERIDE -clock 12.00 -tp
11635,13,17477 -heading 90 -autoplay "150:look=1,0;160:look=1,0;170:look=1,0;250:shot=..."` (a profile that drives
car 348). `-logcaritems BODY_HR`: the 15 BODY_HR items all `visible 0`.

### What changed
- `source/game/cinematic/cinematic_car.{h,cpp}` - the `Paint_LR` / `Plate_LR` hide: a node whose items are all low
  res still hides; a visible node that also has high res items keeps them and skips only the low res items
  (`lowResItems` -> `EdenDrawParams::ForceHiddenItems`, `bDeferBlended` set). The Q7's `BODY_HR` holds one
  `Paint_LR` prim (item 10), which hid the whole shell. `-lrnodehide` = the old rule; `-logcaritems [node]`
  dumps a car's items (node, LOD, material, shader, blend, visibility, box).
- `source/render/eden/eden_model.h` - `isNodeVisible`.

### Verified
- The Q7 whole, head lamp lit (`docs/images/q7_body_before_after.webp`); mean diff to the old rule 1.2.
- Range Rover Sport HSE, Mustang Fastback '68, SLK 55 AMG (the other cars with a mixed LR node): mean diff
  0.000 against `-lrnodehide`; each run deterministic (two runs 0.000).

### Ruled out (with the test that ruled it out)
- The doors swung open / mis-posed: DOOR_L / DOOR_R locals are plain translations (`-dumpcar`), as on the
  California.
- The cockpit LOD switch (`-carlod`): it only swaps the interior; the hole is identical at 0 / 1 / 2.
- A first version that always passed draw params darkened the Range Rover / Mustang head lamps (mean diff
  0.33): a params draw records blended items unsorted unless `bDeferBlended`.

### Not done / next
- The swinging wipers' params now defer their blended items too (the same flag): not looked at in rain.
- Other cars were not audited one by one; the per-item log line ("hidden in node ...") names any car that
  takes the new path.

## Session 2026-11-05 (wheels in their arches, `win`): cars sit at their ride height, the wheels no longer poke through the body

The owner: "some cars have oddities there was a zonda a audi and some cars wheels poke through the body work",
then "Wheels through body".

**Reproduced**: `CinematicCar::load` lifted every `D_RIM_` node to the tyre radius - Zonda F 0.093 m, R8 V10
0.171, TTS 0.159, S3 0.229, Q7 0.297 - with the body left where it was, so each wheel rode that far up into its
arch (`docs/images/wheels_s3_arch_before_after.webp`, left: the rim above the fender lip).

### What changed
- `source/game/cinematic/cinematic_car.{h,cpp}` - `getBodyLift()`: the model origin (the body underside, y = 0 on
  every model, `-loghubs`) sits the DB static ride height above the road (`_ride_height_front/_rear` mean;
  fallback tyre radius - authored hubs). The rim nodes move only to tyre radius - body lift (a few cm). The
  `-loghubs` dump (hub, body minimum, ride heights) stays; `-wheellift` = the old behaviour.
- `source/physics/vehicle_dynamics.{h,cpp}` - `setModelLift`: `getWorldMatrix()` is the model frame (body frame
  raised by the lift); `reset` / `getPosition` stay in the body frame.
- `source/game/freeride/gm_freeride.cpp`, `source/game/shop/gm_testcar.cpp` - the physics hub = model hub + lift,
  `setModelLift`; `startDriving` / `finishWakeup` (a model pose) subtract the lift; the brake disc centre converts.

### Verified
- Side shots (`-clock 12.00 -tp 11635,13,17477 -heading 90`, `look=1,0` x3) with test profiles driving the S3 / Q7 /
  Zonda F: the S3's front rim was above the fender lip, now inside the arch; the Q7 body sat on its wheels, now
  at its 25 cm ride height.
- Car wash cutscene (`-atwash`, validate): before, the rear wheel tucked up under the fender and the sill on the
  road; after, the car on its wheels (`docs/images/wheels_car_wash_before_after.webp`).
- Physics unchanged: `-logdrive`, `60:hold=accel`, 420 frames - S3 ends at x 11715.59, 91.5 km/h (old 91.4),
  4912 rpm, gear 3; Q7 11726.73, 101.7 / 101.7 km/h, 4389 rpm, gear 2.

### Ruled out (with the test that ruled it out)
- `-car <name>` is not a fair test: it keeps the California's rims and tyre (radius 0.339 for every car).
- Raising the whole car by tyre radius - authored hub: the S3 then rode 2.7 cm high (the models hang their wheels
  a little below the resting place) - the DB ride height is the anchor.
- A draw-time offset only: cutscenes and showroom cars place the model by its own origin and would float.

### Not done / next
- The Q7 draws with its front panels missing (both builds) - a separate model problem.
- The Zonda side shot came out in the in-car view (its default camera); a chase-view shot of a Zonda is still to
  take: `-userdata C:\temp\otdu2-win-wheel-Zonda_F -playername BuyTest -gm FREERIDE ... "120:press=camera;..."`.
- (verified) `-vehcheck`: 137 cars, top speed within 1 %: 113 (as recorded), 0-100 within 0.30 s: 70 (recorded 69).

## Session 2026-11-05 (dealer buy window, `win`): the hostess is at her desk and Buy lets you pick rims, paint and interior

The owner: "as for car dealership when buying a car now there's no sales assistant and you can't pick the
cars colour, interior or rims", then "Yes, do the dealer".

**Reproduced**: `-gm SHOP -shopspot ECD_AUDI_1276I` logged `free-ride visit, intro C_INTRO_I, loop ''` (empty
desk), and Buy bought on the spot: the port treated `buin` (open the buy window) as the purchase.

### What changed
- `source/game/shop/gm_shop.{h,cpp}` - `C_LIFE_HOSTESS` first in the free-ride loop list (all 14 dealer
  banks have it), `C_INTRO` as the intro fallback (icd_ferr). The buy window, protocol from the readable
  dealer handler `FUN_009f0840` (spot_interiors_spec 7): `openBuyWindow` (buin), `stepBuyWindow` (neop / prop,
  pages 1 car / 2 wheels / 3 paint / 4 interior / 5 confirm), `applyBuyItem` (chop: reads `/:num_item`),
  `writeBuyPrices` (op_price_1..3, cur_op_price, total), `paintBuySwatches`, `closeBuyWindow` (buou: the slot
  car gets its paint back). `computePrice` (car + rims + paint + the chosen interior). Purchase saves the rims
  and interior. `-logbuywindow` logs the clip tree and the arrow-key routing.
- `source/flash/flash_player.{h,cpp}` - `setClipColor` (Color.setRGB from the game, opaque);
  `gotoLabel(..., bRerunSameFrame)` (opt-in, used by the confirm jump only).
- `source/game/cinematic/cinematic_car.{h,cpp}` - `getDatabasePaintSwatch` / `FindLibrarySwatch` (the paint
  row's material `RAMPCLR1`).
- `source/game/player_profile.{h,cpp}` - `OwnedCarRims` / `OwnedCarInteriors`, saved as `car_rims` /
  `car_interiors`.
- `source/game/freeride/gm_freeride.cpp` - the driven car loads its ordered rims (`FindDatabaseDesc`).

### Verified
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-buytest -playername BuyTest -gm SHOP -shopspot ECD_AUDI_1276I -shopaim 6 -money 500000 -nopad -fixedtimestep -autoplay "150:press=validate;450:press=validate;560:press=validate;720:press=right;820:press=validate;1100:press=right;1200:press=right;1320:press=validate;1470:press=right;1580:press=validate;1720:press=validate" -exitframe 2300
- `loop 'C_LIFE_HOSTESS'`; the hostess behind the desk (`docs/images/dealer_hostess_at_desk.webp`).
- `buy window for Audi R8 V10 FSI Quattro: 5 rims, 12 paints, 13 interiors, 147400`; rims item 2 -> 148280
  (+$880, "Optional 5-arm double-spoke design Rims"); paint 3 "Ice Silver with Apollo silver sideblade" ->
  145880, the podium car repainted, 12 squares in the real colours; interior 2 "Black Fine Nappa leather (red
  couture)"; "Buy this vehicle?" -> `eval` -> `buyc` -> `bought ... colour 2, rims 1, interior 1 for 145880`
  (`docs/images/dealer_buy_window_rims.webp`, `_paint.png`, `_interior.png`, `_confirm.png`).
- `profile.sav`: `car_paints = 442:2`, `car_rims = 442:389`, `car_interiors = 442:1`; the next FREERIDE:
  `driving the profile's car 442 'R8_V10', rims audi/r8_spyder_f_03 (ordered at the dealer)` (default
  `r8_v10_f_01`), `in its ordered paint 2`.

### Ruled out (with the test that ruled it out)
- `/:has_colors` as a boolean: the movie compares `=== 1` (decoded the AVM1 at shopscar.bnk 0x911fe).
- `invoke("/buy", "next")`: `next` / `confirm` are frame labels (the exe's vtable +0xdc is gotoAndPlay).
- `gotoAndPlay('next')` as the exe does it: the arrows of the page go dead in the port (`taken 0` on every
  press, `-logbuywindow`); without it all three pages take the arrows. Kept out, documented [INF].

### Not done / next
- The podium car does not swap rims while browsing; the interior is saved but not drawn.
- The per-page camera `FUN_009f2d80(car, page)` (the retail moves to the wheel / the cabin) - readable, next
  to read; the rims picture box stays empty.
- The interior squares are grey: `_interior_color_n` ids (`C_N_02`) are not in `colors.2dm` - find their
  library.
- Why the movie's own colours script (0x911fe) is not reached in the port: a flash_player frame-routing
  question, `-logbuywindow` dumps the tree.

## Session 2026-11-05 (exits at the door, `win`): leaving a house, a shop or the casino puts the car outside that building

The owner: "the 1st thing I want you to fix is the spawning issue like coming out the casino, dealership etc...
should spawn you just outside that location you exited from", then "Casino should spawn out side casino,
dealership out side the dealership and houses out side the house that you was in". Same report, not done yet:
holes near a roundabout by a motorway, wheels through the body on some cars (a Zonda, an Audi), and the dealer
has no sales assistant and no paint / interior / rims choice when buying.

**Reproduced**: a house or walk-in shop left the car wherever the enter prompt was taken (the prompt works from
tens of metres away); the casino flew you straight back to the Ibiza seaplane dock. The SHOPSCAR dealers were
already right (ECD_AUDI `gofr` -> `handoff at 'ECD_AUDI_1276I' entrance (11635.4, 13.2, 17477.4)`, on the road at
its door).

### What changed
- `source/game/freeride/gm_freeride.cpp` - `FindSpotEntranceById` (the spots.bin record by id; a record whose
  second point is 0,0 has no entrance); `InteriorWalk::eResult::Leave` teleports the car to the spot's road-side
  entrance (+0x28 / +0x38, the point the dealer and garage exits already use) [INF: the retail walk-out is ENC].
  On the casino island, near the dock: A = back into the casino (no flight), X / Square (MENU_OPTION_1) = the
  seaplane home (`RequestFlightHome` with the current car), with a hint once per approach.
- `source/game/casino/casino_trip.{h,cpp}` - `FindCasinoDoor`: the ECAS record's second point on the casino island
  (2749.1, 1.6, 1468.1), heading -2.30 [DATA].
- `source/game/casino/gm_casino.cpp` - `leaveCasino` hands FREERIDE the casino island at that door;
  `-casinoflyhome` keeps the old direct flight.

### Verified
- House: `-tp 36345,5,5765`, validate, back x4 -> `out of 'EPH_L3DI_5067I' -> the car at its entrance (36329.8, 5.2,
  5743.8)`; on the driveway facing out (`docs/images/exit_house_at_door.webp`).
- Clothes shop: `-tp 11530,4,16850` -> `teleport to (11543.7, 4.6, 16830.4)` (EAS_D's door).
- Casino: `-gm CASINO -casinoleave 120` -> `FREERIDE at the casino door`, `island 'casino'`, car on the road with 4
  wheel contacts (`docs/images/exit_casino_at_door.webp`); `760:press=validate` -> `back into the casino`;
  `760:press=opt1` -> `C_CASINO_DEPARTURE`, `C_IBIZA_ARRIVAL`, FREERIDE at the Ibiza dock (24003, 24520).
- Dealer: `docs/images/exit_dealer_at_door.webp` (unchanged code, re-checked).

### Not done / next
- The dealer's empty desk and missing paint / interior / rims pages: `gm_shop.cpp` logs `free-ride visit, intro
  C_INTRO_I, loop ''` - start at which actor / background loop the free-ride path drops versus the story shop.
- The holes and the wheels through the body need the owner's position / car names (PROGRESS.md Open rows).
- The casino-island zone badge still says "Ibiza Area 1" (the known hardcoded badge).
- Not felt in play with the pad.

## Session 2026-11-05 (casino desks + radio names, `win`): the bar and cashier run their retail movies, the cherry raffle is in, and a song name no longer stops the game

The owner: "cline had apparently added in the cashier buying and selling chips but their build isn't up to
date I would like you to commit and push that change ... hook the correct ui for it also implement the
barman ... buy drinks and in a drink you have a random chance for something". Then: the game "refused to
load" after adding songs and an extra station (Annual Hardstyle); removing the .opus files fixed it.

### What changed
- **Cline's uncommitted work** in `C:\OpenTDU2-cline` (desk volumes + the floor wallet re-reading
  `PlayerProfile::CasinoChips` for the F4 cheats) committed there as `a88e201`, pushed to `origin/cline`,
  merged here (fast-forward). Its sync reset `-casinochips` to the profile's 0: `-casinochips` now sets the
  profile too.
- `source/game/casino/casino_desk_hud.{h,cpp}` (new): the bar on `Cas_Common.vmf` and the cashier on
  `Casino.vmf /money_conv` - protocol, data and raffle in `docs/re/casino_spec.md` 9.12.
- `casino_interior.{h,cpp}`: the desks use their retail `SelectableZone` volumes and strings
  (`'cash'` / `'barman'`) and read `DeskData` (cashier gift 200, ticket chances 1/2/3/5 %, the cocktail
  cutscenes).
- `gm_casino.{h,cpp}`: `openCashier` / `openBar` / `buyCocktail` / `tickDeskHud`; barman ->
  avatar drink -> raffle popup; the old native screens stay behind `-nodeskhud`.
- `player_profile.{h,cpp}`: `CasinoRafflePasses[4]`, saved as `casino_passes`.
- **Radio (the load failure)**: `Hardstyle fm` holds `Da Tweekaz - ... Moana ＂How Far I'll Go＂ ...opus`
  whose quotes are **U+FF02** (fullwidth). `std::filesystem::path::string()` converts to the ANSI code page
  and **throws** on a character with no mapping; the station scan did that for every song, so one name
  stopped the game in `RadioPlayer::loadCustomStations` (the log ends right after the retail stations).
  It was never the .opus extension - the bad names happened to be .opus files. New `source/core/path_utf8.h`
  (`PathToUtf8` / `PathFromUtf8` / `PathToDisplay`, never throw); the radio keeps song / art paths in UTF-8
  and shows names in Windows-1252 with look-alikes folded (`＂` -> `"`); `audio_decoders.cpp`,
  `gs_audio.cpp` (log lines) and `freeride_hud.cpp` (station art) follow; each station folder's scan is in a
  `try` (a bad folder is skipped with a warning).

### Verified
- Bar: `-gm casino -casinoroom hall -casinoselect 1 -money 5000 -allcocktails -cherry -fixedtimestep -logdeskhud`
  -> `bar FSCommand 'buy cock 12'`, `'clco'`, `GMCasino: cocktail 12 (tier 2, $200) ... WIN (cherry)`,
  cutscenes `C_COCKTAIL_BARMAN`, `C_COCKTAIL_AVATAR_WIN`, the popup "You win - Level 2 free pass"
  (`docs/images/casino_bar_drink_and_raffle.webp`, `casino_bar_raffle_tutorial.webp`). Without the flags: 4 of 15
  open, a ¤50 cocktail rolls `45 < 1 % -> lose`.
- Cashier: `-casinoselect 0 -money 5000` -> `casi:chx1`, `slri` x3, `conv:maxv`, `conv:vali` -> `dollars->chips
  5000 -> 5000 chips, $0`; `-casinochips 300` chips -> dollars 3 -> 297 chips, $1003; rate text `$1 = 1 ¤`
  (`casino_cashier_converter.png`).
- Radio: a station holding only the U+FF02 file - before: the log stops after `station 3 'Roadtrack'` and the
  process hangs; after: exit 0, `playing Da Tweekaz - Da Tweekaz - Moana "How Far I'll Go" (Official Preview)
  (... 260.0 s)`. The owner's real `Annual Hardtyle` (108 songs) + `Hardstyle fm` (14) linked into a scratch
  stations folder: both listed with their icons, both play, exit 0.

### Not done / next
- The raffle popup's level icon reads "1" on a level 2 win (`tombola_popup/content/level`, the text is right).
- The walk reticle draws over the bar cutscenes (same as the jackpot ones).
- The passes are kept but nothing spends them (the Solar Crown tournament needs other players).
- The owner's folder is `Annual Hardtyle` (typo) - the station is named after the folder, so it shows as typed.

## Session 2026-11-05 (casino rankings, `win`): the rankings tube rises behind the podium and shows its retail page

The owner: "Yes, do rankings" (the screen "meant to show fastest times around casino island in the Audi").

**Reproduced**: the black box beside the podium = `FlashScreen_CRNK_TimeAttack` at its raw placement; no
`C_RANKINGS_*` cutscene ever played and no movie was on it.

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm CASINO -casinoroom hall -skipcine -nopad -fixedtimestep -walkat "0.54,1.2,-27.0,180" -autoplay "120:shot=C:/OpenTDU2-win/_r1.ppm;150:press=validate;230:shot=C:/OpenTDU2-win/_r2.ppm;260:press=back" -exitframe 340

### What changed
- `source/game/casino/casino_ranking_screen.{h,cpp}` (new) - the tube state machine on the four cutscenes,
  the `Cas_Ranking` movie (`time_attack_rank`) in a render target on the screen face.
- `source/game/casino/casino_interior.{h,cpp}` - attach in the hall, the `crnk` selectable (its
  SelectableZone), A = RankingCamera view, B = back to the walk; updates in tick / tickBackground / offscreen.

### Verified
- `docs/images/casino_rankings_tube.webp`: closed (only the hatch), raised as the walker comes near, the
  RankingCamera close-up. Log: `C_RANKINGS_OPEN` -> `C_RANKINGS_STAND_OPENED`, `looking through camera
  0x96a3ae63d18431c0`. `-rankingsclear` turned the face red -> the binding works; the movie draws the page.

### Ruled out
- The binding (red clear A/B): the black face was the movie's empty first frame until `time_attack_rank`.

### Not done / next
- The page says "Connecting to server": the movie's row protocol (`resu_rankings_array`, `../:pos`,
  `../:player_name`, `../:score`) is not decoded and there are no times offline (no casino time attack).
- The five other ranking screens (general, satellite, sit and go) have no movie.

## Session 2026-11-05 (casino cutscenes, `win`): slot jackpots play their cutscenes, the car keys drop and are picked up, C_WINCAR in the hall

The owner: "It's a screen that pops up it's meant to show fastest times around casino island in the Audi that's
on the podium and the car slot if you match all 3 symbols of the car the keys drop out for that car there should
be certain little cutscenes for these as well", then "The key dropped and avatar picked it up".

**Reproduced**: no cutscene played on a jackpot; `-casinocin C_JACKPOT_CAR_MONEY` showed sand and sky - the
walk camera (walker at the room origin) overrode every casino cutscene camera.

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata <a copy of the userdata> -gm CASINO -casinoroom slots -casinoselect 7 -casinoprize car -skipcine -nopad -fixedtimestep -autoplay "1520:shot=C:/OpenTDU2-win/_k.ppm" -exitframe 3150

### What changed
- `source/game/casino/casino_interior.{h,cpp}` - the walk camera only while walking (`bWalking`);
  `playCinematic` / `playCinematicAtPlace` (machine anchor from its `orient_pos_scale`), `handleEndEvents`
  back to the walk or the machine camera, A/B skip; `-casinocin <name>` / `-casinocinat <i>` test hooks.
- `source/game/casino/gm_casino.{h,cpp}` - `playSlotJackpot`: the machine kind's `C_JACKPOT_*` (`_W` for a
  woman) on a jackpot / prize; the first car then `C_WINCAR` in the hall; no shell / HUD over a cutscene;
  `-casinoprize` with `-casinoselect` goes through the same path. `-nocasinojackpotcin`, `-nocasinowincar`.
- `source/game/avatarchoice/intro_cinematic.{h,cpp}` - `setCinematicOrigin`; the player actor also as
  `Avatar_`; a prop whose clip moves no bone draws on its track; the key ring is taken in `Hand_R` once
  dropped and within 0.12 m (`-nokeygrab`); `-logcincam`, `-logpropat <name>` probes.
- `source/game/cinematic/cinematic_scene.h` - `CinematicScene::setOrigin`.

### Verified
- `docs/images/casino_jackpot_keys_wincar.webp`: the car machine's jackpot shots, the key ring in the tray, the
  avatar standing with the keys in hand, then the hall ceremony (hostess + director at the podium, the R8).
- Log: `jackpot cutscene 'C_JACKPOT' at machine 7`, `'CF_KCar' taken in the hand at t 24.03 (0.11 m)`,
  `cinematic 'C_WINCAR'`; 0 stalls. Hall walk start vs the previous build: mean diff 0.05; walking moves.

### Ruled out
- Scene origin wrong: the camera already applied it (`-logcincam` printed nothing - the main camera never
  ran); the walk override was the cause.
- Key ring hash mismatch: `cf13cab40791bea5` matches; its Visibility track shows it from 20.66 s, and
  `-nopropanim` showed it dropping -> its skinned draw was the gap.

### Not done / next
- **The rankings tube** (the "black box" by the podium): `TubeEcransRankings` + `FlashScreen_CRNK_TimeAttack`
  rise / sink with `C_RANKINGS_OPEN` / `_CLOSE` (icashal). Needs its trigger and the
  `flashscreen_ranking_timeattack` screen movie with offline casino-island times in the podium Audi.
- The small `C_SLOT_WIN_0x` / `C_SLOT_LOOSE_0x` shots; the other machines' jackpots are wired but only the
  car's was watched.

## Session 2026-11-05 (casino hall, `win`): the red carpet and the podium under the R8 show

The owner (with a screenshot of the hall): "it's not 100% accurate there should be a red carpet and a podium
in the center with car on top".

**Reproduced**: the car stood on a flat olive-grey floor, no carpet runway, no podium:

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm CASINO -casinoroom hall -skipcine -nopad -fixedtimestep -walkat "0.54,0.6,-16,180" -autoplay "150:shot=C:/OpenTDU2-win/_hall.ppm" -exitframe 160

### What changed
- `source/world/eden_world.{h,cpp}` - `EdenWorld::setAreasEnabled`: the area tiles (terrain + area objects)
  can be switched off.
- `source/game/avatarchoice/intro_cinematic.cpp` (`IntroCinematic::load`) - a scene with no island
  (`bWorld = false`, no streaming) turns them off. `loadAround` loads every area tile whatever the radius and
  the closed room's frame sits at the Ibiza origin, so that tile cut through the hall floor (~-0.6 m) and hid
  the sunken carpet pit and the `Socle_Car` podium (both were loaded all along). `-closedroomareas` = A/B.

### Verified
- `docs/images/casino_hall_carpet_podium.webp`: from the entrance and close up - the red runway, the lit
  podium, the R8 on it.
- Other closed rooms with vs without `-closedroomareas`: FIRST_WM garage 0 px changed, casino slots 3 px,
  VIP lounge lighter overall (the Ibiza tile also shadowed it).

### Ruled out
- `-hidematerial Floor_gloss` (the alpha gloss layer over the floor): identical frame.
- `-noterrain`: identical frame. `-noareas`: the carpet and the podium appear -> the area tiles.

### Not done / next
- The hall's unlabelled black box right of the podium (visible in both builds) is not identified.

## Session 2026-11-05 (road signs, `win`): the Ibiza signs match the x360 photos

The owner: "I was hoping you could fix our road signs as there not accurate and some are broken in some way
or another" - four x360-recomp photos as the reference (TdirIb "Centre / Platja" and "Sant Rafael / Hisenda
Aldea Oest / Platja", Dist_D "Centre 1 / Platja 1 / Puerto 1", DirR_D "SANT MIQUEL / Residencial Nord /
Foradada"). The same message says the casino hall lacks the red carpet and the podium with the car on it
(not started this session - see Not done).

**Reproduced** (the photo signs, found with `tools_probe/sign_locate.py -match`; TdirIb faces look along +Z,
so stand on the far side of the probe's `-spawn`):

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -island ibiza -skipcine -nopad -fixedtimestep -clock 13:00 -tp 11847.5,5.5,16183.7 -heading 170.3 -width 1600 -height 900 -autoplay "300:press=camera;302:press=camera;360:shot=C:/OpenTDU2-win/_sg.ppm" -exitframe 370

(Sant Rafael TdirIb_C; Centre/Platja TdirIb_B `-tp 12305.2,7.5,16575.2 -heading 258.5`; Dist_D
`-tp 12110.2,5.4,16264.6 -heading 251.6`; DirR_D SANT MIQUEL `-tp 11649.7,4.5,16148.9 -heading 244.3`.)
Before: no arrows, no pictograms, only the first TdirIb row had a yellow tip, the middle plate of the 3-row
TdirIb squashed with the spare bar's outline, black text on green, SANT MIQUEL mirrored, the distance sign's
green header "Centre 1 | 1 entreC", the highway panel's rows rotated 90 degrees.

### What changed
- `source/world/sign_text.{h,cpp}` (sign_text_spec 6):
  - rows keep their pictogram byte (`Icon`) and raw distance byte (`DistanceCode`); the code distance goes on
    every row, a zero distance byte repeats the row above (the photo's "1 / 1 / 1").
  - TdirIb: the row byte is the arrow (14 right, 12 left - photo; 11 up, 13 down [INF]); a black arrow in the
    yellow tip, names narrow to fit before it.
  - stacked plates: each member's orientation is measured (`rs_tdirib_c`'s middle plate maps the cell upside
    down) and un-flipped by the remap; the member's source band (`SrcV0/1`) is copied over its spare bar in the
    face, so every row keeps the plate's look (yellow tip on all rows).
  - `FlipU` is measured per zone from the quads (u at min / max model x): `rs_dirr*` reads straight - the
    family rule mirrored every right-pointing direction sign; `rs_dist` green header is a folded mapping,
    unfolded in the geometry hook (`UnfoldMirroredPlates`) + mirror-copied texels; `rs_hwdist` is a rotated cell
    (`bRotated`); town signs drop the phantom "8" (a model id: the census, spec 6.1).
  - lettering: the gauges movie's "Arial" with a padded faux bold (white on the green bar); procedural framed
    pictograms (beach, harbour, airport, old town); the rs_dir row stays off the pointed end.
- `source/world/eden_world.cpp` - `-logsigns` names the model before its plate quads are logged.
- New flag: `-signfontberlin` (the old Berlin Sans FB lettering, A/B).

### Verified
- `docs/images/sign-faces-2026-11-05.webp`: the four photo signs + a DirL_D and the HwDist_C panel, all reading
  upright with the photo's arrows / pictograms / colours.
- `-logsigns`: `remapped 3 stacked plate row(s) ... (1 upside down)` on rs_tdirib_c; `unfolded a mirrored
  plate at u 0.166 (4 vertices), zone u 0.005..0.328` on rs_dist_b/d.
- After the reformat to the house 120-column style: the seven sign shots re-run, mean abs diff 0.00 on all
  channels (pixel-identical). 900-frame Ibiza town drive `-tp 12605.4,16698.1 -snaproad 150 hold=accel`:
  exit 0, 0 stalls.

### Ruled out (with the test that ruled it out)
- A sign font / arrow sheet in the data: a name scan of every non-sector bank (font, sign, arrow, picto,
  panneau...) finds only Hawai'i props, the frontend fonts and road-paint `arrows.2db` / `road_arrows.2db`
  (dumped: white paint arrows) - so arrows and pictograms are drawn procedurally [INF].
- The TdirIb "distance" being kilometres: 11/12/13/14 only, 1,832 rows; 12/14 sit back to back at one junction.

### Not done / next
- The casino hall (owner, same message): done in the next entry (the island tile through the floor).
- 11/13 arrows and the airport / old-town pictograms need an x360 shot (sector-3-3-4-7 "Barri antic 13").
- HwDist "EIVISSA ... 1 2": two digits inside the name window, meaning unknown (drawn as text).
- `.clang-format` says ColumnLimit 80 but the tree is written at 120 - formatting a whole file with it churns
  ~600 lines; format only your hunks (or a 120 copy of the config).

## Session 2026-11-05 (walls + floors in every house, `win`): all 21 living rooms checked

The owner: "Yes, check others".

### What changed
- Scanned the 21 `lph_*` material libraries: every one has `Mat_Floor1_<nn>`, 20 have `Mat_Wall1_<nn>`
  (not the caravan), l2e has a second floor `MAT_Floor2_23` (houses_spec.md 13.1 has the table).
- `intro_cinematic.cpp applyRoomMaterial`: the match is `mat_floor<d>_<n>` / `mat_wall<d>_<n>` (lph_l1bi's
  `MAT_FLOOR_03` is another surface).
- `gm_freeride.cpp`: `-enterhouse <DBHouse spot>` test hook (straight into any living room).

### Verified
- `-tp 22000,23000 -enterhouse <spot> -walkaim diary -surfaceedit floor|wall`: EPH_L2E_0345I 2 floor / 1 wall
  material(s), EPH_L1D_1455I 1 / 1, EPH_L0C_1254I 2 / 0; screenshots
  `docs/images/house_mytduhouse_floor_other_houses.webp` (l1d wood -> marble, l2e slate -> marble). Exit 0.

### Not done / next
- The other 17 banks were checked by their names only, not entered. The `_level` gate; HOME's own item menu.

## Session 2026-11-05 (MyTDUHouse walls + floors, `win`): the walls and the floor can be changed

The owner: "Yes, walls and floors".

### What changed
- `house_furniture.{h,cpp}`: the DBMaterial catalogue (`ground_nn` floors `_type` 1, `wall_nn` walls `_type` 2).
- `intro_cinematic.cpp`: `loadFurnitureModel` also reads Scenery-entry banks (the finish banks);
  `applyRoomMaterial( prefix, bank )` copies the finish's textures into the room's `Mat_Floor*` / `Mat_Wall*`
  materials slot by slot type, keeping the room shader and lightmap; originals kept for revert.
- `interior_walk.cpp`: in MyTDUHouse, A with nothing selected edits the floor (looking down) or the walls;
  Left / Right, A buy, B revert; saved as `#floor` / `#wall` choices, applied at load; `-surfaceedit`.
- Controls: in MyTDUHouse, **Cross / Enter** looking at the floor or a wall (no furniture under the reticle),
  then **D-pad left/right**, **Cross / Enter** buy, **Circle / Esc** cancel.

### Verified
- Floor: own -> Light Marble Tiling ($5000) -> Massaranduba Parquet ($4000) bought, saved `#floor=ground_02`,
  reloaded on the next run (`house_mytduhouse_floor.webp`). Walls: Tuscany-red -> Titanium-grey Concrete ->
  Yellow-saffron Plaster (`house_mytduhouse_walls.png`; 7.9 / 8.7 % of the pixels changed).

### Ruled out
- Copying the whole finish material: the floor drew flat white. The finish's lightmap slot ("...LIG",
  its FAKE_LM) was the cause; the slot copy now skips it.

### Not done / next
- The other houses' surface names (only lph_l3di read), the `_level` gate, HOME's own item menu.

## Session 2026-11-05 (MyTDUHouse colours, `win`): furniture colours and matching sets

The owner: "Yes, colours next".

### What changed
- `eden_model.{h,cpp}`: `setMyHouseColor( n )` re-points the items drawn with `_MH<part>_<nn>` at
  `_MH<part>_<n+1>` (A / B / C at once), `countMyHouseColors()`.
- `intro_cinematic.{h,cpp}`: the slot keeps its colour (`DefaultColor`, then the choice) and `Group`;
  `setFurnitureColor`; a model swap keeps the colour; choices are `<bank>#<colour>`; a suffixed slot reports
  its base bank.
- `interior_walk.{h,cpp}`: the edit has Up / Down for the colour, edits the whole Group (dining chairs,
  bar stools) together, B reverts all of them, colour-only changes are free.
- Controls: in the MyTDUHouse edit, **D-pad up/down / arrows** change the colour (left/right the model,
  **Cross / Enter** buys or applies, **Circle / Esc** cancels).

### Verified
- sofaL_A: colour 1/5 -> 2/5 -> 3/5, applied, `sofal_a#2` (`house_mytduhouse_colours.webp`).
- ChaiBarD_01 (group 3): "3 slot(s)", CONTEXT -> BUBBLE on all three, colour 2, $600
  (`house_mytduhouse_group_barstools.webp`). Reload: every slot logs its saved bank + colour. Exit 0.

### Not done / next
- Walls / floors (`MAT_WALL1_`, `MAT_FLOOR1_`, `ground_01..25`), the `_level` gate, HOME's own item menu.

## Session 2026-11-05 (MyTDUHouse furniture, `win`): the furniture of a house can be changed and bought

The owner: "Yes, furniture next" (the TDU Life / MyTDUHouse furniture left open by the entry below).

### What changed
- `source/game/spots/house_furniture.{h,cpp}` (new): the DBFurniture catalogue (`_bank`, `_type`, `_price`,
  `_name_hc`, `_iscasino`; `-logfurniture`). A slot's Type = row `_type` - 1 [DATA, three pairs].
- `intro_cinematic.cpp`: `loadFurniture` split into `loadFurnitureModel` (one slot from one myhouse bank),
  `getFurnitureSlots`, `replaceFurniture` (live swap; the old model is retired until unload),
  `setFurnitureChoices` (the saved choices applied at load).
- `interior_walk.cpp`: furniture selectables (MyTDUHouse only), the edit (Left / Right swap, A buy, B revert),
  `-furnaim <slot>`. `player_profile`: `FurnitureChoices` (saved `furniture_choices`).
- Controls (DualSense / keyboard): in MyTDUHouse, **Cross / Enter** on a piece starts the edit, **D-pad
  left/right / arrows** change the model, **Cross / Enter** buys, **Circle / Esc** cancels.

### Verified
- `-home -walkaim diary -furnaim sofaL_A` + Info -> MyTDUHouse: "13 furniture selectable(s)", "editing
  'sofaL_A' (type 8, sofal_a), 13 model(s)", right x2 -> sofal_b, sofal_e, A -> "$20000 (money 480000)",
  saved; the next visit logs "furniture 'sofaL_A' (type 8, bank sofal_e)"
  (`docs/images/house_mytduhouse_furniture_swap.webp`). Exit 0.

### Not done / next
- The colour variants inside a model (`DefaultColor`, `_MHA_/_MHB_/_MHC_xx` materials), walls / floors, the
  `_level` gate, HOME's own MyTDUHouse item menu (the hint box stands in). houses_spec.md 11.

## Session 2026-11-05 (dealers + house UI, `win`): the dealers are the retail SHOPSCAR screen, the house runs HOME.vmf (closet, garage pick, MyTDUHouse)

The owner: "I want you to try wire houses wardrobes, garages, tdu life (this allows changing furniture) lastly
dealerships can we plug correct ui believe uses same as the used car dealership".

### What changed
- **Dealers** - `source/game/shop/gm_shop.cpp/.h`: GMShop (the story used-car dealer, SHOP mode + SHOPSCAR.vmf)
  takes a `FreeRideShopRequest` (player_profile.h) for any DBCarShop spot: interior `i<bank>`, exterior
  `<bank>`, the spot's slots, page by spots.bin type (0x11 `scr_open`, else `car_dealer`), `C_INTRO_I`,
  no story step / loan car / tutorial. Buy -> `C_BUY_CAR_I` -> FREERIDE at the entrance in the new car;
  Back -> dialog -> `gofr` -> FREERIDE. `gm_freeride.cpp enterDealerShop` hands over at a `cardealer`
  door (`-nativedealer` = the old walk + native panels). spot_interiors_spec.md 6.
- **The driven car** - `PlayerProfile::DrivenCarId` (saved `driven_car`): FREERIDE loads it at enter
  (owned check), so a purchase / garage pick needs no runtime model swap (the known device wedge).
- **House UI** - `source/game/spots/interior_walk.cpp/.h`: the house rooms load HOME.vmf (`-nohomeui` = A/B),
  its FSCommands are answered: the walk-in closet (`clin` / `neit` / `prit` / `seit`, `/:BtValid`,
  `/:Nb_Items`, `/:ItemName`), the Diary's Info menu, MyTDUHouse open + the GENERAL quit dialog
  (`dbxo` / `dbxc`). houses_spec.md 10 has the full table.
- **Worn clothes** - `PlayerProfile::WornClothes` (saved `worn_clothes`), `eden_avatar.cpp` swaps the slot's
  DBPNJ row for the worn DBClothe row on the player's config (`-noworn` = A/B).
- **Garage pick** - `gm_freeride.cpp`: A within 3 m of an owned car in the house garage sets the driven car;
  leaving the house after a change re-enters FREERIDE at the house entrance in it.
- **Crash fix** - `gm_freeride.cpp/.h`: `static bool bPuddlesWired` (and the FRIM bank / skill-event wirings)
  captured the first GMFreeRide's `this`; the second FREERIDE of a session (back from a shop, a test drive,
  the car wash) crashed in `EdenSurfaceFx::updateLightPools -> EdenWorld::getHeight` (cdb + a linker
  `/MAP`). Now per instance, cleared in `exit()`.
- Tools: `-dumpbuttons` also dumps movies loaded later and the UI viewer's movie (`DumpMovieButtons`
  public); `-shopspot`, `-shopaim <n>`, `-shoppage`, `-walkaim <name|type|car>`;
  `IntroCinematic::hasCinematic`, `WalkMode::placeLooking`.
- Controls (DualSense / keyboard): unchanged bindings - **Cross / Enter** validates (Buy, pick a car, wear),
  **Circle / Esc** backs out (leave dialog, closet back, Quit MyTDUHouse), **D-pad / arrows** move in the
  menus and page the closet strip.

### Verified
- `-gm SHOP -shopspot ECD_FERR_3337I -shopaim 4 -money 900000`: the 458 Italia's price + spec panel, the
  Buy / Get into the car / Data / Compare / Test drive menu (`dealer_shopscar_car_dealer_menu.webp`);
  validate x2 -> `buin` -> "bought ... money 699000" -> `C_BUY_CAR_I` -> FREERIDE "driving the profile's
  car 391 '458_Italia'" outside the dealer (`dealer_shopscar_buy_drive_out.webp`).
- `-gm FREERIDE -tp 21803,24216`, validate at the Ferrari door -> SHOP; back -> dialog -> `gofr` ->
  FREERIDE at the entrance; exit 0 (exit 139 before the crash fix; `-no3d` survived, `-nointeriorworld`
  survived - the streamed island + a second instance).
- House (`-home`, profile with clothes 419 / 505 / 285 / 287 / 307 and cars 391 / 387):
  `-walkaim dressing` -> Walk-in closet -> Hats (3 items: No hat + 2) -> right -> OK -> "wearing 419
  'Cowboy hat'", saved `worn_clothes = 5:419`; Back -> `avou` -> walk (`house_closet_wear_hat.png`). The
  player then walks into the Ferrari dealer in the hat (`house_closet_hat_worn_in_shop.webp`).
  `-housegarage -walkaim car` -> A -> "garage: driven car -> 391", back -> FREERIDE in the 458 at the house
  entrance (`house_garage_pick_car.webp`). `-walkaim diary` -> Info -> MyTDUHouse -> Back -> "Quit
  MyTDUHouse?" -> OK -> `myhs:clos` (`house_mytduhouse_quit_dialog.png`).

### Ruled out
- The dealer screen's missing header on new-car dealers is the movie: `car_dealer` draws no title strip
  (UI viewer `-goto /:car_dealer`); `scr_open` is the "Used Car Dealer" one.
- `ECD_FERR_7841` is the **Hawai** Ferrari row (no `I` suffix), not a broken Ibiza spot.

### Not done / next
- MyTDUHouse furniture: nothing is selectable in the mode yet. Next: make the `Entity__MyHouseObject` slots
  walk selectables (`IntroCinematic::loadFurniture`) and log the FSCommands HOME sends on one.
- The closet camera (`came`) - the walk stays first person; the brand logo frames of `/:logo_Brand`.
- HOME's own `/garage` page (`change_place`, `list`); MyTDULife / My Photos / Leaderboards content; Media.
- The owner's in-play check of the dealers (the native `-nativedealer` path keeps the old "Get into the car").

## Session 2026-11-05 (police HUD, `win`): the traffic-violation gauge is the retail bar, and the Chase Mode help explains it

The owner: "fix the traffic violations - there's normally a ui element with a bar that fills up before police
will chase you, also there's some dialog explaining it".

**Reproduced** - the "meter" was `/HUD/tailing` (char 707), whose readout is `/:distance_KYD`: the online
**Keep Your Distance** tailing event, not the police. It drew a red car + "333999m" over the build stamp
(`-policeviol`, shot at 260). The Help: Chase Mode popup (`-policehelp`) drew nothing: the port hides
`/help_system` at load and never showed it again, and nothing routed the pad to it.

### What changed
- `source/game/freeride/freeride_hud.cpp` `setChaseHud` - drives the retail gauge `/HUD/gauge_content/gauge`
  (sprite 502) [DATA: `-dumphud -dumpactionbytes -dumpsprite 502,501,487,478`]: `gotoLabel open` (its f8
  script reads `/:cur_gm`, a free-ride mode plays on to `up` - the slide-in), `close` f282. The fill is
  `content` (sprite 501)'s frame: 1000 frames slide the bar 160 px under its mask, **frame 1 full, 1000 empty**.
  `content/ic` + `content/bar` take the mode label: `infringement` before the chase (icon = police badge,
  text EditText 467 bound to `/:m_Infraction` -> loc 763 "Traffic violation"), `escape` during it (the
  flashing car, `/:m_escape_` -> loc 1978 "Escape", fill = the break-away `ChaseMode::getEscape01()`;
  which mode the retail chase shows is [ENC], escape is [INF]). `_root.isNoCursor = true` hides the cursor
  (the `up` frame script). `gauge2` stays hidden. The old tailing code and its `copDistance` are gone; the
  chase timer no longer writes its digits into `/:m_escape_` (that is the gauge label).
- `openChaseHelp` shows `/help_system` and fills the two pages' texts `/:m_TutoChaseMode1..4` = loc
  6681 / 6682 / 6683 (with the tokens 8 / 35 [INF], as ChaseMode) / 6684 [DATA: hud.bnk EditText bindings,
  FR authoring defaults]. `routeChaseHelpInput` = the casino help protocol: validate -> `_root.Clip.A()`,
  back -> `B()` (both close), left / right -> `Move('left'|'right')` (pages). `tickModal` advances the movie;
  the clip is hidden 0.8 s after `chmo:hsys:clos`.
- `gm_freeride.cpp` - the popup is **modal** like the pause menu (world waits, engine audio ducks). It opens
  1.5 s after the **first violation a police car witnesses**, once per profile (`PlayerProfile::ChaseHelpSeen`,
  saved as `chase_help_seen`) [INF: the retail seen flag is ENC]. `-nochasehelp` suppresses it;
  `-policeviol` now behaves like a witnessed violation (goal line + help).
- Controls (DualSense / keyboard): **Cross / Enter** or **Circle / Esc** closes the help, **D-pad left/right /
  arrow keys** turn its pages.

### Verified
- `-gm FREERIDE -skipcine -nopad -fixedtimestep -policeviol` (fresh userdata copy): frame 200 the gauge reads
  "Traffic violation" with the badge, 55 % filled left->right green->yellow
  (`docs/images/police_violation_gauge.webp`); 290 the help page 1 (ranks table, `police_chase_help_p1.png`);
  `press=right` -> page 2 with "8 consecutive wins" / "35 victories" and the police photos
  (`police_chase_help_p2.png`); `press=validate` -> "Chase Mode help closed", the world runs again.
- `-police` (chase): the gauge switches to "Escape" with the car icon (`police_escape_gauge.png`).
- `-policeviol -nochasehelp -no3d`: the bar has drained at frame 400 and the gauge is gone by 1100.
- `-gm` test runs have no character name, so `SaveProfile` skips them by design - the seen flag was not
  checked on disk; it saves in a real profile.

### Not done / next
- The awareness numbers (34 per witnessed hit, 6/s decay after 3 s, 100 to start the chase) are still [INF];
  the retail laws are [ENC] - PS3 twin search for the `infringement` string (pc_ps3_anchors.json: ps3_func
  0x9C3504) is the start.
- `/HUD/chase_mode_target` / `cop_detector` (online cop-side widgets) untouched.

## Session 2026-11-05 (GLM's build-459 findings, `win`): casino rooms open inside, the Clothes reel reads in bounds, the off-road ring steps aside for the level-up popup

The owner: "Are you able to look into the other stuff and fix please". The list
is GLM's build-459 sweep (`C:\OpenTDU2-glm\docs\images\glm\`, uncommitted there).

### What changed
- `source/game/casino/casino_interior.cpp` `loadRoom` - **every** room skips its
  `C_INTRO` (was the hall only). The "roulette / poker lose the room, dune
  backdrop" and "VIP spawns misplaced, dressing under a sofa" reports were all the
  intros: each frames the dunes + casino facade from outside (needs the exterior
  bank). `-casinoroomintro` = A/B. GLM's roulette shots at 250/500/1050 were all
  inside the intro / the screen opened over it.
- `startWalk` - VIP: `cam_from_Hall` is at (0.70, 7.24, 23.19), outside the room;
  starts at `Cam_FirstPerson` (0.57, 5.74, -29.98). `CAM_RANKINGS` tried and
  rejected (faces the screen at 1 m). `-logcasino` lists every room camera +
  position; the "walking" line now prints the start position.
- `source/game/casino/casino_slots.{h,cpp}` - **real out-of-bounds read**: the
  strip is built from the machine's first N symbols (Clothes 4 -> 47 stops, Car 6
  -> 61) but every read wrapped at `kStripLength` 64 -> `strip[47..63]` past the
  vector (the Clothes middle reel's blank seam). All reads wrap at `stripSize()`.
- `source/game/freeride/gm_freeride.cpp` - the return-to-road ring /
  "Return to the road" is not shown while the level-up popup is up (it showed
  through the translucent panel; the enter-spot prompt and spec card already
  stepped aside).

### Verified
- Rooms at frame 100 (`-gm CASINO -casinoroom <r> -fixedtimestep`): roulette,
  poker, VIP inside (`docs/images/casino_room_start_{roulette,vip,poker}.png`);
  before: dunes in slots/roulette/poker/VIP, sofa in dressing; with the intro
  off, the dressing walk ('C_main') was already fine.
- Slots self-test 50000: RTP 98.26 %, balance 99921750 = expected - identical to
  GLM's 459 numbers (64-stop machines unchanged). Clothes machine
  (`-casinoroom slots -casinoselect 0`, spin at 300/950): the drum rests on a full
  avatar (`docs/images/casino_clothes_reel_fixed.webp`; GLM's settled2 had a blank top).

### Ruled out / not bugs
- **"Spins: 0 never increments"**: GLM's own result shot shows `Spins 1`; it
  increments at the settle, after the wheel time.
- **"Level UP! double-draws"**: the movie's zoom-echo intro - a larger fading
  copy over ~20 frames, gone by the hold (`-testlevelup`, shots every 8 frames
  272..392; `docs/images/levelup_echo_anim.webp`).
- **Podium head "inside-out" (`C_A5_PODIUM_TESS`), `sta_firstintro` /
  `isl_firstintro` underlit + floating car**: all CINVIEWER captures (the browser
  panel is in the shots) of cutscenes **no game mode plays** (`grep` finds no
  caller). The podium one is the camera inside Tess's head (the back of the hair
  seen from inside), i.e. the scene's placement without its race end-spot; not a
  mesh fault. They become real work when the race podium / race school story
  steps exist.

### Not done / next
- `C_MEETING_TESS` at ~62 s: a climbing plant on the pillar left of the door
  renders flat yellow (GLM `cine_tess459_62s.png`). Unverified cause - next:
  the Tess story cut with `-logmaterials` and a shot at 62 s, find the plant's
  material and its texture slots.
- "A second A press during a slot spin didn't register" - not looked at (likely
  the spin input lock by design).
- `car_dealer C_BUY` black in CINVIEWER (NaN camera, needs its showroom context);
  Solar Crown ext carpet near-plane clip at ~3 s - not looked at.

## Session 2026-11-05 (casino self-test watchdog, `win`): `-pokerselftest` was killed by the watchdog, not crashing

GLM's sweep on build 459 reported "`-pokerselftest` crashes the process (exit
87) after ~950-1000 hands; 600 hands passes, 1100 dies at ~953, 5000 at ~1001,
log cut mid-line".

**Diagnosis** [CODE]: `CasinoPoker::selfTest` scores the exact optimal hold per
hand (32 masks, the full discard alone is C(47,5) = 1.53 M draws), all on the
frame thread from `GMCasino` init, with no `Watchdog::Beat`. At the 60 s kill
threshold the watchdog terminates with `0x43544157` ('WATC'), whose low byte is
`0x57` = **87**. The "rare hand" was just elapsed time; the cut log line is
`TerminateProcess` mid-write.

### What changed
- `source/game/casino/casino_poker.cpp` - `Watchdog::Beat( "pokerselftest", h )` per hand.
- `casino_slots.cpp` / `casino_roulette.cpp` - the same beat every 1024 spins
  (fast today, but the same frame-thread loop shape).

### Verified
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm CASINO -skipcine -no3d -nopad -log C:\OpenTDU2-win\_poker1100.log -pokerselftest 1100
- exit 0 after 90 s (it died at ~60 s before): `evaluate: 13 cases, 0 failed`;
  `1100 hands ... EV mean 95.43% +- 3.14% (1 SE)` (99.54 % reference inside 1.5 SE);
  `balance 99999480, expected 99999480 (ok)`; watchdog `1104 beats, 0 stalls`.

### GLM's other findings - worked the same session (next entry up)

## Session 2026-11-05 (occlusion at 4K, `win`): 4K High drive ~16.5 -> ~11.8 ms (61 -> 85 fps); the depth readback moved to a GPU tile reduction

The owner: "Yes, check 4K".

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -profile high -width 3840 -height 2160 -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -loggputimes -logocclusion -autoplay "60:hold=accel" -exitframe 1250

### Found and fixed
- **The full depth copy cost 3.5 ms of GPU at 4K** (`-loggputimes` prepass 0.7 -> 4.2 ms: 33 MB a frame to host memory
  over PCIe). New `resources/shaders/depthmax.frag` (-> `depthmax_frag.h` via `tools/compile_shaders.py`): one R32
  texel per tile = the tile's farthest depth, drawn into `OcclusionTiles` (~160 x 90) between the static world and
  the moving objects; only that (~60 KB) is read back, the worker just unpacks it. GPU pre-pass 0.63 ms (0.62 without
  the cull). Same culled counts as the CPU reduction.
- **Far-tree impostor cards were culled with their instance box** (367 faint pixels on a tree line at 4K frame
  1140): the cards are sized to the bounding sphere and stick out of the box. `drawInstances` now tests the cube
  around the sphere. A 0.5 m + 0.2 %-of-distance margin was added to every box (`Grid::isHidden`) as well.

### Verified (4K High, same build, `-noocclusion -nooceanskip` vs default)
- Frame avg (120-frame windows 240..1080): 15.2-18.7 ms -> **10.9-13.7 ms** (~61 -> ~85 fps). 72-82 % of the tested
  boxes culled, grid in use 117-120 / 120 frames (the worker keeps up).
- Pixels: frame 1140 identical; frame 600: **2 pixels differ by 9/255** (16 x 17 area at 1936,923) - real (two
  `-noocclusion` runs are identical to each other), cause not found: the sphere cube for model items did not change
  it (and cut the cull to 76 %), so items keep their box.

### Not done / next
- The 2-pixel residual at 4K frame 600.
- The ocean reflection after turning from town to the sea (2-3 stale frames) is still unmeasured.

## Session 2026-11-05 (occlusion cull + ocean skip, `win`): the hidden world is no longer submitted - town 15 -> 11 ms, drive 16 -> 13 ms; the ocean mirror pass is skipped when no water shows (-2.5 ms more)

The owner: "Could do both" (the occlusion cull and the ocean-reflection skip) / "Please continue with those".

### What changed
- `source/render/eden/eden_occlusion.*` (finishes the WIP entry below): a pool of pre-pass depth readbacks, a
  worker thread that reduces a finished one to a tile-max grid (~160 tiles across), `Grid::isHidden` (the
  `-occlusionprobe` test, projected with the GRID's view-projection; off its screen / at its near plane = drawn),
  `Grid::isPlaneVisible` (does the water plane show in any tile: the plane's depth at the tile corners vs the tile's
  farthest depth), camera-cut guard (12 m / 25 degrees) and a **world epoch** guard (`EdenWorld::getDrawEpoch`, bumped
  when the drawn sector set changes: a grid whose depth still had the coarse area terrain a new sector replaced is
  not used - that was the 42-pixel difference after the sector crossing).
- `EdenRenderer::setOcclusionGrid` / `isOccluded`: `isBoxVisible` (model items) and `EdenWorld::drawInstances`
  (.pmi instances) drop the hidden boxes while a grid is set.
- `EdenSceneView::renderOffscreen`: the grid is set around `world.draw` of the pre-pass and of the opaque pass only
  (blended draws are deferred from that traversal; shadows / reflections / env never culled). **The moving objects
  (`drawObjects`: the player's car, traffic, people) are drawn after the depth copy and never culled**: the car
  moves with the camera, so in a 2-3 frame old depth its body stood where the world now is and parts behind it
  were culled against it (2,600 differing pixels behind the car before this split). Not in the map view.
- Ocean: `ocean.renderReflection` is skipped while the grid shows no water (`-nooceanskip` = the A/B).
- Flags: `-noocclusion`, `-nooceanskip`, `-logocclusion` (culled counts every 120 frames, ocean skip on/off).

### Verified (1280x720, `-fixedtimestep`, same build, with vs without)
| Run | frame avg off -> on | pre-pass / opaque CPU |
|---|---|---|
| Ibiza town `-tp 12605.4,16698.1` | 13.0-15.1 -> 11.4 ms (cull), -> **9.2 ms** with the ocean skip | 2.8 / 2.9 -> 1.6-1.8 / 1.6 |
| benchmark drive `-tp 21796.3,24188.1` | 15.8 -> 13.1 ms (cull), 13.0 -> **10.5 ms** with the skip | 3.0 / 3.1 -> 1.7-2.0 / 1.7-1.9 |
- `-logocclusion`: 80-85 % of the tested boxes culled, grid in use 117-120 of 120 frames.
- **Pop-in**: pixel diffs vs `-noocclusion`: town frame 300, drive 300 / 600 / 900 / 1140 identical (bbox None outside
  the build stamp); a hard left-right turn in town (frames 220-420): identical except a 3x3 spot under 8/255 at 300.
- Ocean skip: identical frames in town / the drive / the wall spot `-tp 36096,5760`; at the beach camera
  (`-gm WORLD_VIEWER -campos 36070,12,5960 -camyaw 90 -campitch -15`, sea in view) the skip never triggers and the
  frame is identical.
- Validation (1250-frame drive, cull + skip, a sector crossing): no VUIDs; the exit leak is the same 4 objects as before.

### Not done / next
- When water comes into view after a skip, the first 2-3 frames use the last reflection rendered (unmeasured: no
  scripted shot turns from town to the sea).
- Not measured at 4K (the readback is 33 MB a frame there, reduced on the worker; check the worker keeps up with
  `-logocclusion` "grid on N/120").
- Shadows are the largest pass left (3.6-4.3 ms) and cannot use this cull; PERF_PLAN 0a has the shadow ideas.

## Session 2026-11-04 (occlusion cull, `win`, WIP - stopped at the usage limit): the EdenOcclusion module is written and compiles, not wired in yet

The owner: "Could do both" (the occlusion cull and the ocean-reflection skip).

### What changed
- `source/render/eden/eden_occlusion.{h,cpp}` (new, **unused so far**): pool of pre-pass depth readbacks, a worker
  thread that reduces a finished one to a tile-max grid (~160 tiles across), `Grid::isHidden` (the
  `-occlusionprobe` test, projected with the GRID's own view-projection; off the old screen / near plane = visible),
  camera-cut guard (12 m / 25 degrees). `-noocclusion`.
- `RenderDevice::invalidateBuffer` (readback memory may be non-coherent).

### Next (in order)
1. `EdenRenderer`: `const EdenOcclusion::Grid *pOcclusionGrid` + `isOccluded( min, max )`; in `isBoxVisible` (world AABB,
   as the probe computes it) and `EdenWorld::drawInstances` (after the frustum test) skip a hidden box.
2. `EdenSceneView::renderOffscreen`: `occlusion.beginFrame( device, camera pos, forward )` before the pre-pass; set the
   grid for the pre-pass and opaque `world.draw` + `drawObjects` only (never shadows / reflection / env); after the
   pre-pass `occlusion.afterPrepass( device, pSceneTarget, renderer VP, ... )`; `destroy` with the view.
3. Measure: `-logrecordtimes` prepass/opaque with and without `-noocclusion`; pop-in = pixel diff of a
   `-fixedtimestep` drive at several frames vs `-noocclusion`; validation run.
4. Ocean skip: skip `ocean.renderReflection` when every ocean item box (island / area / sector) is hidden in the grid.

## Session 2026-11-04 (occlusion probe, `win`): 76-80 % of the depth pre-pass draws are hidden behind something - occlusion culling is worth building

The owner asked whether culling "so you only see what the camera sees" would help, then: "Yes, measure it".

Already in the port: frustum culling (per instance, per model item), per-instance LOD distances, small-object culling,
batched far-tree impostors. Missing: **occlusion** culling (what is behind a building / a hill / a tree).

### What changed
- `-occlusionprobe <f1,f2,...>` (`source/render/eden/eden_scene_view.cpp` `OcclusionProbe` /
  `EvaluateOcclusionProbe`, `EdenRenderer::beginOcclusionProbe` / `ProbeDraw`): on those scene frames every draw of
  the depth pre-pass records its object's world box (`isBoxVisible` sets it for model items, `drawInstances` for
  the .pmi instances; terrain and batches have none and count as drawn); the pre-pass depth is read back
  (`copyRenderTargetDepth`) and evaluated when its frame slot comes round. A box is **hidden** when its nearest depth
  is behind the farthest depth of the 8x8-pixel tiles it covers - the conservative hierarchical-Z test; an object can
  never hide itself. It measures; nothing is culled. Log line `[occlusion]`, picture `_occlusion_f<frame>.ppm`
  (depth, hidden boxes outlined red).

### Verified (1280x720, `-fixedtimestep`)

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 12605.4,16698.1 -snaproad 150 -carlod 4 -occlusionprobe 300 -logscenetimes -logrecordtimes -exitframe 420

| Spot | pre-pass draws | hidden |
|---|---|---|
| Ibiza town, tuning-shop street (`-tp 12605.4,16698.1`) frame 300 | 2,422 | **1,933 (79.8 %)** |
| benchmark spot (`-tp 21796.3,24188.1`) frame 300 | 2,776 | 2,106 (75.9 %) |
| benchmark drive frame 600 / 1000 | 3,526 / 2,604 | 77.3 % / 76.2 % |

- The pictures (`docs/images/occlusion_probe_town.webp`, `occlusion_probe_benchmark.png`: frame | depth + hidden
  boxes) line up with the frames: the hidden boxes are the town behind the street-front buildings, the band of
  sector objects / instances behind the hedges and trees on the horizon, and the car's own inner parts behind its body.
- Per-pass CPU at the town spot (`-logrecordtimes`): ocean 2.5 ms (2,013 draws), prepass 2.5 (2,332), shadows 3.6
  (3,113), opaque 2.6 (2,172).

### What it means
- The pre-pass and the opaque pass submit the same world traversal: **~5 ms of CPU a frame at 720p (5.4 at 4K High,
  PERF_PLAN 0a), of which ~3/4 draws nothing visible.** Upper bound of the gain ~3.5-4 ms of a ~15 ms frame (the
  probe is an oracle: it uses the same frame's depth; a real cull sees less). Shadows cannot use it (their casters
  are off-screen by nature).
- Separately: the **ocean reflection pass draws ~2,000 draws (2.5 ms) in the middle of town** - the whole world from
  the mirrored camera, with no sea on screen. Skipping it when no ocean tile is visible is a smaller, simpler win.

### Not done / next
- Build the cull, cheapest first: test each box against the pre-pass depth of a frame ago (the readback this probe
  already does, at a downsampled size) with **that frame's** view-projection, and keep a box drawn unless it was
  hidden in two consecutive tests (objects coming out from behind a building may still appear a frame late - measure
  it with a fast camera turn). The latency-free alternative is a CPU occluder rasterizer (big building boxes +
  terrain) - more work. A/B flag and pixel diffs as usual.
- Skip the ocean reflection when no ocean tile passed the frustum test.

## Session 2026-11-04 (load-ahead streaming, `win`): the sector-crossing hitch is gone - the next ring loads one sector a frame, banks prefetched to RAM, height queries on a grid

The owner: "I want you to do sector loading but I was wondering if something like culling would work ... Or if we
can try maybe utilising more ram as a benefit?"

**Measured first** (`-logsectortimes`, new: per-stage ms of each sector load + the streamWorld split): the crossing
at frame 1132 of the benchmark drive was **world 31 ms (3 sectors x ~10: bank 6, heightfield 2, objects 2-6,
instances 1) + collision 24 ms** in one frame - streaming only ran when the car entered a new sector, and the
collision streamer re-read the same 3 MB sector bank the world had just read.

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -hitchms 33 -logsectortimes -autoplay "60:hold=accel" -exitframe 1250

### What changed
- `source/game/freeride/gm_freeride.cpp` `streamWorld` - **load-ahead**: while the car stays in its sector, one
  step per frame loads the ring one beyond the streaming square (`radius + 1`), the sector nearest the heading
  (`streamHeading`, 600 m ahead) first; world first, then collision. Starts 30 frames after a sector change (a
  spawn / teleport absorbs its own loads first) and goes idle once the ring is complete. `-nopreload` = the old
  synchronous streaming, `-nopreloadcollision` = world only.
- `source/world/eden_world.*` - `preloadOne` / `prefetchAround`; a preloaded sector is `bPreloaded`: **not drawn,
  and not counted as loaded by the area mesh** (`updateAreaIndices`, `updateOceanVisibility` via the new
  `isSectorShown`) - otherwise the coarse area terrain / ocean tile would hide under a sector that does not draw.
  `loadAround` reveals the preloaded sectors of its square (and marks their areas dirty) - exactly when the old
  code would have loaded them, so **the drawn world is identical**.
- `source/physics/vehicle_collision.*` - `preloadOne` (the same, for the collision streamer).
- `source/filesystem/gs_file.*` - **RAM file cache + background reader**: `prefetchFile` queues a path, a worker
  resolves and reads it; `loadFile` copies a cached file out (waits only for the file the worker is reading right
  now). Sector banks read synchronously are kept too (the collision reads each one right after the world).
  LRU, `-filecachemb <n>` (default 768, 0 = off). Prefetch = the banks two rings out, issued with the first
  preload step (issued at the sector change it competed with the spawn's own reads).
- `source/render/eden/eden_model.*` - **XZ grid over the collision triangles** (16 m cells, built at the first
  query): `getSurfaceHeight` / `getCeilingHeight` walked every triangle of every loaded sector per call, several
  calls a frame (`EdenWorld::isSheltered` for the ambience, `getSurfaceHeight`) - ~6 % of the main thread
  (`-sampleprof`), and the preloaded sectors doubled it.

### Verified
- Crossing frame 1132: **76-91 ms (update 52-65) -> under 33 ms, not a hitch** (`-nopreload` vs default, same
  build, two runs each). The worst preload step seen: ~14 ms (one collision sector), frame 1140 at 33 ms.
- Steady update **1.2 -> 0.4 ms** a frame (the grid; both modes). Steady frame times equal with / without preload.
- **Pixel-identical**: frames 600 / 1100 / 1180 `-nopreload` vs preload (bbox None, including after the
  crossing); frame 1300 vs the pre-grid build: only the build stamp differs (physics / camera unchanged).
- Validation (`-vk_enable_validation_layer`, 1250 frames with preload, reveal and 4 unloads): no VUIDs; only the
  pre-existing `vkDestroyDevice` 4 leaked VkBuffers at exit.
- RAM (peak, 1300-frame drive): working set 1,083 -> 1,277 MB, private 2,800 -> 2,995 MB (+~200 MB: the extra
  loaded ring + ~115 MB of banks in the file cache).
- Start-up frames 1-3 are dominated by run-to-run noise (frame 1 160-290 ms either way); frame 1 update is
  lower with the cache (51-57 vs 71-84 ms).

### Ruled out
- `-gpsdrive` as a multi-sector benchmark: it moves the GPS marker, not the car (the car sits in neutral).
- The collision preload as the source of the +0.8 ms update it first showed: `-nopreloadcollision` gave the same
  number; the profile named `isSheltered` / `getSurfaceHeight` (fixed by the grid).

### Culling (the owner's question)
- Already in: per-instance frustum culling + per-instance LOD distances + small-object culling
  (`EdenWorld::drawInstances`), per-item frustum boxes for the sector / area models (`EdenModel::draw`,
  `isBoxVisible`), the impostor batching. Not in: **occlusion culling** (hiding what is behind a hill or a
  building). The GPU is ~4 ms of a 15 ms frame, so occlusion culling would only pay if it cuts CPU draw
  submissions - worth measuring in town (Ibiza city, the casino strip), not on the open roads; a software
  depth pre-test of building boxes is the cheap form. Not started.

### Not done / next
- The benchmark drive crosses one border (the car rolls near frame 1200): a longer straight drive with several
  crossings is still unmeasured. The heading-first order matters most at speed.
- A preload step is still main-thread work (~4-14 ms); moving the bank parse + 3DG/PMI decode to a worker is the
  remaining step if a 25-30 ms frame during a drive is felt.
- Collision never unloads (`VehicleWorldCollision::loadedSectors` only clears on an island change): RAM grows over
  a long drive - pre-existing, preload makes it grow one ring earlier.

## Session 2026-11-04 (hitches + SDL, `win`): pipeline cache on disk, sign faces off the main thread, uploads batched; SDL 3.4.20 + DualSense adaptive triggers

The owner: "the game is barely using the cpu or ram ... is there anything else we can do to help improve stuff
and ... can we benefit from upgrading to latest SDL for additional features for controllers" - then "do both,
fixing hitches 1st".

**Measured** (every run: `-gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 21796.3,24188.1 -snaproad 150
-carlod 4 -hitchms 33 -autoplay "60:hold=accel" -exitframe 2400`, 1280x720; `[hitch]` lines now carry the frame's
uploads / submits / host waits):

    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -hitchms 33 -autoplay "60:hold=accel" -exitframe 2400

### What changed
- `source/render/vulkan/render_device.cpp` - **the VkPipelineCache persists** in `<userdata>/pipeline_cache.bin`
  (9.3 MB; header checked against vendor / device / pipelineCacheUUID, written to `.tmp` and renamed at
  `destroy`; `-nopipelinecache` = start empty). `-sampleprof` on a fresh exe: a first-sight sector frame was
  295 ms with `nvGetCompilerInterface` 31 % and `EdenRenderer::getPipeline` 20 % of it.
- `source/world/sign_text.*` - **road-sign faces compose on a worker** (`SignFaceCache::workerLoop`): the 4 MB
  atlas copy + text + DXT1 of the 1024 chain was ~20 ms of the main thread per new sign. `getFace` queues a
  missing face and returns null (the plain plate draws, exactly as before a face exists); `collectFinished`
  uploads the results. `compose` = `composeEncoded` (CPU, const) + `upload` (main). `-syncsignfaces` = the A/B.
- `source/render/vulkan/render_device.cpp` - **texture copies join one open upload batch** (`getUploadBatch`),
  submitted with a fence and no wait before the frame's submit, before any immediate submit, or past 64 MB;
  the staging is freed when the fence signals (`retireUploads`). Same queue: submission order + the batch's
  own barriers make the textures ready for every later submit. `-syncuploads` = the old wait per texture.
- `source/tdu_instance.cpp` - `[hitch]` lines add `uploads N tex X MB, S submits, W waits T ms`.
- `cmake/Dependencies.cmake` - **SDL 3.4.12 -> 3.4.20** (bug-fix releases + controller DB). The shared cache
  `C:\otdu2-deps\SDL` was checked out at `release-3.4.20` too, so **the glm / cline trees build 3.4.20 on their
  next build** (patch level, API-compatible).
- `source/input/gamepad_output.*`, `gm_freeride.cpp` - **DualSense adaptive triggers** through
  `SDL_SendGamepadEffect` with the raw DS5 effects report (SDL has no dedicated API; the format is SDL's
  `testcontroller.c` one). While driving (engine on): **L2 brake** firm resistance (`01 24 150`), a buzz while
  any wheel's `bAbs` is set (`06 28 63 24`); **R2 throttle** light resistance (`01 0 45`), a buzz on wheelspin
  (a driven wheel `bTcs` or `SlipRatio > 0.25`: `06 38 40 16`). Free triggers otherwise and on exit / when
  the DualSense option is turned off. Only for `SDL_GAMEPAD_TYPE_PS5`; `-notriggers` = off, `-logtriggers`
  logs each change. The values are a chosen feel `[INF]` (the PC original has no DualSense support).

### Verified
- Upload batching: frame 1 had **2,149 fence waits = 320 ms** (`-syncuploads`) -> **0 waits, 3 submits**.
  The frame total did not move (508 vs 511 ms): the waits were not where the time went on this machine.
- Pipeline cache, NVIDIA's own disk cache off (`__GL_SHADER_DISK_CACHE=0`), `-nopipelinecache` vs loaded:
  frame 1 **552 -> 156 ms**, frame 3 84 -> 38, frame 12 91 -> under 33, the sector-load frame (1132)
  91 -> 78 (its draw 39 -> 25).
- Sign faces: the 33-43 ms frames at 900 / 912 / 924 / 1032 / 1104 (1-2 sign textures each) are gone; frame
  12 190 -> 77 ms. Steady drive 12-16 ms.
- **Pixel-identical**: FREERIDE frame 1300 `-syncuploads` vs batched, `-syncsignfaces` vs async (0 differing
  pixels each); garage `FIRST_WM` frame 400 `-syncuploads` vs batched (bbox None). SDL 3.4.20 build vs the
  3.4.12 one: only the build stamp differs.
- Validation (`-vk_enable_validation_layer`, 1200 frames): no VUIDs from the change. One pre-existing message:
  `vkDestroyDevice` - 2-4 VkBuffers leaked at exit; it is there with `-syncuploads -syncsignfaces` too.

### Ruled out
- "Every texture upload waits for the GPU" (PERF_PLAN P1, a `[CODE]` reading) as the cause of the running
  game's hitches: measured, the sector load at frame 1132 waited 3 ms for its 25 textures.

### Not done / next
- **The triggers are not felt yet** - no pad on the headless runs (`-nopad`). Owner check: plug the DualSense
  in, drive, brake hard (L2 should buzz when ABS cuts in), spin the wheels (R2 buzzes). `-logtriggers` prints
  every change to the log. If SDL refuses the effect (Bluetooth without enhanced mode) the log says
  `adaptive triggers refused` and the pad keeps rumble + light bar.
- The trigger state is only updated from FREERIDE's tick: in the pause menu the triggers keep the last
  driving effect until play resumes or the game mode leaves.
- **The remaining sector-load hitch (~52 ms of update at frame 1132)** is `EdenWorld::loadSector` +
  `VehicleWorldCollision::loadSector` on the main thread (bank read + 3DG / PMI decode + collision build).
  Next: PERF_PLAN P1 second bullet - read and decode on a worker, create the GPU objects in per-frame slices.
- The pipeline cache is written at exit only; a crash loses that session's new pipelines (the driver's own
  cache still has them on NVIDIA).
- SDL still has no controller audio (the DualSense speaker) in 3.4.20.

## Session 2026-11-04 (intro atmosphere, `win`): no haze or distance fog in the intro cinematic

The owner: "I think that scene originally shouldn't have any fog or haze I noticed it made the scene look weird".

### What changed
- `eden_scene_view.*` - per-scene switches `setHazeEnabled` (the full-screen Skydome_haze pass) and `setFogEnabled`
  (fog_max -> 0, the map view's rule); `-loghaze` logs the drawn weather's haze / fog inputs when it changes.
- `intro_cinematic.cpp` - the intro view turns both off; `-introhaze` / `-introfog` restore each.

### Verified
- `-loghaze`: every intro shot uses the `Intro` weather - Skydome_haze 0.700, fog 0..1200 m, max 0.500 [DATA]; so the
  haze is in the data, the owner's observation is that the original does not show it (postfx_spec 5 item 5 notes it).
- Intro frames 1200 (bay) and 2000 (party) with and without: `docs/images/intro_no_haze_bay.webp`,
  `intro_no_haze_party.png` (top = old, bottom = new) - the grey wash and the ghostly far trees are gone; the distance
  is darker and more saturated without the haze's added light.

### Not done / next
- The switch covers the whole intro cinematic view (the flyover, the party, the house walk that follows in the same
  view). Free ride keeps its haze. If the original turns out to show haze in some intro shots, make it per shot.

## Session 2026-11-04 (impostors, `win`): the far trees draw - batched, lit by impostor_ambient, camera-facing, atlas view by angle

The owner: "Yes, light the trees".

### What changed
- `eden_world.*` - the TD_Impostor cards draw by default (`-noimpostors` = off): `addImpostor` yaws each card to the
  camera (its -X front face; FUN_00996820 turns impostors by the camera angle [CODE]), picks the atlas side view of the
  camera angle in the instance frame (N-1 side views + a top view [DATA: the 2DB strips]) and transforms it to world
  space into the per-material batch; `drawImpostors` draws each batch with `__AmbLight_Ambient_0` = the weather's
  `impostor_ambient`, `_1` = 0. `-logimpostors`: card positions / UVs / plane, card vs LOD0 size.
- `eden_scene_view.cpp` - `impostor_ambient` read from the weather each frame (FUN_00d769e0 -> renderer +0x45c0, the
  light object at +0x45bc, vtable 0xf2c310, constructed at 0xd82170 [CODE]); the scene ambient without a weather.

### Verified
- Intro frame 1200: far trees on the hills, green, full width; `docs/images/impostors_far_trees_intro.webp` (before /
  after) + `impostors_far_trees_zoom.png`. FREERIDE 13:00: one far tree at the street end, nothing else changes.
- 4K High benchmark (quiet machine): 15.3 / 16.0 ms on, 14.8 / 15.6 ms with `-noimpostors` (+0.4-0.5 ms). `-noimpostors`
  is pixel-identical to the previous commit. Validation: FREERIDE + intro, only the existing shutdown VUID.

### Ruled out (each with its A/B)
- The grey translucent shapes behind some far trees: not a second card (all 19 items are vertical cards), not the
  shadow casters (`-noshadows` identical), not the size (crown top within 1 m of LOD0) - they are the distance haze
  (`-nohaze` removes them) on trees that really are far. The top-down atlas cell on a vertical card was a real bug
  (grey slanted bars): fixed, only the side views are used.

### Not done / next
- The 0.33 -> 1 dissolve against LOD0 (ps_1 / ps_4 `gDissolveTexSampler`, pmi_streaming_spec 4) - the switch is a
  hard cut today. The atlas cell order is [INF]: compare the intro bay against a retail screenshot.
- Measuring note: absolute frame times drifted 15.5 -> 17.4 ms while another process held the CPU at 66 %; quote A/Bs
  run back to back, and check `(Get-CimInstance Win32_Processor).LoadPercentage` first.

## Session 2026-11-04 (perf 3, `win`): the far-tree impostors were 3,900 invisible draws a frame - skipped, 4K High 19.4 -> 15.5 ms (64 fps)

The owner: "Instance the .pmi draws".

### What changed
- `eden_renderer.cpp` - `-logdrawfamilies`: draws per shader family + distinct mesh/material pairs every 120 frames.
  25.5 k draws / 6,451 pairs; TD_Impostor 3,875 draws from 58 pairs - the most repeated family.
- Found: the TD_Impostor vertex shaders have no world matrix (`gl_Position = ViewProj * in_Position0`, read with
  `otdu2_sob_tool <sob> <dir>` + `C:\VulkanSDK\1.4.350.0\Bin\spirv-cross.exe`). Ordinary families (TD_Building,
  TD_Ground...) read their world matrix from per-draw constants, so they cannot be GPU-instanced without new shaders.
- `eden_world.*`, `eden_model.*`, `render_device.h` - impostor items are skipped by default; `-impostors` batches them
  in world space (CPU-transformed quads, one transient vertex buffer + one draw per material and pass, mirrored
  instances re-wound). `RenderDevice::getTransientBuffer()`.

### Verified
- 4K High benchmark (same command as perf 2): **15.5 ms (64 fps)**, was 19.4; ocean 2.6 / prepass 2.7 / shadows 3.8 /
  opaque 2.7 ms. Frame identical to the previous build outside the stamp; intro frames 1200 and 2000 identical to the
  old per-item path. Validation clean with and without `-impostors`.
- `-hidematerial _H-I` (old code): 0 pixels differ -> the impostors never drew anything visible.

### Not done / next
- Make the impostors look right, then default them on (PROGRESS row "Far vegetation impostors"): feed
  `__AmbLight_Ambient_0/1` from `impostor_ambient` (night_lighting_spec 5), check FUN_00996820 for camera-facing quads
  and the dissolve; `docs/images/impostors_far_trees_intro.webp` is the current `-impostors` look (intro frame 1200).
- Real instancing of the other families needs per-instance world matrices in the shaders - a SPIR-V patch of the
  DXSO translation (read the World rows from an instance-rate stream) is the route; not started.

## Session 2026-11-04 (perf 2, `win`): 4K High 31 -> 19.4 ms at the benchmark spawn, 59-79 fps on a real drive — profiler, heap churn, parallel command recording

The owner: "Yes, start it" (the parallel pass recording of the 4K-60 plan).

### What changed
- `source/core/sample_profiler.*`, `tdu_instance.cpp` — **`-sampleprof [<first>,<last>]`**: an in-process stack sampler of
  the main thread (suspend + x64 unwind + DbgHelp), report `<log>-profile.txt`. No admin needed. Symbols need the
  `build-prof` dir (`docs/PERF_PLAN.md` §0a has the configure line; 45 s full build).
- `eden_scene_view.cpp`, `eden_renderer.*` — **`-logrecordtimes`**: `[perf] record/pass:` (ms in recordDraw / pass ms, draws).
- `eden_renderer.*`, `render_device.*` — the redundant-bind filter (`-nobindcache`), driven by a command-buffer
  touch count in `RenderDevice::getCommandBuffer()`.
- `eden_world.cpp` — `drawInstances` built two `EdenDrawParams` (five `unordered_map`s each) per visible instance per
  pass: **22.5 % of the main thread**. Now `std::optional`, built only for neon / merged instances.
- `eden_renderer.cpp` — `FindSmall` for the per-draw override maps; `getPipeline` one-entry memo.
- `freeride_hud.*` — the minimap's road query cached with a 150 m margin (re-queried every 75 m).
- `eden_renderer.*`, `render_device.*`, `renderer_2d.cpp` — **parallel command recording** (`-noparallelrecord`,
  `-recordthreads <n>`, default 6 workers): queued `DrawPacket`s, the device's `PendingRecordHook` flush before any
  other recording, pass restart with LOAD + secondary command buffers, per-slice command pools; the device tracks
  viewport / scissor (`setViewport` / `setScissor`). How it works: `docs/PERF_PLAN.md` §0a.

### Verified
- Same 4K command each step: 31.3 -> 30.1 (bind filter) -> 23.7 (EdenDrawParams) -> 21.9 (minimap query, memo) ->
  **19.4 ms (52 fps)**; GPU unchanged ~4.3 ms. Real-time 2400-frame 4K drive: 59-79 fps, VRAM flat, clean exit.
- Every step pixel-identical: frame-400 screenshots identical outside the build stamp across builds; parallel vs
  `-noparallelrecord` 0 differing pixels in FREERIDE 4K, the intro (frame 400) and the garage (frame 300).
- Validation: FREERIDE, intro and garage runs report only the existing shutdown `VUID-vkDestroyDevice-device-05137`.

### Ruled out
- Deferring constants / descriptor sets to the workers: 103 runtime material-mutation sites in 17 files (car
  paint, avatar faces, traffic colours, sign faces) - they must be resolved at capture on the main thread.
- ETW / xperf: the agent shell is not elevated.

### Not done / next
- The heavy spawn is 19.4 ms in `-fixedtimestep` (15.9-16.9 real time). What is left on the main thread: constant
  fill + descriptor resolution (~0.4 us/draw) and the traversal. Re-run `-sampleprof` on `build-prof` first; then
  `.pmi` instancing (`EdenWorld::drawInstances`) cuts every pass's draw count.
- The intro records inline (its batches are under the 192-packet threshold) - fine at 8 ms.

## Session 2026-11-04 (perf, `win`): the satnav minimap's lost 10 ms is back out — 4K High 39 -> 31 ms; the 4K-60 plan

The owner: "I want a base as 4k 60fps max settings on this pc with potential for higher fps ... the original game
with dxvk 4k max with 8x AA this pc gets over 100 - 140fps".

**Reproduced**: `-loghudtimes` at 4K and at 720p both read `[perf] hud: flash 9.5-10.0 ms`:
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -gm FREERIDE -skipcine -nopad -fixedtimestep -profile high -width 3840 -height 2160 -novsync -tp 21796.3,24188.1 -snaproad 150 -carlod 4 -loghudtimes -logscenetimes -loggputimes -autoplay "60:hold=accel" -exitframe 620

### What changed
- `source/game/freeride/freeride_hud.{h,cpp}` — `drawMinimap` gets cline's session-73 fix back on the satnav map
  rect: a GPU scissor (`Renderer2D::setClipRect`) instead of per-triangle CPU clipping, and the road + route
  vertex list cached in `MinimapCache`, rebuilt at most every 3rd frame when the car moved > 0.75 m, turned, the
  route or the map rect changed. The d92c85e merge had kept glm's satnav and dropped it. `-nominimapcache` = A/B.
  The unused `ClipToRect` is removed.

### Verified
- HUD 10.0 -> 1.8 ms, frame 39.0 -> 31.3 ms (26 -> 32 fps) at 3840x2160 High; GPU unchanged at ~4.1 ms.
- Frame-400 screenshot vs `-nominimapcache`: mean 0.008/255 whole frame, 0.10 in the map box (<= 2 frame map lag);
  roads trimmed cleanly at the satnav edge (`docs/images/minimap_scissor_cache_4k.webp`).
- `render_scale = 200` at 4K: GPU 8.7 ms, frame unchanged (31.5-32.1 ms) — supersampling is free on this PC today.

### Ruled out
- Resolution as the HUD cost: 720p measured the same 9.5 ms -> CPU, not fill.

### Not done / next
- 4K High is still 31 ms: 27.5 ms is the CPU recording of ocean / prepass / shadows / opaque. The ordered plan is
  `docs/PERF_PLAN.md` §0a — start with parallel pass recording (`EdenSceneView::renderOffscreen`, make
  `EdenRenderer::recordDraw`'s transient allocator + `drawSetCache` per thread), then `.pmi` instancing in
  `EdenWorld::drawInstances`.
- Merge watch: a future glm/cline merge touching `drawMinimap` must keep `MinimapCache` + the scissor; check
  `-loghudtimes` stays under ~2 ms after merging.

## Session 2026-11-04 (AI car LOD, `win`): the opponents and rivals draw the `_med` banks, only the 3 nearest cast shadows

The owner picked "Yes, add the LOD" after the AI opponents session.

### What changed
- `cinematic_car.{h,cpp}` - `Desc::Lod` ("med": `vehicules/med/<car>_med.bnk`, model `<car>_med`), `Desc::bInterior`, `getLod()`.
- `ai_drivers.{h,cpp}` - `AiCar` loads `_med` (`-ailod high`), skips the reflection pass (`-aireflect`) and, in the
  shadow casters, all but the 3 nearest AI cars within 60 m (`-aishadows <n>`, `-aishadow <m>`);
  `AiCar::SetCameraPosition` (`gm_freeride.cpp`, before the traffic draw).

### Verified
- opponents_spec 2 / 4: two runs x 8 samples, 22.6 ms (high, all shadows) -> 21.5 ms (default) against 19.0 ms
  with no opponent draw; high / med look the same (`docs/images/race_opponents_med_lod.webp`); 0 VUIDs; Passed 1st of 8.

### Ruled out
- `<car>_low.bnk` as a far model: it is the engine sound bank (TCT_*.wav, CarVSTConfig.xmb).
- A cockpit-less far level: CinematicCar already draws the interior only within 5 m (table 0x00f7e788).
- `_med` alone and the reflection pass: within the run-to-run noise (~0.3 ms).

### Not done / next
- The remaining 2.5 ms is the cars' main + prepass draws; rims / tires are separate models (8 draws per car per pass).

---

## Session 2026-11-04 (AI opponents, `win`): the championship races have their 7 opponents and a race position; the story drivers cruise the islands

The owner: *"could we add race positions and opponents with AI logic for the opponents also around the world when
offline there's normally a chance to see tess and todd etc... driving around the world ... their respected vehicles
like the pink mustang"*.

Controls: none new (races start as before; the position shows on its own).

### What changed
- `database/gs_database.cpp` - `-dumpdbtables` / `-dumpdbtable <T>` / `-dumpdbcols a,b` (any db_data.cpr table).
- `game/freeride/ai_drivers.{h,cpp}` (new) - DBPNJ / DBPNJCar / DBBot / the signature cars, the class pick, `AiCar`
  (a CinematicCar with rolling / steering wheels, brake and headlamps).
- `game/freeride/race_opponents.{h,cpp}` (new) - the course (gates routed over the road graph), the per-driver speed
  profile, lanes / passing / following, contact, catch-up, ranking; `-logopponents`, `-opponentfollow <n>`,
  `-noopponentdraw`.
- `game/licenses.{h,cpp}` - `_bot_n`, `_difficulty_bot_n`, `_aggressiveness_bot_n`, `_rival`.
- `game/freeride/gm_freeride.cpp` - `startRaceOpponents` / `tickRaceOpponents`, the result ("Beaten  7th of 8"), the
  60 s rule, draw / collision / traffic yield; the rival hint; `-noopponents`, `-opponents <n>`,
  `-opponentdifficulty <f>`, `-licensewarpdelay <s>`.
- `license_test.{h,cpp}` - `eResult::Beaten`, `end()`, `setTimeLimit()` (CtDw off with opponents).
- `freeride_hud.{h,cpp}` - `setRacePosition` (`/HUD/position`, `/HUD/time` to `down`).
- `traffic.{h,cpp}` - rival types (an `AiCar`, weight 0), `loadRivals` / `tickRivals`, `findNearestRival`;
  `-norivals`, `-rivals <n>`, `-rival <name>`, `-rivalsoon`, `-rivalsmax`, `-rivalpark`.
- `chase_mode.cpp` - the CinematicCar cop fallback was drawn mirrored (fitOnGround takes an Eden quaternion).

### Verified
- docs/re/opponents_spec.md 4: CHA5A04R grid + GO (`docs/images/race_opponents_field.webp`), the position HUD
  (`race_position_hud.webp`), win 1st of 8, Beaten 7th / 8th of 8, all 7 finishing 321-327 s, CHA7A02R / CHB4A01R
  class picks, 0 VUIDs; the rival Mustang (`freeride_rival_miami_mustang.webp`).

### Ruled out
- The race gates' road words (`0, 236, 2, 1`) as a port road-graph link index: no match on CHA5A04R.
- CtDw as the race clock with opponents: 240 s for 10.8 km gate to gate; the AI needs 321 s.
- A pink paint for car 601: no DBCarsAndColors / DBPNJCar / DBRewardCar colour; it draws the dark green .2dm paint.
- Todd driving: Todd Bishop has no DBPNJCar / DBBot row.

### Not done / next
- `vehicules/med/<car>_med.bnk` for the opponents (CinematicCar::load reads `high/` only; 3.5-4.5 ms for 7 cars).
- Duels (DBEvent `_pnj1`, DBRewardCar DUEL_WIN_*), RaceEvents with opponents, minimap opponents, the standings
  screen, `player_before` / `player_after`. DBBot `_zoneindex` is undecoded.
- The "333999 m" chase meter shows when `-licensewarp` / `-opponentfollow` teleport into traffic (pre-existing).

---

## Session 2026-11-04 (minimap gate icons, `win`): the race gates show on the satnav with the movie's own icons

The owner picked "Minimap gate icons" after the race gates session.

### What changed
- `freeride_hud.{h,cpp}` - `setMinimapObjectives` drives `/HUD/minimap/objectif_01..08` (start / checkpoint /
  finish / radar_on frames) at world positions on the heading-up map, parked on the edge when off it;
  `canvasToMinimap` (the cursor placement now uses it too).
- `gm_freeride.cpp` - fed from `raceGates.getGates()` after the HUD tick, beside the world markers.

### Verified
- `-island hawai -licensetest CHA5A04R -licensepark` (countdown: green start + two orange checkpoints, one parked on
  the top edge; running: the next checkpoint) and `-licensepark last` (red finish):
  `docs/images/race_minimap_gate_icons.webp`. No icon outside a race (count 0 hides them).

### Not done / next
- `/HUD/position` needs opponents; the icons follow `-noracegates` (they read the gates drawn).

---

## Session 2026-11-04 (race gates + lamps, `win`): the races show their gates and the retail race HUD; the car lamps glow and the hot discs have a gradient

The owner: *"look at the checkpoint/finish line gates etc... for both the races and the licenses challenges we have
no ui for where a lap starts or ends ... none of the gates are used"*, and *"improve lights ... indicators, brake
lights, head lights don't seem to look accurate or even good also brake heat (disk brake glow) could also do with a
little extra work"*.

Controls: none new (indicators stay on the in-car menu, D-pad left; `-indicator left|right|hazard` for tests).

### What changed
- `game/freeride/race_gates.{h,cpp}` (new) - the island's `scenarics/packs/pack_cp.bnk` (Entity__Actor__CheckPoint
  types 1 start / 2 finish / 3 checkpoint / 4 radar / 6 barrel pair) loaded and drawn on the gates.
- `gm_freeride.cpp` `updateRaceGates` - which model on which gate (licenses_spec 8.2), ground snap, the start line 8 m
  ahead during the countdown; hooks `-noracegates`, `-logracegates`, `-licensepark [<step>|last]`, `-racestart <n>`.
- `license_test.{h,cpp}` - laps (`getLap`, `getGatesPerLap`), per-gate events with the split against the previous
  lap, the pass zone following the drawn arch (`getPassHalfWidth`); `ig2_challenge` keeps `bRoad` (0x7c records).
- `freeride_hud.{h,cpp}` - `setRaceHud` / `showCheckpointPassed` / `showLastLap` drive HUD.vmf's `/HUD/time`,
  `/HUD/laps`, `/HUD/checkpoints` (+ `cur_time`, `chck_box`, `last_lap`, loc 7429); the zone badge yields during a race.
- `cinematic_car.{h,cpp}` - `collectLampAnchors` / `appendLampGlows`, per-function lens weights, the blinker's bulb
  rise / fall, the headlamp giving way to its blinker. `eden_headlights` `EdenLampGlowFrame`; `eden_surface_fx` the
  `lampglow.frag` additive batch, the disc spill in the wheel plane; `brakedisc.frag` rewritten (surface_effects_spec 7.x).

### Verified
- Gates: `-licensetest RSA1T01 -licensepark` (barrel pairs), `-island hawai -licensetest CHA5A04R -licensepark` /
  `-licensepark last` (CHECKPOINT and FINISH arches), `-racestart 1` (START arch); `-licensewarp` through CHA5A04R:
  18 gates over 3 laps, splits +0.25 then ~0, "Last lap!" at gate 12, the finish model on the last step, Passed.
  `docs/images/race_gates_hud.webp`.
- Lamps / discs: night rear (blinker lit / off), front, `-brakeheattest 100` / `45`, midday -
  `docs/images/car_lamps_glow_brake_disc.webp` (first row = before). `-vk_enable_validation_layer`: 0 VUIDs.
  `-storystep 3`: exit 0, 0 stalls.

### Ruled out
- RSA1T03 / CHA1A04R gates "missing": the courses are on the other island (terrain -1 under them on Ibiza) - not the
  gate code. The first barrel shot showed nothing: the files' gate heights sit ~1 m under the road (snap added).

### Not done / next
- The minimap `objectif_0N` gate icons; `/HUD/position` (no opponents); off-road gate variants; `-licensetest` should
  switch to the course's island. The lamp glows are checked on the California only - the next car to look at is any
  other bank (the anchor rules are generic); traffic keeps its FA_ sprites; no headlamp beam in fog.

---

## Session 2026-11-04 (second chance, `win`): the Poker and Beach machines are two-step - spin, hold / lock, spin again

The owner: *"poker slot logic is different ... 1 spin and then you get to pick which reels to hold and get to spin
again, the 2nd spin is the outcome ... There's another slot also 3 reels, sun, surfboards ... same"*.

Controls (DualSense / keyboard) at the second chance: **D-pad left / right** = move the hand, **Square / X key** = lock /
unlock (the retail bar says "Lock/Unlock" on X; Triangle / Y works too), **Cross / Enter** = second turn, **Circle /
Backspace** = keep the hand as it is.

### What changed
- `gm_casino.{h,cpp}` - `eRelaunch` (Holding / Second), the poker deal / draw / Jacks or Better pay on the card reels,
  `autoHold`, `settleSecondChance`, the hold row; the poker deck seeded by `-casinoseed`.
- `casino_slots.{h,cpp}` - `respin( held, bPay )`, `setPayNextSpin`.
- `casino_slot_hud.{h,cpp}` - `enterRelaunch` / `leaveRelaunch` (the `relaunch_step` frame, the step-2 help bar), X =
  X_Lock at that step, `smof` reported.

### Verified
- See casino_spec 9.9: the Poker hold kept the 4s through the draw; auto-hold 01100 + Jacks or better on seeds 8 / 9;
  Beach lock kept reel 2; seeds 22 / 61 auto-lock all three reels and pay on the respin (`docs/images/casino_slot_second_chance.webp`).
- High Life flow unchanged (9 lines, the 600 win, quit); 0 VUIDs; `-storystep 3` exit 0.

### Not done / next
- The hand cursor over the machine itself (a native row stands in); the relaunch button neon states; the poker
  machine's measured return.

---

## Session 2026-11-04 (slot neons, `win`): the Fashion middle reel, per-reel stops, the decoded neon blink, the machines' light states and win flashes

The owner: *"middle reel on the clothes slot ... seems to miss some of its textures ... when spinning the middle reel is
only one that seems a little off"*, and yes to the neon blink / win lights.

### What changed
- `gm_casino.cpp` `updateReels` - horizontal drums spin along row 0, vertical drums only on full columns (the sheets'
  empty cells); per-reel stop times (`CasinoSlots::setReelStops`); the machine neons' states and the combination flash.
- `intro_cinematic.{h,cpp}` - the decoded neon light (`updateNeons`, `NeonCurve::evaluate`, the ExportType_Curve
  curves), `setNeonState`, `flashNeon`; `-logcasinoneons`.
- `casino_interior.{h,cpp}` - each machine's Neons[] / ComboActors[] / reel stop delays; `setMachineNeons`,
  `flashCombo`; `-logcasinoreels`; `-dumpcasinoentities 0x<hash>` / `root`.

### Verified
- Fashion spin: the chest drum shows a blurred jacket, no grey stripes (`docs/images/casino_fashion_chest_spin.webp`).
- Neon blink at a fixed eye (`-walkat 35.55,10.00,12.78,180`): frame diffs 5.7 / 4.7 / 5.7 / 7.3 / 7.0 / 4.9.
- States (`-logcasinoneons`): High Life `Title_Neon` / `SMHighLife_gain` -> Active on the machine, kept on the 600 win
  (NeonWin CanBeActivate 0), -> Inactive on leaving; Poker the same.
- Flashes over 6 seeds x 3 spins: Beach 4, Classic 4, Poker 1, TDU 2 1 found; High Life 4 missing (meshless panels).
- 0 VUIDs; `-storystep 3`, hall exit 0.

### Not done / next
- The owner's next request: the Poker and Beach machines are two-step (spin, hold / lock reels, re-spin; the poker
  hand pays). The movie has `relaunch_step`, X_Lock and the hand cursor; the buttons have the Relaunch states.
- The High Life pay panels (part of the sign model), the `Pok_Neon` card lights, the drum bone rotation.

---

## Session 2026-11-04 (slot machines, `win`): the reels spin down, the handle pulls, the screen is on the machine, the tops are lit

The owner: *"the reel spins weren't spinning down but more like across"* (all but the clothes machine), *"the
credit screen on slots is normally connected to the slot itself ... also a handle pull and some button press
animations"*, then a reference photo: *"missing textures at top of slots that should hide the reels"*.

### What changed
- `gm_casino.cpp` `updateReels` - SpinnerType 0 / 2 scroll down a sheet column, 1 (Fashion) across;
  `-reelspeedscale`. The handle clip plays from the spin (`handleTime`).
- `intro_cinematic.{h,cpp}` - SlotMachineTrigger, CasinoFlashScreen (+ Ranking / Jackpot), Entity__Actor__Neon and
  NeonSlotMachine load as props (the neons in the slots room only, lit from their IlluminationFactor);
  `setPropClip` (a game-driven clip; "BAS!" layers resolve to their .anm), `setPropTexture`, `setPropEmissive`.
- `casino_interior.{h,cpp}` - each machine's Trigger / its clip / SlotScreen / SlotLinesScreen; `setHandleTime`,
  `setScreenTextures`; `-dumpcasinoentities <substring>` dumps any entity array's fields.
- `casino_slot_hud.{h,cpp}` - Cas_SlotScreen (played + idle) and Cas_SlotLines drawn into render targets
  (`drawOffscreen`); `-slotscreenpanel`, `-slotscreenclear`.
- `renderer_2d.{h,cpp}` - `setTargetSize` (draw into a render target). `render_device.cpp` - the main pass has the
  render targets' two subpass dependencies (VUID-vkCmdDraw-renderPass-02684 otherwise).

### Verified
- Spin direction: `-reelspeedscale 0.04`, frames 20 apart: Classic / High Life symbols move down
  (`docs/images/casino_reels_spin_down.webp`); Fashion still across.
- Handle: TDU 2 machine, lever crop diff vs frame 309: 0.12 / 0.12 / 4.94 / 4.44 / 2.71 at +11 / +26 / +41 / +56 / +71
  frames - the pull and the return (`casino_slot_handle_pull.webp`), clip `cas_sm_spin-tri.anm` 1.21 s.
- Screens: 00 / bet / 1000 at rest, 990 (High Life) and 900 (TDU 2) after a spin, on the machine
  (`casino_slot_screen_on_machine.webp`); `-noprops` A/B: the black panel was the screen prop.
- Tops: walk view at the TDU 2 machine - the pay art (wheels, 1500 / 500) covers the drums; the room's machines have
  their lit tops (`casino_slot_tops_neons.webp`); all six in play (`casino_slot_machines_parts.png`).
- `-vk_enable_validation_layer`: casino spin / back / quit and FREERIDE map + pause, 0 VUIDs (11 before the pass fix).
- Regressions: `-storystep 3` exit 0, 0 stalls; hall and roulette rooms exit 0.

### Ruled out
- The screen binding: `-slotscreenclear` (red target) stayed black -> not the movie draw; the cause was the decal's
  emissive (WeatherEmissive 0 on unregistered props).

### Not done / next
- The neon blink (`TimeFactor`, `CurveFactor`) and the gain panels lighting on a win; the drum bone rotation /
  `AnimStop`; Cas_SlotLines shows no active lines; the button press is not checked separately (the trigger clip
  may move `Boutons`). The owner offered TDU World as a reference for these.

---

## Session 2026-11-04 (slot HUD, `win`): the slot machines are played through the retail HUD movies

The owner: *"Yes, do the HUD"* (the previous session's question: drive `Cas_SlotMachine.vmf`).

Controls at a slot machine (DualSense / keyboard): **Cross / Enter** = A (menu select, spin, validate the lines,
yes in a box), **Circle / Backspace** = B (back to the menu from play; on the menu, the "quit this machine?" box),
**D-pad up / down** = menu, **D-pad left / right** = rules pages / lines manager / bet level at play,
**Square / X key** = "Activate lines" (Classic, Beach, High Life only), **Triangle / Y key** = max bet.
(The cheats panel moved off Square while a machine is played; F4 still opens it.)

### What changed
- `source/game/casino/casino_slot_hud.{h,cpp}` (new) - `CasinoSlotHud`: loads Cas_SlotMachine.vmf + Cas_SlotScreen.vmf,
  sets the machine page / wallet / reels, routes the pad to `/:RootMenuPath` or `/:Clip` (the movie's own A / B /
  Move / Left / Right / X_Lines / X_Lock), answers `smon` `smpl` `smof` `sloo` `lini` `lino` `linc` `left` `right`
  `lock` `dbxo`, draws the DBXO box natively, drives the credit screen (`loss` / `gain` / `not_win`). Protocol:
  `docs/re/casino_spec.md` 9.6.
- `gm_casino.cpp` - the HUD opens at a machine and owns its input; `startSlotSpin` (the movie's money / lines error
  frames), `handleSlotHudEvents`; `-dumpcasinomovie` dumps the root timeline too.
- `casino_slots.{h,cpp}` - per-line stakes on the NbLines > 1 machines ("¤10 per activated line" [DATA]),
  `setActiveLines` / `getMaxLines`; each line pays paytable x bet x Price.
- `flash_player.cpp` - `_width` / `_height` get / set (content bounds; `-noclipextent` A/B), `invokeWithFlag`
  (a real boolean argument: `SetHelp(name, true)`), `-logtext` logs empty fields too.
- `vmf_file.cpp` - the vector-outline fonts' code table / metrics are found past the shapes (`-logfonts`).
- `gs_input.cpp` - `-logkeymap` (MENU_OPTION_1 = X, MENU_OPTION_2 = Y on the keyboard).

### Verified
- `-gm casino -casinoroom slots -casinoselect 6 -casinoseed 7 -casinochips 1000 -fixedtimestep -logslothud` with
  the autoplay of casino_spec 9.6: smon -> smpl ("spin 1 ... 10 chips"), lini / smli x3 / lino ("9 line(s)
  activated, 90 chips a spin"), the 600-chip win counted up 910 -> 1510, smof, sure_quit -> sloo ("back on the floor").
  `docs/images/casino_slot_hud_{menu,play,lines,win_countup,quit_box,rules,six_machines}.png`.
- All six types (`-casinoselect 0 / 4 / 5 / 7 / 22 / 6`): title, menu, play bar, a spin, exit 0. Poker + X: no `lini`.
- 0 chips: A -> the `error_money` box ("You don't have enough money to play...").
- `-noslothud` = the native text (spin 1 at 90 chips: High Life's 9 lines at ¤10 per line).
- Regressions: `-storystep 3` exit 0, 0 stalls; FREERIDE 700-frame drive exit 0, 0 stalls; `_width` A/B: map, HUD,
  options identical (diff 0.000), pause menu 5.2 (the player panel ~20 px lower - its script reads `_height` now).

### Ruled out
- A missing localization as the cause of the blank help bar labels: `m_select` / `m_Quit` resolve (`-logtext`); the
  field's font (Berlin Sans FB id 4) had ascent 6656 from the misparsed vector font.

### Not done / next
- Cas_SlotScreen is a 2D panel (top right), not on the machine's `CasinoFlashScreen`: needs a movie -> texture path.
- The second chance (hold) of Poker / Beach (`relaunch_step`, `X_Lock`, the hand on Left / Right) - the port uses
  those keys for the bet level / max bet [INF].
- The pause menu's player panel moved ~20 px down with `_width` / `_height`: check it against a retail capture
  (`-noclipextent` restores the old placement).
- The sit-in / `C_SLOT_WIN_0x` / `C_JACKPOT_*` cinematics (casino_spec 9.5).

---

## Session 2026-11-04 (casino, `win`): the real slot machines, the R8 on the hall's socle, the seaplane trip cutscenes, and collision on the spot buildings

The owner: *"finish the casino work please allowing the real slots to be used and casino car to shown in the hall then
some places on islands still have no collision lastly need the casino cutscene"*.

Controls: at a slot machine **Cross / Enter** spins, **Left / Right** the bet, **Triangle / Up** max bet, **Circle / Backspace**
back to the walk. To fly to the casino, stop at the casino spot (`EGAMHA`, or the seaplane dock `ELANDCAA` - now on the map
with the casino icon) and press **Cross / Enter**; the hall's exit door flies you home. **Circle / Esc** skips a trip scene.

### What changed
- **Real slot machines** (`casino_interior.cpp`, `gm_casino.cpp`, `casino_slots.cpp`, `intro_cinematic.cpp`): the played
  machine is seen through its own `CameraGame` (`Entity__Camera__CameraCasino`, looked up by `__Hc` - all 38 are `CAM_GAME`);
  its `Entity__Actor__SlotMachineSpinner` reels are loaded as props (they were never loaded: the windows showed the white
  backing) and driven through the decoded `TD_Casino_scroll` VS constants (`scroll_uv0..2` = the three symbol slots,
  `UV_symbol_space`, PS `blur_blend_factor`) with the material `StageConstants`. The engine takes the machine's reel count
  (3 / 5) and stop timing (`TimeBeforeFirstStop` 1.5 s). Spec: `casino_spec.md` 9.5 (the shader decode, the symbol sheets).
- **The hall car** (`CasinoInterior::placeShowCars`, `IntroCinematic` VehiclePosition + turntable): `icashal`'s
  `ActorVehiclePosition` says car 432 (the Audi R8), paint 12, 1 turn a minute [DATA]. `casino_spec.md` 9.4.
- **The seaplane trip** (`casino_trip.{h,cpp}` new, `gm_cin_viewer` `CinViewerRequest::After`, `-cinisland`): departure at
  the island dock -> `C_CASINO_ARRIVAL` -> CASINO; `leaveCasino` -> `C_CASINO_DEPARTURE` -> `C_<ISLAND>_ARRIVAL` -> FREERIDE.
  `casino_spec.md` 10, `cutscenes_inventory.md`.
- **Building collision** (`GMFreeRide::addSpotExteriorCollisions`): the streamed spot exteriors' `<bank>_sc_*.shk` go into
  the drive collision at each spot frame (122 of the 270 exterior banks have one). Before, only the home house had it.

### Verified
- `-gm casino -casinoroom slots -casinoselect 6 -casinoseed 7` + one spin: reels blur, stop in turn, the 66-chip win is
  three speed boats on the reels (`docs/images/casino_slot_highlife_spin.webp`); all six types:
  `casino_slot_machines_real_reels.webp`. Hall: `casino_hall_r8_socle.webp`.
- `-gm FREERIDE -island ibiza -casinotrip -casinoleave 200`: C_IBIZA_DEPARTURE 65.3 s -> C_CASINO_ARRIVAL 53.0 s -> CASINO
  hall -> C_CASINO_DEPARTURE; `-gm casino -casinoleave 60`: 37.0 s + C_IBIZA_ARRIVAL 52.5 s -> FREERIDE on Ibiza at the dock
  (`casino_trip_to_casino.webp`, `casino_trip_home.png`).
- Building collision A/B at the Ferrari dealer (`-tp 21796,14,24150 -heading 0`, accel held 8 s): the car hits the wall and
  stops after 61 m; `-nospotcollision`: straight through the building, 161 m (`building_collision_ferrari_dealer_ab.webp`).
- Regressions: `-storystep 3` exit 0, 0 stalls; `-casinoroom roulette -casinoselect 2` table minimum 25.

### Ruled out
- Unsupported Havok shape classes as the cause of the island collision gaps: a scan of all 4001 sector banks finds only
  classes the loader handles (the "missing" names are base classes in the class tables).

### Not done / next
- `Cas_SlotMachine.vmf` (the retail slot HUD) is not driven: the HUD is native text under the reels
  (`-dumpcasinomovie cas_slotmachine` dumps it). The avatar sit-in and the C_SLOT_ACTIVATE / WIN / JACKPOT beats are not
  played. The symbol-to-cell pairing and the spin speed are [INF].
- The Hawai trip legs are wired but were not played through. After the home arrival one run's chase camera ended in a hedge
  13 s after the handoff - check the parked pose (`gm_cin_viewer.cpp`, the StoryHandoff block).
- Collision: only the spot buildings were missing by measurement; if the owner still finds a spot, get its coordinates and
  run `OTDU_COLLISION_PROBE=x,z` (vehicle_collision.cpp) there.

---

## Session 2026-11-04 (later, `win`): the car wash plays the retail sequence with the fee after the drive-in, the car's state survives scenes, the Solar Crown studio has its TV overlays and the avatar out of the TV shots

The owner: *"cutscene fixing when arriving at solar crown and into the studio players avatar seems to be in the
way most of the time ... top right should show solar crown logo, at bottom right says tess wintery, and when the
camera points to avatar it should print players name"*; the car wash *"when you accept ... the car drives to the
center and then you see the girl put down the radio"* (reference <https://www.youtube.com/watch?v=wfTDjHH-tls>),
then the full beat (drive to the spot, ask to pay, the face, the radio girl, the other two, in-car view with the
hose girl in front and the two at the sides, they walk away), and *"the vehicles state and avatar ... must be
remembered when entering ... except the todd driving you to the used car dealer"*.

**Reproduced:**
    build-win\bin\opentdu2-win.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-win-userdata -storystep 6 -nopad -fixedtimestep -playername Danny -autoplay "900:shot=...;1250:shot=...;2280:shot=..." -exitframe 3050
    build-win\bin\opentdu2-win.exe ... -gm FREERIDE -island ibiza -atwash -money 20000 -nopad -fixedtimestep -logcin -autoplay "120:press=validate;800:press=validate;..." -exitframe 3800

Controls: the car wash is accepted with **Cross / Enter** (validate) at the sponge tile; the fee box: **Cross /
Enter** pays, **Circle / Esc** (back) leaves without paying.

### What changed
- `source/game/avatarchoice/intro_cinematic.cpp/.h`: `applyLogoMessage` - the `.cin` messages `lgtv:open|clos`
  and `lgsc:open:<name>|clos` play GENERAL.vmf `/logo_TV` / `/logo_SC` (labels `open`/`close`) with
  `/:PNJName` (`-player` = `/:PlayerName`), drawn in `draw()` / `drawUi()`. `SampleVisibility`: a multi-key
  Visibility track now hides PNJs and the player too (the avatar of C_SOLAR_CROWN_I 11.41-36.4 / 39.48-58 s).
  `buildInCarFallback`: a NaN shot camera (`c_washing_02`) becomes an in-car view of the driver from the
  scene car's `DVRSEAT_LR`, else the last good camera - no more NaN basis / culling flood. `-dumpgeneral`,
  `-nocinevisibility`.
- `source/game/cinematic/gm_cin_viewer.{h,cpp}`: `CinViewerRequest::Then` (a chain of timelines), `PromptAfter`
  / `PromptLoop` / `Price` (the fee box, validate pays, back leaves), `InCar` / `InCarBackgrounds` (the car's
  mode 23 cockpit camera from camerasingame.xmb, the avatar seated, the background loops); the avatar `avm`
  messages seat the player as in GMCine; the browser / timeline only for the tool (`bTool = !bFromStory`);
  `VehicleSceneState` + `GetVehicleSceneState()`; the viewer no longer writes a default avatar into the profile.
- `source/game/freeride/car_wash.cpp`: the request is `C_INTRO_E_1` -> fee over `C_LOOP` -> `C_WASHING` ->
  `C_INCAR` (+ `C_BABE_01/02/03`) -> `C_OUTRO`; `-washonly` = the old single timeline.
- `source/game/freeride/gm_freeride.cpp`: accepting the wash no longer pays or cleans (the scene does, after the
  drive-in); `exit()` records `VehicleSceneState`, `enter()` restores roof / windows / CarDirt on the same car
  (`-noscenestate`).
- `source/game/cinematic/cinematic_audio.cpp`: `-logcinsound` prints 2D/3D, position, clip ranges, fx flags.

### Verified
- Solar Crown: "Tess Wintory" bottom right at 15 s, the player name ("Danny") on `cam_challenger` at 38 s, the
  TV logo top right; the avatar gone from the 21 / 25 / 50 s TV shots
  (`docs/images/solar_crown_i_avatar_visibility_before_after.webp`, `solar_crown_i_tv_logo_name_captions.png`).
  `-storystep 3` frames identical (diff 0.000 x4); `-storystep 5` differs only where the data hides actors;
  `-storystep 6` exit 0 -> SHOP, 0 stalls.
- Car wash: `accepted -> the drive-in`, `end of 'C_INTRO_E_1' -> the fee prompt ($10) over 'C_LOOP'`, `paid $10`,
  `end of 'C_WASHING' -> 'C_INCAR'`, `in-car part 'C_INCAR' with 3 loop(s), cockpit camera of the car`,
  `end of 'C_OUTRO'`, `FREERIDE: 'California' as it left: roof open, windows open, mud 0.00 dirt 0.00 wet 1.00`
  (with `-dirtseed 70`: the scene car `mud 0.70 dirt 0.42` before the fee). `docs/images/carwash_retail_sequence.webp`.
- The music: a real-time run with `OTDU_AUDIO_LOG=1` keeps `m_washing_intro_CINEMATIC` at gain 1.00 through all
  of `C_WASHING`, then `m_washing_CINEMATIC` in the in-car part.
- 700-frame FREERIDE drive exit 0, 0 stalls.

### Ruled out
- The "music cuts off as she places the radio" was the `-fixedtimestep` test runs (~32 fps: the scene took
  ~31 s of wall time, the 16.6 s track ran out half way) - not the mix (bus 5 is stereo, gains 1.00 every
  second), not the clip (MS-ADPCM decoded offline: continuous music), not distance. car_wash_spec 10.4.
- `-nocinevisibility` does not stop the return-to-FREERIDE crash -> the visibility change is not its cause;
  the committed tip crashes too (1/5).

### Not done / next
- **The pre-existing crash on the return to FREERIDE after the wash** (Open in PROGRESS): AV at
  `opentdu2+0x4246cd` (main thread, FREERIDE reload). Next: a `/Zi` RelWithDebInfo build and
  `cdb -G -o -c "sxe -c \"r;k 30;q\" av; g" <exe> <the wash command>` to name the function.
- `c_washing_02` (the face shot) is a seat-built [INF] framing; its 4-component Position/Heading and track
  `0x7055` are not decoded (car_wash_spec 10.3).
- The scene state is read by the car wash only; shops / house interiors keep their own paths (GMCine story
  scenes by design). No worn-clothes state exists beyond `AvatarConfig`.
- The `.cin` Fx record Z-up lead (car_wash_spec 10.4) - needs a positional cutscene voice to test.

## Session 2026-11-04 (`win`): the airport runway red stripes fixed (2DM address 3 = wrap), the Hawai igloo cutscene plays on its island and hands over to the drive

The owner: *"sync up ... -glm has work unfinished ... the issue is random texture issue at airport runway ... then
focus on cutscenes"*. Sync: `win` already held every origin tip (main = win, 0 behind glm / cline). glm's
unfinished work existed only as an **uncommitted diff in `C:\OpenTDU2-glm`** (2 files: an S_NBTEXTURES override
change + a `-nosectshadow` A/B). glm's local branch is also **32 commits ahead of origin/glm** (a cline merge,
never pushed); that is glm's to push.

**Reproduced:** `-gm FREERIDE -skipcine -nopad -fixedtimestep -tp 16600,19.0,27750 -clock 13.00` (red speckle
ground), `-tp 16860,20.5,27987` (runway: red smears + bleached surface).

### What changed
- `source/render/eden/eden_renderer.cpp`: `AddressIndex` now samples a 2DM slot address **3 (and 4) as wrap**,
  not clamp. The red ground is `Airport_lines` (TD_Road vs16/ps20). It is **not** glm's suspect `SetIndus_Paveme`,
  as `-hidematerial` showed. Its COLOR `beton_b_c` is 2DM (3,1), and the shader samples it with raw UV0
  (spirv-cross of `TD_Road_vs_16` / `ps_20`), which runs u 0.47..5.03. Clamp therefore painted the atlas's
  right-edge red/white kerb column over the whole apron. Survey (`-loguvrange '*' -edenclamp3`): slot-0 "clamp"
  materials whose UV0 leaves [0,1] on the clamped axis: airport 5/5, casino 9/13, car wash 7/11 (`SA_Stone_White`
  u 110..138, `Sector-2-4-6-5_` v 7.5..8.5). `-edenclamp3` = the A/B; `-edenwrapall` ignores all 2DM addressing.
  `-logmaterials` now prints the 2DM S_NBTEXTURES and every non-wrap slot sampler (`@sN`).
- `source/render/eden/eden_model.cpp`: `-loguvrange <substr|*>` (UV0 range + slot samplers per material).
- glm's S_NBTEXTURES patch was **tested and not taken**: the frame did not change at the airport, and it alters
  every material's permutation choice. Its comment also claimed a 2DM-0 fallback its code did not implement.
- `source/game/cine/gm_cine.{h,cpp}`: `Kind::pIsland`. Kind 0x2b (`C_IGLOO`) = `hawai` + exterior
  `islands/hawai/exterior/eph_l0a.bnk`. It used to stream **Ibiza** around a Hawai spot: floating houses, no hut.
  The end of 0x2b hands over to FREERIDE.
- `source/game/cinematic/gm_cin_viewer.h` `StoryHandoff::Island` / `EntranceSpot`; `gm_freeride.cpp` honours them
  (the island, then the spot's road-side entrance via `FindSpotEntrance`). The scene parks the car inside the hut,
  where the drive has no floor and the car tipped over.
- `source/tdu_instance.cpp`: `-storystep 13` = GM_CINE kind 0x2b.
- `source/flash/flash_movie.cpp`: the flash-art override probe checks the file exists first. It logged one
  `Image: cannot read .../mods/flash/fla_<id>.png` WARN per UI bitmap (hundreds per run); now 0.

### Verified
- Airport: runway mean diff 30.5 (before/after), apron 12-14, car wash 0.017 (unaffected), terminal front 0.0001.
  `docs/images/airport_runway_address3_before_after.webp`, `airport_apron_address3_before_after.png`.
- Cutscenes, 8 shots each: `-storystep 3/5/6` play with actors, sets and cuts. `-storystep 13`:
  `CINE: kind 43, C_IGLOO` -> `CINE: end of C_IGLOO -> FREERIDE on hawai` ->
  `FREERIDE: handoff at 'EPH_L0A_1276' entrance (12215.1, 1.0, 17067.7) on hawai`, drivable, 0 stalls
  (`docs/images/cine_igloo_hawai_before_after.webp`, `cine_igloo_handoff_freeride.png`).
- Regressions: 700-frame FREERIDE drive exit 0 / 0 stalls. `-storystep 3` frame identical to before (diff 0.007).

### Ruled out
- `-noobjects` removes the red, but `-noinstances` / `-noterrain` / `-noareas` do not -> a sector object.
  `-hidematerial SetIndus_Paveme` / `Set_` / `trotoir` leave it; `-hidematerial Airport_lines` removes it.
- The vertex bindings of `Airport_lines` are right (`-verbose` pipeline: uv0..3 at b1+8/16/24/32, normal b1+0).
- The 2DM U/V byte order: the avatar mirror cases (skin_zones_spec 1.7) fix +0x18 = U, and a swap leaves clamped
  axes out of range anyway.

### Not done / next
- The red/white striped building beside the apron (`SB_AirpoP_*` / `sb_airpo_big`) is unchanged; not checked
  against retail.
- Step 12 "Hawai First Freeride" (`isl_firstlanded`, the plane landing, `isl_firstintro`) has no kind entry:
  add one in `gm_cine.cpp` `kKinds` with `pIsland = "hawai"` and route `kStepModes[12]` like step 13.
- A few framings clip into an actor's shoulder (Solar Crown, `-storystep 5` / `6`, shots 6/3/8): compare with
  the 360 recomp before treating them as a bug.
- What the retail does with a 2DM address 3 is still [ENC]; wrap is the reading that matches the data.

## Session 2026-11-03 (prep, `win`): the integrator tree is synced to the newest line and rebuilt — cline s88 + glm s70-71b are in, the rules describe the clone layout

The owner asked for the `-win` workspace to be prepared for Claude's return:
*"make sure -win is all synced up with latest build and newest rules etc ... -glm
was stale but fixed now but -win (Claude) should be heavily behind"*. It was:
`win` sat at `9990ce6` (2026-10-30) while cline was 93 commits ahead and glm 68.

### What changed

- **Integrated, one branch at a time (RULES.md §5 order):**
  `origin/main` fast-forwarded `9990ce6 → 116597a` (the PR #3 merge of cline
  through session 82), then `origin/glm` (`4e0844e`, sessions 70-71b: the
  return-to-road badge decode, flash-art overrides, house walk outside, yard
  collision) and `origin/cline` (`6416eca`, sessions 83-88: story step 10 /
  house intros, brake glow, tunnel holes, map caches, carrier audio, foliage
  LOD1/2). `win` now contains every tip: 0 behind `origin/main`, `origin/glm`
  and `origin/cline` (102 commits ahead of the old `origin/win`).
- **Conflicts resolved by feature:**
  - `PROGRESS.md` — takes the newest board (cline s88, the same bytes glm had
    synced) and folds in glm's s70-71b rows: the island-around-the-living-room
    and the walk-out-to-the-garage rows move to **Done** (owner reports closed),
    the HUD row gains the retail return-to-road badge, the Modding row gains
    `<mods>/flash/fla_<id>.png`.
  - `CLAUDE.md` — this tree's build/run reference is `build-win` /
    `opentdu2-win.exe` (the glm merge had brought glm's paths into the shared
    file; the win tree is the one that must build its own name).
  - `docs/re/houses_spec.md` — keeps glm's §5.1/§5.2 rewrite and records the
    caravan report's three causes as closed (the entry-intro fix from cline s84
    is kept in the §9 history).
  - `source/game/spots/interior_walk.cpp/.h` — keeps glm's default island
    streaming (`-nointeriorworld` is the A/B) and reconciles the log line with
    the merged semantics; no behaviour from either side is dropped.
- **Rules/docs modernised for the clone-per-agent layout** (the files still
  described `C:\OpenTDU2`, the path remotes and the retired `sync-workspace.ps1`
  flow): `WORKSPACES.md` (current layout table, per-branch build dirs, the
  E:\OpenTDU2-main owner tree, `-Integrate`/`-Merge`), `RULES.md` (§2 paths +
  `-RequireSynced`, §5 names the win tree as integrator, §9 the new
  one-liners), `CLINE.md`'s workspace table, `CLAUDE.md`'s build/run/§7 paths,
  `check-build.ps1` (win's shard is `docs/state/main.md`, robust count parsing)
  and a rewritten `sync-workspace.ps1`: report / `-Merge` / `-Integrate`, no
  path remotes, missing trees reported instead of crashing.
- **Convergence gate (follow-up):** `check-build.ps1 -RequireIntegrated` fails
  when a tree is behind `origin/main` or `origin/win`. `RULES.md` §2 and
  `CLINE.md` now start a session with `git merge origin/win` (or `origin/main`
  when main is ahead) plus both gates, and the `sync-workspace.ps1` report shows
  the `main/win` lag for all three trees. Verified: the win tree passes both
  gates; the cline tree fails at `1 behind origin/main` until it merges the
  line (expected — that is the point).
- **`origin/main` published (2026-11-03):** fast-forwarded `116597a → 9a236c4`
  with `git push origin win:main`, so the owner's test branch and the hub now
  show the same line (main == win). `update-and-rebuild.bat` also merges
  `origin/win` from now on (main → win → glm → cline). The owner's local
  `E:\OpenTDU2-main` is ahead of that with its own test merges and its own
  unfinished merge, so it will simply merge the new main on its next run.

### Verified

- `git rev-list --left-right --count` against `origin/main`, `origin/glm`,
  `origin/cline`: all **0 behind**; `merge-base --is-ancestor` confirms both
  agent tips are in `HEAD`.
- Both PowerShell scripts parse (`Parser::ParseFile`) and
  `sync-workspace.ps1` reports all three trees.
- **Build 425 (`f93327e`) is clean** (`build.ps1` exit 0, exe
  `build-win\bin\opentdu2-win.exe`, 9,462,272 bytes): `check-build.ps1` says
  **OK**, stamp `build 425 (f93327e) · win`, 0 behind origin/win, 0 behind
  origin/main. Headless smoke: `-gm FREERIDE -skipcine -no3d -nopad -exitframe
  150` → exit 0, `600 beats, 0 stalls`; the 60-frame repeat exits 0 too. The
  game log banner reads
  `OpenTDU2 0.1.0 ... (build 425 (f93327e) · win)`; the only stderr lines are
  the expected missing `mods/flash/fla_*.png` overrides and one
  `attachMovie: no export 'specificHelp'`.

### Not done / next

- **Push** `origin/win` (`git push origin win`) once the build is verified; the
  owner and the other agents pick it up from GitHub.
- The owner's `E:\OpenTDU2-main` had its own in-progress merge (`UU CLAUDE.md`)
  when this was prepared — it is the owner's tree and was left untouched.
- Session number: this prep is not a numbered agent session; the next Claude
  session continues the numbering in this shard.


## Session 2026-11-11 (`main`): the "Eden island" bisected out of Hawaii into its own playable island

The owner: *"there's meant to be another island ... called eden island ... a small island off the hawaii
island ... possible to glitch there ... was in tdu 1 also and has a road etc ... waterfalls ... it would be
neat if we can use and expand it also be a nod to the creators"*. (Same session, earlier: the map-from-real-
locations discussion that `docs/re/modding.md` / the map_gen notes feed into.)

### What changed
- **`docs/re/eden_island_spec.md` (new)**: the islet measured out of the retail data — area 10-7, sectors
  10-7-3-1/3-2/4-2, ~600x650 m, peak 109 m, a **flooded crater** (interior -5.4 m inside a 73 m rim),
  ~16 waterfall meshes, the `SB_FBRID` footbridge, `FU_LINEA`/`FU_CATAM` ferry props, its road only as
  baked mesh + `-R.shk` (zero road-graph nodes within 1.2 km), painted on the retail minimap, **void at
  the island-level heightfield** (retail never LODed it in). Section 2 lists the ten verified loader facts
  the bisect relies on (name-fetched bank entries, index-derived sector positions, area-local Havok frames,
  3dd node names = hashcodes, the data-driven sea level, the XTEA-CBC worldconfig...).
- **`tools/mod/make_eden_island.py` (new)**: assembles `islands/eden/` into the mods overlay from the
  user's own game data — banks renamed (`hawai`->`eden`, `area-10-7`->`area-0-0`, sectors kept at 3-1/3-2/
  4-2 within area 0-0), the area `-o.3dd` sector-group hashcodes patched `Sector-10-7-*`->`Sector-0-0-*`
  with the non-shipped sectors' CITY impostors sunk -500 m, the area heightfield flattened outside the
  islet rect (drops O'ahu's severed coastline), the island heightfield flattened with the islet re-sampled
  from the area sheet, the map screen cropped + sea-masked by the area sheet, road IA / spots deliberately
  absent, `commonworld.bnk` / `worldactors.bnk` / `game_weathers.xmb` copied, and a fresh XTEA-CBC
  `worldconfig.cpr` (`NbColumns="1" NbRows="1"`; the encrypt side is the decipher's exact inverse,
  validated by decrypt->re-encrypt byte-identical on hawai's own file).
- **`tools/x360/build_bank.py` layout bug fixed**: payloads were written back-to-back while the table
  carried 16-aligned offsets — every entry after a payload of 4 mod 16 read garbage. It surfaced here
  because the all-DXT banks the old `--roundtrip` validated never had an unaligned payload. Now pads each
  payload to the alignment; round-trips re-verified on retail banks with .pmi/.shk/.uva (ibiza area-3-3,
  hawai sector-10-7-3-1: 0 problems).
- **`source/game/worldviewer/gm_world_viewer.{h,cpp}`**: `kIslands` gains `"eden"` (+ button label, F-key
  hint), the F5-F8 island-switch loop covers all four (was hard-bound to 3), and `-nopanel` starts with
  the F2 panel closed (headless screenshots).

### Verified (build 17, bed87e8+dirty, headless)
- XTEA self-test OK; the tool writes the full tree (~81 MB, mostly the verbatim commonworld copy) with a
  per-bank read-back self-check.
- `-gm FREERIDE -island eden -tp 2950,1350`: `eden: 1 x 1 areas (worldconfig.cpr)`, `1 areas loaded`,
  `island ocean bounds (-98304 -2 -98304) - (165888 -2 153600)` (the -2 m plane is Hawaii's copied water
  level — the crater floods at retail depth), sector 3-1 **75,042 triangles / 400 boxes / 370 capsules /
  523 instances**, 3-2 4,503/135, 4-2 13,409/44; the baked road triggers "Return to the road" in the
  flooded crater. The 61 missing pure-ocean sector banks warn-and-skip as designed; spots / race gates /
  aircraft / birds warn-and-continue.
- World viewer `-island eden`: aerials `docs/images/eden_island_aerial.webp` (islet + the moored
  catamaran) and `eden_island_crater.webp` (summit road, crater bridge, dock buildings, causeway); the
  drive shot `eden_island_crater_drive.webp`. Retail-map evidence `eden_island_minimap.webp`, flattened
  area sheet `eden_island_heightfield.webp`. 0 stalls, no validation VUIDs.

### Not done / next
- The map screen crop is blurry (94x114 px source; spec 7); the HUD zone label says "Ibiza: Area 1"; the
  shipped weather preset is Hawaii's.
- Expansion (spec 4): spots via `add_spot.py`, props via `props.txt`, an authored road graph (the `.IA`
  parsers take generated files), then the DEM+OSM generator on this 6x6 km canvas.
- `-island eden` FREERIDE boot without `-tp` not exercised (no house/outro flow on a third island by
  design); licences treat it as island 0 like any non-hawai name.

## Session 2026-11-13 (`main`): car stickers / vinyls — the retail decal packs, a per-car canvas and the sticker-shop editor

The owner: *"Can we dig into the car customizations with stickers/vinyls?"* (fallback quests were parked:
both proved feasible, the sticker feature won the owner's own priority). The two Explore probes + the bank
dumps answered feasibility in the plan phase: the packs are plain data, the render side needed one fact.

### What changed
- **The slot fact (spec 1)**: the car paint's sticker slot ships as **`STIKERS`** on the California's
  `Paint` / `Paint_SK` (TD_CarPaint2) — the data's own spelling; one_77's is the `USER_TEX` paint_spec 4
  recorded. Already counted into `S_NBTEXTURES` (the placeholder `user_tex` texture ships in the slot), so
  binding into it never re-permutates. Sampled with the body's UV0 — the whole-texture A/B
  (`docs/images/sticker_bind_ab.webp`) settles it empirically. `Paint_LR` (TD_CarPaint) has no slot: only
  the high LOD takes stickers.
- **`source/game/freeride/sticker_bank.{h,cpp}` (new)**: `StickerBankCatalog` (the 26 `DBStickersBanks`
  rows, localized names through `gpDatabase->getString`; banks load on demand — the art `.2db`s through
  `TextureCache::loadBankTextures` under `stickers/<bankfile>` owners, released at FREERIDE exit; the MAT_
  library's COLOR hashcodes resolve directly, verified 24/24 on `st_basic_nb`; `isBankSold` = `_isvalid`
  and not the casino/chase specials), `StickerPlacement` (bank, index, U/V centre, scale, angle, tint),
  `StickerCanvas` (1024² transparent target, the dash-GPS screen's `createRenderTarget(VK_FORMAT_UNDEFINED)`
  + `beginRenderTarget` + Renderer2D pattern; rotated tinted quads through `drawTriangles`, redrawn only
  when dirty), `StickerArt` / `StickerResolveArt` helpers.
- **`cinematic_car.{h,cpp}`**: `bindStickerTexture` — the slot-variant hunt (`USER_TEX`/`STIKERS`/
  `STICKERS`) on the paint materials (the same family filter `applyDatabasePaint` uses), pointer swap +
  `updateProgram`; `-logstickers` dumps each paint material's slots.
- **`paint_menu.{h,cpp}`**: two pages. PAINT unchanged; **STICKERS**: pack list (name + price +
  recolourable mark) → thumbnail grid (the art as its own thumbnail) → **place** (a new placement at the
  canvas centre, held Menu keys move at 0.35 canvas/s (a fifth with Start), accessories left/right scale,
  up/down rotate, X cycles the 11-colour tint palette on monochrome packs, A buys (the bank `_price` per
  sticker [INF]; a wallet that cannot pay drops the sticker), B cancels — a new sticker is dropped, an
  owned one reverts to its pre-edit values) → the placed list (A edits, Y removes without refund). The
  layout lives in `PlayerProfile::OwnedCarStickers` and is pushed to the canvas on every change.
- **`player_profile.{h,cpp}`**: `OwnedCarStickers` map + the `car_stickers` save/load line
  (`<id>:<bank>;<index>;<u>;<v>;<scale>;<angle>;<tint>|...`, the bank names carry none of the separators).
- **`gm_freeride.{h,cpp}`**: `applyOwnedStickers` beside `applyOwnedPaint` at both car-load sites (mode
  enter + the car swap); `drawOffscreen` composites the canvas when dirty and binds it once; the ESTICK
  flow hands the menu the driven car's canvas (`setStickerContext`); the **place-mode camera cut** — inside
  the walk-in shop, placing renders the freeride scene instead of the room (both draw paths), a 3/4
  rear-left pose taken once on entry (the retail editor's garage-car camera [INF]); exit tears the canvas
  + the catalog owners down. Flags: `-nostickers`, `-logstickers`, `-stickertest <bank>,<index>` (the
  M0/M1 probe, kept as the A/B hook), `-stickershop` (the menu over the freeride scene at spawn, so
  autoplay can drive the editor).
- **Docs**: `docs/re/stickers_spec.md` (new: the slot truth, the bank layout, the editor, the flags, the
  evidence, the open list), README flags row + the two FREERIDE feature rows, PROGRESS row + board header.

### Verified (build 20 (a4a50ba+dirty), headless, 0 VUIDs, 0 stalls)
- The bind A/B: with the slot swap the `st_basic_nb` white shapes appear on the body
  (`sticker_bind_ab.webp`, same pose before/after) — the retail shader does the rest.
- Second car + colour art: the Q7 in a full `st_fun01` green/white livery (`sticker_q7_livery.webp`),
  a vector art over the bonnet from the cockpit (`sticker_bonnet_art.webp`). (The R8's freeride model
  load is incomplete — a pre-existing quirk, not the sticker path; the Q7 stands in.)
- Persistence: a 16-sticker layout seeded into `profile.sav` comes back on the car at spawn
  (`sticker_spawn_layout.webp`).
- The editor driven by autoplay (`-stickershop`): place → move (U 0.5→0.908) → scale (0.22→0.52) →
  buy — "Sticker bought: $5,000" with the money exactly $5,000 lower
  (`sticker_editor_placing.webp` / `sticker_editor_menu.webp`), and the game's own save writing the
  17-entry `car_stickers` line. The tint-cycle press in that script used a wrong autoplay action name
  (`option1` vs the table's `opt1`) so the palette path ran only by code inspection — the owner's pad X
  exercises it.
- The owner's profile was restored from its pre-seed backup after the runs (the $5,000 test purchase
  rolled back with it).

### Not done / next
- The owner's in-play pass: the real ESTICK flow (walk in, the seller, place with the pad) and the feel
  of the keys; the tint cycle from a pad.
- Garage / house scenes' cars do not wear the saved layout yet (the driven car only — the scope
  `applyOwnedPaint` had at first).
- `DBStickersPricing` per-action prices; the retail stamping math is [ENC] (the port composites its own
  way); `FADEALFA` and the `IC_NB` icon question unexplored.
- The quests (driving-crazy score challenge, slow A-to-B, no-damage delivery) stay parked, feasibility
  in hand (FRIM scorer, GPS route + HUD, the damage metric to build; the `-switchcar` wedge shapes the
  "random car" flavour).

## Session 2026-11-19 (`main`): TDUSC weather port — second cloud layer + cirrus, height+noise fog

The owner: *"Is it possible we can use some of this weather stuff to improve our weather system please
thanks start with the clouds and fog then anything after"* — the TDUSC dig earlier the same session
(`docs/re/tdusc_weather_physics.md`) is the source.

### What changed
- **Second cloud layer** (`resources/shaders/volclouds.frag`): the march now runs two slab segments
  front-to-back (`marchSegment`, 56 + 16 steps, transmittance carried) — the second slab derives from the
  pushed main deck (+800 m above its top, 1300 m thick, threshold +0.10, twice the noise tile scale, no
  convection cells, light erosion) because the TDUSC template keeps `Coverage##CloudCoverageSecondLayer`
  and its densities at defaults. The `Rainy Cloud` coupling is the darkness scalar growing it into the
  storm deck. The two opt-outs ride in a 1×1 RGBA32F texture (`uLayerParams`, created once in
  `EdenClouds::initialize`) because the march push block is at the 128-byte cap.
- **Cirrus sheet** (`resources/shaders/volclouds_cirrus.frag`, `EdenClouds::drawCirrus`): the TDUSC
  cirrus is a 2D texture, not raymarched — this is a band intersection at 6500-7200 m over a
  CPU-generated 512×256 anisotropic streak texture (tiling octaves, band modulation), forward-scatter
  glow at the template's Mie anisotropy 0.8, alpha capped ~0.55 (`NewCirrusCoverage` 0.3/0.6/0.1),
  dissolved by darkness (storm), depth-rejected against the existing `copySceneDepth` copy. Drawn in the
  blended pass after the deferred flush, before `compositeCloudLayer`, so the deck occludes it.
- **Scattering**: the forward HG lobe 0.60 → 0.80 (TDUSC Mie Anisotropy; also the silver-lining helper);
  the back lobe and sigma unchanged.
- **Height + noise fog** (`resources/shaders/tdfog.frag`, `EdenSceneView::applyHeightFog`): a fullscreen
  pass over the finished image in the post section, after photoshop and before the upscalers —
  `finishedSceneColor()` (new) is what the upscalers and the final blit consume, so FSR/DLSS/XeSS output
  the fogged image with no other plumbing. Model: `1 - exp(-density·height·km)` with the TDUSC falloff
  0.02/m, layer at sea level, 3-octave value-FBM at 200 m cells drifting with the cloud wind, tint to
  `FogColor`, amount `0.12 + 0.55·Skydome_haze` (the haze pass's own key drives it). Depth-rejects on the
  opaque depth copy (dome pixels hold the cleared far depth and are skipped). Opt-out `-notdfog`.

### Verified (build 306, stamp `0f70dbc+dirty`, headless RTX 5070 Ti, exits 0)
- The A/B matrix `_twx_*.ppm` (day/storm/sunset/night × on/off, same `200:look=0,-1` pose, frame 460):
  day on/off mean diff 4.7 — decomposes as second layer 4.6 (`-nosecondlayer`), cirrus 1.3
  (`-nocirrus`), fog 0.8 (`-notdfog`); storm on/off 1.3 (the deck stays overcast, rain and puddles
  intact); day on/off visually: a fuller deck with a higher band + soft wisps, storm_on reads more
  layered. No mirroring, no ridge-occlusion regressions, `-oldclouds`/`-novolclouds` untouched.
- Perf: 74 fps avg (13.5 ms) at 1080p — the +16-step march is invisible in the frame budget.
- The "night" pair ran with `-clock 2` but the weather preset follows the weather cycle's own clock (the
  WorldClock log line confirms 02:00 while the sky is daylight) — pre-existing behaviour, both variants
  identical apart from the weather deltas.

### Not done / next
- Owner eyeball in play (the shots are headless; the flags are there to A/B live).
- From the same TDUSC data: the hour-keyed sun track (`Polar Coordinates`/`Color`), the
  `TimeCycleElementWind` gust model (grass wiring is open), wet-absorption puddle accumulation.
- The second layer's derived constants (+800 m, +0.10 threshold) are first-cut; the template's unset
  layer-2 fields give no numbers to port, so tune from play if the deck reads too tall.

## Session 2026-11-19 (`main`, later): TDUSC weather port 2 — sun track, wind gusts, wet-absorption puddles

The owner: *"please implement these"* — the three follow-ups of the clouds/fog pass above, same
TDUSC TimeCycle source (docs/re/tdusc_weather_physics.md 6.2).

### What changed
- **Sun track** (`source/render/eden/tdusc_sun.h`, opt-in `-tdsun`, `-tdsunpeak`): the azimuth track
  ported exactly, rise/set 6.65/19.9 from the Intensity keyframes, Color/Intensity tracks modulate the
  retail preset's sun_diffuse; elevation = sine arc [INF] (the template's own numbers don't decode).
  `EdenSceneView::setDayHour(hour)` fed per frame from `weatherCycle` (gm_freeride.cpp, next to the
  per-frame `setWeather`); the override happens in `setupScene`, so shadows / dome / moon / clouds /
  every `__DirLight_Dir_0` follow the one global vector.
- **Wind** (`source/render/eden/eden_wind.h/.cpp`): the template's structure as one shared `WindModel`
  (sway oscillator + gust envelope, base = the clouds' old 42/21 x clouds_cloud3dSpeed). Consumers:
  grass `UserVector0` (v0.x strength, **v0.z the travelling phase** — grass_spec.md 7's open wiring),
  blade-grass amplitude, cloud/cirrus/fog drift (the two hardcoded 42/21 sites are gone), ocean
  `setWaveDrift` (CLI flags keep precedence). `-logwind` prints the state.
- **Wet absorption** (`eden_surface_fx.{h,cpp}`): `GroundWetness::absorption()` from the TDUSC
  TerrainLayerComponent means; soak rates re-ordered (dirt wets faster than grass now); puddles hold
  Water 0..1 (grow with the rain at 1-absorption, dry with the sun at absorption, quad scales/fades,
  dies below 0.05) with the family from a road probe at the cell (mesh above heightfield = tarmac);
  spawn thresholds scale with absorption, tarmac anchors the retail 0.25. `-notdwet` = the old binary
  switch.
- **Fixed en route** (`weather_cycle.cpp ParseHourMinute`): it zeroed hour/minute *before* the null
  check, so the unconditional `-clock` scan wiped every `-clocktime` value — the pinned-clock A/Bs ran
  at midnight no matter what (probes in the logs settled it: sscanf was fine, the null path ate it).

### Verified (build 338, stamp `4380bf6+dirty`, headless RTX 5070 Ti, 8 runs exit 0)
- Sun: `-tdsun` logs morning 7.00 h -> elevation 5.1 deg / azimuth 94.7 deg, noon 59.2 / 172.7,
  evening 19.50 h -> 5.9 / 289.8 (the track sweeps east -> west, dim at the edges); noon within 5.5
  mean of retail (minimal regression), morning/evening 64/52 mean (the point of the feature). Shots
  `_tw2_sun_*.png`: dawn-dim street, low-sun haze at the end of the road.
- Wind: `-logwind` shows the gust envelope 0 -> 0.28 (fade-in) -> 1.60 (hold) with the drift chasing
  25.5 -> 46.8 m/s; the grass phase slides continuously (v0.z). wind on/off shots 5.3 mean.
- Puddles: storm runs early/pooled/binary — pools grow then hold (3.4 / 0.88 mean), `-notdwet`
  returns the binary look.
- Perf: 113 fps avg (8.9 ms) at 1080p.

### Not done / next
- Owner eyeball in play (sun track is opt-in; flipping it on by default is one line).
- Moon `Use As Directional` (the template makes the moon the night directional light), per-channel wet
  darkening tint from the absorption RGB, rain slant (retail kCarSpeedFactor is [ENC] 0).
- The gust constants (interval 7 s, boost 1.6) are first-cut [INF]; the template leaves its wind values
  at defaults, so tune from play.

# Cutscene inventory (`euro/bnk/scenarics/cinematics/`)

Tags as in CLAUDE.md. 256 banks (299 MB) = 42 cutscenes x language variants (`__fr|ge|it|sp|us` and `ja|pl|ru` for some).
Every cutscene is **authored data** (`.sce` scene + `.cin` shot / fx / message tracks + `.anm` animations + `.bfx` voice
Fx + `.3dd/.3dg` props / sets + `.shk/.dhk` placement hash); no video. Players already in the port: `Cinematic` /
`CinematicScene` / `CinematicAudio` (`source/game/cinematic/`), `EdenAvatar` + `EdenAnimation`, used by
`IntroCinematic` (2000 lines), `FirstOutro`, `FadeFreeride`.

## Tooling (2026-10-29): `otdu2_cine_tool` — generated ground truth

`tools/cine_tool/main.cpp` dumps every timeline from the banks with the game's own parsers:

```sh
build-glm/bin/otdu2_cine_tool list  C:/TDU2-Gamedata/TestDrive2            # table: bank, cinematic, duration, shots, scenes
build-glm/bin/otdu2_cine_tool dump  C:/TDU2-Gamedata/TestDrive2 _cine_dump -json
```

`_cine_dump/<bank>/<cinematic>.md` has per scene: the **authored origin matrix** (position + heading, DATA+0x28 of the
`.sce` — the "world origin and heading" hunted for below *is in the data*; runtime-anchored scenes like the dream get
their world transform from game code, the authored value is the authoring-level placement), the shot table (camera,
scene window, rate, fades), the message cues (`weat:`/`wmod:`/`shad:` per shot — confirmed for Meeting Tess) and per
camera/actor: position keys with derived yaw, fov, properties. `-json` adds every raw key (733-key baked paths
included) for the planned editor. 42 banks / 136 base cinematics dump and parse clean.

## Tooling (2026-11-01, session 63): the manifest + the viewer (phase 2 of 3)

**`otdu2_cine_tool manifest`** (`tools/cine_tool/manifest.cpp`) is dump plus the resolution layers — what the runtime
resolves when it plays the scene:

```sh
build-glm/bin/otdu2_cine_tool manifest C:/TDU2-Gamedata/TestDrive2 _cine_manifest -json [-variants] [-lang fr] [-avatar AVT_Female_Intro]
```

- **Scene REFS -> bank assets**: the `.sce` REFS (ANIM 0x0302 / MORPH 0x0306) resolved through the bank's
  `hashcodes.xmb` to the exact `.anm` / `.fxa` entry + size; every Animation track key with its ref, clip length and
  crossfade.
- **Objects -> entities**: each object's `__Hc` matched against the bank's `entities.xmb` (class, `__Name`, position);
  `Entity__PNJ` entities resolve their **DBPNJ row** from `db_data.cpr` (read offline through the same
  `LoadXmbFromFile` decrypt as `GSDatabase`): sex, `.avt` BodyID (body / head names), the 10 outfit slots
  (`_clothe_<slot>` -> DBClothe `_file` with the `M_`<->`W_` sex swap, `_color`). 426 character rows over the tree.
- **Voicelines**: Fx events -> `Fx__Root` -> `.bfx` node walk (the SND nodes of `cinematic_audio.h`'s documented
  layout) -> per-language wav names; the language bank `<bank>__<lang>.bnk` of `-lang` resolves its own wavs
  (verified `-lang us` vs `-lang fr` flip). Positional-vs-2D from the 0x10 flag, world position from the record
  matrix. 142 voice cues in the tree's JSON.
- **Message protocol**: cin-level (`flsh:` = INTRO.vmf gotoLabel, `shad:`, `weat:`, `wmod:`, `snd:`) and actor-level
  (`opd*`/`cld*` doors, `avm4`/`avm1` seating, `lkon`/`lkof`) with what the port does; `zcvn` / `zcne` / `zcde` /
  `phot` flagged **[ENC]** (retail-encrypted, ignored by the port).
- **Story placements + avatar ruling**: the GMCine kind table matched by bank stem + definition name (kind 1 = 2
  variants, 2, 3, 0x2b); `-avatar` (default `AVT_Male_Intro` + `AVT_Female_Intro`) resolves the DBPNJ outfit and the
  `_W` cinematic variant.
- **Subtitle skeletons**: `<C_NAME>.csv.skeleton` per cinematic — one empty `start,end,""` row per voice cue,
  named by the **definition** the engine plays, so the filled file drops straight into
  `<userdata>/cine_subtitles/`. 84 skeletons over the tree.

**The viewer** (CINVIEWER upgrades, `source/game/cinematic/gm_cin_viewer.cpp` + `cine_subtitles.{h,cpp}`):

- the **cinehud**: the shot timeline (cut points, Fx ticks — amber voices), the shot / time / active-camera readout
  (`-cinehud 0` off);
- **pause + scrub**: the camera key pauses (`press=camera` headless), menu left / right step 0.5 s through the new
  `IntroCinematic::seekMain` (the -cinetime seek path: state messages applied up to the time), the browser's slider
  drags; `-cinetime` now seeks the CINVIEWER too;
- the **camera-path debug draw** (`-cinepaths`): every camera's Position keys (cyan), the actor paths (orange) and
  the scene origin axes projected through the active camera;
- the **browser** (ImGui): the bank's cinematics click-to-play, a bank / cinematic pair to load (the mode re-enters),
  the scrub slider;
- **authored captions**: `<userdata>/cine_subtitles/<C_NAME>.csv` (`start,end,"text"`) drawn bottom-center during any
  scene (CINE, CINVIEWER, the car wash, the intro — the hook is IntroCinematic's draw), `-subtitles 0` off,
  `-cinesubdump` writes the skeleton for the playing cinematic. The retail data ships **no** subtitle text for the
  cutscene speech (voices only, `CROSSBUILD_PLAN.md` 51) — the text is authored.

Verified (docs/images/*-s63.png): the caption "Tess: Hey! Are you awake in there?" on screen at t=2.6 (voice cue
1.75-4.79) and absent with `-subtitles 0`; `press=camera` paused at 21.07 s and `press=right` stepped exactly +0.5 s
(PAUSED badge, frozen camera); the paths overlay at eye level and over `-cintopview`; `-cinesubdump` = 2 cues
matching the manifest's skeleton. Regressions: `-storystep 5` exit 0, `-gm CINE -cinekind 1` exit 0, `-atwash` exit
0, 700-frame FREERIDE drive exit 0 (0 stalls).

## Solar Crown interior (C_SOLAR_CROWN_I): the TV overlays and the avatar visibility (2026-11-04, `win`)

The owner: *"players avatar seems to be in the way most of the time ... UI elements ... top right
should show solar crown logo, at bottom right says tess wintery, and when the camera points to avatar
it should print players name"*.

- **The overlays are cued by the .cin itself** [DATA]: `lgtv:open` 11.02 s / `lgtv:clos` 46.36 s,
  `lgsc:open:Tess Wintory` 14.41 s / `lgsc:clos` 19.41 s, `lgsc:open:-player` 37.04 s / `lgsc:clos`
  39.04 s (shot 7 `cam_challenger`, the camera on the avatar). The art is GENERAL.vmf: `/logo_TV`
  (sprite 847, labels `open` 8 / `close` 26, the Solar Crown TV logo, top right) and `/logo_SC`
  (sprite 855, `open` 9 / `close` 38 / `open_double` 56 / `close_double` 85; its text field is
  `/:PNJName`, sprite 852; `open_double` adds `/:PlayerName`, sprite 854) [DATA]. The listener is
  [ENC]; `IntroCinematic::applyLogoMessage` plays the labels, `-player` = `/:PlayerName` (GMCine sets
  it from the profile / `-playername`) [INF]. The name is spelt **Tess Wintory** in the data.
  `-dumpgeneral <depth>` dumps GENERAL.vmf's clip tree from any cinematic.
- **The avatar "in the way"**: its `.sce` Visibility track has real keys - hidden 11.41..36.4 and
  39.48..58 s of the scene (the TV interview of Tess and the Todd part) [DATA]; the port applied
  Visibility to props only, so the player stood in front of the TV cameras. `SampleVisibility` now
  applies any **multi-key** Visibility track to PNJs and the player too (a lone key stays ignored: Tess,
  the avatar and the car carry a lone 0 and are on screen in retail). The same data now also hides
  Assistant_01/03 and Todd until 48.4 s and Tess 3.19..5.38 s (the dolly-in, likely her costume
  change - unverified against retail). `-nocinevisibility` = the A/B.
- Evidence: `docs/images/solar_crown_i_avatar_visibility_before_after.png` (21 s / 25 s, left before),
  `solar_crown_i_tv_logo_name_captions.png` (the logo + "Tess Wintory"; the logo + the player name).
  `-storystep 3` frames identical (diff 0.000); `-storystep 5` differs only where the data hides actors.

## The owner's story chain (what the retail game does, in order)
| # | Beat | Bank | In the port |
|---|---|---|---|
| 1 | Party intro, character select | `intro/new_intro` (+ `avatars`) | **done** (`GMAvatarChoice` / `IntroCinematic`) |
| 2 | House walk, garage | `intro/garage` (187 assets, 10 `.cin`, 16 anims) | **done** (`GMFirstWalkMode`) |
| 3 | First drive in the California | (game) | **done** (`GMFreeRide`) |
| 4 | Knock at the window (live game view) | `intro/fade_freeride` (WOMAN, `ev_tessincar_toplayer01_npc.anm`) | built, **cut by default**, `-wakeup` |
| 5 | White flash, dream: Tess at the door, phone call, key hand-over, both get in | `intro/meeting_tess` (97 assets: 33 anims incl. `meet_sleep`, `meet_gocar`, key / phone props, 23 voice Fx, 16 shots, 66.7 s, M / W variants) | **not built** (fully mapped, STATE session 7 "Step 3 data map") |
| 6 | Drive to the Solar Crown (Miami) | (game: goal text loc 6111) | goal text only; mission, GPS target, Tess as passenger **not built** |
| 7 | Arrival outside the Solar Crown | `intro/solar_crown_ext` (13 anims: `sol_run-*`, `sol_run_tess_npc`, assistant, guards; 17 models) | **not built** |
| 8 | Inside: TV crew, Todd, signing | `intro/solar_crown_int` (41 anims: `sol_talktv01_tess`, `sol_talktodd-*`, `sol_inscription-*`, `sol_enterclubpart01_tess`, TV camera props; 15 models, 20 voice Fx) | **not built** |
| 9 | Used car dealer, choose 1 of 3 cars | `car_dealer` (7 `.cin` + `.sce`: `INT`, `BUY`, `BUY_BIKE`, `PAINT_V`, `WHEELS_V`, `INTERIOR_V`; cameras only, no anims) | the 3 cars are in the DB (`docs/STATE.md` session 38); native menu only |
| 10 | F.R.I.M. (risk driving), bank | (game) | risk driving built (`docs/re/skills_spec.md`) |

## Everything else (not in the story's first hour)
- `island/` `isl_firstintro`, `isl_firstlanded` (plane landing, 13 anims), `isl_igloo`: arrival on Hawai / first island.
  **`isl_igloo` plays (2026-11-04, `win`)**: GM_CINE kind 0x2b = story step 13 "Igloo First Intro" (`-storystep 13`, or
  `-gm CINE -cinekind 43`). `EPH_L0A_1276` is a **Hawai** spot: only `islands/hawai/level/spots.bin` carries it [DATA].
  Before this, the kind had no island and no exterior. `ReadSpot` found the Hawai coordinates (12239.7, 1.37, 17060.9),
  but the streamed world was Ibiza, so Ibiza's houses floated above a bare plane with no hut shell
  (`docs/images/cine_igloo_hawai_before_after.png`, top). Now `GMCine::Kind::pIsland` = `hawai` feeds
  `Setup::HouseIsland`, and the exterior is `islands/hawai/exterior/eph_l0a.bnk`: the Quonset hut, the hedges and the
  sea draw (bottom). The scene ends at about 20 s with the car parked inside the hut. The end hands over to FREERIDE
  on Hawai at the spot's road-side entrance (`StoryHandoff::Island` + `EntranceSpot`, spots.bin +0x28/+0x38)
  (`cine_igloo_handoff_freeride.png`) [INF: the retail step-13 end is encrypted]. Not done: `isl_firstintro` /
  `isl_firstlanded` (step 12 "Hawai First Freeride", the plane landing) have no kind-table entry yet.
- `casino/` `spt_<ibiza|hawai|casino>_arrival|departure`: the casino trips. **Played (2026-11-04, `win`)**: the
  seaplane trip FREERIDE -> `C_IBIZA_DEPARTURE` (dock `ELANDCAA`) -> `C_CASINO_ARRIVAL` (dock `ELANDCA`) -> CASINO, and
  back `C_CASINO_DEPARTURE` -> `C_IBIZA_ARRIVAL` -> FREERIDE (`casino_spec.md` 10, `CinViewerRequest::After`).
- Races: `asphalt/` `sta_a1 a3 a4 a5 a6 a7_a2`, `sta_duels`, `sta_firstintro`, `sta_winloosepodium`; `classic/` `stc_c3 c4 (+_high)`, `stc_duels`,
  `stc_firstintro`, `stc_winloosepodium`; `offroad/` `stb_b3 b4 (+_high)`, `stb_duels`, `stb_firstintro`, `stb_winloosepodium`; `cups/` `cup_hawai1`, `cup_hawai2`,
  `cup_ibiza1`, `cup_win`: race intros and podiums per class (needs the races).

## Effort `[INF]`
- **C_MEETING_TESS**: one new class like `FadeFreeride` with two actors (the player avatar asleep in the car, Tess), 2 props, 16 shots + per-shot weather
  modifiers (`wmod:Meeting_Tess_02..16`), the Fx / voice cues, the M / W variant, then hand back to the drive. Largest unknown: the in-car interior shots.
- **Solar Crown drive**: target position (spots / DB), GPS goal, Tess seated as passenger, arrival trigger.
- **Solar Crown ext / int**: two more scenes of the same kind; many props.
- **car_dealer**: cameras only - the showroom set is a separate interior bank; wires into the dealer menu.

## Where Meeting Tess is placed - RESOLVED (2026-10-29, session 44)

**In front of the E_HOTELA exterior** (spots.bin E_HOTELA (11099.1, 3.66, 17233.5) heading -2.440), the seafront
hotel with the arcade, the flags and the semicircular fountains - identified from the owner's 360 recomp grabs
(photo mode in front of the hotel, map at the coast road) [DATA: owner's recomp]. The scene is NOT in the
spot's frame:
- **Rotation**: the recomp's dream camera headings are 180 - the port's .sce camera headings for three shots
  (-126.1 / -179.8 / 76.1 vs port -53.7 / -0.2 / 103.9): the 360 world frame is the port's turned 180 deg about
  X (its Y values are negated), and the scene frame has heading 0 in it [DATA + INF]. The owner's photo-mode
  heading at the hotel (119.1) agrees (port ~69 deg from the promenade -> 111).
- **Position**: chosen so D_CAR sits on the drive in front of the entrance arches, left of the central
  fountain (frame (11103.0, 4.6, 17252.0), checked from above with `-cintopview`) [INF]; the height is snapped
  to the ground under the car (`IntroCinematic::snapSceneToGround`, -0.14 m).
- The bank brings no set of its own (only the two `garage_int` interior pieces, the key ring and the phone),
  and the entity has no StreamCenter / Micro / CinCamera (all 0) [DATA].
- Evidence: `docs/images/port-meeting-tess-hotel-s44.png`, `port-meeting-tess-keys-s44.png`,
  `port-meeting-tess-topview-s44.png`.
Superseded below: the party-house frame, the E_HOTELA spot frame itself (0.9 m sunk, side terrace), the
second EPH_L3DI spot, and the unplaced `ech_ib` / `ech_ic` variants.

## Where Meeting Tess is placed in the world - first observation (2026-10-28)

**Observed in recomp run `tdu2_186.log` (scene played 18:23:29-18:24:16, game closed before it ended).
This is a camera trace, not yet a resolved world placement.**

```
[18:23:28.996] [scene] intro cinematic selected: id=0 path='scenarics/Cinematics/intro/meeting_tess'
[18:23:28.996] [scene] at cinematic start camera pos=(-66.46, -51.69, 745.90) heading=14.5deg fwd=(0.249, -0.056, 0.967)
[18:23:29.093] [scene] camera jump (cinematic cut/teleport) camera pos=(-21420.54, -888.57, 16520.55) heading=14.5deg fwd=(0.250, -0.052, 0.967)
[18:23:29.608] [scene] camera jump (cinematic cut/teleport) camera pos=(-19.75, -5.24, -12.80) heading=-126.1deg fwd=(-0.808, -0.015, -0.589)
[18:23:35.713] [scene] cinematic '...meeting_tess' camera pos=(-2.75, -6.89, -26.31) heading=-179.8deg
[18:23:37.789] [scene] cinematic '...meeting_tess' camera pos=(23.56, -5.24, -1.00) heading=76.1deg
[18:24:16.027] [scene] cinematic '...meeting_tess' camera pos=(-9.13, -4.58, -23.86) heading=-168.5deg   (last line; game closed before the scene ended)
```

What it shows:

* The engine selects `scenarics/Cinematics/intro/meeting_tess` through `sub_88F74170`
  (the id read at `this+0x11F00` logged 0 here, so treat that field as unconfirmed).
* During the scene the camera stays in a **scene-local frame**: x in about -24..+25,
  y about -3..-12, z about -45..0, a ~70 m box around the scene origin. The shots cut
  between a few positions (e.g. (-19.7,-5.2,-12.8), (23.6,-5.2,-1.0), (-2.8,-6.9,-26.3)),
  matching the screenshot of Tess and the hotel staff under the arcade. So shot cameras
  are relative to an origin, as the `.cin` assert names suggested.
* **Correction (runs 187/188):** the one-frame far position is *not* a stable scene
  anchor. It differs every run - (-21420.54, -888.57, 16520.55), (-1785.95, 568.10,
  -24402.00), (-5236.47, -2557.65, 24581.61) - always with the *same orientation as the
  game camera just before*, and a physical-memory scan for the third value found 0
  copies. Treat it as a camera-transition artefact; discard the earlier "candidate
  anchor" idea. The game camera before the scene also differs per run ((-66,-52,746),
  (-82,-123,254), (-240,-112,610)), so it is not a placement either.
* **What is stable:** the first shot of the scene is identical in runs 186 and 188:
  camera (-19.75, -5.24, -12.80), heading -126.1 deg, fwd (-0.808, -0.015, -0.589).
  A memory scan for it (run 188) found 24+ copies, all in the same 4x4 layout
  (rows (-0.587,-0.069,0.807,0), (-0.053,0.998,0.047,0), (-0.808,-0.015,-0.589,0),
  (-19.749,-5.244,-12.801,1)): row-vector world matrix, translation in the last row,
  forward = row 2 - copies of the camera, not the scene origin.
* Heading convention in the log: degrees from +Z towards +X (atan2(fwd.x, fwd.z)); Y is
  up. World y values are negative throughout this save (-5 to -52 here), so do not
  assume y=0 ground.

**Not answered:** the world origin and heading of the scene (the E_HOTELA spot). Next:
find the scene's placement transform (the engine's cinematic `Start`/setup, `sub_88F75E08` calls the selector) rather than camera copies. Also still
unseen: `game mode` and `CinematicFinished` lines (zero in this run), so those hooks are
unconfirmed or the game closed first.

## Location marks from the recomp (F2/F6 overlay, 2026-10-28, run `tdu2_190.log`)

Marks written with F6 (`tdu2_marks.txt`), camera position, Y up, heading from +Z towards +X.
Correlated with the `[scene]` log lines of the same run:

| # | time | position | heading | context |
|---|---|---|---|---|
| 1 | 19:03:25 | (-7.70, -5.63, -28.32) | 10.2 | Meeting Tess, **scene-local** frame (same box as the earlier shot cameras) |
| 2 | 19:05:37 | (127.84, -28.12, 652.75) | 318.7 | driving, 5 s before `solar_crown_ext` was selected: a **world** position outside the Solar Crown |
| 3 | 19:06:00 | (-0.41, -12.56, -9.08) | 63.7 | inside, `solar_crown_int` (selected 19:05:52), scene-local |
| 4 | 19:07:16 | (15.67, -12.19, -26.54) | 218.1 | garage (`EURO/BNK/INTERIOR/ISTOL_B.BNK` loaded 19:07:12), local |
| 5 | 19:07:23 | (-16.19, -7.24, 2.41) | 338.2 | garage, local |
| 6 | 19:08:34 | (9.54, -23.50, -1.27) | 88.9 | local-looking |
| 7 | 19:08:50 | (-25.01, -2.34, -48.22) | 81.3 | local-looking |

Reading: every cutscene or interior (Meeting Tess, Solar Crown inside, garage) is in a
small origin-centred frame (|x|,|z| < ~50), so those marks are **not world coordinates**.
Only #2 (and the free-roam cameras in earlier logs, roughly x -240..180, z 250..750 around
the Ibiza hotel) are world positions. To get the hotel's world position, press F6 while
the game view is live outside the hotel (before the white flash / after the cutscene), not
during a cutscene. `solar_crown_ext` and `_int` were selected via the same
`sub_88F74170` as `meeting_tess`.

## Marks from a new-game run (`tdu2_191.log`, 19:15-19:19)

| # | time | position | heading | what |
|---|---|---|---|---|
| 1 | 19:15:26 | (6.47, -37.74, -9.01) | 157.8 | party / Tess at the party (`new_intro`, loaded 19:14:32) |
| 2 | 19:15:42 | (-5.08, -38.75, 9.59) | 333.4 | party |
| 3 | 19:15:57 | (9.44, -6.90, -26.49) | 27.9 | room with the California (`GARAGE.BNK`, 19:15:56) |
| 4 | 19:16:56 | (23.30, -11.31, -25.02) | 199.1 | leaving the building with the California |
| 5 | 19:18:46 | (0.02, -105.02, 19.02) | 247.1 | the knock, 2 s before `meeting_tess` was selected (19:18:48) |
| 6-8 | 19:18:51-19:19:00 | (-19.69,-5.23,-12.89) h233.7; (-16.00,-5.07,-19.57) h212.5; (-14.82,-5.22,-20.36) h208.5 | | Meeting Tess shots |
| 9 | 19:19:49 | (19.79, -3.39, -16.37) | 117.1 | Meeting Tess shot |

Every one of these is in a small origin-centred frame (|x|,|z| < 50): the party, garage,
the knock (x = 0.02 here and in run 187, y about -93..-105) and Meeting Tess alike. None
is a world position, so the camera cannot give Meeting Tess's place in the Ibiza world.
Inference (not observed): the scene brings its own set (`.3dd/.3dg` sets in the bank) and
the engine swaps the world out at the start (the far one-frame camera jump), so the hotel
arcade in the dream is the cutscene's own geometry at the scene origin, not a spot in the
world. For OpenTDU2: build it as a local-frame set, shot cameras exactly as marked
(first shot (-19.75, -5.24, -12.80) heading -126.1 deg), rather than hunting a world spot.

**Correction from the owner (2026-10-28):** the dream is *not* a separate set. It plays in the
real Ibiza geometry, at the spot where the player was when the cutscene started. The
"brings its own set" inference above is withdrawn. The marks inside the dream are still in a
scene-local frame (origin-centred) because the camera is authored relative to the scene
origin; the scene origin's world transform has not been captured. Practical way to get the
world position: stand at that spot in the normal game view (before the knock, or after the
dream when control returns) and press F6; that mark is in world coordinates.

**Owner screenshots (2026-10-28):** the in-game map shows the player icon (white car) on the north-west coast of Ibiza, just west of the coastal town; the owner states this is where the party / dream / wake-up sequence takes place. The coordinate overlay in those shots reads the *map camera* ((13685.88, 10801.35, -59498.80) fully zoomed out, (1011.11, 113.37, -3776.11) zoomed in), which is a different space and not the place's world position. Not usable as a coordinate. A world mark (F6) taken at the spot in the normal game view is still needed.

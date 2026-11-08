# Back-on-track / Return-to-the-road UI — findings

## 2026-11-05 (`win`): SOLVED — the element is `/HUD/BOT`, not `/HUD/enter_hold`

The session-70 "remaining delta" below (the arrow pointing down, the road in the
badge) was the wrong element, not a runtime composition. `hud.vmf` has a
dedicated **`/HUD/BOT`** (char 821, "Back On Track", depth 1171 of `/HUD`
sprite 1240) parked at the **same** (1334.75, 588.4) as `/HUD/enter_hold`
(char 828, depth 1186) [DATA]. The two are twins — the same 30-frame
`default`/`open`/`stop`/`close` tween, the same `ic` labels (`default` f1,
`lock` f8, `on` f63), the same sky sweep `anim` (char 811, bitmap 808) — and
differ in:

| | `/HUD/BOT` (821) | `/HUD/enter_hold` (828) |
|---|---|---|
| `ic` | char 817 | char 824 |
| `ic` depth 8 art | shape 813 = **bitmap 812** (50x50): white ring, white DOWN arrow, road with lane dashes, transparent interior | shape 823 = bitmap 822: white ring over a half-alpha disc with an UP-arrow hole |
| label | #11 = sprite 819, EDITTEXT var `/:m_BackOnTrack_` (default `_Back on track`) | #11 = char 826, var `/:m_Enter` |
| `open` (f9) script | `if (_root.IsBOT == true) ic.gotoAndStop("on") else ic.gotoAndPlay("lock")` | — |
| `close` (f23) | `FSCommand:snds:backroad` | |

`ic` (817): f8 `lock` places ch816 (alpha 0.5, the red disc) at depth 1 and
ch114 (the red X, scale 1.15) at depth 9 and plays `FSCommand:sndp:wrong`;
f40 runs `_parent.gotoAndPlay("close")` — **a rejected press flashes the X for
~32 frames and the element closes itself**; f63 `on` removes the X and plays
`sndp:backroad`, and the movie's own `on` path is `gotoAndStop` (it stays).
f93..f120 blink depth 8 and f120 closes too (reached only when `on` is played).

Found by `-dumpflashart` (every bitmap of every frontend bank to PNG,
`FlashMovie::load`), an RMS match of all ~10k bitmaps against the owner's two
captures (812 first for the blue state), then the `-dumpbot` "who uses bitmap
N" walk (shapes filling it -> sprites placing them -> up to the root).

**The port** (`FreeRideHud::showBackOnTrack`) now drives `/HUD/BOT` with the
same code as before (stop, `ic` on/lock, the sweep from the hold progress),
sets `/:m_BackOnTrack_` = loc 50 and `/:IsBOT`, and two fixes: a show that
starts locked makes the `ic` goto (the state cache used to start on 'lock',
so `-botforce lock` drew the `on` art), and `lock` re-shows `ic` depth 1 (the
`on` state's hide outlived the goto: on -> lock drew the X on the white
badge). Evidence: `docs/images/rtr_bot_retail_vs_port.webp` (left the owner's
retail captures, right the port: passive `-offroadhud` f240, locked = an
on-road hold).

**When the red X shows (2026-11-05, the owner):** *"the red cross version
happens when you try return to the road and then try again straight away after
successfully returned to the road"* - that is how both captures were taken. So
the lock is a **cooldown after a successful reset**, not "pressed on the road"
(the s70 [INF] law, which also made a car rolled over ON a road unrecoverable).
The port now: a press works anywhere; a reset starts a 5 s cooldown
`kBackOnTrackCooldown` [INF: the retail timer is in the encrypted driver; the
exe's only other readable name is `PoliceParams.CanBackOnTrack`, the
police-chase ban - not implemented]; a press during it is refused for the whole
hold - ic `lock`, the movie closes the badge itself ~32 movie frames later.
Sequence `docs/images/rtr_bot_cooldown_sequence.webp` (hold sweep -> reset ->
refused press f372 / f420 -> after the cooldown the sweep again, and the reset
lands: `FREERIDE: back on track (0.3 m)`).

**The mods override is no longer needed**: a `<mods>/flash/fla_0822.png`
stand-in now changes only `enter_hold`, which the port no longer shows.

---

# Session 70 notes (kept for the history; the element choice below is superseded)


Status: **finishing parked** (2026-11-03, `glm`, session 70). The two retail
elements are decoded to the placement/action level (`-dumpbot`), the port now
drives the right element at the right position, and the remaining delta — the
exact composition/orientation of the badge art — is narrowed to one question
the planned x360-recomp texture dumper answers directly. Everything below is
read from the movie and the loc bank (`[DATA]`) unless marked `[INF]`.

## What the retail shows (the owner's captures, `docs/images/rtr_retail_capture_s70.webp`)

One circular badge at a **fixed top-centre position** (ring centre canvas
(640, 342) on 1280x720 — measured on the owner's 4K capture at 1/3), the label
"Return to the road" (loc 50) centred under it at (640, 395):

- the badge: a white ring; inside, a blue-teal sky gradient with a white
  horizon band, a diagonal road with white lane dashes, and a bold white
  arrow pointing **down onto the road**;
- state B: the same badge with a bold **red X** over it (the action
  unavailable — the ic 'lock' state plays the `FSCommand:sndp:wrong` cue).

## The two elements, decoded (`-dumpbot`)

### `/HUD/enter_hold` (char 828, 30 frames) — the captures' badge

```
timeline: f1 'default' (ch824, ch826, ch60, ch827)  f9 'open'  f17 'stop'  f23 'close'
  /ic        char 824  depth 1   147 frames   f1 'default'  f8 'lock'  f63 'on'
    /anim    char 811  depth 3   30 frames    f1 places ch807 (the MASK, clipdepth 4) + ch810
      /#2    char 810  depth 2   1 frame      /#1 char 809 -> bitmap 808 (96x59, the sky fill)
    /#8      char 823  depth 8   -> bitmap 822 (50x50, the arrow-in-circle art)
  /#11       char 826  depth 11  -> the label (EDITTEXT var '/:m_Enter')
  depth 13   char 60 (a placeholder bar, removed by 'open')
```

- **The states are the movie's own actions** (AVM1, f9 of the element): the
  'open' frame runs `ic.gotoAndStop("on")`; the f30 'close' tail runs
  `ic.gotoAndStop("default")`; ic's f8 'lock' plays `FSCommand:sndp:wrong`;
  ic's f40/f120 run `_parent.gotoAndPlay("close")` (the auto-close).
- **The fill sweep**: ic/anim's 30 frames move ch810 (the sky art) UP through
  the ring by 1.6 px/frame (f1 y -26.45 -> f30 y -73.45) — the mask (char 807,
  bounds (42.5,-55.5)-(87.2,-10.7), centre (64.85,-33.1) in anim space) wipes
  it. The nested mask WORKS in the port (the parked blocker is gone — the
  stencil push/pop carries into the nested clip).
- **The 'default' frame hides everything** (ic and the label authored at
  alpha 0); the port must drive the labels ('stop' on the element, 'on'/'lock'
  on ic) AND force the first ic goto (the state cache starts on 'lock' so the
  first show fires the goto).
- **The forward-goto leak**: gotoFrame's forward walk applies every
  intermediate frame's placements, so jumping ic 0 -> 62 leaves the 'lock'
  art (ch816 at depth 1) behind — hidden after the 'on' goto
  (`setDepthVisible(kIcPath, 1, false)`).
- **The fresh-sprite plays**: a newly placed sprite starts playing, so
  ic/anim ran its sweep on its own; the driver parks it
  (`gotoFrame(kAnimPath, frame)` + `stopSubtree`).
- **The layout**: at 'stop', ic rests at (-759.25, -72) of the element and the
  label at (-765.5, -75.7); the ring centre inside ic = the mask centre
  (64.85, -33.1) + anim (-0.4, 2.9) = (64.45, -30.2) — so the ring centre is
  (-694.8, -102.2) of the element origin. The movie parks the element at
  (26695, 11768) twips = (1334.75, 588.4) px, which puts the ring at stage
  x 639.95 — **the retail element shows at its parking spot, x exact**; the
  port nudges y by -24 px to the capture's (640, 462 stage)
  (`kElementY = 588.4 - 24`). The label needs NO re-anchor (the movie's own
  arrangement puts it under the ring).

### `/HUD/enter/Spot` (char 227, 751 frames) — the offroad TILE (a different thing!)

The Spot's **f1 'default' IS the offroad kind**: it places the offroad child
(char 4756) + the halo children (news/glow/target/hlight/link/rank/lock/
forbidden/favorite/done) + the shape child (ch4799, the blue diamond plate,
bitmap 4795 — shared with the 'dirt'/'classic' championship kinds). The
session-62 reading was right about the art: the offroad child's 'on' label =
bitmap **4754 (64x64, standalone `fla_4754.2db`)** — the dark circle with the
grass tuft and the white road strip (`docs/images/rtr_art_badge_4754_s70.webp`).
That tile is the grass/"hold A" presentation, NOT the captures' badge.

Other decoded arts (`-dumpbot` writes `_flashart_<id>.png` for the whole
4740..4830 cluster through the engine's own decoder —
`FlashMovie::getBitmapPixels` for the standalones, `getAtlasCellPixels` for
the page cells): 808 = the sky/horizon gradient, 822 = the grey arrow-in-ring
(the ic's arrow art), 4765 = the diagonal road-stripes circle (45x45, used by
Spot/lock AND Spot/forbidden #2), 4767/4770 = the red X pieces (33x33, 33x39),
4783 = the white diamond (hlight), 4795 = the blue diamond (shape plate).

## What the port does now (session 70)

- The off-road indicator (off the road network > 2 s, `-offroadhud` forces it)
  shows **/HUD/enter_hold** at the retail position: 'stop' + ic 'on', the fill
  parked full (progress 1.0), the label via /:m_Enter = loc 50
  (`docs/images/rtr_ring_passive_s70.webp`).
- The BACK_ON_TRACK hold sweeps the fill with the progress and resets the car
  on completion (`docs/images/rtr_ring_hold_s70.webp`); on-road the hold is
  **rejected**: the ring locks (ic 'lock', the sndp:wrong cue) and the
  indicator shows immediately (no 2 s delay) while the button is held
  (`bBackOnTrackRejected`).
- The Spot-offroad tile code stays (correct per the data, unused by this
  flow): `showEnterSpot("offroad")` would show the grass tile — Spot
  'default' + offroad 'on' + the forbidden halo for its red X, no 'asphalt'
  card (the card depths hidden), the halos hidden AFTER the kind goto (the
  goto re-places them visible).
- Test hooks: `-offroadhud` (force the indicator), `-botforce on|lock`
  (the ring up from frame 60 in the forced state), `-botframe <n>` (park the
  sweep at a frame), `-dumpbot` (the placement/action dumps + the art PNGs).

## The remaining delta (why this is parked)

The captures' badge interior — the blue sky filling the whole ring and the
arrow pointing DOWN onto the road — does not match any single authored state:
our ring renders the arrow UP with the fill partially visible at f30. The
arrow-down and the full-sky look are presumably composed/oriented by the
retail driver (a flip toward the road, or a later art swap the movie's frames
do not show). A port-side vertical flip of ic was tried and **breaks the
stencil mask** (the fill vanishes) — do not ship that.

**The next tool is the x360-recomp texture dumper the owner suggested**: hook
the recomp's texture creation and dump (a) the atlas pages as the retail GPU
sees them (settle any row-order doubt) and (b) the RUNTIME-COMPOSED badge —
whatever the retail driver finally binds for this element. With those PNGs the
port can either match the composition exactly or remake the icon.

**The remake route is live (session 70 follow-up)**: `<mods>/flash/fla_<id>.png`
replaces any flash bitmap — `FlashMovie::getBitmap` binds the override over
the standalone / atlas page sources (`findModOverride`, owned by the movie,
destroyed on unload; verified: `fla_0822.png` with the sky crop rendered in
the ring, the `Flash movie 'hud': bitmap 822 overridden` log line). The
owner's inspection confirms 808 (the sky) matches the retail interior while
822 (the grey up-arrow) does not match the retail arrow — so the stand-in
icon is a white ring + white down-arrow PNG (transparent interior, the sky
fill 808 shows through), dropped as `<mods>/flash/fla_0822.png`. The
override's LOGICAL size stays the retail desc's (`ref.Width/Height =
pDesc->Width/Height` in getBitmap) — the shape fills map their coords through
it, so a PNG of ANY resolution fills the shape full-frame (the owner's first
art is 1008x1040 and renders full-frame). The owner's first art (despilled
from a green-screen compose) renders in-game
(`docs/images/rtr_art_override_s70.webp` — the ring, the down arrow and the
road over the sky fill at the retail position). The x360 dumper remains the
tool to recover the TRUE retail arrow art instead of a remake.

## Loc strings (from `-dumploc`, the real retail bank)

| id | text |
|---|---|
| 50 | Return to the road |
| 73 | Return to the road |
| 2216 | Press and hold #command:BACK_ON_TRACK# to put the car back on the road |
| 7142 | [hold] Back on track |

## Verification commands

```bat
:: the passive ring (the offroadhud force)
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-s70 ^
    -gm FREERIDE -skipcine -nopad -fixedtimestep ^
    -autoplay "240:shot=C:/OpenTDU2-glm/_rtr_final.ppm" -exitframe 260 -offroadhud

:: the hold sweep + the reset
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-s70 ^
    -gm FREERIDE -skipcine -nopad -fixedtimestep ^
    -autoplay "240:hold=backontrack;250:shot=C:/OpenTDU2-glm/_rtr_hold.ppm" -exitframe 260

:: the state probes + the art dump
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-glm-s70 ^
    -gm FREERIDE -skipcine -nopad -fixedtimestep ^
    -autoplay "90:shot=C:/OpenTDU2-glm/_bot.ppm" -exitframe 110 ^
    -botforce on -botframe 28 -dumpbot
```

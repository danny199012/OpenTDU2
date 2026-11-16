# Tuner mod: plan (opt-in)

Status: **planned, nothing built.** Written 2026-11-16 from a planning chat with the owner.
This is a roadmap, not a spec. Tags as in `CLAUDE.md` section 4: `[INF]` is a choice or an
estimate; nothing here is `[CODE]` or `[DATA]` yet. Every file or system named below was seen in
the tree during that chat but not studied.

## Goal

Tuner-culture content (Midnight Club / NFS Underground feel) as an **opt-in mod**: off by default,
vanilla untouched. The owner's favourite cars are the R34 and GTR Skylines, but the feature is
**not limited to them**: it covers tuners in general (Subaru, Evo, Supra and the like).
The opt-in pattern already exists: `FuelModEnabled()` in `source/game/freeride/fuel.*`.

## Rules the owner set

- **Parts are made by us** (own models/samples). Where they come from is not settled; see open questions.
- **Customisations are saved per car** in the player profile (like `PlayerProfile::OwnedCarRims`).
- **Mod disabled = car locked, not lost.** Saved parts stay as data. The car can be viewed, with a
  notice ("Custom parts need the Tuner mod"), but not edited or driven with the parts applied.
  Re-enabling the mod restores everything. A toggle must never delete a player's work.
  - The save needs a version field. The loader must skip unknown part ids, not crash on them.

## Design

### 1. Per-car tuner profile [INF]
Data, not a hardcoded list: each car gets a profile (or a `tuner` category) in a mod data file
holding sound settings (pop rate, turbo flutter, flame size), exhaust position for flames, and
the customisation slots it supports. Adding a car later is a data row, not code.

### 2. Exhaust sound and flames
- Audio lives in `source/audio/vehicle_audio.cpp` (layered `.bnk` samples driven by RPM and load).
  A pop is a one-shot triggered when throttle lifts above a threshold RPM: random delay, 1 to 4 pops.
  Optional: turbo flutter / blow-off, rev-match blip on downshift.
- **Sample source is unknown.** Check what the R34 / GTR banks contain; otherwise supply our own.
- Flames: a small particle/sprite effect at the exhaust, timed to each pop. Needs an exhaust
  position per car (model node or an offset table).

### 3. Customisation
| Tier | Parts | Note |
|---|---|---|
| Easy (data/texture) | number plates, colours, vinyls | the sticker shop and the buy window's live rims preview are the pattern to follow |
| Medium (mesh swap) | spoilers, body kits, seats | alternate mesh on a named node; **we build the models** |

### 4. The custom garage: reuse the performance-upgrade shop
The existing performance shop model has a **lift** the car can sit on. Reuse it instead of
building a new interior: car placement on the lift, camera, menu, and a **map marker**.
- Name is undecided. Ideas: "Custom Shop", "Tuner Garage". It is only a localisation string.
- **First check** whether that shop is already an enterable interior or only a model; that
  decides whether this is a placement task or real work.

### 5. Races
- Race starter: a figure standing at the line doing the arms-up-to-down start gesture. Needs a
  model and an animation. Open: does the game's skeletal animation format let us reuse or author it?
- Most race data already exists, so the starter is a visual trigger on existing race definitions.
- Later: random events at spots such as the airport (location plus spawn rules).

## Order of work

1. Mod switch + per-car tuner profile
2. Exhaust pops and flames
3. Plates and colours, with the locked-when-disabled behaviour built in from the start
4. Performance shop as the custom garage (lift, map marker)
5. Spoilers, then body kits, then seats (our own models)
6. Race starter animation, then events (airport etc.)

Each step is useful alone; stopping after any of them leaves something working.

## Open questions

- Which R34 / GTR variants and which other tuner cars go in the first profile list?
- Pops on lift-off only, or also on downshifts?
- Is the performance shop an enterable interior or a model only?
- Do the game's skeletal animations allow a custom starter gesture?
- Part models: all ours, or anything usable in the game files?
- Final garage name.

## Reference

- Scope inspiration: TDU Solar Crown's tuner update and Initial D collab. A scope reference, not a target.
- The Apr 2012 dev build (`E:\Documents\TDU2+Dev+Build`, see memory index) may hold audio code or
  data worth checking. Nothing read yet.

## Status (session 42, 2026-11-16)

- **Shipped** (opt-in, `resources/mods/packages/tuner-garage`): mod switch (`-tunermod` / `-notunermod`), part catalog `tuner/tuner.txt` (3 spoilers, 7 cars), shared OBJ loader (`source/game/spots/obj_mesh.cpp`), engine hook `CinematicCar::setTunerAttachment`, garage = the reused tuning shop on the lift (spots ETNR_A, Ibiza + Hawaii), menu, buy + `car_tuner_parts` save. Order of work steps 1, 4 and the first part of 5 are done.
- **Roster [DATA]:** retail TDU2 has no R34 / Evo / Supra / RX-7 / S15 / NSX; the catalog uses the cars that exist (GTR, 370Z, 350Z_CPE, Impreza WRX STI 2007, RX8, Delta Integrale Evo 2, Escort RS Cosworth).
- **Render bug fixed:** OBJ-route materials drew flat white because `MaterialDesc::Spec` was all zero, which selects the no-texture TD_Gouraud permutation. Setting `Spec[0]=1` (S_NBTEXTURES) and `Spec[1]=1` (S_COORDTEXTURE), as the rain materials do, selects the textured one. Evidence: `docs/images/tuner_wing_gt_wear.png`.

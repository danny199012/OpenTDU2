# The physics test area (`testarea`)

A composed exterior bank for straight physics testing: cars on different
surfaces, flats and hills, so handling can be tuned against measurable
ground. Built with the scene editor's `export_bank` (the M5-last composed
`.3dd` bank build, docs/re/scene_editor.md 8) — no retail asset is touched.

## What is in the bank

| piece | material | tyre surface | notes |
|---|---|---|---|
| 140 x 140 m pad | `pad` | tarmac (mu 1.0) | skidpad ring (r ~41 m), cross lines, a start box; the texture is the UV |
| hill strip, west of the pad | `tarmac` | tarmac | 2 % / 5 % / 10 % / 20 % / 30 % ramps (20 m wide), a plateau, a -20 % descent |
| dirt strip | `dirt` | dirt (mu 0.6) | 12 x 90 m |

The tyre surface comes from the material's name (`dirt` / `grass` in it, else
tarmac) through the drive-triangle collector
(`SpotExterior::collectDriveTriangles`): the authored bank carries no `.shk`,
its kept model triangles ARE the drive collision, each with the explicit
surface override. Grass is free: the terrain around the pad is the grass
surface. Reference frictions (the `Hit::Friction` the assists / HUD read):
tarmac 1.0, dirt 0.6, grass 0.4 — the tyre grip itself is the car DB's
Pacejka `Config[surface]` per index (0 tarmac / 1 dirt / 2 grass).

NOTE (the OBJ mirror): the OBJ route mirrors X at load (obj_mesh.cpp), so a
feature authored east of the origin lands west of the bank anchor and the
other way round. The hill strip is authored +x, lands west; the dirt strip is
authored -x, lands east.

## Install / rebuild

1. Copy `testarea.obj/.mtl/testarea_*.png` (and `m5box.*` if wanted) into
   `<userdata>/mods/props/`.
2. In the scene editor (headless works, `-editorscript`):

```json
[
  { "cmd": "new_scene", "name": "testarea" },
  { "cmd": "add", "kind": "prop", "ref": "testarea", "pos": [12700, 60, 16900], "yaw": 0 },
  { "cmd": "snap_ground" },
  { "cmd": "save_scene" },
  { "cmd": "export_bank", "name": "testarea" }
]
```

This writes `<mods>/Euro/Bnk/islands/ibiza/exterior/testarea.bnk` and the
anchor's `spots.bin` record at (12700, ground, 16900). The anchor y wants to
be the ground height (`snap_ground` gives it); the pad top is +1.2 m over the
anchor so it clears the local terrain (the slab is a 3 m plinth).

3. FREERIDE:

```bat
build-glm\bin\opentdu2-glm.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata <userdata> ^
    -gm FREERIDE -skipcine -nopad -clocktime 12:00 ^
    -tp "12700,30,16900" -heading 0 -logsurface ^
    -autoplay "150:hold=accel" -exitframe 600
```

`-logsurface` prints the per-wheel `SURFACE pos ... wheels s/c/mu` line
(surface index / contact / reference friction x 100) every 30 frames — the
A/B trace for handling changes. A spawn that lands inside the slab (the fit
probe ran before the bank streamed) is popped on top once
("test area refit" in the log); the pad's footprint also joins the
grass-suppression set on load.

## Tuning workflow

- Flat tarmac: launch / braking / skidpad-circle steady state
  (`-logcorner` pairs with it).
- Hills: climb the 2..30 % ramps for grade / load-transfer tests, descend the
  -20 % for brake tests.
- Surfaces: drive off the pad's west edge onto the hill strip approaches, or
  east onto the dirt strip (surface 1); anywhere off the slabs is the
  terrain grass (surface 2).

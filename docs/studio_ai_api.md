# The scene editor's AI / headless interface

The scene editor (`-gm SCENE_EDITOR`, `source/game/scene_editor/`) is drivable
without touching the mouse. One JSON command object in, one JSON result line
out. Two transports share the same interpreter (`scene_cmd.cpp`):

- **Batch** — `-editorscript <file>`: a JSON array of commands, executed once
  the island loads. Combine with `-screenshot <path> -screenshotframe <n>
  -exitframe <n>` for an unattended run (the screenshot carries the build
  stamp). The script path may be an overlay path (`scenes/...`) or a plain
  file anywhere on disk.
- **Live** — the editor starts a JSON-lines TCP server on `127.0.0.1:9377`
  (`-editorport <n>` moves it, `-noserver` turns it off). One client at a
  time; commands run on the editor thread between frames; every response is
  one line ending in `\n`.

Both log their results: the batch prints `SceneEditor: script cmd -> {...}`
per command, so the log alone is a transcript.

## Conventions

- `id` defaults to the current selection when omitted.
- `pos` is a world-space `[x, y, z]` array (metres; the island's coordinate
  system — the same numbers the log and `-campos` print).
- Yaw / pitch / roll are **degrees**; yaw is the heading about Y.
- Without `pos`, `add` places 8 m ahead of the camera, snapped to the ground
  surface.
- Scene files live at `<mods>/scenes/<name>.scene.json` (the mods overlay;
  the game data folder is never written).

## Commands

| command | fields | effect |
|---|---|---|
| `ping` | | `{"ok":true,"note":"pong"}` |
| `status` | | island, world ready, scene name, counts, selection, camera |
| `new_scene` | `name?` | clears the document |
| `load_scene` | `file` | loads `<mods>/scenes/<file>` through the overlay |
| `save_scene` | `file?` | writes the document (default `<name>.scene.json`) |
| `list_scenes` | | the scene files of the overlay |
| `set_island` | `name` | ibiza / hawai / casino / eden / hongkong (streams next tick) |
| `list_assets` | `kind?`, `filter?` | the browsable assets: `library` (commonworld nodes), `actor` (worldactors entities), `spot` (building banks), `prop` (`<mods>/props/*.obj`) |
| `add` / `place` | `kind`, `ref`, `pos?`, `yaw?` | places an asset; returns its `id` and selects it |
| `remove` | `id?` | deletes the object |
| `duplicate` | `id?` | copies it 2 m along +X |
| `select` | `id` | sets the selection ("" clears) |
| `get_object` | `id?` | the object's full record |
| `list_objects` | | every object of the document |
| `set_transform` / `move` | `id?`, `pos?`, `yaw?`, `pitch?`, `roll?`, `scale?` | edits the transform (`scale` is a number or `[x,y,z]`) |
| `set_visible` | `id?`, `visible?` | toggles the object |
| `attach` | `id?`, `parent` | parents to a group object (the transform composes) |
| `detach` | `id?` | clears the parent |
| `snap_ground` | `id?` | puts the object on the highest surface below it |
| `teleport` / `camera` | `pos?`, `yaw?`, `pitch?` | moves the camera (angles in degrees) |
| `look_at` | `pos` | aims the camera at a world point |
| `hide_node` | `target` | hides a baked scene node everywhere (name, or `hc:<16 hex>`); recorded as a document override |
| `hide_material` | `target` | same for a material |
| `clear_overrides` | | removes every override |
| `list_overrides` | | the current overrides |
| `screenshot` | `path` | saves the next presented frame (build stamp burnt in) |
| `commit_props` | | writes the prop placements to `<mods>/props.txt` (what the current game build draws outside the editor) |
| `export_spots` | | M5: appends the scene's spot / interior placements as real `spots.bin` records through the mods overlay (the `add_spot.py` route in C++; re-exports upsert — moving a building and re-exporting rewrites its record) and copies the referenced interior banks to the overlay's `exterior/`. FREERIDE then streams the scene with no engine change |
| `export_pmi` | | M5: appends the scene's library placements as `.pmi` records (type 1, the sector's `-o` list) and actor placements (type 2, `-a`) to the landing sectors' banks, repacked into the overlay (re-exports stack; same-model placements within 0.5 m are skipped). FREERIDE draws the instances with no engine change |
| `export_bank` | `name?` | M5 last: compiles the scene's prop placements into ONE new exterior bank `<mods>/Euro/Bnk/islands/<island>/exterior/<name>.bnk` (authored .3dd/.3dg/.2dm/.2db + entities.xmb/hashcodes.xmb) and upserts the anchor's `spots.bin` record (the anchor is a `spot` object named <name>, else the first prop). FREERIDE streams the bank through the retail path; see docs/re/scene_editor.md 8b |
| `move_axis` | `axis` (x / y / z), `dx`, `dy` | M6: the gizmo's axis drag, AI-drivable — the same screen-delta math the mouse drag uses, synthesized from the screen centre; returns the new `pos` (one undo entry per call) |
| `set_material_color` | `target`, `color` = [r, g, b] | M6: a material color override (the Diffuse.rgb replaced everywhere; the same targets as the hides) — recorded as a `color` override in the document |
| `enter_walk` | `id?`, `pos?`, `yaw?` | M4 first person: walks an `interior` object on its bank's own `.shk` collision from the retail `FirstPerson` camera (`id` default: the selection, else the nearest placement; **none → the scene grounds**, terrain / road floors, no walls). `pos` is a bank-local start eye `[x, y, z]`, `yaw` a bank-local heading in degrees. Returns the walk note + the world `eye` |
| `exit_walk` | | ends the walk; the fly camera parks at the walk eye |

Kinds for `add`: `group` (an attach parent, draws nothing), `library` (a node
of the island's commonworld library — what the `.pmi` V/O lists instance),
`actor` (a `worldactors.bnk` entity by name), `spot` (a whole building bank
from `islands/<island>/exterior/`, streamed like the retail spots),
`interior` (a walkable room bank — M4: the stem `icd_ferr`, a shop spot ref
`ECD_FERR` → its interior, or a house ref `EPH_L2E_0345I` with `:garage` for
the garage room; streamed like the spots, walkable), `prop` (an OBJ from
`<mods>/props/`). `list_assets` with `kind:"interior"` lists the game data's
`interior/*.bnk` stems.

## A session, verbatim

```
$ python - <<'EOF'
import socket, json
s = socket.create_connection(("127.0.0.1", 9377), timeout=10)
def send(cmd):
    s.sendall((json.dumps(cmd) + "\n").encode())
    return s.recv(65536).decode().strip()
print(send({"cmd":"status"}))
print(send({"cmd":"add","kind":"spot","ref":"EAS_A","pos":[12640,10.5,16720],"yaw":90}))
print(send({"cmd":"look_at","pos":[12640,12,16720]}))
print(send({"cmd":"screenshot","path":"_ed_server_shot.png"}))
EOF
{"ok":true,"island":"ibiza","world_ready":true,...}
{"ok":true,"note":"EAS_A","id":"EAS_A"}
{"ok":true}
{"ok":true,"note":"requested (saved on the next present)"}
```

Verified on build 384 (8dda3a9+dirty): the placed `EAS_A` streamed
(`SpotExteriors: 1 scene spots appended`), the screenshot landed, and the
saved scene reloaded (`load_scene` + `spot_exteriors` re-append).

## The document (what `save_scene` writes)

`docs/re/scene_editor.md` is the format reference (objects, overrides,
roads). The editor round-trips it: `load_scene` restores everything, and the
file is plain JSON a script may edit directly.

# PS3 build mining — overview and how to resume

The decrypted PS3 build of TDU2 (`C:\TDU2-PS3-Recomp\game\EBOOT.ELF`, PPC64
big-endian, no SecuROM) is the readable counterpart of the PC executable's
encrypted 2.8 MiB band. It carries the same Eden engine code (`edVehicle2`,
the pause/Flash wiring, the Havok vehicle SDK) compiled for PS3, and it is the
source for everything in this folder.

Nothing here bypasses SecuROM on the PC build: the PS3 ELF was decrypted from
a legally owned disc image by the TDU2-PS3-Recomp project (see its
`NOTES.md`), and OpenTDU2 only *reads* it, the same policy as reading the
readable half of `TestDrive2.exe`.

The data dumps this folder refers to (`classes.txt`, `flash_globals.txt`,
`menu_strings.txt`, `physics_names.txt`, `pc_ps3_anchors.json`,
`ps3_enc_twins.json`) are kept locally for the RE work and are **not committed**
(see the root `NOTICE`, section 3); only the project's own `.md` documents are.

## What the PS3 build contains (verified 2026-10-28)

| Item | Count / value | File here |
|---|---|---|
| Flash `/:` variables (retail native↔SWF contract) | 1,710 | `flash_globals.txt` |
| Vehicle / physics parameter names (car setup fields, tuning) | 851 | `physics_names.txt` |
| Menu / screen / UI strings | 642 | `menu_strings.txt` |
| Havok class catalog (RTTI names incl. the full `hkpVehicle*` family) | 312 | `classes.txt` |
| Lifted C++ for **every** function (53,119 functions, 1.8 GB) | — | `C:\TDU2-PS3-Recomp\port\recompiled\` |
| Function table (OPD-derived, precise bounds) | 19,842 entries | `C:\TDU2-PS3-Recomp\port\out\EBOOT.functions.json` |
| Full string dump with virtual addresses | 68,589 | `C:\TDU2-PS3-Recomp\port\out\strings.json` |

OpenTDU2 currently wires ~60 of the 1,710 `/:` globals (grep
`"/:` over `source/`). The catalog is the checklist for the pause menu, HUD
and screens.

## Tooling (in `C:\TDU2-PS3-Recomp\tools\`, Python 3.14)

| Tool | Purpose |
|---|---|
| `func_index.py` | index of every lifted function definition (`port/out/func_index.json`) |
| `get_func.py <addr>` | print the lifted C++ body of a function |
| `elf_strings.py` | rebuild `strings.json` (address → string) |
| `xref.py <addr>` | find functions materializing a 32-bit address (addis/addi pairs) |
| `mine_catalogs.py` | regenerate the catalogs in this folder from `strings.json` |
| `rtti_scan.py`, `vtable_scan.py`, `addr_refs.py`, `disasm_refs.py` | helpers used while mapping the binary |
| `abs_refs.py` | **session 2** — single-pass index of every address materialization (absolute + TOC-relative) in the whole text segment -> `port/out/abs_refs.json` (20,871 addresses). Replaces the per-query rescan; `--rebuild`, `--near LO SPAN` |
| `find_const.py` | **session 2** — follows one level of pointer indirection for a data address |
| `find_floats.py` | **session 2** — locates the named float constants (1e-5, 1/6, 19000.0, −9.81, ...) in the data segments; step 1 of the constant method in `constant_pools.md` |

Requires the index built once: `python tools/func_index.py` (after any
re-lift). Capstone (`pip install capstone`) for the disassembly helpers.

## Key facts about the binary (learned the hard way, verified)

- **Pointers in the data sections are 32-bit big-endian** (cell PPU ILP32
  style), even though the ELF header says PPC64. 8-byte pointer searches find
  nothing; vtable slots and typeinfo name pointers are 4-byte.
- Code materializes addresses as `addis rT, r0, HI` + `addi/ori rT, rT, LO`
  (absolute form). TOC-relative addressing is rare. `xref.py` implements the
  working pattern.
- **Eden reflection is serializer-based**: every class has a `write(name,
  value)`-style serializer that materializes each member name string. The
  string pool is deduplicated and shared between classes, and a serializer
  materializes the *first* name of a pool run and offsets the rest, so only
  some names have direct xrefs. Sweep the pool (see `physics_layouts.md`) to
  find them all.
- 76 functions materialize names from the vehicle pool `0xFB0000-0xFBFFFF`;
  they are the vehicle class serializers (GearBox `0xCFEE4`, vehicle
  `0xD0544`, engine `0xD25E0`, CarControl FWD `0xCBCC8`/RWD `0xCBC00`/4WD
  `0xCBD90`, drive train `0xD103C`, ...).
- Havok's built-in reflection (class/member tables, PPU32 pointers) is
  present near `0x10d49ac` (`hkpVehicleAerodynamics` et al.) and includes
  member names with types — a second source for struct layouts.

## Next steps (resume points — revised 2026-10-28, session 2)

> Session 2 corrected two things below. Read `constant_pools.md` first: it maps
> the constant pools, records the `.rodata`-is-in-the-first-segment trap, and
> shows that **the functions which actually read the float constants are at
> `0x15xxxx–0x17xxxx`, not in the `0xDxxxx` serializer neighbourhood**. The
> `0xDxxxx` functions are real but they are *serializers* (name-emitting
> reflection), which is why they looked like the physics home.

1. **Locate the vehicle update functions (do this first).** The `[ENC]` numeric
   laws need the code that *reads* `1e-5` / `1/6` / `19000.0`, and Result 3 of
   `constant_pools.md` shows the scalar pool is reached as `base + offset`, so a
   constant xref cannot find it directly. Two ways in:
   - **read the two candidates the session-2 test produced** (do this first, it
     is cheapest): `0x155c20` — one function that contains the whole
     `FUN_00546xxx` engine / gearbox predicate cluster — and `0x15e678` — one
     function that contains both aids tests `FUN_0054eaa0` / `FUN_0054eae0` and
     references three `.rodata` constants. Confirm each by *shape* against
     `vehicle_physics_spec.md` §6.3–§6.6, not by address.
   - find the **pool base register** (dump materializations in
     `0xE00000–0xF20000`; look for a base whose `+offset` lands in
     `0xf11f00–0xf1214c`).

   The **link-order hypothesis is dead** — it was tested this session and no
   single offset fits (deltas +0x230 to +0x2750, no exact function start). Do not
   spend time on it.
2. **Physics formulas** (the `[ENC]` items of `docs/re/vehicle_physics_spec.md`
   §6.8): the engine / gearbox / clutch state machine members are confirmed
   (`State`, `ElapsedTime`, `StartingTime`, `StallingTime`, `LimiterTime`,
   `PedalPosition`, `Position`, `IdlePosition`, ... — see `physics_layouts.md`).
   Read the update functions with `get_func.py`, port the state machines + the
   Pacejka usage into `source/physics/`. The candidate functions are in
   `constant_pools.md` Result 2.
3. **Flash wiring**: walk `flash_globals.txt` against the pause menu / HUD
   movies in the game banks and wire the missing retail variables in
   `source/flash/` + `source/game/pause_menu.cpp`.
4. The zlib-table false leads in `vtables.json` (2,720 runs) are noise; a
   proper vtable pass needs a stricter OPD-pointer filter.


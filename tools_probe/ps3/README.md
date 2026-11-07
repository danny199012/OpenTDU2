# PS3 tooling — versioned copies

`C:\TDU2-PS3-Recomp` is **not a git repository**, so the tools built for the PS3
mining route have no backup there. These are copies, kept so the plan in
`docs/re/CROSSBUILD_PLAN.md` is self-contained and the work survives.

The originals live in `C:\TDU2-PS3-Recomp\tools\` and **must stay there**: they
resolve `game\EBOOT.ELF` and `port\out\` relative to their own parent directory,
so a copy will not run from this folder. If you change one, change it in
`C:\TDU2-PS3-Recomp\tools\` and re-copy.

| File | What |
|---|---|
| `abs_refs.py` | single-pass index of every address materialization (absolute + TOC-relative) in the PS3 text segment → `port/out/abs_refs.json` (20,871 addresses). `--rebuild`, `--near LO SPAN`. **This is the tool that makes the [ENC] hunt possible** — do not use `xref_addr.py` (it rescans 1.8 GB per query and times out) or `addr_refs.py` (wrong addressing form). |
| `find_const.py` | follows one level of pointer indirection for a data address. |
| `find_floats.py` | locates the named float constants (1e-5, 1/6, 19000.0, −9.81, ...) in the data segments → `port/out/float_consts.json`. |

Documentation for the whole route: `docs/re/ps3/README.md`,
`docs/re/ps3/constant_pools.md`, `docs/re/ps3/physics_layouts.md`.

Not copied (they are already in `C:\TDU2-PS3-Recomp\tools\` and are
project-specific): `func_index.py`, `get_func.py`, `elf_strings.py`, `xref.py`,
`rtti_scan.py`, `vtable_scan.py`, `mine_catalogs.py`, `disasm_refs.py`,
`addr_refs.py`.

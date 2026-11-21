# 360 recomp texture log / dump (2026-11-21)

Purpose: find out whether a 360 texture is one image or several put together (sprite sheet / atlas), and which assets the school tests load.

- The recomp's Xenos texture cache has `--texture_log true` (one `[tex]` line per loaded texture: size, dimension, format, mips, guest address) and
  `--texture_dump_dir <dir>` (raw guest bytes of every distinct texture + `textures.csv`). SDK patch 0013 in the recomp tree
  (`patches/rexglue-sdk/0013-...`), rebuilt plugin staged in `out/build/win-amd64-perf`, usage in the recomp's `TEXTURE_DUMP.md`.
- `tools/x360/tex_convert.py report <dir>` summarises a dump (cube / array / 3D counts, large 2D textures = atlas candidates);
  `convert <dir> <out>` writes PNGs (untile + endian swap + DXT decode). The tile and DXT code is unit-checked (round trip) but not yet run on a real dump.
- Not run yet: needs the owner to play the recomp to the school / a test / the HUD with the flags above.

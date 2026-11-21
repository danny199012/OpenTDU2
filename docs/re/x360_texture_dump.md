# 360 recomp texture log / dump (2026-11-21)

Purpose: find out whether a 360 texture is one image or several put together (sprite sheet / atlas), and which assets the school tests load.

- The recomp's Xenos texture cache has `--texture_log true` (one `[tex]` line per loaded texture: size, dimension, format, mips, guest address) and
  `--texture_dump_dir <dir>` (raw guest bytes of every distinct texture + `textures.csv`). SDK patch 0013 in the recomp tree
  (`patches/rexglue-sdk/0013-...`), rebuilt plugin staged in `out/build/win-amd64-perf`, usage in the recomp's `TEXTURE_DUMP.md`.
- `tools/x360/tex_convert.py report <dir>` summarises a dump (cube / array / 3D counts, large 2D textures = atlas candidates);
  `convert <dir> <out>` writes PNGs (untile + endian swap + DXT decode). The tile and DXT code is unit-checked (round trip) but not yet run on a real dump.
- Not run yet: needs the owner to play the recomp to the school / a test / the HUD with the flags above.

## First dump (2026-11-21, school + free ride, 1243 distinct textures, 202 MB) - the answer
`tex_convert.py` worked on the first real data (untile + 8-in-32 swap + DXT1 / DXT3 / DXT5 decode all correct). **The 360 game packs its art into atlases:**
- **UI / HUD / Flash art**: 2048x2048 and 2048x1024 `DXT4_5` sheets with a single mip hold every icon, flag, licence card, button glyph, controller picture, logo and photo of the frontend
  (`docs/images/x360_textures_ui_atlases.webp`) - one texture, hundreds of sprites. The Flash movies' bitmap characters are cells of these sheets (the port's `-dumpflashart` standalone / atlas cells).
- **Road surfaces** (2048x1024 `DXT1` + a matching `DXT4_5` normal map with 9 mips): one texture holds the asphalt, the kerb, the white / red and yellow edge strips side by side
  (`x360_textures_road_grass_atlases.webp`).
- **Vegetation** (2048x128 `DXT4_5`): nine grass / flower strips side by side, and tree-leaf strips; normal maps are DXT4/5 with the green+alpha packing (orange when viewed raw).
- 1227 of the 1243 are 2D, 14 cube, 2 3D; 162 are 8888, 722 DXT1, 273 DXT4_5. Dynamic cubes / screen-sized textures are the render-to-texture re-uploads the dump caps.

## Second dump (`test1`, a school test run, 1357 textures, 330 not in the first) - the school's own art
Read with `tex_convert.py convert <dump> <out> --min 64 --only <list of new files>`. New in the test run: sky domes, the avatars' body maps, the building-facade atlases, tyre / rim maps, the garage-menu icons,
birds, grass, and **the car decal atlases**: a 512x512 `DXT4_5` sheet (8 mips) holds the Classic Racing School livery - cream / maroon stripes, the "07" roundel, the winged "Classic Racing School" logo
(`docs/images/x360_school_livery_decal_atlas.webp`) - and other cars' decal sheets (the Delta's "HF integrale" / Lancia badge, headlight / tail-light atlases). **This is the first real source of the school liveries** that
12.4 could not find in the PC data (the PC has no RS cars 500-502). Not applied: the decals need the school cars' UV layout (the 360 car meshes, not in the PC files); the sheets are saved to look at and to place on the
Delta / Esprit / Mustang models once their UVs are mapped. The decal sheet is the "stickers" layer of the car skin, one atlas for all the livery pieces.

## Third dump (`test2`, all six school tests played through, 3907 textures, 1.3 GB)
- Most of the 2683 textures new to this run are render-time data, not art: 1024x3 / 1152x3 `32_32_32_32_FLOAT` (skinning / instance matrices, 1160), 768x768 `32_FLOAT` (shadow depth), 256x256 `8888` (render targets). 342 are new DXT art:
  island **terrain tiles** (satellite colour + pink-looking normal / mask maps, 1024x1024), shop signs, facade atlases, UI cells (`docs/images/x360_test_textures_sample.webp`).
- **The three school cars' detail atlases are all there** (`x360_car_detail_and_livery_atlases.webp`): Delta (HF integrale + Lancia badge, grille, tail lights), Esprit (the "esprit" script + Lotus badge) and Mustang (the pony badges, "Mustang" script, grille).
  But **only one racing-school livery sheet was captured: the "07 / Classic Racing School" cream-and-maroon decal atlas (512x512, DXT4_5)**. The roundels "70" (Esprit) and "3" (Mustang) and the Esprit / Mustang stripes seen in the clips
  are not in any 512x512 `DXT4_5` of the three dumps - they may be other sizes / formats, or composed from this sheet and the car colours; not found yet.
- **Video Replay** (owner, 2026-11-21): the replay plays the whole run back, with a control bar (previous, rewind, pause, fast-forward, eject; `LB` / `RB` + "Auto. Selection" camera cycling; elapsed / total time; gear and speed) and a very large set of cameras
  (passenger side, wheel, back seat, cockpit ...). The test `.rly` files (12.11) use the same replay format; their second section (35 920 bytes after the recording data, it starts `3, 561, 3, 2` then a position) is the lead for the replay cameras.

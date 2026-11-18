# Mod sticker packs and full-car vinyl packs

Written 2026-11-18 (`main`). Tags as in `CLAUDE.md`. Everything here is `[INF]`: a port feature, not a retail one.

## Layout
    <mods>/stickers/<id>/pack.ini
    <mods>/stickers/<id>/*.png        (sorted by file name = sticker order)

`<mods>` is `gpFile->getModsRoot()` (`<userdata>/mods`, or `-mods` / env). `-nomodstickers` turns the scan off.

## pack.ini (`key = value`, `#` / `;` comments)
| key | meaning |
|---|---|
| `name` | shop title (default: the folder name) |
| `price` | price of the pack (the shop's existing per-pack charge) |
| `mono` | 1 = the art is recoloured by the tint (monochrome pack) |
| `vinyl` | 1 = full-car livery: each PNG is a whole-canvas texture authored to one car's UV0 layout |
| `cars` | comma list of `DBCarPhysicsData._id_car`; the pack is only offered for those cars (empty = all) |

## How it plugs in
- `StickerBankCatalog::scanModPacks` appends one `StickerBankInfo` per pack after the 26 retail rows (`BankFile = mod_<id>`, type 1, valid, `bMod`). The shop UI, the native paint menu, ownership (`OwnedStickerBanks`) and pricing already iterate the catalog, so they see the pack with no UI change.
- `openModBank` loads the PNGs with `LoadImageFileRGBA` into textures the catalog owns (`createFromRGBA` has no owner) and destroys them in `shutdown()`.
- Vinyl: `StickerShopUi::beginEdit` places a vinyl entry at Scale 1 with `YStretch = W/H` so it covers the 1024 canvas. `StickerShopUi::buildCategories` hides packs whose `cars` list excludes the car (`bankFitsCar`).
- A mod pack is stored in a layout as `Bank = "mod_<id>"`, `Index` = file order. **Adding or renaming PNGs in a pack changes indices**: append new files with a later sort name.

## Verified
`-stickertest mod_<id>,0` with a 1024² translucent-blue + yellow-stripe vinyl: `docs/images/sticker_mod_vinyl_test.webp` (California: tint and stripe across bonnet/rear deck).

## Verified through the retail shop UI (2026-11-18)
`-interior ESTICK_B -walkaim StickersShop_area -nointeriorintro -autoplay "200:press=validate;340:press=validate;520:press=right;560:press=validate;720:press=backview;...;1100:press=validate"` (`backview` = previous pack, wraps to the last): the grid lists 17 packs, the mod packs are 16 / 17 ("Test Livery" $2,000, "Test Pack" $500) with their PNGs as thumbnails (`docs/images/sticker_mod_pack_grid.webp`); A on the livery logs `editing 'mod_testlivery' #0` and the car shows the stripe full-length in the editor (`docs/images/sticker_mod_vinyl_editor.webp`). Buying / saving a mod layout and respawning with it were not run.

## Not done / next
- Packs with fewer than 24 images show the movie's grey placeholder squares for the empty slots (cosmetic).
- Vinyl alpha is composited like a sticker; there is no UV-layout template export yet (needed by the creation tool; `car_obj_export` is the starting point).
- Vinyl packs are not yet bundled as a single archive with a manifest/hash (marketplace step).
- Only the first 24 entries of a pack show (`kGridSquares`, the movie's 4 x 6 grid).

## Buy / save / respawn (2026-11-18)
Same scripted run, then `...;1060:press=validate (A on the livery);1200:press=validate (apply);1300:press=back;1400:press=back;1520-1580:press=right x4;1600:press=validate (Valid & Pay);1800:press=validate (yes)`: log `paid 3000 (1 sticker(s) on car 387)` = the $2,000 pack + $1,000 labour; `profile.sav` carries `car_stickers = 387:mod_testlivery;0;0.5;0.5;1;0;...` and `sticker_banks = mod_testlivery`; a fresh `-gm FREERIDE` logs `'California' wears 1 sticker(s)` and the stripe is on the car. **A profile that owns a mod pack keeps the layout line even if the pack folder is later removed; the placement then draws nothing (art null) until the pack is back.**

## UV template tool (`tools/mod/vinyl_template.py`)
`opentdu2.exe -gm assetviewer -exportcar <FileName>` writes `<userdata>/export/<FileName>/<FileName>_ext.obj`; then

    python tools/mod/vinyl_template.py <..._ext.obj> --id <_id_car> --out <mods>/stickers

writes `<car>_vinyl/pack.ini` (`vinyl=1`, `cars=<id>`) and `<car>_vinyl_template.png` (1024², next to the pack folder, not in it: every PNG in a pack is an entry). The template draws only the sticker-slot materials (`Paint`, `Paint_SK`) as a filled coverage with the island outlines and a 128 px grid. **The sticker canvas is sampled with the SECOND texture-coordinate set (D3DDECLUSAGE_TEXCOORD index 1), not UV0** [DATA, verified 2026-11-18]: `ExportCarObj` writes it as `#vt1 u v` comment lines after each primitive's `vt` block (same vertex order; readers skip comments), the tool reads it (`--uvset 1`, the default; `--uvset 0` = the old UV0 drawing). Pixel (x, y) = UV1 (x / 1024, y / 1024), V down. All 12,945 `Paint` triangles of the California carry set 1.

Proof (California, `docs/images/vinyl_template_california_uv1.webp`, `vinyl_marker_test_uv1.webp`): markers painted on the UV1 template at the island holding the bonnet top (red at 276,408) and the rear deck (green at 807,410) land exactly on the bonnet and the rear deck in the shop editor. The same test on UV0 failed (blue on the right sill's UV0 region showed on the bonnet; red/green missing): so `stickers_spec.md` section 1's "sampled with the body's UV0 [EXP]" was wrong - it is UV1. The car's `_id_car` is the number in the shop log (`StickerShopUi: car 387 bound`).
[INF] UV1 is the unique (non-tiled) unwrap the car's detail layers use; the 3D `forward` of an exported OBJ is +z (bonnet z ~ +1.5).

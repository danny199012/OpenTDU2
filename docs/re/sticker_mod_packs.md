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

writes `<car>_vinyl/pack.ini` (`vinyl=1`, `cars=<id>`) and `<car>_vinyl_template.png` (1024², next to the pack folder, not in it: every PNG in a pack is an entry). The template draws only the sticker-slot materials (`Paint`, `Paint_SK`) from the OBJ's UV0 (V down = canvas pixel y = v * 1024) as a filled coverage with the island outlines and a 128 px grid (`docs/images/vinyl_template_gtr.webp`; the car's `_id_car` is the number in the shop log, `StickerShopUi: car 387 bound`).

**The template is NOT yet reliable - orientation test failed (2026-11-18).** On the California I painted three markers on its UV0 template (red at x440-580 y230-420, green at x420-600 y900-960, blue at x120-230 y350-600), loaded it as a vinyl and looked at the car in the shop editor. Per the OBJ the blue region is the right sill (3D centroid x 0.89, y 0.21, z 0.11) and red the boot (z +0.7..1.8, car forward = -z), but on the car the blue block lands on the **bonnet and front wings** and the red/green markers are not seen at all from the rear. So the canvas the shader samples is not the OBJ's UV0 layout (or not that unwrap/transform). `stickers_spec.md` section 1's "sampled with the body's UV0" is `[EXP]` and this contradicts it. Likely cause to check first: the paint shader samples a *second* UV set (Texcoord[1]) or applies a UV transform for the sticker slot - dump `Texcoord[1]` of the `Paint` vertices (the exporter writes only set 0) and redraw the template from it, then re-run the marker test. Until that is done a livery painted on this template will not line up; a vinyl made by trial (full-canvas stripes, as in the tests above) does work.

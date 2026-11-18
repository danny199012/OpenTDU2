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

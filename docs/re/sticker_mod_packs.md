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

## Not done / next
- Not driven through the SHOPSCAR UI interactively (thumbnails use the art as icon; only the `-stickertest` bind and the load log were run). Next: `-autoplay` into the sticker shop and check the grid shows the pack.
- Vinyl alpha is composited like a sticker; there is no UV-layout template export yet (needed by the creation tool; `car_obj_export` is the starting point).
- Vinyl packs are not yet bundled as a single archive with a manifest/hash (marketplace step).
- Only the first 15 entries of a pack show (`kGridSquares`).

# Spot holograms and the satnav ENTER state (`spot_holograms_spec`)

[DATA] `scenarics/packs/pack_spots.bnk` (`entities.xmb`: `ArrayOf_Entity__Actor__SpotMesh` x56 + `ArrayOf_Entity__Actor__SpotMeshAddOn` x8, each `mesh[0].rsc` -> a `spots_holograms_<hash>` 3DD/3DG/2DM scene) is the retail floating marker: an icon plate (4.6 m tall, local y 10..14.7) plus a backplate with the pointer cone (`*_Frame`, y 5.5..15.5). `-logspotholo` lists all 64; `-spotholotest [first]` draws 12 in a row ahead of the car.

[INF] Spot -> model: dealers by bank (`ECD_MERC` -> `CD_MERC`, `ECDC_WLD` -> `CDC_WLD`), houses `Playerhouse` (owned) / `House_To_Buy`, `Realtor`, `Clothes_Shop`, `Tuner`, `Club`, `GamblinHall` / `Casino`, `Car_Wash`, `Stickers`, `Racing_School`, `Photograph`, `Surgery`, `Used_Car_Dealer`, `Hair_Stylist`, `Airport`; race start -> `Event`; championship -> `Championship_<family>_01` / `Cup`. Backplates: event / championship / school = `Square_Blue`, owned house = `Square_Red`, houses for sale = `Rhombus_Blue`, dealers = `Circle_Orange`, clubs / casino = `Circle_Green`, the rest `Square_DarkBlue`. The entities.xmb `Type` condition (raw words in `TypeRaw`) that picks these in the retail game is not decoded.

Placement: the plate turns about Y to the camera, sits up to 12 m in front of the entrance toward the viewer, scale `clamp(distance / 60, 1, 2.2)`. Radius 300 m, 12 nearest. `-flatmarkers` restores the 2D HUD tiles.

ENTER state: `nearRadius` 60 m. `FreeRideHud::setMinimapEnter` -> the minimap eases to 0.45x metres-per-pixel, an ENTER header band is drawn in `drawMinimap`, and `/HUD/enter` is parked in the bottom strip (`getMinimapEnterStrip`) with only the prompt bar's glyph shown. Evidence: `docs/images/spot_hologram_enter.webp`.

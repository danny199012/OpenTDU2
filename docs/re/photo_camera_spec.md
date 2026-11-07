# Photo camera ("Take photo" of the pause screen)

Tags: [CODE] readable exe, [DATA] game files, [INF] inferred.

## 1. Where it hangs off

- The pause screen entry list of free roam carries `photo`, pushed with `/:LockSnapshot` and the icon frame
  `photo` (`MAIN.vmf`, `setSquareMenu`; loc id 957 "Take photo") [DATA].
- With the movie-driven menu (the AVM1 runtime runs `menu_sq.ActionLoop`) the game only routes the keys: the
  square's `valid` frame calls `_root.ValidMenu` and the pause_menu clip's script fires the retail commands
  [DATA, `-logpause` + `-logpausesq`]:
  `phmo:phin` (photo mode **in**) -> `mcls` (menu close) -> `phmo:npin` -> `phph`.
  `PauseMenu`'s FSCommand callback turns `phmo:phin` (and `phin`) into `eResult::Photo`; FREERIDE closes the
  menu and opens the camera. In the game-driven fallback the entry's own result does the same.
- The retail photo mode itself is a Flash subsystem inside `MAIN.vmf` (`photomode_menu`,
  `photomode_console`, `photomode_Polaroid`, `/:PM_FOV`, `/:PM_Filters`, `/:PM_Contrast`, `/:PM_DOF`,
  `/:PM_Exposure`, `/:PM_Hue`, `/:PM_Luminosity`, `/:PM_Saturation`, and the commands `phmo:clic`,
  `phmo:npva`, `phmo:recl`, `phop:*` (photo options), `phsa:*` (photo save), `sndp:shot` (shutter sound),
  `/:nb_photo_taken`, `/:m_TakeSnapshot_`) [DATA: VMF strings]. **It is not driven here**: this build opens a
  native camera instead and the movie's photo UI is left alone (the pause movie is not drawn once it closes).

## 2. The native camera (source/game/freeride/photo_camera.{h,cpp})

- Opens where the car camera is (`open(carPosition, carCamera)`: yaw / pitch recovered from the camera
  rotation, fov = the camera's, clamped to 25..90 deg) and flies free: no collision, the world keeps running
  (the retail photo mode is live), the car takes no input.
- Controls — pad (DualSense) / keyboard:

  | action | pad | keyboard |
  |---|---|---|
  | move along the view | left stick | W A S D / arrows |
  | look around | right stick | mouse (any button held) or Tab-captured |
  | down / up | LT / RT | Q / E, Ctrl / Space |
  | zoom (FOV 25..90 deg) | LB / RB | Page Down / Page Up, wheel |
  | take the photo | A (cross) | Enter |
  | back | B (circle) | Backspace / Esc |

  `IA_CarAccel` / `IA_CarBrake` are the triggers, `IA_CarCamera` / `IA_CarBackView` LB / RB,
  `IA_MenuValidate` / `IA_MenuBack` / `IA_MenuPause` A / B / START [DATA: DeviceX360Pad.xmb].
- The shutter writes a **PNG** of the frame to `<userdata>/photos/photo_NNN.png` (numbering continues across
  sessions; the folder is created on open). `RenderDevice::captureSwapchainImage` writes PNG through
  `stb_image_write` when the path ends in `.png`, binary PPM otherwise (the headless runs) [CODE].
  The capture happens at the end of the current frame, so `PhotoCamera::isCapturing()` is true for it and
  FREERIDE draws neither the controls bar nor the shutter flash into it — the PNG is the scene alone
  (the HUD is hidden outright while the camera is open, `FreeRideHud::setHidden`).
- The controls bar is drawn by the game (black bar + title font) at the bottom, with the photo count and the
  current FOV; the shutter flash fades over 0.35 s [INF].

## 3. Not done

- The retail photo UI (filters, contrast / DOF / exposure / hue / luminosity / saturation, the album, the
  save dialog `phsa:*`) is not driven; `phop:*` / `phsa:*` / `phmo:*` beyond `phin` are only logged.
- No "photo" reward / `CRUISING_PHOTOS_*` event, no album, no photomode restrictions (the retail mode is
  only offered at the photo spots: the pause entry is global here).

# SURVEY — touring the game, what needs work

For the agent picking this up: the `launch\` folder has one .bat per
starting point. Every freeride bat disables the story timer
(`-storytime 86400`); the world viewer bats use the free-fly camera (F1
panel: islands, spots, weather; `-flyspeed`). `16_Freeride_Night.bat` is the
same drive at night (`-night` = the darkest `game_weathers` preset) with the
car-light state in the log (`-logcarlights`) — that is the run for the
headlights / brake lights / indicators / dashboard backlight. The spot
destinations come
from `islands/<island>/level/spots.bin` — regenerate the full table with
`python tools_probe/spots_dump.py` (79 ibiza / 144 hawai / 4 casino spots,
grouped by prefix). Any spot can be spawned at with
`-gm freeride -island <island> -tp x,z -heading <deg>` (the heading is the
spot's radians in degrees; the car is dropped on the ground with the tire
fit).

## The prefixes (what the places are)

| prefix | meaning | ibiza / hawai / casino |
|---|---|---|
| ECD_* | new car dealerships: AUDI, FERR (Ferrari), MERC, ITAL, US1/US2, BRI1/BRI2, EUR1/EUR2, ASIA | 17 / 17 / – |
| ECDC_* | used car dealers (EUR, ITA, WLD) | 3 / 3 / – |
| ETUN_* | tuning shops | 5 / 5 / – |
| EPH_* | buyable houses / villas (L3DI = Tess's party house) | 19 / 42 / – |
| ECH_* | ? houses (IA / HA) | 5 / 10 / – |
| ECW_* | car washes | 3 / 5 / – |
| EDI_* | discount stores (hawai only) | – / 24 / – |
| EAIR_* | airports | 1 / 1 / – |
| EAS_* | ? generic service spots | 4 / 8 / – |
| EGAMHA | ? (game/hangar) | 2 / 4 / – |
| ENC_* | night clubs (PAC, PRIV) | 2 / – / – |
| E_HOTELA / EPHOT / ESTICK / ESTOL / ESUR / ERE | hotel, photo spot, skate park, ? , surf, race school | 1 each |
| ECAS / ESHOP_CA / ESUR_CA / ELANDCA | **the casino**, its shop, ? , landing pad | – / – / 4 |

## What works when you get there (state of the re-implementation)

- The island streams (terrain, roads, buildings, palms, ocean), you can
  DRIVE there (chase camera, physics, sounds, radio, HUD) and the world
  viewer flies through everything including interiors.
- The spot BUILDINGS render as exterior geometry. There are **no interiors,
  no doors, no opening hours, no shop logic**: dealerships / tuning / used
  car / casino floor cannot be entered in freeride (the garage of FIRST_WM
  is the only interior the game mode system can enter today, and the casino
  game floor does not exist — `source/game/casino/` was never started).
- The world viewer can fly through the spot banks (the interiors exist as
  geometry: `-gm worldviewer -island casino -spot ECAS`).

## Known issues to check at each stop (from docs/STATE.md, all still open)

1. Hair cards clip faces (party avatars; avatar work).
2. Hard-edged shadow rectangle on the road ahead while driving (shadow
   camera frustum) + tire banding in the garage.
3. Opening scene ~88 ms/frame: the party avatars + mirrors + shadows (perf).
4. HUD: clock stuck at 00:00; zone badge says "Ibiza Area 1/2" even on
   hawai / casino (zone names are ibiza-hardcoded, `freeride_hud.cpp`); the
   minimap draws only ibiza roads (sector .rd splines are loaded per island
   but the minimap paths / GPS are ibiza-tied).
5. The in-car Accessories menu: **the indicators are driven now** (2026-10-30, `cline`: the left / right entries
   blink their own side's lamps, see `docs/STATE.md` session 11); the four icons render and the retail radial
   highlight / validate-bar behaviour is still only approximated. The same session added the night headlights,
   the brake / reverse lights, the animated windows and the dashboard backlight (`-night`, `-carlights 0|1`,
   `-logcarlights`); the **in-car sat-nav quad** is mapped but not rendered yet.
6. Car physics still rolls too easily (suspension / anti-roll tuning);
   -logcorner / -vehtest exist for measuring.
7. ~~Radio: one shared playlist for the three stations~~ **fixed** (2026-10-30,
   `cline`): per-station song lists, and every `<Music>/OpenTDU2` subfolder is a
   custom station. Still open: no jingles / DJ / ads (sound/radio/comment +
   jingle + advertising banks are unplayed).
8. Ocean look: reflections render but the fresnel weighting reads flat
   (docs/STATE.md "ocean reflection" note).
9. Cut story scenes: the wake-up cinematic + C_MEETING_TESS are behind
   `-wakeup`; the full data map is in docs/STATE.md (nothing needs
   re-animation).
10. The pause screen (2026-10-30, `cline`). The dead entries are **fixed** —
    `players` / `gme` / `profiles` were selectable and opened the empty online
    "TDU2 Friends" screen and a "Link to player" page stuck on "Loading…"; they
    are locked (padlock) again. These four are **open** (compare
    `docs/images/retail-pause-freeride.webp`):
    - the map panel's island badges read **HAWAII** on Ibiza (retail: blue
      "IBIZA 7" / "IBIZA 2") — `refreshEntries()`'s
      `gotoLabel( "/pause_menu/data_extd/map/island/i1", "i1", false )` has no
      effect even though the sprite is there (root cause not established);
    - the **casino / garage / avatar-choice** pause clock reads **00:00** (only
      `GMFreeRide` calls `setClock()`, and from the weather preset's name; the
      port has no in-game clock at all — retail shows 12:36 / 18:14);
    - the info card's objective header is missing (retail: "Obtain C4
      license");
    - the money is unformatted (retail `$31,410`, port `5000000`).

## Suggested survey pass

1. `04` Ibiza drive: the village, the coast, one tunnel — shadow rectangle,
   ocean look, fps while driving, HUD clock/zone.
2. `05`–`07` dealerships / tuning / used car: how close the exteriors are to
   retail, whether the interiors are visible through the glass.
3. `09` + `11` the casino: exterior + fly the interior (what the game floor
   would need); the HUD zone name bug shows here.
4. `08` Hawaii: the whole island is driveable — check streaming cost and the
   EDI_* discount stores.
5. `02` the party: hair clipping + fps with `-nopnj` A/B.

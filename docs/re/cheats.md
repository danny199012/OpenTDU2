# Developer cheats (the retail "dev features")

Tags as in `CLAUDE.md`: `[CODE]` readable executable · `[DATA]` game files ·
`[INF]` inference, from the code's shape.

`source/game/cheats.{h,cpp}` is a panel a game mode draws every frame
(`GameMode::drawUI`), plus the wallet helpers the modes and the command line
share. It exists for testing the shop / progression / map features without
grinding: **money**, **casino chips**, **teleport**, and the "delock" rows of
the retail developer menu.

| How to open it | |
|---|---|
| **F4** | any mode (FREERIDE, CASINO) - `gpInput::wasKeyPressed( SDL_SCANCODE_F4 )`, the same shape as the world viewer's F2 |
| **the pad code** | a circle of the D-pad (up, right, down, left) **three times**, with the option button (X / square) pressed at least once during it. This is the retail dev-feature way in (`[INF]`: the community's description is "spam your D-pad in circles while mashing Square or X"; the retail sequence itself is not decoded). The D-pad circle also opens the accessories wheel - the code only watches the order of the presses |
| `-cheatspanel` | the panel is up from the first frame (any mode) |
| the pad's option button | CASINO only (its D-pad is the menu, so the code cannot be entered there) |

![The cheats panel over FREERIDE](images/cheats_panel.png)

## The rows

| Row | What it writes | Retail equivalent `[DATA]` |
|---|---|---|
| **money** | `PlayerProfile::Money` (`<userdata>/profile.sav`, `money = <n>`), saved at once. This is the wallet `DealerMenu::buySelected` spends, so a car can be bought right after | the profile's money |
| **casino chips** | `PlayerProfile::CasinoChips` (`casino_chips`), the wallet of `GMCasino` | the casino save's balance |
| **teleport** | `GMFreeRide::teleportTo`: the world streams at the target, a ray finds the surface (the Havok road / static collision first, then `EdenWorld::getSurfaceHeight`), the car is reset there with `VehicleDynamics::reset`, the chase camera follows. The list is the island's `spots.bin` (the buildings: dealerships `ECD_*`, tuning `ETUN_*`, houses `EPH_*`, shops...), filtered by a text box; a double click goes there at once | "POSITION/SPOT" |
| **the destinations row** | one button per landmark of the island's own spot list: `Airport` (`EAIR_*`), `Casino` (`EGAMHA`), `Ferrari dealer` (`ECD_FERR`), `Any dealer` (`ECD_*`), `Tuning shop` (`ETUN_A`), `Car wash` (`ECW_A`), `Race school` (`ERS_A`), `Clothes shop` (`EAS_A`). `Cheats::setSpots` resolves each bank prefix against the island's spots and skips a landmark the island does not have, so the row is the same on Ibiza (79 spots) and Hawai (144) | "POSITION/SPOT" |
| **F5 on the map** | teleports to the point the map cursor is over (`MapScreen::getCursorWorld`, the same value the GPS destination uses). The map keeps the car marker under the cursor, so the marker moves to where the player pointed | "POSITION/SPOT" |
| **weather** | the `game_weathers` preset of the world view (`WeatherLibrary`) | - |
| **story step** | `PlayerProfile::StoryStep` (a restart uses it) | - |
| **back on track** | the same reset the pad's BACK_ON_TRACK does (the nearest road triangle) | - |


| **All cars** | `PlayerProfile::OwnedCars` = every `DBCarShop` row (`GetCarShops()`), `NbCars = owned + 1` (the starter car is not in the list) | "ALL CARS" |
| **All clothes** | `PlayerProfile::OwnedClothes` = every `DBClothe` row (`GetClothes()`), so the clothes shops and the hairdresser show everything as owned | "ALL CLOTHES" |
| **All houses** | `PlayerProfile::NbHouses` = the island's `EPH_*` spots (20 on Ibiza) | "ALL HOUSES" |
| **All roads** | `MapScreen::unlockAllRoads`: every link of the island's road graph marked explored, the per-area counters moved with it (so the area milestone events fire) and `ExploredRoads` saved - the map then reads 100 % | "ALL ROADS" |
| **Nearest locked road** | `MapScreen::targetNearestUnexplored`: routes to the closest link the player has not driven | "TARGET NEAREST LOCKED ROAD" |
| **All achievements** | every `DBRewardXP` event awarded once (`AwardSkillEvent`), the levels / HUD popups follow | "ACHIEVEMENTS" |
| **cars on stock (buyable)** | a switch: `DealerMenu` treats every `DBCarShop` row as buyable (`Cheats::isAllCarsBuyable`) | "CARS ON STOCK" |

Rows the retail menu has that have **no** equivalent, because nothing gates them
in the port: "ALL CAR DEALERS" (the dealer buildings are ordinary spots, always
there).

## Command line

| Option | Effect |
|---|---|
| `-money <n>` | `PlayerProfile::Money` at startup (any mode) |
| `-chips <n>` | `PlayerProfile::CasinoChips` at startup (the CASINO mode's own `-casinochips` does the same) |
| `-cheat <row>[,<row>...]` | the delock rows: `allcars`, `clothes`, `houses`, `allroads`, `buyable`, `achievements` |
| `-cheatspanel` | the panel is open at startup |
| `-tpin x,z[,heading]` | a teleport **once the drive runs** (the in-game path; `-tp` only moves the spawn) |
| `-tpdestination <name>` | the destinations row's button, once the island's spots are in: `Airport`, `Casino`, `Ferrari dealer`, `Any dealer`, `Tuning shop`, `Car wash`, `Race school` or `Clothes shop` (an exact name first, then the first name containing it) |

`launch/15_Freeride_Cheats.bat` starts FREERIDE with the panel up and a full
wallet (add `-cheat allcars,clothes,buyable,houses,allroads` for the delock rows
too).

## Tests

Everything is scriptable, which is how the rows are verified (no clicking
needed):

```bat
:: the wallet + the delock rows + the panel, headless
opentdu2.exe -gm freeride -skipcine -nopad -cheatspanel ^
  -cheat allcars,houses,allroads,buyable,achievements -exitframe 330

:: the pad code: three D-pad circles with X
-autoplay "200:press=accup;205:press=accright;210:press=accdown;215:press=accleft;220:press=opt1;..."
```

The log then carries `Cheats: money = $5,000,000`,
`Cheats: all cars: 83 added, 83 owned, NbCars 84`,
`MapScreen: all roads unlocked: 0 -> 1350 of 1350 link(s), 906758 m, 100%`,
`Cheats: achievements: 189 of 191 event(s) awarded`, `Cheats: houses = 20` and
`Cheats: panel open (pad code)`. `-tpin 36000,5800` logs
`FREERIDE: teleport to (36000.0, 6.0, 5800.0), heading -110 deg` - the surface
under the target is found by the ray, not guessed.

The destinations row is scriptable too: `-tpdestination Airport` logs
`Cheats: 8 quick destination(s): Airport, Casino, ...` and then
`Cheats: -tpdestination 'Airport' -> EAIR_A (16780, 27512)` /
`FREERIDE: teleport to (16780.2, 20.9, 27512.4), heading -110 deg`; on Hawai
`-tpdestination Casino` resolves `EGAMHA (23449, 43355)`.

Measured on Ibiza (`[MEASURED]`): 79 spots in the teleport list, 1 350 road
links / 906 758 m of road, 83 cars in the `DBCarShop` table, 191 `DBRewardXP`
events, 20 `EPH_*` houses.

## Notes

- The teleport does not need a road: a point in the open sea stays at the
  target height (`y = 0.6`). That is how a "teleport yourself to Dubai" cheat
  freezes the retail game; here the world streams and the car simply floats
  until it is driven back to land.
- `unlockAllRoads` awards the area milestone events exactly like driving the
  roads would; that is the point (testing the road / Discovery features), and
  it is saved to the profile.
- The cheats write the profile (`profile.sav`); use a separate `-userdata`
  folder when a test must not touch the normal one.

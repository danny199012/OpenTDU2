# Save game format and locations

Found by the owner's pointers, 2026-10-28. Tags as in CLAUDE.md.

## Where the games save
- **Xbox 360 recomp (rexglue)** `[DATA]`: `C:\Users\<user>\Documents\tdu2\00090000002B21AD\49470804\00000001\`
  (`49470804` is the title id, "IG" 0x08 0x04). Folders: `PLAYERSAVE\DATA` (143,714 B, the player),
  `PLAYERSAVE\OPTIONS` (7,531 B), `LICENCES\LIC_03` and `AVATAR\PHOTO00` (JPEG images, 160x? photos),
  `Headers\`. Written on the **owner's own play**, not by the port.
- **PC TDU2 (Steam / World)** `[DATA]`: `C:\Users\<user>\Documents\Eden Games\Test Drive Unlimited 2\savegame\<profile>\`
  with `AVATAR\PHOTO00`, `PLAYERSAVE\DATA`, `KEYMAP`, `OPTIONS`, plus `savegame\ProfileList.dat`
  and `savegame\SystemDefault`. The 360 layout is the same with a title-id wrapper.
- The **PC `DATA` is encrypted** `[DATA]` (167,929 B, first bytes `f4 8f d3 17 ...`, no strings, no
  readable money or name). The **360 `DATA` and `OPTIONS` are not**: plain, ~85 % zeros.
  The PC cipher and key are not identified `[ENC]` (the save code is in the encrypted band of
  TestDrive2.exe); the PS3 build is the place to read it (`ps3_enc_twins.json`). The save file itself is
  the user's own data, but the exe policy of CLAUDE.md section 7 still applies.

## Format `[DATA]`
`DATA` and `OPTIONS` start with `XMBF 01 01` and a small header (`00 00 00 00 00 1c`, then section
sizes), a **string table of type and field names** (`ARRAY_OF_Car`, `ARRAY_OF_CarInGarage`, ...), then the
type schema, then the values. It is the engine's typed-struct XMB, not the text-tree XMB of the game
files. `otdu2_bnk_tool xmb <file>` prints the schema (root `PlayerData`, size 2244).
Full dump: `docs/re/savegame_schema_360.txt`.

`PlayerData` fields (offset in the struct): `Driver` (+10: Id, Name, Money, NbCoupon, OdometerKms,
CurrentCar, CurrentIsland, CurrentHouse, LastPosWorld, NbSurgeries, NbHaircuts ...), `Garage`
(CarInGarage[]), `Ranking`, `NewsList`, `AvatarSave` (+145, AvatarConfiguration of 107 B),
**`RoadLock`** (+260, `UINT64[] Locks`), `CEnvironment`, `ClassicCarStock`, `AchievementsData`,
`StatisticsData` (+308), `GServiceTutorial`, **`PlayerLevels`** (+1217: PlayedTheGame, TutoSeen,
`RacingLevel`, `CollectionLevel`, `SocialLevel`, `CruisingLevels` (CruisingArea), HaircutsBought,
**`AreaRoadsUnlocked[]`, `AreaTotalRoads[]`**, AirportUnlocked), `PlayerLicenses`, `EventsSave`, `Wrecks`,
`SCPhoneCalls`, `PhotosGP`, `MyHouse`, `MySpots`, `Bookmarks`, `StolenCarReceiver`, `Rewards`, `Stickers`,
`Chase`, `FirstIntros`, `KnownSpots`, `FrogCoopObjectives`, `CurrentObjective`, `MyLife`, `HelpSystem`,
`Casino` (+1640), `PlayersList`, `Radio`.

## What it answers
- **Explored roads**: `PlayerLevels.AreaRoadsUnlocked / AreaTotalRoads` (per area counters, the
  "Explored" percentage) and `RoadLock.Locks` (UINT64 list, probably road ids / hashes) `[INF]`.
- **Skill levels**: `RacingLevel`, `CollectionLevel`, `SocialLevel`, `CruisingLevels` are *levels*, not
  points; the points are not in this struct (they are derived from events / statistics) `[INF]`.
- **Money, name, car, island, house**: `Driver`.

## Next
1. Write a value reader for the typed XMBF (the port's `core/xmb` reads the schema already) and dump
   the 360 `DATA` values (check `Driver.Money`, `PlayerLevels` against the owner's play).
2. Make the port's save this same format (writer needed), so the 360 saves load and the port's saves
   are comparable to the retail layout. Until then `<userdata>/profile.sav` (plain text) is a stopgap.
3. PC `DATA`: find the cipher in the PS3 build before reading it.

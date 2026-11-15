# Cut / extra shop types and the nightclubs (`cut_shop_types`)

The owner's leads (2026-11-15, AlexVII pictures): a **petrol-pump map icon** and a
shop-logo collage with one logo covered by a black box, *"apparently a strip club or
something to do with strippers"*. What the data actually has, read from the dev exe
(`tools/devbuild_disasm.py`), the decrypted DBs (`otdu2_bnk_tool decrypt db_data.cpr /
db_us.cpr`) and every UI bank. Tags: [CODE-dev] dev exe, [DATA] game files, [INF]
inference, [ENC] retail-encrypted.

## 1. The authoritative shop-kind tables [CODE-dev]

Two null-separated kind tables in the dev exe's data:

- **Spot kinds** @0x14d7750-0x14d78a0 (next to the "Player is in a Shop / Casino /
  Driving School / House" state strings): `chall_multi, erasable, ECH_, carPaint,
  estates, carRental, carWash, hotel, auctionHall, ORC, airport, stolenCar, surgery,
  photograph, mission_spot, rschool, gamblingHall, my_club, tuning, clothes, bikeDealer,
  carDealer, hair, forsale, home, gasStation, classic_h, dirt, dirt_h, cup_h, asphalt_h`.
- **Map kinds** @0x14f2560 (GM map filter): `pilot_c, f_drivein, f_cmulti_type,
  f_carDealer, f_carDealerClassic, f_bikeDealer, f_carWash, f_carPaint, f_tuning,
  f_carRental, f_stolenCar, f_auctionHall, f_estates, f_clothes, f_home, f_forsale,
  f_club, f_watches, f_hair, f_surgery, f_airport, f_1_10..f_51_60`.

The shipped `spots.bin` banks use only: car dealers (cat 1), clothes (3), tuning (4),
clubs (5), gambling hall (6), car wash / stickers / realtor / **nightclubs / diner** /
building sites (8), race schools (9), photo (11), seaplane dock (14), surgery (16),
used cars (17), hair (19), airport (22), hotel (25). **No gasStation, carRental,
auctionHall, forsale, watches or driveIn spots ship.**

## 2. The petrol symbol = the cut fuel feature [CODE-dev + DATA]

- The engine has a **`gasStation`** spot kind [CODE-dev] and the GM UI speaks an
  **`FSCommand:gaso`** to the game (gmeditor.vmf) [DATA].
- But no `EGAS*` bank exists in either island's spots.bin, neither MAP.vmf nor
  HUD.vmf has a fuel/gas frame label (MAP.vmf kinds: `f_bikeDealer f_carDealer
  f_carDealerClassic f_carPaint f_carWash f_clothes f_club f_estates f_hair f_home
  f_stolenCar f_surgery f_tuning` [DATA: strings]), and the only pump model in the
  whole game is the player-garage prop `gaspump_d.2db` (gph_l2d.bnk,
  fuel_gas_station_spec.md §4.1) [DATA].
- So the petrol-pump icon in the icon sheet is the leftover of the **cut fuel-station
  feature** (TDU1 had fuel; TDU2 shipped without it). The port's own
  `<mods>/stations.txt` / FUEL1 props (fuel_gas_station_spec.md) fill exactly this gap.

## 3. Cut shop types that have UI but no spots [CODE-dev + DATA]

- **carRental / auctionHall / watches / forsale / driveIn**: HUD.vmf enter-tile frame
  labels `bikeDealer, carDealer_hud, carPaint, carPaint_d, tuning, carWash, carRental,
  stolenCar, auctionHall, airport` [DATA]; MAP.vmf's map filter legend names
  `_ESTATES, _CAR DEALER, _BIKE DEALER, _AFTER MARKET, _CAR RENTAL, _CAR PAINT,
  _CAR WASH, _AVATAR SHOP, _CLUB, _DRIVE-IN, _ROOKIE.._MASTER..` [DATA] — with
  `_AFTER MARKET` a further unused one. MAP.vmf's popup code ties `m_AuctionHall_`
  to `f_auctionHall` + `DLC2` (the auction hook lives in the DLC branch) [DATA].
- **ORC**: its own spot kind and a whole `orcscreen.bnk` UI (`FSCommand:orcp:wmra`)
  [DATA]; no ORC spot ships. (ORC = the off-road screen; expansion/DLC hook, [INF].)
- **f_drivein**: a drive-in kind in the map filter; no drive-in anywhere [DATA].

## 4. The nightclubs — the real "extra stuff" behind the club rumors [DATA]

Two **real licensed Ibiza nightclubs** ship as scenery-only spots (category 8, like the
car wash; source paths `spots/nightclub/...`):

| Bank | Place | Spot (Ibiza, spots.bin) | Contents |
|---|---|---|---|
| `enc_pac.bnk` (+`_low`) | **Pacha Ibiza** (the real club, cherries logo) | (23129.9, 4.0, 23132.0) hdg -7° | `pacha_d.2db`: "EVERY FRIDAY PURE PACHA / TODOS LOS VIERNES" flyer art, the cherries logo sheet, "SUSHILOUNGE" roundel |
| `enc_priv.bnk` (+`_low`) | **Privilege Ibiza** ("WORLD'S BIGGEST CLUB") | (19904.7, 140.8, 17979.0) hdg -49° | `privilege_d.2db` + `privilege_neon.2db` (the blue-neon "Privilege" + red spiral logo), `pub_d.2db` (club-merch props: the Privilege banner, a "CAMERAS PROHIBITED" sign, "THE FACE" banner, a t-shirt stand, a candy cane, "TICKETS") |

- Encoded as normal exterior banks; **SpotExteriors already streams both in the port**
  ("ibiza: 79 spots, 79 with an exterior bank") — they are the fenced club compounds on
  the coastal roads.
- No interiors, no DB shop rows, no enter flow, no dancer/stage entities
  (entities.xmb has none of it) [DATA]. Nothing "strip" anywhere: the exe data, both
  decrypted DBs and all 44 UI VMF banks contain no strip-club string (the only exe
  "strip"/"RedLight" hits are `black_stripes` graphics and the traffic AI's
  `InfRedLightValue`) [DATA]. The rumor is best explained [INF] as the *nightclubs*
  (a private-club building with a covered logo in a fan collage) retold as a strip club.
- Hawai has its own scenery-only oddity: **`edi_b.bnk` = "Ruby's Diner"**
  (`spots/diners/di_b`, `rubys_diner.2db`) at **25 spots**, icon-less on the map (the
  map movie has no diner kind).

## 5. The collage logos are all real in-game signs [DATA]

Each logo of AlexVII's collage exists as a shop-sign texture: the clothes shop is
"Hiro Hisaishi" (`eas_a`: `highlife_d`, `hiro_02_d`, `hh_logo_d`), the asphalt racing
school is **"Sea Speed & Sun — Official Hawaiian Asphalt Racing School"**
(`ers_a`: `rs_a_board.2db`, 512x128), the hairdresser has `enseigne_wip_d` (a
work-in-progress placeholder sign, 128x64) and `enseigne_neon`. "The Skinner" tattoo /
"Gregorio Domani" hair / "Dirt Surfers" dirt school are the same kind of per-shop sign
textures in their banks. The collage is a fan compilation of these; **no sign texture
for a strip club (or anything beyond the shops above) exists in the searched data**.

## 6. Port follow-ups (all optional)

- The nightclubs and the diner already draw; making them **discoverable map spots with
  a "club"/scenery icon** would match retail's category system (they are cat 8 like the
  washes, but the map movie has no club-scenery kind — `f_club` is the multiplayer
  club). [INF]
- The cut kinds give the mods system ready-made targets: a `<mods>` station pack could
  register spots as `gasStation` (the engine kind exists), and rental/auction
  buildings could reuse the HUD tiles already in HUD.vmf. [INF]

# Phone calls (DBPhoneCall) - in the retail data, not cut (2026-11-14)

Owner request: "dig for the phone calls" after the dev build (`TestDrive2DEV.exe`) showed `MngPhone2.cpp`, `SCPhoneCalls.cpp`, `DBPhoneCall`,
`HUD/phone_call`, `HUD/phone_call/phone_message/lure`, `FrontEnd/AllRes/PhoneCalls/` and the debug entries `LAUNCH PHONE CALL` / `RESET PHONE CALLS` [CODE-dev].

## What is there [DATA]
- **Retail `db_data.cpr` has the table**: `DBPhoneCall`, 33 rows, columns `_hashcode _phonecallname _npc _text_hc _soundfile _timer _nexttext _nbrepeats _yesnoanswer`
  (`-dumpdbtable DBPhoneCall`; `_text_hc` is a localization id, the text is in the string tables). `DBTutoMess` (76 rows: `_tutomess _mess_hc _messpc_hc
  _conditiontimer _lastonscreentimer _nbtimeallowed _soundfilename`) is the neighbouring tutorial-message table.
- **The voices are in the retail install**: `euro/bnk/sound/voices/<lang>/pc_NN_*.wav` (e.g. `pc_10_reward_car.wav`), and the call-screen art is
  `frontend/allres/phonecalls/` (`spt_*` / `sto_*` / `ev_*` banks, one per caller).
- **The port uses none of it**: the HUD movie has the `phone*` clips (freeride_spec.md 4) and the save has `SCPhoneCallsSave` (`player_profile.h`), nothing drives them.
  So phone calls are *implementable from data we already read*; they are not cut content.

## The 33 calls
| id | name | NPC | voice | timer s | repeats | yes/no | text start |
|---|---|---|---|---|---|---|---|
| 0x1 | 'WRECKS_CALL' | 0x17f | 'PC_10_Reward_Car' | 0 | 1 | 1 | Hey, I'm done with that job. I finished assembling those parts you fou |
| 0x2 | 'CONVOY_WIN_CALL' | 0x12a | 'EV_CO_Phone_Win' | 0 | 1 | 0 | Thank you for your help, good-bye. |
| 0x3 | 'CONVOY_LOOSE_CALL' | 0x12a | 'EV_CO_Phone_Loose' | 0 | 1 | 0 | If I'd known you were incompetent, I wouldn't have handed over the key |
| 0x4 | 'CALL_C4_NOT_PASSED' | 0x187 | 'PC_06_RS_Classic' | 4 | 2 | 1 | I don't have all day. I'm at the Classic Driving School. Don't keep me |
| 0x5 | 'EVENT_JANE' | 0x187 | 'PC_07_Help_Jane' | 3 | -1 | 1 | I've got a favor to ask. A friend of mine needs some help. If you coul |
| 0x6 | 'RIVAL_C4' | 0x18f | 'PC_18_Duel_Miami' | 15 | 2 | 1 | It's Miami... You got lucky in the last championship, and now I bet yo |
| 0x7 | 'RIVAL_B4' | 0x18c | 'PC_19_Duel_Ryan' | 15 | 2 | 1 | Stop playing around! Come and race me in a duel if you have the guts.  |
| 0x8 | 'RIVAL_A5' | 0x194 | 'PC_21_Duel_Tess' | 15 | 2 | 1 | Hi, it's Tess. I want you good and ready for the next Solar Crown, so  |
| 0x9 | 'RIVAL_A2' | 0x190 | 'PC_22_Duel_Stuart' | 15 | 2 | 1 | Listen carefully... Forget the cameras and championships. Just me and  |
| 0xa | 'RIVAL_B3' | 0x18d | 'PC_20_Duel_Chris' | 15 | 2 | 1 | Yo, it's Chris. I hear you think you're hot stuff! Well, I'm just the  |
| 0xb | 'NEW_ASPHALT_CAR' | 0x180 | 'PC_02_New_Asphalt_car' | 15 | 2 | 1 | I don't have to remind you that you need a Category-A car to take part |
| 0xc | 'NEW_SUV_CAR' | 0x17e | 'PC_01_New_SUV_car' | 15 | 2 | 1 | You're going to need a Category-B vehicle for the Dirt Race championsh |
| 0xd | 'REALTOR_CALL' | 0x187 | 'PC_08_New_House' | 0 | 1 | 0 | The garage of yours has got to be getting pretty cramped, with all tho |
| 0xe | 'RS_ASPHALT' | 0x194 | 'PC_03_RS_Asphalt' |  | 2 |  | I think you're ready for an even more challenging race. |
| 0xf | 'CLOTHES_CALL' | 0x194 | 'PC_04_Look' | 0 | 1 | 1 | You're a mess! You can't go on TV looking like something the cat dragg |
| 0x10 | 'TUNER_CALL' | 0x187 | 'PC_09_Tuner' | 0 | 1 | 1 | You need to treat your car right. How do you expect to win races if yo |
| 0x11 | 'HAWAI_CALL' | 0x194 | 'PC_05_Hawai' | 15 | 2 | 1 | We're leaving today! Meet us at the airport! As soon as you get there, |
| 0x12 | 'CHASE_INTRO_IBIZA' | 0x151 | 'PC_11_Chase_Intro_Ibiza' | 0 | 1 | 0 | A reckless driver has been spotted. Do you want to chase him? |
| 0x13 | 'CHASE_INTRO_HAWAI' | 0x151 | 'PC_12_Chase_Intro_Hawai' | 0 | 1 | 0 | A dangerous driver is being pursued by the police. Do you want to join |
| 0x19 | 'SURGERY_HAWAI' | 0x18f | 'PC_25_Surgery_Hawai' | 0 | 1 | 1 | Hey there. It's Miami. You know, it isn't just about how good you driv |
| 0x1a | 'SURGERY_IBIZA' | 0x18f | 'PC_26_Surgery_Ibiza' | 75 | 2 | 1 | Hey there. It's Miami. You know, it isn't just about how well you driv |
| 0x1b | 'PHOTOGRAH' | 0x182 | 'PC_27_Photograph' | 40 | 2 | 1 | Hi, I'm calling to introduce myself. My name is Mikey. Marco gave me y |
| 0x1c | 'STICKERS' | 0x194 | 'PC_28_Stickers' | 90 | 2 | 1 | Hey, it's Tess. Your appearance is important if you're going to be on  |
| 0x1d | 'HAIR_DRESSER' | 0x194 | 'PC_29_Hair_dresser' | 0 | 1 | 1 | It's Tess again. You're a racing celebrity now, and you need to take b |
| 0x1e | 'CAR_WASH' | 0x187 | 'PC_30_Car_Wash' | 0 | 1 | 1 | Hey, it's Todd. I wanted to give you a tip about a good carwash in the |
| 0x1f | 'CLASSIC_CHAMP' | 0x187 | 'PC_31_Classic_Champ' | 45 | 2 | 1 | Hey, it's Todd. There's a lot of buzz about you on the racing circuit. |
| 0x20 | 'DIRT_CHAMP' | 0x17e | 'PC_32_Dirt_Champ' | 45 | 2 | 1 | Hey, what are you doing? The off-road championships don't interest you |
| 0x21 | 'ASPHALT_CHAMP' | 0x180 | 'PC_33_Asphalt_Champ' | 45 | 2 | 1 | It's Marco... Listen, I didn't train you so that you drop out of compe |
| 0x22 | 'PHOTOGRAPH_END_IBIZA' | 0x182 | 'PC_35_Photograph_End' |  | 1 |  | Hey, it's Mikey, congratulations, you have taken all the photos! |
| 0x23 | 'TREASURE_HUNT' | 0x17f | 'PC_34_Treasure_Hunt' | 30 | 2 | 0 | Hi! It's Jude...Sometimes, you'll find wrecked cars hidden on the isla |
| 0x24 | 'HAWAI_DIRT' | 0x17e | 'PC_23_Hawai_RS_Dirt' | 0 | 1 | 1 | Hi, it's Jane. Don't forget to go to the Dirt driving school. You stil |
| 0x25 | 'HAWAI_CLASSIC' | 0x187 | 'PC_24_Hawai_RS_Classic' | 0 | 1 | 1 | Hi, it's Todd. So, do you like being in Hawaii? You know that I have a |
| 0x29 | 'PHOTOGRAPH_END_HAWAI' | 0x182 | 'PC_35_Photograph_End' |  | 1 |  | Hey, it's Mikey, congratulations, you have taken all the photos! |

(`_npc` is the DBPNJ hashcode of the caller; `_timer` is a delay in seconds before the call fires after its condition; `_nbrepeats` -1 = unlimited; `_yesnoanswer`
1 = the call asks Yes/No. Calls such as `CALL_C4_NOT_PASSED`, `REALTOR_CALL`, `HAWAI_CALL`, `CLOTHES_CALL`, `SURGERY_*`, `STICKERS`, `HAIR_DRESSER`, `CAR_WASH`,
`*_CHAMP`, `TREASURE_HUNT`, `PHOTOGRAPH*`, `RIVAL_*` (duel challenges: the caller stakes their car) are story / progression nudges.)

## What the dev exe says about the triggers [CODE-dev]
The string xrefs of `SCPhoneCalls` / `REALTOR_CALL` / `RoadLock` / `AreaRoadsUnlocked` all land in **save-component reflection** (`SCPhoneCalls.cpp`: class
registered at `0x9a3670`, field readers `0x9a1a28..0x9a3068`), i.e. the *persisted state*, not the triggering code (`SC` = save component). That gives the exact
saved fields of `SCPhoneCalls` (registration order, `0x14da938..0x14dab88`):
- **Progress flags / counters:** `PlayTimeWhenArrivingOnHawai`, `BeenInRSDirtHawai`, `BeenInRSClassicHawai`, `BeenInPhotograph`, `BeenInClothesShop`, `BeenInStickerShop`,
  `BeenInHairShop`, `BeenInSurgeryShop`, `BeenInCarWash`.
- **One state entry per call name:** `NEW_SUV_CAR NEW_ASPHALT_CAR CLOTHES_CALL SURGERY_IBIZA REALTOR_CALL RS_ASPHALT HAWAI_DIRT HAWAI_CLASSIC HAIR_DRESSER TUNER_CALL
  PHOTOGRAPH_END_HAWAI PHOTOGRAPH_END_IBIZA PHOTOGRAH HAWAI_CALL TUTO_RESCUE_JANE RACING_LICENCE_A7A6 CLASSIC_CHAMP DIRT_CHAMP ASPHALT_CHAMP EVENT_JANE TREASURE_HUNT
  CALL_C4_NOT_PASSED` (the container is `SCPhoneCallsSave`). `TUTO_RESCUE_JANE` and `RACING_LICENCE_A7A6` are **not** in `DBPhoneCall`: they are saved call slots
  of the tutorial / licence scripts.
- Calls in the DB but **not** in this save (fired by another system, no repeat memory): `WRECKS_CALL`, `CONVOY_*`, `RIVAL_*`, `CHASE_INTRO_*`, `SURGERY_HAWAI`, `STICKERS`, `CAR_WASH`.
- **Likely trigger pairing [INF, by name]:** a nudge call fires (after its `_timer`, up to `_nbrepeats` times) while its `BeenIn*` flag is still false:
  `CLOTHES_CALL`/`BeenInClothesShop`, `STICKERS`/`BeenInStickerShop`, `HAIR_DRESSER`/`BeenInHairShop`, `SURGERY_*`/`BeenInSurgeryShop`, `CAR_WASH`/`BeenInCarWash`,
  `PHOTOGRAH`/`BeenInPhotograph`, `HAWAI_DIRT`/`BeenInRSDirtHawai`, `HAWAI_CLASSIC`/`BeenInRSClassicHawai`, `HAWAI_CALL` after `PlayTimeWhenArrivingOnHawai`.
  `player_profile.h` already carries `BeenInCarWash` as `SCPhoneCallsSave` +4. The *conditions* (which story step enables the call) are still unread.
- The road-discovery rule is **not** there either: `RoadLock` / `AreaRoadsUnlocked` are the save fields (`0xa05771`, `0xa0b4d8`, `0xa0cf0e`); the radius lives in the
  code that writes them (`HudMap` / `MngRoadNetwork` `0xd07120`) and is anchored by no string.

## Built (2026-11-14, `main`)
- `source/game/freeride/phone_calls.{h,cpp}`: reads `DBPhoneCall`, queues calls, rings one at a time with a 20 s gap, plays `pc_*.wav` (language, fallback `us`), shows the text
  in the HUD movie's own popup (`FreeRideHud::showPhoneCall`: `/HUD/phone_call` label `open` + `phone_message` `open`, text var `/:dialog_ph`; hang-up = `end_dialog` / `close`),
  counts rings in the profile (`phone_call_<NAME> = n` in the save) and honours `_nbrepeats`.
- Verified: `-phonecall REALTOR_CALL` (debug: force a call by DB name) shows the retail popup (ringing phone in the satnav + text + OK) with the 10.5 s voice;
  `CAR_WASH` rings then hangs up after its 8.3 s voice (log `PhoneCalls: ring ... / hung up`).
- Only one **automatic** rule so far: `CAR_WASH` while `!BeenInCarWash` and `StoryStep >= 10`, checked every 45 s of free driving [INF]. Other calls need their `BeenIn*` flags set by their shops, or a
  story hook calling `phoneCalls.request("NAME")`.
- **Portraits + answers (2026-11-14, later):** the caller's 2db from `frontend/allres/phonecalls/<spt_|sto_|ev_>name.bnk` is bound into `phone_message/lure` (`setBitmapOverride`); NPC -> bank by the call texts [INF]: 0x17e jane, 0x17f jude, 0x180 marco, 0x182 mickey, 0x187 todd, 0x18c ryan, 0x18d chris, 0x18f miami, 0x190 stuart, 0x194 tess, 0x12a ev_bwoman01; 0x151 (chase dispatcher) uses the movie's `calque_chase`. Validate (A / Enter) = `accept`, Back (B / Esc) = `decline`; either stops the voice and hangs up 2 s later. Verified `-phonecall CAR_WASH` + `press=validate` (Todd's face shown; log `accepted` / `hung up`).
- Not done: what *accepting* does (the retail likely sets the GPS to the shop / starts the event: unread), the voice bus (shares the GPS one [INF]), the story-step conditions of the rest, the chase dispatcher badge unchecked on screen.

## Not done / next [INF]
1. **The trigger conditions**: not in the `SCPhoneCalls` functions (save serialisation, above). Next anchor: `MngPhone2` - `0xd20e50` is only its class-name getter (`mov eax, 0x150e8a8; ret`), so that is a dead end by itself: `0xd20e50` is reached only through the jump thunk `0x4292c6`, which sits in an 8-slot class-descriptor table at `0x150e880` (slots `0x4292c6 0x423e11 0x446957 0x430215 0x40562d 0x42e88e 0x419aab 0x411838`, the descriptor `0x150e880` itself has no direct code reference), NOT in the object's vtable. Next: read those 7 other slots (`--dis` on each thunk target) and look for the Update / OnEvent function from the manager's constructor; `LAUNCH PHONE CALL` (`0x1502350`) has no code xref (debug-menu table); `EFAO_MngPhone` (`0x148b410`) is used at `0x4e2950` and the
   caller of the save-field readers; `python tools/devbuild_disasm.py --dis <va>` (needs `capstone` + `pefile`, not `-I`). Retail twin is `[ENC]`.
2. HUD side: drive the movie's `/HUD/phone_call` clip (`phone_message` / `lure`) from a queue; play `pc_*.wav` through the voices path; Yes/No uses the in-car menu pad.
3. Persist the "already called" counters in `SCPhoneCallsSave` (layout in `savegame_spec.md`).

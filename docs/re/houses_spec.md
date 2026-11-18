# Real estate and houses: the realtor, the buyable houses, entering them

Status (2026-10-30, session 46 `glm`): **implemented** — the realtor sells the
DBHouse houses (`source/game/house_shop.{h,cpp}`, `source/game/freeride/estate_menu.{h,cpp}`),
owned houses are walked into (living room + garage with your cars,
`source/game/spots/interior_walk.{h,cpp}` `enterHouse`), the tuning shops
(ETUN_\*) and the sticker / paint shops (ESTICK_\*) have menus
(`tuning_menu.{h,cpp}`, `paint_menu.{h,cpp}`). The tuning / sticker shop work
was pulled in because they are the other spots whose data was already in the
database. Tags: `[DATA]` read from the game files, `[CODE]` from the readable
exe, `[INF]` chosen, `[ENC]` in the SecuROM band.

## 1. What the map icons are [DATA]

`islands/<island>/level/spots.bin` categories (`map_screen.cpp loadSpots`):
**0x00 "estates" = the `EPH_*` houses themselves** (19 records on Ibiza), and
inside 0x08 the `ERE_*` real-estate agencies carry the same icon; 0x06
`EGAMHA` is the gambling hall ("gamblinghall"); 0x19 `E_HOTELA` the hotel
("home"). The other 0x08 members: `ECW_*` car wash, `ESTICK_*` paint, `ENC_*`
night clubs, `EUC_*` building sites (no icon). Full census per island with
`-logspots`.

An owned house shows the **home** icon instead of "estates" on the map
(`buildSpots`, logged `'X' is home: home icon`) [INF: the retail law of the
`f_home` filter].

## 2. DBHouse: the house market [DATA]

76 rows (`-loghouses` dumps them; `house_shop.cpp GetHouses` reads the table):

| column | meaning |
|---|---|
| `_buyprice` / `_sellprice` | $28,000 (`EPH_L0C_1254I` studio) … $7,000,000 (`EPH_L5Y_6360` villa) |
| `_housename_hc` | localized name ("Cala Moli residence", "Sa Cala house", "Salines mansion" …), read as `LocStr((int32_t)hc)` like every DB hashcode column |
| `_level` | 0..5 (the house tier, and the bank suffix) |
| `_nbcarslots` | 0..8 (2 studio, 4, 6, 8 villa; 0 club houses) |
| `_realtor` | the selling agency's **full spots.bin name**: `ERE_B_2236 / _7747 / _1564 / _7563 / _1264I / _3336I`, plus `ERE_CLUBS` for the `ECH_*` club houses (not buyable here) |
| `_spotname` | the house's full spots.bin name (`EPH_L3DI_5067I`): its record id is `GetIdentifier64bit` of it — the same rule as `FindShopBySpotId` |
| `_windowedgarage` | presentation flag, not used yet |

`EPH_L3DI_5067I` is the story (party) house and is owned from the start
(`PlayerProfile::OwnedHouses` default; `NbHouses` follows the list).

## 3. The realtor (ERE_B) [DATA]

`interior/ire_b.bnk` is a standard shop bank (spot_interiors_spec.md 2):
Entity__FirstPerson { StartCamera `cam_entree` }, `Exit` (Type 0), **`Realtor_area`
Type 7 = "realtor"** (walk_mode.cpp's type table), Entity__PNJ `Realtor`,
cinematics `C_INTRO_I` (entry), `C_LIFE` / `C_LIFE_SOUND_*` (idle loops),
`C_BUY_HOUSE` (purchase scene), `C_CAM_REALTOR`; the key model `cf_khous` is in
the bank.

The agency instance is known from the **spot record id**: `spot.Id ==
GetIdentifier64bit(realtor)` (`FindHousesByRealtor`), so each ERE_B building
sells its own stock (the Ibiza `ERE_B_3336I`: 10 houses).

## 4. The menu and the purchase [INF laws]

`EstateMenu` (native panel, the DealerMenu pattern; the retail UI is the
estate movie, not driven): rows = the agency's houses — localized name, level,
car slots, price (red when unaffordable), owned rows greyed "Yours" / "Yours -
home". **A** buys (`Money -= _buyprice`, `OwnedHouses += _spotname`,
`SaveProfile("house bought")`); **A on an owned row twice** sells it back at
`_sellprice` (the last house cannot be sold; if it was the home house the home
moves to another owned one). Verified: ERE_B_3336I, "Cala Moli residence"
bought for $283,300, `houses_owned = EPH_L2E_0345I` written to profile.sav
(docs/images/estate-menu-s46.webp, estate-buy-s46.png).

profile.sav keys (player_profile.cpp, the `cars_owned` / `car_paints` patterns):
`houses_owned = <spotname>,...`, `home_house = <spotname>`.

## 5. Walking into a house [DATA + INF]

The house banks (`pool_interior_spec.md 4.1`): spot `EPH_<level>[_<id>]` →
**`interior/lph_<level>.bnk`** (living, placed in the **house spot's frame** —
its nodes stand at y 31..36 m) + **`interior/gph_<level>.bnk`** (garage, its
own frame). `HouseBankFor` builds both from the (possibly 8-char truncated)
spot name and probes the file. All the banks checked share the layout:
Entity__FirstPerson { StartCamera `cam_entree` }; the living banks' FpSelectables
`ExitDoor` (0), `Media` (57 "td_media"), **`Garage` (5)**, `Diary` (9),
`Dressing` (10) (+ `PlugGame` in l3di); the garage banks' `Living` (6),
`ExitDoor` (0), `Billboard` (14) and an **Entity__ShopData `D_GarageData`**
whose PosCams (`D_CAR_01..`) are the car places.

The flow (`InteriorWalk::enterHouse`, the shops' `loadRoom` shared):
- living: `Setup{ Bank = interior/lph_x.bnk, HouseSpot = <full name> (the
  house frame), bWorld = false }`; the intro is `C_INTRO` (`C_FIRST_INTRO_I`
  in the studio), the loops `C_LIFE*` (not `C_MYLIFE`, a movie);
- the **Garage** selectable loads the garage room and places the owned cars on
  its ShopData slots (`placeDisplayCars(profile.OwnedCars)`);
- the **Living** selectable comes back through the garage door — the walk
  starts at the living bank's `cam_entree_garage` (the FpSelectable's
  CameraFPEntryPoint in every living bank checked) instead of the front door;
- Diary / Dressing / Media / Billboard say "not wired yet"; **B or the exit
  door** puts you back in the car.
- `-housegarage`: straight into the garage (test hook).

Verified on `EPH_L3DI_5067I`: living room walked (13 furniture slots placed,
docs/images/house-living-s46.webp), garage with the 3 owned cars on its slots
(docs/images/house-garage-s46.webp), A on 'living' → `interior/lph_l3di.bnk`
loaded again (log). The living→garage aim is the standard walk selection at a
counter-height volume (same law as the shops; not scripted).

**Owned only**: the Enter tile shows at a house only when `IsHouseOwned(spotId)`
(`findNearestSpot`'s filter now receives the record id); an unowned house says
"This house is for sale. The realtor sells it." (`-nointeriors`: the old tile
law, the hint still applies).

### 5.1 Walking out of the house (session 71)

The living room loads in its island now (`enterHouse` takes the island): the
retail house presentation — the spot exterior bank `islands/<island>/exterior/
eph_<level>.bnk` draws the shell around the room (the caravan body, the deck,
the garage building, the plot; missing file → the bare room, warned) and the
island streams with the `bWorld` law (`setup.bWorld = true`, the party-house
setup: per-frame streaming around the camera, the ocean on). That is the world
out of the windows (the story house's glass wall shows the terrace pool, the
sea; docs/images/house_story_glass_s71.webp) and it is what makes the plot
walkable: the Caravan's (EPH_L0C_1254I, the DBHouse row localized "Caravan")
living bank carries its `Garage` FpSelectable **14 m out across the yard** —
outside the room's .shk floors, so the walk froze at the front door before
(walk_mode.cpp `FindHavokFloor` failing → `return from`).

The walk crosses anyway now: `WalkMode::setWorldFloorHook` — where the room's
.shk has no floor (the yard), the ground is the streamed island through
`IntroCinematic::getSurfaceHeight` (terrain + roads + the exterior bank's own
floors — the deck), scene feet in, island query, the rotation maps straight
down (Y only); the same step (0.4 m) / drop (2 m) bounds as the .shk search,
false → the freeze law stands (open sea). The hook is set only for the
streamed living room; the garage room keeps its closed frame at the island
origin, and the shops keep the session-51 `bStreamWorld` law untouched.
`-nointeriorworld` puts the old closed-room presentation back (A/B).

The Caravan loop, verified headless (docs/images/house_caravan_yard_garage_s71.webp,
house_caravan_backyard_s71.png, house_caravan_garage_cars_s71.png): through the
front door (the doorway gap at the wall plane x 3.5 of lph_l0c's .shk — 0.85 m,
the straight retail start pose cam_entree yaw -92.3° already aims through it),
across the yard on the island ground, the `Garage` volume selected at the
garage door (the retail prompt), A → `interior/gph_l0c.bnk` with the owned cars
on the D_CAR slots (Enzo / R8 Spyder / Q7), the garage's `Living` volume walks
back — the living room reloads at its `cam_entree_garage` **in the yard**
(previously a floor-less freeze), looking at the caravan. The exit door still
puts you back in the car. The A that swaps the rooms is consumed for the same
tick (`InteriorWalk::tookRoomSwap`): the garage fuel pump no longer opens on
the very press that loaded the room.

### 5.2 The yard collision (session 71, the owner's follow-up)

The yard is not just floor: the walk hook now pushes the body out of the
world's obstacles (`WalkMode::setWorldHook`, the hook signature grew the body +
its previous centre). Two sources, both island space:

- **The exterior bank's own triangles** — the models `setKeepCollision(true)`
  already keep a CPU copy for the height queries (`collectExteriorCollisionTriangles`
  gathers the `setIsExterior` sets' copies; the spot frame = island space).
  Caravan: 6 421 triangles — the caravan shell, the garage building, the plot
  walls. Resolved with `ResolveSphereHavokCollision`.
- **The sector statics around the spot** — `VehicleWorldCollision`
  (`setIsland` + `setHeightFunction` = the carve-aware `getGroundHeight` +
  `streamAround(spot, 1)`), resolved with `sphereContact` (cell-indexed, the
  previous-centre law, the terrain push included). Caravan: 44 360 sector
  triangles; the story house 108 592.

The same step-height law holds without any road filter: the body sphere's
bottom rides kStepUp (0.4 m) above the feet, so every surface within the step
height (roads, kerbs, the deck) is floor and never touches the sphere; above
it is a wall. Verified headless: the walk into the garage building freezes at
the shell's inner wall (house_caravan_garage_shell_block_s71.webp), the walk to
the campsite's exit freezes at the barrier gate / kerb line
(house_caravan_site_barrier_s71.webp), the full Caravan loop still passes (no
false blocks on the door → yard → garage path), the story house walk is
unchanged (13 furniture slots, the glass view), the 700-frame drive exit 0
(0 stalls), update avg 1.4 ms in the walk.

Test hook: `-walkat x,y,z,yawDeg` (scene-local walk start) applies to the
**first** walk of the run only — the later room swaps keep their own cameras
(pinning the garage's cam_entree to the living room's coordinates froze it:
the garage floors do not extend there).

### Furniture [DATA]

The living banks carry `Entity__MyHouseObject` slots (`__Name`,
`orient_pos_scale`, `Type`/`Group`/`DefaultColor`): the model is the myhouse
bank of the same name — `sofaL_E` → `interior/myhouse/sofal_e.bnk`; suffixed
slot names (`ChaiSalD_04`) use the base bank (`chaisald.bnk`) [DATA: the
folder]. Placed like an Entity__Actor prop (`sceneOrigin · entityTRS ·
rootLocal⁻¹`, pool_interior_spec.md 4.3); a cinematic's `.sce` actor still
animates the slot by its entity hashcode. lph_l3di: 13 slots placed.
`-nofurniture` turns it off. The colour variants (`DefaultColor`, the
`_MHA_/_MHB_` materials) are not applied yet.

## 6. The home house and the spawn [INF]

`PlayerProfile::HomeHouse` (default the story house) is the home: `-home`
spawns FREERIDE at its spots.bin record's **second point / heading2** — the
property's road-side entrance (the same field the Solar Crown target uses,
pool_interior_spec.md 4.1) instead of playing the story wake-up. Selling the
home house moves the home to another owned house.

## 7. The tuning and paint shops (the other data-backed menus)

- **ETUN_\*** (`interior/itun_a.bnk`, sellers Type 23 "npc_tuner"): the three
  tuning levels of the car you drove in with (accel / braking / speed, 0..3 —
  the DBRewardCar range), $12,000 × the next level [INF: the pricing table
  DBStickersPricing names no tuning actions — `-logshops` dumps it]. Stored
  per owned car (`OwnedCarTuning`, sav `car_tuning = <id>:<a>,<b>,<s>,...`).
  **The levels drive the physics now** (`physics/vehicle_data.cpp
  ApplyTuningToSetup`, applied by `GMFreeRide::setupVehicle` at spawn and
  `GMFreeRide::applyTuning` when the player gets back in the car after a
  purchase; the base `VehicleSetup` member stays untuned so a re-apply cannot
  compound). Retail's tuning tables are encrypted, so the mapping is [INF] and
  was chosen by measuring `-vehtest` on the California: **accel** scales the
  engine torque curve (+8 %/level), **braking** the tyres' braking friction
  (+10 %/level — the brakes' own torque is ABS-capped, raising it only locked
  the wheels sooner: 100-0 got *worse*, 34.4 → 40.6 m), **speed** the engine's
  top-end torque (+10 % above 70 % of the rev range, with the matching RPM band
  — the top speed is torque-limited, not rev-limited: a taller final drive or a
  higher limiter alone changed nothing). Measured (level 3 each): 0-100 4.52 →
  3.27 s, 402 m 12.60 → 10.93 s, top 309.7 → 337.6 km/h, 100-0 34.4 → 31.9 m.
  Verified bought earlier: two accel levels ($12k, $24k), saved
  (docs/images/tuning-menu-s46.webp).
- **ESTICK_\*** (`interior/istick_b.bnk`, seller Type 28 "npc_stickers"): the
  car's colour catalogue (its DBCarsAndColors rows — the dealer colour
  picker's data, spot_interiors_spec.md "The colour picker") repainting the
  car you drove in with, live (`CinematicCar::applyDatabasePaint`), for the
  row's price, saved to `OwnedCarPaints` like a dealer order. California:
  23 rows. Verified: 'Rosso Scuderia' applied and saved
  (docs/images/paint-menu-s46.webp). The decal banks (DBStickersBanks, logged
  under `-logshops`) are not applied — applying decals to a model is its own
  feature.

## 8. Not done / next

- The purchase cinematics (`C_BUY_HOUSE`, `C_BUY_PART`, `C_NEW_CAR_DESIGN_I`)
  and the retail estate / tuning / sticker movies are not driven (the native
  panels stand in, as at the dealers).
- Club houses (`ERE_CLUBS`, the `ECH_*` spots) are in DBHouse but not buyable.
- Car storage per house (the retail garages hold the cars; the port's cars are
  global), `Entity__MyHouseObject` colours, the retail tuning *tables* (the
  physics mapping above is [INF]), decal banks, the hotel (`E_HOTELA`,
  category 0x19).
- The entry is still the retail fade into the interior (the walk starts
  inside). The yard collision covers the exterior bank + the 3x3 sectors
  (5.2); outside that ring (farther into the campsite) the ground continues
  and only the loaded ring's statics block. The garage building's door
  opening has no door collision (the shell's modelled opening): the walk can
  enter the empty shell — the `Garage` volume at the threshold is the room
  swap. The enter hitch grew once more (the collision streamAround parses the
  9 sector banks synchronously); spreading it like the drive's streaming
  would hide it.

## 9. The caravan report (owner, 2026-11-03): no outside, no walk to the garage, the entry shot repeats

> *"the caravan is in it's 1st house you get given but when inside the exterior didn't seem
> to be loaded and can't walk to the garage outside from it also everytime you enter it
> seems to replay the caravan scene it's ok if this can't be fixed due to lack of visión
> however"*

The house is `EPH_L0C_1254I` (DBHouse "Caravan", granted at story step 10, PROGRESS.md) and
its rooms are `interior/lph_l0c.bnk` / `interior/gph_l0c.bnk`. Three separate causes:

### 9.1 The entry shot repeated on every visit — FIXED (build 408)

`InteriorWalk::loadRoom` played the room's entry cinematic every time the room loaded
(`scene.playMain( introName )`, interior_walk.cpp:264) — for the caravan
**`C_FIRST_INTRO_I`** (the bank also has `C_INTRO`, but `C_FIRST_INTRO_I` comes first in the
bank's cinematic list and the name scan takes it). It now plays **once per house and
profile**: the key is the room bank's name (`lph_l0c`, `gph_l0c`), stored in
`PlayerProfile::SeenHouseIntros` and saved as `house_intros_seen = lph_l0c` beside
`houses_owned`. Two houses of one level share the bank, so they share the intro (as they
share the room). `-replayhouseintro` forces one, `-nointeriorintro` still disables all;
shop interiors keep replaying theirs unchanged.

Verified (build 408, `-storystep 10 -home`, in and out twice):

    InteriorWalk: 'EPH_L0C_1254I' (interior/lph_l0c.bnk): cinematics … C_INTRO
    Profile: saved '…\profile.sav' (house intro seen)          <- first entry
    InteriorWalk: left 'EPH_L0C_1254I'
    InteriorWalk: 'lph_l0c': 'C_FIRST_INTRO_I' already played - straight to the walk
                                                              (-replayhouseintro replays it)

and `profile.sav` carries `house_intros_seen = lph_l0c`.

### 9.2 No exterior while inside — FIXED as the default (glm session 71; the opt-in came first)

`InteriorWalk::enterHouse` called `loadRoom( …, island )` **without** the island, so
`setup.bStreamWorld` was false and the room was drawn as a closed box — while the *shop*
path (`enter( bank, island )`, findSpotInterior call) always streams the world around the
room. Cline session 84 first gated the stream behind `-houseworld` (off by default);
glm session 71 then verified the streamed living room end to end (the exterior bank
`eph_<level>.bnk` draws the shell, the windows show the world, the yard walks to the
Garage volume — 5.1) and made it **the default for living rooms**: the flag is gone,
`-nointeriorworld` restores the closed-room A/B. The garage room stays its own space.

### 9.3 Can't walk out to the garage — DELIVERED (glm session 71 + 71b; the room swap stays)

The living room's FpSelectables are `ExitDoor` (type 0, "exit"), `Garage` (type 5),
`Diary` (9), `Dressing` (10), `Media` (57 `td_media`) — logged by `loadRoom`. A on
`Garage` loads `gph_l0c` (a separate bank: x -23..14, y 0..4, its own space) and A on its
`Living` volume comes back; both verified in session 82. The "no outdoor plot to walk
across" gap (the walk collision was the loaded bank's `.shk` only) is closed since glm
session 71/71b: the streamed living room gets the island floor through
`WalkMode::setWorldFloorHook` (IntroCinematic::getSurfaceHeight where the .shk is silent)
and the obstacles through `setWorldHook` (the sector statics + the exterior bank's own
triangles, 5.1/5.2) — the caravan loop walks out of the front door, across the yard, to
the `Garage` volume at the garage building's threshold (its select sits 14 m out across
the plot), and back. The answer to the old "does `lph_l0c`'s own `.shk` cover a patio"
question is **no** — the world hook is the fix (`docs/images/house_caravan_*_s71.png`).

## 10. The retail house UI: HOME.vmf (session 2026-11-05, `win`)

The owner: *"wire houses wardrobes, garages, tdu life (this allows changing furniture)"*.
The house rooms now load `frontend/hires/home.bnk` (HOME.vmf) instead of the generic
INTRO walk screen (`InteriorWalk::loadRoom`, `bHomeUi`; `-nohomeui` = the A/B).
Every FSCommand below was **read at run time** (`InteriorWalk: FSCommand ...` lines) - the
names are built by the movie's `sendCmd` helper, so the static `-dumpbuttons` string dump
(which now also covers the UI viewer's movie and any movie loaded after start) does not
show them [DATA].

| Where | Label / command | Meaning | Port |
|---|---|---|---|
| walk, living room | `living_in` -> `liin` | living-room walk screen (the selectables' icons: hanger, book ...) | goto at walk start |
| walk, garage | `garage_in`, `/:InGarage` | the garage walk screen | goto at walk start |
| Dressing door (FpSelectable 10) | `dressing_in` -> `mcou`, `came`, `drin` | **Walk-in closet** page: Hats / glasses / upper garments / trousers / shoes / gloves | opened |
| closet category | `clin` | a category opened; the movie set `/:BtValid` to its key (`hats`, `tops` ...) and reads `/:Nb_Items`, `/:logo_Brand`, `/:ItemName` | strip = the free "No ..." row + the owned DBClothe rows of that `_parttype` and sex [INF: retail content ENC] |
| closet strip | `neit` / `prit` | next / previous item | index |
| closet strip OK | `seit` (+ `came`, `drin`) | wear the item | `PlayerProfile::WornClothes[_parttype] = _id_num`, saved `worn_clothes` |
| closet root Back | `sndp:back`, `avou`, `liin` | out of the closet | walk resumes |
| Diary (FpSelectable 9) | `diary_in` -> `camn` | **Info** menu: MyTDULife / MyTDUHouse / My Photos / Leaderboards | opened |
| Info -> MyTDUHouse | `myhs:open`, `wmon`, `camf` | the **MyTDUHouse** walk mode (header + "Quit MyTDUHouse") | walk resumes in that mode |
| MyTDUHouse Back | `dbxo` ... `dbxc`, `myhs:clos`, `liin` | "Quit MyTDUHouse?" dialog (GENERAL), then out | GENERAL dialog relay (the shop's dbxo/dbxc law) |

`_parttype` of the closet keys: hats 5 (helmet), glasses 2, tops 7, trousers 1, shoes 6,
gloves 4 [INF: matched on the key text; the slot pairs are FUN_00cb2f30's, skin_zones_spec 2.3].
**Worn clothes on the avatar**: `EdenAvatar::load` replaces a slot's DBPNJ row by the worn
DBClothe row when the config is the player's (`Avatars: <cfg> wears closet item ...`;
`-noworn` = the A/B). Verified: the Cowboy hat picked in the closet is on the player who
walks into the Ferrari dealer (`docs/images/house_closet_hat_worn_in_shop.webp`).

**Garage**: within 3 m of an owned car's slot, A makes it `PlayerProfile::DrivenCarId`
(saved `driven_car`); leaving the house after a change reloads FREERIDE at the house's
road-side entrance in that car (FREERIDE loads the profile's car at enter - no runtime model
swap). Elsewhere in the garage the pump prompt stays [INF: HOME's own `/garage` page -
`change_place`, `list`, "validation de la voiture a changer" - is not decoded].
`docs/images/house_garage_pick_car.webp`.

**Not done**: the closet's camera on the avatar (`came` is ignored, the walk stays first
person, so the change is seen in the next scene that shows the player); the brand logo frame
names of `/:logo_Brand`; MyTDUHouse's furniture pick / colour change (the
`Entity__MyHouseObject` colours, §Furniture) - the mode opens and closes, nothing is
selectable in it yet; MyTDULife / My Photos / Leaderboards pages (they open, nothing fills
them); the Media (`td_media`) selectable. Test hooks: `-walkaim <selectable name|type|car>`.

## 11. MyTDUHouse: changing the furniture (session 2026-11-05, `win`)

The owner's "tdu life (this allows changing furniture)". In the MyTDUHouse mode (Diary ->
Info -> MyTDUHouse, §10) every `Entity__MyHouseObject` slot of the living room is a walk
selectable (`InteriorWalk::addFurnitureSelectables`: a box over the model bounds, type
string `furniture`; the walk filter keeps them live **only** in MyTDUHouse, the room's own
selectables only outside it) [INF: the retail furniture pick is ENC].

**The catalogue is DBFurniture** (`source/game/spots/house_furniture.cpp`, `-logfurniture`
dumps the fields) [DATA]: 153 rows with `_bank` (interior/myhouse/<bank>.bnk), `_type`,
`_price`, `_level`, `_name_hc` (the retail names: "IMPACT Sofa", "KOPOLA Sofa" ...),
`_iscasino`, `_isdlc`, `_isvalid`. **A slot's Type is the row `_type` minus one** [DATA:
three pairs agree - ChaiSalD 1 / chaisal* 2, sofaL 8 / sofal_* 9, TabRecE 11 / tabrec* 12],
so a slot's choices are the non-casino rows of `_type = Type + 1`.

The edit: A on a piece -> **Left / Right** swaps the slot's model live through the rows
(`IntroCinematic::replaceFurniture`; the replaced model is *retired*, not freed, until the
room unloads - a model freed while a frame uses it hangs the device, CLAUDE.md trap 13),
the hint shows name, n/N and price; **A** buys it for its `_price` [INF: the retail price /
`_level` gates are ENC] and saves `furniture_choices = <house>:<slot>=<bank>;`; **B** puts
the old model back. `loadFurniture` applies the saved choice on every load of that house
(`IntroCinematic::setFurnitureChoices`). Verified on `EPH_L3DI_5067I`: sofaL_A IMPACT (1/13)
-> KOPOLA (2/13) -> SOGUT (3/13) -> bought, $500,000 -> $480,000, and the next visit loads
`sofal_e` (`docs/images/house_mytduhouse_furniture_swap.webp`). Test hook: `-furnaim <slot>`.

**Not done**: the colour variants inside a model (`DefaultColor`, the `_MHA_/_MHB_` materials),
the walls / floors (`MAT_WALL1_`, `MAT_FLOOR1_`, the `ground_01..25` banks), the `_level`
gate, HOME's own MyTDUHouse item menu (the hint box stands in), the garage room's slots.

## 12. MyTDUHouse: the colours and the sets (session 2026-11-05, `win`)

**The colours are materials of the model's own library** [DATA, `-logfurniture` lists them]:
every myhouse bank carries `_MHA_01..05` (and `_MHB_01..05`, `_MHC_01..05` on models with more
colour parts - sofa1p_c: A + B, chaibard: A only); the geometry draws the `_01` ones. A colour
variant *n* (0..4) re-points every item drawn with `_MH<part>_<nn>` at `_MH<part>_<n+1>` of the
same library, all parts at once (`EdenModel::setMyHouseColor`; the draws read the item's
material index each frame, nothing is rebuilt) [INF: one index for every part - the retail
per-part choice, if any, is ENC]. The slot's `DefaultColor` is that index (0 on every slot of
lph_l3di) [DATA]. Verified: IMPACT sofa 1/5 cream -> 2/5 brown -> 3/5 navy, the cushions follow
on their B part (`docs/images/house_mytduhouse_colours.webp`).

**The sets are `Group`** [DATA: lph_l3di's six dining chairs are group 2, the three bar stools
group 3, the single pieces 0 or 1]: editing a piece of a Group > 0 changes every slot of that
group and type together [INF], the price is per piece (`docs/images/house_mytduhouse_group_barstools.webp`:
CONTEXT -> BUBBLE x3, $600).

The edit: **Left / Right** model, **Up / Down** colour, **A** buy (a new model: `_price` per
piece) or apply (a colour of the model you have: free [INF]), **B** reverts every slot.
Saved `furniture_choices = <house>:<slot>=<bank>#<colour>;` and applied by `loadFurniture`.
Also: a suffixed slot (`ChaiSalD_04`) now reports its real bank (`chaisald`), so "yours" is
right for it.

**Not done**: walls / floors (`MAT_WALL1_`, `MAT_FLOOR1_`, the `ground_01..25` banks), the
`_level` gate, HOME's own item menu, the garage room's slots.

## 13. MyTDUHouse: the walls and the floor (session 2026-11-05, `win`)

**The catalogue is DBMaterial** [DATA, `-logfurniture` lists the tables]: 61 rows,
`_materialname` = an interior/myhouse bank - `ground_01..25` with `_type` 1 (floors), `wall_01..20`
with `_type` 2 (walls) - `_price`, `_level`, `_name_hc` ("Light Marble Tiling", "Massaranduba
Parquet Floor", "Pearl-grey Paint", "Titanium-grey Concrete" ...). (DBFurniture has none of them.)
A finish bank is a quad **Scenery** entry (`ArrayOf_Scenery[0].Model`, not an Entity__Actor) whose
one material is the finish (`_Ground_09` "PARKET") with its own textures [DATA].

**The surfaces**: the room set materials named `Mat_Floor1_*` / `Mat_Wall1_*` (lph_l3di:
`Mat_Floor1_09`, `Mat_Wall1_38`; WALL / WALL_RED / WhiteWall_01 are not paintable) [DATA].
`IntroCinematic::applyRoomMaterial` copies the finish's textures into the room material **slot
by slot type**, keeping the room's shader, parameters and lightmap. The slot type names are packed
and the lightmap slot reads "...LIG"; copying it (or the whole finish material) put the finish's
white FAKE_LM on the floor and drew it flat white - that was the first try. The authored
materials are kept for B / "the house's own".

The pick: in MyTDUHouse, A with no furniture under the reticle edits the **floor** when looking
down (forward.y < -0.45), else the **walls** [INF: the retail pick is ENC]; Left / Right go
through "the house's own" + the DBMaterial rows of that type, A buys for `_price` [INF], B
reverts. Saved `furniture_choices = <house>:#floor=<bank>;<house>:#wall=<bank>;`, applied when
the living room loads. Verified on lph_l3di: floor own -> Light Marble Tiling -> Massaranduba
Parquet (`docs/images/house_mytduhouse_floor.webp`); walls Tuscany-red -> Titanium-grey Concrete
-> Yellow-saffron Plaster (`house_mytduhouse_walls.png`, 7.9-8.7 % of the frame changed). Test
hook `-surfaceedit floor|wall`.

**Not done**: the `_level` gate, other houses' wall / floor material names (only lph_l3di was
read - a house whose surfaces are not `Mat_Floor*` / `Mat_Wall*` changes nothing, the log
says "0 material(s)"), HOME's own item menu.

### 13.1 Every house: the surface names (session 2026-11-05, `win`)

The 21 living banks' material libraries, scanned for the paintable surfaces [DATA]:

| bank | floor | wall |
|---|---|---|
| lph_l0a | MAT_Floor1_03 | MAT_Wall1_28 |
| lph_l0c (caravan) | Mat_Floor1_42 | **none** (the walls do not change) |
| lph_l1a | Mat_Floor1_14 | Mat_Wall1_29 |
| lph_l1b / l1bi | MAT_Floor1_15 | MAT_Wall1_30 |
| lph_l1c | Mat_Floor1_16 | Mat_Wall1_31 |
| lph_l1d | MAT_Floor1_18 | MAT_Wall1_32 |
| lph_l2b | Mat_Floor1_20 | Mat_Wall1_44 |
| lph_l2c | MAT_Floor1_17 | MAT_Wall1_34 |
| lph_l2d / l2f | MAT_Floor1_24 | MAT_Wall1_36 |
| lph_l2e | MAT_Floor1_11 + **MAT_Floor2_23** | MAT_Wall1_12 |
| lph_l3a / l3a2 | Mat_Floor1_13 | Mat_Wall1_38 |
| lph_l3c | Mat_Floor1_02 | Mat_Wall1_38 |
| lph_l3d / l3di | Mat_Floor1_09 | Mat_Wall1_38 |
| lph_l3e | MAT_Floor1_25 | MAT_Wall1_39 |
| lph_l4b | MAT_Floor1_22 | MAT_Wall1_40 |
| lph_l4f | Mat_Floor1_1 | Mat_Wall1_05 |
| lph_l5y | Mat_Floor1_21 | MAT_WALL1_41 |

So the law is `mat_floor<zone>_<nn>` / `mat_wall<zone>_<nn>`, case-insensitive; the match now
requires the zone digit + `_` + digits, because lph_l1bi also has an unpaintable `MAT_FLOOR_03`.
A second zone (l2e's `MAT_Floor2_23`) takes the same finish [INF]. Verified by `-enterhouse
<spot>` (new test hook: straight into any house's living room) + `-surfaceedit`: EPH_L2E_0345I
floor 2 materials / wall 1, EPH_L1D_1455I 1 / 1, EPH_L0C_1254I 2 / 0
(`docs/images/house_mytduhouse_floor_other_houses.webp`: l1d wood -> marble, l2e slate -> marble).

## 14. MyTDULife: the My Photos page (session 2026-11-18, `main`, 49)

The Diary's Info menu (`diary_in`) is four buttons: MyTDULife (`mylf:self` -> `mylf:wait`, a Back-only
"connecting" screen: online data), MyTDUHouse (section 10), **My Photos** (`phsp:phin` -> `phpa`) and
Leaderboards (Instant / Multiplayer challenges). My Photos is a book `/diary/photo_book_content/book/photo1..4`
(the movie's own `path_photo` variable) [DATA, `-dumphomeclips phph`]. Each slot has a `lure` placeholder
shape, a caption variable `photo_name` (the text field `../../:photo_name` of `ph_name_scroll`) and the
pager reads `/:num_page` / `/:nb_pages`. FSCommands [DATA, run time]: `phpa` book opened, `phph` focus moved,
`phsp:phcl` closed, `sndp:wrong` on Zoom with no photo; PhNextPage / PhPrevPage page the movie's own counter.
The game fills it: `InteriorWalk::updateAlbum` lists `<userdata>/photos/photo_*.png` (the photo camera's
output, photo_camera_spec 2), binds the PNG with `setBitmapOverride(photoN/lure)`, sets the captions and
the pager; the page follows `/:num_page`. Verified: 6 photos = 2 pages, page 2 = photos 5-6 plus two empty
slots (`docs/images/house_my_photos_album.webp`, `house_my_photos_page2.webp`). New tools:
`-dumplabelslate` (frame labels of movies a mode loads later, e.g. home), `-dumphomeclips <fscommand>`
(+ `-dumphomedepth`) = the clip tree at that FSCommand.

**Not done**: Zoom (A: `PhotoZoomed`, needs the zoom clip's own image), Delete (Y), the photo's real
caption / date; MyTDULife (offline local stats page - protocol `mylf:*` not decoded); Leaderboards
(the two lists open, nothing fills them); the Media selectable (`td_media`: HOME.vmf has no media
label, so TDU Media is another movie - not found yet); the closet's avatar camera.

### 14.1 Zoom, Delete, and the offline MyTDULife (session 2026-11-18, `main`, 50)

**Zoom / Delete.** The slots' states are the photo-spot collection's (`to_take` / `lost` / `taken`), so the
movie answers A on a user photo with `sndp:wrong`; forcing the slot to `taken` broke its art (tried, reverted).
The game answers instead: `sndp:wrong` while the album is open zooms the slot under the focus (`IsOn`) as a
native full-screen frame, Left / Right browse, A / B close; **Y** (`IA_MenuOption1`) arms a delete (hint "Press Y
again"), a second Y moves the PNG to `photos/deleted/` (never erased) [INF]. `docs/images/house_my_photos_zoom.webp`.

**MyTDULife.** `mylf:self` -> `mylf:wait`; `mylf_book` (640,240) is only a frame with a "loading" text - its content
is the online profile [DATA: clip dump]. `mylf:clos` = closed. Offline the game draws a panel over it (`drawLifePanel`):
name, global level + title (sum of the four skill levels, pause_menu's law), money, odometers, the four skills,
cars / houses / events / photo spots [INF: our composition]. `docs/images/house_mytdulife_offline.webp`.

## 15. TDU Media: the house TV (session 2026-11-18, `main`, 51)

**Found:** `frontend/hires/tdmedia.bnk` = `TDMEDIA.vmf`, the "Test Drive Media" TV (stage 1280x960, labels `media_in`,
`in_channel`, `open`, `stop`, `close`; music `sound/musics/tdumedia.wav`). It is what the `td_media` FpSelectable
(the TV in the living room, type 57) opens [DATA; INF: the link by the name]. Inspect it with
`-uiviewer -movie tdmedia -goto "/:media_in"`.
It is a **second movie** next to HOME: `InteriorWalk::openMedia` loads it into its own `FlashPlayer` (own tick /
draw / key routing, `-dumpmedia <depth>` dumps its clips), sets `/:db_tdmedia = 1` before
`ApplyFlashLocalization` (the string category flag; without it the texts read `]r_Currently`).
Four channels (tabs): Weather, News, DLC and a fourth. FSCommands [DATA, run time]: `sndp:mdintro`, `sbdd`,
`sndp:mdmopen` / `mdvalid` / `mdback`, `tdmd:weat:open` / `tdmd:weat:clos` (a channel), `tdmd:clos` (the TV closes ->
the game unloads the player and gives the walk back).
**Weather channel:** six tiles `/weather/<ibiza|hawaii><0..2>` = now / +2 h / +4 h game time, each an `island`
clip (`d_`/`n_` + `ibiza`|`hawaii`) holding a `weather` icon clip (`d_`/`n_` + `clear`|`cloudy`|`rainy`|`stormy`).
The game sets the labels (`updateMediaWeather`, `-logmedia`): the island outline, and day / night from the
world clock at each step; the icon is always *clear* because `WeatherCycle` is random and lives in FREERIDE, not in
the house [INF]. Verified: `-enterhouse EPH_L3DI_5067I -walkaim td_media` + A -> Down -> A: the Weather
Channel with both outlines, sun / sun / moon (`docs/images/house_tdu_media_weather.webp`); B backs out of a channel,
B again closes the TV and the walk resumes.

**Not done:** real forecast icons (needs the weather schedule readable outside FREERIDE), the News and DLC channels
(online feed: the movie shows its own "no news available" text offline), the other tabs' content, the TV music.

## 16. Closet avatar camera, live clothes, Leaderboards (session 2026-11-18, `main`, 52)

**Closet camera.** HOME.vmf sends `came` right after `mcou` when the walk-in closet opens (and again per category and
on a pick) [DATA, run time]. The game answers the first one with the walk's own third-person self view
(`WalkMode::startSelfView( scene, bFront )`): the camera is probed 2.2 m away on the roomiest of four sides, in 0.25 m
steps through the walk collision (`WalkController::sweep`; one long jump hops over thin walls - that was the
first try: the camera ended in the next room), and the avatar turns to face it. Ended by `avou` / `liin`.
`docs/images/house_closet_avatar_camera.webp`. **Live clothes**: a pick (`seit`) saves, then
`IntroCinematic::refreshPlayerAvatar` rebuilds the avatar from the new worn set; the old avatar is retired for 8
ticks, never freed under a frame in flight (trap 13). 0 VUIDs with `-vk_enable_validation_layer`.
Not done: the per-category zoom (the retail `came` after `clin` moves to the head / feet - the "Move head" help
shows; ours stays full body).

**Leaderboards.** `bt_3` (`where_to rankings`) opens two entries; each validates with `/diary/menu:wh_val`
`multi_ice` (Instant challenges, the `ice` string category) or another `multi_*` (Multiplayer), and the movie
sits on "Connecting to server" + Cancel - the online ranking, as retail does offline [DATA]. After 2 s the game
draws a panel over it: Instant = the player's own best times (`PlayerProfile::PassedTests`: licence tests,
championship races), Multiplayer = "needs the Eden servers" [INF]. `-logdiary` logs the validate variables.
`docs/images/house_leaderboards_local.webp`.

### 15.1 TDU Media: music, a real forecast, News and DLC channels (session 2026-11-18, `main`, 53)

**Music.** Opening the TV plays the `TDMEDIA` track of fe_snd.bnk (`tdumedia.wav`) through `FrontEndMusic`; closing
brings the room's track back [DATA + INF].

**Forecast.** The weather is now a **schedule**, not a roll, on the retail clock: `WeatherCycle::ScheduledIntempery(island,
block)` (block = 20 game minutes) walks the state tables from a dry state 16 game hours back with a hash of
(island, block) - a pure function of the wall clock like the clock itself, so Ibiza and Hawai differ and every
machine agrees. FREERIDE follows it on the retail clock (`setIsland`; the rolled walk stays for `-clock*` test
clocks) and the TV forecasts it: now / +2 h / +4 h icons clear / cloudy (1, 2, 4) / rainy (3, 5) / stormy (6) with the
hour's day / night [INF: the transition weights and the 3 / 5 / 6 split are ours]. Verified: the TV showed rain now
on both islands and FREERIDE then started on `intempery 3` (`docs/images/house_tdu_media_forecast.webp`).

**Channels.** `TDMedia.vmf` (disassembled with `tools/vmf_disasm.py` after `otdu2_bnk_tool extract`): tiles by flag -
weather and news always, `/:IsDLCChannel` shopping, `/:IsDLCHsys` the Exploration-pack help (not offered: the pack is
not installed); news tabs by `/:IsNews<Tutorial|GameProgress|Community|Club|DLC|GM|Vehicles|Avatar|Furnitures|Stickers>`.
The list engine (`ASFlashList`) is native and reduced to a cursor here, so the game drives the clip
`/lists/news_list` (the one clip serves both channels; `list_name` = `news_<tab>` / `dlc_<tab>`): 6 `lineN` clips whose text
fields read `../../../:news_title` = a variable **of the line clip** (probed: the content clip does not work) and
`news_date`, `SetLineState( n, normal|empty )`, the cursor via `setListSelection`, the details pane from `/:NewsTitle` /
`/:NewsDescription` / `/:CurrentNewsIndex` / `/:NbNews`. Up / Down move the game's cursor and scroll; Left / Right call
`/lists:changeTab( left|right )`; A ticks (`tdmd:news:<tab>:slct`), Y deletes the ticked rows (this session).
- **News** (offline, our content from the profile): Game Progress (level, money, cars, houses, events), Vehicles (owned
  cars), Avatar (owned clothes), Stickers (owned packs); the other tabs are empty (the retail feed is the server's).
  `docs/images/house_tdu_media_news.webp`.
- **TDU Shopping (DLC)**: the data's own DLC catalogue - 15 cars (`DBCarPhysicsData._isdlc`) and 24 clothes
  (`DBClothe._dlc`); Buy answers with the movie's `m_DlcUnavailable_` text in a toast (the store is gone).
  `docs/images/house_tdu_media_dlc.webp`. `-probedlc` counts the flagged rows of the tables.
Flags: `-dumpmediacmd <fscommand>` / `-dumpmediadepth` (the clip tree at an FSCommand).
**Not done:** Packs / Furniture / Stickers DLC tabs (no flagged rows in the tables read), the empty-list text
(`emptyListText` does not show), the Hsys help channel, a stormy icon check (state 6 not seen in a run).

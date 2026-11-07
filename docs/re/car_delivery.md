# The dealership's car delivery

Tags as in `CLAUDE.md`: `[CODE]` readable executable · `[DATA]` game files ·
`[INF]` inference.

Buying a car at a dealership: the cashier hands the keys over and the car is
parked **outside**. The port's `DealerMenu` buys with the profile's money
(`docs/re/spot_interiors_spec.md` §3), and this is what happens next.

## Where the car goes `[DATA]`

Every shop's **exterior** bank (`islands/<island>/exterior/<bank>.bnk`) carries
`Entity__CarPos` entities: `__Name`, `orient_pos_scale` (the placement in the
bank's frame), `PlayerIndex`, `CarPosType` (the `otdu2_bnk_tool xmb` schema of
`ecd_ferr.bnk`). The exterior banks are placed at the spot frame
(`T(position) R_y(heading)`, `spot_exteriors.cpp`), so a local placement is
transformed by it (`source/game/freeride/car_delivery.{h,cpp}`).

`[MEASURED]` on Ibiza (`-carpos ECD_FERR,ECD_AUDI,...`):

| spot | car positions |
|---|---|
| `ECD_FERR` | 1: `PosCarParking`, type **4**, player **1** — the player's own parking |
| `ECD_AUDI` | 5: `PosCarParking` (type 4, player 1) + `PosCarTraffic`/`_04`/`_05`/`_06` (type 5, player **2001**) |
| `ECD_US1`, `ECD_ITAL`, `ECDC_ITA`, `ECD_MERC`, `ECD_BRI1`, `ECD_ASIA` | only `PosCarTraffic*` (type 5, player 2001) and, for `ECD_MERC`, `BrandCar`/`BrandCar_01` (the brand's display car) |

So the retail marks the player's own spot with **type 4 / `PlayerIndex` 1**, and
the shop's traffic and brand cars with type 5. The port picks, in order: a type 4
entry, an entry named `*Parking*`, else the first one — and the **spot's own
frame** when the bank has no `Entity__CarPos` at all (the car still appears
outside rather than nowhere).

## What the port does

- `DealerMenu` records the purchase (`takeBoughtCar`); `GMFreeRide` delivers it
  when the menu closes (`deliverBoughtCar`): the pose comes from the shop's
  `Entity__CarPos` (the shop is the `DBCarShop._spot_name` the player entered,
  or the `-dealer` test flag), the ground under it from the collision ray
  (Haversine first, `EdenWorld::getSurfaceHeight` second - the cheats' teleport
  rule), and the car is put there. `-carpos <spot>[,<spot>]` dumps the car
  positions of a shop.
- **The driven car's model is not swapped by default.** Replacing it at runtime
  (unload + load + `setupVehicle` + the chase camera) wedges the device: the
  game stops responding a few frames later with the CPU idle (a GPU wait),
  reproduced with `-dealer ECD_FERR` and one purchase. `waitIdle()`, dropping the
  shadow caster cache and clearing the ray tracing instances (`clearRtInstances`)
  were all tried and were not enough. `-switchcar` turns the model change on
  (for whoever debugs it); the default puts the car the player already drives on
  the delivery spot, which is stable, so the retail flow "buy, leave, the car is
  outside" holds and nothing hangs.
- `-switchparts model,audio,hud` narrows a hang in the switch (pose only is
  stable, the model is the culprit).

## Not done

- The cashier's own dialog: the retail plays the bank's purchase cinematic
  (`C_BUY_CAR_I`, `CarDealer-I_BuyCar_01`, the hostess's "give the key" FaceFX)
  and the `shopscar.vmf` UI; the port's native `DealerMenu` stands in.
- Driving the new car away (the runtime model swap above).

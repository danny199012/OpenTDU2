# The casino (`source/game/casino/`) — retail contract and the offline port

Written 2026-10-29 (Cline, `cline`). The casino was the one game mode the port
had never started (`docs/SURVEY.md`, `docs/STATE.md` priority 6: *"the offline
casino (`source/game/casino/` + a local chip balance, driving
`Cas_SlotMachine.vmf` through the existing AVM1 runtime)"*). This is the first
increment: the **floor**, the **slot machine** and **roulette**, offline, with a
chip wallet in the profile.

Tags as in `CLAUDE.md`: `[CODE]` readable executable · `[DATA]` game files ·
`[INF]` inferred or chosen, with the reason · `[ENC]` in the SecuROM range.

---

## 1. What the retail casino is — the evidence

| Piece | Evidence |
|---|---|
| Shell movie | `FrontEnd/HiRes/casino.bnk` (4.2 MB) -> **`Casino.vmf`** (550 KB) `[DATA]`. MedRes / LowRes variants exist. |
| Game movies | `cas_slotmachine.bnk` -> `Cas_SlotMachine.vmf` (395 KB), `cas_slotlines.bnk` -> `Cas_SlotLines.vmf`, `cas_slotscreen.bnk` -> `Cas_SlotScreen.vmf` (56 KB), `cas_roulette.bnk` -> `Cas_Roulette.vmf` (395 KB), `cas_poker.bnk` -> `Cas_Poker.vmf` (966 KB) `[DATA: bnk listing]`. |
| Casino shaders | `Euro/Shaders/TD_Casino_scroll.sob`, `TD_ChipsStack.sob`, `TD_Roulette.sob` `[DATA]` — the casino **interior models'** shaders (chips stacks, the roulette wheel mesh, the scrolling displays). |
| Island | `islands/casino/level/casino.bnk`; spots `ECAS` (the resort), `ESHOP_CA`, `ESUR_CA`, `ELANDCA` `[DATA: docs/SURVEY.md]`. The spot icon is **`gamblinghall`** `[CODE: map_screen.cpp case 0x06]`. |
| Arrival / departure | `scenarics/cinematics/casino/spt_casino_arrival.bnk` (10.4 MB), `spt_casino_departure.bnk` (8.8 MB), plus the `spt_ibiza_*` / `spt_hawai_*` variants `[DATA]` — authored cutscenes, not built. |
| Casino class | `CasinoGame` — PS3 `ps3_func 5720848`, PC `pc_addr 15777476` (0xF0B0C4) `[DATA: pc_ps3_anchors.json]`. |
| Per-game statistics saves | `CasinoSlotMachineStatisticsSaveDesc`, `CasinoRouletteStatisticsSaveDesc`, `CasinoPokerStatisticsSaveDesc`, `CasinoBetaStatisticsSave`; the messages `CRC_{A,R}_STATS_{GET,SET}_CASINO_SLOT_1`, `CRC_E_CASINO_BETA_STATISTICS_UPDATE` `[DATA]`. |
| The casino's own save | `CasinoSave`, `CasinoProgressionSave`, `CasinoRankingsSave`, `CasinoRankingsSaveEntry` `[DATA]` — the wallet, the progression and the online rankings are three separate sections. |
| Flash globals | `/:IsCasino`, `/:HasCasino`, `/:IsCasinoStandAlone`, `/:IsBootCasino`, `/:IsCasinoSaveData`, `/:isProgressCasino`, `/:UseCasinoRankingList`, `/:CasinoAvatar`, `/fl_casino/players_list` `[DATA: flash_globals.txt]`. |
| Pause menu | The `IsCasino` branch of `setSquareMenu` is the 6-entry list **resume / game_progress / players / options / profiles / exit_game** — no `photo`, no `gme` `[DATA: docs/STATE.md session 18, retail-pause-casino.png]`. |

### 1.1 Why it needed a server (what we replace)

| Retail online piece | Evidence | Offline replacement |
|---|---|---|
| The casino is **gated / invite only** | `"<INVITE> Invite rejected : Casino locked"`, `"Casino locked (debug text, to be localized)"`, `CASINO_FORBID_SLOT_MACHINE`, `CASINO_FORBID_CLOTHES_SHOP`, `CasinoUnlockable`, `_casinounlockable` `[DATA]` | A local unlockable list in the profile; **empty = playable**. The slot machine is locked only if `CASINO_FORBID_SLOT_MACHINE` is present. |
| Shared jackpot | `C_SLOT_SHARED_JACKPOT_BACKGROUND`, `C_SLOT_ACTIVATE` / `C_SLOT_WIN` / `C_SLOT_LOOSE` / `C_SLOT_MENU` `[DATA]` | The 5-of-a-kind Jackpot symbol pays from the machine's own table. |
| Rankings / leaderboards | `CasinoRankingsSave`, `CasinoRankingsSaveEntry`, `/:UseCasinoRankingList`, `leaderboard_challenge_casino` `[DATA]` | Not implemented (no online). The globals are pushed as `false`. |
| Sessions / reconnection | `Casino Try Reconnection`, `Casino Try Reconnection Tournament`, `/:IsCasinoStandAlone`, `/:IsBootCasino`, `"TDU2 Casino "` (the standalone client's title) `[DATA]` | Single player; `/:IsCasinoStandAlone` is `false` (we are inside the game). |
| Other players' avatars at the tables | `AvatarBhvSlotMachineMan/Woman`, `AvatarBhvRouletteMan/Woman`, `AvatarBhvPokerMan/Woman`, `CASROU_RouletteJoinSelector` / `LeaveSelector` / `NewPlayer(s)` `[DATA]` | Not implemented; the player is alone at the table. |
| The roulette dealer's phase clock | `CASROU_PlaceYourBet`, `CASROU_BetsAreDone`, `CASROU_RienNeVaPlus`, `CASROU_NoTime`, `CASROU_Roulette*Selector` `[DATA]` | The same phases, driven by a timer (`kBetCloseTime` / `kSpinTime` / `kResultTime` in `casino_roulette.cpp`). |

### 1.2 The casino progression that is in the shipped data (not built yet)

`db_data.cpr` carries a full casino meta-game `[DATA: otdu2_bnk_tool xmb dump]`:

| Table | Fields |
|---|---|
| `DBCasinoLevel` | `_axis`, `_level`, `_pointsneeded` |
| `DBCasinoObjectiveDetail` | `_axis`, `_category`, `_filtera/b/c`, `_hashcode`, `_objectivevalue`, `_pointsawarded`, `_subaxis`, `_text_hc` |
| `DBCasinoReward` | `_carid`, `_casinounlockable`, `_cinematicname`, `_cocktailid`, `_hashcode`, `_iseffectinstant`, `_rewardtype`, `_roomlevel`, `_roomtype`, `_timer` |
| `DBCasinoUnlockable` | `_hashcode`, `_unlockabledesc` |
| `DBFurniture` | `_iscasino` (a casino room is *furnished*) |

plus the seen-flags `_hasSeenCasinoProgressionTutorial`,
`_hasSeenCasinoACEStatusCinematic`, `_hasSeenCasinoVIPStatusCinematic`,
`_b_has_seen_casino_session_change_sm_tutorial` /
`..._pok_rou_tutorial` `[DATA]` — i.e. the casino had **levels, objectives,
rewards (cars, cocktails, cinematics, room levels), a VIP / ACE status ladder and
decorable rooms**. That is the natural next increment; the tables are already
decoded (`docs/re/ps3/` catalogs).

---

## 2. The offline design

| Piece | File |
|---|---|
| The game mode (floor + input + rendering) | `source/game/casino/gm_casino.{h,cpp}` — `GMCasino`, `eGameMode::GM_Casino` |
| The slot machine | `source/game/casino/casino_slots.{h,cpp}` — `CasinoSlots` |
| Roulette | `source/game/casino/casino_roulette.{h,cpp}` — `CasinoRoulette` |
| Video poker | `source/game/casino/casino_poker.{h,cpp}` — `CasinoPoker` |
| The progression, objectives, rewards, unlockables, status | `source/game/casino/casino_progression.{h,cpp}` — the `DBCasino*` tables (§7) |
| The Game Progress page (the objectives) | `source/game/casino/casino_objectives_screen.{h,cpp}` — `CasinoObjectivesScreen` |
| The wallet | `PlayerProfile::CasinoChips` / `CasinoUnlockables` / `CasinoPoints` / `CasinoObjectives` / `CasinoRewardsGranted` / `CasinoSeen` / `CasinoRouletteNumbers` (`<userdata>/profile.sav`: `casino_chips`, `casino_unlockables`, `casino_points`, `casino_objectives`, `casino_rewards`, `casino_seen`, `casino_roulenums`) |
| The casino pause list | `PauseMenu::setCasinoMode( true )` (photo + gme locked) |
| Entry from the world | FREERIDE, at the `gamblinghall` spot (ECAS, 60 m, car under 20 km/h): `A / Enter: enter the casino` -> `GM_Casino` |
| Launcher | `launch/13_Casino.bat` |

The wallet is the profile's, so it survives a save/load (the retail
`CasinoSave` section). Chips are bought with the profile's money 1:1
(`Buy chips`, 1 000 at a time) [INF: the retail rate came from the server].

### 2.1 Controls

| Screen | Controls |
|---|---|
| Floor | Up / Down select, Enter / A choose (**Game progress** opens the objectives page), Backspace / B leave |
| Game progress | Up / Down the objective (hold repeats), Left / Right the axis (32 games / 33 variety / 34 social / 35 collection), Backspace / B back |
| Slot machine | Enter / A spin, Left / Right bet per line (1..10), Y / Up max bet, Backspace / B floor |
| Roulette | arrows move the cursor, Enter / A place the current chip, Y chip size (1 / 5 / 25 / 100), X spin, **R rebet** (the retail Rebet button), Backspace / B clear the bets (or leave with none down) |
| Poker | Enter / A deal, then Enter / A hold, **X or Y draw**, Left / Right the card, Backspace / B floor |
| Any | ESC = the pause menu (the retail casino list) |

### 2.2 Flags

| Flag | Effect |
|---|---|
| `-gm casino` | start in the casino |
| `-casinochips <n>` | starting balance (tests; otherwise the profile's) |
| `-casinoseed <n>` | fixed RNG seed for all three games, so an automated run is reproducible |
| `-slotselftest <n>` | run n spins, report the return, exit |
| `-rouletteselftest <n>` | run n spins of every bet type, report each return, exit |
| `-pokerselftest <n>` | run n hands of *exact optimal* play, report the return, exit |
| `-logcasino` | dump the `DBCasino*` tables and the profile's progress on them |
| `-objectives` | open the Game Progress page straight away (a test hook, like `-dealer <n>`) |
| `-casinomovie` | draw the retail `Casino.vmf` behind the native screens (see §5) |
| `-dumpcasinolabels` | load `Casino.vmf` and dump its timelines / functions (the `-dumplabels` of the casino) |

---

## 3. The slot machine

`[INF]` — the shape is the retail one (5 reels, 3 rows, a bet per line), but
**the reel strips and the paytable are OpenTDU2's own**: the retail machine was
resolved by the server, and the client only kept the statistics save descriptor
and the `CRC_*_STATS_*_CASINO_SLOT_1` messages. Everything lives in
`casino_slots.cpp` so it can be replaced in one place.

* One 64-stop strip shared by the five reels, counts Cherry 14 / Lemon 12 /
  Bell 11 / Bar 10 / Seven 8 / Diamond 6 / Jackpot 3. Because the five reels
  share the strip the return is **closed form**: `analyticRtp()` =
  **96.16%**.
* 9 paylines (5 x 3 rows, `kPaylines`), left-to-right runs of 3 / 4 / 5.
* Paytable in line-bet multiples: Cherry 10/30/90, Lemon 15/45/150, Bell
  25/75/250, Bar 30/100/300, Seven 60/200/600, Diamond 80/250/1000, Jackpot
  5-of-a-kind 5000.
* Reels stop one at a time (`kFirstStop` 0.35 s, `kStopGap` 0.16 s) — the
  retail `C_SLOT_ACTIVATE` beat; the win / loose beat is the settle.

**Verified** — `-gm casino -slotselftest 200000`:

```
CasinoSlots self test: 200000 spins, wagered 1800000, won 1741360,
    measured RTP 96.74%, analytic RTP 96.16%
CasinoSlots self test: 43439 winning spins (21.7%), best win 1900 chips
CasinoSlots self test: balance 941360, expected 941360 (ok)
```

The last line is the wallet arithmetic (`start - wagered + won`), which must
hold exactly.

---

## 4. Roulette

The **wheel order and the payouts are standard** (`[DATA]` for the wheel,
`[INF]` for the payout multiples — the client names the bets, not the odds):
single zero, 35:1 straight, 2:1 dozen / column, 1:1 even money. The table
phases are the retail ones (`CASROU_PlaceYourBet` -> `CASROU_BetsAreDone` ->
`CASROU_RienNeVaPlus` -> `CASROU_Winner`), driven by a timer offline
(`kBetCloseTime` 1.2 s, `kSpinTime` 2.0 s, `kResultTime` 3.5 s) because there
are no other players to wait for.

* Cells: the 37 numbers, then RED / BLACK / ODD / EVEN / 1-18 / 19-36
  (`BET_RED` / `BET_BLACK` / `BET_1TO18` / `BET_19TO36`), then the three dozens
  and the three columns — 49 cells.
* Cursor: `(col, row)`, the 0 to the left of the middle row.

**Verified** — `-gm casino -rouletteselftest 5000` (5 000 spins per bet type,
the theoretical return of every bet on a single-zero wheel is **36/37 =
97.30%**):

```
Straight 93.60%   Red 98.40%   Black 96.60%   Odd 99.16%   Even 96.32%
1-18 97.60%   19-36 100.60%   1st 12 97.56%   2nd 12 95.76%   3rd 12 99.42%
Column 1 96.78%   Column 2 95.16%   Column 3 96.30%
```

The spread is the sampling error of 5 000 draws (the straight bet, 35:1, is the
noisiest); every bet type is centred on the theoretical value, which is what
the test is for. A live run also settled end to end:
`CasinoRoulette: 33 black, payout 0 chips, balance 4990`.

---

## 5. `Casino.vmf` loads but must not be drawn under the native screens

`Casino.vmf` **loads and parses** through the existing runtime:

```
Flash movie 'casino': 1280x960, 135 frames @ 30 fps, 1636 tags, 206 bitmaps, 2 fonts
```

so the bank, its 206 bitmaps, its 2 fonts and its display list are all readable
— the movie is a candidate for a future *fully movie-driven* casino. **But
drawing it before the native screens breaks them** (reproduced with
`-casinomovie`): the native header disappears entirely and every glyph the
native code draws afterwards comes out as a solid block. Without it (the
default) the same screens are correct
(`docs/images/casino_floor.webp`, `casino_slots.png`, `casino_roulette.png`,
`casino_roulette_winner.png`).

Prime suspect: the flash display list leaves the 2D layer's **mask / scissor**
state set (`Renderer2D::setStencil` + the per-draw scissor of `recordDraw`), so
the native draws that follow are clipped and sampled wrong. The next step is to
find which of the movie's frames sets it (draw the movie last, or reset the
stencil after `FlashPlayer::draw`) — until then the movie stays opt-in and the
screens are native.

The retail games themselves are driven by the server's messages, which is the
same reason `DealerMenu` is native rather than driving `shopscar` — the port
draws its own screens and uses the game's *data*, not its message protocol.

---

## 6. Poker

The retail table is `Cas_Poker.vmf` (966 KB) with `C_POKER_YOU_BET` and
`CasinoPokerStatisticsSaveDesc` in the client [DATA: PS3 anchors]. Offline it is
**9/6 Jacks or Better**: five cards, hold any, draw once, paid on a pair of
Jacks or better.

* The paytable is `[INF]` (the retail one was server side, like the slot
  machine's): 9/6 Jacks or Better. A **royal flush pays the max-bet bonus** —
  800 per coin at the 5-coin bet, 250 below — which is the difference between
  the published **99.54%** (max bet) and **98.37%** (one coin).
* The self test plays the **exact optimal strategy**: for every one of the 32
  hold combinations it averages the payout over every way the remaining cards
  can be dealt (`EvaluateHold`), so the measured return can be compared with the
  published figure rather than with a heuristic.

**Verified** — `-gm casino -pokerselftest 120`:

```
CasinoPoker evaluate: royal flush        -> Royal flush      (ok)
CasinoPoker evaluate: straight flush     -> Straight flush   (ok)
CasinoPoker evaluate: four of a kind     -> Four of a kind   (ok)
CasinoPoker evaluate: full house         -> Full house       (ok)
CasinoPoker evaluate: flush              -> Flush            (ok)
CasinoPoker evaluate: straight           -> Straight         (ok)
CasinoPoker evaluate: wheel (A-2-3-4-5)  -> Straight         (ok)
CasinoPoker evaluate: three of a kind    -> Three of a kind  (ok)
CasinoPoker evaluate: two pair           -> Two pair         (ok)
CasinoPoker evaluate: high pair (jacks)  -> Jacks or better  (ok)
CasinoPoker evaluate: high pair (aces)   -> Jacks or better  (ok)
CasinoPoker evaluate: low pair           -> No win           (ok)
CasinoPoker evaluate: junk               -> No win           (ok)
CasinoPoker evaluate: 13 cases, 0 failed
CasinoPoker self test: 120 hands at the max bet, optimal-play EV mean 93.57%
    +- 7.47% (1 SE); 9/6 Jacks or Better is 99.54% at max bet, 98.37% at one coin
CasinoPoker self test: 57 winning hands (47.5%), 0 royal(s), balance
    100000175, expected 100000175 (ok)
```

Two separate claims, and the log keeps them apart:

* **The evaluator is exact** — all 13 hand classes, including the A-2-3-4-5 wheel,
  return the right result. This is the part that must not be approximate.
* **The return is a *sampled* estimate.** The per-deal optimal EV has a large
  spread (a dealt full house, flush or straight, or a 4-to-a-royal, moves it by
  several points), so the mean is reported **with its standard error** and the
  published 99.54% has to be read against it: here the estimate is 93.57% ±
  7.47%, i.e. 99.54% is inside one standard error. A tight estimate would need
  tens of thousands of deals — at ~0.15 s per deal (each one evaluates all 32
  holds against every completion) that is not a useful test, which is why the
  evaluator test above carries the exactness instead.

The win rate (47.5%) matches the published ~45.5% for Jacks or Better, and the
wallet arithmetic is exact. Screenshot: `docs/images/casino_poker.webp`.

---

## 7. The casino progression (`casino_progression.{h,cpp}`)

The retail casino had a full meta-game, and **all of it is in the shipped
data** — `db_data.cpr` carries 10 `DBCasinoLevel` rows over 4 axes, **145
`DBCasinoObjectiveDetail` objectives**, 60 `DBCasinoReward` rows and 54
`DBCasinoUnlockable` rows. `-logcasino` dumps them; this is what the port uses:

| Table | What it gives |
|---|---|
| `DBCasinoLevel` | `_axis` 32 (the games), 33 (variety), 34 / 35 (social / online); `_level` 1..4 and `_pointsneeded` 300..4000 |
| `DBCasinoObjectiveDetail` | the objectives: `_category` (1 money won, 4 all bets placed, 5 a bet / combination won, 6 a win streak, 9 hands won, 13 a jackpot, 14 a feature used, 18 N lines played, 43 a type played, 54 a roulette number seen, 57/59 a UI feature used), `_filtera` (the game: 2 roulette, 3 a poker table, 4 slot machines, 17 the poker cash game), `_filterb` (the bet / hand / machine type), `_objectivevalue` and `_pointsawarded`, plus the **localized text** |
| `DBCasinoReward` | `_casinounlockable` (the unlockable row), `_carid`, `_cinematicname`, `_cocktailid`, `_rewardtype`, `_roomlevel` / `_roomtype`, `_timer` |
| `DBCasinoUnlockable` | the payloads by name: `Cocktail_00..24`, `Emote_11..33` |

**How it is wired.** The games report events through
`ReportCasinoEvent( event, game, subType, amount )`; every objective whose
category and filters match accumulates the amount, and when it reaches
`_objectivevalue` it pays `_pointsawarded` to its axis, which moves the level
and grants the next reward rows. The counters, the points and the granted
rewards all live in the profile (`casino_objectives`, `casino_points`,
`casino_rewards`), so the progression survives a save.

* Slots -> `MoneyWon`, `BetWon` (`_filterb` 10, the 9-combination machine),
  `WinStreak`, `Jackpot`, `_category` 18 (nine lines played), `PlayedType`.
* Roulette -> `MoneyWon`, `WinStreak`, `BetWon` with the real bet-type ids
  (36 straight-up, 43 column, 44 black / red, 45 even / odd, 46 half, 47 dozen,
  48 zero), `NumberSeen` **for a number seen for the first time** (the profile
  keeps a 37-bit mask), and `UiUsed` for the **Rebet** button (`R`).
* Poker -> `MoneyWon`, `HandsWon`, `WinStreak` on the cash game, and `BetWon`
  with the hand-type ids (60 one pair ... 65 full house).

**Honest limits `[INF]`.** The objectives that need the online rooms
(Sit & Go, Tournaments, private tables, friends, cocktails with other players,
the race) are loaded but never complete offline, because the port has no such
mode — that is what `_filterc != 1` skips. The objectives for the games that do
exist are tracked for real. The reward rows carry no axis / level, so they are
**consumed in `_hashcode` order** as levels are reached; the unlockable names
(`Cocktail_*`, `Emote_*`) are what gets granted. `_filterc` (the online table
id) is only matched when it is 1 ("any").

**Status ladder.** The objectives name it: *"Get the VIP status (level 5)"*.
The status level is the total over the axes, so **VIP at 5** and **ACE at the
table's maximum (4 + 2 + 2 + 2 = 10)**; `/:isProgressCasino` is now pushed
`true` to the shell and the pause menu, and the header of every casino screen
shows the status, the level, the points to the next level and the objective
count.

### 7.1 The Game Progress page

`CasinoObjectivesScreen` is the page the retail pause list's `game_progress`
entry opens: one axis at a time, every `DBCasinoObjectiveDetail` row of that axis
with its counter against its target, its `_pointsawarded`, and DONE once the
target is met (with the text dimmed). The Flash markup in the texts
(`#color:FF6600#` … `#nocolor#`) is stripped, because a native draw has no
`#command:` / colour resolver. Two ways in:

* the **floor's `Game progress` entry** — verified end to end (screenshot
  `docs/images/casino_objectives.webp`);
* the **pause menu's `game_progress`** entry, enabled in the casino by
  `PauseMenu::setCasinoMode( true )` and reported as `eResult::Progress`. The
  entry is wired, but in the automated run the movie-driven menu did **not** take
  the down press (`PauseMenu: the movie drives the menu (menu_sq.isMenuH)`, then
  `reco` — the `_root.Input` trap of `CLAUDE.md`), so the floor entry is the
  route that is verified; the pause route needs the movie's own navigation.

`-objectives` opens the page straight away for tests (the `-dealer <n>` pattern).

---

## 8. What is not built yet

| Piece | Evidence it exists | Why it is not here |
|---|---|---|
| **The online casino modes** | `DBCasinoObjectiveDetail` objectives for Sit & Go (`_filtera` 18/20/21), the Tournaments (19), the private tables, friends (`_category` 36/37), cocktails with other players (45), the race (55); `CasinoRankingsSave`, `/:UseCasinoRankingList` | They need other players. The objectives are loaded and shown, they simply never complete offline (`_filterc != 1`). |
| **The casino island interior** | `CasinoGame` (PS3 5720848), `CasinoChipsStack`, `CasinoCocktail`, the room furniture (`DBFurniture._iscasino`), the shaders `TD_ChipsStack.sob` / `TD_Roulette.sob` / `TD_Casino_scroll.sob` | **Built (§9)**: the hall and its slots / roulette / poker / VIP / dressing rooms and the boutique are walked in first person from their own retail banks. |
| **The casino's own rooms / terrace / cashier** | `_category` 25 (VIP terrace), 27/29 (the car in the hall / on the terrace), 32 (the cashier), 34 (the shop), 58 (inspect the exclusive rewards) | **Partly built (§9)**: the cashier and the shop are wired offline; the terrace and the car on its socle are walked but have no interaction of their own. |
| **Arrival / departure cinematics** | `spt_casino_arrival.bnk` (10.4 MB), `spt_casino_departure.bnk` (8.8 MB) + the `spt_ibiza_*` / `spt_hawai_*` variants | Authored scenes, playable through the existing `Cinematic` players (the `cutscenes_inventory.md` route). |
| **Dealer / other-player avatars** | `CASROU_RouletteJoinSelector`, `..._LeaveSelector`, `..._NewPlayer(s)`, `AvatarBhvRouletteMan/Woman`, `AvatarBhvSlotMachineMan/Woman` | Needs the PNJ work, and it is online-shaped. |
| **The cocktails / emotes themselves** | The unlockables are granted (`Cocktail_00..24`, `Emote_11..33`) and the objectives that ask for them are tracked; since 2026-11-10 the **emotes are usable** (the 8 retail `em_*.anm` of `avatarsdriver.bnk` play in every walk mode — `docs/re/emotes_spec.md`), the cocktails still have nothing to drink | The emote -> `Emote_11..33` unlockable mapping is not decoded (the emotes are not gated); the cocktails need the prop work. |
| **The shared jackpot** | `C_SLOT_SHARED_JACKPOT_BACKGROUND`, `CRC_*_CASINO_BETA_STATISTICS_UPDATE` | Online by design; the offline machine pays the jackpot from its own table. |
| **The four retail movies** | `Cas_SlotMachine.vmf` / `Cas_Roulette.vmf` / `Cas_Poker.vmf` / `Cas_SlotLines.vmf` | The screens are native (the retail movies are driven by the server's messages). The shell `Casino.vmf` loads but cannot be drawn under the native screens yet (§5). |

---

## 9. The casino interior (session 80)

The resort is walked in first person. Every room is its own retail bank under
`Euro/Bnk/Interior` [DATA: the bank listing and each `entities.xmb`], loaded as
a closed interior scene (the `spots/interior_walk.cpp` setup: `bWorld` false,
the walk camera's near plane all along). `source/game/casino/casino_interior.{h,cpp}`
is the room machine; `GMCasino` keeps the mapping to its own screens.

| Room | Bank | The bank's own data |
|---|---|---|
| hall | `icashal.bnk` (50 MB) | 8 `FpSelectable` doors (`GoToShop` "shop", `GoToRoulette1` "crou", `GoToPoker1` "cpok", `GoToPokerArcade1` "sitgo", `GoToSlotMachine1` "cslo", `GoToVIP` "cvip", `GoToDressing` "avat", `GoToExit` "outdoor"), `Entity__Actor__CasinoCashier` "Sc_Cashier", `Entity__Actor__CasinoBarman` "Sc_Bar", 14 cameras, 32 cinematics (`C_INTRO`, `C_LIFE_*`, `C_WINCAR`, `C_BUYCAR`, `C_VIP`, `C_ACE`, `C_RANKINGS_*`, `C_COCKTAIL_*`) |
| slots | `icasslo1.bnk` (55 MB) | 38 `CasinoGame__SlotMachine` of 6 types, each with a `SelectableZone` volume + `SelectableString` ("sm_car", "sm_clo", "sm_pok", "sm_haw", "sm_hl", "sm_cla"), 140 `SlotMachineSpinner`, 171 `NeonSlotMachine`, 54 `CasinoFlashScreen`, 19 cameras + 39 `CameraCasino` |
| roulette | `icasrou1.bnk` | 5 `CasinoGameRoulette` tables with `CasinoRouletteTypeDesc` prices 5 / 20 / 25 / 50 / 100, 89 cameras, 10 `CasinoChipStack`, 5 `CasinoCocktailsDispenser`, 58 cinematics (`C_ROUL_WAIT*`, `C_ROUL_BET`, `C_ROUL_*_WHEEL_*`, `C_ROUL_RESULT`, `C_ROUL_Idle_*`) |
| poker | `icaspok1.bnk` | 5 `CasinoGamePoker` tables, 26 `CasinoPokerChairDescription` seats, 92 `CasinoCard`, 15 `CasinoCocktail`, 157 cameras |
| VIP | `icasvip.bnk` | one high stakes poker table, the VIP car (`ActorVehiclePosition`), `C_WIN_CAR` / `C_BUYCAR_VIP` |
| dressing | `icasdr.bnk` | one cinematic per clothing category (`C_head_d`, `C_tops`, `C_hats`, `C_bottoms`, `C_shoes`, `C_bracelets`) |
| boutique | `ishop_ca.bnk` (17 MB) | the casino clothes shop: brand 0x57 "CASINO" (`DBCarShop._clothesshopbrand` = `DBClothe._brand`) |

Construction: `CasinoInterior::enter(room)` loads the bank, plays its `C_LIFE*`
cinematics in the background, then the walk starts at the bank's
`Entity__FirstPerson` camera. The hall's `StartCamera` is `cam_from_Ext`, the
arrival camera **outside** the building (the retail fade comes from the exterior
bank, which an interior-only scene does not have), so the hall starts at
`cam_from_slo1` instead [INF]; its `C_INTRO` is skipped for the same reason.
**Every room's `C_INTRO` is skipped** (2026-11-05): the slots, roulette, poker and
VIP intros all frame the dunes and the casino facade from outside, and the
dressing one starts under the sofa - arrival shots that need the exterior bank
(`docs/images/casino_room_start_*.png`; `-casinoroomintro` plays them, the A/B).
The VIP's `Entity__FirstPerson` camera `cam_from_Hall` sits 7 m up at z +23,
outside the room (table cameras z -30..-34, the car -17), so the VIP walk starts
at its `CameraCasino` `Cam_FirstPerson` (the table overview) [INF].
`-logcasino` lists every room camera with its scene-local position.

Walk interactions: the doors report their retail `SelectableString`; the slot
machines use their own `SelectableZone` volumes (the `volume_mat` is copied, so
the raycast is the retail box); the roulette and poker tables get a 1.6-1.7 m
box around the table entity (they carry no `FpSelectable` — the retail seats
them by camera), and the hall's cashier and bar a 1.2 m box each. Enter / A
acts, B returns to the hall (or leaves from the hall).
`-nocasinointerior` falls back to the old native floor list,
`-casinoroom <hall|slots|roulette|poker|vip|dressing|shop>` boots into a room and
`-casinoselect <n>` acts on the n-th machine / table of it (test hooks).

Screenshots: `docs/images/casino_hall_interior.webp`,
`casino_slots_interior.png`, `casino_roulette_interior.png`,
`casino_poker_interior.png`.



### 9.1 The six slot machines

`CasinoSlotMachineTypeDesc` [DATA] — Price (the stake of one spin), `NbLines`,
`NbDifferentSymbol`:

| Machine | Price | Lines | Symbols | Prize |
|---|---|---|---|---|
| `SlotMachineCar` | 100 | 1 | 6 | 3 of the machine's top symbol (Diamond) pays the **Audi R8 V10** |
| `SlotMachineClothes` | 10 | 1 | 4 | 3 of its top symbol (Bell) pays a casino clothes item |
| `SlotMachinePoker` | 5 | 1 | 8 (52 = cards) | — |
| `SlotMachineHawai` | 15 | 3 | 8 | — |
| `SlotMachineClassic` | 2 | 3 | 8 | — |
| `SlotMachineHighLife` | 10 | 9 | 8 | — |

The wager is `Price x bet level` (1..10) and each winning line pays
`paytable x bet level x Price / NbLines`, so the return stays at the paytable's
**96.16%** on every machine type (measured 97.17% on 50 000 spins,
`-slotselftest`). The machine's symbol set keeps only its first N symbols; the
card machine's 52 are played as the 8 [INF].

The prizes are `DBRewardCar` rows [DATA]: `CASINO_CAR_1` is the **Audi R8 V10**
(`_carunlocked` 432, colour row 12, rims 1) and the clothes come from `DBClothe`
brand 0x57 CASINO. Winning the car grants it into `PlayerProfile::OwnedCars` and
the `Car` unlockable; the line then pays 250 000 chips. The clothes line grants
the first casino outfit the player does not own (then 5 000 chips), plus the
`cloth_set_won` unlockable. `-casinoprize car|clothes` pays one without a spin
(test hook); verified: the car 432 lands in `cars =` of `profile.sav` with
`casino_car_won = 1`, the clothes prize logged `Trousers` (id 580).

### 9.2 Cashier, bar, shop and wardrobe — all offline

* **Cashier** (`Sc_Cashier`): buy 1 000 / 10 000 / 100 000 chips or sell
  1 000 / every chip, 1 chip = 1 unit of money (the retail rate came from the
  server, [INF]). The wallet is `PlayerProfile::CasinoChips`, saved.
* **Bar** (`Sc_Bar`): the `DBCasinoUnlockable` cocktails (`Cocktail_00` ...
  `Cocktail_24`) are drunk for $25 each; the unlockable is granted and the
  cocktail is kept in `PlayerProfile::CasinoDrinks` (saved as `casino_drinks`),
  the list the "drink a cocktail" objectives read.
* **Boutique** (`ishop_ca`): `ClothesMenu` with brand 0x57 and the player's sex.
* **Dressing room**: the wardrobe lists the clothes the player owns
  (`PlayerProfile::OwnedClothes`) with their brand; the avatar mesh swap on a
  worn item is still to do (the list, the purchase and the prizes are wired).

Roulette tables: the `CasinoRouletteTypeDesc` `Price` is the table minimum and
the first plaque at or above it is the table's smallest chip (5 / 25 / 25 / 100 /
100 on the felt's 1 / 5 / 25 / 100 set) [INF: the retail plaque set is not in
the data].

### 9.3 What still needs a server / a later increment

* **Sit & Go tournaments** (`GoToPokerArcade1` "sitgo"): they need opponents.
  The door walks into the poker room and says so; an offline AI table is the
  next increment.
* **The shared jackpot** (`C_SLOT_SHARED_JACKPOT_BACKGROUND`) and the rankings
  screens (`CAM_RANKINGS`, `/:UseCasinoRankingList`): online by design.
* **The VIP car** (`CASINO_CAR_VIP`, the Spyker C8 Aileron Spyder) and the VIP
  lounge's furniture rewards: the lounge is gated by the VIP status, the prizes
  themselves are not granted yet.
* **The club-exclusive cars offline**: `DBClubRewardCars` (4 rows) are the
  server-side club prizes; an offline championship with an entry fee and hard
  (but beatable) AI is the proposed route (see the owner's ask, session 80).

Flags: `-casinochips <n>`, `-casinoseed <n>`, `-casinoroom`, `-casinoselect`,
`-casinoprize`, `-nocasinointerior`, `-nocasinointeriorto`, `-slotselftest`,
`-rouletteselftest`, `-pokerselftest`, `-logcasino`.


### 9.4 The car on the hall's socle (2026-11-04, `win`)

`icashal.bnk` has one `Entity__Actor__ActorVehiclePosition` "VirtualVehiculeActor"
at (0.54, -0.64, -25.26) of the hall frame with **VehicleId 432** (the Audi R8,
the car machine's prize), **PaintId 12, RimIndice 1, InteriorIndice 11,
NbTurnByMinute 1.0** [DATA: logged by `IntroCinematic`]. `CasinoInterior::placeShowCars`
parks that car on it (`IntroCinematic::addDisplayCar` on the entity placement)
and turns it at its own rate (`spinLastDisplayCar`, 1 turn a minute about the
rest point). A room whose VehicleId is not a DBCars id falls back to the room's
`DBRewardCar` (`CASINO_CAR_1`, the VIP lounge `CASINO_CAR_VIP`) [INF].
`-nocasinocar` leaves it out, `-casinocarturns <n>` overrides the rate.
Screenshot: `docs/images/casino_hall_r8_socle.webp`.

**2026-11-05 (`win`): the carpet and the podium.** That screenshot shows the car on a flat
olive-grey floor; retail has a sunken red-carpet runway and the lit `Socle_Car` podium
(prop at (0.54, -0.99, -25.26), loaded all along). The grey was the **island**: a closed
room (`Setup::bWorld = false`, no streaming) still ran `EdenWorld::loadAround`, which loads
every area tile whatever the sector radius, and the scene island defaults to Ibiza - the
Ibiza area tile at the island origin cut through the hall floor and hid the pit. A/B:
`-noareas` showed the carpet and the podium, `-noterrain` / `-hidematerial Floor_gloss` did
not. `IntroCinematic::load` now turns the area tiles off when the scene has no island
(`EdenWorld::setAreasEnabled`; `-closedroomareas` = the old A/B). Other closed rooms:
the intro garage and the slots room are pixel-identical, the VIP lounge is a little lighter
(the tile also shadowed it). Screenshot: `docs/images/casino_hall_carpet_podium.webp`.

### 9.5 The real slot machines (2026-11-04, `win`)

Every `CasinoGame__SlotMachine` of `icasslo1.bnk` names its own pieces [DATA]:

| Field | Use |
|---|---|
| `CameraGame` | an `Entity__Camera__CameraCasino` (Fov 0.698 rad): the view of the played machine. **All 38 are named `CAM_GAME`**, so the camera is looked up by its `__Hc` (`IntroCinematic::getEntityCamera( uint64_t )`), never by name. |
| `Spinners[]` | the reels, left to right: 3 (Car, Clothes, Hawai, Classic) or 5 (Poker, HighLife) `Entity__Actor__SlotMachineSpinner` |
| `TimeBeforeFirstStop` | 1.5 s on every machine |
| spinner `TimeAfterPrevSpinner` | 0.5 s on the clothes machines, 0 elsewhere (0 is played as 0.35 s apart [INF]) |
| spinner `MatName`, `NbColumn/NbLineOfSymbolsByTexture` | `spinhl` 4x3, `spinhawai` 4x3, `spinclassique` 4x3, `spinnerleft` 3x3, `sc_legs` 3x3, `Cards` 8x8 |

The spinners were not loaded before (only `ArrayOf_Entity__Actor` props were),
so the reel windows showed the machine's white backing. They are props now,
one model each (`-noslotspinners` = the A/B).

**`TD_Casino_scroll`** (decoded from the `.sob`'s DXSO CTAB + SPIR-V) [DATA]:
the 2DM COLOR is WHITE, the symbols are `DETAIL1` (`symbolshighlife.2db`, ...)
and `DETAIL1B` its blurred strip. VS `Var_VS` c60-63 WVP, c64 CamPos, c65 DirLight,
**c66-68 `scroll_uv0..2`, c69 `UV_symbol_space`**; PS `blur_blend_factor`
(0 = the sharp sheet, 1 = the blurred one). For a UV1 vertex `(u, v)` with
`s = UV_symbol_space.x`: column `k` = `u in (k s, (k + 1) s]` (k = 0..2), and
while `v < UV_symbol_space.y` the vertex gets `uv1 + scroll_uvk.xy`. So a reel
shows three symbol slots (top, payline, bottom), each scrolled on its own.

The sheets (`docs/images/casino_reel_symbol_sheets.webp`) hold the symbols in
reading order: SlotMachineCar = lamp, wheel, stopwatch, flag, trophy, **the R8**
(its top symbol, the car prize); Classic = cherry, raspberry, strawberry, BAR,
2 BAR, 3 BAR, 7, diamond; Hawai ("Beach") = flowers, surf boards, coconut, sun;
HighLife = chips, champagne, martini, cocktail, three boats, the casino coin;
Clothes ("Fashion") = trousers / tops / heads per reel; Poker = the 52 cards.

`GMCasino::updateReels`: a stopped reel puts slot `k` on the cell of its result
symbol `i` (row `k` of the engine grid): `scroll_uvk = ((i % cols - k) / cols,
(i / cols) / rows)`, `UV_symbol_space = (1 / cols, 1e6)`, blur 0. A turning
reel scrolls the whole strip with blur 1. Symbol `i` = cell `i` is [INF] (the
retail reel strips were resolved by the server); the paytable and the wallet
are §3 / §9.1 unchanged. `CasinoSlots::setReelLayout` gives the engine the
machine's reel count (a 3-reel machine pays runs of 3 on its 3 reels; its
return therefore differs from the 5-reel 96.16 % [INF]) and its stop timing.
The native 2D grid is drawn only without a machine camera (`-slotgrid` forces
it, `-noslotcamera` keeps the walk view).

Verified: `-casinoroom slots -casinoselect 6 -casinoseed 7` + one spin: the
five reels blur, stop one after the other, and the 66-chip win is three speed
boats on the reels (`docs/images/casino_slot_highlife_spin.webp`); all six
machine types after a spin: `docs/images/casino_slot_machines_real_reels.webp`.

Not done: the avatar sit-in and the `C_SLOT_ACTIVATE_0x` / `C_SLOT_WIN_0x` /
`C_JACKPOT_*` beats are not played. `SlotMachineSpinner` `RotationSpeed` is not
decoded (the spin speed is [INF]). The HUD is §9.6.

### 9.6 The retail slot HUD: Cas_SlotMachine.vmf + Cas_SlotScreen.vmf (2026-11-04, `win`)

`source/game/casino/casino_slot_hud.{h,cpp}` (`CasinoSlotHud`) loads both
movies with the casino and drives them while a machine is played; `-noslothud`
keeps the native text, `-logslothud` logs the FSCommands. The protocol below is
read from the movies' own scripts (`-dumpcasinomovie cas_slotmachine` now dumps
the root timeline too) [DATA] unless tagged.

**Cas_SlotMachine.vmf** (1280x960, 136 frames). The root timeline is a state
machine: `default` (1, stopped; resetVariables) / `menu` (15) / `submenu` (30,
the table of gains pages) / `rules` (45) / `play` (60) / `active_controls`
(65) / `popup` (75, the lines manager) / `relaunch_step` (90) / `quit_double`
(105) / `error` (120).

| The game sets | Meaning |
|---|---|
| `/:WhatPage` | the machine page: `smca` TDU 2 (help `sm_car`), `smcl` Fashion (`sm_cloth`), `smpo` Video Poker, `smh1` Beach (`sm_hawai1`), `smc1` Classic, `sml1` High Life (`sm_luxe1`), `smcs` the unused Casino machine. Titles `m_smca`.. ("Slot machine - Test Drive"), rules pages `m_<page>_p<n>_<k>` (DBLoc, category casino, applied on `sbdd`). |
| `/:var_Money_InAccount`, `/:varMachineBuyIn`, `/:nbSpins` (3 / 5 reels), `/:nbActiveLines` (1), `/:is_smcl_man` | the wallet, the price, the reel count (the lines manager steps 1-2-3 on 3 reels, 1-3-5-9 on 5), the activated lines, the Fashion help variant |
| `gotoAndPlay('menu')` | opens the machine menu (Play / Rules / Table of gains / Quit, `m_play` `m_Rules_` `m_gains` `m_Quit`) |

| Focus (the game routes the pad to it) | Methods |
|---|---|
| `/:RootMenuPath` while `/:InRootMenu` (the `menu` clip registers itself on `open`) | `Move('up' / 'down')`, `A()` -> `ValidMenu`: play -> FSCommand `smon` + `play`; rules / gains -> the help system pages; quit -> `sloo` |
| `/:Clip` = the root at `active_controls` | `A` -> `smpl` (spin), `B` -> `smof` + `menu`, `X_Lines` -> `lini` + `popup`, `X_Lock` -> `lock`, `Left` / `Right` -> `left` / `right` |
| `/:Clip` = the root at `popup` | `A` -> `lino` (valid, the game reads `/:nbActiveLines`), `B` -> `linc`, `Left` / `Right` -> `lines_manager.window.lessLines` / `moreLines` (each sends `smli` and `sndp:ADDLINE`) |
| `/:Clip` = `helpsystem` (openHelpSystem) | `A` / `B` close, `Move('left' / 'right')` pages (`hsys:<n>`), `wmof` / `wmon` |

The pad -> movie routing itself is encrypted [ENC]; the port calls those
methods for A / B / the d-pad / X (`X_Lines`) / Y (`X_Lock`) [INF], and X only
on the per-line machines (the movie's `X_Lines` does not check) [INF]. B on the
menu opens the movie's own `sure_quit` box ("Are you sure you want to quit this
machine?", `m_slo_conf_quit`) - the menu clip defines no B [INF]. `DBXO(...)`
(error / confirm boxes) sets `/:Msg1Txt`, `/:DgbxCase`, `/:YesNo` and sends
`dbxo`; the box is drawn natively and answered with `/:PlayerAnswer` 'a' / 'b'
and `DialogBox()` [the drawing is INF]. The `error` frame reads `/:error_type`:
1 `m_slo_err_money`, 2 `m_slo_err_money2`, 3 `m_slo_err_lines`.

**The help bar.** The `play` frame calls `_root.SetHelp(getHelpName())` with no
`Change` flag, and `help.SetHelp` acts on `Change == true` / `== false` only
(undefined equals neither under ECMA Equals2), so the movie alone leaves the
menu's "Select / Quit" bar up. The port calls `SetHelp(<play help>, true)` on
`smon` and `SetHelp('menu', true)` on `smof` [INF: the exe does it; the casino
code is encrypted]. The play bars are the retail ones: "Play / Activate lines /
Back" on Classic, Beach, High Life; "Play / Back" on TDU 2, Fashion, Poker.

**Cas_SlotScreen.vmf** (512x256, the machine's own credit screen; the retail
draws it on the machine's `CasinoFlashScreen`). The game sets
`/:MoneyInMachine`, `/:BetValue`, `/:GainValue`; `loss` (-> `loop_loss`)
takes `BetValue` off at the spin start; `gain` counts `GainValue` into
`MoneyInMachine` (1 a frame up to 120, else `var_percent` a frame for 120
frames, then the rest 1 a frame) with `sndp:SMWIN` / `sndp:UPMONEY`, ending on
`endi`; `not_win` plays `sndp:SMLOOSE`. The port has no movie -> texture path, so
the screen is a 2D panel at the top right of the machine view [INF];
`var_percent = GainValue / 120` [INF].

**Stakes.** The per-line machines (NbLines > 1: Classic, Beach, High Life) cost
their Price per activated line - "The price is ¤10 per activated line"
(`m_sml1_p1_3`, `m_smc1_p1_2`, `m_smh1_p1_3`) [DATA]; the others per spin. Each
activated line pays paytable x bet level x Price, so the return is still the
paytable's [INF]. The movie starts on one line. Left / Right at the play
controls step the port's bet level and Y sets the maximum: in the retail they
move the hold hand of the second-chance machines (Poker, Beach), which the port
does not have [INF].

**Two runtime gaps it exposed (fixed, they apply to every movie):**
* `MovieClip._width` / `_height` were undefined (get) and ignored (set); the
  help system sizes its mask and box from them, so the rules pages came out in a
  narrow clipped strip. They are the clip's content bounds in its parent
  (`FlashPlayer::getClipExtent`); `-noclipextent` is the A/B. A/B on the other
  screens: the map, the driving HUD and the options page are pixel identical;
  the pause menu's player panel sits ~20 px lower (mean diff 5.2) - its script
  reads `_height` now [INF: the new placement is the script's; not compared to
  a retail capture]. The login difference is its random slideshow.
* Fonts that carry vector outlines (no atlas: `Digital` id 43 of
  Cas_SlotScreen, `Berlin Sans FB` ids 4 / 364 of Cas_SlotMachine) store each
  glyph's shape between the glyph records and the code table; the fixed layout
  read landed inside the shapes (ascent -2 / 6656), so their text drew at the
  wrong height or not at all (the help bar labels, the credit digits). The parser
  now finds the code table (ASCII head ascending, then the Windows-1252 order)
  and its metrics: font 4 reads 914 / 208 / 98 like its atlas twin 116, and ends
  1170 bytes before the tag end like 116 [DATA]. `-logfonts` logs every font.

Verified (`-casinoroom slots -casinoselect 6 -casinoseed 7 -casinochips 1000
-fixedtimestep`): menu -> Play (`smon`) -> spin (`smpl`), the lines manager to
9 (`lino`: "9 line(s) activated, 90 chips a spin"), a 600-chip win counted up on
the screen (910 -> 1510), B -> menu (`smof`), B -> the quit box -> A (`sloo`,
back on the floor); Rules -> pages 1 / 2 -> back to the menu; 0 chips -> the
`error_money` box; all six machine types (`casino_slot_hud_six_machines.png`:
menu, play, after a spin). Images: `casino_slot_hud_menu.png`, `_play`, `_lines`,
`_win_countup`, `_quit_box`, `_rules`.

### 9.7 The machine itself: spin direction, the handle, the screen on the machine, the lit tops (2026-11-04, `win`)

The owner: the reels of two machines spun across instead of down, the credit screen belongs on the machine,
the handle pulls, and the tops of the machines (which hide the drums) were missing.

**Spin direction.** `SlotMachineSpinner.SpinnerType` [DATA]: **0** the drums (TDU 2, Classic, Beach,
High Life), **1** the Fashion machine's horizontal drums, **2** the Poker cards. The spin scrolled the sheet
along its rows (U) on every machine, which is sideways on screen; only type 1 does that now. Types 0 / 2
pin the three faces of a reel to one sheet column (`scroll_uvk.x = (column - k) / cols`) and move them down
it together (`scroll_uvk.y = k / rows - roll`): the symbols enter at the top and leave at the bottom.
Speed: `RotationSpeed` 1.3 read as drum turns a second, one turn = the column x 3 [INF]; the retail turns
the spinner's own bone (it has a skeleton and an `AnimStop` clip), not decoded. `-reelspeedscale <f>` slows
it for a look (`docs/images/casino_reels_spin_down.webp`, x0.04, frames 20 apart).

**The handle.** `Entity__Actor__SlotMachineTrigger` (38 `SC_Trigger_Car`: the lever `SMcar_Levier`, the
button panel `Boutons`, own skeleton) was never loaded. Its `Anim` is `cas_sm_spin-tri.bas` - a "BAS!"
layer naming `cas_sm_spin-tri.xan`, held in the bank as `cas_sm_spin-tri.anm` (1.21 s) [DATA]. It plays
from the spin order (`IntroCinematic::setPropClip`, a game-driven clip on a prop; the start on the spin is
INF): the lever holds ~0.4 s, pulls down, springs back (`docs/images/casino_slot_handle_pull.webp`).

**The screen on the machine.** Each machine's `SlotScreen` is an `Entity__Actor__CasinoFlashScreen`
(`SM_FlashScreen_Stack`, Movie `Cas_SlotScreen`, Material `flash`, BitmapId 1) and `SlotLinesScreen` one
with Cas_SlotLines (`Flash_LinesScreen` / `_3R`) [DATA]; neither was loaded. The movies are drawn into
render targets in the offscreen phase (`CasinoSlotHud::drawOffscreen`; `Renderer2D::setTargetSize`): the
played machine's live screen, one idle screen for the 37 others, the lines overlay. The material is
`TD_NEONDECAL` with two slots, 0 NEONGLOW (a 'BLUE' placeholder) and 1 the colour = BitmapId 1 [DATA]; its
emissive is `dp4( NEONGLOW, AnimValue ) x WeatherEmissive` (eden_neons.h), and props are never registered
with the neon manager, so it drew black whatever its texture: the screens get the movie in slot 1, a white
glow and `AnimValue (1, 0, 0, 0)`, `WeatherEmissive 1` [INF]. `-slotscreenpanel` = the old 2D panel,
`-slotscreenclear` clears the target red (the binding A/B). `docs/images/casino_slot_screen_on_machine.webp`.

**The tops / buttons.** `Entity__Actor__Neon` (436: the machines' top pay art `PartOfSM<type>_gain_<n>`, the
room signs) and `Entity__Actor__NeonSlotMachine` (171: `Buttons_0x`, the lit buttons) were never loaded, so
the drums stood open at the top. Loaded now (the slots room only [INF: not checked in other banks]) and lit
steadily: `AnimValue` = the entity's Red / Green / Blue `IlluminationFactor` (the buttons: `NeonInactive`),
`WeatherEmissive 1` [DATA: the factors; INF: no `TimeFactor` blink, no win flash]; `-nocasinoneonlight`.
`docs/images/casino_slot_tops_neons.webp`, `casino_slot_machines_parts.png`.

**A render pass fix that came with it.** The 2D pipelines are built for the main pass; drawing them into a
render target was `VUID-vkCmdDraw-renderPass-02684` (dependencyCount 2 != 1). The main pass now declares the
same two dependencies as the target passes (a superset of its old one). Validation: casino spin / quit and a
FREERIDE run with the map and the pause menu, 0 VUIDs.

Not done: the drum bone rotation and `AnimStop`; the neon blink (`TimeFactor`, `CurveFactor`) and the win
lights of the gain panels; the button press as its own beat (the trigger clip may already move them - not
checked frame by frame); Cas_SlotLines is drawn at rest (no active lines shown on the reels).

### 9.8 The Fashion middle reel, the reel stops, the neon blink and the machines' lights (2026-11-04, `win`)

**The Fashion chest drum.** The symbol sheets leave cells empty: the Fashion sheets hold their four outfits as
row 0 + one cell of row 1 (the rest of row 1 is grey, row 2 repeats row 0); the 4 x 3 sheets leave row 2 /
column 3 empty [DATA: `docs/images/casino_reel_symbol_sheets.webp`]. A horizontal drum spun along row
"reel index", so the chest drum (reel 1) ran along the empty row (the owner's grey stripes). Horizontal drums
spin along row 0; vertical drums only use full columns (0..2 on a 4 x 3 sheet, 0..3 on the card sheet).
`docs/images/casino_fashion_chest_spin.webp`.

**The reel stops.** Every spinner has its own `TimeAfterPrevSpinner` (High Life 0 / 0.7 / 0.7 / 0.9 / 1.1,
TDU 2 0 / 0.7 / 1.1, Fashion 0.5 / 0.5 / 0.7) [DATA]; reel k stops at TimeBeforeFirstStop + the running sum
[INF: from the name] (`CasinoSlots::setReelStops`; `-logcasinoreels` lists them).

**The neon light, decoded** (readable PC code) [CODE]:
* `FUN_00b440d0` loads a channel: +0 TimeFactor (default 1), +4 IlluminationFactor (default 1), +8 the curve.
  The params hold R / G / B channels at +0x10 / +0x20 / +0x30, `UseDecalRandTime` at +0x41.
* `FUN_00b43fb0` (init): UseDecalRandTime -> a start offset = LCG (x 0x7ff8a3ed + 0x2aa01d31) / 2^32 x 3.0,
  else 0.
* `FUN_00b44350` (a channel at time t): no curve -> IlluminationFactor; else period = 3.0 / TimeFactor,
  x = fmod( t, period ) / period, value = IlluminationFactor x curve( x ).
* `FUN_00b43940` / `FUN_00b43830` (the curve): keys of `ExportType_Curve / ArrayOf_Curve` (`PosArray_` x / y
  pairs, `TgtInArray_` / `TgtOutArray_`); a cubic Hermite per segment (h00 P0 + h10 TgtOut0 + h01 P1 + h11
  TgtIn1) solved for x by 32 halvings; before / after the keys their value. The curves are entities of the
  room's `entities.xmb` (330 in the slots room: `Curve_Low`, `Curve_Middle`, `Curve_One_Very_High` ...).
* `FUN_00b44500` / `FUN_00b44400`: AnimValue = ( R, G, B, 0 ) of the evaluation.
`IntroCinematic::updateNeons` plays that every frame for the casino neons (WeatherEmissive 1 [INF]); the gain
panels pulse (frame diffs 4.7 .. 7.3 at a fixed eye, `docs/images/casino_neon_blink.webp`).

**The machines' light states** [DATA]: `Entity__Actor__NeonSlotMachine` has NeonInactive / NeonActive /
NeonWin / NeonLoose / NeonJackpot / NeonWinRelaunch / NeonLooseRelaunch, each `CanBeActivate`, `Duration`,
`NeonParams`. Across the 171: the buttons 1100011 (inactive, active, the two relaunch states); the Poker
`Pok_Neon_0x` 1111011 (Win: blue 1.75); every other machine light 1100000. A machine's `Neons[]` go Active
while it is played, Win / Jackpot / Loose on the result, Inactive on leaving [INF: the beats are the exe's;
a state with CanBeActivate 0 leaves the light as it is]. The `Pok_Neon` lights are not in a machine's lists
(not driven yet).

**The win lights.** A machine's `ComboActors[n]` is the pay panel of combination n on its top
(`PartOfSM_Poker_gain_00`..`_08`) [DATA]. On a win the panel of each winning symbol flashes 3 s (4 blinks a
second, x2 / x0.15) [INF: the flash, and n = the symbol's rank]. It works where the panel is its own model
(Beach, Classic, Poker, TDU 2); the High Life panels have no mesh (`mesh[0]`: they light part of the sign
model, not mapped) - not flashed.

### 9.9 The second chance machines: Poker and Beach (2026-11-04, `win`)

The owner: *"poker slot logic is different to other slots, 1 spin and then you get to pick which reels to hold
and get to spin again, the 2nd spin is the outcome"*, and the same for the Beach machine.

[DATA] `define_SM_Options` sets `machineIsRelaunch` for `smpo` (Poker) and `smh1` (Beach); the rules pages:
"free and systematic Second Chance mode", "the money is removed ... as soon as you pull the lever", "after the
first draw, you can Hold some cards and Draw new cards", "the cards which compose a winning combination are
automatically locked", "if all the cards are locked, the second draw doesn't change anything", "winnings are
received after the second draw" (`m_smpo_p1_2`, `m_smpo_p3_1..4`, `m_smh1_p1_3`, `m_smh1_p2_2`,
`m_smh1_p4_1..4`). The movie's `relaunch_step` frame (currentStep 2) carries the step-2 help bar
**"Select (left / right) / Second turn (A) / Lock/Unlock (X) / Back (B)"** (`sm_poker_2`, `sm_hawai1_2`).

The port (`GMCasino` `eRelaunch`, `startSlotSpin`, `autoHold`, `settleSecondChance`; `CasinoSlots::respin`,
`setPayNextSpin`):
* **Poker**: the stake is taken, five cards are dealt from a shuffled deck (seeded by `-casinoseed`) and shown on
  the five card reels - the `Cards` sheet (`cards02.2db`, 8 x 8) holds hearts, spades, diamonds, clubs, each
  A 2..10 J Q K in reading order, cell 52 the back [DATA, `docs/images/casino_poker_card_sheet.webp`]. The paying
  hand's cards hold themselves (a pair or better: the cards of that rank; a straight / flush / full house /
  straight flush: all five) [INF: the rule]. The second draw replaces the cards not held; the Jacks or Better
  hand after it pays stake x the 9/6 table (`CasinoPoker`) [INF: the machine's own table was the server's]; its
  pay panel (`ComboActors[hand - 1]`) flashes.
* **Beach**: the first spin is charged but not paid; the reels of each winning line's run lock themselves; the
  second spin turns the others for free and pays like a spin.
* Left / Right move the hand along the cards / reels, X (Square) or Y (Triangle) lock / unlock, A (Cross) draws
  again; B during the second chance settles the hand as it is [INF]. A native row under the machine shows the
  cards / reels, HOLD and the hand (the retail hand cursor - the movie's `cursorDisplayed` - is the exe's [INF]).

Verified: Poker deal 7d 4s 5c Qd 8c -> hold the 4s (X) -> 3s 4s 2s 4h 8d (the 4s kept), "No win"; seeds 8 / 9 deal
a pair of jacks or better, auto-held 01100, Jacks or better paid; Beach reel 2 locked through the respin; seeds 22 /
61 win on the first spin (150 / 450 if kept), all three reels auto-lock, the respin pays (955 -> 1105 / 1405).
`docs/images/casino_slot_second_chance.webp`. 0 VUIDs.

Not done: the Poker machine's second chance return (99.54 % is the table's optimal-play figure, not measured on
this machine); the hand cursor over the machine; the `NeonWinRelaunch` / `NeonLooseRelaunch` button states.

### 9.10 The jackpot cutscenes, the car keys and the car-win ceremony (2026-11-05, `win`)

The owner: "if you match all 3 symbols of the car the keys drop out for that car
there should be certain little cutscenes for these as well" and, on the key
cutscene, "the key dropped and avatar picked it up".

**The room cutscenes never had their camera.** `CasinoInterior::tick` ran
`walk.update` every frame, and the walk pins the camera to the walker (who sits
at the room origin until the walk starts, outside the building). Every casino
cutscene therefore showed the dunes from outside; that is where the old "every
C_INTRO is an arrival shot from outside" note came from. The walk camera now runs
only while walking (`bWalking`). `CasinoInterior::playCinematic` /
`playCinematicAtPlace` play a room cutscene with its own cameras and hand back
to the walk, or to the machine's game camera, when it ends. A or B skips it.
`-casinocin <name>` plays one on entry, and `-casinocinat <i>` anchors it at
machine i.

**The jackpot cutscenes** (`icasslo1.bnk`, `*_totem.cin`, the `_W` twin for a
woman avatar) [DATA: the names, the actors, the tracks]:

| cutscene | what it shows |
| --- | --- |
| `C_JACKPOT` / `_W` ("NewJackpot", 30.4 s) | the car won: the `CF_KCar` key ring (`key_ring_car`, own `.XSB`) is shown from 20.66 s and drops into the machine's tray (y 1.01 -> 0.64 m, 21.26..21.6 s), beside `Win_A_Car` |
| `C_JACKPOT_CAR_MONEY` / `_W` (20.4 s) | the car machine paying chips (the car is already owned) |
| `C_JACKPOT_CLOTHES` / `_W`, `C_CLOTHES_MONEY` / `_W` | an outfit won / chips instead |
| `C_JACKPOT_POKER`, `_HAWAI`, `_HIGH_LIFE`, `_PROGRESSIF` (+ `_W`) | the other machine kinds |

They are authored around one machine (the player as `Avatar_` at local
(0, 0, 0.67), the car one's origin at (35.08, 8.49, 11.31), beside the car machine at
(35.55, 8.49, 12.44)). `GMCasino::playSlotJackpot` plays the machine kind's cutscene
anchored at the machine played (`IntroCinematic::setCinematicOrigin` with the
machine entity's `orient_pos_scale`) when a spin pays the jackpot line or the
car / clothes prize [INF: the pick by name; the choosing code is not in the
readable exe]. The slot HUD and the casino shell are hidden while it plays.

The player actor is called `Avatar_` in these scenes. `updatePnjs` used to
match only `Avatar`, so the avatar was missing at the machine; it now tries both.
The key ring's clip (`kcar_cas_sm_winjackpot-m/-w.anm`) leaves every bone at rest
and its skinned draw showed nothing, so a prop whose clip does not move a bone is
drawn on its track instead (`-nopropanim` was the A/B that showed the drop). The
pick-up is not carried by any data the port applies: once the ring has stopped and
the player's `Hand_R` comes within 0.12 m of it, it stays in that hand (log:
`'CF_KCar' taken in the hand at t 24.03 (0.11 m)`) [INF from the owner's account
of retail; `-nokeygrab` = off].

**The car-win ceremony**: after the first car's `C_JACKPOT`, the game leaves the
machine for the hall and plays `C_WINCAR` (`icashal.bnk`, 37.7 s, 17 shots). The
director (`NPC_Director`) and the hostess (`NPC_Hostess`) present the R8 on the
`Socle_Car` podium, and the cutscene has the avatar, an in-car shot, confetti
(`Fx_Confetis`) and white flash cuts [DATA: the actors; going to the hall for it is INF].
`-nocasinowincar` stays at the machine. Test:
`-gm CASINO -casinoroom slots -casinoselect 7 -casinoprize car` on a profile without
the car. Shots: `docs/images/casino_jackpot_keys_wincar.webp`.

Not done:
- The rankings tube: see 9.11.
- `C_SLOT_WIN_0x` / `C_SLOT_LOOSE_0x` (the small win / lose shots) are not played.
- The forced test prize skips the spin, so its reels do not show the three cars.

### 9.11 The rankings tube behind the podium (2026-11-05, `win`)

The owner: "It's a screen that pops up it's meant to show fastest times around
casino island in the Audi that's on the podium". It is the "black box" right
of the podium in the earlier shots.

[DATA] `icashal.bnk` has six `Entity__Actor__CasinoFlashScreen__CasinoFlashScreenRanking`
(Movie `Cas_Ranking`, BitmapId 1, RenderTargetId 1, SelectableString `crnk`,
SelectableDist 6, a `RankingCamera`). One of them, `FlashScreen_CRNK_TimeAttack`
(IsTimeAttack 1, at (0.54, 0.58, -30.39)), stands in the tube `TubeEcransRankings`
behind the podium. Four no-camera cutscenes move them:

| cutscene | screen y | tube y |
| --- | --- | --- |
| `C_RANKINGS_STAND_CLOSED` | -1.97 (under the floor) | -3.62 |
| `C_RANKINGS_OPEN` (1 s) | -2.17 -> 0.22 | -3.60 -> -1.24 |
| `C_RANKINGS_STAND_OPENED` | 0.22 | -0.95 |
| `C_RANKINGS_CLOSE` (1 s) | 0.22 -> -2.17 | -1.24 -> -3.62 |

None of them was played, so the screen stood at its raw entity placement (0.58),
which was the black box. `CasinoRankingScreen` (`source/game/casino/casino_ranking_screen.*`)
parks the tube closed when the hall loads. It raises the tube when the walker
comes within SelectableDist (6 m) and lowers it past 7.5 m [INF: that the
selectable distance is the trigger]. It also draws the `Cas_Ranking` movie into a
render target on the screen's TD_NEONDECAL face (`flashscreen_ran`, slot 1 colour,
slot 0 glow), on the movie's `time_attack_rank` frame (labels: default, slot_rank,
roul_rank, cash_rank, sitgo_rank, tournament_rank, casino_rank, time_attack_rank,
end_menu). The screen's `SelectableZone` is registered as `crnk`: A looks at it
through its `RankingCamera` and B steps back.

What it shows: the retail page says "Connecting to server" with Cancel. The
movie is the online leaderboard (server requests, pages, filters, gamer cards;
its rows are `../:pos`, `../:player_name`, `../:score`, `resu_rankings_array`) and
waits for data the game never sends offline. The port has no casino time-attack
mode either, so there are no times to show; none are invented.
Flags: `-nocasinorankings`, `-norankingmovie`, `-rankingsopen` (raised at once),
`-rankingspage <label>`, `-rankingsclear` (red clear: the binding A/B),
`-logcasinorankings`. Shots: `docs/images/casino_rankings_tube.webp`.

Not done: the movie's data protocol (how the game fills the rows); the casino
time attack itself (the race in the podium car) and recording its times; the
five other ranking screens (general, satellite, sit and go) get no movie yet.

### 9.12 The bar and the cashier on their retail movies (2026-11-05, `win`)

The owner: hook the correct UI for the cashier's chips, and "implement the barman ... buy drinks and in a
drink you have a random chance for something". `source/game/casino/casino_desk_hud.{h,cpp}`
(`CasinoDeskHud`) drives both movies; `-nodeskhud` keeps the native screens of 9.2, `-logdeskhud` logs
the FSCommands.

**The desks' own data** [DATA: `icashal.bnk` entities, member names from the PS3 build 0xf48a20..0xf48e88]:

| Entity | Fields |
|---|---|
| `Entity__Actor__CasinoBarman` `Sc_Bar` | `SelectableZone` (the desk volume), `SelectableString` `'barman'`, `CherryTombola` (the cherry prop), `CinematicPrepareDrink` `C_COCKTAIL_BARMAN`, `CinematicPlayerDrink` `C_COCKTAIL_AVATAR`, `CinematicPlayerDrinkWonTicket` `C_COCKTAIL_AVATAR_WIN`, `WinTicketLevelOne/Two/Three/FourChancePercentage` **1 / 2 / 3 / 5** |
| `Entity__Actor__CasinoCashier` `Sc_Cashier` | `SelectableZone`, `SelectableString` `'cash'`, `AmountGivedToPlayer` **200**, `CinematicIdle` `C_LIFE_Bank` (the hall also has `C_GiveMoney`, `C_DontGiveMoney`, `C_DontGiveMoney_VIP`, `C_TRANSFERT_MONEY`) |

The desks now use their `SelectableZone` volumes (the slot machines' path), then Cline's named
`SelectableCashier` / `SelectableBar` volume, then a box at the actor. The selectable strings are the
retail ones (Casino.vmf's `ItemType` vocabulary `'cash'` / `'barman'`).

**The bar = `Cas_Common.vmf`** (`cas_common.bnk`). The root's `cocktail` frame (30) registers
`_root.Clip` and defines `A` / `B` / `X` / `UP` / `DOWN` / `LEFT` / `RIGHT`, which forward to the
`barman` clip (`Buy_Cocktail`, `Cancel_Bar`, `Offer_Cocktail`, `MoveCursor`). The list is 15 cocktails in
cursor order `00 01 10 11 | 02 03 12 20 | 13 21 22 | 04 14 23 24` (`getCocktailIdFromCursor`), four price
tiers (`setCockInfo`: `m_cockprice_1..4` = ¤50 / ¤200 / ¤500 / ¤1000; `displayTicket`: lvl1..lvl4 by the same
groups). The game sets `cocktail_XX_IsLock`, `/:m_cockPrice_XX` (PS3 0xf37868), `/:tombola_available`,
`/:skipTomboTut`, `/:playerListLocked` (offline: true, X "offer" is locked). A on an open cocktail closes
the list and the close frame sends **`buy cock <id>`** then **`clco`**; B sends `clco`. The first tier has
no unlock condition (no `m_cockUnlock_00/01/10/11`, "This cocktail is available.") [DATA]; the others open
with their `Cocktail_XX` unlockable [INF: the conditions are poker / sit & go / friends texts in DBLoc].

**The random chance = the Solar Crown Raffle** [DATA: DBLoc `m_tombo_*`]: "To participate, simply buy a
cocktail at the casino bar. If you find a cherry in your glass, you've won!"; a ¤50 / 250 (sic, the list
says 200) / 500 / 1000 cocktail can win a level 1 / 2 / 3 / final free pass to the Solar Crown ¤1000000
Poker Tournament (the VIP terrace). The port rolls the barman's own percentage for the cocktail's tier
[the roll was the server's: INF uniform], plays `C_COCKTAIL_BARMAN` -> `C_COCKTAIL_AVATAR` (or `_WIN`) and
then the movie's `tombola_popup` (`var_win`, `var_level_win`, `Open()`; it closes itself after
`timer_val` loops). Passes are saved per level as `casino_passes = l1,l2,l3,l4`. `-cherry` forces a win,
`-allcocktails` opens the list. The tutorial (`open_tuto`, the raffle explained) plays on the first visit
of a session.

**The cashier = `Casino.vmf /money_conv`**, the "Currency converter". Page 1 (`open1` .. `stop1`):
`bt_doll_to_casi` / `bt_casi_to_doll` (locked by `is_DolToCas_locked` / `is_CasToDol_locked`), A sends
**`casi:chx1`** (dollars -> chips) or **`casi:chx2`**, B closes with **`casi:qtcv`**. Then `loop` waits on
`isReady`; the game sets `valSliderMin` / `valSliderMax` and `isReady`, `open2` runs
`slider.initSlider(convDev, 0, max, min)`. Page 2: Left / Right -> the slider's `Move` (**`slle`** /
**`slri`**), LB / RB -> **`conv:minv`** / **`conv:maxv`**, A -> **`conv:vali`**, B -> **`conv:canc`** (back to
page 1). The game writes the slider's `currentValue` / `txtcurrentValue`, calls `setSliderPosition` and
sets `bt_change:txt_bt` (PS3 0xf48d68..0xf48e00). `m_conv_txt2` carries the tokens `§DOLL_CONV§ =
§CASP_CONV§` (Latin-1 0xA7 in the tables) which the port fills with `$1 = 1 ¤` [INF: the rate was the
server's]. The cashier hands a player with no chips and no money `AmountGivedToPlayer` (200 chips) once per
visit [INF: the retail rule also counted `_cashier_time_between_visits_*`].

Pad mapping [INF, the routing is ENC]: A / Enter validate, B / Backspace back, the D-pad / arrows move;
on the slider page D-pad down / X = Min, D-pad up / Y = Max.

Verified (`-gm casino -casinoroom hall -casinoselect 1|0 -fixedtimestep`): the list with its ticket and
locks, a ¤200 cocktail with `-cherry` -> the barman / avatar cutscenes -> "You win - Level 2 free pass"
(`docs/images/casino_bar_raffle_tutorial.webp`, `casino_bar_drink_and_raffle.webp`); a ¤50 cocktail without
the cherry logs `lose`; $5000 -> 5000 chips at Max and 3 chips -> $3 (`casino_cashier_converter.png`).
Not done: the popup's level icon shows "1" for a level 2 win (the `content/level` art; the text is right);
the walk reticle draws over the bar cutscenes; the passes are not spent anywhere yet (the tournament
needs other players).

## 10. The seaplane trip (2026-11-04, `win`)

The casino island is reached by seaplane [DATA: `scenarics/cinematics/casino/`]:

| Bank | Timeline | Frame (spots.bin) | Exterior |
|---|---|---|---|
| `spt_ibiza_departure` / `spt_hawai_departure` | `C_IBIZA_DEPARTURE` (65.3 s) / `C_HAWAI_DEPARTURE` | `ELANDCAA` (Ibiza (23976.9, 5.2, 24659.7), category 0x0e) | `elandcaa.bnk` |
| `spt_casino_arrival` | `C_CASINO_ARRIVAL` (53.0 s) | `ELANDCA` (casino (2763.0, 1.7, 1457.6)) | `elandca.bnk` |
| `spt_casino_departure` | `C_CASINO_DEPARTURE` (37.0 s) | `ELANDCA` | `elandca.bnk` |
| `spt_ibiza_arrival` / `spt_hawai_arrival` | `C_IBIZA_ARRIVAL` (52.5 s) / `C_HAWAI_ARRIVAL` | `ELANDCAA` | `elandcaa.bnk` |

(The `.cin` files are `M_<...>`; the playable names are the `Entity__Cinematic`
`C_<...>` - the car wash trap again.) The island legs animate the player's car
through the `.sce` actor `PLAYER_CAR` (the valet takes / brings the car); the
casino departure has an `Entity__CinCar` `d_car`. The scene frame = the dock
spot frame is [INF] (the placing code is encrypted); the shots line up (the
terminal, the pontoon, the plane on the water, the resort).

`source/game/casino/casino_trip.{h,cpp}`: `RequestFlightToCasino( island, car )`
(FREERIDE: A / Enter stopped at a casino spot - `EGAMHA` or the dock
`ELANDCAA`, which now has the `gamblinghall` map icon [INF]) and
`RequestFlightHome()` (`GMCasino::leaveCasino`). `CinViewerRequest::After`
chains requests across banks and islands (`GMCinViewer` reloads itself for the
next one); only the last scene writes the `StoryHandoff` (island + the car where
the arrival scene leaves it). Esc / Circle skips a scene. `-nocasinotrip` enters
directly, `-casinotrip` (FREERIDE) and `-casinoleave <frame>` (CASINO) are the
test hooks, `-cinisland <island>` places a `-gm CINVIEWER` scene on another
island. Verified: `-gm FREERIDE -casinotrip -casinoleave 200` plays departure ->
arrival -> CASINO hall -> casino departure; `-gm casino -casinoleave 60` plays
the casino departure -> Ibiza arrival -> FREERIDE on Ibiza at the dock
(`docs/images/casino_trip_to_casino.webp`, `casino_trip_home.png`).
Not done: the Hawai legs are wired but not played through yet; the car after
the home arrival is not checked for a parked pose (one run's last frame showed
the chase camera in a hedge, 13 s after the handoff).

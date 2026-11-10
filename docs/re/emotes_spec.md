# The player emotes (walk mode self view)

The retail walk-avatar emotes: the 8 one-shot clips of `scenarics/avatars/avatarsdriver.bnk`
(`anims\emotes\em_xan\`, one per sex) played on the player avatar while the walk camera
parks and a third-person "self view" looks at it — the retail walk-avatar state machine
states 5→6→7→4 (`docs/re/facefx_spec.md` 6.3.3: fade in, self view (camera type 10), the
emote, fade back). The casino grants the `Emote_11..33` unlockables
(`docs/re/casino_spec.md` 7) with nothing to use; the clips below are what an emote is.

## 1. The clips [DATA: avatarsdriver.bnk entry list]

`otdu2_bnk_tool list "Euro/Bnk/scenarics/avatars/avatarsdriver.bnk"` — 16 `em_*.anm`
(subType 8), under `anims\emotes\em_xan\avatar\` (man) / `...\avatrice\` (woman). The
slots of `Entity__AvatarOnline animAvatar[8..16]` (`tess_garage_spec.md` 4.2, read on the
bank's `entities.xmb` schema `AnmRef[] animAvatar`; the dump lists followme twice at slot
16, the 8 unique kept):

| slot | label | man | woman | length |
|---|---|---|---|---|
| 8 | hello | `em_hello-m.anm` | `em_helloa-w.anm` | 4.92 s / (m) |
| 9 | cheers | `em_cheers-m.anm` | `em_cheers-w.anm` | 4.32 s (m) |
| 10 | funny | `em_funny-m.anm` | `em_funny-w.anm` | 4.16 s (m) |
| 11 | followme | `em_followme-m.anm` | `em_followme-w.anm` | |
| 12 | angry | `em_angry-m.anm` | `em_angry-w.anm` | |
| 13 | sad | `em_sad-m.anm` | `em_sad_02-w.anm` | |
| 14 | strong | `em_strong-m.anm` | `em_strong-w.anm` | |
| 15 | laugh | `em_laugh-m.anm` | `em_laugh-w.anm` | |

Every clip also ships as a `.bas` layer + a `.trk` walk-controller curve (`[ENC]`, not
needed: the `.anm` of the same name is a plain "ANM!" clip `EdenAnimation::parse` reads
directly). The idle base pose is `animAvatar[0]` stand: `co_stand-w.anm` / `co_stand-m.anm`
of the same bank (sampled looped).

## 2. The port's implementation

- `IntroCinematic` (`source/game/avatarchoice/intro_cinematic.h/.cpp`): the clip table
  (`getPlayerEmotes`, the retail slot order), `playPlayerEmote(index)`,
  `startPlayerSelfView()` (idle, no clip), `stopPlayerEmote()`,
  `setPlayerEmotePlace(floor, heading)` and `updateEmotePlayer()` — called from
  `updatePnjs()` in the `bPlayerSeated` chain (the player avatar posed from the walk
  place instead of a `.sce` actor; the same draw path, `player.bVisible`).
- The pose: bind → `co_stand` looped → `BlendPoses` toward the clip, ease in over 0.2 s
  and out over the last 0.2 s (the retail fade pace, state 5; the blend weights themselves
  are `[ENC]` — the `SeatedAvatar::setOverride` pattern). The place is scene-local (the
  walk controller frame, the same `setLocalCamera` frame); the avatar world is
  `sceneOrigin * T(floor) * Ry(heading)`.
- `WalkMode` (`source/game/walkmode/walk_mode.*`): `startSelfView` / `endSelfView` /
  `updateSelfView` — while set, `update()` parks the body (retail states 6/7: no
  locomotion, no selection) and glides the camera 0.25 s from the eye to a third-person
  spot 2.2 m behind, 1.75 m up, aimed at floor + 1.2 m, feeding
  `setPlayerEmotePlace` every frame.
- The walk hosts: `GMFirstWalkMode::updateEmotes` (the garage story walk),
  `InteriorWalk::updateEmotes` (the freeride shops / houses),
  `CasinoInterior::updateEmotes` (the casino hall / game rooms) — the same law in all
  three: `IA_Emote` (X, rebindable) toggles the self view, keys 1..8 play the slots, the
  view returns to first person when the clip ends (state 7 → 4); B in the self view goes
  back instead of leaving the room; A is ignored (the selection is cleared). The picker
  is `WalkMode::drawEmotePickerUi` (Dear ImGui), drawn by the owner modes' `drawUI` while
  the walk is live.

## 3. Tests

- `-emote <label>`: plays the emote on the walk start (`GMFirstWalkMode` /
  `InteriorWalk` / `CasinoInterior`, the first walk tick).
- `-logemotes`: the self view in/out lines and the resolved clip per play.
- autoplay `press=emote` (`tdu_instance.cpp` `kActions`): the key without a keyboard
  (autoplay `key=X` injects the raw scancode, which does not map through the action
  bindings — a real keyboard X does).

```
build-main\bin\opentdu2-main.exe -game C:\TDU2-Gamedata\TestDrive2 -userdata C:\temp\otdu2-main-userdata ^
    -gm FIRST_WM -nopad -fixedtimestep -allowskip -emote hello -logemotes ^
    -autoplay "120:press=validate;200:shot=C:/OpenTDU2/_emote_play.ppm;460:shot=C:/OpenTDU2/_emote_after.ppm" -exitframe 520
```

Log: `EMOTE: self view in, eye (...)`, `EMOTE: playing 'hello' (em_hello-m.anm, 4.92 s)`,
`EMOTE: self view out` (the auto return at the clip end, ~frame 417). Evidence:
`docs/images/emote_hello_garage.webp` (the wave, self view + picker),
`emote_funny_garage.webp` (slot 3 by key), `emote_cheers_casino.webp` (the casino hall),
`emote_return_firstperson.webp` (after the return, the A/B).

## 4. Limits and open questions

- The retail trigger binding (input action 0x22), the emote wheel movie (the `emop` /
  `emcl` FSCommands reach the manager, the movie itself is undocumented) and the type 10
  camera law are `[ENC]`: the X key, the ImGui picker and the 2.2 m / 1.75 m orbit are
  OpenTDU2's own (`[INF]`).
- The retail avatar opacity fade (`FUN_00b85460`, 0.2 s) is a skin-shader feature the
  port has no path for; the avatar appears/disappears with the camera glide instead.
- The `Emote_11..33` → slot mapping is not decoded (the unlockable payload names versus
  the animAvatar slots): the casino unlockables stay inert, the emotes are not gated.
- The woman variants are data-verified (the clips are in the bank, the per-sex branch is
  the `SeatedAvatar` law); the interactive runs played the man clips (the default
  profile's avatar sex).

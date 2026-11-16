# Frontend (menu) sound effects — `sndp:` cues

The port plays the retail UI sounds: the flash movies ask for a cue with
`FSCommand("sndp:<name>")`, the game resolves the name through
`Euro/bnk/frontend/sounds/fe_snd.bnk`'s `sounds.ini` and plays the wav on the
**"FrontEnd"** audio client. This closes the "the port never loads it" item of
`docs/STATE.md` (session 2026-10-30, "The audio plan").

Code: `source/audio/frontend_audio.{h,cpp}` (the bank, the cue map, the voices),
`FlashPlayer::SetSoundCommandCallback` (`source/flash/flash_player.cpp`) for the
movie side, `PlayFrontEndCue()` for the native menus the port drives itself.

---------------------------------------------------------------------------
## 1. The bank and the cue map [DATA]

| | |
|---|---|
| Bank | `Euro/bnk/frontend/sounds/fe_snd.bnk` (4 857 856 bytes, 178 entries) |
| Entry 0 | `…\EURO\Sound\Front_End\sounds.ini` (21 058 bytes, a private-path dump) |
| Wavs | 177 RIFF WAV, **MS-ADPCM 48 kHz stereo** (`fmt` tag 2), ~1 KB .. 125 KB |
| `otdu2_bnk_tool` | `list` shows the 178 entries; `extract` writes them out (used to read `sounds.ini`) |

`sounds.ini` has two commands:

```
[SOUNDS]
//AddFESound  flash_message  nom_fichier.wav  volume
AddFESound      "mopen"      "menuOn01.wav"      0.3
...
SetMusic        "ECD_FERRI"  "CarDealer.wav"     1    0     <- shop music, NOT used yet
```

- **`AddFESound "<cue>" "<wav>" <volume>`** — 189 lines. `<cue>` is what the
  movie puts after `sndp:`, `<volume>` is the entry's own gain (0.1 .. 1.0;
  several cues share one wav, e.g. `st_favco`/`map_bmrk` -> `map_bmrk.wav`,
  `sp_score`/`kyd_scor` -> `sp_score.wav`).
- **`SetMusic "<place id>" "<wav>" …`** — the per-shop background music
  (car dealer, airport, club houses, gambling halls, tuners …). Parsed by
  nobody yet; the wavs live in `Euro/bnk/sound/musics/`.

The cues the retail movies actually use (a headless run with
`-logfrontendaudio`, pause menu + map): `mopen`, `mclose`, `ttip`, `move`,
`valid`, `switch`, `three`, plus `st_*`/`md*`/`rd_*`/`S_*`/`UPMONEY`/`UPBET`
families in the other screens.

---------------------------------------------------------------------------
## 2. The audio client [DATA]

`audiosetup.xmb` (dumped with `otdu2_bnk_tool xmbdump`):

- `Clients[9] = "FrontEnd"` -> pipeline `FrontEndStereo` (2 inputs, stereo;
  the bfx index the engine uses is 7, `AudioSetup::GetBfxClientName`).
- `MixPresets[1] = "Menu"` keeps `FrontEnd` at gain **1**, `FrontEndMusic` at
  0.2 and everything else at **0** — the retail boot menu runs on it
  (`GameModes` `BOOTMENU` / `EDITOR`).
- The in-game presets (`FREERIDE` = `MixPresets[0]`, `MISSION`, `TESTCAR` …)
  also keep `FrontEnd` at **1**, so a pause-menu cue is audible over the
  world; nothing had to change the game mode to hear it.
- The player's `SFX` mixer volume applies through the setup's mixer tree like
  any other client.

The port plays the cues **non-spatial** on that client
(`VoiceParams.Client = 7`, no `bSpatial`): the wavs are stereo, the client's
pipeline has 2 inputs.

---------------------------------------------------------------------------
## 3. How a cue reaches the mixer

1. **Movie side (all of them).** AVM1 `GetURL`/`GetURL2` with
   `"FSCommand:sndp:<cue>"` -> `FlashPlayer::dispatchFSCommand` calls the global
   `SoundCommandCallback` (set once in `TestDriveGameInstance::initialize`,
   `source/tdu_instance.cpp`) and then the player's own `fsCommandCallback`, so
   the menus keep seeing every FSCommand (the pause menu logs them).
   Every `FlashPlayer` in the game shares the hook: the boot menu, the pause
   menu, the HUD, the map, the shops and the casino HUDs.
2. **Native menus.** The port drives some menus itself (their swaps bypass the
   movie's scripts, so no cue is ever fired): those call
   `PlayFrontEndCue("move" / "valid" / "wrong" / "back" / "alert")`. Wired:
   `pause_menu.cpp` (entry column *fallback* path only — the movie-driven
   column fires its own cues; blocked entry), `map_screen.cpp` (the cursor
   menu), `freeride/{dealer,estate,fuel,paint,school,tuning,championship}_menu.cpp`,
   `game/clothes_shop.cpp`, `game/advanced_graphics.cpp`. The sticker editor
   (`paint_menu.cpp`'s own BankList / grid / Place / Placed states) uses its
   `st_*` family: `st_move`, `st_uscal` / `st_dscal`, `st_rotat`, `st_apply` on
   the placement, `pdelete` on a removed sticker.
   Movie-driven ones need nothing: `gm_login` / `gm_shop` / the casino / the
   HUD send keys to their movie, which fires `sndp:` itself.

Duplicate protection: a cue asked again within **40 ms** is dropped. The
movies fire some cues twice on purpose (`mopen` from the pause movie and from
another player in the same frame), which would otherwise double the gain.

---------------------------------------------------------------------------
## 4. Verified (build 196, 2026-11-16, headless)

```
FrontEndAudio: 'Euro/Bnk/frontend/sounds/fe_snd.bnk', 189 cues, client 9 (FrontEnd, pipeline 2)
FrontEndAudio: 'mopen' -> menuOn01.wav (volume 0.30, 0.42 s)          <- pause menu open
FrontEndAudio: 'ttip'  -> toolTip02.wav (volume 0.20, 0.05 s)         <- first entry tooltip
FrontEndAudio: 'move'  -> move04.wav  (volume 0.10, 0.37 s)           <- right / left on the squares
FrontEndAudio: 'valid' -> valid01.wav (volume 0.90, 0.88 s)           <- A on the options square
FrontEndAudio: 'mclose'-> menuOff01.wav (volume 0.30, 0.32 s)         <- pause_out
FrontEndAudio: 'switch'-> trigger.wav (volume 1.00, 0.05 s)           <- map filter tabs
FrontEndAudio: 'three' -> O_M_Race_Start_001b.wav (volume 0.80, 0.65 s)<- HUD race countdown
```

Collision sounds (`CollisionAudio`, unchanged) were re-verified in the same
sweep: `CollisionAudio: metal 6/6, ground 6/6, car 4/4, scrape loops 4` and a
wall hit logging `'carmetal_high03_PHYSIC_SFX' volume 1.01 pitch 1.01`
(`-tp 21796.3,60,24188.1` + accel into the barrier).

Flags: `-nofrontendaudio` (no bank), `-logfrontendaudio` (every cue, decode and
unknown cue).

---------------------------------------------------------------------------
## 5. Place music (`SetMusic`)

`sounds.ini`'s second table maps a **place key** to a wav in
`Euro/bnk/sound/musics/` (113 entries, the wavs are loose files, 0.6 .. 5 MB):

```
SetMusic  "ECD_FERRI"  "CarDealer.wav"  1  0
```

- The key is the spot's bank name: **without** the suffix for the outside record
  (`ECD_FERR`), **with an `I`** for the interior record (`ECD_FERRI`); the clubs
  (`ECH_IA`), the airport (`EAIR_A/B`), the houses (`EPH_L2E`) and the screens
  (`LOGIN`, `MAP`, `LOBBY`, `REPLAY`, `EDITOR`, `TDMEDIA`) have no `I` entry.
  `FrontEndMusic::playForSpot("ECD_FERR")` tries `ECD_FERRI` then `ECD_FERR`.
- House spots: `EPH_L3DI_5067I` -> drop the `_<n>` part -> `EPH_L3DI` ->
  `PlayerHouse02.wav`. The garage / dressing keys (`GARAGE`, `DRESSING`) have
  **no wav** in the shipped folder, so the house track keeps playing there.
- Client: `Clients[10] = "FrontEndMusic"` (bfx index 8) on `FrontEndStereo`, in
  the **"Music" mixer** with the radio (so the player's Music volume drives it)
  [DATA]. The presets: `Menu` 0.2, `SHOP` **0.5**, `Freeride` 1.
- Implemented in `source/audio/frontend_music.{h,cpp}`: one looping voice, the
  wav decoded on switch (a 1.4 MB track is ~25 MB of float frames, so the
  previous clip is dropped), fade-in 0.5/s / fade-out 1.2/s driven by
  `FrontEndMusic::update` from the main loop.
- **Where it starts / stops**:
  - `InteriorWalk::loadRoom` -> `updateMusic()` (the shop's interior key, the
    house's own key at 0.8), `InteriorWalk::exit()` -> `stop()`.
  - `GMShop::enter` (the showroom the dealer door leads to) -> the dealer's key,
    `GMShop::exit()` -> `stop()`; the `SHOP` preset turns it down to 0.5.
  - `GMLogin::enter` already played `login.wav` on client 8 before this work.

Verified headless (build 199):

```
FrontEndMusic: 'Euro/Bnk/frontend/sounds/fe_snd.bnk', 113 place tracks, client 8 (FrontEndMusic)
FrontEndMusic: loaded 'Euro/Bnk/sound/musics/CarDealer.wav' (32.0 s)        <- -interior ECD_FERR
FrontEndMusic: 'ECD_FERRI' -> CarDealer.wav (volume 1.00, 32.0 s, voice 41)
FrontEndMusic: 'ecd_ferri' at gain 1.00 (voice 41)                          <- the fade finished
... B / leave: InteriorWalk: left 'ECD_FERR' -> FrontEndMusic: 'ecd_ferri' stopped
FrontEndMusic: 'ECH_IA' -> ClubIbiza.wav   (the ECH_IAI key does not exist: fallback)
FrontEndMusic: 'EPH_L3DI' -> PlayerHouse02.wav (volume 0.80, 34.8 s)        <- -enterhouse
SHOP mode: FrontEndMusic: 'ECD_FERRI' -> CarDealer.wav (voice 1)           <- -gm shop -shopspot ECD_FERR
```

Flags: `-nofrontendmusic` (no table / no music), `-logfrontendaudio` (loads,
switches, the fade end, stops, missing keys).

---------------------------------------------------------------------------
## 6. Not done / next

- **The screens' music**: `MAP`, `LOBBY`, `REPLAY`, `EDITOR`, `TDMEDIA` have
  table entries and wavs; wiring them needs the screen to own the track (the map
  opens and closes from ~8 places in `gm_freeride.cpp`, and inside a shop the
  interior's track should come back on close).
- **Seller voice lines**: still no data (see the STATE note) — do not invent
  audio.
- The pause menu's first `mopen` arrives **twice** (two players, one frame):
  harmless at 0.3 and probably retail behaviour, but unconfirmed by ear.

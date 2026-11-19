# TDUSC `.bank` sound files (session 59, dug after the PKG pass)

Tags: `[DATA]` read from the owner's own TDUSC install (Steam, E:), `[INF]` inferred. Read-only:
`tools/tdusc/bank_probe.py` never writes into the game folder.

## 1. Layout [DATA]

`WIN32/Sounds/` (106 files, 18 GB) + `Sounds/VEHICLES/` (260 per-car banks): 378 `.bank` files total
(multiplied by 6 languages for the voice ones: RTC, Taxi_Voice_M/F, SolarKitVoices in EN/FR/GE/IT/SP/CH_S).
Every one is a **RIFF `FEV`** file — the *legacy FMOD Designer event* format — with exactly **one embedded
`FSB5` audio bank** somewhere inside it (offset 1.9 KB .. 5.3 MB, varies per bank). FMOD Designer event
data in front, FMOD Studio 2.x audio behind. `fmod.dll` + `fmodstudio.dll` ship with the game; there is no
`fmod_event.dll`, so KT's `fmod.dll` presumably loads both halves.

`Master.bank` / `Master.strings.bank` are FEV too (not FMOD Studio `.strings` files despite the name).
No anti-cheat / DRM touches them; nothing encrypted.

## 2. Embedded `FSB5` version 1 [DATA]

The audio data is `FSB5` **version 1** (`0x04` = 1; classic FMOD Studio 1.x banks use 0x00060000).
Layout confirmed against vgmstream `src/meta/fsb5.c` and the files themselves:

```
0x00 "FSB5"  0x04 version=1      0x08 subsongs     0x0C sample-header (entry) bytes
0x10 name-table bytes          0x14 sample-data bytes       0x18 codec
0x1C 1                          0x20-0x2B zero               0x2C.. 16-byte hash
0x3C entries (variable size)    ... name table (u32 rel offsets + NUL names)    ... data
```

* Sanity check passes on every bank: `0x3C + entries + names + data` == file size minus FEV prefix, and
  the FSB5 runs to EOF (`[DATA]` Audi AI: 60 + 604 + 232 + 1,438,816 = 1,439,712 = 1,466,752 - 27,040).
* Codec byte `0x18` = **0x0F (Vorbis) in all 378 banks** — no PCM/FADPCM/Opus anywhere.
* Entries are variable-size: `u64 mode` then chunks while `mode & 1`:
  `num_samples = mode>>34 & 0x3FFFFFFF`, `data_offset = (mode>>7 & 0x07FFFFFF) << 5`,
  `channels = mode>>5 & 3` (1/2/6/8, chunk 0x01 overrides), `rate = mode>>1 & 0x0F`
  (0=4000 ... 8=44100, 9=48000, 10=96000; chunk 0x02 overrides).
  **Gotcha: the rate field is 4 bits** — a 5-bit mask reads bit 5 (set on ~69 entries) as a bogus index 24/25.
* Chunks: `0x0B` = Vorbis setup + seek table (`u32 0x6d39bf3e`, `u32 byte-count`, then
  (global-ish offset, sample) pairs), `0x0D` = peak volume f32. Both on every entry.
* Name table: `u32` offsets **relative to the table start (including the offset array)**, then NUL-terminated
  names in entry order. Names are clean: `Etron_Sub_1`, `Mercedes_AMG_GTR_horn`, `10 McBazooka - Strike Back`,
  `DEJA VU 2019 - Dave Rodgers`, `RUNNING IN THE 90_S - Max Coveri`, `GAS GAS GAS - Manuel` ...
* Vehicle banks carry per-layer engine sounds (Sub/Turbin_Medium/Turbin_Bass/High_Layer) + horns; some banks
  reuse other cars' content (`PERF_RC_AUDI_ETRON_GTR_AI.bank` embeds a `Mercedes_AMG_GTR_horn`).

## 3. Inventory [DATA]

40,811 embedded sounds, **~163 h of audio**, 18.3 GB compressed (`E:/TDUSC Rips/sounds/banks.csv`):

* `Sounds/*.bank`: ambience (`AmbLoc`, `AmbNight`), 9 radio stations (Classical, EDM, Eurobeat, HipHop,
  IndieRock, Retro, World, IRON_Sharps/Streets; e.g. EDM = 276 sounds / 720 min, Retro = 244 / 701 min),
  music layers (`Bnk_Mus_RaceFlow` 365 sounds / 854 min, `Navigation` 243), UI/GUI (483), voices
  (SolarKitVoices 622-688 per language), Bnk_Common 1,759 sounds.
* `Sounds/VEHICLES/*.bank`: 260 banks, 22,412 sounds; player cars 100-176 sounds each, AI variants ~half.
* `WIN32/PKG` holds **no audio** — TOC of CHUNK_0 part 5 (9,313 entries) mentions sound only in a
  `RADIOS.PACLOG` metadata stub.

## 4. Tool

```
python tools/tdusc/bank_probe.py --bank "<TDUSC>/WIN32/Sounds" --out <folder>     # inventory CSV
python tools/tdusc/bank_probe.py --bank <file.bank> [--limit 20]                  # per-entry dump
```

## 5. Ruled out / next

* The FEV `FMT ` chunk is `{u32 0x8E, u32 0x8C}` (FMOD Designer version + header size?) — not parsed further;
  the event side (categories, event paths, parameters) would need its own pass if modding radio UI matters.
* Extraction to playable files needs FMOD-Vorbis decoding: vgmstream reads FSB5 v1 Vorbis directly
  (point it at the bank, choose subsong), python-fsb5 style reconstruction also possible — not built yet.

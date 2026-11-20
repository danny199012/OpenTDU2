#pragma once

#include "game/scene_editor/scene_doc.h"

#include <string>

// The scene's native export (M5, docs/re/scene_editor.md 6): the
// `add_spot.py` route in C++ (tools/mod/add_spot.py - the mod overlay's
// spots.bin route, docs/re/modding.md). `export_spots` writes
// `<mods>/Euro/Bnk/islands/<island>/level/spots.bin` - a copy of the
// island's own list with the scene's `spot` and `interior` placements
// appended as real 0x110-byte records - and copies every referenced
// interior bank to `<mods>/Euro/Bnk/islands/<island>/exterior/<bank>.bnk`
// (the reader resolves every exterior there; interiors live in `interior/`
// in the game data). The overlay shadows the retail list, so FREERIDE
// streams the scene with no engine change; re-running the export stacks on
// the previous one (the overlay copy is the source, records deduped by id).
//
// The record law is spots.bin's own (spot_exteriors.h): u64 id (the
// 8-slot GetIdentifier64bit of a scene-unique name), char bank[8], vec3
// position, f32 heading, the template record's unknown bytes kept (an
// existing record of the same bank is the template; its category byte at
// +0x100 comes with it).
struct SceneExportResult {
    int32_t Appended = 0;   // records appended this run
    int32_t BanksCopied = 0; // interior banks copied to exterior/
    int32_t Total = 0;      // the exported list's record count
    std::string Path;
    std::string Error;
};

SceneExportResult SceneExportSpots( const SceneDocument &document );

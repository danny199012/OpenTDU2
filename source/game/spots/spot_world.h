#pragma once

#include "core/math.h"

#include <cstdint>
#include <string>
#include <vector>

// <bank>/scenarics/spotworld.bin: the world index of the challenge files (Euro/Bnk/ig2/<name>.ig2): one record per
// challenge with its world start position and the property block of the file's header (docs/re/map_spots_spec.md
// section 12). [DATA: decoded from the file; the writer is the dev build's 0x895890.]
//
//   "SPTW" u32 3, then "CONT" blocks; each record is
//   name[8] (NUL padded), x y z (float LE), u32 id, u32 flags, f32, then an "IGE2" big-endian blob whose "MDSC" chunk
//   is a property list { key[4]; u8 3; u8 sub; u16 size; value[size] } (isla, leme, gmth, nblp, cops, ...).
struct SpotWorldEntry {
    std::string Name;      // the .ig2 file name, upper case (EVHA4TI1, CHA5A04R, RSA7T01 ...)
    Vector3f Position;     // the start: x, height, z
    int32_t Island = -1;   // isla: 0 ibiza, 1 hawai, 2 casino
    int32_t GameMode = -1; // gmth: 0 / 1 / 3 / 7 / 10 / 11 / 12, docs/re/map_spots_spec.md section 12
    int32_t Laps = 0;      // nblp
    int32_t Cops = 0;      // cops
    int32_t TimeLimitMs = 0; // CtDw: the time limit (ms)
    int32_t NameLoc = 0;   // dbna: the loc id of the challenge's name ("Extreme speed")
};

class SpotWorld {
public:
    // Loads the file once (an empty list when it is missing).
    static const std::vector<SpotWorldEntry> &GetAll();
    // The entry of a challenge file name (case-insensitive), nullptr when the file does not list it.
    static const SpotWorldEntry *Find( const std::string &name, int32_t island = -1 );
    static int32_t IslandIndex( const std::string &islandName ); // "ibiza" 0, "hawai" 1, "casino" 2, else -1
};

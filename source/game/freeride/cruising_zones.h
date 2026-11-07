#pragma once

#include <string>
#include <vector>

// The cruising zones of an island (the "Ibiza Area 1 / 2" badge of the HUD):
// islands/<island>/level/cruisingzones.bin, 'CRZODATA' [DATA]:
//   +12 "CRZODATA", +24 size, +32 zone count, +36 DEADBEEF x3, then per zone
//   { u32 index, u32 point count, DEADBEEF x2, count x { f32 x, f32 z } (the
//   last point repeats the first), DEADBEEF x2 }: a closed polygon in world
//   metres. Ibiza 2 zones, Hawai 4, Casino none. The names are the
//   DBCruisingZones rows of the island (_name_hc) in file order [INF: the order].
class CruisingZones {
public:
    void load( const std::string &island );
    // Zone index (0-based) at a world position, -1 outside every polygon.
    int32_t find( float x, float z ) const;
    inline int32_t getCount() const { return static_cast<int32_t>( polygons.size() ); }
    const std::string &getName( int32_t zone ) const;
    inline const std::string &getIsland() const { return island; }

private:
    struct Polygon {
        std::vector<float> X, Z;
        std::string Name;
        int32_t Index = 0; // the index field of the block
    };
    std::string island;
    std::vector<Polygon> polygons;
};

CruisingZones &GetCruisingZones();

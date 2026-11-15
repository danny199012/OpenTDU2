#pragma once

#include "core/math.h"

#include <string>
#include <vector>

// The petrol stations of the island as service stations: the pump screen (fuel) and the repair row
// (docs/re/fuel_gas_station_spec.md, docs/re/damage_spec.md). The shipped world registers no station
// spot (the pumps in the data are decoration), so they are placed from a list:
//
//   <mods>/stations.txt            # island  name  x  z
//                                  ibiza  OIL_Eivissa  27043.6  11678.9
//
// (the debug overlay's F6 mark prints a position to copy). A station needs no model: the building is
// already in the world; the entry only makes the place work (a `fuel` mod prop is the other way, with
// a model of our own).
class ServiceStations {
public:
    struct Station {
        std::string Name;
        Vector3f Position;
    };

    // Reads <mods>/stations.txt: only the lines of this island.
    void load( const std::string &island );
    // The nearest station within maxDistance (flat), nullptr when none.
    const Station *findNear( const Vector3f &position, float maxDistance ) const;
    inline const std::vector<Station> &getAll() const { return stations; }

private:
    std::vector<Station> stations;
};

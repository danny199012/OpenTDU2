#pragma once

#include "core/math.h"
#include "game/spots/obj_mesh.h"

#include <functional>
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
GPUTexture *GetStationAsphalt(); // the generated tarmac texture (shared, owned by the texture cache)

class ServiceStations {
public:
    struct Station {
        std::string Name;
        Vector3f Position;
        Vector3f LightOffset = Vector3f( 0.0f, 0.0f, 0.0f ); // forecourt light grid centre, from Position
        std::vector<ObjMeshDraw> Drive;                      // tarmac drive from the forecourt to the nearest road (tickDrives)
        bool bDriveTried = false;
        Vector3f DriveOrigin = Vector3f( 0.0f, 0.0f, 0.0f ); // world position the drive's local frame sits at
        std::vector<Vector3f> DrivePath;                     // traced centre line of the dirt access track (world x, z); empty: none
    };

    // Reads <mods>/stations.txt: only the lines of this island.
    void load( const std::string &island );
    // The nearest station within maxDistance (flat), nullptr when none.
    const Station *findNear( const Vector3f &position, float maxDistance ) const;
    inline const std::vector<Station> &getAll() const { return stations; }
    // Builds (once, when the car is within 250 m) and draws the tarmac drive of each station: a strip from the forecourt
    // (Position + LightOffset) to 5 m short of the nearest map road, on the terrain. heightFn(x, z, fromY, out), roadFn(from, out).
    void tickDrives( EdenRenderer &renderer, const Vector3f &carPosition, const std::function<bool( float, float, float, float & )> &heightFn,
                     const std::function<bool( const Vector3f &, Vector3f & )> &roadFn );
    void drawDrives( EdenRenderer &renderer ) const;
    void unloadDrives();
    ~ServiceStations() { unloadDrives(); }

private:
    std::vector<Station> stations;
};

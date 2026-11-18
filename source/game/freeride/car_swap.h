#pragma once

#include "core/math.h"
#include "game/spots/obj_mesh.h"

#include <map>
#include <string>
#include <vector>

class CinematicCar;

// The car visual swap (session 57/58): a bring-your-own mod that draws a user
// supplied model (a ripped car, `tools/tdusc/blender_car_split.py`) in place of
// a retail car's visuals. The retail car stays the donor for everything else -
// physics, sound, gearing, wheel nodes, collision - so the swapped car drives
// exactly like the donor and only LOOKS different. Data, not code:
// `carswap/carswap.txt` in the mods overlay,
//
//   [swap:458_italia]            the donor's _car_file_name
//   body = lambo_body            OBJs in carswap/, hung from the donor's BODY_HR
//   body = lambo_glass
//   wheel_front = lambo_rimF lambo_tireF   OBJs centred on the wheel, built for
//   wheel_rear  = lambo_rimR lambo_tireR   the engine-negative-x side (the
//                                          positive side turns them by 180 deg)
//   wheel_pos_f_neg = x y z      the wheel centres (engine space, car origin on
//   wheel_pos_f_pos = x y z      the ground, +y up, +z forward)
//   wheel_pos_r_neg = x y z
//   wheel_pos_r_pos = x y z
//
// A missing file / OBJ leaves the retail look and says so in the log.
// -nocarswap disables it.
struct CarSwapDef {
    std::string Donor;
    std::vector<std::string> Body;
    std::vector<std::string> WheelFront, WheelRear;
    Vector3f WheelPos[2][2]; // [front 0 / rear 1][neg 0 / pos 1]
    bool bWheelPos[2][2] = { { false, false }, { false, false } };
};

class CarSwaps {
public:
    static CarSwaps &get();
    // Reads carswap.txt once (false = no mod data: nothing swaps).
    bool load();
    // Swaps the car's visuals when its file name has a [swap:] block. True when applied.
    bool apply( CinematicCar &car );
    void unloadMeshes();

private:
    CarSwaps() = default;
    bool loadParts( const std::string &name, std::vector<ObjMeshDraw> *&out );
    std::vector<CarSwapDef> defs;
    std::map<std::string, std::vector<ObjMeshDraw>> meshes;
    bool bLoaded = false;
    bool bTried = false;
};

CarSwaps &GetCarSwaps();

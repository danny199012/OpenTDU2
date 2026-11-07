#pragma once

#include "core/math.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// A challenge file (Euro/Bnk/ig2/<name>.ig2: the licence tests RSA7T01 ...,
// the championship races CHA7A01T ... of DBLicense._challengefile_n),
// docs/re/licenses_spec.md 2 [DATA, decoded from the files; readable, not ENC]:
//
//   "IGE2" u32 0x1c (header size) u32 0x306 ...           (big endian)
//   "MDSC" u32 size, u32 count, u32 0, u32 4, u32 0x306, then count keys
//          { char fourcc[4]; u8 3; u8 type; u16 size; value[size] } —
//          timG (target time, ms), CtDw (time limit, ms), VMod (the car),
//          dbna / dbd1 (loc ids of the name / description), isla ...
//   "ENTI" u32 size, u32 count, then entities { char tag[4]; u32 size; ... }:
//          CHK2 (gates): 0x68 bytes { u32 1; 3x4 matrix; vec3; f scale;
//          i32 kind; i32 side; f width; 16 x 0xff } (the manoeuvre tests:
//          kind 0 start / wide gate, 1 slalom gate, 2 gate, 3 finish) or 0x7c
//          bytes { u32 0; u32 road link; u32 x3; f along; the same matrix
//          and tail } (the road tests: every gate 7 m, in route order);
//          DEC2: decor (cones, barriers).
// The matrix rows are (r0, tx), (r1, ty), (r2, tz); the third column is the
// gate's forward (the car's heading at the start).
struct Ig2Gate {
    Vector3f Position;
    Vector3f Forward;    // normal of the gate plane (horizontal, normalized)
    float Width = 7.0f;  // metres
    int32_t Kind = 0;
    int32_t Side = 0;
    bool bRoad = false;  // the 0x7c record (a gate on a road link) [DATA]

    // Kind 1 is a slalom pole (the editor's "Slalom Object"): every kind 1 of
    // a test stands on one straight line and Side alternates 1 / 2, so the
    // pole is at Position and the opening is Width wide on one side of it
    // [DATA: the layout]. Side 1 opens towards +Right, side 2 towards -Right
    // [INF: the retail rule is ENC; the sign only picks which way the first
    // weave goes, the weave itself follows from the alternation].
    inline bool isSlalom() const { return !bRoad && Kind == 1 && ( Side == 1 || Side == 2 ); }
    // The gate frame's x axis (horizontal): the third column is Forward.
    inline Vector3f getRight() const { return Vector3f( Forward.z, 0.0f, -Forward.x ); }
    // The middle of what the car has to drive through.
    inline Vector3f getPassCentre() const {
        return isSlalom() ? Position + getRight() * ( ( Side == 1 ? 0.5f : -0.5f ) * Width ) : Position;
    }
};

// A DEC2 record: the decor of the manoeuvre tests [DATA: 0x54 bytes { u32 1;
// 3x4 matrix; vec3; f scale; u32 type; u32 0 } or 0x68 with the road link
// prefix of the 0x7c gates]. Types 1 -> 2 and 4 -> 5 always come in pairs (66
// and 289 of each over the 58 tests), each pair the two ends of a straight
// line 15-50 m long: the cone lines of the editor ("Cone line": cone_start /
// cone_finish, gmeditor.vmf). The 1 -> 2 lines flank the start and finish
// boxes, the 4 -> 5 lines wall the manoeuvre areas. Type 0 is a single
// object (one in all the files).
struct Ig2Decor {
    Vector3f Position;
    Vector3f Right; // the record's x axis
    int32_t Type = 0;
};

class Ig2Challenge {
public:
    bool load( const std::string &name );
    inline const std::string &getName() const { return name; }
    // gates[0] is the start (the car's pose), the last the finish (a speed
    // challenge has the start only).
    inline const std::vector<Ig2Gate> &getGates() const { return gates; }
    inline const std::vector<Ig2Decor> &getDecors() const { return decors; }
    // VEHI entities: the traffic cars of the overtaking tests [DATA: present in
    // those files only].
    inline int32_t getVehicleCount() const { return numVehicles; }
    int64_t getValue( const char *pKey, int64_t fallback = 0 ) const;
    inline int32_t getTargetMs() const { return static_cast<int32_t>( getValue( "timG" ) ); }
    inline int32_t getLimitMs() const { return static_cast<int32_t>( getValue( "CtDw" ) ); }
    inline int32_t getCarId() const { return static_cast<int32_t>( getValue( "VMod", -1 ) ); }
    // gmth: 0 time attack (and the licence tests), 1 speedtrap ('A' races),
    // 3 race ('R'), 10 speed ('S'), 7 events, 11 / 12 missions [DATA: the
    // suffixes of the championship files and their titles].
    enum eMode : int32_t { ModeTimeAttack = 0, ModeSpeedtrap = 1, ModeRace = 3, ModeSpeed = 10 };
    inline int32_t getMode() const { return static_cast<int32_t>( getValue( "gmth" ) ); }
    // lcpi 0: a circuit of nblp laps (the gates loop back to the start line);
    // 1: point to point [INF: from the course geometry, docs/re/licenses_spec.md 5].
    inline bool isCircuit() const { return getValue( "lcpi", 1 ) == 0; }
    inline int32_t getLaps() const {
        return isCircuit() ? static_cast<int32_t>( Max<int64_t>( 1, getValue( "nblp", 1 ) ) ) : 1;
    }
    // The localized name / description (dbna / dbd1), empty when absent.
    std::string getTitle() const;
    std::string getDescription() const;

private:
    std::string name;
    std::map<std::string, int64_t> values;
    std::vector<Ig2Gate> gates;
    std::vector<Ig2Decor> decors;
    int32_t numVehicles = 0;
};

#pragma once

#include "core/math.h"
#include "filesystem/bank.h"
#include "render/eden/eden_model.h"

#include <memory>
#include <string>
#include <vector>

class EdenRenderer;
class EdenSceneView;

// The race gates drawn in the world: the retail checkpoint pack
// (islands/<island>/scenarics/packs/pack_cp.bnk) [DATA, decoded in
// docs/re/licenses_spec.md 8]. Its entities.xmb holds
// ArrayOf_Entity__Actor__CheckPoint, one record per kind of gate, with the
// field CheckPointType:
//   1 Start_line (+ Start_line_Off)   2 Finish_line (+ _Off, Finishline_Event)
//   3 Check_point (+ CP_OffRoad)      4 Radar (speedtrap)
//   5 SF_Barel_D_act (one barrel)     6 Gate_Main (Actor1 / Actor2: a barrel
//                                       pair, no mesh of its own)
//   7 BrakingZone_01
// and OffRoadCheckpoint (0 road, 1 off road, 2 event). The meshes are the
// holograms with their own .UVA pulses (the "Line_Pulse_*", "*_wave_*"
// animations of the same bank).
//
// Which gate of a course gets which model is ENC (the race code); the port
// shows [INF]: the start line on gate 0, the finish line on the last gate (a
// circuit: on gate 0 for the last lap), the barrel pair on the manoeuvre
// gates of the licence tests, the checkpoint arch everywhere else.
class RaceGates {
public:
    // Trajectory: the 'Trajectory' actor (a flat strip with the arrow texture
    // and its Arrow_Orange / Arrow_Red .UVA scroll); BrakingZone: the type 7
    // checkpoint 'BrakingZone_01' (the sf_target mesh, "stop" texture) [DATA].
    enum class eModel : int32_t { Start, Finish, Checkpoint, Barrels, Radar, Trajectory, BrakingZone, Count };

    struct Gate {
        Vector3f Position;
        Vector3f Forward; // the gate plane's normal (horizontal)
        float Width = 7.0f;
        eModel Model = eModel::Checkpoint;
        bool bNext = false; // the gate to pass now (the others are previews)
        bool bSinglePole = false; // a slalom pole: one barrel at Position
    };

    // Static course furniture drawn for the whole run (the slalom poles, the
    // cone lines, the racing line): one model each, at a position with a
    // heading; Length / Width stretch the model along its z / x (the
    // trajectory strips; 0 keeps the authored size).
    struct Marker {
        Vector3f Position;
        float Heading = 0.0f;
        eModel Model = eModel::Barrels;
        float Length = 0.0f;
        float Width = 0.0f;
    };
    void setMarkers( const std::vector<Marker> &newMarkers ) { markers = newMarkers; }
    inline void clearMarkers() { markers.clear(); }
    inline const std::vector<Marker> &getMarkers() const { return markers; }
    inline bool hasModel( eModel model ) const { return static_cast<bool>( parts[static_cast<int32_t>( model )].Model ); }
    // The model's extent along z at scale 1 (the trajectory strip's length).
    float getModelLength( eModel model ) const;

    ~RaceGates() { unload(); }

    bool load( EdenSceneView &view, const std::string &island );
    void unload();
    inline bool isLoaded() const { return bLoaded; }

    // The gates to show this frame (empty: none).
    void setGates( const std::vector<Gate> &newGates ) { gates = newGates; }
    inline void clear() { gates.clear(); }
    inline const std::vector<Gate> &getGates() const { return gates; }

    // The arch's width at scale 1 (metres across the road), so a gate's
    // pass zone can be matched to what is drawn.
    float getModelWidth( eModel model ) const;

    void draw( EdenRenderer &renderer ) const;

private:
    struct Part {
        std::unique_ptr<EdenModel> Model;
        Matrix4x4f Local;  // the model's root inverse (draws it in the gate frame)
        float Width = 0.0f; // across (x) at scale 1
        float MinX = 0.0f;
        float Length = 0.0f; // along z at scale 1
        float MinZ = 0.0f;
    };

    bool bLoaded = false;
    std::string path;
    Bank bank;
    Part parts[static_cast<int32_t>( eModel::Count )];
    std::vector<Gate> gates;
    std::vector<Marker> markers;
};

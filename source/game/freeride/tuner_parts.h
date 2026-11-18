#pragma once

#include "core/math.h"
#include "game/spots/obj_mesh.h"

#include <map>
#include <string>
#include <vector>

class CinematicCar;
class EdenRenderer;

// The tuner-garage mod's data (docs/TUNER_MOD_PLAN.md 1, session 42): the part
// catalog and the per-car tuner profiles of `tuner/tuner.txt` in the mods
// overlay (the enabled `tuner-garage` package's copy, or a loose file - the
// overlay rules decide). Data, not code: a car or a part joins the garage by a
// text block there, not a rebuild. Prices are the shop's [INF: no retail
// pricing table exists for parts; the tuning shop sells its levels in $12k
// steps].
struct TunerPart {
    std::string Id;             // the save's part id (the [part:<id>] block)
    std::string Name;           // the menu label
    std::string Slot = "spoiler"; // the attach slot (v1: spoilers)
    int32_t Price = 0;
    std::string Mesh;           // the OBJ in tuner/parts/ (our art)
    Vector3f Offset;            // the default body-node-space position
    std::string Node = "BODY_HR"; // the car node it hangs from
    // "rear": the offset is relative to the body's rear-bottom corner (x stays
    // as written, y is above the body bottom, z is past the rear face), so one
    // exhaust fits every car's length.
    std::string Align;
};

// One [car:<_car_file_name>] block: a tuner car and the slots its garage
// offers (a car absent from the file is not a tuner car - the garage refuses
// it).
struct TunerCar {
    std::string FileName;
    std::vector<std::string> Slots;
    std::map<std::string, Vector3f> SlotOffsets; // per-slot position overrides
    float Pops = 1.0f; // exhaust pop strength of this car (0 = none), `pops = ` in tuner.txt
};

class TunerParts {
public:
    static TunerParts &get();

    // Reads tuner.txt (once; a restart re-reads). False when the file is
    // missing - the mod enabled with no data: an empty catalog, every car is
    // "not a tuner car".
    bool load();
    inline bool isLoaded() const { return bLoaded; }
    inline const std::vector<TunerPart> &getParts() const { return parts; }
    inline const std::vector<TunerCar> &getCars() const { return cars; }

    // Is this DBCarPhysicsData row a tuner car (its _car_file_name has a
    // [car:...] block)? The duplicate catalog rows of one car (the GTR ships
    // as two rows) match through their shared file name.
    bool isTunerCar( int32_t carId ) const;
    const TunerCar *findCar( int32_t carId ) const;
    // The part a car's garage offers for a slot (its catalog order), nullptr
    // when the car does not take the slot.
    std::vector<const TunerPart *> partsForSlot( int32_t carId, const char *pSlot ) const;
    // The part by save id (nullptr when unknown - a save carrying a part the
    // catalog lost skips it at load; it never crashes and the data stays).
    const TunerPart *findPart( const std::string &id ) const;
    // The body-node-space position the part takes on this car: the car's
    // `<slot>_offset` override, else the part's default.
    Vector3f offsetFor( int32_t carId, const TunerPart &part ) const;

    // Applies the part to a car (the car borrows the mesh - the buffers stay
    // here, owned once for every car instance that draws it). pPart null =
    // back to stock: the attachment is cleared. Needs the renderer the first
    // time a part's mesh is loaded; without one (or without the OBJ) it
    // applies nothing and says so in the log.
    // pSlot names the slot being set/cleared ("spoiler", "exhaust").
    void applyPart( CinematicCar &car, int32_t carId, const char *pSlot, const TunerPart *pPart );
    // The exhaust pop strength for a car wearing `saved` (0 = not a tuner car):
    // the car's `pops` value, full with an exhaust part fitted, 60 % without.
    float popStrength( int32_t carId, const std::map<std::string, std::string> &saved ) const;
    // Applies a car's whole saved loadout (slot -> part id, the profile's
    // TunerCarParts::Parts): every slot the car takes is set to its saved part
    // or cleared; an id the catalog lost is skipped (the data stays saved).
    void applySaved( CinematicCar &car, int32_t carId, const std::map<std::string, std::string> &saved );
    // Slots the whole catalog uses, in menu order (a car offers its own subset).
    static const char *const *slotNames( size_t &count );
    // The menu label of a slot ("Spoiler", "Exhaust tips").
    static const char *slotLabel( const char *pSlot );
    // Frees the part meshes (game shutdown; the car instances borrow them and
    // drop the borrow on their own unload).
    void unloadMeshes();

private:
    TunerParts() = default;
    const TunerPart *partForMesh( const std::string &meshFile ) const;

    std::vector<TunerPart> parts;
    std::vector<TunerCar> cars;
    bool bLoaded = false;
    std::map<std::string, std::vector<ObjMeshDraw>> meshes; // by OBJ file name
};

// The catalog (loads on first use).
TunerParts &GetTunerParts();

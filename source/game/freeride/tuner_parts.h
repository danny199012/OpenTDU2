#pragma once

#include "core/math.h"
#include "game/spots/obj_mesh.h"

#include <map>
#include <string>
#include <vector>

class CinematicCar;
class EdenRenderer;
struct GPUTexture;

// A tip finish (session 55): the look of an exhaust part - chrome, burnt,
// carbon, gold... - chosen in the garage's "Tip finish" row and saved as
// exhaust_finish in the profile. A finish is a texture swap on the part's
// material, with an optional tint (rgb multipliers baked into the pixels at
// load), so any colour is a data row (gold is chrome.png tinted).
struct TunerFinish {
    std::string Id;      // the save's finish id (the [finish:<id>] block)
    std::string Name;    // the menu label
    std::string Texture; // the PNG in tuner/parts/
    Vector3f Tint = Vector3f( 1.0f, 1.0f, 1.0f );
};

// The tuner-garage mod's data (docs/TUNER_MOD_PLAN.md 1, session 42): the part
// catalog and the per-car tuner profiles of `tuner/tuner.txt` in the mods
// overlay (the enabled `tuner-garage` package's copy, or a loose file - the
// overlay rules decide). Data, not code: a car or a part joins the garage by a
// text block there, not a rebuild. Prices are the shop's [INF: no retail
// pricing table exists for parts; the tuning shop sells its levels in $12k
// steps].
struct TunerPart {
    std::string Id;               // the save's part id (the [part:<id>] block)
    std::string Name;             // the menu label
    std::string Slot = "spoiler"; // the attach slot (v1: spoilers)
    int32_t Price = 0;
    std::string Mesh;             // the OBJ in tuner/parts/ (our art)
    Vector3f Offset;              // the default body-node-space position
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
    std::map<std::string, std::vector<std::pair<Vector3f, Vector3f>>>
        HideBoxes; // slot -> stock-part boxes (item space; repeat the key for
                   // more)
    // Exact triangle masks (hide_tris_<slot> = <item index>: <tri>,<tri>,...):
    // the triangle indices of one body item, where a box would catch
    // neighbour triangles. Repeated keys append.
    std::map<std::string, std::map<size_t, std::vector<uint32_t>>> HideTris;
    float Pops = 1.0f; // exhaust pop strength of this car (0 = none), `pops = `
                       // in tuner.txt
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
    // The tip finishes in catalog order (the menu cycles these; "mesh
    // default" - no finish - is the menu's index -1).
    inline const std::vector<TunerFinish> &getFinishes() const { return finishes; }

    // Is this DBCarPhysicsData row a tuner car (its _car_file_name has a
    // [car:...] block)? The duplicate catalog rows of one car (the GTR ships
    // as two rows) match through their shared file name.
    bool isTunerCar( int32_t carId ) const;
    const TunerCar *findCar( int32_t carId ) const;
    // The part a car's garage offers for a slot (its catalog order), nullptr
    // when the car does not take the slot.
    std::vector<const TunerPart *> partsForSlot( int32_t carId,
                                                 const char *pSlot ) const;
    // The part by save id (nullptr when unknown - a save carrying a part the
    // catalog lost skips it at load; it never crashes and the data stays).
    const TunerPart *findPart( const std::string &id ) const;
    // The finish by save id (nullptr when unknown - same skip rule).
    const TunerFinish *findFinish( const std::string &id ) const;
    // The body-node-space position the part takes on this car: the car's
    // `<slot>_offset` override, else the part's default.
    Vector3f offsetFor( int32_t carId, const TunerPart &part ) const;

    // Applies the part to a car (the car borrows the mesh - the buffers stay
    // here, owned once for every car instance that draws it). pPart null =
    // back to stock: the attachment is cleared. Needs the renderer the first
    // time a part's mesh is loaded; without one (or without the OBJ) it
    // applies nothing and says so in the log.
    // pSlot names the slot being set/cleared ("spoiler", "exhaust").
    // pFinishId: the tip finish to wear (nullptr = the mesh's own material);
    // only meaningful for the exhaust slot, and re-applied by applyFinish
    // when the part stays and just the look changes.
    void applyPart( CinematicCar &car,
                    int32_t carId,
                    const char *pSlot,
                    const TunerPart *pPart,
                    const char *pFinishId = nullptr );
    // Re-colours a fitted part in place (the menu's finish cycle): swaps the
    // attachment material's colour texture. pFinishId null = back to the
    // mesh's own texture. No attachment on the slot = nothing to do.
    void applyFinish( CinematicCar &car,
                      const char *pSlot,
                      const char *pFinishId );
    // The box (item space) of the car's stock part for a slot (the retail
    // spoiler lip / exhaust tips a fitted part replaces); false = none known.
    // The runtime override (setRuntimeHideBoxes) wins: the car viewer's live
    // edits land there before they are written into tuner.txt.
    const std::vector<std::pair<Vector3f, Vector3f>> *
    getHideBoxes( const std::string &carFileName, const char *pSlot ) const;
    // The exact triangle lists (item index -> triangle indices) of a slot;
    // nullptr when the car has none.
    const std::map<size_t, std::vector<uint32_t>> *
    getHideTris( const std::string &carFileName, const char *pSlot ) const;
    // The car viewer's live hide-box edits (not yet in tuner.txt): they
    // override the file's boxes for this car + slot until cleared.
    void setRuntimeHideBoxes(
        const std::string &carFileName,
        const char *pSlot,
        const std::vector<std::pair<Vector3f, Vector3f>> &boxes );
    void clearRuntimeHideBoxes( const std::string &carFileName = std::string(),
                                const char *pSlot = nullptr );
    // The same for the per-slot offsets (offsetFor): the viewer's live nudge
    // of a fitted part writes the whole adjusted value here, and the copy
    // button prints it as the <slot>_offset line for tuner.txt.
    void setRuntimeSlotOffset( const std::string &carFileName,
                               const char *pSlot,
                               const Vector3f &offset );
    void clearRuntimeSlotOffset( const std::string &carFileName = std::string(),
                                 const char *pSlot = nullptr );
    // The exhaust pop strength for a car wearing `saved` (0 = not a tuner car):
    // the car's `pops` value, full with an exhaust part fitted, 60 % without.
    float popStrength( int32_t carId,
                       const std::map<std::string, std::string> &saved ) const;
    // Applies a car's whole saved loadout (slot -> part id, the profile's
    // TunerCarParts::Parts; the optional exhaust_finish key colours the
    // exhaust): every slot the car takes is set to its saved part or cleared;
    // an id the catalog lost is skipped (the data stays).
    void applySaved( CinematicCar &car,
                     int32_t carId,
                     const std::map<std::string, std::string> &saved );
    // Slots the whole catalog uses, in menu order (a car offers its own
    // subset).
    static const char *const *slotNames( size_t &count );
    // The menu label of a slot ("Spoiler", "Exhaust tips").
    static const char *slotLabel( const char *pSlot );
    // Frees the part meshes (game shutdown; the car instances borrow them and
    // drop the borrow on their own unload).
    void unloadMeshes();

private:
    TunerParts() = default;
    const TunerPart *partForMesh( const std::string &meshFile ) const;
    // The finish's tinted texture, created once per finish (the cache owns
    // it; the car materials borrow it). nullptr = not loadable.
    GPUTexture *finishTexture( const TunerFinish &finish );

    std::vector<TunerPart> parts;
    std::vector<TunerFinish> finishes;
    std::vector<TunerCar> cars;
    bool bLoaded = false;
    std::map<std::string, std::vector<ObjMeshDraw>> meshes; // by OBJ file name
    std::map<std::string, GPUTexture *> finishTextures;     // by finish id
    // The viewer's live hide-box edits: (car file name, slot) -> boxes.
    std::map<std::pair<std::string, std::string>,
             std::vector<std::pair<Vector3f, Vector3f>>>
        runtimeHideBoxes;
    // The viewer's live offset nudges: (car file name, slot) -> the whole
    // offset.
    std::map<std::pair<std::string, std::string>, Vector3f> runtimeSlotOffsets;
};

// The catalog (loads on first use).
TunerParts &GetTunerParts();

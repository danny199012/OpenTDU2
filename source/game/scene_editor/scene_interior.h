#pragma once

#include "core/math.h"
#include "physics/havok_collision.h"
#include <string>
#include <vector>

// Interior banks as scene-editor assets (M4, docs/re/scene_editor.md 4): the
// game's walkable rooms (`interior/i<name>.bnk` of a shop spot, `lph_*` /
// `gph_*` of a house) have the same bank layout as the exteriors, so the
// editor draws them through the same streaming path and walks them on the
// bank's own `.shk` collision.
//
// A scene `interior` object's `ref` names the room three ways (what the
// retail data itself keys, spot_interiors_spec.md 1):
//   - the interior bank stem          `icd_ferr`, `ias_a`, `lph_l2e`, `gph_l2e`
//   - a shop spot bank                `ECD_FERR` -> its `icd_ferr`
//   - a house spot (its spots.bin     `EPH_L2E_0345I` -> `lph_l2e` (living);
//     name or bank prefix)              `EPH_L2E_0345I:garage` -> `gph_l2e`
// The rules are InteriorWalk's own (InteriorBankFor / HouseBankFor); here
// they resolve against the game data (a bank only counts when the file
// exists).
namespace SceneInterior {

// The interior bank stems of the game data (`interior/*.bnk`), sorted.
std::vector<std::string> ListBanks();

// ref -> the full bank path (`<bankFolder>/interior/<stem>.bnk`), "" when the
// ref names nothing (or the file is missing). Accepts the three forms above.
std::string ResolveBankPath( const std::string &ref );

// A display label for a resolved bank (the stem; "icd_ferr (ECD_FERR)" when
// it came from a spot ref).
std::string DescribeRef( const std::string &ref );

// The walk data of an interior bank: its `.shk` collision (bank frame) and
// the retail walk start (Entity__FirstPerson { StartCamera = Entity__Camera
// __Hc }: cam_entree in every bank checked, spot_interiors_spec.md 2) as a
// bank-local eye pose. bHasStart false: the bank has no FirstPerson camera
// (the caller starts from the collision bounds instead).
struct WalkData {
    HavokCollision Collision;
    Vector3f StartEye = Vector3f( 0.0f, 1.8f, 0.0f );
    Quaternionf StartHeading = Quaternionf( 0.0f, 0.0f, 0.0f, 1.0f );
    bool bHasStart = false;
    std::string StartCameraName;
};

// Loads the bank, extracts the walk data, releases the bank (the collision is
// a plain copy, nothing GPU). False when the bank or its entities.xmb cannot
// be read (Collision stays empty: the caller may still walk the grounds).
bool LoadWalkData( const std::string &bankPath, WalkData &out );

} // namespace SceneInterior

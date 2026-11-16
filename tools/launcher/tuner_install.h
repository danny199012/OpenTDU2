#pragma once

// The tuner-garage mod's one-time install (docs/TUNER_MOD_PLAN.md): a C++ port of
// tools/mod/{add_spot,make_tuner_interior,make_revyard_interior}.py so a player needs no
// Python. From the user's OWN game files it builds
//
//   <userdata>\mods\Euro\Bnk\interior\itnr_a.bnk   Wangan Works  (itun_a.bnk re-skinned dark / neon)
//   <userdata>\mods\Euro\Bnk\interior\itnr_b.bnk   Rev Yard      (icdc_wld.bnk re-skinned dark / neon)
//   <userdata>\mods\Euro\Bnk\islands\<island>\level\spots.bin      + the ETNR_* spot records
//   <userdata>\mods\Euro\Bnk\islands\<island>\exterior\etnr_*.bnk  (copies of the tuning shop's exterior)
//
// into the loose mods folder (NOT the mod package): that folder is always read, so a mod that is
// switched off again still leaves the shops enterable (the game then locks their menus). Nothing
// derived from the game is ever stored in the repo; our own art (PNG) comes from the package folder.
//
// The module links nothing of the engine: it is used by the launcher (with a progress callback)
// and by the headless `--install-tuner` test mode.

#include <functional>
#include <string>

namespace Launcher {

constexpr int kTunerInstallVersion = 1;

struct TunerInstallOptions {
    std::wstring GameRoot;      // the unpacked game folder (contains Euro\Bnk)
    std::wstring UserData;      // the user data folder (mods\ lives inside)
    std::wstring PackageFolder; // <userdata>\mods\packages\tuner-garage (interior\art, interior\art_b)
};

// fraction 0..1 and a short line of what is being done; return false to cancel.
using TunerProgress = std::function<bool( float fraction, const std::string &what )>;

// True when the install marker of the current version is in the user's mods folder.
bool TunerInstalled( const std::wstring &userData );

// Builds everything above. False + `error` on a missing input / write failure / cancel.
// Safe to repeat (overwrites its own outputs; an existing spots.bin of the overlay is extended,
// never replaced, and a spot that is already in it is not added twice).
bool InstallTunerMod( const TunerInstallOptions &options, const TunerProgress &progress, std::string &error );

} // namespace Launcher

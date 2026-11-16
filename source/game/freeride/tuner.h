#pragma once

#include <string>
// The tuner garage is an opt-in mod (owner request 2026-11-16,
// docs/TUNER_MOD_PLAN.md): the retail game has no visual tuning parts, so a
// vanilla run has no tuner garage spot, no part menu, and no parts applied.
// It is on when the launcher's Mods page enabled the `tuner-garage` package
// (<userdata>/mods/enabled.txt); `-tunermod` forces it on and `-notunermod`
// off, for tests.
//
// Mod disabled = parts locked, never lost: the per-car parts stay saved
// (PlayerProfile::OwnedCarTunerParts) and come back whole when the mod is
// re-enabled. A disabled mod just means nothing is applied and the garage
// spot is not offered.
bool TunerModEnabled();
// The player-facing name of a tuner shop by its spot bank: ETNR_A is Wangan
// Works (the tuning-shop room), ETNR_B the Rev Yard (the used-car-dealer room
// re-built as a workshop, make_revyard_interior.py).
inline const char *TunerShopName( const std::string &spotBank ) {
    return spotBank.size() >= 6 && ( spotBank[5] == 'B' || spotBank[5] == 'b' ) ? "Rev Yard" : "Wangan Works";
}

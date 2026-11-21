#pragma once

#include <memory>

// The racing school instructors' end-of-test lines (docs/re/licenses_spec.md 12.4 / 12.5): pack_racingschool__<lang>.bnk holds
// sound/voices/<lang>/rs{a,b,c}/rs?_endtest_win_01..02 and rs?_endtest_loose_01..04 (Marco / Jane / Todd) [DATA]. Retail plays one
// with the end-of-test cinematic (M_<SCHOOL>_WIN_nn / _LOOSE_nn); the port plays the line under the result cart [INF: the pick
// among the numbered lines is random]. -noschoolvoice turns it off.
namespace SchoolVoice {

// letter: the school (A asphalt / B dirt / C classic); bWon: the test passed.
void PlayEndOfTest( char letter, bool bWon );
void Stop();

} // namespace SchoolVoice

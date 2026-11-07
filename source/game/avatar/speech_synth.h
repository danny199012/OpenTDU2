#pragma once

#include "game/avatar/facefx.h"

#include <string>

// Procedural speech: a FaceFX animation built from plain text, for dialog the
// game never recorded (docs/re/custom_animations.md).
//
// The retail game has no phoneme-level lip sync: every voice line ships a
// pre-baked FaceFX animation (.fxe) whose curves are keyed on the actor's
// phoneme targets (docs/re/facefx_spec.md 1). This module makes the same kind
// of animation *from text*: an English grapheme -> phoneme -> FaceFX target
// pass with fixed durations, written as FxAnimCurves named exactly like the
// actor graph's nodes, so FaceFxLibrary::Evaluate drives the 42 facial bones
// through the retail path (it looks a curve up with FaceFxActor::findNode).
//
// A curve whose name is not a node of the actor is ignored by Evaluate, so an
// unknown target degrades to "that mouth shape does nothing" - never to a
// broken pose.
//
// Targets are the 10 phoneme targets of the actor's FxPhonemeMap
// (docs/re/facefx_spec.md 2.1): open W ShCh PBM FV wide tBack tRoof tTeeth,
// plus "Blink" (an eyelid node of the same graph).
struct SpeechAnimation {
    // Builds outAnim from pText. False when there is nothing to say (no
    // letter in the text). The animation is named "OTDU_Say" and is 0.15 s
    // longer than the last phoneme, so it can be looped by the caller.
    static bool Build( const char *pText, FaceFxAnim &outAnim );

    // The FaceFX target node names Build writes curves for (tests and logs).
    static const char *const kTargets[10];
};

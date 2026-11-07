#pragma once

// Authored captions for a cinematic (accessibility; the retail data ships no
// subtitle text for the cutscene speech - the voice lines are wav only, only
// phone calls have loc text - so the text is authored here and the engine
// draws it). The file is <userdata>/cine_subtitles/<cinematic>.csv, one cue
// per line:
//   start,end,"text"
// with the times in cinematic seconds ('#' lines are comments; quotes inside
// the text escaped as ""). The manifest command of otdu2_cine_tool writes a
// .csv.skeleton with one empty row per voice cue, and -cinesubdump writes the
// same skeleton from the engine for the cinematic being played.
#include "game/cinematic/cinematic_scene.h"

#include <string>
#include <vector>

class Renderer2D;
class Cinematic;
class CinematicSounds;

class CineSubtitles {
public:
    struct Cue {
        float Start = 0.0f;
        float End = 0.0f;
        std::string Text;
    };

    // Loads <userdata>/cine_subtitles/<name>.csv (the cinematic definition
    // name, e.g. C_MEETING_TESS). False when there is no file (no captions).
    bool load( const std::string &cinematicName );
    bool empty() const { return cues.empty(); }
    inline const std::vector<Cue> &getCues() const { return cues; }
    // The active caption at a cinematic time, or "" (last cue covering time).
    const std::string &textAt( float time ) const;

    // The caption bottom-center on the 2D canvas (a dark backing band for
    // readability). Nothing without an active cue.
    void draw( Renderer2D &renderer, float time ) const;

    // The skeleton for a cinematic: one empty row per voice cue (the .cin Fx
    // events and the .sce scene Fx tracks whose Fx__Root is an LSGL voice
    // bank, scene times mapped through the shots). Writes
    // <userdata>/cine_subtitles/<name>.csv.skeleton and never overwrites an
    // existing .csv. Returns the path written, "" when nothing was.
    static std::string DumpSkeleton( const std::string &cinematicName,
                                     const Cinematic &cine,
                                     const std::vector<CinematicScene> &scenes,
                                     const CinematicSounds &sounds );

    static std::string Folder();

private:
    std::vector<Cue> cues;
};

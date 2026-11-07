#pragma once

#include "game/licenses.h"

#include <string>
#include <vector>

class Renderer2D;

// A championship's race list (native; docs/re/licenses_spec.md 5): opened at
// the championship's start (its first race's start gate). The races are run
// in order, like the licence tests [INF]; speed challenges are listed but not
// run (their scoring is not decoded).
class ChampionshipMenu {
public:
    void open( const LicenseInfo &championship );
    void close();
    inline bool isOpen() const { return pChampionship != nullptr; }
    inline const LicenseInfo *getChampionship() const { return pChampionship; }
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // One shot: the race chosen (its .ig2 name).
    bool takeStartedRace( std::string &outRace );

private:
    struct RaceRow {
        std::string Name;
        std::string Title;
        float Target = 0.0f; // time attack: timG; race / speedtrap: the limit CtDw
        int32_t Laps = 1;
        bool bOpen = false;
    };

    const LicenseInfo *pChampionship = nullptr;
    std::vector<RaceRow> races;
    int32_t selected = 0;
    std::string startedRace;
    std::string message;
    float messageTimer = 0.0f;
};

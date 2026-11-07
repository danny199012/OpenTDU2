#pragma once

#include <string>
#include <vector>

class Renderer2D;
class Font;

// The casino's Game Progress page: the DBCasinoObjectiveDetail objectives of
// one axis at a time, with the level, the points and the state of each (done,
// or the counter against its target).
//
// The retail casino pause list carries a `game_progress` entry (setSquareMenu's
// IsCasino branch) and the shell reads /:isProgressCasino; this is that page.
// It is native, like the rest of the casino screens, and every row comes from
// the game's own table (the text is DBCasinoObjectiveDetail._text_hc, the
// counters from PlayerProfile::CasinoObjectives).
class CasinoObjectivesScreen {
public:
    void open();
    void close();
    inline bool isOpen() const { return bOpen; }

    // Up / Down move the selection, Left / Right change the axis, B / Backspace
    // closes. Returns true when the player asked to close.
    bool tick( float deltaTime );
    void draw( Renderer2D &renderer, const Font *pFont );

    // The Flash markup of an objective text (#color:RRGGBB#, #nocolor#, ...) is
    // dropped for display: the Eden text fields would resolve it, a native draw
    // has to.
    static std::string stripMarkup( const std::string &text );

private:
    void rebuildAxes();
    // The objectives of the current axis, in table order.
    void rebuildRows();

    struct Row {
        int32_t ObjectiveIndex = 0; // index into GetCasinoObjectives()
        std::string Text;
        int32_t Counter = 0;
        int32_t Target = 0;
        int32_t Points = 0;
        bool bDone = false;
    };

    bool bOpen = false;
    std::vector<int32_t> axes; // the axes that have objectives, in order
    int32_t axisIndex = 0;     // index into `axes`
    std::vector<Row> rows;
    int32_t selected = 0;
    int32_t scroll = 0;
    float stickHold = 0.0f; // the D-pad / arrow repeat
    float stickRepeat = 0.0f;
};

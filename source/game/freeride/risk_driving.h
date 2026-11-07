#pragma once

#include <functional>
#include <string>

// FRIM (the retail name: the HUD movie hud.bnk /HUD/FRG, "Free-Ride Instant
// Money"). The laws are the retail ones [CODE: TestDrive2.exe 0xa76cb0 tick,
// 0xa767c0 gauge rate, 0xa769e0 level up / down, 0xa752fc perf points,
// 0xa76e50 FSCommands, 0xa76bd0 hit, 0xa767f0 reset; PS3 func 0x7c8d24 HUD
// variables, 0x7c9eb0 config copy, 0x7cae18 rate curve] and every number is
// the game's own tuning file scenarics/frpoints.cpr [DATA] (docs/re/frim_spec.md):
//  - the gauge (PerfPts) gains rate(speed) per second, the DEC_SPEED curve:
//    piecewise linear over val1..4 x the car's top speed -> pts1..4 (-30 / 0 /
//    3 / 8 by default: slow driving drains it);
//  - every trick adds its points to the gauge (ESQUIVE near miss, SAUT jump,
//    DERAPAGE drift), scaled by CALCUL_PTS;
//  - gauge >= PP_TARGET[level + 1] -> level + 1, the rest carries over (level
//    10 is the top: the gauge stops); a new highest level makes the bank
//    available (the movie's fr_gge/bank "on", the [A] BANK prompt);
//  - gauge < 0 above level 0 with no bank pending -> level - 1 (the gauge
//    carries over from the level below); at level 0 it stays at 0;
//  - the bank (the player's A, the movie's frgp:bank) pays the money table of
//    the level (PA_TARGET, 0 / 100 / 200 ... 4000) and resets the chain;
//  - a hit (contact with traffic) only plays the HUD "hit" for 2 s - no money,
//    no level is lost.
class RiskDriving {
public:
    struct Frame {
        float DeltaTime = 0.0f;
        float SpeedKmh = 0.0f;
        float MaxSpeedKmh = 250.0f;   // the car's top speed (the curve's x scale)
        bool bOnGround = true;
        float X = 0.0f, Y = 0.0f, Z = 0.0f; // car position (jump height / length)
        int32_t NearMisses = 0;       // near misses with traffic this frame
        float NearMissGap = 0.0f;     // gap of the last one (m)
        float NearMissClosing = 0.0f; // its closing speed (m/s)
        float SlipAngle = 0.0f;       // body slip angle, velocity vs heading (rad)
        bool bCrash = false;          // contact with another car (types != 0/3)
    };

    // The tuning of scenarics/frpoints.cpr; the defaults are the file's values.
    struct Config {
        float CalculPts = 1.0f;
        float DecSpeedVal[4] = { 0.0f, 0.4f, 0.7f, 1.0f };
        float DecSpeedPts[4] = { -30.0f, 0.0f, 3.0f, 8.0f };
        float PaTarget[11] = { 0, 100, 200, 350, 500, 750, 1000, 1500, 2000, 2500, 4000 };
        float PaTarget2[11] = { 0, 200, 350, 600, 950, 1500, 2200, 3500, 5000, 7500, 10000 };
        float PpTarget[11] = { 0, 300, 350, 400, 500, 600, 700, 800, 900, 1000, 1200 };
        // ESQUIVE (near miss): gap min..max -> pts min..max, closing speed
        // (km/h) min..max -> x min..max, at or above SPEED (km/h).
        float DodgeDistMin = 2.3f, DodgeDistMax = 0.0f, DodgePtsMin = 70.0f, DodgePtsMax = 150.0f;
        float DodgeDiffMin = 30.0f, DodgeDiffMax = 200.0f, DodgeMulMin = 1.0f, DodgeMulMax = 1.5f;
        float DodgeSpeed = 90.0f;
        // SAUT (jump): height and length ranges -> pts, at or above SPEED.
        float JumpHeightMin = 0.5f, JumpHeightMax = 4.0f, JumpHeightPtsMin = 10.0f, JumpHeightPtsMax = 100.0f;
        float JumpLengthMin = 5.0f, JumpLengthMax = 60.0f, JumpLengthPtsMin = 20.0f, JumpLengthPtsMax = 300.0f;
        float JumpSpeed = 30.0f;
        // DERAPAGE (drift): distance range -> pts, SPEED, SLIP_ANGLE (rad).
        float DriftDistMin = 5.0f, DriftDistMax = 50.0f, DriftPtsMin = 30.0f, DriftPtsMax = 230.0f;
        float DriftSpeed = 25.0f, DriftSlip = 0.4f;
    };
    // Reads scenarics/frpoints.cpr (false: the defaults stay).
    static bool LoadConfig( Config &outConfig );

    RiskDriving();

    int32_t tick( const Frame &frame );
    // The HUD variables (PS3 0x7c8d24): PALevel / PerfPts / PerfPtsTarget /
    // PACurLevel / PANextLevel / PATotal.
    inline int32_t getLevel() const { return level; }
    inline int32_t getHudLevel() const { return level <= 9 ? level + 1 : level; }
    inline int32_t getProgress() const { return level <= 9 ? static_cast<int32_t>( progress ) : 1; }
    inline int32_t getThreshold() const { return level <= 9 ? static_cast<int32_t>( config.PpTarget[level + 1] ) : 1; }
    int32_t getMoneyForLevel() const;
    int32_t getMoneyForNextLevel() const;
    inline int32_t getTotal() const { return total; }
    // The last trick: its points (the LastPts badge), its icon ("dodge" /
    // "jump" / "drift"), and a one-shot flag (consumed by FreeRideHud::setRisk).
    inline int32_t getLastPoints() const { return lastPoints; }
    inline const std::string &getPerfType() const { return perfType; }
    inline bool takeNewPerf() { const bool b = bNewPerf; bNewPerf = false; return b; }
    inline bool takeHit() { const bool b = bHit; bHit = false; return b; }
    // The bank is available (a new highest level was reached and not banked
    // yet: the movie's [A] BANK prompt; retail +0x264).
    inline bool isBanking() const { return bBankAvailable; }
    // The player asked to bank (A): pays when the movie confirms with
    // frgp:bank, or after a short wait if the movie does not.
    void requestBank();
    // The movie's FSCommands frgp:bank (pay + reset) / frgp:end_bank.
    void onBankFSCommand( const std::string &command );
    inline void setOnBank( std::function<void( int32_t amount )> callback ) { onBank = std::move( callback ); }
    // The prize table: PA_TARGET, or PA_TARGET2 when the retail selector picks
    // it [INF: the selector 0x445780 is encrypted; the port keeps PA_TARGET].
    inline void setSecondMoneyTable( bool bSecond ) { bSecondTable = bSecond; }
    void reset();
    inline const Config &getConfig() const { return config; }
    // -riskselftest: scripted frames, logs the gauge / levels / bank.
    static void selfTest();

private:
    void addPerfEvent( float points, const char *pPerfType );
    float rate( float speedKmh, float maxSpeedKmh ) const;
    void updateLevel();
    void bank();

    static constexpr float kHitTime = 2.0f;    // the hit state (0xeddb40) [CODE 0xa76bd0]
    static constexpr float kBankWait = 1.5f;   // pay anyway if the movie's frgp:bank does not come [INF]

    Config config;
    float progress = 0.0f; // +0x254
    int32_t level = 0;     // +0x258
    int32_t bestLevel = 0; // +0x260
    int32_t total = 0;     // +0x25c
    bool bBankAvailable = false; // +0x264
    bool bSecondTable = false;
    float hitTimer = 0.0f;
    bool bBankRequested = false;
    float bankWait = 0.0f;
    // Jump / drift trackers.
    bool bAirborne = false;
    float takeoffX = 0.0f, takeoffY = 0.0f, takeoffZ = 0.0f, takeoffSpeed = 0.0f, peakY = 0.0f;
    float driftDistance = 0.0f;
    float lastX = 0.0f, lastZ = 0.0f;
    bool bHasLast = false;
    int32_t lastPoints = 0;
    std::string perfType;
    bool bNewPerf = false;
    bool bHit = false;
    std::function<void( int32_t )> onBank;
};

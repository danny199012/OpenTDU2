#include "shared.h"
#include "pause_menu.h"

#include "database/gs_database.h"
#include "flash/flash_localization.h"
#include "flash/gs_flash.h"
#include "config/gs_config.h"
#include "game/advanced_graphics.h"
#include "game/game_options.h"
#include "game/player_profile.h"
#include "game/car_shop.h"
#include "game/skill_levels.h"
#include "game/world_clock.h"
#include "input/gs_input.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "filesystem/gs_file.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <deque>
#include <fstream>

namespace {
    // db_us.cpr line ids (found with -dumploc: "Pause" 51, "Resume" 213,
    // "Options" 466, "Quit" 519, "Game progress" 605, "Players" 3868,
    // "Take photo" 957, "Editor" 129, "Profiles" 1386, "Exit game" 1605).
    constexpr int32_t kLocPause = 51;
    constexpr int32_t kLocResume = 213;
    constexpr int32_t kLocOptions = 466;
    constexpr int32_t kLocQuit = 519;
    constexpr int32_t kLocGameProgress = 605;
    constexpr int32_t kLocPlayers = 3868;
    constexpr int32_t kLocTakePhoto = 957;
    constexpr int32_t kLocEditor = 129;
    constexpr int32_t kLocProfiles = 1386;
    constexpr int32_t kLocExitGame = 1605;

    // The 16:9 stage band (SystemPC.cpr screen ratio): stage y 120..840 maps to
    // the window, stage x 0..1280 is 1:1. Stage position -> window y offset.
    constexpr float kStageTopOffset = 120.0f;

    // Length of the pause_out animation the world stays frozen for.
    constexpr float kCloseDuration = 0.5f;

    // The pause screen entry model. MAIN.vmf builds its own list in the root's
    // setSquareMenu() (frame 35 script), but that script returns early in this
    // runtime (numSq stays undefined; the guard was not chased to the end), so
    // the game drives the squares directly instead — the same protocol the
    // movie itself uses (menu_sq.ActionLoop: tellTarget(sqN)
    // gotoAndPlay(state)).
    //
    // The retail list per game mode (setSquareMenu, -logpause disassembly —
    // WhatGameMode 1 is freeride / explore, the mode this build plays; 2 builds
    // the race list restart / quit_race):
    //   resume / game_progress / players / [xbox_live_party, X360 only] /
    //   photo / gme / options / [profiles, PC only] / exit_game
    // Each entry pushes with its lock variable (LockResume, LockGameProgress,
    // LockPlayers, LockSnapshot, LockEditor, LockOptions, LockProfiles,
    // LockExitGame): a locked entry is grayed out and skips. game_progress /
    // players / gme / profiles do not exist in this build yet, so they are
    // listed and locked — the row matches the console frontend visually and
    // unlocks entry by entry as the features land. 'photo' is unlocked: it
    // opens the free photo camera (PhotoCamera, the retail m_TakeSnapshot_).
    // (Race modes use a different list: stop_test / restart / quit_race...)
    //
    // Square states (labels of the sq0..sq8 timelines, 208 frames each):
    //   default  parked / empty (content alpha 0 there)
    //   open     idle entry; open_lock the locked variant, on the selected one,
    //            on_lock a selected locked one
    //   valid    validate flash
    // The column (menu_sq, 52 frames): default -> open -> wait -> stop, close.
    struct MenuEntry {
        int32_t LocId;
        const char *pFallback;
        PauseMenu::eResult Result;
        const char *pIcon; // frame label of the attached 'specificIcons' symbol
        // false = locked. The retail lock state of an entry whose feature the
        // port does not have (players / profiles are the online screens, gme is
        // the editor): the movie greys it out and its Move() skips it, so the
        // entry cannot be selected into a screen that does not exist. Set the
        // flag true as soon as the feature is built (game_progress, photo).
        bool bEnabled;
        // Writing icLoaded:_x/_y resets whatever transform the movie's own
        // scripts left on the icon holder — that stale offset is what drew the
        // icons at the square corners (the place tags read (0,0), the instances
        // carried e.g. (−770,−770)). Per-art residuals (px, applied directly as
        // the _x / _y property). All 0 since FlashPlayer::placeObject keeps the
        // matrix on a replace-move (session 12): the old non-zero values
        // compensated for that runtime bug. Kept for tuning.
        float icX;
        float icY;
    };

    // Freeroam entries (FREERIDE / FIRST_WM), in the setSquareMenu order. All
    // the icon labels exist in 'specificIcons' (icLoaded, 386 frames: gme,
    // xbox_live_party, game_progress, resume, resume_cod, quit_race, players,
    // photo, profiles, exit_game, leaderboards, achievement, options,
    // help_options, restart, stop_test, quit_cod...). Options reports to the
    // game mode as well (its screen is the movie's OPTIONS sprite, attached by
    // the root's LaunchOptions script).
    constexpr MenuEntry kEntries[] = {
        {kLocResume,       "Resume",        PauseMenu::eResult::Resume,   "resume",    true,
         0.0f,                                                                                      0.0f},
        {kLocGameProgress, "Game Progress", PauseMenu::eResult::Progress,
         "game_progress",                                                              true, 0.0f, 0.0f},
        {kLocPlayers,      "Players",       PauseMenu::eResult::None,     "players",   false,
         0.0f,                                                                                      0.0f},
        {kLocTakePhoto,    "Take photo",    PauseMenu::eResult::Photo,    "photo",     true,
         0.0f,                                                                                      0.0f},
        {kLocEditor,       "Editor",        PauseMenu::eResult::None,     "gme",       false, 0.0f,
         0.0f                                                                                           },
        {kLocOptions,      "Options",       PauseMenu::eResult::Options,  "options",   true,
         0.0f,                                                                                      0.0f},
        {kLocProfiles,     "Profiles",      PauseMenu::eResult::None,     "profiles",  false,
         0.0f,                                                                                      0.0f},
        {kLocExitGame,     "Exit game",     PauseMenu::eResult::Quit,     "exit_game",
         true,                                                                                0.0f, 0.0f},
    };
    constexpr int32_t kEntryCount =
        static_cast<int32_t>( sizeof( kEntries ) / sizeof( kEntries[0] ) );
    constexpr float kTabRowLift = 30.0f; // stage px, see the gap note in tick
    // (18 left the card's top edge touching the icons' bottom edge - measured
    // y239 vs y240 on the 720p capture; retail keeps ~10 px of daylight there,
    // docs/images/retail-pause-freeride.webp, so the row is raised the rest.)
    constexpr int32_t kSquareCount = 9;  // sq0..sq8 of menu_sq
    // Indices of the entries the game modes control (setEntryEnabled).
    constexpr int32_t kEntryResume = 0;
    constexpr int32_t kEntryGameProgress = 1;
    constexpr int32_t kEntryPlayers = 2;
    constexpr int32_t kEntryPhoto = 3;
    constexpr int32_t kEntryEditor = 4;
    constexpr int32_t kEntryOptions = 5;
    constexpr int32_t kEntryProfiles = 6;
    constexpr int32_t kEntryExit = 7;

    // The island badge frames of the map (the i1 / i2 sprite labels: h1..h4, h,
    // i1, i2, i, casino — one authored art per island / club level). The
    // streamed world of this build is Ibiza.
    constexpr const char *pIslandBadge1 = "i1";
    constexpr const char *pIslandBadge2 = "i2";

    std::string StringJoin( const std::vector<std::string> &parts,
                            const char *pSep ) {
        std::string joined;
        for ( size_t i = 0; i < parts.size(); i++ ) {
            if ( i != 0 ) {
                joined += pSep;
            }
            joined += parts[i];
        }
        return joined;
    }

    // The card's money in the retail shape ("$27,000"): the movie prints the
    // /:Money variable raw (the retail game pushes the formatted text, SURVEY
    // item 10 - the port pushed the number and showed e.g. 19990).
    std::string FormatMoney( int32_t value ) {
        std::string digits = std::to_string( value < 0 ? -value : value );
        std::string out;
        for ( size_t i = 0; i < digits.size(); i++ ) {
            if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
                out += ',';
            }
            out += digits[i];
        }
        return ( value < 0 ? "-$" : "$" ) + out;
    }

    // -logpause: the display list of the pause screen (instance names,
    // positions, masks) — the entries of MAIN.vmf are the instances named
    // resume / options / quit; an entry missing here is a movie/AVM1 issue, an
    // entry present but not drawn is a rendering one.
    void DumpDisplayList( const FlashPlayer::Clip &clip,
                          int32_t level,
                          const FlashDefinitions *pDefs ) {
        if ( level > 6 ) {
            return;
        }
        for ( const auto &entry : clip.DisplayList ) {
            const FlashPlayer::Instance &instance = *entry.second;
            OTDU_LOG_INFO(
                "  %*s[%u] '%s' visible %d mask %u at (%.0f, %.0f) scale "
                "(%.4f, %.4f) sprite %d frame %u\n",
                level * 2, "", entry.first, instance.Name.c_str(),
                instance.bVisible ? 1 : 0, instance.ClipDepth,
                instance.Matrix.TX, instance.Matrix.TY, instance.Matrix.A,
                instance.Matrix.D, instance.pClip != nullptr ? 1 : 0,
                instance.pClip != nullptr ? instance.pClip->CurrentFrame : 0 );
            // Text fields: binding + default (a badge shows its authored
            // default until the game provides the value: 'HAWAI 7').
            if ( instance.pClip == nullptr && pDefs != nullptr ) {
                const FlashEditText *pText =
                    pDefs->getEditText( instance.CharacterId );
                if ( pText != nullptr ) {
                    OTDU_LOG_INFO( "  %*s  text id %u var '%s' default '%s'\n",
                                   level * 2, "", instance.CharacterId,
                                   pText->VariableName.c_str(),
                                   pText->Text.c_str() );
                }
            }
            if ( instance.pClip != nullptr ) {
                DumpDisplayList( *instance.pClip, level + 1, pDefs );
            }
        }
    }

    // AVM1 disassembler for the -logpause probe: constant pools, Push values,
    // function definitions, jumps. Names match FlashPlayer's eAction table.
    void DumpActions(
        const std::vector<uint8_t> &data,
        uint32_t offset,
        uint32_t length,
        uint32_t frame,
        const std::vector<std::string> *pFunctionConstants = nullptr ) {
        const uint32_t end = Min<uint32_t>(
            offset + length, static_cast<uint32_t>( data.size() ) );
        uint32_t pc = offset;
        std::vector<std::string> constants;
        if ( pFunctionConstants != nullptr ) {
            // The constant pool of a user function is carried by its
            // DefineFunction action (not by an inline ConstantPool): the Push
            // constant indices resolve against it.
            constants = *pFunctionConstants;
        }
        int32_t guard = 0;
        while ( pc < end && guard++ < 4096 ) {
            const uint8_t code = data[pc];
            uint16_t actionLength = 0;
            uint32_t payload = pc + 1;
            if ( code >= 0x80 && pc + 3 <= end ) {
                actionLength = data[pc + 1] | ( data[pc + 2] << 8 );
                payload = pc + 3;
            }
            const uint32_t next = Min<uint32_t>( payload + actionLength, end );
            auto readString = [&]( uint32_t at ) -> std::string {
                std::string s;
                while ( at < next && data[at] != 0 ) {
                    s += static_cast<char>( data[at++] );
                }
                return s;
            };
            auto readU16 = [&]( uint32_t at ) -> uint16_t {
                return at + 1 < next ? static_cast<uint16_t>(
                                           data[at] | ( data[at + 1] << 8 ) )
                                     : 0;
            };
            switch ( code ) {
            case 0x88: { // ConstantPool
                const uint16_t count = readU16( payload );
                uint32_t at = payload + 2;
                constants.clear();
                for ( uint16_t i = 0; i < count && at < next; i++ ) {
                    constants.push_back( readString( at ) );
                    at += constants.back().size() + 1;
                }
                OTDU_LOG_INFO( "    f%u ConstantPool %u: %s\n", frame, count,
                               StringJoin( constants, ", " ).c_str() );
                break;
            }
            case 0x96: { // Push
                // Typed walk (string 0, constant8 8, constant16 9, register 4,
                // bool 5, int 7, double 6, null 2, undefined 3).
                uint32_t p = payload;
                std::string line;
                int32_t pguard = 0;
                while ( p < next && pguard++ < 64 ) {
                    const uint8_t type = data[p++];
                    if ( type == 0 ) {
                        std::string s = readString( p );
                        line += "'" + s + "' ";
                        p += s.size() + 1;
                    } else if ( type == 4 ) {
                        line += "reg" + std::to_string( data[p++] ) + " ";
                    } else if ( type == 5 ) {
                        line += data[p++] != 0 ? "true " : "false ";
                    } else if ( type == 6 ) {
                        double d;
                        memcpy( &d, &data[p], 8 );
                        p += 8;
                        line += std::to_string( d ) + " ";
                    } else if ( type == 7 ) {
                        const int32_t v = static_cast<int32_t>(
                            readU16( p ) | ( readU16( p + 2 ) << 16 ) );
                        p += 4;
                        line += std::to_string( v ) + " ";
                    } else if ( type == 8 ) {
                        const uint16_t i = data[p++];
                        line += i < constants.size()
                                    ? "'" + constants[i] + "' "
                                    : "cst" + std::to_string( i ) + " ";
                    } else if ( type == 9 ) {
                        const uint16_t i = readU16( p );
                        p += 2;
                        line += i < constants.size()
                                    ? "'" + constants[i] + "' "
                                    : "cst" + std::to_string( i ) + " ";
                    } else if ( type == 2 ) {
                        line += "null ";
                    } else if ( type == 3 ) {
                        line += "undef ";
                    } else {
                        break;
                    }
                }
                OTDU_LOG_INFO( "    f%u Push %s\n", frame, line.c_str() );
                break;
            }
            case 0x8e: { // DefineFunction2
                const std::string name = readString( payload );
                uint32_t at = payload + name.size() + 1;
                const uint16_t numParams = readU16( at );
                at += 2;
                const uint8_t regCount = data[at];
                at += 1;
                at += 2; // flags
                std::string params;
                for ( uint16_t i = 0; i < numParams && at + 1 < next; i++ ) {
                    at++; // register
                    params += ( i != 0 ? "," : "" ) + readString( at );
                    at += readString( at ).size() + 1;
                }
                OTDU_LOG_INFO( "    f%u DefineFunction2 '%s'(%s) regs %u\n",
                               frame, name.c_str(), params.c_str(), regCount );
                break;
            }
            case 0x9b: { // DefineFunction (old)
                const std::string name = readString( payload );
                uint32_t at = payload + name.size() + 1;
                const uint16_t numParams = readU16( at );
                at += 2;
                std::string params;
                for ( uint16_t i = 0; i < numParams && at < next; i++ ) {
                    params += ( i != 0 ? "," : "" ) + readString( at );
                    at += readString( at ).size() + 1;
                }
                at += 2; // code size
                OTDU_LOG_INFO( "    f%u DefineFunction '%s'(%s)\n", frame,
                               name.c_str(), params.c_str() );
                break;
            }
            case 0x81:
                OTDU_LOG_INFO( "    f%u GotoFrame %u\n", frame,
                               readU16( payload ) );
                break;
            case 0x8c:
                OTDU_LOG_INFO( "    f%u GoToLabel '%s'\n", frame,
                               readString( payload ).c_str() );
                break;
            case 0x8b:
                OTDU_LOG_INFO( "    f%u SetTarget '%s'\n", frame,
                               readString( payload ).c_str() );
                break;
            case 0x87:
                OTDU_LOG_INFO( "    f%u StoreRegister %u\n", frame,
                               data[payload] );
                break;
            case 0x83: {
                const std::string url = readString( payload );
                const std::string target =
                    readString( payload + url.size() + 1 );
                OTDU_LOG_INFO( "    f%u GetURL '%s' '%s'\n", frame, url.c_str(),
                               target.c_str() );
                break;
            }
            case 0x99:
                OTDU_LOG_INFO( "    f%u Jump %+d\n", frame,
                               static_cast<int16_t>( readU16( payload ) ) );
                break;
            case 0x9d:
                OTDU_LOG_INFO( "    f%u If %+d\n", frame,
                               static_cast<int16_t>( readU16( payload ) ) );
                break;
            case 0x07:
                OTDU_LOG_INFO( "    f%u Stop\n", frame );
                break;
            case 0x06:
                OTDU_LOG_INFO( "    f%u Play\n", frame );
                break;
            default:
                // SWF action codes 0x00..0x6f, eight per row (the table used to
                // be one entry short at 0x19, which shifted every name from
                // GetVariable on).
                static const char *kNames[0x70] = { "End",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "NextFrame",
                                                    "PrevFrame",
                                                    "Play",
                                                    "Stop", // 0x00
                                                    "ToggleQuality",
                                                    "StopSounds",
                                                    "Add",
                                                    "Subtract",
                                                    "Multiply",
                                                    "Divide",
                                                    "Equals",
                                                    "Less", // 0x08
                                                    "And",
                                                    "Or",
                                                    "Not",
                                                    "StringEquals",
                                                    "StringLength",
                                                    "StringExtract",
                                                    "?",
                                                    "Pop", // 0x10
                                                    "ToInteger",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "GetVariable",
                                                    "SetVariable",
                                                    "?",
                                                    "?", // 0x18
                                                    "SetTarget2",
                                                    "StringAdd",
                                                    "GetProperty",
                                                    "SetProperty",
                                                    "CloneSprite",
                                                    "RemoveSprite",
                                                    "Trace",
                                                    "StartDrag", // 0x20
                                                    "EndDrag",
                                                    "StringLess",
                                                    "Throw",
                                                    "CastOp",
                                                    "ImplementsOp",
                                                    "?",
                                                    "?",
                                                    "?", // 0x28
                                                    "RandomNumber",
                                                    "MBStringLength",
                                                    "CharToAscii",
                                                    "AsciiToChar",
                                                    "GetTime",
                                                    "MBStringExtract",
                                                    "MBCharToAscii",
                                                    "MBAsciiToChar", // 0x30
                                                    "?",
                                                    "?",
                                                    "Delete",
                                                    "Delete2",
                                                    "DefineLocal",
                                                    "CallFunction",
                                                    "Return",
                                                    "Modulo", // 0x38
                                                    "NewObject",
                                                    "DefineLocal2",
                                                    "InitArray",
                                                    "InitObject",
                                                    "TypeOf",
                                                    "TargetPath",
                                                    "Enumerate",
                                                    "Add2", // 0x40
                                                    "Less2",
                                                    "Equals2",
                                                    "ToNumber",
                                                    "ToString",
                                                    "PushDuplicate",
                                                    "StackSwap",
                                                    "GetMember",
                                                    "SetMember", // 0x48
                                                    "Increment",
                                                    "Decrement",
                                                    "CallMethod",
                                                    "NewMethod",
                                                    "InstanceOf",
                                                    "Enumerate2",
                                                    "?",
                                                    "?", // 0x50
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?", // 0x58
                                                    "BitAnd",
                                                    "BitOr",
                                                    "BitXor",
                                                    "BitLShift",
                                                    "BitRShift",
                                                    "BitURShift",
                                                    "StrictEquals",
                                                    "Greater", // 0x60
                                                    "StringGreater",
                                                    "Extends",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?",
                                                    "?" }; // 0x68
                if ( code < 0x70 ) {
                    OTDU_LOG_INFO( "    f%u %s\n", frame, kNames[code] );
                } else {
                    OTDU_LOG_INFO( "    f%u op %02x\n", frame, code );
                }
                break;
            }
            pc = next;
        }
    }
    void DumpClipTimeline( const FlashPlayer::Clip &clip,
                           const char *pPath,
                           const FlashMovie *pMovie,
                           uint32_t maxFrames = 6 ) {
        if ( clip.pTimeline == nullptr ) {
            return;
        }
        const std::vector<uint8_t> &data = pMovie->getVMF().getData();
        OTDU_LOG_INFO(
            "PauseMenu: timeline of %s: %u frames, playing %d, frame %u\n",
            pPath, clip.pTimeline->FrameCount, clip.bPlaying ? 1 : 0,
            clip.CurrentFrame );
        // All labels first (state machines: sq states, island badge frames...).
        for ( uint32_t f = 0; f < clip.pTimeline->FrameCount; f++ ) {
            for ( const FlashControlTag &control : clip.pTimeline->Frames[f] ) {
                if ( control.Type == eFlashControlType::CT_Label ) {
                    OTDU_LOG_INFO( "  frame %u: label '%s'\n", f,
                                   control.Label.c_str() );
                }
            }
        }
        const uint32_t last =
            Min<uint32_t>( clip.pTimeline->FrameCount, maxFrames );
        for ( uint32_t f = 0; f < last; f++ ) {
            for ( const FlashControlTag &control : clip.pTimeline->Frames[f] ) {
                switch ( control.Type ) {
                case eFlashControlType::CT_Place:
                    OTDU_LOG_INFO(
                        "  frame %u: place depth %u id %u '%s' at (%d, %d) "
                        "scale (%.4f, %.4f) char %d move %d\n",
                        f, control.Place.Depth, control.Place.CharacterId,
                        control.Place.Name.c_str(), control.Place.Matrix.TX,
                        control.Place.Matrix.TY, control.Place.Matrix.A,
                        control.Place.Matrix.D,
                        control.Place.bHasCharacter ? 1 : 0,
                        control.Place.bMove ? 1 : 0 );
                    break;
                case eFlashControlType::CT_Remove:
                    OTDU_LOG_INFO( "  frame %u: remove depth %u\n", f,
                                   control.Depth );
                    break;
                case eFlashControlType::CT_Label:
                    OTDU_LOG_INFO( "  frame %u: label '%s'\n", f,
                                   control.Label.c_str() );
                    break;
                case eFlashControlType::CT_Action:
                    if ( pMovie != nullptr ) {
                        DumpActions( data, control.ActionOffset,
                                     control.ActionLength, f );
                    }
                    break;
                default:
                    break;
                }
            }
        }
    }

    // -logpause: every sprite of a subtree, with its timeline. The options
    // screen is attached at run time (the root's LaunchOptions script), so its
    // path is only known once the screen is up (pFilter keeps the dump to that
    // subtree).
    void DumpSubtrees( const FlashPlayer::Clip &clip,
                       const std::string &path,
                       const FlashMovie *pMovie,
                       const char *pFilter,
                       int32_t level,
                       int32_t maxLevel,
                       uint32_t maxFrames ) {
        if ( level > maxLevel ) {
            return;
        }
        const std::string filter =
            pFilter != nullptr ? ToUpper( pFilter ) : std::string();
        for ( const auto &entry : clip.DisplayList ) {
            const FlashPlayer::Instance &instance = *entry.second;
            if ( instance.pClip == nullptr ) {
                continue;
            }
            const std::string childPath =
                path + "/" +
                ( instance.Name.empty() ? std::to_string( entry.first )
                                        : instance.Name );
            if ( filter.empty() ||
                 ToUpper( childPath ).find( filter ) != std::string::npos ) {
                DumpClipTimeline( *instance.pClip, childPath.c_str(), pMovie,
                                  maxFrames );
            }
            DumpSubtrees( *instance.pClip, childPath, pMovie, pFilter,
                          level + 1, maxLevel, maxFrames );
        }
    }

    // -logpause probe: instances whose world position lands in a stage region
    // (px) — identifies stray elements (the mystery blue box of the entry row).
    void DumpWorldRegion( const FlashPlayer::Clip &clip,
                          const FlashMatrix &world,
                          const char *pPath,
                          float minX,
                          float maxX,
                          float minY,
                          float maxY ) {
        for ( const auto &entry : clip.DisplayList ) {
            const FlashPlayer::Instance &instance = *entry.second;
            const FlashMatrix instanceWorld = world * instance.Matrix;
            const float px = instanceWorld.TX / 20.0f;
            const float py = instanceWorld.TY / 20.0f;
            if ( px >= minX && px <= maxX && py >= minY && py <= maxY ) {
                OTDU_LOG_INFO( "  region hit '%s' id %u at stage (%.0f, %.0f) "
                               "scale (%.3f, %.3f) visible %d\n",
                               instance.Name.c_str(), instance.CharacterId, px,
                               py, instanceWorld.A, instanceWorld.D,
                               instance.bVisible ? 1 : 0 );
            }
            if ( instance.pClip != nullptr ) {
                DumpWorldRegion( *instance.pClip, instanceWorld, pPath, minX,
                                 maxX, minY, maxY );
            }
        }
    }

    // The extra options-row table (the definitions sit below, with the
    // options-page code): the defaults + <user data>/menu_rows.ini.
    void LoadExtraRows();
    int32_t ExtraRowCount( const char *pPage );
} // namespace

void DumpFlashSubtree( FlashPlayer &player,
                       const char *pFilter,
                       int32_t maxLevel,
                       uint32_t maxFrames ) {
    if ( player.getRoot() != nullptr ) {
        DumpSubtrees( *player.getRoot(), "/", player.getMovie(), pFilter, 0,
                      maxLevel, maxFrames );
    }
}

void DumpFlashRootTimeline( FlashPlayer &player, uint32_t maxFrames ) {
    if ( player.getRoot() != nullptr ) {
        DumpClipTimeline( *player.getRoot(), "/", player.getMovie(), maxFrames );
    }
}

void DumpFlashFunctions( FlashPlayer &player ) {
    if ( player.getRoot() == nullptr || player.getMovie() == nullptr ) {
        return;
    }
    const std::vector<uint8_t> &data = player.getMovie()->getVMF().getData();
    for ( const auto &var : player.getRoot()->Variables ) {
        if ( var.second.Type != FlashPlayer::Value::eType::Function ) {
            continue;
        }
        uint32_t bodyOffset = 0, bodyLength = 0;
        if ( !player.getFunctionBody( var.second.FunctionIndex, bodyOffset,
                                      bodyLength ) ) {
            continue;
        }
        OTDU_LOG_INFO( "FlashDump: root function '%s' (%u bytes)\n",
                       var.first.c_str(), bodyLength );
        DumpActions( data, bodyOffset, bodyLength, 0,
                     player.getFunctionConstants( var.second.FunctionIndex ) );
    }
}

bool PauseMenu::load() {
    unload();
    FlashMovie *pMovie =
        gpFlash != nullptr ? gpFlash->getMovie( "main" ) : nullptr;
    if ( pMovie == nullptr || !player.load( pMovie ) ) {
        OTDU_LOG_WARN(
            "PauseMenu: MAIN.vmf (FrontEnd/HiRes/main.bnk) not available\n" );
        return false;
    }
    player.setVariable( "/:m_Pause", LocStr( kLocPause, "Pause" ) );
    player.setVariable( "/:m_Resume", LocStr( kLocResume, "Resume" ) );
    player.setVariable( "/:m_Options", LocStr( kLocOptions, "Options" ) );
    player.setVariable( "/:m_Quit", LocStr( kLocQuit, "Quit" ) );
    // FUN_00d8e200 (GENERAL, MAIN): /:WhatPlatform = 1 (PC: the button glyph
    // sprites pick their "pc" frame from it).
    player.setVariableNumber( "/:WhatPlatform", 1.0 );
    // setSquareMenu switches on /:WhatGameMode: 1 builds the free roam list
    // (resume / game_progress / players / photo / gme / options / profiles /
    // exit_game, the table above), 2 the race list (restart / quit_race...)
    // [DATA, MAIN.vmf; 2 was a guess that only mattered once the movie's own
    // square scripts started running].
    player.setVariableNumber( "/:WhatGameMode", 1.0 );
    player.setVariableBool( "/:IsInOptions", false );
    // Options categories: Language is locked on PC (the retail options
    // screen shows it grayed with a padlock, docs/images/
    // retail-options-screen.png: the language comes from the launcher)
    // [DATA]; the others are open.
    player.setVariableBool( "/:LockLanguage", true );
    // Audio & Video lists Audio, Video, Image settings (gamma) and Stereo.
    // "set square menu GAMMA" pushes the gamma icon on /:GammaEnabled and the
    // stereo icon on /:is3DAllowed [DATA, MAIN.vmf]; the retail PC build shows
    // both (OBS capture, docs/images/retail-options-audiovideo.webp). Stereo 3D
    // is not implemented, so that page stays locked [INF].
    player.setVariableBool( "/:GammaEnabled", true );
    player.setVariableBool( "/:LockGamma", false );
    player.setVariableBool( "/:is3DAllowed", true );
    player.setVariableBool( "/:LockRelief3d", true );
    // Player panel of the pause screen: MAIN.vmf reads these root variables
    // (undefined until the game fills them, which is why the panel of the
    // original screenshot stayed empty). The save game is not implemented:
    // offline, story step defaults.
    PlayerProfile &profile = GetPlayerProfile();
    player.setVariable( "/:AvatarName",
                        profile.Name.empty() ? "Player" : profile.Name );
    player.setVariable( "/:Money", FormatMoney( profile.Money ) );
    player.setVariableNumber( "/:NbCars", profile.NbCars );
    player.setVariableNumber( "/:NbHouses", profile.NbHouses );
    panelMoney = profile.Money;
    panelCars = profile.NbCars;
    panelHouses = profile.NbHouses;
    player.setVariableNumber( "/:ClubLevel", 0.0 );
    player.setVariableBool( "/:state_online", false );
    player.setVariableBool( "/:voice_on", false );
    // The player title text field shows its authored default (its own
    // variable path) until the game defines the variable — no progression
    // data here: no title.
    player.setVariable( "/:m_PlayerTitle", "" );
    player.setVariable( "/:m_PlayerTitle1", "" );
    player.setVariable( "/:CasinoAvatar", "" );
    if ( CmdLine::HasArg( "-logskills" ) ) {
        for ( int32_t s = 0; s < 4; s++ ) {
            GetSkillLevels( s );
        }
        GetGlobalLevels();
        GetRewardXp();
        GetCarShops();
    }
    LoadExtraRows();
    pushProgressData();
    // The exe blanks the 'fake' visibility-test field before the frontend
    // scripts run; do the same (drawEditText note in flash_player).
    player.setVariable( "/:fake", "" );
    // The retail locks (kEntries' bEnabled flags), then the mode's own lock
    // state on top.
    for ( int32_t n = 0; n < kEntryCount; n++ ) {
        bEntryEnabled[n] = kEntries[n].bEnabled;
    }
    setEntryEnabled( true, true, true );
    if ( bCasinoMode ) {
        setCasinoMode( true ); // re-apply the casino list on the loaded movie
    }
    // The movie tells the game which entry was used (FSCommand) and whether
    // the options screen is up.
    player.setFSCommandCallback( [this]( const std::string &command,
                                         const std::string &args ) {
        OTDU_LOG_INFO( "PauseMenu: FSCommand '%s' '%s'\n", command.c_str(),
                       args.c_str() );
        if ( StringCmpI( command.c_str(), "quit" ) == 0 ||
             StringCmpI( command.c_str(), "quitgame" ) == 0 ||
             StringCmpI( command.c_str(), "quitgame_direct" ) == 0 ) {
            result = eResult::Quit;
        } else if ( StringCmpI( command.c_str(), "resume" ) == 0 ||
                    StringCmpI( command.c_str(), "close" ) == 0 ) {
            bInOptions = false;
            player.setVariableBool( "/:IsInOptions", false );
            result = eResult::Resume;
        } else if ( StringCmpI( command.c_str(), "reco" ) == 0 ||
                    StringCmpI( command.c_str(), "rcod" ) == 0 ) {
            // The movie's own resumeGame() (MAIN.vmf): the validated resume
            // square fires 'reco' (freeride list, WhatGameMode 1) or 'rcod'
            // (the race list), then pauseOut() ('fain' / 'paou'). Without
            // this the movie played its pause_out and the game stayed paused
            // with no menu (the headless run: validate on Resume -> the
            // world frozen, no menu, no result). The mode's close() on the
            // result replays pause_out (already invisible) and clears bOpen.
            bInOptions = false;
            player.setVariableBool( "/:IsInOptions", false );
            result = eResult::Resume;
        } else if ( StringCmpI( command.c_str(), "opin" ) == 0 ) {
            // LaunchOptions / the options entry: the options screen is up.
            bInOptions = true;
            bOptionsDumped = false;
            player.setVariableBool( "/:IsInOptions", true );
        } else if ( StringCmpI( command.c_str(), "gapr:open" ) == 0 ) {
            pushProgressData(); // the pages open with the current points
        } else if ( StringCmpI( command.c_str(), "phmo:phin" ) == 0 ||
                    StringCmpI( command.c_str(), "phin" ) == 0 ) {
            // The 'photo' entry of the movie-driven menu: its square calls
            // _root.ValidMenu and the pause_menu clip's script fires the photo
            // mode command (the retail one is phmo:phin, "photo mode in",
            // followed by mcls, then its own photomode clips). The mode opens
            // the native free camera (PhotoCamera) and the pause menu closes.
            // phmo:phou (photo mode out) is fired by the movie's own photo UI,
            // which is not driven here.
            result = eResult::Photo;
        } else if ( StringNCmpI( command.c_str(), "gapr:part:", 10 ) == 0 ) {
            onProgressCommand( command );
        } else if ( StringNCmpI( command.c_str(), "opts:", 5 ) == 0 ) {
            onOptionsCommand( command );
        } else if ( StringCmpI( command.c_str(), "dbxo" ) == 0 ) {
            // The DBXO confirm-dialog request (the Controls page's Back asks
            // "Save control option?" - back_controls). The port has no
            // dialog UI and its bindings apply live, so Back walks them
            // back like Cancel (the video page's Quit law) and the page
            // closes.
            if ( bOnControlsPage ) {
                gpInput->RestoreControls( controlsBackup );
                bOnControlsPage = false;
                bBindCaptureArmed = false;
                player.setVariable( "/:fake", "" );
                GetAdvancedGraphics().setVideoPageHint( bOnVideoPage );
                player.gotoLabel( "/options/controls_menu", "close", true );
                OTDU_LOG_INFO(
                    "PauseMenu: controls page back (dbxo: bindings restored)" );
            }
        } else if ( StringCmpI( command.c_str(), "sbdd" ) == 0 ) {
            // SetBDD (db_hud + db_main) -> the localized m_* strings of the
            // movie (the options title m_Options_, the square names...).
            ApplyFlashLocalization( player );
        } else if ( StringCmpI( command.c_str(), "opou" ) == 0 ) {
            // BackFromOptions / CloseAllOptions: back to the entry column.
            bInOptions = false;
            bOptionsDumped = false;
            bOnControlsPage = false;
            bBindCaptureArmed = false;
            player.setVariableBool( "/:IsInOptions", false );
        }
    } );
    // -logpausekeys: the key-press buttons of the movie (on(keyPress) handlers:
    // how the menus take the pad / keys) and the clips that place them.
    if ( CmdLine::HasArg( "-logpausekeys" ) ) {
        for ( uint32_t id = 0; id < 0x10000; id++ ) {
            const FlashButton *pButton =
                player.getDefinitions().getType( static_cast<uint16_t>(
                    id ) ) == eFlashCharacterType::CT_Button
                    ? player.getDefinitions().getButton(
                          static_cast<uint16_t>( id ) )
                    : nullptr;
            if ( pButton == nullptr ) {
                continue;
            }
            for ( const FlashButtonCondition &condition :
                  pButton->Conditions ) {
                if ( ( condition.Conditions >> 9 ) != 0 ) {
                    OTDU_LOG_INFO(
                        "PauseMenu: button %u key %u (actions %u bytes)\n", id,
                        condition.Conditions >> 9, condition.ActionLength );
                    DumpActions( pMovie->getVMF().getData(),
                                 condition.ActionOffset, condition.ActionLength,
                                 0 );
                }
            }
        }
    }
    // The movie's own SetBDD() declares its string categories and fires
    // 'sbdd' (the HUD / map pattern); without it no m_* text is defined.
    if ( !player.invoke( "/", "SetBDD", {} ) ) {
        ApplyFlashLocalization( player );
    }
    OTDU_LOG_INFO(
        "PauseMenu: MAIN.vmf loaded (%u labels)\n",
        static_cast<uint32_t>( pMovie->getVMF().getFrameLabels().size() ) );
    return true;
}

void PauseMenu::unload() {
    player.unload();
    bOpen = false;
    bInOptions = false;
    bEntriesShown = false;
    bOptionsDumped = false;
    result = eResult::None;
}

void PauseMenu::setEntryEnabled( bool bResume, bool bOptions, bool bQuit ) {
    // The modes only control the three entries that do something (the movie's
    // setSquareMenu locks the same ones per context). The other entries keep
    // their retail lock flags (features not built yet).
    bEntryEnabled[kEntryResume] = bResume;
    bEntryEnabled[kEntryOptions] = bOptions;
    bEntryEnabled[kEntryExit] = bQuit;
    player.setVariableBool( "/:LockResume", !bResume );
    player.setVariableBool( "/:LockOptions", !bOptions );
    player.setVariableBool( "/:LockQuit", !bQuit );
    // The square lists read LockExitGame (setSquareMenu: exit_game entry).
    player.setVariableBool( "/:LockExitGame", !bQuit );
    // Every other entry's lock variable follows that entry's own state - it is
    // NOT cleared. Clearing them unlocked the entries whose screens do not
    // exist in this build, and the movie-driven menu (menu_sq.isMenuH) then
    // let them be selected: 'players' opened the online TDU2 Friends screen
    // (empty - there is no online here) and 'gme' a "Link to player" page stuck
    // on Loading. A locked entry is greyed out and skipped by the movie's own
    // Move(), so the dead entries stay unreachable.
    player.setVariableBool( "/:LockGameProgress",
                            !bEntryEnabled[kEntryGameProgress] );
    player.setVariableBool( "/:LockPlayers", !bEntryEnabled[kEntryPlayers] );
    player.setVariableBool( "/:LockSnapshot", !bEntryEnabled[kEntryPhoto] );
    player.setVariableBool( "/:LockEditor", !bEntryEnabled[kEntryEditor] );
    player.setVariableBool( "/:LockProfiles", !bEntryEnabled[kEntryProfiles] );
}

void PauseMenu::setCasinoMode( bool bCasino ) {
    bCasinoMode = bCasino;
    // The retail casino list (setSquareMenu's IsCasino branch): the same
    // entries as free roam minus 'photo' (the snapshot tool) and 'gme' (the
    // editor) — docs/STATE.md session 18, retail-pause-casino.png. Locking an
    // entry greys it out and skips it (refreshEntries reads bEntryEnabled).
    bEntryEnabled[kEntryPhoto] = !bCasino;
    // The casino's Game Progress page is the casino progression (the retail
    // IsCasino list carries game_progress); free roam keeps it locked.
    bEntryEnabled[kEntryGameProgress] = bCasino;
    bEntryEnabled[kEntryEditor] = !bCasino;
    if ( !player.isLoaded() ) {
        return; // load() re-applies this once the movie is up
    }
    player.setVariableBool( "/:IsCasino", bCasino );
    player.setVariableBool( "/:LockSnapshot", bCasino );
    player.setVariableBool( "/:LockEditor", bCasino );
    if ( bEntriesShown ) {
        refreshEntries();
    }
}

namespace {
    // The four skills of the Game Progress pages. The movie calls them Racing
    // (Competition), Cruising (Discovery), Collection and Social [DATA,
    // MAIN.vmf variable names]. Levels, titles and thresholds come from the
    // database tables (skill_levels.h) [DATA]; the points come from the
    // profile.
    constexpr const char *kSkillPrefixes[4] = { "Racing", "Cruising",
                                                "Collection", "Social" };
    // Page maxima from the owner's retail captures [INF]: the skill page shows
    // "n/15" levels and "/ 10000" points (Discovery 18 and 14500: its area
    // levels add to the table), the Completion page the larger counts with the
    // casino levels (19 / 20 / 17 / 17 = 73, the NB_LVL).
    constexpr int32_t kPageMaxLevel[4] = { 15, 18, 15, 15 };
    constexpr int32_t kPageMaxPoints[4] = { 10000, 14500, 10000, 10000 };
    constexpr int32_t kCompletionMaxLevel[4] = { 19, 20, 17, 17 };

    struct SkillState {
        int32_t Points = 0;
        int32_t Level = 0;
        int32_t NextLevelIn = 0;    // points to the next level (0 at the top)
        int32_t NextTitleLevel = 0; // first level with another title
        std::string Title;
    };

    SkillState ComputeSkill( int32_t index, const PlayerProfile &profile ) {
        SkillState state;
        state.Points = profile.Points[index];
        const std::vector<SkillLevel> &levels = GetSkillLevels( index );
        if ( levels.empty() ) {
            return state;
        }
        const int32_t at =
            Max( 0, FindSkillLevelIndex( levels, state.Points ) );
        state.Level = levels[at].Level;
        state.Title = levels[at].Title;
        state.NextTitleLevel = levels.back().Level;
        if ( static_cast<size_t>( at ) + 1 < levels.size() ) {
            state.NextLevelIn = levels[at + 1].Points - state.Points;
        }
        for ( size_t j = at + 1; j < levels.size(); j++ ) {
            if ( levels[j].Title != state.Title ) {
                state.NextTitleLevel = levels[j].Level;
                break;
            }
        }
        return state;
    }

    // Detail rows: percent per objective type; nothing earns them yet (only the
    // explored roads, set from the profile) [INF].
    struct PercentRow {
        const char *pVariable;
        int32_t Value;
    };
    constexpr PercentRow kPercents[] = {
        {"/:PercentSchool",       0},
        {"/:PercentChampionship", 0},
        {"/:PercentCup",          0},
        {"/:PercentDuel",         0},
        {"/:PercentMulti",        0},
        {"/:PercentInstant",      0},
        {"/:PercentVehicule",     0},
        {"/:PercentHouse",        0},
        {"/:PercentClothe",       0},
        {"/:PercentHair",         0},
        {"/:PercentSurgery",      0},
        {"/:PercentSticker",      0},
        {"/:PercentRoad",         0},
        {"/:PercentEvent",        0},
        {"/:PercentTreasure",     0},
        {"/:PercentPhotographer", 0},
        {"/:PercentFriend",       0},
        {"/:PercentClub",         0},
        {"/:PercentCoop",         0},
        {"/:PercentChase",        0},
    };
} // namespace

void PauseMenu::pushProgressData() {
    const PlayerProfile &profile = GetPlayerProfile();
    int32_t globalLevel = 0;
    for ( int32_t index = 0; index < 4; index++ ) {
        const SkillState skill = ComputeSkill( index, profile );
        const std::string prefix = std::string( "/:" ) + kSkillPrefixes[index];
        // <Prefix>Points drives the gauge ring (maskLevel.gotoAndStop(
        // focusPoints )): a percentage; <Prefix>LevelCount is the "n/max" text
        // under the icon (gradeLevelCount = _root.focusLevelCount) [DATA,
        // -logprogress].
        player.setVariableNumber(
            ( prefix + "Points" ).c_str(),
            Min( 100, 100 * skill.Points / kPageMaxPoints[index] ) );
        player.setVariableNumber( ( prefix + "LvlPoints" ).c_str(),
                                  kPageMaxPoints[index] );
        // The Completion page opens first: its counts include the casino
        // levels.
        player.setVariable( ( prefix + "LevelCount" ).c_str(),
                            std::to_string( skill.Level ) + "/" +
                                std::to_string( kCompletionMaxLevel[index] ) );
        player.setVariable( ( prefix + "Title" ).c_str(), skill.Title );
        static const char *kGpTitles[4] = {
            "/:GPRacingTitle", "/:GPCruisingTitle", "/:GPCollectionTitle",
            "/:GPSocialTitle" };
        player.setVariable( kGpTitles[index], skill.Title );
        globalLevel += skill.Level;
    }
    // The global level is the sum of the four skill levels; its title comes
    // from DBPlayerLevel (a new title every 10 levels) [DATA].
    const std::vector<SkillLevel> &globals = GetGlobalLevels();
    std::string globalTitle = "Inglorious";
    for ( const SkillLevel &level : globals ) {
        if ( level.Level <= globalLevel ) {
            globalTitle = level.Title;
        }
    }
    player.setVariableNumber( "/:GlobalLevel", globalLevel );
    player.setVariable( "/:GlobalLevelCount",
                        std::to_string( globalLevel ) + "/73" );
    player.setVariableNumber( "/:GlobalPoints",
                              Min( 100, 100 * globalLevel / 73 ) );
    player.setVariableNumber( "/:GlobalLvlPoints", 73 );
    player.setVariable( "/:GlobalLevelTitle", globalTitle );
    player.setVariable( "/:PlayerTitle", globalTitle );
    player.setVariableNumber( "/:LevelCount", 15 );
    const SkillState racing = ComputeSkill( 0, profile );
    player.setVariableNumber( "/:NextLevelIn", racing.NextLevelIn );
    player.setVariableNumber( "/:NextTitleIn", racing.NextLevelIn );
    player.setVariableNumber( "/:NextLevelCount", racing.NextLevelIn );
    player.setVariableNumber( "/:NextTitleCount", racing.NextTitleLevel );
    for ( const PercentRow &row : kPercents ) {
        player.setVariableNumber( row.pVariable, row.Value );
    }
    player.setVariableNumber( "/:PercentRoad", profile.ExploredPercent );
    player.setVariableBool( "/:LockGlobal", false );
    player.setVariableBool( "/:LockCollection", false );
    player.setVariableBool( "/:LockSocial", false );
    player.setVariableBool( "/:LockChase", false );
    player.setVariableBool( "/:LockClub", false );
}

void PauseMenu::onProgressCommand( const std::string &command ) {
    // -logprogress: the scripts of the Game Progress pages (once, when the
    // last page has asked for its numbers).
    static bool bProgressDumped = false;
    if ( !bProgressDumped && CmdLine::HasArg( "-logprogress" ) ) {
        bProgressDumped = true;
        DumpSubtrees( *player.getRoot(), "/", player.getMovie(), "gameprogress",
                      0, 8, 60 );
    }
    // Skill page order of kSkills: raci / crui / coll / soci -> 0 / 1 / 2 / 3.
    static const struct {
        const char *pCommand;
        int32_t Index;
        const char *pTitleVariable;
    } kParts[] = {
        {"gapr:part:raci", 0, "/:GPRacingTitle"    },
        {"gapr:part:crui", 1, "/:GPCruisingTitle"  },
        {"gapr:part:coll", 2, "/:GPCollectionTitle"},
        {"gapr:part:soci", 3, "/:GPSocialTitle"    },
    };
    for ( const auto &part : kParts ) {
        if ( StringCmpI( command.c_str(), part.pCommand ) != 0 ) {
            continue;
        }
        const SkillState skill = ComputeSkill( part.Index, GetPlayerProfile() );
        const std::string prefix =
            std::string( "/:" ) + kSkillPrefixes[part.Index];
        // The skill page counts levels without the casino ones.
        player.setVariable( ( prefix + "LevelCount" ).c_str(),
                            std::to_string( skill.Level ) + "/" +
                                std::to_string( kPageMaxLevel[part.Index] ) );
        player.setVariable( part.pTitleVariable, skill.Title );
        // "Next level: 125 Pts - Next title: Level 3" [DATA: the movie prints
        // NextLevelCount / NextTitleCount after its m_NextLevelDots_ labels].
        player.setVariable( "/:NextLevelCount",
                            std::to_string( skill.NextLevelIn ) + " " +
                                player.getVariable( "/:m_Pts" ) );
        player.setVariable( "/:NextTitleCount",
                            "Level " + std::to_string( skill.NextTitleLevel ) );
        // The points line under the header, a text bound to _root.GPTitle:
        // "<skill> points: <points> / <max> (<percent>%)" (retail capture).
        static const char *kNames[] = {
            "/:m_RacingGrade_", "/:m_CruisingGrade_", "/:m_CollectionGrade_",
            "/:m_SocialGrade_" };
        std::string points = player.getVariable( "/:m_Points_" );
        for ( char &c : points ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
        const int32_t maxPoints = kPageMaxPoints[part.Index];
        player.setVariable(
            "/:GPTitle",
            player.getVariable( kNames[part.Index] ) + " " + points + ": " +
                std::to_string( skill.Points ) + " / " +
                std::to_string( maxPoints ) + " (" +
                std::to_string( Min( 100, 100 * skill.Points / maxPoints ) ) +
                "%)" );
        return;
    }
}

void PauseMenu::open() {
    if ( !isLoaded() || bOpen ) {
        return;
    }
    bOpen = true;
    SaveProfile( "pause" );
    bInOptions = false;
    result = eResult::None;
    selected = 0;
    bEntriesShown = false;
    bOptionsDumped = false;
    bClockShown = false; // refreshClock pushes the world clock on the first tick
    bMovieDriven = false;
    stickNav = eNav::None;
    stickHoldTime = 0.0f;
    stickRepeatTimer = 0.0f;
    bTabRowLifted = false;
    fTabRowBaseTY = 0.0f;
    player.setVariableBool( "/:IsInOptions", false );
    player.gotoLabel( "/", "pause_in", true );
}

PauseMenu::eNav PauseMenu::takeStickNav( float deltaTime ) {
    // [INF] the stick repeat: the retail pad has a D-pad, so the rates are
    // chosen to feel like a held D-pad (one step, then a step every
    // kRepeatRate after kRepeatDelay).
    constexpr float kStickDeadZone = 0.5f;
    constexpr float kRepeatDelay = 0.40f;
    constexpr float kRepeatRate = 0.22f;
    const float x = gpInput->getLeftStickX();
    const float y = gpInput->getLeftStickY(); // up = -1
    eNav direction = eNav::None;
    if ( std::fabs( x ) > std::fabs( y ) ) {
        if ( x < -kStickDeadZone ) {
            direction = eNav::Left;
        } else if ( x > kStickDeadZone ) {
            direction = eNav::Right;
        }
    } else if ( y < -kStickDeadZone ) {
        direction = eNav::Up;
    } else if ( y > kStickDeadZone ) {
        direction = eNav::Down;
    }
    if ( direction == eNav::None ) {
        stickNav = eNav::None;
        stickHoldTime = 0.0f;
        stickRepeatTimer = 0.0f;
        return eNav::None;
    }
    if ( direction != stickNav ) {
        stickNav = direction;
        stickHoldTime = 0.0f;
        stickRepeatTimer = 0.0f;
        return direction; // the first frame past the dead zone steps at once
    }
    stickHoldTime += deltaTime;
    if ( stickHoldTime >= kRepeatDelay ) {
        stickRepeatTimer += deltaTime;
        if ( stickRepeatTimer >= kRepeatRate ) {
            stickRepeatTimer = 0.0f;
            return direction;
        }
    }
    return eNav::None;
}

void PauseMenu::tickMovieInput( float deltaTime ) {
    // The Advanced graphics page (native) opens from the Video page and owns
    // the input while it is up.
    AdvancedGraphics &advanced = GetAdvancedGraphics();
    if ( advanced.isOpen() ) {
        return;
    }
    if ( bOnVideoPage && gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) {
        advanced.open();
        return;
    }
    // The bind dialog of the Controls page owns the input while it waits:
    // the pressed key / pad button IS the new binding (tickControlsPage
    // captures it) and must not reach the movie's own buttons.
    if ( bOnControlsPage && bBindCaptureArmed ) {
        return;
    }
    // The menus take the keys through their own key-press buttons
    // (-logpausekeys: 201 on(keyPress) handlers in MAIN.vmf): Up / Down /
    // Left / Right call the owning menu's Move('up' / ...), Enter its A(),
    // Backspace its B(), 'x' / 'y' its X() / Y(); each handler checks
    // _root.Input itself, so only the menu that has the focus reacts [DATA].
    // The exe has no "Move" string (PS3 strings.json): it sends keys, not
    // calls. SWF key codes: Left 1, Right 2, Backspace 8, Enter 13, Up 14,
    // Down 15. The Controls page's list button also listens for the Delete
    // key (SWF 6 = its Unbind: DeleteBind on the focused row) - the game
    // forwards the raw key there.
    static const struct {
        eInputAction Action;
        uint8_t SwfKey;
    } kKeys[] = {
        {eInputAction::IA_MenuLeft,     1  },
        {eInputAction::IA_MenuRight,    2  },
        {eInputAction::IA_MenuUp,       14 },
        {eInputAction::IA_MenuDown,     15 },
        {eInputAction::IA_MenuValidate, 13 },
        {eInputAction::IA_MenuOption1,  120},
        {eInputAction::IA_MenuOption2,  121},
    };
    if ( bOnControlsPage && gpInput->wasKeyPressed( SDL_SCANCODE_DELETE ) ) {
        player.sendKey( 6 );
    }
    for ( const auto &key : kKeys ) {
        if ( !gpInput->wasPressed( key.Action ) ) {
            continue;
        }
        // Up / Down on an extended page moves the row focus game-side
        // (moveOptionsFocus): the Gamma page's own key button (16593 on its
        // stop frame) has no Up / Down conditions (retail's Image settings
        // page is ONE slider row - its Move is never called), and a page
        // whose game rows overflow the panel needs the window law. Every
        // other key (Enter / Back / the Video page while it fits) stays with
        // the movie's own buttons (-logpausekeys: 201 on(keyPress) handlers
        // in MAIN.vmf; SWF key codes: Left 1, Right 2, Backspace 8, Enter 13,
        // Up 14, Down 15).
        const bool bGameFocus =
            bInOptions && bRowsExtended &&
            ( key.SwfKey == 14 || key.SwfKey == 15 ) &&
            ( openPage == eRowsPage::Gamma ||
              ( openPage == eRowsPage::Video &&
                ExtraRowCount( "video" ) > kRowSlots ) );
        if ( bGameFocus ) {
            moveOptionsFocus( key.SwfKey == 14 ? -1 : 1 );
        } else {
            const bool bHandled = player.sendKey( key.SwfKey );
            static const bool bLogKeys =
                CmdLine::HasArg( "-logcontrols" );
            if ( bOnControlsPage && bLogKeys ) {
                OTDU_LOG_INFO( "PauseMenu: controls key %u handled %d\n",
                               key.SwfKey, bHandled ? 1 : 0 );
            }
        }
    }
    // The left stick: the same SWF keys as the D-pad, so the stick navigates
    // the movie's own menus (Move('left') ... ) like the pad's D-pad does.
    const eNav nav = takeStickNav( deltaTime );
    switch ( nav ) {
    case eNav::Left:
        player.sendKey( 1 );
        break;
    case eNav::Right:
        player.sendKey( 2 );
        break;
    case eNav::Up:
    case eNav::Down:
        if ( bInOptions && bRowsExtended &&
             ( openPage == eRowsPage::Gamma ||
               ( openPage == eRowsPage::Video &&
                 ExtraRowCount( "video" ) > kRowSlots ) ) ) {
            moveOptionsFocus( nav == eNav::Up ? -1 : 1 );
        } else {
            player.sendKey( nav == eNav::Up ? 14 : 15 );
        }
        break;
    default:
        break;
    }
    // LB / RB (keyboard Q / E) step the tab row of the Game Progress pages:
    // button 17227 maps SWF keys 49 / 51 to this.Move('left' / 'right')
    // [DATA, -logpausekeys]; the pad -> flash key map itself is not traced
    // [INF]. On an OPTIONS page the same shoulders cycle the pages instead
    // (audio -> video -> gamma, the shared page template).
    if ( bInOptions && openPage != eRowsPage::None ) {
        if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) ||
             gpInput->wasKeyPressed( SDL_SCANCODE_E ) ) {
            cycleOptionsPage( 1 );
        } else if ( gpInput->wasPressed( eInputAction::IA_CarBackView ) ||
                    gpInput->wasKeyPressed( SDL_SCANCODE_Q ) ) {
            cycleOptionsPage( -1 );
        }
    } else {
        if ( gpInput->wasPressed( eInputAction::IA_CarBackView ) ||
             gpInput->wasKeyPressed( SDL_SCANCODE_Q ) ) {
            player.sendKey( 49 );
        }
        if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) ||
             gpInput->wasKeyPressed( SDL_SCANCODE_E ) ) {
            player.sendKey( 51 );
        }
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        // Inside the options the screens' own B() steps back; on the entry
        // row back resumes the game (the game closes the menu).
        if ( bInOptions ) {
            player.sendKey( 8 );
        } else {
            close();
        }
    }
}

namespace {
    // The audio page (options/audio initParameters): setParam rows are
    // [clip, label, type, variable, command, mute]; the sliders read and write
    // these root variables, 0..20 [DATA, MAIN.vmf].
    struct AudioOptionVariable {
        const char *pVariable;
        int32_t GameOptions::*pValue;
    };
    constexpr AudioOptionVariable kAudioVariables[] = {
        {"/:master_volume",    &GameOptions::MasterVolume  },
        {"/:music_volume",     &GameOptions::MusicVolume   },
        {"/:vehicules_volume", &GameOptions::VehiclesVolume},
        {"/:voices_volume",    &GameOptions::VoicesVolume  },
        {"/:sfx_volume",       &GameOptions::SfxVolume     },
        {"/:voiceIP_volume",   &GameOptions::VoiceIPVolume },
        {"/:mike",             &GameOptions::Mike          },
    };
    // The 6th setParam field ("mute_*"): a muted row is drawn disabled. Nothing
    // is muted by the game.
    constexpr const char *kAudioMutes[] = {
        "/:mute_master", "/:mute_music",   "/:mute_vehicules", "/:mute_voices",
        "/:mute_sfx",    "/:mute_voiceIP", "/:mute_pokemote" };
    // The game's own rows (OpenTDU2, not a retail page content): written into
    // the page clip's setParam one tick after it opened (extendOptionsRows).
    // The pages are child clips of /options ('video' / 'gamma', the tabs page
    // shows one of them) and hold their own row instances p0..pN (the generic
    // choicebox sprite, ~48 px pitch) behind the bkgd<N> strips; they are
    // FIXED 8-slot panels - the movie has no scrollbar on them (its scroll
    // machinery, ASFlashList + initScrollBar, belongs to the list pages:
    // game progress, players) [DATA: dumpClips + MAIN.vmf]. So the rows go
    // where a page has or can get a slot: the Video page's 8 retail rows are
    // all taken, but its Frequency row (p1) is a dead retail setting - locked
    // '60 Hz', the display's own rate, which the port does not control - so
    // Display mode takes that slot [INF: documented divergence, the refresh
    // rate stays follow-the-display]; the Gamma page shows one retail slider
    // row and its panel has room for the rest of the series, so its rows are
    // placed instances of the generic choicebox row sprite (found once by its
    // label set) with their own bkgd strips. setParam entries are [variable,
    // label, type, '_root.<variable>', 'opts:<page>:<cmd>', lock] - the shape
    // initParameters pushes (see extendOptionsRows) [DATA]. Labels are pushed
    // as plain text (English) [INF: the DB has no keys for them].
    //
    // When a page carries more game rows than its panel shows (kRowSlots),
    // the rows scroll a window through the slots (moveOptionsFocus) - the
    // treadmill the owner asked for; the movie's own parameter pages never
    // scroll, so the window law is game-side.
    struct ExtraOptionRow {
        const char *pPage;     // the page clip: 'video' / 'gamma'
        const char *pPrefix;   // the FSCommand page prefix: 'vide' / 'gama'
        const char *pCommand;  // the game-side command id ('opts:<page>:<cmd>')
        const char *pVariable; // the root variable the row displays
        const char *pLabel;    // the row's title (plain text)
    };
    // The display order of a page's rows IS the vector order (slot 1 first).
    constexpr ExtraOptionRow kDefaultRows[] = {
        { "video", "vide", "wnmd", "wndmode",    "Display mode"     },
        { "gamma", "gama", "rnsc", "rndscale",   "Render scale"     },
        { "gamma", "gama", "upsc", "upscaler",   "Upscaling filter" },
        { "gamma", "gama", "shrp", "sharpness",  "Sharpening"       },
        { "gamma", "gama", "shdw", "sunshadows", "Sun shadows"      },
        { "gamma", "gama", "shds", "shdistance", "Shadow distance"  },
        { "gamma", "gama", "fpsl", "fpslimit",   "Frame rate limit" },
        { "gamma", "gama", "shze", "haze",       "Haze"             },
        { "gamma", "gama", "snfl", "sunflare",   "Sun flare"        },
        { "gamma", "gama", "ocrf", "oceanrefl",  "Ocean reflections"},
        { "gamma", "gama", "drds", "drawdist",   "Draw distance"    },
        { "gamma", "gama", "scrr", "screenratio","Screen ratio"     },
        { "gamma", "gama", "uilr", "uilayout",   "UI layout"        },
        { "gamma", "gama", "fovs", "fovscale",   "FOV scale"        },
    };
    // The live table: the defaults, then <user data>/menu_rows.ini overrides
    // per page section (the owner's "tool to alter the UI": adding, renaming,
    // reordering or removing a row becomes a data change).
    std::vector<ExtraOptionRow> g_extraRows;
    int32_t ExtraRowCount( const char *pPage ) {
        int32_t count = 0;
        for ( const ExtraOptionRow &row : g_extraRows ) {
            if ( StringCmpI( row.pPage, pPage ) == 0 ) {
                count++;
            }
        }
        return count;
    }
    // The pitch of the row slots [DATA: dumpClips of /options/video].
    constexpr float kExtraRowPitch = 48.0f;

    // <user data>/menu_rows.ini — the row layout as data. A [page] section
    // ('video' / 'gamma') replaces that page's default rows; each line is
    //     command = variable, label
    // in display order (the first line lands in slot 1, the next in 2 ...).
    // The command ids and their value / step behaviour stay code-owned
    // (extraRowText / stepExtraRow): a row only does something if the game
    // knows its command. Comments (';' / '#') and unknown sections are
    // ignored; the file is optional - without it the defaults above run.
    // The parsed row texts. A deque: push_back never moves the strings, so
    // the c_str()s handed to the rows stay valid (a vector would dangle the
    // short strings' SSO buffers on its first reallocation - the labels came
    // out garbled in the first run).
    std::deque<std::string> g_rowTexts;
    const char *OwnText( std::string text ) {
        g_rowTexts.push_back( std::move( text ) );
        return g_rowTexts.back().c_str();
    }
    std::string TrimCopy( const std::string &text ) {
        const size_t first = text.find_first_not_of( " \t\r" );
        if ( first == std::string::npos ) {
            return std::string();
        }
        const size_t last = text.find_last_not_of( " \t\r" );
        return text.substr( first, last - first + 1 );
    }
    void LoadExtraRows() {
        g_extraRows.assign( std::begin( kDefaultRows ),
                            std::end( kDefaultRows ) );
        if ( gpFile == nullptr ) {
            return;
        }
        const std::filesystem::path file =
            gpFile->getUserDataFolder() / "menu_rows.ini";
        std::ifstream in( file );
        if ( !in ) {
            return;
        }
        struct {
            const char *pPage;
            const char *pPrefix;
        } kPages[] = { {"video", "vide"}, {"gamma", "gama"} };
        const char *pPage = nullptr;
        const char *pPrefix = nullptr;
        std::vector<ExtraOptionRow> parsed;
        int32_t count = 0;
        std::string line;
        while ( std::getline( in, line ) ) {
            const size_t comment = line.find_first_of( ";#" );
            if ( comment != std::string::npos ) {
                line = line.substr( 0, comment );
            }
            line = TrimCopy( line );
            if ( line.empty() ) {
                continue;
            }
            if ( line[0] == '[' ) {
                const size_t close = line.find( ']' );
                if ( close == std::string::npos ) {
                    continue;
                }
                const std::string section =
                    ToLower( TrimCopy( line.substr( 1, close - 1 ) ) );
                pPage = nullptr;
                pPrefix = nullptr;
                for ( const auto &page : kPages ) {
                    if ( section == page.pPage ) {
                        pPage = page.pPage;
                        pPrefix = page.pPrefix;
                        break;
                    }
                }
                continue;
            }
            if ( pPage == nullptr ) {
                continue;
            }
            const size_t equals = line.find( '=' );
            if ( equals == std::string::npos ) {
                continue;
            }
            const std::string command = ToLower( TrimCopy( line.substr( 0, equals ) ) );
            if ( command.empty() ) {
                continue;
            }
            // variable, label (further columns ignored)
            std::string variable, label;
            const std::string values = line.substr( equals + 1 );
            const size_t comma = values.find( ',' );
            if ( comma == std::string::npos ) {
                variable = TrimCopy( values );
            } else {
                variable = TrimCopy( values.substr( 0, comma ) );
                label = TrimCopy( values.substr( comma + 1 ) );
                const size_t second = label.find( ',' );
                if ( second != std::string::npos ) {
                    label = TrimCopy( label.substr( 0, second ) );
                }
            }
            if ( variable.empty() || label.empty() ) {
                continue;
            }
            parsed.push_back( { pPage, pPrefix, OwnText( command ),
                                OwnText( variable ), OwnText( label ) } );
            count++;
        }
        if ( parsed.empty() ) {
            return;
        }
        // A section replaces that page's defaults; sections not in the file
        // keep theirs.
        bool bVideo = false, bGamma = false;
        for ( const ExtraOptionRow &row : parsed ) {
            if ( StringCmpI( row.pPage, "video" ) == 0 ) {
                bVideo = true;
            }
            if ( StringCmpI( row.pPage, "gamma" ) == 0 ) {
                bGamma = true;
            }
        }
        std::vector<ExtraOptionRow> final;
        for ( const ExtraOptionRow &row : parsed ) {
            final.push_back( row );
        }
        for ( const ExtraOptionRow &row : kDefaultRows ) {
            const bool bSectionOverridden =
                ( StringCmpI( row.pPage, "video" ) == 0 && bVideo ) ||
                ( StringCmpI( row.pPage, "gamma" ) == 0 && bGamma );
            if ( !bSectionOverridden ) {
                final.push_back( row );
            }
        }
        g_extraRows = final;
        OTDU_LOG_INFO( "PauseMenu: menu_rows.ini: %d rows from '%s'\n", count,
                       file.string().c_str() );
    }

    // --- The Driving Controls page (/options/controls_menu, 'opts:cont:') ---
    // The movie's list page (content_menu/pc) shows the bindings in 9 view
    // lines ('line1'..'line9', each with the variable-bound text fields
    // control_name / control_bind / control_device and the pad-glyph icon at
    // content/command_bind/icon) and drives its own navigation: its key
    // button handles Up / Down (the 'line_view' focus, wrapping within the 9
    // lines), Enter (the bind dialog), Backspace, x (Detect Device), y
    // (Save) and Delete (Unbind). The game fills the rows and feeds the
    // binder. The row names are plain English like the graphics rows' [INF:
    // the DB has no keys for them]; the retail list's bike rows (Lie down /
    // Sit up) have no port action and are left out.
    struct ControlRow {
        eInputAction Action;
        const char *pName;
    };
    constexpr ControlRow kControlRows[] = {
        { eInputAction::IA_CarAccel,     "Acceleration"  },
        { eInputAction::IA_CarBrake,     "Brake"         },
        { eInputAction::IA_CarLeft,      "Left"          },
        { eInputAction::IA_CarRight,     "Right"         },
        { eInputAction::IA_CarHandBrake, "Handbrake"     },
        { eInputAction::IA_CarClutch,    "Clutch"        },
        { eInputAction::IA_CarGearUp,    "Gear up"       },
        { eInputAction::IA_CarGearDown,  "Gear down"     },
        { eInputAction::IA_CarCamera,    "Change camera" },
        { eInputAction::IA_CarBackView,  "Look back"     },
        { eInputAction::IA_CarLight,     "Lights"        },
        { eInputAction::IA_CarHorn,      "Horn"          },
        { eInputAction::IA_BackOnTrack,  "Back on track" },
    };
    constexpr int32_t kControlViewLines = 9; // the page's view lines (GetViewSize)

    // The pad glyph frame + the retail binding text of a pad input. The
    // glyph labels are the row icon sprite's frames; the "Button N" texts
    // are the DirectInput channel numbers of DeviceX360Pad.xmb (X = BUTTON_2
    // -> 'Button 2', LT = BUTTON_6 -> 'Button 6', RT = BUTTON_7 -> 'Button
    // 7', the owner's retail capture), which the SDL button enum matches
    // (WEST = 2, NORTH = 3) and the trigger axes offset by two (SDL axes
    // 4 / 5 are channels 6 / 7). The sticks read like retail ('Stick 1
    // left'); a keyboard row shows the bare key letter and no glyph.
    const char *PadGlyphOfButton( int32_t button ) {
        switch ( button ) {
        case SDL_GAMEPAD_BUTTON_SOUTH:            return "a";
        case SDL_GAMEPAD_BUTTON_EAST:             return "b";
        case SDL_GAMEPAD_BUTTON_WEST:             return "x";
        case SDL_GAMEPAD_BUTTON_NORTH:            return "y";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:    return "lb";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:   return "rb";
        case SDL_GAMEPAD_BUTTON_BACK:             return "select";
        case SDL_GAMEPAD_BUTTON_START:            return "start";
        case SDL_GAMEPAD_BUTTON_DPAD_UP:          return "up";
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:        return "down";
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:        return "left";
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:       return "right";
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:       return "clic_ls";
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:      return "clic_rs";
        default:                                  return "empty";
        }
    }
    const char *PadGlyphOfAxis( int32_t axis, float sign ) {
        switch ( axis ) {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return "lt";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "rt";
        case SDL_GAMEPAD_AXIS_LEFTX:         return sign >= 0.0f ? "right_ls" : "left_ls";
        case SDL_GAMEPAD_AXIS_LEFTY:         return sign >= 0.0f ? "down_ls" : "up_ls";
        case SDL_GAMEPAD_AXIS_RIGHTX:        return sign >= 0.0f ? "right_rs" : "left_rs";
        case SDL_GAMEPAD_AXIS_RIGHTY:        return sign >= 0.0f ? "down_rs" : "up_rs";
        default:                             return "empty";
        }
    }
    std::string PadTextOfButton( int32_t button ) {
        int32_t channel = -1;
        switch ( button ) {
        case SDL_GAMEPAD_BUTTON_SOUTH:          channel = 0;  break;
        case SDL_GAMEPAD_BUTTON_EAST:           channel = 1;  break;
        case SDL_GAMEPAD_BUTTON_WEST:           channel = 2;  break;
        case SDL_GAMEPAD_BUTTON_NORTH:          channel = 3;  break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  channel = 4;  break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: channel = 5;  break;
        case SDL_GAMEPAD_BUTTON_START:          channel = 8;  break;
        case SDL_GAMEPAD_BUTTON_BACK:           channel = 9;  break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:     channel = 10; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    channel = 11; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:        channel = 12; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      channel = 13; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      channel = 14; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     channel = 15; break;
        }
        return channel >= 0 ? "Button " + std::to_string( channel ) : std::string();
    }
    std::string PadTextOfAxis( int32_t axis, float sign ) {
        switch ( axis ) {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return "Button 6";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Button 7";
        case SDL_GAMEPAD_AXIS_LEFTX:         return std::string( "Stick 1 " ) + ( sign >= 0.0f ? "right" : "left" );
        case SDL_GAMEPAD_AXIS_LEFTY:         return std::string( "Stick 1 " ) + ( sign >= 0.0f ? "down" : "up" );
        case SDL_GAMEPAD_AXIS_RIGHTX:        return std::string( "Stick 2 " ) + ( sign >= 0.0f ? "right" : "left" );
        case SDL_GAMEPAD_AXIS_RIGHTY:        return std::string( "Stick 2 " ) + ( sign >= 0.0f ? "down" : "up" );
        default:                             return std::string();
        }
    }
} // namespace

void PauseMenu::onOptionsCommand( const std::string &command ) {
    // The pages talk to the game with 'opts:<page>:<what>': 'open' when the
    // page builds its rows (before it reads the variables: the values are
    // pushed synchronously from here), '<cmd>:slid:updt' / choice box
    // updates after the widget wrote its variable back, 'vali' (OK) and
    // 'canc' (Cancel) [DATA, MAIN.vmf A() / B() / sendUpdateCommand()].
    // A page's Move handler gates on _root.Input === true (the strict check
    // of the page script, MAIN.vmf Move): the entry menus pass with their
    // looser one, but the option pages' row focus never moved without it -
    // the exe's Flash glue sets the flag for the screen it makes
    // interactive [INF: the exact retail mechanism is ENC, the effect is
    // this flag].
    if ( command.size() >= 5 && command.compare( command.size() - 5, 5, ":open" ) == 0 ) {
        player.setVariableBool( "/:Input", true );
    }
    const std::string rest = command.substr( 5 );
    if ( StringNCmpI( rest.c_str(), "audi:", 5 ) == 0 ) {
        const std::string what = rest.substr( 5 );
        GameOptions &options = GetGameOptions();
        if ( what == "open" ) {
            openPage = eRowsPage::Audio;
            optionsBackup = options;
            for ( const AudioOptionVariable &entry : kAudioVariables ) {
                player.setVariableNumber( entry.pVariable,
                                          options.*entry.pValue );
            }
            for ( const char *pMute : kAudioMutes ) {
                player.setVariableBool( pMute, false );
            }
        } else if ( what == "canc" ) {
            // The units row lives on the native Advanced settings page and
            // saves at once (SetImperialUnits), so Cancel must not revert it:
            // the file would stay imperial while the live readouts went metric.
            const int32_t units = options.Units;
            options = optionsBackup;
            options.Units = units;
            ApplyAudioOptions();
        } else if ( what == "vali" ) {
            SaveGameOptions();
        } else {
            // A widget update: read every row back (the slider has already
            // written _root.<variable>) and apply it live.
            for ( const AudioOptionVariable &entry : kAudioVariables ) {
                const std::string value = player.getVariable( entry.pVariable );
                if ( !value.empty() && value != "undefined" ) {
                    options.*entry.pValue =
                        static_cast<int32_t>( atof( value.c_str() ) + 0.5 );
                }
            }
            ApplyAudioOptions();
        }
        OTDU_LOG_INFO( "PauseMenu: options audio '%s': master %d music %d "
                       "vehicles %d voices %d sfx %d voip %d\n",
                       what.c_str(), options.MasterVolume, options.MusicVolume,
                       options.VehiclesVolume, options.VoicesVolume,
                       options.SfxVolume, options.VoiceIPVolume );
        return;
    }
    if ( StringNCmpI( rest.c_str(), "gama:", 5 ) == 0 ) {
        // The brightness page: one slider (gams, /:gamma, 0..20) that the
        // post-process LUT pass reads live [DATA, MAIN.vmf options/gamma].
        // The game's graphics rows extend it (extendOptionsRows).
        const std::string what = rest.substr( 5 );
        GameOptions &options = GetGameOptions();
        if ( what == "open" ) {
            optionsBackup = options;
            player.setVariableNumber( "/:gamma", options.Gamma );
            for ( const ExtraOptionRow &row : g_extraRows ) {
                if ( StringCmpI( row.pPrefix, "gama" ) == 0 ) {
                    player.setVariable( std::string( "/:" ) + row.pVariable,
                                        extraRowText( row.pCommand ) );
                }
            }
            openPage = eRowsPage::Gamma;
            pendingRowsPage = eRowsPage::Gamma;
            bRowsExtended = false;
            focusSlot = 0;
            windowFirst = 0;
        } else if ( what == "canc" ) {
            // Units were saved by the native page; Cancel keeps them (see the
            // audio page's canc).
            const int32_t units = options.Units;
            options = optionsBackup;
            options.Units = units;
            ApplyVideoOptions();
            removeExtraOptionRows();
        } else if ( what == "vali" ) {
            SaveGameOptions();
            removeExtraOptionRows();
        } else if ( what.compare( 0, 5, "gams:" ) == 0 ) {
            const std::string value = player.getVariable( "/:gamma" );
            if ( !value.empty() && value != "undefined" ) {
                options.Gamma =
                    Clamp( static_cast<int32_t>( atof( value.c_str() ) + 0.5 ),
                           0, GameOptions::kGammaMax );
            }
        } else {
            // A game row: '<cmd>:scpr' / '<cmd>:scne' from the clone's arrows.
            const size_t colon = what.find( ':' );
            const std::string command =
                colon == std::string::npos ? what : what.substr( 0, colon );
            const std::string action =
                colon == std::string::npos ? std::string() : what.substr( colon + 1 );
            stepExtraRow( command, action == "scne" ? 1 : action == "scpr" ? -1 : 0 );
        }
        OTDU_LOG_INFO( "PauseMenu: options gamma '%s': %d (post gamma %.2f)\n",
                       what.c_str(), options.Gamma, GetPostGamma() );
        return;
    }
    if ( StringNCmpI( rest.c_str(), "vide:", 5 ) == 0 ) {
        onVideoOptionsCommand( rest.substr( 5 ) );
        return;
    }
    if ( StringNCmpI( rest.c_str(), "cont:", 5 ) == 0 ) {
        // The Controls page (opts:cont:): the movie's list page
        // (content_menu/pc) shows the bindings and drives its own navigation
        // and bind dialog; the game fills the rows ('bind:open', one tick
        // later like the other pages' rows), feeds the binder, unbinds
        // ('bind:delt' from the movie's Delete / Unbind) and persists (Y is
        // the page's own Save: the close fires 'vadt', Back fires 'canc').
        const std::string what = rest.substr( 5 );
        if ( what == "bind:open" ) {
            bOnControlsPage = true;
            bControlsPendingFill = true;
            controlsBackup = gpInput->SerializeControls();
            GetAdvancedGraphics().setVideoPageHint( false );
            // The page's own key button navigates only while _root.fake is
            // truthy (the Down / Up handlers gate on it - the retail exe's
            // glue runs this page with the flag set; the port's other pages
            // check it the other way, so it is scoped to this page and
            // restored on close).
            player.setVariableBool( "/:fake", true );
            OTDU_LOG_INFO( "PauseMenu: controls page opened (bind:open)" );
        } else if ( what == "canc" ) {
            gpInput->RestoreControls( controlsBackup );
            bOnControlsPage = false;
            bBindCaptureArmed = false;
            player.setVariable( "/:fake", "" );
            GetAdvancedGraphics().setVideoPageHint( bOnVideoPage );
            OTDU_LOG_INFO( "PauseMenu: controls page back (bindings restored)" );
        } else if ( what == "vadt" ) {
            if ( gpFile != nullptr ) {
                gpInput->SaveControls( gpFile->getUserDataFolder() / "controls.ini" );
            }
            bOnControlsPage = false;
            bBindCaptureArmed = false;
            player.setVariable( "/:fake", "" );
            GetAdvancedGraphics().setVideoPageHint( bOnVideoPage );
        } else if ( what == "bind:delt" ) {
            // Unbind: the selected row (the movie's line_view) of the window.
            const std::string lineViewText =
                player.getVariable( std::string( kControlsPcPath ) + ":line_view" );
            const int32_t lineView = atoi( lineViewText.c_str() );
            const int32_t row = controlsOffset + lineView - 1;
            if ( lineView >= 1 &&
                 row >= 0 && row < static_cast<int32_t>( std::size( kControlRows ) ) ) {
                gpInput->clearPadBinding( kControlRows[row].Action );
                gpInput->clearKeyboardBinding( kControlRows[row].Action );
                fillControlsList();
                OTDU_LOG_INFO( "PauseMenu: controls unbind '%s'\n",
                               kControlRows[row].pName );
            }
        } else if ( what == "sede:canc" || what == "sede:vali" ) {
            // The device-select page (x / Detect Device) closed: the list
            // comes back (its lines were hidden for the sub-page).
            player.gotoLabel( kControlsPcPath, "device_out", true );
        }
        return;
    }
    OTDU_LOG_INFO( "PauseMenu: options command '%s' not handled yet\n",
                   command.c_str() );
}

// The video page (options/video initParameters): setParam rows are
// [clip, label, 'choicebox', variable, command, lock]. A choice box shows the
// TEXT of its variable and fires 'opts:vide:<cmd>:scpr' / ':scne' on
// Left / Right, then re-reads the variable (updateText): the game owns the
// list of values [DATA, MAIN.vmf options/video + choicebox].
namespace {
    enum class VideoRow {
        Resolution,
        Frequency,
        VSync,
        AntiAlias,
        Environment,
        Car,
        Texture,
        Fx
    };
    struct VideoRowInfo {
        VideoRow Row;
        const char *pCommand;
        const char *pVariable;
    };
    constexpr VideoRowInfo kVideoRows[] = {
        {VideoRow::Resolution,  "prrs", "/:resolution"  },
        {VideoRow::Frequency,   "prfr", "/:frequency"   },
        {VideoRow::VSync,       "vsyn", "/:VSync"       },
        {VideoRow::AntiAlias,   "praa", "/:anti_alias"  },
        {VideoRow::Environment, "prlo", "/:lod"         },
        {VideoRow::Car,         "caop", "/:car_opponent"},
        {VideoRow::Texture,     "txfl", "/:texture"     },
        {VideoRow::Fx,          "fxqu", "/:fx_qual"     },
    };
} // namespace

std::string PauseMenu::localized( const char *pName, const char *pFallback ) {
    const std::string value = player.getVariable( std::string( "/:" ) + pName );
    return value.empty() || value == "undefined" ? pFallback : value;
}

std::string PauseMenu::videoRowText( int32_t row ) {
    const GameOptions &options = GetGameOptions();
    auto level = [this]( int32_t value ) {
        return value <= 1   ? localized( "m_Low", "Low" )
               : value == 2 ? localized( "m_Medium", "Medium" )
                            : localized( "m_High", "High" );
    };
    switch ( kVideoRows[row].Row ) {
    case VideoRow::Resolution:
        return std::to_string( options.VideoWidth ) + " x " +
               std::to_string( options.VideoHeight );
    case VideoRow::Frequency:
        return std::to_string( GetDisplayRefreshRate() ) + " Hz";
    case VideoRow::VSync:
        return options.VSync != 0 ? localized( "m_On", "On" )
                                  : localized( "m_Off", "Off" );
    case VideoRow::AntiAlias:
        // The retail anti-aliasing row is the port's edge smoothing: the
        // renderer has no MSAA, the FXAA-like pass IS its anti-aliasing
        // (AntiAlias itself is kept in options.ini, applied by nothing).
        return options.EdgeSmoothing != 0 ? localized( "m_On", "On" )
                                          : localized( "m_Off", "Off" );
    case VideoRow::Environment:
        return level( options.EnvQuality );
    case VideoRow::Car:
        return level( options.CarQuality );
    case VideoRow::Texture:
        return level( options.TextureQuality );
    case VideoRow::Fx:
        return level( options.FxQuality );
    }
    return std::string();
}

void PauseMenu::onVideoOptionsCommand( const std::string &what ) {
    GameOptions &options = GetGameOptions();
    bOnVideoPage =
        what != "vali" && what != "canc" && what != "quit" && what != "test";
    GetAdvancedGraphics().setVideoPageHint( bOnVideoPage );
    if ( what == "open" ) {
        optionsBackup = options;
        // Options not chosen yet start from what the game runs with.
        const VideoSettings &video = gpConfig->getVideoSettings();
        if ( options.VideoWidth <= 0 || options.VideoHeight <= 0 ) {
            options.VideoWidth = video.Width;
            options.VideoHeight = video.Height;
        }
        if ( options.VSync < 0 ) {
            options.VSync = video.bVSync ? 1 : 0;
        }
        if ( options.EnvQuality < 0 ) {
            options.EnvQuality = Max( video.EnvironmentQuality, 1 );
        }
        for ( int32_t i = 0;
              i < static_cast<int32_t>( std::size( kVideoRows ) ); i++ ) {
            player.setVariable( kVideoRows[i].pVariable, videoRowText( i ) );
        }
        // The refresh rate follows the display; its retail row (p1, locked
        // greyed) is the game's Display mode row now (the default rows), so the
        // lock flag goes [INF: documented divergence].
        player.setVariableBool( "/:LockFrequency", false );
        player.setVariableBool( "/:LockVSync", false );
        player.setVariableBool( "/:IsVideoChanged", false );
        player.setVariableBool( "/:IsVideoChangeOk", false );
        // The game's own rows (Display mode): values now, the rows themselves
        // one tick later when the retail setParam table is complete.
        for ( const ExtraOptionRow &row : g_extraRows ) {
            if ( StringCmpI( row.pPrefix, "vide" ) == 0 ) {
                player.setVariable( std::string( "/:" ) + row.pVariable,
                                    extraRowText( row.pCommand ) );
            }
        }
        openPage = eRowsPage::Video;
        pendingRowsPage = eRowsPage::Video;
        bRowsExtended = false;
        focusSlot = 0;
        windowFirst = 0;
    } else if ( what == "canc" ) {
        // Units were saved by the native page; Cancel keeps them (see the
        // audio page's canc).
        const int32_t units = options.Units;
        options = optionsBackup;
        options.Units = units;
        ApplyVideoOptions();
        removeExtraOptionRows();
    } else if ( what == "vali" ) {
        SaveGameOptions();
        removeExtraOptionRows();
    } else if ( what == "quit" ) {
        // B() of the page: the movie closes it itself. Retail applies a video
        // change only through OK (and its keep-settings dialog), so backing
        // out discards it - the port applies live, so Quit walks the values
        // (and the display mode) back like Cancel does.
        const int32_t units = options.Units;
        options = optionsBackup;
        options.Units = units;
        ApplyVideoOptions();
        removeExtraOptionRows();
    } else if ( what == "test" ) {
        // A() of the page sends only 'test', then reads IsVideoChanged /
        // IsVideoChangeOk: a changed mode raises the 'keep these settings?'
        // dialog (10 s timer), an unchanged one does nothing. The changes are
        // applied live (and Cancel restores them), so nothing is asked: keep
        // them and step back with the movie's own B() once this call returns.
        SaveGameOptions();
        bPendingVideoClose = true;
        removeExtraOptionRows();
    } else {
        const size_t colon = what.find( ':' );
        const std::string command = what.substr( 0, colon );
        const std::string action = colon == std::string::npos
                                       ? std::string()
                                       : what.substr( colon + 1 );
        const int32_t step = action == "scne" ? 1 : action == "scpr" ? -1 : 0;
        bool bHandled = stepExtraRow( command, step );
        for ( int32_t i = 0;
              step != 0 && i < static_cast<int32_t>( std::size( kVideoRows ) );
              i++ ) {
            if ( command != kVideoRows[i].pCommand ) {
                continue;
            }
            bHandled = true;
            switch ( kVideoRows[i].Row ) {
            case VideoRow::Resolution: {
                const auto list = GetVideoResolutions();
                int32_t index = 0;
                for ( int32_t j = 0; j < static_cast<int32_t>( list.size() );
                      j++ ) {
                    if ( list[j].first == options.VideoWidth &&
                         list[j].second == options.VideoHeight ) {
                        index = j;
                    }
                }
                index = Clamp( index + step, 0,
                               static_cast<int32_t>( list.size() ) - 1 );
                options.VideoWidth = list[index].first;
                options.VideoHeight = list[index].second;
                break;
            }
            case VideoRow::VSync:
                options.VSync = options.VSync != 0 ? 0 : 1;
                break;
            case VideoRow::AntiAlias:
                // The port's real anti-aliasing (see videoRowText).
                options.EdgeSmoothing = options.EdgeSmoothing != 0 ? 0 : 1;
                break;
            case VideoRow::Environment:
                options.EnvQuality = Clamp( options.EnvQuality + step, 1, 3 );
                break;
            case VideoRow::Car:
                options.CarQuality = Clamp( options.CarQuality + step, 1, 3 );
                break;
            case VideoRow::Texture:
                options.TextureQuality =
                    Clamp( options.TextureQuality + step, 1, 3 );
                break;
            case VideoRow::Fx:
                options.FxQuality = Clamp( options.FxQuality + step, 1, 3 );
                break;
            case VideoRow::Frequency:
                break;
            }
            player.setVariable( kVideoRows[i].pVariable, videoRowText( i ) );
            ApplyVideoOptions();
        }
        if ( !bHandled ) {
            OTDU_LOG_INFO(
                "PauseMenu: options video command '%s' not handled\n",
                what.c_str() );
        }
    }
    OTDU_LOG_INFO( "PauseMenu: options video '%s': %dx%d vsync %d env %d aa %d "
                   "car %d tex %d fx %d\n",
                   what.c_str(), options.VideoWidth, options.VideoHeight,
                   options.VSync, options.EnvQuality, options.EdgeSmoothing,
                   options.CarQuality, options.TextureQuality,
                   options.FxQuality );
}

// The value text of the game's own rows (the command ids). English plain text
// [INF: the DB has no keys for these settings].
std::string PauseMenu::extraRowText( const char *pCommand ) {
    const GameOptions &options = GetGameOptions();
    if ( StringCmpI( pCommand, "wnmd" ) == 0 ) {
        // The mode the window actually runs with: -1 ("not chosen") resolves
        // through EffectiveWindowMode, so the row never says Windowed while
        // the screen is a fullscreen desktop (that lie is what read as
        // "windowed keeps reverting to borderless" - the first step away from
        // it landed on Borderless and looked like a revert).
        switch ( EffectiveWindowMode() ) {
        case GameOptions::kWindowModeBorderless:
            return "Borderless";
        case GameOptions::kWindowModeFullscreen:
            return "Fullscreen";
        default:
            return "Windowed";
        }
    }
    if ( StringCmpI( pCommand, "rnsc" ) == 0 ) {
        return std::to_string( options.RenderScale ) + " %";
    }
    if ( StringCmpI( pCommand, "upsc" ) == 0 ) {
        return options.Upscaler == 1 ? "Bicubic (Catmull-Rom)" : "Bilinear";
    }
    if ( StringCmpI( pCommand, "shrp" ) == 0 ) {
        return options.Sharpness > 0 ? std::to_string( options.Sharpness ) : "Off";
    }
    if ( StringCmpI( pCommand, "shdw" ) == 0 ) {
        return options.Shadows != 0 ? "On" : "Off";
    }
    if ( StringCmpI( pCommand, "shds" ) == 0 ) {
        return std::to_string( options.ShadowDistance ) + " %";
    }
    if ( StringCmpI( pCommand, "fpsl" ) == 0 ) {
        return options.FpsLimit > 0 ? std::to_string( options.FpsLimit ) + " fps" : "Unlimited";
    }
    if ( StringCmpI( pCommand, "shze" ) == 0 ) {
        return options.Haze != 0 ? "On" : "Off";
    }
    if ( StringCmpI( pCommand, "snfl" ) == 0 ) {
        return options.SunFlare != 0 ? "On" : "Off";
    }
    if ( StringCmpI( pCommand, "ocrf" ) == 0 ) {
        return options.OceanReflections != 0 ? "On" : "Off";
    }
    if ( StringCmpI( pCommand, "drds" ) == 0 ) {
        return std::to_string( options.DrawDistance ) + " %";
    }
    if ( StringCmpI( pCommand, "scrr" ) == 0 ) {
        switch ( options.ScreenRatioOverride ) {
        case 1:
            return "16:9";
        case 2:
            return "4:3";
        default:
            return "Auto";
        }
    }
    if ( StringCmpI( pCommand, "uilr" ) == 0 ) {
        return options.UiLayout == GameOptions::kUiLayoutFit ? "Fit (bars)" : "Stretch (fill)";
    }
    if ( StringCmpI( pCommand, "fovs" ) == 0 ) {
        return std::to_string( options.FovScale ) + " %";
    }
    return std::string();
}

// Left / Right on one of the game's rows: the value steps, the text is
// pushed back (the clone's updateText re-reads the variable) and the
// graphics rows mark the quality profile Custom (like the F2 page). The
// window mode applies at once (ApplyWindowMode inside ApplyVideoOptions);
// the other rows are read live by the renderer every frame.
bool PauseMenu::stepExtraRow( const std::string &command, int32_t step ) {
    if ( step == 0 ) {
        return false;
    }
    for ( const ExtraOptionRow &row : g_extraRows ) {
        if ( StringCmpI( command.c_str(), row.pCommand ) != 0 ) {
            continue;
        }
        GameOptions &options = GetGameOptions();
        if ( StringCmpI( row.pCommand, "wnmd" ) == 0 ) {
            // The first step starts from the mode the window runs with (the
            // same resolution extraRowText shows - EffectiveWindowMode), so
            // one press lands exactly on the neighbour of what is on screen.
            if ( options.WindowMode < 0 ) {
                options.WindowMode = EffectiveWindowMode();
            }
            options.WindowMode = Clamp( options.WindowMode + step, 0, 2 );
            ApplyVideoOptions();
        } else if ( StringCmpI( row.pCommand, "rnsc" ) == 0 ) {
            options.RenderScale = Clamp( options.RenderScale + step * 5, 50, 200 );
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "upsc" ) == 0 ) {
            options.Upscaler = Clamp( options.Upscaler + step, 0, 1 );
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "shrp" ) == 0 ) {
            options.Sharpness = Clamp( options.Sharpness + step, 0, 10 );
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "shdw" ) == 0 ) {
            options.Shadows = options.Shadows != 0 ? 0 : 1;
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "shds" ) == 0 ) {
            options.ShadowDistance = Clamp( options.ShadowDistance + step * 10, 50, 150 );
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "fpsl" ) == 0 ) {
            options.FpsLimit = Clamp( options.FpsLimit + step * 30, 0, 240 );
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "shze" ) == 0 ) {
            options.Haze = options.Haze != 0 ? 0 : 1;
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "snfl" ) == 0 ) {
            options.SunFlare = options.SunFlare != 0 ? 0 : 1;
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "ocrf" ) == 0 ) {
            options.OceanReflections = options.OceanReflections != 0 ? 0 : 1;
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "drds" ) == 0 ) {
            options.DrawDistance = Clamp( options.DrawDistance + step * 10, 50, 150 );
            options.QualityProfile = 0;
        } else if ( StringCmpI( row.pCommand, "scrr" ) == 0 ) {
            // 0 Auto (the window aspect picks the HUD arrangement), 1 = 16:9,
            // 2 = 4:3. Live: the stage rect re-maps per frame and the drive
            // HUD re-runs its 16_9 / 4_3 arrangements (FreeRideHud::draw).
            options.ScreenRatioOverride = Clamp( options.ScreenRatioOverride + step, 0, 2 );
        } else if ( StringCmpI( row.pCommand, "uilr" ) == 0 ) {
            // 0 Stretch (the retail fill), 1 Fit (the centered 16:9 band).
            options.UiLayout = Clamp( options.UiLayout + step, 0, 1 );
        } else if ( StringCmpI( row.pCommand, "fovs" ) == 0 ) {
            options.FovScale = Clamp( options.FovScale + step * 5, 50, 150 );
        }
        player.setVariable( std::string( "/:" ) + row.pVariable,
                            extraRowText( row.pCommand ) );
        OTDU_LOG_INFO( "PauseMenu: options row '%s' -> %s\n", row.pCommand,
                       extraRowText( row.pCommand ).c_str() );
        return true;
    }
    return false;
}

// Writes the game's rows into the open page clip's setParam (one tick after
// its 'open', when the retail rows are in) and has the page initialize them
// with its own setParameters(first, last): every row lives in a row slot
// p<slot> of the page clip - an existing one (the Video page's dead
// Frequency row) or a clone of the Video page's choicebox row placed into
// the page's free panel space (the Gamma page). The page's machinery does
// the rest (values from the variables, arrows firing
// 'opts:<page>:<cmd>:scpr/scne', locks, focus).
void PauseMenu::extendOptionsRows() {
    if ( bRowsExtended || pendingRowsPage == eRowsPage::None ) {
        return;
    }
    const bool bVideo = pendingRowsPage == eRowsPage::Video;
    const char *pPageClip = bVideo ? "video" : "gamma";
    const std::string pagePath = std::string( "/options/" ) + pPageClip;
    const int32_t rowCount = ExtraRowCount( pPageClip );
    const int32_t visible = Min( kRowSlots, rowCount );
    if ( visible == 0 ) {
        pendingRowsPage = eRowsPage::None;
        return;
    }
    // The generic choicebox row sprite of the movie, identified once by its
    // animation label set (definitions scan - the sprite is not exported and
    // a page's row instances only exist while that page is open).
    static uint16_t rowCharacter = 0;
    if ( rowCharacter == 0 ) {
        rowCharacter = player.getDefinitions().findSpriteByLabels(
            { "default", "open", "stop", "on", "stop_on", "change_left", "change_right", "out",
              "close" },
            80 );
        if ( rowCharacter != 0 ) {
            OTDU_LOG_INFO( "PauseMenu: options row sprite: character %u\n", rowCharacter );
        }
    }
    // The page's own bkgd0 anchors the local slot grid (the panels share one
    // layout: the strips sit at the same local positions, a row 3 px lower
    // and 19 px right of its strip, 48 px apart) [DATA: dumpClips].
    float pageX = 0.0f, pageY = 0.0f, bkgdX = 0.0f, bkgdY = 0.0f;
    const bool bHaveAnchor =
        player.getClipStagePosition( pagePath, pageX, pageY ) &&
        player.getClipStagePosition( pagePath + "/bkgd0", bkgdX, bkgdY );
    // Slots 1..visible: an existing row clip (the Video page's rows) or a
    // placed instance of the generic choicebox row (the Gamma page's free
    // panel space). The game rows scroll a window through these slots
    // (moveOptionsFocus); slot 0 stays the page's own first row (the Video
    // page's Resolution / the Gamma page's slider).
    int32_t placed = 0;
    for ( int32_t slot = 1; slot <= visible; slot++ ) {
        const std::string rowName = "p" + std::to_string( slot );
        if ( player.findClip( pagePath + "/" + rowName ) != nullptr ) {
            continue;
        }
        // The page art has no such slot: place a choicebox row (and clone
        // the page's own bkgd strip) into the page's free panel space.
        if ( rowCharacter == 0 || !bHaveAnchor ) {
            continue;
        }
        const float slotX = bkgdX - pageX + 19.0f;
        const float slotY = bkgdY - pageY + 3.0f + kExtraRowPitch * slot;
        player.placeCharacter( pagePath, rowCharacter, rowName, 200 + slot, slotX,
                               slotY );
        if ( player.duplicate( pagePath + "/bkgd0", "bkgd" + std::to_string( slot ),
                               250 + slot ) ) {
            player.setClipTransform( pagePath + "/bkgd" + std::to_string( slot ),
                                     bkgdX - pageX, ( bkgdY - pageY ) + kExtraRowPitch * slot,
                                     0.0f, 100.0f, 100.0f );
        }
        placed++;
    }
    windowFirst = 0;
    focusSlot = 0;
    writeOptionsWindow( pPageClip );
    bRowsExtended = true;
    // The Gamma page's retail hint board ("Adjust the Gamma by observing...")
    // occupies the panel space the game rows now use - it goes away while the
    // rows are up and comes back with them (the hint is the board's art).
    if ( !bVideo ) {
        player.setClipVisible( pagePath + "/gamma_board", false );
    }
    OTDU_LOG_INFO(
        "PauseMenu: options %s extended with %d game rows (%d visible, "
        "%d slots placed)\n",
        pPageClip, rowCount, visible, placed );
}

// (Re)writes the visible window of the open page's game rows into
// setParam[1..visible] and re-runs the page's own setParameters(1, last):
// every slot row is rebound (label, id, cmd, varToFocus, initChoiceBox), so
// the arrows fire the visible row's command and the values / locks follow.
// The window shows the game rows windowFirst..windowFirst+visible-1 of the
// page's list; the retail rows keep their slots (on Gamma that is the slider
// at slot 0 - it never scrolls; on Video the game rows replace the retail
// entries of the slots they take).
void PauseMenu::writeOptionsWindow( const char *pPageClip ) {
    const std::string pagePath = std::string( "/options/" ) + pPageClip;
    std::vector<const ExtraOptionRow *> rows;
    for ( const ExtraOptionRow &row : g_extraRows ) {
        if ( StringCmpI( row.pPage, pPageClip ) == 0 ) {
            rows.push_back( &row );
        }
    }
    const int32_t visible = Min( kRowSlots, static_cast<int32_t>( rows.size() ) );
    if ( visible == 0 ) {
        return;
    }
    std::vector<std::vector<std::string>> entries;
    for ( int32_t slot = 0; slot < visible; slot++ ) {
        const ExtraOptionRow &row = *rows[windowFirst + slot];
        // The setParam shape initParameters pushes (the NewObject args pop in
        // reverse, so the retail rows read): [0] the variable name the row
        // displays, [1] the label text (the row's param_title field), [2] the
        // type, [3] the '_root.<name>' value path, [4] the full command
        // (getParamCmd -> the row's cmd), [5] the lock flag.
        entries.push_back( { row.pVariable,
                             row.pLabel,
                             "choicebox",
                             std::string( "_root." ) + row.pVariable,
                             std::string( "opts:" ) + row.pPrefix + ":" + row.pCommand,
                             "0" } );
    }
    if ( !player.writeArrayRows( pagePath + ":setParam", 1, entries ) ) {
        OTDU_LOG_WARN( "PauseMenu: options rows: %s:setParam is not there\n",
                       pagePath.c_str() );
        removeExtraOptionRows();
        return;
    }
    // setParameters walks 1..visible with the second bound as an EXCLUSIVE
    // limit (the movie calls it with (0, setParam.length) for its own rows):
    // the loop compare reads two numbers, so the bounds go as numbers - two
    // strings would compare lexicographically and '8' < '8' is false (the
    // row never initialized).
    player.invokeWithNumbers( pagePath, "setParameters",
                              { 1.0, static_cast<double>( visible + 1 ) } );
    // The value text of the row boxes: the page fills the retail rows from
    // the variables during its own open flow, a re-initialized row shows its
    // first value only once focused - the game sets it directly here
    // (updateText re-reads the variable on focus / change anyway).
    for ( int32_t slot = 0; slot < visible; slot++ ) {
        const ExtraOptionRow &row = *rows[windowFirst + slot];
        player.setVariable( pagePath + "/p" + std::to_string( slot + 1 ) + ":param_txt",
                            extraRowText( row.pCommand ) );
    }
}

// Up / Down on an open page that carries game rows. Retail never needs it on
// the Gamma page (its own key button 16593 on the page's stop frame has NO
// Up / Down conditions - the retail Image settings page is one slider row,
// its Move is never called; -logpausekeys + the sprite dump), and a page
// whose game rows overflow the panel needs the window shift. The law is the
// page's own Move (numParam up: -1 with wrap, down: +1 with wrap over the
// page's lastParam) applied to the panel slots 0..visible, plus the game's
// window: a focus that walks off the visible game rows moves windowFirst one
// row and rebinds the slots (writeOptionsWindow). The row highlight is the
// page's own lightParam (reads numParam: 'out' on the old slot, 'on' + its
// own updateText on the new one - 'on'/'stop_on' is also where the row's
// Left / Right arrow buttons are placed, so the focused row is the only one
// that answers them).
void PauseMenu::moveOptionsFocus( int32_t direction ) {
    if ( openPage != eRowsPage::Video && openPage != eRowsPage::Gamma ) {
        return;
    }
    const bool bVideo = openPage == eRowsPage::Video;
    const char *pPageClip = bVideo ? "video" : "gamma";
    const std::string pagePath = std::string( "/options/" ) + pPageClip;
    const int32_t rowCount = ExtraRowCount( pPageClip );
    if ( rowCount == 0 ) {
        return;
    }
    // The Video page's own key button drives the page's Move while its rows
    // fit the panel; the game takes the keys only for the overflow case.
    if ( bVideo && rowCount <= kRowSlots ) {
        player.sendKey( direction < 0 ? 14 : 15 );
        return;
    }
    const int32_t visible = Min( kRowSlots, rowCount );
    int32_t slot = focusSlot;
    bool bShifted = false;
    if ( direction < 0 ) {
        if ( slot == 1 && windowFirst > 0 ) {
            windowFirst--;
            bShifted = true;
        } else {
            slot = slot > 0 ? slot - 1 : visible;
        }
    } else {
        if ( slot == visible && windowFirst + visible < rowCount ) {
            windowFirst++;
            bShifted = true;
        } else {
            slot = slot < visible ? slot + 1 : 0;
        }
    }
    // Unlight the old slot first (the page's lightParam reads numParam).
    player.setVariableNumber( pagePath + ":numParam", focusSlot );
    player.invokeWithNumbers( pagePath, "lightParam", { 0.0 } );
    if ( bShifted ) {
        writeOptionsWindow( pPageClip );
        // Park every game-slot row at 'stop': a stale 'stop_on' state keeps
        // its arrow buttons live (they are placed on that state's frame) and
        // an unfocused row must not answer Left / Right.
        for ( int32_t i = 1; i <= visible; i++ ) {
            player.gotoLabel( pagePath + "/p" + std::to_string( i ), "stop",
                              false );
        }
    }
    focusSlot = slot;
    player.setVariableNumber( pagePath + ":numParam", slot );
    player.invokeWithNumbers( pagePath, "setVarToFocus", { static_cast<double>( slot ) } );
    player.invokeWithNumbers( pagePath, "lightParam", { 1.0 } );
    player.setVariableNumber( "/:CurrentParam", slot );
    OTDU_LOG_INFO(
        "PauseMenu: options %s focus slot %d (game rows %d..%d of %d)%s\n",
        pPageClip, slot, windowFirst + 1, windowFirst + visible, rowCount,
        bShifted ? " [window scrolled]" : "" );
}

// The rows are the pages' own clips or clones inside them: the page close
// resets them to their blank first frame (the next page entry
// re-initializes them; the bkgd clones stay, hidden with their page).
void PauseMenu::removeExtraOptionRows() {
    for ( const char *pPageClip : { "video", "gamma" } ) {
        const int32_t visible =
            Min( kRowSlots, ExtraRowCount( pPageClip ) );
        for ( int32_t slot = 1; slot <= visible; slot++ ) {
            player.gotoLabel( std::string( "/options/" ) + pPageClip + "/p" +
                                  std::to_string( slot ),
                              "default", false );
        }
    }
    player.setClipVisible( "/options/gamma/gamma_board", true );
    pendingRowsPage = eRowsPage::None;
    openPage = eRowsPage::None;
    bRowsExtended = false;
    focusSlot = 0;
    windowFirst = 0;
}

// Writes the current bindings of the driving actions into the Controls list
// page's 9 view lines. Each line reads its texts from its own variables
// (control_name / control_bind / control_device - the row's text fields are
// variable-bound to them, the same law as the gamma rows' param_title); the
// pad-glyph icon sits at content/command_bind/icon and its frame label picks
// the glyph (a / b / x / y / lb / rb / lt / rt / stick arrows). A row shows
// the pad binding when the action has one, else the keyboard's; the device
// column follows. Lines past the row count play the movie's 'empty' state.
void PauseMenu::fillControlsList() {
    const int32_t rowCount = static_cast<int32_t>( std::size( kControlRows ) );
    const int32_t maxOffset = Max( 0, rowCount - kControlViewLines );
    controlsOffset = Clamp( controlsOffset, 0, maxOffset );
    const bool bPadPlayStation =
        gpInput->getPadStyle() == GSInput::ePadStyle::PS_PlayStation;
    for ( int32_t line = 1; line <= kControlViewLines; line++ ) {
        const std::string linePath =
            std::string( kControlsPcPath ) + "/line" + std::to_string( line );
        const int32_t row = controlsOffset + line - 1;
        if ( row >= rowCount ) {
            player.setVariableNumber( linePath + ":empty", 1 );
            continue;
        }
        player.setVariableNumber( linePath + ":empty", 0 );
        // An unset 'lock' reads as locked in the page's PlayAnimValidate (the
        // Enter on the row answers sndp:wrong and never opens the binder).
        player.setVariableNumber( linePath + ":lock", 0 );
        const ControlRow &controlRow = kControlRows[row];
        player.setVariable( linePath + ":control_name", controlRow.pName );
        const GSInput::PadBinding *pad = nullptr;
        for ( const GSInput::PadBinding &binding : gpInput->getPadBindings() ) {
            if ( binding.Action == controlRow.Action ) {
                pad = &binding;
                break;
            }
        }
        std::string bindText, deviceText;
        const char *pGlyph = nullptr;
        if ( pad != nullptr ) {
            if ( pad->Button >= 0 ) {
                bindText = PadTextOfButton( pad->Button );
                pGlyph = PadGlyphOfButton( pad->Button );
            } else if ( pad->Axis >= 0 ) {
                bindText = PadTextOfAxis( pad->Axis, pad->AxisSign );
                pGlyph = PadGlyphOfAxis( pad->Axis, pad->AxisSign );
            }
            deviceText = bPadPlayStation ? "PlayStation controller"
                                         : "Xbox 360 controller";
        }
        if ( bindText.empty() ) {
            const std::vector<int32_t> &scancodes =
                gpInput->getKeyboardScancodes( controlRow.Action );
            if ( !scancodes.empty() ) {
                const char *pKey = SDL_GetScancodeName(
                    static_cast<SDL_Scancode>( scancodes.front() ) );
                std::string name = pKey != nullptr ? pKey : "";
                if ( !name.empty() ) {
                    name[0] = static_cast<char>(
                        toupper( static_cast<unsigned char>( name[0] ) ) );
                }
                bindText = name;
                deviceText = "Keyboard";
            }
        }
        player.setVariable( linePath + ":control_bind", bindText );
        player.setVariable( linePath + ":control_device", deviceText );
        if ( pGlyph != nullptr ) {
            player.setVariableNumber( linePath + ":line_with_icon", 1 );
            player.setClipVisible(
                linePath + "/content/command_bind/icon", true );
            // Via the first label: a jump backwards into the icon's timeline
            // (a row re-showing a glyph after the demo fill parked it deep)
            // rebuilds the display list and can drop the glyph art, while
            // the forward walk applies the placements as authored.
            player.gotoLabel( linePath + "/content/command_bind/icon", "a",
                              false );
            player.gotoLabel( linePath + "/content/command_bind/icon", pGlyph,
                              false );
            static const bool bLogIcons = CmdLine::HasArg( "-logcontrols" );
            if ( bLogIcons ) {
                const FlashPlayer::Clip *pIcon = player.findClip(
                    linePath + "/content/command_bind/icon" );
                OTDU_LOG_INFO(
                    "PauseMenu: controls line %d glyph '%s' -> icon frame %u\n",
                    line, pGlyph,
                    pIcon != nullptr ? pIcon->CurrentFrame : 0xffff );
            }
        } else {
            player.setVariableNumber( linePath + ":line_with_icon", 0 );
            // A keyboard row (the retail look) shows no glyph: hide the icon
            // clip outright - the page's own loading fill leaves stick
            // glyphs on the lines and the placed icon sprite has no empty
            // frame to park on.
            player.setClipVisible(
                linePath + "/content/command_bind/icon", false );
        }
    }
    player.setVariableNumber( std::string( kControlsPcPath ) + ":nb_lines",
                              kControlViewLines );
    static const bool bLogFill = CmdLine::HasArg( "-logcontrols" );
    if ( bLogFill ) {
        for ( const GSInput::PadBinding &binding : gpInput->getPadBindings() ) {
            if ( binding.Action == eInputAction::IA_CarRight ) {
                OTDU_LOG_INFO(
                    "PauseMenu: controls fill: CarRight pad binding button %d "
                    "axis %d\n",
                    binding.Button, binding.Axis );
            }
        }
        const std::vector<int32_t> &rsc =
            gpInput->getKeyboardScancodes( eInputAction::IA_CarRight );
        std::string codes;
        for ( int32_t code : rsc ) {
            codes += " " + std::to_string( code );
        }
        OTDU_LOG_INFO( "PauseMenu: controls fill: CarRight kb [%s]\n",
                       codes.c_str() );
    }
    // The page's loading spinner ('loading_anim', placed on the 'loading'
    // frames) keeps playing once the timeline jumped to 'open' - the retail
    // exe's glue removed it after the fill; the game hides it here.
    player.setClipVisible( std::string( kControlsPcPath ) + "/loading_anim",
                           false );
}

// Per tick while the Controls page is up. Three jobs:
//  1. the fill (one tick after 'bind:open', when the page rests on its
//     'loading' frame): write the rows and play the page's 'open' - the
//     loading spinner gives way to the list and the page activates itself
//     (its stop frame sets _root.Input and its _buttons);
//  2. the scroll: the movie's own Move wraps line_view within the 9 view
//     lines, so a wrap at the bottom / top edge is the scroll signal - the
//     window shifts one row and the lines are re-fed (the highlight is put
//     back with the page's own PlayAnimSelectionOut / In);
//  3. the bind dialog: the page rests at its '_stop' frame (43) while it
//     waits for the input to bind; the capture (GSInput) takes the next raw
//     key / pad button / pad push, the row is rebound live and the page's
//     'binder_out' closes the dialog. Escape / Backspace cancels it.
void PauseMenu::tickControlsPage() {
    if ( bControlsPendingFill ) {
        bControlsPendingFill = false;
        controlsOffset = 0;
        controlsLastLineView = 1;
        fillControlsList();
        // The retail exe's loading-done hook: endLoading() plays the page's
        // 'open' AND clears its isLoading (the gate of the page's own A():
        // Enter on a row never reached the binder while isLoading stayed
        // true).
        if ( !player.invoke( kControlsPcPath, "endLoading", {} ) ) {
            player.gotoLabel( kControlsPcPath, "open", true );
        }
        // The page's open flow stops at its f25 (Stop + InitLoop): the
        // retail exe's InitLoop call starts the row-animation looper from
        // there, whose own callbacks play the lines in and land the page on
        // its 'stop' frame (where the list activates and its key button
        // lives). Our AVM1 stops the page at f25 without starting the
        // looper, so the game drives the same step; the looper's callbacks
        // finish the open themselves.
        player.setVariableNumber( std::string( kControlsPcPath ) + ":line_view",
                                  1 );
        player.gotoLabel( std::string( kControlsPcPath ) + "/looper",
                          "loop_inc", true );
        OTDU_LOG_INFO( "PauseMenu: controls list filled (%d actions)\n",
                       static_cast<int32_t>( std::size( kControlRows ) ) );
        // -logcontrols: the filled page's display list (the instances, their
        // frames and visibility) - the reference for driving the page.
        static const bool bLogControls = CmdLine::HasArg( "-logcontrols" );
        if ( bLogControls ) {
            OTDU_LOG_INFO( "PauseMenu: controls page display list:\n" );
            if ( FlashPlayer::Clip *pMenu =
                     player.findClip( "/options/controls_menu" ) ) {
                DumpDisplayList( *pMenu, 0, &player.getDefinitions() );
            }
            DumpSubtrees( *player.getRoot(), "/", player.getMovie(),
                          "controls_menu", 0, 8, 40 );
        }
        return;
    }
    FlashPlayer::Clip *pPc = player.findClip( kControlsPcPath );
    if ( pPc == nullptr ) {
        return;
    }
    // -logcontrols: the page's state once its own open flow settles (the
    // 'stop' frame activates the list: _root.Clip, list.Activate, _buttons).
    static const bool bLogControls2 = CmdLine::HasArg( "-logcontrols" );
    static bool bDumpedSettled = false;
    if ( bLogControls2 && !bDumpedSettled && pPc->CurrentFrame == 26 &&
         !pPc->bPlaying ) {
        bDumpedSettled = true;
        OTDU_LOG_INFO(
            "PauseMenu: controls page settled at 'stop': line_view '%s' "
            "nb_lines '%s' fake '%s' Input '%s'\n",
            player.getVariable( std::string( kControlsPcPath ) + ":line_view" ).c_str(),
            player.getVariable( std::string( kControlsPcPath ) + ":nb_lines" ).c_str(),
            player.getVariable( "/:fake" ).c_str(),
            player.getVariable( "/:Input" ).c_str() );
        if ( FlashPlayer::Clip *pLooper =
                 player.findClip( std::string( kControlsPcPath ) + "/looper" ) ) {
            OTDU_LOG_INFO( "PauseMenu: controls looper frame %u playing %d\n",
                           pLooper->CurrentFrame, pLooper->bPlaying ? 1 : 0 );
        }
        if ( FlashPlayer::Clip *pPcDump = player.findClip( kControlsPcPath ) ) {
            OTDU_LOG_INFO( "PauseMenu: controls pc display list (frame %u):\n",
                           pPcDump->CurrentFrame );
            DumpDisplayList( *pPcDump, 1, &player.getDefinitions() );
        }
    } else if ( bLogControls2 && !bDumpedSettled ) {
        static int32_t lastFrame = -1;
        if ( pPc->CurrentFrame != lastFrame ) {
            lastFrame = pPc->CurrentFrame;
            OTDU_LOG_INFO( "PauseMenu: controls page frame %d playing %d\n",
                           pPc->CurrentFrame, pPc->bPlaying ? 1 : 0 );
        }
    }
    const bool bBindWait =
        pPc->CurrentFrame == 43 && !pPc->bPlaying; // the '_stop' of binder_in
    if ( CmdLine::HasArg( "-logcontrols" ) ) {
        static uint32_t iconProbe = 0;
        if ( ++iconProbe % 300 == 0 ) {
            for ( int32_t line = 5; line <= 7; line++ ) {
                const std::string base = std::string( kControlsPcPath ) +
                                         "/line" + std::to_string( line );
                const FlashPlayer::Clip *pIcon = player.findClip(
                    base + "/content/command_bind/icon" );
                const FlashPlayer::Clip *pBind = player.findClip(
                    base + "/content/command_bind" );
                const FlashPlayer::Clip *pLine = player.findClip( base );
                OTDU_LOG_INFO(
                    "PauseMenu: controls icon probe line %d: icon %d/%u/%d "
                    "bind %d/%d line %d/%u\n",
                    line,
                    pIcon != nullptr ? 1 : 0,
                    pIcon != nullptr ? pIcon->CurrentFrame : 0xffff,
                    pIcon != nullptr && pIcon->pInstance != nullptr &&
                            pIcon->pInstance->bVisible
                        ? 1
                        : 0,
                    pBind != nullptr ? 1 : 0,
                    pBind != nullptr && pBind->pInstance != nullptr &&
                            pBind->pInstance->bVisible
                        ? 1
                        : 0,
                    pLine != nullptr ? 1 : 0,
                    pLine != nullptr ? pLine->CurrentFrame : 0xffff );
            }
        }
    }
    if ( CmdLine::HasArg( "-logcontrols" ) ) {
        static int32_t lastLoggedFrame = -1;
        static int32_t lastLoggedView = -1;
        if ( pPc->CurrentFrame != lastLoggedFrame ) {
            lastLoggedFrame = pPc->CurrentFrame;
            OTDU_LOG_INFO( "PauseMenu: controls pc frame -> %d (playing %d)\n",
                           pPc->CurrentFrame, pPc->bPlaying ? 1 : 0 );
        }
        const std::string viewText =
            player.getVariable( std::string( kControlsPcPath ) + ":line_view" );
        const int32_t view = atoi( viewText.c_str() );
        if ( view != lastLoggedView ) {
            lastLoggedView = view;
            OTDU_LOG_INFO( "PauseMenu: controls line_view -> %d\n", view );
        }
    }
    if ( bBindWait ) {
        if ( !bBindCaptureArmed ) {
            gpInput->beginBindCapture();
            bBindCaptureArmed = true;
            // The binder dialog reads its texts from the binder_key clip's
            // control_* variables (its fields bind '../../:control_name'
            // etc.): feed the focused row's.
            const std::string viewText2 =
                player.getVariable( std::string( kControlsPcPath ) + ":line_view" );
            const int32_t view2 = atoi( viewText2.c_str() );
            const int32_t row2 = controlsOffset + view2 - 1;
            if ( view2 >= 1 &&
                 row2 >= 0 && row2 < static_cast<int32_t>( std::size( kControlRows ) ) ) {
                const std::string binderPath =
                    std::string( kControlsPcPath ) + "/binder_key";
                player.setVariable( binderPath + ":control_name",
                                    kControlRows[row2].pName );
                player.setVariable( binderPath + ":control_bind", "" );
                player.setVariable( binderPath + ":control_device", "" );
            }
        }
        GSInput::CapturedBinding cap;
        if ( gpInput->takeCapturedBinding( cap ) ) {
            bBindCaptureArmed = false;
            const std::string lineViewText =
                player.getVariable( std::string( kControlsPcPath ) + ":line_view" );
            const int32_t lineView = atoi( lineViewText.c_str() );
            const int32_t row = controlsOffset + lineView - 1;
            bool bApplied = false;
            if ( lineView >= 1 &&
                 row >= 0 && row < static_cast<int32_t>( std::size( kControlRows ) ) ) {
                // The row shows ONE binding (the retail look): the newly
                // captured input replaces the old one on whichever device it
                // came from.
                if ( cap.Scancode == SDL_SCANCODE_ESCAPE ||
                     cap.Scancode == SDL_SCANCODE_BACKSPACE ) {
                    // the dialog cancelled: no change
                } else if ( cap.Scancode >= 0 ) {
                    gpInput->setKeyboardBinding( kControlRows[row].Action,
                                                 cap.Scancode );
                    gpInput->clearPadBinding( kControlRows[row].Action );
                    bApplied = true;
                } else if ( cap.PadButton >= 0 || cap.PadAxis >= 0 ) {
                    gpInput->setPadBinding( kControlRows[row].Action, cap );
                    gpInput->clearKeyboardBinding( kControlRows[row].Action );
                    bApplied = true;
                }
                OTDU_LOG_INFO(
                    "PauseMenu: controls bind '%s' %s\n",
                    kControlRows[row].pName,
                    bApplied ? "rebound" : "cancelled" );
            }
            fillControlsList();
            player.gotoLabel( kControlsPcPath, "binder_out", true );
        }
        return;
    }
    bBindCaptureArmed = false;
    // The scroll (see the comment above): the movie wraps line_view 9 -> 1
    // on Down at the last view line; with rows below the window that is the
    // scroll-down signal (and 1 -> 9 the scroll-up).
    const std::string lineViewText =
        player.getVariable( std::string( kControlsPcPath ) + ":line_view" );
    if ( lineViewText.empty() || lineViewText == "undefined" ) {
        return;
    }
    const int32_t lineView = atoi( lineViewText.c_str() );
    if ( lineView < 1 || lineView > kControlViewLines ) {
        return;
    }
    const int32_t rowCount = static_cast<int32_t>( std::size( kControlRows ) );
    const int32_t maxOffset = Max( 0, rowCount - kControlViewLines );
    if ( controlsLastLineView >= 1 && lineView != controlsLastLineView ) {
        if ( controlsLastLineView == kControlViewLines && lineView == 1 &&
             controlsOffset < maxOffset ) {
            // The movie wrapped the highlight to the top view line: scroll
            // the data one row under it (the highlight stays where the movie
            // put it - line 1 now shows the next row).
            controlsOffset++;
            fillControlsList();
            OTDU_LOG_INFO( "PauseMenu: controls list scrolled down (rows %d..%d of %d)\n",
                           controlsOffset + 1, controlsOffset + kControlViewLines, rowCount );
        } else if ( controlsLastLineView == 1 && lineView == kControlViewLines &&
                    controlsOffset > 0 ) {
            controlsOffset--;
            fillControlsList();
            OTDU_LOG_INFO( "PauseMenu: controls list scrolled up (rows %d..%d of %d)\n",
                           controlsOffset + 1, controlsOffset + kControlViewLines, rowCount );
        }
    }
    controlsLastLineView = lineView;
}

// LB / RB on an open options page: the previous / next page of the ring
// audio -> video -> gamma. The pages share one template (default / open /
// stop / close), so the switch is a close of the open page and an 'open' of
// the next one - the page's own frame script runs initParameters, fires its
// 'open' and the values / game rows follow. The tab icons of the Audio &
// Video page keep their own highlight (cosmetic).
void PauseMenu::cycleOptionsPage( int32_t direction ) {
    static constexpr struct {
        const char *pClip;
        eRowsPage Page;
    } kPages[] = {
        { "audio", eRowsPage::Audio },
        { "video", eRowsPage::Video },
        { "gamma", eRowsPage::Gamma },
    };
    constexpr int32_t kNumPages = static_cast<int32_t>( std::size( kPages ) );
    int32_t index = 0;
    for ( ; index < kNumPages; index++ ) {
        if ( kPages[index].Page == openPage ) {
            break;
        }
    }
    if ( index >= kNumPages ) {
        return;
    }
    const int32_t next = ( index + direction + kNumPages ) % kNumPages;
    if ( next == index ) {
        return;
    }
    // The next page's 'open' runs its own frame script (initParameters fires
    // the page's 'open'); the visibility of the three sibling pages is set
    // here (the retail tab flow shows one child and hides the others, and
    // its close glue would close the page we just opened).
    player.gotoLabel( std::string( "/options/" ) + kPages[next].pClip, "open", true );
    for ( const auto &page : kPages ) {
        player.setClipVisible( std::string( "/options/" ) + page.pClip,
                               page.Page == kPages[next].Page );
    }
    openPage = kPages[next].Page;
    bOnVideoPage = kPages[next].Page == eRowsPage::Video;
    GetAdvancedGraphics().setVideoPageHint( bOnVideoPage );
    OTDU_LOG_INFO( "PauseMenu: options page -> %s (LB / RB)\n", kPages[next].pClip );
}

void PauseMenu::setClock( int32_t hours, int32_t minutes ) {
    clockOverrideHours_ = Clamp( hours, 0, 23 );
    clockOverrideMinutes_ = Clamp( minutes, 0, 59 );
}

void PauseMenu::refreshClock() {
    // clock/time/disp frame 0 (-logclock): setHour() builds TimeArray from
    // _root.MUnit / MDecade / HUnit / HDecade, sets number<i>.currentValue
    // and calls number<i>.changeNumber() -> 'change', whose frame 15 does
    // num.gotoAndStop( currentValue + 1 ) [DATA]. The four names are retail
    // native->SWF globals (flash_globals.txt: /:HDecade 0xf54bb8, /:HUnit
    // 0xf54bc8, /:MDecade 0xf54bd0, /:MUnit 0xf54be0) [CODE PS3].
    // The time is the PC's clock (world_clock.h): pushed when it changed
    // since the last push, so the digits only roll on a new minute - the
    // -clock flag freezes it for tests.
    static const char *kDisp = "/pause_menu/clock/time/disp";
    static int32_t shownHours = -1, shownMinutes = -1;
    if ( player.findClip( kDisp ) == nullptr ) {
        return;
    }
    const int32_t clockHours = clockOverrideHours_ >= 0 ? clockOverrideHours_ : WorldClockHours();
    const int32_t clockMinutes = clockOverrideHours_ >= 0 ? clockOverrideMinutes_ : WorldClockMinutes();
    if ( clockHours == shownHours && clockMinutes == shownMinutes ) {
        return;
    }
    player.setVariableNumber( "/:HDecade", clockHours / 10 );
    player.setVariableNumber( "/:HUnit", clockHours % 10 );
    player.setVariableNumber( "/:MDecade", clockMinutes / 10 );
    player.setVariableNumber( "/:MUnit", clockMinutes % 10 );
    if ( !player.invoke( kDisp, "setHour", {} ) ) {
        return; // frame 0 actions not run yet: retry next tick
    }
    // Day / night icon [INF: 07:00..19:59 is day; the retail threshold is
    // the time of day system, ENC].
    const bool bDay = WorldClockIsDay();
    player.gotoLabel( "/pause_menu/clock/time/day", bDay ? "on" : "off", true );
    player.gotoLabel( "/pause_menu/clock/time/night", bDay ? "off" : "on",
                      true );
    shownHours = clockHours;
    shownMinutes = clockMinutes;
    bClockShown = true;
    OTDU_LOG_INFO( "PauseMenu: clock %02d:%02d (%s)\n", clockHours,
                   clockMinutes, bDay ? "day" : "night" );
}

void PauseMenu::refreshPlayerPanel() {
    // The card's money / cars / houses (load() pushes them once): refreshed
    // while open, so money earned while driving shows without re-entering
    // the mode. Only on change - a setVariable per frame is wasteful.
    const PlayerProfile &profile = GetPlayerProfile();
    if ( profile.Money != panelMoney ) {
        panelMoney = profile.Money;
        player.setVariable( "/:Money", FormatMoney( profile.Money ) );
    }
    if ( profile.NbCars != panelCars ) {
        panelCars = profile.NbCars;
        player.setVariableNumber( "/:NbCars", profile.NbCars );
    }
    if ( profile.NbHouses != panelHouses ) {
        panelHouses = profile.NbHouses;
        player.setVariableNumber( "/:NbHouses", profile.NbHouses );
    }
}

void PauseMenu::dumpClockDebug() {
    static const char *kDisp = "/pause_menu/clock/time/disp";
    for ( int32_t i = 0; i < 4; i++ ) {
        const std::string path =
            std::string( kDisp ) + "/number" + std::to_string( i );
        FlashPlayer::Clip *pNumber = player.findClip( path );
        OTDU_LOG_INFO( "PauseMenu: clock digit %s found %d currentValue '%s' "
                       "frame %u playing %d\n",
                       path.c_str(), pNumber != nullptr,
                       player.getVariable( path + ":currentValue" ).c_str(),
                       pNumber ? pNumber->CurrentFrame : 0,
                       pNumber ? pNumber->bPlaying : 0 );
        if ( pNumber != nullptr ) {
            DumpDisplayList( *pNumber, 2, &player.getDefinitions() );
        }
    }
    OTDU_LOG_INFO(
        "PauseMenu: clock disp TimeArray '%s' i '%s'\n",
        player.getVariable( std::string( kDisp ) + ":TimeArray" ).c_str(),
        player.getVariable( std::string( kDisp ) + ":i" ).c_str() );
}

void PauseMenu::close() {
    if ( !isLoaded() || !bOpen || bClosing ) {
        return;
    }
    // The movie plays pause_out / pause_out_direct; the world stays frozen
    // for that short animation (isOpen() keeps returning true) and the menu is
    // released when it is over (or after kCloseDuration, if the movie loops).
    bOnVideoPage = false;
    bOnControlsPage = false;
    removeExtraOptionRows();
    SaveProfile( "pause closed" );
    GetAdvancedGraphics().setVideoPageHint( false );
    player.gotoLabel( "/", bInOptions ? "pause_out_direct" : "pause_out",
                      true );
    bInOptions = false;
    player.setVariableBool( "/:IsInOptions", false );
    bClosing = true;
    closeTimer = kCloseDuration;
}

void PauseMenu::tick( float deltaTime ) {
    if ( !isLoaded() || !bOpen ) {
        return;
    }
    player.tick( deltaTime );
    if ( bPendingVideoClose ) {
        bPendingVideoClose = false;
        player.sendKey( 8 );
    }
    refreshClock();
    refreshPlayerPanel();
    {
        static float clockDebugTimer = 0.0f;
        static bool bClockDumped = false;
        static bool bSqDumped = false;
        if ( bClockShown && !bSqDumped && CmdLine::HasArg( "-logpausesq" ) &&
             clockDebugTimer > 2.0f ) {
            bSqDumped = true;
            if ( FlashPlayer::Clip *pMenuSq =
                     player.findClip( "/pause_menu/menu_sq" ) ) {
                OTDU_LOG_INFO( "PauseMenu: menu_sq settled, frame %u:\n",
                               pMenuSq->CurrentFrame );
                DumpDisplayList( *pMenuSq, 1, &player.getDefinitions() );
            }
        }
        if ( bClockShown && !bClockDumped &&
             ( CmdLine::HasArg( "-logclock" ) ||
               CmdLine::HasArg( "-logpausesq" ) ) ) {
            clockDebugTimer += deltaTime;
            if ( clockDebugTimer > 3.0f ) {
                bClockDumped = true;
                dumpClockDebug();
            }
        }
    }
    if ( bClosing ) {
        closeTimer -= deltaTime;
        if ( closeTimer <= 0.0f ) {
            bClosing = false;
            bOpen = false;
        }
        return;
    }
    // -logpause: the state of the movie (the pause screen is the root frame
    // between pause_in and pause_out; the entries are the display list).
    static const bool bLogPause = CmdLine::HasArg( "-logpause" );
    if ( bLogPause ) {
        FlashPlayer::Clip *pRoot = player.getRoot();
        OTDU_LOG_INFO(
            "PauseMenu: root frame %u playing %d, %zu objects, in options %d\n",
            pRoot->CurrentFrame, pRoot->bPlaying ? 1 : 0,
            pRoot->DisplayList.size(), bInOptions ? 1 : 0 );
        // Once, as soon as the menu is in place (pause_in finished).
        static bool bDumped = false;
        if ( !bDumped && !pRoot->bPlaying ) {
            bDumped = true;
            OTDU_LOG_INFO( "PauseMenu: display list of the pause screen:\n" );
            DumpDisplayList( *pRoot, 0, &player.getDefinitions() );
            // The root's user functions: the options screen protocol
            // (LaunchOptions / CloseAllOptions / BackFromOptions / ValidMenu /
            // SetHelp...) lives in their bodies, so disassemble them.
            const std::vector<uint8_t> &data =
                player.getMovie()->getVMF().getData();
            for ( const auto &var : pRoot->Variables ) {
                if ( var.second.Type != FlashPlayer::Value::eType::Function ) {
                    continue;
                }
                uint32_t bodyOffset = 0, bodyLength = 0;
                if ( !player.getFunctionBody( var.second.FunctionIndex,
                                              bodyOffset, bodyLength ) ) {
                    continue;
                }
                OTDU_LOG_INFO( "PauseMenu: root function '%s' (%u bytes)\n",
                               var.first.c_str(), bodyLength );
                DumpActions(
                    data, bodyOffset, bodyLength, 0,
                    player.getFunctionConstants( var.second.FunctionIndex ) );
            }
            // The options screen sprite ('options', placed on the root by the
            // movie) and its subtree: labels, placements, actions.
            OTDU_LOG_INFO( "PauseMenu: options subtree timelines:\n" );
            DumpSubtrees( *pRoot, "/", player.getMovie(), "opt", 0, 6, 40 );
            // -logclock: the clock sprite (top left: the in-game time).
            if ( CmdLine::HasArg( "-logclock" ) ) {
                OTDU_LOG_INFO( "PauseMenu: clock subtree timelines:\n" );
                DumpSubtrees( *pRoot, "/", player.getMovie(), "clock", 0, 6,
                              80 );
            }
            // The entry state machine (labels + actions of every square: the
            // squares carry their own scripts — the FSCommand each entry fires,
            // e.g. phmo:phin for 'photo' — so all of them are dumped).
            if ( FlashPlayer::Clip *pMenuSq =
                     player.findClip( "/pause_menu/menu_sq" ) ) {
                for ( const auto &sq : pMenuSq->DisplayList ) {
                    if ( sq.second->Name.compare( 0, 2, "sq" ) != 0 ||
                         sq.second->pClip == nullptr ) {
                        continue;
                    }
                    // Full timeline: the state machine is 208 frames — every
                    // placement / stop matters to drive it like the movie's
                    // ActionLoop does.
                    const std::string base =
                        "/pause_menu.menu_sq." + sq.second->Name;
                    DumpClipTimeline( *sq.second->pClip, base.c_str(),
                                      player.getMovie(), 250 );
                    // The icon holder (icLoaded = the attached specificIcons
                    // symbol: one frame per entry icon).
                    for ( const auto &ic : sq.second->pClip->DisplayList ) {
                        if ( ic.second->Name != "ic" ||
                             ic.second->pClip == nullptr ) {
                            continue;
                        }
                        for ( const auto &child :
                              ic.second->pClip->DisplayList ) {
                            if ( child.second->pClip != nullptr ) {
                                const std::string path =
                                    base + ".ic." + child.second->Name;
                                DumpClipTimeline( *child.second->pClip,
                                                  path.c_str(),
                                                  player.getMovie(), 250 );
                            }
                        }
                    }
                }
            }
        }
    }
    // -logpause: the map panel's island badge (the info card's island art).
    // The clip's frame labels are the per-island / per-club-level art, and the
    // badge showed HAWAII while the world is Ibiza — dump the labels so the
    // right one can be picked.
    if ( bLogPause ) {
        static bool bBadgeDumped = false;
        if ( !bBadgeDumped ) {
            bBadgeDumped = true;
            if ( FlashPlayer::Clip *pIsland =
                     player.findClip( "/pause_menu/data_extd/map/island" ) ) {
                OTDU_LOG_INFO( "PauseMenu: island badge clip, frame %u:\n",
                               pIsland->CurrentFrame );
                DumpClipTimeline( *pIsland, "/pause_menu/data_extd/map/island",
                                  player.getMovie(), 250 );
                for ( const auto &child : pIsland->DisplayList ) {
                    OTDU_LOG_INFO( "  island child '%s' clip %d frame %u\n",
                                   child.second->Name.c_str(),
                                   child.second->pClip != nullptr ? 1 : 0,
                                   child.second->pClip != nullptr
                                       ? child.second->pClip->CurrentFrame
                                       : 0 );
                    if ( child.second->pClip != nullptr ) {
                        const std::string path =
                            "/pause_menu/data_extd/map/island/" +
                            child.second->Name;
                        DumpClipTimeline( *child.second->pClip, path.c_str(),
                                          player.getMovie(), 60 );
                    }
                }
            }
        }
    }
    // -logpause: the options screen is attached at run time (the root's
    // LaunchOptions script) — dump its subtree (labels, placements, actions)
    // once it is up, that is the protocol reference for driving it.
    static bool bDumpedOptions = false;
    if ( bLogPause && bInOptions && !bDumpedOptions ) {
        bDumpedOptions = true;
        OTDU_LOG_INFO( "PauseMenu: options screen display list:\n" );
        DumpDisplayList( *player.getRoot(), 0, &player.getDefinitions() );
        OTDU_LOG_INFO( "PauseMenu: options subtree timelines:\n" );
        DumpSubtrees( *player.getRoot(), "/", player.getMovie(), "opt", 0, 8,
                      24 );
    }
    // Movie-driven menus: once the movie's own square menu is up (its
    // openMenuH ran: menu_sq.isMenuH), the movie owns the squares, the
    // selection and the options pages; the game only routes the keys / pad.
    // (Before the AVM1 fixes of sessions 12-13 — new Array, Array.push,
    // tellTarget calls — those scripts died early and the game drove the
    // squares itself: the code below is that fallback.)
    if ( !bMovieDriven &&
         player.getVariable( "/pause_menu/menu_sq:isMenuH" ) == "true" ) {
        bMovieDriven = true;
        OTDU_LOG_INFO(
            "PauseMenu: the movie drives the menu (menu_sq.isMenuH)\n" );
    }
    if ( bMovieDriven ) {
        // The tab row (and its tooltip) sits on the top edge of the info card
        // in our render; retail leaves a gap of about 10 px (OBS capture,
        // docs/images/retail-pause-tab-progress.webp) [DATA]. The movie's own y
        // for menu_sq is 222.8; the card's missing objective header is not
        // explained, so only the gap is matched here [INF]. The lift is applied
        // once the row has settled; the matrix is not locked, so whenever the
        // movie moves the row itself (options screen, pause_out) the lift is
        // dropped and re-applied after it settles at its own position again.
        FlashPlayer::Clip *pMenuSq = player.findClip( "/pause_menu/menu_sq" );
        if ( pMenuSq != nullptr && pMenuSq->pInstance != nullptr ) {
            float &ty = pMenuSq->pInstance->Matrix.TY;
            const float liftTwips = kTabRowLift * 20.0f;
            if ( bTabRowLifted ) {
                if ( std::abs( ty - ( fTabRowBaseTY - liftTwips ) ) > 1.0f ) {
                    bTabRowLifted = false;
                }
            } else if ( !bInOptions && !pMenuSq->bPlaying ) {
                if ( fTabRowBaseTY == 0.0f ) {
                    fTabRowBaseTY = ty;
                }
                if ( std::abs( ty - fTabRowBaseTY ) <= 1.0f ) {
                    ty -= liftTwips;
                    bTabRowLifted = true;
                }
            }
        }
        // -logoptpage: each options page the movie settles on (its sprites are
        // attached when the page opens: the parameter tables live there).
        static const bool bLogPage = CmdLine::HasArg( "-logoptpage" );
        if ( bLogPage && bInOptions ) {
            static uint32_t lastDumped = 0xffffffffu;
            FlashPlayer::Clip *pOptions = player.findClip( "/options" );
            if ( pOptions != nullptr && !pOptions->bPlaying &&
                 pOptions->CurrentFrame != lastDumped ) {
                lastDumped = pOptions->CurrentFrame;
                OTDU_LOG_INFO( "PauseMenu: options page settled on frame %u\n",
                               lastDumped );
                DumpDisplayList( *pOptions, 0, &player.getDefinitions() );
                DumpSubtrees( *pOptions, "/options", player.getMovie(), nullptr,
                              0, 4, 40 );
            }
        }
        // The page's own initParameters runs its setParameters over the
        // retail rows only after the 'open' FSCommand returned (the game's
        // rows would land before them in setParam): append the game's rows
        // one tick later, when the retail table is complete.
        if ( bInOptions && pendingRowsPage != eRowsPage::None ) {
            extendOptionsRows();
            pendingRowsPage = eRowsPage::None;
        }
        // The Driving Controls page: the row fill, the window scroll and the
        // bind dialog (see tickControlsPage).
        if ( bInOptions && bOnControlsPage ) {
            tickControlsPage();
        }
        tickMovieInput( deltaTime );
        return;
    }
    // The entry squares are driven as soon as the pause_in animation settled
    // (the root stops at pause_stop; driving earlier gets overwritten by the
    // animation's own matrix / alpha work).
    if ( !bEntriesShown && !player.getRoot()->bPlaying ) {
        refreshEntries();
    }
    // The options screen: ESC closes the whole menu, BACK returns to the entry
    // column (the movie's BackFromOptions plays 'close' + fires 'opou'). The
    // pages / widgets of the screen are the movie's own (see the -logpause
    // dump of /options: page labels + their square scripts).
    if ( bInOptions ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
            closeOptions();
            return;
        }
        // -logpause: the options page, once its 'open' animation settled.
        if ( bLogPause && !bOptionsDumped ) {
            FlashPlayer::Clip *pOptions = player.findClip( "/options" );
            if ( pOptions != nullptr && !pOptions->bPlaying ) {
                bOptionsDumped = true;
                OTDU_LOG_INFO(
                    "PauseMenu: options page frame %u (%zu objects):\n",
                    pOptions->CurrentFrame, pOptions->DisplayList.size() );
                DumpDisplayList( *pOptions, 0, &player.getDefinitions() );
            }
        }
        return;
    }
    // Entries: keyboard / pad navigation (the row reads left / right on a
    // pad; up / down kept for the column-era bindings), mouse picks a square.
    // The movie's own root.Input(code) glue is not usable (the function is
    // not defined by the movie scripts and setSquareMenu returns early), so
    // the game owns the selection and sends the state changes the movie's
    // ActionLoop would. Locked entries are skipped.
    const int32_t previous = selected;
    const eNav nav = takeStickNav( deltaTime );
    if ( gpInput->wasPressed( eInputAction::IA_MenuUp ) ||
         gpInput->wasPressed( eInputAction::IA_MenuLeft ) || nav == eNav::Up ||
         nav == eNav::Left ) {
        do {
            selected = ( selected + kEntryCount - 1 ) % kEntryCount;
        } while ( !bEntryEnabled[selected] && selected != previous );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuDown ) ||
         gpInput->wasPressed( eInputAction::IA_MenuRight ) ||
         nav == eNav::Down || nav == eNav::Right ) {
        do {
            selected = ( selected + 1 ) % kEntryCount;
        } while ( !bEntryEnabled[selected] && selected != previous );
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        validateSelected();
    }
    if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
        close();
    }
    // Mouse: the square under the cursor is selected, a click validates.
    // Square slots are at a fixed stage position (the column of menu_sq);
    // world positions of the live instances give the exact spots. The cursor
    // goes window px -> canvas -> stage px through the UI layout mapping
    // (exact at every window aspect; at 1280x720 this is the old
    // y - kStageTopOffset band math).
    const float mouseX = gpInput->getMouseX();
    const float mouseY = gpInput->getMouseY();
    float stageMouseX = mouseX, stageMouseY = mouseY;
    if ( gpRender != nullptr ) {
        const float backbufferWidth = static_cast<float>( gpRender->getDevice().getBackbufferWidth() );
        const float backbufferHeight = static_cast<float>( gpRender->getDevice().getBackbufferHeight() );
        float cx0 = 0.0f, cy0 = 0.0f, cw = 1.0f, ch = 1.0f;
        GetUiCanvasRect( backbufferWidth, backbufferHeight, cx0, cy0, cw, ch );
        const float canvasX = ( ( mouseX / backbufferWidth - cx0 ) / Max( cw, 1e-6f ) ) * 1280.0f;
        const float canvasY = ( ( mouseY / backbufferHeight - cy0 ) / Max( ch, 1e-6f ) ) * 720.0f;
        FlashCanvasToStage( canvasX, canvasY, 1280.0f, 960.0f, stageMouseX, stageMouseY );
    }
    for ( int32_t n = 0; n < kEntryCount; n++ ) {
        float x = 0.0f, y = 0.0f;
        if ( !player.getClipStagePosition(
                 "/pause_menu/menu_sq/sq" + std::to_string( n ), x, y ) ) {
            continue;
        }
        const float dy = stageMouseY - y;
        if ( stageMouseX < x - 64.0f || stageMouseX > x + 64.0f || dy < -48.0f ||
             dy > 48.0f ) {
            continue;
        }
        if ( bEntryEnabled[n] ) {
            selected = n;
            if ( gpInput->wasMouseClicked() ) {
                validateSelected();
            }
        }
        break;
    }
    if ( selected != previous ) {
        refreshEntries();
    }
    // The squares' state animations write their own matrix back: the row
    // layout is re-imposed every frame while the entry column is up.
    if ( bEntriesShown ) {
        layoutEntries();
        if ( bLogPause ) {
            static bool bLoggedRow = false;
            if ( !bLoggedRow ) {
                bLoggedRow = true;
                for ( int32_t n = 0; n < kEntryCount; n++ ) {
                    float x = 0.0f, y = 0.0f;
                    if ( player.getClipStagePosition( "/pause_menu/menu_sq/sq" +
                                                          std::to_string( n ),
                                                      x, y ) ) {
                        OTDU_LOG_INFO( "PauseMenu: entry %d stage (%.1f, %.1f) "
                                       "window y %.1f\n",
                                       n, x, y, y - kStageTopOffset );
                    }
                }
                // The mystery blue box above the row: everything whose world
                // position lands in that region (stage px).
                OTDU_LOG_INFO( "PauseMenu: instances in the box region:\n" );
                DumpWorldRegion( *player.getRoot(), FlashMatrix(), "", 300.0f,
                                 650.0f, 100.0f, 220.0f );
            }
        }
    }
}

void PauseMenu::layoutEntries() {
    // Row of icons across the top (the console frontend layout): the squares
    // are authored as a column, so their position is imposed. Units are the
    // menu_sq local pixels (the clip is scaled 0.85 on stage).
    //   menu_sq stage origin: pause_menu (0, 120) + menu_sq (640, 222.8)
    //   pitch: the authored 96.3 px keeps the icon spacing
    //   y: the icon centres land ~55 px below the top of the 16:9 band
    constexpr float kRowPitch = 96.3f;
    constexpr float kRowY = -197.5f;
    for ( int32_t n = 0; n < kEntryCount; n++ ) {
        const std::string path = "/pause_menu/menu_sq/sq" + std::to_string( n );
        player.setVariableNumber(
            path + ":_x", ( n - ( kEntryCount - 1 ) * 0.5f ) * kRowPitch );
        player.setVariableNumber( path + ":_y", kRowY );
    }
}

void PauseMenu::refreshEntries() {
    bEntriesShown = true;
    // Island badges of the map (authored art per island: the default frame
    // is Hawaii, this build streams Ibiza).
    player.gotoLabel( "/pause_menu/data_extd/map/island/i1", pIslandBadge1,
                      false );
    player.gotoLabel( "/pause_menu/data_extd/map/island/i2", pIslandBadge2,
                      false );
    for ( int32_t n = 0; n < kSquareCount; n++ ) {
        const std::string sqPath =
            "/pause_menu/menu_sq/sq" + std::to_string( n );
        if ( n >= kEntryCount ) {
            // Parked: the movie's 'default' state keeps the square outline
            // visible, so unused squares are hidden outright.
            player.setClipVisible( sqPath, false );
            continue;
        }
        player.setClipVisible( sqPath, true );
        const bool bLocked = !bEntryEnabled[n];
        const char *pState = n == selected ? ( bLocked ? "on_lock" : "on" )
                                           : ( bLocked ? "open_lock" : "open" );
        player.gotoLabel( sqPath, pState, true );
        player.gotoLabel( sqPath + "/ic/icLoaded", kEntries[n].pIcon, false );
        // Centre the icon art (see the icX / icY note on MenuEntry). Writing
        // the transform also resets whatever offset the movie's own scripts
        // left on icLoaded — that stale offset is what put the icons at the
        // square corners. Values are px (setProperty scales by 20 into
        // twips).
        player.setVariableNumber( sqPath + "/ic/icLoaded:_x", kEntries[n].icX );
        player.setVariableNumber( sqPath + "/ic/icLoaded:_y", kEntries[n].icY );
        // Retail shows the tooltip bubble on the selected square only; the
        // movie drives the others' ttip timelines through its own scripts
        // (not run here). Hidden + parked off-screen.
        player.setVariableNumber( sqPath + "/ttip:_x",
                                  n == selected ? 70.0f : 2000.0f );
        player.setVariableNumber( sqPath + "/ttip:_y",
                                  n == selected ? -21.0f : 0.0f );
        player.setClipVisible( sqPath + "/ttip", n == selected );
    }
    // The tooltip label of the selected square (a text field with the
    // relative variable 'txt'; the movie scripts also use /:txt).
    const std::string label =
        LocStr( kEntries[selected].LocId, kEntries[selected].pFallback );
    player.setVariable(
        "/pause_menu/menu_sq/sq" + std::to_string( selected ) + ":txt", label );
    player.setVariable( "/:txt", label );
    layoutEntries();
}

void PauseMenu::validateSelected() {
    if ( !bEntryEnabled[selected] ) {
        // Locked entry (Lock* state of the movie): validate does nothing.
        return;
    }
    const MenuEntry &entry = kEntries[selected];
    player.gotoLabel( "/pause_menu/menu_sq/sq" + std::to_string( selected ),
                      "valid", true );
    if ( entry.Result == eResult::Options ) {
        // The options screen is an internal page of the same pause menu: the
        // menu stays open (the world stays frozen) and no result reaches the
        // game mode. LaunchOptions does the same three steps; the movie's own
        // glue is not usable, so the game drives them.
        openOptions();
        return;
    }
    result = entry.Result;
}

void PauseMenu::openOptions() {
    bInOptions = true;
    bOptionsDumped = false;
    player.setVariableBool( "/:IsInOptions", true );
    // _root.gotoAndStop('pause_stop') + _root.options.gotoAndPlay('open')
    // (LaunchOptions, -logpause disassembly).
    player.gotoLabel( "/", "pause_stop", false );
    if ( !player.gotoLabel( "/options", "open", true ) ) {
        // No 'options' clip (a different MAIN.vmf): let the movie's own
        // function try (it also fires the 'opin' FSCommand).
        OTDU_LOG_WARN( "PauseMenu: '/options' sprite not found: LaunchOptions "
                       "fallback\n" );
        player.invoke( "/", "LaunchOptions", {} );
    }
    OTDU_LOG_INFO( "PauseMenu: options screen opened\n" );
}

void PauseMenu::closeOptions() {
    // BackFromOptions / CloseAllOptions: the sprite plays 'close' and fires
    // the 'opou' FSCommand; the entry column is restored.
    removeExtraOptionRows();
    bOnControlsPage = false;
    bBindCaptureArmed = false;
    player.gotoLabel( "/options", "close", true );
    player.setVariableBool( "/:IsInOptions", false );
    bInOptions = false;
    bOptionsDumped = false;
    refreshEntries();
    OTDU_LOG_INFO( "PauseMenu: options screen closed\n" );
}

void PauseMenu::draw( Renderer2D &renderer ) {
    if ( isLoaded() && bOpen ) {
        player.draw( renderer );
    }
}

PauseMenu::eResult PauseMenu::takeResult() {
    const eResult value = result;
    result = eResult::None;
    return value;
}

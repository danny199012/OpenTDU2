#include "shared.h"
#include "gm_base.h"

namespace {
    constexpr const char *kGameModeNames[] = {
        "NONE",     "LOGIN",       "VIDEOBUMPER",  "AVATAR_CHOICE",
        "FREERIDE", "CARSHOWCASE", "CINVIEWER",    "TESTCAR",
        "FIRST_WM", "CASINO",      "ASSET_VIEWER", "WORLD_VIEWER",
        "CINE",     "MISSION",     "SHOP",         "FREE_DRIVE",
        "UI_VIEWER",
    };
    static_assert( sizeof( kGameModeNames ) / sizeof( kGameModeNames[0] ) ==
                       static_cast<size_t>( eGameMode::GM_Count ),
                   "Game mode names mismatch" );
} // namespace

const char *GetGameModeName( eGameMode gameMode ) {
    const int32_t index = static_cast<int32_t>( gameMode );
    if ( index < 0 || index >= static_cast<int32_t>( eGameMode::GM_Count ) ) {
        return "UNKNOWN";
    }
    return kGameModeNames[index];
}

eGameMode GetGameModeFromName( const char *pName ) {
    // The command line name may be written with or without the underscores:
    // launch/10_WorldViewer.bat passes "worldviewer" for "WORLD_VIEWER", which
    // matched nothing and silently dropped the run into LOGIN. Compare on the
    // alphanumerics only, case-insensitively.
    std::string name;
    for ( const char *p = pName; p != nullptr && *p != '\0'; p++ ) {
        if ( *p != '_' && *p != '-' && *p != ' ' ) {
            name += static_cast<char>(
                toupper( static_cast<unsigned char>( *p ) ) );
        }
    }
    for ( int32_t i = 0; i < static_cast<int32_t>( eGameMode::GM_Count );
          i++ ) {
        std::string candidate;
        for ( const char *p = kGameModeNames[i]; *p != '\0'; p++ ) {
            if ( *p != '_' && *p != '-' && *p != ' ' ) {
                candidate += *p;
            }
        }
        if ( name == candidate ) {
            return static_cast<eGameMode>( i );
        }
    }
    if ( name == "INTRO" ) {
        return eGameMode::GM_AvatarChoice;
    }
    return eGameMode::GM_None;
}

#include "shared.h"
#include "gm_freeride.h"

#include "audio/gs_audio.h"
#include "core/crypto/cpr.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "game/casino/casino_trip.h"
#include "game/cinematic/gm_cin_viewer.h"
#include "game/command_resolver.h"
#include "game/freeride/car_wash.h"
#include "game/freeride/cruising_zones.h"
#include "game/game_options.h"
#include "game/licenses.h"
#include "game/house_shop.h"
#include "game/player_profile.h"
#include "game/skill_levels.h"
#include "game/units.h"
#include "game/spots/mod_props.h"
#include "game/freeride/fuel.h"
#include "game/freeride/fuel_menu.h"
#include "game/freeride/sticker_bank.h"
#include "game/world_clock.h"
#include "input/gs_input.h"
#include "physics/havok_collision.h"
#include "render/eden/eden_headlights.h"
#include "render/gs_render.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"
#include "world/eden_world.h"

#include "imgui.h"
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>
#include "physics/vehicle_collision.h"

namespace {
// Story step 2 timer (DAT_00ede78c) [CODE].
constexpr float kFirstFreerideTime = 90.0f;
// C_FADE_FREERIDE ("fade_blanc"): fade to white [INF duration].
constexpr float kFadeDuration = 2.0f;
// Out of the C_MEETING_TESS white flash into the drive [INF: duration].
constexpr float kDreamWhiteIn = 1.5f;
// The Solar Crown, destination of story step 3 ("take Tess to the shoot"): the
// club house villa on the west coast, the ECH_IA spot at (9874.5, 10.95, 14885.0)
// ("CLUB HOUSE" sign at its gate; the owner's 360 recomp grabs: the walled villa
// on the beach headland, its gate on the crazy paving) [DATA: spots.bin + the
// owner's grabs]. The target is that record's second point, the road outside the
// gate (the hotel record's second point is likewise its entrance) [DATA].
const Vector3f kSolarCrownTarget( 9938.134f, 8.95f, 14834.809f );
// Parked pose of the car while C_FADE_FREERIDE plays, house frame [INF]:
// beside the WOMAN entity placement (1.429, 0, 0, facing +z, sitting on the
// ground: the anim pelvis rests 0.08 above the actor origin), heading east
// (+90 deg) so the driver side (+x -> -z) faces her, the door plane ~0.55 m
// in front (her reaching arm / legs rest against the flank). The white fade
// hides the teleport; at the end the freeride resumes from here.
const Vector3f kWakeupParkLocal( 1.79f, 0.0f, 1.50f );
constexpr float kWakeupParkYaw = 1.5707964f;
// BACK_ON_TRACK hold: the reset only happens after the button was held (the
// HUD element fills while it is) [INF duration].
constexpr float kBackOnTrackHoldTime = 1.0f;
// After a reset the next press is refused for this long - the badge shows its
// red X (ic 'lock'). [INF: the owner saw the X when pressing again straight
// after a successful return; the retail timer is in the encrypted driver, 5 s
// chosen to cover "straight away" without blocking a genuine second rescue.]
constexpr float kBackOnTrackCooldown = 5.0f;
// The traffic violation latch: a collision stays reported while the two cars
// touch (the impulse separates them over several frames), so the raw crash
// flag fired one violation per frame and a single bump filled the awareness
// meter - the owner's "one crash and they chase you". The contact has to be
// clear for this long before the next crash counts (a bouncing car does not
// stack hits) [INF].
constexpr float kCrashClearGrace = 1.0f;
// The awareness band that warns "too many traffic violations" (the tailing
// meter's red state starts at 0.67, freeride_hud's setChaseHud) [INF].
constexpr float kViolationWarnLevel = 0.67f;
// The arrest hold: the police pull-over cutscene (the cops drive up beside the
// stopped car before the sequence closes) [INF: the retail hold is longer than
// the old 3 s, which left no room for the cops to arrive].
constexpr float kArrestHoldSeconds = 5.0f;
// Radio panel / accessories wheel auto-hide (FUN_00929150: the
// RadioAutoHideDuration / AccAutoHideDuration tunables, their defaults at
// 0x00f75f14 / 0x00f75f18 [CODE]; no override found in the unpacked data) and
// the volume step of FUN_00929660 / FUN_009295c0 (0.1, clamped to [0, 1]).
constexpr float kRadioAutoHideDuration = 3.0f;
constexpr float kAccAutoHideDuration = 4.0f;
constexpr float kRadioVolumeStep = 0.1f;

Quaternionf Conjugate( const Quaternionf &q ) { return Quaternionf( -q.x, -q.y, -q.z, q.w ); }

// The spots.bin record of a full spot name ("EPH_L3DI_5067I"): its second
// point (+0x28) and heading2 (+0x38) - the road-side entrance point of the
// property (the Solar Crown target uses the same field) [DATA:
// pool_interior_spec.md 4.1].
bool FindSpotEntranceById( const std::string &island, uint64_t id, Vector3f &outPosition, float &outHeading );

bool FindSpotEntrance( const std::string &island, const char *pSpotName, Vector3f &outPosition, float &outHeading ) {
    return FindSpotEntranceById( island, GetIdentifier64bit( pSpotName ), outPosition, outHeading );
}

// The same record looked up by its id (the spot the player walked into keeps
// its spots.bin id, GMFreeRide::interiorSpotId). A record whose second point
// is unset (0, 0, 0: the casino-floor shops) has no road-side entrance.
bool FindSpotEntranceById( const std::string &island, uint64_t id, Vector3f &outPosition, float &outHeading ) {
    std::vector<uint8_t> data;
    if ( gpFile == nullptr ||
         !gpFile->loadFile( ( gpFile->getBankFolder() + "islands/" + island + "/level/spots.bin" ).c_str(), data ) ||
         data.size() < 0x20 || memcmp( data.data() + 0x10, "SPTA", 4 ) != 0 ) {
        return false;
    }
    uint32_t size = 0;
    memcpy( &size, data.data() + 0x14, 4 );
    for ( size_t offset = 0x20; offset + 0x110 <= data.size() && offset < 0x20 + size; offset += 0x110 ) {
        uint64_t recordId = 0;
        memcpy( &recordId, data.data() + offset, 8 );
        if ( recordId == id ) {
            memcpy( &outPosition, data.data() + offset + 0x28, 12 );
            memcpy( &outHeading, data.data() + offset + 0x38, 4 );
            return outPosition.x != 0.0f || outPosition.z != 0.0f;
        }
    }
    return false;
}

// The story's own house grant (PROGRESS.md "Story steps 3-13"): step 10 "Home
// sweet home: Caravan" hands the player the caravan as the home house, so a
// normal playthrough reaches it without the realtor. The step numbers are
// FUN_00b10da0's profile+0x58 values [CODE]. Idempotent and cheap: the grant
// only touches the profile when the house is not owned yet or the home moves,
// so calling it on every enter covers a save advanced by a cheat or by an
// older build.
// True when the grant changed the profile (the caller saves it then: the story
// grant must survive the session, not only live in memory).
bool ApplyStoryGrants() {
    PlayerProfile &profile = GetPlayerProfile();
    if ( profile.StoryStep >= 10 && GrantHouse( "EPH_L0C_1254I", true ) ) {
        SaveProfile( "story step 10: the caravan as home" );
        return true;
    }
    return false;
}

bool gSuppressLevelUp = false; // -testlevelup: the forced popup must not be replaced by the real one

// The skills as the level-up popup gauges read them (the numbers of the pause
// Game Progress pages: pause_menu.cpp, [INF] maxima from the owner's captures).
FreeRideHud::ProgressSnapshot MakeProgressSnapshot() {
    static const int32_t kMaxLevel[4] = { 15, 18, 15, 15 };
    static const int32_t kMaxPoints[4] = { 10000, 14500, 10000, 10000 };
    FreeRideHud::ProgressSnapshot snapshot;
    int32_t globalLevel = 0;
    for ( int32_t i = 0; i < 4; i++ ) {
        const std::vector<SkillLevel> &levels = GetSkillLevels( i );
        const int32_t points = GetPlayerProfile().Points[i];
        const int32_t at = Max( 0, FindSkillLevelIndex( levels, points ) );
        const int32_t level = levels.empty() ? 0 : levels[at].Level;
        snapshot.LevelCount[i] = std::to_string( level ) + "/" + std::to_string( kMaxLevel[i] );
        snapshot.Title[i] = levels.empty() ? "" : levels[at].Title;
        snapshot.Percent[i] = Min( 100, 100 * points / kMaxPoints[i] );
        snapshot.NextLevelIn[i] = static_cast<size_t>( at ) + 1 < levels.size() ? levels[at + 1].Points - points : 0;
        globalLevel += level;
    }
    snapshot.GlobalLevel = globalLevel;
    snapshot.GlobalLevelCount = std::to_string( globalLevel ) + "/73";
    for ( const SkillLevel &level : GetGlobalLevels() ) {
        if ( level.Level <= globalLevel ) {
            snapshot.GlobalTitle = level.Title;
        }
    }
    return snapshot;
}

// Rotation part of a rigid matrix -> quaternion.
Quaternionf QuaternionFromMatrix( const Matrix4x4f &m ) {
    const float m00 = m.m[0][0], m11 = m.m[1][1], m22 = m.m[2][2];
    const float trace = m00 + m11 + m22;
    Quaternionf q;
    if ( trace > 0.0f ) {
        const float s = std::sqrt( trace + 1.0f ) * 2.0f;
        q = Quaternionf( ( m.m[1][2] - m.m[2][1] ) / s, ( m.m[2][0] - m.m[0][2] ) / s, ( m.m[0][1] - m.m[1][0] ) / s, 0.25f * s );
    } else if ( m00 > m11 && m00 > m22 ) {
        const float s = std::sqrt( 1.0f + m00 - m11 - m22 ) * 2.0f;
        q = Quaternionf( 0.25f * s, ( m.m[1][0] + m.m[0][1] ) / s, ( m.m[2][0] + m.m[0][2] ) / s, ( m.m[1][2] - m.m[2][1] ) / s );
    } else if ( m11 > m22 ) {
        const float s = std::sqrt( 1.0f + m11 - m00 - m22 ) * 2.0f;
        q = Quaternionf( ( m.m[1][0] + m.m[0][1] ) / s, 0.25f * s, ( m.m[2][1] + m.m[1][2] ) / s, ( m.m[2][0] - m.m[0][2] ) / s );
    } else {
        const float s = std::sqrt( 1.0f + m22 - m00 - m11 ) * 2.0f;
        q = Quaternionf( ( m.m[2][0] + m.m[0][2] ) / s, ( m.m[2][1] + m.m[1][2] ) / s, 0.25f * s, ( m.m[0][1] - m.m[1][0] ) / s );
    }
    return q;
}

Matrix4x4f WithoutTranslation( Matrix4x4f m ) {
    m.m[3][0] = m.m[3][1] = m.m[3][2] = 0.0f;
    return m;
}

int32_t SectorKey( const Vector3f &position ) {
    const int32_t row = static_cast<int32_t>( std::floor( position.z / 768.0f ) );
    const float offset = ( row & 1 ) ? 384.0f : 0.0f;
    const int32_t column = static_cast<int32_t>( std::floor( ( position.x - offset ) / 768.0f ) );
    return row * 1024 + column;
}

// The rims the profile bought for a car (defined with the other profile
// helpers below; enter() needs it before them).
void ApplyProfileRims( CinematicCar::Desc &desc );
} // namespace

GMFreeRide::GMFreeRide() : GameMode( eGameMode::GM_FreeRide ) {}

GMFreeRide::~GMFreeRide() { exit(); }

bool GMFreeRide::surfaceHeight( float x, float z, float y, float &outHeight ) const {
    VehicleWorldCollision::Hit hit;
    if ( collision.raycast( Vector3f( x, y, z ), Vector3f( 0.0f, -1.0f, 0.0f ), 60.0f, hit ) ) {
        outHeight = hit.Position.y;
        return true;
    }
    return false;
}

// -intempery <n>: resolve the picked preset with intempery n's block over its
// base (the rain blocks the base map does not hold). Called at every pick
// point (the load pick and the cheats panel's weather switch) so a forced
// rain/storm stays forced across preset changes.
void GMFreeRide::applyForcedIntempery() {
    static const int32_t forced = CmdLine::HasArg( "-intempery" ) ? CmdLine::GetArgInt( "-intempery", -1 ) : -1;
    if ( pWeather == nullptr || forced < 0 ) {
        return;
    }
    intemperyWeather = *pWeather;
    intemperyWeather.ActiveIntempery =
        forced < static_cast<int32_t>( intemperyWeather.Intemperies.size() ) ? forced : -1;
    pWeather = &intemperyWeather;
    // Which blocks of this preset carry rain (the flag's index comes from the
    // file, and the files differ per island) - one line, once per pick.
    std::string rainy;
    for ( size_t i = 0; i < intemperyWeather.Intemperies.size(); i++ ) {
        auto it = intemperyWeather.Intemperies[i].find( "rain_alpha" );
        if ( ( it != intemperyWeather.Intemperies[i].end() && it->second.x > 0.001f ) ||
             intemperyWeather.Intemperies[i].count( "rain_ground_global_intensity" ) ) {
            rainy += ( rainy.empty() ? "" : "," ) + std::to_string( i );
        }
    }
    OTDU_LOG_INFO( "FREERIDE: weather '%s' intempery %d of %zu (rain blocks: %s) rain_alpha %.3f"
                   " rain_ground_global_intensity %.3f droplets max %d\n",
                   intemperyWeather.Name.c_str(), intemperyWeather.ActiveIntempery,
                   intemperyWeather.Intemperies.size(), rainy.empty() ? "none" : rainy.c_str(),
                   intemperyWeather.getFloat( "rain_alpha", 0.0f ),
                   intemperyWeather.getFloat( "rain_ground_global_intensity", 0.0f ),
                   static_cast<int>( intemperyWeather.getFloat( "rain_droplets_max", 0.0f ) ) );
}

bool GMFreeRide::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    // FUN_00b10da0 case 3 (0xc0c0): profile+0x58 = 2, current car 0x183.
    // Only raises the step: a loaded save already past it keeps its progress.
    GetPlayerProfile().StoryStep = Max( GetPlayerProfile().StoryStep, 2 );
    // The story's own house grants follow the step (the caravan at step 10):
    // a save that is already there gets it on entry, so the house is reachable
    // without the realtor.
    ApplyStoryGrants();
    // -island <ibiza|hawai|casino> and -tp x,z[,y] with -heading <deg>:
    // spawn the car anywhere for touring (the house / outro cinematic flow is
    // ibiza only; the wake-up cinematic with it).
    const char *pIslandArg = CmdLine::GetArgValue( "-island" );
    islandName = ToLower( std::string( pIslandArg != nullptr ? pIslandArg : "ibiza" ) );
    if ( islandName == "hawaii" || islandName == "oahu" ) {
        islandName = "hawai";
    }
    // Back from a cinematic that handed the car over: the car where the scene
    // left it. The dream (bStory) is the story flow - Ibiza only, the Solar
    // Crown goal continues; a mode's own scene (the car wash, docs/re/
    // car_wash_spec.md) hands back on any island as a plain drive.
    StoryHandoff &handoff = GetStoryHandoff();
    bool bFromHandoff = false;
    if ( handoff.bSet && !handoff.Island.empty() ) {
        // A scene on another island hands its island over with the car (the
        // Hawai igloo intro, story step 13): the drive resumes there.
        islandName = handoff.Island;
    }
    if ( handoff.bSet && ( islandName == "ibiza" || !handoff.bStory ) ) {
        bFromDream = handoff.bStory;
        bFromHandoff = true;
        bTp = true;
        tpPosition = handoff.Position;
        tpYaw = handoff.Yaw;
        dreamWhiteIn = handoff.bWhiteIn ? 0.0f : -1.0f;
        Vector3f entrance;
        float entranceHeading = 0.0f;
        if ( !handoff.EntranceSpot.empty() &&
             FindSpotEntrance( islandName, handoff.EntranceSpot.c_str(), entrance, entranceHeading ) ) {
            // The scene parked the car where the drive cannot start (inside
            // the igloo hut: no drive collision there): the spot's own
            // road-side entrance instead.
            tpPosition = entrance;
            tpYaw = entranceHeading;
            OTDU_LOG_INFO( "FREERIDE: handoff at '%s' entrance (%.1f, %.1f, %.1f) on %s\n", handoff.EntranceSpot.c_str(),
                           entrance.x, entrance.y, entrance.z, islandName.c_str() );
        }
        handoff = StoryHandoff();
    }
    const bool bHouseFlow = islandName == "ibiza" && !CmdLine::HasArg( "-tp" ) && !CmdLine::HasArg( "-home" ) &&
                            !bFromDream && !bFromHandoff;
    if ( const char *pTp = CmdLine::HasArg( "-tp" ) ? CmdLine::GetArgValue( "-tp" ) : nullptr ) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if ( sscanf( pTp, "%f,%f,%f", &x, &y, &z ) == 3 ) {
            tpPosition = Vector3f( x, y, z );
            bTp = true;
        } else if ( sscanf( pTp, "%f,%f", &x, &z ) == 2 ) {
            tpPosition = Vector3f( x, 0.0f, z );
            bTp = true;
        } else {
            OTDU_LOG_WARN( "FREERIDE: -tp '%s' not x,z or x,y,z\n", pTp );
        }
        if ( const char *pHeading = CmdLine::GetArgValue( "-heading" ) ) {
            tpYaw = static_cast<float>( atof( pHeading ) ) * 0.0174532925f;
        }
    }
    // -home: spawn at the home house (profile HomeHouse, the DBHouse spot's
    // entrance point). The story wake-up flow stays on the runs without it.
    if ( CmdLine::HasArg( "-home" ) ) {
        const PlayerProfile &profile = GetPlayerProfile();
        const std::string &home = !profile.HomeHouse.empty() ? profile.HomeHouse : std::string( "EPH_L3DI_5067I" );
        Vector3f entrance;
        float heading2 = 0.0f;
        if ( FindSpotEntrance( islandName, home.c_str(), entrance, heading2 ) ) {
            tpPosition = entrance;
            tpYaw = heading2;
            bTp = true;
            OTDU_LOG_INFO( "FREERIDE: -home '%s', entrance at (%.1f, %.1f, %.1f)\n", home.c_str(), entrance.x, entrance.y,
                           entrance.z );
        } else {
            OTDU_LOG_WARN( "FREERIDE: -home '%s' not in the spots of '%s'\n", home.c_str(), islandName.c_str() );
        }
    }
    // -atwash: spawn 18 m in front of the island's first car wash, facing it
    // (the wash test hook: the prompt and the validate can be scripted). The
    // forecourt is the building's local -Z (C_WASHING rests PLAYER_CAR at local
    // z -17.5), so the car goes 18 m down the spot's forward axis; y is the
    // spot record's ground height (the fit probe shoots DOWN from it, so y 0
    // would park the car under the island). A handoff wins: the hook only
    // spawns the first entry, not the return from the wash scene.
    if ( CmdLine::HasArg( "-atwash" ) && !bFromHandoff ) {
        CarWash washes;
        if ( washes.load( islandName ) ) {
            const CarWashSpot &spot = washes.getSpots().front();
            const float forwardX = std::sin( spot.Heading ), forwardZ = std::cos( spot.Heading );
            tpPosition = Vector3f( spot.Position.x - forwardX * 18.0f, spot.Position.y,
                                   spot.Position.z - forwardZ * 18.0f );
            tpYaw = spot.Heading; // the car faces the building (its local +Z)
            bTp = true;
            OTDU_LOG_INFO( "FREERIDE: -atwash '%s' at (%.1f, %.1f), spawn 18 m in front\n", spot.Bank.c_str(),
                           spot.Position.x, spot.Position.z );
        } else {
            OTDU_LOG_WARN( "FREERIDE: -atwash: no car wash on '%s'\n", islandName.c_str() );
        }
    }
    // -atsign: spawn 15 m from the island's first direction sign, facing it
    // (the sign-text test hook; -atsign <m> sets the distance). The sign's
    // plate faces look along its transformed -Z; y is the sign's base height
    // (the sector load follows the spawn).
    if ( CmdLine::HasArg( "-atsign" ) && !bFromHandoff ) {
        float distance = 15.0f;
        if ( const char *pValue = CmdLine::GetArgValue( "-atsign" ) ) {
            distance = static_cast<float>( atof( pValue ) );
        }
        Vector3f signPosition;
        float signYaw = 0.0f;
        if ( EdenWorld::findSignSpawn( islandName.c_str(), signPosition, signYaw ) ) {
            const float forwardX = std::sin( signYaw ), forwardZ = std::cos( signYaw );
            tpPosition = Vector3f( signPosition.x - forwardX * distance, signPosition.y,
                                   signPosition.z - forwardZ * distance );
            tpYaw = signYaw; // the car faces the plate
            bTp = true;
            OTDU_LOG_INFO( "FREERIDE: -atsign spawn %.0f m in front of the sign, facing it\n", distance );
        } else {
            OTDU_LOG_WARN( "FREERIDE: -atsign: no direction sign found on '%s'\n", islandName.c_str() );
        }
    }
    // locate() first: it sets the house position (read before it, the stream
    // centre was (0,0) and the C_FIRST_OUTRO drove over the bare island LOD).
    // The -home block above already set tpPosition, so the stream centre below
    // still follows the spawn.
    if ( bHouseFlow && !outro.locate() ) {
        return false;
    }
    const Vector3f streamCenter = bHouseFlow ? outro.getHousePosition() : ( bTp ? tpPosition : Vector3f() );
    const int32_t radius = CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    if ( !view.initialize( islandName.c_str(), streamCenter, radius ) ) {
        return false;
    }
    // Whole-island tunnel / bridge vs terrain survey (-scantunnels): the
    // run ends through -exitframe; the survey blocks one frame for minutes
    // (every sector bank loads, scans, frees) and logs its own progress.
    if ( CmdLine::HasArg( "-scantunnels" ) ) {
        view.getWorld().scanTunnelTerrain();
    }
    // -hideinstances x,z,r: skip the streamed instances within the flat radius
    // (debug: the shadow flicker freeze-proof — animated cloth casters sweep
    // their depth texels through the shadow map, STATE session 41).
    if ( const char *pHide = CmdLine::HasArg( "-hideinstances" ) ? CmdLine::GetArgValue( "-hideinstances" )
                                                                 : nullptr ) {
        float hx = 0.0f, hz = 0.0f, hr = 0.0f;
        if ( sscanf( pHide, "%f,%f,%f", &hx, &hz, &hr ) == 3 && hr > 0.0f ) {
            view.getWorld().setInstanceHide( Vector3f( hx, 0.0f, hz ), hr );
            OTDU_LOG_INFO( "FREERIDE: instances hidden within %.1f m of (%.1f, %.1f)\n", hr, hx, hz );
        }
    }
    // In-game weather: game_weathers.xmb time of day presets, driven by the
    // weather cycle (game/freeride/weather_cycle.h).
    gameWeathers.load( ( gpFile->getBankFolder() + "islands/" + islandName + "/fx/game_weathers.xmb" ).c_str() );
    // The clock and the weather it drives (game/freeride/weather_cycle.h): the
    // machine's time by default, the preset at or below it, and the file's
    // intemperies walking clear -> cloud -> rain -> storm. -weather <name> pins
    // one preset, -intempery its variant, -clocktime / -clockrate the clock,
    // -clock HH.MM freezes it (the GLM shape of the same hook), -night the
    // darkest time.
    weatherCycle.load( gameWeathers );
    pWeather = weatherCycle.getPreset();
    // -intempery without -weather / -night: the running cycle would walk away
    // from the forced state, so pin the picked preset and force the intempery
    // on it (the behaviour origin/main's -intempery runs verified).
    if ( CmdLine::HasArg( "-intempery" ) && weatherCycle.isRunning() ) {
        weatherCycle.setPinned( pWeather );
        if ( pWeather != nullptr ) {
            if ( const WeatherPreset *pBase = gameWeathers.find( pWeather->Name.c_str() ) ) {
                pWeather = pBase; // the overlay maps live on the library preset
            }
        }
        applyForcedIntempery();
    }
    OTDU_LOG_INFO( "FREERIDE: weather '%s' intempery %d\n",
                   pWeather != nullptr ? pWeather->Name.c_str() : "(none)",
                   pWeather != nullptr
                       ? ( pWeather->ActiveIntempery >= 0 ? pWeather->ActiveIntempery : pWeather->Intempery )
                       : 0 );
    // The unit system of the readouts (game/units.h; -imperial / -metric).
    OTDU_LOG_INFO( "FREERIDE: units %s\n", IsImperialUnits() ? "imperial (mph / mi)" : "metric (km/h / km)" );
    if ( pWeather != nullptr ) {
        // The NIGHTLMAP day / night weights the spot exteriors blend by
        // (EdenSceneView::updateLightmapMaterials) [session 45: the dealer
        // showroom rendered with the night lightmap at midday].
        const Vector4f extLmap =
            pWeather->getVector4( "spot_exterior_lightmap_day_night", Vector4f( -1.0f, -1.0f, -1.0f, -1.0f ) );
        const Vector4f intLmap =
            pWeather->getVector4( "spot_interior_lightmap_day_night", Vector4f( -1.0f, -1.0f, -1.0f, -1.0f ) );
        OTDU_LOG_INFO( "FREERIDE: lmap ext (%.3f %.3f) int (%.3f %.3f)\n", extLmap.x, extLmap.y, intLmap.x,
                       intLmap.y );
    }
    view.setWeather( pWeather );
    if ( bHouseFlow ) {
        if ( !outro.load( view ) ) {
            OTDU_LOG_WARN( "FREERIDE: house spot / C_FIRST_OUTRO not available\n" );
        }
        // C_FADE_FREERIDE (fade_freeride.bnk): the wake-up cinematic after the
        // story timer, in the same house spot frame as C_FIRST_OUTRO. Optional:
        // without it the mode falls back to resuming the drive from the white.
        if ( !wakeup.load( outro.getSceneOrigin() ) ) {
            OTDU_LOG_WARN( "FREERIDE: C_FADE_FREERIDE not available\n" );
        }
    } else {
        OTDU_LOG_INFO( "FREERIDE: island '%s'%s — no house cinematic\n", islandName.c_str(),
                       bTp ? ", spawn overridden" : "" );
    }
    firstFreerideTime = CmdLine::HasArg( "-storytime" ) ? CmdLine::GetArgInt( "-storytime", 90 ) : kFirstFreerideTime;
    // -car <_car_file_name>: drive / -vehtest another DBCarPhysicsData car
    // (the rims keep the California's; the tyre radius comes from the DB).
    if ( const char *pCar = CmdLine::GetArgValue( "-car" ) ) {
        carDesc.FileName = pCar;
    } else {
        // The car the player picked in a house garage or drove out of a dealer
        // (PlayerProfile::DrivenCarId); an unknown / no longer owned id keeps
        // the starter car.
        PlayerProfile &profile = GetPlayerProfile();
        const int32_t driven = profile.DrivenCarId;
        const bool bOwned =
            std::find( profile.OwnedCars.begin(), profile.OwnedCars.end(), driven ) != profile.OwnedCars.end();
        CinematicCar::Desc drivenDesc;
        // The rims it was ordered with at a dealer (PlayerProfile::OwnedCarRims,
        // the buy window's wheels page); none = the car's _default_rims.
        const auto rimsIt = profile.OwnedCarRims.find( driven );
        const uint64_t orderedRims = rimsIt != profile.OwnedCarRims.end() ? rimsIt->second : 0;
        if ( driven >= 0 && bOwned &&
             ( orderedRims != 0 ? CinematicCar::FindDatabaseDesc( driven, orderedRims, drivenDesc )
                                : CinematicCar::DescFromDatabase( driven, drivenDesc ) ) ) {
            carDesc = drivenDesc;
            OTDU_LOG_INFO( "FREERIDE: driving the profile's car %d '%s', rims %s%s\n", driven, carDesc.FileName.c_str(),
                           carDesc.RimFront.c_str(), orderedRims != 0 ? " (ordered at the dealer)" : "" );
            drivenCarIdAtEnter = driven;
        } else if ( driven >= 0 ) {
            OTDU_LOG_WARN( "FREERIDE: profile car %d is not owned / not in the database: starter car\n", driven );
            profile.DrivenCarId = -1;
        }
    }
    // -bike: motorbike prototype (docs/re/bikes_spec.md). The shipped database
    // has one bike row (Default_Bike, id 666, _vehiclecategory 19 = Ma2) and
    // the model the cut bikes left behind is the Ducati Diavel bank, so the
    // physics row and the model bank differ: Desc::ModelBank carries the model
    // and FileName stays Default_Bike for LoadVehicleSetup.
    const bool bBikeMode = CmdLine::HasArg( "-bike" );
    if ( bBikeMode ) {
        CinematicCar::Desc bikeDesc;
        if ( CinematicCar::DescFromDatabase( 666, bikeDesc ) ) {
            carDesc = bikeDesc;
            carDesc.ModelBank = "diavel";
            // No plus-size car rims on a bike (the profile's rim sizes are the
            // driven car's).
            carDesc.RimDiameterFront = carDesc.RimDiameterRear = 0;
            OTDU_LOG_INFO( "FREERIDE: -bike: '%s' physics row, 'diavel' model (tyre r %.3f m)\n",
                           carDesc.FileName.c_str(), carDesc.TireRadius );
        } else {
            OTDU_LOG_WARN( "FREERIDE: -bike: Default_Bike row 666 not in the database\n" );
        }
    }
    // The rims bought at a tuning shop (a plus size: the tire keeps the car's
    // rolling radius and gets the matching sidewall).
    if ( !bBikeMode ) {
        ApplyProfileRims( carDesc );
    }
    // -rims <inch>: force a rim size (0 = stock) for a test, without touching
    // the profile.
    if ( const char *pRims = CmdLine::GetArgValue( "-rims" ) ) {
        const int32_t size = atoi( pRims );
        carDesc.RimDiameterFront = carDesc.RimDiameterRear = size > 0 ? size : 0;
        OTDU_LOG_INFO( "FREERIDE: -rims %d\"\n", size );
    }
    car.load( carDesc );
    applyOwnedPaint();
    applyOwnedStickers();
    // The sticker shop rooms' movie: its stsh: FSCommands come to the shop UI.
    interior.setCommandSink( [this]( const std::string &command, const std::string & ) {
        stickerShop.onCommand( command );
    } );
    // -stickershop: the paint / sticker shop menu over the freeride scene at
    // spawn (a test hook: the retail flow walks into an ESTICK interior and
    // the menu opens at the seller; this opens it directly with the car in
    // view - autoplay can drive the editor from it).
    if ( CmdLine::HasArg( "-stickershop" ) ) {
        paintMenu.setSidePanel( false );
        paintMenu.setStickerContext( &stickerCanvas, car.getIdCar() );
        paintMenu.open( car.getIdCar(), FindCarDisplayName( car.getIdCar() ) );
    }
    if ( const char *pStickerTest = CmdLine::GetArgValue( "-stickertest" ) ) {
        char bankFile[128] = {};
        int32_t index = -1;
        if ( sscanf( pStickerTest, "%127[^,],%d", bankFile, &index ) == 2 && index >= 0 ) {
            stickerTestBank = bankFile;
            stickerTestIndex = index;
            OTDU_LOG_INFO( "FREERIDE: -stickertest '%s' #%d\n", stickerTestBank.c_str(), stickerTestIndex );
        } else {
            OTDU_LOG_WARN( "FREERIDE: -stickertest: '<bankfile>,<index>' expected, got '%s'\n", pStickerTest );
        }
    }
    {
        // Back from a scene in the same car: the roof, the windows and the body
        // (mud / dust / crust / wetness) as they were (VehicleSceneState, written
        // by exit(); a different car - the story's loan, a purchase - starts
        // fresh). -noscenestate is the A/B: the old reset.
        const VehicleSceneState &state = GetVehicleSceneState();
        if ( state.bSet && state.CarFile == carDesc.FileName && !CmdLine::HasArg( "-noscenestate" ) ) {
            if ( car.hasHardtop() && state.bHardtopOpen ) {
                car.setHardtopOpen( true );
                car.updateHardtop( 1000.0f ); // the end pose at once
            }
            if ( state.bWindowsOpen ) {
                car.setWindowsOpen( true );
                car.updateWindows( 1000.0f );
            }
            carDirt.seed( state.Mud, state.Dirt, state.Hardness, state.Wetness );
            car.applySurfaceState( state.Mud, state.Dirt, state.Hardness, state.Wetness );
            OTDU_LOG_INFO( "FREERIDE: '%s' as it left: roof %s, windows %s, mud %.2f dirt %.2f wet %.2f\n",
                           carDesc.FileName.c_str(), state.bHardtopOpen ? "open" : "closed",
                           state.bWindowsOpen ? "open" : "closed", state.Mud, state.Dirt, state.Wetness );
        }
    }
    if ( CmdLine::HasArg( "-loglicenses" ) ) {
        GetLicenses();
    }

    // Vehicle sounds: CarVSTConfig.xmb of sound/vehicles/<car>_high.bnk
    // (lowercase file name of the car).
    if ( gpAudio != nullptr && gpAudio->isReady() ) {
        gpAudio->setGameMode( "FREERIDE" );
        std::string soundBank = carDesc.FileName;
        for ( char &c : soundBank ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
        soundBank = gpFile->getBankFolder() + "sound/vehicles/" + soundBank + "_high.bnk";
        if ( !vehicleAudio.load( soundBank.c_str() ) ) {
            OTDU_LOG_WARN( "FREERIDE: no vehicle sounds in '%s'\n", soundBank.c_str() );
        }
    }
    // Car radio (sound/radio/music, plus the custom station folders of
    // <Music>/OpenTDU2): the D-pad right opens the panel.
    if ( !radio.load() ) {
        OTDU_LOG_WARN( "FREERIDE: no radio station (sound/radio/music and the station folder are empty)\n" );
    }
    gps.load();
    // The island ambience beds and the ambient traffic engines / horns
    // (docs/re/ambience_audio_spec.md): both are data driven from the game's
    // own sound files, both keep quiet without a device or a missing bank.
    {
        const std::string islandFolder = gpFile->getBankFolder() + "islands/" + islandName + "/";
        ambience.setHeightQuery( [this]( float x, float z, float &outHeight ) {
            return view.getWorld().getHeight( x, z, outHeight );
        } );
        if ( !ambience.load( islandFolder ) ) {
            OTDU_LOG_WARN( "FREERIDE: no island ambience for '%s'\n", islandName.c_str() );
        }
        if ( !trafficAudio.load() ) {
            OTDU_LOG_WARN( "FREERIDE: no ambient traffic sounds\n" );
        }
    }
    // -dsled r,g,b: the idle light-bar colour of the pad (an A/B / taste hook;
    // the chase flash overrides it).
    if ( const char *pLed = CmdLine::GetArgValue( "-dsled" ) ) {
        int red = 0, green = 0, blue = 0;
        if ( sscanf( pLed, "%d,%d,%d", &red, &green, &blue ) >= 3 ) {
            gamepad.setBaseColor( static_cast<uint8_t>( Clamp( red, 0, 255 ) ),
                                  static_cast<uint8_t>( Clamp( green, 0, 255 ) ),
                                  static_cast<uint8_t>( Clamp( blue, 0, 255 ) ) );
        }
    }
    if ( const char *pDrive = CmdLine::HasArg( "-gpsdrive" ) ? CmdLine::GetArgValue( "-gpsdrive" ) : nullptr ) {
        float x = 0.0f, z = 0.0f, speed = 25.0f;
        const int32_t count = sscanf( pDrive, "%f,%f,%f", &x, &z, &speed );
        if ( count >= 2 ) {
            bGpsDrive = true;
            gpsDriveTarget = Vector3f( x, 0.0f, z );
            gpsDriveSpeed = count == 3 ? speed : 25.0f;
        } else {
            OTDU_LOG_WARN( "FREERIDE: -gpsdrive '%s' not x,z[,speed]\n", pDrive );
        }
    }

    // Driving collision: sector .shk / -R.shk + the spot collision
    // (eph_l3di_sc_*.shk, house frame) + the heightfield (the carve ground of
    // the tunnel / bridge floors where the sheet is carved away,
    // EdenWorld::getGroundHeight).
    collision.setHeightFunction(
        [this]( float x, float z, float fromY, float &outHeight ) {
            return view.getWorld().getGroundHeight( x, z, fromY, outHeight );
        } );
    // The collision streams the sector .shk files of the island being played
    // (the constructor default is ibiza: Hawaii / Casino had no collision).
    collision.setIsland( islandName.c_str() );
    // Library.dhk models (walls, poles, trees...) placed by the sector V / O .pmi lists.
    collision.loadLibrary( view.getWorld().getCommonBank() );
    collision.streamAround( streamCenter, 1 );
    for ( const BankEntry *pEntry : bHouseFlow ? outro.getBank().findEntriesByExtension( "shk" )
                                               : std::vector<const BankEntry *>() ) {
        HavokCollision spotCollision;
        if ( LoadHavokCollision( pEntry->pData, pEntry->Size, spotCollision, true ) ) {
            collision.addCollision( spotCollision, outro.getSceneOrigin(), true );
            OTDU_LOG_INFO( "FREERIDE: spot collision %s: %zu triangles, %zu boxes\n", pEntry->Filename.c_str(),
                           spotCollision.getNumTriangles(), spotCollision.Boxes.size() );
        }
    }
    setupVehicle();
    chase.load( setup.CameraId != 0 ? setup.CameraId : setup.Db.CarId );
    hud.load( hudFileName() );
    // -tuningmenu: open the tuning shop panel at spawn (the menu test hook).
    if ( CmdLine::HasArg( "-tuningmenu" ) ) {
        tuningMenu.setSidePanel( false );
        tuningMenu.open( car.getIdCar(), FindCarDisplayName( car.getIdCar() ) );
    }

    view.setDrawCallback( [this]( EdenRenderer &renderer, EdenSceneView::ePass pass ) {
        // Port effects (wheel spray, tyre tracks) and the island's birds: the
        // only things drawn in that pass, and it draws nothing else.
        if ( pass == EdenSceneView::ePass::Effects ) {
            if ( gpRender != nullptr ) {
                gpRender->getSurfaceFx().draw( gpRender->getDevice(), renderer.getViewProjection(),
                                               renderer.getSceneParams() );
                birds.draw( gpRender->getDevice(), renderer.getViewProjection(), renderer.getSceneParams() );
            }
            return;
        }
        outro.draw( renderer );
        // The island's buildings: every pass except the reflection actors (like
        // the world viewer), so they are lit, cast shadows and reflect.
        if ( pass != EdenSceneView::ePass::ProjectorActors ) {
            spotExteriors.draw( renderer );
            modProps.draw( renderer );
        }
        // The race gates: holograms, no shadow of their own.
        if ( pass != EdenSceneView::ePass::ShadowCasters && pass != EdenSceneView::ePass::ProjectorActors &&
             pass != EdenSceneView::ePass::Environment ) {
            raceGates.draw( renderer );
        }
        const bool bScenePass = pass != EdenSceneView::ePass::Environment && pass != EdenSceneView::ePass::Reflection;
        // The other cars ride in the dynamic environment cube: the retail layer
        // table has ENVMAP_* (6 faces x {plain, NO_CARS, WITH_CARS}) and the
        // mirrors / paint / glass of a car sample that cube, so a traffic car
        // only shows in them when the Environment pass draws it (postfx_spec.md
        // 1.5; the owner's "cars don't show up in the mirrors"). The player car
        // stays out - it is the cube's centre (setEnvironmentCenter), its own
        // faces would fill with itself - and the planar Reflection pass keeps
        // the scene-only rule. The cube faces are copied to the sampled target
        // after the pass (EdenSceneView::renderEnvironment), so sampling it
        // here is safe.
        const bool bCubeActors = pass == EdenSceneView::ePass::Environment;
        if ( ( bScenePass || bCubeActors ) && car.isLoaded() ) {
            if ( bScenePass ) {
                car.draw( renderer, carWorld, view.getWeather(), bEnvironmentAssigned );
            }
            // The sweeping blades clear the windscreen layer (once per frame,
            // the main view): the blade's pivot->tip segment, projected to
            // the screen uv the droplets live in.
            if ( pass == EdenSceneView::ePass::Opaque && view.getWindscreen().isEnabled() ) {
                const Matrix4x4f &vp = renderer.getViewProjection();
                auto toUv = [&vp]( const Vector3f &p, float &outU, float &outV ) {
                    const float x = vp.m[0][0] * p.x + vp.m[1][0] * p.y + vp.m[2][0] * p.z + vp.m[3][0];
                    const float y = vp.m[0][1] * p.x + vp.m[1][1] * p.y + vp.m[2][1] * p.z + vp.m[3][1];
                    const float w = vp.m[0][3] * p.x + vp.m[1][3] * p.y + vp.m[2][3] * p.z + vp.m[3][3];
                    if ( w <= 1e-4f ) {
                        return false;
                    }
                    outU = x / w * 0.5f + 0.5f;
                    outV = 0.5f - y / w * 0.5f;
                    return outU > -0.2f && outU < 1.2f && outV > -0.2f && outV < 1.2f;
                };
                Vector3f pivot, tip;
                for ( uint32_t half = 0; half < 2; half++ ) {
                    if ( !car.getWiperSweep( half, pivot, tip ) ) {
                        continue;
                    }
                    float au, av, bu, bv;
                    if ( toUv( pivot, au, av ) && toUv( tip, bu, bv ) ) {
                        view.getWindscreen().wipe( au, av, bu, bv, 0.02f );
                    }
                }
            }
            AiCar::SetCameraPosition( view.getSceneParams().CameraPosition ); // the AI cars' LOD
            traffic.draw( renderer, view.getWeather() );
            aircraft.draw( renderer, view.getWorld(), view.getSceneParams().CameraPosition );
            police.draw( renderer, view.getWeather() );
            opponents.draw( renderer, view.getWeather() );
        }
        // Tess at the car window (C_FADE_FREERIDE; end pose kept after it).
        if ( bScenePass && wakeup.isStarted() ) {
            wakeup.draw( renderer );
        }
    } );

    // The port's surface effects draw in their own pass (wheel spray, tyre
    // tracks) - see tickSurfaceFx.
    view.setEffectsPass( true );

    bLoaded = true;
    // The '#command:BACK_ON_TRACK#' tip of the return to road prompt (loc
    // 2216) needs the Default_KB.xmb resolver (shared with the walk mode).
    SetDefaultCommandResolver();
    pauseMenu.load();
    // The pause screen clock reads the PC's clock itself (world_clock.h,
    // PauseMenu::refreshClock); the weather preset is picked from the same
    // hour below (the retail running clock is [ENC]).
    mapScreen.load();
    // The retail map is the island colour map drawn flat. The world extent of
    // Ibiza's texture is fitted on the road graph (Ibiza_ROAD_MAP.IA node
    // positions over ibiza-c, docs/STATE.md session 23) [INF]; the other
    // islands keep the 3D map camera until theirs is fitted.
    if ( view.getWorld().getIslandColorMap() != nullptr ) {
        // World extent = the area grid of worldconfig.cpr (<Area NbColumns= NbRows=>)
        // x 6144 m: Ibiza 7 x 6 = 43008 x 36864 [DATA, the fit on the road graph
        // matched]; Hawai 11 x 9 = 67584 x 55296 [INF: same rule, checked on the roads].
        std::string worldConfig;
        int32_t columns = 0, rows = 0;
        if ( LoadCPRText( ( gpFile->getBankFolder() + "islands/" + islandName + "/level/worldconfig.cpr" ).c_str(), worldConfig ) ) {
            const size_t area = worldConfig.find( "<Area" );
            if ( area != std::string::npos ) {
                const size_t c = worldConfig.find( "NbColumns=", area ), r = worldConfig.find( "NbRows", area );
                if ( c != std::string::npos && r != std::string::npos ) {
                    columns = atoi( worldConfig.c_str() + worldConfig.find( '"', c ) + 1 );
                    rows = atoi( worldConfig.c_str() + worldConfig.find( '"', r ) + 1 );
                }
            }
        }
        if ( columns > 0 && rows > 0 ) {
            mapScreen.setIslandMap( view.getWorld().getIslandColorMap(), columns * 6144.0f, rows * 6144.0f );
        }
    }
    mapScreen.loadSpots( islandName );
    mapScreen.loadRoads( islandName );
    // The grass road-graph suppression (input to drawGrass; the village
    // streets / plazas are terrain paint the geometric road index never
    // sees - docs/re/grass_spec.md 3 / 7).
    if ( !mapScreen.getRoadShapes().empty() ) {
        view.getWorld().buildGrassRoadIndex( mapScreen.getRoadShapes() );
        if ( CmdLine::HasArg( "-loggrassroads" ) ) {
            // Sample spots for -tpin probes: the middle point of every Nth shape.
            const auto &shapes = mapScreen.getRoadShapes();
            for ( size_t i = 0; i < shapes.size(); i += Max<size_t>( shapes.size() / 40, 1 ) ) {
                if ( !shapes[ i ].empty() ) {
                    const Vector3f &p = shapes[ i ][ shapes[ i ].size() / 2 ];
                    OTDU_LOG_INFO( "grassroad sample %zu/%zu: %.0f,%.0f (%zu pts)\n", i, shapes.size(), p.x, p.z,
                                   shapes[ i ].size() );
                }
            }
        }
    }
    // Instant challenges (race markers on the road graph) and the police
    // chase (the cop car of the island's chaseconfig.cpr).
    raceEvents.load( islandName, mapScreen.getRoadNodes(), mapScreen.getRoadEdges() );
    loadChampionshipSpots();
    police.loadCopCar( islandName );
    police.setRoads( &mapScreen.getRoadNodes(), &mapScreen.getRoadEdges(), &mapScreen.getRoadShapes() );
    opponents.setRoads( &mapScreen.getRoadNodes(), &mapScreen.getRoadEdges(), &mapScreen.getRoadShapes() );
    GetCruisingZones().load( islandName );
    discovery.load( islandName );
    // The island's buildings (dealerships, shops, houses...): one exterior bank
    // per spot type, streamed around the car in tick(). The story house is a
    // spot too, but its bank is drawn by the C_FIRST_OUTRO set (FirstOutro): it
    // is excluded so the texture cache owner is not released twice.
    bSpotExteriors = !CmdLine::HasArg( "-noexteriors" );
    spotExteriors.load( islandName );
    // The race gates (the island's checkpoint pack, drawn on the course being
    // driven: RaceGates).
    bRaceGates = !CmdLine::HasArg( "-noracegates" );
    if ( bRaceGates ) {
        raceGates.load( view, islandName );
    }
    // The island's car washes (docs/re/car_wash_spec.md) - a second walk over
    // spots.bin for the ECW_* records only (the prompt + the washing scene).
    if ( !CmdLine::HasArg( "-nowash" ) ) {
        carWash.load( islandName );
    }
    // Mod props (docs/re/modding.md): our own meshes (OBJ + MTL + PNG) placed
    // from the mods overlay; -nomodprops turns them off. The fuel stations of
    // our own get a map icon (the movie has none): the map draws them from
    // these positions (docs/re/fuel_gas_station_spec.md). The renderer is not
    // ready here on the first attempt - ensureModPropsLoaded retries in tick.
    ensureModPropsLoaded();
    // The cheats (source/game/cheats.h): the island's spots for the teleport
    // list, the weather presets, and -tpin (a teleport once the drive runs: the
    // in-game path, which -tp - a spawn override - does not exercise). The
    // wallet is set once at startup (tdu_instance.cpp).
    {
        std::vector<Cheats::Spot> cheatSpots;
        for ( const SpotRecord &spot : LoadSpotList( islandName ) ) {
            Cheats::Spot entry;
            entry.Name = spot.Bank;
            entry.Position = spot.Position;
            entry.Heading = spot.Heading;
            cheatSpots.push_back( std::move( entry ) );
        }
        cheats.setSpots( std::move( cheatSpots ), islandName );
    }
    {
        std::vector<std::string> names;
        int32_t current = 0;
        const std::vector<WeatherPreset> &presets = gameWeathers.getPresets();
        for ( size_t i = 0; i < presets.size(); i++ ) {
            names.push_back( presets[i].Name );
            if ( &presets[i] == pWeather ) {
                current = static_cast<int32_t>( i );
            }
        }
        cheats.setWeathers( std::move( names ), current );
    }
    // -carpos <spot>[,<spot>...]: test, log the Entity__CarPos of the shops
    // (CarDelivery: where a bought car is parked outside).
    if ( const char *pCarPos = CmdLine::GetArgValue( "-carpos" ) ) {
        std::string names( pCarPos );
        size_t start = 0;
        while ( start <= names.size() ) {
            const size_t comma = names.find( ',', start );
            const std::string spot =
                names.substr( start, comma == std::string::npos ? std::string::npos : comma - start );
            if ( !spot.empty() ) {
                CarDeliveryPose pose;
                FindCarDeliveryPose( islandName, spot, pose );
            }
            if ( comma == std::string::npos ) {
                break;
            }
            start = comma + 1;
        }
    }
    if ( const char *pTpIn = CmdLine::GetArgValue( "-tpin" ) ) {
        float x = 0.0f, z = 0.0f, heading = 0.0f;
        const int32_t fields = sscanf( pTpIn, "%f,%f,%f", &x, &z, &heading );
        if ( fields >= 2 ) {
            bTeleportIn = true;
            teleportInTarget = Vector3f( x, 0.0f, z );
            teleportInHeading = heading * kDegToRad;
            bTeleportInHeading = fields >= 3;
        }
    }
    if ( bHouseFlow && outro.isLoaded() ) {
        spotExteriors.excludeBank( outro.getBank().getPath() );
    }
    // Ambient traffic follows the road graph; density and mix by the game
    // hour (the PC's clock, world_clock.h) [DATA aiconfig.cpr]. -notraffic
    // turns it off.
    if ( !CmdLine::HasArg( "-notraffic" ) ) {
        const float trafficHour =
            static_cast<float>( WorldClockHours() ) + static_cast<float>( WorldClockMinutes() ) / 60.0f;
        traffic.load( islandName, mapScreen.getRoadNodes(), mapScreen.getRoadEdges(), mapScreen.getRoadShapes(),
                      trafficHour );
    }
    // The island's aeroplanes: the flight loops of world_aircrafts.bnk flown by
    // the library aircraft that aircrafts.bnk names (a landing, a runway roll
    // and a take-off per loop, plus the high circuits). -noaircraft.
    bLogAircraft = CmdLine::HasArg( "-logaircraft" );
    if ( !CmdLine::HasArg( "-noaircraft" ) ) {
        aircraft.load( islandName, view.getWorld(),
                       [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
    }
    // The island's birds (fx/details/birds.bnk: five sprites, no geometry).
    // -nobirds.
    bLogBirds = CmdLine::HasArg( "-logbirds" );
    if ( !CmdLine::HasArg( "-nobirds" ) ) {
        birds.load( islandName );
    }
    if ( CmdLine::HasArg( "-skipcine" ) || bTp || !outro.isLoaded() ) {
        startDriving();
    } else {
        // FUN_00a7cc70: C_FIRST_OUTRO, the player car under cinematic
        // control (FUN_00c41100(1)), HUD hidden (listener event 5).
        outro.start( static_cast<float>( CmdLine::GetArgInt( "-cinetime", 0 ) ) );
        state = eState::Outro;
        carWorld = outroCarWorld();
        outroWheelSpin = 0.0f;
        hud.setVisible( false );
    }
    return true;
}

std::string GMFreeRide::hudFileName() const {
    // DBCarPhysicsData _hud_file_name (gauge bank name).
    if ( gpDatabase != nullptr ) {
        XmbValue root = gpDatabase->getDocument().getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
            XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
            if ( StringCmpI( car["_car_file_name"].asString().c_str(), carDesc.FileName.c_str() ) == 0 ) {
                return car["_hud_file_name"].asString();
            }
        }
    }
    return carDesc.FileName;
}


namespace {
// The near-car spec card data of a car id (the DB rows are read once; the
// card is filled per frame while a car stands near - the traffic types and
// the showroom display cars).
struct SpecCardData {
    bool bValid = false;
    std::string Name;
    int32_t Price = 0;
    CarSpec Spec;
};

const SpecCardData *CachedSpecCard( int32_t carId ) {
    static std::unordered_map<int32_t, SpecCardData> cache;
    const auto found = cache.find( carId );
    if ( found != cache.end() ) {
        return found->second.bValid ? &found->second : nullptr;
    }
    SpecCardData data;
    const int32_t physicsRow = FindCarPhysicsRow( carId );
    data.bValid = physicsRow >= 0 && FillCarSpec( physicsRow, data.Spec );
    if ( data.bValid ) {
        data.Name = FindCarDisplayName( carId );
        data.Price = data.Spec.Price;
    }
    const auto inserted = cache.emplace( carId, std::move( data ) ).first;
    return inserted->second.bValid ? &inserted->second : nullptr;
}

// The tuning levels the profile has for a car (0/0/0 when none was bought).
TuningLevels ProfileTuningLevels( int32_t carId ) {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarTuning.find( carId );
    return found != profile.OwnedCarTuning.end() ? found->second : TuningLevels();
}

// The rim diameter (inches) the profile has for a car (0 = stock).
int32_t ProfileRimSize( int32_t carId ) {
    const PlayerProfile &profile = GetPlayerProfile();
    const auto found = profile.OwnedCarRimSizes.find( carId );
    return found != profile.OwnedCarRimSizes.end() ? found->second : 0;
}

// DBCarPhysicsData _id_car of a _car_file_name (-1 when absent).
int32_t FindCarIdByFileName( const std::string &fileName ) {
    if ( gpDatabase == nullptr ) {
        return -1;
    }
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBCarPhysicsData" ); i++ ) {
        XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
        if ( StringCmpI( car["_car_file_name"].asString().c_str(), fileName.c_str() ) == 0 ) {
            return static_cast<int32_t>( car["_id_car"].asInt( -1 ) );
        }
    }
    return -1;
}

// The rim size bought at a tuning shop on a car Desc (0 = the stock rims).
void ApplyProfileRims( CinematicCar::Desc &desc ) {
    const int32_t carId = FindCarIdByFileName( desc.FileName );
    const int32_t size = ProfileRimSize( carId );
    desc.RimDiameterFront = size > 0 ? size : 0;
    desc.RimDiameterRear = size > 0 ? size : 0;
    if ( size > 0 ) {
        OTDU_LOG_INFO( "FREERIDE: '%s' on its %d\" rims (tuning shop)\n", desc.FileName.c_str(), size );
    }
}

// -vehtest: straight line runs of the player car on a flat road plane
// (friction 0.7, the road material) with the driving model, to compare with
// DBCarPhysicsData _acceleration_0_100_kmh / _max_theorical_speed /
// _braking_100_0 (debug only, no game state is touched).
void RunVehicleTest( const VehicleDynamics &templateVehicle ) {
    VehicleWorldCollision flat;
    flat.setFlatGround( true, 0.0f, 0.7f );
    const VehicleSetup &setup = templateVehicle.getSetup();
    const float dt = 1.0f / 60.0f;
    // -logcorner: attitude / load trace of the steady state cornering runs
    // (rollover analysis).
    static const bool bLogCorner = CmdLine::HasArg( "-logcorner" );
    auto logState = [&]( const char *pTag, float t, const VehicleDynamics &v ) {
        const std::vector<VehicleDynamics::WheelState> &w = v.getWheels();
        float slipFront = 0.0f, slipRear = 0.0f, loadFront = 0.0f, loadRear = 0.0f;
        for ( size_t i = 0; i < w.size(); i++ ) {
            const bool bFront = v.getWheelDescs()[i].bFront;
            ( bFront ? slipFront : slipRear ) += 0.5f * w[i].SlipRatio;
            ( bFront ? loadFront : loadRear ) += w[i].Load;
        }
        OTDU_LOG_INFO( "VEHTEST %s t %6.2f v %6.1f km/h rpm %5.0f gear %d slip f %.3f r %.3f load f %.0f r %.0f N y %.3f\n", pTag, t,
                       v.getSpeedKmh(), v.getRpm(), v.getGear(), slipFront, slipRear, loadFront, loadRear, v.getPosition().y );
    };
    // Acceleration from rest, full throttle.
    VehicleDynamics v = templateVehicle;
    v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f() );
    VehicleDynamics::Input input;
    for ( int32_t i = 0; i < 60; i++ ) { // settle 1 s
        v.step( dt, input, flat );
    }
    input.Throttle = 1.0f;
    float t100 = -1.0f, t200 = -1.0f, top = 0.0f, tTop = 0.0f, d400 = -1.0f;
    const Vector3f start = v.getPosition();
    for ( int32_t i = 0; i < 120 * 60; i++ ) {
        v.step( dt, input, flat );
        const float t = ( i + 1 ) * dt;
        const float kmh = v.getSpeedKmh();
        if ( t100 < 0.0f && kmh >= 100.0f ) {
            t100 = t;
        }
        if ( t200 < 0.0f && kmh >= 200.0f ) {
            t200 = t;
        }
        if ( d400 < 0.0f && ( v.getPosition() - start ).length() >= 402.3f ) {
            d400 = t;
        }
        if ( kmh > top + 0.05f ) {
            top = kmh;
            tTop = t;
        }
        if ( ( i % 30 ) == 29 && t <= 40.0f ) {
            logState( "accel", t, v );
        }
        if ( t <= 6.0f && ( i % 3 ) == 2 ) {
            OTDU_LOG_INFO( "VEHTEST dt t %5.2f v %6.1f %s\n", t, v.getSpeedKmh(), v.getDebugState().c_str() );
        }
    }
    OTDU_LOG_INFO( "VEHTEST result '%s': 0-100 km/h %.2f s (DB %.2f), 0-200 %.2f s, 402 m %.2f s (DB %.2f), top %.1f km/h at %.1f s (DB %.0f)\n",
                   setup.Db.FileName.c_str(), t100, setup.Accel0To100, t200, d400, setup.QuarterMile, top, tTop,
                   setup.MaxTheoricalSpeed );
    // Braking from 100 km/h.
    v = templateVehicle;
    v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f( 0.0f, 0.0f, 100.0f / 3.6f ) );
    input = VehicleDynamics::Input();
    input.Brake = 1.0f;
    const Vector3f brakeStart = v.getPosition();
    float tStop = -1.0f;
    for ( int32_t i = 0; i < 20 * 60; i++ ) {
        v.step( dt, input, flat );
        if ( ( i % 15 ) == 14 ) {
            logState( "brake", ( i + 1 ) * dt, v );
        }
        if ( v.getSpeedKmh() < 0.5f ) {
            tStop = ( i + 1 ) * dt;
            break;
        }
    }
    OTDU_LOG_INFO( "VEHTEST result '%s': 100-0 km/h %.1f m in %.2f s (DB _braking_100_0 %.0f, _brakingdist %.0f)\n",
                   setup.Db.FileName.c_str(), ( v.getPosition() - brakeStart ).length(), tStop, setup.Braking100To0,
                   setup.BrakingDistance );
    // Reverse cycle (pad report: "after reversing I have to hold square and
    // come to a stop before I can go forward"): brake from rest engages
    // reverse, then the accelerator alone must stop the car, leave reverse and
    // pull away forward.
    {
        v = templateVehicle;
        v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f() );
        input = VehicleDynamics::Input();
        for ( int32_t i = 0; i < 60; i++ ) {
            v.step( dt, input, flat );
        }
        input.Brake = 1.0f; // L2: reverse
        for ( int32_t i = 0; i < 3 * 60; i++ ) {
            v.step( dt, input, flat );
        }
        OTDU_LOG_INFO( "VEHTEST reverse: 3 s of brake -> %.1f km/h, gear %d\n", v.getSpeedKmh(), v.getGear() );
        input.Brake = 0.0f;
        input.Throttle = 1.0f; // R2: the pedal swap brakes, then forward
        for ( int32_t i = 0; i < 6 * 60; i++ ) {
            v.step( dt, input, flat );
            if ( ( i % 30 ) == 0 ) {
                const std::vector<VehicleDynamics::WheelState> &rw = v.getWheels();
                OTDU_LOG_INFO( "VEHTEST reverse fwd t %.1f: %.1f km/h, gear %d, rpm %.0f, w0 %.1f slip %.2f brake %.2f "
                               "load0 %.0f\n",
                               i * dt, v.getSpeedKmh(), v.getGear(), v.getRpm(),
                               rw.empty() ? 0.0f : rw[0].AngularVelocity, rw.empty() ? 0.0f : rw[0].SlipRatio,
                               rw.empty() ? 0.0f : rw[0].BrakingPosition, rw.empty() ? 0.0f : rw[0].Load );
            }
        }
    }
    // Steady-state cornering: fixed steering input, throttle held by a
    // speed loop (debug driver), yaw rate / lateral acceleration after 8 s.
    for ( const float steering : { 0.25f, 0.5f, 1.0f } ) {
        for ( const float targetKmh : { 60.0f, 100.0f } ) {
            v = templateVehicle;
            v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f( 0.0f, 0.0f, targetKmh / 3.6f ) );
            input = VehicleDynamics::Input();
            float yawRate = 0.0f, lateral = 0.0f, speedSum = 0.0f;
            float rollMax = 0.0f, pitchMax = 0.0f;
            bool bFlipped = false;
            int32_t samples = 0;
            for ( int32_t i = 0; i < 12 * 60; i++ ) {
                const float kmh = v.getSpeedKmh();
                input.Steering = steering;
                input.Throttle = Clamp( ( targetKmh - kmh ) * 0.2f + 0.3f, 0.0f, 1.0f );
                input.Brake = kmh > targetKmh + 8.0f ? 0.3f : 0.0f;
                const Quaternionf before = v.getOrientation();
                v.step( dt, input, flat );
                // Body attitude in the car frame: the tilt of the car up axis
                // around its own lateral (roll) and forward (pitch) axes. A car
                // that lies on its side reads a roll near 90 deg.
                const Vector3f upAxis = v.getOrientation().rotate( Vector3f( 0.0f, 1.0f, 0.0f ) );
                const Vector3f sideAxis = v.getOrientation().rotate( Vector3f( 1.0f, 0.0f, 0.0f ) );
                const Vector3f fwdAxis = v.getOrientation().rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
                rollMax = Max( rollMax, std::abs( std::asin( Clamp( sideAxis.y, -1.0f, 1.0f ) ) * 57.29578f ) );
                pitchMax = Max( pitchMax, std::abs( std::asin( Clamp( fwdAxis.y, -1.0f, 1.0f ) ) * 57.29578f ) );
                if ( upAxis.y < 0.5f ) {
                    bFlipped = true;
                }
                if ( bLogCorner && ( i % 5 ) == 0 && i < 180 ) {
                    const std::vector<VehicleDynamics::WheelState> &w = v.getWheels();
                    OTDU_LOG_INFO( "VEHTEST corner log t %5.2f v %5.1f roll %6.1f pitch %6.1f loads %.0f %.0f %.0f %.0f "
                                   "comp %.3f %.3f %.3f %.3f slip %.2f %.2f %.2f %.2f\n",
                                   i * dt, v.getSpeedKmh(), std::asin( Clamp( sideAxis.y, -1.0f, 1.0f ) ) * 57.29578f,
                                   std::asin( Clamp( fwdAxis.y, -1.0f, 1.0f ) ) * 57.29578f,
                                   w.size() > 0 ? w[0].Load : 0.0f, w.size() > 1 ? w[1].Load : 0.0f,
                                   w.size() > 2 ? w[2].Load : 0.0f, w.size() > 3 ? w[3].Load : 0.0f,
                                   w.size() > 0 ? w[0].Compression : 0.0f, w.size() > 1 ? w[1].Compression : 0.0f,
                                   w.size() > 2 ? w[2].Compression : 0.0f, w.size() > 3 ? w[3].Compression : 0.0f,
                                   w.size() > 0 ? w[0].SlipAngle : 0.0f, w.size() > 1 ? w[1].SlipAngle : 0.0f,
                                   w.size() > 2 ? w[2].SlipAngle : 0.0f, w.size() > 3 ? w[3].SlipAngle : 0.0f );
                }
                if ( i >= 8 * 60 ) {
                    const Vector3f f0 = before.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
                    const Vector3f f1 = v.getOrientation().rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
                    const float yaw = std::atan2( f0.x * f1.z - f0.z * f1.x, f0.x * f1.x + f0.z * f1.z ) / dt;
                    yawRate += yaw;
                    lateral += yaw * v.getVelocity().length();
                    speedSum += kmh;
                    samples++;
                }
            }
            yawRate /= Max( samples, 1 );
            lateral /= Max( samples, 1 );
            OTDU_LOG_INFO( "VEHTEST corner '%s': steer %.2f target %.0f km/h -> %.1f km/h, yaw %.3f rad/s, lateral %.2f m/s2 "
                           "(%.2f g), radius %.1f m, steer angle %.2f deg, gear %d, roll max %.1f deg, pitch max %.1f deg%s\n",
                           setup.Db.FileName.c_str(), steering, targetKmh, speedSum / Max( samples, 1 ), yawRate, lateral,
                           lateral / 9.81f, std::abs( yawRate ) > 1e-3f ? v.getVelocity().length() / std::abs( yawRate ) : 0.0f,
                           v.getSteerAngle() * 57.29578f, v.getGear(), rollMax, pitchMax, bFlipped ? " FLIPPED" : "" );
        }
    }
}
} // namespace

// -vehtest: the bridge case (the owner's "clip through a bridge and back on
// road"). A bridge deck is a road surface with a gap under it: the island
// heightfield runs *under* the bridge, so the deck is the only floor the car can
// stand on, and once a chassis sphere is below it the wheel rays (which only
// look down) can no longer see it - the car free falls to the road / valley
// below. The deck of the test is two big road triangles at `height`, the terrain
// heightfield at 0, and - when bBarrier - a 0.05 m thick static box wall along
// each side (the bridge railing, the other thin surface of a bridge).
// docs/re/havok_collision_spec.md 8.
void BuildBridgeCollision( VehicleWorldCollision &collision, float height, bool bBarrier, bool bRamp ) {
    collision.setHeightFunction( []( float x, float z, float fromY, float &outHeight ) {
        (void)x;
        (void)z;
        (void)fromY;
        outHeight = 0.0f;
        return true;
    } );
    HavokCollision deck;
    // Without bRamp the deck is a flat road surface at `height`, placed by the
    // toWorld translation below. With bRamp it carries its approach ramp, which
    // is where a real bridge's road plane steps up off the island road - the
    // joint where a chassis sphere ends up on the wrong side of the plane.
    // The deck is 40 m wide: wide enough that a car launched off the ramp
    // cannot leave it sideways (only the barrier or a clip-through can).
    Matrix4x4f toWorld = Matrix4x4f::Translation( Vector3f( 0.0f, height, 0.0f ) );
    if ( bRamp ) {
        toWorld = Matrix4x4f::Identity(); // the geometry is already in world space
        deck.Vertices = { Vector3f( -20.0f, 0.0f, -20.0f ), Vector3f( 20.0f, 0.0f, -20.0f ),
                          Vector3f( 20.0f, height, 0.0f ), Vector3f( -20.0f, height, 0.0f ),
                          Vector3f( -20.0f, height, 600.0f ), Vector3f( 20.0f, height, 600.0f ) };
        deck.Indices = { 0, 1, 2, 0, 2, 3, 3, 2, 5, 3, 5, 4 };
    } else {
        deck.Vertices = { Vector3f( -20.0f, 0.0f, -40.0f ), Vector3f( 20.0f, 0.0f, -40.0f ),
                          Vector3f( 20.0f, 0.0f, 600.0f ), Vector3f( -20.0f, 0.0f, 600.0f ) };
        deck.Indices = { 0, 1, 2, 0, 2, 3 };
    }
    HavokCollision::Body body;
    body.MotionType = 7; // fixed (a .shk body)
    body.Friction = 0.7f;
    body.FirstTriangle = 0;
    body.NumTriangles = bRamp ? 4 : 2;
    body.FirstBox = 0;
    body.NumBoxes = bBarrier ? 2 : 0;
    deck.Bodies.push_back( body );
    if ( bBarrier ) {
        const float boxY = ( bRamp ? height : 0.0f ) + 0.6f;
        for ( const float x : { -7.85f, 7.85f } ) {
            HavokCollision::Box box;
            box.Transform = Matrix4x4f::Translation( Vector3f( x, boxY, 0.0f ) );
            box.HalfExtents = Vector3f( 0.025f, 0.6f, 300.0f ); // 0.05 m thick: a railing plate
            box.BodyIndex = 0;
            deck.Boxes.push_back( box );
        }
    }
    collision.addCollision( deck, toWorld, true );
}

void RunBridgeCollisionTest( const VehicleDynamics &templateVehicle ) {
    const float deck = 4.0f; // deck height, the valley floor 4 m below it
    const float dt = 1.0f / 60.0f;
    // The rule itself: a sphere that crossed the surface in one tick is pushed
    // back out through the face it entered, not through the face it is nearest
    // to (which for a deck it just passed through is the *far* one: down).
    {
        VehicleWorldCollision bridge;
        BuildBridgeCollision( bridge, deck, false, false );
        const Vector3f above( 0.0f, deck + 0.2f, 0.0f ), below( 0.0f, deck - 0.2f, 0.0f );
        VehicleWorldCollision::Contact contact;
        const bool bDown = bridge.sphereContact( below, 0.3f, contact, &above );
        OTDU_LOG_INFO( "VEHTEST bridge contact above->below: %s normal (%.2f %.2f %.2f) depth %.3f -> %s\n",
                       bDown ? "hit" : "miss", contact.Normal.x, contact.Normal.y, contact.Normal.z, contact.Depth,
                       ( bDown && contact.Normal.y > 0.9f && contact.Depth > 0.4f ) ? "PASS" : "FAIL" );
        VehicleWorldCollision::Contact under;
        const bool bUp = bridge.sphereContact( above, 0.3f, under, &below );
        OTDU_LOG_INFO( "VEHTEST bridge contact below->above: %s normal (%.2f %.2f %.2f) depth %.3f -> %s\n",
                       bUp ? "hit" : "miss", under.Normal.x, under.Normal.y, under.Normal.z, under.Depth,
                       ( bUp && under.Normal.y < -0.9f ) ? "PASS" : "FAIL" );
    }
    // A car on the deck must stay on it: throttle down the bridge.
    struct Scenario {
        const char *Name;
        Vector3f Start;
        Vector3f Velocity;
        float Pitch; // rad, nose down
        float Throttle;
        float Seconds;
        bool bBarrier;
        bool bRamp;
    };
    const Scenario scenarios[] = {
        { "drive", Vector3f( -3.0f, deck + 0.05f, -30.0f ), Vector3f( 0.0f, 0.0f, 0.0f ), 0.0f, 1.0f, 4.0f, false, false },
        { "landing", Vector3f( -3.0f, deck + 6.0f, -30.0f ), Vector3f( 0.0f, 0.0f, 20.0f ), 0.26f, 0.0f, 3.0f, false,
          false },
        { "land-hard", Vector3f( -3.0f, deck + 2.0f, -30.0f ), Vector3f( 0.0f, -10.0f, 30.0f ), 0.44f, 0.0f, 3.0f, false,
          false },
        { "barrier", Vector3f( -5.0f, deck + 0.05f, -30.0f ), Vector3f( 20.0f, 0.0f, 20.0f ), 0.0f, 0.0f, 3.0f, true,
          false },
        { "ramp", Vector3f( -3.0f, 0.05f, -60.0f ), Vector3f( 0.0f, 0.0f, 0.0f ), 0.0f, 1.0f, 8.0f, false, true },
        { "ramp-fast", Vector3f( -3.0f, 0.05f, -150.0f ), Vector3f( 0.0f, 0.0f, 40.0f ), 0.0f, 1.0f, 8.0f, false,
          true },
    };
    for ( const Scenario &scenario : scenarios ) {
        VehicleWorldCollision bridge;
        BuildBridgeCollision( bridge, deck, scenario.bBarrier, scenario.bRamp );
        VehicleDynamics v = templateVehicle;
        const Quaternionf pitch = Quaternionf::FromAxisAngle( Vector3f( 1.0f, 0.0f, 0.0f ), scenario.Pitch );
        v.reset( scenario.Start, pitch, scenario.Velocity );
        VehicleDynamics::Input input;
        input.Throttle = scenario.Throttle;
        float minY = 1e30f, maxY = -1e30f, minYDeck = 1e30f, maxX = -1e30f;
        for ( int32_t i = 0; i < static_cast<int32_t>( scenario.Seconds * 60.0f ); i++ ) {
            v.step( dt, input, bridge );
            const Vector3f p = v.getPosition();
            if ( ( i % 30 ) == 0 ) {
                const std::vector<VehicleDynamics::WheelState> &w = v.getWheels();
                OTDU_LOG_INFO( "VEHTEST bridge %s t %.1f p (%.2f %.2f %.2f) v %.1f km/h w0 c %.3f load %.0f %s\n",
                               scenario.Name, i * dt, p.x, p.y, p.z, v.getSpeedKmh(), w.empty() ? 0.0f : w[0].Compression,
                               w.empty() ? 0.0f : w[0].Load, w.empty() ? "?" : ( w[0].bContact ? "contact" : "air" ) );
            }
            minY = Min( minY, p.y );
            maxY = Max( maxY, p.y );
            maxX = Max( maxX, p.x );
            if ( p.z > 5.0f ) {
                minYDeck = Min( minYDeck, p.y );
            }
        }
        const Vector3f end = v.getPosition();
        // On the deck the car stands wheel radius + suspension travel above it;
        // a car that sank through it reads a y near the valley floor (0.3). The
        // ramp scenarios start on the island road, so only the deck part counts.
        const bool bOnDeck = end.y > deck - 0.2f && ( scenario.bRamp ? minYDeck > deck - 0.2f : minY > deck - 0.2f );
        const bool bStayed = scenario.bBarrier ? maxX < 7.5f : true;
        OTDU_LOG_INFO( "VEHTEST bridge %s: end (%.2f, %.2f, %.2f) y min %.2f (on deck %.2f) max %.2f x max %.2f -> %s\n",
                       scenario.Name, end.x, end.y, end.z, minY, minYDeck, maxY, maxX,
                       ( bOnDeck && bStayed ) ? "PASS" : "FAIL" );
    }
}

// Wheels from the database (no model bank): a car gets its four corners from
// the track / wheelbase, a bike gets its two wheels in line with no track
// (docs/re/bikes_spec.md). `setupVehicle` uses it when the model has no D_RIM_
// nodes and -vehcheck uses it for every car of the roster.
void BuildDatabaseWheels( const VehicleSetup &vehicleSetup, std::vector<VehicleDynamics::WheelDesc> &outWheels ) {
    outWheels.clear();
    // The DBRims radius is what VehicleDynamics::setup uses for the wheel; the
    // DB `_tire_radius` is only the model's rolling radius and can differ by a
    // centimetre (R8: 0.3175 vs 0.3235).
    const float radius =
        vehicleSetup.TyreFront.Radius > 0.0f ? vehicleSetup.TyreFront.Radius : vehicleSetup.Db.TireRadius;
    const float frontZ = vehicleSetup.FrontAxleLocalPos;
    const float rearZ = vehicleSetup.FrontAxleLocalPos - vehicleSetup.WheelBase;
    if ( vehicleSetup.bBike ) {
        // A bike has one wheel per axle, on the centre line (x = 0). The side
        // flag is irrelevant: there is no left / right pair.
        VehicleDynamics::WheelDesc front;
        front.bFront = true;
        front.bLeft = true;
        front.HubLocal = Vector3f( 0.0f, radius, frontZ );
        front.Radius = radius;
        outWheels.push_back( front );
        VehicleDynamics::WheelDesc rear = front;
        rear.bFront = false;
        rear.bLeft = false;
        rear.HubLocal = Vector3f( 0.0f, radius, rearZ );
        outWheels.push_back( rear );
        return;
    }
    for ( int32_t i = 0; i < 4; i++ ) {
        VehicleDynamics::WheelDesc wheel;
        wheel.bFront = i < 2;
        wheel.bLeft = ( i & 1 ) == 0;
        const float track = wheel.bFront ? vehicleSetup.TrackFront : vehicleSetup.TrackRear;
        wheel.HubLocal =
            Vector3f( ( wheel.bLeft ? 0.5f : -0.5f ) * track, radius, wheel.bFront ? frontZ : rearZ );
        wheel.Radius = radius;
        outWheels.push_back( wheel );
    }
}

// -vehcheck: the speed audit of the whole roster (docs/re/vehicle_physics_spec.md
// 7.2). For every valid DBCarPhysicsData row it loads the setup, runs a
// straight-line full-throttle run on the flat test plane and compares 0-100 and
// the top speed with _acceleration_0_100_kmh / _max_theorical_speed. The run
// stops once the speed has been stable for 2.5 s after 20 s, which keeps the
// whole roster to a few minutes; rows without a model bank (cut / placeholder
// rows) are counted and skipped. -car limits it to one car.
void RunVehicleSpeedCheck() {
    if ( gpDatabase == nullptr ) {
        return;
    }
    VehicleWorldCollision flat;
    flat.setFlatGround( true, 0.0f, 0.7f );
    const float dt = 1.0f / 60.0f;
    const char *pOnly = CmdLine::GetArgValue( "-car" );
    const std::vector<std::string> cars = gpDatabase->getCarFileNames();
    int32_t checked = 0, skipped = 0, noBank = 0;
    int32_t speedWithin = 0, accelWithin = 0;
    float worstSpeedError = 0.0f, worstAccelError = 0.0f;
    std::string worstSpeedCar, worstAccelCar;
    for ( size_t c = 0; c < cars.size(); c++ ) {
        const std::string &carName = cars[c];
        if ( pOnly != nullptr && StringCmpI( carName.c_str(), pOnly ) != 0 ) {
            continue;
        }
        VehicleSetup carSetup;
        if ( !LoadVehicleSetup( carName, carSetup ) ) {
            skipped++;
            continue;
        }
        if ( !carSetup.bHasCarBank ) {
            noBank++;
            if ( pOnly != nullptr ) {
                OTDU_LOG_WARN( "VEHCHECK '%s': no model bank (cut / placeholder row), skipped\n", carName.c_str() );
            }
            continue;
        }
        std::vector<VehicleDynamics::WheelDesc> wheels;
        BuildDatabaseWheels( carSetup, wheels );
        VehicleDynamics v;
        v.setup( carSetup, wheels );
        v.reset( Vector3f( 0.0f, 0.12f, 0.0f ), Quaternionf(), Vector3f() );
        VehicleDynamics::Input input;
        for ( int32_t i = 0; i < 60; i++ ) {
            v.step( dt, input, flat ); // settle 1 s
        }
        input.Throttle = 1.0f;
        float t100 = -1.0f, t200 = -1.0f, top = 0.0f;
        float stableTime = 0.0f;
        for ( int32_t i = 0; i < 120 * 60; i++ ) {
            v.step( dt, input, flat );
            const float t = ( i + 1 ) * dt;
            const float kmh = v.getSpeedKmh();
            if ( t100 < 0.0f && kmh >= 100.0f ) {
                t100 = t;
            }
            if ( t200 < 0.0f && kmh >= 200.0f ) {
                t200 = t;
            }
            if ( kmh > top + 0.05f ) {
                top = kmh;
                stableTime = 0.0f;
            } else {
                stableTime += dt;
            }
            if ( t > 20.0f && stableTime > 2.5f ) {
                break;
            }
        }
        checked++;
        const float dbSpeed = carSetup.MaxTheoricalSpeed;
        const float speedPercent = dbSpeed > 0.0f ? ( top - dbSpeed ) * 100.0f / dbSpeed : 0.0f;
        const float dbAccel = carSetup.Accel0To100;
        const float accelError = dbAccel > 0.0f && t100 > 0.0f ? t100 - dbAccel : 0.0f;
        if ( dbSpeed > 0.0f ) {
            speedWithin += std::abs( speedPercent ) <= 1.0f ? 1 : 0;
            if ( std::abs( speedPercent ) > std::abs( worstSpeedError ) ) {
                worstSpeedError = speedPercent;
                worstSpeedCar = carName;
            }
        }
        if ( dbAccel > 0.0f && t100 > 0.0f ) {
            accelWithin += accelError <= 0.3f ? 1 : 0;
            if ( std::abs( accelError ) > std::abs( worstAccelError ) ) {
                worstAccelError = accelError;
                worstAccelCar = carName;
            }
        }
        OTDU_LOG_INFO( "VEHCHECK '%s': 0-100 %.2f s (DB %.2f, %+.2f), 0-200 %.2f s, top %.1f km/h (DB %.0f, %+.1f %%)\n",
                       carName.c_str(), t100, dbAccel, t100 > 0.0f ? accelError : 0.0f, t200, top, dbSpeed, speedPercent );
        if ( ( checked % 25 ) == 0 ) {
            OTDU_LOG_INFO( "VEHCHECK: %d cars done\n", checked );
        }
    }
    OTDU_LOG_INFO( "VEHCHECK summary: %d cars, %d without a bank (cut rows), %d skipped; top speed within 1 %%: "
                   "%d (%+.1f %% worst '%s'), 0-100 within 0.30 s: %d (%+.2f s worst '%s')\n",
                   checked, noBank, skipped, speedWithin, worstSpeedError, worstSpeedCar.c_str(), accelWithin,
                   worstAccelError, worstAccelCar.c_str() );
}

void GMFreeRide::setupVehicle() {
    LoadVehicleSetup( carDesc.FileName, setup );
    std::vector<VehicleDynamics::WheelDesc> wheels;
    wheelNodes.clear();
    if ( car.isLoaded() ) {
        const EdenModel &model = car.getModel();
        const std::vector<SceneNode> &nodes = model.getScene().getNodes();
        for ( size_t i = 0; i + 1 < nodes.size(); i++ ) {
            if ( nodes[i].Name.rfind( "D_RIM_", 0 ) != 0 ) {
                continue;
            }
            WheelNode node;
            node.RimNode = static_cast<int32_t>( i );
            node.RimRest = model.getNodeTransform( node.RimNode );
            node.TireRest = model.getNodeTransform( node.RimNode + 1 );
            const Vector3f hub = node.RimRest.getTranslation();
            node.bFront = hub.z > 0.0f; // car frame: +z forward, +x left
            node.bLeft = hub.x > 0.0f;
            wheelNodes.push_back( node );
            VehicleDynamics::WheelDesc wheel;
            // The physics hub is the model hub in the body frame, which sits
            // the model lift under the model origin (CinematicCar::
            // getBodyLift): the drawn wheel and the ray wheel are the same
            // wheel, at the tire radius above the contact. For a bike the two
            // nodes are in line (x = 0) and fitOnGround rests the two tires on
            // the road [INF].
            wheel.HubLocal = hub + Vector3f( 0.0f, car.getBodyLift(), 0.0f );
            wheel.bFront = node.bFront;
            wheel.bLeft = node.bLeft;
            wheel.Radius = carDesc.TireRadius;
            wheels.push_back( wheel );
            OTDU_LOG_INFO( "FREERIDE: wheel %s hub (%.3f, %.3f, %.3f) %s %s\n", nodes[i].Name.c_str(), hub.x, hub.y,
                           hub.z, wheel.bFront ? "front" : "rear", wheel.bLeft ? "left" : "right" );
        }
    }
    if ( wheels.empty() ) {
        // No model (or a bike): wheels from the database (track, wheelbase;
        // two in line for a bike) [INF].
        BuildDatabaseWheels( setup, wheels );
    }
    vehicle.setModelLift( car.isLoaded() && !wheelNodes.empty() ? car.getBodyLift() : 0.0f );
    // The tuning shop levels of this car modulate the base setup; the member
    // `setup` stays the base, so applyTuning() (a live re-apply) can never
    // compound the scaling.
    {
        const TuningLevels levels = ProfileTuningLevels( car.getIdCar() );
        VehicleSetup tunedSetup = setup;
        ApplyTuningToSetup( tunedSetup, levels.Accel, levels.Braking, levels.Speed );
        vehicle.setup( tunedSetup, wheels );
    }
    // Another car: the previous one's dust, tracks and disc heat are not this
    // one's (setupVehicle runs once per car - enter(), the -switchcar change).
    if ( gpRender != nullptr ) {
        gpRender->getSurfaceFx().clear();
    }
    // The tank of this car (docs/re/fuel_gas_station_spec.md): capacity from its
    // DBCarPhysicsData row, -fuel to force a level for a test.
    InitFuelTank( setup.Db );
    fuelOdometer = 0.0f;
    bFuelEmpty = false;
    vehicle.setLogContacts( CmdLine::HasArg( "-logcontacts" ) );
    if ( CmdLine::HasArg( "-vehtest" ) ) {
        RunVehicleTest( vehicle );
        RunBridgeCollisionTest( vehicle );
    }
    // -vehcheck: the speed audit of the whole roster (0-100 / top speed against
    // the DB; docs/re/vehicle_physics_spec.md 7.2). -car limits it to one car.
    if ( CmdLine::HasArg( "-vehcheck" ) ) {
        RunVehicleSpeedCheck();
    }
}

Matrix4x4f GMFreeRide::outroCarWorld() {
    Vector3f position, velocity;
    Quaternionf orientation;
    if ( !outro.getCarPose( position, orientation, velocity ) ) {
        return carWorld;
    }
    // The .sce track velocity: the Outro tick integrates it into the wheel spin
    // (the physics is not stepped while the cinematic owns the car).
    outroVelocity = velocity;
    if ( !car.isLoaded() ) {
        return Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( orientation );
    }
    // The cinematic pose rests on the surface (Havok road / spot collision).
    return car.fitOnGround( position, Conjugate( orientation ),
                            [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
}

float GMFreeRide::outroGearRatio( int32_t index ) const {
    // VehicleDynamics::gearRatio from the setup: Ratio[0] = -reverse,
    // Ratio[1] = 0 (neutral), Ratio[2+i] = Gears[i], Ratio[9] = 1, x the
    // final drive (vehicle_dynamics.cpp 200 / 255).
    if ( index < 0 || index > 9 ) {
        return 0.0f;
    }
    float ratio = 0.0f;
    if ( index == 0 ) {
        ratio = setup.ReverseRatio;
    } else if ( index == 9 ) {
        ratio = 1.0f;
    } else if ( index >= 2 && index - 2 < static_cast<int32_t>( setup.Gears.size() ) ) {
        ratio = setup.Gears[index - 2];
    }
    return ratio * setup.FinalDrive;
}

void GMFreeRide::startDriving() {
    // End of C_FIRST_OUTRO (CinEnd 3: end pose; listener event 3): the car
    // keeps the last cinematic pose and the track velocity [INF, hand-off
    // ENC], the game camera returns (FUN_00ca18b0).
    Vector3f position, velocity;
    Quaternionf orientation;
    if ( bTp ) {
        // -tp x,z[,y] + -heading <deg>: parked at the requested spot, on the
        // ground (the tire fit also picks up the road camber).
        // -snaproad [radius m]: moves the spot to the nearest road triangle
        // (-R.shk) within the radius and heads along it, to tour the roads
        // without knowing their coordinates (touring aid).
        if ( CmdLine::HasArg( "-snaproad" ) ) {
            const float radius = static_cast<float>( CmdLine::GetArgInt( "-snaproad", 600 ) );
            float bestDistance = 1e30f;
            Vector3f bestCentre, bestEdge;
            collision.forEachRoadTriangle( tpPosition, radius, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
                const Vector3f centre = ( a + b + c ) * ( 1.0f / 3.0f );
                const float dx = centre.x - tpPosition.x, dz = centre.z - tpPosition.z;
                const float distance = dx * dx + dz * dz;
                if ( distance < bestDistance ) {
                    bestDistance = distance;
                    bestCentre = centre;
                    const Vector3f e0 = b - a, e1 = c - b, e2 = a - c;
                    bestEdge = e0;
                    if ( e1.length() > bestEdge.length() ) {
                        bestEdge = e1;
                    }
                    if ( e2.length() > bestEdge.length() ) {
                        bestEdge = e2;
                    }
                }
            } );
            if ( bestDistance < 1e29f ) {
                tpPosition = Vector3f( bestCentre.x, bestCentre.y, bestCentre.z );
                tpYaw = std::atan2( bestEdge.x, bestEdge.z );
                OTDU_LOG_INFO( "FREERIDE: -snaproad -> (%.1f, %.1f, %.1f), heading %.1f deg\n", tpPosition.x, tpPosition.y,
                               tpPosition.z, tpYaw * 57.29578f );
            } else {
                OTDU_LOG_WARN( "FREERIDE: -snaproad found no road triangle within %.0f m\n", radius );
            }
        }
        const Quaternionf yaw( 0.0f, std::sin( tpYaw * 0.5f ), 0.0f, std::cos( tpYaw * 0.5f ) );
        const Matrix4x4f fitted =
            car.isLoaded()
                ? car.fitOnGround( tpPosition, Conjugate( yaw ),
                                   [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } )
                : Matrix4x4f::Translation( tpPosition ) * Matrix4x4f::FromQuaternion( yaw );
        position = fitted.getTranslation();
        orientation = QuaternionFromMatrix( fitted );
        velocity = Vector3f();
    } else if ( outro.isLoaded() && outro.getCarPose( outro.getDuration(), position, orientation, velocity ) ) {
        const Matrix4x4f fitted =
            car.isLoaded()
                ? car.fitOnGround( position, Conjugate( orientation ),
                                   [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } )
                : Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( orientation );
        position = fitted.getTranslation();
        orientation = QuaternionFromMatrix( fitted );
    } else {
        position = outro.getHousePosition();
    }
    if ( CmdLine::HasArg( "-startstopped" ) ) {
        velocity = Vector3f();
    }
    collision.streamAround( position, 1 );
    // A bike's two-wheel ground fit needs the collision world (the plane fit of
    // the cars happens above, but the sectors around the start were not loaded
    // yet): fit once more now that they are, so the tires start on the road and
    // the ray suspension finds contact [INF].
    if ( car.isLoaded() && setup.bBike ) {
        const Matrix4x4f refit =
            car.fitOnGround( position, Conjugate( orientation ),
                             [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
        position = refit.getTranslation();
        orientation = QuaternionFromMatrix( refit );
    }
    // position is the model origin (fitOnGround): the body frame is the model
    // lift under it.
    vehicle.reset( position + Matrix4x4f::FromQuaternion( orientation ).transformVector(
                                  Vector3f( 0.0f, 0.05f - vehicle.getModelLift(), 0.0f ) ),
                   orientation, velocity );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( position, Matrix4x4f::FromQuaternion( orientation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    // Step 2 tick at FREERIDE without spot cinematic: /:IsWalkmode false,
    // pop_goal 0x13ae (5038 "Enjoy!"), TUTO_ENJOY, roof open, timer 90 s.
    hud.setVisible( true );
    if ( !vehicleAudio.isEngineOn() ) {
        // Already running when C_FIRST_OUTRO just played (the outro engine
        // starts with the drive-away, the Outro tick): no restart one-shot.
        vehicleAudio.startEngine();
    }
    if ( bFromDream ) {
        // Story step 3: drive Tess to the Solar Crown. The destination is set on
        // the GPS [INF: the retail target is not decoded; kSolarCrownTarget].
        hud.showGoal( LocStr( 6111, "On the road to \"Solar Crown\"" ) );
        mapScreen.tickRoute( position, 0.0f );
        mapScreen.setTarget( kSolarCrownTarget );
        OTDU_LOG_INFO( "FREERIDE: from the dream: story step %d, GPS to the Solar Crown (%.0f, %.0f), route %.0f m\n",
                       GetPlayerProfile().StoryStep, kSolarCrownTarget.x, kSolarCrownTarget.z, mapScreen.getRouteLength() );
    } else if ( GetPlayerProfile().StoryStep == 9 ) {
        // Story step 9 "Go Home" (FUN_00b10da0's step table, docs/re/
        // tess_garage_spec.md 1.2): the retail beat is the drive back to the
        // caravan, and step 10 "Home sweet home : Caravan" hands it over. The
        // port has no mission for it, so the goal is the GPS route to the
        // caravan's own entrance point and arriving there advances the step
        // (the tick's takeArrival below).
        Vector3f caravanEntrance;
        float caravanHeading = 0.0f;
        if ( FindSpotEntrance( islandName, "EPH_L0C_1254I", caravanEntrance, caravanHeading ) ) {
            hud.showGoal( "Go Home" ); // [DATA] the step's own name
            mapScreen.tickRoute( position, 0.0f );
            mapScreen.setTarget( caravanEntrance );
            OTDU_LOG_INFO( "FREERIDE: story step 9 'Go Home': GPS to the caravan (%.0f, %.0f), route %.0f m\n",
                           caravanEntrance.x, caravanEntrance.z, mapScreen.getRouteLength() );
        } else {
            OTDU_LOG_WARN( "FREERIDE: story step 9: the caravan 'EPH_L0C_1254I' is not in the spots of '%s'\n",
                           islandName.c_str() );
        }
    } else {
        hud.showGoal( LocStr( 5038, "Enjoy!" ) );
    }
    stepTimer = firstFreerideTime;
    state = eState::Drive;
    setShadowConfig( "FREERIDE" );
    OTDU_LOG_INFO( "FREERIDE: driving from (%.2f, %.2f, %.2f) at %.1f km/h, story timer %.0f s\n", position.x, position.y,
                   position.z, velocity.length() * 3.6f, stepTimer );
}

void GMFreeRide::startDream() {
    // The white flash covers the switch: C_MEETING_TESS ("thedream", in front of
    // the seafront hotel), M or W by the player avatar, then FREERIDE again at
    // story step 3 (gm_cin_viewer: StoryHandoff). -nodream keeps the old cut.
    const std::string &avatar = GetPlayerProfile().AvatarConfig;
    const bool bWoman = avatar.find( "Female" ) != std::string::npos;
    CinViewerRequest &request = GetCinViewerRequest();
    request = CinViewerRequest();
    request.Bank = "scenarics/cinematics/intro/meeting_tess.bnk";
    request.Cinematic = bWoman ? "C_MEETING_TESS_W" : "C_MEETING_TESS";
    request.Next = eGameMode::GM_FreeRide;
    request.StoryStepAfter = 3;
    request.bFromStory = true;
    request.bStory = true; // the return keeps the story goal (the Solar Crown GPS)
    OTDU_LOG_INFO( "FREERIDE: white fade done -> %s (avatar '%s')\n", request.Cinematic.c_str(), avatar.c_str() );
    pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
}

void GMFreeRide::startKnock() {
    // C_FADE_FREERIDE over the gameplay (retail order): the car brakes to a
    // stop where it is, Tess appears at the driver window, the knock and
    // the first lines play with the game camera and the HUD still up. The
    // white fade follows and hides the move to the house.
    wakeup.anchorToCar( carWorld );
    wakeup.start();
    state = eState::Knock;
    const Vector3f p = carWorld.getTranslation();
    OTDU_LOG_INFO( "FREERIDE: %.0f s elapsed -> C_FADE_FREERIDE knock scene at (%.2f, %.2f, %.2f)\n", firstFreerideTime,
                   p.x, p.y, p.z );
}

void GMFreeRide::startWakeup() {
    // C_FADE_FREERIDE starts under the white: the car is parked beside Tess
    // ([INF] pose, kWakeupParkLocal) and the physics stops until CinEnd 3.
    const Quaternionf parkYaw( 0.0f, std::sin( kWakeupParkYaw * 0.5f ), 0.0f, std::cos( kWakeupParkYaw * 0.5f ) );
    const Quaternionf worldHeading = QuaternionFromMatrix( wakeup.getSceneOrigin() ) * parkYaw;
    const Vector3f position = wakeup.getSceneOrigin().transformPoint( kWakeupParkLocal );
    const Matrix4x4f fitted =
        car.isLoaded()
            ? car.fitOnGround( position, Conjugate( worldHeading ),
                               [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } )
            : Matrix4x4f::Translation( position ) * Matrix4x4f::FromQuaternion( worldHeading );
    carWorld = fitted;
    // The drive may have taken the car anywhere: stream the park area.
    collision.streamAround( carWorld.getTranslation(), 1 );
    streamWorld( carWorld.getTranslation() );
    hud.setVisible( false );
    // Placeholder for C_MEETING_TESS ("thedream", meeting_tess.bnk, the
    // retail post-flash scene): the knock cinematic replays muted at the
    // house entity placement, with the .sce camera, under the shot's fade
    // in from white.
    wakeup.anchorToHouse();
    wakeup.start( 0.0f, false );
    state = eState::Wakeup;
    const Vector3f p = carWorld.getTranslation();
    OTDU_LOG_INFO( "FREERIDE: white fade done, wake-up scene (C_MEETING_TESS placeholder), car parked at (%.2f, %.2f, %.2f)\n",
                   p.x, p.y, p.z );
}

void GMFreeRide::finishWakeup() {
    // CinEnd 3: end pose kept. Story step 3 "Meeting Tess" is not
    // implemented: the freeride resumes from the parked pose with its goal
    // text (loc 6111 'On the road to "Solar Crown"' [INF: the step-3 goal;
    // the mission itself - GPS target, Tess passenger, arrival - is next]).
    GetPlayerProfile().StoryStep = 3;
    SaveProfile( "story step 3" );
    const Vector3f position = carWorld.getTranslation();
    const Quaternionf orientation = QuaternionFromMatrix( carWorld );
    vehicle.reset( position + Matrix4x4f::FromQuaternion( orientation ).transformVector(
                                  Vector3f( 0.0f, 0.05f - vehicle.getModelLift(), 0.0f ) ),
                   orientation, Vector3f() ); // the scene's model pose -> the body frame
    chase.reset( position, carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    hud.setVisible( true );
    hud.showGoal( LocStr( 6111, "On the road to \"Solar Crown\"" ) );
    if ( !vehicleAudio.isEngineOn() ) {
        vehicleAudio.startEngine();
    }
    setShadowConfig( "FREERIDE" );
    state = eState::Drive;
    OTDU_LOG_INFO( "FREERIDE: wake-up done -> story step 3 'Meeting Tess', goal 'On the road to Solar Crown' (mission not implemented), freeride resumes\n" );
}

void GMFreeRide::backOnTrack() {
    // BACK_ON_TRACK (pad B / circle, R): teleport the car onto the closest
    // road triangle around it, aligned with the road.
    const Vector3f position = carWorld.getTranslation();
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f flatForward = Vector3f( forward.x, 0.0f, forward.z ).normalized();
    Vector3f bestPoint( 0.0f, 0.0f, 0.0f );
    Vector3f bestDir( 0.0f, 0.0f, 0.0f );
    float bestDistance = 1e30f;
    bool bFound = false;
    collision.forEachRoadTriangle(
        position, 150.0f, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
            const Vector3f centroid = ( a + b + c ) * ( 1.0f / 3.0f );
            if ( fabsf( centroid.y - position.y ) > 30.0f ) {
                return; // a bridge / tunnel above or below
            }
            // Closest point of the triangle to the car, in the triangle
            // plane (Real-Time Collision Detection 5.1.5).
            const Vector3f ab = b - a, ac = c - a, ap = position - a;
            const float d1 = ab.dot( ap ), d2 = ac.dot( ap );
            Vector3f point;
            if ( d1 <= 0.0f && d2 <= 0.0f ) {
                point = a;
            } else {
                const Vector3f bp = position - b;
                const float d3 = ab.dot( bp ), d4 = ac.dot( bp );
                if ( d3 >= 0.0f && d4 <= d3 ) {
                    point = b;
                } else {
                    const float vc = d1 * d4 - d3 * d2;
                    if ( vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f ) {
                        point = a + ab * ( d1 / ( d1 - d3 ) );
                    } else {
                        const Vector3f cp = position - c;
                        const float d5 = ab.dot( cp ), d6 = ac.dot( cp );
                        if ( d6 >= 0.0f && d5 <= d6 ) {
                            point = c;
                        } else {
                            const float vb = d5 * d2 - d1 * d6;
                            if ( vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f ) {
                                point = a + ac * ( d2 / ( d2 - d6 ) );
                            } else {
                                const float va = d3 * d6 - d5 * d4;
                                if ( va <= 0.0f && ( d4 - d3 ) >= 0.0f && ( d5 - d6 ) >= 0.0f ) {
                                    point = b + ( c - b ) * ( ( d4 - d3 ) / ( ( d4 - d3 ) + ( d5 - d6 ) ) );
                                } else {
                                    const float denominator = 1.0f / ( va + vb + vc );
                                    const Vector3f v = ab * vb * denominator, w = ac * vc * denominator;
                                    point = a + v + w;
                                }
                            }
                        }
                    }
                }
            }
            const Vector3f flat( point.x - position.x, 0.0f, point.z - position.z );
            const float distance = flat.length();
            if ( distance < bestDistance ) {
                bestDistance = distance;
                bestPoint = point;
                // The road direction is the longest edge of the triangle
                // (roads are strips), oriented with the current heading.
                Vector3f edge = b - a;
                const Vector3f e1 = c - b, e2 = a - c;
                if ( e1.length() > edge.length() ) {
                    edge = e1;
                }
                if ( e2.length() > edge.length() ) {
                    edge = e2;
                }
                Vector3f dir = Vector3f( edge.x, 0.0f, edge.z ).normalized();
                if ( dir.dot( flatForward ) < 0.0f ) {
                    dir = -dir;
                }
                bestDir = dir;
                bFound = true;
            }
        } );
    if ( !bFound || bestDistance > 150.0f ) {
        OTDU_LOG_WARN( "FREERIDE: back on track: no road within 150 m\n" );
        return;
    }
    const Quaternionf orientation =
        Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), atan2f( bestDir.x, bestDir.z ) );
    vehicle.reset( bestPoint + Vector3f( 0.0f, 0.1f, 0.0f ), orientation, bestDir * 5.0f );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( bestPoint, bestDir );
    OTDU_LOG_INFO( "FREERIDE: back on track (%.1f m)\n", bestDistance );
}

bool GMFreeRide::projectToCanvas( const Vector3f &world, float &outX, float &outY, float &outDistance ) const {
    // The game camera of the frame: the same projection setupScene builds
    // (EdenEffectiveFov: the fov_scale option and the retail narrow-aspect
    // law), mapped NDC -> window fraction -> canvas through the UI layout
    // rect. Markers land on their spots at every window aspect, not just
    // 16:9.
    const Quaternionf inverse = Conjugate( camera.Rotation );
    const Vector3f forward = inverse.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f right = inverse.rotate( Vector3f( 1.0f, 0.0f, 0.0f ) );
    const Vector3f up = inverse.rotate( Vector3f( 0.0f, 1.0f, 0.0f ) );
    const Vector3f d = world - camera.Position;
    const float z = d.dot( forward );
    if ( z < 1.0f ) {
        return false;
    }
    float windowWidth = 1280.0f, windowHeight = 720.0f;
    if ( gpRender != nullptr ) {
        windowWidth = static_cast<float>( gpRender->getDevice().getBackbufferWidth() );
        windowHeight = static_cast<float>( gpRender->getDevice().getBackbufferHeight() );
    }
    const float aspect = windowWidth / Max( 1.0f, windowHeight );
    const float tanHalf = std::tan( EdenEffectiveFov( camera.Fov, camera.FovScale, aspect ) * 0.5f );
    const float ndcX = d.dot( right ) / ( z * tanHalf * aspect );
    const float ndcY = d.dot( up ) / ( z * tanHalf );
    const float fx = ( ndcX + 1.0f ) * 0.5f;
    const float fy = ( 1.0f - ndcY ) * 0.5f;
    float cx0 = 0.0f, cy0 = 0.0f, cw = 1.0f, ch = 1.0f;
    GetUiCanvasRect( windowWidth, windowHeight, cx0, cy0, cw, ch );
    outX = ( ( fx - cx0 ) / Max( cw, 1e-6f ) ) * 1280.0f;
    outY = ( ( fy - cy0 ) / Max( ch, 1e-6f ) ) * 720.0f;
    outDistance = z;
    return true;
}

void GMFreeRide::tickWorldSpots( float deltaTime, const Vector3f &carPosition, float speedKmh ) {
    // The spot kinds of the HUD element (HUD.vmf /HUD/enter/Spot frame labels
    // [DATA]) for the building categories of spots.bin [INF: same kinds as
    // the map spot icons].
    const float nearRadius = 60.0f;   // the enter element shows (and A works)
    const float markerRadius = 300.0f; // distant markers show
    std::string nearName;
    Vector3f nearPosition;
    const char *pNearKind = nullptr;
    const char *pNearAction = nullptr;
    // The world position the enter element anchors to (the retail element
    // follows the spot's projected screen position, like the distant markers).
    Vector3f nearSpotPosition = carPosition;

    // Return to road: shown after 2 s off the road network (the enter
    // element's offroad tile with loc 50 [DATA: HUD.vmf /HUD/enter/Spot/offroad;
    // loc 50 'Return to the road' [DATA: db_us]]). -offroadhud forces it (a
    // test hook, like -trafficbrake: the island's roads leave few off-road
    // places to reach headlessly).
    static const bool bForceOffRoad = CmdLine::HasArg( "-offroadhud" );
    bool bOnRoad = false;
    collision.forEachRoadTriangle( carPosition, 12.0f, [&]( const Vector3f &a, const Vector3f &b, const Vector3f &c ) {
        if ( fabsf( ( a + b + c ).y * ( 1.0f / 3.0f ) - carPosition.y ) < 30.0f ) {
            bOnRoad = true;
        }
    } );
    static float offRoadTime = 0.0f;
    static bool bOffRoadShown = false;

    // The nearest spot that can be walked into (any kind with an interior
    // bank: dealerships, clothes shops, hairdressers, clubs ... the HUD tile
    // labels are the map icon names [DATA: -dumpsprite 227]); -nointeriors:
    // only the dealerships and the clothes shops, whose menus open from the
    // car. A spot at hand wins over the off road indicator (the shop parking
    // lots are off the road triangles).
    const bool bSlow = std::fabs( speedKmh ) < 20.0f;
    static const bool bInteriors = !CmdLine::HasArg( "-nointeriors" );
    const char *pSpotIcon = nullptr;
    uint64_t nearSpotId = 0;
    // A house spot is enterable only when it is one of yours (the others are
    // for sale: the realtor sells them); the other spots when they have an
    // interior bank. The car washes (ECW_*, icw_a exists) are not walked into:
    // their scene is the washing cutscene (the CarWash hook below), not a room.
    const auto enterable = [&]( const std::string &name, uint64_t id ) {
        if ( !bInteriors ) {
            return true;
        }
        if ( name.compare( 0, 3, "ECW" ) == 0 ) {
            return false;
        }
        if ( name.compare( 0, 3, "EPH" ) == 0 ) {
            return IsHouseOwned( id ) && !InteriorWalk::HouseBankFor( name, false ).empty();
        }
        return !InteriorWalk::InteriorBankFor( name ).empty();
    };
    if ( mapScreen.findNearestSpot( carPosition, nearRadius, enterable, nearName, pSpotIcon, &nearPosition,
                                    &nearSpotId ) &&
         ( bInteriors || strcmp( pSpotIcon, "clothes" ) == 0 || strcmp( pSpotIcon, "cardealer" ) == 0 ) ) {
        // The house law is the map's own (an owned EPH house is 'home', the
        // ones on sale 'estates') — the tile art and the label follow it.
        if ( strcmp( pSpotIcon, "estates" ) == 0 && nearName.compare( 0, 3, "EPH" ) == 0 &&
             IsHouseOwned( nearSpotId ) ) {
            pSpotIcon = "home";
        }
        pNearKind = pSpotIcon;
        pNearAction = LocStr( 1703, "Enter" );
        nearSpotPosition = nearPosition;
    }
    // The nearest race start (the challenge marker; the retail hint loc 2220
    // shows beside it).
    int32_t nearRace = -1;
    if ( pNearKind == nullptr && raceEvents.isLoaded() ) {
        nearRace = raceEvents.findStartNear( carPosition, nearRadius * 1.5f );
        if ( nearRace >= 0 ) {
            pNearKind = "event";
            pNearAction = LocStr( 1703, "Enter" );
            nearSpotPosition = raceEvents.getStarts()[nearRace].Position;
        }
    }
    // A championship's start (its first race's start gate): the tile of its
    // family (asphalt / dirt / classic / cup, "_h" for the high one).
    const ChampionshipSpot *pNearChampionship = nullptr;
    if ( pNearKind == nullptr ) {
        float best = 40.0f;
        for ( const ChampionshipSpot &spot : championshipSpots ) {
            const float d = Vector3f( spot.Position.x - carPosition.x, 0.0f, spot.Position.z - carPosition.z ).length();
            if ( d < best ) {
                best = d;
                pNearChampionship = &spot;
            }
        }
        if ( pNearChampionship != nullptr ) {
            pNearKind = pNearChampionship->Family.c_str();
            pNearAction = LocStr( 1703, "Enter" );
            nearSpotPosition = pNearChampionship->Position;
        }
    }
    // The nearest car wash (docs/re/car_wash_spec.md): the 'carwash' tile of the
    // same enter element, the retail hint 3913 once per approach. It loses to
    // the spots / races / championships above (a wash beside one of those keeps
    // that one's prompt) and shows within 35 m - the fuel-station distance, not
    // the 60 m of the walkable spots, because validate here pays and switches
    // the mode. -nowash off.
    static const bool bWashEnabled = !CmdLine::HasArg( "-nowash" );
    static const bool bLogWash = CmdLine::HasArg( "-logwash" );
    const CarWashSpot *pNearWash = nullptr;
    float nearWashDistance = 0.0f;
    if ( bWashEnabled && pNearKind == nullptr && carWash.isLoaded() ) {
        pNearWash = carWash.findNear( carPosition, 35.0f, nearWashDistance );
        if ( pNearWash != nullptr ) {
            pNearKind = "carwash";
            pNearAction = LocStr( 1703, "Enter" );
            nearSpotPosition = pNearWash->Position;
        }
    }
    if ( bLogWash && carWash.isLoaded() ) {
        static float washLogTimer = 0.0f;
        washLogTimer += deltaTime;
        if ( washLogTimer >= 1.0f ) {
            washLogTimer = 0.0f;
            float distance = 0.0f;
            const CarWashSpot *pSpot = carWash.findNear( carPosition, 1e6f, distance );
            if ( pSpot != nullptr ) {
                OTDU_LOG_INFO( "CarWash: nearest '%s' at (%.0f, %.0f), %.1f m%s\n", pSpot->Bank.c_str(),
                               pSpot->Position.x, pSpot->Position.z, distance,
                               pNearWash != nullptr ? ", prompt shown" : "" );
            }
        }
    }
    // The retail approach hint (loc 3913), once per approach like the race hint.
    static bool bWashHinted = false;
    if ( pNearWash != nullptr ) {
        if ( !bWashHinted && !hud.isHintActive() ) {
            bWashHinted = true;
            hud.showHint( LocStr( 3913, "Is your vehicle dirty? You'll be able to get it washed here and really make it shine!" ),
                          7.0f );
        }
    } else {
        bWashHinted = false;
    }

    // The return to road indicator (/HUD/BOT, docs/re/backontrack_ui_findings.md):
    // shown off the road network after 2 s; the refused press (the reset
    // cooldown) shows it at once with its red X.
    backOnTrackCooldown = Max( backOnTrackCooldown - deltaTime, 0.0f );
    const bool bShowBadge = ( !bOnRoad || bForceOffRoad ) && pNearKind == nullptr;
    if ( bShowBadge ) {
        offRoadTime += deltaTime;
        if ( offRoadTime > 2.0f && !bOffRoadShown ) {
            bOffRoadShown = true;
            OTDU_LOG_INFO( "FREERIDE: off the road network: return to road indicator shown\n" );
        }
    } else {
        offRoadTime = 0.0f;
        bOffRoadShown = false;
    }

    // The enter element: the retail element follows the spot's projected
    // screen position (the same canvas placement the distant markers use —
    // the element's own 'stop' arrangement carries the tile and the prompt
    // bar), the icon art swapped per spot kind, the scale shrinking with the
    // distance like the markers'. Nothing while a race runs (the countdown
    // and the race HUD own the screen).
    const bool bRaceUp = raceEvents.getStage() != RaceEvents::eStage::Idle || licenseTest.isActive();
    const auto spotCanvas = [&]( const Vector3f &position, float &outX, float &outY, float &outScale ) {
        outX = 640.0f;
        outY = 400.0f;
        outScale = 100.0f;
        float x = 0.0f, y = 0.0f, distance = 1.0f;
        const Vector3f anchor( position.x, position.y + 7.0f, position.z );
        if ( projectToCanvas( anchor, x, y, distance ) ) {
            outX = x;
            outY = y;
            outScale = Clamp( 100.0f * 40.0f / Max( distance, 10.0f ), 25.0f, 100.0f );
        }
    };
    if ( pNearKind != nullptr && !hud.isLevelUpActive() && !bRaceUp ) {
        bOffRoadShown = false;
        float x = 0.0f, y = 0.0f, scale = 100.0f;
        spotCanvas( nearSpotPosition, x, y, scale );
        hud.showEnterSpot( pNearKind, bSlow ? pNearAction : "", x, y, scale );
    } else if ( ( bOffRoadShown || bBackOnTrackHeld ) && !bRaceUp && !hud.isLevelUpActive() &&
                !CmdLine::HasArg( "-botforce" ) ) {
        // (Not under the level-up / reward popup: its translucent panel let
        // the ring and "Return to the road" show through the middle of it -
        // GLM's build-459 race-finish capture. The enter-spot prompt above and
        // the spec card below already step aside for it.)
        // The retail presentation (the owner's captures): /HUD/BOT at a FIXED
        // top-centre position - the sky/road fill inside the ring, the down
        // arrow, the label under it [INF: the retail driver is encrypted; the
        // element, the arts and loc 50 are [DATA], the position from the 4K
        // capture]. A hold sweeps the fill; a refused press (the reset
        // cooldown) shows the red X, which the movie closes by itself
        // (ic f40 _parent.gotoAndPlay("close")).
        float progress = 1.0f;
        if ( bBackOnTrackHeld && !bBackOnTrackRejected ) {
            progress = bBackOnTrackDone ? 1.0f : backOnTrackHold / kBackOnTrackHoldTime;
        }
        hud.showBackOnTrack( progress, bBackOnTrackHeld && bBackOnTrackRejected );
    } else {
        hud.closeEnterSpot();
        if ( !CmdLine::HasArg( "-botforce" ) ) {
            hud.showBackOnTrack( -1.0f );
        }
    }

    // The near-car spec card: a traffic car within ~9 m while slowed down -
    // the lowest priority (nothing else near, no race / popup: the card is
    // informational, there is no action on it). The stats of the agent's type
    // come from its DBCarPhysicsData row (CachedSpecCard reads them once).
    {
        int32_t nearAgent = -1;
        if ( pNearKind == nullptr && bSlow && !bRaceUp && !hud.isLevelUpActive() && !bOffRoadShown ) {
            float nearCarDistance = 0.0f;
            nearAgent = traffic.findNearestAgent( carPosition, 9.0f, nearCarDistance );
        }
        if ( nearAgent >= 0 ) {
            const SpecCardData *pCard = CachedSpecCard( traffic.getAgentCarId( nearAgent ) );
            if ( pCard != nullptr ) {
                hud.showCarSpecCard( pCard->Name, pCard->Price, pCard->Spec );
            } else {
                hud.closeCarSpecCard();
            }
        } else {
            hud.closeCarSpecCard();
        }
    }

    // A rival on the road (Traffic's story drivers): the name and the car,
    // once per meeting, when it comes within 30 m [INF: the retail name tag
    // above the car is not decoded; the HUD hint line stands in].
    {
        static std::string lastRival;
        static float rivalCooldown = 0.0f;
        rivalCooldown = Max( 0.0f, rivalCooldown - deltaTime );
        std::string driver, carName;
        float rivalDistance = 0.0f;
        if ( !bRaceUp && traffic.findNearestRival( carPosition, 30.0f, driver, carName, rivalDistance ) ) {
            if ( driver != lastRival || rivalCooldown <= 0.0f ) {
                hud.showHint( driver + "  -  " + carName, 4.0f );
                OTDU_LOG_INFO( "FREERIDE: rival %s (%s) passes at %.0f m\n", driver.c_str(), carName.c_str(), rivalDistance );
            }
            lastRival = driver;
            rivalCooldown = 45.0f;
        }
    }

    // A gas station of our own (a `fuel` prop, docs/re/modding.md 3): stopped next
    // to it, validate opens the retail pump screen (loc 1635..1645). The prompt is
    // the HUD's one-line bar, so it cannot fight the enter-spot element above.
    {
        std::string stationPrompt;
        const ModProps::Prop *pStation = bRaceUp ? nullptr : modProps.findFuelStation( vehicle.getPosition(), 35.0f );
        if ( pStation != nullptr && !fuelMenu.isOpen() ) {
            // The pump is a stop, like the shops: rolling past at speed does not
            // open the screen (the prompt still names the station so the player
            // knows to pull in).
            stationPrompt = std::string( LocStr( 1642, "Fill" ) ) + ": " + pStation->Name;
            if ( bSlow && gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
                fuelMenu.open( pStation->Name );
            }
        }
        hud.setPrompt( stationPrompt );
    }

    // The distant markers: race starts and shops around the car, plus the
    // next checkpoint of a running race.
    FreeRideHud::WorldMarker markers[FreeRideHud::kMaxWorldMarkers];
    int32_t numMarkers = 0;
    // bShop: the shop / house tiles (the owner's retail capture: a house down
    // the road reads ~120 canvas px wide, floating well above the building)
    // [INF: size and height read off that one capture; the race markers keep
    // the earlier small law].
    auto pushMarker = [&]( const char *pKind, const Vector3f &position, int32_t logo = -1, bool bShop = false ) {
        if ( numMarkers >= FreeRideHud::kMaxWorldMarkers ) {
            return;
        }
        float x = 0.0f, y = 0.0f, distance = 1.0f;
        const Vector3f anchor( position.x, position.y + ( bShop ? 14.0f : 7.0f ), position.z );
        if ( !projectToCanvas( anchor, x, y, distance ) || distance > markerRadius * 2.0f ) {
            return;
        }
        if ( x < -80.0f || x > 1360.0f || y < -80.0f || y > 800.0f ) {
            return;
        }
        markers[numMarkers].pSpotKind = pKind;
        markers[numMarkers].CanvasX = x;
        markers[numMarkers].CanvasY = y;
        markers[numMarkers].ScalePercent = bShop ? Clamp( 100.0f * 300.0f / Max( distance, 10.0f ), 150.0f, 230.0f )
                                                 : Clamp( 100.0f * 40.0f / Max( distance, 10.0f ), 18.0f, 60.0f );
        markers[numMarkers].LogoIndex = logo;
        static const bool bLogMarkers = CmdLine::HasArg( "-logmarkers" );
        static uint32_t logTick = 0;
        if ( bLogMarkers && ( logTick++ % 60 ) == 0 ) {
            OTDU_LOG_INFO( "FREERIDE: marker '%s' canvas (%.0f, %.0f) distance %.0f m scale %.0f%%\n", pKind, x, y, distance,
                           markers[numMarkers].ScalePercent );
        }
        numMarkers++;
    };
    if ( raceEvents.isLoaded() ) {
        Vector3f checkpoint;
        int32_t checkpointIndex = 0;
        if ( raceEvents.getNextCheckpoint( checkpoint, checkpointIndex ) ) {
            pushMarker( "race", checkpoint );
        }
        for ( const RaceEvents::RaceStart &start : raceEvents.getStarts() ) {
            const Vector3f d( start.Position.x - carPosition.x, 0.0f, start.Position.z - carPosition.z );
            if ( d.length() < markerRadius ) {
                pushMarker( "event", start.Position );
            }
        }
    }
    if ( !licenseTest.isActive() ) {
        for ( const ChampionshipSpot &spot : championshipSpots ) {
            if ( Vector3f( spot.Position.x - carPosition.x, 0.0f, spot.Position.z - carPosition.z ).length() < markerRadius ) {
                pushMarker( spot.Family.c_str(), spot.Position );
            }
        }
    }
    // A licence test: its next two gates.
    {
        Vector3f gatePosition;
        int32_t gateIndex = 0;
        if ( licenseTest.getNextGate( gatePosition, gateIndex ) ) {
            pushMarker( "race", gatePosition );
            const int32_t after = licenseTest.getGateAfterNext();
            if ( after >= 0 ) {
                pushMarker( "race", licenseTest.getChallenge().getGates()[after].Position );
            }
        }
    }
    // The shops and houses around the car (the owner's retail capture: the
    // house tile over a home down the road; clothes shops, dealerships ...
    // the same, each with its own Spot art). Nearest first, after the race
    // markers; not the spot whose enter element is up, nor during a race /
    // licence test (their gates own the markers).
    if ( !bRaceUp && !CmdLine::HasArg( "-noshopmarkers" ) ) {
        struct ShopMarker {
            const char *pKind;
            Vector3f Position;
            float Distance;
            int32_t Logo;
        };
        std::vector<ShopMarker> shops;
        mapScreen.forEachSpot( [&]( const std::string &name, const char *pIcon, const Vector3f &position, uint64_t id, int32_t logo ) {
            const float distance =
                Vector3f( position.x - carPosition.x, 0.0f, position.z - carPosition.z ).length();
            if ( distance >= markerRadius ||
                 ( pNearKind != nullptr && ( nearSpotPosition - position ).length() < 1.0f ) ) {
                return;
            }
            // The houses you own carry the home art, the ones on sale the
            // realtor's (the map's own rule, MapScreen spot build).
            const char *pKind = pIcon;
            if ( strcmp( pIcon, "estates" ) == 0 && name.compare( 0, 3, "EPH" ) == 0 && IsHouseOwned( id ) ) {
                pKind = "home";
            }
            shops.push_back( { pKind, position, distance, logo } );
        } );
        std::sort( shops.begin(), shops.end(),
                   []( const ShopMarker &a, const ShopMarker &b ) { return a.Distance < b.Distance; } );
        for ( const ShopMarker &shop : shops ) {
            pushMarker( shop.pKind, shop.Position, shop.Logo, true );
        }
        static bool bLogged = false;
        if ( !bLogged && !shops.empty() ) {
            bLogged = true;
            OTDU_LOG_INFO( "FREERIDE: %zu shop markers in %.0f m (nearest '%s' at %.0f m)\n", shops.size(), markerRadius,
                           shops[0].pKind, shops[0].Distance );
        }
    }
    // -markertest <kind>[,<kind>...]: markers pinned across the upper screen
    // at full size (the art / layout check of each Spot kind).
    if ( const char *pTest = CmdLine::GetArgValue( "-markertest" ) ) {
        static std::vector<std::string> testKinds;
        if ( testKinds.empty() ) {
            std::string list = pTest;
            size_t start = 0;
            while ( start <= list.size() ) {
                const size_t comma = list.find( ',', start );
                testKinds.push_back( list.substr( start, comma == std::string::npos ? std::string::npos : comma - start ) );
                if ( comma == std::string::npos ) {
                    break;
                }
                start = comma + 1;
            }
        }
        numMarkers = 0;
        for ( size_t i = 0; i < testKinds.size() && numMarkers < FreeRideHud::kMaxWorldMarkers; i++ ) {
            markers[numMarkers].pSpotKind = testKinds[i].c_str();
            markers[numMarkers].CanvasX = 160.0f + 960.0f * ( testKinds.size() > 1 ? i / float( testKinds.size() - 1 ) : 0.5f );
            markers[numMarkers].CanvasY = 200.0f;
            markers[numMarkers].ScalePercent = 100.0f;
            // -markertest carDealer: the generic car logo (62), -logoframe
            // style override with -markerlogo <n>.
            markers[numMarkers].LogoIndex = StringCmpI( testKinds[i].c_str(), "cardealer" ) == 0
                                                ? CmdLine::GetArgInt( "-markerlogo", 62 )
                                                : -1;
            numMarkers++;
        }
    }
    hud.setWorldMarkers( markers, numMarkers );
    // The race gates on the minimap (its own objectif icons): the same gates
    // the world shows this frame (updateRaceGates).
    {
        FreeRideHud::MinimapObjective objectives[FreeRideHud::kMaxMinimapObjectives];
        int32_t numObjectives = 0;
        for ( const RaceGates::Gate &gate : raceGates.getGates() ) {
            if ( numObjectives >= FreeRideHud::kMaxMinimapObjectives ) {
                break;
            }
            objectives[numObjectives].Position = gate.Position;
            objectives[numObjectives].pLabel = gate.Model == RaceGates::eModel::Start    ? "start"
                                               : gate.Model == RaceGates::eModel::Finish ? "finish"
                                               : gate.Model == RaceGates::eModel::Radar  ? "radar_on"
                                                                                         : "checkpoint";
            numObjectives++;
        }
        hud.setMinimapObjectives( objectives, numObjectives );
    }

    // The challenge hint (loc 2220) beside a race start, once per approach.
    if ( nearRace >= 0 ) {
        static int32_t lastHintedStart = -1;
        if ( lastHintedStart != nearRace && !hud.isHintActive() ) {
            lastHintedStart = nearRace;
            hud.showHint( LocStr( 2220, "Press #command:CAR_LIGHT# to challenge other players" ), 7.0f );
        }
    }

    // The A button of the nearest spot (nothing while a menu or a race is up).
    const bool bBusy = clothesMenu.isOpen() || dealer.isOpen() || schoolMenu.isOpen() || schoolScreen.isOpen() || championshipMenu.isOpen() ||
                       estate.isOpen() || tuningMenu.isOpen() || paintMenu.isOpen() || licenseTest.isActive() ||
                       raceEvents.getStage() != RaceEvents::eStage::Idle;
    if ( !bBusy && bSlow && pNearKind != nullptr && !hud.isLevelUpActive() && !risk.isBanking() &&
         gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        const bool bEvent = pNearKind == std::string( "event" );
        if ( pNearWash != nullptr ) {
            // The car wash (docs/re/car_wash_spec.md): the fee, the retail
            // washing cutscene through GM_CinViewer, FREERIDE again under the
            // StoryHandoff (the car stays where the scene left it). No pump
            // screen exists for it (unlike fuel 1635..1645) - the cutscene IS
            // the screen.
            // The retail order (the owner, 2026-11-04): accepting drives the car
            // to the wash place first (C_INTRO_E_1), THEN the fee is asked;
            // the payment and the clean body happen in the scene
            // (GMCinViewer's pay prompt, VehicleSceneState). A wallet that
            // cannot cover the fee is told here, without the drive-in [INF].
            PlayerProfile &profile = GetPlayerProfile();
            if ( profile.Money >= CarWash::kPrice ) {
                CarWash::fillRequest( *pNearWash, islandName, carDesc );
                hud.closeEnterSpot();
                hud.closeHint();
                OTDU_LOG_INFO( "FREERIDE: car wash '%s' accepted (money %d) -> the drive-in\n", pNearWash->Bank.c_str(),
                               profile.Money );
                pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
            } else {
                hud.showHint( "The wash costs $10 and you cannot pay it.", 4.0f );
            }
        } else if ( pNearChampionship != nullptr ) {
            std::string reason;
            if ( IsChampionshipOpen( *pNearChampionship->pChampionship, &reason ) ) {
                hud.closeEnterSpot();
                championshipMenu.open( *pNearChampionship->pChampionship );
            } else {
                hud.showHint( pNearChampionship->pChampionship->Name + ": " + reason, 5.0f );
            }
        } else if ( !bEvent && bInteriors ) {
            const HouseRow *pHouse = FindHouseBySpotId( nearSpotId );
            bool bEntered = false;
            if ( pHouse != nullptr && nearName.compare( 0, 3, "EPH" ) == 0 ) {
                // A house: walking in is for the ones you own (the others are
                // for sale at the realtor; -nointeriors shows every tile).
                // -housegarage: straight into the garage room (test hook).
                if ( IsHouseOwned( nearSpotId ) &&
                     interior.enterHouse( *pHouse, CmdLine::HasArg( "-housegarage" ), islandName ) ) {
                    OTDU_LOG_INFO( "FREERIDE: walking into your house '%s'\n", pHouse->SpotName.c_str() );
                    bEntered = true;
                } else if ( !IsHouseOwned( nearSpotId ) ) {
                    hud.showHint( "This house is for sale. The realtor sells it.", 4.0f );
                    bEntered = true; // handled: nothing opens from the car
                }
            } else if ( strcmp( pNearKind, "cardealer" ) == 0 && !CmdLine::HasArg( "-nativedealer" ) &&
                        enterDealerShop( nearName, nearSpotId ) ) {
                // The retail dealer screen: the SHOP mode with the SHOPSCAR
                // movie (the story used-car dealer's), FREERIDE comes back at
                // the door. -nativedealer keeps the walk + the native panels.
                return;
            } else {
                bEntered = interior.enter( nearName, islandName );
            }
            if ( bEntered ) {
                // Walk in; a menu opens at the seller (tick, InteriorWalk::eResult::Shop).
                interiorKind = pNearKind;
                interiorSpotId = nearSpotId;
                bindStickerShop();
                hud.closeEnterSpot();
                hud.closeHint();
                // A dealership shows its stock on the podiums.
                if ( interiorKind == "cardealer" ) {
                    if ( const CarShop *pShop = findSpotCarShop( nearName, nearSpotId ) ) {
                        std::vector<int32_t> carIds;
                        for ( const ShopCar &shopCar : pShop->Cars ) {
                            carIds.push_back( shopCar.CarId );
                        }
                        interior.placeDisplayCars( carIds );
                    }
                }
            }
        } else if ( !bEvent ) {
            openSpotShop( pNearKind, nearName, nearSpotId );
        } else {
            raceEvents.start( nearRace );
            hud.closeHint();
        }
    }
}

bool GMFreeRide::enterDealerShop( const std::string &spotBank, uint64_t spotId ) {
    const CarShop *pShop = findSpotCarShop( spotBank, spotId );
    if ( pShop == nullptr || pShop->SpotName.empty() || InteriorWalk::InteriorBankFor( spotBank ).empty() ) {
        return false;
    }
    FreeRideShopRequest &request = GetFreeRideShopRequest();
    request = FreeRideShopRequest();
    request.bActive = true;
    request.Spot = pShop->SpotName;
    request.Bank = spotBank;
    request.Island = islandName;
    hud.closeEnterSpot();
    hud.closeHint();
    OTDU_LOG_INFO( "FREERIDE: into the dealer '%s' (%s, %s) -> SHOP\n", pShop->Name.c_str(), pShop->SpotName.c_str(),
                   spotBank.c_str() );
    pGameInstance->setNextGameMode( eGameMode::GM_Shop );
    return true;
}

const CarShop *GMFreeRide::findSpotCarShop( const std::string &spotBank, uint64_t spotId ) const {
    // DBCarShop._spot_name names the exact building [DATA]; the name rule is
    // the fallback for a spot the table does not list.
    const CarShop *pShop = FindShopBySpotId( spotId );
    if ( pShop == nullptr || pShop->Cars.empty() ) {
        OTDU_LOG_WARN( "FREERIDE: spot %s (0x%llx) not in DBCarShop, shop by name\n", spotBank.c_str(),
                       static_cast<unsigned long long>( spotId ) );
        pShop = FindShopForSpot( spotBank, islandName );
    }
    return pShop;
}

void GMFreeRide::applyOwnedPaint() {
    // The paint the car was ordered in at the dealer (PlayerProfile::OwnedCarPaints);
    // a car never bought keeps its model's own.
    if ( !car.isLoaded() ) {
        return;
    }
    const PlayerProfile &profile = GetPlayerProfile();
    auto it = profile.OwnedCarPaints.find( car.getIdCar() );
    if ( it != profile.OwnedCarPaints.end() ) {
        const std::string name = car.applyDatabasePaint( it->second );
        OTDU_LOG_INFO( "FREERIDE: '%s' in its ordered paint %d '%s'\n", carDesc.FileName.c_str(), it->second, name.c_str() );
    }
    // The custom paint of the sticker shop's Paint page goes over the dealer's.
    const auto custom = profile.OwnedCarCustomPaints.find( car.getIdCar() );
    if ( custom != profile.OwnedCarCustomPaints.end() ) {
        car.applyCustomPaint( custom->second.Type, custom->second.Colour1, custom->second.Bicolour, custom->second.Colour2 );
        OTDU_LOG_INFO( "FREERIDE: '%s' in its custom paint (type %d, %08x%s)\n", carDesc.FileName.c_str(),
                       custom->second.Type, custom->second.Colour1, custom->second.Bicolour ? " + bicolour" : "" );
    }
}

void GMFreeRide::applyOwnedStickers() {
    // The stickers the car wears from a sticker shop
    // (PlayerProfile::OwnedCarStickers): the layout onto the canvas, the
    // canvas bound to the paint materials' sticker slot in drawOffscreen
    // (the renderer is not up yet at spawn - the same defer as the test bind
    // and the GPS screen).
    if ( !StickerBankCatalog::isEnabled() ) {
        return;
    }
    if ( stickerCanvasCarId != car.getIdCar() ) {
        stickerCanvasCarId = car.getIdCar();
        bStickerCanvasBound = false;
    }
    const PlayerProfile &profile = GetPlayerProfile();
    const auto it = profile.OwnedCarStickers.find( car.getIdCar() );
    if ( it == profile.OwnedCarStickers.end() || it->second.empty() ) {
        if ( stickerCanvas.hasLayout() ) {
            stickerCanvas.setLayout( {}, {} );
        }
        return;
    }
    std::vector<GPUTexture *> art;
    StickerResolveArt( it->second, art );
    stickerCanvas.setLayout( it->second, art );
    OTDU_LOG_INFO( "FREERIDE: '%s' wears %zu sticker(s)\n", carDesc.FileName.c_str(), it->second.size() );
}

void GMFreeRide::applyTuning() {
    // The tuning levels the car was upgraded with at a tuning shop
    // (PlayerProfile::OwnedCarTuning, accel / braking / speed 0..3). setupVehicle()
    // applies them at spawn; this re-applies them to the live vehicle after a
    // purchase (the tuning shop is inside an interior, so the physics must pick
    // the change up on the way back to the car). The base setup member is
    // untouched, so this never compounds.
    if ( !car.isLoaded() ) {
        return;
    }
    // The rim size of the tuning shop (a live change: every tire mesh was
    // remapped to the car's stock radius at load, so only the node / scale
    // selection changes - no model reload).
    {
        const int32_t rimSize = ProfileRimSize( car.getIdCar() );
        const int32_t custom = rimSize > 0 ? rimSize : 0;
        if ( carDesc.RimDiameterFront != custom || carDesc.RimDiameterRear != custom ) {
            carDesc.RimDiameterFront = custom;
            carDesc.RimDiameterRear = custom;
            car.setRimDiameters( rimSize > 0 ? rimSize : carDesc.DiameterFront,
                                 rimSize > 0 ? rimSize : carDesc.DiameterRear );
            OTDU_LOG_INFO( "FREERIDE: '%s' rims %d\" applied live\n", carDesc.FileName.c_str(),
                           rimSize > 0 ? rimSize : carDesc.DiameterFront );
        }
    }
    const TuningLevels levels = ProfileTuningLevels( car.getIdCar() );
    if ( levels.Accel == 0 && levels.Braking == 0 && levels.Speed == 0 ) {
        return;
    }
    VehicleSetup tunedSetup = setup;
    ApplyTuningToSetup( tunedSetup, levels.Accel, levels.Braking, levels.Speed );
    vehicle.setParams( tunedSetup );
    OTDU_LOG_INFO( "FREERIDE: '%s' tuning applied live (accel %d, braking %d, speed %d)\n", carDesc.FileName.c_str(),
                   levels.Accel, levels.Braking, levels.Speed );
}

void GMFreeRide::loadChampionshipSpots() {
    // Each championship is entered at its first race's start gate [INF: the
    // retail map icons' positions are not decoded; the first race is where
    // the series begins].
    championshipSpots.clear();
    for ( const LicenseInfo *pChampionship : GetChampionships( islandName == "hawai" ? 1 : 0 ) ) {
        Ig2Challenge first;
        if ( !first.load( pChampionship->Challenges[0] ) ) {
            continue;
        }
        ChampionshipSpot spot;
        spot.pChampionship = pChampionship;
        spot.Position = first.getGates()[0].Position;
        spot.Family = ChampionshipFamily( *pChampionship );
        championshipSpots.push_back( spot );
        std::string reason;
        const bool bOpen = IsChampionshipOpen( *pChampionship, &reason );
        OTDU_LOG_INFO( "FREERIDE: championship '%s' (%s, class %s) starts at (%.0f, %.0f): %s%s\n", pChampionship->Name.c_str(),
                       spot.Family.c_str(), VehicleClassName( pChampionship->VehicleCategory[0] ), spot.Position.x,
                       spot.Position.z, bOpen ? ( IsChampionshipComplete( *pChampionship ) ? "complete" : "open" ) : "locked - ",
                       reason.c_str() );
    }
}

const LicenseInfo *GMFreeRide::findRaceChampionship( const std::string &race ) const {
    for ( const LicenseInfo &license : GetLicenses() ) {
        if ( license.bChampionship &&
             std::find( license.Challenges.begin(), license.Challenges.end(), race ) != license.Challenges.end() ) {
            return &license;
        }
    }
    return nullptr;
}

void GMFreeRide::placeCar( const Vector3f &target, float heading ) {
    // The ground under the point (the road / the static collision first, the
    // heightfield second: the delivery's rule).
    streamWorld( target );
    collision.streamAround( target, 1 );
    float surfaceY = target.y;
    VehicleWorldCollision::Hit hit;
    if ( collision.raycast( Vector3f( target.x, target.y + 30.0f, target.z ), Vector3f( 0.0f, -1.0f, 0.0f ), 80.0f, hit ) ) {
        surfaceY = hit.Position.y;
    } else if ( !view.getWorld().getSurfaceHeight( target.x, target.z, target.y + 30.0f, surfaceY ) ) {
        surfaceY = target.y;
    }
    const Vector3f position( target.x, surfaceY + 0.6f, target.z );
    vehicle.reset( position, Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading ), Vector3f( 0.0f, 0.0f, 0.0f ) );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( position, Vector3f( sinf( heading ), 0.0f, cosf( heading ) ) );
    lastSectorKey = INT32_MIN;
}

void GMFreeRide::startLicenseTest( const std::string &test ) {
    if ( !licenseTest.start( test ) ) {
        interior.showMessage( "This test could not be loaded." );
        return;
    }
    // Out of the school, onto the start gate, facing the course. The car is
    // the one the player drives: the test's own (VMod) needs the runtime car
    // change (-switchcar, car_delivery.md) [INF].
    interior.exit();
    placeCar( licenseTest.getStartPosition(), licenseTest.getStartHeading() );
    hud.closeEnterSpot();
    const std::string title = licenseTest.getChallenge().getTitle();
    hud.showGoal( title.empty() ? test : title );
    startRaceOpponents( test );
    OTDU_LOG_INFO( "FREERIDE: licence test %s from (%.1f, %.1f, %.1f), car %d (test car %d)\n", test.c_str(),
                   licenseTest.getStartPosition().x, licenseTest.getStartPosition().y, licenseTest.getStartPosition().z,
                   car.getIdCar(), licenseTest.getChallenge().getCarId() );
}

void GMFreeRide::startRaceOpponents( const std::string &test ) {
    opponents.stop();
    opponentsHold = -1.0f;
    hud.setRacePosition( 0, 0 );
    const Ig2Challenge &challenge = licenseTest.getChallenge();
    if ( CmdLine::HasArg( "-noopponents" ) || challenge.getMode() != Ig2Challenge::ModeRace ) {
        return;
    }
    // NbIA: the number of AI cars of the file (7 on every championship race,
    // 3 on the rest) [DATA]; -opponents <n> caps it.
    int32_t count = Clamp( static_cast<int32_t>( challenge.getValue( "NbIA", 7 ) ), 0, 7 );
    if ( CmdLine::HasArg( "-opponents" ) ) {
        count = Clamp( CmdLine::GetArgInt( "-opponents", count ), 0, 7 );
    }
    const LicenseInfo *pChampionship = findRaceChampionship( test );
    // The cars: each driver's garage car of the championship's class; a race
    // outside a championship takes the class of its own car (VMod) [INF].
    int32_t class1 = pChampionship != nullptr ? pChampionship->VehicleCategory[0] : 0;
    int32_t class2 = pChampionship != nullptr ? pChampionship->VehicleCategory[1] : 0;
    if ( class1 <= 0 ) {
        AiCarStats stats;
        if ( GetAiCarStats( challenge.getCarId(), stats ) || GetAiCarStats( car.getIdCar(), stats ) ) {
            class1 = stats.VehicleClass;
        }
    }
    std::vector<RaceOpponents::Entry> entries;
    const std::vector<AiDriver> &drivers = GetStoryDrivers();
    for ( int32_t n = 0; n < 7 && static_cast<int32_t>( entries.size() ) < count; n++ ) {
        const uint64_t pnj = pChampionship != nullptr && pChampionship->Bots[n] != 0
                                 ? pChampionship->Bots[n]
                                 : ( static_cast<size_t>( n ) < drivers.size() ? drivers[static_cast<size_t>( n )].Pnj : 0 );
        const AiDriver *pDriver = FindAiDriver( pnj );
        if ( pDriver == nullptr ) {
            continue;
        }
        const AiGarageCar *pCar = PickDriverCar( *pDriver, class1, class2 );
        if ( pCar == nullptr ) {
            continue;
        }
        RaceOpponents::Entry entry;
        entry.Pnj = pnj;
        entry.Name = pDriver->Name;
        entry.CarId = pCar->CarId;
        entry.ColorId = pCar->ColorId;
        entry.Difficulty = pChampionship != nullptr ? pChampionship->BotDifficulty[n] : 0.5f;
        entry.Aggressiveness = pChampionship != nullptr ? pChampionship->BotAggressiveness[n] : 0.5f;
        entry.bRival = pChampionship != nullptr && pChampionship->Rival == pnj;
        // -opponentdifficulty <f>: every driver at one difficulty (tuning / tests).
        if ( CmdLine::HasArg( "-opponentdifficulty" ) ) {
            entry.Difficulty = static_cast<float>( atof( CmdLine::GetArgValue( "-opponentdifficulty" ) ) );
        }
        entries.push_back( entry );
    }
    // The course: the start line, then every gate in passing order.
    std::vector<Vector3f> waypoints{ licenseTest.getStartPosition() };
    for ( int32_t step = 0; step < licenseTest.getGateCount(); step++ ) {
        const int32_t gate = licenseTest.getSequenceGate( static_cast<size_t>( step ) );
        if ( gate >= 0 ) {
            waypoints.push_back( challenge.getGates()[static_cast<size_t>( gate )].Position );
        }
    }
    if ( opponents.start( waypoints, licenseTest.getStartHeading(), entries,
                          [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } ) ) {
        // The finishing order decides a race with opponents, not the file's CtDw.
        licenseTest.setTimeLimit( false );
    }
}

void GMFreeRide::tickRaceOpponents( float deltaTime, bool bRunning, float raceTime ) {
    if ( !opponents.isActive() ) {
        return;
    }
    opponents.tick( deltaTime, bRunning, raceTime, vehicle.getWorldMatrix().getTranslation(),
                    [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
    // -opponentfollow <n> (test): the player car rides 14 m behind opponent n
    // (grid order), so the chase camera shows the AI driving the course.
    if ( bRunning && CmdLine::HasArg( "-opponentfollow" ) ) {
        Vector3f followPosition;
        float followYaw = 0.0f;
        if ( opponents.getFollowPose( CmdLine::GetArgInt( "-opponentfollow", 0 ), 14.0f, followPosition, followYaw ) ) {
            streamWorld( followPosition );
            collision.streamAround( followPosition, 1 );
            vehicle.reset( followPosition, Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), followYaw ),
                           Vector3f( std::sin( followYaw ), 0.0f, std::cos( followYaw ) ) * 1.0f );
            carWorld = vehicle.getWorldMatrix();
        }
    }
    // -logopponents: the field every 2 s.
    static float logTimer = 0.0f;
    logTimer -= deltaTime;
    if ( CmdLine::HasArg( "-logopponents" ) && logTimer <= 0.0f ) {
        logTimer = 2.0f;
        std::string line;
        int32_t place = 1;
        for ( const RaceOpponents::Standing &standing : opponents.getStandings() ) {
            char text[96];
            snprintf( text, sizeof( text ), " %d.%s@%.0fm%s", place++, standing.bPlayer ? "PLAYER" : standing.Name.c_str(),
                      standing.Progress, standing.bFinished ? "(F)" : "" );
            line += text;
        }
        OTDU_LOG_INFO( "RaceOpponents: t %.1f:%s\n", raceTime, line.c_str() );
    }
}

void GMFreeRide::tickLicenseTest( float deltaTime ) {
    // The field after the result: the opponents finish their run, then go.
    if ( opponentsHold > 0.0f ) {
        opponentsHold -= deltaTime;
        tickRaceOpponents( deltaTime, true, licenseTest.getTime() );
        if ( opponentsHold <= 0.0f ) {
            opponents.stop();
        }
    }
    if ( !licenseTest.isActive() ) {
        return;
    }
    // The car waits on the start line during the countdown.
    if ( licenseTest.getStage() == LicenseTest::eStage::Countdown ) {
        const Vector3f start = licenseTest.getStartPosition();
        const Vector3f at = vehicle.getWorldMatrix().getTranslation();
        if ( Vector3f( at.x - start.x, 0.0f, at.z - start.z ).length() > 1.5f ) {
            placeCar( start, licenseTest.getStartHeading() );
        }
    }
    // -licensewarp (test): through each gate in two hops (before it, then past
    // it), the headless exercise of the gate law and the finish.
    if ( CmdLine::HasArg( "-licensewarp" ) && licenseTest.getStage() == LicenseTest::eStage::Running ) {
        static float warpTimer = 0.0f;
        static bool bBefore = true;
        warpTimer -= deltaTime;
        Vector3f gatePosition;
        int32_t gateIndex = 0;
        if ( warpTimer <= 0.0f && licenseTest.getNextGate( gatePosition, gateIndex ) ) {
            // -licensewarpdelay <s>: slower hops (the opponents finish first).
            static const float hop =
                CmdLine::HasArg( "-licensewarpdelay" ) ? static_cast<float>( atof( CmdLine::GetArgValue( "-licensewarpdelay" ) ) ) : 0.25f;
            warpTimer = Max( 0.05f, hop );
            const Ig2Gate &gate = licenseTest.getChallenge().getGates()[gateIndex];
            // Approach from the side the car is on (a corner gate faces the
            // corner's bisector: a straight hop to a fixed side cuts its line).
            const Vector3f at = vehicle.getWorldMatrix().getTranslation();
            const Vector3f centre = gate.getPassCentre(); // a slalom: beside the pole
            const float carSide = Vector3f( at.x - centre.x, 0.0f, at.z - centre.z ).dot( gate.Forward ) < 0.0f ? -1.0f : 1.0f;
            const float side = bBefore ? 3.0f * carSide : -3.0f * carSide;
            const Vector3f target = centre + gate.Forward * side;
            vehicle.reset( target + Vector3f( 0.0f, 0.5f, 0.0f ), Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ),
                                                                                                  std::atan2( gate.Forward.x, gate.Forward.z ) ),
                           Vector3f( 0.0f, 0.0f, 0.0f ) );
            bBefore = !bBefore;
        }
    }
    // -licensepark (test): once the run starts, the car is parked 25 m before
    // the next gate, facing it - a fixed view of the gate models.
    if ( CmdLine::HasArg( "-licensepark" ) && licenseTest.getStage() == LicenseTest::eStage::Running ) {
        static std::string parkedFor;
        Vector3f gatePosition;
        int32_t gateIndex = 0;
        if ( parkedFor != licenseTest.getChallenge().getName() ) {
            // -licensepark <step>: that step's gate (the last: the finish).
            const char *pStep = CmdLine::GetArgValue( "-licensepark" );
            if ( pStep != nullptr && isdigit( static_cast<unsigned char>( pStep[0] ) ) ) {
                licenseTest.debugSkipTo( static_cast<size_t>( atoi( pStep ) ) );
            } else if ( pStep != nullptr && strcmp( pStep, "last" ) == 0 ) {
                licenseTest.debugSkipTo( static_cast<size_t>( licenseTest.getGateCount() - 1 ) );
            }
        }
        if ( parkedFor != licenseTest.getChallenge().getName() && licenseTest.getNextGate( gatePosition, gateIndex ) ) {
            parkedFor = licenseTest.getChallenge().getName();
            const Ig2Gate &gate = licenseTest.getChallenge().getGates()[gateIndex];
            placeCar( gate.Position - gate.Forward * 25.0f, std::atan2( gate.Forward.x, gate.Forward.z ) );
            OTDU_LOG_INFO( "FREERIDE: -licensepark 25 m before gate %d\n", gateIndex );
        }
    }
    // -licenseforce <pass|fail> [<seconds>] (test): ends a running test with that
    // result once it has run that long (default 2 s), so the result screen can
    // be checked without driving the course.
    if ( const char *pForce = CmdLine::GetArgValue( "-licenseforce" ) ) {
        static std::string forcedFor;
        const float after = static_cast<float>( CmdLine::GetArgInt( "-licenseforceafter", 2 ) );
        if ( licenseTest.getStage() == LicenseTest::eStage::Running && licenseTest.getTime() >= after &&
             forcedFor != licenseTest.getChallenge().getName() ) {
            forcedFor = licenseTest.getChallenge().getName();
            licenseTest.end( strcmp( pForce, "pass" ) == 0 ? LicenseTest::eResult::Passed
                                                           : LicenseTest::eResult::TooSlow );
            OTDU_LOG_INFO( "FREERIDE: -licenseforce %s on %s\n", pForce, forcedFor.c_str() );
        }
    }
    licenseTest.tick( deltaTime, vehicle.getWorldMatrix().getTranslation() );
    if ( opponents.isActive() && opponentsHold <= 0.0f ) {
        tickRaceOpponents( deltaTime, licenseTest.getStage() == LicenseTest::eStage::Running, licenseTest.getTime() );
        if ( licenseTest.getStage() == LicenseTest::eStage::Countdown || licenseTest.getStage() == LicenseTest::eStage::Running ) {
            hud.setRacePosition( opponents.getPlayerPosition(), opponents.getRacerCount() );
        }
        // The whole field is home and the player still out a minute later:
        // the race is over for the player (last place) [INF].
        const float lastFinish = opponents.getLastFinishTime();
        if ( licenseTest.getStage() == LicenseTest::eStage::Running && lastFinish >= 0.0f &&
             licenseTest.getTime() > lastFinish + 60.0f ) {
            OTDU_LOG_INFO( "FREERIDE: race %s: the field finished at %.1f s, the player is out - Beaten\n",
                           licenseTest.getChallenge().getName().c_str(), lastFinish );
            licenseTest.end( LicenseTest::eResult::Beaten );
        }
    }
    static float goHold = 0.0f;
    if ( licenseTest.getStage() == LicenseTest::eStage::Countdown ) {
        goHold = 1.0f;
        hud.showCountdown( licenseTest.getCountdownNumber() );
    } else if ( licenseTest.getStage() == LicenseTest::eStage::Running ) {
        // With opponents the position cart sits where GENERAL.vmf's goal
        // popup is (top left): the goal closes on GO [INF].
        if ( opponents.isActive() && goHold >= 1.0f ) {
            hud.closeGoal();
        }
        goHold -= deltaTime;
        hud.showCountdown( goHold > 0.0f ? 0 : -1 );
        Vector3f gatePosition;
        int32_t gateIndex = 0;
        if ( licenseTest.getNextGate( gatePosition, gateIndex ) ) {
            // The retail readouts: the time, checkpoint n of the lap's N, lap
            // n of N (a circuit).
            FreeRideHud::RaceHud race;
            race.bVisible = true;
            race.Seconds = licenseTest.getTime();
            race.Checkpoint = licenseTest.getLapGatesPassed() + 1;
            race.Checkpoints = licenseTest.getGatesPerLap();
            race.Lap = licenseTest.getLap();
            race.Laps = licenseTest.getLapCount();
            hud.setRaceHud( race );
        }
        LicenseTest::GateEvent event;
        while ( licenseTest.takeGateEvent( event ) ) {
            hud.showCheckpointPassed( event.Time, event.bHasSplit, event.Split );
            if ( event.bLastLapStarts ) {
                hud.showLastLap();
            }
        }
    }
    LicenseTest::eResult result = LicenseTest::eResult::None;
    float time = 0.0f;
    if ( licenseTest.takeResult( result, time ) ) {
        hud.showCountdown( -1 );
        hud.setRaceTimer( false, 0.0f, 0, 0 );
        hud.setRaceHud( FreeRideHud::RaceHud() );
        const std::string test = licenseTest.getChallenge().getName();
        const std::string title = licenseTest.getChallenge().getTitle();
        // A race with opponents is won in first place; behind the winner it is
        // "Beaten" with the place [INF: the retail result rule is ENC].
        std::string placeText;
        if ( opponents.isActive() ) {
            if ( result == LicenseTest::eResult::Passed || result == LicenseTest::eResult::Beaten ) {
                if ( result == LicenseTest::eResult::Passed ) {
                    opponents.playerFinished( time );
                }
                const int32_t place = opponents.getPlayerPosition();
                const char *pSuffix = place == 1 ? "st" : place == 2 ? "nd" : place == 3 ? "rd" : "th";
                placeText = "  " + std::to_string( place ) + pSuffix + " of " + std::to_string( opponents.getRacerCount() );
                if ( place > 1 ) {
                    result = LicenseTest::eResult::Beaten;
                }
            }
            hud.setRacePosition( 0, 0 );
            opponentsHold = result == LicenseTest::eResult::Abandoned ? 0.01f : 8.0f;
            OTDU_LOG_INFO( "FREERIDE: race %s result %s%s\n", test.c_str(), LicenseResultText( result ), placeText.c_str() );
        }
        char text[200];
        snprintf( text, sizeof( text ), "%s: %s%s  %d:%02d.%02d", title.empty() ? test.c_str() : title.c_str(),
                  LicenseResultText( result ), placeText.c_str(), static_cast<int32_t>( time ) / 60,
                  static_cast<int32_t>( time ) % 60, static_cast<int32_t>( time * 100.0f ) % 100 );
        // A licence test (not a championship race) ends on the retail result
        // screen; the goal line stays for the races.
        const bool bLicenceTest = findRaceChampionship( test ) == nullptr && test.size() > 3 &&
                                  ( test.compare( 0, 2, "RS" ) == 0 || test.compare( 0, 2, "rs" ) == 0 );
        const LicenseInfo *pTestLicence = nullptr;
        for ( const LicenseInfo &license : GetLicenses() ) {
            if ( !license.bChampionship &&
                 std::find( license.Challenges.begin(), license.Challenges.end(), test ) != license.Challenges.end() ) {
                pTestLicence = &license;
                break;
            }
        }
        const bool bLicenceWasObtained = pTestLicence != nullptr && IsLicenseObtained( *pTestLicence );
        if ( !bLicenceTest || result == LicenseTest::eResult::Abandoned ) {
            hud.showGoal( text );
        }
        if ( result == LicenseTest::eResult::Passed ) {
            PlayerProfile &profile = GetPlayerProfile();
            auto best = profile.PassedTests.find( test );
            const bool bFirstPass = best == profile.PassedTests.end();
            const LicenseInfo *pChampionship = findRaceChampionship( test );
            const bool bWasComplete = pChampionship != nullptr && IsChampionshipComplete( *pChampionship );
            if ( bFirstPass || time < best->second ) {
                profile.PassedTests[test] = time;
            }
            // Championship prizes: _racemoneyreward the first time a race is
            // won, _champmoneyreward when the last one is [DATA amounts; INF
            // when they are paid].
            if ( pChampionship != nullptr && bFirstPass ) {
                profile.Money += pChampionship->RaceMoney;
                std::string prize = "+$" + std::to_string( pChampionship->RaceMoney );
                if ( !bWasComplete && IsChampionshipComplete( *pChampionship ) ) {
                    profile.Money += pChampionship->ChampMoney;
                    prize = pChampionship->Name + " complete!  +$" + std::to_string( pChampionship->RaceMoney + pChampionship->ChampMoney );
                    OTDU_LOG_INFO( "FREERIDE: championship '%s' complete (+$%d)\n", pChampionship->Name.c_str(),
                                   pChampionship->ChampMoney );
                }
                hud.showHint( prize, 6.0f );
                OTDU_LOG_INFO( "FREERIDE: race %s won, money now $%d\n", test.c_str(), profile.Money );
            }
            // The licence of this test, when it is now complete.
            for ( const LicenseInfo &license : GetLicenses() ) {
                if ( !license.bChampionship &&
                     std::find( license.Challenges.begin(), license.Challenges.end(), test ) != license.Challenges.end() &&
                     IsLicenseObtained( license ) ) {
                    hud.showHint( "Licence " + LicenseClasses( license ) + " obtained!", 6.0f );
                    OTDU_LOG_INFO( "FREERIDE: licence %s obtained\n", LicenseClasses( license ).c_str() );
                }
            }
            SaveProfile( "licence test passed" );
        }
        if ( bLicenceTest && result != LicenseTest::eResult::Abandoned ) {
            const bool bLicenceNew =
                !bLicenceWasObtained && pTestLicence != nullptr && IsLicenseObtained( *pTestLicence );
            openLicenseResult( test, result == LicenseTest::eResult::Passed, result, time, bLicenceNew );
        }
    }
}

void GMFreeRide::openLicenseResult( const std::string &test, bool bPassed, LicenseTest::eResult result, float time,
                                    bool bLicenceNew ) {
    resultTest = test;
    LicenseResultScreen::Info info;
    info.bPassed = bPassed;
    info.TestName = licenseTest.getChallenge().getTitle();
    if ( info.TestName.empty() ) {
        info.TestName = test;
    }
    info.TestDesc = licenseTest.getChallenge().getDescription();
    info.Message = LicenseResultText( result );
    info.Time = time;
    info.Target = licenseTest.getChallenge().getTargetMs() / 1000.0f;
    for ( const LicenseInfo &license : GetLicenses() ) {
        auto it = std::find( license.Challenges.begin(), license.Challenges.end(), test );
        if ( license.bChampionship || it == license.Challenges.end() ) {
            continue;
        }
        info.LicenceName = LicenseClasses( license );
        info.TestsTotal = static_cast<int32_t>( license.Challenges.size() );
        for ( const std::string &name : license.Challenges ) {
            info.TestsDone += IsTestPassed( name ) ? 1 : 0;
        }
        // IsLicensed: 1 the licence was just obtained (Quit first), 2 the last
        // test (no next one), 0 otherwise [INF: from the menus SetRootMenuRS builds].
        const bool bLast = it + 1 == license.Challenges.end();
        info.Licensed = bLicenceNew ? 1 : ( bLast ? 2 : 0 );
        break;
    }
    if ( bLicenceNew ) {
        info.Message = "Licence " + info.LicenceName + " obtained!";
    }
    licenseResult.open( info );
    if ( !licenseResult.isOpen() ) {
        // No RESULTS.vmf: the goal line says it.
        hud.showGoal( info.TestName + ": " + info.Message );
        return;
    }
    OTDU_LOG_INFO( "FREERIDE: licence result screen for %s (%s)\n", test.c_str(), bPassed ? "passed" : "failed" );
}

void GMFreeRide::tickLicenseResult( float deltaTime ) {
    licenseResult.tick( deltaTime );
    const LicenseResultScreen::eChoice choice = licenseResult.takeChoice();
    if ( choice == LicenseResultScreen::eChoice::None ) {
        return;
    }
    licenseResult.close();
    const std::string test = resultTest;
    switch ( choice ) {
    case LicenseResultScreen::eChoice::Retry:
        OTDU_LOG_INFO( "FREERIDE: licence result: retry %s\n", test.c_str() );
        startLicenseTest( test );
        break;
    case LicenseResultScreen::eChoice::Next: {
        // The next test of the licence; past the last one, the school.
        std::string next;
        for ( const LicenseInfo &license : GetLicenses() ) {
            auto it = std::find( license.Challenges.begin(), license.Challenges.end(), test );
            if ( !license.bChampionship && it != license.Challenges.end() ) {
                if ( it + 1 != license.Challenges.end() ) {
                    next = *( it + 1 );
                }
                break;
            }
        }
        // Tests open in order (SchoolMenu): the next one needs this one passed.
        if ( !next.empty() && IsTestPassed( test ) ) {
            OTDU_LOG_INFO( "FREERIDE: licence result: next test %s\n", next.c_str() );
            startLicenseTest( next );
        } else {
            OTDU_LOG_INFO( "FREERIDE: licence result: no next test after %s, back to the school\n", test.c_str() );
            returnToSchool( test );
        }
        break;
    }
    case LicenseResultScreen::eChoice::Quit:
        OTDU_LOG_INFO( "FREERIDE: licence result: back to the school\n" );
        returnToSchool( test );
        break;
    default:
        break;
    }
}

void GMFreeRide::openSchool( char letter, const std::string &test ) {
    // The retail R_SCHOOL.vmf desk (licenses_spec 11); -nativeschool: the
    // port's own panel.
    const int32_t island = islandName == "hawai" ? 1 : 0;
    if ( !CmdLine::HasArg( "-nativeschool" ) ) {
        schoolScreen.openAtTest( letter, island, test );
        if ( schoolScreen.isOpen() ) {
            return;
        }
    }
    schoolMenu.open( letter, island );
    if ( !test.empty() ) {
        schoolMenu.openAtTest( test );
    }
}

void GMFreeRide::returnToSchool( const std::string &test ) {
    // RS<letter>...: the school spot ERS_<letter> stands beside its tests
    // (docs/re/licenses_spec.md 1) [DATA].
    const char letter = test.size() > 2 ? static_cast<char>( toupper( test[2] ) ) : 'A';
    const std::string spot = std::string( "ERS_" ) + letter;
    if ( !interior.enter( spot, islandName ) ) {
        OTDU_LOG_WARN( "FREERIDE: school interior %s could not be entered\n", spot.c_str() );
    } else {
        interiorKind = "rschool";
    }
    openSchool( letter, test );
}

void GMFreeRide::buildCourseMarkers() {
    const Ig2Challenge &challenge = licenseTest.getChallenge();
    courseMarkersFor = challenge.getName();
    std::vector<RaceGates::Marker> markers;
    auto snapToGround = [this]( Vector3f &position ) {
        VehicleWorldCollision::Hit hit;
        float surfaceY = position.y;
        if ( collision.raycast( position + Vector3f( 0.0f, 6.0f, 0.0f ), Vector3f( 0.0f, -1.0f, 0.0f ), 12.0f, hit ) ) {
            surfaceY = hit.Position.y;
        } else if ( !view.getWorld().getSurfaceHeight( position.x, position.z, position.y + 6.0f, surfaceY ) ) {
            surfaceY = position.y;
        }
        position.y = surfaceY;
    };
    auto headingOf = []( const Vector3f &direction ) { return std::atan2( direction.x, direction.z ); };
    const std::vector<Ig2Gate> &gates = challenge.getGates();
    // The slalom poles, all of them for the whole run (the player reads the
    // weave ahead) [INF].
    int32_t numPoles = 0;
    for ( const Ig2Gate &gate : gates ) {
        if ( gate.isSlalom() ) {
            RaceGates::Marker marker;
            marker.Position = gate.Position;
            snapToGround( marker.Position );
            marker.Heading = headingOf( gate.Forward );
            marker.Model = RaceGates::eModel::Barrels;
            markers.push_back( marker );
            numPoles++;
        }
    }
    // The DEC2 cone lines: a start record (type 1 / 4) and its end (type 2 /
    // 5) next in the file, one marker every kConeSpacing metres between them
    // [DATA: the pairs; INF: the spacing - the retail cone model and its
    // spacing are not in the pack; the racing school's barrel stands in].
    constexpr float kConeSpacing = 3.0f;
    const std::vector<Ig2Decor> &decors = challenge.getDecors();
    int32_t numLines = 0, numCones = 0;
    for ( size_t i = 0; i < decors.size(); i++ ) {
        const Ig2Decor &decor = decors[i];
        const bool bLineStart = ( decor.Type == 1 || decor.Type == 4 ) && i + 1 < decors.size() &&
                                decors[i + 1].Type == decor.Type + 1;
        if ( !bLineStart ) {
            if ( decor.Type == 0 ) {
                RaceGates::Marker marker;
                marker.Position = decor.Position;
                snapToGround( marker.Position );
                markers.push_back( marker );
                numCones++;
            }
            continue;
        }
        const Vector3f a = decor.Position, b = decors[i + 1].Position;
        const Vector3f along( b.x - a.x, 0.0f, b.z - a.z );
        const float length = along.length();
        const int32_t count = Max( 2, static_cast<int32_t>( std::round( length / kConeSpacing ) ) + 1 );
        for ( int32_t n = 0; n < count; n++ ) {
            RaceGates::Marker marker;
            marker.Position = a + ( b - a ) * ( static_cast<float>( n ) / static_cast<float>( count - 1 ) );
            snapToGround( marker.Position );
            marker.Heading = headingOf( along );
            markers.push_back( marker );
            numCones++;
        }
        numLines++;
        i++; // the end record is used
    }
    // The racing line [INF: the retail line's source is not decoded - the
    // pack has its strip ('Trajectory') and the braking target
    // ('BrakingZone_01'), the files no path; the port draws it through the
    // gate openings of the manoeuvre tests]: a Catmull-Rom curve from the
    // start pose through every opening of one lap, cut into strips; a road
    // test (gates on road links, 150-600 m apart) has no straight line to
    // draw and gets none.
    int32_t numStrips = 0, numBrakes = 0;
    if ( !CmdLine::HasArg( "-noracingline" ) && raceGates.hasModel( RaceGates::eModel::Trajectory ) ) {
        std::vector<Vector3f> points;
        points.push_back( gates[0].Position );
        const int32_t perLap = Max( 1, licenseTest.getGatesPerLap() );
        for ( int32_t n = 0; n < perLap; n++ ) {
            const int32_t index = licenseTest.getSequenceGate( static_cast<size_t>( n ) );
            if ( index < 0 ) {
                break;
            }
            points.push_back( gates[index].getPassCentre() );
        }
        bool bRoadCourse = false;
        for ( const Ig2Gate &gate : gates ) {
            bRoadCourse = bRoadCourse || gate.bRoad;
        }
        std::vector<Vector3f> samples;
        if ( !bRoadCourse && points.size() >= 2 ) {
            constexpr float kSampleStep = 2.5f;
            for ( size_t i = 0; i + 1 < points.size(); i++ ) {
                const Vector3f &p0 = points[i > 0 ? i - 1 : i];
                const Vector3f &p1 = points[i];
                const Vector3f &p2 = points[i + 1];
                const Vector3f &p3 = points[i + 2 < points.size() ? i + 2 : i + 1];
                const int32_t steps = Max( 1, static_cast<int32_t>( ( p2 - p1 ).length() / kSampleStep ) );
                for ( int32_t s = 0; s < steps; s++ ) {
                    const float t = static_cast<float>( s ) / static_cast<float>( steps );
                    const float t2 = t * t, t3 = t2 * t;
                    samples.push_back( ( p1 * 2.0f + ( p2 - p0 ) * t + ( p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3 ) * t2 +
                                         ( p1 * 3.0f - p0 - p2 * 3.0f + p3 ) * t3 ) *
                                       0.5f );
                }
            }
            samples.push_back( points.back() );
        }
        for ( Vector3f &sample : samples ) {
            snapToGround( sample );
            sample.y += 0.04f; // over the road surface, under the tyres
        }
        std::vector<float> headings;
        for ( size_t i = 0; i + 1 < samples.size(); i++ ) {
            const Vector3f d = samples[i + 1] - samples[i];
            headings.push_back( headingOf( Vector3f( d.x, 0.0f, d.z ) ) );
            // The strip's arrow points down its -z [DATA: the chevrons face the
            // car when laid along +z]: it is laid from the far end back.
            RaceGates::Marker strip;
            strip.Position = samples[i + 1];
            strip.Heading = headings.back() + kPI;
            strip.Model = RaceGates::eModel::Trajectory;
            strip.Length = Vector3f( d.x, 0.0f, d.z ).length();
            strip.Width = 1.2f;
            markers.push_back( strip );
            numStrips++;
        }
        // Where to slow down [INF]: the line turns more than 50 degrees within
        // the next 20 m -> a braking target 25 m before the turn starts, one
        // per 40 m at most.
        if ( raceGates.hasModel( RaceGates::eModel::BrakingZone ) && headings.size() > 2 ) {
            auto turn = []( float a, float b ) {
                float d = b - a;
                while ( d > kPI ) {
                    d -= 2.0f * kPI;
                }
                while ( d < -kPI ) {
                    d += 2.0f * kPI;
                }
                return std::fabs( d );
            };
            constexpr float kLook = 20.0f, kLead = 25.0f, kMinGap = 40.0f, kSharp = 50.0f * kPI / 180.0f;
            float lastBrake = -1000.0f, along = 0.0f;
            std::vector<float> distances( headings.size(), 0.0f );
            for ( size_t i = 1; i < headings.size(); i++ ) {
                distances[i] = distances[i - 1] + ( samples[i] - samples[i - 1] ).length();
            }
            for ( size_t i = 0; i < headings.size(); i++ ) {
                along = distances[i];
                size_t j = i;
                while ( j + 1 < headings.size() && distances[j + 1] - along <= kLook ) {
                    j++;
                }
                if ( turn( headings[i], headings[j] ) < kSharp || along - lastBrake < kMinGap ) {
                    continue;
                }
                size_t k = i;
                while ( k > 0 && along - distances[k] < kLead ) {
                    k--;
                }
                RaceGates::Marker brake;
                brake.Position = samples[k];
                brake.Heading = headings[k];
                brake.Model = RaceGates::eModel::BrakingZone;
                markers.push_back( brake );
                if ( CmdLine::HasArg( "-logracegates" ) ) {
                    OTDU_LOG_INFO( "FREERIDE: braking zone at (%.1f, %.1f, %.1f), %.0f m along the line\n",
                                   brake.Position.x, brake.Position.y, brake.Position.z, distances[k] );
                }
                lastBrake = along;
                numBrakes++;
            }
        }
    }
    raceGates.setMarkers( markers );
    OTDU_LOG_INFO( "FREERIDE: %s course: %d slalom pole(s), %d cone line(s) / %d marker(s), racing line %d strip(s), "
                   "%d braking zone(s)\n",
                   courseMarkersFor.c_str(), numPoles, numLines, numCones, numStrips, numBrakes );
}

void GMFreeRide::updateRaceGates() {
    std::vector<RaceGates::Gate> gates;
    if ( !bRaceGates || !raceGates.isLoaded() ) {
        raceGates.clear();
        return;
    }
    // The road arch is drawn at its authored size (24.5 m: it spans the road
    // and its shoulders), wider than the files' 7 m road gates; the pass zone
    // follows what is drawn (LicenseTest::getPassHalfWidth) [INF: the retail
    // scale and pass rule are ENC].
    const float kRoadArchWidth = Max( 14.0f, raceGates.getModelWidth( RaceGates::eModel::Checkpoint ) );
    constexpr float kStartLineAhead = 8.0f;
    // The gate on the drivable surface: the files' heights sit up to a metre
    // under the road (a barrel is 1.1 m tall), the road graph's nodes off it.
    auto snapToGround = [this]( Vector3f &position ) {
        VehicleWorldCollision::Hit hit;
        float surfaceY = position.y;
        if ( collision.raycast( position + Vector3f( 0.0f, 6.0f, 0.0f ), Vector3f( 0.0f, -1.0f, 0.0f ), 12.0f, hit ) ) {
            surfaceY = hit.Position.y;
        } else if ( !view.getWorld().getSurfaceHeight( position.x, position.z, position.y + 6.0f, surfaceY ) ) {
            surfaceY = position.y;
        }
        position.y = surfaceY;
    };
    if ( !licenseTest.isActive() || licenseTest.getStage() == LicenseTest::eStage::Finished ) {
        if ( !courseMarkersFor.empty() ) {
            raceGates.clearMarkers();
            courseMarkersFor.clear();
        }
    } else if ( courseMarkersFor != licenseTest.getChallenge().getName() ) {
        buildCourseMarkers();
    }
    if ( licenseTest.isActive() && licenseTest.getStage() != LicenseTest::eStage::Finished ) {
        const Ig2Challenge &challenge = licenseTest.getChallenge();
        const std::vector<Ig2Gate> &courseGates = challenge.getGates();
        const bool bCircuit = challenge.isCircuit();
        const size_t lastStep = static_cast<size_t>( Max( 0, licenseTest.getGateCount() - 1 ) );
        auto pushGate = [&]( int32_t gateIndex, size_t step, bool bNext, bool bStartLine ) {
            if ( gateIndex < 0 || gateIndex >= static_cast<int32_t>( courseGates.size() ) ) {
                return;
            }
            const Ig2Gate &course = courseGates[gateIndex];
            RaceGates::Gate gate;
            gate.Position = course.Position;
            snapToGround( gate.Position );
            gate.Forward = course.Forward;
            gate.Width = course.Width;
            gate.bNext = bNext;
            if ( gateIndex == 0 ) {
                // The start line; a circuit's last lap closes on it: the finish.
                gate.Model = bCircuit && !bStartLine && step == lastStep ? RaceGates::eModel::Finish : RaceGates::eModel::Start;
            } else if ( ( !bCircuit && gateIndex == static_cast<int32_t>( courseGates.size() ) - 1 ) ||
                        ( !course.bRoad && course.Kind == 3 ) ) {
                gate.Model = RaceGates::eModel::Finish;
            } else if ( course.isSlalom() ) {
                return; // a slalom pole: drawn for the whole run (buildCourseMarkers)
            } else if ( !course.bRoad && ( course.Kind == 1 || course.Kind == 2 ) ) {
                gate.Model = RaceGates::eModel::Barrels; // the manoeuvre gates
            } else {
                gate.Model = RaceGates::eModel::Checkpoint;
            }
            if ( course.bRoad && gate.Model != RaceGates::eModel::Barrels ) {
                gate.Width = Max( gate.Width, kRoadArchWidth );
            }
            licenseTest.setVisualHalfWidth( static_cast<size_t>( gateIndex ),
                                            gate.Model == RaceGates::eModel::Barrels ? 0.0f : gate.Width * 0.5f );
            // The countdown: the car stands on the start pose, the chase
            // camera behind it; the line is drawn just ahead so it reads in
            // front of the car [INF].
            if ( bStartLine ) {
                gate.Position = gate.Position + gate.Forward * kStartLineAhead;
                snapToGround( gate.Position );
            }
            gates.push_back( gate );
        };
        const size_t step = static_cast<size_t>( licenseTest.getGatesPassed() );
        if ( licenseTest.getStage() == LicenseTest::eStage::Countdown ) {
            pushGate( 0, 0, false, true );
        }
        pushGate( licenseTest.getSequenceGate( step ), step, true, false );
        if ( licenseTest.getSequenceGate( step + 1 ) != licenseTest.getSequenceGate( step ) ) {
            pushGate( licenseTest.getSequenceGate( step + 1 ), step + 1, false, false );
        }
    } else if ( raceEvents.getStage() == RaceEvents::eStage::Countdown ||
                raceEvents.getStage() == RaceEvents::eStage::Running ) {
        // The instant challenges: the route's checkpoints (road nodes), each
        // facing along the route; the start line on the start marker.
        const std::vector<Vector3f> &checkpoints = raceEvents.getCheckpoints();
        const int32_t startIndex = raceEvents.getActiveStart();
        const Vector3f startPosition = startIndex >= 0 ? raceEvents.getStarts()[startIndex].Position : Vector3f();
        auto pointAt = [&]( int32_t i ) { return i < 0 ? startPosition : checkpoints[i]; };
        auto pushCheckpoint = [&]( int32_t i, bool bNext ) {
            if ( i >= static_cast<int32_t>( checkpoints.size() ) || ( i < 0 && startIndex < 0 ) ) {
                return;
            }
            const Vector3f from = pointAt( i - 1 );
            const Vector3f to = i + 1 < static_cast<int32_t>( checkpoints.size() ) ? pointAt( i + 1 ) : pointAt( i );
            Vector3f forward( to.x - from.x, 0.0f, to.z - from.z );
            if ( i < 0 && !checkpoints.empty() ) {
                forward = Vector3f( checkpoints[0].x - startPosition.x, 0.0f, checkpoints[0].z - startPosition.z );
            }
            const float length = forward.length();
            RaceGates::Gate gate;
            gate.Position = pointAt( i );
            gate.Forward = length > 1e-3f ? forward * ( 1.0f / length ) : Vector3f( 0.0f, 0.0f, 1.0f );
            gate.Width = kRoadArchWidth;
            gate.bNext = bNext;
            gate.Model = i < 0 ? RaceGates::eModel::Start
                               : i == static_cast<int32_t>( checkpoints.size() ) - 1 ? RaceGates::eModel::Finish
                                                                                      : RaceGates::eModel::Checkpoint;
            if ( i < 0 ) {
                gate.Position = gate.Position + gate.Forward * kStartLineAhead;
            }
            snapToGround( gate.Position );
            gates.push_back( gate );
        };
        if ( raceEvents.getStage() == RaceEvents::eStage::Countdown ) {
            pushCheckpoint( -1, false );
        }
        const int32_t next = raceEvents.getNextCheckpointIndex();
        pushCheckpoint( next, true );
        pushCheckpoint( next + 1, false );
    }
    raceGates.setGates( gates );
    static const bool bLogGates = CmdLine::HasArg( "-logracegates" );
    if ( bLogGates && !gates.empty() && pGameInstance->getFrameIndex() % 60 == 0 ) {
        const Vector3f car = vehicle.getWorldMatrix().getTranslation();
        for ( const RaceGates::Gate &gate : gates ) {
            float ground = -1.0f;
            view.getWorld().getHeight( gate.Position.x, gate.Position.z, ground );
            OTDU_LOG_INFO( "RaceGates: frame %u car (%.1f, %.1f, %.1f) gate model %d at (%.1f, %.1f, %.1f) fwd (%.2f, %.2f) "
                           "width %.1f%s, %.1f m away, terrain %.1f\n",
                           pGameInstance->getFrameIndex(), car.x, car.y, car.z, static_cast<int32_t>( gate.Model ),
                           gate.Position.x, gate.Position.y, gate.Position.z, gate.Forward.x, gate.Forward.z, gate.Width,
                           gate.bNext ? " next" : "", ( gate.Position - car ).length(), ground );
        }
    }
}

bool GMFreeRide::stickerCarView() const {
    return paintMenu.isStickerPlacing() || ( interior.isStickUi() && stickerShop.wantsCarView() );
}

void GMFreeRide::bindStickerShop() {
    if ( !interior.isStickUi() || !StickerBankCatalog::isEnabled() ) {
        return;
    }
    // The car stands in the room (the retail design studio): the player's own
    // paint on a second copy; no car place -> the old outside view.
    pStickerRoomCar = car.isLoaded() ? interior.placeStickerCar( car.getIdCar() ) : nullptr;
    bStickerRoomCarBound = false;
    if ( pStickerRoomCar != nullptr ) {
        const PlayerProfile &profile = GetPlayerProfile();
        const auto paint = profile.OwnedCarPaints.find( car.getIdCar() );
        if ( paint != profile.OwnedCarPaints.end() ) {
            pStickerRoomCar->applyDatabasePaint( paint->second );
        }
        const auto custom = profile.OwnedCarCustomPaints.find( car.getIdCar() );
        if ( custom != profile.OwnedCarCustomPaints.end() ) {
            pStickerRoomCar->applyCustomPaint( custom->second.Type, custom->second.Colour1, custom->second.Bicolour,
                                               custom->second.Colour2 );
        }
    }
    stickerShop.bind( &interior.getScene().getUi(), &stickerCanvas, car.getIdCar(),
                      pStickerRoomCar != nullptr ? pStickerRoomCar : ( car.isLoaded() ? &car : nullptr ) );
}

bool GMFreeRide::openSpotShop( const std::string &kind, const std::string &spotBank, uint64_t spotId ) {
    if ( kind == "cardealer" ) {
        const CarShop *pShop = findSpotCarShop( spotBank, spotId );
        OTDU_LOG_INFO( "FREERIDE: dealer '%s' (%s)\n", pShop != nullptr ? pShop->Name.c_str() : "?",
                       pShop != nullptr ? pShop->SpotName.c_str() : "" );
        dealer.setSidePanel( interior.isActive() );
        dealer.open( pShop );
        // Where the bought car is parked outside (CarDelivery).
        dealerSpotName = pShop != nullptr ? pShop->SpotName : spotBank;
        return pShop != nullptr;
    }
    if ( kind == "clothes" ) {
        // The shop row's brand (DBCarShop._clothesshopbrand) is its stock.
        const CarShop *pShop = FindShopBySpotId( spotId );
        if ( pShop != nullptr && pShop->ClothesBrand != 0 ) {
            clothesMenu.open( pShop->Name, pShop->ClothesBrand, PlayerClothesSex() );
        } else {
            clothesMenu.open( LocStr( 401, "Clothes shop" ) );
        }
        return true;
    }
    if ( kind == "rschool" ) {
        // ERS_A / ERS_B / ERS_C: the school's letter is its licences' (RSA..., RSB..., RSC...) [DATA];
        // DBLicense._island 0 Ibiza, 1 Hawai.
        const char letter = spotBank.size() > 4 ? spotBank[4] : 'A';
        openSchool( letter, std::string() );
        return true;
    }
    if ( kind == "hair" ) {
        // The hairdresser sells the HAIR brand items (haircuts, DBClothe part 3) [DATA].
        clothesMenu.open( "Hairdresser", FindBrandHashcode( "HAIR" ), PlayerClothesSex() );
        return true;
    }
    if ( kind == "estates" && spotBank.rfind( "EPH", 0 ) != 0 ) {
        // The realtor (ERE_*): its DBHouse rows (the house spots are also
        // "estates" spots on the map - they have no menu).
        estate.setSidePanel( interior.isActive() );
        estate.open( spotId );
        OTDU_LOG_INFO( "FREERIDE: realtor '%s' (0x%llx): %s\n", spotBank.c_str(),
                       static_cast<unsigned long long>( spotId ), estate.isOpen() ? "stock found" : "no stock" );
        return estate.isOpen();
    }
    if ( kind == "tuning" ) {
        // The tuning shop (ETUN_*): the three levels of the car you drove in with.
        tuningMenu.setSidePanel( interior.isActive() );
        tuningMenu.open( car.getIdCar(), FindCarDisplayName( car.getIdCar() ) );
        return true;
    }
    if ( kind == "carpaint" ) {
        // The sticker / paint shop (ESTICK_*): the car's colour catalogue and
        // the sticker packs (the canvas the sticker page edits live).
        paintMenu.setSidePanel( interior.isActive() );
        paintMenu.setStickerContext( &stickerCanvas, car.getIdCar() );
        paintMenu.open( car.getIdCar(), FindCarDisplayName( car.getIdCar() ) );
        return true;
    }
    return false;
}

void GMFreeRide::teleportTo( const Vector3f &target, float heading, bool bHasHeading ) {
    // The cheats' teleport (the panel, F5 on the map, -tpin): stream the world
    // at the target, drop the car on the surface under it and reset the
    // dynamics / the camera - what backOnTrack does for the nearest road, but
    // to a point the player chose.
    streamWorld( Vector3f( target.x, target.y, target.z ) );
    float surfaceY = target.y;
    VehicleWorldCollision::Hit hit;
    const Vector3f from( target.x, target.y + 400.0f, target.z );
    if ( collision.raycast( from, Vector3f( 0.0f, -1.0f, 0.0f ), 900.0f, hit ) ) {
        surfaceY = hit.Position.y; // the road / the static collision
    } else if ( !view.getWorld().getSurfaceHeight( target.x, target.z, 4000.0f, surfaceY ) ) {
        surfaceY = target.y; // nothing there (open sea): keep the target height
    }
    const Vector3f position( target.x, surfaceY + 0.6f, target.z );
    Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    if ( bHasHeading ) {
        forward = Vector3f( std::sin( heading ), 0.0f, std::cos( heading ) );
    }
    const Vector3f flatForward = Vector3f( forward.x, 0.0f, forward.z ).normalized();
    const float yaw = atan2f( flatForward.x, flatForward.z );
    vehicle.reset( position, Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), yaw ),
                   Vector3f( 0.0f, 0.0f, 0.0f ) );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( position, flatForward );
    // The old spot's dust / tracks and the discs' heat do not come along.
    if ( gpRender != nullptr ) {
        gpRender->getSurfaceFx().clear();
    }
    // The next tick re-streams (the sector key changed) and the map follows.
    lastSectorKey = INT32_MIN;
    OTDU_LOG_INFO( "FREERIDE: teleport to (%.1f, %.1f, %.1f), heading %.0f deg\n", position.x,
                   position.y, position.z, yaw * kRadToDeg );
}

void GMFreeRide::drawUI() {
    // The cheats panel: F4, or the retail dev-feature pad code (a circle of the
    // D-pad three times with the option button mashed - see Cheats::feedPadCode).
    // gpInput::wasKeyPressed, like the world viewer's F2, so the autoplay can
    // drive it (-autoplay "200:key=F4").
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F4 ) ) {
        cheats.toggle();
        OTDU_LOG_INFO( "Cheats: panel %s\n", cheats.isOpen() ? "open" : "closed" );
    }
    cheats.feedPadCode( gpInput->wasPressed( eInputAction::IA_AccessoriesUp ),
                        gpInput->wasPressed( eInputAction::IA_AccessoriesRight ),
                        gpInput->wasPressed( eInputAction::IA_AccessoriesDown ),
                        gpInput->wasPressed( eInputAction::IA_AccessoriesLeft ),
                        gpInput->wasPressed( eInputAction::IA_MenuOption1 ) );
    // Fast travel from the map (F5 / pad option 2): the point the cursor has
    // snapped to - a shop, or a road - but only on a road that has already been
    // discovered, which is the retail rule (an undiscovered road refuses, and
    // /:IsTeleportOK stays false so the movie shows no travel prompt).
    if ( mapScreen.isOpen() && mapScreen.isUiShown() &&
         ( gpInput->wasKeyPressed( SDL_SCANCODE_F5 ) || gpInput->wasPressed( eInputAction::IA_MenuOption2 ) ) ) {
        Vector3f travelPoint;
        if ( mapScreen.getFastTravelPoint( travelPoint ) ) {
            OTDU_LOG_INFO( "FREERIDE: fast travel to (%.1f, %.1f, %.1f)\n", travelPoint.x, travelPoint.y, travelPoint.z );
            mapScreen.close();
            teleportTo( travelPoint, 0.0f, false );
        } else {
            OTDU_LOG_INFO( "FREERIDE: fast travel refused - that road has not been discovered\n" );
        }
    }
    cheats.drawUI( "FREERIDE" );

    // What the panel asked for.
    Vector3f target;
    float heading = 0.0f;
    bool bHasHeading = false;
    if ( cheats.takeTeleport( target, heading, bHasHeading ) ) {
        teleportTo( target, heading, bHasHeading );
    }
    int32_t weatherIndex = 0;
    if ( cheats.takeWeather( weatherIndex ) ) {
        const std::vector<WeatherPreset> &presets = gameWeathers.getPresets();
        if ( weatherIndex >= 0 && weatherIndex < static_cast<int32_t>( presets.size() ) ) {
            // The panel picks a time of day: pin the cycle to it (the clock and
            // the weather state stand still, a test look; -weather does the same
            // from the command line).
            pWeather = &presets[weatherIndex];
            weatherCycle.setPinned( pWeather );
            // The -intempery force on top of the pinned pick (a no-op without
            // the flag: the cheats panel pick is a time of day).
            applyForcedIntempery();
            view.setWeather( pWeather );
            OTDU_LOG_INFO( "Cheats: weather '%s' (pinned)\n", pWeather->Name.c_str() );
        }
    }
    int32_t storyStep = 0;
    if ( cheats.takeStoryStep( storyStep ) ) {
        GetPlayerProfile().StoryStep = storyStep;
        // A step the story grants a house at (10: the caravan) applies at once,
        // not only after the restart the tip mentions.
        ApplyStoryGrants();
        SaveProfile( "cheat: story step" );
        OTDU_LOG_INFO( "Cheats: story step = %d (a restart uses it)\n", storyStep );
    }
    if ( cheats.takeBackOnTrack() ) {
        backOnTrack();
    }
    // The delock rows that need the world: the map owns the road graph.
    if ( cheats.takeUnlockAllRoads() ) {
        mapScreen.unlockAllRoads();
        SaveProfile( "cheat: all roads" );
    }
    if ( cheats.takeUnlockAllSpots() ) {
        mapScreen.unlockAllSpots();
        SaveProfile( "cheat: all spots" );
    }
    if ( cheats.takeTargetNearestLockedRoad() ) {
        mapScreen.targetNearestUnexplored( carWorld.getTranslation() );
    }
    cheats.setRoadStatus( mapScreen.getExploredPercent(), mapScreen.getNumExploredRoads(),
                          mapScreen.getNumRoadEdges() );
    cheats.setSpotStatus( mapScreen.getNumDiscovered(), mapScreen.getNumIconSpots() );
}

void GMFreeRide::deliverBoughtCar( int32_t carId ) {
    // The cashier hands the keys over: the car is parked outside, on the shop's
    // own car position (the `Entity__CarPos` entities of the exterior bank,
    // car_delivery.cpp), and it becomes the car the player drives - so leaving
    // the shop puts them in it, which is what the retail does.
    CinematicCar::Desc desc;
    if ( !CinematicCar::DescFromDatabase( carId, desc ) ) {
        OTDU_LOG_WARN( "FREERIDE: car %d is not in DBCarPhysicsData: nothing to deliver\n", carId );
        return;
    }
    const std::string spot = !dealerSpotName.empty() ? dealerSpotName : interior.getSpotBank();
    CarDeliveryPose pose;
    if ( !FindCarDeliveryPose( islandName, spot, pose ) ) {
        OTDU_LOG_WARN( "FREERIDE: no delivery position for '%s'\n", spot.c_str() );
        return;
    }
    // The ground under the parking spot (the road / the static collision first,
    // the heightfield second - the same rule the cheats' teleport uses).
    streamWorld( Vector3f( pose.Position.x, pose.Position.y, pose.Position.z ) );
    float surfaceY = pose.Position.y;
    VehicleWorldCollision::Hit hit;
    const Vector3f from( pose.Position.x, pose.Position.y + 400.0f, pose.Position.z );
    if ( collision.raycast( from, Vector3f( 0.0f, -1.0f, 0.0f ), 900.0f, hit ) ) {
        surfaceY = hit.Position.y;
    } else if ( !view.getWorld().getSurfaceHeight( pose.Position.x, pose.Position.z, 4000.0f, surfaceY ) ) {
        surfaceY = pose.Position.y;
    }
    const Vector3f position( pose.Position.x, surfaceY + 0.6f, pose.Position.z );
    OTDU_LOG_INFO( "FREERIDE: delivering '%s' to '%s' (%s) at (%.1f, %.1f, %.1f) heading %.0f deg\n",
                   desc.FileName.c_str(), pose.Name.c_str(), spot.c_str(), position.x, position.y,
                   position.z, pose.Heading * kRadToDeg );
    switchCar( desc, position, pose.Heading );
}

bool GMFreeRide::switchCar( const CinematicCar::Desc &desc, const Vector3f &position, float heading ) {
    // KNOWN ISSUE: replacing the *driven* car's model at runtime wedges the
    // device (the game stops responding a few frames later, CPU idle, a GPU
    // wait - reproduced with -dealer ECD_FERR + a purchase). Draining the
    // device, dropping the shadow caster cache and the ray tracing instances
    // was not enough, so the model change is opt-in until it is understood
    // (-switchcar); the default moves the car the player already drives to the
    // delivery position, which is stable.
    if ( !CmdLine::HasArg( "-switchcar" ) ) {
        const Quaternionf orientation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading );
        vehicle.reset( position, orientation, Vector3f( 0.0f, 0.0f, 0.0f ) );
        carWorld = vehicle.getWorldMatrix();
        chase.reset( position, Vector3f( sinf( heading ), 0.0f, cosf( heading ) ) );
        lastSectorKey = INT32_MIN;
        OTDU_LOG_INFO( "FREERIDE: car at the delivery position (the model change needs -switchcar)\n" );
        return true;
    }
    // Test hook: -switchparts model,audio,hud narrows a hang in the runtime car
    // change (default: all of them).
    const char *pParts = CmdLine::GetArgValue( "-switchparts" );
    const std::string parts = pParts != nullptr ? ToLower( pParts ) : std::string( "model,audio,hud" );
    const bool bModel = parts.find( "model" ) != std::string::npos;
    const bool bAudio = parts.find( "audio" ) != std::string::npos;
    const bool bHud = parts.find( "hud" ) != std::string::npos;
    // The car the player drives becomes the new one: the model, its sounds, the
    // HUD gauges, the wheels / physics and the chase camera - the same steps
    // enter() takes once at startup - then it is put down at the delivery
    // position.
    //
    // The old car's model holds GPU resources and the shadow caster cache holds
    // its materials, so the device is drained and the cache dropped before it
    // goes (the rule of SpotExteriors::unload: a model released while a frame
    // still uses it hangs the device - the first version of this did exactly
    // that and froze the game right after the delivery).
    if ( gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
            // The ray traced reflection instances hold the model's BLAS
            // addresses, which EdenModel::destroy() frees (gs_render rebuilds
            // the list every frame, but a stale address in between wedges the
            // GPU: the first version of this froze the game here).
            pRenderer->clearRtInstances();
        }
    }
    car.unload();
    carDesc = desc;
    ApplyProfileRims( carDesc );
    if ( bModel && !car.load( carDesc ) ) {
        OTDU_LOG_WARN( "FREERIDE: '%s' could not be loaded\n", carDesc.FileName.c_str() );
        return false;
    }
    applyOwnedPaint();
    applyOwnedStickers();
    if ( bAudio && gpAudio != nullptr && gpAudio->isReady() ) {
        std::string soundBank = carDesc.FileName;
        for ( char &c : soundBank ) {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
        soundBank = gpFile->getBankFolder() + "sound/vehicles/" + soundBank + "_high.bnk";
        vehicleAudio.unload();
        if ( !vehicleAudio.load( soundBank.c_str() ) ) {
            OTDU_LOG_WARN( "FREERIDE: no vehicle sounds in '%s'\n", soundBank.c_str() );
        }
    }
    // The gauges are the car's own (`_hud_file_name`).
    if ( bHud ) {
        hud.load( hudFileName() );
    }
    setupVehicle();
    chase.load( setup.CameraId != 0 ? setup.CameraId : setup.Db.CarId );
    const Quaternionf orientation = Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), heading );
    vehicle.reset( position, orientation, Vector3f( 0.0f, 0.0f, 0.0f ) );
    carWorld = vehicle.getWorldMatrix();
    chase.reset( position, Vector3f( sinf( heading ), 0.0f, cosf( heading ) ) );
    // The next tick re-streams around the new position and the map follows.
    lastSectorKey = INT32_MIN;
    OTDU_LOG_INFO( "FREERIDE: now driving '%s'\n", carDesc.FileName.c_str() );
    return true;
}

void GMFreeRide::setShadowConfig( const std::string &name ) {
    if ( name != shadowConfig ) {
        shadowConfig = name;
        view.setShadowConfig( EdenShadows::FindConfig( name ) );
    }
}

void GMFreeRide::streamWorld( const Vector3f &position ) {
    const int32_t key = SectorKey( position );
    const int32_t radius =
        CmdLine::HasArg( "-sectorradius" ) ? CmdLine::GetArgInt( "-sectorradius", 1 ) : 1;
    // Load-ahead (2026-11-04): crossing into a sector used to load the whole
    // new row (3 world sectors + 3 collision sectors, ~55 ms) in that one
    // frame. Now the ring one beyond the streaming square is loaded while the
    // car is still inside its sector - one sector per frame, the one ahead of
    // the heading first, kept hidden until the square reaches it (EdenWorld::
    // preloadOne) - and the banks around are read into RAM by the file cache's
    // background reader. The crossing then finds everything in place.
    // -nopreload = the synchronous streaming (the A/B).
    static const bool bPreload = !CmdLine::HasArg( "-nopreload" );
    if ( bHasStreamPosition ) {
        const Vector3f delta = position - lastStreamPosition;
        const float length = delta.length();
        if ( length > 0.01f && length < 200.0f ) { // not a teleport
            const Vector3f direction = delta * ( 1.0f / length );
            streamHeading = streamHeading * 0.9f + direction * 0.1f;
        }
    }
    lastStreamPosition = position;
    bHasStreamPosition = true;
    if ( key == lastSectorKey ) {
        // Not in the first 30 frames after a sector change: a spawn or a
        // teleport has its own load frames to absorb first.
        if ( bPreload && !bPreloadIdle && ++framesInSector > 30 ) {
            if ( framesInSector == 31 ) {
                // The banks two rings out go to RAM in the background (the
                // world and the collision both read each one) - not at the
                // sector change itself, where the reads competed with the
                // spawn's own loads (frame 1 ~160 -> ~250 ms).
                view.getWorld().prefetchAround( position, radius + 2 );
            }
            const Vector3f ahead = position + streamHeading * 600.0f;
            const uint64_t start = SDL_GetPerformanceCounter();
            const bool bWorld = view.getWorld().preloadOne( position, ahead, radius + 1 );
            static const bool bPreloadCollision = !CmdLine::HasArg( "-nopreloadcollision" );
            const bool bCollision = !bWorld && bPreloadCollision && collision.preloadOne( position, ahead, radius + 1 );
            // Nothing left in the ring: stop scanning it every frame until the
            // car changes sector (or a teleport resets lastSectorKey).
            bPreloadIdle = !bWorld && !bCollision;
            static const bool bLogPreload = CmdLine::HasArg( "-logsectortimes" );
            if ( bLogPreload && ( bWorld || bCollision ) ) {
                OTDU_LOG_INFO( "[sector] preload step (%s) %.1f ms\n", bWorld ? "world" : "collision",
                               static_cast<double>( SDL_GetPerformanceCounter() - start ) * 1000.0 /
                                   static_cast<double>( SDL_GetPerformanceFrequency() ) );
            }
        }
        return;
    }
    lastSectorKey = key;
    bPreloadIdle = false;
    framesInSector = 0;
    // Driving across the island must not keep every sector ever visited
    // resident: drop the ring one sector beyond the load radius (hysteresis,
    // like the world viewer) so the drawn sector count follows the car
    // instead of growing all session.
    if ( view.getWorld().unloadFar( position, radius + 1 ) != 0 ) {
        if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
            pRenderer->clearShadowCasterCache();
        }
    }
    const uint64_t start = SDL_GetPerformanceCounter();
    view.getWorld().loadAround( position, radius );
    const uint64_t worldEnd = SDL_GetPerformanceCounter();
    collision.streamAround( position, 1 );
    static const bool bLogTimes = CmdLine::HasArg( "-logsectortimes" );
    if ( bLogTimes ) {
        const double toMs = 1000.0 / static_cast<double>( SDL_GetPerformanceFrequency() );
        const GSFile::FileCacheStats cache = gpFile->getFileCacheStats();
        OTDU_LOG_INFO( "[sector] streamWorld ms: world %.1f collision %.1f (file cache %u hits / %u misses, %u files "
                       "%.0f MB)\n",
                       static_cast<double>( worldEnd - start ) * toMs,
                       static_cast<double>( SDL_GetPerformanceCounter() - worldEnd ) * toMs, cache.Hits, cache.Misses,
                       cache.Entries, static_cast<double>( cache.Bytes ) / ( 1024.0 * 1024.0 ) );
    }
}

namespace {
// The port's ground classification (eden_surface_fx.h): the tyre surface of the
// car data gives tarmac / dirt / grass, and a low terrain contact is the beach
// (the sand is a texture in the original, it has no surface id of its own; the
// island's shoreline is the only place that is that low).
eGroundMaterial ClassifyGround( const VehicleDynamics::WheelState &wheel ) {
    switch ( wheel.Surface ) {
    case TyreSetup::Tarmac:
        return eGroundMaterial::Tarmac;
    case TyreSetup::Dirt:
        return eGroundMaterial::Dirt;
    default:
        break;
    }
    static const float kBeachHeight = 2.5f; // m above the sea [INF]
    return wheel.ContactPoint.y < kBeachHeight ? eGroundMaterial::Sand : eGroundMaterial::Grass;
}
} // namespace

void GMFreeRide::tickSurfaceFx( float deltaTime ) {
    if ( gpRender == nullptr ) {
        return;
    }
    EdenSurfaceFx &fx = gpRender->getSurfaceFx();
    static const bool bDisabled = CmdLine::HasArg( "-nosurfacefx" );
    static const float scale = static_cast<float>( CmdLine::GetArgInt( "-surfacefxscale", 1 ) );
    static const bool bTest = CmdLine::HasArg( "-surfacefxtest" );
    static const bool bLog = CmdLine::HasArg( "-logsurfacefx" );
    // The brake discs (docs/re/surface_effects_spec.md 7): -nobrakeheat is the
    // A/B of the glow, -brakeheattest <0..100> starts them that hot (a shot
    // without a braking run) and -logbrakeheat prints the temperatures.
    static const bool bLogHeat = CmdLine::HasArg( "-logbrakeheat" );
    fx.setEnabled( !bDisabled );
    fx.setEmissionScale( scale );
    fx.setBrakeGlowEnabled( !CmdLine::HasArg( "-nobrakeheat" ) );
    // The terrain puddles: the height query (the heightfield, carve-aware)
    // and the -nopupdles A/B, wired once.
    // Per instance: the lambda captures this mode (a static flag left the second
    // FREERIDE of a session - back from a shop, a test drive, the car wash -
    // querying the first one's deleted world: an access violation).

    if ( !bPuddlesWired ) {
        bPuddlesWired = true;
        fx.setPuddlesEnabled( !CmdLine::HasArg( "-nopupdles" ) );
        fx.setHeightQuery( [this]( float x, float z, float &outHeight ) {
            return view.getWorld().getHeight( x, z, outHeight );
        } );
        // The light pools' corner query: the road meshes (sector objects) and
        // the tunnels included, scoped to the corner's sector. fromY already
        // carries the cone origin + the climb headroom (EdenSurfaceFx).
        fx.setSurfaceQuery( [this]( float x, float z, float fromY, float &outHeight ) {
            return view.getWorld().getDriveHeight( x, z, fromY, 0.0f, outHeight );
        } );
    }
    // -surfacefxwet <0..100>: start the ground wet (a test hook: with a dry
    // preset the log then shows it drying out, -logsurfacefx).
    static const bool bSeedWet = CmdLine::HasArg( "-surfacefxwet" );
    static bool bWetSeeded = false;
    if ( bSeedWet && !bWetSeeded ) {
        bWetSeeded = true;
        fx.seedWetness( static_cast<float>( CmdLine::GetArgInt( "-surfacefxwet", 100 ) ) / 100.0f );
    }
    // -brakeheattest <0..100>: 0 cold, 100 the top of the dial (1000 C).
    static const bool bSeedHeat = CmdLine::HasArg( "-brakeheattest" );
    static bool bHeatSeeded = false;
    if ( bSeedHeat && !bHeatSeeded ) {
        bHeatSeeded = true;
        fx.seedBrakeHeat( static_cast<float>( CmdLine::GetArgInt( "-brakeheattest", 100 ) ) * 10.0f );
    }
    // The weather preset drives the wetness of the ground: rain soaks it, the
    // sun dries it (the rain states already carry rain_ground_global_intensity
    // for the wet road shaders).
    // The road offset (session 69): the heightfield the light-pool corners
    // query sits below the road meshes (sector objects), so on roads the
    // pools drew under the tarmac and vanished (reported on Hawaii; grass has
    // no mesh above the terrain and worked). Measured at the car every 0.15 s
    // (getSurfaceHeight walks the sector collision triangles), smoothed,
    // clamped to [-0.5, +4] m; on grass surface == heightfield so it decays
    // to 0 and nothing changes.
    {
        static Vector3f lastMeasurePos( FLT_MAX, 0.0f, 0.0f );
        static float measuredOffset = 0.0f;
        static float smoothOffset = 0.0f;
        static float measureTimer = 0.0f;
        const Vector3f carPos = carWorld.getTranslation();
        measureTimer += deltaTime;
        if ( measureTimer >= 0.15f || ( carPos - lastMeasurePos ).dot( carPos - lastMeasurePos ) > 1.2f * 1.2f ) {
            measureTimer = 0.0f;
            lastMeasurePos = carPos;
            float surface = 0.0f, base = 0.0f;
            if ( view.getWorld().getSurfaceHeight( carPos.x, carPos.z, carPos.y + 2.5f, surface ) &&
                 view.getWorld().getHeight( carPos.x, carPos.z, base ) ) {
                measuredOffset = Clamp( surface - base, -0.5f, 4.0f );
            }
        }
        smoothOffset += ( measuredOffset - smoothOffset ) * Min( 1.0f, deltaTime * 5.0f );
        fx.setRoadOffset( smoothOffset );
    }
    const float rain = pWeather != nullptr ? pWeather->getFloat( "rain_ground_global_intensity", 0.0f ) : 0.0f;
    const Vector3f sun = pWeather != nullptr ? pWeather->getVector3( "sun_diffuse", Vector3f( 1.0f, 1.0f, 1.0f ) )
                                             : Vector3f( 1.0f, 1.0f, 1.0f );
    const float sunEnergy = ( sun.x + sun.y + sun.z ) / 3.0f;
    fx.update( deltaTime, rain, sunEnergy );
    // The terrain puddles scan around the car (the wetness of the tarmac
    // drives both their life and their alpha).
    fx.updatePuddles( deltaTime, carWorld.getTranslation(), fx.getWetness().get( eGroundMaterial::Tarmac ) );

    // The car's own dirt / mud / water state (CarDirt): the wheels throw the
    // surface onto the body, the rain washes it off, the sun bakes a crust the
    // rain can hardly remove, and the wash is the only full clean. Parked or in
    // a cinematic the wheels report nothing, but the weather still works on the
    // body - the early return below calls it with zero contacts.
    static const bool bDirtEnabled = !CmdLine::HasArg( "-nodirt" );
    // -dirtseed <0..100>: start the body that dirty (a shot of the paint blend
    // / the crust without a drive).
    static const bool bSeedDirt = CmdLine::HasArg( "-dirtseed" );
    static bool bDirtSeeded = false;
    if ( bSeedDirt && bDirtEnabled && !bDirtSeeded ) {
        bDirtSeeded = true;
        const float v = Clamp( static_cast<float>( CmdLine::GetArgInt( "-dirtseed", 60 ) ) / 100.0f, 0.0f, 1.0f );
        carDirt.seed( v, v * 0.6f, v * 0.7f, 0.2f );
        car.applySurfaceState( carDirt.getMud(), carDirt.getDirt(), carDirt.getMudHardness(), carDirt.getWetness() );
    }
    const auto applyCarDirt = [&]( float mudShare, float dirtShare, float speedKmh ) {
        if ( !bDirtEnabled || !car.isLoaded() ) {
            return;
        }
        carDirt.update( deltaTime, mudShare, dirtShare, speedKmh, rain, sunEnergy );
        car.applySurfaceState( carDirt.getMud(), carDirt.getDirt(), carDirt.getMudHardness(), carDirt.getWetness() );
        carDirt.log( deltaTime );
        // Rain on the body, plus the water film still running off a soaked
        // car after the shower (the drips need no rain of their own).
        if ( rain > 0.001f || carDirt.getWetness() > 0.15f ) {
            Vector3f bodyMin, bodyMax;
            if ( car.getBodyBounds( bodyMin, bodyMax ) ) {
                Vector3f worldMin( FLT_MAX, FLT_MAX, FLT_MAX );
                Vector3f worldMax( -FLT_MAX, -FLT_MAX, -FLT_MAX );
                for ( int32_t c = 0; c < 8; c++ ) {
                    const Vector3f corner( ( c & 1 ) ? bodyMax.x : bodyMin.x, ( c & 2 ) ? bodyMax.y : bodyMin.y,
                                           ( c & 4 ) ? bodyMax.z : bodyMin.z );
                    const Vector3f w = carWorld.transformPoint( corner );
                    worldMin = Vector3f( Min( worldMin.x, w.x ), Min( worldMin.y, w.y ), Min( worldMin.z, w.z ) );
                    worldMax = Vector3f( Max( worldMax.x, w.x ), Max( worldMax.y, w.y ), Max( worldMax.z, w.z ) );
                }
                fx.emitRainImpacts( deltaTime, worldMin, worldMax, rain, speedKmh, carDirt.getWetness() );
            }
        }
    };

    // Only the player's driving car throws anything around: the cinematics freeze
    // the physics, so the wheel states are stale there. The body still soaks in
    // the rain and dries in the sun while parked.
    if ( state != eState::Drive || !car.isLoaded() ) {
        applyCarDirt( 0.0f, 0.0f, 0.0f );
        if ( bLog ) {
            fx.logStats();
        }
        if ( bLogHeat ) {
            fx.logBrakeHeat();
        }
        return;
    }
    // The wheel state carries no wheel velocity of its own: the body's velocity
    // is the wheel's travel (they only differ while the tyre slips, which the
    // slip term of the emission covers).
    const Vector3f carVelocity = vehicle.getVelocity();
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const std::vector<VehicleDynamics::WheelState> &wheels = vehicle.getWheels();
    const std::vector<VehicleDynamics::WheelDesc> &wheelDescs = vehicle.getWheelDescs();
    // The share of the grounded wheels on mud / dry dirt feeds one body stain
    // (CarDirt), not a per-wheel one.
    float mudContacts = 0.0f;
    float dirtContacts = 0.0f;
    float groundedWheels = 0.0f;
    for ( size_t i = 0; i < wheels.size() && i < EdenSurfaceFx::kMaxWheels; i++ ) {
        const VehicleDynamics::WheelState &wheel = wheels[i];
        const bool bHasDesc = i < wheelDescs.size();
        const bool bFront = bHasDesc ? wheelDescs[i].bFront : true;
        EdenSurfaceFx::WheelContact contact;
        contact.bContact = wheel.bContact;
        contact.Position = wheel.ContactPoint;
        contact.Normal = wheel.ContactNormal;
        contact.Velocity = carVelocity;
        contact.Load = wheel.Load;
        contact.Slip = fabsf( wheel.SlipRatio ) + fabsf( wheel.SlipAngle );
        contact.Material = ClassifyGround( wheel );
        if ( contact.bContact ) {
            groundedWheels += 1.0f;
            const bool bMud = fx.getWetness().isMud();
            if ( contact.Material == eGroundMaterial::Dirt ) {
                ( bMud ? mudContacts : dirtContacts ) += 1.0f;
            } else if ( contact.Material == eGroundMaterial::Grass && bMud ) {
                dirtContacts += 0.5f; // wet grass still spits a little soil
            }
        }
        // Hard braking (docs/re/surface_effects_spec.md 7): the pad torque the
        // controller applies heats the disc, and the share of the tyre's
        // friction it uses is what leaves the mark. The slip ratio alone misses
        // a stop held by the ABS (it keeps the ratio under the slide
        // threshold), which is why the owner saw no marks from a hard stop.
        const float frictionLimit = Max( wheel.Load * wheel.SurfaceFriction, 1.0f );
        contact.BrakeSlip = Saturate( ( Max( -wheel.LongitudinalForce, 0.0f ) / frictionLimit - 0.45f ) / 0.4f );
        contact.BrakeTorque = vehicle.getBrakeTorque( i );
        contact.bFront = bFront;
        if ( bHasDesc ) {
            // The disc: the hub (with the suspension travel) in the wheel's own
            // steering frame. The car frame is +x left, so the outward axle
            // direction is +x on the left wheel and -x on the right one.
            const VehicleDynamics::WheelDesc &desc = wheelDescs[i];
            const Matrix4x4f steer = Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle(
                Vector3f( 0.0f, 1.0f, 0.0f ), bFront ? vehicle.getSteerAngle() : 0.0f ) );
            contact.WheelRadius = desc.Radius;
            contact.DiscCenter = carWorld.transformPoint(
                desc.HubLocal + Vector3f( 0.0f, wheel.Compression - vehicle.getModelLift(), 0.0f ) );
            contact.DiscAxis = carWorld.transformVector( steer.transformVector( Vector3f( 1.0f, 0.0f, 0.0f ) ) ) *
                               ( desc.bLeft ? 1.0f : -1.0f );
            contact.DiscUp = carWorld.transformVector( steer.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) ) );
            // The disc is smaller than the tyre (a 380 mm disc on a 19" wheel).
            contact.DiscRadius = desc.Radius * 0.63f;
        }
        if ( bTest ) {
            // -surfacefxtest: a synthetic contact under every wheel, so a
            // headless run on any surface proves the pipeline and the shaders
            // (the front wheels throw sand, the rear ones dirt).
            contact.bContact = true;
            contact.Material = bFront ? eGroundMaterial::Sand : eGroundMaterial::Dirt;
            contact.Velocity = forward * 12.0f;
            contact.Slip = 0.1f;
        }
        fx.emitWheel( static_cast<uint32_t>( i ), contact, deltaTime );
    }
    const float contactsInv = groundedWheels > 0.0f ? 1.0f / groundedWheels : 0.0f;
    applyCarDirt( mudContacts * contactsInv, dirtContacts * contactsInv, carVelocity.length() * 3.6f );
    if ( bLog ) {
        fx.logStats();
    }
    if ( bLogHeat ) {
        fx.logBrakeHeat();
    }
}

void GMFreeRide::updateWheels() {
    if ( !car.isLoaded() ) {
        return;
    }
    EdenModel &model = car.getModel();
    const std::vector<VehicleDynamics::WheelState> &states = vehicle.getWheels();
    // C_FIRST_OUTRO: the physics is not stepped (the cinematic owns the car), so
    // the rolling angle comes from the track velocity (outroWheelSpin,
    // integrated by the Outro tick) instead of the wheel state.
    const bool bCinematic = state == eState::Outro;
    const size_t count = bCinematic ? wheelNodes.size() : Min( wheelNodes.size(), states.size() );
    for ( size_t i = 0; i < count; i++ ) {
        const WheelNode &node = wheelNodes[i];
        // Car frame: steering about +y (positive = left), rolling about +x.
        const float steer = !bCinematic && node.bFront ? vehicle.getSteerAngle() : 0.0f;
        const float spin = bCinematic ? outroWheelSpin : states[i].SpinAngle;
        const Matrix4x4f motion =
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0, 1, 0 ), steer ) ) *
            Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 1, 0, 0 ), spin ) );
        const Vector3f lift( 0.0f, vehicle.getWheelOffset( i ), 0.0f );
        model.setNodeTransform( node.RimNode, Matrix4x4f::Translation( node.RimRest.getTranslation() + lift ) * motion *
                                                  WithoutTranslation( node.RimRest ) );
        model.setNodeTransform( node.RimNode + 1, Matrix4x4f::Translation( node.TireRest.getTranslation() + lift ) * motion *
                                                      WithoutTranslation( node.TireRest ) );
    }
}

void GMFreeRide::ensureModPropsLoaded() {
    // getEdenRenderer() is null until the Eden renderer readies (the first
    // world draw), which is after enter() - the props silently never loaded
    // from there (and the map's fuel stations stayed empty). One attempt per
    // tick until it exists; loadFile("props.txt") missing is the normal case.
    if ( bModPropsTried ) {
        return;
    }
    EdenRenderer *pEdenRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
    if ( pEdenRenderer == nullptr ) {
        return;
    }
    bModPropsTried = true;
    modProps.load( *pEdenRenderer, islandName );
    std::vector<Vector3f> stations;
    for ( const ModProps::Prop &prop : modProps.getProps() ) {
        if ( prop.bFuelStation ) {
            stations.push_back( prop.Position );
        }
    }
    mapScreen.setFuelStations( stations );
}

void GMFreeRide::tick( float deltaTime ) {
    if ( !bLoaded ) {
        return;
    }
    // Sharp by default; the driving camera below sets the retail speed blur.
    view.setSpeedBlur( 0.0f );
    ensureModPropsLoaded();
    if ( dreamWhiteIn >= 0.0f ) {
        dreamWhiteIn += deltaTime;
        if ( dreamWhiteIn >= kDreamWhiteIn ) {
            dreamWhiteIn = -1.0f;
        }
    }
    // World map (FrontEnd/HiRes/map.bnk): the retail HUD's MAP action — the
    // pad's back button (Select / Share) and its keyboard equivalent. The world
    // is frozen while it is up, like the pause menu.
    if ( mapScreen.isOpen() ) {
        // Back / the map key again closes it; the screen's own IsBackFromMap /
        // IsMapQuit / MapCancel do the same from inside (MapScreen::tick).
        if ( gpInput->wasPressed( eInputAction::IA_MenuBack ) || gpInput->wasPressed( eInputAction::IA_Map ) ||
             gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            if ( !mapScreen.onBack() ) { // BACK first leaves the credits
                mapScreen.close();
            }
            return;
        }
        mapScreen.tick( deltaTime );
        mapScreen.updateCamera( deltaTime );
        {
            const Vector3f fwd = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
            const Vector3f flatFwd = Vector3f( fwd.x, 0.0f, fwd.z ).normalized();
            mapScreen.setPlayer( carWorld.getTranslation(), flatFwd.x, flatFwd.z );
        }
        // OK sets / clears the GPS destination at the cursor
        if ( mapScreen.isUiShown() && gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            mapScreen.toggleTarget();
        }
        if ( vehicleAudio.isEngineOn() ) {
            VehicleAudio::Params muted;
            muted.bEngineOn = false; // the engine sounds duck with the world
            vehicleAudio.update( muted, deltaTime );
        }
        if ( mapScreen.isFlat() ) {
            return; // only the flat map is up: nothing to render in 3D
        }
        // The camera flies from the car to the map camera and back; at the closer
        // zoom levels the island under the movie is the streamed world.
        EdenSceneView::Camera mapCamera;
        mapScreen.getTransitionCamera( mapCamera );
        camera = mapCamera;
        if ( mapScreen.getZoomLevel() >= 1 && mapScreen.getTransition() >= 1.0f ) {
            const Vector3f mapCenter = mapScreen.getCenter();
            const int32_t mapRadius = mapScreen.getStreamRadius();
            // The owner's map freeze: panning crosses a sector boundary and the
            // whole (2r+1)^2 square loaded synchronously - radius 4 at the two
            // far zoom levels meant up to 81 bank loads (and as many unloads)
            // in one frame, fps 120 -> 19 with 223 ms updates (session 52).
            // Spread it instead: a small budget every frame; the loaded /
            // failed-sector guards make the settled frames cheap. One sector
            // per frame keeps the fill frames near ~20 ms (one bank load); a
            // whole far-zoom square (~81 sectors) lands over ~1.4 s.
            view.getWorld().loadAround( mapCenter, mapRadius, 1 );
            // The unloaded sectors' materials live in the renderer's caster /
            // Z-pass caches (EdenWorld::unloadFar, eden_world.h): the next
            // shadow pass iterates them and writes descriptors with the freed
            // image views. The validation layers named it - "vkUpdateDescriptorSets:
            // pImageInfo[0].imageView Invalid VkImageView" right after "1 sectors
            // unloaded", and "vkCmdDrawIndexed: ... binding 13 (t0_2d) is using
            // imageView VkImageView 0x0 that is invalid or has been destroyed" -
            // which is the "odd textures stamped on the map" and the GPU reset
            // (VK_ERROR_DEVICE_LOST) a few frames later. Every other unloadFar
            // caller (streamWorld, the world viewer, the intro, the chase) drops
            // them; the map's own streaming did not.
            if ( view.getWorld().unloadFar( mapCenter, mapRadius + 1, 1 ) != 0 ) {
                if ( EdenRenderer *pRenderer = gpRender->getEdenRenderer() ) {
                    pRenderer->clearShadowCasterCache();
                }
            }
            // The buildings the map flies over: the same streamer as the drive,
            // but only at the closer levels (level 1 looks at the island from
            // 10 km, where a building is a few pixels and the stream radius
            // would hold every bank on the island). Called every frame: the
            // streamer spreads a dense area over several frames.
            if ( mapScreen.getZoomLevel() >= 2 && spotExteriors.tick( view, mapCenter, bSpotExteriors ) &&
                 outro.isLoaded() ) {
                outro.registerLightmaps( view );
            }
            lastSectorKey = INT32_MIN; // re-stream around the car when the map closes
        } else {
            streamWorld( carWorld.getTranslation() );
        }
        // no fog / haze from the first frame of the fly (they wash the climb out
        // white); the sky stays until the camera is high above it
        view.setMapView( true, mapScreen.getTransition() < 0.55f );
        view.setCamera( camera );
        view.tick( deltaTime );
        return;
    }
    // The licence test result screen (RESULTS.vmf): the world is frozen behind
    // it, like the map; its menu retries, starts the next test or goes back to
    // the school.
    if ( licenseResult.isOpen() ) {
        if ( vehicleAudio.isEngineOn() ) {
            VehicleAudio::Params muted;
            muted.bEngineOn = false;
            vehicleAudio.update( muted, deltaTime );
        }
        tickLicenseResult( deltaTime );
        view.setCamera( camera );
        view.tick( deltaTime );
        return;
    }
    // Car dealership / clothes shop (native menus): the world is frozen while
    // one is up.
    if ( dealer.isOpen() ) {
        // The retail test drive (the dealer menu's Test drive entry, gm_shop's
        // startTestDrive fills the same request): 120 s in the car, back to
        // FREERIDE at the dealer (gm_testcar honors the ReturnMode).
        DealerTestDrive testDrive;
        if ( dealer.takeTestDrive( testDrive ) ) {
            TestDriveRequest &request = GetTestDriveRequest();
            request = TestDriveRequest();
            request.bActive = true;
            request.IdCar = testDrive.CarId;
            request.ColorId = testDrive.ColorId;
            request.RimsHashcode = testDrive.RimsHashcode;
            request.Duration = 120.0f;
            request.ReturnMode = static_cast<int32_t>( eGameMode::GM_FreeRide );
            request.Spot = interior.getSpotBank();
            request.Island = islandName;
            // No passenger: the freeride dealers have no Entity__PNJ named by
            // the story shop (the retail dealer drive rides with Jude [INF]).
            hud.closeEnterSpot();
            hud.closeHint();
            pGameInstance->setNextGameMode( eGameMode::GM_TestCar );
            return;
        }
        dealer.tick( deltaTime );
        // In a showroom, the selected car (and the colour being chosen) is shown
        // from its podium's camera; the walk camera comes back with the menu.
        int32_t previewCarId = -1, previewPaint = -1;
        if ( dealer.takePreview( previewCarId, previewPaint ) ) {
            interior.previewCar( previewCarId, previewPaint );
        }
        if ( !dealer.isOpen() ) {
            interior.endPreview();
        }
        interior.tickBackground( deltaTime );
        // A purchase is delivered once the menu is closed (the cashier hands
        // the keys over: the car is outside, deliverBoughtCar).
        int32_t boughtCarId = -1;
        if ( dealer.takeBoughtCar( boughtCarId ) ) {
            pendingDeliveryCarId = boughtCarId;
        }
        return;
    }
    if ( pendingDeliveryCarId >= 0 ) {
        const int32_t carId = pendingDeliveryCarId;
        pendingDeliveryCarId = -1;
        deliverBoughtCar( carId );
    }
    if ( clothesMenu.isOpen() ) {
        clothesMenu.tick( deltaTime );
        interior.tickBackground( deltaTime );
        return;
    }
    if ( estate.isOpen() ) {
        estate.tick( deltaTime );
        interior.tickBackground( deltaTime );
        return;
    }
    if ( tuningMenu.isOpen() ) {
        tuningMenu.tick( deltaTime );
        interior.tickBackground( deltaTime );
        // A rim size (or a tuning level) bought in the panel takes effect as
        // the panel closes: the tire meshes were remapped at load, so the rim
        // change is a node / scale selection, no model reload.
        if ( !tuningMenu.isOpen() ) {
            applyTuning();
        }
        return;
    }
    // The pump screen (fuel_menu): its own little screen, the world keeps living.
    if ( fuelMenu.isOpen() ) {
        fuelMenu.tick( deltaTime );
        interior.tickBackground( deltaTime );
        return;
    }
    if ( paintMenu.isOpen() ) {
        paintMenu.tick( deltaTime );
        interior.tickBackground( deltaTime );
        // The paint bought is applied to the car you drove in with.
        int32_t paintCarId = -1, paintRow = -1;
        if ( paintMenu.takeAppliedPaint( paintCarId, paintRow ) && paintCarId == car.getIdCar() ) {
            const std::string name = car.applyDatabasePaint( paintRow );
            OTDU_LOG_INFO( "FREERIDE: '%s' repainted: row %d '%s'\n", carDesc.FileName.c_str(), paintRow, name.c_str() );
        }
        return;
    }
    if ( schoolScreen.isOpen() ) {
        schoolScreen.tick( deltaTime );
        interior.tickBackground( deltaTime );
        std::string test;
        if ( schoolScreen.takeStartedTest( test ) ) {
            startLicenseTest( test );
        }
        return;
    }
    if ( schoolMenu.isOpen() ) {
        schoolMenu.tick( deltaTime );
        interior.tickBackground( deltaTime );
        std::string test;
        if ( schoolMenu.takeStartedTest( test ) ) {
            startLicenseTest( test );
        }
        return;
    }
    if ( championshipMenu.isOpen() ) {
        championshipMenu.tick( deltaTime );
        std::string race;
        if ( championshipMenu.takeStartedRace( race ) ) {
            startLicenseTest( race );
        }
        return;
    }
    // -interior <spot>: straight into a shop interior from the spawn (the
    // -housegarage test hook): no drive to the door, no prompt.
    static const char *pForcedInterior = CmdLine::GetArgValue( "-interior" );
    if ( pForcedInterior != nullptr ) {
        interior.enter( pForcedInterior, islandName );
        bindStickerShop();
        pForcedInterior = nullptr;
    }
    // -enterhouse <DBHouse spot>: straight into that house's living room (any
    // house, owned or not: a test hook for the per-house checks).
    static const char *pForcedHouse = CmdLine::GetArgValue( "-enterhouse" );
    if ( pForcedHouse != nullptr && !interior.isActive() ) {
        const HouseRow *pHouse = nullptr;
        for ( const HouseRow &house : GetHouses() ) {
            if ( StringCmpI( house.SpotName.c_str(), pForcedHouse ) == 0 ) {
                pHouse = &house;
            }
        }
        if ( pHouse != nullptr && interior.enterHouse( *pHouse, false, islandName ) ) {
            interiorKind = "house";
            OTDU_LOG_INFO( "FREERIDE: -enterhouse '%s'\n", pHouse->SpotName.c_str() );
        } else {
            OTDU_LOG_WARN( "FREERIDE: -enterhouse '%s': no such house\n", pForcedHouse );
        }
        pForcedHouse = nullptr;
    }
    // Out of a sticker shop room: an unpaid layout goes back on the car.
    if ( stickerShop.isBound() && !interior.isActive() ) {
        stickerShop.unbind();
        pStickerRoomCar = nullptr;
    }
    // Inside a spot interior (InteriorWalk): the island and the car are
    // frozen behind it, the engine is off.
    if ( interior.isActive() ) {
        // The sticker place mode's camera cut: once per entry, a 3/4 view of
        // the car outside (the retail editor's garage-car camera); the pose
        // holds while the room is frozen behind the editor.
        if ( stickerCarView() && !bStickerPreviewCamera && pStickerRoomCar != nullptr ) {
            bStickerPreviewCamera = true;
            stickerCamYaw = 0.6f;
            stickerCamPitch = 0.3f;
            stickerCamDistance = 5.5f;
        } else if ( stickerCarView() && !bStickerPreviewCamera && car.isLoaded() ) {
            bStickerPreviewCamera = true;
            const Vector3f carPos = carWorld.getTranslation();
            const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
            const float yaw = std::atan2( forward.x, forward.z );
            // Three quarters from the rear left, above the paint line.
            stickerCamYaw = yaw + 2.35f;
            stickerCamPitch = 0.35f;
            stickerCamDistance = 6.5f;
            OTDU_LOG_INFO( "FREERIDE: sticker editor camera orbit around (%.1f, %.1f, %.1f)\n", carPos.x, carPos.y,
                           carPos.z );
        } else if ( !stickerCarView() ) {
            bStickerPreviewCamera = false;
        }
        if ( stickerCarView() && bStickerPreviewCamera && ( car.isLoaded() || pStickerRoomCar != nullptr ) ) {
            // The editor's "Move camera" (right stick, or the mouse with the
            // right button held) and "Zoom" (wheel) around the car [DATA: the
            // movie's help bar names both; the controls are INF].
            float orbitX = gpInput->getRightStickX(), orbitY = gpInput->getRightStickY();
            if ( gpInput->isMouseButtonDown( 1 ) ) {
                orbitX += gpInput->getMouseDeltaX() * 0.25f;
                orbitY += gpInput->getMouseDeltaY() * 0.25f;
            }
            if ( interior.isStickUi() ) {
                // The movie's "Move camera" icon is the mouse and the D-pad.
                orbitX += ( gpInput->isDown( eInputAction::IA_MenuRight ) ? 1.0f : 0.0f ) -
                          ( gpInput->isDown( eInputAction::IA_MenuLeft ) ? 1.0f : 0.0f );
                orbitY += ( gpInput->isDown( eInputAction::IA_MenuDown ) ? 1.0f : 0.0f ) -
                          ( gpInput->isDown( eInputAction::IA_MenuUp ) ? 1.0f : 0.0f );
            }
            if ( std::fabs( orbitX ) < 0.12f ) orbitX = 0.0f;
            if ( std::fabs( orbitY ) < 0.12f ) orbitY = 0.0f;
            stickerCamYaw += orbitX * 1.8f * deltaTime;
            stickerCamPitch = Clamp( stickerCamPitch - orbitY * 1.2f * deltaTime, 0.02f, 1.3f );
            stickerCamDistance = Clamp( stickerCamDistance - gpInput->getMouseWheel() * 0.6f, 2.5f, 14.0f );
            if ( pStickerRoomCar != nullptr ) {
                // The room's own car: the orbit is scene-local around it.
                Vector3f local;
                int32_t roomCarId = -1;
                if ( interior.getScene().getDisplayCarInfo( 0, local, roomCarId ) ) {
                    const Vector3f focus = local + Vector3f( 0.0f, 0.8f, 0.0f );
                    const Vector3f eye = focus + Vector3f( std::sin( stickerCamYaw ) * std::cos( stickerCamPitch ),
                                                           std::sin( stickerCamPitch ),
                                                           std::cos( stickerCamYaw ) * std::cos( stickerCamPitch ) ) *
                                                     stickerCamDistance;
                    interior.getScene().setLocalCamera( eye, ChaseCamera::LookRotation( eye, focus ), 55.0f * kDegToRad, 0.1f );
                }
            } else {
            const Vector3f target = carWorld.getTranslation() + Vector3f( 0.0f, 0.9f, 0.0f );
            camera.Position = target + Vector3f( std::sin( stickerCamYaw ) * std::cos( stickerCamPitch ),
                                                 std::sin( stickerCamPitch ),
                                                 std::cos( stickerCamYaw ) * std::cos( stickerCamPitch ) ) *
                                           stickerCamDistance;
            camera.Rotation = ChaseCamera::LookRotation( camera.Position, target );
            camera.Fov = 55.0f * kDegToRad;
            camera.FovScale = 1.0f;
            view.setMapView( false );
            view.setCamera( camera );
            view.tick( deltaTime );
            }
        }
        if ( vehicleAudio.isEngineOn() ) {
            VehicleAudio::Params muted;
            muted.bEngineOn = false;
            vehicleAudio.update( muted, deltaTime );
        }
        if ( interior.isStickUi() ) {
            stickerShop.tick( deltaTime );
        }
        InteriorWalk::eResult interiorResult = interior.tick( deltaTime );
        if ( interior.takeLeaveRequest() ) {
            interiorResult = InteriorWalk::eResult::Leave; // the movie's gofr
        }
        switch ( interiorResult ) {
        case InteriorWalk::eResult::Leave: {
            // A car picked in the house garage: FREERIDE again at the house's
            // road-side entrance in it (the mode loads the profile's car at
            // enter; the runtime model swap is the known device wedge).
            if ( interior.isHouse() && GetPlayerProfile().DrivenCarId != drivenCarIdAtEnter ) {
                StoryHandoff &handoff = GetStoryHandoff();
                handoff = StoryHandoff();
                handoff.bSet = true;
                handoff.Position = carWorld.getTranslation();
                handoff.Yaw = std::atan2( carWorld.m[2][0], carWorld.m[2][2] );
                handoff.Island = islandName;
                handoff.EntranceSpot = interior.getHouseSpot();
                OTDU_LOG_INFO( "FREERIDE: out of the house in the car %d -> FREERIDE at '%s'\n",
                               GetPlayerProfile().DrivenCarId, handoff.EntranceSpot.c_str() );
                interior.exit();
                pGameInstance->setNextGameMode( eGameMode::GM_FreeRide );
                return;
            }
            const std::string leftBank = interior.getSpotBank(); // exit() clears it
            interior.exit();
            // Out of the door (the owner, 2026-11-05: "houses out side the
            // house that you was in"): the car waits at the spot's road-side
            // entrance - the spots.bin second point, the point the dealer exit
            // and the house garage exit already use - not wherever it was
            // left when the enter prompt was taken (up to the prompt radius
            // away) [INF: the retail walk-out is encrypted; the entrance is
            // the one exit point the data names].
            {
                Vector3f entrance;
                float entranceHeading = 0.0f;
                if ( interiorSpotId != 0 &&
                     FindSpotEntranceById( islandName, interiorSpotId, entrance, entranceHeading ) ) {
                    OTDU_LOG_INFO( "FREERIDE: out of '%s' -> the car at its entrance (%.1f, %.1f, %.1f)\n",
                                   leftBank.c_str(), entrance.x, entrance.y, entrance.z );
                    teleportTo( entrance, entranceHeading, true );
                }
            }
            // A tuning bought inside the shop takes effect as the player gets
            // back in the car (the physics is re-seeded from the base setup).
            applyTuning();
            OTDU_LOG_INFO( "FREERIDE: back in the car\n" );
            break;
        }
        case InteriorWalk::eResult::Shop:
            if ( !openSpotShop( interiorKind, interior.getSpotBank(), interiorSpotId ) ) {
                // Surgeon, club, tuning ...: walkable, no menu yet.
                interior.showMessage( "This shop is not open yet." );
            }
            break;
        default:
            break;
        }
        // The gas pump of the house garage (docs/re/fuel_gas_station_spec.md 4.1:
        // the only pump model of the game is the gph_l2d garage one). The interior
        // tick above gets the key first, so a selectable always wins; a room
        // swap (Garage / Living) consumed the press this tick - the pump menu
        // must not open on the very press that loaded the room.
        if ( interior.isHouse() && interior.isInGarage() && !fuelMenu.isOpen() && !interior.tookRoomSwap() ) {
            // Next to one of your cars (within 3 m of its slot): pick it as the
            // car to drive out (PlayerProfile::DrivenCarId; leaving the house
            // re-enters FREERIDE in it) [INF: the retail garage's
            // "validation de la voiture a changer" flow is HOME.vmf's /garage
            // page, not decoded]. Elsewhere in the garage: the pump.
            const int32_t nearCar = interior.findNearDisplayCar( 3.0f );
            const int32_t nearCarId = nearCar >= 0 ? interior.getDisplayCarCarId( nearCar ) : -1;
            PlayerProfile &profile = GetPlayerProfile();
            if ( nearCarId >= 0 ) {
                const bool bCurrent = nearCarId == profile.DrivenCarId;
                hud.setPrompt( bCurrent ? FindCarDisplayName( nearCarId ) + ": the car you drive"
                                        : "A / Enter: drive the " + FindCarDisplayName( nearCarId ) );
                if ( !bCurrent && gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
                    profile.DrivenCarId = nearCarId;
                    SaveProfile( "garage: driven car" );
                    interior.showMessage( "You will drive the " + FindCarDisplayName( nearCarId ) + " out." );
                    OTDU_LOG_INFO( "FREERIDE: garage: driven car -> %d '%s'\n", nearCarId,
                                   FindCarDisplayName( nearCarId ).c_str() );
                }
            } else {
                hud.setPrompt( std::string( "A / Enter: " ) + LocStr( 1642, "Fill" ) );
                if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
                    hud.setPrompt( "" );
                    fuelMenu.open( "your garage" );
                }
            }
        }
        // The near-car spec card in a dealership (the retail showroom shows the
        // car's data as you walk up to it): the display car within 5 m of the
        // walk position. Only where cars stand (the houses / clothes shops have
        // none); the menus' branches returned before this.
        if ( interiorKind == "cardealer" ) {
            const SpecCardData *pCard = nullptr;
            const int32_t display = interior.findNearDisplayCar( 5.0f );
            if ( display >= 0 ) {
                pCard = CachedSpecCard( interior.getDisplayCarCarId( display ) );
            }
            if ( pCard != nullptr ) {
                hud.showCarSpecCard( pCard->Name, pCard->Price, pCard->Spec );
            } else {
                hud.closeCarSpecCard();
            }
        } else {
            hud.closeCarSpecCard();
        }
        return;
    }
    // Pause menu (MAIN.vmf): ESC opens it, ESC again (or "Resume") closes it.
    // The world is frozen while it is up (the HUD keeps its last state).
    if ( pauseMenu.isOpen() ) {
        if ( gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
            // ESC is bound to MENU_PAUSE and MENU_BACK: close, do not send
            // the "back" key of the movie (that is the options screen).
            pauseMenu.close();
            return;
        }
        hud.tickSavingLogo( deltaTime );
        pauseMenu.tick( deltaTime );
        if ( vehicleAudio.isEngineOn() ) {
            VehicleAudio::Params muted;
            muted.bEngineOn = false; // the engine sounds duck with the world
            vehicleAudio.update( muted, deltaTime );
        }
        switch ( pauseMenu.takeResult() ) {
        case PauseMenu::eResult::Resume:
            pauseMenu.close();
            break;
        case PauseMenu::eResult::Photo:
            // "Take photo": the free camera opens once the pause menu's out
            // animation has finished (the menu owns the frame until then).
            pauseMenu.close();
            bPendingPhoto = true;
            break;
        case PauseMenu::eResult::Quit:
            pauseMenu.close();
            OTDU_LOG_INFO( "FREERIDE: quit from the pause menu -> LOGIN\n" );
            pGameInstance->setNextGameMode( eGameMode::GM_Login );
            break;
        default:
            break;
        }
        return;
    }
    // The "Help: Chase Mode" popup (HUD help system): modal like the pause
    // menu - the world waits, the pad drives the popup (A / B close it, left
    // / right turn the pages) until the movie reports it closed.
    if ( hud.isChaseHelpOpen() ) {
        hud.routeChaseHelpInput();
        hud.tickModal( deltaTime );
        if ( vehicleAudio.isEngineOn() ) {
            VehicleAudio::Params muted;
            muted.bEngineOn = false;
            vehicleAudio.update( muted, deltaTime );
        }
        return;
    }
    // "Take photo" (PhotoCamera): the pause menu has closed, open the camera at
    // the car camera's pose. The world keeps running (the retail photo mode is
    // live), the HUD is hidden so the photo is clean.
    if ( bPendingPhoto && !pauseMenu.isOpen() ) {
        bPendingPhoto = false;
        hud.setVisible( false );
        hud.setHidden( true ); // no goal bar / progress clip in the photo either
        photoCamera.open( carWorld.getTranslation(), camera );
    }
    if ( photoCamera.isOpen() ) {
        if ( photoCamera.tick( deltaTime, carWorld.getTranslation() ) ) {
            photoCamera.close();
            hud.setHidden( false );
            hud.setVisible( state == eState::Drive || state == eState::Knock || state == eState::Fade );
            return;
        }
        photoCamera.getCamera( camera );
        // The buildings stream around the free camera (it can fly away from the
        // car), like the drive.
        if ( spotExteriors.tick( view, camera.Position, bSpotExteriors ) && outro.isLoaded() ) {
            outro.registerLightmaps( view );
        }
        view.setCamera( camera );
        view.tick( deltaTime );
        return;
    }
    // World map: the MAP action (the pad's back button — Select / Share — and
    // its keyboard equivalent) opens it. Same states as the pause menu: not
    // during a cinematic, the wake-up or the knock.
    if ( state != eState::Outro && state != eState::Wakeup && state != eState::Knock && mapScreen.isLoaded() &&
         gpInput->wasPressed( eInputAction::IA_Map ) ) {
        mapScreen.resetCamera( carWorld.getTranslation() );
        {
            // [INF] the localized brand string plus the model name; the retail
            // header shows the DB's full car name, whose key is not decoded.
            const std::string carName = setup.Db.ModelName;
            OTDU_LOG_INFO( "MapScreen: header car '%s'\n", carName.c_str() );
            mapScreen.setPlayerInfo( carName, 0.0 ); // no money system yet
        }
        OTDU_LOG_INFO( "MapScreen: car at (%.0f, %.0f)\n", carWorld.getTranslation().x, carWorld.getTranslation().z );
        mapScreen.setStartCamera( camera );
        mapScreen.open();
        return;
    }
    if ( state != eState::Outro && state != eState::Wakeup && state != eState::Knock && pauseMenu.isLoaded() &&
         gpInput->wasPressed( eInputAction::IA_MenuPause ) ) {
        pauseMenu.open();
        return;
    }
    switch ( state ) {
    case eState::Outro: {
        outro.tick( deltaTime );
        outro.getCamera( camera );
        carWorld = outroCarWorld();
        // C_FIRST_OUTRO drives the car ~350 m down the drive (the .sce
        // PLAYER_CAR track) with the physics frozen: roll the rims / tyres at
        // the track speed, or the car slides along on four locked wheels.
        {
            const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
            outroWheelSpin +=
                outroVelocity.dot( forward ) / Max( carDesc.TireRadius, 0.01f ) * deltaTime;
        }
        // The engine of the drive-away: retail's C_FIRST_OUTRO pulls off with
        // the engine audible. The physics is not stepped (the cinematic owns
        // the car), so the engine starts here and its rpm / gear follow the
        // track velocity through VehicleDynamics' own laws - the gear for the
        // speed (FUN_00885730 rule, vehicle_dynamics.cpp 721) and rpm =
        // |speed| / radius * ratio clamped to the idle / limiter (396).
        if ( !vehicleAudio.isEngineOn() ) {
            vehicleAudio.startEngine();
        }
        {
            const float radius = Max( carDesc.TireRadius, 0.01f );
            const float forwardSpeed = outroVelocity.dot( carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
            int32_t gear = 2;
            while ( gear < static_cast<int32_t>( setup.Gears.size() ) + 1 &&
                    forwardSpeed > 0.9f * setup.EngineMaxVelocity / outroGearRatio( gear ) * radius ) {
                gear++;
            }
            outroGear = gear;
            outroSpeed = forwardSpeed;
            outroRpm = Clamp( std::abs( forwardSpeed ) / radius * outroGearRatio( gear ), setup.IdleVelocity,
                              setup.EngineMaxVelocity );
        }
        // The car drives off down the road: the sectors follow it, as in the
        // wake-up scene below (the stream centre used to be read before
        // outro.locate(), so the world streamed at (0, 0)).
        streamWorld( carWorld.getTranslation() );
        setShadowConfig( outro.getShadowConfig() );
        // Skippable when FUN_00425d70 (CinProps |= 4) [INF: always].
        if ( !outro.isPlaying() || gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            startDriving();
        }
        break;
    }
    case eState::Wakeup: {
        // C_FADE_FREERIDE: fixed scene camera, Tess at the window, the car
        // parked (physics not stepped). Skippable like the outro.
        wakeup.tick( deltaTime );
        wakeup.getCamera( camera );
        streamWorld( carWorld.getTranslation() );
        if ( !wakeup.isPlaying() || gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            finishWakeup();
        }
        break;
    }
    case eState::Drive:
    case eState::Knock:
    case eState::Fade: {
        VehicleDynamics::Input input;
        if ( state == eState::Drive ) {
            // -tpin x,z[,heading]: the in-game teleport, once (the cheats).
            if ( bTeleportIn ) {
                bTeleportIn = false;
                teleportTo( teleportInTarget, teleportInHeading, bTeleportInHeading );
            }
            input.Throttle = gpInput->getAnalog( eInputAction::IA_CarAccel );
            input.Brake = gpInput->getAnalog( eInputAction::IA_CarBrake );
            input.Steering = gpInput->getSteering();
            input.bHandBrake = gpInput->isDown( eInputAction::IA_CarHandBrake );
            // The arrest sequence takes the controls away (the retail
            // pull-over: the cops stop the car and walk up to the window).
            // The brake pedal is held so the car rolls to a stop under the
            // scripted shot; the player cannot drive away from the arrest.
            if ( police.isArrested() ) {
                input.Throttle = 0.0f;
                input.Steering = 0.0f;
                input.bHandBrake = false;
                input.Brake = 1.0f;
            }
            if ( gpInput->wasPressed( eInputAction::IA_CarCamera ) && !police.isArrested() ) {
                chase.nextMode();
            }
            // Free look: the game's AVT_LOOK_* actions are the right stick
            // (docs/STATE.md issue 13 — not implemented until now). Orbits the
            // chase camera around the car and turns the head in the cockpit
            // views; the offsets spring back to centre when it is released.
            // CAR_BACKVIEW (pad L1 / LB) is a full look behind while held.
            chase.setLookInput( gpInput->getRightStickX(), gpInput->getRightStickY(),
                                gpInput->isDown( eInputAction::IA_CarBackView ) );
            // Accessories wheel / radio panel: the retail controller
            // FUN_00929be0 [CODE]. ACCESSORIES_LEFT / RIGHT / UP / DOWN are the
            // D-pad (numpad 4 / 6 / 8 / 2 on the keyboard) and their meaning
            // depends on what is open:
            //   nothing:   right opens the radio panel, left the accessories.
            //   radio:     up / down = volume (FUN_00929660 / FUN_009295c0),
            //              right = next station (FUN_00928690), left closes.
            //   wheel:     each direction applies its entry (FUN_009293f0):
            //              up roof, down windows, left / right indicators.
            // Every action re-arms the auto-hide timer (RadioAutoHideDuration
            // 3 s, AccAutoHideDuration 4 s, FUN_00929150 defaults).
            const bool bAccLeft = gpInput->wasPressed( eInputAction::IA_AccessoriesLeft );
            const bool bAccRight = gpInput->wasPressed( eInputAction::IA_AccessoriesRight );
            const bool bAccUp = gpInput->wasPressed( eInputAction::IA_AccessoriesUp );
            const bool bAccDown = gpInput->wasPressed( eInputAction::IA_AccessoriesDown );
            auto openRadio = [&]() {
                bRadioOpen = true;
                accessoriesHideTimer = kRadioAutoHideDuration;
                if ( !radio.isPlaying() ) {
                    radio.play();
                }
            };
            auto closeRadio = [&]() {
                bRadioOpen = false;
                hud.showRadio( false, radio.getStationInfo(), std::string(), std::string(), radio.getVolume() );
            };
            auto closeCarMenu = [&]() {
                bCarMenuOpen = false;
                hud.showCarMenu( false );
            };
            if ( bRadioOpen ) {
                if ( bAccUp ) {
                    radio.setVolume( radio.getVolume() + kRadioVolumeStep );
                    accessoriesHideTimer = kRadioAutoHideDuration;
                } else if ( bAccDown ) {
                    radio.setVolume( radio.getVolume() - kRadioVolumeStep );
                    accessoriesHideTimer = kRadioAutoHideDuration;
                } else if ( bAccLeft ) {
                    closeRadio();
                } else if ( bAccRight ) {
                    radio.nextStation();
                    accessoriesHideTimer = kRadioAutoHideDuration;
                }
            } else if ( bCarMenuOpen ) {
                int32_t entry = -1;
                if ( bAccUp ) {
                    entry = FreeRideHud::CME_Up;
                } else if ( bAccDown ) {
                    entry = FreeRideHud::CME_Down;
                } else if ( bAccLeft ) {
                    entry = FreeRideHud::CME_Left;
                } else if ( bAccRight ) {
                    entry = FreeRideHud::CME_Right;
                }
                if ( entry >= 0 ) {
                    // A locked entry keeps the wheel up (retail plays the
                    // "wrong" sound and re-arms the timer).
                    bool bApplied = true;
                    switch ( entry ) {
                    case FreeRideHud::CME_Up:
                        bApplied = car.hasHardtop();
                        if ( bApplied ) {
                            car.setHardtopOpen( !car.isHardtopOpen() );
                        }
                        break;
                    case FreeRideHud::CME_Down:
                        bApplied = car.hasWindows();
                        if ( bApplied ) {
                            car.setWindowsOpen( !car.areWindowsOpen() );
                        }
                        break;
                    case FreeRideHud::CME_Left:
                    case FreeRideHud::CME_Right: {
                        // "Indicators left" / "Indicators right": the entry turns
                        // its side's blinkers on, the same entry turns them off.
                        bApplied = car.hasLamps();
                        if ( bApplied ) {
                            const int32_t side = entry == FreeRideHud::CME_Left ? 0 : 1;
                            car.setIndicator( car.getIndicator() == side ? -1 : side );
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    if ( bApplied ) {
                        bCarMenuOpen = false;
                        hud.validateCarMenu( entry );
                    } else {
                        accessoriesHideTimer = kAccAutoHideDuration;
                    }
                }
            } else if ( bAccRight ) {
                openRadio();
            } else if ( bAccLeft ) {
                bCarMenuOpen = true;
                accessoriesHideTimer = kAccAutoHideDuration;
                hud.showCarMenu( true, car.hasHardtop(), car.hasWindows() );
            }
            // The game's own radio keys (RADIO_CHANNEL / RADIO_VOL_LESS /
            // RADIO_VOL_MORE = keyboard 1 / 2 / 3; not read from the pad, see
            // GSInput::loadPadMap), FUN_00929710: any of them opens the panel
            // (closing the wheel), then the volume keys step the volume and
            // the channel key changes the station once the panel is up [INF:
            // retail also steps the station on the press that opens it, which
            // reads as a skip].
            const bool bRadioChannel = gpInput->wasPressed( eInputAction::IA_RadioChannel );
            const bool bRadioLess = gpInput->wasPressed( eInputAction::IA_RadioVolLess );
            const bool bRadioMore = gpInput->wasPressed( eInputAction::IA_RadioVolMore );
            if ( bRadioChannel || bRadioLess || bRadioMore ) {
                if ( bCarMenuOpen ) {
                    closeCarMenu();
                }
                if ( !bRadioOpen ) {
                    openRadio();
                } else if ( bRadioChannel ) {
                    radio.nextStation();
                }
                if ( bRadioLess ) {
                    radio.setVolume( radio.getVolume() - kRadioVolumeStep );
                } else if ( bRadioMore ) {
                    radio.setVolume( radio.getVolume() + kRadioVolumeStep );
                }
                accessoriesHideTimer = kRadioAutoHideDuration;
            }
            // Circle (menu back) also closes either panel [INF: not in the
            // retail controller]; the back on track hold of the same button is
            // suppressed until it is released.
            if ( ( bRadioOpen || bCarMenuOpen ) && gpInput->wasPressed( eInputAction::IA_MenuBack ) ) {
                if ( bRadioOpen ) {
                    closeRadio();
                }
                if ( bCarMenuOpen ) {
                    closeCarMenu();
                }
                bBackOnTrackDone = true;
            }
            // Auto-hide (FUN_00929be0 tail -> FUN_00929100).
            if ( bRadioOpen || bCarMenuOpen ) {
                accessoriesHideTimer -= deltaTime;
                if ( accessoriesHideTimer <= 0.0f ) {
                    if ( bRadioOpen ) {
                        closeRadio();
                    }
                    if ( bCarMenuOpen ) {
                        closeCarMenu();
                    }
                }
            }
            // BACK_ON_TRACK (pad B / circle, R): hold the button, the return
            // to road badge (/HUD/BOT) sweeps its fill, then the car is put
            // back on the closest road [INF: ~1 s hold]. Disabled while the
            // radio panel is up (circle closes it). The press works anywhere
            // (on the road it straightens a stuck / rolled car onto the
            // lane); it is refused - the badge's red X, the movie's 'lock'
            // with its sndp:wrong cue, closing itself - when pressed again
            // right after a reset (the owner's retail captures were taken
            // that way: return to the road, then press again at once).
            // -botforce on|lock: the indicator up from frame 60 in the forced
            // state (the headless shots of the two states).
            if ( CmdLine::HasArg( "-botforce" ) ) {
                hud.showBackOnTrack( 0.0f, StringCmpI( CmdLine::GetArgValue( "-botforce" ), "lock" ) == 0 );
            } else if ( !bRadioOpen && gpInput->isDown( eInputAction::IA_BackOnTrack ) ) {
                if ( !bBackOnTrackHeld ) {
                    // A new press: refused while the cooldown of the last
                    // reset runs (the whole hold stays refused).
                    bBackOnTrackHeld = true;
                    bBackOnTrackRejected = backOnTrackCooldown > 0.0f;
                }
                if ( !bBackOnTrackRejected && !bBackOnTrackDone ) {
                    backOnTrackHold += deltaTime;
                    if ( backOnTrackHold >= kBackOnTrackHoldTime ) {
                        // One reset per hold: the button has to be released
                        // before the element fills again.
                        bBackOnTrackDone = true;
                        backOnTrackHold = 0.0f;
                        backOnTrackCooldown = kBackOnTrackCooldown;
                        backOnTrack();
                    }
                }
            } else {
                backOnTrackHold = 0.0f;
                bBackOnTrackDone = false;
                bBackOnTrackHeld = false;
                bBackOnTrackRejected = false;
            }
        } else {
            input.Brake = 1.0f;
        }
        // The arrest (the police pull-over): the player loses control - the
        // car brakes to a stop while the handcuffs sequence plays. Before this
        // the car kept driving through the 3 s arrest hold (nothing wrote the
        // input), so "the police coming to your car" never read as a pull-over.
        if ( police.isArrested() ) {
            input.Throttle = 0.0f;
            input.Brake = 1.0f;
            input.Steering = 0.0f;
            input.bHandBrake = false;
        }
        vehicle.step( deltaTime, input, collision );
        // The pose sync right after the step: everything below (the light
        // cones, pools and lamp glows, the cops, the chase camera) reads the
        // matrix of THIS frame. It used to be re-synced only past the light
        // block, so the lights rode the previous frame's pose and lagged the
        // car body (which draws with the fresh one) by exactly one frame.
        carWorld = vehicle.getWorldMatrix();
        // Fuel (docs/re/fuel_gas_station_spec.md): the tank burns `_gas_cons_*`
        // l/100 km of the distance the odometer gained. Empty: the engine dies
        // (no throttle) and the retail Breakdown line (loc 1644) shows until the
        // player fills up - in the house garage or at a station prop.
        {
            const float odometer = vehicle.getOdometer();
            const float distanceKm = Max( 0.0f, odometer - fuelOdometer ) / 1000.0f;
            fuelOdometer = odometer;
            if ( distanceKm > 0.0f && !fuelMenu.isOpen() && !bFuelEmpty ) {
                if ( BurnFuel( setup.Db, distanceKm, vehicle.getSpeedKmh() ) ) {
                    OTDU_LOG_INFO( "Fuel: tank empty after %.0f m - Breakdown\n", odometer );
                }
                bFuelEmpty = GetFuelTank().Litres <= 0.0f;
            }
            if ( bFuelEmpty ) {
                input.Throttle = 0.0f;
                // The line itself is set after tickWorldSpots (below): that
                // layer owns the prompt and clears it every frame, so setting it
                // here would never reach the screen.
            }
            if ( CmdLine::HasArg( "-logfuel" ) ) {
                static float fuelLogTimer = 0.0f;
                fuelLogTimer += deltaTime;
                if ( fuelLogTimer >= 1.0f ) {
                    fuelLogTimer = 0.0f;
                    const FuelTank &tank = GetFuelTank();
                    OTDU_LOG_INFO( "Fuel: %.2f / %.1f L, %.1f l/100 km, %.0f km left, grade %d%s\n", tank.Litres,
                                   tank.Capacity, tank.Consumption, tank.RangeKm, tank.Grade,
                                   bFuelEmpty ? " (empty)" : "" );
                }
            }
        }
        // Car lights: headlights at night, the brake lights on the brake /
        // handbrake, the reverse lights with the reverse gear, and the night
        // dashboard backlight. The indicators are set from the in-car menu
        // (CME_Left / CME_Right) and blink in updateLights.
        {
            static const int32_t lightMode = CmdLine::GetArgInt( "-carlights", -1 );
            static const float lampBoost = CmdLine::HasArg( "-lampboost" )
                                               ? static_cast<float>( atof( CmdLine::GetArgValue( "-lampboost" ) ) )
                                               : 1.4f;
            car.setLampBoost( lampBoost );
            const bool bNight = pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 );
            const bool bHeadlights = lightMode >= 0 ? lightMode != 0 : bNight;
            car.setHeadlights( bHeadlights );
            car.setDashboardLit( bHeadlights );
            car.setBrakeLights( input.Brake > 0.1f || input.bHandBrake );
            car.setReverseLights( vehicle.getGear() < 0 );
            // The indicators auto-cancel (the owner's ask, the retail latch
            // behaviour): indicating a side and actually steering that way
            // arms the cancel, the steering back to centre fires it. Steering
            // is +1 right / -1 left (GSInput::getSteering); the manual toggle
            // (the in-car menu) and the forced -indicator test hook are
            // untouched.
            {
                static const bool bIndicatorTest = CmdLine::HasArg( "-indicatortest" );
                static int32_t lastIndicator = -1;
                static bool bCancelArmed = false;
                static bool bTestIndicatorSet = false;
                static float testTimer = 0.0f;
                float steer = input.Steering;
                if ( bIndicatorTest ) {
                    // The headless proof: the blinker switches on once at
                    // t = 1, a left steer pulse 2.5..4.5 s feeds the cancel
                    // logic; the log then shows the auto-cancel after the
                    // pulse (armed at the pulse start, fired when it centres).
                    testTimer += deltaTime;
                    if ( testTimer >= 1.0f && !bTestIndicatorSet ) {
                        car.setIndicator( 0 );
                        bTestIndicatorSet = true;
                    }
                    steer = ( testTimer >= 2.5f && testTimer < 4.5f ) ? -0.8f : 0.0f;
                }
                const int32_t indicator = car.getIndicator();
                if ( indicator != lastIndicator ) {
                    bCancelArmed = false; // a fresh blink starts unarmed
                    lastIndicator = indicator;
                }
                if ( indicator == 0 || indicator == 1 ) {
                    const float dir = indicator == 0 ? -1.0f : 1.0f; // left steers negative
                    if ( !bCancelArmed && steer * dir > 0.35f ) {
                        bCancelArmed = true;
                    } else if ( bCancelArmed && std::abs( steer ) < 0.12f ) {
                        car.setIndicator( -1 );
                        bCancelArmed = false;
                        OTDU_LOG_INFO( "CarLights: the %s indicator auto-cancelled after the turn\n",
                                       lastIndicator == 0 ? "left" : "right" );
                    }
                } else {
                    bCancelArmed = false;
                }
            }
            car.updateLights( deltaTime );
            // The street lamp glow shares the night gate (-lampday: always on).
            view.getWorld().setNightLights( bNight || CmdLine::HasArg( "-lampday" ) );

            // The headlight ground projection (eden_headlights.h): these cones
            // reach EdenShadows::resolve, which encodes them into the s15 G/B
            // channels the scene shaders decode (rain_shadow_spec B.7) - the
            // retail's HEADLIGHTS light volumes, port-native.
            {
                static const bool bNoProjection = CmdLine::HasArg( "-noheadlights" );
                std::vector<EdenHeadlightCone> cones;
                if ( !bNoProjection && car.areHeadlightsOn() ) {
                    Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
                    forward.y *= 0.25f; // levelled, a slight down aim
                    if ( forward.dot( forward ) > 1e-6f ) {
                        forward = forward.normalized();
                        EdenHeadlightCone cone;
                        cone.Position =
                            carWorld.getTranslation() + forward * 2.0f + Vector3f( 0.0f, 0.62f, 0.0f );
                        cone.Direction = forward;
                        cone.Intensity =
                            pWeather != nullptr ? pWeather->getFloat( "car_headlights_intensity", 1.2f ) : 1.2f;
                        cones.push_back( cone );
                    }
                }
                // (The traffic's cones are appended after traffic.tick below:
                // agent.World is rebuilt there, and reading it any earlier drew
                // the traffic pools a frame behind the cars.)
                EdenHeadlightFrameCones() = std::move( cones );

                // The rear pools: brake (red) and reverse (white), from the
                // same state the LGT_ lens materials show.
                std::vector<EdenHeadlightCone> rear;
                if ( !bNoProjection ) {
                    Vector3f backward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, -1.0f ) );
                    backward.y = 0.0f;
                    if ( backward.dot( backward ) > 1e-6f ) {
                        backward = backward.normalized();
                        const bool bBrake = car.areBrakeLightsOn();
                        const bool bReverse = car.getReverseLights();
                        if ( bBrake || bReverse ) {
                            EdenHeadlightCone pool;
                            pool.Position = carWorld.getTranslation() + backward * 1.6f + Vector3f( 0.0f, 0.6f, 0.0f );
                            pool.Direction = backward;
                            pool.Range = 6.0f;
                            pool.Intensity = bReverse ? 1.0f : 0.7f;
                            if ( bReverse ) {
                                pool.Tint = Vector4f( 1.0f, 1.0f, 1.0f, 0.42f ); // the white reverse lamps
                            }
                            rear.push_back( pool );
                        }
                        // The indicator's patch on the tarmac beside the car
                        // (the ground half of the blinker, to match the lens
                        // glows): an amber quad on the indicating side, lit on
                        // the blink phase only.
                        if ( car.getIndicator() >= 0 && car.isIndicatorLampOn() ) {
                            const Vector3f fwd = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
                            Vector3f side( fwd.z, 0.0f, -fwd.x ); // the driver's left
                            if ( car.getIndicator() == 1 ) {
                                side = -side; // the right indicator
                            }
                            if ( side.dot( side ) > 1e-6f ) {
                                side = side.normalized();
                                EdenHeadlightCone pool;
                                pool.Position = carWorld.getTranslation() + side * 0.9f +
                                                Vector3f( fwd.x, 0.0f, fwd.z ).normalized() * 0.4f +
                                                Vector3f( 0.0f, 0.45f, 0.0f );
                                pool.Direction = side;
                                pool.Intensity = 1.2f;
                                pool.Tint = Vector4f( 1.0f, 0.52f, 0.10f, 0.55f );
                                pool.PoolAhead = 1.1f;
                                pool.PoolAlong = 1.1f;
                                pool.PoolAcross = 1.5f;
                                rear.push_back( pool );
                            }
                        }
                    }
                }
                EdenRearLightFrameCones() = std::move( rear );
            }
            // The lit lamps' glow (car_lights_spec.md 8): the bloom quads of
            // the player's car, drawn by EdenSurfaceFx.
            {
                std::vector<EdenLampGlow> glows;
                if ( car.isLoaded() ) {
                    car.appendLampGlows( carWorld, glows );
                }
                EdenLampGlowFrame() = std::move( glows );
            }
            // -logcarlights: the light state once a second (with the window
            // position), so a headless run can check the driver.
            if ( CmdLine::HasArg( "-logcarlights" ) ) {
                static float lightLogTimer = 0.0f;
                lightLogTimer += deltaTime;
                if ( lightLogTimer >= 1.0f ) {
                    lightLogTimer = 0.0f;
                    OTDU_LOG_INFO( "CarLights: night %d headlights %d indicator %d blink %d brake %d reverse %d dash %d "
                                   "windows %.2f\n",
                                   bNight ? 1 : 0, car.areHeadlightsOn() ? 1 : 0, car.getIndicator(),
                                   car.isIndicatorLampOn() ? 1 : 0, car.areBrakeLightsOn() ? 1 : 0,
                                   car.getReverseLights() ? 1 : 0, car.isDashboardLit() ? 1 : 0,
                                   car.getWindowsFactor() );
                }
            }
        }
        {
            static int32_t selfTestFrames = 0;
            if ( CmdLine::HasArg( "-nearmisstest" ) ) {
                traffic.nearMissTest( deltaTime );
            }
            static bool bRiskTested = false;
            if ( CmdLine::HasArg( "-riskselftest" ) && !bRiskTested ) {
                bRiskTested = true;
                RiskDriving::selfTest();
            }
            if ( CmdLine::HasArg( "-trafficselftest" ) && ++selfTestFrames % 240 == 0 ) {
                traffic.selfTest();
            }
        }
        bool bTrafficCrash = false; // the police chase reads it below
        {
            // Traffic: contact impulse on the player car (the traffic car stops).
            const Vector3f heading = vehicle.getWorldMatrix().transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
            Vector3f deltaVelocity;
            const Vector3f flatHeading = Vector3f( heading.x, 0.0f, heading.z ).normalized();
            RiskDriving::Frame riskFrame;
            if ( traffic.resolveCollision( vehicle.getPosition(), flatHeading, vehicle.getVelocity(), setup.Size.x, setup.Size.z,
                                           setup.Mass, deltaVelocity ) ) {
                vehicle.addVelocity( deltaVelocity );
                riskFrame.bCrash = true;
                bTrafficCrash = true;
                // The pad takes the hit (input/gamepad_output.h), the wheel too.
                gamepad.pulse( Clamp( deltaVelocity.length() / 12.0f, 0.35f, 1.0f ), 0.3f );
                wheelFfb.kick( Clamp( deltaVelocity.length() / 12.0f, 0.35f, 1.0f ) );
            }
            // The race opponents: the same contact (no violation - racing).
            if ( opponents.isActive() && opponents.resolveCollision( vehicle.getPosition(), flatHeading, vehicle.getVelocity(),
                                                                     setup.Size.x, setup.Size.z, setup.Mass, deltaVelocity ) ) {
                vehicle.addVelocity( deltaVelocity );
                gamepad.pulse( Clamp( deltaVelocity.length() / 12.0f, 0.35f, 1.0f ), 0.3f );
                wheelFfb.kick( Clamp( deltaVelocity.length() / 12.0f, 0.35f, 1.0f ) );
            }
            riskFrame.DeltaTime = deltaTime;
            riskFrame.SpeedKmh = CmdLine::HasArg( "-riskdemo" ) ? 180.0f : vehicle.getSpeedKmh(); // -riskdemo: HUD check
            // The spawn drop and the first seconds after it earn nothing.
            static float riskWarmup = 3.0f;
            riskWarmup = Max( 0.0f, riskWarmup - deltaTime );
            riskFrame.bOnGround = riskWarmup > 0.0f ? true : vehicle.isOnGround();
            riskFrame.NearMisses = traffic.detectNearMisses( vehicle.getPosition(), flatHeading, vehicle.getVelocity(), setup.Size.x,
                                                             setup.Size.z, riskFrame.NearMissClosing, riskFrame.NearMissGap );
            // The rate curve's x scale: the car's top speed (vehicle +0x254 in
            // the PS3 curve 0x7cae18) [INF: _max_theorical_speed].
            riskFrame.MaxSpeedKmh = setup.MaxTheoricalSpeed > 1.0f ? setup.MaxTheoricalSpeed : 250.0f;
            riskFrame.X = vehicle.getPosition().x;
            riskFrame.Y = vehicle.getPosition().y;
            riskFrame.Z = vehicle.getPosition().z;
            {
                // Body slip angle: the flat velocity against the heading (drift).
                const Vector3f v = vehicle.getVelocity();
                const Vector3f flatV( v.x, 0.0f, v.z );
                const float speed = flatV.length();
                riskFrame.SlipAngle = speed > 2.0f ? std::acos( Clamp( std::fabs( flatV.dot( flatHeading ) ) / speed, 0.0f, 1.0f ) ) : 0.0f;
            }
            // -riskevent: a scripted near miss every 4 s, to see the FRIM gauge
            // event choreography headlessly.
            static int32_t riskEventFrames = 0;
            if ( CmdLine::HasArg( "-riskevent" ) && ++riskEventFrames % 240 == 0 ) {
                riskFrame.NearMisses += 1;
                riskFrame.NearMissClosing = 20.0f;
                riskFrame.NearMissGap = 1.0f;
            }
            // The bank pays when the movie's animation asks (frgp:bank), like
            // the retail FSCommand handler 0xa76e50 [CODE].

            if ( !bBankWired ) {
                bBankWired = true;
                risk.setOnBank( []( int32_t amount ) {
                    GetPlayerProfile().Money += amount;
                    SaveProfile( "FRIM bank" );
                } );
                hud.setBankCommandCallback( [ this ]( const std::string &command ) { risk.onBankFSCommand( command ); } );
            }
            risk.tick( riskFrame );
            // The bank is player-confirmed: the movie shows the [A] "BANK"
            // prompt after a level-up; A plays the pay animation (frgp:bank
            // then arrives from the movie in retail; the port pays directly
            // and jumps the animation to its bank segment).
            if ( risk.isBanking() && gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
                hud.playFrimBank();
                risk.onBankFSCommand( "frgp:bank" );
            }
            hud.setRisk( risk );
        }
        {
            // Police chase (the retail "Chase Mode" UI: the tailing meter,
            // the arrest countdown, IsPolice / IsUnderArrest). A traffic hit
            // is a violation; the chase, the arrest and the escape follow
            // ChaseMode's state machine. (The car position member is used:
            // carWorld carries it.)
            // One crash = one violation: the collision stays reported while
            // the cars touch (the impulse separates them over several
            // frames), so the raw flag fired a violation per frame and one
            // bump filled the awareness meter - the "one crash and they
            // chase you" report. The rising edge counts; the latch re-arms
            // once the contact has been clear for kCrashClearGrace.
            bool bNewCrash = false;
            if ( bTrafficCrash ) {
                if ( !bTrafficCrashLatch ) {
                    bTrafficCrashLatch = true;
                    bNewCrash = true;
                }
                trafficCrashClearTime = 0.0f;
            } else {
                trafficCrashClearTime += deltaTime;
                if ( trafficCrashClearTime >= kCrashClearGrace ) {
                    bTrafficCrashLatch = false;
                }
            }
            if ( bNewCrash ) {
                // A violation needs a witness: a police car among the traffic
                // within sight (160 m [INF]) makes a hit count in full (three
                // solid hits start a chase); with no police around the hit
                // only nudges the awareness - the retail police react to what
                // they see, not to every scrape on an empty road. The ambient
                // police car is aiconfig POLICE_CAR_ID (3009, weight 0.60 on
                // Ibiza, so one is around often) [DATA]. -policeviol still
                // forces a full violation.
                const float witness = traffic.nearestPoliceDistance( carWorld.getTranslation() );
                const bool bSeen = witness >= 0.0f && witness < 160.0f;
                police.addViolation( bSeen ? 34.0f : 12.0f );
                if ( bSeen ) {
                    // The retail violation feedback (loc 763 'Traffic violation'):
                    // the popup shows for the crashes the police can see; the
                    // chase follows from the meter, not from this popup.
                    hud.showGoal( LocStr( 763, "Traffic violation" ) );
                    // The first one the police see explains the system: the
                    // "Help: Chase Mode" pages, once per profile.
                    if ( !GetPlayerProfile().ChaseHelpSeen && !CmdLine::HasArg( "-nochasehelp" ) ) {
                        bChaseHelpPending = true;
                    }
                }
                if ( CmdLine::HasArg( "-logpolice" ) ) {
                    OTDU_LOG_INFO( "ChaseMode: traffic hit, police %s (%.0f m), awareness %.2f\n", bSeen ? "saw it" : "not around",
                                   witness, police.getAwareness01() );
                }
            }
            if ( CmdLine::HasArg( "-policeviol" ) && pGameInstance->getFrameIndex() == 150 ) {
                // test: one witnessed violation (the gauge fills to 55 %, the
                // first one opens the Chase Mode help like a real one).
                police.addViolation( 55.0f );
                hud.showGoal( LocStr( 763, "Traffic violation" ) );
                if ( !GetPlayerProfile().ChaseHelpSeen && !CmdLine::HasArg( "-nochasehelp" ) ) {
                    bChaseHelpPending = true;
                }
            }
            // The help opens a moment after the violation (the gauge has slid
            // in and the goal line shows first) [INF].
            if ( bChaseHelpPending ) {
                chaseHelpDelay += deltaTime;
                if ( chaseHelpDelay >= 1.5f ) {
                    bChaseHelpPending = false;
                    chaseHelpDelay = 0.0f;
                    GetPlayerProfile().ChaseHelpSeen = true;
                    SaveProfile( "chase help seen" );
                    hud.openChaseHelp();
                }
            }
            if ( CmdLine::HasArg( "-police" ) && pGameInstance->getFrameIndex() == 150 ) {
                police.startChase(); // test: the chase without the violations
            }
            // The retail "too many traffic violations" warning: the tailing
            // meter in its red band means the next hit starts the pursuit
            // (loc 7465 "the driver has committed too many traffic violations
            // and he's now in Chase Mode"; the chase help's own warning lines
            // are loc 2187 "Warning: the police are on high alert!!!" and loc
            // 3158 "The police are targeting... you!"). Shown once per fill,
            // re-armed when the meter falls back into the low band.
            if ( !bViolationWarnShown && police.getState() != ChaseMode::eState::Chase &&
                 police.getState() != ChaseMode::eState::Arrested && police.getAwareness01() >= kViolationWarnLevel ) {
                bViolationWarnShown = true;
                hud.showHint( LocStr( 2187, "Warning: the police are on high alert!!! Too many traffic violations and they will chase you." ),
                              8.0f );
                if ( CmdLine::HasArg( "-logpolice" ) ) {
                    OTDU_LOG_INFO( "ChaseMode: violation warning shown (awareness %.2f)\n", police.getAwareness01() );
                }
            } else if ( police.getAwareness01() < 0.5f ) {
                bViolationWarnShown = false;
            }
            // The pursuit starts (Clean / Noticed -> Chase): the retail "The
            // police are targeting... you!" line (loc 3158).
            static ChaseMode::eState lastPoliceState = ChaseMode::eState::Clean;
            const ChaseMode::eState policeState = police.getState();
            if ( policeState == ChaseMode::eState::Chase && lastPoliceState != ChaseMode::eState::Chase ) {
                hud.showGoal( LocStr( 3158, "The police are targeting... you!" ) );
                if ( CmdLine::HasArg( "-logpolice" ) ) {
                    OTDU_LOG_INFO( "ChaseMode: pursuit started\n" );
                }
            }
            // The arrest sequence (the police pull-over): the car brakes to a
            // stop and the camera leaves it for a scripted shot beside the
            // driver's window while the cops close in. arrestShotTime < 0 is
            // idle; the state machine holds Arrested for kArrestHoldSeconds.
            if ( police.isArrested() ) {
                if ( arrestShotTime < 0.0f ) {
                    arrestShotTime = 0.0f;
                    OTDU_LOG_INFO( "FREERIDE: arrest sequence - the police pull-over shot starts\n" );
                } else {
                    arrestShotTime += deltaTime;
                }
            } else if ( arrestShotTime >= 0.0f ) {
                arrestShotTime = -1.0f;
                OTDU_LOG_INFO( "FREERIDE: arrest sequence over\n" );
            }
            lastPoliceState = policeState;
            police.tick( deltaTime, carWorld.getTranslation(), vehicle.getSpeedKmh(),
                         [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
            // The ambient police cars switch their roof bars on for the
            // duration of the chase (they are off on patrol). -policebar keeps
            // them on for a screenshot.
            if ( !CmdLine::HasArg( "-policebar" ) ) {
                traffic.setPoliceLights( police.getState() == ChaseMode::eState::Chase ||
                                         police.getState() == ChaseMode::eState::Arrested );
            }
            float fine = 0.0f, reward = 0.0f;
            if ( police.takeArrested( fine ) ) {
                GetPlayerProfile().Money -= Min( static_cast<int32_t>( fine ), GetPlayerProfile().Money );
                SaveProfile( "police arrest" );
                hud.closeHint();
                char text[128];
                snprintf( text, sizeof( text ), "%s  (-$%d)", LocStr( 5202, "You've been arrested by the police!" ),
                          static_cast<int32_t>( fine ) );
                hud.showGoal( text );
            }
            if ( police.takeEscaped( reward ) ) {
                SaveProfile( "police escape" );
                char text[128];
                snprintf( text, sizeof( text ), "%s  (+$%d)", LocStr( 5201, "Congratulations, you escaped the police!" ),
                          static_cast<int32_t>( reward ) );
                hud.showGoal( text );
            }
            hud.setChaseHud( police.getState() == ChaseMode::eState::Chase, police.getAwareness01(), police.getEscape01(),
                             police.isTimerVisible(), police.getTimerSeconds(), police.isArrested() );
            // The timer hint (loc 6614), once per armed countdown.
            static bool bTimerHintShown = false;
            if ( police.isTimerVisible() && !bTimerHintShown ) {
                bTimerHintShown = true;
                hud.showHint( LocStr( 6614, "When the timer appears, your end is near! Try to escape! If the timer reaches 0, you lose the chase!" ),
                              9.0f );
            } else if ( !police.isTimerVisible() ) {
                bTimerHintShown = false;
            }
            if ( CmdLine::HasArg( "-policehelp" ) && pGameInstance->getFrameIndex() == 150 ) {
                hud.openChaseHelp(); // test: the retail Help: Chase Mode pages
            }
        }
        {
            // Instant challenges: countdown (the HUD's /_3_2_1_go), the
            // checkpoints, the finish.
            // -racestart <n> (test): on instant-race start n, the race started
            // (the marker's A press), once the world is in.
            if ( const char *pRace = CmdLine::GetArgValue( "-racestart" ) ) {
                static bool bStarted = false;
                const int32_t index = atoi( pRace );
                if ( !bStarted && pGameInstance->getFrameIndex() > 120 && index >= 0 &&
                     index < static_cast<int32_t>( raceEvents.getStarts().size() ) ) {
                    bStarted = true;
                    const Vector3f at = raceEvents.getStarts()[index].Position;
                    raceEvents.start( index );
                    Vector3f first;
                    int32_t firstIndex = 0;
                    float heading = 0.0f;
                    if ( !raceEvents.getCheckpoints().empty() ) {
                        first = raceEvents.getCheckpoints()[0];
                        heading = std::atan2( first.x - at.x, first.z - at.z );
                    }
                    (void)firstIndex;
                    placeCar( at, heading );
                    OTDU_LOG_INFO( "FREERIDE: -racestart %d at (%.1f, %.1f)\n", index, at.x, at.z );
                }
            }
            raceEvents.tick( deltaTime, carWorld.getTranslation() );
            // -racetest: warp the car onto the next checkpoint once the race
            // runs (headless exercise of the gates and the finish).
            if ( CmdLine::HasArg( "-racetest" ) && raceEvents.getStage() == RaceEvents::eStage::Running ) {
                static float warpCooldown = 0.0f;
                warpCooldown -= deltaTime;
                Vector3f checkpoint;
                int32_t checkpointIndex = 0;
                if ( warpCooldown <= 0.0f && raceEvents.getNextCheckpoint( checkpoint, checkpointIndex ) ) {
                    warpCooldown = 1.0f;
                    const Quaternionf identity;
                    vehicle.reset( checkpoint + Vector3f( 0.0f, 0.2f, 0.0f ), identity, Vector3f( 0.0f, 0.0f, 8.0f ) );
                    collision.streamAround( checkpoint, 1 );
                    OTDU_LOG_INFO( "FREERIDE: -racetest warp to checkpoint %d\n", checkpointIndex + 1 );
                }
            }
            const RaceEvents::eStage raceStage = raceEvents.getStage();
            static float goHold = 0.0f;
            if ( raceStage == RaceEvents::eStage::Countdown ) {
                goHold = 1.0f;
                hud.showCountdown( raceEvents.getCountdownNumber() );
            } else if ( raceStage == RaceEvents::eStage::Running ) {
                goHold -= deltaTime;
                hud.showCountdown( goHold > 0.0f ? 0 : -1 );
                Vector3f checkpoint;
                int32_t checkpointIndex = 0;
                if ( raceEvents.getNextCheckpoint( checkpoint, checkpointIndex ) ) {
                    FreeRideHud::RaceHud race;
                    race.bVisible = true;
                    race.Seconds = raceEvents.getRaceTime();
                    race.Checkpoint = checkpointIndex + 1;
                    race.Checkpoints = raceEvents.getCheckpointCount();
                    hud.setRaceHud( race );
                    // A checkpoint passed: its time in the split box.
                    static int32_t lastCheckpoint = 0;
                    if ( checkpointIndex > lastCheckpoint ) {
                        hud.showCheckpointPassed( raceEvents.getRaceTime(), false, 0.0f );
                    }
                    lastCheckpoint = checkpointIndex;
                }
            } else if ( !licenseTest.isActive() ) {
                // (A licence test owns the countdown and the timer: tickLicenseTest.)
                hud.showCountdown( -1 );
                hud.setRaceTimer( false, 0.0f, 0, 0 );
                hud.setRaceHud( FreeRideHud::RaceHud() );
            }
            tickLicenseTest( deltaTime );
            updateRaceGates();
            float raceTime = 0.0f;
            int32_t raceReward = 0;
            bool bRaceSuccess = false;
            if ( raceEvents.takeFinished( raceTime, raceReward, bRaceSuccess ) ) {
                PlayerProfile &profile = GetPlayerProfile();
                profile.Money += raceReward;
                profile.Points[0] += raceReward / 10; // the racing skill [INF share]
                SaveProfile( "race won" );
                char text[128];
                snprintf( text, sizeof( text ), "Finished in %d.%02d s  (+$%d)", static_cast<int32_t>( raceTime ),
                          static_cast<int32_t>( raceTime * 100.0f ) % 100, raceReward );
                hud.showGoal( text );
            }
        }
        // The world spot layer runs after hud.tick below (its element
        // positioning must survive the movie tick, like the map's markers).
        // (The carWorld sync moved up to the physics step: the lights, the
        // cops and this camera all want the fresh pose.)
        streamWorld( carWorld.getTranslation() );
        camera = chase.update( deltaTime, carWorld, vehicle.getSpeedKmh(),
                               [this]( float x, float z, float &outHeight ) { return view.getWorld().getHeight( x, z, outHeight ); },
                               vehicle.getRpm(), setup.Db.RedLine );
        // The retail speed blur of the camera data, applied by the final blit
        // (EdenSceneView::setSpeedBlur; docs/re/needles_spec.md). -noblur is
        // the A/B switch.
        if ( !CmdLine::HasArg( "-noblur" ) ) {
            view.setSpeedBlur( chase.getSpeedBlur() );
        }
        // The arrest shot (the police pull-over): the camera leaves the car and
        // frames it from the driver's side, drifting slowly so it reads as a
        // cutscene; the cops close in beside it during ChaseMode's Arrested
        // hold (kArrestHoldSeconds). The chase camera returns when the hold
        // ends. -noarrestcam keeps the gameplay camera for A/B checks.
        static const bool bNoArrestCam = CmdLine::HasArg( "-noarrestcam" );
        if ( !bNoArrestCam && police.isArrested() && arrestShotTime >= 0.0f ) {
            const Vector3f carPos = carWorld.getTranslation();
            const Vector3f carFwd = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
            const Vector3f flatFwd = Vector3f( carFwd.x, 0.0f, carFwd.z ).normalized();
            // The driver's left: "forward x up = (-fz, fx)" is the driver's
            // right in the port's right-handed frame (chase_mode's lane rule),
            // so the left is its negation.
            const Vector3f carLeft( flatFwd.z, 0.0f, -flatFwd.x );
            // The shot frames the pair (the car and the cop pulling in beside
            // it), so the player reads it as the police coming to the window.
            // The nearest cop's position is sampled per frame: he drives up
            // during the hold and the framing follows him.
            Vector3f copPos = carPos;
            float nearestCop = 1e30f;
            for ( const ChaseMode::Cop &cop : police.getCops() ) {
                const float distance = ( cop.Position - carPos ).length();
                if ( distance < nearestCop ) {
                    nearestCop = distance;
                    copPos = cop.Position;
                }
            }
            // The view axis runs across the car - cop line, so both sit in
            // the frame; without a cop it falls back to the driver's side.
            Vector3f across( copPos.z - carPos.z, 0.0f, carPos.x - copPos.x );
            if ( across.length() < 0.1f ) {
                across = carLeft;
            }
            across = across.normalized();
            if ( across.dot( carLeft ) < 0.0f ) {
                across = across * -1.0f; // keep the eye on the driver's side
            }
            const float t = Clamp( arrestShotTime / kArrestHoldSeconds, 0.0f, 1.0f );
            const Vector3f target = carPos + ( copPos - carPos ) * 0.4f + Vector3f( 0.0f, 0.9f, 0.0f );
            const Vector3f eye = target + across * ( 7.4f - 1.6f * t ) + Vector3f( 0.0f, 1.6f - 0.3f * t, 0.0f ) -
                                 flatFwd * 0.6f;
            camera.Position = eye;
            camera.Rotation = ChaseCamera::LookRotation( eye, target );
            camera.Fov = 46.0f * kDegToRad;
            camera.Near = 0.5f;
            camera.Far = 40000.0f;
        }
        // Rain on the windscreen shows from the views that sit behind the
        // glass (the droplets are screen space): the camera owner gates the
        // windscreen effect on. The bumper / bonnet view has no glass in front
        // of its eye, so it must not get the droplet composite (it fogged the
        // whole frame with rain streaks).
        view.setDropletsEnabled( chase.isBehindGlass() && CmdLine::GetArgInt( "-droplets", 1 ) != 0 );
        // -cameramode <n>: a test start in one of the car's views (23 driver,
        // 24 bumper, 20 chase; the car's list is camerasingame.xmb's). Once:
        // the C key keeps cycling from there.
        static const int32_t cameraMode = CmdLine::GetArgInt( "-cameramode", -1 );
        static bool bCameraModeApplied = cameraMode < 0;
        if ( !bCameraModeApplied ) {
            bCameraModeApplied = true;
            if ( !chase.setMode( cameraMode ) ) {
                OTDU_LOG_WARN( "FREERIDE: -cameramode %d is not one of this car's views\n", cameraMode );
            }
        }
        // Story step 2 tick: timer -> C_FADE_FREERIDE over the gameplay
        // (retail order: the knock at the window BEFORE the white flash),
        // then the white fade hides the car parked at the house and the
        // wake-up scene plays.
        if ( state == eState::Drive ) {
            if ( GetPlayerProfile().StoryStep == 2 ) {
                stepTimer -= deltaTime;
                if ( stepTimer < 0.0f ) {
                    // The scene is cut by default (-wakeup re-enables the
                    // knock scene + the wake-up placeholder): the flash is
                    // the whole story beat, then the freeride resumes.
                    if ( wakeup.isLoaded() && CmdLine::HasArg( "-wakeup" ) ) {
                        startKnock();
                    } else {
                        state = eState::Fade;
                        fadeTime = 0.0f;
                        hud.closeGoal();
                        OTDU_LOG_INFO( "FREERIDE: %.0f s elapsed -> fade to white\n", firstFreerideTime );
                    }
                }
            }
        } else if ( state == eState::Knock ) {
            // C_FADE_FREERIDE has no camera of its own (shot NO_CAMERA): the
            // game camera and the HUD stay, the car rolls to its stop under
            // the brake while Tess knocks at the window.
            if ( vehicle.getSpeedKmh() > 0.5f ) {
                wakeup.anchorToCar( carWorld );
            }
            wakeup.tick( deltaTime );
            if ( !wakeup.isPlaying() || gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
                state = eState::Fade;
                fadeTime = 0.0f;
                hud.closeGoal();
                OTDU_LOG_INFO( "FREERIDE: knock scene done -> fade to white\n" );
            }
        } else {
            fadeTime += deltaTime;
            if ( fadeTime >= kFadeDuration ) {
                if ( !CmdLine::HasArg( "-nodream" ) ) {
                    startDream();
                } else if ( wakeup.isLoaded() && CmdLine::HasArg( "-wakeup" ) ) {
                    startWakeup();
                } else {
                    // Scene cut: back to exploring from the white, at the
                    // step-3 goal text. Revisit with C_MEETING_TESS
                    // ("thedream", meeting_tess.bnk — data mapped in
                    // docs/STATE.md, nothing needs re-animation).
                    GetPlayerProfile().StoryStep = 3;
                    SaveProfile( "story step 3" );
                    hud.showGoal( LocStr( 6111, "On the road to \"Solar Crown\"" ) );
                    state = eState::Drive;
                    OTDU_LOG_INFO( "FREERIDE: white fade done -> story step 3, freeride resumes\n" );
                }
            }
        }
        break;
    }
    }
    updateWheels();
    tickSurfaceFx( deltaTime );
    // The world neons (shop signs): strength from the weather, the .pmi
    // blink states advance (neon_spec 2).
    view.getWorld().updateNeons( pWeather, deltaTime );
    // The wipers (port-invented driver over the retail parked prim): auto
    // mode sweeps while it rains, the interval follows the intensity. F6
    // toggles, -wipers off|on|fast picks the start mode.
    static bool bWiperModeSet = false;
    if ( !bWiperModeSet && car.isLoaded() ) {
        bWiperModeSet = true;
        const char *pWipers = CmdLine::GetArgValue( "-wipers" );
        if ( pWipers != nullptr ) {
            if ( StringCmpI( pWipers, "off" ) == 0 ) {
                car.setWiperMode( CinematicCar::eWiperMode::Off );
            } else if ( StringCmpI( pWipers, "fast" ) == 0 ) {
                car.setWiperMode( CinematicCar::eWiperMode::Fast );
            } else if ( StringCmpI( pWipers, "on" ) == 0 ) {
                car.setWiperMode( CinematicCar::eWiperMode::Auto );
            }
        }
        OTDU_LOG_INFO( "FREERIDE: wipers %s (%s)\n", car.hasWipers() ? "fitted" : "none on this car",
                       car.getWiperMode() == CinematicCar::eWiperMode::Off
                           ? "off"
                           : ( car.getWiperMode() == CinematicCar::eWiperMode::Fast ? "fast" : "auto" ) );
    }
    if ( gpInput->wasKeyPressed( SDL_SCANCODE_F6 ) && car.isLoaded() ) {
        const CinematicCar::eWiperMode next =
            car.getWiperMode() == CinematicCar::eWiperMode::Off
                ? CinematicCar::eWiperMode::Auto
                : ( car.getWiperMode() == CinematicCar::eWiperMode::Auto ? CinematicCar::eWiperMode::Fast
                                                                          : CinematicCar::eWiperMode::Off );
        car.setWiperMode( next );
        OTDU_LOG_INFO( "FREERIDE: wipers %s\n", next == CinematicCar::eWiperMode::Off
                                                     ? "off"
                                                     : ( next == CinematicCar::eWiperMode::Fast ? "fast" : "auto" ) );
    }
    car.updateWipers( deltaTime, pWeather != nullptr ? pWeather->getFloat( "rain_alpha", 0.0f ) : 0.0f );

    // -logdrive: the drive / wheel telemetry. Logged for every state (the
    // C_FIRST_OUTRO wheels roll from the .sce track speed, not the physics
    // wheel state, so the Outro has to be covered too). The rim node's y axis
    // must rotate with the spin; its translation must stay on the hub.
    static const bool bLogDrive = CmdLine::HasArg( "-logdrive" );
    if ( bLogDrive && ( pGameInstance->getFrameIndex() % 10 ) == 0 ) {
        const Vector3f p = carWorld.getTranslation();
        const VehicleDynamics::WheelState &w0 =
            vehicle.getWheels().empty() ? VehicleDynamics::WheelState() : vehicle.getWheels()[0];
        Vector3f rimT, rimY;
        if ( !wheelNodes.empty() ) {
            const Matrix4x4f &rim = car.getModel().getNodeTransform( wheelNodes[0].RimNode );
            rimT = rim.getTranslation();
            rimY = rim.transformVector( Vector3f( 0.0f, 1.0f, 0.0f ) );
        }
        OTDU_LOG_INFO( "DRIVE pos (%.2f %.2f %.2f) %.1f km/h rpm %.0f gear %d steer %.3f contacts %d%d%d%d "
                       "spin %.1f w %.1f audio %.2f load %.0f odo %.1f m rimT (%.2f %.2f %.2f) rimY (%.2f %.2f %.2f)\n",
                       p.x, p.y, p.z, vehicle.getSpeedKmh(), vehicle.getRpm(), vehicle.getGear(),
                       vehicle.getSteerAngle(), vehicle.getWheels().size() > 0 && vehicle.getWheels()[0].bContact,
                       vehicle.getWheels().size() > 1 && vehicle.getWheels()[1].bContact,
                       vehicle.getWheels().size() > 2 && vehicle.getWheels()[2].bContact,
                       vehicle.getWheels().size() > 3 && vehicle.getWheels()[3].bContact, w0.SpinAngle,
                       w0.AngularVelocity, vehicleAudio.getMaxLayerGain(), vehicleAudio.getLoad(),
                       vehicle.getOdometer(), rimT.x, rimT.y, rimT.z, rimY.x, rimY.y, rimY.z );
    }

    FreeRideHud::State hudState;
    hudState.SpeedKmh = state == eState::Outro || state == eState::Wakeup ? 0.0f : vehicle.getSpeedKmh();
    hudState.Rpm = state == eState::Outro || state == eState::Wakeup ? setup.Db.RPMMin : vehicle.getRpm();
    // The fuel bar of the HUD (docs/re/fuel_gas_station_spec.md): the car has no
    // fuel needle of its own and the movie no fuel element, so the port draws one.
    {
        const FuelTank &tank = GetFuelTank();
        hud.setFuel( tank.Litres, tank.Capacity, tank.RangeKm, tank.Grade );
    }
    hudState.RedLine = setup.Db.RedLine;
    hudState.RpmMax = setup.Db.RPMMax;
    hudState.Gear = vehicle.getGear();
    hudState.CameraMode = chase.getMode();
    hudState.OdometerKm = vehicle.getOdometer() / 1000.0f;
    const Vector3f carPosition = carWorld.getTranslation();
    const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
    const Vector3f flat = Vector3f( forward.x, 0.0f, forward.z ).normalized();
    // The grass flattens around the car (grass_spec.md 12). The half extents
    // are generic [INF]; the footprint includes the tyres' reach.
    view.getWorld().setGrassCar( carPosition, flat, 2.6f, 1.25f );
    hudState.CarX = carPosition.x;
    hudState.CarZ = carPosition.z;
    hudState.HeadingX = flat.x;
    hudState.HeadingZ = flat.z;
    // The in-game clock and the weather it drives (weather_cycle.h): the world
    // follows the time of day and the file's intemperies walk clear -> cloud ->
    // rain (the rain sets carry the wet ground, the puddles and the darker sky).
    {
        const WeatherCycle::Tick weatherTick = weatherCycle.tick( deltaTime );
        if ( weatherTick.bPresetChanged && weatherTick.pPreset != nullptr ) {
            pWeather = weatherTick.pPreset;
            OTDU_LOG_INFO( "FREERIDE: clock %02d:%02d, weather '%s' intempery %d (rain %.2f)\n",
                           weatherCycle.getHour(), weatherCycle.getMinute(), pWeather->Name.c_str(),
                           pWeather->Intempery, pWeather->getFloat( "rain_ground_global_intensity", 0.0f ) );
        }
        if ( weatherTick.bClockChanged ) {
            // The pause screen has its own clock (PauseMenu::refreshClock reads
            // the PC clock); the cycle's clock is the lighting / weather one and
            // is only logged here.
            static int32_t lastLoggedHour = -1;
            if ( weatherCycle.getHour() != lastLoggedHour ) {
                lastLoggedHour = weatherCycle.getHour();
                OTDU_LOG_INFO( "FREERIDE: clock %02d:%02d\n", weatherCycle.getHour(), weatherCycle.getMinute() );
            }
        }
    }
    hud.tick( deltaTime, hudState );
    // The 3D cockpit gauge needles: the car's interior model meshes follow the
    // same interior_* tables as the GAUGES movie (docs/re/needles_spec.md).
    // -noneedles keeps the rest pose (the -needletest / -needlepose A/B).
    if ( !CmdLine::HasArg( "-noneedles" ) ) {
        car.setDashboardNeedles( hud.getInteriorSpeedNeedleAngle(), hud.getInteriorRpmNeedleAngle() );
    }
    // The world spot layer: the enter element (shops / race starts / the
    // return to road tile) and the distant markers, positioned after the
    // movie tick so the placements hold (the map's marker pattern).
    if ( state == eState::Drive ) {
        tickWorldSpots( deltaTime, carPosition, vehicle.getSpeedKmh() );
        // The fuel Breakdown line (loc 1644) is a status, not an interaction:
        // it shows whenever the tank is dry and no spot prompt is up (a station
        // prompt wins - that is the way out of the breakdown).
        if ( bFuelEmpty && !hud.hasPrompt() ) {
            hud.setPrompt( LocStr( 1644, "Breakdown" ) );
        }
    }
    {
        static int32_t levelUpTestFrames = 0;
        if ( CmdLine::HasArg( "-testlevelup" ) && ++levelUpTestFrames == 150 ) {
            GetPlayerProfile().Points[2] = 530;
            gSuppressLevelUp = true; // the real detector would pop a second popup for these points
            hud.setProgress( MakeProgressSnapshot() );
            hud.showLevelUp( 2, "2% discount on each vehicle", !CmdLine::HasArg( "-testglobalonly" ), "Middle class", 1, "You now have 2% discount on each vehicle",
                              CmdLine::HasArg( "-testglobal" ) || CmdLine::HasArg( "-testglobalonly" ) ? "Someone" : "" );
        }
    }
    {
        static int32_t dbDumpFrames = 0;
        if ( CmdLine::HasArg( "-logdbprogress" ) && ++dbDumpFrames == 120 ) {
            hud.dumpGeneral( "db_progress" );
        }
    }
    if ( hud.isLevelUpActive() && gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
        hud.acceptLevelUp();
    }
    {
        static int32_t awardFrames = 0;
        if ( const char *pAward = CmdLine::GetArgValue( "-awardevent" ) ) {
            if ( ++awardFrames == 150 ) {
                AwardSkillEvent( pAward ); // test: pays a DBRewardXP event now
            }
        }
    }
    // Wrecks (Discovery): the metal detector shows the distance to the nearest
    // unfound wreck within its range; within the activation distance it is found
    // and pays CRUISING_WRECK_PART_<ISLAND>_AREA<n> [DATA: wrecks_config.cpr,
    // DBRewardXP]. The detector needs no unlock here [INF] (retail: the Racing
    // level 3 reward).
    {
        std::vector<uint32_t> &found = GetPlayerProfile().FoundWrecks[islandName];
        std::vector<DiscoveryPoint> &wrecks = discovery.getWrecks();
        float nearest = -1.0f;
        for ( size_t i = 0; i < wrecks.size(); i++ ) {
            if ( std::find( found.begin(), found.end(), static_cast<uint32_t>( i ) ) != found.end() ) {
                continue;
            }
            const float dx = wrecks[i].X - carPosition.x, dz = wrecks[i].Z - carPosition.z;
            const float distance = std::sqrt( dx * dx + dz * dz );
            if ( distance < discovery.getActivationDistance() ) { // flat distance: no wreck lies under or over another
                found.push_back( static_cast<uint32_t>( i ) );
                std::string island = islandName;
                for ( char &c : island ) {
                    c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
                }
                const std::string event = "CRUISING_WRECK_PART_" + island + "_AREA" + std::to_string( Max( 1, wrecks[i].Area + 1 ) );
                OTDU_LOG_INFO( "Discovery: wreck '%s' found (%zu of %zu)\n", wrecks[i].Name.c_str(), found.size(), wrecks.size() );
                AwardSkillEvent( event.c_str() );
                SaveProfile( "wreck found" );
            } else if ( distance < discovery.getDetectorRange() && ( nearest < 0.0f || distance < nearest ) ) {
                nearest = distance;
            }
        }
        hud.setDetector( nearest, discovery.getDetectorRange() );
    }
    // The casino (the "gamblinghall" spot, ECAS on the casino island):
    // stopped within 60 m, A / Enter switches to the CASINO game mode
    // (GMCasino). The dealerships and the clothes shops enter through
    // tickWorldSpots (the enter element above).
    {
        std::string casinoSpot;
        const bool bSlow = std::fabs( vehicle.getSpeedKmh() ) < 20.0f;
        bool bNearCasino = mapScreen.findSpotNear( carPosition, "gamblinghall", 60.0f, casinoSpot );
        // Test hook: -casinotrip takes the seaplane at frame 60 (no drive to the dock).
        static int32_t tripFrames = 0;
        if ( CmdLine::HasArg( "-casinotrip" ) && ++tripFrames == 60 ) {
            casinoSpot = "-casinotrip";
            if ( CasinoTripScenes::RequestFlightToCasino( islandName, carDesc ) ) {
                OTDU_LOG_INFO( "FREERIDE: -casinotrip -> the seaplane trip\n" );
                pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
            }
        }
        // On the casino island itself (out of the casino door): A walks back
        // in without a flight, X / Square takes the seaplane home from the
        // dock beside it (the casino departure, the landing at the island
        // the player flew from) [INF: the retail prompt is encrypted].
        const bool bOnCasinoIsland = islandName == "casino";
        static bool bCasinoHintShown = false;
        if ( bOnCasinoIsland && bSlow && bNearCasino && !hud.isLevelUpActive() ) {
            if ( !bCasinoHintShown ) {
                hud.showHint( "A / Enter: back into the casino.   X / Square: the seaplane home.", 6.0f );
                bCasinoHintShown = true;
            }
            if ( gpInput->wasPressed( eInputAction::IA_MenuOption1 ) ) {
                CasinoTrip &trip = GetCasinoTrip();
                trip.Car = carDesc;
                trip.bCarSet = true;
                if ( CasinoTripScenes::RequestFlightHome() ) {
                    OTDU_LOG_INFO( "FREERIDE: the casino dock -> the seaplane home to %s\n", trip.OriginIsland.c_str() );
                    pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
                    return;
                }
            } else if ( gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
                OTDU_LOG_INFO( "FREERIDE: back into the casino ('%s') from its door -> CASINO\n", casinoSpot.c_str() );
                pGameInstance->setNextGameMode( eGameMode::GM_Casino );
                return;
            }
        } else if ( !bNearCasino ) {
            bCasinoHintShown = false;
        }
        if ( !bOnCasinoIsland && bSlow && bNearCasino && !hud.isLevelUpActive() &&
             gpInput->wasPressed( eInputAction::IA_MenuValidate ) ) {
            // The seaplane trip (docs/re/casino_spec.md 10): the departure at
            // this island's dock, the flight and the landing on the casino
            // island, then the casino. -nocasinotrip (or a missing bank)
            // enters directly.
            if ( CasinoTripScenes::RequestFlightToCasino( islandName, carDesc ) ) {
                OTDU_LOG_INFO( "FREERIDE: entering the casino ('%s') -> the seaplane trip\n", casinoSpot.c_str() );
                pGameInstance->setNextGameMode( eGameMode::GM_CinViewer );
            } else {
                OTDU_LOG_INFO( "FREERIDE: entering the casino ('%s') -> CASINO\n", casinoSpot.c_str() );
                pGameInstance->setNextGameMode( eGameMode::GM_Casino );
            }
        }
    }
    // Dealer test hook: -dealer <index> opens a DBCarShop directly;
    // -clothesshop opens the clothes menu without the spot.
    {
        static int32_t dealerFrames = 0;
        if ( const char *pShop = CmdLine::GetArgValue( "-dealer" ) ) {
            if ( ++dealerFrames == 120 ) {
                // Test: open a DBCarShop by index (24 = the used car dealer) or
                // by its `_spot_name` / spot bank ("ECD_FERR").
                const CarShop *pTestShop = nullptr;
                if ( pShop[0] >= '0' && pShop[0] <= '9' ) {
                    pTestShop = FindCarShop( atoi( pShop ) );
                } else {
                    for ( const CarShop &shop : GetCarShops() ) {
                        if ( shop.SpotName.rfind( pShop, 0 ) == 0 ) {
                            pTestShop = &shop;
                            break;
                        }
                    }
                }
                dealer.open( pTestShop );
                dealerSpotName = pTestShop != nullptr ? pTestShop->SpotName : std::string();
                OTDU_LOG_INFO( "FREERIDE: -dealer '%s' -> %s\n", pShop,
                               pTestShop != nullptr ? pTestShop->Name.c_str() : "(not found)" );
            }
        }
        // Test: -licensetest <RSA7T01 ...> drives a licence test straight away;
        // -schoolmenu <A|B|C> opens a school's desk without the spot.
        static int32_t licenseFrames = 0;
        if ( const char *pTest = CmdLine::GetArgValue( "-licensetest" ) ) {
            if ( ++licenseFrames == 120 ) {
                startLicenseTest( pTest );
            }
        }
        static int32_t schoolFrames = 0;
        if ( const char *pSchool = CmdLine::GetArgValue( "-schoolmenu" ) ) {
            if ( ++schoolFrames == 120 ) {
                openSchool( pSchool[0], std::string() );
            }
        }
        // Test: -championship <part of its name> opens its race list.
        static int32_t championshipFrames = 0;
        if ( const char *pChamp = CmdLine::GetArgValue( "-championship" ) ) {
            if ( ++championshipFrames == 120 ) {
                for ( const ChampionshipSpot &spot : championshipSpots ) {
                    if ( spot.pChampionship->Name.find( pChamp ) != std::string::npos ) {
                        championshipMenu.open( *spot.pChampionship );
                        break;
                    }
                }
            }
        }
        static int32_t clothesFrames = 0;
        if ( CmdLine::HasArg( "-clothesshop" ) && ++clothesFrames == 120 ) {
            clothesMenu.open( LocStr( 3311, "Clothes" ) ); // test: the clothes menu without the spot
        }
    }
    // -gpsdrive: the route and the voice follow a virtual car moved along the
    // route (the real car stays where it is).
    Vector3f gpsPosition = carPosition;
    Vector3f gpsVelocity = vehicle.getVelocity();
    static int32_t gpsDriveFrames = 0;
    if ( bGpsDrive && state == eState::Drive && ++gpsDriveFrames > 60 ) {
        if ( !bGpsDriveStarted ) {
            bGpsDriveStarted = true;
            gpsDrivePosition = carPosition;
            mapScreen.tickRoute( gpsDrivePosition, 0.0f );
            mapScreen.setTarget( gpsDriveTarget );
            OTDU_LOG_INFO( "FREERIDE: -gpsdrive to %.0f,%.0f at %.1f m/s, route %.0f m\n", gpsDriveTarget.x, gpsDriveTarget.z,
                           gpsDriveSpeed, mapScreen.getRouteLength() );
        }
        float offset = 0.0f;
        Vector3f tangent;
        const float along = mapScreen.projectOnRoute( gpsDrivePosition, offset, tangent );
        const std::vector<Vector3f> &points = mapScreen.getRoutePoints();
        if ( along >= 0.0f && points.size() > 1 ) {
            // the point of the route at along + speed * dt
            const float wanted = Max( 0.0f, along + gpsDriveSpeed * deltaTime );
            float walked = 0.0f;
            Vector3f next = points.back();
            for ( size_t i = 0; i + 1 < points.size(); i++ ) {
                const Vector3f segment( points[i + 1].x - points[i].x, 0.0f, points[i + 1].z - points[i].z );
                const float length = segment.length();
                if ( walked + length >= wanted && length > 1e-4f ) {
                    next = Vector3f( points[i].x, 0.0f, points[i].z ) + segment * ( ( wanted - walked ) / length );
                    break;
                }
                walked += length;
            }
            gpsDrivePosition = Vector3f( next.x, 0.0f, next.z );
            gpsVelocity = tangent * gpsDriveSpeed;
        }
    }
    if ( bGpsDriveStarted ) {
        gpsPosition = gpsDrivePosition; // (it only moves in the Drive state)
        gpsVelocity = state == eState::Drive ? gpsVelocity : Vector3f( 0.0f, 0.0f, 0.0f );
    }
    mapScreen.tickRoute( gpsPosition, deltaTime );
    hud.setRoute( &mapScreen.getRoutePoints(), mapScreen.getRouteLength() );
    const bool bArrived = mapScreen.takeArrival();
    if ( bArrived ) {
        hud.showGoal( "You have reached your destination" ); // [INF] the retail text is not in the string tables
        if ( GetPlayerProfile().StoryStep == 9 ) {
            // Step 9 "Go Home" arrived: step 10 "Home sweet home : Caravan"
            // hands the caravan over as the home house (ApplyStoryGrants does
            // the grant; the step change itself is saved either way).
            GetPlayerProfile().StoryStep = 10;
            if ( !ApplyStoryGrants() ) {
                SaveProfile( "story step 10: arrived at the caravan" );
            }
            hud.showGoal( "Home sweet home : Caravan" ); // [DATA] the step's own name
            OTDU_LOG_INFO( "FREERIDE: story step 9 -> 10 'Home sweet home : Caravan', the caravan is your home\n" );
        }
    }
    gps.tick( mapScreen, gpsPosition, gpsVelocity, bArrived, deltaTime );
    hud.showGpsArrow( gps.getArrow() );
    traffic.setParkForward( carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    // The spawn ring prefers the links behind the car (no car pops in the
    // middle of the screen): the traffic needs the heading of the player.
    traffic.setPlayerHeading( carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );
    aircraft.setParkPose( carPosition, carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) ) );

    if ( !police.getCops().empty() || opponents.isActive() ) {
        // The traffic brakes for the chase's cop cars (and the race
        // opponents) like for any other car.
        std::vector<Vector3f> copPositions;
        for ( const ChaseMode::Cop &cop : police.getCops() ) {
            copPositions.push_back( cop.Position );
        }
        opponents.appendPositions( copPositions );
        traffic.setExtraVehicles( copPositions );
    }
    traffic.tick( deltaTime, carPosition,
                  [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
    // The traffic's own light contributions, read from the agent.World
    // matrices traffic.tick has just rebuilt (the appends used to run in the
    // light block, a full frame before the cars moved - the pools and glows
    // lagged in front of / behind the cars). Same gates as the FA_ sprite
    // toggle of Traffic::draw: front lamps at night, stop lamps while braking,
    // the blinker on its phase.
    {
        static const bool bNoProjection = CmdLine::HasArg( "-noheadlights" );
        const bool bNight = pWeather != nullptr && ( pWeather->Hour < 6 || pWeather->Hour >= 20 );
        const Vector3f playerPos = carWorld.getTranslation();
        if ( !bNoProjection && bNight ) {
            traffic.appendHeadlightCones( EdenHeadlightFrameCones(), playerPos, bNight, 8 );
        }
        if ( !bNoProjection ) {
            traffic.appendBrakeCones( EdenRearLightFrameCones(), playerPos, bNight, 6 );
        }
        traffic.appendLampGlows( EdenLampGlowFrame(), playerPos, bNight );
        // The cop cars ride the same registries: their light state is
        // advanced here (the FA_ sprite toggle in ChaseMode::draw stays the
        // lens art; these are the glows and the headlight cones).
        police.appendLampGlows( EdenLampGlowFrame(), bNight, deltaTime );
        if ( !bNoProjection && bNight ) {
            police.appendHeadlightCones( EdenHeadlightFrameCones(), playerPos, 2 );
        }
    }
    aircraft.tick( deltaTime );
    if ( bLogAircraft ) {
        aircraft.logStats( deltaTime );
    }
    // The birds of the island (game/freeride/birds.h): flocks around the
    // player, grounded when it rains hard. -nobirds / -birdsnear / -logbirds.
    if ( birds.isLoaded() ) {
        float birdRain = 0.0f, birdSun = 1.0f;
        if ( pWeather != nullptr ) {
            birdRain = pWeather->getFloat( "rain_ground_global_intensity", 0.0f );
            const Vector4f sun = pWeather->getVector4( "sun_diffuse", Vector4f( 1.0f, 1.0f, 1.0f, 1.0f ) );
            birdSun = ( sun.x + sun.y + sun.z ) / 3.0f;
        }
        // Where the camera looks is taken from the drawn frame inside Birds
        // (the view matrix), so only the weather is passed here.
        birds.tick( deltaTime, birdRain, birdSun,
                    [this]( float x, float z, float y, float &outHeight ) { return surfaceHeight( x, z, y, outHeight ); } );
        if ( bLogBirds ) {
            birds.logStats( deltaTime );
        }
    }
    // Explored roads: the DBRewardXP events "CRUISING_<ISLAND>_ROADS_AREA<n>_<10..100>"
    // pay Discovery points at 10 / 25 / 50 / 75 / 100 % of the roads of an area [DATA].
    {

        if ( !bEventCallbackWired ) {
            bEventCallbackWired = true;
            SetSkillEventCallback( [this]( int32_t skill, const std::string &name, int32_t points ) {
                hud.setProgress( MakeProgressSnapshot() );
                hud.showEarn( skill, name, points );
            } );
        }
        mapScreen.trackCar( carPosition, deltaTime );
        // Spot discovery: the buildings the car drives within range of get
        // their map icon (the retail law; discoverNear saves the profile).
        mapScreen.discoverNear( carPosition );
        for ( const std::string &event : mapScreen.takeEvents() ) {
            AwardSkillEvent( event.c_str() );
        }
        // Level up: a skill whose points crossed the next threshold shows the
        // new level, its title and the reward of the tables [DATA].
        static int32_t lastLevel[4] = { -1, -1, -1, -1 };
        for ( int32_t skill = 0; skill < 4; skill++ ) {
            const std::vector<SkillLevel> &levels = GetSkillLevels( skill );
            const int32_t at = FindSkillLevelIndex( levels, GetPlayerProfile().Points[skill] );
            if ( at < 0 ) {
                continue;
            }
            if ( gSuppressLevelUp ) {
                lastLevel[skill] = at;
                continue;
            }
            if ( lastLevel[skill] >= 0 && at > lastLevel[skill] ) {
                const SkillLevel &level = levels[at];
                hud.setProgress( MakeProgressSnapshot() );
                const bool bNewTitle = levels[lastLevel[skill]].Title != level.Title;
                // reward tier = how many title changes up to this level (1..3)
                int32_t tier = 0;
                for ( int32_t j = 1; j <= at; j++ ) {
                    tier += levels[j].Title != levels[j - 1].Title ? 1 : 0;
                }
                // A new global title: the sum of the skill levels crossed a DBPlayerLevel title.
                const FreeRideHud::ProgressSnapshot snapshot = MakeProgressSnapshot();
                std::string globalTitle;
                static int32_t lastGlobalLevel = -1;
                if ( lastGlobalLevel >= 0 && snapshot.GlobalLevel > lastGlobalLevel ) {
                    std::string before;
                    for ( const SkillLevel &g : GetGlobalLevels() ) {
                        if ( g.Level <= lastGlobalLevel ) {
                            before = g.Title;
                        }
                    }
                    if ( before != snapshot.GlobalTitle ) {
                        globalTitle = snapshot.GlobalTitle;
                    }
                }
                lastGlobalLevel = snapshot.GlobalLevel;
                hud.showLevelUp( skill, level.Reward, bNewTitle, level.Title, Max( 1, tier ), level.RewardDesc, globalTitle );
            }
            lastLevel[skill] = at;
        }
        GetPlayerProfile().ExploredPercent = mapScreen.getExploredPercent();
    }
    // Radio: keep the playlist going (next song at the end) and refresh the
    // panel while it is up.
    radio.update( deltaTime );
    if ( bRadioOpen ) {
        hud.showRadio( true, radio.getStationInfo(), radio.getCurrentArtist(), radio.getCurrentTitle(),
                       radio.getVolume() );
    }
    // Retractable roof (Hardtop.anm): advances an open / close request.
    car.updateHardtop( deltaTime );
    // Side windows: slides WND_x1 between its rest and the door frame rest.
    car.updateWindows( deltaTime );

    view.setMapView( false );
    view.setCamera( camera );
    view.setWeather( pWeather );
    view.setEnvironmentCenter( carPosition, car.isLoaded() );
    if ( gpAudio != nullptr ) {
        const Vector3f cameraForward =
            EdenRotationFromQuaternion( camera.Rotation ).transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
        gpAudio->setListener( camera.Position, cameraForward, Vector3f() );
        // Vehicle sounds (CarVSTConfig curves): rpm / speed / load / wheel
        // slip axes, spatialized at the car.
        if ( vehicleAudio.isEngineOn() ) {
            VehicleAudio::Params ap;
            // bEngineOn gates the layer gains AND the engine load ramp of
            // VehicleAudio::update (false = silence, used to duck the engine
            // with the world while the pause menu is up, and while the
            // wake-up cinematic plays): the driving update must set it,
            // otherwise every engine / rolling / skid layer stays at gain 0
            // and the car is silent outside the cinematics.
            ap.bEngineOn = state != eState::Wakeup;
            if ( state == eState::Outro ) {
                // The cinematic owns the car: the engine follows the .sce
                // track (outroRpm / outroGear, integrated by the Outro tick
                // from the track velocity - the pull-away of C_FIRST_OUTRO).
                ap.Rpm = outroRpm;
                ap.SpeedKmh = std::abs( outroSpeed ) * 3.6f;
                ap.Throttle = 0.8f; // the accelerating load [INF]
                ap.Position = carWorld.getTranslation();
                ap.Velocity = outroVelocity;
                vehicleAudio.setGear( outroGear - 1 ); // getGear() convention
                vehicleAudio.update( ap, deltaTime );
            } else {
                ap.Rpm = vehicle.getRpm();
                ap.SpeedKmh = vehicle.getSpeedKmh();
                ap.Throttle = state == eState::Drive ? gpInput->getAnalog( eInputAction::IA_CarAccel ) : 0.0f;
                ap.Position = carPosition;
                ap.Velocity = vehicle.getVelocity();
                for ( const VehicleDynamics::WheelState &wheel : vehicle.getWheels() ) {
                    if ( !wheel.bContact ) {
                        continue;
                    }
                    ap.Surface = Max( ap.Surface, wheel.Surface );
                    ap.MaxSlipRatio = Max( ap.MaxSlipRatio, fabsf( wheel.SlipRatio ) * 10.0f );
                    ap.MaxSlipAngle = Max( ap.MaxSlipAngle, fabsf( wheel.SlipAngle ) );
                }
                vehicleAudio.setGear( vehicle.getGear() );
                vehicleAudio.update( ap, deltaTime );
            }
        }
        // The player's horn (CarVSTConfig "Horn" of the car's own sound bank):
        // held while the button is, only while actually driving.
        vehicleAudio.setHorn( state == eState::Drive && gpInput != nullptr &&
                              gpInput->isDown( eInputAction::IA_CarHorn ) );
        // Ambient traffic engines (the nearest cars) and the horns Traffic
        // raised this frame (a car held up by the player).
        traffic.appendAudioAgents( audioAgents );
        trafficAudio.update( audioAgents, camera.Position, deltaTime );
        traffic.drainHornEvents( hornEvents );
        for ( const Traffic::HornEvent &event : hornEvents ) {
            trafficAudio.horn( event.Position, event.Volume );
        }
        // The island bed: sea / cliff / mountain / city / residential / plain
        // from the world around the listener, crossfaded day <-> night.
        if ( GetGameOptions().Ambience != 0 && !CmdLine::HasArg( "-noambience" ) ) {
            AmbienceAudio::Context context;
            context.Position = camera.Position;
            context.Hour = static_cast<float>( WorldClockHours() ) + static_cast<float>( WorldClockMinutes() ) / 60.0f;
            context.WaterHeight = view.getOcean().getWaterPlaneHeight();
            context.bGroundValid = view.getWorld().getHeight( camera.Position.x, camera.Position.z, context.GroundHeight );
            if ( !context.bGroundValid ) {
                context.GroundHeight = context.WaterHeight;
            }
            // A roof over the listener muffles the bed (a tunnel, a car park).
            context.Shelter = view.getWorld().isSheltered( camera.Position.x, camera.Position.z,
                                                           camera.Position.y + 0.5f, 9.0f )
                                  ? 1.0f
                                  : 0.0f;
            context.NearbyCars = traffic.countNear( camera.Position, 130.0f );
            ambience.update( context, deltaTime );
        } else {
            ambience.stop();
        }
    }
    // The pad extras (input/gamepad_output.h): the light bar flashes red / blue
    // while the police chase, the rumble follows the road (speed, surface, the
    // engine) and punches on a crash (pulse calls elsewhere in this tick).
    {
        // Road feel: speed on a rough surface plus the engine's load, silent
        // while parked / in a cinematic.
        float feel = 0.0f;
        if ( state == eState::Drive && vehicleAudio.isEngineOn() ) {
            const float speed = Clamp( vehicle.getSpeedKmh() / 180.0f, 0.0f, 1.0f );
            uint32_t surface = 0;
            for ( const VehicleDynamics::WheelState &wheel : vehicle.getWheels() ) {
                if ( wheel.bContact ) {
                    surface = Max( surface, wheel.Surface );
                }
            }
            // 0 road, 1 dirt, 2 grass: the rougher the louder the buzz.
            const float roughness = surface == 0 ? 0.16f : ( surface == 1 ? 0.42f : 0.55f );
            feel = Saturate( speed * roughness * 1.4f + vehicleAudio.getLoad() / 900.0f );
        }
        // Smooth so a bump does not step the motors.
        roadFeel += ( feel - roadFeel ) * Clamp( deltaTime * 6.0f, 0.0f, 1.0f );
        gamepad.setRoadFeel( roadFeel );
        gamepad.setWanted( police.getState() == ChaseMode::eState::Chase );
        // Adaptive triggers (DualSense): ABS / traction control from the
        // physics' own per-wheel flags, wheelspin from a driven wheel's slip.
        GamepadOutput::TriggerFeel triggers;
        triggers.bActive = state == eState::Drive && vehicleAudio.isEngineOn();
        if ( triggers.bActive ) {
            for ( const VehicleDynamics::WheelState &wheel : vehicle.getWheels() ) {
                if ( !wheel.bContact ) {
                    continue;
                }
                triggers.bAbs |= wheel.bAbs;
                triggers.bWheelspin |= wheel.bTcs || wheel.SlipRatio > 0.25f;
            }
        }
        gamepad.setTriggerFeel( triggers );
        // Wheel FFB (input/wheel_ffb.h): the rim torque from the front axle's
        // lateral load (the self-aligning pull), the centring pull at speed,
        // light past the tyres' grip peak, silent under the option / without a
        // haptic device (docs/re/ffb_spec.md).
        WheelFFB::Feel ffb;
        ffb.bActive = triggers.bActive;
        if ( ffb.bActive ) {
            const float speedKmh = vehicle.getSpeedKmh();
            const float speedNorm = Clamp( speedKmh / 180.0f, 0.0f, 1.0f );
            const std::vector<VehicleDynamics::WheelDesc> &descs = vehicle.getWheelDescs();
            const std::vector<VehicleDynamics::WheelState> &states = vehicle.getWheels();
            // Front axle: the lateral forces (the self-aligning load) against
            // the grip actually on offer (load x surface), each contribution
            // falling to a quarter past the tyre's grip peak - the rim "goes
            // light" when the tyres slide [INF: the feel, not a retail law].
            float latSum = 0.0f, maxLat = 0.0f, gripSum = 0.0f;
            uint32_t contacts = 0;
            const float optimalSlip = Max( vehicle.getSetup().TyreFront.Config[TyreSetup::Tarmac].OptimalSlipAngle, 0.02f );
            for ( size_t i = 0; i < states.size() && i < descs.size(); i++ ) {
                if ( !states[i].bContact ) {
                    continue;
                }
                contacts++;
                if ( !descs[i].bFront ) {
                    continue;
                }
                const float slipFalloff =
                    1.0f - 0.75f * Saturate( ( fabsf( states[i].SlipAngle ) - optimalSlip ) / ( 2.0f * optimalSlip ) );
                latSum += states[i].LateralForce * slipFalloff;
                maxLat += states[i].Load * states[i].SurfaceFriction;
                gripSum += states[i].SurfaceFriction;
            }
            // No fight at a standstill (full force from 10 km/h); the axle's
            // mean surface grip scales the centring, the sat term is
            // normalized by the load x grip itself.
            const float speedGate = Saturate( ( speedKmh - 2.0f ) / 8.0f );
            const float axleGrip = contacts > 0 ? gripSum / static_cast<float>( contacts ) : 1.0f;
            const float satTerm = latSum / Max( maxLat, 2000.0f );
            const float centerTerm =
                Clamp( vehicle.getSteerAngle() / 0.6f, -1.0f, 1.0f ) * speedNorm * 0.35f * axleGrip;
            ffb.Torque = Clamp( ( satTerm + centerTerm ) * speedGate, -1.0f, 1.0f );
            ffb.Damper = speedNorm;
        }
        wheelFfb.setFeel( ffb );
        wheelFfb.update( deltaTime );
        gamepad.update( deltaTime );
    }
    // The island's buildings around the car (or the map camera): the same
    // streaming the world viewer does. A bank that was dropped cleared the
    // view's lightmap list, so the house cinematic re-registers its own.
    if ( spotExteriors.tick( view, carWorld.getTranslation(), bSpotExteriors ) && outro.isLoaded() ) {
        outro.registerLightmaps( view );
    }
    addSpotExteriorCollisions();
    view.tick( deltaTime );
}

void GMFreeRide::addSpotExteriorCollisions() {
    // The buildings on the plots (dealerships, shops, houses, car washes,
    // the casino resort ...) were drawn but had no collision: the car drove
    // through their walls (the owner, 2026-11-04: "some places on islands
    // still have no collision"). 122 of the 270 exterior banks carry their
    // own Havok packfile <bank>_sc_*.shk in the bank frame [DATA], the frame
    // the models are drawn in (the spot frame T(position) R_y(heading)), the
    // same file the house flow already adds for EPH_L3DI. Added once per bank
    // and spot frame; kept when the bank is dropped (a few thousand
    // triangles a building) [INF]. -nospotcollision: off.
    static const bool bOff = CmdLine::HasArg( "-nospotcollision" );
    if ( bOff ) {
        return;
    }
    for ( const auto &pExterior : spotExteriors.getExteriors() ) {
        const std::vector<Matrix4x4f> &frames = pExterior->getFrames();
        for ( size_t f = 0; f < frames.size(); f++ ) {
            const std::string key = pExterior->getPath() + "#" + std::to_string( f );
            if ( !spotCollisionsAdded.insert( key ).second ) {
                continue;
            }
            size_t numTriangles = 0, numBoxes = 0;
            for ( const BankEntry *pEntry : pExterior->getBank().findEntriesByExtension( "shk" ) ) {
                HavokCollision spotCollision;
                if ( LoadHavokCollision( pEntry->pData, pEntry->Size, spotCollision, true ) ) {
                    collision.addCollision( spotCollision, frames[f], true );
                    numTriangles += spotCollision.getNumTriangles();
                    numBoxes += spotCollision.Boxes.size();
                }
            }
            if ( numTriangles + numBoxes > 0 ) {
                OTDU_LOG_INFO( "FREERIDE: building collision %s (frame %zu at %.0f, %.0f): %zu triangles, %zu boxes\n",
                               pExterior->getPath().c_str(), f, frames[f].getTranslation().x,
                               frames[f].getTranslation().z, numTriangles, numBoxes );
            }
        }
    }
}

void GMFreeRide::drawOffscreen() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bLoaded || pRenderer == nullptr ) {
        return;
    }
    // -no3d: physics / HUD tests without the (software) 3D rendering.
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    if ( bNo3D || ( mapScreen.isOpen() && mapScreen.isFlat() ) ) {
        return;
    }
    if ( !bEnvironmentAssigned && car.isLoaded() ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            car.assignEnvironment( *pRenderer, pCube );
            traffic.assignEnvironment( *pRenderer, pCube );
            police.assignEnvironment( *pRenderer, pCube );
            bEnvironmentAssigned = true;
        }
    }
    // The race opponents and the free ride rivals come and go: each binds the
    // view's cube once (AiCar keeps the flag).
    if ( opponents.isActive() || traffic.hasRivals() ) {
        if ( GPUTexture *pCube = view.getEnvironmentCube() ) {
            opponents.assignEnvironment( *pRenderer, pCube );
            traffic.assignRivalEnvironment( *pRenderer, pCube );
        }
    }
    // -stickertest: the pack art onto the paint materials' USER_TEX slot once
    // the Eden renderer is ready (the materials exist at spawn, the renderer
    // only from the first render — the same defer the GPS screen uses).
    if ( !bStickerTestBound && !stickerTestBank.empty() && car.isLoaded() && StickerBankCatalog::isEnabled() ) {
        bStickerTestBound = true;
        StickerBankCatalog &catalog = StickerBankCatalog::get();
        catalog.load();
        for ( size_t i = 0; i < catalog.getBankCount(); i++ ) {
            if ( StringCmpI( catalog.getBank( i ).BankFile.c_str(), stickerTestBank.c_str() ) != 0 ) {
                continue;
            }
            const std::vector<StickerEntry> *pEntries = catalog.openBank( i );
            GPUTexture *pArt = nullptr;
            if ( pEntries != nullptr && stickerTestIndex < static_cast<int32_t>( pEntries->size() ) ) {
                pArt = ( *pEntries )[static_cast<size_t>( stickerTestIndex )].pArt;
            }
            if ( pArt == nullptr ) {
                OTDU_LOG_WARN( "FREERIDE: -stickertest: bank '%s' #%d has no art texture\n", stickerTestBank.c_str(),
                               stickerTestIndex );
                break;
            }
            OTDU_LOG_INFO( "FREERIDE: -stickertest binding '%s' #%d '%s' (%ux%u) onto the USER_TEX slot\n",
                           stickerTestBank.c_str(), stickerTestIndex, ( *pEntries )[static_cast<size_t>( stickerTestIndex )].Name.c_str(),
                           pArt->Width, pArt->Height );
            car.bindStickerTexture( *pRenderer, pArt );
            break;
        }
    }
    // The dashboard sat-nav (-gps 1 enables): the interior GPS quad samples
    // a small render target redrawn car-centred from the map data (the same
    // road triangles / route / island colour map the HUD minimap and the map
    // screen draw; docs/re/opentdu1_reuse.md 1). Bound once per car load —
    // the first render initialises the target, then setGpsTexture puts it
    // into the TD_GPS satellitemap slot with TDU1's UVOffset params.
    // WIP (session 49): everything binds and logs, but the quad is not
    // visible in any UVOffset variant tried yet — default off until the
    // param semantics are cracked (see the STATE session entry).
    static const bool bGpsScreen = CmdLine::GetArgInt( "-gps", 0 ) != 0;
    static const Vector4f gpsUv = [] {
        Vector4f uv( 1.0f, 1.0f, 3.0f, 0.0f );
        if ( const char *pArg = CmdLine::GetArgValue( "-gpsuv" ) ) {
            sscanf( pArg, "%f,%f,%f", &uv.x, &uv.y, &uv.z );
        }
        return uv;
    }();
    if ( bGpsScreen && !interior.isActive() && car.isLoaded() && car.hasGps() ) {
        if ( GPUTexture *pIsland = view.getWorld().getIslandColorMap() ) {
            if ( gpsScreen.ensureTarget( gpRender->getDevice() ) ) {
                const Vector3f carPos = carWorld.getTranslation();
                const Vector3f forward = carWorld.transformVector( Vector3f( 0.0f, 0.0f, 1.0f ) );
                const std::vector<Vector3f> &route = mapScreen.getRoutePoints();
                if ( !car.isGpsBound() || gpsScreen.needsRedraw( carPos.x, carPos.z, route.size() ) ) {
                    gpsScreen.render( carPos.x, carPos.z, forward.x, forward.z, &collision, &route, pIsland,
                                      mapScreen.getExtentX(), mapScreen.getExtentZ() );
                }
                if ( !car.isGpsBound() ) {
                    car.setGpsTexture( *pRenderer, gpsScreen.getTexture(), gpsUv );
                }
            }
        }
    }
    // The sticker canvas (the profile layout / the shop's live edits):
    // composite when dirty, bind to the paint materials' sticker slot once.
    if ( StickerBankCatalog::isEnabled() && ( stickerCanvas.hasLayout() || stickerCanvas.isDirty() ) && car.isLoaded() ) {
        stickerCanvas.render( gpRender->getDevice(), gpRender->get2DRenderer() );
        if ( !bStickerCanvasBound && stickerCanvas.getTexture() != nullptr ) {
            bStickerCanvasBound = car.bindStickerTexture( *pRenderer, stickerCanvas.getTexture() ) > 0;
        }
    }
    if ( pStickerRoomCar != nullptr && interior.isActive() && stickerCanvas.getTexture() != nullptr &&
         !bStickerRoomCarBound ) {
        bStickerRoomCarBound = pStickerRoomCar->bindStickerTexture( *pRenderer, stickerCanvas.getTexture() ) > 0;
    }
    if ( interior.isActive() ) {
        // The sticker place mode cuts outside to the car (the retail editor's
        // garage-car camera): the freeride scene renders instead of the room
        // while the sticker is being placed (a room car stays in the room).
        if ( stickerCarView() && pStickerRoomCar == nullptr ) {
            view.renderOffscreen();
            return;
        }
        interior.renderOffscreen();
        return;
    }
    view.renderOffscreen();
}

void GMFreeRide::draw() {
    if ( !bLoaded ) {
        return;
    }
    static const bool bNo3D = CmdLine::HasArg( "-no3d" );
    Renderer2D &renderer = gpRender->get2DRenderer();
    const bool bFlatMap = mapScreen.isOpen() && mapScreen.isFlat();
    if ( interior.isActive() && stickerCarView() && interior.isStickUi() && pStickerRoomCar == nullptr ) {
        // The shop movie's car view: the car outside, the movie's pages over it.
        if ( !bNo3D ) {
            view.draw();
        }
        interior.getScene().drawUi();
    } else if ( interior.isActive() && ( !stickerCarView() || pStickerRoomCar != nullptr ) ) {
        // The interior replaces the island (its own scene, reticle and
        // labels); the shop menus draw over it below. The sticker place mode
        // is the exception: the car outside is the preview.
        if ( !bNo3D ) {
            interior.draw();
        }
        // The near-car spec card over the showroom (the drive HUD is hidden
        // inside the interiors; the retail showroom shows the car's data).
        hud.drawCarSpecCard( renderer );
    } else if ( !bNo3D && !bFlatMap ) {
        view.draw();
    }
    mapScreen.drawIsland( renderer );
    // The HUD is hidden while the pause menu is up (retail screenshots
    // docs/images/retail-pause-freeride.webp / retail-options-screen.png: no
    // minimap, gauge or zone badge behind the menu) [DATA].
    if ( !pauseMenu.isOpen() && !mapScreen.isOpen() && !interior.isActive() && !licenseResult.isOpen() &&
         !schoolScreen.isOpen() ) {
        hud.draw( renderer, &collision );
    }
    licenseResult.draw( renderer );
    pauseMenu.draw( renderer );
    dealer.draw( renderer );
    clothesMenu.draw( renderer );
    estate.draw( renderer );
    tuningMenu.draw( renderer );
    paintMenu.draw( renderer );
    fuelMenu.draw( renderer );
    schoolMenu.draw( renderer );
    schoolScreen.draw( renderer );
    championshipMenu.draw( renderer );
    // The world map covers the HUD (the pause menu is drawn over it above).
    mapScreen.draw( renderer );
    // Photo camera: the HUD is hidden and this bar is the only UI, so the frame
    // (and the PNG the shutter writes) is the scene. Skipped on the capture
    // frame, with the flash.
    if ( photoCamera.isOpen() && !photoCamera.isCapturing() ) {
        renderer.begin( 1280.0f, 720.0f );
        renderer.drawRect( Rect2D( 240.0f, 612.0f, 800.0f, 34.0f ), MakeRGBA( 0, 0, 0, 170 ) );
        renderer.drawText( gpFlash->getTitleFont(), photoCamera.getPrompt().c_str(), 640.0f, 618.0f, 20.0f,
                           MakeRGBA( 255, 255, 255 ), eTextAlign::TA_Center );
        renderer.end();
        if ( photoCamera.getFlashAlpha() > 0.0f ) {
            const uint32_t flash = static_cast<uint32_t>( photoCamera.getFlashAlpha() * 255.0f );
            renderer.begin();
            renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ), 0x00ffffffu | ( flash << 24 ) );
            renderer.end();
        }
    }
    if ( pauseMenu.isOpen() || mapScreen.isOpen() ) {
        hud.drawSavingLogo( renderer );
    }
    // C_FADE_FREERIDE ("fade_blanc"): out to white, then the cinematic's
    // shot fade back in from white (FadeType 1, 2.5 s).
    float whiteAlpha = -1.0f;
    if ( dreamWhiteIn >= 0.0f ) {
        whiteAlpha = Clamp( 1.0f - dreamWhiteIn / kDreamWhiteIn, 0.0f, 1.0f );
    }
    if ( state == eState::Fade ) {
        whiteAlpha = Clamp( fadeTime / kFadeDuration, 0.0f, 1.0f );
    } else if ( state == eState::Wakeup ) {
        whiteAlpha = wakeup.getWhiteAlpha();
    }
    if ( whiteAlpha >= 0.0f ) {
        const uint32_t alpha = static_cast<uint32_t>( whiteAlpha * 255.0f );
        renderer.begin();
        renderer.drawRect( Rect2D( -10000.0f, -10000.0f, 20000.0f, 20000.0f ), 0x00ffffffu | ( alpha << 24 ) );
        renderer.end();
    }
}

void GMFreeRide::exit() {
    if ( !bLoaded ) {
        return;
    }
    ClearDefaultCommandResolver();
    // The callbacks that capture this mode go with it (the next mode - a shop,
    // a test drive - would call into a deleted GMFreeRide otherwise).
    SetSkillEventCallback( nullptr );
    if ( gpRender != nullptr ) {
        gpRender->getSurfaceFx().setHeightQuery( nullptr );
    }
    vehicleAudio.unload();
    ambience.unload();
    trafficAudio.unload();
    gamepad.shutdown();
    wheelFfb.shutdown(); // never leave the wheel torqued
    radio.stop();
    opponents.stop();
    traffic.unload();
    aircraft.unload();
    birds.unload();
    police.unload();
    raceEvents.unload();
    pauseMenu.unload();
    mapScreen.unload();
    licenseResult.unload();
    schoolScreen.unload();
    photoCamera.close();
    // The sticker layer: the canvas back (the catalog's borrowed bank
    // textures go with the shutdown too).
    stickerShop.unbind();
    stickerCanvas.destroy();
    stickerCanvasCarId = -1;
    bStickerCanvasBound = false;
    StickerBankCatalog::get().shutdown();
    interior.exit();
    spotExteriors.unload();
    modProps.unload();
    hud.unload();
    wakeup.unload();
    outro.unload();
    if ( car.isLoaded() ) {
        // The car and the avatar as they leave for a scene (VehicleSceneState):
        // the scene car shows them and the next FREERIDE puts them back.
        VehicleSceneState &state = GetVehicleSceneState();
        state.bSet = true;
        state.CarFile = carDesc.FileName;
        state.bHardtopOpen = car.hasHardtop() && car.isHardtopOpen();
        state.bWindowsOpen = car.areWindowsOpen();
        state.Mud = carDirt.getMud();
        state.Dirt = carDirt.getDirt();
        state.Hardness = carDirt.getMudHardness();
        state.Wetness = carDirt.getWetness();
        state.AvatarConfig = GetPlayerProfile().AvatarConfig;
    }
    car.unload();
    view.destroy();
    collision.clear();
    spotCollisionsAdded.clear();
    bLoaded = false;
}

void GMFreeRide::drawDebug() {
    if ( ImGui::Begin( "FREERIDE" ) ) {
        const Vector3f p = carWorld.getTranslation();
        ImGui::Text( "state %d, story timer %.1f s", static_cast<int32_t>( state ), stepTimer );
        ImGui::Text( "car (%.1f, %.1f, %.1f) %.1f km/h, %.0f rpm, gear %d", p.x, p.y, p.z, vehicle.getSpeedKmh(),
                     vehicle.getRpm(), vehicle.getGear() );
        ImGui::Text( "collision: %zu sectors, %zu triangles, camera mode %d", collision.getNumSectors(),
                     collision.getNumTriangles(), chase.getMode() );
        ImGui::Text( "spot buildings: %zu banks loaded, %zu spots", spotExteriors.getNumBanks(),
                     spotExteriors.getSpots().size() );
        for ( const VehicleDynamics::WheelState &wheel : vehicle.getWheels() ) {
            ImGui::Text( "wheel %d load %.0f N comp %.3f slip %.3f / %.3f", wheel.bContact, wheel.Load, wheel.Compression,
                         wheel.SlipRatio, wheel.SlipAngle );
        }
        // Audio: the mixer layout, the island bed, the ambient traffic voices
        // and the pad extras (docs/re/ambience_audio_spec.md).
        if ( gpAudio != nullptr ) {
            ImGui::Separator();
            ImGui::Text( "audio: %s, %d Hz", gpAudio->getSpeakerLayoutName(), gpAudio->getOutputRate() );
            ImGui::Text( "ambience: %s%s, shore %.0f m, level %.2f, %zu records",
                         ambience.getEnvironmentName(), ambience.isLoaded() ? "" : " (not loaded)",
                         ambience.getShoreDistance(), ambience.getLevel(), ambience.getRecordCount() );
            ImGui::Text( "traffic: %d engine voices, %d horns", trafficAudio.getVoiceCount(),
                         trafficAudio.getHornCount() );
            ImGui::Text( "pad: %s%s%s", gamepad.isAttached() ? "attached" : "none",
                         gamepad.hasRumble() ? ", rumble" : "", gamepad.hasLED() ? ", led" : "" );
            ImGui::Text( "ffb: %s%s, torque %+.2f", wheelFfb.isAttached() ? "attached" : "none",
                         wheelFfb.hasConstant() ? ", constant" : "", wheelFfb.getTorque() );
            // Live toggles (the same values options.ini keeps).
            GameOptions &options = GetGameOptions();
            bool bAmbience = options.Ambience != 0;
            if ( ImGui::Checkbox( "ambience beds", &bAmbience ) ) {
                options.Ambience = bAmbience ? 1 : 0;
                SaveGameOptions();
            }
            bool bDualSense = options.DualSense != 0;
            if ( ImGui::Checkbox( "pad light bar + haptics", &bDualSense ) ) {
                options.DualSense = bDualSense ? 1 : 0;
                SaveGameOptions();
            }
            bool bFfb = options.FFB != 0;
            if ( ImGui::Checkbox( "wheel force feedback", &bFfb ) ) {
                options.FFB = bFfb ? 1 : 0;
                SaveGameOptions();
            }
        }
    }
    ImGui::End();
}

#include "shared.h"
#include "tdu_instance.h"

#include "audio/gs_audio.h"

#include "config/gs_config.h"
#include "core/sample_profiler.h"
#include "core/watchdog.h"
#include "database/gs_database.h"
#include "debug/coords_overlay.h"
#include "debug/gs_imgui.h"
#include "debug/watermark_overlay.h"
#include "filesystem/gs_file.h"
#include "flash/flash_player.h"
#include "flash/gs_flash.h"
#include "input/gs_input.h"
#include "physics/gs_physics.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include "game/bootmenu/gm_login.h"
#include "game/avatarchoice/gm_avatar_choice.h"
#include "game/firstwalkmode/gm_first_walk_mode.h"
#include "game/cinematic/gm_cin_viewer.h"
#include "game/assetviewer/gm_asset_viewer.h"
#include "game/cheats.h"
#include "game/casino/gm_casino.h"
#include "game/freeride/gm_freeride.h"
#include "game/worldviewer/gm_world_viewer.h"
#include "game/uiviewer/gm_ui_viewer.h"
#include "game/cine/gm_cine.h"
#include "game/shop/gm_shop.h"
#include "game/shop/gm_testcar.h"
#include "game/advanced_graphics.h"
#include "game/game_options.h"
#include "game/player_profile.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <cstdio>

#include "build_info.h"

TestDriveGameInstance *gpTestDriveInstance = nullptr;

static constexpr const char *kOpenTDU2Version = "0.1.0";

const char *TestDrive::GetVersion() { return kOpenTDU2Version; }

int32_t TestDrive::InitAndRun( const char **pCmdLineArgs,
                               const int32_t argCount ) {
    CmdLine::Initialize( argCount, pCmdLineArgs );

    const char *pLogPath = CmdLine::GetArgValue( "-log" );
    Logger::Initialize(
        CmdLine::HasArg( "-nolog" )
            ? nullptr
            : ( pLogPath != nullptr ? pLogPath : "opentdu2.log" ),
        CmdLine::HasArg( "-verbose" ), CmdLine::HasArg( "-silent" ) );
    Assert::gbBreakOnFailure = CmdLine::HasArg( "-break_on_assert_failure" );
    Assert::gbDisabled = CmdLine::HasArg( "-disable_assert" );

    // Frame loop watchdog (source/core/watchdog.h): a hang inside the frame
    // loop writes nothing, so the log simply stops and there is no trace of
    // where. It beats into "<log>-watchdog.log" and into the game log -
    // "-nowatchdog" turns it off, "-watchdog <s>" sets the period (default
    // 5 s), "-watchdoglog <path>" its own file. On top of the reports it is
    // a kill guard (the owner asked for it after a full system freeze while
    // testing): "-watchdogkill <s>" (default 60 s, 0 = off) terminates the
    // process (exit code 0x43544157) when no frame has run for that long,
    // so a wedged app does not keep the machine hostage. A debugger attached
    // suppresses the kill; the known-long jobs beat through it (the
    // -scantunnels survey, the asset index build).
    Watchdog::Initialize(
        CmdLine::HasArg( "-nolog" )
            ? nullptr
            : ( pLogPath != nullptr ? pLogPath : "opentdu2.log" ),
        CmdLine::HasArg( "-nowatchdog" ) ? 0
                                         : CmdLine::GetArgInt( "-watchdog", 5 ),
        CmdLine::GetArgValue( "-watchdoglog" ) );
    Watchdog::SetKillThreshold( CmdLine::GetArgInt( "-watchdogkill", 60 ) );
    // -sampleprof [<first>,<last>]: sample the main thread's stacks over a
    // frame window, report next to the log (source/core/sample_profiler.h).
    if ( CmdLine::HasArg( "-sampleprof" ) ) {
        std::string reportPath = pLogPath != nullptr ? pLogPath : "opentdu2.log";
        const size_t dot = reportPath.rfind( '.' );
        reportPath = ( dot != std::string::npos ? reportPath.substr( 0, dot ) : reportPath ) + "-profile.txt";
        SampleProfiler::Start( reportPath.c_str() );
    }

    OTDU_LOG_ALWAYS( "OpenTDU2 %s - Test Drive Unlimited 2 source port (%s)\n", GetVersion(),
                     OTDU_BUILD_FULL_STRING );

    TestDriveGameInstance instance;
    int32_t exitCode = 0;
    if ( instance.initialize() ) {
        instance.mainLoop();
    } else {
        exitCode = 1;
    }
    instance.terminate();
    SampleProfiler::Shutdown();
    Watchdog::Shutdown();
    Logger::Shutdown();
    return exitCode;
}

TestDriveGameInstance::TestDriveGameInstance()
    : pWindow( nullptr ),
      nextGameMode( eGameMode::GM_None ),
      bRequestedExit( false ),
      bShowDebugUI( false ),
      totalTime( 0.0f ),
      frameIndex( 0 ),
      autoExitFrame( -1 ),
      screenshotFrame( -1 ) {
    gpTestDriveInstance = this;
}

TestDriveGameInstance::~TestDriveGameInstance() {
    gpTestDriveInstance = nullptr;
}

// Loads the Vulkan loader. On macOS the LunarG SDK is often not installed
// system wide: look for it in $VULKAN_SDK and ~/VulkanSDK/<version>/macOS and
// point the loader to the MoltenVK ICD manifest of that SDK.
static bool LoadVulkanLibrary() {
    if ( SDL_Vulkan_LoadLibrary( nullptr ) ) {
        return true;
    }
#if defined( __APPLE__ )
    std::vector<std::filesystem::path> roots;
    if ( const char *pSDK = getenv( "VULKAN_SDK" ) ) {
        roots.emplace_back( pSDK );
    }
    if ( const char *pHome = getenv( "HOME" ) ) {
        std::error_code ec;
        std::vector<std::filesystem::path> versions;
        for ( const auto &entry : std::filesystem::directory_iterator(
                  std::filesystem::path( pHome ) / "VulkanSDK", ec ) ) {
            if ( entry.is_directory() ) {
                versions.push_back( entry.path() / "macOS" );
            }
        }
        std::sort( versions.rbegin(), versions.rend() ); // newest first
        roots.insert( roots.end(), versions.begin(), versions.end() );
    }
    for ( const std::filesystem::path &root : roots ) {
        const std::filesystem::path loader = root / "lib" / "libvulkan.1.dylib";
        std::error_code ec;
        if ( !std::filesystem::exists( loader, ec ) ) {
            continue;
        }
        const std::filesystem::path icd =
            root / "share" / "vulkan" / "icd.d" / "MoltenVK_icd.json";
        if ( getenv( "VK_DRIVER_FILES" ) == nullptr &&
             getenv( "VK_ICD_FILENAMES" ) == nullptr &&
             std::filesystem::exists( icd, ec ) ) {
            setenv( "VK_DRIVER_FILES", icd.string().c_str(), 0 );
        }
        const std::filesystem::path layers =
            root / "share" / "vulkan" / "explicit_layer.d";
        if ( getenv( "VK_LAYER_PATH" ) == nullptr &&
             std::filesystem::exists( layers, ec ) ) {
            setenv( "VK_LAYER_PATH", layers.string().c_str(), 0 );
        }
        OTDU_LOG_INFO( "Loading Vulkan from '%s'\n", loader.string().c_str() );
        if ( SDL_Vulkan_LoadLibrary( loader.string().c_str() ) ) {
            return true;
        }
    }
#endif
    return false;
}

bool TestDriveGameInstance::createWindow() {
    if ( !SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS ) ) {
        OTDU_LOG_ERROR( "SDL_Init failed: %s\n", SDL_GetError() );
        return false;
    }
    if ( !LoadVulkanLibrary() ) {
        OTDU_LOG_ERROR( "Failed to load Vulkan: %s\n", SDL_GetError() );
        OTDU_LOG_ERROR( "On macOS install the LunarG Vulkan SDK (it provides "
                        "MoltenVK) in ~/VulkanSDK "
                        "or set VULKAN_SDK / SDL_VULKAN_LIBRARY\n" );
        return false;
    }

    const VideoSettings &video = pConfig->getVideoSettings();
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if ( !video.bWindowed ) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // The window title always carries the build stamp (taskbar / screenshot
    // identification): bug reports quote it. -tester adds the name in front.
    char title[256];
    if ( const char *pTester = CmdLine::GetArgValue( "-tester" ) ) {
        std::snprintf( title, sizeof( title ),
                       "OpenTDU2 - Test Drive Unlimited 2 — %s · %s", pTester,
                       OTDU_BUILD_FULL_STRING );
    } else {
        std::snprintf( title, sizeof( title ), "OpenTDU2 - Test Drive Unlimited 2 — %s",
                       OTDU_BUILD_FULL_STRING );
    }

    pWindow = SDL_CreateWindow( title, video.Width, video.Height, flags );
    if ( pWindow == nullptr ) {
        OTDU_LOG_ERROR( "SDL_CreateWindow failed: %s\n", SDL_GetError() );
        return false;
    }
    return true;
}

bool TestDriveGameInstance::initialize() {
    pFile = std::make_unique<GSFile>();
    pConfig = std::make_unique<GSConfig>();
    pDatabase = std::make_unique<GSDatabase>();
    pRender = std::make_unique<GSRender>();
    pInput = std::make_unique<GSInput>();
    pPhysics = std::make_unique<GSPhysics>();
    pFlash = std::make_unique<GSFlash>();
    pImGui = std::make_unique<GSImGui>();

    // Initialization order matters (FUN_ order of the original game services
    // list: file system, config, database...).
    services = { pFile.get(),  pConfig.get(),  pDatabase.get(), pRender.get(),
                 pInput.get(), pPhysics.get(), pFlash.get(),    pImGui.get() };

    for ( GameSystem *pService : services ) {
        OTDU_LOG_INFO( "Initializing service '%s'...\n", pService->getName() );
        if ( !pService->initialize( this ) ) {
            OTDU_LOG_ERROR( "...service '%s' initialization FAILED!\n",
                            pService->getName() );
            return false;
        }
        OTDU_LOG_INFO( "...service '%s' successfully initialized.\n",
                       pService->getName() );

        // The window and the render device are created once the config is
        // known (window size, vsync...).
        if ( pService == pConfig.get() ) {
            if ( !createWindow() ) {
                return false;
            }
        } else if ( pService == pRender.get() ) {
            const VideoSettings &video = pConfig->getVideoSettings();
            if ( !pRender->createDevice(
                     pWindow, video.bVSync,
                     CmdLine::HasArg( "-vk_enable_validation_layer" ) ) ) {
                OTDU_LOG_ERROR( "Failed to create the Vulkan render device\n" );
                return false;
            }
        }
    }

    // The player's options (the OPTIONS screen; <userdata>/options.ini). Read
    // before the audio device opens: the speaker layout of the options page is
    // applied when the mixer opens its stream.
    LoadGameOptions();
    pAudio = std::make_unique<GSAudio>();
    if ( !CmdLine::HasArg( "-noaudio" ) ) {
        pAudio->setSpeakerLayout( GetGameOptions().SpeakerLayout );
        pAudio->initialize();
    }
    LoadProfile();
    // -playername (the launcher's Player tab): the profile's name, applied
    // after the save is loaded so the launcher's field wins over an older save
    // [INF: retail edits the profile name on the profile screen].
    if ( const char *pPlayerName = CmdLine::GetArgValue( "-playername" ) ) {
        if ( pPlayerName[0] != '\0' ) {
            GetPlayerProfile().Name = pPlayerName;
        }
    }
    // -onlineusername / -onlineserver (the launcher's Online tab): stored for
    // the networking work, nothing connects yet [INF].
    if ( const char *pOnlineUser = CmdLine::GetArgValue( "-onlineusername" ) ) {
        if ( pOnlineUser[0] != '\0' ) {
            const char *pServer = CmdLine::GetArgValue( "-onlineserver" );
            OTDU_LOG_ALWAYS( "Online: account '%s'%s%s (networking not implemented)\n",
                             pOnlineUser, pServer != nullptr ? ", server " : "",
                             pServer != nullptr ? pServer : "" );
        }
    }
    // -money <n> / -chips <n>: the developer cheats' wallet, once the profile is
    // in (source/game/cheats.h; the panel that uses it is F4 in the game modes).
    Cheats::applyCommandLine();
    ApplyAudioOptions();
    ApplyVideoOptions();

    // Automated test support
    autoExitFrame = CmdLine::GetArgInt( "-exitframe", -1 );
    if ( const char *pAutoplay = CmdLine::GetArgValue( "-autoplay" ) ) {
        parseAutoplay( pAutoplay );
    }
    const char *pScreenshot = CmdLine::GetArgValue( "-screenshot" );
    if ( pScreenshot != nullptr ) {
        screenshotPath = pScreenshot;
        screenshotFrame = CmdLine::GetArgInt( "-screenshotframe", 60 );
    }

    eGameMode firstGameMode = eGameMode::GM_Login;
    const char *pGameModeArg = CmdLine::GetArgValue( "-gm" );
    if ( pGameModeArg != nullptr ) {
        eGameMode requested = GetGameModeFromName( pGameModeArg );
        if ( requested == eGameMode::GM_None ) {
            requested = static_cast<eGameMode>( atoi( pGameModeArg ) );
        }
        if ( requested > eGameMode::GM_None &&
             requested < eGameMode::GM_Count ) {
            firstGameMode = requested;
        }
    }
    if ( CmdLine::HasArg( "-intro" ) ) {
        firstGameMode = eGameMode::GM_AvatarChoice;
    }
    // -assets: the asset viewer without -gm (the -intro pattern).
    if ( CmdLine::HasArg( "-assets" ) ) {
        firstGameMode = eGameMode::GM_AssetViewer;
    }
    // -uiviewer: the UI viewer without -gm (the same pattern).
    if ( CmdLine::HasArg( "-uiviewer" ) ) {
        firstGameMode = eGameMode::GM_UiViewer;
    }
    // -storystep n: start at the game mode of story step n (FUN_00b10da0
    // enter events), ported from CamilleKMI/OpenTDU2 335759a: 0 AVATAR_CHOICE,
    // 1 FIRST_WM, 2 FREERIDE, 3 CINE kind 1 (C_MEETING_TESS), 4 MISSION
    // (TESSEVEN "Driving Tess"), 5 CINE kind 2 (C_SOLAR_CROWN_E), 6 CINE kind 3
    // (C_SOLAR_CROWN_I), 7 SHOP (Todd Bishop's used car dealer), 8 FREERIDE
    // "Tuto FROG". Steps 9..13 (the step names of FUN_00b10da0's table,
    // docs/re/tess_garage_spec.md 1.2: "Go Home", "Home sweet home: Caravan",
    // "Done", "Hawai First Freeride", "Igloo First Intro") have no ported
    // cinematic or mission yet: they start in FREERIDE so a save at that step
    // is reachable and the story's own grants (the caravan at 10) apply.
    if ( CmdLine::HasArg( "-storystep" ) ) {
        const int32_t step = CmdLine::GetArgInt( "-storystep", 0 );
        static const eGameMode kStepModes[] = {
            eGameMode::GM_AvatarChoice, eGameMode::GM_FirstWalkMode, eGameMode::GM_FreeRide, eGameMode::GM_Cine,
            eGameMode::GM_Mission,      eGameMode::GM_Cine,         eGameMode::GM_Cine,     eGameMode::GM_Shop,
            eGameMode::GM_FreeRide,     eGameMode::GM_FreeRide,     eGameMode::GM_FreeRide, eGameMode::GM_FreeRide,
            eGameMode::GM_FreeRide,     eGameMode::GM_Cine };
        // 13 "Igloo First Intro" = CINE kind 0x2b (isl_igloo.bnk at the Hawai
        // spot EPH_L0A_1276, gm_cine.cpp) [INF: the step name + the kind table
        // entry; the step's enter case is encrypted].
        static const int32_t kStepCineKinds[] = { 0, 0, 0, 1, 0, 2, 3, 0, 0, 0, 0, 0, 0, 0x2b };
        if ( step >= 0 && step < 14 ) {
            firstGameMode = kStepModes[step];
            GetPlayerProfile().StoryStep = step;
            if ( kStepCineKinds[step] != 0 ) {
                GetCineRequest().Kind = kStepCineKinds[step];
                GetCineRequest().bStoryCallback = true;
            }
            if ( step >= 9 && step <= 12 ) {
                // The step's own name from the retail table, so a run says what
                // it is testing; 12/13 are the Hawai steps (pass -island hawai).
                static const char *const kStepNames[] = { "Avatar choice / First WalkMode", "First Garage",
                                                          "First Freeride",                     "Meeting Tess",
                                                          "Driving Tess",                       "Solar Crown exterior",
                                                          "Solar Crown interior",               "Used Car Dealer",
                                                          "Tuto FROG",                          "Go Home",
                                                          "Home sweet home : Caravan",          "Done",
                                                          "Hawai First Freeride",               "Igloo First Intro" };
                OTDU_LOG_INFO( "-storystep %d '%s': no ported cinematic/mission yet, starting in FREERIDE%s\n", step,
                               kStepNames[step], step >= 12 ? " (pass -island hawai)" : "" );
            }
        } else {
            OTDU_LOG_WARN( "-storystep %d: story step not implemented (0..13)\n", step );
        }
    }
    setNextGameMode( firstGameMode );
    return true;
}

void TestDriveGameInstance::terminate() {
    if ( pRender != nullptr ) {
        pRender->getDevice().waitIdle();
    }
    if ( pActiveGameMode != nullptr ) {
        pActiveGameMode->exit();
        pActiveGameMode.reset();
    }
    pAudio.reset();
    for ( auto it = services.rbegin(); it != services.rend(); ++it ) {
        ( *it )->terminate();
    }
    services.clear();

    pImGui.reset();
    pFlash.reset();
    pPhysics.reset();
    pInput.reset();
    pRender.reset();
    pDatabase.reset();
    pConfig.reset();
    pFile.reset();

    if ( pWindow != nullptr ) {
        SDL_DestroyWindow( pWindow );
        pWindow = nullptr;
    }
    SDL_Quit();
}

void TestDriveGameInstance::setNextGameMode( eGameMode mode ) {
    nextGameMode = mode;
}

std::unique_ptr<GameMode>
TestDriveGameInstance::createGameMode( eGameMode mode ) {
    switch ( mode ) {
    case eGameMode::GM_Login:
        return std::make_unique<GMLogin>();
    case eGameMode::GM_AvatarChoice:
        return std::make_unique<GMAvatarChoice>();
    case eGameMode::GM_FirstWalkMode:
        return std::make_unique<GMFirstWalkMode>();
    case eGameMode::GM_CinViewer:
        return std::make_unique<GMCinViewer>();
    case eGameMode::GM_FreeRide:
        return std::make_unique<GMFreeRide>();
    case eGameMode::GM_Casino:
        return std::make_unique<GMCasino>();
    case eGameMode::GM_WorldViewer:
        return std::make_unique<GMWorldViewer>();
    case eGameMode::GM_AssetViewer:
        return std::make_unique<GMAssetViewer>();
    case eGameMode::GM_UiViewer:
        return std::make_unique<GMUiViewer>();
    case eGameMode::GM_Cine:
        return std::make_unique<GMCine>();
    case eGameMode::GM_Shop:
        return std::make_unique<GMShop>();
    case eGameMode::GM_TestCar:
        return std::make_unique<GMTestCar>();
    default:
        OTDU_LOG_WARN( "Game mode '%s' is not implemented yet\n",
                       GetGameModeName( mode ) );
        return nullptr;
    }
}

void TestDriveGameInstance::switchToNextGameMode() {
    if ( nextGameMode == eGameMode::GM_None ) {
        return;
    }
    const eGameMode mode = nextGameMode;
    nextGameMode = eGameMode::GM_None;

    // DAT_00fd5080 +0x12c = +0x128 when the mode changes (the story / shop code
    // tests the previous mode, e.g. FUN_009f4ab0 C_INTRO_I after TESTCAR).
    if ( mode != currentGameMode ) {
        previousGameMode = currentGameMode;
    }
    currentGameMode = mode;

    pRender->getDevice().waitIdle();
    if ( pActiveGameMode != nullptr ) {
        pActiveGameMode->exit();
        pActiveGameMode.reset();
    }

    OTDU_LOG_ALWAYS( "--------------------------------------------------\n" );
    OTDU_LOG_ALWAYS( "|              GAME MODE : %s\n",
                     GetGameModeName( mode ) );
    OTDU_LOG_ALWAYS( "--------------------------------------------------\n" );

    pActiveGameMode = createGameMode( mode );
    if ( pActiveGameMode != nullptr && !pActiveGameMode->enter( this ) ) {
        OTDU_LOG_ERROR( "Failed to enter game mode '%s'\n",
                        GetGameModeName( mode ) );
        pActiveGameMode.reset();
        if ( mode != eGameMode::GM_Login ) {
            nextGameMode = eGameMode::GM_Login;
        }
    }
}

void TestDriveGameInstance::pumpEvents() {
    pInput->beginFrame();
    SDL_Event event;
    while ( SDL_PollEvent( &event ) ) {
        pImGui->processEvent( event );
        pInput->processEvent( event, pImGui->wantsKeyboard(),
                              pImGui->wantsMouse() );
        switch ( event.type ) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            bRequestedExit = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            pRender->onWindowResized();
            break;
        case SDL_EVENT_KEY_DOWN:
            if ( event.key.scancode == SDL_SCANCODE_F1 && !event.key.repeat ) {
                bShowDebugUI = !bShowDebugUI;
            }
            if ( event.key.scancode == SDL_SCANCODE_F2 && !event.key.repeat ) {
                CoordsOverlay::Toggle();
            }
            if ( event.key.scancode == SDL_SCANCODE_F6 && !event.key.repeat ) {
                CoordsOverlay::WriteMark();
            }
            break;
        default:
            break;
        }
    }
    // The button prompts (WhatPlatform values: 0 x360, 1 pc, 2 ps3): the
    // Button prompts option, or on Auto the device in use - the PC art on
    // keyboard and mouse, the pad's own family on a pad [INF: the original
    // reads the options' controller; docs/re/text_icons_spec.md 6].
    int32_t promptPlatform = 1;
    switch ( GetGameOptions().ButtonPrompts ) {
    case 1: promptPlatform = 1; break;
    case 2: promptPlatform = 0; break;
    case 3: promptPlatform = 2; break;
    default:
        if ( pInput->getLastDevice() == GSInput::eDevice::D_Pad ) {
            promptPlatform = pInput->getPadStyle() == GSInput::ePadStyle::PS_PlayStation ? 2 : 0;
        }
        break;
    }
    FlashPlayer::SetPromptPlatform( promptPlatform );
}

void TestDriveGameInstance::mainLoop() {
    uint64_t lastCounter = SDL_GetPerformanceCounter();
    const double frequency =
        static_cast<double>( SDL_GetPerformanceFrequency() );
    const bool bFixedTimestep = CmdLine::HasArg( "-fixedtimestep" );
    CoordsOverlay::SetVisible( CmdLine::HasArg( "-coords" ) );
    WatermarkOverlay::Initialize();

    // Frame-time instrumentation: update (ticks) vs draw (begin/endFrame,
    // includes the present) split, averaged and logged every 120 frames so
    // stalls are visible in the log without an external overlay.
    double updateMsSum = 0.0, updateMsMax = 0.0;
    double drawMsSum = 0.0, drawMsMax = 0.0;
    double frameMsSum = 0.0, frameMsMax = 0.0;
    double offscreenMsSum = 0.0, mainMsSum = 0.0, uiMsSum = 0.0, endMsSum = 0.0;
    uint32_t perfFrames = 0;
    // -watchdogtest <ms>: freeze the frame loop once, mid-frame, to check the
    // watchdog's stall report (developer diagnostic, source/core/watchdog.h).
    const int32_t watchdogTestMs = CmdLine::GetArgInt( "-watchdogtest", 0 );
    bool bWatchdogTestDone = false;

    while ( !bRequestedExit ) {
        switchToNextGameMode();
        Watchdog::Beat( "loop", frameIndex );
        SampleProfiler::SetFrame( static_cast<uint32_t>( frameIndex ) );

        const uint64_t counter = SDL_GetPerformanceCounter();
        float deltaTime =
            static_cast<float>( ( counter - lastCounter ) / frequency );
        lastCounter = counter;
        if ( bFixedTimestep ) {
            deltaTime = 1.0f / 60.0f;
        }
        deltaTime = Clamp( deltaTime, 0.0001f, 0.1f );
        totalTime += deltaTime;

        pumpEvents();
        runAutoplay();

        // The watchdog (source/core/watchdog.h) turns "no frame for N s" into
        // a log line naming the phase below that stopped.
        Watchdog::Beat( "update", frameIndex );

        for ( GameSystem *pService : services ) {
            if ( !pService->isPaused() ) {
                pService->tick( deltaTime );
            }
        }

        if ( pActiveGameMode != nullptr ) {
            pActiveGameMode->tick( deltaTime );
        }
        pAudio->tick();

        if ( screenshotFrame >= 0 &&
             static_cast<int64_t>( frameIndex ) == screenshotFrame ) {
            pRender->getDevice().requestScreenshot( screenshotPath.c_str() );
        }

        const uint64_t updateEnd = SDL_GetPerformanceCounter();

        Watchdog::Beat( "draw", frameIndex );
        double offscreenMs = 0.0, mainMs = 0.0, uiMs = 0.0, endMs = 0.0;
        if ( pRender->beginFrame() ) {
            uint64_t sectionStart = SDL_GetPerformanceCounter();
            if ( pActiveGameMode != nullptr ) {
                pActiveGameMode->drawOffscreen();
            }
            offscreenMs = static_cast<double>( SDL_GetPerformanceCounter() -
                                               sectionStart ) /
                          frequency * 1000.0;
            sectionStart = SDL_GetPerformanceCounter();
            pRender->getDevice().beginMainPass();
            if ( pActiveGameMode != nullptr ) {
                pActiveGameMode->draw();
            }

            mainMs = static_cast<double>( SDL_GetPerformanceCounter() -
                                          sectionStart ) /
                     frequency * 1000.0;
            sectionStart = SDL_GetPerformanceCounter();
            pImGui->beginFrame( deltaTime );
            if ( pActiveGameMode != nullptr ) {
                pActiveGameMode->drawUI();
            }
            GetAdvancedGraphics().drawUI();
            CoordsOverlay::Draw();
            WatermarkOverlay::Draw();
            if ( bShowDebugUI ) {
                for ( GameSystem *pService : services ) {
                    pService->drawDebug();
                }
                if ( pActiveGameMode != nullptr ) {
                    pActiveGameMode->drawDebug();
                }
            }
            pImGui->endFrame();
            uiMs = static_cast<double>( SDL_GetPerformanceCounter() -
                                        sectionStart ) /
                   frequency * 1000.0;
            sectionStart = SDL_GetPerformanceCounter();
            Watchdog::Beat( "present", frameIndex );
            if ( !bWatchdogTestDone && watchdogTestMs > 0 && frameIndex >= 30 ) {
                bWatchdogTestDone = true;
                OTDU_LOG_INFO( "Watchdog test: freezing the frame loop for %d ms\n",
                               watchdogTestMs );
                SDL_Delay( static_cast<uint32_t>( watchdogTestMs ) );
            }
            pRender->endFrame();
            endMs = static_cast<double>( SDL_GetPerformanceCounter() -
                                         sectionStart ) /
                    frequency * 1000.0;
        } else {
            Watchdog::Beat( "idle (no frame)", frameIndex );
            SDL_Delay( 16 );
        }

        frameIndex++;

        // A lost Vulkan device (a driver reset after a GPU hang, or the driver
        // running out of memory) can never present again: report it once and
        // leave cleanly, rather than spinning on failing presents with the
        // window frozen on the last image - the owner's "the game goes
        // unresponsive" left 639 identical vkQueueSubmit failures and a log
        // that simply stopped.
        if ( pRender->getDevice().hasDeviceLost() ) {
            OTDU_LOG_ERROR( "RenderDevice: the Vulkan device is lost - exiting "
                            "(nothing can be presented any more; the watchdog log "
                            "names the frame and the phase)\n" );
            bRequestedExit = true;
        }

        // Options > Advanced: frame rate limit.
        if ( const int32_t fpsLimit = GetGameOptions().FpsLimit;
             fpsLimit > 0 && !bFixedTimestep ) {
            const double target = frequency / fpsLimit;
            while ( static_cast<double>( SDL_GetPerformanceCounter() -
                                         counter ) < target ) {
                const double remaining =
                    ( target - static_cast<double>(
                                   SDL_GetPerformanceCounter() - counter ) ) /
                    frequency;
                if ( remaining > 0.002 ) {
                    SDL_Delay( 1 );
                }
            }
        }

        {
            const double frameMs =
                static_cast<double>( SDL_GetPerformanceCounter() - counter ) /
                frequency * 1000.0;
            const double updateMs =
                static_cast<double>( updateEnd - counter ) / frequency * 1000.0;
            const double drawMs = frameMs - updateMs;
            // The stall capture (the owner's "random unresponsiveness after
            // driving a while"): one log line per frame slower than
            // -hitchms (default 100, 0 = off). The lines around it in the log
            // (sector loads, SpotExteriors loads/unloads, weather, ...) say
            // what the update was doing when the frame went long.
            static const int32_t hitchMs = CmdLine::GetArgInt( "-hitchms", 100 );
            // The frame's texture uploads and host fence waits (batched since
            // 2026-11-04; -syncuploads = one wait per texture, the A/B).
            const RenderDevice::UploadStats uploads = pRender->getDevice().takeUploadStats();
            if ( hitchMs > 0 && frameMs >= static_cast<double>( hitchMs ) ) {
                OTDU_LOG_WARN(
                    "[hitch] frame %llu: %.0f ms (update %.0f, draw %.0f; "
                    "offscreen %.0f main %.0f ui %.0f end %.0f; uploads %u tex %.1f MB, "
                    "%u submits, %u waits %.0f ms)\n",
                    static_cast<unsigned long long>( frameIndex ), frameMs,
                    updateMs, drawMs, offscreenMs, mainMs, uiMs, endMs, uploads.Textures,
                    static_cast<double>( uploads.Bytes ) / ( 1024.0 * 1024.0 ), uploads.Submits,
                    uploads.Waits, uploads.WaitMs );
            }
            updateMsSum += updateMs;
            drawMsSum += drawMs;
            frameMsSum += frameMs;
            offscreenMsSum += offscreenMs;
            mainMsSum += mainMs;
            uiMsSum += uiMs;
            endMsSum += endMs;
            updateMsMax = Max( updateMsMax, updateMs );
            drawMsMax = Max( drawMsMax, drawMs );
            frameMsMax = Max( frameMsMax, frameMs );
            if ( ++perfFrames == 120 ) {
                OTDU_LOG_INFO(
                    "[perf] frame %llu: avg %.1f ms (fps %.0f)  update avg "
                    "%.1f / max %.1f  draw avg %.1f / max %.1f\n",
                    static_cast<unsigned long long>( frameIndex ),
                    frameMsSum / perfFrames,
                    1000.0 / ( frameMsSum / perfFrames ),
                    updateMsSum / perfFrames, updateMsMax,
                    drawMsSum / perfFrames, drawMsMax );
                OTDU_LOG_INFO( "[perf] draw split: offscreen %.1f  main %.1f  "
                               "ui %.1f  end(present) %.1f\n",
                               offscreenMsSum / perfFrames,
                               mainMsSum / perfFrames, uiMsSum / perfFrames,
                               endMsSum / perfFrames );
                // Device memory: a device lost after a long drive is often the
                // driver running out (textures + BLASes of the sectors the
                // car streamed through).
                uint64_t vramUsed = 0, vramBudget = 0;
                pRender->getDevice().getMemoryUsage( vramUsed, vramBudget );
                OTDU_LOG_INFO( "[perf] vram: %.0f MB used / %.0f MB budget\n",
                               vramUsed / ( 1024.0 * 1024.0 ),
                               vramBudget / ( 1024.0 * 1024.0 ) );
                updateMsSum = drawMsSum = frameMsSum = 0.0;
                offscreenMsSum = mainMsSum = uiMsSum = endMsSum = 0.0;
                updateMsMax = drawMsMax = frameMsMax = 0.0;
                perfFrames = 0;
            }
        }

        if ( autoExitFrame >= 0 &&
             static_cast<int64_t>( frameIndex ) >= autoExitFrame ) {
            bRequestedExit = true;
        }
    }
}

// Scripted input for automated tests, e.g.
//   -autoplay "60:press=validate;90:text=Camille;120:shot=/tmp/a.ppm"
// Commands: press=<action>, hold=<action>, release=<action>, text=<utf8>,
// shot=<path>,
//           mouse=<dx>,<dy> (relative mouse motion, pixels),
//           key=<SDL scancode name> (raw key press, e.g. key=4 main row,
//           key=Keypad 4), hold= / release=<SDL scancode name> (raw key held,
//           e.g. hold=W, hold=Left Shift) or mouse_left / mouse_right /
//           mouse_middle (mouse button held), wheel=<notches> (mouse wheel),
//           device=keyboard|pad|ps|xbox (the last-used device and pad
//           family: the button prompts)
void TestDriveGameInstance::parseAutoplay( const char *pScript ) {
    std::string script( pScript );
    size_t start = 0;
    while ( start < script.size() ) {
        size_t end = script.find( ';', start );
        if ( end == std::string::npos ) {
            end = script.size();
        }
        const std::string entry = script.substr( start, end - start );
        start = end + 1;
        const size_t colon = entry.find( ':' );
        if ( colon == std::string::npos ) {
            continue;
        }
        AutoplayCommand command;
        command.Frame =
            strtoull( entry.substr( 0, colon ).c_str(), nullptr, 10 );
        const std::string body = entry.substr( colon + 1 );
        const size_t equal = body.find( '=' );
        command.Command = body.substr( 0, equal );
        command.Argument = equal != std::string::npos ? body.substr( equal + 1 )
                                                      : std::string();
        autoplay.push_back( command );
    }
}

void TestDriveGameInstance::runAutoplay() {
    static const std::pair<const char *, eInputAction> kActions[] = {
        {"validate",    eInputAction::IA_MenuValidate    },
        {"back",        eInputAction::IA_MenuBack        },
        {"pause",       eInputAction::IA_MenuPause       },
        {"up",          eInputAction::IA_MenuUp          },
        {"down",        eInputAction::IA_MenuDown        },
        {"left",        eInputAction::IA_MenuLeft        },
        {"right",       eInputAction::IA_MenuRight       },
        {"opt1",        eInputAction::IA_MenuOption1     },
        {"opt2",        eInputAction::IA_MenuOption2     },
        {"accel",       eInputAction::IA_CarAccel        },
        {"brake",       eInputAction::IA_CarBrake        },
        {"steerleft",   eInputAction::IA_CarLeft         },
        {"steerright",  eInputAction::IA_CarRight        },
        {"handbrake",   eInputAction::IA_CarHandBrake    },
        {"camera",      eInputAction::IA_CarCamera       },
        {"backontrack", eInputAction::IA_BackOnTrack     },
        {"backview",    eInputAction::IA_CarBackView     },
        {"carcamera",   eInputAction::IA_CarCamera       },
        {"walk",        eInputAction::IA_AvatarForward   },
        {"walkleft",    eInputAction::IA_AvatarLeft      },
        {"walkright",   eInputAction::IA_AvatarRight     },
        {"radio",       eInputAction::IA_RadioChannel    },
        {"volup",       eInputAction::IA_RadioVolMore    },
        {"voldown",     eInputAction::IA_RadioVolLess    },
        {"accleft",     eInputAction::IA_AccessoriesLeft },
        {"accright",    eInputAction::IA_AccessoriesRight},
        {"accup",       eInputAction::IA_AccessoriesUp   },
        {"accdown",     eInputAction::IA_AccessoriesDown },
        {"map",         eInputAction::IA_Map             },
    };
    for ( const AutoplayCommand &command : autoplay ) {
        if ( command.Frame != frameIndex ) {
            continue;
        }
        OTDU_LOG_INFO( "Autoplay frame %llu: %s=%s\n",
                       static_cast<unsigned long long>( frameIndex ),
                       command.Command.c_str(), command.Argument.c_str() );
        if ( command.Command == "text" ) {
            pInput->injectText( command.Argument );
            continue;
        }
        if ( command.Command == "mouse" ) {
            float dx = 0.0f, dy = 0.0f;
            sscanf( command.Argument.c_str(), "%f,%f", &dx, &dy );
            pInput->injectMouseDelta( dx, dy );
            continue;
        }
        if ( command.Command == "look" ) {
            // The free look (right stick) without a pad: "look=x,y" sets it,
            // "look=0,0" releases it (the offsets then spring back).
            float lx = 0.0f, ly = 0.0f;
            sscanf( command.Argument.c_str(), "%f,%f", &lx, &ly );
            pInput->setInjectedRightStick( lx, ly );
            continue;
        }
        if ( command.Command == "move" ) {
            // The left stick without a pad: "move=x,y" (the world map pans
            // with it), "move=0,0" releases it.
            float lx = 0.0f, ly = 0.0f;
            sscanf( command.Argument.c_str(), "%f,%f", &lx, &ly );
            pInput->setInjectedLeftStick( lx, ly );
            continue;
        }
        if ( command.Command == "key" ) {
            const SDL_Scancode scancode =
                SDL_GetScancodeFromName( command.Argument.c_str() );
            if ( scancode != SDL_SCANCODE_UNKNOWN ) {
                pInput->injectKeyPress( static_cast<int32_t>( scancode ) );
            } else {
                OTDU_LOG_WARN( "Autoplay: unknown key '%s'\n",
                               command.Argument.c_str() );
            }
            continue;
        }
        if ( command.Command == "device" ) {
            // pad = the connected pad's family; ps / xbox force one.
            const bool bPad = command.Argument == "pad" || command.Argument == "ps" || command.Argument == "xbox";
            pInput->setLastDevice( bPad ? GSInput::eDevice::D_Pad : GSInput::eDevice::D_Keyboard );
            if ( command.Argument == "ps" || command.Argument == "xbox" ) {
                pInput->setPadStyle( command.Argument == "ps" ? GSInput::ePadStyle::PS_PlayStation
                                                               : GSInput::ePadStyle::PS_Xbox );
            }
            continue;
        }
        if ( command.Command == "wheel" ) {
            pInput->injectMouseWheel(
                static_cast<float>( atof( command.Argument.c_str() ) ) );
            continue;
        }
        if ( command.Command == "shot" ) {
            pRender->getDevice().requestScreenshot( command.Argument.c_str() );
            continue;
        }
        bool bAction = false;
        for ( const auto &action : kActions ) {
            if ( command.Argument != action.first ) {
                continue;
            }
            bAction = true;
            if ( command.Command == "press" ) {
                pInput->injectPress( action.second );
            } else if ( command.Command == "hold" ) {
                pInput->setInjectedHold( action.second, true );
            } else if ( command.Command == "release" ) {
                pInput->setInjectedHold( action.second, false );
            }
        }
        if ( !bAction &&
             ( command.Command == "hold" || command.Command == "release" ) ) {
            const bool bHold = command.Command == "hold";
            static const std::pair<const char *, int32_t> kButtons[] = {
                {"mouse_left",   SDL_BUTTON_LEFT  },
                {"mouse_right",  SDL_BUTTON_RIGHT },
                {"mouse_middle", SDL_BUTTON_MIDDLE}
            };
            bool bButton = false;
            for ( const auto &button : kButtons ) {
                if ( command.Argument == button.first ) {
                    pInput->setInjectedMouseButton( button.second, bHold );
                    bButton = true;
                }
            }
            if ( !bButton ) {
                const SDL_Scancode scancode =
                    SDL_GetScancodeFromName( command.Argument.c_str() );
                if ( scancode != SDL_SCANCODE_UNKNOWN ) {
                    pInput->setInjectedKeyHold(
                        static_cast<int32_t>( scancode ), bHold );
                } else {
                    OTDU_LOG_WARN( "Autoplay: unknown action / key '%s'\n",
                                   command.Argument.c_str() );
                }
            }
        }
    }
}

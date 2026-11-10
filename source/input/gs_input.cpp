#include "shared.h"
#include "gs_input.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "system/cmd_line.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <fstream>
#include <sstream>

GSInput *gpInput = nullptr;

namespace {
struct ActionName {
    eInputAction Action;
    const char *pName;
};

constexpr ActionName kActionNames[] = {
    { eInputAction::IA_CarAccel, "CAR_ACCEL" },
    { eInputAction::IA_CarBrake, "CAR_BRAKE" },
    { eInputAction::IA_CarLeft, "CAR_LEFT" },
    { eInputAction::IA_CarRight, "CAR_RIGHT" },
    { eInputAction::IA_CarHandBrake, "CAR_HBRAKE" },
    { eInputAction::IA_CarClutch, "CAR_CLUTCH" },
    { eInputAction::IA_CarGearUp, "CAR_GUP" },
    { eInputAction::IA_CarGearDown, "CAR_GDOWN" },
    { eInputAction::IA_CarCamera, "CAR_CAMERA" },
    { eInputAction::IA_CarBackView, "CAR_BACKVIEW" },
    { eInputAction::IA_CarLight, "CAR_LIGHT" },
    { eInputAction::IA_CarHorn, "CAR_HORN" },
    { eInputAction::IA_BackOnTrack, "BACK_ON_TRACK" },
    { eInputAction::IA_AvatarLeft, "AVT_MOVE_LEFT" },
    { eInputAction::IA_AvatarRight, "AVT_MOVE_RIGHT" },
    { eInputAction::IA_AvatarForward, "AVT_MOVE_FORWARD" },
    { eInputAction::IA_AvatarBackward, "AVT_MOVE_BACKWARD" },
    { eInputAction::IA_Map, "MAP" },
    { eInputAction::IA_MenuLeft, "MENU_LEFT" },
    { eInputAction::IA_MenuRight, "MENU_RIGHT" },
    { eInputAction::IA_MenuUp, "MENU_UP" },
    { eInputAction::IA_MenuDown, "MENU_DOWN" },
    { eInputAction::IA_MenuValidate, "MENU_VALIDATE" },
    { eInputAction::IA_MenuBack, "MENU_BACK" },
    { eInputAction::IA_MenuPause, "MENU_PAUSE" },
    { eInputAction::IA_MenuOption1, "MENU_OPTION_1" },
    { eInputAction::IA_MenuOption2, "MENU_OPTION_2" },
    { eInputAction::IA_RadioChannel, "RADIO_CHANNEL" },
    { eInputAction::IA_RadioVolLess, "RADIO_VOL_LESS" },
    { eInputAction::IA_RadioVolMore, "RADIO_VOL_MORE" },
    { eInputAction::IA_AccessoriesLeft, "ACCESSORIES_LEFT" },
    { eInputAction::IA_AccessoriesRight, "ACCESSORIES_RIGHT" },
    { eInputAction::IA_AccessoriesUp, "ACCESSORIES_UP" },
    { eInputAction::IA_AccessoriesDown, "ACCESSORIES_DOWN" },
    // OpenTDU2 extra, no Default_KB.xmb row (the extras block binds it).
    { eInputAction::IA_Emote, "EMOTE_MENU" },
};

// Eden key channel names (DeviceXXX.xmb / Default_KB.xmb) to SDL scancodes.
SDL_Scancode ChannelToScancode( const std::string &channel ) {
    static const std::unordered_map<std::string, SDL_Scancode> kChannels = {
        { "UP", SDL_SCANCODE_UP },         { "DOWN", SDL_SCANCODE_DOWN },
        { "LEFT", SDL_SCANCODE_LEFT },     { "RIGHT", SDL_SCANCODE_RIGHT },
        { "LSHIFT", SDL_SCANCODE_LSHIFT }, { "RSHIFT", SDL_SCANCODE_RSHIFT },
        { "LCTRL", SDL_SCANCODE_LCTRL },   { "RCTRL", SDL_SCANCODE_RCTRL },
        { "LALT", SDL_SCANCODE_LALT },     { "RALT", SDL_SCANCODE_RALT },
        { "SPACE", SDL_SCANCODE_SPACE },   { "ENTER", SDL_SCANCODE_RETURN },
        { "BACKSPACE", SDL_SCANCODE_BACKSPACE }, { "ESC", SDL_SCANCODE_ESCAPE },
        { "TAB", SDL_SCANCODE_TAB },       { "INS", SDL_SCANCODE_INSERT },
        { "SUP", SDL_SCANCODE_DELETE },    { "HOME", SDL_SCANCODE_HOME },
        { "END", SDL_SCANCODE_END },       { "PAGE_UP", SDL_SCANCODE_PAGEUP },
        { "PAGE_DOWN", SDL_SCANCODE_PAGEDOWN },
        { "PAD_0", SDL_SCANCODE_KP_0 },    { "PAD_1", SDL_SCANCODE_KP_1 },
        { "PAD_2", SDL_SCANCODE_KP_2 },    { "PAD_3", SDL_SCANCODE_KP_3 },
        { "PAD_4", SDL_SCANCODE_KP_4 },    { "PAD_5", SDL_SCANCODE_KP_5 },
        { "PAD_6", SDL_SCANCODE_KP_6 },    { "PAD_7", SDL_SCANCODE_KP_7 },
        { "PAD_8", SDL_SCANCODE_KP_8 },    { "PAD_9", SDL_SCANCODE_KP_9 },
        { "PAD_PLUS", SDL_SCANCODE_KP_PLUS }, { "PAD_MINUS", SDL_SCANCODE_KP_MINUS },
    };
    auto it = kChannels.find( channel );
    if ( it != kChannels.end() ) {
        return it->second;
    }
    if ( channel.size() == 1 ) {
        char c = static_cast<char>( toupper( static_cast<unsigned char>( channel[0] ) ) );
        if ( c >= 'A' && c <= 'Z' ) {
            return static_cast<SDL_Scancode>( SDL_SCANCODE_A + ( c - 'A' ) );
        }
        if ( c >= '1' && c <= '9' ) {
            return static_cast<SDL_Scancode>( SDL_SCANCODE_1 + ( c - '1' ) );
        }
        if ( c == '0' ) {
            return SDL_SCANCODE_0;
        }
    }
    if ( channel.size() >= 2 && channel[0] == 'F' ) {
        int32_t index = atoi( channel.c_str() + 1 );
        if ( index >= 1 && index <= 12 ) {
            return static_cast<SDL_Scancode>( SDL_SCANCODE_F1 + index - 1 );
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

// Eden pad channel names (DeviceX360Pad.xmb, DevSubType 3 = X360 pad) to
// SDL gamepad elements. The DirectInput X360 layout: BUTTON_0..3 = A B X Y,
// 4 / 5 = LB / RB, 6 / 7 = LT / RT (analog axes in SDL), 8 = start,
// 9 = back, 10 / 11 = stick clicks; the dpad comes as POV / UP..LEFT and
// as plain UP / DOWN / LEFT / RIGHT entries with DevType 2.
bool ChannelToPadBinding( const std::string &channel, int32_t &outButton, int32_t &outAxis, float &outAxisSign ) {
    outButton = -1;
    outAxis = -1;
    outAxisSign = 0.0f;
    if ( channel.rfind( "BUTTON_", 0 ) == 0 ) {
        const int32_t index = atoi( channel.c_str() + 7 );
        switch ( index ) {
        case 0: outButton = SDL_GAMEPAD_BUTTON_SOUTH; return true;
        case 1: outButton = SDL_GAMEPAD_BUTTON_EAST; return true;
        case 2: outButton = SDL_GAMEPAD_BUTTON_WEST; return true;
        case 3: outButton = SDL_GAMEPAD_BUTTON_NORTH; return true;
        case 4: outButton = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER; return true;
        case 5: outButton = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER; return true;
        case 6: outAxis = SDL_GAMEPAD_AXIS_LEFT_TRIGGER; outAxisSign = 1.0f; return true;
        case 7: outAxis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; outAxisSign = 1.0f; return true;
        case 8: outButton = SDL_GAMEPAD_BUTTON_START; return true;
        case 9: outButton = SDL_GAMEPAD_BUTTON_BACK; return true;
        case 10: outButton = SDL_GAMEPAD_BUTTON_LEFT_STICK; return true;
        case 11: outButton = SDL_GAMEPAD_BUTTON_RIGHT_STICK; return true;
        case 12: outButton = SDL_GAMEPAD_BUTTON_DPAD_UP; return true;
        case 13: outButton = SDL_GAMEPAD_BUTTON_DPAD_DOWN; return true;
        case 14: outButton = SDL_GAMEPAD_BUTTON_DPAD_LEFT; return true;
        case 15: outButton = SDL_GAMEPAD_BUTTON_DPAD_RIGHT; return true;
        default: return false;
        }
    }
    if ( channel.rfind( "STICK_0_", 0 ) == 0 ) {
        const std::string side = channel.substr( 8 );
        if ( side == "LEFT" ) { outAxis = SDL_GAMEPAD_AXIS_LEFTX; outAxisSign = -1.0f; return true; }
        if ( side == "RIGHT" ) { outAxis = SDL_GAMEPAD_AXIS_LEFTX; outAxisSign = 1.0f; return true; }
        if ( side == "UP" ) { outAxis = SDL_GAMEPAD_AXIS_LEFTY; outAxisSign = -1.0f; return true; }
        if ( side == "DOWN" ) { outAxis = SDL_GAMEPAD_AXIS_LEFTY; outAxisSign = 1.0f; return true; }
        return false;
    }
    if ( channel.rfind( "STICK_1_", 0 ) == 0 ) {
        const std::string side = channel.substr( 8 );
        if ( side == "LEFT" ) { outAxis = SDL_GAMEPAD_AXIS_RIGHTX; outAxisSign = -1.0f; return true; }
        if ( side == "RIGHT" ) { outAxis = SDL_GAMEPAD_AXIS_RIGHTX; outAxisSign = 1.0f; return true; }
        if ( side == "UP" ) { outAxis = SDL_GAMEPAD_AXIS_RIGHTY; outAxisSign = -1.0f; return true; }
        if ( side == "DOWN" ) { outAxis = SDL_GAMEPAD_AXIS_RIGHTY; outAxisSign = 1.0f; return true; }
        return false;
    }
    if ( channel.rfind( "POV_0_", 0 ) == 0 ) {
        const std::string side = channel.substr( 6 );
        if ( side == "UP" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_UP; return true; }
        if ( side == "DOWN" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_DOWN; return true; }
        if ( side == "LEFT" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_LEFT; return true; }
        if ( side == "RIGHT" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_RIGHT; return true; }
        return false;
    }
    // The dpad of the pad key maps (DevType 2; the same names as the arrow
    // keys of the keyboard maps, which ChannelToScancode handles first).
    if ( channel == "UP" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_UP; return true; }
    if ( channel == "DOWN" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_DOWN; return true; }
    if ( channel == "LEFT" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_LEFT; return true; }
    if ( channel == "RIGHT" ) { outButton = SDL_GAMEPAD_BUTTON_DPAD_RIGHT; return true; }
    return false;
}
} // namespace

GSInput::GSInput()
    : pGamepad( nullptr ),
      bTextInputActive( false ),
      bBackspacePressed( false ),
      bUIHasKeyboard( false ),
      mouseX( 0.0f ),
      mouseY( 0.0f ),
      bMouseClicked( false ),
      steeringAxis( 0.0f ) {
    gpInput = this;
    currentState.fill( false );
    previousState.fill( false );
    analogState.fill( 0.0f );
    injectedHolds.fill( false );
}

GSInput::~GSInput() { gpInput = nullptr; }

void GSInput::loadDefaultBindings() {
    auto bind = [&]( eInputAction action, std::initializer_list<SDL_Scancode> scancodes ) {
        for ( SDL_Scancode scancode : scancodes ) {
            bindings[static_cast<size_t>( action )].Scancodes.push_back( scancode );
        }
    };
    bind( eInputAction::IA_CarAccel, { SDL_SCANCODE_UP } );
    bind( eInputAction::IA_CarBrake, { SDL_SCANCODE_DOWN } );
    bind( eInputAction::IA_CarLeft, { SDL_SCANCODE_LEFT } );
    bind( eInputAction::IA_CarRight, { SDL_SCANCODE_RIGHT } );
    bind( eInputAction::IA_CarHandBrake, { SDL_SCANCODE_LSHIFT } );
    bind( eInputAction::IA_CarGearUp, { SDL_SCANCODE_SPACE } );
    bind( eInputAction::IA_CarGearDown, { SDL_SCANCODE_LALT } );
    bind( eInputAction::IA_CarCamera, { SDL_SCANCODE_C } );
    bind( eInputAction::IA_CarBackView, { SDL_SCANCODE_V } );
    bind( eInputAction::IA_BackOnTrack, { SDL_SCANCODE_B } );
    bind( eInputAction::IA_MenuLeft, { SDL_SCANCODE_LEFT } );
    bind( eInputAction::IA_MenuRight, { SDL_SCANCODE_RIGHT } );
    bind( eInputAction::IA_MenuUp, { SDL_SCANCODE_UP } );
    bind( eInputAction::IA_MenuDown, { SDL_SCANCODE_DOWN } );
    bind( eInputAction::IA_MenuValidate, { SDL_SCANCODE_RETURN } );
    bind( eInputAction::IA_MenuBack, { SDL_SCANCODE_BACKSPACE } );
    bind( eInputAction::IA_MenuPause, { SDL_SCANCODE_ESCAPE } );
    bind( eInputAction::IA_AccessoriesLeft, { SDL_SCANCODE_KP_4 } );
    bind( eInputAction::IA_AccessoriesRight, { SDL_SCANCODE_KP_6 } );
    bind( eInputAction::IA_AccessoriesUp, { SDL_SCANCODE_KP_8 } );
    bind( eInputAction::IA_AccessoriesDown, { SDL_SCANCODE_KP_2 } );
    bind( eInputAction::IA_Emote, { SDL_SCANCODE_X } );
}

void GSInput::loadDefaultPadBindings() {
    // Copy of DeviceX360Pad.xmb (the entries DevSubType 3) for the runs
    // without game data.
    auto bind = [&]( eInputAction action, int32_t button ) {
        padBindings.push_back( { action, button, -1, 0.0f } );
    };
    auto bindAxis = [&]( eInputAction action, int32_t axis, float sign ) {
        padBindings.push_back( { action, -1, axis, sign } );
    };
    bindAxis( eInputAction::IA_CarAccel, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1.0f );
    bindAxis( eInputAction::IA_CarBrake, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1.0f );
    bindAxis( eInputAction::IA_AvatarForward, SDL_GAMEPAD_AXIS_LEFTY, -1.0f );
    bindAxis( eInputAction::IA_AvatarBackward, SDL_GAMEPAD_AXIS_LEFTY, 1.0f );
    bindAxis( eInputAction::IA_AvatarLeft, SDL_GAMEPAD_AXIS_LEFTX, -1.0f );
    bindAxis( eInputAction::IA_AvatarRight, SDL_GAMEPAD_AXIS_LEFTX, 1.0f );
    bind( eInputAction::IA_CarHandBrake, SDL_GAMEPAD_BUTTON_WEST );  // X / square
    bind( eInputAction::IA_CarCamera, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER );
    bind( eInputAction::IA_CarBackView, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER );
    bind( eInputAction::IA_CarHorn, SDL_GAMEPAD_BUTTON_RIGHT_STICK );
    bind( eInputAction::IA_BackOnTrack, SDL_GAMEPAD_BUTTON_EAST );   // B / circle
    bind( eInputAction::IA_Map, SDL_GAMEPAD_BUTTON_BACK );
    bind( eInputAction::IA_MenuUp, SDL_GAMEPAD_BUTTON_DPAD_UP );
    bind( eInputAction::IA_MenuDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN );
    bind( eInputAction::IA_MenuLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT );
    bind( eInputAction::IA_MenuRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT );
    bind( eInputAction::IA_MenuValidate, SDL_GAMEPAD_BUTTON_SOUTH );
    bind( eInputAction::IA_MenuBack, SDL_GAMEPAD_BUTTON_EAST );
    bind( eInputAction::IA_MenuPause, SDL_GAMEPAD_BUTTON_START );
    bind( eInputAction::IA_AccessoriesUp, SDL_GAMEPAD_BUTTON_DPAD_UP );
    bind( eInputAction::IA_AccessoriesDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN );
    bind( eInputAction::IA_AccessoriesLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT );
    bind( eInputAction::IA_AccessoriesRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT );
    OTDU_LOG_INFO( "Key mapping: %zu default pad bindings\n", padBindings.size() );
}

bool GSInput::loadPadMap( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    const uint32_t numKeys = root.getArraySize( "KeyMap" );
    if ( numKeys == 0 ) {
        return false;
    }
    std::vector<PadBinding> loaded;
    for ( uint32_t i = 0; i < numKeys; i++ ) {
        XmbValue key = root.getArrayElement( "KeyMap", i );
        const std::string actionName = key["ActionName"].asString();
        const std::string channel = key["ChannelName"].asString();
        const std::string modifier = key["ChannelNameAND"].asString();
        if ( !modifier.empty() ) {
            continue; // key combinations are not supported yet
        }
        int32_t button = -1, axis = -1;
        float sign = 0.0f;
        if ( !ChannelToPadBinding( channel, button, axis, sign ) ) {
            continue; // a keyboard channel of the same file (DevType 0)
        }
        // RADIO_CHANNEL / RADIO_VOL_MORE / RADIO_VOL_LESS sit on the same
        // D-pad as ACCESSORIES_*: read from the pad they open the radio from
        // D-pad up / down and skip a station on the very press that opens it
        // (retail runs that direct handler, FUN_00929710, only while the flag
        // at 0x00fd8fef is clear [INF: the pad goes through the accessories
        // state machine FUN_00929be0 alone]). They stay keyboard only.
        if ( actionName.compare( 0, 6, "RADIO_" ) == 0 ) {
            continue;
        }
        for ( const ActionName &entry : kActionNames ) {
            if ( actionName == entry.pName ) {
                loaded.push_back( { entry.Action, button, axis, sign } );
            }
        }
    }
    if ( loaded.empty() ) {
        return false;
    }
    padBindings = std::move( loaded );
    OTDU_LOG_INFO( "Key mapping '%s': %zu pad bindings\n", pPath, padBindings.size() );
    return true;
}

bool GSInput::loadKeyMap( const char *pPath ) {
    XmbDocument document;
    if ( !LoadXmbFromFile( pPath, document ) ) {
        return false;
    }
    XmbValue root = document.getRoot();
    const uint32_t numKeys = root.getArraySize( "KeyMap" );
    if ( numKeys == 0 ) {
        return false;
    }

    for ( ActionBinding &binding : bindings ) {
        binding.Scancodes.clear();
    }
    uint32_t numBound = 0;
    for ( uint32_t i = 0; i < numKeys; i++ ) {
        XmbValue key = root.getArrayElement( "KeyMap", i );
        const std::string actionName = key["ActionName"].asString();
        const std::string channel = key["ChannelName"].asString();
        const std::string modifier = key["ChannelNameAND"].asString();
        if ( !modifier.empty() || channel.empty() ) {
            continue; // Key combinations are not supported yet
        }
        for ( const ActionName &entry : kActionNames ) {
            if ( actionName == entry.pName ) {
                SDL_Scancode scancode = ChannelToScancode( channel );
                if ( scancode != SDL_SCANCODE_UNKNOWN ) {
                    bindings[static_cast<size_t>( entry.Action )].Scancodes.push_back( scancode );
                    numBound++;
                    // -logkeymap: which key an action is on (the controls notes).
                    if ( CmdLine::HasArg( "-logkeymap" ) ) {
                        OTDU_LOG_INFO( "Key mapping: %s = %s\n", actionName.c_str(), channel.c_str() );
                    }
                }
            }
        }
    }
    OTDU_LOG_INFO( "Key mapping '%s': %u bindings\n", pPath, numBound );
    return numBound > 0;
}

bool GSInput::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );
    if ( !loadKeyMap( "Default_KB.xmb" ) ) {
        loadDefaultBindings();
    }
    if ( !loadPadMap( "DeviceX360Pad.xmb" ) ) {
        loadDefaultPadBindings();
    }
    // OpenTDU2 extras on the pad. The game's own pad map has no MENU_* entries
    // (its D-pad up / down are RADIO_VOL_MORE / RADIO_VOL_LESS), but the port's
    // menus are driven with the menu actions: the D-pad left / right are added
    // (in the car they open the car menu and the radio panel, in a menu they
    // navigate it). Duplicated bindings are harmless.
    padBindings.push_back( { eInputAction::IA_MenuLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT, -1, 0.0f } );
    padBindings.push_back( { eInputAction::IA_MenuRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, -1, 0.0f } );
    // Keep a couple of convenient extra bindings (WASD).
    bindings[static_cast<size_t>( eInputAction::IA_CarAccel )].Scancodes.push_back( SDL_SCANCODE_W );
    bindings[static_cast<size_t>( eInputAction::IA_CarBrake )].Scancodes.push_back( SDL_SCANCODE_S );
    bindings[static_cast<size_t>( eInputAction::IA_CarLeft )].Scancodes.push_back( SDL_SCANCODE_A );
    bindings[static_cast<size_t>( eInputAction::IA_CarRight )].Scancodes.push_back( SDL_SCANCODE_D );
    bindings[static_cast<size_t>( eInputAction::IA_MenuValidate )].Scancodes.push_back( SDL_SCANCODE_KP_ENTER );
    bindings[static_cast<size_t>( eInputAction::IA_MenuBack )].Scancodes.push_back( SDL_SCANCODE_ESCAPE );
    // BACK_ON_TRACK has no key in Default_KB.xmb (pad only, B / circle):
    // OpenTDU2 adds R for the keyboard players.
    bindings[static_cast<size_t>( eInputAction::IA_BackOnTrack )].Scancodes.push_back( SDL_SCANCODE_R );
    // The walk-mode emote / self view (no Default_KB.xmb action 0x22 row):
    // X, rebindable like the rest (controls.ini).
    bindings[static_cast<size_t>( eInputAction::IA_Emote )].Scancodes.push_back( SDL_SCANCODE_X );
    // Radio, keyboard only (the pad uses the accessories D-pad): the game's
    // own keys 1 / 2 / 3.
    bindings[static_cast<size_t>( eInputAction::IA_RadioChannel )].Scancodes.push_back( SDL_SCANCODE_1 );
    bindings[static_cast<size_t>( eInputAction::IA_RadioVolLess )].Scancodes.push_back( SDL_SCANCODE_2 );
    bindings[static_cast<size_t>( eInputAction::IA_RadioVolMore )].Scancodes.push_back( SDL_SCANCODE_3 );
    // The Driving Controls page's save (controls.ini beside options.ini)
    // overrides the defaults (the file is optional).
    if ( gpFile != nullptr ) {
        LoadControls( gpFile->getUserDataFolder() / "controls.ini" );
    }
    return true;
}

void GSInput::terminate() {
    if ( pGamepad != nullptr ) {
        SDL_CloseGamepad( pGamepad );
        pGamepad = nullptr;
    }
}

bool GSInput::wasKeyPressed( int32_t scancode ) const {
    return std::find( frameKeyPresses.begin(), frameKeyPresses.end(), scancode ) != frameKeyPresses.end();
}

bool GSInput::isKeyDown( int32_t scancode ) const {
    if ( std::find( injectedKeyHolds.begin(), injectedKeyHolds.end(), scancode ) != injectedKeyHolds.end() ) {
        return true;
    }
    if ( bUIHasKeyboard || scancode <= 0 || scancode >= SDL_SCANCODE_COUNT ) {
        return false;
    }
    const bool *pKeys = SDL_GetKeyboardState( nullptr );
    return pKeys != nullptr && pKeys[scancode];
}

void GSInput::setInjectedKeyHold( int32_t scancode, bool bHold ) {
    auto it = std::find( injectedKeyHolds.begin(), injectedKeyHolds.end(), scancode );
    if ( bHold && it == injectedKeyHolds.end() ) {
        injectedKeyHolds.push_back( scancode );
    } else if ( !bHold && it != injectedKeyHolds.end() ) {
        injectedKeyHolds.erase( it );
    }
}

void GSInput::setInjectedMouseButton( int32_t button, bool bHold ) {
    if ( button <= 0 || button > 31 ) {
        return;
    }
    if ( bHold ) {
        injectedMouseButtons |= SDL_BUTTON_MASK( button );
    } else {
        injectedMouseButtons &= ~SDL_BUTTON_MASK( button );
    }
}

GSInput::ePadStyle GSInput::getPadStyle() const {
    if ( padStyleOverride >= 0 ) {
        return static_cast<ePadStyle>( padStyleOverride );
    }
    if ( pGamepad != nullptr ) {
        switch ( SDL_GetGamepadType( pGamepad ) ) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            return ePadStyle::PS_PlayStation;
        default:
            break;
        }
    }
    return ePadStyle::PS_Xbox;
}

void GSInput::beginFrame() {
    previousState = currentState;
    framePresses.clear();
    frameKeyPresses.clear();
    textInput.clear();
    bBackspacePressed = false;
    bMouseClicked = false;
    bStartReleased = false;
    mouseDeltaX = 0.0f;
    mouseDeltaY = 0.0f;
    mouseWheel = 0.0f;
}

void GSInput::processEvent( const SDL_Event &event, bool bUICapturesKeyboard, bool bUICapturesMouse ) {
    bUIHasKeyboard = bUICapturesKeyboard;
    if ( event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_WHEEL ) {
        lastDevice = eDevice::D_Keyboard;
    } else if ( event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                ( event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs( event.gaxis.value ) > 16384 ) ) {
        lastDevice = eDevice::D_Pad;
    }
    switch ( event.type ) {
    case SDL_EVENT_KEY_DOWN:
        if ( !bUICapturesKeyboard && !event.key.repeat ) {
            frameKeyPresses.push_back( static_cast<int32_t>( event.key.scancode ) );
            for ( size_t i = 0; i < bindings.size(); i++ ) {
                for ( int32_t scancode : bindings[i].Scancodes ) {
                    if ( scancode == event.key.scancode ) {
                        framePresses.push_back( static_cast<eInputAction>( i ) );
                    }
                }
            }
        }
        if ( event.key.scancode == SDL_SCANCODE_BACKSPACE ) {
            bBackspacePressed = true;
        }
        break;
    case SDL_EVENT_KEY_UP:
        if ( !bUICapturesKeyboard &&
             ( event.key.scancode == SDL_SCANCODE_ESCAPE || event.key.scancode == SDL_SCANCODE_SPACE ||
               event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER ) ) {
            bStartReleased = true;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if ( !bUICapturesMouse && event.button.button == SDL_BUTTON_LEFT ) {
            bStartReleased = true;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        bStartReleased = true;
        break;
    case SDL_EVENT_TEXT_INPUT:
        if ( bTextInputActive ) {
            textInput += event.text.text;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        {
            // Window points -> backbuffer pixels (Retina displays)
            SDL_Window *pEventWindow = SDL_GetWindowFromID( event.motion.windowID );
            const float density = pEventWindow != nullptr ? SDL_GetWindowPixelDensity( pEventWindow ) : 1.0f;
            mouseX = event.motion.x * ( density > 0.0f ? density : 1.0f );
            mouseY = event.motion.y * ( density > 0.0f ? density : 1.0f );
            if ( !bUICapturesMouse ) {
                mouseDeltaX += event.motion.xrel * ( density > 0.0f ? density : 1.0f );
                mouseDeltaY += event.motion.yrel * ( density > 0.0f ? density : 1.0f );
            }
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if ( !bUICapturesMouse ) {
            mouseWheel += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if ( !bUICapturesMouse && event.button.button == SDL_BUTTON_LEFT ) {
            bMouseClicked = true;
            SDL_Window *pEventWindow = SDL_GetWindowFromID( event.button.windowID );
            const float density = pEventWindow != nullptr ? SDL_GetWindowPixelDensity( pEventWindow ) : 1.0f;
            mouseX = event.button.x * ( density > 0.0f ? density : 1.0f );
            mouseY = event.button.y * ( density > 0.0f ? density : 1.0f );
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        // -nopad: headless test runs ignore the pad (a connected pad feeds
        // every running instance, the player's presses would drive the test).
        if ( pGamepad == nullptr && !CmdLine::HasArg( "-nopad" ) ) {
            pGamepad = SDL_OpenGamepad( event.gdevice.which );
            OTDU_LOG_INFO( "Gamepad connected: %s\n", SDL_GetGamepadName( pGamepad ) );
        }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if ( pGamepad != nullptr && SDL_GetGamepadID( pGamepad ) == event.gdevice.which ) {
            SDL_CloseGamepad( pGamepad );
            pGamepad = nullptr;
        }
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        // The edge presses of the pad key map (DeviceX360Pad.xmb; the analog
        // bindings come from updateGamepad).
        for ( const PadBinding &binding : padBindings ) {
            if ( binding.Button == event.gbutton.button && binding.Axis < 0 ) {
                framePresses.push_back( binding.Action );
                if ( CmdLine::HasArg( "-logpad" ) ) {
                    OTDU_LOG_INFO( "Pad: button %d -> action %u\n", static_cast<int32_t>( binding.Button ),
                                   static_cast<uint32_t>( binding.Action ) );
                }
            }
        }
        break;
    default:
        break;
    }
}

bool GSInput::isMouseButtonDown( int32_t button ) const {
    return ( ( SDL_GetMouseState( nullptr, nullptr ) | injectedMouseButtons ) & SDL_BUTTON_MASK( button ) ) != 0;
}

void GSInput::updateGamepad() {
    steeringAxis = 0.0f;
    leftStick[0] = leftStick[1] = rightStick[0] = rightStick[1] = 0.0f;
    if ( bHasInjectedRightStick ) {
        // -autoplay look=x,y: the free look without a pad (the runs use -nopad).
        rightStick[0] = injectedRightStick[0];
        rightStick[1] = injectedRightStick[1];
    }
    if ( bHasInjectedLeftStick ) {
        // -autoplay move=x,y: the world map pans with the left stick.
        leftStick[0] = injectedLeftStick[0];
        leftStick[1] = injectedLeftStick[1];
    }
    if ( pGamepad == nullptr ) {
        return;
    }
    auto axis = [&]( SDL_GamepadAxis a ) {
        return SDL_GetGamepadAxis( pGamepad, a ) / 32767.0f;
    };
    auto stick = [&]( SDL_GamepadAxis a ) {
        const float value = Clamp( axis( a ), -1.0f, 1.0f );
        return std::fabs( value ) < 0.15f ? 0.0f : value;
    };
    leftStick[0] = stick( SDL_GAMEPAD_AXIS_LEFTX );
    leftStick[1] = stick( SDL_GAMEPAD_AXIS_LEFTY );
    if ( !bHasInjectedRightStick ) {
        rightStick[0] = stick( SDL_GAMEPAD_AXIS_RIGHTX );
        rightStick[1] = stick( SDL_GAMEPAD_AXIS_RIGHTY );
    }
    float steer = axis( SDL_GAMEPAD_AXIS_LEFTX );
    if ( std::fabs( steer ) < 0.12f ) {
        steer = 0.0f;
    }
    steeringAxis = Clamp( steer, -1.0f, 1.0f );
    // Analog / held state of the pad bindings (tick() reset the analog
    // values from the keyboard just before this call).
    for ( const PadBinding &binding : padBindings ) {
        float value = 0.0f;
        if ( binding.Axis >= 0 ) {
            value = Max( axis( static_cast<SDL_GamepadAxis>( binding.Axis ) ) * binding.AxisSign, 0.0f );
            if ( std::fabs( value ) < 0.12f ) {
                value = 0.0f;
            }
        } else if ( binding.Button >= 0 ) {
            value = SDL_GetGamepadButton( pGamepad, static_cast<SDL_GamepadButton>( binding.Button ) ) ? 1.0f : 0.0f;
        }
        if ( value > 0.0f ) {
            const size_t index = static_cast<size_t>( binding.Action );
            analogState[index] = Max( analogState[index], value );
        }
    }
}

void GSInput::tick( float deltaTime ) {
    const bool *pKeys = SDL_GetKeyboardState( nullptr );
    for ( size_t i = 0; i < bindings.size(); i++ ) {
        bool bDown = injectedHolds[i];
        if ( !bUIHasKeyboard && pKeys != nullptr ) {
            for ( int32_t scancode : bindings[i].Scancodes ) {
                bDown |= pKeys[scancode];
            }
        }
        currentState[i] = bDown;
        analogState[i] = bDown ? 1.0f : 0.0f;
    }
    for ( eInputAction action : injectedPresses ) {
        framePresses.push_back( action );
        // Scripted "validate" (autoplay): also a press start release.
        bStartReleased |= action == eInputAction::IA_MenuValidate;
    }
    injectedPresses.clear();
    frameKeyPresses.insert( frameKeyPresses.end(), injectedKeyPresses.begin(), injectedKeyPresses.end() );
    injectedKeyPresses.clear();
    mouseDeltaX += injectedMouseX;
    mouseDeltaY += injectedMouseY;
    injectedMouseX = injectedMouseY = 0.0f;
    mouseWheel += injectedWheel;
    injectedWheel = 0.0f;
    // Scripted clicks (autoplay hold/release=mouse_left): the same edge the
    // real SDL_EVENT_MOUSE_BUTTON_DOWN produces (wasMouseClicked).
    if ( ( injectedMouseButtons & SDL_BUTTON_MASK( SDL_BUTTON_LEFT ) ) != 0 &&
         ( previousInjectedMouseButtons & SDL_BUTTON_MASK( SDL_BUTTON_LEFT ) ) == 0 ) {
        bMouseClicked = true;
    }
    previousInjectedMouseButtons = injectedMouseButtons;
    updateGamepad();
    // The bind capture (the Driving Controls dialog): the first new key
    // press, pad button press or pad push past the threshold since
    // beginBindCapture. The pad is edge-scanned against the arm-time
    // snapshot, so a held button (the A that opened the dialog) never
    // captures.
    if ( bBindCaptureArmed && !capturedBinding.bValid ) {
        if ( !frameKeyPresses.empty() ) {
            capturedBinding = CapturedBinding{};
            capturedBinding.bValid = true;
            capturedBinding.Scancode = frameKeyPresses.front();
        } else if ( pGamepad != nullptr ) {
            for ( int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !capturedBinding.bValid; b++ ) {
                const uint8_t held = SDL_GetGamepadButton( pGamepad, static_cast<SDL_GamepadButton>( b ) ) ? 1 : 0;
                if ( held != 0 && ( b >= static_cast<int>( captureButtonSnapshot.size() ) ||
                                    captureButtonSnapshot[b] == 0 ) ) {
                    capturedBinding = CapturedBinding{};
                    capturedBinding.bValid = true;
                    capturedBinding.PadButton = b;
                }
            }
            for ( int a = 0; a < 6 && !capturedBinding.bValid; a++ ) {
                const float value = SDL_GetGamepadAxis( pGamepad, static_cast<SDL_GamepadAxis>( a ) ) / 32767.0f;
                const bool bPast = std::fabs( value ) > 0.55f;
                if ( bPast && std::fabs( captureAxisSnapshot[a] ) <= 0.55f ) {
                    capturedBinding = CapturedBinding{};
                    capturedBinding.bValid = true;
                    capturedBinding.PadAxis = a;
                    capturedBinding.AxisValue = value;
                }
            }
        }
    }
}

bool GSInput::isDown( eInputAction action ) const {
    return currentState[static_cast<size_t>( action )] ||
           analogState[static_cast<size_t>( action )] > 0.5f;
}

bool GSInput::wasPressed( eInputAction action ) const {
    return std::find( framePresses.begin(), framePresses.end(), action ) != framePresses.end();
}

float GSInput::getAnalog( eInputAction action ) const {
    return analogState[static_cast<size_t>( action )];
}

float GSInput::getSteering() const {
    float digital = ( isDown( eInputAction::IA_CarRight ) ? 1.0f : 0.0f ) -
                    ( isDown( eInputAction::IA_CarLeft ) ? 1.0f : 0.0f );
    return digital != 0.0f ? digital : steeringAxis;
}

void GSInput::startTextInput() {
    if ( !bTextInputActive && gpTestDriveInstance != nullptr ) {
        SDL_StartTextInput( gpTestDriveInstance->getWindow() );
        bTextInputActive = true;
    }
}

void GSInput::stopTextInput() {
    if ( bTextInputActive && gpTestDriveInstance != nullptr ) {
        SDL_StopTextInput( gpTestDriveInstance->getWindow() );
        bTextInputActive = false;
    }
}

void GSInput::drawDebug() {
    if ( !ImGui::Begin( "Input" ) ) {
        ImGui::End();
        return;
    }
    ImGui::Text( "Gamepad: %s", pGamepad != nullptr ? SDL_GetGamepadName( pGamepad ) : "none" );
    ImGui::Text( "Steering: %.2f", getSteering() );
    for ( const ActionName &entry : kActionNames ) {
        ImGui::Text( "%-20s %s %.2f", entry.pName, isDown( entry.Action ) ? "DOWN" : "-",
                     getAnalog( entry.Action ) );
    }
    ImGui::End();
}


// --- The Driving Controls page: bind capture, rebinding, persistence ----------

const char *GSInput::ActionNameOf( eInputAction action ) {
    for ( const ActionName &entry : kActionNames ) {
        if ( entry.Action == action ) {
            return entry.pName;
        }
    }
    return nullptr;
}

void GSInput::beginBindCapture() {
    bBindCaptureArmed = true;
    bBindCaptureAxisArmed = true;
    capturedBinding = CapturedBinding{};
    // Snapshot the pad so a button / trigger held when the dialog opened
    // (the A that validated the row) does not count as the new binding: only
    // a new press (a rising edge against this snapshot) captures.
    captureButtonSnapshot.assign( SDL_GAMEPAD_BUTTON_COUNT, 0 );
    if ( pGamepad != nullptr ) {
        for ( int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++ ) {
            captureButtonSnapshot[b] = SDL_GetGamepadButton( pGamepad, static_cast<SDL_GamepadButton>( b ) ) ? 1 : 0;
        }
        for ( int a = 0; a < 6; a++ ) {
            captureAxisSnapshot[a] = SDL_GetGamepadAxis( pGamepad, static_cast<SDL_GamepadAxis>( a ) ) / 32767.0f;
        }
    }
}

bool GSInput::takeCapturedBinding( CapturedBinding &out ) {
    if ( !capturedBinding.bValid ) {
        return false;
    }
    out = capturedBinding;
    capturedBinding = CapturedBinding{};
    bBindCaptureArmed = false;
    return true;
}

void GSInput::setPadBinding( eInputAction action, const CapturedBinding &cap ) {
    clearPadBinding( action );
    if ( cap.PadButton >= 0 ) {
        padBindings.push_back( { action, cap.PadButton, -1, 0.0f } );
    } else if ( cap.PadAxis >= 0 ) {
        padBindings.push_back( { action, -1, cap.PadAxis, cap.AxisValue >= 0.0f ? 1.0f : -1.0f } );
    }
}

void GSInput::clearPadBinding( eInputAction action ) {
    padBindings.erase( std::remove_if( padBindings.begin(), padBindings.end(),
                                       [action]( const PadBinding &binding ) {
                                           return binding.Action == action;
                                       } ),
                       padBindings.end() );
}

void GSInput::setKeyboardBinding( eInputAction action, int32_t scancode ) {
    bindings[static_cast<size_t>( action )].Scancodes = { scancode };
}

void GSInput::clearKeyboardBinding( eInputAction action ) {
    bindings[static_cast<size_t>( action )].Scancodes.clear();
}


// The controls.ini text: the whole binding state, one line per action and
// device kind. The Driving Controls page saves it (Y / Save); Back restores
// the text taken when the page opened.
//
//   [controls]
//   kb_CAR_ACCEL = 26, 22      SDL scancodes ('none' = unbound)
//   pad_CAR_ACCEL = a5+        a<axis>+/a<axis>- (a push), b<button>, or none
std::string GSInput::SerializeControls() const {
    std::string text = "[controls]\n";
    for ( const ActionName &entry : kActionNames ) {
        const size_t index = static_cast<size_t>( entry.Action );
        text += "kb_";
        text += entry.pName;
        text += " = ";
        const std::vector<int32_t> &scancodes = bindings[index].Scancodes;
        if ( scancodes.empty() ) {
            text += "none";
        } else {
            for ( size_t i = 0; i < scancodes.size(); i++ ) {
                if ( i != 0 ) {
                    text += ", ";
                }
                text += std::to_string( scancodes[i] );
            }
        }
        text += "\n";
    }
    for ( const ActionName &entry : kActionNames ) {
        text += "pad_";
        text += entry.pName;
        text += " = ";
        bool bFound = false;
        for ( const PadBinding &binding : padBindings ) {
            if ( binding.Action != entry.Action ) {
                continue;
            }
            if ( bFound ) {
                text += ", ";
            }
            bFound = true;
            if ( binding.Button >= 0 ) {
                text += "b" + std::to_string( binding.Button );
            } else if ( binding.Axis >= 0 ) {
                text += "a" + std::to_string( binding.Axis ) +
                        ( binding.AxisSign >= 0.0f ? "+" : "-" );
            }
        }
        if ( !bFound ) {
            text += "none";
        }
        text += "\n";
    }
    return text;
}

void GSInput::RestoreControls( const std::string &text ) {
    // Parse "key = value" lines of the [controls] section.
    std::string line;
    size_t at = 0;
    while ( at <= text.size() ) {
        size_t end = text.find( '\n', at );
        if ( end == std::string::npos ) {
            end = text.size();
        }
        line = text.substr( at, end - at );
        at = end + 1;
        const size_t comment = line.find( ';' );
        if ( comment != std::string::npos ) {
            line = line.substr( 0, comment );
        }
        const size_t equals = line.find( '=' );
        if ( equals == std::string::npos ) {
            continue;
        }
        std::string key = line.substr( 0, equals );
        while ( !key.empty() && ( key.back() == ' ' || key.back() == '\t' ) ) {
            key.pop_back();
        }
        std::string value = line.substr( equals + 1 );
        size_t first = value.find_first_not_of( " \t" );
        if ( first == std::string::npos ) {
            continue;
        }
        value = value.substr( first );
        while ( !value.empty() &&
                ( value.back() == ' ' || value.back() == '\t' ||
                  value.back() == '\r' ) ) {
            value.pop_back();
        }
        // Resolve the action from the key (kb_<name> / pad_<name>).
        const bool bKeyboard = key.rfind( "kb_", 0 ) == 0;
        const bool bPad = key.rfind( "pad_", 0 ) == 0;
        if ( !bKeyboard && !bPad ) {
            continue;
        }
        const std::string name =
            key.substr( bPad ? 4 : 3 ); // past the 'pad_' / 'kb_' prefix
        eInputAction action = eInputAction::IA_Count;
        for ( const ActionName &entry : kActionNames ) {
            if ( name == entry.pName ) {
                action = entry.Action;
                break;
            }
        }
        if ( action == eInputAction::IA_Count ) {
            continue;
        }
        if ( bKeyboard ) {
            bindings[static_cast<size_t>( action )].Scancodes.clear();
            if ( value != "none" && !value.empty() ) {
                size_t pos = 0;
                while ( pos < value.size() ) {
                    const size_t comma = value.find( ',', pos );
                    const std::string token =
                        value.substr( pos, comma == std::string::npos ? std::string::npos : comma - pos );
                    if ( !token.empty() && token != "none" ) {
                        const int scancode = atoi( token.c_str() );
                        if ( scancode > 0 ) {
                            bindings[static_cast<size_t>( action )].Scancodes.push_back( scancode );
                        }
                    }
                    if ( comma == std::string::npos ) {
                        break;
                    }
                    pos = comma + 1;
                }
            }
        } else {
            clearPadBinding( action );
            if ( value != "none" && !value.empty() ) {
                size_t pos = 0;
                while ( pos < value.size() ) {
                    const size_t comma = value.find( ',', pos );
                    std::string token =
                        value.substr( pos, comma == std::string::npos ? std::string::npos : comma - pos );
                    while ( !token.empty() && ( token.front() == ' ' || token.front() == '\t' ) ) {
                        token.erase( token.begin() );
                    }
                    if ( !token.empty() && token != "none" ) {
                        CapturedBinding cap;
                        if ( token[0] == 'b' ) {
                            cap.PadButton = atoi( token.c_str() + 1 );
                            cap.bValid = cap.PadButton >= 0;
                        } else if ( token[0] == 'a' ) {
                            cap.PadAxis = atoi( token.c_str() + 1 );
                            cap.AxisValue = !token.empty() && token.back() == '-' ? -1.0f : 1.0f;
                            cap.bValid = cap.PadAxis >= 0;
                        }
                        if ( cap.bValid ) {
                            setPadBinding( action, cap );
                        }
                    }
                    if ( comma == std::string::npos ) {
                        break;
                    }
                    pos = comma + 1;
                }
            }
        }
    }
}

bool GSInput::SaveControls( const std::filesystem::path &file ) const {
    std::ofstream out( file );
    if ( !out ) {
        OTDU_LOG_WARN( "GSInput: cannot write '%s'\n", file.string().c_str() );
        return false;
    }
    out << SerializeControls();
    OTDU_LOG_INFO( "GSInput: controls saved '%s'\n", file.string().c_str() );
    return true;
}

void GSInput::LoadControls( const std::filesystem::path &file ) {
    std::ifstream in( file );
    if ( !in ) {
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    RestoreControls( buffer.str() );
    OTDU_LOG_INFO( "GSInput: controls loaded '%s'\n", file.string().c_str() );
}

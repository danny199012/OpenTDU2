#pragma once

#include "game/gs_base.h"

#include <filesystem>

union SDL_Event;
struct SDL_Gamepad;

// Logical actions (names from Default_KB.xmb / Device*.xmb).
enum class eInputAction : uint32_t {
    IA_CarAccel = 0,
    IA_CarBrake,
    IA_CarLeft,
    IA_CarRight,
    IA_CarHandBrake,
    IA_CarClutch,
    IA_CarGearUp,
    IA_CarGearDown,
    IA_CarCamera,
    IA_CarBackView,
    IA_CarLight,
    IA_CarHorn,
    IA_BackOnTrack,
    IA_AvatarLeft,
    IA_AvatarRight,
    IA_AvatarForward,
    IA_AvatarBackward,
    IA_Map,
    IA_MenuLeft,
    IA_MenuRight,
    IA_MenuUp,
    IA_MenuDown,
    IA_MenuValidate,
    IA_MenuBack,
    IA_MenuPause,
    IA_MenuOption1,
    IA_MenuOption2,
    // Car radio (Default_KB.xmb RADIO_CHANNEL / RADIO_VOL_LESS / RADIO_VOL_MORE,
    // keys 1 / 2 / 3): the D-pad right opens the radio panel on the pad.
    IA_RadioChannel,
    IA_RadioVolLess,
    IA_RadioVolMore,
    // In-car accessories wheel / radio panel (ACCESSORIES_LEFT / RIGHT / UP /
    // DOWN, retail action ids 0x34..0x37 read by FUN_00929be0): the D-pad in
    // DeviceX360Pad.xmb, numpad 4 / 6 / 8 / 2 in Default_KB.xmb.
    IA_AccessoriesLeft,
    IA_AccessoriesRight,
    IA_AccessoriesUp,
    IA_AccessoriesDown,
    // OpenTDU2 extra: the walk-mode emote / self view (docs/re/emotes_spec.md;
    // the retail action 0x22 binding is ENC, X is ours).
    IA_Emote,

    IA_Count
};

class GSInput : public GameSystem {
public:
    GSInput();
    ~GSInput();

    const char *getName() const override { return "Service : Input"; }
    bool initialize( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void terminate() override;
    void drawDebug() override;

    void beginFrame();
    void processEvent( const SDL_Event &event, bool bUICapturesKeyboard, bool bUICapturesMouse );

    // Digital state
    bool isDown( eInputAction action ) const;
    bool wasPressed( eInputAction action ) const;
    // Analog value in [0, 1] (triggers/pedals) or [-1, 1] (steering axis).
    float getAnalog( eInputAction action ) const;
    float getSteering() const;

    // The open gamepad (nullptr without one): the light bar / rumble of the
    // FREERIDE pad go through it (input/gamepad_output.h).
    inline SDL_Gamepad *getGamepad() const { return pGamepad; }

    // Text input (profile name...)
    void startTextInput();
    void stopTextInput();
    const std::string &getTextInput() const { return textInput; }
    void clearTextInput() { textInput.clear(); }
    void injectText( const std::string &text ) { textInput += text; }
    bool wasBackspacePressed() const { return bBackspacePressed; }

    // Mouse (window pixels)
    float getMouseX() const { return mouseX; }
    float getMouseY() const { return mouseY; }
    bool wasMouseClicked() const { return bMouseClicked; }
    // Relative mouse motion this frame (backbuffer pixels).
    float getMouseDeltaX() const { return mouseDeltaX; }
    float getMouseDeltaY() const { return mouseDeltaY; }
    bool isMouseButtonDown( int32_t button ) const;
    // Gamepad sticks in [-1, 1] (dead zone applied; y up = -1 like SDL).
    float getLeftStickX() const { return leftStick[0]; }
    float getLeftStickY() const { return leftStick[1]; }
    float getRightStickX() const { return rightStick[0]; }
    float getRightStickY() const { return rightStick[1]; }
    // FUN_00d5eed0 ("press start"): release of ESC, SPACE, ENTER, the left
    // mouse button or any pad button this frame (edge on release).
    bool wasStartReleased() const { return bStartReleased; }
    // Raw key press this frame (SDL scancode, debug keys not in the key maps).
    bool wasKeyPressed( int32_t scancode ) const;
    // Raw key held (SDL scancode; false while the UI has the keyboard).
    bool isKeyDown( int32_t scancode ) const;
    // Mouse wheel motion this frame (notches, up > 0; 0 over the UI).
    float getMouseWheel() const { return mouseWheel; }

    // Device of the last user input (keyboard/mouse or pad). The game itself
    // uses the controller selected in the options instead: profile +0x45
    // IsX360PadIconsUsed = (selected device type == 3) (FUN_0093d620,
    // FUN_00acaaf0 -> flash "/:IsPC_X360"), and FUN_00d87cc0 picks the pad
    // bitmap form per key map entry (DevSubType == 3).
    enum class eDevice : uint8_t { D_Keyboard = 0, D_Pad };
    eDevice getLastDevice() const { return lastDevice; }
    // Scripted device switch (autoplay device=...).
    void setLastDevice( eDevice device ) { lastDevice = device; }
    // Family of the connected pad, for the button prompts: PlayStation for
    // the SDL PS3 / PS4 / PS5 types, Xbox for everything else (the PC
    // original's pad art). setPadStyle forces it (autoplay device=ps|xbox).
    enum class ePadStyle : uint8_t { PS_Xbox = 0, PS_PlayStation };
    ePadStyle getPadStyle() const;
    void setPadStyle( ePadStyle style ) { padStyleOverride = static_cast<int8_t>( style ); }

    // Scripted input injection (automated tests).
    void injectPress( eInputAction action ) { injectedPresses.push_back( action ); }
    // Scripted right stick (the free look), for the automated tests.
    void setInjectedRightStick( float x, float y ) {
        injectedRightStick[0] = x;
        injectedRightStick[1] = y;
        bHasInjectedRightStick = true;
    }
    void clearInjectedRightStick() { bHasInjectedRightStick = false; }
    // Scripted left stick (the world map pans with it), for the automated tests.
    void setInjectedLeftStick( float x, float y ) {
        injectedLeftStick[0] = x;
        injectedLeftStick[1] = y;
        bHasInjectedLeftStick = true;
    }
    void clearInjectedLeftStick() { bHasInjectedLeftStick = false; }
    // Raw key press next tick (wasKeyPressed), SDL scancode.
    void injectKeyPress( int32_t scancode ) { injectedKeyPresses.push_back( scancode ); }
    void injectMouseDelta( float dx, float dy ) { injectedMouseX += dx; injectedMouseY += dy; }
    void setInjectedHold( eInputAction action, bool bHold ) { injectedHolds[static_cast<uint32_t>( action )] = bHold; }
    // Raw key / mouse button held (SDL scancode / SDL_BUTTON_*), wheel notches.
    void setInjectedKeyHold( int32_t scancode, bool bHold );
    void setInjectedMouseButton( int32_t button, bool bHold );
    void injectMouseWheel( float notches ) { injectedWheel += notches; }

    // One binding of DeviceX360Pad.xmb: an Eden pad channel (BUTTON_n,
    // STICK_n_*, POV_n_*, or the dpad UP/DOWN/LEFT/RIGHT with DevType 2)
    // resolved to an SDL gamepad element. BUTTON_6 / BUTTON_7 of the DInput
    // X360 layout are the triggers (analog axes in SDL). Public: the Driving
    // Controls page (options) rebinds actions at runtime.
    struct PadBinding {
        eInputAction Action;
        int32_t Button = -1; // SDL_GAMEPAD_BUTTON_*, -1 = none
        int32_t Axis = -1;   // SDL_GAMEPAD_AXIS_*, -1 = none
        float AxisSign = 0.0f; // side of the axis (STICK_0_LEFT = -1 x)
    };

    // The Driving Controls page (options -> Controls): the bind dialog waits
    // for the next raw input; the game arms the capture, then takes what the
    // user pressed (a key, a pad button, or a pad push past the threshold).
    struct CapturedBinding {
        bool bValid = false;
        int32_t Scancode = -1;  // a keyboard press (SDL scancode), else -1
        int32_t PadButton = -1; // a pad button press (SDL_GAMEPAD_BUTTON_*), else -1
        int32_t PadAxis = -1;   // a pad push (SDL_GAMEPAD_AXIS_*), else -1
        float AxisValue = 0.0f; // the side pushed (+ / -)
    };
    void beginBindCapture();
    bool takeCapturedBinding( CapturedBinding &out );

    // Rebinding (live: the current bindings drive the game immediately).
    void setPadBinding( eInputAction action, const CapturedBinding &cap );
    void clearPadBinding( eInputAction action );
    void setKeyboardBinding( eInputAction action, int32_t scancode );
    void clearKeyboardBinding( eInputAction action );
    const std::vector<PadBinding> &getPadBindings() const { return padBindings; }
    const std::vector<int32_t> &getKeyboardScancodes( eInputAction action ) const {
        return bindings[static_cast<size_t>( action )].Scancodes;
    }

    // The bindings as data (controls.ini beside options.ini): the whole
    // state, so the options' Back can restore and the save persists.
    std::string SerializeControls() const;
    void RestoreControls( const std::string &text );
    bool SaveControls( const std::filesystem::path &file ) const;
    void LoadControls( const std::filesystem::path &file );

    // Stable per-action names (the controls.ini keys).
    static const char *ActionNameOf( eInputAction action );

private:
    struct ActionBinding {
        std::vector<int32_t> Scancodes;
    };

    std::array<ActionBinding, static_cast<size_t>( eInputAction::IA_Count )> bindings;
    std::array<bool, static_cast<size_t>( eInputAction::IA_Count )> currentState;
    std::array<bool, static_cast<size_t>( eInputAction::IA_Count )> previousState;
    std::array<float, static_cast<size_t>( eInputAction::IA_Count )> analogState;
    std::array<bool, static_cast<size_t>( eInputAction::IA_Count )> injectedHolds;
    std::vector<PadBinding> padBindings;
    // The bind capture (beginBindCapture / takeCapturedBinding).
    bool bBindCaptureArmed = false;
    bool bBindCaptureAxisArmed = false;
    CapturedBinding capturedBinding;
    std::vector<uint8_t> captureButtonSnapshot; // the pad buttons at arm time
    std::array<float, 6> captureAxisSnapshot = {};
    std::vector<eInputAction> injectedPresses;
    std::vector<eInputAction> framePresses;
    std::vector<int32_t> frameKeyPresses;
    std::vector<int32_t> injectedKeyPresses;
    SDL_Gamepad *pGamepad;
    std::string textInput;
    bool bTextInputActive;
    bool bBackspacePressed;
    bool bUIHasKeyboard;
    float mouseX;
    float mouseY;
    bool bMouseClicked;
    float injectedMouseX = 0.0f;
    float injectedMouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float mouseWheel = 0.0f;
    float injectedWheel = 0.0f;
    std::vector<int32_t> injectedKeyHolds;
    uint32_t injectedMouseButtons = 0;
    uint32_t previousInjectedMouseButtons = 0;
    float leftStick[2] = { 0.0f, 0.0f };
    float rightStick[2] = { 0.0f, 0.0f };
    float injectedRightStick[2] = { 0.0f, 0.0f };
    bool bHasInjectedRightStick = false;
    float injectedLeftStick[2] = { 0.0f, 0.0f };
    bool bHasInjectedLeftStick = false;
    bool bStartReleased = false;
    float steeringAxis;
    eDevice lastDevice = eDevice::D_Keyboard;
    int8_t padStyleOverride = -1; // setPadStyle, -1 = from the connected pad

private:
    void loadDefaultBindings();
    bool loadKeyMap( const char *pPath );
    void loadDefaultPadBindings();
    bool loadPadMap( const char *pPath );
    void updateGamepad();
};

extern GSInput *gpInput;

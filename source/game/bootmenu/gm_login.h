#pragma once

#include "audio/gs_audio.h"
#include "flash/flash_player.h"
#include "game/bootmenu/title_scene.h"
#include "game/gm_base.h"

// Game mode LOGIN (0x14, class GMLogin, docs/re/login_spec.md): the press
// start screen. 3D press start cinematic (TitleScene) under LOG_IN2.vmf
// (bands, logo, "Press START to play", NAT message); on press start the
// movie plays its "next" sequence, then the first-run "New profile" screen
// (MAIN.vmf /menus/profile/new_profile: Nickname, tdu2.com account name,
// Password, OK / Quit / Offline — the retail dialog) collects the profile
// name, then the savegame check shows the GENERAL "avert" popup (no
// progression data: OK / Retry); OK starts a new game (SCENARIOTUTORIAL = ON:
// AVATAR_CHOICE).
class GMLogin : public GameMode {
public:
    GMLogin();
    ~GMLogin();

    bool enter( TestDriveGameInstance *pInstance ) override;
    void tick( float deltaTime ) override;
    void drawOffscreen() override;
    void draw() override;
    void exit() override;
    void drawDebug() override;

private:
    // States of the original machine used on the offline / no save path.
    enum eState : int32_t {
        LS_PressStart = 1, // sub 0: loading screen, 1: 3000 ms, 2: press start, 3: wait "stok"
        LS_Profile = 2,    // the "New profile" screen (retail state 2 = the profile
                           // check; the GameSpy widget itself is not ported [ENC])
        LS_SaveCheck = 6,  // starts the save check (6 -> 7)
        LS_SaveResult = 7, // popup "no progression data" (OK / Retry)
        LS_NextMode = 21,  // next game mode decision
        LS_Leave = 23,     // mode switch request
    };

    // The three fields of the New profile dialog (the focus cycle order,
    // S250: /:nickname -> /:email -> /:password).
    enum eField : int32_t { Field_Nickname = 0, Field_Email = 1, Field_Password = 2, Field_Count = 3 };

    eState state = LS_PressStart;
    int32_t subState = 0;
    int32_t timerMs = 0; // +0x1adc9c: ms since the mode object was created
    bool bStok = false;  // +0x1adcad: "stok" received from LOG_IN2
    bool bPopupShown = false; // GENERAL /avert drawn (open, rest, close)
    eGameMode nextMode = eGameMode::GM_None;
    FlashPlayer login2;  // LOG_IN2.vmf
    FlashPlayer general; // GENERAL.vmf (avert popup)
    // MAIN.vmf: the "New profile" popup lives in the frontend menus tree
    // (/menus/profile/new_profile, sprite 257 + its popup sprite 250). The
    // rest of the tree stays hidden; the popup floats over the title scene.
    FlashPlayer main;
    bool bProfileUiReady = false;
    // The typed field buffers, the focused field and the online toggle
    // (the popup's gamespy_on / gamespy_off states; Offline commits the
    // nickname alone). bCommitRequested defers the popup teardown out of the
    // movie's own tick (the OK FSCommand arrives inside it).
    std::string fields[Field_Count];
    int32_t focusedField = Field_Nickname;
    bool bCommitRequested = false;
    TitleScene titleScene;
    std::string island = "ibiza";
    // FlashSounds::PlayFrontEndMusic "LOGIN" (FUN_00d85280): the loose wav
    // Euro/Bnk/sound/musics/login.wav on the FrontEndMusic client, looped.
    std::shared_ptr<AudioClip> musicClip;
    GSAudio::VoiceHandle musicVoice = GSAudio::kInvalidVoice;

private:
    void setState( eState newState );
    void loadTitleScene( int32_t cinematicIndex );
    void showNoDataPopup();
    bool isPopupClosing();
    // The New profile screen (the state LS_Profile helpers).
    void enterProfileScreen();
    void tickProfileScreen( float deltaTime );
    void setFocusedField( int32_t field, bool bNotifyMovie );
    void pushFieldVariables();
    void commitProfile();
    void handleProfileCommand( const std::string &command, const std::string &args );
};

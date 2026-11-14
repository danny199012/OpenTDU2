#pragma once

#include "audio/gs_audio.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

class FreeRideHud;

// The story phone calls (docs/re/phone_calls_spec.md). The calls, their text,
// caller, voice file, delay and repeat count are the retail DBPhoneCall table
// [DATA, 33 rows]; the voices are euro/bnk/sound/voices/<lang>/pc_*.wav [DATA].
// The retail trigger code is not read yet (MngPhone2 [CODE-dev]); the nudge
// rules below are inferred from the SCPhoneCalls save fields (BeenIn* flags)
// [INF].
class PhoneCalls {
public:
    struct Call {
        std::string Name;      // _phonecallname
        int32_t Npc = 0;       // _npc (DBPNJ hashcode)
        std::string SoundFile; // _soundfile (a pc_*.wav stem)
        int32_t TextHc = 0;    // _text_hc
        float Timer = 0.0f;    // _timer: seconds between the condition and the ring
        int32_t Repeats = 1;   // _nbrepeats (-1: unlimited)
        bool bYesNo = false;   // _yesnoanswer (the Yes/No answer is not implemented)
    };

    // What the game tells the manager each frame.
    struct Context {
        bool bFreeDriving = false; // in the free roam, in the car, no modal screen
        int32_t StoryStep = 0;
        bool bAccept = false;  // Validate pressed this frame (take the call)
        bool bDecline = false; // Back pressed this frame (decline the call)
    };

    // Reads DBPhoneCall. Returns the number of calls.
    int32_t load();
    inline int32_t getNumCalls() const { return static_cast<int32_t>( calls.size() ); }

    // Queues a call by name (the retail names, e.g. "CAR_WASH"). Honours the
    // repeat count saved in the profile unless bForce. False: unknown / spent.
    bool request( const std::string &name, bool bForce = false );
    inline bool isActive() const { return !activeName.empty(); }

    void tick( float deltaTime, const Context &context, FreeRideHud &hud );

private:
    const Call *find( const std::string &name ) const;
    void ring( const Call &call, FreeRideHud &hud );
    void hangUp( FreeRideHud &hud );
    void checkNudges( const Context &context );

    std::vector<Call> calls;
    std::deque<std::string> queue;
    std::string activeName;
    float activeTimer = 0.0f; // seconds until the call hangs up
    float idleTimer = 0.0f;   // the quiet gap after a call
    float nudgeClock = 0.0f;  // free driving time since the last nudge check
    GSAudio::VoiceHandle voice = GSAudio::kInvalidVoice;
    std::shared_ptr<AudioClip> clip;
    std::string portraitOwner; // TextureCache owner of the caller's portrait bank (released at hang-up)
    bool bAnswered = false;    // accepted / declined: the popup is closing
};

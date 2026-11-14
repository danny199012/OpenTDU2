#include "shared.h"
#include "phone_calls.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/freeride/freeride_hud.h"
#include "filesystem/bank.h"
#include "game/player_profile.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

namespace {
    // The voice client: the phone shares the sat-nav voice bus [INF: the
    // retail client of the phone voices is not read, 5 = "VoiceGPS"].
    constexpr int32_t kVoiceClient = 5;
    // Quiet gap between two calls, and the first nudge after the story reaches
    // free roam [INF].
    constexpr float kGapSeconds = 20.0f;
    constexpr float kNudgeCheckSeconds = 45.0f;
    // A call with no voice file (or a voice that failed to load) stays up this long.
    constexpr float kSilentSeconds = 7.0f;

    // The caller's portrait bank (frontend/allres/phonecalls/<name>.bnk, one
    // .2db) by DBPNJ hashcode [INF: read off the call texts - 0x187 says "it's
    // Todd", 0x194 "it's Tess", ...; 0x151 is the chase dispatcher: no
    // portrait, the movie's calque_chase badge is used].
    const char *PortraitBank( int32_t npc ) {
        switch ( npc ) {
        case 0x17e: return "spt_jane";
        case 0x17f: return "spt_jude";
        case 0x180: return "spt_marco";
        case 0x182: return "spt_mickey";
        case 0x187: return "spt_todd";
        case 0x18c: return "sto_ryan";
        case 0x18d: return "sto_chris";
        case 0x18f: return "sto_miami";
        case 0x190: return "sto_stuart";
        case 0x194: return "sto_tess";
        case 0x12a: return "ev_bwoman01";
        default: return nullptr;
        }
    }
} // namespace

int32_t PhoneCalls::load() {
    calls.clear();
    if ( gpDatabase == nullptr ) {
        return 0;
    }
    const XmbValue root = gpDatabase->getDocument().getRoot();
    const uint32_t rows = root.getArraySize( "DBPhoneCall" );
    for ( uint32_t i = 0; i < rows; i++ ) {
        const XmbValue row = root.getArrayElement( "DBPhoneCall", i );
        Call call;
        call.Name = row["_phonecallname"].asString();
        call.Npc = static_cast<int32_t>( row["_npc"].asInt() );
        call.SoundFile = row["_soundfile"].asString();
        call.TextHc = static_cast<int32_t>( row["_text_hc"].asInt() );
        call.Timer = static_cast<float>( row["_timer"].asDouble() );
        call.Repeats = static_cast<int32_t>( row["_nbrepeats"].asInt( 1 ) );
        call.bYesNo = row["_yesnoanswer"].asInt() != 0;
        if ( !call.Name.empty() ) {
            calls.push_back( std::move( call ) );
        }
    }
    OTDU_LOG_INFO( "PhoneCalls: %zu calls from DBPhoneCall\n", calls.size() );
    return static_cast<int32_t>( calls.size() );
}

const PhoneCalls::Call *PhoneCalls::find( const std::string &name ) const {
    for ( const Call &call : calls ) {
        if ( call.Name == name ) {
            return &call;
        }
    }
    return nullptr;
}

bool PhoneCalls::request( const std::string &name, bool bForce ) {
    const Call *pCall = find( name );
    if ( pCall == nullptr ) {
        OTDU_LOG_WARN( "PhoneCalls: unknown call '%s'\n", name.c_str() );
        return false;
    }
    if ( !bForce ) {
        const auto done = GetPlayerProfile().PhoneCallCounts.find( name );
        const int32_t times = done != GetPlayerProfile().PhoneCallCounts.end() ? done->second : 0;
        if ( pCall->Repeats >= 0 && times >= pCall->Repeats ) {
            return false;
        }
    }
    if ( name == activeName || std::find( queue.begin(), queue.end(), name ) != queue.end() ) {
        return false;
    }
    queue.push_back( name );
    return true;
}

void PhoneCalls::checkNudges( const Context &context ) {
    // The nudge rules, inferred from the SCPhoneCalls save fields [INF]: a
    // call that points the player at a shop rings while that shop's BeenIn*
    // flag is still false. Only the flags the port keeps are used (the car
    // wash); the others are added when their shop sets a flag. The story
    // gate: the first drive after the house is granted (StoryStep >= 10).
    if ( context.StoryStep < 10 ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    if ( !profile.BeenInCarWash ) {
        request( "CAR_WASH" );
    }
}

void PhoneCalls::ring( const Call &call, FreeRideHud &hud ) {
    activeName = call.Name;
    activeTimer = kSilentSeconds;
    GetPlayerProfile().PhoneCallCounts[call.Name]++;
    clip.reset();
    voice = GSAudio::kInvalidVoice;
    // pc_*.wav of the language, fallback "us".
    if ( gpAudio != nullptr && gpAudio->isReady() && gpFile != nullptr && !call.SoundFile.empty() ) {
        const std::string language = gpConfig != nullptr ? ToLower( gpConfig->getLanguage() ) : std::string( "us" );
        for ( const std::string &candidate : { language, std::string( "us" ) } ) {
            const std::string path =
                gpFile->getBankFolder() + "sound/voices/" + candidate + "/" + ToLower( call.SoundFile ) + ".wav";
            std::vector<uint8_t> data;
            if ( gpFile->fileExists( path.c_str() ) && gpFile->loadFile( path.c_str(), data ) && !data.empty() ) {
                clip = gpAudio->loadClip( data.data(), data.size(), call.SoundFile.c_str() );
                if ( clip != nullptr ) {
                    break;
                }
            }
        }
    }
    if ( clip != nullptr ) {
        GSAudio::VoiceParams params;
        params.Client = kVoiceClient;
        params.Volume = 1.0f;
        voice = gpAudio->play( clip, params );
        activeTimer = clip->Duration + 1.0f;
    }
    // The caller's portrait: the bank's 2db, bound into the popup's lure quad.
    GPUTexture *pPortrait = nullptr;
    const char *pBank = PortraitBank( call.Npc );
    if ( pBank != nullptr && gpRender != nullptr && gpFile != nullptr ) {
        const std::string path = gpFile->getBankFolder() + "frontend/allres/phonecalls/" + pBank + ".bnk";
        Bank bank;
        if ( gpFile->fileExists( path.c_str() ) && bank.load( path.c_str() ) ) {
            portraitOwner = std::string( "phonecall_" ) + pBank;
            gpRender->getTextureCache().loadBankTextures( bank, portraitOwner.c_str() );
            pPortrait = gpRender->getTextureCache().find( portraitOwner.c_str(), pBank );
            if ( pPortrait == nullptr ) {
                OTDU_LOG_WARN( "PhoneCalls: portrait texture '%s' not found in its bank\n", pBank );
            }
        }
    }
    bAnswered = false;
    const std::string text = LocStr( call.TextHc, call.Name.c_str() );
    hud.showPhoneCall( text, pPortrait, call.Npc == 0x151 );
    OTDU_LOG_INFO( "PhoneCalls: ring '%s' (npc 0x%x, %s, %.1f s): %s\n", call.Name.c_str(), call.Npc, call.SoundFile.c_str(),
                   activeTimer, text.c_str() );
}

void PhoneCalls::hangUp( FreeRideHud &hud ) {
    hud.closePhoneCall();
    OTDU_LOG_INFO( "PhoneCalls: '%s' hung up\n", activeName.c_str() );
    activeName.clear();
    clip.reset();
    voice = GSAudio::kInvalidVoice;
    if ( !portraitOwner.empty() && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( portraitOwner.c_str() );
        portraitOwner.clear();
    }
    idleTimer = kGapSeconds;
}

void PhoneCalls::tick( float deltaTime, const Context &context, FreeRideHud &hud ) {
    if ( !activeName.empty() ) {
        activeTimer -= deltaTime;
        // Validate takes the call (the movie's `accept` label), Back declines it
        // (`decline`); either ends the voice and the popup closes shortly after
        // [INF: the retail answer buttons are not read].
        if ( !bAnswered && ( context.bAccept || context.bDecline ) ) {
            bAnswered = true;
            OTDU_LOG_INFO( "PhoneCalls: '%s' %s\n", activeName.c_str(), context.bAccept ? "accepted" : "declined" );
            if ( voice != GSAudio::kInvalidVoice && gpAudio != nullptr ) {
                gpAudio->stop( voice );
            }
            hud.answerPhoneCall( context.bAccept );
            activeTimer = Min( activeTimer, 2.0f );
        }
        if ( activeTimer <= 0.0f ) {
            hangUp( hud );
        }
        return;
    }
    if ( !context.bFreeDriving ) {
        return;
    }
    idleTimer = Max( 0.0f, idleTimer - deltaTime );
    nudgeClock += deltaTime;
    if ( nudgeClock >= kNudgeCheckSeconds ) {
        nudgeClock = 0.0f;
        checkNudges( context );
    }
    if ( idleTimer > 0.0f || queue.empty() ) {
        return;
    }
    const std::string name = queue.front();
    queue.pop_front();
    if ( const Call *pCall = find( name ) ) {
        ring( *pCall, hud );
    }
}

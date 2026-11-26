#include "shared.h"
#include "phone_calls.h"

#include "config/gs_config.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/freeride/freeride_hud.h"
#include "filesystem/bank.h"
#include "game/player_profile.h"
#include "game/licenses.h"
#include "render/gs_render.h"
#include "render/texture_cache.h"

namespace {
    // The voice client: the phone shares the sat-nav voice bus [INF: the
    // retail client of the phone voices is not read, 5 = "VoiceGPS"].
    constexpr int32_t kVoiceClient = 5;
    // Quiet gap between two calls, and the first nudge after the story reaches
    // free roam [INF].
    constexpr float kGapSeconds = 30.0f; // [CODE-dev SCPhoneCalls 0x9a32d0: +0x18 = 30.0 at each launch]
    constexpr float kNudgeCheckSeconds = 5.0f; // the retail checks every frame once the gap is over
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
        call.bYesNo = row["_yesnoanswer"].asInt() != 0 || call.Name.compare( 0, 12, "CHASE_INTRO_" ) == 0; // the text asks "Do you want to chase him?" [INF]
        if ( !call.Name.empty() ) {
            calls.push_back( std::move( call ) );
        }
    }
    // Every caller's portrait, once, here: load() runs in the game load, where
    // bank textures are created like the rest of the level's [a texture created
    // when the call rings, mid-frame, crashed the NVIDIA driver (nvoglv64.dll)
    // in two of three runs, and one released at hang-up left the movie's bitmap
    // override pointing at a destroyed texture].
    portraits.clear();
    if ( gpRender != nullptr && gpFile != nullptr ) {
        for ( const Call &call : calls ) {
            const char *pBank = PortraitBank( call.Npc );
            if ( pBank == nullptr || portraits.count( pBank ) != 0 ) {
                continue;
            }
            GPUTexture *pTexture = nullptr;
            const std::string path = gpFile->getBankFolder() + "frontend/allres/phonecalls/" + pBank + ".bnk";
            Bank bank;
            if ( gpFile->fileExists( path.c_str() ) && bank.load( path.c_str() ) ) {
                const std::string owner = std::string( "phonecall_" ) + pBank;
                gpRender->getTextureCache().loadBankTextures( bank, owner.c_str() );
                pTexture = gpRender->getTextureCache().find( owner.c_str(), pBank );
            }
            portraits[pBank] = pTexture;
        }
    }
    OTDU_LOG_INFO( "PhoneCalls: %zu calls from DBPhoneCall, %zu portraits\n", calls.size(), portraits.size() );
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
        const auto wait = retrigger.find( name );
        if ( wait != retrigger.end() && wait->second > 0.0f ) {
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
    // The retail trigger order [CODE-dev SCPhoneCalls 0x9a2c30, docs/re/phone_calls_spec.md]:
    // the first rule that holds rings, one call per pass. Rules whose state the
    // port does not keep (the C4 / Jane flags, the car categories, the photo
    // challenge, HAWAI_CALL's progress counter, the championship steps) are not
    // implemented. The minutes of the TimeBefore*Call settings are unknown
    // [INF values below].
    if ( context.StoryStep < 10 ) {
        return;
    }
    PlayerProfile &profile = GetPlayerProfile();
    auto state = [&]( const char *pName ) {
        const auto found = profile.PhoneState.find( pName );
        return found != profile.PhoneState.end() ? found->second : 0;
    };
    const int32_t playTime = state( "PlayTime" );
    auto after = [&]( float minutes ) { return static_cast<float>( playTime ) > minutes * 60.0f; };
    // 1. CALL_C4_NOT_PASSED: the Classic school's first licence was started and not finished [INF: the
    // retail reads the classic test-state object +0x28 and a global flag, 0x9a2c30].
    for ( const LicenseInfo *pLicense : GetSchoolLicenses( 'C', 0 ) ) {
        bool bStarted = false;
        for ( const std::string &test : pLicense->Challenges ) {
            bStarted = bStarted || IsTestPassed( test );
        }
        if ( bStarted && !IsLicenseObtained( *pLicense ) && request( "CALL_C4_NOT_PASSED" ) ) {
            return;
        }
        break; // the first licence of the school only
    }
    // 2. EVENT_JANE: Todd's favour call ("a friend of mine needs some help. If you could catch up to her, she'll explain
    // everything"): the retail rule is 'the same flag and two state objects set and a third == 1' [INF meaning, 0x9a2c30];
    // the port rings it once, 2 minutes into the free drive, until the Jane event (EVENJANE) is done [INF].
    if ( state( "JaneDone" ) == 0 && after( 2.0f ) && request( "EVENT_JANE" ) ) {
        return;
    }
    // Car categories owned (DBCarPhysicsData._vehiclecategory: 1..7 = Cat A, 9..12 = Cat B).
    auto ownsCategory = [&]( int32_t low, int32_t high ) {
        std::vector<int32_t> ids = profile.OwnedCars;
        for ( const OwnedCar &car : profile.Garage ) {
            ids.push_back( car.IdCar );
        }
        const XmbValue root = gpDatabase->getDocument().getRoot();
        const uint32_t rows = root.getArraySize( "DBCarPhysicsData" );
        for ( int32_t id : ids ) {
            for ( uint32_t i = 0; i < rows; i++ ) {
                const XmbValue car = root.getArrayElement( "DBCarPhysicsData", i );
                if ( static_cast<int32_t>( car["_id_car"].asInt() ) == id ) {
                    const int32_t category = static_cast<int32_t>( car["_vehiclecategory"].asInt() );
                    if ( category >= low && category <= high ) {
                        return true;
                    }
                    break;
                }
            }
        }
        return false;
    };
    // RIVAL_<class>: the rival's duel challenge, once a championship of the class is complete and the duel is not won
    // yet (the caller stakes their car) [INF rule, the same as the duel spot's].
    for ( const std::string &duelClass : context.OpenDuels ) {
        if ( request( ( "RIVAL_" + duelClass ).c_str() ) ) {
            return;
        }
    }
    // CHASE_INTRO_<ISLAND>: the dispatcher's offer to chase a reckless driver, once, 12 minutes into the free drive [INF].
    if ( state( "ChaseOffered" ) == 0 && after( 12.0f ) ) {
        profile.PhoneState["ChaseOffered"] = 1;
        if ( request( context.Island == "hawai" ? "CHASE_INTRO_HAWAI" : "CHASE_INTRO_IBIZA" ) ) {
            return;
        }
    }
    // HAWAI_CALL: Tess's "We're leaving today! Meet us at the airport!" - not online, not tutorial, a progress counter >= 10
    // and Hawaii not visited [CODE-dev 0x9a2c30; the counter and the delay are INF: 20 minutes of play, on Ibiza].
    if ( context.Island == "ibiza" && !profile.PhoneState.count( "PlayTimeWhenArrivingOnHawai" ) && after( 20.0f ) &&
         request( "HAWAI_CALL" ) ) {
        return;
    }
    // 3. REALTOR_CALL: one house and two or more cars (the garage is cramped).
    const size_t cars = std::max( profile.Garage.size(), profile.OwnedCars.size() + 1 );
    if ( profile.OwnedHouses.size() == 1 && cars >= 2 && request( "REALTOR_CALL" ) ) {
        return;
    }
    // 6. The shop nudges: play time past the delay and the shop not visited yet.
    if ( after( 10.0f ) && state( "BeenInClothesShop" ) == 0 && request( "CLOTHES_CALL" ) ) {
        return;
    }
    if ( after( 30.0f ) && state( "BeenInHairShop" ) == 0 && request( "HAIR_DRESSER" ) ) {
        return;
    }
    if ( after( 45.0f ) && state( "BeenInSurgeryShop" ) == 0 && request( "SURGERY_IBIZA" ) ) {
        return;
    }
    if ( after( 20.0f ) && state( "BeenInStickerShop" ) == 0 && request( "STICKERS" ) ) {
        return;
    }
    if ( after( 6.0f ) && !profile.BeenInCarWash && request( "CAR_WASH" ) ) {
        return;
    }
    // 7. TREASURE_HUNT: no wreck found yet.
    int32_t wrecks = 0;
    for ( const auto &island : profile.FoundWrecks ) {
        wrecks += static_cast<int32_t>( island.second.size() );
    }
    if ( wrecks == 0 && after( 25.0f ) && request( "TREASURE_HUNT" ) ) {
        return;
    }
    // 8. PHOTOGRAH: on Ibiza while no photo has been taken [CODE-dev; the delay is INF].
    if ( context.Island == "ibiza" && state( "BeenInPhotograph" ) == 0 && after( 20.0f ) && request( "PHOTOGRAH" ) ) {
        return;
    }
    // 5. RS_ASPHALT: the first Asphalt licence is obtained and the next not started [INF: the retail reads
    // the ERS_A / ERS_B championship states in the refresh 0x9a33d0].
    {
        const std::vector<const LicenseInfo *> asphalt = GetSchoolLicenses( 'A', 0 );
        if ( asphalt.size() >= 2 && IsLicenseObtained( *asphalt[0] ) && !IsLicenseObtained( *asphalt[1] ) ) {
            bool bStarted = false;
            for ( const std::string &test : asphalt[1]->Challenges ) {
                bStarted = bStarted || IsTestPassed( test );
            }
            if ( !bStarted && request( "RS_ASPHALT" ) ) {
                return;
            }
        }
    }
    // 10. TUNER_CALL: a one-shot [INF: the retail flag +0x44 is set by the refresh; here, a car owned and
    // nothing ever bought at a tuning shop after 15 minutes].
    if ( after( 15.0f ) && cars >= 2 && profile.OwnedCarTuning.empty() && request( "TUNER_CALL" ) ) {
        return;
    }
    // 12. Championship nudges [CODE-dev 0x9a2c30]: a family not finished -> the new-car call while the
    // player has no car of its category, else the "do not drop out" call. [INF: 15 minutes of play first.]
    if ( after( 15.0f ) ) {
        auto familyDone = [&]( const char *pFamily ) {
            bool bAny = false;
            for ( int32_t island = 0; island < 2; island++ ) {
                for ( const LicenseInfo *pChamp : GetChampionships( island ) ) {
                    if ( ChampionshipFamily( *pChamp ).compare( 0, strlen( pFamily ), pFamily ) == 0 ) {
                        bAny = true;
                        if ( !IsChampionshipComplete( *pChamp ) ) {
                            return false;
                        }
                    }
                }
            }
            return bAny;
        };
        if ( !familyDone( "asphalt" ) && request( ownsCategory( 1, 7 ) ? "ASPHALT_CHAMP" : "NEW_ASPHALT_CAR" ) ) {
            return;
        }
        if ( !familyDone( "dirt" ) && request( ownsCategory( 9, 12 ) ? "DIRT_CHAMP" : "NEW_SUV_CAR" ) ) {
            return;
        }
        if ( !familyDone( "classic" ) && request( "CLASSIC_CHAMP" ) ) {
            return;
        }
    }
    // 11. On Hawaii: the racing schools, some minutes after the arrival.
    if ( context.Island == "hawai" ) {
        const int32_t sinceArrival = playTime - state( "PlayTimeWhenArrivingOnHawai" );
        if ( state( "BeenInRSDirtHawai" ) == 0 && sinceArrival > 3 * 60 && request( "HAWAI_DIRT" ) ) {
            return;
        }
        if ( state( "BeenInRSClassicHawai" ) == 0 && sinceArrival > 8 * 60 && request( "HAWAI_CLASSIC" ) ) {
            return;
        }
        // SURGERY_HAWAI: Miami's tip about the Hawaii cosmetics clinic, once the clinic was not visited there [INF: 5 minutes].
        if ( state( "BeenInSurgeryHawai" ) == 0 && sinceArrival > 5 * 60 && request( "SURGERY_HAWAI" ) ) {
            return;
        }
    }
}

void PhoneCalls::ring( const Call &call, FreeRideHud &hud ) {
    activeName = call.Name;
    activeTimer = kSilentSeconds;
    GetPlayerProfile().PhoneCallCounts[call.Name]++;
    retrigger[call.Name] = call.Timer * 60.0f;
    idleTimer = kGapSeconds; // counts from the launch, like the retail's +0x18
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
    // The caller's portrait (preloaded in load()), bound into the popup's lure quad.
    GPUTexture *pPortrait = nullptr;
    if ( const char *pBank = PortraitBank( call.Npc ) ) {
        const auto found = portraits.find( pBank );
        pPortrait = found != portraits.end() ? found->second : nullptr;
    }
    bAnswered = false;
    const std::string text = LocStr( call.TextHc, call.Name.c_str() );
    std::string offerIcon, offerPrefix;
    // The caller's name for the portrait header (/:dialog_pnj) [DATA: loc 7697.. 'Tess Wintory'.. 'Jane O'Mahan';
    // the npc -> id pairing follows the portrait banks, [INF]].
    int32_t nameId = 0;
    switch ( call.Npc ) {
    case 0x194: nameId = 7697; break; // Tess
    case 0x190: nameId = 7698; break; // Stuart
    case 0x187: nameId = 7699; break; // Todd
    case 0x17f: nameId = 7700; break; // Jude
    case 0x18f: nameId = 7703; break; // Miami
    case 0x18d: nameId = 7704; break; // Chris
    case 0x18c: nameId = 7705; break; // Ryan
    case 0x180: nameId = 7706; break; // Marco
    case 0x17e: nameId = 7707; break; // Jane
    default: break;
    }
    const std::string callerName = nameId != 0 ? LocStr( nameId, "" ) : std::string();
    hud.showPhoneCall( text, pPortrait, call.Npc == 0x151, TargetSpot( call.Name, offerIcon, offerPrefix ), callerName );
    OTDU_LOG_INFO( "PhoneCalls: ring '%s' (npc 0x%x, %s, %.1f s): %s\n", call.Name.c_str(), call.Npc, call.SoundFile.c_str(),
                   activeTimer, text.c_str() );
}

void PhoneCalls::hangUp( FreeRideHud &hud ) {
    hud.closePhoneCall();
    OTDU_LOG_INFO( "PhoneCalls: '%s' hung up\n", activeName.c_str() );
    activeName.clear();
    clip.reset();
    voice = GSAudio::kInvalidVoice;
}

void PhoneCalls::tick( float deltaTime, const Context &context, FreeRideHud &hud ) {
    if ( context.bFreeDriving ) {
        // The play time the nudges read (profile.PhoneState "PlayTime", seconds).
        clockFraction += deltaTime;
        if ( clockFraction >= 1.0f ) {
            const int32_t whole = static_cast<int32_t>( clockFraction );
            clockFraction -= static_cast<float>( whole );
            GetPlayerProfile().PhoneState["PlayTime"] += whole;
        }
        if ( context.Island == "hawai" && GetPlayerProfile().PhoneState.count( "PlayTimeWhenArrivingOnHawai" ) == 0 ) {
            GetPlayerProfile().PhoneState["PlayTimeWhenArrivingOnHawai"] = GetPlayerProfile().PhoneState["PlayTime"];
        }
    }
    for ( auto &wait : retrigger ) {
        wait.second = Max( 0.0f, wait.second - deltaTime );
    }
    idleTimer = Max( 0.0f, idleTimer - deltaTime );
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
            if ( context.bAccept ) {
                acceptedName = activeName;
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

std::string PhoneCalls::takeAccepted() {
    std::string name;
    name.swap( acceptedName );
    return name;
}

bool PhoneCalls::TargetSpot( const std::string &callName, std::string &outIcon, std::string &outPrefix ) {
    struct Entry {
        const char *Call, *Icon, *Prefix;
    };
    static const Entry kTable[] = {
        { "CLOTHES_CALL", "clothes", "" },
        { "TUNER_CALL", "tuning", "" },
        { "REALTOR_CALL", "estates", "ERE_" }, // the real estate agent, not a house
        { "CAR_WASH", "carwash", "" },
        { "STICKERS", "carpaint", "" },
        { "HAIR_DRESSER", "hair", "" },
        { "SURGERY_IBIZA", "surgery", "" },
        { "SURGERY_HAWAI", "surgery", "" },
        { "PHOTOGRAH", "photograph", "" },
        { "NEW_ASPHALT_CAR", "cardealer", "" },
        { "NEW_SUV_CAR", "cardealer", "" },
        { "RS_ASPHALT", "rschool", "" },
        { "CALL_C4_NOT_PASSED", "rschool", "" },
        { "HAWAI_DIRT", "rschool", "" },
        { "HAWAI_CLASSIC", "rschool", "" },
        { "HAWAI_CALL", "airport", "" },
        { "WRECKS_CALL", "cardealer", "" }, // Jude, the used car dealer [INF: the car "waits at the warehouse"]
    };
    for ( const Entry &entry : kTable ) {
        if ( callName == entry.Call ) {
            outIcon = entry.Icon;
            outPrefix = entry.Prefix;
            return true;
        }
    }
    return false;
}

#include "shared.h"
#include "surgery_ui.h"

#include "core/xmb.h"
#include "database/gs_database.h"
#include "flash/flash_player.h"
#include "game/avatar/facefx.h"
#include "game/player_profile.h"
#include "game/skill_levels.h"
#include "system/cmd_line.h"

#include <cstdlib>
#include <ctime>

namespace {
// The movie's slider roots -> the FaceFX custom node they edit [DATA: the root
// names are shopsavatar.vmf's setParamMenu constant pool; the node pairing is
// by name and is the port's reading, INF]. Kind 0 = a FaceParamArray slot, 1 =
// the age float (CustPos Older), 2 = the freckle float.
struct Slider {
    const char *Var;
    const char *Node; // kFaceFxCustomNodes entry, or the DBSurgeryCost name of a float
    int32_t Kind;
};
const Slider kSliders[] = {
    { "HeadDepth", "C_Head_depth", 0 },
    { "HeadHeight", "C_Head_height", 0 },
    { "HeadWidth", "C_Head_width", 0 },
    { "BrowPushed", "C_Brow_push", 0 },
    { "CheekBonesSize", "C_Cheek_size", 0 },
    { "CheekBonesHeight", "C_Cheek_height", 0 },
    { "FatOrThin", "C_Fat_or_Thin", 0 },
    { "LowerFaceHeight", "C_LowerFace", 0 },
    { "Throat", "C_Throat", 0 },
    { "JawThin", "CustPos Jaw thin", 0 },
    { "JawSize", "C_Jaw_size", 0 },
    { "ChinSize", "C_Chin_size", 0 },
    { "ChinOrientation", "C_Chin_orient", 0 },
    { "ChinUpandDown", "C_Chin_up_and_down", 0 },
    { "ChinSlimorWide", "C_Chin_Slim_or_Wide", 0 },
    { "EyesOpening", "C_Eyes_opening", 0 },
    { "EyesType", "C_Eyes_type", 0 },
    { "EyesRightandLeft", "C_Eyes_right_and_left", 0 },
    { "EyesUpandDown", "C_Eyes_up_and_down", 0 },
    { "EyesWidth", "C_Eyes_width", 0 },
    { "EyebrowsInside", "C_Eyebrow_inside", 0 },
    { "EyebrowsOutside", "C_Eyebrow_outside", 0 },
    { "EyebrowsPushed", "C_Eyebrow_pushed", 0 },
    { "EyebrowsRotate", "C_EyeBrow_rotate", 0 },
    { "EyebrowsSize", "C_Eyebrow_size", 0 },
    { "EarOrientation", "C_Ear_orientation", 0 },
    { "EarSize", "C_Ear_size", 0 },
    { "NoseSize", "C_Nose_size", 0 },
    { "NoseShape", "C_Nose_shape", 0 },
    { "NoseTallorSmall", "C_Nose_Tall_or_Small", 0 },
    { "NosePushed", "CustPos Nose pushed", 0 },
    { "NoseThickandThin", "C_Nose_thick_and_thin", 0 },
    { "NoseUpandDown", "C_Nose_up_and_down", 0 },
    { "MouthPosition", "C_Mouth_position", 0 },
    { "MouthSize", "C_Mouth_size", 0 },
    { "LipsSize", "C_Lips_size", 0 },
    { "LipsDowninandOut", "C_Lips_down_in_and_out", 0 },
    { "LipsUpinandOut", "C_Lips_up_in_and_out", 0 },
    { "Age", "CustPos Older", 1 },
    { "Freckle", "FRECKLES", 2 },
};

// The movie's slider runs 0.._root.NbNotches: initParam calls initSlider(cmd, value, _root.NbNotches) [DATA].
// The notch count is the game's; 20 notches (10 of the -100..100 register each) is the port's choice [INF].
constexpr int32_t kSliderMax = 20;

const Slider *FindSlider( const std::string &var ) {
    // /:VarToFocus may carry the "_root." prefix.
    const size_t dot = var.rfind( '.' );
    const std::string name = dot == std::string::npos ? var : var.substr( dot + 1 );
    for ( const Slider &slider : kSliders ) {
        if ( StringCmpI( slider.Var, name.c_str() ) == 0 ) {
            return &slider;
        }
    }
    return nullptr;
}

int32_t NodeIndex( const char *pNode ) {
    for ( int32_t n = 0; n < 49; n++ ) {
        if ( StringCmpI( kFaceFxCustomNodes[n], pNode ) == 0 ) {
            return n;
        }
    }
    return -1;
}

int32_t CostOf( const char *pName ) {
    XmbValue root = gpDatabase->getDocument().getRoot();
    for ( uint32_t i = 0; i < root.getArraySize( "DBSurgeryCost" ); i++ ) {
        XmbValue entry = root.getArrayElement( "DBSurgeryCost", i );
        if ( StringCmpI( entry["_surgname"].asString().c_str(), pName ) == 0 ) {
            return static_cast<int32_t>( entry["_cost"].asUInt64() );
        }
    }
    return 0;
}

std::string FormatMoney( int32_t value ) {
    // "$ 999 999 999": the movie's own grouping with spaces [DATA: the text field's sample].
    std::string digits = std::to_string( value < 0 ? -value : value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ' ';
        }
        out += digits[i];
    }
    return "$ " + out;
}
} // namespace

int32_t SurgeryUi::sliderOf( int32_t byteValue ) const {
    return Clamp( ( byteValue + 100 ) * kSliderMax / 200, 0, kSliderMax );
}

int32_t SurgeryUi::byteOf( int32_t slider ) const {
    return Clamp( slider * 200 / kSliderMax - 100, -100, 100 );
}

void SurgeryUi::bind( FlashPlayer *pNewUi, const int8_t *pBaselineParams, float baselineAge, float baselineFreckle ) {
    pUi = pNewUi;
    memcpy( baseParams, pBaselineParams, sizeof( baseParams ) );
    memcpy( params, pBaselineParams, sizeof( params ) );
    baseAge = age = baselineAge;
    baseFreckle = freckle = baselineFreckle;
}

int32_t SurgeryUi::totalPrice( int32_t *pChanged ) const {
    int32_t total = 0, changed = 0;
    for ( const Slider &slider : kSliders ) {
        bool bChanged = false;
        if ( slider.Kind == 0 ) {
            const int32_t index = NodeIndex( slider.Node );
            bChanged = index >= 0 && params[index] != baseParams[index];
        } else if ( slider.Kind == 1 ) {
            bChanged = static_cast<int32_t>( age * 100.0f + 0.5f ) != static_cast<int32_t>( baseAge * 100.0f + 0.5f );
        } else {
            bChanged = static_cast<int32_t>( freckle * 100.0f + 0.5f ) != static_cast<int32_t>( baseFreckle * 100.0f + 0.5f );
        }
        if ( bChanged ) {
            total += CostOf( slider.Node );
            changed++;
        }
    }
    if ( pChanged != nullptr ) {
        *pChanged = changed;
    }
    return total;
}

void SurgeryUi::pushValues() {
    pUi->setVariableNumber( "/:NbNotches", kSliderMax );
    for ( const Slider &slider : kSliders ) {
        int32_t value = 0;
        if ( slider.Kind == 0 ) {
            const int32_t index = NodeIndex( slider.Node );
            value = sliderOf( index >= 0 ? params[index] : 0 );
        } else {
            value = Clamp( static_cast<int32_t>( ( slider.Kind == 1 ? age : freckle ) * kSliderMax + 0.5f ), 0, kSliderMax );
        }
        pUi->setVariableNumber( std::string( "/:" ) + slider.Var, value );
    }
}

void SurgeryUi::pushPrice() {
    int32_t changed = 0;
    const int32_t total = totalPrice( &changed );
    const PlayerProfile &profile = GetPlayerProfile();
    pUi->setVariable( "/:TotalPriceStr", FormatMoney( total ) );
    pUi->setVariable( "/:MyMoneyStr", FormatMoney( profile.Money ) );
    pUi->setVariableBool( "/:EnoughMoney", profile.Money >= total );
}

void SurgeryUi::pay() {
    int32_t changed = 0;
    const int32_t price = totalPrice( &changed );
    PlayerProfile &profile = GetPlayerProfile();
    if ( changed == 0 || profile.Money < price ) {
        return;
    }
    profile.Money -= price;
    profile.FaceParams.assign( params, params + 49 );
    profile.FaceAge = age;
    profile.FaceFreckle = freckle;
    profile.NbSurgeries++;
    AwardSkillEvent( "COLLECTION_SURGERY" ); // 25 points, 4 times [DATA; INF: once per operation]
    profile.BandageUntil = static_cast<int64_t>( time( nullptr ) ) + 600; // [INF] the retail rule is ENC
    memcpy( baseParams, params, sizeof( baseParams ) );
    baseAge = age;
    baseFreckle = freckle;
    OTDU_LOG_INFO( "SurgeryUi: paid $%d for %d change(s) (money now $%d, %d surgeries)\n", price, changed, profile.Money,
                   profile.NbSurgeries );
    SaveProfile( "surgery bought" );
}

bool SurgeryUi::onCommand( const std::string &command ) {
    if ( command.compare( 0, 5, "surg:" ) != 0 || pUi == nullptr ) {
        return false;
    }
    const std::string rest = command.substr( 5 );
    if ( rest == "edit:open" ) {
        pushValues();
        pushPrice();
    } else if ( rest.size() > 5 && rest.compare( rest.size() - 5, 5, ":open" ) == 0 ) {
        // A part page opened (surg:edit:eyes:open ...): the sliders read their
        // `_root.<Name>` when the page's clips initialise, right after this command.
        pushValues();
        pushPrice();
        if ( CmdLine::HasArg( "-logsurgery" ) ) {
            OTDU_LOG_INFO( "SurgeryUi: %s: EyesOpening '%s'\n", rest.c_str(), pUi->getVariable( "/:EyesOpening" ).c_str() );
        }
    } else if ( rest == "edit:chpa" ) {
        OTDU_LOG_INFO( "SurgeryUi: focus %s\n", pUi->getVariable( "/:VarToFocus" ).c_str() );
    } else if ( rest.size() > 10 && rest.compare( rest.size() - 10, 10, ":slid:updt" ) == 0 ) {
        const std::string var = pUi->getVariable( "/:VarToFocus" );
        const Slider *pSlider = FindSlider( var );
        if ( pSlider != nullptr ) {
            const int32_t value = static_cast<int32_t>( atof( pUi->getVariable( std::string( "/:" ) + pSlider->Var ).c_str() ) );
            if ( pSlider->Kind == 0 ) {
                const int32_t index = NodeIndex( pSlider->Node );
                if ( index >= 0 ) {
                    params[index] = static_cast<int8_t>( byteOf( value ) );
                }
            } else if ( pSlider->Kind == 1 ) {
                age = Clamp( value, 0, kSliderMax ) / static_cast<float>( kSliderMax );
            } else {
                freckle = Clamp( value, 0, kSliderMax ) / static_cast<float>( kSliderMax );
            }
            OTDU_LOG_INFO( "SurgeryUi: %s = %d\n", pSlider->Var, value );
        } else {
            OTDU_LOG_INFO( "SurgeryUi: slider update for unknown var '%s' (value '%s')\n", var.c_str(),
                           pUi->getVariable( "/:" + var ).c_str() );
        }
        pushPrice();
    } else if ( rest == "edit:vali" ) {
        pay();
        pushPrice();
    } else if ( rest == "edit:canc" ) {
        memcpy( params, baseParams, sizeof( params ) );
        age = baseAge;
        freckle = baseFreckle;
        pushPrice();
    } else if ( rest == "edit:rand" ) {
        // Random: every FaceFX slider drawn within -60..60 [INF: the retail range is ENC].
        for ( const Slider &slider : kSliders ) {
            if ( slider.Kind == 0 ) {
                const int32_t index = NodeIndex( slider.Node );
                if ( index >= 0 ) {
                    params[index] = static_cast<int8_t>( rand() % 121 - 60 );
                }
            }
        }
        pushValues();
        pushPrice();
    }
    return true;
}

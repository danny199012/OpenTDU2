#include "shared.h"
#include "car_dirt.h"

#include "system/cmd_line.h"

namespace {
// The port's laws [INF]: the retail accumulation / wash rates are inside the
// encrypted range and the save only carries the resulting per-car bytes.
constexpr float kMudRate = 0.13f;      // mud thrown on the body, 1/s at full mud contact and 60 km/h
constexpr float kDirtRate = 0.07f;     // dry dirt dusting, 1/s at full dirt contact and 60 km/h
constexpr float kRainSoak = 0.35f;     // water film, 1/s at full rain
constexpr float kRainWash = 0.055f;    // mud washed off, 1/s at full rain on runny mud
constexpr float kSoakSoftening = 0.05f;// the baked crust softens under the rain, 1/s at full rain
constexpr float kSunDry = 0.040f;      // mud sets into a crust, 1/s at full sun
constexpr float kEvaporate = 0.10f;    // water film, 1/s (scaled by the sun)
constexpr float kWindDust = 0.003f;    // dust blown off at speed
} // namespace

void CarDirt::update( float deltaTime, float mudContacts, float dirtContacts, float speedKmh, float rainIntensity,
                      float sunEnergy ) {
    if ( deltaTime <= 0.0f ) {
        return;
    }
    const float speed = Clamp( speedKmh / 60.0f, 0.0f, 1.5f );
    // Below walking pace the tread keeps what it picked up.
    const float throwRate = speedKmh > 5.0f ? speed : 0.0f;
    if ( mudContacts > 0.0f && throwRate > 0.0f ) {
        const float rate = kMudRate * Clamp( mudContacts, 0.0f, 1.0f ) * throwRate;
        mud = Min( 1.0f, mud + rate * deltaTime );
        dirt = Min( 1.0f, dirt + rate * 0.45f * deltaTime );
        wetness = Min( 1.0f, wetness + rate * 1.6f * deltaTime );
        // Fresh, wet mud is runny: it replaces whatever crust was there.
        hardness = Max( 0.0f, hardness - rate * 0.9f * deltaTime );
    }
    if ( dirtContacts > 0.0f && throwRate > 0.0f ) {
        dirt = Min( 1.0f, dirt + kDirtRate * Clamp( dirtContacts, 0.0f, 1.0f ) * throwRate * deltaTime );
    }
    // Rain soaks the body, washes the runny mud and slowly softens a crust.
    // A baked crust (hardness -> 1) cuts the wash rate to ~15%: the car stays
    // dirty until the wash (the owner's request).
    if ( rainIntensity > 0.001f ) {
        const float rain = Clamp( rainIntensity, 0.0f, 1.0f );
        wetness = Min( 1.0f, wetness + kRainSoak * rain * deltaTime );
        const float wash = kRainWash * rain * ( 1.0f - 0.85f * hardness ) * deltaTime;
        mud = Max( 0.0f, mud - wash * ( 1.0f + mud ) );
        dirt = Max( 0.0f, dirt - wash * 1.6f );
        hardness = Max( 0.0f, hardness - kSoakSoftening * rain * deltaTime );
        if ( mud <= 0.02f ) {
            mud = 0.0f;
            hardness = 0.0f;
        }
    } else {
        wetness = Max( 0.0f, wetness - kEvaporate * ( 0.25f + Max( sunEnergy, 0.0f ) ) * deltaTime );
    }
    // The sun dries the water off and bakes the mud into a crust. The drier
    // the body, the faster it sets - a wet car stays runny in the sun.
    if ( sunEnergy > 0.02f && wetness < 0.6f && mud > 0.01f ) {
        const float dry = kSunDry * Clamp( sunEnergy, 0.0f, 1.5f ) * ( 1.0f - wetness ) * deltaTime;
        hardness = Min( 1.0f, hardness + dry );
    }
    // A dry film dusts off in the airflow; a crust and wet mud hold on.
    if ( rainIntensity <= 0.001f && dirt > 0.0f ) {
        dirt = Max( 0.0f, dirt - kWindDust * speed * ( 1.0f - hardness ) * deltaTime );
    }
    mud = Clamp( mud, 0.0f, 1.0f );
    dirt = Clamp( dirt, 0.0f, 1.0f );
    hardness = Clamp( hardness, 0.0f, 1.0f );
    wetness = Clamp( wetness, 0.0f, 1.0f );
}

void CarDirt::wash() {
    mud = 0.0f;
    dirt = 0.0f;
    hardness = 0.0f;
    wetness = 1.0f; // it leaves the wash dripping wet
}

void CarDirt::seed( float mudValue, float dirtValue, float hardnessValue, float wetnessValue ) {
    mud = Clamp( mudValue, 0.0f, 1.0f );
    dirt = Clamp( dirtValue, 0.0f, 1.0f );
    hardness = Clamp( hardnessValue, 0.0f, 1.0f );
    wetness = Clamp( wetnessValue, 0.0f, 1.0f );
}

void CarDirt::log( float deltaTime, bool bForce ) {
    logTimer += deltaTime;
    if ( !CmdLine::HasArg( "-logcardirt" ) || ( !bForce && logTimer < 1.5f ) ) {
        return;
    }
    logTimer = 0.0f;
    const float stain = getStain();
    if ( !bForce && std::abs( stain - lastLogged ) < 0.02f && wetness < 0.99f ) {
        return;
    }
    lastLogged = stain;
    OTDU_LOG_INFO( "CarDirt: mud %.2f (crust %.2f) dirt %.2f water %.2f%s\n", mud, hardness, dirt, wetness,
                   needsWash() ? " - needs the wash" : "" );
}

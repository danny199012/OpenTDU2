#pragma once

// Per-car dirt / mud state (port-native [INF], docs/re/surface_effects_spec.md 3
// and docs/re/car_wash_spec.md 1): the wheels throw the surface onto the body,
// the rain washes it off, the sun bakes it into a crust that the rain can
// hardly remove - a car wash is then the only way back to clean. The paint
// blend itself is done by CinematicCar::applySurfaceState (the retail
// TD_CarPaint RampClr1/2 + EnvCoeff terms), so this class is pure state: no
// device access, no save. The retail CarDirtData bytes are not persisted by
// the port yet (car_wash_spec 4.6).
class CarDirt {
public:
    // mudContacts / dirtContacts: the share of the grounded wheels that sit on
    // wet mud / dry dirt (0..1, the caller divides by the contact count);
    // speedKmh: the car's speed; rainIntensity: rain_ground_global_intensity of
    // the frame (0..1); sunEnergy: the sun_diffuse luminance (~0..1.2).
    void update( float deltaTime, float mudContacts, float dirtContacts, float speedKmh, float rainIntensity,
                 float sunEnergy );

    // The car wash (or a teleport / car change): back to showroom clean.
    void wash();
    // A test seed (-dirtseed mud,dirt,hardness,wetness, each 0..100).
    void seed( float mud, float dirt, float hardness, float wetness );

    inline float getMud() const { return mud; }
    inline float getDirt() const { return dirt; }
    inline float getMudHardness() const { return hardness; }
    inline float getWetness() const { return wetness; }
    // 0..1 blend the paint uses: the mud dominates a dust film.
    inline float getStain() const { return mud > dirt ? mud : dirt; }
    // The car wash prompt's "your vehicle is dirty" test.
    inline bool needsWash() const { return mud > 0.25f || dirt > 0.45f; }
    // -logcardirt: one line per 1.5 s while anything moves.
    void log( float deltaTime, bool bForce = false );

private:
    float mud = 0.0f;      // wet mud cake on the body (0..1)
    float dirt = 0.0f;     // fine dust / dried mud film (0..1)
    float hardness = 0.0f; // 0 runny mud .. 1 a sun-baked crust
    float wetness = 0.0f;  // water film on the paint (rain, puddle spray)
    float logTimer = 0.0f;
    float lastLogged = -1.0f;
};

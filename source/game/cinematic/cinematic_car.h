#pragma once

#include "filesystem/bank.h"
#include "render/eden/eden_animation.h"
#include "render/eden/eden_headlights.h"
#include "render/eden/eden_model.h"

#include <functional>

class EdenRenderer;
struct WeatherPreset;
struct GPUTexture;

// A car of a cinematic (Entity__CinCar / the current car): the high
// resolution vehicule bank (vehicules/high/<file>.bnk), its rims and tires
// (common_car.bnk), drawn with the original car shaders.
class CinematicCar {
public:
    struct Desc {
        std::string FileName = "California";
        // The bank the model is loaded from when it is not the physics row's
        // own bank. Empty = FileName. Used by the bike prototype (-bike: the
        // cut Ducati Diavel model with the Default_Bike physics row), whose
        // model and DB row are two different banks (docs/re/bikes_spec.md).
        std::string ModelBank;
        std::string RimFront = "ferrari/california_f_01";
        std::string RimRear = "ferrari/california_f_01";
        int32_t DiameterFront = 19;
        int32_t DiameterRear = 19;
        // The tuning shop's plus-size rims (inches), 0 = the database
        // diameters above. The tire keeps TireRadius (the rolling radius, the
        // ride height and the speedometer do not change): CinematicCar remaps
        // the T_<diameter>P tire mesh so its bead sits on the rim lip and its
        // tread on TireRadius, i.e. the sidewall gets thinner as the rim grows.
        int32_t RimDiameterFront = 0;
        int32_t RimDiameterRear = 0;
        float TireRadius = 0.339f; // 245/40 R19
        // Level of detail: "" the high bank (vehicules/high/<car>.bnk), "med" /
        // "low" the vehicules/med/<car>_med|_low.bnk the game ships for every
        // car (the AI drivers use them, docs/re/opponents_spec.md 6).
        std::string Lod;
        // The cockpit (vehicules/interiors/<car>_i.bnk); a far car skips it.
        bool bInterior = true;
    };
    // Ground height at (x, z) searched below y.
    using SurfaceFunction = std::function<bool( float x, float z, float y, float &outHeight )>;

public:
    CinematicCar();
    ~CinematicCar();

    bool load( const Desc &desc );
    void unload();
    inline bool isLoaded() const { return car.isLoaded(); }
    inline float getTireRadius() const { return tireRadius; }
    // How far the model's wheel hubs sit under the tire radius (tire radius -
    // the D_RIM_ node height, the mean of the wheels): the model's origin is
    // the underside of its body, and its wheels are authored at their resting
    // place, so at rest the origin is this far above the ground. The player
    // car's physics keeps its body frame on the ground and carries this as
    // its model lift (VehicleDynamics::setModelLift); fitOnGround places the
    // model with it. 0 with -wheellift (the old behaviour: the wheels moved).
    inline float getBodyLift() const { return bodyLift; }
    inline EdenModel &getModel() { return car; }
    // The rim diameters drawn: the Desc's, or a plus size bought at a tuning
    // shop. Reselects the tire mesh nodes (common_car.bnk T_<diameter>P); the
    // tire geometries were all remapped to the car's stock radius at load, so
    // this is live (no model reload, no device drain).
    void setRimDiameters( int32_t frontInch, int32_t rearInch );
    // The 3D cockpit gauge needles (docs/re/needles_spec.md): rotate the
    // interior model's `ND_SPD` / `ND_RPM` needle meshes to the angles the
    // car's interior_kmh / interior_mph / interior_rpm tables map the speed /
    // rpm to. The mesh node is rotated about its local Z from the rest pose
    // captured at load; a car whose interior bank has no needle nodes is a
    // no-op. Call every frame (FreeRideHud::getInterior*NeedleAngle()).
    void setDashboardNeedles( float speedDegrees, float rpmDegrees );
    // Rolls the wheels while a cinematic drives the car (the physics is not
    // stepped there): the D_RIM_xx nodes and their tyre sibling node rotate
    // about the axle (+x of the car frame) from their rest transforms, the
    // hubs stay put. The garage C_OUTRO integrates the .sce track velocity
    // and calls this per frame — the same law as gm_freeride's
    // outroWheelSpin for C_FIRST_OUTRO. radians is cumulative (0 restores).
    void setWheelSpin( float radians );

    // Car world pose: the cinematic pose with the 4 tires resting on the
    // surface (plane through the hub targets).
    Matrix4x4f fitOnGround( Vector3f position, const Quaternionf &rotation,
                            const SurfaceFunction &surfaceHeight ) const;
    // Environment cube of the car reflections: the dynamic cube rendered
    // around the car, or a static cube (bDynamic false: the CUBECAR texture
    // of the garage bank).
    void assignEnvironment( EdenRenderer &renderer, GPUTexture *pCube, bool bDynamic = true );
    // Door opening (docs: /tmp/claude-0/spec_garage_car.md 3): side 0 left
    // (DOOR_L, car slot 29), 1 right (DOOR_R, 30). FUN_00c67230 /
    // FUN_00c67200 store the angle at car +0xa794 / +0xa790; without the
    // DOOR_x_A_n key nodes (absent on the California) FUN_00c67020 rotates the
    // door node about its local Y axis by that angle (FUN_00ddbcd0). The
    // door subtree (mirror, window, INT_LR_x) and the interior bank panel
    // INT_HR_x (instanced on the door, FUN_00c69e30) follow.
    void setDoorAngle( int32_t side, float angle );
    inline float getDoorAngle( int32_t side ) const { return doorAngle[side & 1]; }
    // Side windows (FUN_00c66150 / FUN_00c66110): lowered windows are the
    // WND_x1 nodes moved onto the WND_x2 rest transform (the door frame at the
    // bottom of the glass), raised back to their own rest pose. The rests are
    // captured at load, before the retractable roof lowers them.
    // setWindowsOpen requests the state; updateWindows animates WND_x1 from
    // one rest to the other over kWndAnimDuration (the retail switch is a
    // factor 0..1, FUN_00c66150(f) [INF duration: not in the readable code]).
    void setWindowsOpen( bool bOpen );
    void updateWindows( float deltaTime );
    inline bool areWindowsOpen() const { return bWindowsOpen; }
    inline bool isWindowsAnimating() const { return windowTarget != windowTime; }
    // 0 raised, 1 lowered (the animated position of WND_x1).
    inline float getWindowsFactor() const { return windowTime; }
    inline bool hasWindows() const { return windows[0].Raised >= 0 || windows[1].Raised >= 0; }
    // Exterior lights (docs/re/car_lights_spec.md, ported from the upstream
    // read of FUN_00c5ee20 / FUN_00c588d0 / FUN_00c57ef0). The lamp materials
    // carry a GLOSSLIGHT mask (california_lgt_g) whose channels select the
    // function and a TEXLIGHT lit art (california_lgt_on); the pixel shader
    // adds TEXLIGHT x dot(mask, GLOSSFCT) and kills the diffuse of the lit
    // pixels. GLOSSFCT is the per frame light state: x brake, y reverse,
    // z position / headlights, w the blinker of the material side (the
    // amber round-lamp centres). The data ships (1, 1, 1, 1) = every function
    // lit at once; the driver writes the real state every frame.
    // setHeadlights(false) leaves the mode weather driven (the car_* keys:
    // 0 by day, 1 at night), true forces the lamps (mode 2, -carlights 1).
    void setHeadlights( bool bOn );
    inline bool areHeadlightsOn() const { return bHeadlights; }
    inline bool hasLamps() const { return !lightMaterials.empty(); }
    void setIndicator( int32_t side ); // -1 off, 0 left, 1 right
    // Emission multiplier over the GLOSSFCT lamp channels (the brake /
    // reverse / position / blinker lens brightness). 1 = the retail values
    // (the port reads dimmer on screen than the owner's retail screenshots);
    // -lampboost feeds it.
    inline void setLampBoost( float boost ) { lampBoost = Max( boost, 0.0f ); }
    inline float getLampBoost() const { return lampBoost; }
    // The windscreen wipers (session 65, port-invented driver over the retail
    // parked wiper prim: the car models bake their blades into the body - one
    // wide thin strip prim at the glass base, none carries a pivot or an
    // animation - so the driver hides that prim and re-draws its own triangles
    // as two halves swung about pivots at its ends). updateWipers per frame
    // (auto mode sweeps while the weather rains, the interval follows the
    // intensity; forced mode sweeps continuously); the sweep state also
    // drives the droplet wipe (getWiperSweep, the FREERIDE callback projects
    // the blade to the screen and clears the windscreen layer).
    enum class eWiperMode { Off, Auto, Fast };
    void setWiperMode( eWiperMode mode );
    inline eWiperMode getWiperMode() const { return wiperMode; }
    void updateWipers( float deltaTime, float rainAlpha );
    // World-space pivot and tip of one blade (0 left, 1 right) right now;
    // false when the wipers are parked or this car has none.
    bool getWiperSweep( uint32_t half, Vector3f &outPivot, Vector3f &outTip ) const;
    inline bool hasWipers() const { return !wipers.empty(); }
    inline int32_t getIndicator() const { return indicatorSide; }
    void setBrakeLights( bool bOn );
    inline bool areBrakeLightsOn() const { return bBraking; }
    void setReverseLights( bool bOn );
    inline bool getReverseLights() const { return bReverse; }
    // The blink phase lamp (a blinker channel currently lit, of either side).
    inline bool isIndicatorLampOn() const { return bIndicatorLamp; }
    // Advances the blinker timer (call every frame with the frame time). The
    // retail cadence (FUN_00c57ef0): period 1.0 s, lit while t < 0.6.
    void updateLights( float deltaTime );
    // The glow of the lit lamps (car_lights_spec.md 8, port-native [INF]):
    // one soft additive quad per lit lamp, in world space (the car drawn at
    // `world`), appended to `out` (EdenLampGlowFrame). Uses the state the
    // last applyLights() wrote.
    void appendLampGlows( const Matrix4x4f &world, std::vector<EdenLampGlow> &out ) const;
    // Night dashboard / interior lighting (TD_DashBoard reads IlumParam /
    // IlumColor / HeadlightsIntensity): the material ships
    // HEADLIGHTSINTENSITY = 0, which zeroes its whole backlight term, so the
    // driver raises it with the headlights. bOn = the headlights are on.
    void setDashboardLit( bool bOn );
    inline bool isDashboardLit() const { return bDashboardLit; }
    // The dashboard sat-nav (docs/re/opentdu1_reuse.md 1): binds our map
    // render target into the interior GPS material's satellitemap slot and
    // writes the UVOffset the TD_GPS vertex shader reads (TDU1 setGPSTexture
    // writes static params [0]=minimap U, [1]=V, [2]=3.0; the material ships
    // (1, 1, 2, 0)). Also re-shows the GPS subtrees, hidden at load since
    // session 45 (unprojected they draw as a black sail). Returns false when
    // the interior carries no GPS material / slot.
    bool setGpsTexture( EdenRenderer &renderer, GPUTexture *pTexture, const Vector4f &uvOffset );
    inline bool hasGps() const { return !gpsMaterials.empty(); }
    inline bool isGpsBound() const { return bGpsBound; }
    // Retractable roof (Hardtop.anm, FUN_00c3fcc0 / FUN_00c66920): the skinned
    // HANIM_S is re-sampled while the clip plays, so the roof can be opened and
    // closed at run time. updateHardtop advances an open / close request (call
    // every frame; no-op when the roof is at rest).
    void setHardtopOpen( bool bOpen );
    void updateHardtop( float deltaTime );
    inline bool isHardtopOpen() const { return hardtopTime > 0.0f; }
    inline bool hasHardtop() const { return bHardtop; }
    inline bool isHardtopAnimating() const { return hardtopTarget != hardtopTime; }
    // Model space transform of a car node (names longer than 8 characters
    // are folded, e.g. DVRSEAT_LR); false if absent.
    bool getNodeModelTransform( const char *pName, Matrix4x4f &outTransform ) const;
    // Model space bounds of the high resolution body (BODY_HR).
    bool getBodyBounds( Vector3f &outMin, Vector3f &outMax ) const;
    void draw( EdenRenderer &renderer, const Matrix4x4f &world, const WeatherPreset *pWeather,
               bool bEnvironmentAssigned );

    // The level of detail loaded ("" = high).
    inline const std::string &getLod() const { return lodName; }
    // DBCarPhysicsData._id_car of the loaded car file (-1 if unknown).
    int32_t getIdCar() const;

    // The Desc of a database car: DBCarPhysicsData (_id_car -> _car_file_name,
    // _default_rims) -> DBRims (_rsc_path, _rsc_file_name_front / rear,
    // _diameter_*, the tire from _largeur_front / _hauteur_front) [DATA]
    // (the TitleScene resolution). False when the car is not in the database.
    static bool DescFromDatabase( int32_t carId, Desc &outDesc );
    // The same for a given rims row (the shop slots / the test drive carry
    // their own rims hashcode; 0 = the car's default rims).
    static bool FindDatabaseDesc( int32_t idCar, uint64_t rimsHashcode, Desc &outDesc );
    // A colour by material id (FUN_00a50ca0 -> FUN_00d8e6a0): the
    // DBCarsAndColors row of the car whose _color_id_1 is that id, painted
    // like applyDatabasePaint. False when the id is not one of the car's
    // colours ("" / "##": no change).
    bool applyColor( const std::string &colorId );
    // The swatch colour of a paint row (0..1 RGB): the row's _color_id_1
    // material from the car's own colour library, then colors.2dm - the
    // material applyDatabasePaint copies - its RAMPCLR1 body colour, else its
    // diffuse. False when the row has no colour material. The dealer's buy
    // window paints its colour squares with it [INF: the retail swatch render
    // callback FUN_009ee3c0 reads a per-row colour pair it computed elsewhere].
    bool getDatabasePaintSwatch( int32_t paintIndex, float outRgb[3] );
    // The same for a colour library material by name (the DBInterior
    // _interior_color_<n> ids 'C_N_02' ...): false when no library has it.
    static bool FindLibrarySwatch( const std::string &materialName, float outRgb[3] );

    // Database paint (docs/re/paint_spec.md 3.2 / 3.3, FUN_00d8e6a0 ->
    // FUN_00c5ff20 mode 0): row paintIndex of the car's DBCarsAndColors rows
    // (table order; out of range -> 0); its _color_id_1 names a material of
    // the colour libraries (the car bank's <car>_color.2dm, then
    // vehicules/colors.bnk colors.2dm) whose paint parameters are copied onto
    // the car's paint materials. Returns the row's _color_name ("" when the
    // car has no row / no colour material: the .2dm paint is kept).
    std::string applyDatabasePaint( int32_t paintIndex );
    // The paint shop's custom paint (the sticker shop's Paint page): the
    // finish (0 gloss, 1 matt, 2 metal, 3 carbon - the colors.2dm base
    // materials GLOSSY / MATT / METAL / CARBON) with the body colour (RGBA8,
    // MakeRGBA order) on the Paint materials and, with bBicolour, the second
    // colour on Paint2. restorePaint() puts the dealer paint back (the values
    // the first custom paint found). getBodyPaintColour() is the body's
    // current colour (RampClr2). [INF: the colour -> ramp mapping is [ENC]]
    bool applyCustomPaint( int32_t type, uint32_t colour1, bool bBicolour, uint32_t colour2 );
    void restorePaint();
    uint32_t getBodyPaintColour();
    // Number of DBCarsAndColors rows of a car.
    static int32_t CountDatabasePaints( int32_t carId );
    // The catalogue of a car: its DBCarsAndColors rows in paint index order
    // (_color_name, _price: the colour's extra cost).
    struct PaintInfo {
        std::string Name;
        int32_t Price = 0;
    };
    static std::vector<PaintInfo> DatabasePaints( int32_t carId );
    // The DBCarsAndColors row's _color_id_1 of the car's paint ("" when the
    // row does not exist): the test-drive request's ColorId.
    static std::string DatabasePaintColorId( int32_t carId, int32_t paintIndex );

    // The sticker layer (docs/re/paint_spec.md 4/6): the TD_CarPaint2 pixel
    // shader lerps a sticker texture sampled on s5 over the paint; s5 is the
    // paint material's USER_TEX slot (one_77.2dm carries it: USER_TEX
    // (stickers)). The port puts its per-car sticker canvas there. Binds
    // pTexture onto every body paint material that carries a USER_TEX slot
    // (updateProgram re-derives the permutation: the slot counts into
    // S_NBTEXTURES). Returns the number of materials bound (0 = no paint
    // material has the slot: the car takes no stickers).
    uint32_t bindStickerTexture( EdenRenderer &renderer, GPUTexture *pTexture );

    // Dynamic dirt / mud / water on the body (CarDirt). The paint materials
    // blend toward a mud tint and lose their reflection as the stain grows; a
    // wet car keeps a slight darkening and extra gloss. Call every frame with
    // the simulated 0..1 values; the clean parameter values are captured once
    // (and after every repaint) so the blend never accumulates.
    void applySurfaceState( float mud, float dirt, float hardness, float wetness );

    // Car damage (docs/re/damage_spec.md): 0 clean, 1 scratched, 2 deformed, 3 broken. From state 2 the
    // exterior glass binds the retail damaged-glass art (common_car's glass_b, a starred pane) in its
    // DETAIL2 slot - the CarPaint glass shader fades the base toward it - and back to the clean glass
    // base (g_i) below. Idempotent: it only touches the materials when the state changes.
    void applyDamageState( int32_t state );

private:
    // The light driver (docs/re/car_lights_spec.md): collectLights at load
    // (FUN_00c5ee20 + FUN_00c5f060), updateLights per frame (the blinker
    // timer, FUN_00c57ef0), applyLights at draw (FUN_00c588d0: the GLOSSFCT
    // channels, the glow node flags, the light box INTENSITY).
    void collectLights();
    void applyLights( const WeatherPreset *pWeather );

    std::string fileName; // Desc::FileName actually loaded
    std::string lodName;  // Desc::Lod actually loaded ("" = high)
    // The texture-cache owner of this instance's banks (per instance: two
    // CinematicCars of the same car - the player's California and the cops'
    // California - must not share one owner entry, or the first one to unload
    // pulls the textures from under the other, session 46's device loss).
    uint32_t ownerId = 0;
    std::string textureOwnerName( const Bank &bank ) const;
    Bank carBank;
    Bank carCommonBank;
    Bank rimBank;
    Bank rimRearBank;
    Bank interiorBank;
    EdenModel car;
    EdenModel interior;
    EdenModel rim;
    EdenModel rimRear;
    EdenModel tire;
    // Retractable roof (docs: /tmp/claude-0/spec_california.md): skeleton
    // <car>.xsb + Hardtop.anm posing the skinned HANIM_S node at
    // hardtopTime (0: closed, end of the clip: open).
    EdenSkeleton hardtopSkeleton;
    EdenAnimation hardtopAnimation;
    bool bHardtop = false;
    float hardtopTime = 0.0f;
    float hardtopTarget = 0.0f; // run time open / close request
    bool bWindowsOpen = false;
    // Animated window position (0 raised, 1 lowered) and its target.
    float windowTime = 0.0f;
    float windowTarget = 0.0f;
    // WND_x1 / WND_x2 rest transforms (captured before the roof lowers them).
    struct WindowNodes {
        int32_t Raised = -1;
        Matrix4x4f RaisedRest;
        Matrix4x4f LoweredRest;
    } windows[2];
    // Light materials (docs/re/car_lights_spec.md 2.1): the materials of the
    // car named after table 0x00f7e700 (FUN_00c5ee20) - LGT_FL, LGT_RL,
    // LGT_RL_SK, LGT_FR, LGT_RR, LGT_RR_SK, LGT_R_S, LGT_L, LGT_R - each
    // carries the GLOSSFCT light state the driver writes every frame.
    struct LightMaterial {
        uint32_t Material = 0; // car.getMaterials() index
        bool bLeft = false;    // LGT_xL left, LGT_xR right
        bool bFront = false;   // LGTxF front
    };
    std::vector<LightMaterial> lightMaterials;
    // Lamp glow meshes Fa_<L|B|S|R|W>_<FL|FR|RL|RR>[_n][_C] (FUN_00c5f060):
    // 'L' front lights, 'B' rear position, 'S' stop, 'R' reverse, 'W' warning
    // (the blinker of the node side). The "_C" variants are the additive glow
    // meshes, the plain ones flare sprites (only 6 car banks have them).
    struct LightNode {
        int32_t Node = -1;
        char Function = 0;
        bool bLeft = false;
        bool bFront = false;
        bool bFlare = false;
    };
    std::vector<LightNode> causticNodes;
    std::vector<LightNode> flareNodes;
    // The projected light boxes: car slots 13 BREAKING, 14 REVERSING,
    // 15 HEADLIGHT (TD_ShadowBox ps1, base texture breakcaustics /
    // reversecaustics / headlightcaustics, additive x INTENSITY; FUN_00c63a30).
    // The renderer still skips TD_ShadowBox (the projection constants are not
    // implemented); the driver keeps visibility + INTENSITY right for the day
    // that decodes them.
    int32_t lightBoxNodes[3] = { -1, -1, -1 };
    std::vector<uint32_t> lightBoxMaterials[3];
    // Light state (car +0x280 driver: +0x1d0 brake, +0x1d1 reverse, +0x1d4
    // light mode, +0x1db / +0x1dc left / right blinker, +0x1f10 blink timer).
    int32_t headlightMode = -1; // -1 follow setHeadlights, 0 off, 1 weather, 2 forced
    bool bHeadlights = false;   // the setHeadlights request (mode -1)
    bool bBraking = false;
    bool bReverse = false;
    int32_t indicatorSide = -1;   // -1 off, 0 left, 1 right
    float indicatorTimer = -1.0f; // car +0x1f10 (-1: blinkers off)
    float highBeamTimer = 0.0f;   // car +0x1f24 (mode 2 delay)
    bool bIndicatorLamp = false;
    float indicatorValue[2] = { 0.0f, 0.0f }; // left, right blinker output
    // The lamp glow anchors (collectLampAnchors): where each lamp function
    // shines from, in its node's space.
    struct LampAnchor {
        int32_t Node = -1;
        Vector3f Local;
        Vector3f Normal = Vector3f( 0.0f, 0.0f, 1.0f ); // model space
        float Size = 0.15f;
        char Function = 0; // L B S R W (car_lights_spec.md 2.4)
        bool bLeft = false;
        bool bFront = false;
        bool bRepeater = false; // a side repeater (seen from the side)
        bool bStrip = false;    // part of a third stop light band (halo only)
    };
    std::vector<LampAnchor> lampAnchors;
    void collectLampAnchors();
    // The values applyLights wrote last (the glows follow them).
    float appliedHeadlight = 0.0f, appliedPosition = 0.0f, appliedBrake = 0.0f, appliedReverse = 0.0f;
    float appliedNight = 0.0f; // 0 day .. 1 night (car_headlight_flare_intensity)
    // The blinker lamp's rise / fall (a bulb, not a switch) [INF].
    float indicatorGlow[2] = { 0.0f, 0.0f };
    float lampBoost = 1.4f;                   // GLOSSFCT emission multiplier (setLampBoost)
    bool bInTunnel = false;                   // the tunnel_car_* weather keys
    // Interior dashboard materials (HUD / HUD_01) and their ILUMCOLOR /
    // HEADLIGHTSINTENSITY rest values.
    struct DashMaterial {
        int32_t Index = -1;      // interior.getMaterials() index
        Vector4f RestIlum;       // the loaded ILUMCOLOR
        Vector4f RestIntensity;  // the loaded HEADLIGHTSINTENSITY
    } dash[2];
    // The dashboard sat-nav: interior materials on the TD_GPS shader
    // (7dad16b47eb57b95) and the GPS node subtrees (hidden at load,
    // re-shown when the map texture is bound).
    std::vector<int32_t> gpsMaterials; // interior.getMaterials() indices
    std::vector<int32_t> gpsNodes;     // interior nodes of the GPS subtrees
    bool bGpsBound = false;            // setGpsTexture ran for this load
    bool bDashboardLit = false;
    // The cockpit gauge needles (setDashboardNeedles): the interior model's
    // needle mesh nodes and the rest pose each is rotated from.
    int32_t dashboardSpeedNode = -1;
    int32_t dashboardRpmNode = -1;
    Matrix4x4f dashboardSpeedRest;
    Matrix4x4f dashboardRpmRest;
    // setWheelSpin: the rest transforms of the wheel node pairs (D_RIM_xx and
    // its tyre sibling), captured on the first call (unload clears them).
    std::vector<Matrix4x4f> wheelSpinRests[2]; // [0] rim, [1] tyre
    bool bWheelSpinRests = false;
    std::vector<Matrix4x4f> hardtopSkin;
    // Cockpit level of detail (FUN_00c66380): LOD 4 shows the interior
    // bank (COCK_HR, INT_HR_L/R), LOD 3 the exterior low resolution cockpit
    // (COCK_LR, INT_LR_L/R).
    std::vector<int32_t> cockpitLowNodes;    // car nodes
    std::vector<int32_t> interiorDrawnNodes; // interior nodes (COCK_HR subtree, INT_HR_L/R)
    // The windscreen glass (GLASS_F) and the side window glass (WND_L1 / R1) of
    // the exterior model: hidden in the inside views. The exterior Glass
    // material carries the DIRT_SCRATCH detail art (the scratch streaks) and
    // its REFLECTION cube; from the driver's seat that reads as a dirty,
    // dark windscreen you can hardly see through - the retail build swaps it
    // for the interior bank's GLASS_I carrier (TD_CarGlassDirt) there
    // (FUN_00c53d10), which the port does not instance. `-keepglass` keeps the
    // exterior art for an A/B.
    std::vector<int32_t> glassNodes;
    // -hidenode A/B: subtrees that must stay hidden across the per-frame
    // cockpit LOD re-show (car side [0], interior side [1]).
    std::vector<int32_t> alwaysHiddenNodes[2];
    // The low resolution paint / plate items (Paint_LR, Plate_LR) that share a
    // node with high resolution items (the Q7's BODY_HR carries one): hidden
    // per item on every draw, the node stays visible.
    std::vector<int32_t> lowResItems;
    bool bLastInterior = false;
    // The retail inside view swaps the car "Glass" material for the interior
    // bank's Glass_I (TD_CarGlassDirt, california_spec.md 4: FUN_00c53d10 on
    // the camera flag 0x40000) - the outside glass is a near-opaque dirt wall
    // from the driver seat otherwise (docs/STATE.md session 45).
    std::vector<std::pair<EdenModel *, int32_t>> glassSwap; // the car's "Glass" materials
    std::vector<EdenMaterial> glassOriginals;               // their outside state
    EdenMaterial glassDirt;                                 // the interior's Glass_I
    bool bGlassSwapped = false;
    bool bGlassGathered = false;
    int32_t appliedDamageState = -1;
    void applyGlassSwap( bool bInside );
    // The wiper driver (the comment at setWiperMode).
    struct WiperHalf {
        int32_t ItemIndex = -1;
        EdenMesh Mesh;              // the item's streams, own index buffer (the half's triangles)
        uint32_t IndexCount = 0;
        Vector3f Pivot;             // node space, the outer end of the half
        Vector3f Blade;             // node space, pivot -> tip at rest
        GPUBuffer *pIndices = nullptr;
    };
    std::vector<WiperHalf> wipers;
    int32_t wiperNode = -1;         // the node the wiper prim hangs from
    eWiperMode wiperMode = eWiperMode::Auto;
    float wiperAngle = 0.0f;        // rad, 0 = the parked pose of the art
    float wiperTimer = 0.0f;        // intermittent countdown / sweep progress (<0: sweeping u)
    void collectWipers();
    struct DoorNodes {
        int32_t Door = -1;
        Matrix4x4f DoorRest;
        std::vector<std::pair<int32_t, Matrix4x4f>> CarRest;      // DOOR_x subtree
        std::vector<std::pair<int32_t, Matrix4x4f>> InteriorRest; // INT_HR_x subtree
    };
    DoorNodes doors[2];
    float doorAngle[2] = { 0.0f, 0.0f };
    // The clean paint parameters applySurfaceState blends from (captured on
    // the first call and after every repaint).
    struct PaintBaseline {
        uint32_t Material = 0;
        Vector4f RampClr1;
        Vector4f RampClr2;
        Vector4f Diffuse;
        Vector4f Ambient;
        Vector4f Specular;
        bool bRampClr1 = false;
        bool bRampClr2 = false;
    };
    std::vector<PaintBaseline> paintBaseline;
    bool paintBaselineValid = false;
    // The dealer paint's materials (applyCustomPaint snapshots them once;
    // restorePaint writes them back; a new database paint drops the snapshot).
    struct PaintSnapshot {
        uint32_t Material = 0;
        std::unordered_map<uint64_t, Vector4f> Parameters;
        Vector4f Diffuse;
        Vector4f Ambient;
        Vector4f Specular;
    };
    std::vector<PaintSnapshot> paintOriginal;
    bool bPaintOriginal = false;
    float stainVisual = 0.0f; // the last applySurfaceState inputs (draw's gloss)
    float wetVisual = 0.0f;
    std::vector<int32_t> wheelNodes;
    std::vector<int32_t> wheelShadowNodes;
    int32_t tireNodeFront = -1;
    int32_t tireNodeRear = -1;
    int32_t rimDiameterFront = 19;
    int32_t rimDiameterRear = 19;
    int32_t loggedRimFront = -1; // the last multiplier-logged diameter (this car)
    int32_t loggedRimRear = -1;
    float tireRadius = 0.339f;
    float bodyLift = 0.0f;
};

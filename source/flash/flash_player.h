#pragma once

#include "flash/flash_definitions.h"

// The retail stage destination rect (normalized to the backbuffer: x0, y0,
// width, height) of the screen ratio mode (SystemPC.cpr SetScreenRatio,
// FUN_00d7faa0): ratio 1 (16:9) shows the stage band y 120..840 at full
// width. The canvas <-> stage conversions use this one: it is window
// independent (canvas and the stage band map 1:1 inside it).
void FlashRetailStageRect( int32_t screenRatio, float &x0, float &y0, float &width, float &height );
// The rect the stage actually draws into for the UI layout option
// (options.ini ui_layout): Stretch = the retail rect, Fit = the stage's
// visible band scaled uniformly and centered in the window (black bars,
// zero distortion).
void FlashStageRect( int32_t screenRatio, float stageWidth, float stageHeight,
                     float backbufferWidth, float backbufferHeight,
                     float &x0, float &y0, float &width, float &height );
// Canvas (1280x720) <-> Flash stage pixels, through the window: the two
// mappings above compose, so the HUD's native canvas layers and the movies
// stay aligned in both UI layout modes (at 16:9 in Stretch this is the
// retail band math, mode 1: stage y = canvas y + 120).
void FlashCanvasToStage( float canvasX, float canvasY, float stageWidth, float stageHeight,
                         float &outStageX, float &outStageY );
void FlashStageToCanvas( float stageX, float stageY, float stageWidth, float stageHeight,
                         float &outCanvasX, float &outCanvasY );

class FlashMovie;
class Renderer2D;
class Font;

// Runtime of an Eden flash movie (.vmf): display list, timelines (root and
// sprites), a subset of the ActionScript 1 virtual machine (timeline control,
// variables, user functions, movie clip properties) and rendering through the
// 2D renderer (original TD_Flash.sob shaders).
class FlashPlayer {
public:
    using FSCommandCallback = std::function<void( const std::string &command, const std::string &args )>;
    // '#command:<ACTION>#' markup of read only text fields (FUN_00849dd0:
    // the global resolver DAT_013e281c, vtable 0x00ee6ff0, vfunc +4 =
    // FUN_00d87cc0 returns a markup string laid out in place, recursively).
    // Empty result: the markup is dropped.
    using CommandResolver = std::function<std::string( const std::string &action )>;
    static void SetCommandResolver( CommandResolver resolver );
    // Pad form of FUN_00d87cc0 for a key map entry whose DevSubType (+0x179)
    // is 3 (X360 pad): channel name -> "#bitmap:x360_..#" (table
    // FUN_00d877b0, strings 0x00ee6270..0x00ee64b8). Empty for an unknown
    // channel (0x00f35018 "").
    static std::string PadChannelMarkup( const std::string &channel );
    // Keyboard form of FUN_00d87cc0 (format 0x00ee61f4, key map entry +0x60
    // ChannelLocName).
    static std::string KeyboardChannelMarkup( const std::string &channelLocName );

    // '#bitmap:<name>#' inline images: global registry DAT_013e2720 filled
    // from the named children of a "$$bitmaps$$" sprite (FUN_0085ad40 /
    // FUN_00dd8830; GENERAL.vmf ch612: x360_* and PS3_* button icons).
    struct TextBitmap {
        const FlashPlayer *pOwner = nullptr;
        GPUTexture *pTexture = nullptr;
        float U0 = 0.0f, V0 = 0.0f, U1 = 1.0f, V1 = 1.0f;
        float Width = 0.0f, Height = 0.0f; // bitmap pixels (object +0xa / +0xc)
        float ScaleX = 1.0f, ScaleY = 1.0f; // placement scale in "$$bitmaps$$"
    };
    static const TextBitmap *FindTextBitmap( const std::string &name );

    // The platform of the button prompts, a WhatPlatform value (0 x360,
    // 1 pc, 2 ps3). The glyph sprites read /:WhatPlatform once when placed
    // and jump to their 'x360' / 'pc' / 'ps3' frame [DATA]; the retail PC
    // sets 1 on GENERAL and MAIN (FUN_00d8d930 [CODE]) and picks x360 art
    // from the options' controller (/:IsPC_X360). OpenTDU2 follows the
    // device in use instead [INF]: a glyph's own read gets this value, the
    // other scripts keep the movie's WhatPlatform (MAIN's pause menu builds
    // its PC-only squares from it). Setting it re-points the glyphs on screen.
    static void SetPromptPlatform( int32_t whatPlatform );
    static int32_t GetPromptPlatform();
    // FUN_00d877b0's x360 bitmap name -> its PS3_* twin in GENERAL.vmf's
    // '$$bitmaps$$' (x360_A -> PS3_X...); other names are returned unchanged.
    static std::string PS3BitmapName( const std::string &x360Name );

    struct Clip;
    struct Object;

    struct Value {
        enum class eType : uint8_t { Undefined, Null, Bool, Number, String, Clip, Object, Function };
        eType Type = eType::Undefined;
        double Number = 0.0;
        std::string String;
        Clip *pClip = nullptr;
        std::shared_ptr<Object> pObject;
        int32_t FunctionIndex = -1;

        static Value MakeNumber( double v ) { Value r; r.Type = eType::Number; r.Number = v; return r; }
        static Value MakeString( std::string v ) { Value r; r.Type = eType::String; r.String = std::move( v ); return r; }
        static Value MakeBool( bool v ) { Value r; r.Type = eType::Bool; r.Number = v ? 1.0 : 0.0; return r; }
        double toNumber() const;
        std::string toString() const;
        bool toBool() const;
    };

    struct Object {
        std::map<std::string, Value> Members;
        // An Array (InitArray / new Array): an index member at or past
        // "length" grows it; push / pop / shift / unshift / join / slice.
        bool bArray = false;
    };

    struct Instance {
        uint16_t CharacterId = 0;
        uint16_t Depth = 0;
        FlashMatrix Matrix;
        FlashColorTransform ColorTransform;
        uint16_t Ratio = 0;
        uint16_t ClipDepth = 0;
        std::string Name;
        bool bVisible = true;
        bool bScripted = false;      // created by attachMovie (kept by timeline seeks)
        // _x / _y / _xscale / _yscale / _alpha set by a script: the parent
        // timeline's later moves keep the transform (the Flash rule; Ruffle's
        // transformed_by_script). MAIN.vmf openMenuH lays the squares out so.
        bool bTransformedByScript = false;
        std::unique_ptr<Clip> pClip; // sprite instances
    };

    struct CaseInsensitiveLess {
        bool operator()( const std::string &a, const std::string &b ) const {
            return StringCmpI( a.c_str(), b.c_str() ) < 0;
        }
    };

    struct Clip {
        const FlashTimeline *pTimeline = nullptr;
        Clip *pParent = nullptr;
        Instance *pInstance = nullptr; // owning display object (nullptr for the root)
        uint32_t CurrentFrame = 0;
        bool bPlaying = true;
        std::map<uint16_t, std::unique_ptr<Instance>> DisplayList;
        // Game supplied bitmap for the placeholder fills of the shapes this clip
        // directly holds (the map's brand logos: bitmap fills with no bitmap).
        GPUTexture *pBitmapOverride = nullptr;
        float OverrideUV[4] = { 0.0f, 0.0f, 1.0f, 1.0f }; // u0, v0, u1, v1
        // Case-insensitive like AVM1 up to SWF 6: the Eden movies define
        // getSquarePath and call GetSquarePath, read m_music_ for the database
        // string m_Music_...
        std::map<std::string, Value, CaseInsensitiveLess> Variables;
    };

public:
    inline bool isLoaded() const { return pMovie != nullptr; }
    inline FlashMovie *getMovie() const { return pMovie; }
    inline Clip *getRoot() { return &root; }
    inline const FlashDefinitions &getDefinitions() const { return definitions; }
    void setFSCommandCallback( FSCommandCallback callback ) { fsCommandCallback = std::move( callback ); }

public:
    FlashPlayer();
    ~FlashPlayer();

    bool load( FlashMovie *pFlashMovie );
    void unload();

    // Advances the movie (at its own frame rate).
    void tick( float deltaTime );
    // Draws the stage onto the destination rect of the screen ratio mode
    // (SystemPC.cpr SetScreenRatio; 16:9: stage y 120..840, full width).
    void draw( Renderer2D &renderer );
    // Destination rect of the stage, normalized to the backbuffer (x0, y0,
    // width, height): a tool that shows a movie inside its own panel sets it
    // (the asset viewer's preview); clearStageRect() goes back to the screen
    // ratio rect.
    void setStageRect( const float rect[4] ) {
        for ( int32_t i = 0; i < 4; i++ ) {
            stageRectOverride[i] = rect[i];
        }
        bStageRect = true;
    }
    void clearStageRect() { bStageRect = false; }
    // Draws one clip only (with its parents' transforms), e.g. the letterbox
    // "/black_stripes" of GENERAL.vmf.
    void drawClipOnly( Renderer2D &renderer, const std::string &path );

    // Scripting interface used by the game (paths: "/:var", "/clip/sub:var").
    void setVariable( const std::string &path, const std::string &value );
    // Typed variants (edFlash variant types 5 = bool, 7 = int).
    void setVariableBool( const std::string &path, bool bValue );
    void setVariableNumber( const std::string &path, double value );
    std::string getVariable( const std::string &path );
    // bRerunSameFrame: a goto onto the frame the clip already shows runs that
    // frame's actions again (the edFlash behaviour the dealer buy window needs,
    // see the .cpp); the default keeps the Flash rule (nothing happens).
    bool gotoLabel( const std::string &clipPath, const std::string &label, bool bPlay, bool bRerunSameFrame = false );
    // Frame control of a clip timeline (game driven animations, e.g. the
    // "hold to get back on track" fill of the HUD): the frame is clamped to
    // the clip timeline. True when the clip exists.
    bool gotoFrame( const std::string &clipPath, uint32_t frame, bool bPlay );
    Clip *findClip( const std::string &path );
    // Binds a texture rectangle to the placeholder bitmap fills of the shapes the
    // clip holds (u0, v0, u1, v1 in 0..1). True when the clip exists.
    // Sets a clip's whole transform at once (stage px, degrees, percent): the
    // route bars of the map are rotated and scaled copies of one bar.
    bool setClipTransform( const std::string &clipPath, float x, float y, float rotationDegrees, float xScalePercent,
                           float yScalePercent );
    // Visibility of the instance sitting at a depth of a clip (unnamed
    // children the path cannot address — the HUD enter element's parked cargo).
    bool setDepthVisible( const std::string &clipPath, uint16_t depth, bool bVisible );
    // Stops a clip and every clip below it (the game-driven HUD elements the
    // movie parks children inside — a freshly placed child clip plays its own
    // timeline otherwise, and looping children blink the art on and off).
    void stopSubtree( const std::string &clipPath );
    bool setBitmapOverride( const std::string &clipPath, GPUTexture *pTexture, float u0, float v0, float u1, float v1 );
    // duplicateMovieClip from the game side (the map's spots and credits lines
    // are built this way). True when the clip exists. The clone lands in the
    // source's parent; duplicateIn chooses it (a page re-uses another page's
    // row sprite this way).
    bool duplicate( const std::string &clipPath, const std::string &newName, int32_t depth );
    bool duplicateIn( const std::string &parentPath, const std::string &sourcePath,
                      const std::string &newName, int32_t depth );
    // attachMovie: places a sprite DEFINITION of the movie into a clip at a
    // scripted depth and position (stage px) - a row sprite a page's own art
    // never placed is instantiated this way. True when the character is a
    // sprite and the parent exists.
    bool placeCharacter( const std::string &parentPath, uint16_t characterId, const std::string &newName,
                         int32_t depth, float x, float y );
    // Writes rows (each a list of strings) into an array variable of a clip
    // ("/options/video:setParam") at absolute indices, growing the array when
    // an index is at or past its length. The options pages drive their rows
    // from such an array (setParameters reads the members through getParamType
    // / Cmd / Var); the game puts its own rows into a page's free slots this
    // way. True when the variable holds an array.
    bool writeArrayRows( const std::string &path, int32_t firstIndex,
                         const std::vector<std::vector<std::string>> &rows );
    // Debug: logs the clip tree below a path (name, character, frame, position,
    // visibility, variables) down to maxDepth. -dumpclips of the map screen.
    void dumpClips( const std::string &path, int32_t maxDepth );

    // Button events (DefineButton2 condition actions): key press (SWF key
    // code: 1 left, 2 right, 13 enter...) and mouse release on a button
    // (OverDownToOverUp) at a backbuffer position. True when handled.
    bool sendKey( uint8_t swfKey );
    // Calls an ActionScript function of a clip ("/" root, "/cursor"...) with
    // string arguments (game -> movie calls such as ItemType('over')).
    bool invoke( const std::string &clipPath, const std::string &function, const std::vector<std::string> &args );
    // The same with number arguments: AVM1 compares two strings as strings
    // ('8' < '9' lexicographically), so a loop bound a movie does arithmetic
    // on must arrive as a number (the options pages' setParameters(first,
    // lastIndex) walks its row table that way).
    bool invokeWithNumbers( const std::string &clipPath, const std::string &function,
                            const std::vector<double> &args );
    // The same with a (string, boolean) pair: the movies test flags with
    // Equals2 against true / false, which a string "true" never equals
    // (Cas_SlotMachine's SetHelp(HelpFrame, Change)).
    bool invokeWithFlag( const std::string &clipPath, const std::string &function, const std::string &arg,
                         bool bFlag );
    // Calls a method of the clip a variable holds (the frontend menus
    // register themselves as _root.Clip; the exe then calls Clip.Move('left'),
    // Clip.A(), Clip.B()). False when the variable is not a clip or the
    // method is not defined on it.
    bool invokeMethod( const std::string &variablePath, const std::string &method,
                       const std::vector<std::string> &args );
    // The clip a variable holds (nullptr if it holds anything else).
    Clip *getClipVariable( const std::string &variablePath );
    // MovieClip _visible (edFlash vfunc +0x20 on a clip).
    bool setClipVisible( const std::string &clipPath, bool bVisible );
    // Color.setRGB on a clip from the game side (0..1 RGB: multiply 0, add the
    // colour; alpha untouched) - the SHOPSCAR buy window's colour squares,
    // which the retail exe paints from native code. Kept against the
    // timeline's later moves like a scripted transform. True when it exists.
    bool setClipColor( const std::string &clipPath, float r, float g, float b );
    // The same for a child the movie placed without a name: the display list is
    // keyed by depth, so a path cannot reach it (HUD.vmf /HUD/enter places its
    // prompt bar at depth 1 unnamed, and it paints a stray beige rectangle at
    // the bottom left of the driving screen).
    bool setInstanceVisible( const std::string &clipPath, uint16_t depth, bool bVisible );
    // The same for the transform of a child placed without a name (HUD.vmf
    // /HUD/enter_hold places its label at depth 11 unnamed). Stage px,
    // degrees, percent, like setClipTransform.
    bool setInstanceTransform( const std::string &clipPath, uint16_t depth, float x, float y, float rotationDegrees,
                               float xScalePercent, float yScalePercent );
    // The same for the alpha of an unnamed child (the movie places the back on
    // track ring and its label with alpha 0; the retail exe fades them in).
    bool setInstanceAlpha( const std::string &clipPath, uint16_t depth, float alphaPercent );
    bool sendMouseRelease( float screenX, float screenY );
    // Stage coordinates (px) of a clip's world transform (parents included),
    // e.g. hit tests against the mouse: y 120..840 is the visible 16:9 band.
    bool getClipStagePosition( const std::string &clipPath, float &outX, float &outY ) const;
    // Body range of an AVM1 user function by index (a clip variable of type
    // Function carries the index): the frontend debug dumps disassemble these
    // bodies. False when the index is out of range.
    bool getFunctionBody( int32_t index, uint32_t &outOffset, uint32_t &outLength ) const;
    // Constant pool of an AVM1 user function (the debug dumps resolve the Push
    // constant indices). Owned by the player: do not keep the pointer.
    const std::vector<std::string> *getFunctionConstants( int32_t index ) const;

private:
    struct Function {
        uint32_t Offset = 0;
        uint32_t Length = 0;
        std::vector<std::string> Parameters;
        uint8_t NumRegisters = 4;
        std::vector<std::pair<uint8_t, std::string>> RegisterParameters; // DefineFunction2
        uint16_t Flags = 0;
        std::vector<std::string> Constants; // constant pool at definition
        std::string Name;                   // -logflashcalls
    };

    struct PendingAction {
        Clip *pClip;
        uint32_t Offset;
        uint32_t Length;
    };

    FlashMovie *pMovie;
    FlashDefinitions definitions;
    Clip root;
    // Destination rect of the stage (setStageRect): the screen ratio rect of
    // beginStage unless the caller set one.
    float stageRectOverride[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    bool bStageRect = false;
    float frameTime;
    float accumulator;
    std::vector<PendingAction> pendingActions;
    std::vector<std::unique_ptr<Instance>> graveyard;
    std::vector<Function> functions;
    std::map<std::string, Value, CaseInsensitiveLess> globals;
    FSCommandCallback fsCommandCallback;
    int32_t actionDepth;
    uint32_t maskLevel = 0; // nesting of the masks being drawn
    uint32_t maskDrawing = 0;

private:
    // Timeline
    void advanceClip( Clip &clip );
    void gotoFrame( Clip &clip, uint32_t frame, bool bQueueActions );
    void applyControlTags( Clip &clip, uint32_t frame, bool bQueueActions, bool bQueueChildActions = false );
    void placeObject( Clip &clip, const FlashPlaceInfo &place, bool bQueueActions );
    void removeObject( Clip &clip, uint16_t depth );
    static constexpr int32_t kScriptDepthBase = 0x4000;
    Value attachMovie( Clip &clip, const std::string &linkage, const std::string &name, int32_t depth );
    // duplicateMovieClip: a new instance of the source clip's character in the
    // same parent, at a script depth, with the source's matrix and colour.
    Value duplicateClip( Clip &source, const std::string &name, int32_t depth );
    Value duplicateClipIn( Clip &source, Clip *pParentOverride, const std::string &name, int32_t depth );
    void initClip( Instance &instance, Clip *pParent, bool bQueueActions );
    void registerTextBitmap( const Instance &instance );
    // Re-points the button glyph sprites below a clip to the prompt
    // platform's frame (keeping a '_locked' variant).
    void refreshPlatformGlyphs( Clip &clip );
    void runPendingActions();
    bool resolveFrame( Clip &clip, const Value &frame, uint32_t &outFrame );

    // ActionScript
    Value execute( Clip *pClip, uint32_t offset, uint32_t length, std::map<std::string, Value> *pLocals,
                   std::vector<Value> *pRegisters, Value *pThis,
                   const std::vector<std::string> *pConstants = nullptr );
    Value callFunction( int32_t index, Clip *pClip, const std::vector<Value> &args, Value *pThis );
    Value getMember( const Value &object, const std::string &name );
    void setMember( Value &object, const std::string &name, const Value &value );
    Value getProperty( Clip *pClip, int32_t index );
    // MovieClip _width / _height: the content bounds of a clip.
    void accumulateBounds( const Clip &clip, const FlashMatrix &matrix, float ( &bounds )[4], bool &bAny ) const;
    bool getClipExtent( const Clip &clip, float &outWidth, float &outHeight ) const;
    void setProperty( Clip *pClip, int32_t index, const Value &value );
    Value lookupVariable( Clip *pClip, const std::string &name, std::map<std::string, Value> *pLocals );
    void assignVariable( Clip *pClip, const std::string &name, const Value &value,
                         std::map<std::string, Value> *pLocals );
    Clip *resolveTarget( Clip *pClip, const std::string &target );
    Value callMethod( Value &object, const std::string &name, const std::vector<Value> &args, Clip *pClip );

    bool sendKeyToClip( Clip &clip, uint8_t swfKey );
    bool sendMouseToClip( Clip &clip, const FlashMatrix &matrix, float stageX, float stageY );
    void stageRect( float &x0, float &y0, float &width, float &height ) const;

    // Rendering
    void beginStage( Renderer2D &renderer ) const;
    void drawClip( Renderer2D &renderer, const Clip &clip, const FlashMatrix &matrix,
                   const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    // The clip whose display list is being drawn (text field variables
    // resolve from it).
    mutable const Clip *pDrawOwner = nullptr;
    void drawInstance( Renderer2D &renderer, const Instance &instance, const FlashMatrix &matrix,
                       const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    const Clip *pShapeOverrideOwner = nullptr; // clip whose override the shape being drawn uses
    void drawShape( Renderer2D &renderer, const FlashShape &shape, const FlashMatrix &matrix,
                    const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas );
    void drawEditText( Renderer2D &renderer, const FlashEditText &text, const Clip &owner, const FlashMatrix &matrix,
                       const FlashColorTransform &colorTransform, const FlashMatrix &stageToCanvas,
                       const std::string &instanceName );
    const Font *findFont( uint16_t fontId ) const;
};

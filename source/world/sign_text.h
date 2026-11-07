#pragma once

#include "core/math.h"
#include "render/font.h"

#include <cstdint>
#include <string>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Render3DG;
struct BankEntry;
struct GPUTexture;

// Decoded row of a road-sign face (the type-4 payload of the A .pmi lists,
// grammar in docs/re/sign_text_spec.md 2).
struct SignTextRow {
    std::string Name;
    std::string Distance; // digits or empty
    // The raw distance byte (0 when the row has none). On the TdirIb plates it
    // is not a distance but the row's arrow: 0x26..0x29 = 11 up, 12 left,
    // 13 down, 14 right (docs/re/sign_text_spec.md 6).
    uint8_t DistanceCode = 0;
    // The row's pictogram byte (0x48 harbour, 0x49 old town, 0x4a airport,
    // 0x4b beach; spec 5.1), 0 when the row has none.
    uint8_t Icon = 0;
};

// One text zone of a direction-sign plate: the atlas rectangle the plate's
// quads sample (rs_maps UV space) plus the plate's model-space top (rows
// read top down). Built from the actor .3dg, one per plate variant.
struct SignQuadLayout {
    float U0 = 0.0f, V0 = 0.0f, U1 = 0.0f, V1 = 0.0f;
    float Top = 0.0f;    // model-space Y of the plate top (bigger = higher)
    float Bottom = 0.0f; // model-space Y of the plate bottom (the plate's
                         // vertex span [Bottom, Top] tells stacked plates apart)
    // How the family's plate quads sample the atlas rect, measured on the
    // model vertices (docs/re/sign_text_spec.md 4.9): rs_dir* and the town
    // plates run their u against the plate's reading direction (the stamp is
    // mirrored to compensate), the dist/tdirib/hwdist plates run with it; the
    // town plates alone also read their v bottom up (the plate's top edge
    // samples the rect's high v), so their stamp flips vertically too.
    bool FlipU = true;
    bool FlipV = false;
    // The atlas v band the plate sampled before RemapStackedPlateRows moved
    // it onto a spare bar (== V0/V1 when it was not moved). The composer
    // copies that source band over the spare bar, so a moved plate keeps its
    // own look (outline, yellow arrow tip) instead of the spare bar's.
    float SrcV0 = 0.0f, SrcV1 = 0.0f;
    // A folded plate (the rs_dist green header samples its cell from both
    // ends to the middle, u up to FoldU): RemapStackedPlateRows unfolds the
    // mirrored half (the vertices past FoldX on the bFoldPositiveX side) and
    // widens U1; the composer mirror-copies the cell past FoldU.
    // A rotated cell (the rs_hwdist panel): reading right runs +v, reading up
    // runs +u; the composer stamps its rows turned 90 degrees.
    bool bRotated = false;
    bool bFolded = false;
    bool bFoldPositiveX = true;
    float FoldU = 0.0f;
    float FoldX = 0.0f;
};

// Which actor family a layout was built for (the model-name prefix; decides
// the stamp orientation above and whether the payload's distance bytes are
// drawn - the retail photos show them only on the distance plates).
enum class SignFamily : uint8_t {
    Unknown,
    Dir,  // rs_dir* (rs_dirl/rs_dirr)
    Dist, // rs_dist*
    Tdir, // rs_tdirib*
    Hw,   // rs_hwdist*
    Twn,  // rs_twnin/rs_twnout
};

struct SignLayout {
    bool bValid = false;
    std::vector<SignQuadLayout> Quads; // text zones, top plate first
    bool bDistanceRows = false;        // stamp the rows' distance bytes
    SignFamily Family = SignFamily::Unknown;
};

// Extracts the plate text zones of a road-sign .3dg (worldactors bank).
SignLayout BuildSignLayout( const uint8_t *pData, uint32_t size, SignFamily family = SignFamily::Unknown );

// Several sign models draw more than one physical plate with the SAME atlas
// cell (rs_tdirib_b/c and rs_dirl_d stack their rows on one blank bar). Retail
// must have rewritten the UVs per plate; this gives each stacked plate its own
// blank bar of rs_maps and rewrites the geometry's texture coordinates to
// match, updating the layout's rectangles (the caller composes the face from
// the updated layout). No-op for models whose plates already use distinct
// cells.
void RemapStackedPlateRows( Render3DG &geometry, SignLayout &layout );

// Direction-sign face composer (docs/re/sign_text_spec.md 4): the retail
// plates are baked blank in the rs_maps atlas and the sign text travels in
// the payload the original renderer stamped onto a per-sign face texture.
// This cache composes those faces (full 1024 x 1024 atlas copy with the text
// stamped into the blank plate bands, CPU-encoded DXT1 + mips, same sRGB
// format as the original) and hands them to EdenWorld::drawInstances for the
// per-instance texture swap.
class SignFaceCache {
public:
    // pRsMapsEntry: the rs_maps.2db of commonworld.bnk (kept alive by the
    // world's bank). The font + its glyph page pixels come from the frontend
    // (GSFlash main movie); both are optional - without them faces are not
    // composed and signs draw as before.
    void initialize( const BankEntry *pRsMapsEntry );
    void destroy();

    // Decodes the payload into rows and returns a stable hash of its bytes
    // (0 when the payload is empty). The bytes are only read here - callers
    // keep none.
    uint64_t decodePayload( const uint8_t *pPayload, uint32_t size, std::vector<SignTextRow> &outRows ) const;

    // Composed face for a payload hash + rows + the variant's plate layout
    // (built once per actor model by EdenWorld). Composes on first use,
    // LRU-capped.
    GPUTexture *
    getFace( uint64_t payloadHash, const std::vector<SignTextRow> &rows, char variant, const SignLayout &layout );

    inline bool isEnabled() const { return bEnabled; }
    inline size_t getNumFaces() const { return faces.size(); }
    inline uint32_t getTotalComposed() const { return totalComposed; }

private:
    struct Face {
        GPUTexture *pTexture = nullptr;
        uint64_t lastUse = 0;
        std::vector<SignTextRow> Rows;
        char Variant = 0;
        SignLayout Layout;
    };

    // compose = composeEncoded (the CPU work: atlas copy, text stamp, DXT1 +
    // mips; touches no cache state, safe on the worker) + upload (main thread).
    void compose( Face &face, uint64_t payloadHash );
    void composeEncoded( const Face &face, uint64_t payloadHash, std::vector<uint8_t> &encoded ) const;
    void upload( Face &face, uint64_t payloadHash, const std::vector<uint8_t> &encoded );
    GPUTexture *insertFace( uint64_t key, Face &&face );
    void ensureAtlasBase();
    // White coverage pixels of the frontend font page (black on the plates).
    bool ensureFontPage();

    const BankEntry *pRsMapsEntry = nullptr;
    bool bEnabled = false;
    bool bLoggedFontFailure = false;
    // rs_maps mip 0 decoded to RGBA8 once (4 MB) - the base every face copies.
    std::vector<uint8_t> atlasBase;
    // The sign lettering: the gauges movie's "Arial" (the closest of the
    // shipped fonts to the retail sign face), else the frontend "Berlin Sans
    // FB". A copy: the worker reads its metrics after the movie may be gone.
    Font fontCopy;
    const Font *pFont = nullptr;
    std::vector<uint8_t> fontPage;
    uint32_t fontPageWidth = 0;
    uint32_t fontPageHeight = 0;

    std::unordered_map<uint64_t, Face> faces;
    uint64_t faceClock = 0;
    uint32_t totalComposed = 0;

    // Background composition (2026-11-04): a face took ~20 ms of the main
    // thread (4 MB atlas copy + DXT1 of the 1024 chain), the 33-40 ms frames
    // every few seconds of a drive. A missing face is queued and the sign
    // draws its blank plate until the worker's result is uploaded (a frame or
    // two later, at sign distance). -syncsignfaces = compose inline (the A/B).
    struct ComposeJob {
        uint64_t Key = 0;
        uint64_t PayloadHash = 0;
        Face FaceData;
        std::vector<uint8_t> Encoded;
    };
    void workerLoop();
    void collectFinished();
    void stopWorker();
    std::thread worker;
    std::mutex jobMutex;
    std::condition_variable jobSignal;
    std::deque<ComposeJob> jobs;
    std::vector<ComposeJob> finished;
    std::atomic<uint32_t> numFinished { 0 };
    bool bStopWorker = false;
    std::unordered_set<uint64_t> pendingKeys;
};

bool IsSignTextEnabled();
bool IsSignTextLogging();

#pragma once

#include "render/shaders/flash_shader_pipeline.h"
#include "render/vulkan/render_device.h"

class Font;
struct ImDrawData;

struct Rect2D {
    float X = 0.0f, Y = 0.0f, Width = 0.0f, Height = 0.0f;
    Rect2D() = default;
    constexpr Rect2D( float x, float y, float w, float h )
        : X( x ), Y( y ), Width( w ), Height( h ) {}
    bool contains( float px, float py ) const {
        return px >= X && py >= Y && px <= X + Width && py <= Y + Height;
    }
};

enum class eTextAlign : uint8_t { TA_Left = 0, TA_Center, TA_Right };

struct Vertex2D {
    float X, Y;
    float U, V;
    uint32_t Color; // RGBA8
};
OTDU_SIZE_MUST_MATCH( Vertex2D, 20 );

// Immediate mode 2D batcher. Coordinates are expressed in a virtual canvas
// (1280x720 by default, i.e. the TDU2 frontend authoring resolution) scaled
// to the backbuffer while preserving the aspect ratio.
class Renderer2D {
public:
    inline float getCanvasWidth() const { return canvasWidth; }
    inline float getCanvasHeight() const { return canvasHeight; }

public:
    Renderer2D();
    ~Renderer2D();

    bool initialize( RenderDevice *pDevice, GPUTexture *pWhiteTexture );
    void destroy();

    // bCover: scale to the backbuffer width (crop vertically) instead of
    // letterboxing (used by the flash stage, 4:3 authored).
    void begin( float virtualWidth = 1280.0f, float virtualHeight = 720.0f, bool bCover = false );
    // The size of the surface being drawn: the backbuffer, or an offscreen
    // render target (a flash movie drawn into a texture: the slot machines'
    // screens). 0 x 0 = the backbuffer.
    void setTargetSize( uint32_t width, uint32_t height ) {
        overrideWidth = width;
        overrideHeight = height;
    }
    float getTargetWidth() const { return static_cast<float>( targetWidth() ); }
    float getTargetHeight() const { return static_cast<float>( targetHeight() ); }
    // Canvas mapped onto a normalized rectangle of the backbuffer (edFlash
    // player destination rect, FUN_008424d0): x0, y0, width, height.
    void beginRect( float virtualWidth, float virtualHeight, float x0, float y0, float width, float height );
    void end();
    // Stencil masking of the following draws (flash ClipDepth layers).
    void setStencil( eStencilMode mode, uint32_t reference );
    // Scissor clip of the following draws, in canvas coordinates (the minimap
    // clips its road triangles on the GPU instead of on the CPU). Flushes the
    // batch that is already queued; clearClipRect() flushes too.
    void setClipRect( const Rect2D &rect );
    void clearClipRect();

    void drawQuad( GPUTexture *pTexture,
                   const Rect2D &destination,
                   const Rect2D &uv,
                   uint32_t color );
    void drawQuad( GPUTexture *pTexture, const Rect2D &destination, uint32_t color ) {
        drawQuad( pTexture, destination, Rect2D( 0, 0, 1, 1 ), color );
    }
    void drawRect( const Rect2D &destination, uint32_t color );
    // Triangle list in canvas coordinates (pTexture nullptr = white).
    // bFontAtlas: glyph atlas (alpha from the red channel, TD_Flash PS #7).
    void drawTriangles( GPUTexture *pTexture, const Vertex2D *pVertices, uint32_t numVertices,
                        bool bFontAtlas = false );
    void drawGradient( const Rect2D &destination, uint32_t colorTop, uint32_t colorBottom );
    void drawRectOutline( const Rect2D &destination, float thickness, uint32_t color );

    void drawText( const Font *pFont,
                   const char *pText,
                   float x,
                   float y,
                   float size,
                   uint32_t color,
                   eTextAlign align = eTextAlign::TA_Left,
                   float letterSpacing = 0.0f );

    void setSaturation( float saturation ) { currentSaturation = saturation; }
    // True when the 2D layer is drawn with the game's TD_Flash.sob shaders.
    bool usesOriginalShaders() const { return flashPipeline.isReady(); }

    // Draws Dear ImGui (in backbuffer pixels).
    void drawImGui( ImDrawData *pDrawData, GPUTexture *pFontTexture );

    // Converts backbuffer pixels to canvas coordinates (mouse picking).
    void screenToCanvas( float screenX, float screenY, float &outX, float &outY ) const;

private:
    uint32_t overrideWidth = 0, overrideHeight = 0;
    uint32_t targetWidth() const;
    uint32_t targetHeight() const;
    RenderDevice *pDevice;
    GPUTexture *pWhite;
    VkPipeline pipeline;
    VkPipeline stencilPipelines[static_cast<size_t>( eStencilMode::SM_Count )] = {};
    eStencilMode currentStencil = eStencilMode::SM_None;
    uint32_t currentStencilReference = 0;
    FlashShaderPipeline flashPipeline;
    std::vector<Vertex2D> vertices;
    GPUTexture *pCurrentTexture;
    bool bCurrentAlphaOnly;
    float currentSaturation;
    float canvasWidth;
    float canvasHeight;
    float scale;
    float scaleY;
    float offsetX;
    float offsetY;
    bool bClipRect;
    Rect2D clipRect;

private:
    void flush();
    void pushQuad( float x0, float y0, float x1, float y1,
                   float u0, float v0, float u1, float v1,
                   uint32_t colorTop, uint32_t colorBottom );
    void setTexture( GPUTexture *pTexture, bool bAlphaOnly );
    void recordDraw( const Vertex2D *pVertices, uint32_t numVertices,
                     const uint16_t *pIndices, uint32_t numIndices,
                     GPUTexture *pTexture, bool bAlphaOnly,
                     const float *pProjection, const VkRect2D &scissor );
};

inline constexpr uint32_t MakeRGBA( uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255 ) {
    return static_cast<uint32_t>( r ) | ( static_cast<uint32_t>( g ) << 8 ) |
           ( static_cast<uint32_t>( b ) << 16 ) | ( static_cast<uint32_t>( a ) << 24 );
}

inline uint32_t MultiplyAlpha( uint32_t color, float alpha ) {
    uint32_t a = static_cast<uint32_t>( ( color >> 24 ) * Clamp( alpha, 0.0f, 1.0f ) );
    return ( color & 0x00ffffff ) | ( a << 24 );
}

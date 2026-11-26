#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Tattoo compositor (new feature, not in the original; docs/re/tattoo_spec.md).
// The retail game composites the skin texture at runtime (avatar_spec.md 6.3,
// SKIN slot -> per-avatar render target) and its avatar profile reserved a
// tattoo input. The compositing pass is not decoded, so OpenTDU2 does its own:
// a decal is an alpha mask placed in the SKIN texture's UV space, blended over
// a CPU copy of the body's base skin texture, uploaded once per avatar load.
struct TattooDecal {
    std::string Design;    // "star", "ring", "cross", "bars", "tribal", or <userdata>/tattoos/<name>.png
    float U = 0.5f;        // centre in the skin texture, 0..1
    float V = 0.5f;        // 0 = top
    float Scale = 0.1f;    // half size, as a fraction of the texture width
    float Rotation = 0.0f; // degrees
    float Opacity = 0.9f;
    uint32_t Color = 0x181818; // 0xRRGGBB ink
    bool Head = false;         // on the head skin texture (face, neck, ears) instead of the body's
};

// Mask value 0..1 of a design at local coordinates x, y in -1..1 (< 0: unknown design).
float TattooMask( const std::string &design, float x, float y );

// Blends the decals into an RGBA8 image (top-down).
void CompositeTattoos( uint8_t *pRGBA, uint32_t width, uint32_t height, const std::vector<TattooDecal> &decals );

// "design,u,v,scale,rot,opacity,rrggbb;..." (the profile's tattoos line).
std::string SerializeTattoos( const std::vector<TattooDecal> &decals );
std::vector<TattooDecal> ParseTattoos( const std::string &text );

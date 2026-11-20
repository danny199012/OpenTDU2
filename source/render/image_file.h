#pragma once

#include <filesystem>

// Loose image files: the custom radio stations' icons. PNG / JPG / BMP / TGA /
// GIF through stb_image (the same dependency the PNG screenshots use) and DDS
// through the Eden DXT decoder (render/2db.h) — the format the game's own
// textures are authored in.
//
// outPixels: RGBA8, width * height * 4 bytes, top-down.
bool LoadImageFileRGBA( const std::filesystem::path &path,
                        std::vector<uint8_t> &outPixels,
                        uint32_t &outWidth,
                        uint32_t &outHeight );

// Writes RGBA8 (top-down) as a PNG (stb_image_write; the implementation lives
// in render_device.cpp's translation unit). False on I/O failure. The tools
// use it to generate their own art (mod_roads' default asphalt).
bool SaveImagePng( const std::filesystem::path &path, const uint8_t *pRGBA, int32_t width, int32_t height );

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

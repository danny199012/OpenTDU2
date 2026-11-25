#include "shared.h"
#include "licence_photo.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/image_file.h"
#include "system/cmd_line.h"
#include <filesystem>
#include <vector>

namespace LicencePhoto {

static GPUTexture *spTexture = nullptr;
// The texture before the current one: the Flash player holds the pointer as a bitmap override and the consumers
// rebind it on their own schedule (the pause menu each frame, the school screen on entry), so a replaced texture is
// freed one reload later, not at once.
static GPUTexture *spPrevious = nullptr;
static bool sbStale = true;

std::string Path() {
    if ( gpFile == nullptr ) {
        return std::string();
    }
    return ( gpFile->getUserDataFolder() / "Licence00.png" ).string();
}

bool Exists() {
    const std::string path = Path();
    std::error_code error;
    return !path.empty() && std::filesystem::exists( path, error );
}

bool Capture() {
    const std::string path = Path();
    if ( path.empty() || gpRender == nullptr ) {
        return false;
    }
    gpRender->getDevice().saveScreenshot( path.c_str() );
    sbStale = true;
    OTDU_LOG_INFO( "LicencePhoto: frame -> '%s'\n", path.c_str() );
    return true;
}

void Invalidate() {
    sbStale = true;
}

GPUTexture *GetTexture() {
    // -testlicencephoto N: the first N calls reload the photo (the leak check of the replaced textures).
    static int32_t numForced = 0;
    if ( numForced < CmdLine::GetArgInt( "-testlicencephoto", 0 ) ) {
        numForced++;
        sbStale = true;
    }
    if ( !sbStale ) {
        return spTexture;
    }
    if ( !Exists() || gpRender == nullptr ) {
        return spTexture;
    }
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    if ( !LoadImageFileRGBA( Path(), pixels, width, height ) ) {
        return spTexture; // not written yet: try again next call
    }
    // createFromRGBA hands the texture to us (the cache does not own it): the one from two photos ago is freed now,
    // the previous one stays until the next reload (see spPrevious).
    if ( spPrevious != nullptr ) {
        gpRender->getDevice().destroyTexture( spPrevious );
    }
    spPrevious = spTexture;
    spTexture = gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(), "LicencePhoto" );
    sbStale = false;
    OTDU_LOG_INFO( "LicencePhoto: loaded %ux%u\n", width, height );
    return spTexture;
}

} // namespace LicencePhoto

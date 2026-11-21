#include "shared.h"
#include "licence_photo.h"
#include "filesystem/gs_file.h"
#include "render/gs_render.h"
#include "render/image_file.h"
#include <filesystem>
#include <vector>

namespace LicencePhoto {

static GPUTexture *spTexture = nullptr;
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
    // The old texture is left to the cache (a few photos at most per session).
    spTexture = gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(), "LicencePhoto" );
    sbStale = false;
    OTDU_LOG_INFO( "LicencePhoto: loaded %ux%u\n", width, height );
    return spTexture;
}

} // namespace LicencePhoto

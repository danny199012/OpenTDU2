#include "launcher_artwork.h"

#include "launcher_util.h"

#include <windows.h>
#include <wincodec.h>

#include <vector>

namespace Launcher {

namespace {

bool DecodePngFromResource( int resourceId, std::vector<uint8_t> &outPixels, int &outWidth,
                            int &outHeight, std::string &outError ) {
    HRSRC resource = FindResourceW( nullptr, MAKEINTRESOURCEW( resourceId ),
                                    MAKEINTRESOURCEW( 10 ) ); // RT_RCDATA
    if ( resource == nullptr ) {
        outError = "artwork resource " + std::to_string( resourceId ) + " is missing";
        return false;
    }
    const DWORD size = SizeofResource( nullptr, resource );
    HGLOBAL handle = LoadResource( nullptr, resource );
    const void *data = handle != nullptr ? LockResource( handle ) : nullptr;
    if ( data == nullptr || size == 0 ) {
        outError = "artwork resource " + std::to_string( resourceId ) + " is empty";
        return false;
    }

    IWICImagingFactory *factory = nullptr;
    HRESULT hr = CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS( &factory ) );
    if ( FAILED( hr ) || factory == nullptr ) {
        outError = "WIC is unavailable";
        return false;
    }

    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;

    hr = factory->CreateStream( &stream );
    if ( SUCCEEDED( hr ) ) {
        hr = stream->InitializeFromMemory( const_cast<BYTE *>( static_cast<const BYTE *>( data ) ),
                                           size );
    }
    if ( SUCCEEDED( hr ) ) {
        hr = factory->CreateDecoderFromStream( stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                               &decoder );
    }
    if ( SUCCEEDED( hr ) ) {
        hr = decoder->GetFrame( 0, &frame );
    }
    if ( SUCCEEDED( hr ) ) {
        hr = factory->CreateFormatConverter( &converter );
    }
    if ( SUCCEEDED( hr ) ) {
        hr = converter->Initialize( frame, GUID_WICPixelFormat32bppRGBA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom );
    }
    UINT width = 0;
    UINT height = 0;
    if ( SUCCEEDED( hr ) ) {
        hr = converter->GetSize( &width, &height );
    }
    if ( SUCCEEDED( hr ) && width > 0 && height > 0 ) {
        const UINT stride = width * 4;
        const UINT bufferSize = stride * height;
        outPixels.resize( bufferSize );
        hr = converter->CopyPixels( nullptr, stride, bufferSize, outPixels.data() );
        if ( SUCCEEDED( hr ) ) {
            outWidth = static_cast<int>( width );
            outHeight = static_cast<int>( height );
        }
    }

    if ( converter != nullptr ) {
        converter->Release();
    }
    if ( frame != nullptr ) {
        frame->Release();
    }
    if ( decoder != nullptr ) {
        decoder->Release();
    }
    if ( stream != nullptr ) {
        stream->Release();
    }
    factory->Release();

    if ( FAILED( hr ) ) {
        outError = "cannot decode artwork resource " + std::to_string( resourceId );
        return false;
    }
    return true;
}

} // namespace

void ArtworkImage::Release() {
    if ( View != nullptr ) {
        View->Release();
        View = nullptr;
    }
    Width = 0;
    Height = 0;
}

void Artwork::Release() {
    Play.Release();
    GameData.Release();
    Updates.Release();
    Player.Release();
    Online.Release();
    Banner.Release();
}

ArtworkImage &Artwork::ForResource( int resourceId ) {
    switch ( resourceId ) {
    case kArtPlay: return Play;
    case kArtGameData: return GameData;
    case kArtUpdates: return Updates;
    case kArtPlayer: return Player;
    case kArtOnline: return Online;
    default: return Banner;
    }
}

bool LoadArtwork( ID3D11Device *device, Artwork &artwork ) {
    if ( device == nullptr ) {
        return false;
    }
    const int ids[] = { kArtPlay, kArtGameData, kArtUpdates, kArtPlayer, kArtOnline, kArtBanner };
    for ( const int id : ids ) {
        std::vector<uint8_t> pixels;
        int width = 0;
        int height = 0;
        std::string error;
        if ( !DecodePngFromResource( id, pixels, width, height, error ) ) {
            artwork.Error = error;
            continue;
        }
        D3D11_TEXTURE2D_DESC description = {};
        description.Width = static_cast<UINT>( width );
        description.Height = static_cast<UINT>( height );
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial = {};
        initial.pSysMem = pixels.data();
        initial.SysMemPitch = static_cast<UINT>( width ) * 4;

        ID3D11Texture2D *texture = nullptr;
        HRESULT hr = device->CreateTexture2D( &description, &initial, &texture );
        if ( FAILED( hr ) || texture == nullptr ) {
            continue;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription = {};
        viewDescription.Format = description.Format;
        viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDescription.Texture2D.MipLevels = 1;

        ArtworkImage &image = artwork.ForResource( id );
        image.Release();
        hr = device->CreateShaderResourceView( texture, &viewDescription, &image.View );
        texture->Release();
        if ( SUCCEEDED( hr ) ) {
            image.Width = width;
            image.Height = height;
        }
    }
    return artwork.Play.IsValid() || artwork.Banner.IsValid();
}

} // namespace Launcher

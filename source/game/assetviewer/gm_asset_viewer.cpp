#include "shared.h"
#include "gm_asset_viewer.h"

#include "audio/gs_audio.h"
#include "debug/gs_imgui.h"
#include "filesystem/gs_file.h"
#include "flash/flash_movie.h"
#include "flash/flash_player.h"
#include "game/asset_labels.h"
#include "game/cinematic/cinematic_scene.h"
#include "game/freeride/chase_camera.h"
#include "input/gs_input.h"
#include "render/eden/eden_scene_view.h"
#include "render/eden/eden_sky.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"
#include "core/watchdog.h"
#include "tdu_instance.h"

#include "imgui.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace {
    // The bank the viewer opens when no -bank is given and no label points
    // anywhere.
    constexpr const char *kDefaultBank = "euro/bnk/frontend/hires/casino.bnk";
    // The 3D view: the models of a bank are laid out in a row along X, this far
    // apart (m), and the turntable turns at this rate (rad/s) [INF: developer
    // tool values].
    constexpr float kSceneGap = 2.0f;
    constexpr float kTurntableSpeed = 0.35f;
    constexpr float kPi = 3.14159265f;
    // The 3D view render target (4:3, the image is fitted to the panel).
    constexpr uint32_t kSceneTargetWidth = 800;
    constexpr uint32_t kSceneTargetHeight = 600;
    // At most this many models of one bank are loaded (a sector bank can carry
    // a lot; the panel says when the list was cut).
    constexpr size_t kMaxSceneModels = 64;
    // `-pickscan`: the resolution of the ray grid it logs.
    constexpr int32_t kScanCols = 64;
    constexpr int32_t kScanRows = 32;

    std::string SizeText( uint32_t size ) {
        char text[32] = {};
        if ( size >= 1024u * 1024u ) {
            snprintf( text, sizeof( text ), "%.1f MB",
                      static_cast<double>( size ) / ( 1024.0 * 1024.0 ) );
        } else if ( size >= 1024u ) {
            snprintf( text, sizeof( text ), "%.1f KB",
                      static_cast<double>( size ) / 1024.0 );
        } else {
            snprintf( text, sizeof( text ), "%u B", size );
        }
        return text;
    }

    // The asset's stable identity in the label file, for the UI to show.
    std::string AssetKey( const std::string &bankKey, const BankEntry &entry ) {
        return bankKey + " / " + entry.Filename;
    }

    bool IsModelExtension( const std::string &extension ) {
        return extension == "3dd" || extension == "3dg" || extension == "2dm";
    }

    // The language copies of a bank ("<name>__fr.bnk"): hidden by default, they
    // roughly triple the bank list for interiors and frontend movies.
    bool IsLanguageBank( const std::string &path ) {
        static const char *kSuffixes[] = { "__fr", "__ge", "__it", "__sp",
                                           "__us", "__ja", "__pl", "__ru",
                                           "__uk", "__en", "__de" };
        const size_t dot = path.find_last_of( '.' );
        const std::string stem =
            dot == std::string::npos ? path : path.substr( 0, dot );
        for ( const char *pSuffix : kSuffixes ) {
            const size_t length = strlen( pSuffix );
            if ( stem.size() > length &&
                 stem.compare( stem.size() - length, length, pSuffix ) == 0 ) {
                return true;
            }
        }
        return false;
    }

    // Case insensitive substring search, no allocation: the index holds a few
    // hundred thousand lines and the search runs on every keystroke (pNeedle is
    // lowercase).
    bool ContainsI( const std::string &haystack, const char *pNeedle ) {
        const size_t needleLength = strlen( pNeedle );
        if ( needleLength == 0 ) {
            return true;
        }
        if ( needleLength > haystack.size() ) {
            return false;
        }
        for ( size_t i = 0; i + needleLength <= haystack.size(); i++ ) {
            size_t j = 0;
            while ( j < needleLength &&
                    tolower( static_cast<unsigned char>( haystack[i + j] ) ) ==
                        static_cast<unsigned char>( pNeedle[j] ) ) {
                j++;
            }
            if ( j == needleLength ) {
                return true;
            }
        }
        return false;
    }

    // "ArrayOf_Entity__Volume__FpSelectable" -> "Volume__FpSelectable".
    std::string EntityClassName( const std::string &arrayName ) {
        const std::string prefix = "ArrayOf_Entity";
        if ( arrayName.compare( 0, prefix.size(), prefix ) == 0 ) {
            std::string rest = arrayName.substr( prefix.size() );
            while ( !rest.empty() && rest[0] == '_' ) {
                rest.erase( rest.begin() );
            }
            return rest;
        }
        return arrayName;
    }

    // The asset list's type filter names (the buttons of the panel).
    const char *kTypeFilterNames[] = { "all",    "models",   "textures",
                                       "movies", "sounds",   "anims",
                                       "cine",   "collision" };

    // A cube map as the panel can show it: face 0, first mip (the faces are
    // face major: face 0 with its whole mip chain comes first).
    Texture2DB CubeFaceAs2D( const Texture2DB &cube ) {
        Texture2DB face = cube;
        face.Depth = 1;
        face.Flags &= ~0x40u;
        face.NumMips = 1;
        return face;
    }

    // The movie of a `FrontEnd/<Res>/<name>.bnk` bank: FlashMovie::load
    // resolves that layout, so a bank somewhere else cannot be previewed (it
    // would load a different bank).
    bool MovieBankName( const std::string &bankKey,
                        std::string &outName,
                        std::string &outResolution ) {
        const size_t slash = bankKey.find_last_of( '/' );
        const size_t folderEnd = slash == std::string::npos ? 0 : slash;
        const size_t folderStart =
            bankKey.find_last_of( '/', folderEnd == 0 ? 0 : folderEnd - 1 );
        const std::string folder = bankKey.substr(
            folderStart == std::string::npos ? 0 : folderStart + 1,
            folderEnd -
                ( folderStart == std::string::npos ? 0 : folderStart + 1 ) );
        if ( folder != "hires" && folder != "medres" && folder != "lowres" ) {
            return false;
        }
        outResolution = folder == "hires"
                            ? "HiRes"
                            : ( folder == "medres" ? "MedRes" : "LowRes" );
        std::string stem =
            slash == std::string::npos ? bankKey : bankKey.substr( slash + 1 );
        const size_t dot = stem.find_last_of( '.' );
        if ( dot != std::string::npos ) {
            stem = stem.substr( 0, dot );
        }
        outName = stem;
        return !outName.empty();
    }
} // namespace

GMAssetViewer::GMAssetViewer() : GameMode( eGameMode::GM_AssetViewer ) {}
GMAssetViewer::~GMAssetViewer() { exit(); }

bool GMAssetViewer::enter( TestDriveGameInstance *pInstance ) {
    GameMode::enter( pInstance );
    gpRender->setClearColor( ColorRGBA( 0.04f, 0.05f, 0.07f, 1.0f ) );

    // -labellint / -labellabels: the label file against the game data, then out
    // (a test hook: the verdict is in the log).
    if ( CmdLine::HasArg( "-labellint" ) ) {
        LintAssetLabels();
        pInstance->requestExit();
        return true;
    }
    if ( CmdLine::HasArg( "-labellabels" ) ) {
        for ( const AssetLabel &label : GetAssetLabels() ) {
            OTDU_LOG_INFO( "AssetLabels: %s\t%s\t%s\t%s\n", label.Bank.c_str(),
                           label.Asset.c_str(), label.Label.c_str(),
                           label.Note.c_str() );
        }
        OTDU_LOG_INFO( "AssetLabels: %zu label(s) listed\n",
                       GetAssetLabels().size() );
        pInstance->requestExit();
        return true;
    }

    // The 3D view (models) and the island its shared material library comes
    // from.
    bScene = !CmdLine::HasArg( "-noscene" );
    bTurntable = !CmdLine::HasArg( "-noturntable" );
    if ( const char *pIsland = CmdLine::GetArgValue( "-island" ) ) {
        island = ToLower( std::string( pIsland ) );
    }
    // -pickat x,y: pick a mesh at a pixel of the 3D view (a click, scriptable).
    if ( const char *pPick = CmdLine::GetArgValue( "-pickat" ) ) {
        if ( sscanf( pPick, "%f,%f", &pendingPick[0], &pendingPick[1] ) == 2 ) {
            bPickPending = true;
        }
    }
    bPickScan = CmdLine::HasArg( "-pickscan" );
    bPickCheck = CmdLine::HasArg( "-pickcheck" );
    // -buildindex: walk every bank once and write the cross-bank index, then
    // exit. A one-time job (minutes: it reads the directory of all 9,454
    // banks), what makes the Search tab usable.
    if ( CmdLine::HasArg( "-buildindex" ) ) {
        scanBanks(); // the index walks the bank list
        buildAssetIndex();
        pInstance->requestExit();
        return true;
    }
    // The light of the preview scene: the island's game weathers (what
    // WORLD_VIEWER uses, so a model previews under the same sun).
    if ( gpFile != nullptr ) {
        weathers.load( ( gpFile->getBankFolder() + "islands/" + island +
                         "/fx/game_weathers.xmb" )
                           .c_str() );
        pWeather = weathers.find( "14" );
        if ( pWeather == nullptr && !weathers.getPresets().empty() ) {
            pWeather = &weathers.getPresets().front();
        }
    }

    scanBanks();
    refreshBankLabelCounts();

    // -bank <path>: open it directly, else the first bank a label points at (so
    // the viewer starts on something already named), else the casino shell.
    std::string start = kDefaultBank;
    if ( const char *pBank = CmdLine::GetArgValue( "-bank" ) ) {
        start = NormalizeAssetBank( pBank );
    } else if ( !GetAssetLabels().empty() ) {
        start = GetAssetLabels().front().Bank;
    }
    openBank( start );

    // -asset <filename>: select it, so a run can open on a specific asset (and
    // a screenshot of the preview can be scripted).
    if ( const char *pAsset = CmdLine::GetArgValue( "-asset" ) ) {
        for ( int32_t i = 0; i < static_cast<int32_t>( rows.size() ); i++ ) {
            if ( StringCmpI( rows[i]->Filename.c_str(), pAsset ) == 0 ) {
                selectAsset( i );
                focusScene( i );
                break;
            }
        }
    }
    // -label <text> + -note <text> + -savelabel: write the label of the
    // selected asset (with -pickat: the label of the asset the click resolved
    // to). A test hook that drives the whole click-to-label flow of the panel.
    // The texts are applied when the selection has settled (a pick clears the
    // editor).
    if ( const char *pLabel = CmdLine::GetArgValue( "-label" ) ) {
        pendingLabel = pLabel;
    }
    if ( const char *pNote = CmdLine::GetArgValue( "-note" ) ) {
        pendingNote = pNote;
    }
    bSaveLabelPending = CmdLine::HasArg( "-savelabel" );
    // -search <text>: run a global search at startup (the Search tab).
    if ( const char *pSearch = CmdLine::GetArgValue( "-search" ) ) {
        snprintf( searchText, sizeof( searchText ), "%s", pSearch );
        middleTab = 2;
        requestedTab = 2;
        loadAssetIndex();
        runSearch();
    }
    // -entity <name>: open the Entities tab on one entity (its fields and the
    // 3D marker), -entitytab: just open the tab.
    if ( CmdLine::HasArg( "-entitytab" ) ) {
        middleTab = 1;
        requestedTab = 1;
    }
    if ( const char *pEntity = CmdLine::GetArgValue( "-entity" ) ) {
        middleTab = 1;
        requestedTab = 1;
        for ( int32_t i = 0; i < static_cast<int32_t>( entities.size() );
              i++ ) {
            if ( StringCmpI( entities[i].Name.c_str(), pEntity ) == 0 ) {
                selectedEntity = i;
                OTDU_LOG_INFO(
                    "AssetViewer: entity '%s' (%s) at (%.2f, %.2f, %.2f)\n",
                    entities[i].Name.c_str(), entities[i].Class.c_str(),
                    entities[i].Position.x, entities[i].Position.y,
                    entities[i].Position.z );
                break;
            }
        }
    }
    // -playaudio: play the selected sound (a `wav` asset), so an automated run
    // covers the audio preview too.
    if ( CmdLine::HasArg( "-playaudio" ) ) {
        toggleAudioPreview();
    }
    // -camera <name>: start in one of the bank's entity cameras (an interior
    // previews as a box seen from outside otherwise). -collision: draw the
    // collision wireframe at startup.
    if ( const char *pCamera = CmdLine::GetArgValue( "-camera" ) ) {
        for ( int32_t i = 0; i < static_cast<int32_t>( cameraNames.size() );
              i++ ) {
            if ( StringCmpI( cameraNames[i].c_str(), pCamera ) == 0 ) {
                useEntityCamera( i );
                break;
            }
        }
    }
    if ( CmdLine::HasArg( "-collision" ) && bCollisionLoaded ) {
        bShowCollision = true;
    }
    return true;
}

void GMAssetViewer::exit() {
    unloadMoviePreview();
    stopAudioPreview();
    audioClip.reset();
    unloadSceneModels();
    if ( pSceneTarget != nullptr && gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
        gpRender->getDevice().destroyRenderTarget( pSceneTarget );
        pSceneTarget = nullptr;
    }
    if ( bLibraryLoaded && gpRender != nullptr ) {
        gpRender->getDevice().waitIdle();
        gpRender->getTextureCache().releaseOwner(
            libraryBank.getPath().c_str() );
        libraryBank.unload();
        bLibraryLoaded = false;
    }
    bank.unload();
    pPreviewTexture = nullptr;
}

void GMAssetViewer::scanBanks() {
    banks.clear();
    if ( gpFile == nullptr ) {
        return;
    }
    const std::filesystem::path root = gpFile->getGameRoot();
    const auto bankRoot = gpFile->resolvePath( "Euro/Bnk" );
    if ( !bankRoot.has_value() ) {
        OTDU_LOG_WARN( "AssetViewer: no Euro/Bnk under '%s'\n",
                       root.string().c_str() );
        return;
    }
    std::error_code ec;
    for ( std::filesystem::recursive_directory_iterator it( *bankRoot, ec ),
          end;
          it != end; it.increment( ec ) ) {
        if ( ec ) {
            break;
        }
        std::error_code fileEc;
        if ( !it->is_regular_file( fileEc ) || fileEc ) {
            continue;
        }
        if ( ToLower( it->path().extension().string() ) != ".bnk" ) {
            continue;
        }
        BankInfo info;
        std::error_code relEc;
        const std::filesystem::path rel =
            std::filesystem::relative( it->path(), root, relEc );
        info.Path = NormalizeAssetBank(
            ( relEc ? it->path().string() : rel.string() ).c_str() );
        info.Label = it->path().string();
        std::error_code sizeEc;
        info.Size = static_cast<uint32_t>( it->file_size( sizeEc ) );
        banks.push_back( info );
        // The scan walks every bank directory on the main thread (seconds to
        // a minute): beat the watchdog's kill guard (source/core/watchdog.h).
        if ( ( banks.size() & 0x7f ) == 1 ) {
            Watchdog::Beat( "assetviewer bank scan", banks.size() );
        }
    }
    std::sort( banks.begin(), banks.end(),
               []( const BankInfo &a, const BankInfo &b ) {
                   return a.Path < b.Path;
               } );
    OTDU_LOG_INFO( "AssetViewer: %zu bank(s) under '%s'\n", banks.size(),
                   bankRoot->string().c_str() );
}

bool GMAssetViewer::openBank( const std::string &path ) {
    if ( path.empty() ) {
        return false;
    }
    // A new bank: the previews and the models of the previous one go (the
    // models hold the bank's textures, so they are released before it).
    unloadMoviePreview();
    stopAudioPreview();
    audioClip.reset();
    audioInfo.clear();
    unloadSceneModels();
    bank.unload();
    if ( !bank.load( path.c_str() ) ) {
        setStatus( "cannot load '" + path + "'" );
        OTDU_LOG_WARN( "AssetViewer: cannot load '%s'\n", path.c_str() );
        return false;
    }
    bankKey = NormalizeAssetBank( path.c_str() );
    selectedAsset = -1;
    pPreviewTexture = nullptr;
    bPreviewIsTexture = false;
    texture = Texture2DB();
    previewInfo.clear();
    labelText[0] = '\0';
    noteText[0] = '\0';
    labelledAssets.clear();
    for ( const AssetLabel &label : GetAssetLabels() ) {
        if ( label.Bank == bankKey ) {
            labelledAssets.push_back( label.Asset );
        }
    }
    rebuildRows();
    OTDU_LOG_INFO( "AssetViewer: '%s' - %u entr(ies)\n", bankKey.c_str(),
                   bank.getNumEntries() );
    // The bank's entities (the panel), its entity cameras (the "view from a
    // camera" of an interior) and its collision (the wireframe overlay).
    loadEntities();
    buildCameraList();
    loadCollisionPreview();
    cinematicInfo.clear();
    if ( bScene ) {
        loadSceneModels();
    }
    return true;
}

void GMAssetViewer::releasePreview() {
    if ( pPreviewTexture != nullptr ) {
        // createFrom2DB does not register the texture with an owner: the caller
        // frees it (TextureCache::releaseOwner only knows loadBankTextures').
        gpRender->getDevice().destroyTexture( pPreviewTexture );
        pPreviewTexture = nullptr;
    }
    bPreviewIsTexture = false;
    texture = Texture2DB();
    previewInfo.clear();
}

bool GMAssetViewer::matchesTypeFilter( const BankEntry &entry ) const {
    const std::string &extension = entry.Extension;
    switch ( typeFilter ) {
    case eTypeFilter::Models:
        return extension == "3dd" || extension == "3dg" || extension == "2dm";
    case eTypeFilter::Textures:
        return extension == "2db";
    case eTypeFilter::Movies:
        return extension == "vmf";
    case eTypeFilter::Sounds:
        return extension == "wav";
    case eTypeFilter::Anims:
        return extension == "anm" || extension == "xan" || extension == "uva";
    case eTypeFilter::Cinematics:
        return extension == "cin" || extension == "sce" || extension == "fxe" ||
               extension == "fxa";
    case eTypeFilter::Collision:
        return extension == "shk" || extension == "dhk";
    default:
        return true;
    }
}

void GMAssetViewer::rebuildRows() {
    rows.clear();
    const std::string filter = ToLower( assetFilterText );
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( !matchesTypeFilter( entry ) ) {
            continue;
        }
        if ( !filter.empty() &&
             ToLower( entry.Filename ).find( filter ) == std::string::npos &&
             ToLower( entry.Path ).find( filter ) == std::string::npos ) {
            continue;
        }
        rows.push_back( &entry );
    }
}

void GMAssetViewer::selectAsset( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( rows.size() ) ) {
        return;
    }
    selectedAsset = index;
    const BankEntry &entry = *rows[index];
    releasePreview();
    // A new selection: the movie and the sound of the previous one go.
    unloadMoviePreview();
    stopAudioPreview();
    audioClip.reset();
    audioInfo.clear();
    pickedRow = -1;
    pickedNode = -1;

    // The existing label, if any, fills the editor.
    const AssetLabel *pLabel =
        FindAssetLabel( bankKey.c_str(), entry.Filename.c_str() );
    snprintf( labelText, sizeof( labelText ), "%s",
              pLabel != nullptr ? pLabel->Label.c_str() : "" );
    snprintf( noteText, sizeof( noteText ), "%s",
              pLabel != nullptr ? pLabel->Note.c_str() : "" );

    // Preview: a 2db texture renders (one face of a cube map: that is what the
    // panel can show flat), a model reports what the 3D view loaded for it, a
    // movie loads a FlashMovie + FlashPlayer, a wav decodes on Play.
    if ( entry.Extension == "2db" ) {
        if ( texture.parse( entry.pData, entry.Size ) ) {
            const bool bCube = texture.isCube();
            const Texture2DB upload = bCube ? CubeFaceAs2D( texture ) : texture;
            pPreviewTexture = gpRender->getTextureCache().createFrom2DB(
                upload, entry.Name.c_str() );
            bPreviewIsTexture = pPreviewTexture != nullptr;
            char info[256] = {};
            snprintf( info, sizeof( info ),
                      "%ux%u, depth %u, %u mip(s), format %d%s%s",
                      texture.Width, texture.Height, texture.Depth,
                      texture.NumMips, static_cast<int32_t>( texture.Format ),
                      bCube ? " (cube map: face 0 shown flat)" : "",
                      bPreviewIsTexture ? "" : " (no GPU texture)" );
            previewInfo = info;
        } else {
            previewInfo = "2db parse failed";
        }
    } else if ( IsModelExtension( entry.Extension ) ) {
        // What the 3D view holds for this asset (the .3dd that carries the
        // meshes; a .3dg / .2dm is one half of it).
        const SceneModel *pModel = nullptr;
        for ( const SceneModel &model : models ) {
            if ( model.RowIndex == index ) {
                pModel = &model;
                break;
            }
        }
        char info[320] = {};
        if ( pModel != nullptr ) {
            snprintf(
                info, sizeof( info ),
                "%s, %u bytes - model '%s': %u mesh(es), %u vertices, "
                "%u triangles, %u material(s)%s",
                entry.Extension.c_str(), entry.Size, pModel->Asset.c_str(),
                pModel->NumMeshes, pModel->NumVertices, pModel->NumTriangles,
                pModel->NumMaterials, bScene ? "" : " (-noscene: not drawn)" );
        } else {
            snprintf( info, sizeof( info ),
                      "%s, %u bytes, group %d, subtype %d - no .3dd of this "
                      "bank loads it (a geometry or material of a model)",
                      entry.Extension.c_str(), entry.Size, entry.Group,
                      entry.SubType );
        }
        previewInfo = info;
    } else if ( entry.Extension == "vmf" ) {
        loadMoviePreview();
        previewInfo = movieInfo.empty() ? "movie" : movieInfo;
    } else if ( entry.Extension == "wav" ) {
        char info[256] = {};
        snprintf(
            info, sizeof( info ),
            "wav, %u bytes - press Play to decode and play it (%s)", entry.Size,
            gpAudio != nullptr && gpAudio->isReady() ? "audio ready"
                                                     : "no audio device" );
        previewInfo = info;
        audioInfo = previewInfo;
    } else if ( entry.Extension == "cin" || entry.Extension == "sce" ) {
        // The cinematic's duration, shots and actors (Cinematic::parse /
        // CinematicScene::parseScene).
        loadCinematicPreview();
        previewInfo = cinematicInfo.empty() ? "cinematic" : cinematicInfo;
    } else if ( entry.Extension == "shk" || entry.Extension == "dhk" ) {
        // The bank's collision is loaded for the overlay: report what it holds.
        char info[256] = {};
        snprintf( info, sizeof( info ), "%s, %u bytes - %s",
                  entry.Extension.c_str(), entry.Size,
                  collisionInfo.empty() ? "no collision in this bank"
                                        : collisionInfo.c_str() );
        previewInfo = info;
    } else {
        char info[320] = {};
        snprintf( info, sizeof( info ), "%s, %u bytes, group %d, subtype %d",
                  entry.Extension.c_str(), entry.Size, entry.Group,
                  entry.SubType );
        previewInfo = info;
    }
    if ( pLabel != nullptr && !pLabel->Label.empty() ) {
        // Log the human name too, so a run's log reads as the labelled asset.
        OTDU_LOG_INFO( "AssetViewer: '%s' = '%s' - %s\n",
                       entry.Filename.c_str(), pLabel->Label.c_str(),
                       previewInfo.c_str() );
    } else {
        OTDU_LOG_INFO( "AssetViewer: '%s' - %s\n", entry.Filename.c_str(),
                       previewInfo.c_str() );
    }
}

void GMAssetViewer::saveLabel() {
    if ( selectedAsset < 0 ||
         selectedAsset >= static_cast<int32_t>( rows.size() ) ) {
        return;
    }
    const BankEntry &entry = *rows[selectedAsset];
    if ( !SetAssetLabel( bankKey.c_str(), entry.Filename.c_str(), labelText,
                         noteText ) ) {
        setStatus( "no change" );
        return;
    }
    if ( SaveAssetLabels() ) {
        // The bank and asset lists mark what is labelled: refresh them.
        labelledAssets.clear();
        for ( const AssetLabel &label : GetAssetLabels() ) {
            if ( label.Bank == bankKey ) {
                labelledAssets.push_back( label.Asset );
            }
        }
        refreshBankLabelCounts();
        setStatus( std::string( "saved '" ) + labelText + "' for " +
                   entry.Filename );
    } else {
        setStatus( "cannot write the label file" );
    }
}

void GMAssetViewer::setStatus( const std::string &text ) {
    statusText = text;
    statusTimer = 4.0f;
    OTDU_LOG_INFO( "AssetViewer: %s\n", text.c_str() );
}

void GMAssetViewer::tick( float deltaTime ) {
    if ( statusTimer > 0.0f ) {
        statusTimer = Max( 0.0f, statusTimer - deltaTime );
    }
    sceneTime += deltaTime;
    // The turntable: the models turn slowly in front of the camera (the sun
    // stays where it is, so the shape is what changes, not the light).
    if ( bTurntable && !models.empty() ) {
        sceneYaw += kTurntableSpeed * deltaTime;
        if ( sceneYaw > kPi ) {
            sceneYaw -= 2.0f * kPi;
        }
    }
    // The movie advances at its own frame rate.
    if ( pPlayer != nullptr ) {
        pPlayer->tick( deltaTime );
    }
    // -pickat x,y: the click the panel cannot take (the image rect is only
    // known once a frame has been laid out).
    if ( bPickScan && sceneImageMax[0] - sceneImageMin[0] > 4.0f ) {
        bPickScan = false;
        // A grid of rays over the view: where the models are (and whether the
        // ray of a pixel is what the click would cast).
        const Vector3f forward =
            Vector3f( std::sin( sceneYaw ) * std::cos( scenePitch ),
                      std::sin( scenePitch ),
                      std::cos( sceneYaw ) * std::cos( scenePitch ) );
        OTDU_LOG_INFO( "AssetViewer: scan centre (%.2f, %.2f, %.2f) distance "
                       "%.2f yaw %.2f pitch %.2f\n",
                       sceneCenter.x, sceneCenter.y, sceneCenter.z,
                       sceneDistance, sceneYaw, scenePitch );
        for ( const SceneModel &model : models ) {
            Vector3f collisionMin, collisionMax;
            const bool bCollision =
                model.Model->getCollisionBounds( collisionMin, collisionMax );
            uint32_t numItems =
                static_cast<uint32_t>( model.Model->getItems().size() );
            uint32_t numTriangles = 0;
            for ( const EdenModel::Item &item : model.Model->getItems() ) {
                numTriangles += item.CollisionCount / 3;
            }
            OTDU_LOG_INFO( "AssetViewer: scan model '%s' (%.1f, %.1f, %.1f) - "
                           "(%.1f, %.1f, %.1f) %u item(s), %u "
                           "collision triangle(s), collision bounds (%.1f, "
                           "%.1f, %.1f) - (%.1f, %.1f, %.1f)\n",
                           model.Asset.c_str(), model.BoundsMin.x,
                           model.BoundsMin.y, model.BoundsMin.z,
                           model.BoundsMax.x, model.BoundsMax.y,
                           model.BoundsMax.z, numItems, numTriangles,
                           bCollision ? collisionMin.x : 0.0f,
                           bCollision ? collisionMin.y : 0.0f,
                           bCollision ? collisionMin.z : 0.0f,
                           bCollision ? collisionMax.x : 0.0f,
                           bCollision ? collisionMax.y : 0.0f,
                           bCollision ? collisionMax.z : 0.0f );
        }
        for ( int32_t gy = 0; gy < kScanRows; gy++ ) {
            std::string line;
            for ( int32_t gx = 0; gx < kScanCols; gx++ ) {
                const float px =
                    sceneImageMin[0] + ( sceneImageMax[0] - sceneImageMin[0] ) *
                                           ( gx + 0.5f ) / kScanCols;
                const float py =
                    sceneImageMin[1] + ( sceneImageMax[1] - sceneImageMin[1] ) *
                                           ( gy + 0.5f ) / kScanRows;
                float distance = 0.0f;
                int32_t row = -1, node = -1;
                line += pickScene( px, py, row, node, distance ) ? '*' : '.';
            }
            OTDU_LOG_INFO( "AssetViewer: scan %d %s\n", gy, line.c_str() );
        }
    }
    if ( bPickCheck && sceneImageMax[0] - sceneImageMin[0] > 4.0f ) {
        bPickCheck = false;
        // Project a point with the scene's view-projection (D3D clip space, y
        // up) and turn it into a pixel of the 3D view.
        auto project = [this]( const Vector3f &p, float &outX, float &outY ) {
            const Vector3f clip = sceneViewProjection.transformPoint( p );
            const float w = sceneViewProjection.m[0][3] * p.x +
                            sceneViewProjection.m[1][3] * p.y +
                            sceneViewProjection.m[2][3] * p.z +
                            sceneViewProjection.m[3][3];
            const float inverse = std::abs( w ) > 1e-8f ? 1.0f / w : 1.0f;
            const float ndcX = clip.x * inverse;
            const float ndcY = clip.y * inverse;
            outX =
                sceneImageMin[0] + ( ndcX + 1.0f ) * 0.5f *
                                       ( sceneImageMax[0] - sceneImageMin[0] );
            outY =
                sceneImageMin[1] + ( 1.0f - ndcY ) * 0.5f *
                                       ( sceneImageMax[1] - sceneImageMin[1] );
        };
        for ( const SceneModel &model : models ) {
            const Vector3f centre =
                ( model.BoundsMin + model.BoundsMax ) * 0.5f;
            float x = 0.0f, y = 0.0f;
            project( centre, x, y );
            float distance = 0.0f;
            int32_t row = -1, node = -1;
            const bool bHit = pickScene( x, y, row, node, distance );
            OTDU_LOG_INFO(
                "AssetViewer: check '%s' centre -> pixel %.1f,%.1f: %s%s\n",
                model.Asset.c_str(), x, y, bHit ? "hit " : "MISS ",
                bHit ? model.Asset.c_str() : "" );
        }
    }
    if ( bPickPending && sceneImageMax[0] - sceneImageMin[0] > 4.0f ) {
        float distance = 0.0f;
        int32_t row = -1, node = -1;
        if ( pickScene( pendingPick[0], pendingPick[1], row, node,
                        distance ) ) {
            applyPick( row, node, distance, "-pickat" );
            bPickPending = false;
        } else if ( ++pendingPickTries >= 3 ) {
            OTDU_LOG_WARN( "AssetViewer: -pickat %.0f,%.0f hit nothing\n",
                           pendingPick[0], pendingPick[1] );
            bPickPending = false;
        }
    }
    // -savelabel: the selection has settled (a -pickat pick included): write
    // it.
    if ( bSaveLabelPending && !bPickPending ) {
        bSaveLabelPending = false;
        if ( !pendingLabel.empty() ) {
            snprintf( labelText, sizeof( labelText ), "%s",
                      pendingLabel.c_str() );
        }
        if ( !pendingNote.empty() ) {
            snprintf( noteText, sizeof( noteText ), "%s", pendingNote.c_str() );
        }
        saveLabel();
    }
}

void GMAssetViewer::drawOffscreen() {
    // The 3D view: the models of the open bank into a panel sized render
    // target. drawOffscreen runs before the main pass, and drawUI samples the
    // target as an ImGui image, so the models are ready when the panel draws.
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( !bScene || models.empty() || pRenderer == nullptr ) {
        return;
    }
    RenderDevice &device = gpRender->getDevice();
    if ( pSceneTarget == nullptr ) {
        pSceneTarget = device.createRenderTarget(
            kSceneTargetWidth, kSceneTargetHeight, VK_FORMAT_UNDEFINED, true,
            "AssetViewerScene" );
    }
    if ( pSceneTarget == nullptr ) {
        return;
    }
    const EdenSceneParams params =
        buildSceneParams( pSceneTarget->Width, pSceneTarget->Height );
    // What a click unprojects with (the same matrix the GPU draws with).
    sceneViewProjection = params.Projection.mulPrecise( params.View );
    device.beginRenderTarget( pSceneTarget, true,
                              ColorRGBA( 0.10f, 0.11f, 0.13f, 1.0f ) );
    pRenderer->beginScene( params );
    for ( const SceneModel &model : models ) {
        model.Model->draw( *pRenderer, model.Transform );
    }
    pRenderer->endScene();
    // -logscenedraws: how many of the models' meshes the renderer actually
    // recorded (a mesh outside the frustum, or without a program, is skipped).
    static const bool bLogSceneDraws = CmdLine::HasArg( "-logscenedraws" );
    static bool bLoggedSceneDraws = false;
    if ( bLogSceneDraws && !bLoggedSceneDraws ) {
        bLoggedSceneDraws = true;
        uint32_t numItems = 0;
        for ( const SceneModel &model : models ) {
            numItems += static_cast<uint32_t>( model.Model->getItems().size() );
        }
        OTDU_LOG_INFO( "AssetViewer: scene %u draw(s) of %u item(s)\n",
                       pRenderer->getNumDrawCalls(), numItems );
    }
    device.endRenderTarget();
}

void GMAssetViewer::draw() {
    // The 2D layer: the movie preview (`vmf`), drawn by FlashPlayer into the
    // stage rect the 3D view panel reported last frame (a panel sized stage;
    // setStageRect overrides the screen ratio rect of FlashPlayer::beginStage).
    if ( pPlayer == nullptr || !bMovieRectValid ) {
        return;
    }
    pPlayer->setStageRect( movieRect );
    pPlayer->draw( gpRender->get2DRenderer() );
}

void GMAssetViewer::drawUI() {
    if ( !bShowPanel ) {
        return;
    }
    // The free camera's WASD (ImGui keys: drawUI is inside the frame).
    updateFreeCamera( ImGui::GetIO().DeltaTime );

    // ---- Banks (left, top) ----
    ImGui::SetNextWindowPos( DpiScaled( 10.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 430.0f, 310.0f ),
                              ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "Asset Viewer - banks" ) ) {
        ImGui::TextWrapped( "labels: %s", GetAssetLabelPath().c_str() );
        ImGui::Text( "%zu bank(s), %zu label(s)", banks.size(),
                     GetAssetLabels().size() );
        ImGui::Checkbox( "language banks (__fr, __ge...)",
                         &bShowLanguageBanks );
        ImGui::SetNextItemWidth( -1.0f );
        ImGui::InputTextWithHint( "##bankfilter", "filter banks",
                                  bankFilterText, sizeof( bankFilterText ) );
        ImGui::BeginChild( "##banklist" );
        const std::string bankFilter = ToLower( bankFilterText );
        size_t numShown = 0;
        for ( int32_t i = 0; i < static_cast<int32_t>( banks.size() ); i++ ) {
            const BankInfo &info = banks[i];
            if ( !bShowLanguageBanks && IsLanguageBank( info.Path ) ) {
                continue;
            }
            if ( !bankFilter.empty() &&
                 ToLower( info.Path ).find( bankFilter ) ==
                     std::string::npos ) {
                continue;
            }
            const bool bLabelled =
                i < static_cast<int32_t>( bankLabelCounts.size() ) &&
                bankLabelCounts[i] > 0;
            char text[600] = {};
            snprintf( text, sizeof( text ), "%s  [%s]%s", info.Path.c_str(),
                      SizeText( info.Size ).c_str(),
                      bLabelled ? "  (labelled)" : "" );
            if ( ImGui::Selectable( text, info.Path == bankKey ) ) {
                selectedBank = i;
                openBank( info.Path );
            }
            numShown++;
        }
        ImGui::EndChild();
        ImGui::TextDisabled( "%zu shown", numShown );
    }
    ImGui::End();

    // ---- Middle: the assets of the open bank, its entities, and the search
    // ----
    ImGui::SetNextWindowPos( DpiScaled( 10.0f, 330.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 430.0f, 380.0f ),
                              ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "Asset Viewer - assets / entities / search" ) ) {
        if ( ImGui::BeginTabBar( "##middle" ) ) {
            const ImGuiTabItemFlags kForceAssets =
                requestedTab == 0 ? ImGuiTabItemFlags_SetSelected
                                  : ImGuiTabItemFlags_None;
            const ImGuiTabItemFlags kForceEntities =
                requestedTab == 1 ? ImGuiTabItemFlags_SetSelected
                                  : ImGuiTabItemFlags_None;
            const ImGuiTabItemFlags kForceSearch =
                requestedTab == 2 ? ImGuiTabItemFlags_SetSelected
                                  : ImGuiTabItemFlags_None;
            if ( ImGui::BeginTabItem( "assets", nullptr, kForceAssets ) ) {
                middleTab = 0;
                ImGui::Text( "%s - %zu of %u",
                             bankKey.empty() ? "(no bank)" : bankKey.c_str(),
                             rows.size(), bank.getNumEntries() );
                // The type filter: the extension families of the game data (a
                // 158 entry bank is mostly .2db).
                for ( int32_t i = 0;
                      i < static_cast<int32_t>( eTypeFilter::Count ); i++ ) {
                    if ( i == 4 ) {
                        ImGui::NewLine();
                    } else if ( i > 0 ) {
                        ImGui::SameLine();
                    }
                    if ( ImGui::RadioButton(
                             kTypeFilterNames[i],
                             static_cast<int32_t>( typeFilter ) == i ) ) {
                        typeFilter = static_cast<eTypeFilter>( i );
                        rebuildRows();
                    }
                }
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputTextWithHint(
                         "##assetfilter", "filter by name or authoring path",
                         assetFilterText, sizeof( assetFilterText ) ) ) {
                    rebuildRows();
                }
                ImGui::BeginChild( "##assetlist" );
                for ( int32_t i = 0; i < static_cast<int32_t>( rows.size() );
                      i++ ) {
                    const BankEntry &entry = *rows[i];
                    const bool bLabelled =
                        std::find( labelledAssets.begin(), labelledAssets.end(),
                                   entry.Filename ) != labelledAssets.end();
                    char text[600] = {};
                    snprintf( text, sizeof( text ), "%s%s  %s  grp %d",
                              bLabelled ? "* " : "  ", entry.Filename.c_str(),
                              SizeText( entry.Size ).c_str(), entry.Group );
                    if ( ImGui::Selectable( text, i == selectedAsset ) ) {
                        selectAsset( i );
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "entities", nullptr, kForceEntities ) ) {
                middleTab = 1;
                ImGui::Text( "%s - %s",
                             bankKey.empty() ? "(no bank)" : bankKey.c_str(),
                             entitiesInfo.empty() ? "no entities.xmb"
                                                  : entitiesInfo.c_str() );
                // The class filter (the Entity__ classes present in this bank).
                ImGui::SetNextItemWidth( 200.0f );
                const char *pClassLabel =
                    entityClassFilter > 0 &&
                            entityClassFilter <=
                                static_cast<int32_t>( entityClasses.size() )
                        ? entityClasses[entityClassFilter - 1].c_str()
                        : "(all classes)";
                if ( ImGui::BeginCombo( "##entityclass", pClassLabel ) ) {
                    if ( ImGui::Selectable( "(all classes)",
                                            entityClassFilter == 0 ) ) {
                        entityClassFilter = 0;
                        buildEntityRows();
                    }
                    for ( int32_t i = 0;
                          i < static_cast<int32_t>( entityClasses.size() );
                          i++ ) {
                        if ( ImGui::Selectable( entityClasses[i].c_str(),
                                                entityClassFilter == i + 1 ) ) {
                            entityClassFilter = i + 1;
                            buildEntityRows();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::Checkbox( "mark in 3D", &bMarkEntities );
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputTextWithHint(
                         "##entityfilter", "filter entities", entityFilterText,
                         sizeof( entityFilterText ) ) ) {
                    buildEntityRows();
                }
                ImGui::BeginChild( "##entitylist" );
                for ( int32_t index : entityRows ) {
                    const EntityEntry &entity = entities[index];
                    char text[400] = {};
                    snprintf( text, sizeof( text ),
                              "%s  %s  (%.1f, %.1f, %.1f)",
                              entity.Class.c_str(), entity.Name.c_str(),
                              entity.Position.x, entity.Position.y,
                              entity.Position.z );
                    if ( ImGui::Selectable( text, index == selectedEntity ) ) {
                        selectedEntity = index;
                        OTDU_LOG_INFO( "AssetViewer: entity '%s' (%s) at "
                                       "(%.2f, %.2f, %.2f)\n",
                                       entity.Name.c_str(),
                                       entity.Class.c_str(), entity.Position.x,
                                       entity.Position.y, entity.Position.z );
                    }
                    if ( index == selectedEntity ) {
                        // Every field of the object (what no other tool shows).
                        for ( const std::pair<std::string, std::string> &field :
                              entity.Fields ) {
                            ImGui::TextDisabled( "   %s = %s",
                                                 field.first.c_str(),
                                                 field.second.c_str() );
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if ( ImGui::BeginTabItem( "search", nullptr, kForceSearch ) ) {
                middleTab = 2;
                loadAssetIndex();
                if ( bIndexMissing ) {
                    ImGui::TextWrapped( "No index yet at %s",
                                        indexPath.c_str() );
                    ImGui::TextDisabled( "It indexes every bank's assets and "
                                         "entities once (reads "
                                         "the game data: about 30 s), then the "
                                         "search is instant." );
                } else {
                    ImGui::Text( "%zu asset(s), %zu entity(ies) - %s",
                                 indexAssetCount, indexEntityCount,
                                 indexPath.c_str() );
                }
                if ( ImGui::Button( bIndexMissing ? "build the index"
                                                  : "rebuild index" ) ) {
                    buildAssetIndex();
                    indexLines.clear();
                    indexAssetCount = indexEntityCount = 0;
                    bIndexLoaded = false;
                    bIndexMissing = false;
                    loadAssetIndex();
                    runSearch();
                }
                ImGui::SetNextItemWidth( -1.0f );
                if ( ImGui::InputTextWithHint(
                         "##search",
                         "search every bank: ferrari, slot, cam_entree...",
                         searchText, sizeof( searchText ) ) ) {
                    runSearch();
                }
                if ( !lastSearch.empty() ) {
                    ImGui::Text( "%zu hit(s)%s", searchHitCount,
                                 bSearchTruncated ? " (showing the first 400)"
                                                  : "" );
                }
                ImGui::BeginChild( "##hits" );
                for ( int32_t i = 0;
                      i < static_cast<int32_t>( searchHits.size() ); i++ ) {
                    const IndexHit &hit = searchHits[i];
                    char text[600] = {};
                    snprintf( text, sizeof( text ), "%s %s  -  %s",
                              hit.Kind == 'e' ? "[e]" : "   ", hit.Name.c_str(),
                              hit.Bank.c_str() );
                    if ( ImGui::Selectable( text, false ) ) {
                        openHit( i );
                    }
                    if ( ImGui::IsItemHovered() ) {
                        ImGui::SetTooltip( "%s", hit.Path.c_str() );
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
            requestedTab = -1;
        }
    }
    ImGui::End();

    // ---- Preview + label (the identity and the editor, the preview beside)
    // ----
    ImGui::SetNextWindowPos( DpiScaled( 450.0f, 10.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 820.0f, 245.0f ),
                              ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "Asset Viewer - preview + label" ) ) {
        if ( selectedAsset < 0 ||
             selectedAsset >= static_cast<int32_t>( rows.size() ) ) {
            ImGui::TextUnformatted( "Select an asset on the left." );
        } else {
            const BankEntry &entry = *rows[selectedAsset];
            if ( ImGui::BeginTable( "##preview", 2,
                                    ImGuiTableFlags_SizingStretchProp ) ) {
                ImGui::TableSetupColumn(
                    "identity", ImGuiTableColumnFlags_WidthFixed, 400.0f );
                ImGui::TableSetupColumn( "preview",
                                         ImGuiTableColumnFlags_WidthStretch );
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                // The identity the label is attached to: never guess, show it.
                ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ), "%s",
                                    entry.Filename.c_str() );
                ImGui::TextWrapped( "bank: %s", bankKey.c_str() );
                ImGui::TextWrapped( "authoring path: %s", entry.Path.c_str() );
                // The key a label is written under, exactly as the file holds
                // it.
                ImGui::TextDisabled( "key: %s",
                                     AssetKey( bankKey, entry ).c_str() );
                ImGui::Text( "%s  %u bytes  group %d  subtype %d  index %d",
                             entry.Extension.c_str(), entry.Size, entry.Group,
                             entry.SubType, entry.Index );
                if ( !previewInfo.empty() ) {
                    ImGui::TextWrapped( "%s", previewInfo.c_str() );
                }
                // The label editor: directly under the identity, above the
                // preview (it used to fall below the fold at 1280x720).
                ImGui::Separator();
                ImGui::SetNextItemWidth( -1.0f );
                ImGui::InputTextWithHint( "##label", "label - what is this?",
                                          labelText, sizeof( labelText ) );
                ImGui::SetNextItemWidth( -1.0f );
                ImGui::InputTextWithHint( "##note", "note (optional)", noteText,
                                          sizeof( noteText ) );
                const bool bEnter = ImGui::IsKeyPressed( ImGuiKey_Enter ) &&
                                    !ImGui::IsAnyItemActive();
                if ( ImGui::Button( "Save label (Enter)" ) || bEnter ) {
                    saveLabel();
                }
                ImGui::SameLine();
                if ( ImGui::Button( "Clear" ) ) {
                    labelText[0] = '\0';
                    noteText[0] = '\0';
                    saveLabel();
                }
                // The sound of a `wav` asset (GSAudio play / stop).
                if ( entry.Extension == "wav" ) {
                    ImGui::SameLine();
                    if ( ImGui::Button( bAudioPlaying ? "Stop" : "Play" ) ) {
                        toggleAudioPreview();
                    }
                }
                if ( !audioInfo.empty() ) {
                    ImGui::TextWrapped( "%s", audioInfo.c_str() );
                }
                if ( !collisionInfo.empty() && ( entry.Extension == "shk" ||
                                                 entry.Extension == "dhk" ) ) {
                    ImGui::TextWrapped( "%s", collisionInfo.c_str() );
                }
                ImGui::TableNextColumn();
                // The preview: a texture image, the movie note, or nothing.
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                if ( bPreviewIsTexture && pPreviewTexture != nullptr &&
                     texture.Width > 0 && texture.Height > 0 ) {
                    const float w = static_cast<float>( texture.Width );
                    const float h = static_cast<float>( texture.Height );
                    const float fit =
                        avail.x > 16.0f && avail.y > 16.0f
                            ? Min( 1.0f, Min( avail.x / w, avail.y / h ) )
                            : 1.0f;
                    ImGui::Image( (ImTextureID)pPreviewTexture,
                                  ImVec2( w * fit, h * fit ) );
                } else if ( entry.Extension == "vmf" && pPlayer != nullptr ) {
                    ImGui::TextDisabled(
                        "the movie is playing in the 3D view panel" );
                } else {
                    ImGui::TextDisabled( "(no pixel preview for this type)" );
                }
                ImGui::EndTable();
            }
        }
        if ( statusTimer > 0.0f ) {
            ImGui::TextColored( ImVec4( 0.5f, 1.0f, 0.6f, 1.0f ), "%s",
                                statusText.c_str() );
        }
        ImGui::TextDisabled(
            "one label per asset; -labellint checks every key against the game "
            "data" );
    }
    ImGui::End();

    // ---- 3D view: the models of the bank, the picking, the overlays ----
    ImGui::SetNextWindowPos( DpiScaled( 450.0f, 265.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 820.0f, 445.0f ),
                              ImGuiCond_FirstUseEver );
    if ( ImGui::Begin( "Asset Viewer - 3D view" ) ) {
        ImGui::Text( "%s", bankKey.empty() ? "(no bank)" : bankKey.c_str() );
        ImGui::Text( "%s", sceneInfo.empty() ? "loading models..."
                                             : sceneInfo.c_str() );
        ImGui::Checkbox( "turntable", &bTurntable );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( 130.0f );
        ImGui::SliderFloat( "fov", &sceneFov, 20.0f, 90.0f, "%.0f deg" );
        ImGui::SameLine();
        if ( ImGui::Button( "fit bank" ) ) {
            bCameraOverride = false;
            bFreeCamera = false;
            layoutSceneModels();
        }
        // The free camera (WASD / QE, Shift x4): walk through a room bank.
        ImGui::SameLine();
        if ( ImGui::Button( bFreeCamera ? "free camera: on"
                                        : "free camera" ) ) {
            if ( bFreeCamera ) {
                bFreeCamera = false;
            } else {
                const Vector3f forward =
                    Vector3f( std::sin( sceneYaw ) * std::cos( scenePitch ),
                              std::sin( scenePitch ),
                              std::cos( sceneYaw ) * std::cos( scenePitch ) );
                freePosition = bCameraOverride
                                   ? cameraOverridePosition
                                   : ( sceneCenter - forward * sceneDistance );
                bFreeCamera = true;
                bCameraOverride = false;
                bTurntable = false;
                setStatus( "free camera: WASD move, Q/E down/up, Shift x4, "
                           "drag looks" );
            }
        }
        // The entity cameras of the bank: an interior previews as a box seen
        // from outside without one.
        if ( !cameraNames.empty() ) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth( 210.0f );
            if ( ImGui::BeginCombo(
                     "##camera",
                     selectedCamera >= 0 &&
                             selectedCamera <
                                 static_cast<int32_t>( cameraNames.size() )
                         ? cameraNames[selectedCamera].c_str()
                         : "view from camera..." ) ) {
                for ( int32_t i = 0;
                      i < static_cast<int32_t>( cameraNames.size() ); i++ ) {
                    if ( ImGui::Selectable( cameraNames[i].c_str(),
                                            i == selectedCamera ) ) {
                        useEntityCamera( i );
                    }
                }
                ImGui::EndCombo();
            }
        }
        if ( bCollisionLoaded ) {
            ImGui::SameLine();
            ImGui::Checkbox( "collision", &bShowCollision );
        }
        if ( !entities.empty() ) {
            ImGui::SameLine();
            ImGui::Checkbox( "entities", &bMarkEntities );
        }
        // The offscreen target of drawOffscreen, 4:3, fitted to the panel.
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        float imageWidth = Max( 64.0f, avail.x );
        float imageHeight = imageWidth * 0.75f;
        const float maxHeight = Max( 64.0f, avail.y - 44.0f );
        if ( imageHeight > maxHeight ) {
            imageHeight = maxHeight;
            imageWidth = imageHeight * 4.0f / 3.0f;
        }
        if ( pSceneTarget != nullptr ) {
            ImGui::Image( (ImTextureID)&pSceneTarget->Color,
                          ImVec2( imageWidth, imageHeight ) );
        } else {
            ImGui::Dummy( ImVec2( imageWidth, imageHeight ) );
        }
        // The rect of the image (or of the empty box: a `vmf` asset plays
        // there), taken before anything else is laid out in the panel.
        const ImVec2 imageMin = ImGui::GetItemRectMin();
        const ImVec2 imageMax = ImGui::GetItemRectMax();
        if ( pSceneTarget == nullptr ) {
            ImGui::TextDisabled( "no model to draw" );
        }
        sceneImageMin[0] = imageMin.x;
        sceneImageMin[1] = imageMin.y;
        sceneImageMax[0] = imageMax.x;
        sceneImageMax[1] = imageMax.y;
        bSceneHovered = ImGui::IsItemHovered();
        // The overlays: the entity markers and the collision wireframe, drawn
        // with the same projection the GPU used (they cannot drift from the
        // image).
        auto project = [this]( const Vector3f &p, ImVec2 &out ) -> bool {
            const Vector3f clip = sceneViewProjection.transformPoint( p );
            const float w = sceneViewProjection.m[0][3] * p.x +
                            sceneViewProjection.m[1][3] * p.y +
                            sceneViewProjection.m[2][3] * p.z +
                            sceneViewProjection.m[3][3];
            if ( w < 1e-4f ) {
                return false; // behind the camera
            }
            const float inverse = 1.0f / w;
            const float ndcX = clip.x * inverse;
            const float ndcY = clip.y * inverse;
            if ( ndcX < -1.2f || ndcX > 1.2f || ndcY < -1.2f || ndcY > 1.2f ) {
                return false;
            }
            out.x =
                sceneImageMin[0] + ( ndcX + 1.0f ) * 0.5f *
                                       ( sceneImageMax[0] - sceneImageMin[0] );
            out.y =
                sceneImageMin[1] + ( 1.0f - ndcY ) * 0.5f *
                                       ( sceneImageMax[1] - sceneImageMin[1] );
            return true;
        };
        ImDrawList *pDrawList = ImGui::GetWindowDrawList();
        if ( bMarkEntities && selectedEntity >= 0 &&
             selectedEntity < static_cast<int32_t>( entities.size() ) ) {
            const EntityEntry &entity = entities[selectedEntity];
            ImVec2 screen;
            if ( project( entity.Position, screen ) ) {
                const ImU32 color = IM_COL32( 255, 200, 60, 255 );
                pDrawList->AddCircleFilled( screen, 4.0f, color );
                pDrawList->AddText( ImVec2( screen.x + 7.0f, screen.y - 6.0f ),
                                    color, entity.Name.c_str() );
                // A camera draws its frustum (Fov, Ratio) along its view axis.
                if ( entity.Class == "Camera" ) {
                    float fov = 60.0f * kPi / 180.0f;
                    float ratio = 16.0f / 9.0f;
                    for ( const std::pair<std::string, std::string> &field :
                          entity.Fields ) {
                        if ( field.first == "Fov" ) {
                            fov = static_cast<float>(
                                atof( field.second.c_str() ) );
                        } else if ( field.first == "Ratio" ) {
                            ratio = static_cast<float>(
                                atof( field.second.c_str() ) );
                        }
                    }
                    const Matrix4x4f rotation =
                        EdenRotationFromQuaternion( entity.Rotation );
                    const Vector3f forward = rotation.transformVector(
                        Vector3f( 0.0f, 0.0f, 1.0f ) );
                    const Vector3f right = rotation.transformVector(
                        Vector3f( 1.0f, 0.0f, 0.0f ) );
                    const Vector3f up = rotation.transformVector(
                        Vector3f( 0.0f, 1.0f, 0.0f ) );
                    const float length = 1.5f;
                    const float halfHeight = std::tan( fov * 0.5f ) * length;
                    const float halfWidth = halfHeight * ratio;
                    const Vector3f center = entity.Position + forward * length;
                    const Vector3f corners[4] = {
                        center + right * halfWidth + up * halfHeight,
                        center - right * halfWidth + up * halfHeight,
                        center - right * halfWidth - up * halfHeight,
                        center + right * halfWidth - up * halfHeight,
                    };
                    ImVec2 previous;
                    bool bPrevious = false;
                    ImVec2 apex;
                    const bool bApex = project( entity.Position, apex );
                    for ( int32_t i = 0; i <= 4; i++ ) {
                        ImVec2 corner;
                        if ( !project( corners[i % 4], corner ) ) {
                            bPrevious = false;
                            continue;
                        }
                        if ( bPrevious ) {
                            pDrawList->AddLine( previous, corner, color );
                        }
                        if ( bApex ) {
                            pDrawList->AddLine( apex, corner, color );
                        }
                        previous = corner;
                        bPrevious = true;
                    }
                }
            }
        }
        if ( bShowCollision && bCollisionLoaded ) {
            const ImU32 color = IM_COL32( 80, 200, 255, 160 );
            // The boxes: 12 edges each (the body transforms are already
            // applied: the file is flattened into the bank's frame).
            for ( const HavokCollision::Box &box : collision.Boxes ) {
                Vector3f corner[8];
                for ( int32_t i = 0; i < 8; i++ ) {
                    corner[i] = box.Transform.transformPoint( Vector3f(
                        ( i & 1 ) != 0 ? box.HalfExtents.x : -box.HalfExtents.x,
                        ( i & 2 ) != 0 ? box.HalfExtents.y : -box.HalfExtents.y,
                        ( i & 4 ) != 0 ? box.HalfExtents.z
                                       : -box.HalfExtents.z ) );
                }
                static const int32_t kEdges[12][2] = {
                    {0, 1},
                    {2, 3},
                    {4, 5},
                    {6, 7},
                    {0, 2},
                    {1, 3},
                    {4, 6},
                    {5, 7},
                    {0, 4},
                    {1, 5},
                    {2, 6},
                    {3, 7}
                };
                for ( const int32_t *pEdge : kEdges ) {
                    ImVec2 a, b;
                    if ( project( corner[pEdge[0]], a ) &&
                         project( corner[pEdge[1]], b ) ) {
                        pDrawList->AddLine( a, b, color );
                    }
                }
            }
            // The triangles: the wireframe, capped (a sector bank has tens of
            // thousands: the overlay is a check, not a render).
            const size_t maxTriangles = 4000;
            const size_t numTriangles =
                Min<size_t>( collision.getNumTriangles(), maxTriangles );
            for ( size_t t = 0; t < numTriangles; t++ ) {
                const Vector3f &a =
                    collision.Vertices[collision.Indices[t * 3]];
                const Vector3f &b =
                    collision.Vertices[collision.Indices[t * 3 + 1]];
                const Vector3f &c =
                    collision.Vertices[collision.Indices[t * 3 + 2]];
                ImVec2 sa, sb, sc;
                const bool bA = project( a, sa );
                const bool bB = project( b, sb );
                const bool bC = project( c, sc );
                if ( bA && bB ) {
                    pDrawList->AddLine( sa, sb, color );
                }
                if ( bB && bC ) {
                    pDrawList->AddLine( sb, sc, color );
                }
                if ( bC && bA ) {
                    pDrawList->AddLine( sc, sa, color );
                }
            }
        }
        // A movie (`vmf`) plays in this panel: the 2D layer (draw) needs its
        // destination rect normalized to the backbuffer, and it draws before
        // the panels, so it uses the rect of this frame's layout from the next
        // frame on.
        if ( pPlayer != nullptr && pMovie != nullptr && imageWidth > 16.0f ) {
            const VMFFile &vmf = pMovie->getVMF();
            const float stageAspect =
                vmf.getStageWidth() / Max( 1.0f, vmf.getStageHeight() );
            float width = imageWidth, height = imageWidth / stageAspect;
            if ( height > imageHeight ) {
                height = imageHeight;
                width = imageHeight * stageAspect;
            }
            const float x = imageMin.x + ( imageWidth - width ) * 0.5f;
            const float y = imageMin.y + ( imageHeight - height ) * 0.5f;
            // ImGui pixels -> backbuffer pixels -> normalized (beginRect).
            const ImVec2 framebufferScale =
                ImGui::GetIO().DisplayFramebufferScale;
            const float backbufferWidth = static_cast<float>(
                gpRender->getDevice().getBackbufferWidth() );
            const float backbufferHeight = static_cast<float>(
                gpRender->getDevice().getBackbufferHeight() );
            movieRect[0] =
                x * framebufferScale.x / Max( 1.0f, backbufferWidth );
            movieRect[1] =
                y * framebufferScale.y / Max( 1.0f, backbufferHeight );
            movieRect[2] =
                width * framebufferScale.x / Max( 1.0f, backbufferWidth );
            movieRect[3] =
                height * framebufferScale.y / Max( 1.0f, backbufferHeight );
            bMovieRectValid = true;
        } else {
            bMovieRectValid = false;
        }
        // The pixels -pickat takes (the log is the script's input).
        if ( loggedSceneRect[0] != sceneImageMin[0] ||
             loggedSceneRect[1] != sceneImageMin[1] ||
             loggedSceneRect[2] != sceneImageMax[0] ||
             loggedSceneRect[3] != sceneImageMax[1] ) {
            loggedSceneRect[0] = sceneImageMin[0];
            loggedSceneRect[1] = sceneImageMin[1];
            loggedSceneRect[2] = sceneImageMax[0];
            loggedSceneRect[3] = sceneImageMax[1];
            OTDU_LOG_INFO( "AssetViewer: 3D view %.0f,%.0f - %.0f,%.0f (%s)\n",
                           sceneImageMin[0], sceneImageMin[1], sceneImageMax[0],
                           sceneImageMax[1], sceneInfo.c_str() );
        }
        // A click selects the asset that carries the mesh under the mouse (the
        // same identity as the lists, so the label keys on it).
        if ( ImGui::IsItemClicked( ImGuiMouseButton_Left ) ) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            float distance = 0.0f;
            int32_t row = -1, node = -1;
            if ( pickScene( mouse.x, mouse.y, row, node, distance ) ) {
                applyPick( row, node, distance, "click" );
            } else {
                setStatus( "click: no mesh there" );
            }
        }
        // Drag: look (orbit or free camera), wheel: zoom / speed.
        if ( bSceneHovered ) {
            if ( ImGui::IsMouseDragging( ImGuiMouseButton_Left, 3.0f ) ) {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                sceneYaw -= delta.x * 0.01f;
                scenePitch = Clamp( scenePitch + delta.y * 0.01f, -1.4f, 1.4f );
                bTurntable = false; // the owner took the camera
                bCameraOverride = false;
            }
            const float wheel = ImGui::GetIO().MouseWheel;
            if ( wheel > 0.0f ) {
                sceneDistance = Max( sceneDistance * 0.9f, 0.2f );
                freeSpeed = Max( freeSpeed * 1.1f, 0.2f );
            } else if ( wheel < 0.0f ) {
                sceneDistance = Min( sceneDistance * 1.1f, 40000.0f );
                freeSpeed = Min( freeSpeed / 1.1f, 4000.0f );
            }
        }
        if ( bCameraOverride && selectedCamera >= 0 &&
             selectedCamera < static_cast<int32_t>( cameraNames.size() ) ) {
            ImGui::TextColored( ImVec4( 0.6f, 0.9f, 1.0f, 1.0f ),
                                "view from '%s' (fov %.0f deg)",
                                cameraNames[selectedCamera].c_str(),
                                cameraOverrideFov );
        } else if ( bFreeCamera ) {
            ImGui::TextColored( ImVec4( 0.6f, 0.9f, 1.0f, 1.0f ),
                                "free camera at (%.1f, %.1f, %.1f), %.1f m/s",
                                freePosition.x, freePosition.y, freePosition.z,
                                freeSpeed );
        } else if ( pickedRow >= 0 &&
                    pickedRow < static_cast<int32_t>( rows.size() ) ) {
            ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.2f, 1.0f ),
                                "picked: %s (mesh node %d, %.1f m)",
                                rows[pickedRow]->Filename.c_str(), pickedNode,
                                pickedDistance );
        } else {
            ImGui::TextDisabled(
                "click a mesh: it selects the asset that carries it" );
        }
        if ( !collisionInfo.empty() ) {
            ImGui::TextDisabled( "%s", collisionInfo.c_str() );
        }
        ImGui::TextDisabled( "drag: look, wheel: zoom / speed, WASD+QE: free "
                             "camera, -pickat x,y: a "
                             "scriptable click" );
    }
    ImGui::End();
}

void GMAssetViewer::refreshBankLabelCounts() {
    bankLabelCounts.assign( banks.size(), 0 );
    for ( const AssetLabel &label : GetAssetLabels() ) {
        for ( size_t i = 0; i < banks.size(); i++ ) {
            if ( banks[i].Path == label.Bank ) {
                bankLabelCounts[i]++;
                break;
            }
        }
    }
}

// ---- The 3D view
// -----------------------------------------------------------------

bool GMAssetViewer::loadLibraryMaterials() {
    // The shared library of the island (commonworld.bnk library.2dm, what
    // EdenWorld parses for the world): the fallback of the banks' materials.
    if ( bLibraryLoaded ) {
        return true;
    }
    if ( gpFile == nullptr || gpRender == nullptr ) {
        return false;
    }
    const std::string path = gpFile->getBankFolder() + "islands/" + island +
                             "/level/commonworld.bnk";
    if ( !libraryBank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "AssetViewer: no shared material library at '%s'\n",
                       path.c_str() );
        return false;
    }
    gpRender->getTextureCache().loadBankTextures(
        libraryBank, libraryBank.getPath().c_str() );
    if ( const BankEntry *pLibrary =
             libraryBank.findEntry( "library", "2dm" ) ) {
        libraryMaterials.parse( pLibrary->pData, pLibrary->Size );
    }
    bLibraryLoaded = true;
    OTDU_LOG_INFO(
        "AssetViewer: shared material library '%s' (%zu material(s))\n",
        path.c_str(), libraryMaterials.getMaterials().size() );
    return true;
}

void GMAssetViewer::loadSceneModels() {
    EdenRenderer *pRenderer = gpRender->getEdenRenderer();
    if ( pRenderer == nullptr || !bank.isLoaded() ) {
        return;
    }
    loadLibraryMaterials();
    // The bank's textures: the models' materials look them up by hashcode under
    // this bank's path (what the texture owner is).
    if ( !bBankTexturesLoaded ) {
        gpRender->getTextureCache().loadBankTextures( bank,
                                                      bank.getPath().c_str() );
        bBankTexturesLoaded = true;
    }
    uint32_t numVertices = 0, numTriangles = 0, numMeshes = 0;
    bool bTruncated = false;
    // One model per `.3dd` of the bank (its `.3dg` geometry and `.2dm`
    // materials are the entries of the same name, see SpotExterior).
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension != "3dd" ) {
            continue;
        }
        if ( models.size() >= kMaxSceneModels ) {
            bTruncated = true;
            break;
        }
        const BankEntry *pGeometry =
            bank.findEntry( entry.Name.c_str(), "3dg" );
        if ( pGeometry == nullptr ) {
            continue;
        }
        const BankEntry *pMaterials =
            bank.findEntry( entry.Name.c_str(), "2dm" );
        SceneModel model;
        model.Asset = entry.Filename;
        model.Name = entry.Name;
        model.Model = std::make_unique<EdenModel>();
        // The CPU copy of the triangles is what a click picks with.
        model.Model->setKeepCollision( true );
        if ( !model.Model->load( *pRenderer, entry, *pGeometry, pMaterials,
                                 &libraryMaterials, bank.getPath().c_str() ) ) {
            OTDU_LOG_WARN( "AssetViewer: model '%s' of '%s' did not load\n",
                           entry.Filename.c_str(), bankKey.c_str() );
            continue;
        }
        model.Model->bindUVAnimations( bank );
        for ( const EdenModel::Item &item : model.Model->getItems() ) {
            model.NumMeshes++;
            model.NumVertices += item.NumVertices;
            model.NumTriangles += item.Mesh.IndexCount / 3;
        }
        model.NumMaterials =
            static_cast<uint32_t>( model.Model->getMaterials().size() );
        // The row of this asset in the list, so a click can select it.
        for ( int32_t i = 0; i < static_cast<int32_t>( rows.size() ); i++ ) {
            if ( rows[i] == &entry ) {
                model.RowIndex = i;
                break;
            }
        }
        numVertices += model.NumVertices;
        numTriangles += model.NumTriangles;
        numMeshes += model.NumMeshes;
        models.push_back( std::move( model ) );
    }
    layoutSceneModels();
    if ( models.empty() ) {
        sceneInfo = "no .3dd model in this bank";
    } else {
        char info[192] = {};
        snprintf( info, sizeof( info ),
                  "%zu model(s), %u mesh(es), %u vertices, %u triangles%s",
                  models.size(), numMeshes, numVertices, numTriangles,
                  bTruncated ? " (list cut)" : "" );
        sceneInfo = info;
    }
    OTDU_LOG_INFO( "AssetViewer: '%s' - %s\n", bankKey.c_str(),
                   sceneInfo.c_str() );
}

void GMAssetViewer::unloadSceneModels() {
    if ( !models.empty() && gpRender != nullptr ) {
        // The models hold GPU buffers and their materials hold the bank's
        // textures: drain the device before they go.
        gpRender->getDevice().waitIdle();
    }
    models.clear();
    sceneInfo.clear();
    pickedRow = -1;
    pickedNode = -1;
    pickedDistance = 0.0f;
    if ( bBankTexturesLoaded && gpRender != nullptr ) {
        gpRender->getTextureCache().releaseOwner( bank.getPath().c_str() );
        bBankTexturesLoaded = false;
    }
}

void GMAssetViewer::layoutSceneModels() {
    // A row along X: every model's own bounds start at the cursor, the row sits
    // on y = 0 and is centred on z = 0, so the turntable turns around all of
    // them and a click can name any of them.
    float cursor = 0.0f;
    float maxHeight = 0.1f;
    for ( SceneModel &model : models ) {
        const Vector3f min = model.Model->getBoundsMin();
        const Vector3f max = model.Model->getBoundsMax();
        const float width = Max( max.x - min.x, 0.1f );
        model.Transform = Matrix4x4f::Translation(
            Vector3f( cursor - min.x, -min.y, -( min.z + max.z ) * 0.5f ) );
        model.BoundsMin = model.Transform.transformPoint( min );
        model.BoundsMax = model.Transform.transformPoint( max );
        maxHeight = Max( maxHeight, model.BoundsMax.y - model.BoundsMin.y );
        cursor += width + kSceneGap;
    }
    if ( models.empty() ) {
        sceneCenter = Vector3f( 0.0f, 0.0f, 0.0f );
        sceneDistance = 20.0f;
        return;
    }
    const float total = cursor - kSceneGap;
    sceneCenter = Vector3f( total * 0.5f, maxHeight * 0.5f, 0.0f );
    // The distance that makes the row fit the frame: the horizontal and
    // vertical half angles of the projection (the target is 4:3, so tanX =
    // tanY * 4/3, see EdenPerspectiveFovRH), with a margin.
    const float tanY = std::tan( sceneFov * kPi / 360.0f );
    const float tanX = tanY * static_cast<float>( kSceneTargetWidth ) /
                       static_cast<float>( kSceneTargetHeight );
    sceneDistance =
        Max( total * 0.5f / tanX, maxHeight * 0.5f / tanY ) * 1.1f + 1.0f;
}

void GMAssetViewer::focusScene( int32_t rowIndex ) {
    for ( const SceneModel &model : models ) {
        if ( model.RowIndex != rowIndex ) {
            continue;
        }
        sceneCenter = ( model.BoundsMin + model.BoundsMax ) * 0.5f;
        // Fit the model itself (its bounding sphere), with a margin.
        const float radius =
            Max( ( model.BoundsMax - model.BoundsMin ).length() * 0.5f, 0.5f );
        const float tanY = std::tan( sceneFov * kPi / 360.0f );
        sceneDistance = Max( radius / tanY * 1.1f, 1.0f );
        return;
    }
}

EdenSceneParams GMAssetViewer::buildSceneParams( uint32_t width,
                                                 uint32_t height ) const {
    // The camera and the light of EdenSceneView::setupScene, built here: the 3D
    // view is a bank's models alone, so it does not need the island world.
    EdenSceneParams params;
    const float aspect =
        static_cast<float>( width ) / Max( 1.0f, static_cast<float>( height ) );
    const Vector3f forward = Vector3f(
        std::sin( sceneYaw ) * std::cos( scenePitch ), std::sin( scenePitch ),
        std::cos( sceneYaw ) * std::cos( scenePitch ) );
    // The camera: an entity camera's pose when one was picked ("view from
    // cam_entree"), a walkable free camera, else the turntable orbit.
    Vector3f position;
    Quaternionf rotation;
    float fovDegrees = sceneFov;
    if ( bCameraOverride ) {
        position = cameraOverridePosition;
        rotation = cameraOverrideRotation;
        fovDegrees = cameraOverrideFov;
    } else if ( bFreeCamera ) {
        position = freePosition;
        rotation =
            ChaseCamera::LookRotation( freePosition, freePosition + forward );
    } else {
        position = sceneCenter - forward * sceneDistance;
        rotation = ChaseCamera::LookRotation( position, sceneCenter );
    }
    const Matrix4x4f cameraWorld =
        Matrix4x4f::Translation( position ) *
        EdenRotationFromQuaternion( rotation ) *
        Matrix4x4f::Scale( Vector3f( -1.0f, 1.0f, -1.0f ) );
    params.View = cameraWorld.inverse();
    params.Projection = EdenPerspectiveFovRH( fovDegrees * kPi / 180.0f, aspect,
                                              0.05f, 40000.0f );
    params.CameraPosition = position;
    params.CameraDirection =
        cameraWorld.transformVector( Vector3f( 0.0f, 0.0f, -1.0f ) );
    params.Width = width;
    params.Height = height;
    params.Time = sceneTime;
    // Frame user vectors: x = sun visibility factor (a preview is in full sun).
    // y / w = the night lights of the weather (outside_street_lightmap /
    // building_nightwindow_frequency, night_lighting_spec 2), so a night
    // preview lights its street maps and windows like the game.
    static const bool bNoStreetLights = CmdLine::HasArg( "-nostreetlights" );
    if ( pWeather != nullptr && !bNoStreetLights ) {
        params.UserVectors[1] = Vector4f( 1.0f, pWeather->getFloat( "outside_street_lightmap", 0.0f ), 1.0f,
                                          pWeather->getFloat( "building_nightwindow_frequency", 0.0f ) );
        const Vector4f shop = pWeather->getVector4( "building_shop_front_back_lighting", Vector4f() );
        params.UserVectors[2] = Vector4f( shop.x, shop.y, 0.0f, 0.0f );
    } else {
        params.UserVectors[1] = Vector4f( 1.0f, 0.0f, 0.0f, 0.0f );
        params.UserVectors[2] = Vector4f();
    }
    if ( pWeather != nullptr ) {
        params.SunDirection =
            pWeather->getVector3( "sundirection", params.SunDirection )
                .normalized();
        params.SunDiffuse =
            pWeather->getVector3( "sun_diffuse", params.SunDiffuse );
        params.SunSpecular =
            pWeather->getVector3( "sun_specular", params.SunSpecular );
        params.Ambient = pWeather->getVector3( "sun_ambient", params.Ambient );
        params.FogColor = pWeather->getVector3( "fog_color", params.FogColor );
        const float fogStart = pWeather->getFloat( "fog_start", 100.0f );
        const float fogEnd = pWeather->getFloat( "fog_end", 2000.0f );
        params.FogConfig = Vector4f( fogStart, fogEnd, 0.0f,
                                     1.0f / Max( fogEnd - fogStart, 1.0f ) );
    }
    EdenSky::setupAtmosphere( params, pWeather );
    return params;
}

bool GMAssetViewer::pickScene( float pixelX,
                               float pixelY,
                               int32_t &outRow,
                               int32_t &outNode,
                               float &outDistance ) {
    const float width = sceneImageMax[0] - sceneImageMin[0];
    const float height = sceneImageMax[1] - sceneImageMin[1];
    if ( models.empty() || width < 4.0f || height < 4.0f ) {
        return false;
    }
    // The pixel -> the ray of the camera buildSceneParams set up. The ray comes
    // from the view-projection the GPU used (unprojected near / far points), so
    // it cannot drift from what was drawn.
    const float ndcX = ( ( pixelX - sceneImageMin[0] ) / width ) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ( ( pixelY - sceneImageMin[1] ) / height ) * 2.0f;
    const Matrix4x4f inverseViewProjection = sceneViewProjection.inverse();
    // D3D clip space (the renderer keeps it): x, y in [-1, 1], z in [0, 1],
    // y up (the viewport is flipped for it).
    auto unproject = [&inverseViewProjection]( float x, float y, float z ) {
        const Vector4f clip( x, y, z, 1.0f );
        const Vector3f p = inverseViewProjection.transformPoint(
            Vector3f( clip.x, clip.y, clip.z ) );
        // transformPoint applies the matrix to the vector as a point (row
        // convention); the w of the result is the fourth row of the inverse.
        const float w = inverseViewProjection.m[0][3] * clip.x +
                        inverseViewProjection.m[1][3] * clip.y +
                        inverseViewProjection.m[2][3] * clip.z +
                        inverseViewProjection.m[3][3];
        return std::abs( w ) > 1e-8f ? p * ( 1.0f / w ) : p;
    };
    const Vector3f nearPoint = unproject( ndcX, ndcY, 0.0f );
    const Vector3f farPoint = unproject( ndcX, ndcY, 1.0f );
    const Vector3f origin = nearPoint;
    const Vector3f direction = ( farPoint - nearPoint ).normalized();

    float best = 1e30f;
    int32_t bestRow = -1, bestNode = -1;
    for ( const SceneModel &model : models ) {
        if ( model.RowIndex < 0 ) {
            continue;
        }
        // The layout is a translation, so moving the ray by it keeps the
        // distances of the models comparable.
        const Vector3f localOrigin = origin - model.Transform.getTranslation();
        float distance = 0.0f;
        int32_t node = -1;
        if ( model.Model->raycast( localOrigin, direction, distance, node ) &&
             distance < best ) {
            best = distance;
            bestRow = model.RowIndex;
            bestNode = node;
        }
    }
    if ( bestRow < 0 ) {
        return false;
    }
    outRow = bestRow;
    outNode = bestNode;
    outDistance = best;
    return true;
}

void GMAssetViewer::applyPick( int32_t row,
                               int32_t node,
                               float distance,
                               const char *pSource ) {
    if ( row < 0 || row >= static_cast<int32_t>( rows.size() ) ) {
        return;
    }
    const BankEntry &entry = *rows[row];
    // The scene node of the mesh, for the panel and the log (the label keys on
    // the asset, the node is what the owner pointed at).
    std::string nodeName;
    for ( const SceneModel &model : models ) {
        if ( model.RowIndex != row ) {
            continue;
        }
        const std::vector<SceneNode> &nodes =
            model.Model->getScene().getNodes();
        if ( node >= 0 && node < static_cast<int32_t>( nodes.size() ) ) {
            nodeName = nodes[node].Name;
        }
        break;
    }
    selectAsset( row );
    pickedRow = row;
    pickedNode = node;
    pickedDistance = distance;
    // The camera goes to what was picked: the row layout shows the bank, the
    // click zooms onto the asset that carries the mesh (fit bank goes back).
    focusScene( row );
    OTDU_LOG_INFO(
        "AssetViewer: pick (%s) -> '%s' in '%s' (mesh node '%s', %.2f m)\n",
        pSource, entry.Filename.c_str(), bankKey.c_str(), nodeName.c_str(),
        distance );
    char text[512] = {};
    snprintf(
        text, sizeof( text ),
        "picked '%s' (node '%s', %.1f m) - write the label and press Enter",
        entry.Filename.c_str(), nodeName.empty() ? "?" : nodeName.c_str(),
        distance );
    setStatus( text );
}

// ---- Movie (`vmf`) and sound (`wav`) previews
// -------------------------------------

void GMAssetViewer::loadMoviePreview() {
    unloadMoviePreview();
    if ( selectedAsset < 0 ||
         selectedAsset >= static_cast<int32_t>( rows.size() ) ) {
        return;
    }
    const BankEntry &entry = *rows[selectedAsset];
    std::string name, resolution;
    if ( !MovieBankName( bankKey, name, resolution ) ) {
        movieInfo = "not previewable: FlashMovie resolves "
                    "FrontEnd/<Res>/<name>.bnk and "
                    "this bank is not in that layout";
        return;
    }
    pMovie = std::make_unique<FlashMovie>();
    if ( !pMovie->load( name.c_str(), resolution.c_str() ) ) {
        pMovie.reset();
        movieInfo =
            "cannot load the movie bank '" + name + "' (" + resolution + ")";
        return;
    }
    pPlayer = std::make_unique<FlashPlayer>();
    if ( !pPlayer->load( pMovie.get() ) ) {
        unloadMoviePreview();
        movieInfo = "cannot start the movie player";
        return;
    }
    const VMFFile &vmf = pMovie->getVMF();
    char info[288] = {};
    snprintf( info, sizeof( info ),
              "movie '%s' (%s): %.0fx%.0f, %u frames @ %.0f fps, %zu tags, %zu "
              "bitmaps "
              "- drawn in the 3D view panel",
              entry.Filename.c_str(), name.c_str(), vmf.getStageWidth(),
              vmf.getStageHeight(), vmf.getFrameCount(), vmf.getFrameRate(),
              vmf.getTags().size(), vmf.getBitmaps().size() );
    movieInfo = info;
    OTDU_LOG_INFO( "AssetViewer: %s\n", movieInfo.c_str() );
}

void GMAssetViewer::unloadMoviePreview() {
    // The player first (it holds clips of the movie, the movie holds the
    // textures).
    pPlayer.reset();
    pMovie.reset();
    movieInfo.clear();
}

void GMAssetViewer::toggleAudioPreview() {
    if ( bAudioPlaying ) {
        stopAudioPreview();
        return;
    }
    if ( selectedAsset < 0 ||
         selectedAsset >= static_cast<int32_t>( rows.size() ) ) {
        return;
    }
    const BankEntry &entry = *rows[selectedAsset];
    if ( entry.Extension != "wav" ) {
        return;
    }
    if ( gpAudio == nullptr || !gpAudio->isReady() ) {
        audioInfo = "no audio device";
        return;
    }
    if ( audioClip == nullptr ) {
        audioClip =
            gpAudio->loadClip( entry.pData, entry.Size, entry.Name.c_str() );
        if ( audioClip == nullptr ) {
            audioInfo = "cannot decode '" + entry.Filename + "'";
            OTDU_LOG_WARN( "AssetViewer: %s\n", audioInfo.c_str() );
            return;
        }
    }
    audioVoice = gpAudio->play( audioClip );
    bAudioPlaying = audioVoice != GSAudio::kInvalidVoice;
    char info[224] = {};
    snprintf( info, sizeof( info ), "'%s': %u frame(s), %d Hz, %.2f s%s",
              entry.Filename.c_str(), audioClip->NumFrames, audioClip->Rate,
              audioClip->Duration,
              bAudioPlaying ? " (playing)" : " (the mixer refused it)" );
    audioInfo = info;
    OTDU_LOG_INFO( "AssetViewer: sound %s\n", audioInfo.c_str() );
}

void GMAssetViewer::stopAudioPreview() {
    if ( bAudioPlaying && gpAudio != nullptr ) {
        gpAudio->stop( audioVoice );
        OTDU_LOG_INFO( "AssetViewer: sound stopped\n" );
    }
    bAudioPlaying = false;
    audioVoice = 0;
}

// ---- Entities (`entities.xmb`)
// ----------------------------------------------------

void GMAssetViewer::collectFields(
    const XmbValue &value,
    const std::string &prefix,
    int32_t depth,
    std::vector<std::pair<std::string, std::string>> &out ) const {
    // Every field of an entity object, nested composites walked: what the other
    // tools (otdu2_bnk_tool xmb prints the schema) never show.
    const XmbType *pType = value.getType();
    if ( pType == nullptr || !pType->bComposite || depth > 2 ) {
        return;
    }
    for ( const XmbType::Field &field : pType->Fields ) {
        const std::string name =
            prefix.empty() ? field.Name : prefix + "." + field.Name;
        if ( field.bArray ) {
            char text[32] = {};
            snprintf( text, sizeof( text ), "[%u]",
                      value.getArraySize( field.Name.c_str() ) );
            out.emplace_back( name, text );
            continue;
        }
        const XmbValue child( value.getDocument(), field.TypeIndex,
                              value.getOffset() + field.Offset );
        const XmbType *pChild = child.getType();
        if ( pChild == nullptr ) {
            continue;
        }
        if ( pChild->bComposite ) {
            collectFields( child, name, depth + 1, out );
            continue;
        }
        char text[64] = {};
        switch ( pChild->Primitive ) {
        case eXmbPrimitive::XP_String:
            out.emplace_back( name, child.asString() );
            continue;
        case eXmbPrimitive::XP_Float:
        case eXmbPrimitive::XP_Double:
            snprintf( text, sizeof( text ), "%.5g", child.asDouble() );
            break;
        case eXmbPrimitive::XP_Bool:
            out.emplace_back( name, child.asBool() ? "true" : "false" );
            continue;
        case eXmbPrimitive::XP_UInt64:
            // Hashcodes read as hex (the __Hc of an entity, a resource).
            snprintf( text, sizeof( text ), "%016llx",
                      static_cast<unsigned long long>( child.asUInt64() ) );
            break;
        default:
            snprintf( text, sizeof( text ), "%lld",
                      static_cast<long long>( child.asInt() ) );
            break;
        }
        out.emplace_back( name, text );
    }
}

void GMAssetViewer::loadEntities() {
    entities.clear();
    entitiesInfo.clear();
    entityRows.clear();
    selectedEntity = -1;
    entityClassFilter = 0;
    if ( !bank.isLoaded() ) {
        return;
    }
    const BankEntry *pEntry = bank.findEntry( "entities.xmb" );
    if ( pEntry == nullptr ) {
        entitiesInfo = "no entities.xmb in this bank";
        return;
    }
    XmbDocument document;
    if ( !document.load( pEntry->pData, pEntry->Size ) ) {
        entitiesInfo = "entities.xmb did not parse";
        return;
    }
    const XmbValue entityExport = document.getRoot()["ExportType_Entity"];
    const XmbType *pType = entityExport.getType();
    if ( !entityExport.isValid() || pType == nullptr ) {
        entitiesInfo = "no ExportType_Entity";
        return;
    }
    size_t numArrays = 0;
    // Every `ArrayOf_Entity__*` field of the export (the entity classes:
    // Camera, Actor__Mesh, Volume__FpSelectable, Cinematic, Light...).
    for ( const XmbType::Field &field : pType->Fields ) {
        if ( !field.bArray ||
             strncmp( field.Name.c_str(), "ArrayOf_Entity", 14 ) != 0 ) {
            continue;
        }
        const uint32_t count = entityExport.getArraySize( field.Name.c_str() );
        if ( count > 0 ) {
            numArrays++;
        }
        for ( uint32_t i = 0; i < count; i++ ) {
            const XmbValue entry =
                entityExport.getArrayElement( field.Name.c_str(), i );
            if ( !entry.isValid() ) {
                continue;
            }
            EntityEntry entity;
            entity.Array = field.Name;
            entity.Class = EntityClassName( field.Name );
            entity.Name = entry["__Name"].asString();
            entity.Hashcode = entry["__Hc"].asUInt64();
            const XmbValue placement = entry["orient_pos_scale"];
            if ( placement.isValid() ) {
                const XmbValue position = placement["pos"];
                entity.Position =
                    Vector3f( position["X"].asFloat(), position["Y"].asFloat(),
                              position["Z"].asFloat() );
                const XmbValue rotation = placement["rot"];
                entity.Rotation = Quaternionf(
                    rotation["X"].asFloat(), rotation["Y"].asFloat(),
                    rotation["Z"].asFloat(), rotation["W"].asFloat( 1.0f ) );
            }
            collectFields( entry, "", 0, entity.Fields );
            entities.push_back( std::move( entity ) );
        }
    }
    char info[160] = {};
    snprintf( info, sizeof( info ), "%zu entity(ies) in %zu class array(s)",
              entities.size(), numArrays );
    entitiesInfo = info;
    buildEntityRows();
    OTDU_LOG_INFO( "AssetViewer: '%s' - %s\n", bankKey.c_str(),
                   entitiesInfo.c_str() );
}

void GMAssetViewer::buildEntityRows() {
    entityRows.clear();
    entityClasses.clear();
    const std::string filter = ToLower( entityFilterText );
    // The class filter combo is built from the classes present in the bank.
    for ( const EntityEntry &entity : entities ) {
        if ( std::find( entityClasses.begin(), entityClasses.end(),
                        entity.Class ) == entityClasses.end() ) {
            entityClasses.push_back( entity.Class );
        }
    }
    std::sort( entityClasses.begin(), entityClasses.end() );
    const std::string wanted =
        entityClassFilter > 0 && entityClassFilter <= static_cast<int32_t>(
                                                          entityClasses.size() )
            ? entityClasses[entityClassFilter - 1]
            : std::string();
    for ( int32_t i = 0; i < static_cast<int32_t>( entities.size() ); i++ ) {
        const EntityEntry &entity = entities[i];
        if ( !wanted.empty() && entity.Class != wanted ) {
            continue;
        }
        if ( !filter.empty() &&
             ToLower( entity.Name ).find( filter ) == std::string::npos &&
             ToLower( entity.Class ).find( filter ) == std::string::npos ) {
            continue;
        }
        entityRows.push_back( i );
    }
}

// ---- The cross-bank index, and the search
// -----------------------------------------

void GMAssetViewer::loadAssetIndex() {
    if ( bIndexLoaded ) {
        return;
    }
    bIndexLoaded = true;
    indexPath =
        gpFile != nullptr
            ? ( gpFile->getUserDataFolder() / "asset_index.txt" ).string()
            : std::string( "asset_index.txt" );
    std::ifstream file( indexPath );
    if ( !file ) {
        bIndexMissing = true;
        OTDU_LOG_INFO(
            "AssetIndex: no index at '%s' yet (-buildindex writes it)\n",
            indexPath.c_str() );
        return;
    }
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        if ( line[0] == 'a' ) {
            indexAssetCount++;
        } else if ( line[0] == 'e' ) {
            indexEntityCount++;
        }
        indexLines.push_back( std::move( line ) );
    }
    OTDU_LOG_INFO(
        "AssetIndex: %zu line(s) from '%s' (%zu asset(s), %zu entity(ies))\n",
        indexLines.size(), indexPath.c_str(), indexAssetCount,
        indexEntityCount );
}

void GMAssetViewer::runSearch() {
    const std::string query = ToLower( searchText );
    lastSearch = query;
    searchHits.clear();
    searchHitCount = 0;
    bSearchTruncated = false;
    if ( query.empty() ) {
        return;
    }
    loadAssetIndex();
    constexpr size_t kMaxHits = 400;
    for ( const std::string &line : indexLines ) {
        if ( !ContainsI( line, query.c_str() ) ) {
            continue;
        }
        searchHitCount++;
        if ( searchHits.size() >= kMaxHits ) {
            bSearchTruncated = true;
            continue;
        }
        // a<TAB>bank<TAB>name<TAB>path  | e<TAB>bank<TAB>class<TAB>name<TAB>pos
        std::vector<std::string> fields;
        size_t at = 2;
        while ( at <= line.size() ) {
            const size_t tab = line.find( '\t', at );
            const size_t end = tab == std::string::npos ? line.size() : tab;
            fields.push_back( line.substr( at, end - at ) );
            if ( tab == std::string::npos ) {
                break;
            }
            at = tab + 1;
        }
        IndexHit hit;
        hit.Kind = line[0];
        if ( hit.Kind == 'a' && fields.size() >= 3 ) {
            // a<TAB>bank<TAB>asset filename<TAB>authoring path
            hit.Bank = fields[0];
            hit.Name = fields[1];
            hit.Path = fields[2];
        } else if ( hit.Kind == 'e' && fields.size() >= 4 ) {
            // e<TAB>bank<TAB>entity class<TAB>entity name<TAB>x,y,z
            hit.Bank = fields[0];
            hit.Name = fields[2] + "  (" + fields[1] + ")";
            hit.Path = fields[1] + " at " + fields[3];
        } else {
            continue;
        }
        searchHits.push_back( std::move( hit ) );
    }
    OTDU_LOG_INFO( "AssetViewer: search '%s' -> %zu hit(s)%s\n", searchText,
                   searchHitCount, bSearchTruncated ? " (display cut)" : "" );
}

bool GMAssetViewer::buildAssetIndex() {
    // One pass over every bank of the game data: the entries (filename +
    // authoring path) and the entities.xmb entity class / name / position. The
    // result is a text file the Search tab reads back in a second.
    if ( gpFile == nullptr || gpRender == nullptr ) {
        return false;
    }
    indexPath = ( gpFile->getUserDataFolder() / "asset_index.txt" ).string();
    std::ofstream file( indexPath );
    if ( !file ) {
        OTDU_LOG_WARN( "AssetIndex: cannot write '%s'\n", indexPath.c_str() );
        return false;
    }
    file << "# OpenTDU2 asset index - every bank's entries and its "
            "entities.xmb entities.\n";
    file << "# Written by `-gm assetviewer -buildindex` (one pass over the "
            "game data).\n";
    file << "# a<TAB><bank><TAB><asset filename><TAB><authoring path>\n";
    file << "# e<TAB><bank><TAB><entity class><TAB><entity name><TAB><x,y,z>\n";
    size_t numAssets = 0, numEntities = 0, numBanks = 0;
    const uint64_t start = SDL_GetTicks();
    for ( const BankInfo &info : banks ) {
        // Minutes on the main thread (9,454 banks): beat the watchdog's kill
        // guard per bank (source/core/watchdog.h).
        Watchdog::Beat( "asset index build", numBanks );
        Bank indexBank;
        if ( !indexBank.load( info.Path.c_str() ) ) {
            continue;
        }
        numBanks++;
        for ( const BankEntry &entry : indexBank.getEntries() ) {
            file << "a\t" << info.Path << '\t' << entry.Filename << '\t'
                 << entry.Path << '\n';
            numAssets++;
        }
        // The entities of the bank (the classes and their names): what makes
        // "cam_entree" or "Hostess_area" find the bank that has them.
        const BankEntry *pEntities = indexBank.findEntry( "entities.xmb" );
        XmbDocument document;
        if ( pEntities != nullptr &&
             document.load( pEntities->pData, pEntities->Size ) ) {
            const XmbValue entityExport =
                document.getRoot()["ExportType_Entity"];
            const XmbType *pType = entityExport.getType();
            if ( entityExport.isValid() && pType != nullptr ) {
                for ( const XmbType::Field &field : pType->Fields ) {
                    if ( !field.bArray ||
                         strncmp( field.Name.c_str(), "ArrayOf_Entity", 14 ) !=
                             0 ) {
                        continue;
                    }
                    const std::string className = EntityClassName( field.Name );
                    const uint32_t count =
                        entityExport.getArraySize( field.Name.c_str() );
                    for ( uint32_t i = 0; i < count; i++ ) {
                        const XmbValue entry = entityExport.getArrayElement(
                            field.Name.c_str(), i );
                        if ( !entry.isValid() ) {
                            continue;
                        }
                        const XmbValue position =
                            entry["orient_pos_scale"]["pos"];
                        file << "e\t" << info.Path << '\t' << className << '\t'
                             << entry["__Name"].asString() << '\t'
                             << position["X"].asFloat() << ','
                             << position["Y"].asFloat() << ','
                             << position["Z"].asFloat() << '\n';
                        numEntities++;
                    }
                }
            }
        }
        if ( numBanks % 500 == 0 ) {
            OTDU_LOG_INFO( "AssetIndex: %zu/%zu bank(s), %zu asset(s), %zu "
                           "entity(ies), %.0f s\n",
                           numBanks, banks.size(), numAssets, numEntities,
                           ( SDL_GetTicks() - start ) / 1000.0 );
        }
    }
    file.close();
    OTDU_LOG_INFO( "AssetIndex: '%s' written: %zu bank(s), %zu asset(s), %zu "
                   "entity(ies) in %.0f s\n",
                   indexPath.c_str(), numBanks, numAssets, numEntities,
                   ( SDL_GetTicks() - start ) / 1000.0 );
    return true;
}

void GMAssetViewer::openHit( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( searchHits.size() ) ) {
        return;
    }
    const IndexHit &hit = searchHits[index];
    if ( hit.Bank != bankKey ) {
        openBank( hit.Bank );
    }
    if ( hit.Kind == 'a' ) {
        for ( int32_t i = 0; i < static_cast<int32_t>( rows.size() ); i++ ) {
            if ( StringCmpI( rows[i]->Filename.c_str(), hit.Name.c_str() ) ==
                 0 ) {
                selectAsset( i );
                focusScene( i );
                middleTab = 0;
                return;
            }
        }
        // The asset is filtered out of the list (the type filter, the text
        // filter): clear both so the selection lands.
        assetFilterText[0] = '\0';
        typeFilter = eTypeFilter::All;
        rebuildRows();
        for ( int32_t i = 0; i < static_cast<int32_t>( rows.size() ); i++ ) {
            if ( StringCmpI( rows[i]->Filename.c_str(), hit.Name.c_str() ) ==
                 0 ) {
                selectAsset( i );
                focusScene( i );
                middleTab = 0;
                return;
            }
        }
        setStatus( "'" + hit.Name + "' is not in '" + hit.Bank +
                   "' (index stale?)" );
        return;
    }
    // An entity: open the Entities tab and select it (the 3D view marks it).
    middleTab = 1;
    const std::string name = hit.Name.substr( 0, hit.Name.find( "  (" ) );
    for ( int32_t i = 0; i < static_cast<int32_t>( entities.size() ); i++ ) {
        if ( entities[i].Name == name ) {
            selectedEntity = i;
            setStatus( "entity '" + entities[i].Name + "' (" +
                       entities[i].Class + ")" );
            return;
        }
    }
    setStatus( "entity '" + name + "' not found in '" + hit.Bank + "'" );
}

// ---- The bank's entity cameras, the free camera
// ------------------------------------

void GMAssetViewer::buildCameraList() {
    cameraNames.clear();
    cameraPositions.clear();
    cameraRotations.clear();
    cameraFovs.clear();
    selectedCamera = -1;
    for ( const EntityEntry &entity : entities ) {
        if ( entity.Class != "Camera" ) {
            continue;
        }
        // Entity__Camera Fov is in radians (intro_spec: 1.0472 = 60 deg); the
        // preview camera is in degrees.
        float fov = 60.0f;
        for ( const std::pair<std::string, std::string> &field :
              entity.Fields ) {
            if ( field.first == "Fov" ) {
                fov = static_cast<float>( atof( field.second.c_str() ) ) *
                      180.0f / kPi;
            }
        }
        cameraNames.push_back( entity.Name );
        cameraPositions.push_back( entity.Position );
        cameraRotations.push_back( entity.Rotation );
        cameraFovs.push_back( fov );
    }
    if ( !cameraNames.empty() ) {
        OTDU_LOG_INFO( "AssetViewer: %zu entity camera(s), first '%s' at "
                       "(%.2f, %.2f, %.2f)\n",
                       cameraNames.size(), cameraNames.front().c_str(),
                       cameraPositions.front().x, cameraPositions.front().y,
                       cameraPositions.front().z );
    }
}

void GMAssetViewer::useEntityCamera( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( cameraNames.size() ) ) {
        return;
    }
    selectedCamera = index;
    // The entity camera pose *is* the view (walk_mode's convention: the
    // orient_pos_scale rotation is the Eden camera rotation).
    cameraOverridePosition = cameraPositions[index];
    cameraOverrideRotation = cameraRotations[index];
    cameraOverrideFov = cameraFovs[index];
    bCameraOverride = true;
    bTurntable = false;
    bFreeCamera = false;
    OTDU_LOG_INFO(
        "AssetViewer: view from '%s' (%.2f, %.2f, %.2f), fov %.1f deg\n",
        cameraNames[index].c_str(), cameraOverridePosition.x,
        cameraOverridePosition.y, cameraOverridePosition.z, cameraOverrideFov );
    setStatus( "view from camera '" + cameraNames[index] + "'" );
}

void GMAssetViewer::updateFreeCamera( float deltaTime ) {
    // WASD / QE while the 3D view is the active panel (it is the tool's only
    // keyboard consumer): an interior can be walked through instead of looked
    // at from outside.
    if ( !bFreeCamera ) {
        return;
    }
    const Vector3f forward = Vector3f(
        std::sin( sceneYaw ) * std::cos( scenePitch ), std::sin( scenePitch ),
        std::cos( sceneYaw ) * std::cos( scenePitch ) );
    Vector3f right = forward.cross( Vector3f( 0.0f, 1.0f, 0.0f ) );
    right = right.length() > 1e-4f ? right.normalized()
                                   : Vector3f( -1.0f, 0.0f, 0.0f );
    Vector3f move;
    if ( ImGui::IsKeyDown( ImGuiKey_W ) ) {
        move += forward;
    }
    if ( ImGui::IsKeyDown( ImGuiKey_S ) ) {
        move -= forward;
    }
    if ( ImGui::IsKeyDown( ImGuiKey_D ) ) {
        move += right;
    }
    if ( ImGui::IsKeyDown( ImGuiKey_A ) ) {
        move -= right;
    }
    if ( ImGui::IsKeyDown( ImGuiKey_E ) ||
         ImGui::IsKeyDown( ImGuiKey_Space ) ) {
        move += Vector3f( 0.0f, 1.0f, 0.0f );
    }
    if ( ImGui::IsKeyDown( ImGuiKey_Q ) ) {
        move -= Vector3f( 0.0f, 1.0f, 0.0f );
    }
    if ( move.length() < 1e-4f ) {
        return;
    }
    const float speed =
        freeSpeed * ( ImGui::IsKeyDown( ImGuiKey_LeftShift ) ? 4.0f : 1.0f );
    freePosition += move.normalized() * ( speed * deltaTime );
}

// ---- `.cin` / `.sce` previews, and the `.shk` collision overlay
// --------------------

void GMAssetViewer::loadCinematicPreview() {
    cinematicInfo.clear();
    if ( selectedAsset < 0 ||
         selectedAsset >= static_cast<int32_t>( rows.size() ) ) {
        return;
    }
    const BankEntry &entry = *rows[selectedAsset];
    char info[512] = {};
    if ( entry.Extension == "cin" ) {
        Cinematic cinematic;
        if ( !cinematic.parse( entry.pData, entry.Size ) ) {
            cinematicInfo = "cin: parse failed";
            return;
        }
        snprintf( info, sizeof( info ),
                  "cinematic: %.1f s, %zu shot(s), %zu scene(s), %zu "
                  "message(s), %zu fx event(s)",
                  cinematic.getDuration(), cinematic.getShots().size(),
                  cinematic.getSceneReferences().size(),
                  cinematic.getMessages().size(),
                  cinematic.getFxEvents().size() );
        cinematicInfo = info;
        std::string shots;
        for ( const Cinematic::Shot &shot : cinematic.getShots() ) {
            char line[64] = {};
            snprintf( line, sizeof( line ), "%.1fs", shot.Duration );
            shots += shots.empty() ? line : std::string( " + " ) + line;
        }
        if ( !shots.empty() ) {
            cinematicInfo += "\nshots: " + shots;
        }
        // The scene paths the shots reference (what the cinematic pulls in).
        std::string scenes;
        for ( const Cinematic::Reference &reference :
              cinematic.getReferences() ) {
            if ( reference.Text.find( ".sce" ) != std::string::npos ) {
                scenes +=
                    scenes.empty() ? reference.Text : ", " + reference.Text;
            }
        }
        if ( !scenes.empty() ) {
            cinematicInfo += "\nscenes: " + scenes;
        }
    } else {
        CinematicScene scene;
        if ( !scene.parseScene( entry.pData, entry.Size ) ) {
            cinematicInfo = "sce: parse failed";
            return;
        }
        size_t numActors = 0, numCameras = 0;
        std::string actors;
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            if ( object.isCamera() ) {
                numCameras++;
            } else {
                numActors++;
                actors += actors.empty() ? object.Name : ", " + object.Name;
            }
        }
        snprintf( info, sizeof( info ),
                  "scene: %.1f s, %zu object(s): %zu actor(s), %zu camera(s)",
                  scene.getDuration(), scene.getObjects().size(), numActors,
                  numCameras );
        cinematicInfo = info;
        if ( !actors.empty() ) {
            cinematicInfo += "\nactors: " + actors;
        }
    }
    OTDU_LOG_INFO( "AssetViewer: '%s' - %s\n", entry.Filename.c_str(),
                   cinematicInfo.c_str() );
}

void GMAssetViewer::loadCollisionPreview() {
    collision.clear();
    bCollisionLoaded = false;
    bShowCollision = false;
    collisionInfo.clear();
    if ( !bank.isLoaded() ) {
        return;
    }
    for ( const BankEntry &entry : bank.getEntries() ) {
        if ( entry.Extension != "shk" && entry.Extension != "dhk" ) {
            continue;
        }
        if ( !LoadHavokCollision( entry.pData, entry.Size, collision,
                                  false ) ) {
            continue;
        }
        bCollisionLoaded = true;
        char info[224] = {};
        snprintf(
            info, sizeof( info ),
            "%s: %zu triangle(s), %zu box(es), %zu capsule(s), %zu body(ies)",
            entry.Filename.c_str(), collision.getNumTriangles(),
            collision.Boxes.size(), collision.Capsules.size(),
            collision.Bodies.size() );
        collisionInfo = info;
        OTDU_LOG_INFO( "AssetViewer: collision '%s'\n", collisionInfo.c_str() );
        return;
    }
}

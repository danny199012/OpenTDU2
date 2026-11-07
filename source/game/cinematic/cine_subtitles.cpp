#include "shared.h"
#include "cine_subtitles.h"

#include "cinematic_audio.h"
#include "filesystem/gs_file.h"
#include "flash/gs_flash.h"
#include "render/renderer_2d.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace {
// The caption band (canvas 1280x720, the frontend authoring resolution).
constexpr float kCanvasW = 1280.0f;
constexpr float kCanvasH = 720.0f;
constexpr float kBandY = 630.0f;
constexpr float kBandH = 54.0f;
constexpr float kTextSize = 22.0f;

uint32_t Rgba( uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255 ) {
    return r | ( g << 8 ) | ( b << 16 ) | ( a << 24 );
}

// One CSV row: start,end,"text" ('""' inside the text is a literal quote).
std::string CsvRow( float start, float end, const std::string &text ) {
    char line[32];
    snprintf( line, sizeof( line ), "%.2f,%.2f,", start, end );
    std::string out = line;
    out += '"';
    for ( const char c : text ) {
        out += c;
        if ( c == '"' ) {
            out += '"';
        }
    }
    out += "\"\n";
    return out;
}

// start,end[,text] with optional quotes; false on malformed rows.
bool ParseRow( const std::string &line, CineSubtitles::Cue &outCue ) {
    const size_t firstComma = line.find( ',' );
    if ( firstComma == std::string::npos ) {
        return false;
    }
    const size_t secondComma = line.find( ',', firstComma + 1 );
    if ( secondComma == std::string::npos ) {
        return false;
    }
    outCue.Start = static_cast<float>( atof( line.substr( 0, firstComma ).c_str() ) );
    outCue.End = static_cast<float>( atof( line.substr( firstComma + 1, secondComma - firstComma - 1 ).c_str() ) );
    std::string text = line.substr( secondComma + 1 );
    // Trim.
    const auto isSpace = []( unsigned char c ) { return isspace( c ) != 0; };
    while ( !text.empty() && isSpace( static_cast<unsigned char>( text.front() ) ) ) {
        text.erase( text.begin() );
    }
    while ( !text.empty() && isSpace( static_cast<unsigned char>( text.back() ) ) ) {
        text.pop_back();
    }
    if ( text.size() >= 2 && text.front() == '"' && text.back() == '"' ) {
        text = text.substr( 1, text.size() - 2 );
        // Unescape "" -> ".
        std::string unescaped;
        for ( size_t i = 0; i < text.size(); i++ ) {
            unescaped += text[i];
            if ( text[i] == '"' && i + 1 < text.size() && text[i + 1] == '"' ) {
                i++;
            }
        }
        text = unescaped;
    }
    outCue.Text = text;
    return true;
}
} // namespace

std::string CineSubtitles::Folder() {
    return ( gpFile->getUserDataFolder() / "cine_subtitles" ).string();
}

bool CineSubtitles::load( const std::string &cinematicName ) {
    cues.clear();
    const std::string path = Folder() + "/" + cinematicName + ".csv";
    std::ifstream file( path, std::ios::binary );
    if ( !file ) {
        return false;
    }
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        Cue cue;
        if ( ParseRow( line, cue ) ) {
            cues.push_back( std::move( cue ) );
        }
    }
    std::sort( cues.begin(), cues.end(),
               []( const Cue &a, const Cue &b ) { return a.Start < b.Start; } );
    OTDU_LOG_INFO( "CineSubtitles: %zu cue(s) from '%s'\n", cues.size(),
                   path.c_str() );
    return !cues.empty();
}

const std::string &CineSubtitles::textAt( float time ) const {
    static const std::string kNone;
    for ( const Cue &cue : cues ) {
        if ( time >= cue.Start && time < cue.End ) {
            return cue.Text;
        }
    }
    return kNone;
}

void CineSubtitles::draw( Renderer2D &renderer, float time ) const {
    const std::string &text = textAt( time );
    if ( text.empty() ) {
        return;
    }
    const Font *pFont = gpFlash->getTitleFont();
    if ( pFont == nullptr ) {
        return;
    }
    renderer.begin();
    renderer.drawRect( Rect2D( 100.0f, kBandY, kCanvasW - 200.0f, kBandH ),
                       0xb0000000u );
    renderer.drawText( pFont, text.c_str(), kCanvasW * 0.5f,
                       kBandY + kBandH * 0.5f - kTextSize * 0.5f, kTextSize,
                       Rgba( 255, 255, 255 ), eTextAlign::TA_Center );
    renderer.end();
}

std::string CineSubtitles::DumpSkeleton( const std::string &cinematicName,
                                         const Cinematic &cine,
                                         const std::vector<CinematicScene> &scenes,
                                         const CinematicSounds &sounds ) {
    // The voice cues in cinematic time: the .cin Fx events, plus the .sce
    // scene Fx tracks mapped through the shots (the runtime plays them
    // through updateSceneFx at the shot's scene window).
    std::vector<Cue> rows;
    for ( const Cinematic::Event &event : cine.getFxEvents() ) {
        if ( event.Reference >= 0 &&
             event.Reference < static_cast<int32_t>( cine.getReferences().size() ) &&
             sounds.isVoiceRoot( cine.getReferences()[event.Reference].Hashcode ) ) {
            rows.push_back( { event.Time, event.Time + event.Duration, std::string() } );
        }
    }
    for ( size_t sceneIndex = 0; sceneIndex < scenes.size(); sceneIndex++ ) {
        const CinematicScene &scene = scenes[sceneIndex];
        const std::vector<CinematicScene::Reference> &sceneRefs = scene.getReferences();
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            const CinematicScene::Track *pFx =
                object.findTrack( CinematicScene::kTrackFx );
            if ( pFx == nullptr || pFx->Times.empty() ) {
                continue;
            }
            for ( size_t k = 0; k < pFx->Times.size(); k++ ) {
                const float *pValue = &pFx->Values[k * pFx->NumComponents];
                uint32_t refIndex;
                memcpy( &refIndex, pValue, sizeof( refIndex ) );
                const float duration = pValue[3];
                if ( refIndex >= sceneRefs.size() ||
                     !sounds.isVoiceRoot( sceneRefs[refIndex].Hashcode ) ) {
                    continue;
                }
                const float keyTime = pFx->Times[k];
                // The shots that play this scene window: the key's cinematic
                // time (rate = scene time per cinematic time).
                for ( const Cinematic::Shot &shot : cine.getShots() ) {
                    if ( shot.Scene != sceneIndex || shot.Duration <= 0.0f ) {
                        continue;
                    }
                    const float rate = ( shot.CameraEnd - shot.CameraStart ) / shot.Duration;
                    if ( keyTime < shot.CameraStart - 0.001f ||
                         keyTime > shot.CameraEnd + 0.001f ) {
                        continue;
                    }
                    const float t =
                        shot.Start + ( keyTime - shot.CameraStart ) / rate;
                    rows.push_back( { t, t + duration, std::string() } );
                }
            }
        }
    }
    // The same root fired from two objects (meeting_tess line 06) lands as
    // duplicate rows - keep one per (start, end).
    std::sort( rows.begin(), rows.end(),
               []( const Cue &a, const Cue &b ) { return a.Start < b.Start; } );
    rows.erase( std::unique( rows.begin(), rows.end(),
                             []( const Cue &a, const Cue &b ) {
                                 return fabsf( a.Start - b.Start ) < 0.01f &&
                                        fabsf( a.End - b.End ) < 0.01f;
                             } ),
                rows.end() );
    if ( rows.empty() ) {
        return std::string();
    }
    const std::string folder = Folder();
    std::error_code ec;
    std::filesystem::create_directories( folder, ec );
    const std::string path = folder + "/" + cinematicName + ".csv.skeleton";
    if ( std::filesystem::exists( std::filesystem::path( Folder() + "/" + cinematicName + ".csv" ) ) ) {
        OTDU_LOG_INFO( "CineSubtitles: '%s' already has a captions csv, the skeleton is not written\n",
                       cinematicName.c_str() );
        return std::string();
    }
    std::ofstream file( path, std::ios::binary );
    if ( !file ) {
        return std::string();
    }
    file << "# subtitle skeleton for " << cinematicName << " (fill the third "
            "column; rename to " << cinematicName << ".csv)\n";
    for ( const Cue &cue : rows ) {
        file << CsvRow( cue.Start, cue.End, cue.Text );
    }
    OTDU_LOG_INFO( "CineSubtitles: skeleton '%s' (%zu voice cue(s))\n",
                   path.c_str(), rows.size() );
    return path;
}

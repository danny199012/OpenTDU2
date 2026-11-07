// Shared helpers of the otdu2_cine_tool commands (dump / list / manifest):
// formatting, the minimal JSON writer and bank discovery. Header-only so the
// command files stay standalone.
#pragma once

#include "shared.h"

#include "core/math.h"
#include "filesystem/bank.h"
#include "game/cinematic/cinematic_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// formatting helpers

inline std::string F( float value ) {
    char buffer[32];
    snprintf( buffer, sizeof( buffer ), "%.4g", value );
    return buffer;
}

// Some key values in the data are NaN/Inf (unused tracks); emit null so the
// JSON stays valid.
inline std::string JsonNum( float value ) {
    return std::isfinite( value ) ? F( value ) : std::string( "null" );
}

inline std::string Vec3( const Vector3f &v ) {
    return "(" + F( v.x ) + ", " + F( v.y ) + ", " + F( v.z ) + ")";
}

inline std::string Hash32( uint32_t hash ) {
    char buffer[16];
    snprintf( buffer, sizeof( buffer ), "0x%08x", hash );
    return buffer;
}

inline std::string Hash64( uint64_t hash ) {
    char buffer[24];
    snprintf( buffer, sizeof( buffer ), "0x%016llx",
              static_cast<unsigned long long>( hash ) );
    return buffer;
}

inline std::string TailFile( const std::string &path ) {
    const size_t slash = path.find_last_of( "/\\" );
    return slash == std::string::npos ? path : path.substr( slash + 1 );
}

// Track ids are folded names (CinematicNameHash); name the known ones.
inline std::string TrackKindName( uint32_t id ) {
    switch ( id ) {
    case CinematicScene::kTrackPosition:
        return "Position";
    case CinematicScene::kTrackRotation:
        return "Heading";
    case CinematicScene::kTrackScale:
        return "Scale";
    case CinematicScene::kTrackFov:
        return "Fov";
    case CinematicScene::kTrackMessage:
        return "Message";
    case CinematicScene::kTrackFx:
        return "Fx";
    case CinematicScene::kTrackProperty:
        return "Property";
    }
    if ( id == CinematicNameHash( "Animation" ) )
        return "Animation";
    if ( id == CinematicNameHash( "Visibility" ) )
        return "Visibility";
    if ( id == CinematicNameHash( "Morph" ) )
        return "Morph";
    return Hash32( id );
}

// The .sce origin is a D3D row-vector matrix; its third row is the frame
// forward axis. Heading convention of docs/re/cutscenes_inventory.md:
// degrees from +Z towards +X, Y up.
inline float OriginHeadingDeg( const Matrix4x4f &origin ) {
    const Vector3f fwd( origin.m[2][0], origin.m[2][1], origin.m[2][2] );
    return std::atan2( fwd.x, fwd.z ) * kRadToDeg;
}

// Yaw of a Heading quaternion, same convention (forward = q * (0,0,1)).
inline float YawDeg( const Quaternionf &q ) {
    const Vector3f fwd = q.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
    return std::atan2( fwd.x, fwd.z ) * kRadToDeg;
}

// ---------------------------------------------------------------------------
// minimal JSON writer (indented, exact float text kept short)

inline std::string JsonQuote( const std::string &text ) {
    std::string out = "\"";
    for ( const char c : text ) {
        switch ( c ) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if ( static_cast<unsigned char>( c ) < 0x20 ||
                 static_cast<unsigned char>( c ) >= 0x80 ) {
                // Data names can carry Latin-1 accents; emit them as \uXXXX
                // so the file stays valid UTF-8 for strict parsers.
                char buffer[8];
                snprintf( buffer, sizeof( buffer ), "\\u%04x",
                          static_cast<unsigned char>( c ) );
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += "\"";
    return out;
}

class JsonOut {
public:
    JsonOut() {
        scopes_.push_back( Scope() );
        scopes_.back().bInline = true; // no newline before the root brace
    }

    void beginObject() {
        separate();
        buf_ += "{";
        push_scope();
    }
    void beginArray() {
        separate();
        buf_ += "[";
        push_scope();
    }
    void endObject() { end_scope( "}" ); }
    void endArray() { end_scope( "]" ); }

    void key( const std::string &name ) {
        separate();
        buf_ += JsonQuote( name ) + ": ";
        scopes_.back().bAwaitValue = true;
    }
    void str( const std::string &name, const std::string &value ) {
        key( name );
        buf_ += JsonQuote( value );
        scopes_.back().bAwaitValue = false;
    }
    void num( const std::string &name, double value ) {
        key( name );
        buf_ += JsonNum( static_cast<float>( value ) );
        scopes_.back().bAwaitValue = false;
    }
    void boolean( const std::string &name, bool value ) {
        key( name );
        buf_ += value ? "true" : "false";
        scopes_.back().bAwaitValue = false;
    }
    void hash( const std::string &name, uint64_t value ) {
        key( name );
        buf_ += JsonQuote( Hash64( value ) );
        scopes_.back().bAwaitValue = false;
    }
    void raw( const std::string &name, const std::string &raw ) {
        key( name );
        buf_ += raw;
        scopes_.back().bAwaitValue = false;
    }
    void value( const std::string &raw ) {
        separate();
        buf_ += raw;
    } // array element

    const std::string &str() const { return buf_; }

private:
    struct Scope {
        bool bHasItems = false;
        bool bInline = false;     // the next item continues on the same line
        bool bAwaitValue = false; // a key was written, its value comes next
    };
    void push_scope() {
        scopes_.push_back( Scope() );
        scopes_.back().bInline = true; // first item on the brace line
    }
    // Exactly one separator per item: a key is an item, its value is the
    // continuation that bAwaitValue allows to skip a second separator.
    void separate() {
        Scope &scope = scopes_.back();
        if ( scope.bAwaitValue ) {
            scope.bAwaitValue = false;
            return;
        }
        if ( scope.bHasItems ) {
            buf_ += ",";
        }
        if ( !scope.bInline ) {
            buf_ += "\n";
            buf_.append( ( scopes_.size() - 1 ) * 2, ' ' );
        }
        scope.bHasItems = true;
        scope.bInline = false;
    }
    void end_scope( const char *close ) {
        const Scope scope = scopes_.back();
        scopes_.pop_back();
        if ( scope.bHasItems ) {
            buf_ += "\n";
            buf_.append( ( scopes_.size() - 1 ) * 2, ' ' );
        }
        buf_ += close;
        scopes_.back().bAwaitValue = false;
    }

    std::string buf_;
    std::vector<Scope> scopes_;
};

// ---------------------------------------------------------------------------
// bank discovery

// "meeting_tess__us" (language variant bank) -> true
inline bool IsVariantBank( const std::string &stem ) {
    const size_t pos = stem.rfind( "__" );
    return pos != std::string::npos && pos + 4 == stem.size();
}

inline void CollectBanks( const std::string &source,
                          bool bVariants,
                          std::vector<fs::path> &out ) {
    const fs::path root( source );
    if ( fs::is_regular_file( root ) ) {
        out.push_back( root );
        return;
    }
    fs::path dir = root;
    // A game root (the folder with Euro/) is accepted as well.
    const fs::path candidate =
        root / "Euro" / "Bnk" / "scenarics" / "cinematics";
    if ( fs::is_directory( candidate ) ) {
        dir = candidate;
    }
    std::error_code ec;
    fs::recursive_directory_iterator it(
        dir, fs::directory_options::skip_permission_denied, ec );
    if ( ec ) {
        return;
    }
    for ( const fs::directory_entry &entry : it ) {
        if ( !entry.is_regular_file() ||
             StringCmpI( entry.path().extension().string().c_str(), ".bnk" ) !=
                 0 ) {
            continue;
        }
        if ( !bVariants && IsVariantBank( entry.path().stem().string() ) ) {
            continue;
        }
        out.push_back( entry.path() );
    }
    std::sort( out.begin(), out.end() );
}

// ---------------------------------------------------------------------------
// per cinematic parse shared by dump and manifest

struct ParsedScene {
    std::string displayName; // scene file name, or the ref text if unresolved
    bool ok = false;
    CinematicScene scene;
};

struct CinSummary {
    std::string bankStem;
    std::string cinName;
    float duration = 0.0f;
    int shots = 0;
    int scenes = 0;
    int cameras = 0;
    int actors = 0;
    int messages = 0;
    int fx = 0;
    bool ok = false;
};

// Parse a .cin and its .sce scenes of this bank (the dump/manifest preamble).
inline bool ParseCinematic( const Bank &bank,
                            const BankEntry &entry,
                            Cinematic &cine,
                            std::vector<ParsedScene> &scenes ) {
    if ( !cine.parse( entry.pData, entry.Size ) ) {
        return false;
    }
    const std::vector<Cinematic::Reference> &refs = cine.getReferences();
    for ( int32_t refIndex : cine.getSceneReferences() ) {
        ParsedScene parsed;
        if ( refIndex >= 0 && refIndex < static_cast<int32_t>( refs.size() ) ) {
            parsed.displayName = TailFile( refs[refIndex].Text );
            const BankEntry *pScene =
                bank.findEntry( parsed.displayName.c_str() );
            if ( pScene != nullptr ) {
                parsed.ok =
                    parsed.scene.parseScene( pScene->pData, pScene->Size );
            }
        }
        scenes.push_back( std::move( parsed ) );
    }
    return true;
}

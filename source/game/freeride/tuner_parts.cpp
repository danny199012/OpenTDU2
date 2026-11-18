#include "shared.h"
#include "tuner_parts.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_car.h"
#include "render/gs_render.h"
#include "render/image_file.h"
#include "render/texture_cache.h"
#include "system/cmd_line.h"

#include <cstring>

namespace {

    // The data file and the part folder, inside the mods overlay (the enabled
    // tuner-garage package layer, or a loose <mods>/tuner/ - the overlay
    // rules).
    constexpr const char *kTunerFile = "tuner/tuner.txt";
    constexpr const char *kPartsFolder = "tuner/parts/";

    std::string Trim( const std::string &text ) {
        size_t a = text.find_first_not_of( " \t" );
        if ( a == std::string::npos ) {
            return std::string();
        }
        size_t b = text.find_last_not_of( " \t" );
        return text.substr( a, b - a + 1 );
    }

    bool ParseVector3( const std::string &text, Vector3f &out ) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if ( sscanf( text.c_str(), "%f %f %f", &x, &y, &z ) != 3 ) {
            return false;
        }
        out = Vector3f( x, y, z );
        return true;
    }

    // The DBCarPhysicsData row of a car id (the database has no by-id lookup,
    // so this walks the 279 file names once per call - nothing per frame).
    bool FindCarById( int32_t carId, CarPhysicsData &outCar ) {
        if ( gpDatabase == nullptr || carId < 0 ) {
            return false;
        }
        for ( const std::string &fileName : gpDatabase->getCarFileNames() ) {
            if ( gpDatabase->findCarPhysics( fileName.c_str(), outCar ) &&
                 outCar.CarId == carId ) {
                return true;
            }
        }
        return false;
    }

} // namespace

TunerParts &TunerParts::get() {
    static TunerParts instance;
    return instance;
}

TunerParts &GetTunerParts() { return TunerParts::get(); }

bool TunerParts::load() {
    if ( bLoaded ) {
        return true;
    }
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( kTunerFile, data ) ) {
        return false; // the mod's data is not in the overlay: an empty catalog
    }
    // The INI-like reader the other data files use (sounds.ini, menu_rows.ini):
    // [section] blocks, key = value lines, '#' comments.
    const std::string text( data.begin(), data.end() );
    std::string block; // "part" / "finish" / "car", and the id after the ':'
    TunerPart part;
    TunerFinish finish;
    TunerFlame flame;
    TunerCar car;
    bool bInPart = false, bInFinish = false, bInCar = false, bInFlame = false;
    auto flush = [&]() {
        if ( bInPart && !part.Id.empty() && !part.Mesh.empty() ) {
            parts.push_back( part );
        }
        if ( bInFinish && !finish.Id.empty() && !finish.Texture.empty() ) {
            finishes.push_back( finish );
        }
        if ( bInFlame && !flame.Id.empty() ) {
            flames.push_back( flame );
        }
        if ( bInCar && !car.FileName.empty() && !car.Slots.empty() ) {
            cars.push_back( car );
        }
        part = TunerPart();
        finish = TunerFinish();
        flame = TunerFlame();
        car = TunerCar();
        bInPart = bInFinish = bInCar = bInFlame = false;
    };
    size_t pos = 0;
    while ( pos <= text.size() ) {
        size_t end = text.find( '\n', pos );
        if ( end == std::string::npos ) {
            end = text.size();
        }
        std::string line = text.substr( pos, end - pos );
        // A CRLF checkout (core.autocrlf) leaves a '\r' that Trim keeps: it
        // ended up in `mesh = wing_gt.obj` and the OBJ was never found.
        while ( !line.empty() && ( line.back() == '\r' || line.back() == '\n' ) ) {
            line.pop_back();
        }
        line = Trim( line );
        pos = end + 1;
        if ( pos > text.size() ) {
            break;
        }
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        if ( line[0] == '[' ) {
            const size_t close = line.find( ']' );
            if ( close == std::string::npos ) {
                continue;
            }
            const std::string section = Trim( line.substr( 1, close - 1 ) );
            const size_t colon = section.find( ':' );
            flush();
            if ( colon != std::string::npos ) {
                block = ToLower( Trim( section.substr( 0, colon ) ) );
                const std::string id = Trim( section.substr( colon + 1 ) );
                if ( block == "part" ) {
                    part = TunerPart();
                    part.Id = id;
                    bInPart = true;
                } else if ( block == "finish" ) {
                    finish = TunerFinish();
                    finish.Id = ToLower( id );
                    bInFinish = true;
                } else if ( block == "flame" ) {
                    flame = TunerFlame();
                    flame.Id = ToLower( id );
                    bInFlame = true;
                } else if ( block == "car" ) {
                    car = TunerCar();
                    car.FileName = id;
                    bInCar = true;
                }
            }
            continue;
        }
        const size_t eq = line.find( '=' );
        if ( eq == std::string::npos ) {
            continue;
        }
        const std::string key = ToLower( Trim( line.substr( 0, eq ) ) );
        const std::string value = Trim( line.substr( eq + 1 ) );
        if ( bInPart ) {
            if ( key == "name" ) {
                part.Name = value;
            } else if ( key == "slot" ) {
                part.Slot = ToLower( value );
            } else if ( key == "price" ) {
                part.Price = atoi( value.c_str() );
            } else if ( key == "mesh" ) {
                part.Mesh = value;
            } else if ( key == "offset" ) {
                ParseVector3( value, part.Offset );
            } else if ( key == "node" ) {
                part.Node = value;
            } else if ( key == "align" ) {
                part.Align = ToLower( value );
            } else if ( key == "tip" ) {
                Vector3f tip;
                if ( ParseVector3( value, tip ) ) {
                    part.Tips.push_back( tip );
                }
            }
        } else if ( bInFinish ) {
            if ( key == "name" ) {
                finish.Name = value;
            } else if ( key == "texture" ) {
                finish.Texture = value;
            } else if ( key == "tint" ) {
                ParseVector3( value, finish.Tint );
            } else if ( key == "slot" ) {
                finish.Slot = ToLower( value );
            } else if ( key == "paint" ) {
                finish.bBodyPaint = ToLower( value ) == "body";
            }
        } else if ( bInFlame ) {
            if ( key == "name" ) {
                flame.Name = value;
            } else if ( key == "core" ) {
                ParseVector3( value, flame.Core );
            } else if ( key == "body" ) {
                ParseVector3( value, flame.Body );
            }
        } else if ( bInCar ) {
            if ( key == "slots" ) {
                // comma separated
                size_t cursor = 0;
                while ( cursor < value.size() ) {
                    const size_t comma = value.find( ',', cursor );
                    const std::string slot = Trim( value.substr(
                        cursor, comma == std::string::npos ? std::string::npos
                                                           : comma - cursor ) );
                    if ( !slot.empty() ) {
                        car.Slots.push_back( ToLower( slot ) );
                    }
                    if ( comma == std::string::npos ) {
                        break;
                    }
                    cursor = comma + 1;
                }
            } else if ( key.compare( 0, 10, "hide_tris_" ) == 0 ) {
                // hide_tris_<slot> = <item index>: <tri>,<tri>,... - the exact
                // triangle list of one body item (indices are triangle numbers
                // of that item, as -logcaritems orders them). Before the
                // hide_ prefix check: these keys start with hide_ too.
                const std::string slot = ToLower( key.substr( 10 ) );
                const size_t colon = value.find( ':' );
                if ( colon != std::string::npos ) {
                    const size_t item = static_cast<size_t>(
                        strtoull( value.c_str(), nullptr, 10 ) );
                    size_t cursor = colon + 1;
                    while ( cursor < value.size() ) {
                        const size_t comma = value.find( ',', cursor );
                        const std::string token = Trim(
                            value.substr( cursor, comma == std::string::npos
                                                      ? std::string::npos
                                                      : comma - cursor ) );
                        if ( !token.empty() ) {
                            car.HideTris[slot][item].push_back(
                                static_cast<uint32_t>(
                                    atoi( token.c_str() ) ) );
                        }
                        if ( comma == std::string::npos ) {
                            break;
                        }
                        cursor = comma + 1;
                    }
                }
            } else if ( key.compare( 0, 5, "hide_" ) == 0 ) {
                float v[6];
                if ( sscanf( value.c_str(), "%f %f %f %f %f %f", &v[0], &v[1],
                             &v[2], &v[3], &v[4], &v[5] ) == 6 ) {
                    car.HideBoxes[key.substr( 5 )].push_back(
                        { Vector3f( v[0], v[1], v[2] ),
                          Vector3f( v[3], v[4], v[5] ) } );
                }
            } else if ( key == "pops" ) {
                car.Pops = static_cast<float>( atof( value.c_str() ) );
            } else if ( key.size() > 7 &&
                        key.compare( key.size() - 7, 7, "_offset" ) == 0 ) {
                Vector3f offset;
                if ( ParseVector3( value, offset ) ) {
                    car.SlotOffsets[key.substr( 0, key.size() - 7 )] = offset;
                }
            }
        }
    }
    flush();
    bLoaded = true;
    OTDU_LOG_INFO( "Tuner: %zu parts, %zu finishes, %zu flame styles, %zu "
                   "tuner cars ('%s'%s)\n",
                   parts.size(), finishes.size(), flames.size(), cars.size(),
                   kTunerFile,
                   CmdLine::HasArg( "-logtuner" ) ? ", -logtuner" : "" );
    for ( const TunerCar &entry : cars ) {
        OTDU_LOG_INFO( "Tuner: car '%s' (slots: %s)\n", entry.FileName.c_str(),
                       entry.Slots.empty() ? "-" : entry.Slots[0].c_str() );
    }
    return true;
}

const TunerCar *TunerParts::findCar( int32_t carId ) const {
    const_cast<TunerParts *>( this )->load();
    CarPhysicsData row;
    if ( !FindCarById( carId, row ) ) {
        return nullptr;
    }
    for ( const TunerCar &car : cars ) {
        if ( StringCmpI( car.FileName.c_str(), row.FileName.c_str() ) == 0 ) {
            return &car;
        }
    }
    return nullptr;
}

bool TunerParts::isTunerCar( int32_t carId ) const {
    return findCar( carId ) != nullptr;
}

std::vector<const TunerPart *>
TunerParts::partsForSlot( int32_t carId, const char *pSlot ) const {
    std::vector<const TunerPart *> out;
    const TunerCar *pCar = findCar( carId );
    if ( pCar == nullptr || pSlot == nullptr ) {
        return out;
    }
    bool bTakes = false;
    for ( const std::string &slot : pCar->Slots ) {
        if ( StringCmpI( slot.c_str(), pSlot ) == 0 ) {
            bTakes = true;
            break;
        }
    }
    if ( !bTakes ) {
        return out;
    }
    for ( const TunerPart &part : parts ) {
        if ( StringCmpI( part.Slot.c_str(), pSlot ) == 0 ) {
            out.push_back( &part );
        }
    }
    return out;
}

const TunerPart *TunerParts::findPart( const std::string &id ) const {
    const_cast<TunerParts *>( this )->load();
    for ( const TunerPart &part : parts ) {
        if ( StringCmpI( part.Id.c_str(), id.c_str() ) == 0 ) {
            return &part;
        }
    }
    return nullptr;
}

const TunerFinish *TunerParts::findFinish( const std::string &id ) const {
    const_cast<TunerParts *>( this )->load();
    for ( const TunerFinish &finish : finishes ) {
        if ( StringCmpI( finish.Id.c_str(), id.c_str() ) == 0 ) {
            return &finish;
        }
    }
    return nullptr;
}

static std::string FinishCacheKey( const TunerFinish &finish,
                                   uint32_t bodyColour ) {
    char key[64];
    if ( finish.bBodyPaint ) {
        snprintf( key, sizeof( key ), "%s#%08x", finish.Id.c_str(),
                  bodyColour );
        return key;
    }
    return finish.Id;
}

bool TunerParts::finishPixels( const TunerFinish &finish,
                               uint32_t bodyColour,
                               std::vector<uint8_t> &pixels,
                               uint32_t &width,
                               uint32_t &height ) const {
    const std::string texturePath = std::string( kPartsFolder ) + finish.Texture;
    auto resolved = gpFile->resolvePath( texturePath.c_str() );
    if ( !resolved || !LoadImageFileRGBA( *resolved, pixels, width, height ) ||
         width == 0 || height == 0 ) {
        return false;
    }
    // The tint is baked into the pixels: any colour is a data row (gold is
    // chrome.png with a tint = ), no shader or texture change needed. A
    // `paint = body` finish multiplies the car's own paint colour in.
    Vector3f tint = finish.Tint;
    if ( finish.bBodyPaint ) {
        tint = Vector3f( static_cast<float>( bodyColour & 0xff ) / 255.0f,
                         static_cast<float>( ( bodyColour >> 8 ) & 0xff ) / 255.0f,
                         static_cast<float>( ( bodyColour >> 16 ) & 0xff ) / 255.0f );
    }
    if ( tint.x != 1.0f || tint.y != 1.0f || tint.z != 1.0f ) {
        for ( size_t i = 0; i + 3 < pixels.size(); i += 4 ) {
            pixels[i + 0] = static_cast<uint8_t>(
                Min( 255.0f, static_cast<float>( pixels[i + 0] ) * tint.x ) );
            pixels[i + 1] = static_cast<uint8_t>(
                Min( 255.0f, static_cast<float>( pixels[i + 1] ) * tint.y ) );
            pixels[i + 2] = static_cast<uint8_t>(
                Min( 255.0f, static_cast<float>( pixels[i + 2] ) * tint.z ) );
        }
    }
    return true;
}

GPUTexture *TunerParts::finishTexture( const TunerFinish &finish,
                                       uint32_t bodyColour ) {
    const std::string cacheKey = FinishCacheKey( finish, bodyColour );
    const auto cached = finishTextures.find( cacheKey );
    if ( cached != finishTextures.end() ) {
        return cached->second;
    }
    if ( gpRender == nullptr ) {
        return nullptr; // no renderer yet: the caller retries (drawOffscreen)
    }
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    if ( !finishPixels( finish, bodyColour, pixels, width, height ) ) {
        OTDU_LOG_WARN( "Tuner: finish '%s': '%s%s' is not readable\n",
                       finish.Id.c_str(), kPartsFolder, finish.Texture.c_str() );
        finishTextures[cacheKey] = nullptr;
        return nullptr;
    }
    // Named per finish (not per texture) so two finishes on one base PNG stay
    // distinct in the texture cache.
    GPUTexture *pTexture = gpRender->getTextureCache().createFromRGBA(
        width, height, pixels.data(),
        ( std::string( "tuner_finish_" ) + cacheKey ).c_str() );
    finishTextures[cacheKey] = pTexture;
    OTDU_LOG_INFO( "Tuner: finish '%s' ('%s%s'%s) loaded\n", cacheKey.c_str(),
                   kPartsFolder, finish.Texture.c_str(),
                   finish.bBodyPaint ? ", body paint"
                                     : ( finish.Tint.x != 1.0f ||
                                                 finish.Tint.y != 1.0f ||
                                                 finish.Tint.z != 1.0f
                                             ? ", tinted"
                                             : "" ) );
    return pTexture;
}

std::vector<const TunerFinish *>
TunerParts::finishesForSlot( const char *pSlot ) const {
    const_cast<TunerParts *>( this )->load();
    std::vector<const TunerFinish *> out;
    for ( const TunerFinish &finish : finishes ) {
        if ( StringCmpI( finish.Slot.c_str(), pSlot ) == 0 ) {
            out.push_back( &finish );
        }
    }
    return out;
}

std::string TunerParts::finishKey( const char *pSlot ) {
    return std::string( pSlot ) + "_finish";
}

Vector3f TunerParts::finishSwatch( const TunerFinish &finish,
                                   uint32_t bodyColour ) const {
    std::vector<uint8_t> pixels;
    uint32_t width = 0, height = 0;
    if ( !finishPixels( finish, bodyColour, pixels, width, height ) ) {
        return Vector3f( 0.5f, 0.5f, 0.5f );
    }
    double r = 0.0, g = 0.0, b = 0.0;
    size_t count = 0;
    for ( size_t i = 0; i + 3 < pixels.size(); i += 4 ) {
        r += pixels[i + 0];
        g += pixels[i + 1];
        b += pixels[i + 2];
        count++;
    }
    if ( count == 0 ) {
        return Vector3f( 0.5f, 0.5f, 0.5f );
    }
    const double scale = 1.0 / ( 255.0 * static_cast<double>( count ) );
    return Vector3f( static_cast<float>( r * scale ),
                     static_cast<float>( g * scale ),
                     static_cast<float>( b * scale ) );
}

const TunerFlame *TunerParts::findFlame( const std::string &id ) const {
    const_cast<TunerParts *>( this )->load();
    for ( const TunerFlame &flame : flames ) {
        if ( StringCmpI( flame.Id.c_str(), id.c_str() ) == 0 ) {
            return &flame;
        }
    }
    return nullptr;
}

const TunerPart *TunerParts::exhaustFor(
    const std::map<std::string, std::string> &saved ) const {
    const auto exhaust = saved.find( "exhaust" );
    if ( exhaust == saved.end() || exhaust->second.empty() ) {
        return nullptr;
    }
    return findPart( exhaust->second );
}

const TunerFlame *TunerParts::flameFor(
    const std::map<std::string, std::string> &saved ) const {
    const TunerPart *pExhaust = exhaustFor( saved );
    if ( pExhaust == nullptr || pExhaust->Tips.empty() || flames.empty() ) {
        return nullptr;
    }
    const auto style = saved.find( "exhaust_flames" );
    if ( style == saved.end() || style->second.empty() ) {
        return &flames.front();
    }
    if ( StringCmpI( style->second.c_str(), "off" ) == 0 ) {
        return nullptr;
    }
    const TunerFlame *pFlame = findFlame( style->second );
    return pFlame != nullptr ? pFlame : &flames.front();
}

Vector3f TunerParts::offsetFor( int32_t carId, const TunerPart &part ) const {
    const TunerCar *pCar = findCar( carId );
    if ( pCar != nullptr ) {
        // The viewer's live nudge first (it is not in tuner.txt yet).
        const auto runtime = runtimeSlotOffsets.find(
            { ToLower( pCar->FileName ), ToLower( part.Slot ) } );
        if ( runtime != runtimeSlotOffsets.end() ) {
            return runtime->second;
        }
        const auto found = pCar->SlotOffsets.find( ToLower( part.Slot ) );
        if ( found != pCar->SlotOffsets.end() ) {
            return found->second;
        }
    }
    return part.Offset;
}

void TunerParts::setRuntimeSlotOffset( const std::string &carFileName,
                                       const char *pSlot,
                                       const Vector3f &offset ) {
    if ( pSlot == nullptr ) {
        return;
    }
    runtimeSlotOffsets[{ ToLower( carFileName ), ToLower( pSlot ) }] = offset;
}

void TunerParts::clearRuntimeSlotOffset( const std::string &carFileName,
                                         const char *pSlot ) {
    if ( carFileName.empty() ) {
        runtimeSlotOffsets.clear();
        return;
    }
    if ( pSlot == nullptr ) {
        for ( auto it = runtimeSlotOffsets.begin();
              it != runtimeSlotOffsets.end(); ) {
            if ( it->first.first == ToLower( carFileName ) ) {
                it = runtimeSlotOffsets.erase( it );
            } else {
                ++it;
            }
        }
        return;
    }
    runtimeSlotOffsets.erase( { ToLower( carFileName ), ToLower( pSlot ) } );
}

const TunerPart *TunerParts::partForMesh( const std::string &meshFile ) const {
    for ( const TunerPart &part : parts ) {
        if ( StringCmpI( part.Mesh.c_str(), meshFile.c_str() ) == 0 ) {
            return &part;
        }
    }
    return nullptr;
}

const std::vector<std::pair<Vector3f, Vector3f>> *
TunerParts::getHideBoxes( const std::string &carFileName,
                          const char *pSlot ) const {
    // The viewer's live edits first (they are not in tuner.txt yet).
    const auto runtime =
        runtimeHideBoxes.find( { ToLower( carFileName ), ToLower( pSlot ) } );
    if ( runtime != runtimeHideBoxes.end() ) {
        return &runtime->second;
    }
    for ( const TunerCar &car : cars ) {
        if ( StringCmpI( car.FileName.c_str(), carFileName.c_str() ) != 0 ) {
            continue;
        }
        const auto found = car.HideBoxes.find( ToLower( pSlot ) );
        return found != car.HideBoxes.end() ? &found->second : nullptr;
    }
    return nullptr;
}

const std::map<size_t, std::vector<uint32_t>> *
TunerParts::getHideTris( const std::string &carFileName,
                         const char *pSlot ) const {
    const_cast<TunerParts *>( this )->load();
    for ( const TunerCar &car : cars ) {
        if ( StringCmpI( car.FileName.c_str(), carFileName.c_str() ) != 0 ) {
            continue;
        }
        const auto found = car.HideTris.find( ToLower( pSlot ) );
        return found != car.HideTris.end() && !found->second.empty()
                   ? &found->second
                   : nullptr;
    }
    return nullptr;
}

void TunerParts::setRuntimeHideBoxes(
    const std::string &carFileName,
    const char *pSlot,
    const std::vector<std::pair<Vector3f, Vector3f>> &boxes ) {
    if ( pSlot == nullptr ) {
        return;
    }
    runtimeHideBoxes[{ ToLower( carFileName ), ToLower( pSlot ) }] = boxes;
}

void TunerParts::clearRuntimeHideBoxes( const std::string &carFileName,
                                        const char *pSlot ) {
    if ( carFileName.empty() ) {
        runtimeHideBoxes.clear();
        return;
    }
    if ( pSlot == nullptr ) {
        for ( auto it = runtimeHideBoxes.begin();
              it != runtimeHideBoxes.end(); ) {
            if ( it->first.first == ToLower( carFileName ) ) {
                it = runtimeHideBoxes.erase( it );
            } else {
                ++it;
            }
        }
        return;
    }
    runtimeHideBoxes.erase( { ToLower( carFileName ), ToLower( pSlot ) } );
}

float TunerParts::popStrength(
    int32_t carId, const std::map<std::string, std::string> &saved ) const {
    const TunerCar *pCar = findCar( carId );
    if ( pCar == nullptr ) {
        return 0.0f;
    }
    const auto exhaust = saved.find( "exhaust" );
    const bool bExhaust = exhaust != saved.end() && !exhaust->second.empty() &&
                          findPart( exhaust->second ) != nullptr;
    return pCar->Pops * ( bExhaust ? 1.0f : 0.6f );
}

const char *const *TunerParts::slotNames( size_t &count ) {
    static const char *const kSlots[] = { "spoiler", "exhaust" };
    count = sizeof( kSlots ) / sizeof( kSlots[0] );
    return kSlots;
}

const char *TunerParts::slotLabel( const char *pSlot ) {
    if ( pSlot != nullptr && StringCmpI( pSlot, "exhaust" ) == 0 ) {
        return "Exhaust tips";
    }
    return "Spoiler";
}

void TunerParts::applySaved( CinematicCar &car,
                             int32_t carId,
                             const std::map<std::string, std::string> &saved ) {
    car.clearTunerAttachment();
    size_t slotCount = 0;
    const char *const *pSlots = slotNames( slotCount );
    const TunerCar *pCar = findCar( carId );
    for ( size_t i = 0; i < slotCount && pCar != nullptr; i++ ) {
        const auto found = saved.find( pSlots[i] );
        if ( found == saved.end() || found->second.empty() ) {
            continue;
        }
        if ( const TunerPart *pPart = findPart( found->second ) ) {
            // The slot's finish rides in the same map (<slot>_finish = <finish
            // id>); a finish the catalog lost is skipped like an unknown part.
            const auto finishKeyFound = saved.find( finishKey( pSlots[i] ) );
            applyPart( car, carId, pSlots[i], pPart,
                       finishKeyFound != saved.end() &&
                               !finishKeyFound->second.empty()
                           ? finishKeyFound->second.c_str()
                           : nullptr );
        } else {
            OTDU_LOG_INFO( "Tuner: part '%s' (%s) is not in the catalog (kept "
                           "as data, not drawn)\n",
                           found->second.c_str(), pSlots[i] );
        }
    }
}

void TunerParts::applyFinish( CinematicCar &car,
                              const char *pSlot,
                              const char *pFinishId ) {
    if ( pSlot == nullptr ) {
        return;
    }
    GPUTexture *pTexture = nullptr;
    if ( pFinishId != nullptr && pFinishId[0] != '\0' ) {
        const TunerFinish *pFinish = findFinish( pFinishId );
        if ( pFinish == nullptr ) {
            OTDU_LOG_INFO( "Tuner: finish '%s' is not in the catalog (kept as "
                           "data, not drawn)\n", pFinishId );
            return;
        }
        pTexture = finishTexture( *pFinish, car.getBodyPaintColour() );
        if ( pTexture == nullptr ) {
            return;
        }
    }
    car.setTunerAttachmentTexture( pSlot, pTexture );
}

void TunerParts::applyPart( CinematicCar &car,
                            int32_t carId,
                            const char *pSlot,
                            const TunerPart *pPart,
                            const char *pFinishId ) {
    car.clearTunerAttachment( pSlot );
    if ( pPart == nullptr || pPart->Mesh.empty() ) {
        return;
    }
    // The catalog names the OBJ file ("wing_gt.obj"); the loader takes the
    // stem (it appends ".obj" itself, like the props loader).
    std::string meshName = pPart->Mesh;
    if ( meshName.size() > 4 &&
         StringCmpI( meshName.c_str() + meshName.size() - 4, ".obj" ) == 0 ) {
        meshName.resize( meshName.size() - 4 );
    }
    EdenRenderer *pRenderer =
        gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
    if ( pRenderer == nullptr ) {
        return; // no renderer yet: the caller retries (GMFreeRide's
                // drawOffscreen)
    }
    auto found = meshes.find( meshName );
    if ( found == meshes.end() ) {
        std::vector<ObjMeshDraw> draws;
        if ( !LoadObjDraws( *pRenderer, kPartsFolder, meshName.c_str(),
                            "TD_Gouraud", draws, nullptr, nullptr, nullptr,
                            "TunerParts" ) ) {
            OTDU_LOG_WARN(
                "Tuner: part '%s' has no '%s%s.obj' in the overlay\n",
                pPart->Id.c_str(), kPartsFolder, meshName.c_str() );
            return;
        }
        if ( draws.size() > 1 ) {
            OTDU_LOG_WARN(
                "Tuner: part '%s' has %zu materials, only the first draws\n",
                pPart->Id.c_str(), draws.size() );
        }
        found = meshes.emplace( meshName, std::move( draws ) ).first;
        OTDU_LOG_INFO( "Tuner: part '%s' mesh '%s' loaded (%u triangles)\n",
                       pPart->Id.c_str(), pPart->Mesh.c_str(),
                       found->second.empty()
                           ? 0u
                           : found->second.front().Mesh.IndexCount / 3 );
    }
    if ( found->second.empty() ) {
        return;
    }
    Vector3f offset = offsetFor( carId, *pPart );
    if ( pPart->Align == "rear" ) {
        // Body-relative (exhaust tips): the rear face and the body bottom of
        // THIS car, so a catalog offset of (x, 0.2, -0.03) puts the opening
        // 3 cm past the rear bumper on any tuner car.
        Vector3f bMin, bMax;
        if ( car.getRearBounds( bMin, bMax ) ) {
            offset = Vector3f( offset.x, bMin.y + offset.y, bMin.z + offset.z );
        }
    }
    car.setTunerAttachment( pSlot, found->second.front().Mesh,
                            found->second.front().Material, pPart->Node.c_str(),
                            Matrix4x4f::Translation( offset ) );
    car.setTunerAttachmentTips( pSlot, pPart->Tips );
    // The tip finish lands on the attachment's own material copy (per car),
    // so two cars can wear one mesh in different looks.
    applyFinish( car, pSlot, pFinishId );
}

void TunerParts::unloadMeshes() {
    for ( auto &entry : meshes ) {
        UnloadObjDraws( entry.second );
    }
    meshes.clear();
    // The finish textures: the cache owns them (the car materials borrow).
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( auto &entry : finishTextures ) {
            if ( entry.second != nullptr ) {
                device.destroyTexture( entry.second );
            }
        }
    }
    finishTextures.clear();
}

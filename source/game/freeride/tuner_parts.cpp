#include "shared.h"
#include "tuner_parts.h"

#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_car.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include <cstring>

namespace {

// The data file and the part folder, inside the mods overlay (the enabled
// tuner-garage package layer, or a loose <mods>/tuner/ - the overlay rules).
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

// The DBCarPhysicsData row of a car id (the database has no by-id lookup, so
// this walks the 279 file names once per call - nothing per frame).
bool FindCarById( int32_t carId, CarPhysicsData &outCar ) {
    if ( gpDatabase == nullptr || carId < 0 ) {
        return false;
    }
    for ( const std::string &fileName : gpDatabase->getCarFileNames() ) {
        if ( gpDatabase->findCarPhysics( fileName.c_str(), outCar ) && outCar.CarId == carId ) {
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

TunerParts &GetTunerParts() {
    return TunerParts::get();
}

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
    std::string block; // "part" / "car", and the id after the ':'
    TunerPart part;
    TunerCar car;
    bool bInPart = false, bInCar = false;
    auto flush = [&]() {
        if ( bInPart && !part.Id.empty() && !part.Mesh.empty() ) {
            parts.push_back( part );
        }
        if ( bInCar && !car.FileName.empty() && !car.Slots.empty() ) {
            cars.push_back( car );
        }
        part = TunerPart();
        car = TunerCar();
        bInPart = bInCar = false;
    };
    size_t pos = 0;
    while ( pos <= text.size() ) {
        size_t end = text.find( '\n', pos );
        if ( end == std::string::npos ) {
            end = text.size();
        }
        std::string line = Trim( text.substr( pos, end - pos ) );
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
            }
        } else if ( bInCar ) {
            if ( key == "slots" ) {
                // comma separated
                size_t cursor = 0;
                while ( cursor < value.size() ) {
                    const size_t comma = value.find( ',', cursor );
                    const std::string slot = Trim( value.substr( cursor, comma == std::string::npos
                                                                              ? std::string::npos
                                                                              : comma - cursor ) );
                    if ( !slot.empty() ) {
                        car.Slots.push_back( ToLower( slot ) );
                    }
                    if ( comma == std::string::npos ) {
                        break;
                    }
                    cursor = comma + 1;
                }
            } else if ( key == "pops" ) {
                car.Pops = static_cast<float>( atof( value.c_str() ) );
            } else if ( key.size() > 7 && key.compare( key.size() - 7, 7, "_offset" ) == 0 ) {
                Vector3f offset;
                if ( ParseVector3( value, offset ) ) {
                    car.SlotOffsets[key.substr( 0, key.size() - 7 )] = offset;
                }
            }
        }
    }
    flush();
    bLoaded = true;
    OTDU_LOG_INFO( "Tuner: %zu parts, %zu tuner cars ('%s'%s)\n", parts.size(), cars.size(), kTunerFile,
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

std::vector<const TunerPart *> TunerParts::partsForSlot( int32_t carId, const char *pSlot ) const {
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

Vector3f TunerParts::offsetFor( int32_t carId, const TunerPart &part ) const {
    const TunerCar *pCar = findCar( carId );
    if ( pCar != nullptr ) {
        const auto found = pCar->SlotOffsets.find( ToLower( part.Slot ) );
        if ( found != pCar->SlotOffsets.end() ) {
            return found->second;
        }
    }
    return part.Offset;
}

const TunerPart *TunerParts::partForMesh( const std::string &meshFile ) const {
    for ( const TunerPart &part : parts ) {
        if ( StringCmpI( part.Mesh.c_str(), meshFile.c_str() ) == 0 ) {
            return &part;
        }
    }
    return nullptr;
}

float TunerParts::popStrength( int32_t carId, const std::map<std::string, std::string> &saved ) const {
    const TunerCar *pCar = findCar( carId );
    if ( pCar == nullptr ) {
        return 0.0f;
    }
    const auto exhaust = saved.find( "exhaust" );
    const bool bExhaust = exhaust != saved.end() && !exhaust->second.empty() && findPart( exhaust->second ) != nullptr;
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

void TunerParts::applySaved( CinematicCar &car, int32_t carId, const std::map<std::string, std::string> &saved ) {
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
            applyPart( car, carId, pSlots[i], pPart );
        } else {
            OTDU_LOG_INFO( "Tuner: part '%s' (%s) is not in the catalog (kept as data, not drawn)\n",
                           found->second.c_str(), pSlots[i] );
        }
    }
}

void TunerParts::applyPart( CinematicCar &car, int32_t carId, const char *pSlot, const TunerPart *pPart ) {
    car.clearTunerAttachment( pSlot );
    if ( pPart == nullptr || pPart->Mesh.empty() ) {
        return;
    }
    // The catalog names the OBJ file ("wing_gt.obj"); the loader takes the
    // stem (it appends ".obj" itself, like the props loader).
    std::string meshName = pPart->Mesh;
    if ( meshName.size() > 4 && StringCmpI( meshName.c_str() + meshName.size() - 4, ".obj" ) == 0 ) {
        meshName.resize( meshName.size() - 4 );
    }
    EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
    if ( pRenderer == nullptr ) {
        return; // no renderer yet: the caller retries (GMFreeRide's drawOffscreen)
    }
    auto found = meshes.find( meshName );
    if ( found == meshes.end() ) {
        std::vector<ObjMeshDraw> draws;
        if ( !LoadObjDraws( *pRenderer, kPartsFolder, meshName.c_str(), "TD_Gouraud", draws, nullptr, nullptr,
                            nullptr, "TunerParts" ) ) {
            OTDU_LOG_WARN( "Tuner: part '%s' has no '%s%s.obj' in the overlay\n", pPart->Id.c_str(), kPartsFolder,
                           meshName.c_str() );
            return;
        }
        if ( draws.size() > 1 ) {
            OTDU_LOG_WARN( "Tuner: part '%s' has %zu materials, only the first draws\n", pPart->Id.c_str(),
                           draws.size() );
        }
        found = meshes.emplace( meshName, std::move( draws ) ).first;
        OTDU_LOG_INFO( "Tuner: part '%s' mesh '%s' loaded (%u triangles)\n", pPart->Id.c_str(), pPart->Mesh.c_str(),
                       found->second.empty() ? 0u : found->second.front().Mesh.IndexCount / 3 );
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
        if ( car.getBodyBounds( bMin, bMax ) ) {
            offset = Vector3f( offset.x, bMin.y + offset.y, bMin.z + offset.z );
        }
    }
    car.setTunerAttachment( pSlot, found->second.front().Mesh, found->second.front().Material, pPart->Node.c_str(),
                            Matrix4x4f::Translation( offset ) );
}

void TunerParts::unloadMeshes() {
    for ( auto &entry : meshes ) {
        UnloadObjDraws( entry.second );
    }
    meshes.clear();
}

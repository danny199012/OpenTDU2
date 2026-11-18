#include "shared.h"
#include "car_swap.h"

#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_car.h"
#include "render/gs_render.h"
#include "system/cmd_line.h"

#include <cstdio>
#include <cstring>
#include <sstream>

namespace {
    constexpr const char *kSwapFolder = "carswap/";
    constexpr const char *kSwapFile = "carswap/carswap.txt";

    std::string Trim( const std::string &s ) {
        const size_t a = s.find_first_not_of( " \t\r" );
        if ( a == std::string::npos ) {
            return std::string();
        }
        const size_t b = s.find_last_not_of( " \t\r" );
        return s.substr( a, b - a + 1 );
    }
    std::vector<std::string> Words( const std::string &s ) {
        std::vector<std::string> out;
        std::istringstream in( s );
        std::string w;
        while ( in >> w ) {
            if ( w.size() > 4 && StringCmpI( w.c_str() + w.size() - 4, ".obj" ) == 0 ) {
                w.resize( w.size() - 4 );
            }
            out.push_back( w );
        }
        return out;
    }
} // namespace

CarSwaps &GetCarSwaps() {
    return CarSwaps::get();
}

CarSwaps &CarSwaps::get() {
    static CarSwaps instance;
    return instance;
}

bool CarSwaps::load() {
    if ( bTried ) {
        return bLoaded;
    }
    bTried = true;
    if ( CmdLine::HasArg( "-nocarswap" ) || gpFile == nullptr ) {
        return false;
    }
    std::vector<uint8_t> data;
    if ( !gpFile->loadFile( kSwapFile, data ) ) {
        return false;
    }
    const std::string text( data.begin(), data.end() );
    std::istringstream in( text );
    std::string line;
    CarSwapDef def;
    bool bIn = false;
    auto flush = [&]() {
        if ( bIn && !def.Donor.empty() && !def.Body.empty() ) {
            defs.push_back( def );
        }
        def = CarSwapDef();
        bIn = false;
    };
    while ( std::getline( in, line ) ) {
        line = Trim( line );
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        if ( line[0] == '[' ) {
            flush();
            const size_t colon = line.find( ':' );
            const size_t close = line.find( ']' );
            if ( line.compare( 0, 6, "[swap:" ) == 0 && colon != std::string::npos && close != std::string::npos ) {
                def.Donor = Trim( line.substr( colon + 1, close - colon - 1 ) );
                bIn = true;
            }
            continue;
        }
        if ( !bIn ) {
            continue;
        }
        const size_t eq = line.find( '=' );
        if ( eq == std::string::npos ) {
            continue;
        }
        const std::string key = Trim( line.substr( 0, eq ) );
        const std::string value = Trim( line.substr( eq + 1 ) );
        if ( key == "body" ) {
            for ( const std::string &w : Words( value ) ) {
                def.Body.push_back( w );
            }
        } else if ( key == "wheel_front" ) {
            def.WheelFront = Words( value );
        } else if ( key == "wheel_rear" ) {
            def.WheelRear = Words( value );
        } else if ( key.compare( 0, 10, "wheel_pos_" ) == 0 && key.size() == 15 ) {
            // wheel_pos_f_neg / wheel_pos_r_pos
            const int32_t axle = key[10] == 'r' ? 1 : 0;
            const int32_t side = key.compare( 12, 3, "pos" ) == 0 ? 1 : 0;
            float x = 0, y = 0, z = 0;
            if ( sscanf( value.c_str(), "%f %f %f", &x, &y, &z ) == 3 ) {
                def.WheelPos[axle][side] = Vector3f( x, y, z );
                def.bWheelPos[axle][side] = true;
            }
        }
    }
    flush();
    bLoaded = !defs.empty();
    OTDU_LOG_INFO( "CarSwap: %zu swap(s) in '%s'\n", defs.size(), kSwapFile );
    return bLoaded;
}

bool CarSwaps::loadParts( const std::string &name, std::vector<ObjMeshDraw> *&out ) {
    auto found = meshes.find( name );
    if ( found == meshes.end() ) {
        EdenRenderer *pRenderer = gpRender != nullptr ? gpRender->getEdenRenderer() : nullptr;
        if ( pRenderer == nullptr ) {
            return false;
        }
        std::vector<ObjMeshDraw> draws;
        if ( !LoadObjDraws( *pRenderer, kSwapFolder, name.c_str(), "TD_Gouraud", draws, nullptr, nullptr, nullptr,
                            "CarSwap" ) ) {
            OTDU_LOG_WARN( "CarSwap: no '%s%s.obj' in the overlay\n", kSwapFolder, name.c_str() );
        }
        found = meshes.emplace( name, std::move( draws ) ).first;
    }
    out = &found->second;
    return !found->second.empty();
}

bool CarSwaps::apply( CinematicCar &car ) {
    if ( !load() ) {
        return false;
    }
    const CarSwapDef *pDef = nullptr;
    for ( const CarSwapDef &d : defs ) {
        if ( StringCmpI( d.Donor.c_str(), car.getFileName().c_str() ) == 0 ) {
            pDef = &d;
        }
    }
    if ( pDef == nullptr ) {
        return false;
    }
    CinematicCar::VisualSwap swap;
    auto addAll = [&]( const std::vector<std::string> &names, std::vector<CinematicCar::SwapMesh> &dst ) {
        for ( const std::string &name : names ) {
            std::vector<ObjMeshDraw> *pDraws = nullptr;
            if ( !loadParts( name, pDraws ) ) {
                continue;
            }
            for ( const ObjMeshDraw &draw : *pDraws ) {
                CinematicCar::SwapMesh mesh;
                mesh.Mesh = draw.Mesh;
                mesh.Material = draw.Material;
                dst.push_back( mesh );
            }
        }
    };
    addAll( pDef->Body, swap.Body );
    addAll( pDef->WheelFront, swap.WheelFront );
    addAll( pDef->WheelRear, swap.WheelRear );
    for ( int32_t a = 0; a < 2; a++ ) {
        for ( int32_t s = 0; s < 2; s++ ) {
            swap.WheelPos[a][s] = pDef->WheelPos[a][s];
            swap.bWheelPos[a][s] = pDef->bWheelPos[a][s];
        }
    }
    if ( swap.Body.empty() ) {
        return false;
    }
    car.setVisualSwap( swap );
    OTDU_LOG_INFO( "CarSwap: '%s' wears %zu body draw(s), %zu + %zu wheel draw(s)\n", car.getFileName().c_str(),
                   swap.Body.size(), swap.WheelFront.size(), swap.WheelRear.size() );
    return true;
}

void CarSwaps::unloadMeshes() {
    for ( auto &entry : meshes ) {
        UnloadObjDraws( entry.second );
    }
    meshes.clear();
    bTried = false;
    bLoaded = false;
    defs.clear();
}

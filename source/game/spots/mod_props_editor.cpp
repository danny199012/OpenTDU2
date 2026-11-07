#include "shared.h"
#include "mod_props.h"

#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

// The map editor's props.txt helpers (gm_world_viewer's placement panel).
// Kept out of mod_props.cpp: the editor is the only consumer.

std::vector<std::string> ModProps::listAvailableProps() {
    std::vector<std::string> names;
    if ( gpFile == nullptr || CmdLine::HasArg( "-nomodprops" ) ) {
        return names;
    }
    std::error_code error;
    const std::filesystem::path folder = gpFile->getModsRoot() / "props";
    for ( std::filesystem::directory_iterator it( folder, error ), end; !error && it != end; it.increment( error ) ) {
        if ( it->is_directory( error ) ) {
            continue;
        }
        std::string name = it->path().filename().string();
        if ( name.size() < 4 && name != ".obj" ) {
            continue;
        }
        if ( name.size() < 5 || StringCmpI( name.c_str() + name.size() - 4, ".obj" ) != 0 ) {
            continue;
        }
        names.push_back( name.substr( 0, name.size() - 4 ) );
    }
    std::sort( names.begin(), names.end() );
    return names;
}

static std::vector<std::string> ReadPropsLines() {
    std::vector<std::string> lines;
    if ( gpFile == nullptr ) {
        return lines;
    }
    const std::filesystem::path path = gpFile->getModsRoot() / "props.txt";
    std::ifstream stream( path, std::ios::binary );
    if ( !stream.is_open() ) {
        return lines;
    }
    std::string line;
    while ( std::getline( stream, line ) ) {
        if ( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        lines.push_back( line );
    }
    return lines;
}

std::vector<ModProps::PlacementLine> ModProps::listPlacements() {
    std::vector<PlacementLine> out;
    const std::vector<std::string> lines = ReadPropsLines();
    for ( size_t i = 0; i < lines.size(); i++ ) {
        const std::string &line = lines[ i ];
        if ( line.empty() || line[ 0 ] == '#' ) {
            continue;
        }
        char lineIsland[ 64 ] = {}, name[ 128 ] = {};
        float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
        if ( sscanf( line.c_str(), "%63s %127s %f %f %f %f", lineIsland, name, &x, &y, &z, &heading ) < 6 ) {
            continue;
        }
        PlacementLine entry;
        entry.FileLine = i;
        entry.Island = lineIsland;
        entry.Name = name;
        entry.Position = Vector3f( x, y, z );
        entry.Heading = heading;
        entry.Scale = 1.0f;
        entry.ShaderName = "TD_Gouraud";
        size_t cursor = 0;
        int column = 0;
        while ( cursor < line.size() ) {
            while ( cursor < line.size() && line[ cursor ] == ' ' ) {
                cursor++;
            }
            const size_t start = cursor;
            while ( cursor < line.size() && line[ cursor ] != ' ' ) {
                cursor++;
            }
            if ( cursor == start ) {
                break;
            }
            const std::string token = line.substr( start, cursor - start );
            if ( column >= 6 ) {
                char *pEnd = nullptr;
                const double number = strtod( token.c_str(), &pEnd );
                if ( pEnd != nullptr && *pEnd == '\0' && pEnd != token.c_str() ) {
                    entry.Scale = static_cast<float>( number );
                } else if ( StringCmpI( token.c_str(), "fuel" ) == 0 ) {
                    entry.bFuelStation = true;
                } else {
                    entry.ShaderName = token;
                }
            }
            column++;
        }
        out.push_back( std::move( entry ) );
    }
    return out;
}

static std::string FormatPlacementLine( const std::string &island, const std::string &name, const Vector3f &position,
                                        float heading, float scale, bool bFuelStation,
                                        const std::string &shaderName ) {
    char head[ 192 ];
    snprintf( head, sizeof( head ), "%s %s %.2f %.2f %.2f %.4f", island.c_str(), name.c_str(), position.x, position.y,
              position.z, heading );
    std::string line( head );
    if ( fabsf( scale - 1.0f ) > 1e-3f ) {
        char tail[ 32 ];
        snprintf( tail, sizeof( tail ), " %.3f", scale );
        line += tail;
    }
    if ( StringCmpI( shaderName.c_str(), "TD_Gouraud" ) != 0 ) {
        line += " " + shaderName;
    }
    if ( bFuelStation ) {
        line += " fuel";
    }
    return line;
}

bool ModProps::appendPlacement( const std::string &island, const std::string &name, const Vector3f &position,
                                float heading, float scale, bool bFuelStation, const std::string &shaderName ) {
    if ( gpFile == nullptr ) {
        return false;
    }
    const std::filesystem::path path = gpFile->getModsRoot() / "props.txt";
    std::error_code error;
    std::filesystem::create_directories( path.parent_path(), error );
    std::ofstream stream( path, std::ios::binary | std::ios::app );
    if ( !stream.is_open() ) {
        return false;
    }
    stream << FormatPlacementLine( island, name, position, heading, scale, bFuelStation, shaderName ) << "\n";
    return stream.good();
}

bool ModProps::replacePlacementLine( size_t fileLine, const std::string &line ) {
    if ( gpFile == nullptr ) {
        return false;
    }
    const std::filesystem::path path = gpFile->getModsRoot() / "props.txt";
    std::vector<std::string> lines = ReadPropsLines();
    if ( fileLine >= lines.size() ) {
        return false;
    }
    lines[ fileLine ] = line;
    std::ofstream stream( path, std::ios::binary | std::ios::trunc );
    if ( !stream.is_open() ) {
        return false;
    }
    for ( const std::string &l : lines ) {
        stream << l << "\n";
    }
    return stream.good();
}

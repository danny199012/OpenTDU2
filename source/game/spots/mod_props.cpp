#include "shared.h"
#include "mod_props.h"

#include "filesystem/gs_file.h"
#include "obj_mesh.h"
#include "render/gs_render.h"
#include "render/eden/eden_scene_view.h"
#include "render/image_file.h"
#include "render/texture_cache.h"

#include <cstring>
#include <unordered_map>

namespace {

// The prop folder inside the mods overlay.
constexpr const char *kPropsList = "props.txt";
constexpr const char *kPropsFolder = "props/";

} // namespace

bool ModProps::load( EdenRenderer &renderer, const std::string &island ) {
    unload();
    if ( CmdLine::HasArg( "-nomodprops" ) ) {
        return false;
    }
    std::vector<uint8_t> list;
    if ( !gpFile->loadFile( kPropsList, list ) ) {
        return false; // no props.txt in the overlay: the normal case
    }
    const std::string text( list.begin(), list.end() );
    size_t pos = 0;
    while ( pos < text.size() ) {
        size_t end = text.find( '\n', pos );
        if ( end == std::string::npos ) {
            end = text.size();
        }
        std::string line = text.substr( pos, end - pos );
        pos = end + 1;
        if ( !line.empty() && line.back() == '\r' ) {
            line.pop_back();
        }
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        char lineIsland[64] = {}, name[128] = {};
        float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
        const int fields = sscanf( line.c_str(), "%63s %127s %f %f %f %f", lineIsland, name, &x, &y, &z, &heading );
        if ( fields < 6 ) {
            OTDU_LOG_WARN( "ModProps: '%s' is not 'island name x y z heading [scale] [shader] [fuel]'\n",
                           line.c_str() );
            continue;
        }
        if ( StringCmpI( lineIsland, island.c_str() ) != 0 ) {
            continue;
        }
        // The optional columns, in any order after the heading: a number is the
        // scale, "fuel" makes the prop a gas station (the pump screen opens next
        // to it), anything else names the shader family (default TD_Gouraud).
        float scale = 1.0f;
        std::string shaderName = "TD_Gouraud";
        bool bFuelStation = false;
        {
            size_t cursor = 0;
            int column = 0;
            while ( cursor < line.size() ) {
                while ( cursor < line.size() && line[cursor] == ' ' ) {
                    cursor++;
                }
                const size_t start = cursor;
                while ( cursor < line.size() && line[cursor] != ' ' ) {
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
                        scale = static_cast<float>( number );
                    } else if ( StringCmpI( token.c_str(), "fuel" ) == 0 ) {
                        bFuelStation = true;
                    } else {
                        shaderName = token;
                    }
                }
                column++;
            }
        }
        const char *pShaderName = shaderName.c_str();

        const std::string objPath = std::string( kPropsFolder ) + name + ".obj";
        std::vector<ObjMeshDraw> draws;
        Vector3f boundsMin, boundsMax;
        uint32_t numTriangles = 0;
        if ( !LoadObjDraws( renderer, kPropsFolder, name, pShaderName, draws, &boundsMin, &boundsMax, &numTriangles,
                            "ModProps" ) ) {
            OTDU_LOG_WARN( "ModProps: '%s' did not load ('%s' missing or empty)\n", name, objPath.c_str() );
            continue;
        }

        RenderDevice &device = gpRender->getDevice();

        Prop prop;
        prop.Name = name;
        prop.Position = Vector3f( x, y, z );
        prop.Heading = heading;
        prop.Scale = scale;
        prop.bFuelStation = bFuelStation;
        prop.BoundsMin = boundsMin;
        prop.BoundsMax = boundsMax;

        for ( ObjMeshDraw &draw : draws ) {
            prop.VertexBuffers.push_back( draw.pVertexBuffer );
            prop.IndexBuffers.push_back( draw.pIndexBuffer );
            prop.Textures.push_back( draw.pTexture );
            prop.Meshes.push_back( draw.Mesh );
            prop.Materials.push_back( draw.Material );
        }
        OTDU_LOG_INFO( "ModProps: '%s' on %s at (%.1f, %.1f, %.1f) heading %.3f: %zu materials, %u triangles, "
                       "bounds (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f), shader %s%s\n",
                       name, island.c_str(), x, y, z, heading, prop.Meshes.size(), numTriangles, prop.BoundsMin.x,
                       prop.BoundsMin.y, prop.BoundsMin.z, prop.BoundsMax.x, prop.BoundsMax.y, prop.BoundsMax.z,
                       pShaderName,
                       bFuelStation ? ", fuel station" : "" );
        props.push_back( std::move( prop ) );
    }
    bLoaded = !props.empty();
    if ( bLoaded ) {
        OTDU_LOG_INFO( "ModProps: %zu props on %s ('%s' in the mods overlay)\n", props.size(), island.c_str(),
                       kPropsList );
    }
    return bLoaded;
}

void ModProps::unload() {
    if ( gpRender != nullptr ) {
        RenderDevice &device = gpRender->getDevice();
        for ( Prop &prop : props ) {
            for ( GPUBuffer *pBuffer : prop.VertexBuffers ) {
                device.destroyBuffer( pBuffer );
            }
            for ( GPUBuffer *pBuffer : prop.IndexBuffers ) {
                device.destroyBuffer( pBuffer );
            }
            for ( GPUTexture *pTexture : prop.Textures ) {
                if ( pTexture != nullptr ) {
                    device.destroyTexture( pTexture );
                }
            }
        }
    }
    props.clear();
    bLoaded = false;
}

const ModProps::Prop *ModProps::findFuelStation( const Vector3f &position, float maxDistance ) const {
    const Prop *pNearest = nullptr;
    float bestDistance = maxDistance;
    for ( const Prop &prop : props ) {
        if ( !prop.bFuelStation ) {
            continue;
        }
        const float dx = prop.Position.x - position.x, dz = prop.Position.z - position.z;
        const float distance = std::sqrt( dx * dx + dz * dz );
        if ( distance < bestDistance ) {
            bestDistance = distance;
            pNearest = &prop;
        }
    }
    return pNearest;
}

void ModProps::draw( EdenRenderer &renderer ) const {
    for ( const Prop &prop : props ) {
        const float half = prop.Heading * 0.5f;
        const Quaternionf rotation( 0.0f, std::sin( half ), 0.0f, std::cos( half ) );
        Matrix4x4f world = Matrix4x4f::Translation( prop.Position ) * EdenRotationFromQuaternion( rotation );
        if ( prop.Scale != 1.0f ) {
            world = world * Matrix4x4f::Scale( Vector3f( prop.Scale, prop.Scale, prop.Scale ) );
        }
        if ( !renderer.isBoxVisible( world, prop.BoundsMin, prop.BoundsMax ) ) {
            continue;
        }
        for ( size_t i = 0; i < prop.Meshes.size(); i++ ) {
            renderer.draw( prop.Meshes[i], prop.Materials[i], world );
        }
    }
}



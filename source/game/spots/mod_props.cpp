#include "shared.h"
#include "mod_props.h"

#include "filesystem/gs_file.h"
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

// Blender (right handed, Y up, -Z forward) -> the space the renderer draws the
// world in: mirror X and reverse the triangle winding, which is exactly what
// the 3DG loader does to the game's own left handed geometry (render/3dg.cpp
// parse(..., bMirrorX)). A prop authored in Blender therefore lands the same
// way round as a retail building at the same heading.
struct ObjMaterial {
    std::string Name;
    std::string Texture; // map_Kd, relative to the .obj
    Vector4f Diffuse = Vector4f( 1, 1, 1, 1 );
};

struct ObjVertexKey {
    int32_t Position = -1;
    int32_t UV = -1;
    int32_t Normal = -1;
    bool operator==( const ObjVertexKey &o ) const {
        return Position == o.Position && UV == o.UV && Normal == o.Normal;
    }
};

struct ObjVertexKeyHash {
    size_t operator()( const ObjVertexKey &k ) const {
        return ( static_cast<size_t>( k.Position + 1 ) * 73856093u ) ^
               ( static_cast<size_t>( k.UV + 1 ) * 19349663u ) ^
               ( static_cast<size_t>( k.Normal + 1 ) * 83492791u );
    }
};

// One interleaved GPU stream: position, normal, texcoord (32 bytes).
struct PropVertex {
    float Position[3];
    float Normal[3];
    float UV[2];
};

struct ObjGroup {
    std::string Material;
    std::vector<PropVertex> Vertices;
    std::vector<uint32_t> Indices;
    std::unordered_map<ObjVertexKey, uint32_t, ObjVertexKeyHash> Lookup;
};

void ParseMtl( const std::string &text, std::unordered_map<std::string, ObjMaterial> &out ) {
    ObjMaterial current;
    bool bHave = false;
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
        if ( line.compare( 0, 7, "newmtl " ) == 0 ) {
            if ( bHave ) {
                out[current.Name] = current;
            }
            current = ObjMaterial();
            current.Name = line.substr( 7 );
            bHave = true;
        } else if ( line.compare( 0, 7, "map_Kd " ) == 0 ) {
            current.Texture = line.substr( 7 );
        } else if ( line.compare( 0, 3, "Kd " ) == 0 ) {
            float r = 1.0f, g = 1.0f, b = 1.0f;
            if ( sscanf( line.c_str() + 3, "%f %f %f", &r, &g, &b ) == 3 ) {
                current.Diffuse = Vector4f( r, g, b, 1.0f );
            }
        }
    }
    if ( bHave ) {
        out[current.Name] = current;
    }
}

// Reads a face vertex "v/vt/vn" (any part may be missing), 1 based, negative =
// relative to the end (the OBJ rule).
ObjVertexKey ParseFaceVertex( const std::string &token, size_t numPositions, size_t numUVs, size_t numNormals ) {
    ObjVertexKey key;
    int32_t parts[3] = { 0, 0, 0 };
    size_t part = 0;
    int32_t value = 0;
    bool bDigits = false;
    for ( size_t i = 0; i <= token.size() && part < 3; i++ ) {
        const char c = i < token.size() ? token[i] : '/';
        if ( c >= '0' && c <= '9' ) {
            value = value * 10 + ( c - '0' );
            bDigits = true;
        } else if ( c == '-' ) {
            value = -1;
        } else {
            if ( bDigits || value < 0 ) {
                int32_t index = bDigits ? value - 1 : -1;
                if ( bDigits && value == 0 ) {
                    index = -1; // 0 is not a valid OBJ index
                }
                parts[part] = index;
            } else {
                parts[part] = -1;
            }
            value = 0;
            bDigits = false;
            part++;
        }
    }
    key.Position = parts[0];
    key.UV = parts[1];
    key.Normal = parts[2];
    // Negative indices are relative to the end of the list.
    if ( key.Position < 0 ) {
        key.Position += static_cast<int32_t>( numPositions );
    }
    if ( key.UV < 0 && parts[1] != -1 ) {
        key.UV += static_cast<int32_t>( numUVs );
    }
    if ( key.Normal < 0 && parts[2] != -1 ) {
        key.Normal += static_cast<int32_t>( numNormals );
    }
    return key;
}
// ---------------------------------------------------------------------------
// OBJ
// ---------------------------------------------------------------------------

// One group per `usemtl` (a group is one draw). Returns false when the file has
// no face at all.
bool ParseObj( const std::string &text, std::vector<ObjGroup> &outGroups, std::string &outMtlFile ) {
    std::vector<float> positions; // xyz
    std::vector<float> uvs;       // uv
    std::vector<float> normals;   // xyz
    std::vector<ObjGroup> groups;
    std::string currentMaterial = "default";

    auto group = [&]() -> ObjGroup & {
        if ( !groups.empty() && groups.back().Material == currentMaterial ) {
            return groups.back();
        }
        groups.push_back( ObjGroup() );
        groups.back().Material = currentMaterial;
        return groups.back();
    };

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
        if ( line.compare( 0, 2, "v " ) == 0 ) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if ( sscanf( line.c_str() + 2, "%f %f %f", &x, &y, &z ) == 3 ) {
                positions.push_back( -x ); // mirror X (see the note at the top)
                positions.push_back( y );
                positions.push_back( z );
            }
        } else if ( line.compare( 0, 3, "vt " ) == 0 ) {
            float u = 0.0f, v = 0.0f;
            if ( sscanf( line.c_str() + 3, "%f %f", &u, &v ) == 2 ) {
                uvs.push_back( u );
                uvs.push_back( v );
            }
        } else if ( line.compare( 0, 3, "vn " ) == 0 ) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if ( sscanf( line.c_str() + 3, "%f %f %f", &x, &y, &z ) == 3 ) {
                normals.push_back( -x );
                normals.push_back( y );
                normals.push_back( z );
            }
        } else if ( line.compare( 0, 7, "usemtl " ) == 0 ) {
            currentMaterial = line.substr( 7 );
        } else if ( line.compare( 0, 7, "mtllib " ) == 0 ) {
            outMtlFile = line.substr( 7 );
        } else if ( line.compare( 0, 2, "f " ) == 0 ) {
            std::vector<ObjVertexKey> face;
            size_t tokenStart = 2;
            while ( tokenStart < line.size() ) {
                size_t tokenEnd = line.find( ' ', tokenStart );
                if ( tokenEnd == std::string::npos ) {
                    tokenEnd = line.size();
                }
                if ( tokenEnd > tokenStart ) {
                    face.push_back( ParseFaceVertex( line.substr( tokenStart, tokenEnd - tokenStart ),
                                                     positions.size() / 3, uvs.size() / 2, normals.size() / 3 ) );
                }
                tokenStart = tokenEnd + 1;
            }
            if ( face.size() < 3 ) {
                continue;
            }
            ObjGroup &g = group();
            auto addVertex = [&]( const ObjVertexKey &key ) -> uint32_t {
                auto it = g.Lookup.find( key );
                if ( it != g.Lookup.end() ) {
                    return it->second;
                }
                PropVertex vertex = {};
                if ( key.Position >= 0 && static_cast<size_t>( key.Position ) * 3 + 2 < positions.size() ) {
                    vertex.Position[0] = positions[key.Position * 3 + 0];
                    vertex.Position[1] = positions[key.Position * 3 + 1];
                    vertex.Position[2] = positions[key.Position * 3 + 2];
                }
                if ( key.UV >= 0 && static_cast<size_t>( key.UV ) * 2 + 1 < uvs.size() ) {
                    vertex.UV[0] = uvs[key.UV * 2 + 0];
                    vertex.UV[1] = uvs[key.UV * 2 + 1];
                }
                if ( key.Normal >= 0 && static_cast<size_t>( key.Normal ) * 3 + 2 < normals.size() ) {
                    vertex.Normal[0] = normals[key.Normal * 3 + 0];
                    vertex.Normal[1] = normals[key.Normal * 3 + 1];
                    vertex.Normal[2] = normals[key.Normal * 3 + 2];
                } else {
                    vertex.Normal[1] = 1.0f;
                }
                const uint32_t index = static_cast<uint32_t>( g.Vertices.size() );
                g.Vertices.push_back( vertex );
                g.Lookup.emplace( key, index );
                return index;
            };
            for ( size_t i = 1; i + 1 < face.size(); i++ ) {
                // Reversed winding: the X mirror flips the handedness.
                const uint32_t a = addVertex( face[0] );
                const uint32_t b = addVertex( face[i + 1] );
                const uint32_t c = addVertex( face[i] );
                g.Indices.push_back( a );
                g.Indices.push_back( b );
                g.Indices.push_back( c );
            }
        }
    }
    outGroups = std::move( groups );
    for ( const ObjGroup &g : outGroups ) {
        if ( !g.Indices.empty() ) {
            return true;
        }
    }
    return false;
}
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
        std::vector<uint8_t> obj;
        if ( !gpFile->loadFile( objPath.c_str(), obj ) ) {
            OTDU_LOG_WARN( "ModProps: '%s' has no '%s'\n", name, objPath.c_str() );
            continue;
        }
        std::vector<ObjGroup> groups;
        std::string mtlFile;
        if ( !ParseObj( std::string( obj.begin(), obj.end() ), groups, mtlFile ) ) {
            OTDU_LOG_WARN( "ModProps: '%s' has no triangle\n", objPath.c_str() );
            continue;
        }
        std::unordered_map<std::string, ObjMaterial> mtl;
        if ( !mtlFile.empty() ) {
            std::vector<uint8_t> mtlData;
            const std::string mtlPath = std::string( kPropsFolder ) + mtlFile;
            if ( gpFile->loadFile( mtlPath.c_str(), mtlData ) ) {
                ParseMtl( std::string( mtlData.begin(), mtlData.end() ), mtl );
            } else {
                OTDU_LOG_WARN( "ModProps: '%s' is missing ('%s')\n", mtlFile.c_str(), mtlPath.c_str() );
            }
        }

        ShaderLibrary *pLibrary = renderer.getShaderSystem().findLibrary( pShaderName );
        if ( pLibrary == nullptr ) {
            OTDU_LOG_WARN( "ModProps: '%s': unknown shader family '%s'\n", name, pShaderName );
        }
        RenderDevice &device = gpRender->getDevice();

        Prop prop;
        prop.Name = name;
        prop.Position = Vector3f( x, y, z );
        prop.Heading = heading;
        prop.Scale = scale;
        prop.bFuelStation = bFuelStation;
        prop.BoundsMin = Vector3f( 0.0f, 0.0f, 0.0f );
        prop.BoundsMax = Vector3f( 0.0f, 0.0f, 0.0f );
        bool bHasBounds = false;
        uint32_t numTriangles = 0;

        for ( ObjGroup &g : groups ) {
            if ( g.Indices.empty() ) {
                continue;
            }
            for ( const PropVertex &vertex : g.Vertices ) {
                const Vector3f p( vertex.Position[0], vertex.Position[1], vertex.Position[2] );
                if ( !bHasBounds ) {
                    prop.BoundsMin = prop.BoundsMax = p;
                    bHasBounds = true;
                } else {
                    prop.BoundsMin = Vector3f( Min( prop.BoundsMin.x, p.x ), Min( prop.BoundsMin.y, p.y ),
                                               Min( prop.BoundsMin.z, p.z ) );
                    prop.BoundsMax = Vector3f( Max( prop.BoundsMax.x, p.x ), Max( prop.BoundsMax.y, p.y ),
                                               Max( prop.BoundsMax.z, p.z ) );
                }
            }

            GPUBuffer *pVertexBuffer = device.createBuffer( g.Vertices.size() * sizeof( PropVertex ),
                                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, g.Vertices.data() );
            GPUBuffer *pIndexBuffer = device.createBuffer( g.Indices.size() * sizeof( uint32_t ),
                                                           VK_BUFFER_USAGE_INDEX_BUFFER_BIT, g.Indices.data() );
            EdenMesh mesh;
            mesh.NumStreams = 1;
            mesh.pStreamBuffers[0] = pVertexBuffer;
            mesh.Strides[0] = sizeof( PropVertex );
            RawVertexAttribute position;
            position.Usage = 0; // D3DDECLUSAGE_POSITION
            position.Stream = 0;
            position.Format = VAF_POSITION_R32G32B32_SFLOAT;
            position.Offset = offsetof( PropVertex, Position );
            RawVertexAttribute normal;
            normal.Usage = 3; // D3DDECLUSAGE_NORMAL
            normal.Stream = 0;
            normal.Format = VAF_NORMAL_R32G32B32_SFLOAT;
            normal.Offset = offsetof( PropVertex, Normal );
            RawVertexAttribute texcoord;
            texcoord.Usage = 5; // D3DDECLUSAGE_TEXCOORD
            texcoord.Stream = 0;
            texcoord.Format = VAF_TEXCOORD_R32G32_SFLOAT;
            texcoord.Offset = offsetof( PropVertex, UV );
            mesh.Attributes = { position, normal, texcoord };
            mesh.pIndexBuffer = pIndexBuffer;
            mesh.IndexCount = static_cast<uint32_t>( g.Indices.size() );
            mesh.Topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            mesh.computeLayoutKey();

            const auto itMaterial = mtl.find( g.Material );
            const ObjMaterial &source = itMaterial != mtl.end() ? itMaterial->second : ObjMaterial();
            MaterialDesc desc;
            desc.Name = std::string( name ) + "_" + g.Material;
            desc.ShaderSetGUID = pLibrary != nullptr ? pLibrary->getGUID() : 0;
            // Opaque, depth tested and written, back faces culled: the .2DM
            // render state bytes the material handlers expect.
            desc.States[0] = 1;   // depth test
            desc.States[1] = 1;   // depth write
            desc.States[3] = 3;   // cull
            desc.States[5] = 1;   // no alpha test
            desc.States[0xa] = 6; // no blending
            desc.DepthTest = 1;
            desc.DepthWrite = 1;
            desc.CullMode = 3;
            desc.SrcBlend = 2;
            desc.DstBlend = 1;
            desc.DiffuseColor = source.Diffuse;
            desc.AmbientColor = source.Diffuse;
            MaterialTextureSlot slot;
            slot.SlotType = GetIdentifier64bit( "COLOR" );
            slot.TextureHashcode = !source.Texture.empty() ? GetIdentifier64bit( source.Texture.c_str() ) : 0;
            desc.Textures.push_back( slot );
            desc.AllTextures.push_back( slot );

            EdenMaterial material;
            renderer.setupMaterial( desc, material, "modprops" );

            // The texture is ours (a PNG in the overlay, not a .2db in a bank):
            // create it and hand it to the material's colour slot.
            GPUTexture *pTexture = nullptr;
            if ( !source.Texture.empty() ) {
                const std::string texturePath = std::string( kPropsFolder ) + source.Texture;
                auto resolved = gpFile->resolvePath( texturePath.c_str() );
                std::vector<uint8_t> pixels;
                uint32_t width = 0, height = 0;
                if ( resolved && LoadImageFileRGBA( *resolved, pixels, width, height ) ) {
                    pTexture = gpRender->getTextureCache().createFromRGBA( width, height, pixels.data(),
                                                                           texturePath.c_str() );
                } else {
                    OTDU_LOG_WARN( "ModProps: '%s': texture '%s' is not readable\n", name, texturePath.c_str() );
                }
                if ( pTexture != nullptr ) {
                    renderer.assignTexture( material, "COLOR", pTexture );
                    renderer.updateProgram( material );
                }
            }
            if ( material.pProgram == nullptr ) {
                OTDU_LOG_WARN( "ModProps: '%s': material '%s' has no shader permutation (family '%s')\n", name,
                               desc.Name.c_str(), pShaderName );
            }
            numTriangles += mesh.IndexCount / 3;
            prop.VertexBuffers.push_back( pVertexBuffer );
            prop.IndexBuffers.push_back( pIndexBuffer );
            prop.Textures.push_back( pTexture );
            prop.Meshes.push_back( mesh );
            prop.Materials.push_back( material );
        }
        if ( prop.Meshes.empty() ) {
            continue;
        }
        OTDU_LOG_INFO( "ModProps: '%s' on %s at (%.1f, %.1f, %.1f) heading %.3f: %zu materials, %u triangles, "
                       "bounds (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f), shader %s%s\n",
                       name, island.c_str(), x, y, z, heading, prop.Meshes.size(), numTriangles, prop.BoundsMin.x,
                       prop.BoundsMin.y, prop.BoundsMin.z, prop.BoundsMax.x, prop.BoundsMax.y, prop.BoundsMax.z,
                       pLibrary != nullptr ? pLibrary->getName().c_str() : "-",
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



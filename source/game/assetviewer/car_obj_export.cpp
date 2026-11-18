#include "shared.h"
#include "car_obj_export.h"

#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_car.h"
#include "game/freeride/tuner.h"
#include "game/freeride/tuner_parts.h"
#include "render/eden/eden_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <set>

namespace {

    // OBJ names: Blender and the part loader are happiest with one token.
    std::string Sanitize( const std::string &text ) {
        std::string out = text;
        for ( char &c : out ) {
            const unsigned char u = static_cast<unsigned char>( c );
            if ( !std::isalnum( u ) && c != '_' && c != '-' ) {
                c = '_';
            }
        }
        if ( out.empty() ) {
            out = "unnamed";
        }
        return out;
    }

    struct ObjFile {
        std::ofstream stream;
        std::set<std::string> materials; // sanitized names written to the .mtl
        std::map<std::string, std::string>
            materialTokens; // engine name -> sanitized (dedup)
        uint32_t triangles = 0;
        uint32_t vertices = 0;

        bool open( const std::filesystem::path &folder,
                   const std::string &baseName ) {
            stream.open( folder / ( baseName + ".obj" ), std::ios::binary );
            if ( !stream.is_open() ) {
                return false;
            }
            stream << "# OpenTDU2 car export - metres, Y up, -Z forward\n";
            stream << "# x is negated and every face reversed versus the "
                      "engine's data:\n";
            stream << "# re-importing through the OBJ part loader "
                      "(obj_mesh.cpp) lands the\n";
            stream
                << "# same triangles back (see docs/re/car_viewer_spec.md).\n";
            stream << "mtllib " << baseName << ".mtl\n";
            return true;
        }
    };

    // The one EdenMaterial -> MTL mapping: a name-sanitized `newmtl` (the
    // engine's texture slots ride in a comment - the art stays in the game
    // banks).
    std::string WriteMaterial( ObjFile &file, const EdenMaterial &material ) {
        std::string name = Sanitize( material.Name );
        auto it = file.materialTokens.find( material.Name );
        if ( it != file.materialTokens.end() ) {
            name = it->second;
        } else {
            // Two materials can sanitize to one token; the second keeps its
            // index.
            if ( file.materials.count( name ) != 0 ) {
                char suffix[16];
                snprintf( suffix, sizeof( suffix ), "_%zu",
                          file.materials.size() );
                name += suffix;
            }
            file.materials.insert( name );
            file.materialTokens.emplace( material.Name, name );
        }
        file.stream << "o mat_" << name << "\n";
        file.stream << "usemtl " << name << "\n";
        return name;
    }

    // The second texture-coordinate set (D3DDECLUSAGE_TEXCOORD = 5, usage
    // index 1) straight from the primitive's raw vertex stream; the first set
    // is MeshVertex::Texcoord. Returns false when the primitive has none.
    bool ReadTexcoordSet( const GeometryPrimitive &primitive,
                          uint32_t usageIndex,
                          uint32_t vertex,
                          float &outU,
                          float &outV ) {
        for ( const RawVertexAttribute &a : primitive.RawAttributes ) {
            if ( a.Usage != 5 || a.UsageIndex != usageIndex || a.Stream >= 2 ||
                 primitive.RawStrides[a.Stream] == 0 ) {
                continue;
            }
            const uint32_t stride = primitive.RawStrides[a.Stream];
            const std::vector<uint8_t> &stream = primitive.RawStreams[a.Stream];
            const size_t at = static_cast<size_t>( vertex ) * stride + a.Offset;
            if ( at + 4 > stream.size() ) {
                return false;
            }
            const uint8_t *q = stream.data() + at;
            if ( a.Format == VAF_TEXCOORD_R32G32_SFLOAT ) {
                if ( at + 8 > stream.size() ) {
                    return false;
                }
                std::memcpy( &outU, q, 4 );
                std::memcpy( &outV, q + 4, 4 );
                return true;
            }
            if ( a.Format == VAF_TEXCOORD_R16G16_SNORM ) {
                int16_t h[2];
                std::memcpy( h, q, 4 );
                outU = std::max( -1.0f, h[0] / 32767.0f );
                outV = std::max( -1.0f, h[1] / 32767.0f );
                return true;
            }
            if ( a.Format == VAF_TEXCOORD_R16G16_SFLOAT ) {
                uint16_t h[2];
                std::memcpy( h, q, 4 );
                float out[2];
                for ( int i = 0; i < 2; i++ ) {
                    const uint32_t sign = ( h[i] >> 15 ) & 1, exp = ( h[i] >> 10 ) & 31, man = h[i] & 1023;
                    const float f = exp == 0 ? std::ldexp( static_cast<float>( man ), -24 )
                                             : std::ldexp( static_cast<float>( man | 1024 ), static_cast<int>( exp ) - 25 );
                    out[i] = sign ? -f : f;
                }
                outU = out[0];
                outV = out[1];
                return true;
            }
            return false;
        }
        return false;
    }

    // One item: its kept primitive's vertices (car space, x pre-negated) +
    // faces (reversed). Vertices are written per primitive - no dedup, the
    // shapes are exact.
    void WriteItem( ObjFile &file,
                    EdenModel &model,
                    const EdenModel::Item &item,
                    const Render3DG &geometry,
                    const Matrix4x4f &nodeWorld ) {
        const Geometry *pGeometry = geometry.find( item.SourceGeometryHash );
        if ( pGeometry == nullptr ||
             item.SourcePrimitive >= pGeometry->Primitives.size() ) {
            return;
        }
        const GeometryPrimitive &primitive =
            pGeometry->Primitives[item.SourcePrimitive];
        if ( primitive.Vertices.empty() || primitive.Indices.size() < 3 ) {
            return;
        }
        const EdenMaterial &material = model.getMaterials()[item.MaterialIndex];
        WriteMaterial( file, material );

        const uint32_t base = file.vertices;
        file.stream.setf( std::ios::fixed, std::ios::floatfield );
        for ( const MeshVertex &vertex : primitive.Vertices ) {
            const Vector3f p = nodeWorld.transformPoint( Vector3f(
                vertex.Position[0], vertex.Position[1], vertex.Position[2] ) );
            file.stream.precision( 4 );
            file.stream << "v " << -p.x << " " << p.y << " " << p.z << "\n";
            file.vertices++;
        }
        for ( const MeshVertex &vertex : primitive.Vertices ) {
            Vector3f n( vertex.Normal[0], vertex.Normal[1], vertex.Normal[2] );
            n = nodeWorld.transformVector( n );
            const float length = n.length();
            if ( length > 1e-8f ) {
                n = n * ( 1.0f / length );
            }
            file.stream.precision( 3 );
            file.stream << "vn " << -n.x << " " << n.y << " " << n.z << "\n";
        }
        for ( const MeshVertex &vertex : primitive.Vertices ) {
            file.stream.precision( 5 );
            file.stream << "vt " << vertex.Texcoord[0] << " "
                        << vertex.Texcoord[1] << "\n";
        }
        // Comment lines (any OBJ reader skips them): the second UV set, one per
        // vertex in the same order as the vt lines (the sticker canvas may be
        // sampled with it - docs/re/sticker_mod_packs.md).
        for ( uint32_t v = 0; v < primitive.Vertices.size(); v++ ) {
            float u1 = 0.0f, v1 = 0.0f;
            if ( !ReadTexcoordSet( primitive, 1, v, u1, v1 ) ) {
                break;
            }
            file.stream.precision( 5 );
            file.stream << "#vt1 " << u1 << " " << v1 << "\n";
        }
        // The engine keeps the authored winding; the OBJ loader reverses tri
        // faces on import, so write (a, c, b) to land (a, b, c) back.
        for ( size_t t = 0; t + 2 < primitive.Indices.size(); t += 3 ) {
            const uint32_t a =
                base + primitive.Indices[t] + 1; // OBJ indices are 1 based
            const uint32_t b = base + primitive.Indices[t + 2] + 1;
            const uint32_t c = base + primitive.Indices[t + 1] + 1;
            file.stream << "f " << a << "/" << a << "/" << a << " " << b << "/"
                        << b << "/" << b << " " << c << "/" << c << "/" << c
                        << "\n";
            file.triangles++;
        }
    }

    bool WriteModel( ObjFile &file,
                     EdenModel &model,
                     const std::vector<SceneNode> &nodes,
                     const char *pLogLabel ) {
        if ( !model.isLoaded() ) {
            return false;
        }
        const Render3DG *pGeometry = model.getKeptGeometry();
        if ( pGeometry == nullptr ) {
            OTDU_LOG_WARN( "CarExport: %s has no kept geometry "
                           "(setKeepParsedGeometry was not set)\n",
                           pLogLabel );
            return false;
        }
        uint32_t items = 0;
        const std::vector<EdenModel::Item> &modelItems = model.getItems();
        for ( size_t i = 0; i < modelItems.size(); i++ ) {
            const EdenModel::Item &item = modelItems[i];
            if ( item.Lod != 0 || item.SourceGeometryHash == 0 ||
                 item.SourcePrimitive == ~0u || item.NodeIndex < 0 ||
                 item.NodeIndex >= static_cast<int32_t>( nodes.size() ) ) {
                continue;
            }
            WriteItem( file, model, item, *pGeometry,
                       model.getNodeTransform( item.NodeIndex ) );
            items++;
        }
        OTDU_LOG_INFO( "CarExport: %s: %u items, %u triangles\n", pLogLabel,
                       items, file.triangles );
        return items > 0;
    }

    void WriteMtl( const std::filesystem::path &folder,
                   const std::string &baseName,
                   const ObjFile &file ) {
        std::ofstream mtl( folder / ( baseName + ".mtl" ), std::ios::binary );
        if ( !mtl.is_open() ) {
            return;
        }
        mtl << "# OpenTDU2 car export - the textures stay in the game banks; "
               "the\n";
        mtl << "# engine material each group came from is its object name.\n";
        for ( const std::string &name : file.materials ) {
            mtl << "newmtl " << name << "\nKd 0.8 0.8 0.8\n\n";
        }
    }

} // namespace

bool ExportCarObj( CinematicCar &car,
                   std::string &outFolder,
                   std::string &outError ) {
    if ( !car.isLoaded() ) {
        outError = "car is not loaded";
        return false;
    }
    if ( gpFile == nullptr ) {
        outError = "no file system";
        return false;
    }
    const std::string carName = Sanitize( car.getFileName() );
    const std::filesystem::path folder =
        gpFile->getUserDataFolder() / "export" / carName;
    std::error_code ec;
    std::filesystem::create_directories( folder, ec );
    if ( ec ) {
        outError = "cannot create " + folder.string();
        return false;
    }

    struct ModelSource {
        const char *pLabel;
        EdenModel *pModel;
        std::string pBase;
    };
    ModelSource sources[] = {
        {"ext",       &car.getModel(),         carName + "_ext"      },
        {"interior",  &car.getInteriorModel(), carName + "_interior" },
        {"rim front", &car.getRimModel(),      carName + "_rim_front"},
        {"rim rear",  &car.getRimRearModel(),  carName + "_rim_rear" },
        {"tire",      &car.getTireModel(),     carName + "_tire"     },
    };

    std::string written;
    for ( const ModelSource &source : sources ) {
        if ( !source.pModel->isLoaded() ) {
            continue;
        }
        ObjFile file;
        if ( !file.open( folder, source.pBase ) ) {
            outError =
                "cannot write " +
                ( folder / ( std::string( source.pBase ) + ".obj" ) ).string();
            return false;
        }
        const std::vector<SceneNode> &nodes =
            source.pModel->getScene().getNodes();
        WriteModel( file, *source.pModel, nodes, source.pLabel );
        file.stream.close();
        WriteMtl( folder, source.pBase, file );
        written += " " + std::string( source.pBase ) + ".obj (" +
                   std::to_string( file.triangles ) + " tris)";
    }

    // anchors.txt: the numbers a part author needs, in engine car space (the
    // OBJ files' x is the negation of these). The tuner boxes stay in item
    // (primitive) space - the space collectTunerMasks tests them in.
    std::ofstream anchors( folder / "anchors.txt", std::ios::binary );
    if ( anchors.is_open() ) {
        anchors << "# " << car.getFileName() << " (id " << car.getIdCar()
                << ", LOD '" << car.getLod() << "')\n";
        anchors << "# Space law: the OBJ files' x = -(engine x). Engine "
                   "numbers here are CAR space\n";
        anchors << "# (the raycast / collision space), the tuner hide boxes "
                   "are ITEM (primitive) space\n";
        anchors << "# - convert a Blender-measured box with x_engine = -x_obj "
                   "before writing tuner.txt.\n";
        anchors.setf( std::ios::fixed, std::ios::floatfield );
        anchors.precision( 4 );
        anchors << "tire_radius " << car.getTireRadius() << "\n";
        anchors << "body_lift " << car.getBodyLift() << "\n";
        Vector3f bMin, bMax;
        if ( car.getBodyBounds( bMin, bMax ) ) {
            anchors << "bounds body (" << bMin.x << " " << bMin.y << " "
                    << bMin.z << ")-(" << bMax.x << " " << bMax.y << " "
                    << bMax.z << ")\n";
        }
        if ( car.getRearBounds( bMin, bMax ) ) {
            anchors << "bounds rear (" << bMin.x << " " << bMin.y << " "
                    << bMin.z << ")-(" << bMax.x << " " << bMax.y << " "
                    << bMax.z << ")\n";
        }
        for ( const char *const pNode :
              { "BODY_HR", "BODY_MR", "BUMP_R_H", "EXHAUST", "SPOILER" } ) {
            Matrix4x4f transform;
            if ( car.getNodeModelTransform( pNode, transform ) ) {
                const Vector3f t = transform.getTranslation();
                anchors << "node " << pNode << " t (" << t.x << " " << t.y
                        << " " << t.z << ")\n";
            }
        }
        GetTunerParts().load();
        const TunerCar *pTuner = GetTunerParts().findCar( car.getIdCar() );
        if ( pTuner != nullptr ) {
            anchors << "tuner slots";
            for ( const std::string &slot : pTuner->Slots ) {
                anchors << " " << slot;
            }
            anchors << "\n";
            for ( const auto &slotBoxes : pTuner->HideBoxes ) {
                for ( const auto &box : slotBoxes.second ) {
                    anchors << "tuner hide_" << slotBoxes.first << " box ("
                            << box.first.x << " " << box.first.y << " "
                            << box.first.z << ")-(" << box.second.x << " "
                            << box.second.y << " " << box.second.z
                            << ") item space\n";
                }
            }
            for ( const auto &slotTris : pTuner->HideTris ) {
                for ( const auto &itemTris : slotTris.second ) {
                    anchors << "tuner hide_tris_" << slotTris.first << " item "
                            << itemTris.first << ":";
                    for ( uint32_t tri : itemTris.second ) {
                        anchors << " " << tri;
                    }
                    anchors << "\n";
                }
            }
            for ( const auto &slotOffset : pTuner->SlotOffsets ) {
                anchors << "tuner " << slotOffset.first << "_offset ("
                        << slotOffset.second.x << " " << slotOffset.second.y
                        << " " << slotOffset.second.z << ") node space\n";
            }
            anchors << "tuner pops " << pTuner->Pops << "\n";
        }
        for ( const TunerPart &part : GetTunerParts().getParts() ) {
            anchors << "part " << part.Id << " slot " << part.Slot << " mesh "
                    << part.Mesh << " node " << part.Node << " offset ("
                    << part.Offset.x << " " << part.Offset.y << " "
                    << part.Offset.z << ") align "
                    << ( part.Align.empty() ? "-" : part.Align ) << "\n";
        }
        anchors.close();
        written += " anchors.txt";
    }

    outFolder = folder.string();
    OTDU_LOG_INFO( "CarExport: %s written to %s%s\n", car.getFileName().c_str(),
                   outFolder.c_str(), written.c_str() );
    return true;
}

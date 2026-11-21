#include "shared.h"
#include "scene_doc.h"
#include "scene_json.h"

#include "filesystem/gs_file.h"

#include <algorithm>
#include <cstdio>

namespace {

constexpr const char *kScenesFolder = "scenes/";

bool IsScaleOne( const Vector3f &scale ) {
    return scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f;
}

JsonValue ObjectToJson( const SceneObject &object ) {
    JsonValue root = JsonValue::MakeObject();
    root.add( "id", JsonValue::MakeString( object.Id ) );
    root.add( "kind", JsonValue::MakeString( object.Kind ) );
    root.add( "ref", JsonValue::MakeString( object.Ref ) );
    if ( !object.Parent.empty() ) {
        root.add( "parent", JsonValue::MakeString( object.Parent ) );
    }
    JsonValue pos = JsonValue::MakeArray();
    pos.push( JsonValue::MakeNumber( object.Position.x ) );
    pos.push( JsonValue::MakeNumber( object.Position.y ) );
    pos.push( JsonValue::MakeNumber( object.Position.z ) );
    root.add( "pos", std::move( pos ) );
    if ( object.Yaw != 0.0f ) {
        root.add( "yaw", JsonValue::MakeNumber( object.Yaw ) );
    }
    if ( object.Pitch != 0.0f ) {
        root.add( "pitch", JsonValue::MakeNumber( object.Pitch ) );
    }
    if ( object.Roll != 0.0f ) {
        root.add( "roll", JsonValue::MakeNumber( object.Roll ) );
    }
    if ( !IsScaleOne( object.Scale ) ) {
        JsonValue scale = JsonValue::MakeArray();
        scale.push( JsonValue::MakeNumber( object.Scale.x ) );
        scale.push( JsonValue::MakeNumber( object.Scale.y ) );
        scale.push( JsonValue::MakeNumber( object.Scale.z ) );
        root.add( "scale", std::move( scale ) );
    }
    if ( !object.bVisible ) {
        root.add( "visible", JsonValue::MakeBool( false ) );
    }
    return root;
}

Vector3f ReadVec3( const JsonValue *pValue, const Vector3f &fallback ) {
    if ( pValue == nullptr || !pValue->isArray() || pValue->array.size() < 3 ) {
        return fallback;
    }
    return Vector3f( static_cast<float>( pValue->array[ 0 ].asNumber() ),
                     static_cast<float>( pValue->array[ 1 ].asNumber() ),
                     static_cast<float>( pValue->array[ 2 ].asNumber() ) );
}

// The Eden rotation of mod_props.cpp / drawInstances: the conjugated
// quaternion (the mirrored render space), Matrix4x4f::FromQuaternion built.
Matrix4x4f EdenRotationMatrix( const Quaternionf &q ) {
    return Matrix4x4f::FromQuaternion( Quaternionf( -q.x, -q.y, -q.z, q.w ) );
}

bool IsMatrixIdentity( const Matrix4x4f &m ) {
    for ( int32_t c = 0; c < 4; c++ ) {
        for ( int32_t r = 0; r < 4; r++ ) {
            if ( m.m[ c ][ r ] != ( c == r ? 1.0f : 0.0f ) ) {
                return false;
            }
        }
    }
    return true;
}

// D3D row-vector conventions of the engine (pmi.cpp / mod_props.cpp): the
// world matrix is T * RotY(yaw) * RotZ(roll) * RotX(pitch) * S.
Matrix4x4f BuildObjectMatrix( const Vector3f &position, float yawDegrees, float pitchDegrees, float rollDegrees,
                             const Vector3f &scale ) {
    const float yawRad = yawDegrees * 3.14159265f / 180.0f;
    const float pitchRad = pitchDegrees * 3.14159265f / 180.0f;
    const float rollRad = rollDegrees * 3.14159265f / 180.0f;
    const Quaternionf qYaw( 0.0f, std::sin( yawRad * 0.5f ), 0.0f, std::cos( yawRad * 0.5f ) );
    const Quaternionf qPitch( std::sin( pitchRad * 0.5f ), 0.0f, 0.0f, std::cos( pitchRad * 0.5f ) );
    const Quaternionf qRoll( 0.0f, 0.0f, std::sin( rollRad * 0.5f ), std::cos( rollRad * 0.5f ) );
    Matrix4x4f world = Matrix4x4f::Translation( position );
    world = world * EdenRotationMatrix( qYaw * qRoll * qPitch );
    if ( !IsScaleOne( scale ) ) {
        world = world * Matrix4x4f::Scale( scale );
    }
    return world;
}

} // namespace

JsonValue SceneDocument::toJson() const {
    JsonValue root = JsonValue::MakeObject();
    root.add( "version", JsonValue::MakeNumber( Version ) );
    root.add( "island", JsonValue::MakeString( Island ) );
    root.add( "name", JsonValue::MakeString( Name ) );
    JsonValue objects = JsonValue::MakeArray();
    for ( const SceneObject &object : Objects ) {
        objects.push( ObjectToJson( object ) );
    }
    root.add( "objects", std::move( objects ) );
    JsonValue overrides = JsonValue::MakeArray();
    for ( const SceneOverride &override : Overrides ) {
        JsonValue entry = JsonValue::MakeObject();
        entry.add( "kind", JsonValue::MakeString( override.Kind ) );
        entry.add( "target", JsonValue::MakeString( override.Target ) );
        entry.add( "hidden", JsonValue::MakeBool( override.bHidden ) );
        if ( override.Kind == "color" ) {
            JsonValue color = JsonValue::MakeArray();
            color.push( JsonValue::MakeNumber( override.Color.x ) );
            color.push( JsonValue::MakeNumber( override.Color.y ) );
            color.push( JsonValue::MakeNumber( override.Color.z ) );
            entry.add( "color", std::move( color ) );
        }
        overrides.push( std::move( entry ) );
    }
    root.add( "overrides", std::move( overrides ) );
    JsonValue roads = JsonValue::MakeArray();
    for ( const SceneRoad &road : Roads ) {
        JsonValue entry = JsonValue::MakeObject();
        entry.add( "id", JsonValue::MakeString( road.Id ) );
        JsonValue points = JsonValue::MakeArray();
        for ( const Vector3f &point : road.Points ) {
            JsonValue p = JsonValue::MakeArray();
            p.push( JsonValue::MakeNumber( point.x ) );
            p.push( JsonValue::MakeNumber( point.y ) );
            p.push( JsonValue::MakeNumber( point.z ) );
            points.push( std::move( p ) );
        }
        entry.add( "points", std::move( points ) );
        entry.add( "width", JsonValue::MakeNumber( road.Width ) );
        entry.add( "lanes", JsonValue::MakeNumber( road.Lanes ) );
        entry.add( "closed", JsonValue::MakeBool( road.bClosed ) );
        entry.add( "collide", JsonValue::MakeBool( road.bCollide ) );
        if ( !road.Texture.empty() ) {
            entry.add( "texture", JsonValue::MakeString( road.Texture ) );
        }
        roads.push( std::move( entry ) );
    }
    root.add( "roads", std::move( roads ) );
    JsonValue stations = JsonValue::MakeArray();
    for ( const SceneStation &station : Stations ) {
        JsonValue entry = JsonValue::MakeObject();
        entry.add( "id", JsonValue::MakeString( station.Id ) );
        entry.add( "name", JsonValue::MakeString( station.Name ) );
        JsonValue pos = JsonValue::MakeArray();
        pos.push( JsonValue::MakeNumber( station.Position.x ) );
        pos.push( JsonValue::MakeNumber( station.Position.y ) );
        pos.push( JsonValue::MakeNumber( station.Position.z ) );
        entry.add( "pos", std::move( pos ) );
        entry.add( "yaw", JsonValue::MakeNumber( station.Yaw ) );
        entry.add( "length", JsonValue::MakeNumber( station.Length ) );
        entry.add( "width", JsonValue::MakeNumber( station.Width ) );
        entry.add( "connect", JsonValue::MakeBool( station.bConnect ) );
        entry.add( "max_link", JsonValue::MakeNumber( station.MaxLink ) );
        stations.push( std::move( entry ) );
    }
    root.add( "stations", std::move( stations ) );
    return root;
}

bool SceneDocument::fromJson( const JsonValue &root, std::string &outError ) {
    if ( !root.isObject() ) {
        outError = "not a JSON object";
        return false;
    }
    Version = static_cast<int32_t>( root.getNumber( "version", 1.0 ) );
    Island = root.getString( "island", "ibiza" );
    ToLower( Island );
    Name = root.getString( "name", "" );
    Objects.clear();
    Overrides.clear();
    Roads.clear();
    Stations.clear();
    if ( const JsonValue *pObjects = root.find( "objects" ) ) {
        for ( const JsonValue &entry : pObjects->array ) {
            SceneObject object;
            object.Id = entry.getString( "id", "" );
            object.Kind = ToLower( entry.getString( "kind", "" ) );
            object.Ref = entry.getString( "ref", "" );
            object.Parent = entry.getString( "parent", "" );
            object.Position = ReadVec3( entry.find( "pos" ), Vector3f( 0.0f, 0.0f, 0.0f ) );
            object.Yaw = static_cast<float>( entry.getNumber( "yaw", 0.0 ) );
            object.Pitch = static_cast<float>( entry.getNumber( "pitch", 0.0 ) );
            object.Roll = static_cast<float>( entry.getNumber( "roll", 0.0 ) );
            object.Scale = ReadVec3( entry.find( "scale" ), Vector3f( 1.0f, 1.0f, 1.0f ) );
            object.bVisible = entry.getBool( "visible", true );
            if ( object.Id.empty() ) {
                outError = "an object has no id";
                return false;
            }
            Objects.push_back( std::move( object ) );
        }
    }
    if ( const JsonValue *pOverrides = root.find( "overrides" ) ) {
        for ( const JsonValue &entry : pOverrides->array ) {
            SceneOverride override;
            override.Kind = ToLower( entry.getString( "kind", "node" ) );
            override.Target = entry.getString( "target", "" );
            override.bHidden = entry.getBool( "hidden", true );
            if ( const JsonValue *pColor = entry.find( "color" ) ) {
                if ( pColor->isArray() && pColor->array.size() >= 3 ) {
                    override.Color = Vector4f( static_cast<float>( pColor->array[ 0 ].asNumber() ),
                                               static_cast<float>( pColor->array[ 1 ].asNumber() ),
                                               static_cast<float>( pColor->array[ 2 ].asNumber() ), 1.0f );
                }
            }
            if ( override.Target.empty() ) {
                continue;
            }
            Overrides.push_back( std::move( override ) );
        }
    }
    if ( const JsonValue *pRoads = root.find( "roads" ) ) {
        for ( const JsonValue &entry : pRoads->array ) {
            SceneRoad road;
            road.Id = entry.getString( "id", "" );
            if ( const JsonValue *pPoints = entry.find( "points" ) ) {
                for ( const JsonValue &point : pPoints->array ) {
                    road.Points.push_back( ReadVec3( &point, Vector3f( 0.0f, 0.0f, 0.0f ) ) );
                }
            }
            road.Width = static_cast<float>( entry.getNumber( "width", 7.0 ) );
            road.Lanes = static_cast<int32_t>( entry.getNumber( "lanes", 2.0 ) );
            road.bClosed = entry.getBool( "closed", false );
            road.bCollide = entry.getBool( "collide", true );
            road.Texture = entry.getString( "texture", "" );
            if ( road.Id.empty() || road.Points.size() < 2 ) {
                continue;
            }
            Roads.push_back( std::move( road ) );
        }
    }
    if ( const JsonValue *pStations = root.find( "stations" ) ) {
        for ( const JsonValue &entry : pStations->array ) {
            SceneStation station;
            station.Id = entry.getString( "id", "" );
            station.Name = entry.getString( "name", station.Id );
            station.Position = ReadVec3( entry.find( "pos" ), Vector3f( 0.0f, 0.0f, 0.0f ) );
            station.Yaw = static_cast<float>( entry.getNumber( "yaw", 0.0 ) );
            station.Length = static_cast<float>( entry.getNumber( "length", 34.0 ) );
            station.Width = static_cast<float>( entry.getNumber( "width", 24.0 ) );
            station.bConnect = entry.getBool( "connect", true );
            station.MaxLink = static_cast<float>( entry.getNumber( "max_link", 400.0 ) );
            if ( !station.Id.empty() ) {
                Stations.push_back( std::move( station ) );
            }
        }
    }
    return true;
}

bool SceneDocument::loadFromVfs( const std::string &fileName, std::string &outError ) {
    const std::string path = std::string( kScenesFolder ) + fileName;
    std::vector<uint8_t> bytes;
    if ( !gpFile->loadFile( path.c_str(), bytes ) ) {
        outError = "'" + path + "' not found";
        return false;
    }
    std::string text( bytes.begin(), bytes.end() );
    JsonValue root;
    if ( !JsonParse( text, root, outError ) ) {
        return false;
    }
    return fromJson( root, outError );
}

bool SceneDocument::saveToMods( const std::string &fileName, std::string &outError ) const {
    if ( gpFile == nullptr ) {
        outError = "file service missing";
        return false;
    }
    const std::filesystem::path folder = gpFile->getModsRoot() / kScenesFolder;
    std::error_code ec;
    std::filesystem::create_directories( folder, ec );
    const std::filesystem::path file = folder / fileName;
    FILE *pFile = fopen( file.string().c_str(), "wb" );
    if ( pFile == nullptr ) {
        outError = "cannot write '" + file.string() + "'";
        return false;
    }
    const std::string text = JsonWrite( toJson() );
    const bool bOk = fwrite( text.data(), 1, text.size(), pFile ) == text.size();
    fclose( pFile );
    if ( !bOk ) {
        outError = "write failed '" + file.string() + "'";
        return false;
    }
    OTDU_LOG_INFO( "SceneEditor: scene saved -> '%s'\n", file.string().c_str() );
    return true;
}

SceneObject *SceneDocument::findObject( const std::string &id ) {
    for ( SceneObject &object : Objects ) {
        if ( object.Id == id ) {
            return &object;
        }
    }
    return nullptr;
}

const SceneObject *SceneDocument::findObject( const std::string &id ) const {
    for ( const SceneObject &object : Objects ) {
        if ( object.Id == id ) {
            return &object;
        }
    }
    return nullptr;
}

SceneRoad *SceneDocument::findRoad( const std::string &id ) {
    for ( SceneRoad &road : Roads ) {
        if ( road.Id == id ) {
            return &road;
        }
    }
    return nullptr;
}

SceneStation *SceneDocument::findStation( const std::string &id ) {
    for ( SceneStation &station : Stations ) {
        if ( station.Id == id ) {
            return &station;
        }
    }
    return nullptr;
}

std::string SceneDocument::uniqueId( const std::string &base ) const {
    std::string candidate = base;
    int32_t suffix = 2;
    while ( findObject( candidate ) != nullptr ) {
        char buffer[ 16 ];
        snprintf( buffer, sizeof( buffer ), "_%d", suffix++ );
        candidate = base + buffer;
    }
    return candidate;
}

void SceneDocument::removeObject( const std::string &id ) {
    Objects.erase(
        std::remove_if( Objects.begin(), Objects.end(),
                        [ &id ]( const SceneObject &object ) { return object.Id == id; } ),
        Objects.end() );
    for ( SceneObject &object : Objects ) {
        if ( object.Parent == id ) {
            object.Parent.clear();
        }
    }
}

Matrix4x4f SceneDocument::objectMatrix( const SceneObject &object ) const {
    return BuildObjectMatrix( object.Position, object.Yaw, object.Pitch, object.Roll, object.Scale );
}

void SceneDocument::resolveWorlds( std::vector<Matrix4x4f> &outWorlds ) const {
    outWorlds.assign( Objects.size(), Matrix4x4f::Identity() );
    // Iterative parent resolution: every pass composes one more level; a
    // cycle terminates by the depth cap (its nodes compose from the root).
    for ( int32_t depth = 0; depth < 8; depth++ ) {
        bool bChanged = false;
        for ( size_t i = 0; i < Objects.size(); i++ ) {
            const SceneObject &object = Objects[ i ];
            if ( object.Parent.empty() ) {
                outWorlds[ i ] = objectMatrix( object );
                continue;
            }
            const SceneObject *pParent = findObject( object.Parent );
            if ( pParent == nullptr ) {
                outWorlds[ i ] = objectMatrix( object );
                continue;
            }
            const size_t parentIndex = static_cast<size_t>( pParent - Objects.data() );
            // Parent's world known (or the parent is a root): compose. A not
            // yet composed parent is identity only if it has a parent of its
            // own still pending; a next pass fills it.
            const bool bParentPending = !pParent->Parent.empty() && IsMatrixIdentity( outWorlds[ parentIndex ] ) &&
                                        pParent->Parent != pParent->Id;
            if ( !bParentPending ) {
                outWorlds[ i ] = outWorlds[ parentIndex ] * objectMatrix( object );
            } else {
                bChanged = true;
            }
        }
        if ( !bChanged ) {
            break;
        }
    }
}

std::vector<std::string> ListSceneFiles() {
    std::vector<std::string> names;
    if ( gpFile == nullptr ) {
        return names;
    }
    const std::vector<std::filesystem::path> files = gpFile->listFolder( kScenesFolder, ".scene.json" );
    for ( const std::filesystem::path &file : files ) {
        names.push_back( file.filename().string() );
    }
    std::sort( names.begin(), names.end() );
    return names;
}

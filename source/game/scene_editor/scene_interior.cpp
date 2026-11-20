#include "shared.h"
#include "scene_interior.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "game/spots/interior_walk.h"
#include "physics/havok_collision.h"

#include "core/xmb.h"

#include <algorithm>
#include <unordered_map>

// The interior-bank asset layer of the scene editor (see scene_interior.h).
// The ref rules are InteriorWalk's own bank-name rules, resolved against the
// game data; the walk data is the bank's own (.shk + Entity__FirstPerson).

namespace {

std::string BankFolder() {
    return gpFile != nullptr ? gpFile->getBankFolder() : std::string();
}

bool BankFileExists( const std::string &relativePath ) {
    return !relativePath.empty() && gpFile != nullptr &&
           gpFile->fileExists( ( gpFile->getBankFolder() + relativePath ).c_str() );
}

// orient_pos_scale of the entities.xmb placements (the same fields
// SpotExterior / IntroCinematic read).
void ReadPlacement( XmbValue entry, Vector3f &outPosition, Quaternionf &outRotation ) {
    outPosition = Vector3f( entry["pos"]["X"].asFloat(), entry["pos"]["Y"].asFloat(), entry["pos"]["Z"].asFloat() );
    outRotation = Quaternionf( entry["rot"]["X"].asFloat(), entry["rot"]["Y"].asFloat(), entry["rot"]["Z"].asFloat(),
                               entry["rot"]["W"].asFloat( 1.0f ) );
}

} // namespace

namespace SceneInterior {

std::vector<std::string> ListBanks() {
    std::vector<std::string> stems;
    if ( gpFile == nullptr ) {
        return stems;
    }
    for ( const std::filesystem::path &file : gpFile->listFolder( ( BankFolder() + "interior" ).c_str(), ".bnk" ) ) {
        std::string stem = file.filename().string();
        if ( stem.size() > 4 ) {
            stem.resize( stem.size() - 4 );
            // The __fr / __ge / __it / __sp / __us localisation clones stay
            // out: the plain stem is the asset (the walk modes load it too).
            if ( stem.find( "__" ) == std::string::npos ) {
                stems.push_back( stem );
            }
        }
    }
    std::sort( stems.begin(), stems.end() );
    return stems;
}

std::string ResolveBankPath( const std::string &ref ) {
    if ( ref.empty() || gpFile == nullptr ) {
        return "";
    }
    // `EPH_...:garage`: the house's garage room (the living room is the
    // default of the plain house ref).
    std::string name = ref;
    bool bGarage = false;
    const size_t colon = name.rfind( ':' );
    if ( colon != std::string::npos && ToLower( name.substr( colon + 1 ) ) == "garage" ) {
        bGarage = true;
        name.resize( colon );
    }
    if ( name.empty() ) {
        return "";
    }
    std::string relative;
    if ( name.size() > 4 && _strnicmp( name.c_str(), "EPH_", 4 ) == 0 ) {
        relative = InteriorWalk::HouseBankFor( name, bGarage );
    } else if ( !bGarage && ( name[ 0 ] == 'E' || name[ 0 ] == 'e' ) ) {
        // A shop spot bank (ECD_FERR ...): its interior by the retail rule.
        relative = InteriorWalk::InteriorBankFor( name );
    }
    if ( relative.empty() ) {
        // The interior bank stem itself (icd_ferr, lph_l2e, gph_l2e, itun_a...).
        relative = "interior/" + ToLower( name ) + ".bnk";
    }
    return BankFileExists( relative ) ? BankFolder() + relative : "";
}

std::string DescribeRef( const std::string &ref ) {
    const std::string path = ResolveBankPath( ref );
    if ( path.empty() ) {
        return ref;
    }
    const size_t slash = path.find_last_of( "/\\" );
    std::string stem = slash != std::string::npos ? path.substr( slash + 1 ) : path;
    if ( stem.size() > 4 && _stricmp( stem.c_str() + stem.size() - 4, ".bnk" ) == 0 ) {
        stem.resize( stem.size() - 4 );
    }
    return stem;
}

bool LoadWalkData( const std::string &bankPath, WalkData &out ) {
    out = WalkData();
    Bank bank;
    if ( !bank.load( bankPath.c_str() ) ) {
        OTDU_LOG_WARN( "SceneInterior: '%s' could not be loaded\n", bankPath.c_str() );
        return false;
    }
    // The walk collision (the room's floors / walls / stairs; the same file
    // the walk modes load, intro_cinematic.cpp).
    for ( const BankEntry *pShk : bank.findEntriesByExtension( "shk" ) ) {
        if ( LoadHavokCollision( pShk->pData, pShk->Size, out.Collision, true ) ) {
            OTDU_LOG_INFO( "SceneInterior: collision %s: %zu triangles, %zu boxes, %zu capsules\n", pShk->Name.c_str(),
                           out.Collision.Indices.size() / 3, out.Collision.Boxes.size(), out.Collision.Capsules.size() );
        }
        break;
    }
    // The retail walk start: Entity__FirstPerson { StartCamera = Entity__Camera __Hc }
    // (spot_interiors_spec.md 2 - cam_entree in every bank checked).
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument entities;
    if ( pEntities == nullptr || !entities.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "SceneInterior: '%s' has no entities.xmb\n", bankPath.c_str() );
        return !out.Collision.Indices.empty() || !out.Collision.Boxes.empty();
    }
    XmbValue entityExport = entities.getRoot()["ExportType_Entity"];
    std::unordered_map<uint64_t, std::string> cameraNames;
    std::unordered_map<std::string, std::pair<Vector3f, Quaternionf>> cameras;
    for ( uint32_t i = 0; i < entityExport.getArraySize( "ArrayOf_Entity__Camera" ); i++ ) {
        XmbValue entry = entityExport.getArrayElement( "ArrayOf_Entity__Camera", i );
        const std::string name = entry["__Name"].asString();
        cameraNames[ entry["__Hc"].asUInt64() ] = name;
        ReadPlacement( entry["orient_pos_scale"], cameras[ name ].first, cameras[ name ].second );
    }
    if ( entityExport.getArraySize( "ArrayOf_Entity__FirstPerson" ) > 0 ) {
        const auto it = cameraNames.find( entityExport.getArrayElement( "ArrayOf_Entity__FirstPerson", 0 )
                                              ["StartCamera"].asUInt64() );
        if ( it != cameraNames.end() ) {
            const auto &pose = cameras[ it->second ];
            out.StartEye = pose.first;
            out.StartHeading = pose.second;
            out.bHasStart = true;
            out.StartCameraName = it->second;
        }
    }
    return true;
}

} // namespace SceneInterior

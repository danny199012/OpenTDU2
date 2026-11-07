#include "shared.h"
#include "car_delivery.h"

#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "game/spots/spot_exteriors.h"

#include <cstring>

namespace {
// The spots.bin record of a shop: the DBCarShop `_spot_name` is the record id
// (GetIdentifier64bit, spot_interiors_spec.md 4), the bank name the fallback
// for the callers that only have the building.
const SpotRecord *FindShopSpot( const std::string &island, const std::string &spotName,
                               std::vector<SpotRecord> &outSpots ) {
    outSpots = LoadSpotList( island );
    const uint64_t id = GetIdentifier64bit( spotName.c_str() );
    for ( const SpotRecord &spot : outSpots ) {
        if ( spot.Id == id ) {
            return &spot;
        }
    }
    for ( const SpotRecord &spot : outSpots ) {
        if ( spot.Bank == spotName || spotName.rfind( spot.Bank, 0 ) == 0 ) {
            return &spot;
        }
    }
    return nullptr;
}
} // namespace

std::vector<CarDeliveryPose> LoadShopCarPositions( const std::string &island,
                                                   const std::string &spotName ) {
    std::vector<CarDeliveryPose> poses;
    std::vector<SpotRecord> spots;
    const SpotRecord *pSpot = FindShopSpot( island, spotName, spots );
    if ( pSpot == nullptr ) {
        OTDU_LOG_WARN( "CarDelivery: '%s' is not a spot of '%s'\n", spotName.c_str(), island.c_str() );
        return poses;
    }
    if ( pSpot->ExteriorPath.empty() ) {
        OTDU_LOG_WARN( "CarDelivery: spot '%s' has no exterior bank\n", pSpot->Bank.c_str() );
        return poses;
    }
    Bank bank;
    // ExteriorPath is already a full path (SpotExteriors builds it).
    const std::string path = pSpot->ExteriorPath;
    if ( !bank.load( path.c_str() ) ) {
        OTDU_LOG_WARN( "CarDelivery: '%s' could not be loaded\n", path.c_str() );
        return poses;
    }
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument document;
    if ( pEntities == nullptr || !document.load( pEntities->pData, pEntities->Size ) ) {
        OTDU_LOG_WARN( "CarDelivery: '%s' has no entities.xmb\n", path.c_str() );
        return poses;
    }
    // The bank's frame: the spot exteriors place it at
    // T(position) R_y(heading) (spot_exteriors.cpp).
    const Matrix4x4f toWorld =
        Matrix4x4f::Translation( pSpot->Position ) *
        Matrix4x4f::FromQuaternion( Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), pSpot->Heading ) );
    const XmbValue entityExport = document.getRoot()["ExportType_Entity"];
    const XmbType *pType = entityExport.getType();
    if ( !entityExport.isValid() || pType == nullptr ) {
        OTDU_LOG_WARN( "CarDelivery: '%s' has no ExportType_Entity\n", path.c_str() );
        return poses;
    }
    for ( const XmbType::Field &field : pType->Fields ) {
        if ( !field.bArray || field.Name != "ArrayOf_Entity__CarPos" ) {
            continue;
        }
        const uint32_t count = entityExport.getArraySize( field.Name.c_str() );
        for ( uint32_t i = 0; i < count; i++ ) {
            const XmbValue entry = entityExport.getArrayElement( field.Name.c_str(), i );
            if ( !entry.isValid() ) {
                continue;
            }
            CarDeliveryPose pose;
            pose.Name = entry["__Name"].asString();
            pose.Type = entry["CarPosType"].asInt();
            pose.PlayerIndex = entry["PlayerIndex"].asInt();
            Vector3f local( 0.0f, 0.0f, 0.0f );
            Quaternionf rotation;
            const XmbValue placement = entry["orient_pos_scale"];
            if ( placement.isValid() ) {
                const XmbValue position = placement["pos"];
                local = Vector3f( position["X"].asFloat(), position["Y"].asFloat(),
                                  position["Z"].asFloat() );
                const XmbValue rot = placement["rot"];
                rotation = Quaternionf( rot["X"].asFloat(), rot["Y"].asFloat(),
                                        rot["Z"].asFloat(), rot["W"].asFloat( 1.0f ) );
            }
            pose.Position = toWorld.transformPoint( local );
            // The heading the car faces: the composed orientation's forward
            // (+z, the spot / vehicle convention) flattened.
            const Quaternionf composed =
                Quaternionf::FromAxisAngle( Vector3f( 0.0f, 1.0f, 0.0f ), pSpot->Heading ) * rotation;
            const Vector3f forward = composed.rotate( Vector3f( 0.0f, 0.0f, 1.0f ) );
            pose.Heading = atan2f( forward.x, forward.z );
            poses.push_back( std::move( pose ) );
        }
    }
    OTDU_LOG_INFO( "CarDelivery: '%s' (%s) has %zu car position(s)\n", pSpot->Bank.c_str(),
                   pSpot->ExteriorPath.c_str(), poses.size() );
    for ( const CarDeliveryPose &pose : poses ) {
        OTDU_LOG_INFO( "CarDelivery:   '%s' type %d player %d at (%.1f, %.1f, %.1f) heading %.0f deg\n",
                       pose.Name.c_str(), pose.Type, pose.PlayerIndex, pose.Position.x, pose.Position.y,
                       pose.Position.z, pose.Heading * kRadToDeg );
    }
    return poses;
}

bool FindCarDeliveryPose( const std::string &island, const std::string &spotName,
                          CarDeliveryPose &outPose ) {
    const std::vector<CarDeliveryPose> poses = LoadShopCarPositions( island, spotName );
    if ( poses.empty() ) {
        // No `Entity__CarPos` in the bank: the spot's own frame (the building)
        // still puts the car outside rather than nowhere.
        std::vector<SpotRecord> spots;
        const SpotRecord *pSpot = FindShopSpot( island, spotName, spots );
        if ( pSpot == nullptr ) {
            return false;
        }
        outPose = CarDeliveryPose();
        outPose.Position = pSpot->Position;
        outPose.Heading = pSpot->Heading;
        outPose.Name = "(spot frame)";
        return true;
    }
    // The player's own parking first: `PosCarParking` is `CarPosType` 4 with
    // `PlayerIndex` 1 (ECD_FERR / ECD_AUDI), the shop's traffic and brand cars
    // are type 5 with 2001 / other indices [MEASURED]. Then the name, then the
    // first entry - every one of them is a car position of the shop's own lot.
    for ( const CarDeliveryPose &pose : poses ) {
        if ( pose.Type == 4 ) {
            outPose = pose;
            return true;
        }
    }
    for ( const CarDeliveryPose &pose : poses ) {
        if ( pose.Name.find( "Parking" ) != std::string::npos ||
             pose.Name.find( "parking" ) != std::string::npos ) {
            outPose = pose;
            return true;
        }
    }
    outPose = poses.front();
    return true;
}

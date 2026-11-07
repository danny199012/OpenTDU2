#include "shared.h"
#include "discovery_points.h"

#include "core/crypto/cpr.h"
#include "core/xmb.h"
#include "filesystem/bank.h"
#include "filesystem/gs_file.h"
#include "game/freeride/cruising_zones.h"
#include "render/3dd.h"
#include "system/cmd_line.h"

void DiscoveryPoints::load( const std::string &island ) {
    wrecks.clear();
    panoramas.clear();
    const std::string folder = gpFile->getBankFolder() + "islands/" + island + "/scenarics/";
    // Detector limits (wrecks_config.cpr, XML).
    std::string config;
    if ( LoadCPRText( ( gpFile->getBankFolder() + "scenarics/wrecks/wrecks_config.cpr" ).c_str(), config ) ) {
        auto readAttribute = [&]( const char *pName, float &outValue ) {
            const size_t at = config.find( std::string( pName ) + "=\"" );
            if ( at != std::string::npos ) {
                outValue = static_cast<float>( atof( config.c_str() + at + strlen( pName ) + 2 ) );
            }
        };
        readAttribute( "max_detector_range", detectorRange );
        readAttribute( "min_activation_distance", activationDistance );
    }
    // Wrecks: entities.xmb of wrecks.bnk.
    Bank wreckBank;
    if ( gpFile->fileExists( ( folder + "wrecks/wrecks.bnk" ).c_str() ) && wreckBank.load( ( folder + "wrecks/wrecks.bnk" ).c_str() ) ) {
        const BankEntry *pEntities = wreckBank.findEntry( "entities.xmb" );
        XmbDocument document;
        if ( pEntities != nullptr && document.load( pEntities->pData, pEntities->Size ) ) {
            XmbValue entities = document.getRoot()["ExportType_Entity"];
            for ( uint32_t i = 0; i < entities.getArraySize( "ArrayOf_Entity__Wreck" ); i++ ) {
                XmbValue row = entities.getArrayElement( "ArrayOf_Entity__Wreck", i );
                XmbValue pos = row["orient_pos_scale"]["pos"];
                DiscoveryPoint point;
                point.Id = static_cast<int32_t>( row["ID"].asInt() );
                point.Name = row["__Name"].asString();
                point.X = pos["X"].asFloat();
                point.Y = pos["Y"].asFloat();
                point.Z = pos["Z"].asFloat();
                point.Area = GetCruisingZones().find( point.X, point.Z );
                wrecks.push_back( point );
            }
        }
    }
    // Panoramas: the nodes of the photos .3DD (one per photo frame).
    Bank photoBank;
    if ( gpFile->fileExists( ( folder + "photos.bnk" ).c_str() ) && photoBank.load( ( folder + "photos.bnk" ).c_str() ) ) {
        for ( const BankEntry &entry : photoBank.getEntries() ) {
            if ( entry.Extension != "3dd" ) {
                continue;
            }
            Render3DD scene;
            if ( !scene.parse( entry.pData, entry.Size ) ) {
                continue;
            }
            int32_t id = 0;
            for ( const SceneNode &node : scene.getNodes() ) {
                DiscoveryPoint point;
                point.Id = id++;
                point.Name = node.Name;
                point.X = node.World.m[3][0];
                point.Y = node.World.m[3][1];
                point.Z = node.World.m[3][2];
                point.Area = GetCruisingZones().find( point.X, point.Z );
                panoramas.push_back( point );
            }
        }
    }
    OTDU_LOG_INFO( "Discovery: %s: %zu wrecks, %zu panorama nodes, detector range %.0f m, activation %.0f m\n", island.c_str(),
                   wrecks.size(), panoramas.size(), detectorRange, activationDistance );
    if ( CmdLine::HasArg( "-logdiscovery" ) ) {
        for ( const DiscoveryPoint &p : wrecks ) {
            OTDU_LOG_INFO( "Discovery: wreck %d '%s' at (%.0f, %.0f, %.0f) area %d\n", p.Id, p.Name.c_str(), p.X, p.Y, p.Z, p.Area );
        }
        for ( const DiscoveryPoint &p : panoramas ) {
            OTDU_LOG_INFO( "Discovery: panorama node %d '%s' at (%.0f, %.0f, %.0f) area %d\n", p.Id, p.Name.c_str(), p.X, p.Y, p.Z, p.Area );
        }
    }
}

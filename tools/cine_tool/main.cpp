// otdu2_cine_tool: dumps the cutscene authoring data of the
// scenarics/cinematics banks (.cin shot timelines, .sce scenes) as readable
// Markdown reports, plus optional JSON with the raw key data (for the future
// cutscene editor and external tooling). Read-only: everything is decoded by
// the same parsers the game uses (Cinematic / CinematicScene), no new format.
//   otdu2_cine_tool list <game root | cinematics folder | file.bnk>
//   otdu2_cine_tool dump <game root | cinematics folder | file.bnk> [output
//   folder] [-json] [-variants]
//     -json      also write one .json per cinematic (raw tracks, hashcodes)
//     -variants  include the language variant banks (meeting_tess__us.bnk...)
//   otdu2_cine_tool manifest <...> [output folder] [-json] [-variants]
//     [-lang us] [-avatar AVT_Male_Intro]
//     dump plus the resolution layers: scene REFS -> bank assets, objects ->
//     entities (PNJ characters + outfits from db_data.cpr), Fx events ->
//     .bfx nodes / per-language wav names, the message protocol (known codes
//     vs [ENC]) and the story kind placements. Also writes a subtitle
//     skeleton CSV per cinematic (one row per voice cue).
#include "cine_common.h"

#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

#include <cstdio>
#include <fstream>
#include <sstream>

// manifest.cpp
int ManifestAll( const std::string &source, const std::string &output,
                 bool bJson, bool bVariants );

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// per cinematic dump

static void DumpObjectJson( JsonOut &json,
                            const CinematicScene::Object &object ) {
    json.beginObject();
    json.str( "name", object.Name );
    json.str( "class", object.isCamera() ? "camera" : "actor" );
    json.raw( "class_hash", JsonQuote( Hash32( object.ClassHash ) ) );
    json.raw( "name_hash", JsonQuote( Hash32( object.NameHash ) ) );
    json.raw( "entity_hashcode", JsonQuote( Hash64( object.EntityHashcode ) ) );

    if ( !object.Properties.empty() ) {
        json.key( "properties" );
        json.beginArray();
        for ( const CinematicScene::Property &property : object.Properties ) {
            json.beginObject();
            json.str( "name", property.Name );
            json.key( "keys" );
            json.beginArray();
            for ( size_t k = 0;
                  k < property.Times.size() && k < property.Values.size();
                  k++ ) {
                json.beginArray();
                json.value( JsonNum( property.Times[k] ) );
                json.value( JsonQuote( property.Values[k] ) );
                json.endArray();
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();
    }

    if ( !object.Tracks.empty() ) {
        json.key( "tracks" );
        json.beginArray();
        for ( const CinematicScene::Track &track : object.Tracks ) {
            json.beginObject();
            json.raw( "id", JsonQuote( Hash32( track.Id ) ) );
            json.str( "kind", TrackKindName( track.Id ) );
            json.num( "components", track.NumComponents );
            json.key( "keys" );
            json.beginArray();
            for ( size_t k = 0; k < track.Times.size(); k++ ) {
                json.beginArray();
                json.value( JsonNum( track.Times[k] ) );
                for ( uint32_t c = 0; c < track.NumComponents; c++ ) {
                    const size_t index = k * track.NumComponents + c;
                    const float value = index < track.Values.size()
                                            ? track.Values[index]
                                            : 0.0f;
                    json.value( JsonNum( value ) );
                }
                json.endArray();
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();
    }
    json.endObject();
}

static CinSummary DumpCinematic( const Bank &bank,
                                 const BankEntry &entry,
                                 const std::string &bankStem,
                                 const fs::path &outDir,
                                 bool bJson ) {
    CinSummary summary;
    summary.bankStem = bankStem;
    summary.cinName = entry.Name;

    Cinematic cine;
    std::vector<ParsedScene> scenes;
    if ( !ParseCinematic( bank, entry, cine, scenes ) ) {
        printf( "  [!] %s.cin: parse failed\n", entry.Name.c_str() );
        return summary;
    }
    summary.ok = true;
    summary.duration = cine.getDuration();
    summary.shots = static_cast<int>( cine.getShots().size() );
    summary.scenes = static_cast<int>( cine.getSceneReferences().size() );
    summary.messages = static_cast<int>( cine.getMessages().size() );
    summary.fx = static_cast<int>( cine.getFxEvents().size() );

    const std::vector<Cinematic::Reference> &refs = cine.getReferences();
    const fs::path outFolder = outDir / bankStem;
    std::error_code ec;
    fs::create_directories( outFolder, ec );

    // ---- Markdown ---------------------------------------------------------
    std::ostringstream md;
    md << "# " << entry.Name << "\n\n";
    md << "Timeline `" << entry.Filename << "` of bank `" << bankStem
       << ".bnk` (" << entry.Size << " bytes). Duration **"
       << F( cine.getDuration() ) << " s**, " << cine.getShots().size()
       << " shots, " << scenes.size() << " scene(s).\n";

    md << "\nOther timelines in this bank:";
    int otherCins = 0;
    for ( const BankEntry &other : bank.getEntries() ) {
        if ( StringCmpI( other.Extension.c_str(), "cin" ) == 0 &&
             other.Name != entry.Name ) {
            md << " `" << other.Filename << "`";
            otherCins++;
        }
    }
    if ( otherCins == 0 ) {
        md << " none";
    }
    md << "\n";

    md << "\n## References\n\n";
    md << "| # | type | hashcode | text / name |\n|---|---|---|---|\n";
    for ( size_t i = 0; i < refs.size(); i++ ) {
        const char *kind = "?";
        switch ( refs[i].Type ) {
        case 0x020b:
            kind = "message";
            break;
        case 0x010d:
            kind = "fx";
            break;
        case 0x0303:
            kind = "scene";
            break;
        }
        md << "| " << i << " | " << kind << " (" << Hash32( refs[i].Type )
           << ") | " << Hash64( refs[i].Hashcode ) << " | `" << refs[i].Text
           << "` |\n";
    }

    md << "\n## Shots\n\n";
    md << "| # | t | dur | scene | camera | scene window | rate | fade in "
          "|\n|---|---|---|---|---|---|---|---|\n";
    for ( size_t s = 0; s < cine.getShots().size(); s++ ) {
        const Cinematic::Shot &shot = cine.getShots()[s];
        std::string sceneName = "?";
        if ( shot.Scene < scenes.size() ) {
            sceneName = "`" + scenes[shot.Scene].displayName + "`";
            if ( !scenes[shot.Scene].ok ) {
                sceneName += " (unresolved)";
            }
        }
        std::string cameraName = Hash32( shot.CameraHash );
        if ( shot.Scene < scenes.size() && scenes[shot.Scene].ok ) {
            if ( const CinematicScene::Object *pCamera =
                     scenes[shot.Scene].scene.findObjectByHash(
                         shot.CameraHash ) ) {
                cameraName = pCamera->Name;
            }
        }
        const float rate =
            shot.Duration > 0.0f
                ? ( shot.CameraEnd - shot.CameraStart ) / shot.Duration
                : 1.0f;
        std::string fade = "-";
        if ( shot.FadeType == 1 ) {
            fade = "from (" + F( shot.FadeColor.x ) + ", " +
                   F( shot.FadeColor.y ) + ", " + F( shot.FadeColor.z ) +
                   ") in " + F( shot.FadeDuration ) + " s";
        }
        md << "| " << s << " | " << F( shot.Start ) << " | "
           << F( shot.Duration ) << " | " << sceneName << " | " << cameraName
           << " | " << F( shot.CameraStart ) << " -> " << F( shot.CameraEnd )
           << " | " << F( rate ) << " | " << fade << " |\n";
    }

    md << "\nRate is the scene time per cinematic time (a rate below 1 is the "
          "slow motion of a \"time stop\" shot). "
          "Camera / window / rate and fades are `[DATA]`; yaw and fov below "
          "are derived.\n";

    if ( !cine.getMessages().empty() ) {
        md << "\n## Messages\n\n| t | dur | text |\n|---|---|---|\n";
        for ( const Cinematic::Event &event : cine.getMessages() ) {
            std::string text = "#?";
            if ( event.Reference >= 0 &&
                 event.Reference < static_cast<int32_t>( refs.size() ) ) {
                text = "`" + refs[event.Reference].Text + "`";
            }
            md << "| " << F( event.Time ) << " | " << F( event.Duration )
               << " | " << text << " |\n";
        }
    }

    if ( !cine.getFxEvents().empty() ) {
        md << "\n## FX events\n\n| t | dur | fx | world position "
              "|\n|---|---|---|---|\n";
        for ( const Cinematic::Event &event : cine.getFxEvents() ) {
            std::string fxName = "#?";
            if ( event.Reference >= 0 &&
                 event.Reference < static_cast<int32_t>( refs.size() ) ) {
                fxName = Hash64( refs[event.Reference].Hashcode );
            }
            const Vector3f pos( event.World.m[3][0], event.World.m[3][1],
                                event.World.m[3][2] );
            md << "| " << F( event.Time ) << " | " << F( event.Duration )
               << " | " << fxName << " | " << Vec3( pos ) << " |\n";
        }
    }

    for ( size_t sceneIndex = 0; sceneIndex < scenes.size(); sceneIndex++ ) {
        const ParsedScene &parsed = scenes[sceneIndex];
        md << "\n## Scene " << sceneIndex << ": `" << parsed.displayName
           << "`\n\n";
        if ( !parsed.ok ) {
            md << "Not found in this bank (or parse failed) - camera / actor "
                  "data unavailable.\n";
            continue;
        }
        const CinematicScene &scene = parsed.scene;
        int cameras = 0, actors = 0;
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            object.isCamera() ? cameras++ : actors++;
        }
        summary.cameras = cameras;
        summary.actors = actors;

        const Matrix4x4f &origin = scene.getOrigin();
        const Vector3f originPos = origin.getTranslation();
        md << "- Duration " << F( scene.getDuration() ) << " s, "
           << scene.getObjects().size() << " objects (" << cameras
           << " cameras, " << actors << " actors)\n";
        md << "- Origin (scene local -> world): position " << Vec3( originPos )
           << ", heading " << F( OriginHeadingDeg( origin ) ) << " deg\n";
        md << "- Origin rows (D3D, translation in the last row): ["
           << F( origin.m[0][0] ) << " " << F( origin.m[0][1] ) << " "
           << F( origin.m[0][2] ) << "] [" << F( origin.m[1][0] ) << " "
           << F( origin.m[1][1] ) << " " << F( origin.m[1][2] ) << "] ["
           << F( origin.m[2][0] ) << " " << F( origin.m[2][1] ) << " "
           << F( origin.m[2][2] ) << "] [" << F( origin.m[3][0] ) << " "
           << F( origin.m[3][1] ) << " " << F( origin.m[3][2] ) << "]\n";
        md << "- The origin is the authored placement; scenes anchored to "
              "gameplay (the dream plays where the player "
              "parked) get their world transform from the game code instead - "
              "see docs/re/cutscenes_inventory.md.\n";

        md << "\n### Cameras\n";
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            if ( !object.isCamera() ) {
                continue;
            }
            const CinematicScene::Track *pPosition =
                object.findTrack( CinematicScene::kTrackPosition );
            const CinematicScene::Track *pHeading =
                object.findTrack( CinematicScene::kTrackRotation );
            const CinematicScene::Track *pFov =
                object.findTrack( CinematicScene::kTrackFov );
            md << "\n**" << object.Name << "** -";
            if ( pPosition != nullptr )
                md << " Position " << pPosition->Times.size() << " keys";
            if ( pHeading != nullptr )
                md << ", Heading " << pHeading->Times.size() << " keys";
            if ( pFov != nullptr )
                md << ", Fov " << pFov->Times.size() << " keys";
            md << "\n\n";
            if ( pPosition == nullptr || pPosition->Times.empty() ) {
                md << "(no position track)\n";
                continue;
            }
            md << "| t | position (scene local) | yaw deg | fov deg "
                  "|\n|---|---|---|---|\n";
            // Baked 30 Hz paths have hundreds of keys - stride the table,
            // the JSON keeps every key.
            const size_t keyCount = pPosition->Times.size();
            const size_t stride = Max<size_t>( 1, ( keyCount + 47 ) / 48 );
            if ( stride > 1 ) {
                md << "(" << keyCount << " keys, showing every " << stride
                   << "th - the JSON has them all)\n";
            }
            for ( size_t k = 0; k < keyCount; k++ ) {
                if ( k % stride != 0 && k + 1 != keyCount ) {
                    continue;
                }
                const float time = pPosition->Times[k];
                const Vector3f pos = object.samplePosition( time );
                std::string yaw = "-";
                if ( pHeading != nullptr && !pHeading->Times.empty() ) {
                    yaw = F( YawDeg( object.sampleRotation( time ) ) );
                }
                std::string fov = "-";
                if ( pFov != nullptr && !pFov->Times.empty() ) {
                    fov = F( object.sampleFloat( CinematicScene::kTrackFov,
                                                 time, 60.0f ) );
                }
                md << "| " << F( time ) << " | " << Vec3( pos ) << " | " << yaw
                   << " | " << fov << " |\n";
            }
        }

        md << "\n### Actors\n";
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            if ( object.isCamera() ) {
                continue;
            }
            md << "\n**" << object.Name << "** (entity "
               << Hash64( object.EntityHashcode ) << ")";
            if ( !object.Tracks.empty() ) {
                md << " -";
                for ( const CinematicScene::Track &track : object.Tracks ) {
                    md << " " << TrackKindName( track.Id ) << " "
                       << track.Times.size() << " keys";
                }
            }
            md << "\n";
            if ( const CinematicScene::Track *pPosition =
                     object.findTrack( CinematicScene::kTrackPosition ) ) {
                if ( !pPosition->Times.empty() ) {
                    const float first = pPosition->Times.front();
                    const float last = pPosition->Times.back();
                    md << "- path: t=" << F( first ) << " "
                       << Vec3( object.samplePosition( first ) )
                       << " -> t=" << F( last ) << " "
                       << Vec3( object.samplePosition( last ) ) << "\n";
                }
            }
            if ( const CinematicScene::Track *pAnim =
                     object.findTrack( CinematicNameHash( "Animation" ) ) ) {
                md << "- animation keys at t:";
                for ( const float time : pAnim->Times ) {
                    md << " " << F( time );
                }
                md << " (resolved in the manifest)\n";
            }
            for ( const CinematicScene::Property &property :
                  object.Properties ) {
                md << "- property `" << property.Name << "`:";
                for ( size_t k = 0;
                      k < property.Times.size() && k < property.Values.size();
                      k++ ) {
                    md << " t=" << F( property.Times[k] ) << " \""
                       << property.Values[k] << "\"";
                }
                md << "\n";
            }
        }
    }

    md << "\n---\nGenerated by `otdu2_cine_tool dump` (parsers: "
          "`source/game/cinematic/cinematic_scene.cpp`).\n";

    const fs::path mdPath = outFolder / ( entry.Name + ".md" );
    std::ofstream stream( mdPath, std::ios::binary );
    stream << md.str();

    // ---- JSON -------------------------------------------------------------
    if ( bJson ) {
        JsonOut json;
        json.beginObject();
        json.str( "cinematic", entry.Name );
        json.str( "bank", bankStem + ".bnk" );
        json.num( "duration", cine.getDuration() );

        json.key( "refs" );
        json.beginArray();
        for ( const Cinematic::Reference &ref : refs ) {
            json.beginObject();
            json.str( "type", Hash32( ref.Type ) );
            json.hash( "hashcode", ref.Hashcode );
            json.str( "text", ref.Text );
            json.endObject();
        }
        json.endArray();

        json.key( "shots" );
        json.beginArray();
        for ( const Cinematic::Shot &shot : cine.getShots() ) {
            json.beginObject();
            json.num( "t", shot.Start );
            json.num( "duration", shot.Duration );
            json.num( "scene", shot.Scene );
            json.raw( "camera_hash", JsonQuote( Hash32( shot.CameraHash ) ) );
            json.num( "scene_start", shot.CameraStart );
            json.num( "scene_end", shot.CameraEnd );
            json.raw( "fade_type", std::to_string( shot.FadeType ) );
            json.raw( "fade_duration", F( shot.FadeDuration ) );
            json.key( "fade_color" );
            json.beginArray();
            json.value( JsonNum( shot.FadeColor.x ) );
            json.value( JsonNum( shot.FadeColor.y ) );
            json.value( JsonNum( shot.FadeColor.z ) );
            json.endArray();
            json.endObject();
        }
        json.endArray();

        json.key( "messages" );
        json.beginArray();
        for ( const Cinematic::Event &event : cine.getMessages() ) {
            json.beginObject();
            json.num( "t", event.Time );
            json.num( "duration", event.Duration );
            json.num( "ref", event.Reference );
            json.num( "track", event.Track );
            if ( event.Reference >= 0 &&
                 event.Reference < static_cast<int32_t>( refs.size() ) ) {
                json.str( "text", refs[event.Reference].Text );
            }
            json.endObject();
        }
        json.endArray();

        json.key( "fx" );
        json.beginArray();
        for ( const Cinematic::Event &event : cine.getFxEvents() ) {
            json.beginObject();
            json.num( "t", event.Time );
            json.num( "duration", event.Duration );
            json.num( "ref", event.Reference );
            json.raw( "flags", std::to_string( event.Flags ) );
            if ( event.Reference >= 0 &&
                 event.Reference < static_cast<int32_t>( refs.size() ) ) {
                json.hash( "hashcode", refs[event.Reference].Hashcode );
            }
            json.key( "world" );
            json.beginArray();
            for ( int c = 0; c < 4; c++ ) {
                for ( int r = 0; r < 4; r++ ) {
                    json.value( JsonNum( event.World.m[c][r] ) );
                }
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();

        json.key( "scenes" );
        json.beginArray();
        for ( const ParsedScene &parsed : scenes ) {
            json.beginObject();
            json.str( "file", parsed.displayName );
            json.raw( "parsed", parsed.ok ? "true" : "false" );
            if ( parsed.ok ) {
                const Matrix4x4f &origin = parsed.scene.getOrigin();
                json.key( "origin" );
                json.beginArray();
                for ( int c = 0; c < 4; c++ ) {
                    for ( int r = 0; r < 4; r++ ) {
                        json.value( JsonNum( origin.m[c][r] ) );
                    }
                }
                json.endArray();
                json.key( "objects" );
                json.beginArray();
                for ( const CinematicScene::Object &object :
                      parsed.scene.getObjects() ) {
                    DumpObjectJson( json, object );
                }
                json.endArray();
            }
            json.endObject();
        }
        json.endArray();
        json.endObject();

        const fs::path jsonPath = outFolder / ( entry.Name + ".json" );
        std::ofstream jsonStream( jsonPath, std::ios::binary );
        jsonStream << json.str() << "\n";
    }

    printf( "  %s  %.1fs %d shots -> %s\n", entry.Name.c_str(),
            cine.getDuration(), summary.shots,
            fs::path( outFolder / ( entry.Name + ".md" ) ).string().c_str() );
    return summary;
}

// ---------------------------------------------------------------------------
// commands

static int DumpAll( const std::string &source,
                    const std::string &output,
                    bool bJson,
                    bool bVariants ) {
    std::vector<fs::path> banks;
    CollectBanks( source, bVariants, banks );
    if ( banks.empty() ) {
        printf( "no .bnk found under %s\n", source.c_str() );
        return 1;
    }
    const fs::path outDir( output );
    std::error_code ec;
    fs::create_directories( outDir, ec );

    std::vector<CinSummary> summaries;
    int failedBanks = 0;
    for ( const fs::path &bankPath : banks ) {
        const std::string stem = bankPath.stem().string();
        Bank bank;
        if ( !bank.load( bankPath.string().c_str() ) ) {
            printf( "[!] cannot load %s\n", bankPath.string().c_str() );
            failedBanks++;
            continue;
        }
        int cins = 0;
        for ( const BankEntry &entry : bank.getEntries() ) {
            if ( StringCmpI( entry.Extension.c_str(), "cin" ) != 0 ) {
                continue;
            }
            summaries.push_back(
                DumpCinematic( bank, entry, stem, outDir, bJson ) );
            cins++;
        }
        if ( cins == 0 ) {
            printf( "[bank] %s: no .cin entries\n", stem.c_str() );
        }
    }

    // Index.
    std::ostringstream index;
    index << "# Cutscene dump index\n\n";
    index << "Generated by `otdu2_cine_tool dump " << source
          << ( bVariants ? " -variants" : "" ) << ( bJson ? " -json" : "" )
          << "` - the `.cin`/`.sce` authoring data of the scenarics cinematics "
             "banks, "
          << "decoded with the game's own parsers "
             "(docs/re/cutscenes_inventory.md).\n\n";
    index << "| bank | cinematic | dur s | shots | scenes | cameras | actors | "
             "msgs | fx |\n";
    index << "|---|---|---|---|---|---|---|---|---|\n";
    int okCount = 0;
    for ( const CinSummary &summary : summaries ) {
        if ( !summary.ok ) {
            continue;
        }
        okCount++;
        index << "| [" << summary.bankStem << "](./" << summary.bankStem
              << "/) | [" << summary.cinName << "](./" << summary.bankStem
              << "/" << summary.cinName << ".md) | " << F( summary.duration )
              << " | " << summary.shots << " | " << summary.scenes << " | "
              << summary.cameras << " | " << summary.actors << " | "
              << summary.messages << " | " << summary.fx << " |\n";
    }
    index << "\n"
          << banks.size() << " banks, " << okCount << " cinematics parsed.\n";
    std::ofstream stream( outDir / "index.md", std::ios::binary );
    stream << index.str();
    printf( "index: %s\n", ( outDir / "index.md" ).string().c_str() );
    return failedBanks > 0 ? 2 : 0;
}

static int ListAll( const std::string &source, bool bVariants ) {
    std::vector<fs::path> banks;
    CollectBanks( source, bVariants, banks );
    if ( banks.empty() ) {
        printf( "no .bnk found under %s\n", source.c_str() );
        return 1;
    }
    printf( "%-32s %-42s %9s %6s %6s\n", "bank", "cinematic", "dur s", "shots",
            "scenes" );
    for ( const fs::path &bankPath : banks ) {
        Bank bank;
        if ( !bank.load( bankPath.string().c_str() ) ) {
            printf( "[!] cannot load %s\n", bankPath.string().c_str() );
            continue;
        }
        for ( const BankEntry &entry : bank.getEntries() ) {
            if ( StringCmpI( entry.Extension.c_str(), "cin" ) != 0 ) {
                continue;
            }
            Cinematic cine;
            if ( !cine.parse( entry.pData, entry.Size ) ) {
                printf( "%-32s %-42s %9s %6s %6s\n",
                        bankPath.stem().string().c_str(), entry.Name.c_str(),
                        "PARSE FAIL", "-", "-" );
                continue;
            }
            printf( "%-32s %-42s %9.2f %6d %6d\n",
                    bankPath.stem().string().c_str(), entry.Name.c_str(),
                    cine.getDuration(),
                    static_cast<int>( cine.getShots().size() ),
                    static_cast<int>( cine.getSceneReferences().size() ) );
        }
    }
    return 0;
}

int main( int argc, const char **argv ) {
    CmdLine::Initialize( argc, argv );
    Logger::Initialize( nullptr, false, false );
    GSFile file;

    bool bJson = false;
    bool bVariants = false;
    std::string source;
    std::string output = "_cine_dump";
    for ( int i = 2; i < argc; i++ ) { // argv[1] is the command
        const std::string arg = argv[i];
        if ( arg == "-json" ) {
            bJson = true;
        } else if ( arg == "-variants" ) {
            bVariants = true;
        } else if ( arg == "-lang" || arg == "-avatar" ) {
            i++; // valued flags; manifest.cpp reads them through CmdLine
        } else if ( arg.size() > 0 && arg[0] != '-' ) {
            if ( source.empty() ) {
                source = arg;
            } else {
                output = arg;
            }
        }
    }

    const bool bManifest =
        argc >= 2 && StringCmpI( argv[1], "manifest" ) == 0;
    if ( bManifest && output == "_cine_dump" ) {
        output = "_cine_manifest"; // the manifest default output folder
    }

    if ( argc < 3 || source.empty() ||
         ( StringCmpI( argv[1], "dump" ) != 0 &&
           StringCmpI( argv[1], "list" ) != 0 && !bManifest ) ) {
        printf( "usage: %s list <game root | cinematics folder | file.bnk> "
                "[-variants]\n",
                argv[0] );
        printf( "       %s dump <game root | cinematics folder | file.bnk> "
                "[output folder] [-json] [-variants]\n",
                argv[0] );
        printf( "       %s manifest <game root | cinematics folder | file.bnk>"
                " [output folder] [-json] [-variants] [-lang us] "
                "[-avatar AVT_Male_Intro]\n",
                argv[0] );
        return 1;
    }
    if ( StringCmpI( argv[1], "list" ) == 0 ) {
        return ListAll( source, bVariants );
    }
    if ( bManifest ) {
        return ManifestAll( source, output, bJson, bVariants );
    }
    return DumpAll( source, output, bJson, bVariants );
}

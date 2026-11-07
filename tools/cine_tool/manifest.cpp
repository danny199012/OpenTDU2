// otdu2_cine_tool manifest: the dump command plus the resolution layers.
// Where dump prints the raw .cin/.sce key data, manifest resolves what the
// runtime resolves when it plays the scene:
//   - the .sce REFS (animations, FaceFX morphs) -> the bank's files
//   - the .sce objects -> the bank's entities.xmb entities (PNJ characters
//     and their outfits, from db_data.cpr - the DBPNJ/DBClothe rows)
//   - the Fx events -> the Fx__Root/.bfx graph -> per-language wav names
//     (the voicelines; the language banks <bank>__<lang>.bnk)
//   - the message protocol: the codes the port handles (flsh:/shad:/weat:/
//     wmod:/snd: and the actor codes) vs the retail-encrypted ones
//   - the story kind placements (the GMCine kind table) and the avatar
//     ruling (the AVT_* config -> sex/outfit/_W cinematic variant)
// It also writes a subtitle skeleton CSV per cinematic (one empty row per
// voice cue) - the authoring file the engine's -cinesubdump/captions reader
// consumes.
#include "cine_common.h"

#include "core/xmb.h"
#include "filesystem/gs_file.h"
#include "game/cinematic/cinematic_audio.h" // BankHashcodes
#include "system/cmd_line.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>

namespace fs = std::filesystem;

// Track values are raw dwords bit-cast to float by the parser; the integer
// fields (refs, flags) come back through their bits (the engine's keyRef
// memcpy, intro_cinematic.cpp:2405).
static uint32_t BitsOf( float value ) {
    uint32_t bits;
    memcpy( &bits, &value, sizeof( bits ) );
    return bits;
}

// ---------------------------------------------------------------------------
// .bfx (SND node bank) - the documented layout of cinematic_audio.h 33-79 /
// CinematicSounds::ParseBfx (private there, reproduced for the tool).

static constexpr uint32_t kSndClassHash = 0x8540076c; // edFxSound node +0x58
static constexpr size_t kSndNodeSize = 0xe0;          // tag " DNS" at +0xe0

template <typename T>
static T ReadAt( const uint8_t *pData, size_t offset ) {
    T value;
    memcpy( &value, pData + offset, sizeof( T ) );
    return value;
}

struct ManifestSndNode {
    std::string Name;
    uint64_t Rsc = 0;
    uint32_t LoopCount = 0;
    float Volume = 1.0f;
    float Pitch = 1.0f;
};

struct ManifestBfx {
    bool bLanguageSwitch = false;
    std::vector<ManifestSndNode> Nodes;
};

static bool ParseManifestBfx( const uint8_t *pData, size_t size,
                              ManifestBfx &outBfx ) {
    outBfx = ManifestBfx();
    if ( size < kSndNodeSize + 4 ) {
        return false;
    }
    outBfx.bLanguageSwitch =
        size >= 0xb8 && memcmp( pData + 0xb4, "LSGL", 4 ) == 0;
    for ( size_t tag = kSndNodeSize; tag + 4 <= size; tag += 4 ) {
        if ( memcmp( pData + tag, " DNS", 4 ) != 0 ) {
            continue;
        }
        const uint8_t *pNode = pData + tag - kSndNodeSize;
        if ( ReadAt<uint32_t>( pNode, 0x58 ) != kSndClassHash ) {
            continue;
        }
        ManifestSndNode node;
        const char *pName = reinterpret_cast<const char *>( pNode + 0x78 );
        node.Name.assign( pName, strnlen( pName, 32 ) );
        node.Rsc = ReadAt<uint64_t>( pNode, 0xa0 );
        node.LoopCount = ReadAt<uint32_t>( pNode, 0x5c );
        node.Volume = ReadAt<float>( pNode, 0xac );
        node.Pitch = ReadAt<float>( pNode, 0xb0 );
        outBfx.Nodes.push_back( std::move( node ) );
        tag += kSndNodeSize - 4;
    }
    return !outBfx.Nodes.empty();
}

// gpConfig language (db suffix) -> .bfx LSGL node name prefix
// (LanguagePrefix, cinematic_audio.cpp:29).
static std::string LanguagePrefixOf( const std::string &language ) {
    static constexpr const char *kPrefixes[][2] = {
        { "us", "US" }, { "fr", "FR" }, { "ge", "GE" }, { "it", "IT" },
        { "sp", "SP" }, { "ru", "RU" }, { "po", "PL" }, { "pl", "PL" },
        { "jp", "JP" },
    };
    for ( const auto &prefix : kPrefixes ) {
        if ( StringCmpI( language.c_str(), prefix[0] ) == 0 ) {
            return std::string( prefix[1] ) + "_";
        }
    }
    std::string upper = language;
    for ( char &c : upper ) {
        c = static_cast<char>( toupper( static_cast<unsigned char>( c ) ) );
    }
    return upper + "_";
}

// The language of an LSGL node name ("FR_Hello", "US_x_W") - "" when unknown.
static std::string NodeLanguage( const std::string &name ) {
    const size_t underscore = name.find( '_' );
    if ( underscore == 0 || underscore == std::string::npos ) {
        return "";
    }
    return name.substr( 0, underscore );
}

// ---------------------------------------------------------------------------
// the Fx__Root graph of a bank (CinematicSounds::load, cinematic_audio.cpp:97)

struct FxResolver {
    struct Source {
        uint64_t BfxRsc = 0;
        std::vector<uint64_t> Nodes; // Fx__Sound hashcodes
    };
    std::map<uint64_t, uint64_t> rootToSource; // Fx__Root -> Fx__SourceData
    std::map<uint64_t, Source> sources;
    std::map<uint64_t, uint64_t> soundFiles; // Fx__Sound -> wav rsc

    bool Load( const Bank &bank ) {
        const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
        XmbDocument document;
        if ( pEntities == nullptr ||
             !document.load( pEntities->pData, pEntities->Size ) ) {
            return false;
        }
        XmbValue root = document.getRoot();
        XmbValue fx = root["ExportType_Fx"];
        for ( uint32_t i = 0; i < fx.getArraySize( "ArrayOf_Fx__Root" ); i++ ) {
            XmbValue entry = fx.getArrayElement( "ArrayOf_Fx__Root", i );
            rootToSource[entry["__Hc"].asUInt64()] =
                entry["SourceData"].asUInt64();
        }
        XmbValue sourceData = root["ExportType_FxSourceData"];
        for ( uint32_t i = 0;
              i < sourceData.getArraySize( "ArrayOf_Fx__SourceData" ); i++ ) {
            XmbValue entry =
                sourceData.getArrayElement( "ArrayOf_Fx__SourceData", i );
            Source &source = sources[entry["__Hc"].asUInt64()];
            if ( entry.getArraySize( "Fx" ) > 0 ) {
                source.BfxRsc =
                    entry.getArrayElement( "Fx", 0 )["rsc"].asUInt64();
            }
            for ( uint32_t j = 0; j < entry.getArraySize( "SoundList" ); j++ ) {
                source.Nodes.push_back(
                    entry.getArrayElement( "SoundList", j )["SoundNode"]
                        .asUInt64() );
            }
        }
        XmbValue soundExport = root["ExportType_FxSound"];
        if ( const XmbType *pType =
                 soundExport.isValid() ? soundExport.getType() : nullptr ) {
            for ( const XmbType::Field &field : pType->Fields ) {
                if ( !field.bArray ||
                     field.Name.rfind( "ArrayOf_Fx__Sound", 0 ) != 0 ) {
                    continue;
                }
                for ( uint32_t i = 0;
                      i < soundExport.getArraySize( field.Name.c_str() );
                      i++ ) {
                    XmbValue entry = soundExport.getArrayElement(
                        field.Name.c_str(), i );
                    for ( const char *pFile : { "SoundFile",
                                                "LocalizedSoundFile" } ) {
                        if ( entry.getArraySize( pFile ) > 0 ) {
                            soundFiles[entry["__Hc"].asUInt64()] =
                                entry.getArrayElement( pFile, 0 )["rsc"]
                                    .asUInt64();
                            break;
                        }
                    }
                }
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// the bank's entity index (the __Hc -> entity rows of entities.xmb)

struct EntityInfo {
    std::string Class;      // "PNJ", "Cinematic", "PosCam", ...
    std::string Name;       // __Name
    Vector3f Pos;           // orient_pos_scale.pos
    std::string ConfigName; // PNJ
};

static std::string EntityClassName( const std::string &arrayName ) {
    // "ArrayOf_Entity__PNJ" -> "PNJ"
    constexpr const char *kPrefix = "ArrayOf_Entity__";
    const size_t prefix = strlen( kPrefix );
    if ( arrayName.size() > prefix &&
         arrayName.compare( 0, prefix, kPrefix ) == 0 ) {
        return arrayName.substr( prefix );
    }
    return arrayName;
}

static std::map<uint64_t, EntityInfo> LoadEntityIndex( const Bank &bank ) {
    std::map<uint64_t, EntityInfo> out;
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument document;
    if ( pEntities == nullptr ||
         !document.load( pEntities->pData, pEntities->Size ) ) {
        return out;
    }
    XmbValue entityExport = document.getRoot()["ExportType_Entity"];
    const XmbType *pType = entityExport.isValid() ? entityExport.getType()
                                                  : nullptr;
    if ( pType == nullptr ) {
        return out;
    }
    for ( const XmbType::Field &field : pType->Fields ) {
        if ( !field.bArray ||
             field.Name.rfind( "ArrayOf_Entity", 0 ) != 0 ) {
            continue;
        }
        EntityInfo info;
        info.Class = EntityClassName( field.Name );
        const uint32_t count =
            entityExport.getArraySize( field.Name.c_str() );
        for ( uint32_t i = 0; i < count; i++ ) {
            XmbValue entry =
                entityExport.getArrayElement( field.Name.c_str(), i );
            if ( !entry.isValid() ) {
                continue;
            }
            info.Name = entry["__Name"].asString();
            info.ConfigName.clear();
            XmbValue position = entry["orient_pos_scale"]["pos"];
            info.Pos = Vector3f( position["X"].asFloat(),
                                 position["Y"].asFloat(),
                                 position["Z"].asFloat() );
            if ( info.Class == "PNJ" ) {
                info.ConfigName = entry["ConfigName"].asString();
            }
            out[entry["__Hc"].asUInt64()] = info;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// the Entity__Cinematic definitions (loadDefinitions,
// intro_cinematic.cpp:251): name, props, the .cin they point at, the m/w
// style anim substitution.

struct CineDefinition {
    std::string Name;
    uint32_t CinProps = 0;
    std::string ShadowConfig;
    std::string CinFile; // resolved .cin entry name (rscSubType 16)
    // StyleAnims: base xan rsc -> styled xan rsc, per style (0 = m, 1 = w),
    // resolved to file names for the report.
    std::map<uint64_t, uint64_t> StyleAnims[2];
};

static std::vector<CineDefinition>
LoadCineDefinitions( const Bank &bank, const BankHashcodes &hashcodes ) {
    std::vector<CineDefinition> out;
    const BankEntry *pEntities = bank.findEntry( "entities.xmb" );
    XmbDocument document;
    if ( pEntities == nullptr ||
         !document.load( pEntities->pData, pEntities->Size ) ) {
        return out;
    }
    XmbValue entityExport = document.getRoot()["ExportType_Entity"];
    const uint32_t count =
        entityExport.getArraySize( "ArrayOf_Entity__Cinematic" );
    for ( uint32_t i = 0; i < count; i++ ) {
        XmbValue entry =
            entityExport.getArrayElement( "ArrayOf_Entity__Cinematic", i );
        CineDefinition def;
        def.Name = entry["__Name"].asString();
        def.CinProps =
            entry.getArraySize( "CinProps" ) > 0
                ? static_cast<uint32_t>(
                      entry.getArrayElement( "CinProps", 0 ).asInt() )
                : 0u;
        def.ShadowConfig = entry["DefaultShadowConfig"].asString();
        for ( uint32_t j = 0; j < entry.getArraySize( "CinFile" ); j++ ) {
            XmbValue file = entry.getArrayElement( "CinFile", j );
            if ( file["rscSubType"].asInt() != 16 ) {
                continue;
            }
            const uint64_t rsc = file["rsc"].asUInt64();
            if ( const BankEntry *pCin = hashcodes.find( rsc, -1, 16 ) ) {
                def.CinFile = pCin->Name;
            } else if ( const BankEntry *pAny = hashcodes.find( rsc ) ) {
                def.CinFile = pAny->Name;
            }
            break;
        }
        const uint32_t numAnims = entry.getArraySize( "additionalsAnims" );
        for ( uint32_t s = 0;
              s < entry.getArraySize( "additionalsStyleAnims" ); s++ ) {
            XmbValue style =
                entry.getArrayElement( "additionalsStyleAnims", s );
            const std::string styleName = style["style"].asString();
            const int32_t styleIndex =
                styleName == "m" ? 0 : styleName == "w" ? 1 : -1;
            if ( styleIndex < 0 ) {
                continue;
            }
            for ( uint32_t a = 0;
                  a < numAnims && a < style.getArraySize( "anims" ); a++ ) {
                XmbValue base = entry.getArrayElement( "additionalsAnims", a );
                XmbValue styled = style.getArrayElement( "anims", a );
                if ( base.getArraySize( "xan" ) == 0 ||
                     styled.getArraySize( "xan" ) == 0 ) {
                    continue;
                }
                const uint64_t from =
                    base.getArrayElement( "xan", 0 )["rsc"].asUInt64();
                const uint64_t to =
                    styled.getArrayElement( "xan", 0 )["rsc"].asUInt64();
                if ( from != 0 && to != 0 ) {
                    def.StyleAnims[styleIndex][from] = to;
                }
            }
        }
        out.push_back( std::move( def ) );
    }
    return out;
}

// ---------------------------------------------------------------------------
// the database (db_data.cpr): DBPNJ rows for the characters and the avatar
// ruling. LoadXmbFromFile handles the .cpr decryption (GSDatabase
// initializes the same way).

struct Database {
    XmbDocument doc;
    bool ok = false;

    bool Load( const fs::path &path ) {
        ok = LoadXmbFromFile( path.string().c_str(), doc );
        return ok;
    }

    XmbValue FindRow( const char *pTable, const char *pField,
                      const std::function<bool( XmbValue )> &match ) const {
        if ( !ok ) {
            return XmbValue();
        }
        XmbValue root = doc.getRoot();
        for ( uint32_t i = 0; i < root.getArraySize( pTable ); i++ ) {
            XmbValue row = root.getArrayElement( pTable, i );
            if ( match( row[pField] ) ) {
                return row;
            }
        }
        return XmbValue();
    }

    XmbValue FindPnj( const std::string &configId ) const {
        return FindRow( "DBPNJ", "_configid", [&configId]( XmbValue v ) {
            return StringCmpI( v.asString().c_str(), configId.c_str() ) == 0;
        } );
    }

    XmbValue FindClothe( uint64_t hashcode ) const {
        return FindRow( "DBClothe", "_hashcode",
                        [hashcode]( XmbValue v ) {
                            return v.asUInt64() == hashcode;
                        } );
    }
};

// GetStoryAvatarConfig (player_profile.cpp:82): the story strips the intro
// suffix; the .W cinematic variant and the anim style follow the sex.
static std::string NormalizeAvatarConfig( const std::string &config ) {
    std::string result = config;
    const std::string suffix = "_Intro";
    if ( result.size() > suffix.size() &&
         result.compare( result.size() - suffix.size(), suffix.size(),
                         suffix ) == 0 ) {
        result.resize( result.size() - suffix.size() );
    }
    return result.empty() ? std::string( "AVT_Male" ) : result;
}

// kBodies (eden_avatar.cpp:19) - BodyID -> body mesh / head names.
struct BodyEntry {
    const char *pBody;
    const char *pHead;
};
static const BodyEntry kBodies[6] = {
    { "man_body", "man_head_occ" },     { "woman_body", "woman_head_occ" },
    { "man_body", "man_head_asia" },    { "woman_body", "woman_head_asia" },
    { "man_body", "man_head_afrik" },   { "woman_body", "woman_head_afrik" },
};

static const char *kSlots[10] = { "hair",   "top",      "bottom", "dress",
                                  "shoes",  "glasses",  "gloves", "helmet",
                                  "accessories", "beard" };

// Clothes directory from the file code (eden_avatar.cpp:59).
static const char *ClothesDirectory( const std::string &file ) {
    const std::string code =
        file.size() >= 4 ? ToUpper( file.substr( 2, 2 ) ) : "";
    static const std::pair<const char *, const char *> kDirectories[] = {
        { "HR", "hair" },    { "PN", "pnj" },        { "SP", "sportswear" },
        { "SW", "streetwear" }, { "FH", "fashion" }, { "HL", "highlife" },
        { "CA", "casino" },  { "ED", "eden" },
    };
    for ( const auto &entry : kDirectories ) {
        if ( code == entry.first ) {
            return entry.second;
        }
    }
    return nullptr;
}

struct OutfitRow {
    std::string Slot;
    std::string File;    // the DBClothe _file (post sex-swap)
    std::string RawFile; // as authored
    int32_t Color = 0;
    bool bMissing = false; // clothe hash not in DBClothe
};

// The outfit of a DBPNJ row (EdenAvatar::load's clothes loop,
// eden_avatar.cpp:281-325): the 10 slots, the M_/<->W_/ file swap for body
// garments, the color index.
static std::vector<OutfitRow> ReadOutfit( const Database &db,
                                          XmbValue pnj ) {
    std::vector<OutfitRow> rows;
    for ( const char *pSlot : kSlots ) {
        OutfitRow row;
        row.Slot = pSlot;
        const uint64_t clotheHash =
            pnj[( std::string( "_clothe_" ) + pSlot ).c_str()].asUInt64();
        XmbValue clothe = db.FindClothe( clotheHash );
        if ( !clothe.isValid() || clotheHash == 0 ) {
            row.bMissing = true;
            rows.push_back( std::move( row ) );
            continue;
        }
        row.RawFile = clothe["_file"].asString();
        row.File = row.RawFile;
        if ( row.File.empty() || row.File.find( "NO_" ) != std::string::npos ) {
            row.RawFile += " (none)";
            row.File.clear();
        } else {
            const bool bWoman = pnj["_sex"].asInt() == 1;
            const bool bHead = strcmp( pSlot, "hair" ) == 0 ||
                               strcmp( pSlot, "beard" ) == 0 ||
                               strcmp( pSlot, "glasses" ) == 0 ||
                               strcmp( pSlot, "helmet" ) == 0;
            if ( !bHead ) {
                std::string swapped = row.File;
                if ( !bWoman && swapped.compare( 0, 2, "W_" ) == 0 ) {
                    swapped[0] = 'M';
                } else if ( bWoman && swapped.compare( 0, 2, "M_" ) == 0 ) {
                    swapped[0] = 'W';
                }
                row.File = swapped;
            }
        }
        row.Color =
            pnj[( std::string( pSlot ) + "_color" ).c_str()].asInt();
        rows.push_back( std::move( row ) );
    }
    return rows;
}

// The BodyID of a config's .avt (avatar/config/pnj.bnk, key 0xcd26), or -1.
static int ReadBodyId( const std::string &gameRoot,
                       const std::string &avtName ) {
    Bank configBank;
    const fs::path path = fs::path( gameRoot ) / "Euro" / "Bnk" / "avatar" /
                          "config" / "pnj.bnk";
    std::error_code ec;
    if ( !fs::exists( path, ec ) ||
         !configBank.load( path.string().c_str() ) ) {
        return -1;
    }
    const BankEntry *pAvt = configBank.findEntry( avtName.c_str(), "avt" );
    if ( pAvt == nullptr || pAvt->Size <= 2 ) {
        return -1;
    }
    // FindAvtValue (eden_avatar.cpp:75): big endian tree { u16 size; u16 key;
    // value }.
    std::function<bool( const uint8_t *, size_t, size_t, uint16_t, uint16_t & )>
        find = [&]( const uint8_t *pData, size_t begin, size_t end,
                    uint16_t wantedKey, uint16_t &outValue ) -> bool {
        size_t offset = begin;
        while ( offset + 4 <= end ) {
            const uint16_t entrySize = static_cast<uint16_t>(
                ( pData[offset] << 8 ) | pData[offset + 1] );
            const uint16_t key = static_cast<uint16_t>(
                ( pData[offset + 2] << 8 ) | pData[offset + 3] );
            if ( entrySize < 2 ) {
                return false;
            }
            const size_t valueSize = entrySize - 2u;
            if ( offset + 4 + valueSize > end ) {
                return false;
            }
            if ( key == wantedKey && valueSize == 2 ) {
                outValue = static_cast<uint16_t>( ( pData[offset + 4] << 8 ) |
                                                  pData[offset + 5] );
                return true;
            }
            if ( valueSize > 16 &&
                 find( pData, offset + 4, offset + 4 + valueSize, wantedKey,
                       outValue ) ) {
                return true;
            }
            offset += 4 + valueSize;
        }
        return false;
    };
    uint16_t value = 0;
    if ( find( pAvt->pData, 2, pAvt->Size, 0xcd26, value ) && value < 6 ) {
        return value;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// the story kind table (the copy of GMCine::kKinds, gm_cine.cpp:18 - keep in
// sync; the Kind fields are the retail FUN_009b57b0 decode).

struct StoryKind {
    int32_t Id;
    const char *pBank;
    const char *pSpot;
    const char *pCinematic;
    const char *pCinematicW;
};
static const StoryKind kStoryKinds[] = {
    { 1, "meeting_tess", "E_HOTELA_1266I", "C_MEETING_TESS",
      "C_MEETING_TESS_W" },
    { 2, "solar_crown_ext", "ECH_IA_1243I", "C_SOLAR_CROWN_E", "" },
    { 3, "solar_crown_int", "ECH_IA_1243I", "C_SOLAR_CROWN_I", "" },
    { 0x2b, "isl_igloo", "EPH_L0A_1276", "C_IGLOO", "" },
};

// ---------------------------------------------------------------------------
// the message protocol (processMessages intro_cinematic.cpp:528 for the cin
// level; the actor codes per intro_cinematic.h:283 / gm_cine.cpp:266).

struct MessageInfo {
    bool bKnown = false;
    std::string Action; // what the port does with it
};

static MessageInfo ClassifyCinMessage( const std::string &text ) {
    MessageInfo info;
    auto prefix = [&text]( const char *p ) {
        return text.rfind( p, 0 ) == 0;
    };
    if ( prefix( "flsh:" ) ) {
        info.bKnown = true;
        info.Action = "UI: INTRO.vmf gotoLabel '" + text.substr( 5 ) + "'";
    } else if ( prefix( "shad:" ) ) {
        info.bKnown = true;
        info.Action = "shadow config '" + text.substr( 5 ) + "'";
    } else if ( prefix( "weat:" ) ) {
        info.bKnown = true;
        info.Action = "weather preset '" + text.substr( 5 ) + "'";
    } else if ( prefix( "wmod:" ) ) {
        info.bKnown = true;
        info.Action = "weather modifier '" + text.substr( 5 ) + "'";
    } else if ( prefix( "snd:" ) ) {
        info.bKnown = true;
        info.Action = "Entity__Fx emitter '" + text.substr( 4 ) + "'";
    } else if ( text == "zcvn" || text == "zcne" || text == "zcde" ||
                text == "phot" ) {
        info.Action = "[ENC] retail-encrypted handler, ignored by the port";
    } else {
        info.Action = "[?] no handler known";
    }
    return info;
}

static MessageInfo ClassifyActorMessage( const std::string &text ) {
    MessageInfo info;
    if ( text.rfind( "opd", 0 ) == 0 || text.rfind( "cld", 0 ) == 0 ) {
        info.bKnown = true;
        info.Action = "car door (opd*/cld*: open/close L/R/both)";
    } else if ( text == "avm4" ) {
        info.bKnown = true;
        info.Action = "seat the player avatar in the car";
    } else if ( text == "avm1" ) {
        info.bKnown = true;
        info.Action = "unseat the player avatar";
    } else if ( text == "lkon" || text == "lkof" ) {
        info.bKnown = true;
        info.Action = "look-at target on/off";
    } else if ( text == "zcvn" || text == "zcne" || text == "zcde" ||
                text == "phot" ) {
        info.Action = "[ENC] retail-encrypted handler, ignored by the port";
    } else {
        info.Action = "[?] no handler known";
    }
    return info;
}

// ---------------------------------------------------------------------------
// audio resolution of one Fx event

struct AudioResolution {
    bool bFound = false;
    bool bLanguageSwitch = false;
    std::string BfxFile;
    struct NodeLine {
        std::string Name;
        std::string Language;  // "US" / "FR" / ""
        bool bAltW = false;    // "_W" alternate take
        std::string Wav;       // resolved file ("" = not found)
        uint32_t WavSize = 0;
        float Volume = 1.0f;
        bool bLoop = false;
    };
    std::vector<NodeLine> Nodes;
};

static AudioResolution
ResolveFxEvent( uint64_t rootHashcode, const FxResolver &fx,
                const BankHashcodes &hashcodes,
                const BankHashcodes *pLanguageHashcodes ) {
    AudioResolution out;
    const auto sourceIt = fx.rootToSource.find( rootHashcode );
    if ( sourceIt == fx.rootToSource.end() ) {
        return out;
    }
    const auto source = fx.sources.find( sourceIt->second );
    if ( source == fx.sources.end() || source->second.BfxRsc == 0 ) {
        return out;
    }
    const BankEntry *pBfx = hashcodes.find( source->second.BfxRsc, 0, 31 );
    if ( pBfx == nullptr ) {
        pBfx = hashcodes.find( source->second.BfxRsc );
    }
    if ( pBfx == nullptr ) {
        return out;
    }
    out.bFound = true;
    out.BfxFile = pBfx->Name;
    ManifestBfx bfx;
    ParseManifestBfx( pBfx->pData, pBfx->Size, bfx );
    out.bLanguageSwitch = bfx.bLanguageSwitch;
    auto findWav = [&]( uint64_t rsc ) -> const BankEntry * {
        if ( const BankEntry *pEntry = hashcodes.find( rsc, 10, 6 ) ) {
            return pEntry;
        }
        if ( pLanguageHashcodes != nullptr ) {
            if ( const BankEntry *pEntry = pLanguageHashcodes->find( rsc ) ) {
                return pEntry->Extension == "wav" ? pEntry : nullptr;
            }
        }
        if ( const BankEntry *pEntry = hashcodes.find( rsc ) ) {
            return pEntry->Extension == "wav" ? pEntry : nullptr;
        }
        return nullptr;
    };
    for ( const ManifestSndNode &node : bfx.Nodes ) {
        AudioResolution::NodeLine line;
        line.Name = node.Name;
        line.Language = NodeLanguage( node.Name );
        line.bAltW = node.Name.size() >= 2 &&
                     node.Name.compare( node.Name.size() - 2, 2, "_W" ) == 0;
        line.Volume = node.Volume;
        line.bLoop = node.LoopCount == 0xffffffffu;
        if ( const BankEntry *pWav = findWav( node.Rsc ) ) {
            line.Wav = pWav->Filename;
            line.WavSize = pWav->Size;
        }
        out.Nodes.push_back( std::move( line ) );
    }
    // Sort: the -lang nodes first, then the rest (deterministic reading).
    const std::string wanted = LanguagePrefixOf(
        CmdLine::GetArgValue( "-lang" ) ? CmdLine::GetArgValue( "-lang" )
                                        : "us" );
    std::stable_sort(
        out.Nodes.begin(), out.Nodes.end(),
        [&wanted]( const AudioResolution::NodeLine &a,
                   const AudioResolution::NodeLine &b ) {
            const bool aWanted = a.Name.rfind( wanted, 0 ) == 0;
            const bool bWanted = b.Name.rfind( wanted, 0 ) == 0;
            if ( aWanted != bWanted ) {
                return aWanted;
            }
            return a.Name < b.Name;
        } );
    return out;
}

// ---------------------------------------------------------------------------
// per cinematic manifest

struct ManifestContext {
    Database db;
    bool bDbOk = false;
    std::string gameRoot; // "" when the database was not located
    std::string language = "us";
    std::vector<std::string> avatarConfigs; // the ruling inputs
    mutable int jsonCinematics = 0;
    mutable int mdCinematics = 0;
    mutable int skeletons = 0;
};

// One character line set for MD + JSON: the PNJ config's outfit.
static void WriteCharacterMd( std::ostringstream &md, const Database &db,
                              const std::string &configId,
                              const std::string &gameRoot,
                              const char *pWhy ) {
    XmbValue pnj = db.FindPnj( configId );
    md << "- Character " << pWhy << ": entity PNJ ConfigName `" << configId
       << "`";
    if ( !pnj.isValid() ) {
        md << " - DBPNJ row not found\n";
        return;
    }
    const bool bWoman = pnj["_sex"].asInt() == 1;
    const std::string avtName = pnj["_fileconfigid"].asString();
    const int bodyId = ReadBodyId( gameRoot, avtName );
    md << " -> " << ( bWoman ? "woman" : "man" ) << ", .avt `" << avtName
       << "`";
    if ( bodyId >= 0 && bodyId < 6 ) {
        md << ", body " << bodyId << " (" << kBodies[bodyId].pBody << " / "
           << kBodies[bodyId].pHead << ")";
    } else {
        md << ", body default (the .avt was not found)";
    }
    md << "\n";
    const std::vector<OutfitRow> outfit = ReadOutfit( db, pnj );
    md << "  | slot | file | color |\n  |---|---|---|\n";
    for ( const OutfitRow &row : outfit ) {
        if ( row.bMissing ) {
            md << "  | " << row.Slot << " | (no DBClothe row) | - |\n";
        } else if ( row.File.empty() ) {
            md << "  | " << row.Slot << " | " << row.RawFile << " | - |\n";
        } else {
            md << "  | " << row.Slot << " | `" << row.File << "`"
               << ( row.File != row.RawFile
                        ? " (authored " + row.RawFile + ")"
                        : "" )
               << " | " << row.Color << " |\n";
        }
    }
}

static void WriteCharacterJson( JsonOut &json, const Database &db,
                                const std::string &configId,
                                const std::string &gameRoot ) {
    json.beginObject();
    json.str( "config", configId );
    XmbValue pnj = db.FindPnj( configId );
    if ( !pnj.isValid() ) {
        json.boolean( "found", false );
        json.endObject();
        return;
    }
    json.boolean( "found", true );
    const bool bWoman = pnj["_sex"].asInt() == 1;
    json.str( "sex", bWoman ? "woman" : "man" );
    json.str( "avt", pnj["_fileconfigid"].asString() );
    const int bodyId =
        ReadBodyId( gameRoot, pnj["_fileconfigid"].asString() );
    if ( bodyId >= 0 && bodyId < 6 ) {
        json.num( "body_id", bodyId );
        json.str( "body", kBodies[bodyId].pBody );
        json.str( "head", kBodies[bodyId].pHead );
    }
    json.key( "outfit" );
    json.beginArray();
    for ( const OutfitRow &row : ReadOutfit( db, pnj ) ) {
        json.beginObject();
        json.str( "slot", row.Slot );
        if ( row.bMissing ) {
            json.boolean( "missing", true );
        } else {
            json.str( "file", row.File );
            if ( row.File != row.RawFile ) {
                json.str( "authored_file", row.RawFile );
            }
            json.num( "color", row.Color );
        }
        json.endObject();
    }
    json.endArray();
    json.endObject();
}

static CinSummary ManifestCinematic( const Bank &bank,
                                     const std::string &bankStem,
                                     const BankHashcodes &hashcodes,
                                     const FxResolver &fx,
                                     const std::vector<CineDefinition> &defs,
                                     const std::map<uint64_t, EntityInfo> &entities,
                                     const ManifestContext &ctx,
                                     const BankEntry &entry,
                                     const fs::path &outDir,
                                     const Bank *pLanguageBank,
                                     const BankHashcodes *pLanguageHashcodes,
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

    // The definition that plays this .cin (the runtime goes definition ->
    // CinFile rsc -> this entry).
    const CineDefinition *pDef = nullptr;
    for ( const CineDefinition &def : defs ) {
        if ( def.CinFile == entry.Name ) {
            pDef = &def;
            break;
        }
    }

    // The story kind (bank stem + definition name).
    const StoryKind *pKind = nullptr;
    const char *pKindVariant = nullptr;
    for ( const StoryKind &kind : kStoryKinds ) {
        if ( strcmp( kind.pBank, bankStem.c_str() ) != 0 || pDef == nullptr ) {
            continue;
        }
        if ( strcmp( kind.pCinematic, pDef->Name.c_str() ) == 0 ) {
            pKind = &kind;
            pKindVariant = "default";
        } else if ( kind.pCinematicW[0] != '\0' &&
                    strcmp( kind.pCinematicW, pDef->Name.c_str() ) == 0 ) {
            pKind = &kind;
            pKindVariant = "W (female avatar)";
        }
    }

    // ---- Markdown ---------------------------------------------------------
    std::ostringstream md;
    md << "# " << entry.Name << " (manifest)\n\n";
    md << "Timeline `" << entry.Filename << "` of bank `" << bankStem
       << ".bnk` (" << entry.Size << " bytes). Duration **"
       << F( cine.getDuration() ) << " s**, " << cine.getShots().size()
       << " shots, " << scenes.size() << " scene(s).\n";
    if ( pDef != nullptr ) {
        md << "- Definition `" << pDef->Name << "` (Entity__Cinematic): "
           << "CinProps 0x" << std::hex << pDef->CinProps << std::dec
           << " (" << ( ( pDef->CinProps & 0x2 ) ? "loop " : "" )
           << ( ( pDef->CinProps & 0x10 ) ? "no-camera " : "" )
           << "), shadow config '"
           << ( pDef->ShadowConfig.empty() ? "-" : pDef->ShadowConfig )
           << "'\n";
    } else {
        md << "- No Entity__Cinematic definition references this .cin (not "
              "played by definition name)\n";
    }
    if ( pKind != nullptr ) {
        md << "- **Story kind " << pKind->Id << "** (" << pKindVariant
           << "): spot `" << pKind->pSpot << "`\n";
    }

    md << "\n## Message protocol\n\n";
    md << "Cin-level messages (the .cin message tracks):\n\n";
    md << "| t | dur | text | the port does |\n|---|---|---|---|\n";
    for ( const Cinematic::Event &event : cine.getMessages() ) {
        std::string text = "#?";
        if ( event.Reference >= 0 &&
             event.Reference < static_cast<int32_t>( refs.size() ) ) {
            text = refs[event.Reference].Text;
        }
        const MessageInfo info = ClassifyCinMessage( text );
        md << "| " << F( event.Time ) << " | " << F( event.Duration ) << " | `"
           << text << "` | " << info.Action << " |\n";
    }

    md << "\n## Voice / audio cues\n\n";
    md << "| t | dur | fx root | space | world position | resolution "
          "|\n|---|---|---|---|---|---|\n";
    std::vector<std::pair<float, float>> voiceCues; // the skeleton rows
    for ( const Cinematic::Event &event : cine.getFxEvents() ) {
        uint64_t rootHash = 0;
        if ( event.Reference >= 0 &&
             event.Reference < static_cast<int32_t>( refs.size() ) ) {
            rootHash = refs[event.Reference].Hashcode;
        }
        const AudioResolution audio =
            ResolveFxEvent( rootHash, fx, hashcodes, pLanguageHashcodes );
        const Vector3f pos( event.World.m[3][0], event.World.m[3][1],
                            event.World.m[3][2] );
        const bool b2D = ( event.Flags & 0x10 ) != 0;
        std::string resolution;
        if ( !audio.bFound ) {
            resolution = "(.bfx not found)";
        } else {
            resolution = audio.BfxFile +
                         ( audio.bLanguageSwitch ? " [LSGL voices]" : "" );
            for ( const AudioResolution::NodeLine &node : audio.Nodes ) {
                resolution += "; `" + node.Name + "`";
                if ( !node.Wav.empty() ) {
                    resolution += " -> " + node.Wav + " (" +
                                  std::to_string( node.WavSize ) + " B)";
                } else {
                    resolution += " -> (wav not in this bank)";
                }
                if ( node.bLoop ) {
                    resolution += " [loop]";
                }
            }
            const bool bVoice = audio.bLanguageSwitch;
            if ( bVoice ) {
                voiceCues.push_back( { event.Time,
                                       event.Time + event.Duration } );
            }
        }
        md << "| " << F( event.Time ) << " | " << F( event.Duration ) << " | "
           << Hash64( rootHash ) << " | " << ( b2D ? "2D" : "positional" )
           << " | " << Vec3( pos ) << " | " << resolution << " |\n";
    }

    for ( size_t sceneIndex = 0; sceneIndex < scenes.size(); sceneIndex++ ) {
        const ParsedScene &parsed = scenes[sceneIndex];
        md << "\n## Scene " << sceneIndex << ": `" << parsed.displayName
           << "`\n\n";
        if ( !parsed.ok ) {
            md << "Not found in this bank (or parse failed).\n";
            continue;
        }
        const CinematicScene &scene = parsed.scene;
        const Matrix4x4f &origin = scene.getOrigin();
        const Vector3f originPos = origin.getTranslation();
        md << "- Duration " << F( scene.getDuration() ) << " s, "
           << scene.getObjects().size() << " objects\n";
        md << "- Origin: position " << Vec3( originPos ) << ", heading "
           << F( OriginHeadingDeg( origin ) ) << " deg\n";

        // Scene REFS resolved (animations / morphs).
        const std::vector<CinematicScene::Reference> &sceneRefs =
            scene.getReferences();
        if ( !sceneRefs.empty() ) {
            md << "- REFS (" << sceneRefs.size() << "):\n";
            for ( const CinematicScene::Reference &ref : sceneRefs ) {
                md << "  - " << Hash64( ref.Hashcode ) << " `";
                if ( !ref.Name.empty() ) {
                    md << ref.Name << "` -> ";
                }
                if ( const BankEntry *pAnim = hashcodes.find(
                         ref.Hashcode, 0, 8 ) ) {
                    md << "`" << pAnim->Filename << "` (" << pAnim->Size
                       << " B)";
                } else if ( const BankEntry *pAny =
                                hashcodes.find( ref.Hashcode ) ) {
                    md << "`" << pAny->Filename << "` (" << pAny->Size
                       << " B)";
                } else {
                    md << "(not in this bank";
                    if ( ref.Type == 0x0306 ) {
                        md << "; MORPH lives in avatar_face_fx.bnk";
                    }
                    md << ")";
                }
                md << "\n";
            }
        }

        md << "\n### Objects\n\n";
        md << "| object | class | entity (resolved) | tracks | notes "
              "|\n|---|---|---|---|---|\n";
        std::vector<std::pair<std::string, std::string>> sceneCharacters;
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            std::string entityCell = Hash64( object.EntityHashcode );
            std::string notes;
            const auto it = entities.find( object.EntityHashcode );
            if ( it != entities.end() ) {
                const EntityInfo &info = it->second;
                entityCell = info.Class + " `" + info.Name + "` at " +
                             Vec3( info.Pos );
                if ( !info.ConfigName.empty() ) {
                    notes = "PNJ config `" + info.ConfigName + "`";
                    sceneCharacters.push_back( { info.Name,
                                                 info.ConfigName } );
                }
            } else if ( object.EntityHashcode == 0x6666666666666666ULL ) {
                entityCell = "(no entity - driven purely by tracks)";
            }
            std::string tracks;
            for ( const CinematicScene::Track &track : object.Tracks ) {
                if ( !tracks.empty() ) {
                    tracks += ", ";
                }
                tracks += TrackKindName( track.Id ) + " " +
                          std::to_string( track.Times.size() );
            }
            md << "| " << object.Name << " | "
               << ( object.isCamera() ? "camera" : "actor" ) << " | "
               << entityCell << " | " << tracks << " | " << notes << " |\n";
        }

        // The animation keys resolved to files.
        md << "\n### Animation keys\n\n";
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            const CinematicScene::Track *pAnim = object.findTrack(
                CinematicNameHash( "Animation" ) );
            if ( pAnim == nullptr || pAnim->Times.empty() ) {
                continue;
            }
            md << "**" << object.Name << "**:\n\n";
            md << "| t | ref | animation | clip length | crossfade "
                  "|\n|---|---|---|---|---|\n";
            for ( size_t k = 0; k < pAnim->Times.size(); k++ ) {
                const uint32_t refIndex =
                    BitsOf( pAnim->Values[k * pAnim->NumComponents + 0] );
                const float length =
                    pAnim->NumComponents > 2
                        ? pAnim->Values[k * pAnim->NumComponents + 2]
                        : 0.0f;
                const float fade =
                    pAnim->NumComponents > 7
                        ? pAnim->Values[k * pAnim->NumComponents + 7]
                        : 0.0f;
                std::string anim = "#?";
                if ( refIndex < sceneRefs.size() ) {
                    anim = "`" + sceneRefs[refIndex].Name + "`";
                    if ( const BankEntry *pFile = hashcodes.find(
                             sceneRefs[refIndex].Hashcode, 0, 8 ) ) {
                        anim += " -> `" + pFile->Filename + "`";
                    }
                }
                md << "| " << F( pAnim->Times[k] ) << " | " << refIndex
                   << " | " << anim << " | " << F( length ) << " | "
                   << F( fade ) << " |\n";
            }
        }

        // The actor messages of this scene.
        bool bAnyActorMessage = false;
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            const CinematicScene::Track *pMessage =
                object.findTrack( CinematicScene::kTrackMessage );
            if ( pMessage == nullptr || pMessage->Times.empty() ) {
                continue;
            }
            if ( !bAnyActorMessage ) {
                md << "\n### Actor messages\n\n";
                bAnyActorMessage = true;
            }
            for ( size_t k = 0; k < pMessage->Times.size(); k++ ) {
                const uint32_t refIndex =
                    BitsOf( pMessage->Values[k * pMessage->NumComponents + 0] );
                const float duration =
                    pMessage->NumComponents > 1
                        ? pMessage->Values[k * pMessage->NumComponents + 1]
                        : 0.0f;
                std::string text = "#?";
                if ( refIndex < sceneRefs.size() ) {
                    text = sceneRefs[refIndex].Name;
                }
                const MessageInfo info = ClassifyActorMessage( text );
                md << "- t=" << F( pMessage->Times[k] ) << " dur="
                   << F( duration ) << " **" << object.Name << "**: `" << text
                   << "` - " << info.Action << "\n";
            }
        }

        // The scene Fx tracks (voices fired from an object).
        for ( const CinematicScene::Object &object : scene.getObjects() ) {
            const CinematicScene::Track *pFx =
                object.findTrack( CinematicScene::kTrackFx );
            if ( pFx == nullptr || pFx->Times.empty() ) {
                continue;
            }
            md << "\n### Scene Fx of " << object.Name << "\n\n";
            for ( size_t k = 0; k < pFx->Times.size(); k++ ) {
                const float *pValue =
                    &pFx->Values[k * pFx->NumComponents];
                const uint32_t refIndex = BitsOf( pValue[0] );
                const uint32_t flags = BitsOf( pValue[1] );
                const float duration = pValue[3];
                std::string root = "#?";
                AudioResolution audio;
                if ( refIndex < sceneRefs.size() ) {
                    root = Hash64( sceneRefs[refIndex].Hashcode );
                    audio = ResolveFxEvent( sceneRefs[refIndex].Hashcode, fx,
                                            hashcodes,
                                            pLanguageHashcodes );
                }
                md << "- t=" << F( pFx->Times[k] ) << " dur=" << F( duration )
                   << " root " << root << " ("
                   << ( ( flags & 0x10 ) != 0 ? "2D" : "positional" ) << ")";
                if ( audio.bFound ) {
                    md << ": " << audio.BfxFile
                       << ( audio.bLanguageSwitch ? " [LSGL voices]" : "" );
                    for ( const AudioResolution::NodeLine &node :
                          audio.Nodes ) {
                        md << "; `" << node.Name << "`";
                        md << ( node.Wav.empty()
                                    ? " -> (wav not in this bank)"
                                    : " -> " + node.Wav );
                    }
                    if ( audio.bLanguageSwitch ) {
                        voiceCues.push_back(
                            { pFx->Times[k], pFx->Times[k] + duration } );
                    }
                }
                md << "\n";
            }
        }

        // The characters of the scene (their outfit).
        if ( !sceneCharacters.empty() && ctx.bDbOk ) {
            md << "\n### Characters\n\n";
            for ( const auto &character : sceneCharacters ) {
                WriteCharacterMd( md, ctx.db, character.second, ctx.gameRoot,
                                  character.first.c_str() );
            }
        }
    }

    // The avatar ruling: what each -avatar config would play / wear here.
    if ( ctx.bDbOk && pDef != nullptr ) {
        md << "\n## Avatar ruling\n\n";
        for ( const std::string &config : ctx.avatarConfigs ) {
            const std::string normalized = NormalizeAvatarConfig( config );
            XmbValue pnj = ctx.db.FindPnj( normalized );
            if ( !pnj.isValid() ) {
                md << "- `" << config << "` -> DBPNJ `" << normalized
                   << "` not found\n";
                continue;
            }
            const bool bWoman = pnj["_sex"].asInt() == 1;
            md << "- `" << config << "` -> DBPNJ `" << normalized << "` ("
               << ( bWoman ? "woman" : "man" ) << ")";
            if ( bWoman && pDef != nullptr ) {
                // The _W variant of this definition, when the bank has one.
                for ( const CineDefinition &other : defs ) {
                    if ( other.Name == pDef->Name + "_W" ) {
                        md << ", plays variant `" << other.Name << "`";
                    }
                }
            }
            md << "\n";
            WriteCharacterMd( md, ctx.db, normalized, ctx.gameRoot, "ruling" );
        }
        // The m/w style anim substitution of this definition.
        if ( pDef != nullptr &&
             ( !pDef->StyleAnims[0].empty() || !pDef->StyleAnims[1].empty() ) ) {
            md << "\nStyle anim substitution (additionalsStyleAnims):\n";
            for ( int style = 0; style < 2; style++ ) {
                if ( pDef->StyleAnims[style].empty() ) {
                    continue;
                }
                md << "- " << ( style == 0 ? "m" : "w" ) << ":";
                for ( const auto &pair : pDef->StyleAnims[style] ) {
                    std::string from = Hash64( pair.first );
                    std::string to = Hash64( pair.second );
                    if ( const BankEntry *pFrom =
                             hashcodes.find( pair.first, 0, 8 ) ) {
                        from = pFrom->Name;
                    }
                    if ( const BankEntry *pTo =
                             hashcodes.find( pair.second, 0, 8 ) ) {
                        to = pTo->Name;
                    }
                    md << " `" << from << "` -> `" << to << "`";
                }
                md << "\n";
            }
        }
    }

    md << "\n---\nGenerated by `otdu2_cine_tool manifest`.\n";

    const fs::path mdPath = outFolder / ( entry.Name + ".md" );
    std::ofstream stream( mdPath, std::ios::binary );
    stream << md.str();
    ctx.mdCinematics++;

    // The subtitle skeleton: one empty row per voice cue. Named by the
    // definition the engine plays (playMain's name - the CINVIEWER's -cin),
    // so the filled file drops straight into the captions folder.
    if ( !voiceCues.empty() ) {
        const std::string skeletonName =
            pDef != nullptr ? pDef->Name : entry.Name;
        const fs::path csvPath =
            outFolder / ( skeletonName + ".csv.skeleton" );
        std::ofstream csv( csvPath, std::ios::binary );
        csv << "# subtitle skeleton for " << entry.Name << " (fill the third "
               "column; rename to " << skeletonName << ".csv in "
               "<userdata>/cine_subtitles/)\n";
        for ( const auto &cue : voiceCues ) {
            char line[128];
            snprintf( line, sizeof( line ), "%.2f,%.2f,\"\"\n", cue.first,
                      cue.second );
            csv << line;
        }
        ctx.skeletons++;
    }

    // ---- JSON -------------------------------------------------------------
    if ( bJson ) {
        JsonOut json;
        json.beginObject();
        json.str( "cinematic", entry.Name );
        json.str( "bank", bankStem + ".bnk" );
        json.num( "duration", cine.getDuration() );
        if ( pDef != nullptr ) {
            json.key( "definition" );
            json.beginObject();
            json.str( "name", pDef->Name );
            json.raw( "cin_props", F( static_cast<float>( pDef->CinProps ) ) );
            json.str( "shadow_config", pDef->ShadowConfig );
            json.str( "cin_file", pDef->CinFile );
            json.key( "style_anims" );
            json.beginObject();
            for ( int style = 0; style < 2; style++ ) {
                if ( pDef->StyleAnims[style].empty() ) {
                    continue;
                }
                json.key( style == 0 ? "m" : "w" );
                json.beginObject();
                for ( const auto &pair : pDef->StyleAnims[style] ) {
                    std::string from = Hash64( pair.first );
                    if ( const BankEntry *pFrom =
                             hashcodes.find( pair.first, 0, 8 ) ) {
                        from = pFrom->Name;
                    }
                    std::string to = Hash64( pair.second );
                    if ( const BankEntry *pTo =
                             hashcodes.find( pair.second, 0, 8 ) ) {
                        to = pTo->Name;
                    }
                    json.str( from, to );
                }
                json.endObject();
            }
            json.endObject();
            json.endObject();
        }
        if ( pKind != nullptr ) {
            json.key( "story" );
            json.beginObject();
            json.num( "kind", pKind->Id );
            json.str( "spot", pKind->pSpot );
            json.str( "variant", pKindVariant ? pKindVariant : "" );
            json.endObject();
        }

        json.key( "messages" );
        json.beginArray();
        for ( const Cinematic::Event &event : cine.getMessages() ) {
            json.beginObject();
            json.num( "t", event.Time );
            json.num( "duration", event.Duration );
            std::string text;
            if ( event.Reference >= 0 &&
                 event.Reference < static_cast<int32_t>( refs.size() ) ) {
                text = refs[event.Reference].Text;
            }
            json.str( "text", text );
            const MessageInfo info = ClassifyCinMessage( text );
            json.boolean( "known", info.bKnown );
            json.str( "action", info.Action );
            json.endObject();
        }
        json.endArray();

        json.key( "audio" );
        json.beginArray();
        for ( const Cinematic::Event &event : cine.getFxEvents() ) {
            json.beginObject();
            json.num( "t", event.Time );
            json.num( "duration", event.Duration );
            uint64_t rootHash = 0;
            if ( event.Reference >= 0 &&
                 event.Reference < static_cast<int32_t>( refs.size() ) ) {
                rootHash = refs[event.Reference].Hashcode;
            }
            json.hash( "root", rootHash );
            json.boolean( "2d", ( event.Flags & 0x10 ) != 0 );
            const AudioResolution audio = ResolveFxEvent(
                rootHash, fx, hashcodes, pLanguageHashcodes );
            json.boolean( "voice", audio.bFound && audio.bLanguageSwitch );
            if ( audio.bFound ) {
                json.str( "bfx", audio.BfxFile );
                json.key( "nodes" );
                json.beginArray();
                for ( const AudioResolution::NodeLine &node : audio.Nodes ) {
                    json.beginObject();
                    json.str( "name", node.Name );
                    json.str( "language", node.Language );
                    json.boolean( "alt_w", node.bAltW );
                    json.str( "wav", node.Wav );
                    if ( !node.Wav.empty() ) {
                        json.num( "wav_size", node.WavSize );
                    }
                    json.boolean( "loop", node.bLoop );
                    json.num( "volume", node.Volume );
                    json.endObject();
                }
                json.endArray();
            }
            json.endObject();
        }
        json.endArray();

        json.key( "scenes" );
        json.beginArray();
        for ( const ParsedScene &parsed : scenes ) {
            json.beginObject();
            json.str( "file", parsed.displayName );
            json.raw( "parsed", parsed.ok ? "true" : "false" );
            if ( !parsed.ok ) {
                json.endObject();
                continue;
            }
            const CinematicScene &scene = parsed.scene;
            const Matrix4x4f &origin = scene.getOrigin();
            json.key( "origin" );
            json.beginArray();
            for ( int c = 0; c < 4; c++ ) {
                for ( int r = 0; r < 4; r++ ) {
                    json.value( JsonNum( origin.m[c][r] ) );
                }
            }
            json.endArray();
            json.key( "refs" );
            json.beginArray();
            for ( const CinematicScene::Reference &ref :
                  scene.getReferences() ) {
                json.beginObject();
                json.raw( "type", F( static_cast<float>( ref.Type ) ) );
                json.hash( "hashcode", ref.Hashcode );
                json.str( "name", ref.Name );
                if ( const BankEntry *pFile =
                         hashcodes.find( ref.Hashcode, 0, 8 ) ) {
                    json.str( "file", pFile->Filename );
                    json.num( "size", pFile->Size );
                } else if ( const BankEntry *pAny =
                                hashcodes.find( ref.Hashcode ) ) {
                    json.str( "file", pAny->Filename );
                    json.num( "size", pAny->Size );
                }
                json.endObject();
            }
            json.endArray();
            json.key( "objects" );
            json.beginArray();
            for ( const CinematicScene::Object &object :
                  scene.getObjects() ) {
                json.beginObject();
                json.str( "name", object.Name );
                json.str( "class", object.isCamera() ? "camera" : "actor" );
                json.hash( "entity_hashcode", object.EntityHashcode );
                const auto it = entities.find( object.EntityHashcode );
                if ( it != entities.end() ) {
                    json.str( "entity_class", it->second.Class );
                    json.str( "entity_name", it->second.Name );
                    if ( !it->second.ConfigName.empty() ) {
                        json.str( "pnj_config", it->second.ConfigName );
                    }
                }
                const CinematicScene::Track *pAnim = object.findTrack(
                    CinematicNameHash( "Animation" ) );
                if ( pAnim != nullptr && !pAnim->Times.empty() ) {
                    json.key( "animation_keys" );
                    json.beginArray();
                    for ( size_t k = 0; k < pAnim->Times.size(); k++ ) {
                        json.beginArray();
                        json.value( JsonNum( pAnim->Times[k] ) );
                        const uint32_t refIndex =
                            BitsOf( pAnim->Values[k * pAnim->NumComponents + 0] );
                        json.value( F( static_cast<float>( refIndex ) ) );
                        if ( refIndex < scene.getReferences().size() ) {
                            json.value( JsonQuote(
                                scene.getReferences()[refIndex].Name ) );
                            if ( const BankEntry *pFile = hashcodes.find(
                                     scene.getReferences()[refIndex].Hashcode,
                                     0, 8 ) ) {
                                json.value(
                                    JsonQuote( pFile->Filename ) );
                            }
                        }
                        json.endArray();
                    }
                    json.endArray();
                }
                const CinematicScene::Track *pSceneFx =
                    object.findTrack( CinematicScene::kTrackFx );
                if ( pSceneFx != nullptr && !pSceneFx->Times.empty() ) {
                    json.key( "fx" );
                    json.beginArray();
                    for ( size_t k = 0; k < pSceneFx->Times.size(); k++ ) {
                        const float *pValue =
                            &pSceneFx->Values[k * pSceneFx->NumComponents];
                        const uint32_t refIndex = BitsOf( pValue[0] );
                        const uint32_t flags = BitsOf( pValue[1] );
                        json.beginObject();
                        json.num( "t", pSceneFx->Times[k] );
                        json.num( "duration", pValue[3] );
                        json.boolean( "2d", ( flags & 0x10 ) != 0 );
                        if ( refIndex < scene.getReferences().size() ) {
                            json.hash( "root",
                                       scene.getReferences()[refIndex]
                                           .Hashcode );
                            const AudioResolution audio = ResolveFxEvent(
                                scene.getReferences()[refIndex].Hashcode, fx,
                                hashcodes, pLanguageHashcodes );
                            json.boolean( "voice",
                                          audio.bFound &&
                                              audio.bLanguageSwitch );
                            if ( audio.bFound ) {
                                json.str( "bfx", audio.BfxFile );
                                json.key( "nodes" );
                                json.beginArray();
                                for ( const AudioResolution::NodeLine &node :
                                      audio.Nodes ) {
                                    json.beginObject();
                                    json.str( "name", node.Name );
                                    json.str( "wav", node.Wav );
                                    json.endObject();
                                }
                                json.endArray();
                            }
                        }
                        json.endObject();
                    }
                    json.endArray();
                }
                json.endObject();
            }
            json.endArray();
            json.endObject();
        }
        json.endArray();

        if ( ctx.bDbOk ) {
            json.key( "characters" );
            json.beginArray();
            std::vector<std::string> written;
            for ( const ParsedScene &parsed : scenes ) {
                if ( !parsed.ok ) {
                    continue;
                }
                for ( const CinematicScene::Object &object :
                      parsed.scene.getObjects() ) {
                    const auto it = entities.find( object.EntityHashcode );
                    if ( it == entities.end() ||
                         it->second.ConfigName.empty() ) {
                        continue;
                    }
                    if ( std::find( written.begin(), written.end(),
                                    it->second.ConfigName ) !=
                         written.end() ) {
                        continue;
                    }
                    written.push_back( it->second.ConfigName );
                    WriteCharacterJson( json, ctx.db, it->second.ConfigName,
                                        ctx.gameRoot );
                }
            }
            json.endArray();
        }
        json.endObject();

        const fs::path jsonPath = outFolder / ( entry.Name + ".json" );
        std::ofstream jsonStream( jsonPath, std::ios::binary );
        jsonStream << json.str() << "\n";
        ctx.jsonCinematics++;
    }

    printf( "  %s  %.1fs %d shots -> %s\n", entry.Name.c_str(),
            cine.getDuration(), summary.shots,
            fs::path( outFolder / ( entry.Name + ".md" ) ).string().c_str() );
    return summary;
}

// ---------------------------------------------------------------------------
// command

int ManifestAll( const std::string &source, const std::string &output,
                 bool bJson, bool bVariants ) {
    ManifestContext ctx;
    if ( const char *pLang = CmdLine::GetArgValue( "-lang" ) ) {
        ctx.language = pLang;
    }
    // The avatar ruling inputs: -avatar, else both story defaults.
    if ( const char *pAvatar = CmdLine::GetArgValue( "-avatar" ) ) {
        ctx.avatarConfigs.push_back( pAvatar );
    } else {
        ctx.avatarConfigs.push_back( "AVT_Male_Intro" );
        ctx.avatarConfigs.push_back( "AVT_Female_Intro" );
    }

    std::vector<fs::path> banks;
    CollectBanks( source, bVariants, banks );
    if ( banks.empty() ) {
        printf( "no .bnk found under %s\n", source.c_str() );
        return 1;
    }

    // Locate the game root (for db_data.cpr): the source itself, then the
    // bank folders' parents.
    fs::path root( source );
    if ( fs::is_regular_file( root ) ) {
        root = root.parent_path();
    }
    for ( fs::path probe = fs::absolute( root ); !probe.empty();
          probe = probe.parent_path() ) {
        std::error_code ec;
        const fs::path candidate =
            probe / "Euro" / "Bnk" / "Database" / "db_data.cpr";
        if ( fs::exists( candidate, ec ) ) {
            ctx.gameRoot = probe.string();
            break;
        }
        if ( probe == probe.root_path() ) {
            break;
        }
    }
    if ( !ctx.gameRoot.empty() ) {
        const fs::path dbPath = fs::path( ctx.gameRoot ) / "Euro" / "Bnk" /
                                "Database" / "db_data.cpr";
        ctx.bDbOk = ctx.db.Load( dbPath );
        printf( "database: %s (%s)\n", dbPath.string().c_str(),
                ctx.bDbOk ? "ok" : "LOAD FAILED" );
    } else {
        printf( "database: not found (characters / avatar ruling without "
                "db_data.cpr are skipped)\n" );
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
        BankHashcodes hashcodes;
        hashcodes.load( bank );
        FxResolver fx;
        fx.Load( bank );
        const std::vector<CineDefinition> defs =
            LoadCineDefinitions( bank, hashcodes );
        const std::map<uint64_t, EntityInfo> entities =
            LoadEntityIndex( bank );

        // The language bank (voices): "<stem>__<lang>.bnk" next to this one.
        BankHashcodes languageHashcodes;
        BankHashcodes *pLanguageHashcodes = nullptr;
        std::unique_ptr<Bank> pLanguageBank;
        if ( !IsVariantBank( stem ) ) {
            const fs::path languagePath =
                bankPath.parent_path() /
                ( stem + "__" + ctx.language + ".bnk" );
            if ( fs::exists( languagePath, ec ) ) {
                pLanguageBank = std::make_unique<Bank>();
                if ( pLanguageBank->load( languagePath.string().c_str() ) ) {
                    languageHashcodes.load( *pLanguageBank );
                    pLanguageHashcodes = &languageHashcodes;
                } else {
                    pLanguageBank.reset();
                }
            }
        }

        int cins = 0;
        for ( const BankEntry &entry : bank.getEntries() ) {
            if ( StringCmpI( entry.Extension.c_str(), "cin" ) != 0 ) {
                continue;
            }
            summaries.push_back( ManifestCinematic(
                bank, stem, hashcodes, fx, defs, entities, ctx,
                entry, outDir, pLanguageBank.get(), pLanguageHashcodes,
                bJson ) );
            cins++;
        }
        if ( cins == 0 ) {
            printf( "[bank] %s: no .cin entries\n", stem.c_str() );
        }
    }

    // Index.
    std::ostringstream index;
    index << "# Cutscene manifest index\n\n";
    index << "Generated by `otdu2_cine_tool manifest " << source
          << ( bVariants ? " -variants" : "" ) << ( bJson ? " -json" : "" )
          << " -lang " << ctx.language << "` - dump plus the resolved assets, "
             "entities, characters, voicelines, message protocol and story "
             "placements (docs/re/cutscenes_inventory.md).\n\n";
    index << "| bank | cinematic | dur s | shots | scenes | msgs | fx |\n";
    index << "|---|---|---|---|---|---|---|\n";
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
              << summary.messages << " | " << summary.fx << " |\n";
    }
    index << "\n" << banks.size() << " banks, " << okCount
          << " cinematics manifested, " << ctx.skeletons
          << " subtitle skeletons.\n";
    std::ofstream stream( outDir / "index.md", std::ios::binary );
    stream << index.str();
    printf( "index: %s\n", ( outDir / "index.md" ).string().c_str() );
    return failedBanks > 0 ? 2 : 0;
}

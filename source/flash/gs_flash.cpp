#include "shared.h"
#include "gs_flash.h"

#include "system/cmd_line.h"
#include "filesystem/gs_file.h"

#include "imgui.h"

GSFlash *gpFlash = nullptr;

GSFlash::GSFlash() { gpFlash = this; }

GSFlash::~GSFlash() { gpFlash = nullptr; }

namespace {
uint16_t ReadU16( const VMFFile &vmf, uint32_t offset ) {
    const std::vector<uint8_t> &data = vmf.getData();
    return static_cast<uint16_t>( data[offset] | ( data[offset + 1] << 8 ) );
}

// -dumpbuttons: every DefineButton2 of the movie (and of its sprites) with
// its event conditions and the string constants of the action bytecode
// (the FSCommand / function names the buttons trigger), plus the DoAction
// script strings per timeline. DefineButton2: u16 id, u8 flags, u16
// action offset, character records, then condition action records
// { u16 size, u16 conditions, actions }.
void DumpActionStrings( const VMFFile &vmf, uint32_t start, uint32_t end, const char *pContext ) {
    const std::vector<uint8_t> &data = vmf.getData();
    uint32_t offset = start;
    std::string strings;
    while ( offset < end ) {
        const uint8_t code = data[offset];
        offset += 1;
        uint32_t length = 0;
        if ( code >= 0x80 ) {
            if ( offset + 2 > end ) {
                break;
            }
            length = data[offset] | ( data[offset + 1] << 8 );
            offset += 2;
        }
        if ( code == 0x00 ) { // End
            break;
        }
        // Push. The Eden VMF bytecode uses 0x96 where the SWF reference
        // uses 0x40 (GetMember 0x1c / SetMember 0x4e / Pop 0x17 are
        // standard); both are accepted, the payload starts with the type
        // byte either way.
        if ( ( code == 0x40 || code == 0x96 ) && offset < end ) {
            const uint8_t type = data[offset];
            uint32_t value = offset + 1;
            if ( type == 0 && value < end ) { // string constant
                uint32_t stringEnd = value;
                while ( stringEnd < end && data[stringEnd] != 0 ) {
                    stringEnd++;
                }
                if ( !strings.empty() ) {
                    strings += ", ";
                }
                strings += std::string( reinterpret_cast<const char *>( &data[value] ), stringEnd - value );
            }
        }
        offset += length;
    }
    if ( !strings.empty() ) {
        OTDU_LOG_INFO( "    ACTIONS [%s]: %s\n", pContext, strings.c_str() );
    }
}

void DumpTags( const VMFFile &vmf, const std::vector<FlashTag> &tags, const std::string &context ) {
    const std::vector<uint8_t> &data = vmf.getData();
    for ( const FlashTag &tag : tags ) {
        if ( tag.Code == FT_DefineSprite || tag.Code == FT_DefineSpriteEden ) {
            const uint32_t start = tag.Offset + ( tag.Code == FT_DefineSpriteEden ? 8 : 4 );
            std::vector<FlashTag> spriteTags;
            vmf.parseTags( start, tag.Offset + tag.Length, spriteTags );
            DumpTags( vmf, spriteTags, context );
        } else if ( tag.Code == FT_DefineButton2 ) {
            const uint16_t id = ReadU16( vmf, tag.Offset );
            const uint16_t actionOffset = ReadU16( vmf, tag.Offset + 3 );
            OTDU_LOG_INFO( "  BUTTON %u (context %s)\n", id, context.c_str() );
            if ( actionOffset != 0 ) {
                uint32_t condition = tag.Offset + 3 + actionOffset;
                const uint32_t end = tag.Offset + tag.Length;
                while ( condition + 4 <= end ) {
                    const uint16_t conditionSize = ReadU16( vmf, condition );
                    const uint16_t conditions = ReadU16( vmf, condition + 2 );
                    const uint32_t conditionEnd = conditionSize != 0 ? condition + conditionSize : end;
                    const uint32_t actionStart = condition + 4;
                    const uint32_t actionEnd = conditionEnd > actionStart ? conditionEnd : actionStart;
                    static const char *kNames[] = { "idle", "press", "release", "rollOut", "rollOver",
                                                    "dragOut", "dragOver", "releaseOut", "key" };
                    std::string flags;
                    for ( int bit = 0; bit < 9; bit++ ) {
                        if ( conditions & ( 1 << bit ) ) {
                            flags += std::string( flags.empty() ? "" : "|" ) + kNames[bit];
                        }
                    }
                    if ( ( conditions & 0x100 ) != 0 ) {
                        flags += " keyCode " + std::to_string( ( conditions >> 9 ) & 0x7f );
                    }
                    OTDU_LOG_INFO( "    conditions %s (%04x) size %u actions %u\n", flags.c_str(), conditions,
                                   conditionSize, actionEnd - actionStart );
                    {
                        std::string hex;
                        for ( uint32_t b = actionStart; b < actionEnd && b < actionStart + 64; b++ ) {
                            char buffer[8];
                            snprintf( buffer, sizeof( buffer ), "%02x ", data[b] );
                            hex += buffer;
                        }
                        if ( !hex.empty() ) {
                            OTDU_LOG_INFO( "      hex %s\n", hex.c_str() );
                        }
                    }
                    DumpActionStrings( vmf, actionStart, actionEnd, std::to_string( id ).c_str() );
                    if ( conditionSize == 0 ) {
                        break;
                    }
                    condition += conditionSize;
                }
            }
        } else if ( tag.Code == FT_DoAction ) {
            DumpActionStrings( vmf, tag.Offset, tag.Offset + tag.Length, "DoAction" );
        }
    }
}

} // namespace

void DumpMovieButtons( const FlashMovie *pMovie ) {
    const VMFFile &vmf = pMovie->getVMF();
    OTDU_LOG_INFO( "Movie buttons:\n" );
    DumpTags( vmf, vmf.getTags(), "root" );
}

bool GSFlash::initialize( TestDriveGameInstance *pInstance ) {
    GameSystem::initialize( pInstance );
    // main.bnk holds the common frontend resources (fonts, icons pages).
    if ( getMovie( "main" ) == nullptr ) {
        OTDU_LOG_WARN( "Failed to load FrontEnd/HiRes/main.bnk (UI fonts missing)\n" );
    } else if ( CmdLine::HasArg( "-dumplabels" ) ) {
        // Frame labels of the loaded movies (the pause menu labels of
        // MAIN.vmf: pause_menu, pause_in, pause_stop, resume, options, quit)
        // and the export table (attachMovie linkage names: specificIcons...).
        for ( const auto &entry : movies ) {
            OTDU_LOG_INFO( "Movie '%s' frame labels:\n", entry.first.c_str() );
            for ( const std::string &label : entry.second->getVMF().getFrameLabels() ) {
                OTDU_LOG_INFO( "  LABEL '%s'\n", label.c_str() );
            }
            for ( const auto &exported : entry.second->getVMF().getExports() ) {
                OTDU_LOG_INFO( "  EXPORT '%s' -> %u\n", exported.first.c_str(), exported.second );
            }
        }
    } else if ( CmdLine::HasArg( "-dumpbuttons" ) ) {
        // The DefineButton2 tables + action strings of every loaded movie
        // (which entry of the pause menu sends which FSCommand).
        for ( const auto &entry : movies ) {
            DumpMovieButtons( entry.second.get() );
        }
    }
    // -dumpflashart all: also load (and so dump, FlashMovie::load) every
    // frontend movie, not only the ones this mode opens - the art of a
    // retail capture may live in a bank the port never loads.
    if ( const char *pDump = CmdLine::GetArgValue( "-dumpflashart" ) ) {
        if ( StringCmpI( pDump, "all" ) == 0 ) {
            const std::string folder = gpFile->getBankFolder() + "FrontEnd/HiRes";
            for ( const std::filesystem::path &bank : gpFile->listFolder( folder.c_str(), ".bnk" ) ) {
                getMovie( bank.stem().string().c_str() );
            }
        } else {
            // A comma list of banks ("r_school,results"): load and dump each.
            std::string list = pDump;
            size_t start = 0;
            while ( start <= list.size() ) {
                const size_t comma = list.find( ',', start );
                const std::string bank = list.substr( start, comma == std::string::npos ? std::string::npos : comma - start );
                if ( !bank.empty() ) {
                    getMovie( bank.c_str() );
                }
                if ( comma == std::string::npos ) {
                    break;
                }
                start = comma + 1;
            }
        }
    }
    return true;
}

void GSFlash::terminate() { movies.clear(); }

FlashMovie *GSFlash::getMovie( const char *pName ) {
    const std::string key = ToLower( pName );
    auto it = movies.find( key );
    if ( it != movies.end() ) {
        return it->second.get();
    }
    auto pMovie = std::make_unique<FlashMovie>();
    if ( !pMovie->load( pName ) ) {
        return nullptr;
    }
    FlashMovie *pResult = pMovie.get();
    // -dumpbuttons also covers the movies a mode loads later (home, shopscar,
    // shopsavatar ...): their FSCommand strings are the protocol to answer.
    if ( key != "main" && CmdLine::HasArg( "-dumpbuttons" ) ) {
        OTDU_LOG_INFO( "Movie '%s':\n", key.c_str() );
        DumpMovieButtons( pResult );
    }
    movies[key] = std::move( pMovie );
    return pResult;
}

void GSFlash::releaseMovie( const char *pName ) {
    const std::string key = ToLower( pName );
    if ( key == "main" ) {
        return;
    }
    movies.erase( key );
}

const Font *GSFlash::getTitleFont() const {
    auto it = movies.find( "main" );
    if ( it == movies.end() ) {
        return nullptr;
    }
    const Font *pFont = it->second->getFont( "Berlin Sans FB", false );
    return pFont != nullptr ? pFont : it->second->getDefaultFont();
}

const Font *GSFlash::getTextFont() const {
    auto it = movies.find( "main" );
    if ( it == movies.end() ) {
        return nullptr;
    }
    const Font *pFont = it->second->getFont( "Berlin Sans FB", true );
    return pFont != nullptr ? pFont : it->second->getDefaultFont();
}

const FlashMovie *GSFlash::getMainMovie() const {
    auto it = movies.find( "main" );
    return it != movies.end() ? it->second.get() : nullptr;
}

void GSFlash::drawDebug() {
    if ( !ImGui::Begin( "Flash" ) ) {
        ImGui::End();
        return;
    }
    for ( const auto &movie : movies ) {
        const VMFFile &vmf = movie.second->getVMF();
        if ( ImGui::TreeNode( movie.first.c_str() ) ) {
            ImGui::Text( "Stage %.0fx%.0f, %u frames @ %.0f fps, SWF v%u", vmf.getStageWidth(),
                         vmf.getStageHeight(), vmf.getFrameCount(), vmf.getFrameRate(),
                         vmf.getVersion() );
            ImGui::Text( "%zu tags, %zu bitmaps, %zu fonts", vmf.getTags().size(),
                         vmf.getBitmaps().size(), vmf.getFonts().size() );
            for ( const FlashFontDesc &font : vmf.getFonts() ) {
                ImGui::BulletText( "Font %u '%s' (texture %u, %zu/%zu glyphs)", font.CharacterId,
                                   font.Name.c_str(), font.TextureId, font.Glyphs.size(),
                                   font.SecondaryGlyphs.size() );
            }
            for ( const auto &exported : vmf.getExports() ) {
                ImGui::BulletText( "Export '%s' -> %u", exported.first.c_str(), exported.second );
            }
            for ( const std::string &label : vmf.getFrameLabels() ) {
                ImGui::BulletText( "Label '%s'", label.c_str() );
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

const Font *GSFlash::findFont( const char *pName ) const {
    auto main = movies.find( "main" );
    if ( main != movies.end() ) {
        if ( const Font *pFont = main->second->getFont( pName ) ) {
            return pFont;
        }
    }
    for ( const auto &entry : movies ) {
        if ( const Font *pFont = entry.second->getFont( pName ) ) {
            return pFont;
        }
    }
    return nullptr;
}

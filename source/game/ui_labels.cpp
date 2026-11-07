#include "shared.h"
#include "ui_labels.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"

#include <algorithm>
#include <fstream>

namespace {
    std::vector<UiElementLabel> labels;
    std::string labelPath;
    bool bLoaded = false;

    std::filesystem::path ResolveLabelPath() {
        // The launchers cd to the repo root, so docs/re/ui_elements.txt is the
        // canonical, versioned location; without a docs/re (an exe run from
        // elsewhere) keep it beside the other user data.
        std::error_code ec;
        if ( std::filesystem::exists( "docs/re", ec ) ) {
            return std::filesystem::path( "docs/re/ui_elements.txt" );
        }
        if ( gpFile != nullptr ) {
            return gpFile->getUserDataFolder() / "ui_elements.txt";
        }
        return std::filesystem::path( "ui_elements.txt" );
    }

    // Splits a TSV line into at most 4 fields.
    void SplitLine( const std::string &line, std::string out[4] ) {
        size_t at = 0;
        for ( int32_t field = 0; field < 4; field++ ) {
            if ( field == 3 ) {
                out[3] = line.substr( at );
                break;
            }
            const size_t tab = line.find( '\t', at );
            if ( tab == std::string::npos ) {
                out[field] = line.substr( at );
                at = line.size();
                continue;
            }
            out[field] = line.substr( at, tab - at );
            at = tab + 1;
        }
        // Trim the trailing \r of a CRLF file.
        for ( int32_t field = 0; field < 4; field++ ) {
            while ( !out[field].empty() && out[field].back() == '\r' ) {
                out[field].pop_back();
            }
        }
    }
} // namespace

std::string NormalizeUiMovie( const char *pMovie ) {
    std::string key = pMovie != nullptr ? pMovie : "";
    for ( char &c : key ) {
        if ( c == '\\' ) {
            c = '/';
        } else {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
    }
    // Accept a bank path ("euro/bnk/frontend/hires/main.bnk"), a bank stem
    // ("main") or the movie name as authored ("MAIN.vmf"): the key is always
    // the bank's stem plus ".vmf", because that is what the game data holds.
    const size_t slash = key.find_last_of( '/' );
    if ( slash != std::string::npos ) {
        key = key.substr( slash + 1 );
    }
    if ( key.size() > 4 && key.compare( key.size() - 4, 4, ".bnk" ) == 0 ) {
        key = key.substr( 0, key.size() - 4 );
    }
    if ( key.size() > 4 && key.compare( key.size() - 4, 4, ".vmf" ) == 0 ) {
        key = key.substr( 0, key.size() - 4 );
    }
    if ( key.empty() ) {
        return key;
    }
    return key + ".vmf";
}

std::string NormalizeUiClip( const char *pClip ) {
    std::string key = pClip != nullptr ? pClip : "";
    for ( char &c : key ) {
        if ( c == '\\' ) {
            c = '/';
        } else {
            c = static_cast<char>( tolower( static_cast<unsigned char>( c ) ) );
        }
    }
    while ( !key.empty() && key.front() == '/' ) {
        key.erase( key.begin() );
    }
    while ( !key.empty() && key.back() == '/' ) {
        key.pop_back();
    }
    if ( key.empty() ) {
        return "/";
    }
    return "/" + key;
}

const std::string &GetUiElementLabelPath() {
    if ( labelPath.empty() ) {
        labelPath = ResolveLabelPath().string();
    }
    return labelPath;
}

const std::vector<UiElementLabel> &GetUiElementLabels() {
    LoadUiElementLabels();
    return labels;
}

bool LoadUiElementLabels() {
    if ( bLoaded ) {
        return true;
    }
    bLoaded = true;
    const std::string path = GetUiElementLabelPath();
    std::ifstream file( path );
    if ( !file ) {
        OTDU_LOG_INFO(
            "UiLabels: no label file at '%s' yet (the viewer creates it)\n",
            path.c_str() );
        return true;
    }
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        std::string field[4];
        SplitLine( line, field );
        if ( field[0].empty() || field[1].empty() ) {
            continue;
        }
        UiElementLabel label;
        label.Movie = NormalizeUiMovie( field[0].c_str() );
        label.Clip = NormalizeUiClip( field[1].c_str() );
        label.Label = field[2];
        label.Note = field[3];
        labels.push_back( label );
    }
    OTDU_LOG_INFO( "UiLabels: %zu label(s) from '%s'\n", labels.size(),
                   path.c_str() );
    return true;
}

bool SaveUiElementLabels() {
    const std::string path = GetUiElementLabelPath();
    std::sort( labels.begin(), labels.end(),
               []( const UiElementLabel &a, const UiElementLabel &b ) {
                   return a.Movie != b.Movie ? a.Movie < b.Movie
                                             : a.Clip < b.Clip;
               } );
    std::ofstream file( path );
    if ( !file ) {
        OTDU_LOG_WARN( "UiLabels: cannot write '%s'\n", path.c_str() );
        return false;
    }
    file << "# OpenTDU2 UI element labels - the human names of the frontend's\n";
    file << "# flash UI elements. Written by the UI viewer (-gm UIVIEWER); safe\n";
    file << "# to hand edit.\n";
    file << "#\n";
    file << "# The UI is a set of Flash movies; every element on screen is a node\n";
    file << "# of the movie's display list, addressed by its path from the root\n";
    file << "# (/pause_menu/menu_sq/sq3). That path is the identity the runtime\n";
    file << "# takes (FlashPlayer::findClip / gotoLabel), so it is the key here.\n";
    file << "#\n";
    file << "# One element per line, TAB separated, sorted by movie then clip:\n";
    file << "#   <movie .vmf>\t<clip path>\t<label>\t<note>\n";
    file << "# -lintui checks every key against the movie's display list.\n";
    for ( const UiElementLabel &label : labels ) {
        file << label.Movie << '\t' << label.Clip << '\t' << label.Label << '\t'
             << label.Note << '\n';
    }
    OTDU_LOG_INFO( "UiLabels: wrote %zu label(s) to '%s'\n", labels.size(),
                   path.c_str() );
    return true;
}

const UiElementLabel *FindUiElementLabel( const char *pMovie, const char *pClip ) {
    LoadUiElementLabels();
    const std::string movie = NormalizeUiMovie( pMovie );
    const std::string clip = NormalizeUiClip( pClip );
    for ( const UiElementLabel &label : labels ) {
        if ( label.Movie == movie && label.Clip == clip ) {
            return &label;
        }
    }
    return nullptr;
}

bool SetUiElementLabel( const char *pMovie,
                        const char *pClip,
                        const std::string &label,
                        const std::string &note ) {
    LoadUiElementLabels();
    const std::string movie = NormalizeUiMovie( pMovie );
    const std::string clip = NormalizeUiClip( pClip );
    if ( movie.empty() || clip.empty() ) {
        return false;
    }
    for ( size_t i = 0; i < labels.size(); i++ ) {
        if ( labels[i].Movie == movie && labels[i].Clip == clip ) {
            if ( labels[i].Label == label && labels[i].Note == note ) {
                return false;
            }
            if ( label.empty() && note.empty() ) {
                labels.erase( labels.begin() + i );
            } else {
                labels[i].Label = label;
                labels[i].Note = note;
            }
            return true;
        }
    }
    if ( label.empty() && note.empty() ) {
        return false;
    }
    UiElementLabel entry;
    entry.Movie = movie;
    entry.Clip = clip;
    entry.Label = label;
    entry.Note = note;
    labels.push_back( entry );
    return true;
}


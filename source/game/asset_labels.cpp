#include "shared.h"
#include "asset_labels.h"

#include "filesystem/bank.h"
#include "filesystem/gs_file.h"

#include <algorithm>
#include <fstream>

namespace {
    std::vector<AssetLabel> labels;
    std::string labelPath;
    bool bLoaded = false;

    std::filesystem::path ResolveLabelPath() {
        // The launchers cd to the repo root, so docs/re/asset_labels.txt is the
        // canonical, versioned location; without a docs/re (an exe run from
        // elsewhere) keep it beside the other user data.
        std::error_code ec;
        if ( std::filesystem::exists( "docs/re", ec ) ) {
            return std::filesystem::path( "docs/re/asset_labels.txt" );
        }
        if ( gpFile != nullptr ) {
            return gpFile->getUserDataFolder() / "asset_labels.txt";
        }
        return std::filesystem::path( "asset_labels.txt" );
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

std::string NormalizeAssetBank( const char *pBank ) {
    std::string key = pBank != nullptr ? pBank : "";
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
    return key;
}

const std::string &GetAssetLabelPath() {
    if ( labelPath.empty() ) {
        labelPath = ResolveLabelPath().string();
    }
    return labelPath;
}

const std::vector<AssetLabel> &GetAssetLabels() {
    LoadAssetLabels();
    return labels;
}

bool LoadAssetLabels() {
    if ( bLoaded ) {
        return true;
    }
    bLoaded = true;
    const std::string path = GetAssetLabelPath();
    std::ifstream file( path );
    if ( !file ) {
        OTDU_LOG_INFO(
            "AssetLabels: no label file at '%s' yet (the viewer creates it)\n",
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
        AssetLabel label;
        label.Bank = NormalizeAssetBank( field[0].c_str() );
        label.Asset = field[1];
        label.Label = field[2];
        label.Note = field[3];
        labels.push_back( label );
    }
    OTDU_LOG_INFO( "AssetLabels: %zu label(s) from '%s'\n", labels.size(),
                   path.c_str() );
    return true;
}

bool SaveAssetLabels() {
    const std::string path = GetAssetLabelPath();
    std::sort( labels.begin(), labels.end(),
               []( const AssetLabel &a, const AssetLabel &b ) {
                   return a.Bank != b.Bank ? a.Bank < b.Bank
                                           : a.Asset < b.Asset;
               } );
    std::ofstream file( path );
    if ( !file ) {
        OTDU_LOG_WARN( "AssetLabels: cannot write '%s'\n", path.c_str() );
        return false;
    }
    file << "# OpenTDU2 asset labels - the human names of the game's assets.\n";
    file << "# Written by the asset viewer (-gm ASSET_VIEWER); safe to hand "
            "edit.\n";
    file << "# One asset per line, TAB separated, sorted by bank then asset:\n";
    file << "#   <bank, relative to the game root, lowercase, '/'>\t<asset "
            "filename>\t<label>\t<note>\n";
    file << "# -labellint checks every key against the game data.\n";
    for ( const AssetLabel &label : labels ) {
        file << label.Bank << '\t' << label.Asset << '\t' << label.Label << '\t'
             << label.Note << '\n';
    }
    OTDU_LOG_INFO( "AssetLabels: wrote %zu label(s) to '%s'\n", labels.size(),
                   path.c_str() );
    return true;
}

const AssetLabel *FindAssetLabel( const char *pBank, const char *pAsset ) {
    LoadAssetLabels();
    const std::string bank = NormalizeAssetBank( pBank );
    const std::string asset = pAsset != nullptr ? pAsset : "";
    for ( const AssetLabel &label : labels ) {
        if ( label.Bank == bank &&
             StringCmpI( label.Asset.c_str(), asset.c_str() ) == 0 ) {
            return &label;
        }
    }
    return nullptr;
}

bool SetAssetLabel( const char *pBank,
                    const char *pAsset,
                    const std::string &label,
                    const std::string &note ) {
    LoadAssetLabels();
    const std::string bank = NormalizeAssetBank( pBank );
    const std::string asset = pAsset != nullptr ? pAsset : "";
    if ( bank.empty() || asset.empty() ) {
        return false;
    }
    for ( size_t i = 0; i < labels.size(); i++ ) {
        if ( labels[i].Bank == bank &&
             StringCmpI( labels[i].Asset.c_str(), asset.c_str() ) == 0 ) {
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
    AssetLabel entry;
    entry.Bank = bank;
    entry.Asset = asset;
    entry.Label = label;
    entry.Note = note;
    labels.push_back( entry );
    return true;
}

const char *AssetLabelOrName( const char *pBank, const char *pAsset ) {
    const AssetLabel *pLabel = FindAssetLabel( pBank, pAsset );
    if ( pLabel != nullptr && !pLabel->Label.empty() ) {
        return pLabel->Label.c_str();
    }
    return pAsset != nullptr ? pAsset : "";
}

int32_t LintAssetLabels() {
    LoadAssetLabels();
    // Every key must exist in the game data: a bank that does not load, or a
    // filename that is not in it, is a problem (a typo, or a renamed asset).
    std::vector<Bank *> loaded;
    std::vector<std::string> loadedPaths;
    int32_t problems = 0;
    for ( const AssetLabel &label : labels ) {
        Bank *pBank = nullptr;
        for ( size_t i = 0; i < loadedPaths.size(); i++ ) {
            if ( loadedPaths[i] == label.Bank ) {
                pBank = loaded[i];
                break;
            }
        }
        if ( pBank == nullptr ) {
            pBank = new Bank();
            if ( !pBank->load( label.Bank.c_str() ) ) {
                OTDU_LOG_WARN( "AssetLabels lint: '%s' is not a bank in the "
                               "game data ('%s')\n",
                               label.Bank.c_str(), label.Asset.c_str() );
                problems++;
                delete pBank;
                continue;
            }
            loaded.push_back( pBank );
            loadedPaths.push_back( label.Bank );
        }
        if ( pBank->findEntry( label.Asset.c_str() ) == nullptr ) {
            OTDU_LOG_WARN( "AssetLabels lint: '%s' is not in '%s' ('%s')\n",
                           label.Asset.c_str(), label.Bank.c_str(),
                           label.Label.c_str() );
            problems++;
        }
    }
    for ( Bank *pBank : loaded ) {
        delete pBank;
    }
    OTDU_LOG_INFO(
        "AssetLabels lint: %zu label(s), %d problem(s)%s\n", labels.size(),
        problems, problems == 0 ? " - every key exists in the game data" : "" );
    return problems;
}

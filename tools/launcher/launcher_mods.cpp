#include "launcher_mods.h"

#include "launcher_json.h"
#include "launcher_util.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace Launcher {

namespace {

namespace fs = std::filesystem;

const char *kOriginalPhysicsJson =
    "{\n"
    "  \"name\": \"Original physics tuning\",\n"
    "  \"version\": \"1.0\",\n"
    "  \"author\": \"OpenTDU2 (the data is yours, not ours)\",\n"
    "  \"description\": \"The Steam / TDU World install carries TDU World's retuned Physics.cpr. Eden's original "
    "tuning (strong speed-dependent yaw damping, a steering lock that shrinks with speed, original ABS / traction "
    "control / handbrake settings) is in the Test Drive Unlimited 2 Dev Build prototype. Nothing is shipped here: "
    "Import copies Physics.cpr from your own copy of that build.\",\n"
    "  \"importPrompt\": \"Choose the folder of your Test Drive Unlimited 2 Dev Build (the one with TestDrive2DEV.exe)\",\n"
    "  \"import\": [ \"Physics.cpr\" ]\n"
    "}\n";

int CountFiles( const fs::path &folder ) {
    int count = 0;
    std::error_code ec;
    for ( const auto &entry : fs::recursive_directory_iterator( folder, ec ) ) {
        if ( entry.is_regular_file( ec ) && entry.path().filename() != L"mod.json" ) {
            count++;
        }
    }
    return count;
}

} // namespace

bool IsPlainName( const std::string &name ) {
    if ( name.empty() || name == "." || name == ".." || name.size() > 128 ) {
        return false;
    }
    return name.find_first_of( "/\\:*?\"<>|" ) == std::string::npos;
}

std::wstring ModsRoot( const std::wstring &userData ) {
    return JoinPath( userData, L"mods" );
}

void EnsureBundledMods( const std::wstring &userData ) {
    const std::wstring folder = JoinPath( JoinPath( ModsRoot( userData ), L"packages" ), L"original-physics" );
    const std::wstring manifest = JoinPath( folder, L"mod.json" );
    if ( !FileExists( manifest ) ) {
        MakeDirs( folder );
        WriteTextFile( manifest, kOriginalPhysicsJson );
    }
}

std::vector<ModInfo> ScanMods( const std::wstring &userData ) {
    std::vector<ModInfo> mods;
    const std::wstring root = ModsRoot( userData );
    const std::wstring packages = JoinPath( root, L"packages" );
    std::error_code ec;
    if ( !fs::is_directory( fs::path( packages ), ec ) ) {
        return mods;
    }
    for ( const auto &entry : fs::directory_iterator( fs::path( packages ), ec ) ) {
        if ( !entry.is_directory( ec ) ) {
            continue;
        }
        ModInfo mod;
        mod.Id = WideToUtf8( entry.path().filename().wstring() );
        if ( !IsPlainName( mod.Id ) ) {
            continue;
        }
        mod.Folder = entry.path().wstring();
        mod.Name = mod.Id;
        std::string text;
        JsonValue json;
        std::string error;
        if ( ReadTextFile( JoinPath( mod.Folder, L"mod.json" ), text ) && JsonValue::Parse( text, json, error ) &&
             json.IsObject() ) {
            mod.Name = json.GetString( "name", mod.Id );
            mod.Version = json.GetString( "version" );
            mod.Author = json.GetString( "author" );
            mod.Description = json.GetString( "description" );
            mod.ImportPrompt = json.GetString( "importPrompt", "Choose the folder that holds the files" );
            const JsonValue *pImport = json.Find( "import" );
            if ( pImport != nullptr && pImport->IsArray() ) {
                for ( const JsonValue &file : pImport->AsArray() ) {
                    if ( file.IsString() && IsPlainName( file.AsString() ) ) {
                        mod.ImportFiles.push_back( file.AsString() );
                    }
                }
            }
        }
        mod.FileCount = CountFiles( entry.path() );
        mods.push_back( std::move( mod ) );
    }

    // enabled.txt: priority order.
    std::vector<std::string> order;
    std::string list;
    if ( ReadTextFile( JoinPath( root, L"enabled.txt" ), list ) ) {
        size_t pos = 0;
        while ( pos <= list.size() ) {
            size_t end = list.find( '\n', pos );
            if ( end == std::string::npos ) {
                end = list.size();
            }
            std::string line = list.substr( pos, end - pos );
            while ( !line.empty() && ( line.back() == '\r' || line.back() == ' ' || line.back() == '\t' ) ) {
                line.pop_back();
            }
            const size_t first = line.find_first_not_of( " \t" );
            if ( first != std::string::npos && line[first] != '#' ) {
                order.push_back( line.substr( first ) );
            }
            pos = end + 1;
        }
    }
    std::vector<ModInfo> sorted;
    std::set<std::string> used;
    for ( const std::string &id : order ) {
        for ( ModInfo &mod : mods ) {
            if ( mod.Id == id && used.insert( id ).second ) {
                mod.bEnabled = true;
                sorted.push_back( mod );
            }
        }
    }
    std::vector<ModInfo> rest;
    for ( ModInfo &mod : mods ) {
        if ( !used.count( mod.Id ) ) {
            rest.push_back( mod );
        }
    }
    std::sort( rest.begin(), rest.end(), []( const ModInfo &a, const ModInfo &b ) {
        return ToLowerAscii( a.Name ) < ToLowerAscii( b.Name );
    } );
    sorted.insert( sorted.end(), rest.begin(), rest.end() );
    return sorted;
}

bool SaveEnabledMods( const std::wstring &userData, const std::vector<ModInfo> &mods ) {
    std::string text = "# Written by the OpenTDU2 launcher (Mods page): one mod folder per line, highest priority first.\n";
    for ( const ModInfo &mod : mods ) {
        if ( mod.bEnabled && !mod.NeedsImport() && IsPlainName( mod.Id ) ) {
            text += mod.Id + "\n";
        }
    }
    MakeDirs( ModsRoot( userData ) );
    return WriteTextFile( JoinPath( ModsRoot( userData ), L"enabled.txt" ), text );
}

bool ImportModFiles( const ModInfo &mod, const std::wstring &sourceFolder, std::string &error ) {
    if ( mod.ImportFiles.empty() ) {
        error = "This mod does not import anything.";
        return false;
    }
    std::error_code ec;
    for ( const std::string &name : mod.ImportFiles ) {
        if ( !IsPlainName( name ) ) {
            error = "The mod lists an unsafe file name.";
            return false;
        }
        const fs::path from = fs::path( sourceFolder ) / Utf8ToWide( name );
        if ( !fs::is_regular_file( from, ec ) ) {
            error = "\"" + name + "\" was not found in that folder.";
            return false;
        }
    }
    for ( const std::string &name : mod.ImportFiles ) {
        const fs::path from = fs::path( sourceFolder ) / Utf8ToWide( name );
        const fs::path to = fs::path( mod.Folder ) / Utf8ToWide( name );
        fs::copy_file( from, to, fs::copy_options::overwrite_existing, ec );
        if ( ec ) {
            error = "Could not copy \"" + name + "\": " + ec.message();
            return false;
        }
    }
    return true;
}

} // namespace Launcher

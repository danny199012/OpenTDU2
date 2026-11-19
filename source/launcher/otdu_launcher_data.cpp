#include "shared.h"
#include "otdu_launcher_data.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace OTDULauncher {

namespace {

bool ReadText( const fs::path &path, std::string &out ) {
    std::ifstream in( path, std::ios::binary );
    if ( !in ) {
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

bool WriteText( const fs::path &path, const std::string &text ) {
    std::error_code ec;
    fs::create_directories( path.parent_path(), ec );
    std::ofstream out( path, std::ios::binary | std::ios::trunc );
    if ( !out ) {
        return false;
    }
    out << text;
    return out.good();
}

int64_t LastWrite( const fs::path &path ) {
    std::error_code ec;
    const auto ft = fs::last_write_time( path, ec );
    if ( ec ) {
        return 0;
    }
    const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - fs::file_time_type::clock::now() + std::chrono::system_clock::now() );
    return static_cast<int64_t>( std::chrono::system_clock::to_time_t( sys ) );
}

std::string Trim( std::string text ) {
    while ( !text.empty() && ( text.back() == ' ' || text.back() == '\r' || text.back() == '\n' ||
                               text.back() == '\t' || text.back() == '.' ) ) {
        text.pop_back();
    }
    size_t start = 0;
    while ( start < text.size() && ( text[start] == ' ' || text[start] == '\t' ) ) {
        start++;
    }
    return text.substr( start );
}

std::string AvatarLabel( std::string config ) {
    if ( config.rfind( "AVT_", 0 ) == 0 ) {
        config.erase( 0, 4 );
    }
    const size_t intro = config.find( "_Intro" );
    if ( intro != std::string::npos ) {
        config.erase( intro );
    }
    for ( char &c : config ) {
        if ( c == '_' ) {
            c = ' ';
        }
    }
    return config;
}

fs::path ProfilesRoot( const std::string &userData ) {
    return fs::path( userData ) / "profiles";
}

std::string DateText( int64_t t, bool bWithTime ) {
    const time_t tt = static_cast<time_t>( t );
    std::tm tmv = {};
#if defined( _WIN32 )
    localtime_s( &tmv, &tt );
#else
    localtime_r( &tt, &tmv );
#endif
    char buffer[64];
    std::strftime( buffer, sizeof( buffer ), bWithTime ? "%d %b %Y %H:%M" : "%d %b %Y", &tmv );
    return buffer;
}

bool ReadProfileSave( const fs::path &savePath, ProfileInfo &info ) {
    std::string text;
    if ( !ReadText( savePath, text ) ) {
        return false;
    }
    info.bHasSave = true;
    info.LastPlayed = LastWrite( savePath );
    size_t pos = 0;
    while ( pos < text.size() ) {
        const size_t newline = text.find( '\n', pos );
        const std::string line = text.substr( pos, newline == std::string::npos ? std::string::npos : newline - pos );
        pos = newline == std::string::npos ? text.size() : newline + 1;
        const size_t equals = line.find( '=' );
        if ( equals == std::string::npos ) {
            continue;
        }
        const std::string key = Trim( line.substr( 0, equals ) );
        std::string value = line.substr( equals + 1 );
        while ( !value.empty() && ( value.front() == ' ' || value.front() == '\t' ) ) {
            value.erase( 0, 1 );
        }
        while ( !value.empty() && ( value.back() == '\r' || value.back() == ' ' ) ) {
            value.pop_back();
        }
        if ( key == "name" ) {
            info.Name = value;
        } else if ( key == "avatar_config" ) {
            info.Avatar = AvatarLabel( value );
        } else if ( key == "story_step" ) {
            info.StoryStep = std::atoi( value.c_str() );
        } else if ( key == "money" ) {
            info.Money = std::atoll( value.c_str() );
        } else if ( key == "cars" ) {
            info.Cars = std::atoi( value.c_str() );
        } else if ( key == "houses" ) {
            info.Houses = std::atoi( value.c_str() );
        }
    }
    // The New profile dialog writes a name-only save (story step 0) before the
    // story starts: that is still a new game, not progression.
    if ( info.StoryStep < 1 ) {
        info.bHasSave = false;
    }
    return true;
}

std::string UniqueId( const fs::path &root, const std::string &base ) {
    std::string id = base;
    std::error_code ec;
    for ( int suffix = 2; fs::exists( root / id, ec ); suffix++ ) {
        id = base + " " + std::to_string( suffix );
    }
    return id;
}

fs::path BackupFolderOf( const std::string &userData, const ProfileInfo &profile ) {
    return fs::path( userData ) / "backups" / ( profile.bMain ? std::string( "main" ) : profile.Id );
}

// A mod.json string member: "key": "value" (no escapes beyond \" and \\).
std::string JsonString( const std::string &text, const char *key ) {
    const std::string needle = std::string( "\"" ) + key + "\"";
    size_t pos = text.find( needle );
    if ( pos == std::string::npos ) {
        return std::string();
    }
    pos = text.find( ':', pos + needle.size() );
    if ( pos == std::string::npos ) {
        return std::string();
    }
    pos = text.find( '"', pos );
    if ( pos == std::string::npos ) {
        return std::string();
    }
    std::string out;
    for ( size_t i = pos + 1; i < text.size() && text[i] != '"'; i++ ) {
        if ( text[i] == '\\' && i + 1 < text.size() ) {
            i++;
        }
        out += text[i];
    }
    return out;
}

bool IsPlainName( const std::string &name ) {
    return !name.empty() && name != "." && name != ".." && name.find_first_of( "/\\" ) == std::string::npos;
}

} // namespace

std::string StoryStepLabel( int step ) {
    switch ( step ) {
    case 0: return "Prologue";
    case 1: return "First Garage";
    case 4: return "Driving Tess";
    case 6: return "Solar Crown";
    case 7: return "Used Car Dealer";
    default: return "Story step " + std::to_string( step );
    }
}

std::string FormatLastPlayed( int64_t t ) {
    if ( t <= 0 ) {
        return std::string();
    }
    const int64_t now = static_cast<int64_t>( std::time( nullptr ) );
    const int64_t seconds = now - t;
    char buffer[64];
    if ( seconds < 60 ) {
        return "just now";
    }
    if ( seconds < 3600 ) {
        std::snprintf( buffer, sizeof( buffer ), "%d min ago", static_cast<int>( seconds / 60 ) );
        return buffer;
    }
    if ( seconds < 86400 ) {
        const int hours = static_cast<int>( seconds / 3600 );
        std::snprintf( buffer, sizeof( buffer ), "%d hour%s ago", hours, hours == 1 ? "" : "s" );
        return buffer;
    }
    if ( seconds < 172800 ) {
        return "yesterday";
    }
    if ( seconds < 86400 * 30 ) {
        std::snprintf( buffer, sizeof( buffer ), "%d days ago", static_cast<int>( seconds / 86400 ) );
        return buffer;
    }
    return DateText( t, false );
}

std::vector<ProfileInfo> ScanProfiles( const std::string &userData ) {
    std::vector<ProfileInfo> profiles;
    std::error_code ec;
    const fs::path mainSave = fs::path( userData ) / "profile.sav";
    if ( fs::is_regular_file( mainSave, ec ) ) {
        ProfileInfo main;
        main.bMain = true;
        main.Folder = userData;
        main.SavePath = mainSave.string();
        ReadProfileSave( mainSave, main );
        if ( main.Name.empty() ) {
            main.Name = "Main save";
        }
        profiles.push_back( main );
    }
    std::vector<ProfileInfo> folders;
    for ( const auto &entry : fs::directory_iterator( ProfilesRoot( userData ), ec ) ) {
        const std::string name = entry.path().filename().string();
        if ( !entry.is_directory( ec ) || name.empty() || name[0] == '.' ) {
            continue;
        }
        ProfileInfo info;
        info.Id = name;
        info.Name = name;
        info.Folder = entry.path().string();
        info.SavePath = ( entry.path() / "profile.sav" ).string();
        if ( fs::is_regular_file( info.SavePath, ec ) ) {
            ReadProfileSave( info.SavePath, info );
        }
        info.Name = info.Id; // the folder is the profile's name
        folders.push_back( info );
    }
    std::sort( folders.begin(), folders.end(), []( const ProfileInfo &a, const ProfileInfo &b ) {
        if ( a.LastPlayed != b.LastPlayed ) {
            return a.LastPlayed > b.LastPlayed;
        }
        return a.Id < b.Id;
    } );
    profiles.insert( profiles.end(), folders.begin(), folders.end() );
    return profiles;
}

std::string SanitizeProfileName( const std::string &name ) {
    std::string out;
    for ( const char c : name ) {
        if ( c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
             c == '|' || static_cast<unsigned char>( c ) < 32 ) {
            out += '_';
        } else {
            out += c;
        }
    }
    out = Trim( out );
    if ( out.size() > 40 ) {
        out.resize( 40 );
        out = Trim( out );
    }
    return out;
}

bool CreateProfile( const std::string &userData, const std::string &name, std::string &outId, std::string &outError ) {
    const std::string base = SanitizeProfileName( name );
    if ( base.empty() ) {
        outError = "Type a name first.";
        return false;
    }
    const fs::path root = ProfilesRoot( userData );
    outId = UniqueId( root, base );
    std::error_code ec;
    if ( !fs::create_directories( root / outId, ec ) && ec ) {
        outError = "Could not create the profile folder.";
        return false;
    }
    return true;
}

bool DuplicateProfile( const std::string &userData, const ProfileInfo &source, std::string &outId, std::string &outError ) {
    const fs::path root = ProfilesRoot( userData );
    outId = UniqueId( root, ( source.bMain ? std::string( "Main save" ) : source.Id ) + " copy" );
    std::error_code ec;
    fs::create_directories( root / outId, ec );
    if ( fs::is_regular_file( source.SavePath, ec ) ) {
        fs::copy_file( source.SavePath, root / outId / "profile.sav", fs::copy_options::overwrite_existing, ec );
        if ( ec ) {
            outError = "Could not copy the save.";
            return false;
        }
    }
    return true;
}

bool RenameProfile( const std::string &userData, const ProfileInfo &profile, const std::string &newName,
                    std::string &outId, std::string &outError ) {
    if ( profile.bMain ) {
        outError = "The main save cannot be renamed.";
        return false;
    }
    const std::string base = SanitizeProfileName( newName );
    if ( base.empty() ) {
        outError = "Type a name first.";
        return false;
    }
    if ( base == profile.Id ) {
        outId = profile.Id;
        return true;
    }
    const fs::path root = ProfilesRoot( userData );
    outId = UniqueId( root, base );
    std::error_code ec;
    fs::rename( root / profile.Id, root / outId, ec );
    if ( ec ) {
        outError = "Could not rename the folder.";
        return false;
    }
    return true;
}

bool DeleteProfile( const std::string &userData, const ProfileInfo &profile, std::string &outError ) {
    if ( profile.bMain ) {
        outError = "The main save cannot be removed here.";
        return false;
    }
    const fs::path root = ProfilesRoot( userData );
    std::error_code ec;
    fs::create_directories( root / ".trash", ec );
    const std::string target = UniqueId( root / ".trash", profile.Id );
    fs::rename( root / profile.Id, root / ".trash" / target, ec );
    if ( ec ) {
        outError = "Could not move the profile to the trash.";
        return false;
    }
    return true;
}

bool BackupProfile( const std::string &userData, const ProfileInfo &profile ) {
    std::error_code ec;
    if ( !fs::is_regular_file( profile.SavePath, ec ) ) {
        return false;
    }
    std::string current;
    ReadText( profile.SavePath, current );
    const fs::path folder = BackupFolderOf( userData, profile );
    fs::create_directories( folder, ec );
    std::vector<fs::path> existing;
    for ( const auto &entry : fs::directory_iterator( folder, ec ) ) {
        if ( entry.path().extension() == ".sav" ) {
            existing.push_back( entry.path() );
        }
    }
    std::sort( existing.begin(), existing.end() );
    if ( !existing.empty() ) {
        std::string newest;
        if ( ReadText( existing.back(), newest ) && newest == current ) {
            return false; // identical to the newest backup
        }
    }
    const time_t now = std::time( nullptr );
    std::tm tmv = {};
#if defined( _WIN32 )
    localtime_s( &tmv, &now );
#else
    localtime_r( &now, &tmv );
#endif
    char stamp[64];
    std::strftime( stamp, sizeof( stamp ), "profile-%Y%m%d-%H%M%S.sav", &tmv );
    if ( !WriteText( folder / stamp, current ) ) {
        return false;
    }
    existing.push_back( folder / stamp );
    std::sort( existing.begin(), existing.end() );
    while ( existing.size() > 12 ) {
        fs::remove( existing.front(), ec );
        existing.erase( existing.begin() );
    }
    return true;
}

std::vector<BackupInfo> ListBackups( const std::string &userData, const ProfileInfo &profile ) {
    std::vector<BackupInfo> backups;
    std::error_code ec;
    for ( const auto &entry : fs::directory_iterator( BackupFolderOf( userData, profile ), ec ) ) {
        if ( entry.path().extension() != ".sav" ) {
            continue;
        }
        BackupInfo info;
        info.Path = entry.path().string();
        info.Time = LastWrite( entry.path() );
        info.Label = DateText( info.Time, true );
        backups.push_back( info );
    }
    std::sort( backups.begin(), backups.end(),
               []( const BackupInfo &a, const BackupInfo &b ) { return a.Path > b.Path; } );
    return backups;
}

bool RestoreBackup( const std::string &userData, const ProfileInfo &profile, const BackupInfo &backup, std::string &outError ) {
    BackupProfile( userData, profile ); // keep what is there now
    std::error_code ec;
    fs::create_directories( fs::path( profile.SavePath ).parent_path(), ec );
    fs::copy_file( backup.Path, profile.SavePath, fs::copy_options::overwrite_existing, ec );
    if ( ec ) {
        outError = "Could not restore the backup.";
        return false;
    }
    return true;
}

std::vector<ModInfo> ScanMods( const std::string &userData ) {
    std::vector<ModInfo> mods;
    std::error_code ec;
    const fs::path root = fs::path( userData ) / "mods";
    for ( const auto &entry : fs::directory_iterator( root / "packages", ec ) ) {
        const std::string id = entry.path().filename().string();
        if ( !entry.is_directory( ec ) || !IsPlainName( id ) ) {
            continue;
        }
        ModInfo mod;
        mod.Id = id;
        mod.Name = id;
        std::string json;
        bool bImport = false;
        if ( ReadText( entry.path() / "mod.json", json ) ) {
            const std::string name = JsonString( json, "name" );
            if ( !name.empty() ) {
                mod.Name = name;
            }
            mod.Version = JsonString( json, "version" );
            mod.Author = JsonString( json, "author" );
            mod.Description = JsonString( json, "description" );
            bImport = json.find( "\"import\"" ) != std::string::npos;
        }
        for ( const auto &file : fs::recursive_directory_iterator( entry.path(), ec ) ) {
            if ( file.is_regular_file( ec ) && file.path().filename() != "mod.json" ) {
                mod.FileCount++;
            }
        }
        mod.bNeedsImport = bImport && mod.FileCount == 0;
        mods.push_back( mod );
    }
    std::string list;
    std::vector<std::string> enabledOrder;
    if ( ReadText( root / "enabled.txt", list ) ) {
        std::istringstream in( list );
        std::string line;
        while ( std::getline( in, line ) ) {
            line = Trim( line );
            if ( !line.empty() && line[0] != '#' ) {
                enabledOrder.push_back( line );
            }
        }
    }
    std::vector<ModInfo> ordered;
    for ( const std::string &id : enabledOrder ) {
        for ( ModInfo &mod : mods ) {
            if ( mod.Id == id && !mod.bEnabled ) {
                mod.bEnabled = true;
                ordered.push_back( mod );
            }
        }
    }
    std::vector<ModInfo> rest;
    for ( const ModInfo &mod : mods ) {
        if ( !mod.bEnabled ) {
            rest.push_back( mod );
        }
    }
    std::sort( rest.begin(), rest.end(), []( const ModInfo &a, const ModInfo &b ) { return a.Name < b.Name; } );
    ordered.insert( ordered.end(), rest.begin(), rest.end() );
    return ordered;
}

bool SaveEnabledMods( const std::string &userData, const std::vector<ModInfo> &mods ) {
    std::string text =
        "# Written by the OpenTDU2 launcher (Mods page): one mod folder per line, highest priority first.\n";
    for ( const ModInfo &mod : mods ) {
        if ( mod.bEnabled && !mod.bNeedsImport && IsPlainName( mod.Id ) ) {
            text += mod.Id + "\n";
        }
    }
    return WriteText( fs::path( userData ) / "mods" / "enabled.txt", text );
}

} // namespace OTDULauncher

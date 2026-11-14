#include "launcher_profiles.h"

#include "launcher_util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace Launcher {

namespace {

uint64_t ToTicks( const FILETIME &time ) {
    return ( static_cast<uint64_t>( time.dwHighDateTime ) << 32 ) | time.dwLowDateTime;
}

uint64_t LastWriteOf( const std::wstring &path ) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if ( !GetFileAttributesExW( path.c_str(), GetFileExInfoStandard, &data ) ) {
        return 0;
    }
    return ToTicks( data.ftLastWriteTime );
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

std::string DateText( uint64_t ticks, bool bWithTime ) {
    static const char *kMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    FILETIME utc;
    utc.dwLowDateTime = static_cast<DWORD>( ticks & 0xFFFFFFFFu );
    utc.dwHighDateTime = static_cast<DWORD>( ticks >> 32 );
    FILETIME local;
    SYSTEMTIME st = {};
    if ( !FileTimeToLocalFileTime( &utc, &local ) || !FileTimeToSystemTime( &local, &st ) ) {
        return std::string();
    }
    char buffer[64];
    if ( bWithTime ) {
        std::snprintf( buffer, sizeof( buffer ), "%d %s %d %02d:%02d", st.wDay,
                       kMonths[( st.wMonth - 1 ) % 12], st.wYear, st.wHour, st.wMinute );
    } else {
        std::snprintf( buffer, sizeof( buffer ), "%d %s %d", st.wDay,
                       kMonths[( st.wMonth - 1 ) % 12], st.wYear );
    }
    return buffer;
}

std::wstring BackupFolderOf( const std::wstring &userData, const ProfileInfo &profile ) {
    return JoinPath( JoinPath( userData, L"backups" ),
                     profile.bMain ? std::wstring( L"main" ) : Utf8ToWide( profile.Id ) );
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

bool CopyFileTo( const std::wstring &from, const std::wstring &to ) {
    return CopyFileW( from.c_str(), to.c_str(), FALSE ) != FALSE;
}

} // namespace

std::string StoryStepLabel( int step ) {
    // Only the steps the port names are labelled; the rest stay numeric.
    switch ( step ) {
    case 0: return "Prologue";
    case 1: return "First Garage";
    case 4: return "Driving Tess";
    case 6: return "Solar Crown";
    case 7: return "Used Car Dealer";
    default: return "Story step " + std::to_string( step );
    }
}

std::string FormatLastPlayed( uint64_t ticks ) {
    if ( ticks == 0 ) {
        return std::string();
    }
    FILETIME nowFile;
    GetSystemTimeAsFileTime( &nowFile );
    const uint64_t now = ToTicks( nowFile );
    if ( now <= ticks ) {
        return "just now";
    }
    const uint64_t seconds = ( now - ticks ) / 10000000ull;
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
    if ( seconds < 86400ull * 30 ) {
        std::snprintf( buffer, sizeof( buffer ), "%d days ago", static_cast<int>( seconds / 86400 ) );
        return buffer;
    }
    return DateText( ticks, false );
}

std::wstring ProfilesRoot( const std::wstring &userData ) {
    return JoinPath( userData, L"profiles" );
}

bool ReadProfileSave( const std::wstring &savePath, ProfileInfo &info ) {
    std::string text;
    if ( !ReadTextFile( savePath, text ) ) {
        return false;
    }
    info.bHasSave = true;
    info.LastPlayed = LastWriteOf( savePath );
    size_t pos = 0;
    while ( pos < text.size() ) {
        const size_t newline = text.find( '\n', pos );
        std::string line = text.substr(
            pos, newline == std::string::npos ? std::string::npos : newline - pos );
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
    // The game's New profile dialog writes a name-only save (story step 0)
    // before the story starts: that is still a new game, not progression.
    if ( info.StoryStep < 1 ) {
        info.bHasSave = false;
    }
    return true;
}

std::vector<ProfileInfo> ScanProfiles( const std::wstring &userData ) {
    std::vector<ProfileInfo> profiles;

    const std::wstring mainSave = JoinPath( userData, L"profile.sav" );
    if ( FileExists( mainSave ) ) {
        ProfileInfo main;
        main.bMain = true;
        main.Folder = userData;
        main.SavePath = mainSave;
        ReadProfileSave( mainSave, main );
        if ( main.Name.empty() ) {
            main.Name = "Main save";
        }
        profiles.push_back( main );
    }

    std::vector<ProfileInfo> folders;
    const std::wstring root = ProfilesRoot( userData );
    WIN32_FIND_DATAW find = {};
    HANDLE handle = FindFirstFileW( JoinPath( root, L"*" ).c_str(), &find );
    if ( handle != INVALID_HANDLE_VALUE ) {
        do {
            if ( ( find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 || find.cFileName[0] == L'.' ) {
                continue;
            }
            ProfileInfo info;
            info.Id = WideToUtf8( find.cFileName );
            info.Name = info.Id;
            info.Folder = JoinPath( root, find.cFileName );
            info.SavePath = JoinPath( info.Folder, L"profile.sav" );
            std::string savedName;
            if ( FileExists( info.SavePath ) ) {
                ReadProfileSave( info.SavePath, info );
            }
            info.Name = info.Id; // the folder is the profile's name; -playername keeps the save in step
            folders.push_back( info );
        } while ( FindNextFileW( handle, &find ) );
        FindClose( handle );
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
        if ( c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
             c == '>' || c == '|' || static_cast<unsigned char>( c ) < 32 ) {
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
    // reserved device names would make an unusable folder
    std::string lower = ToLowerAscii( out );
    static const char *kReserved[] = { "con", "prn", "aux", "nul", "com1", "com2", "com3",
                                       "lpt1", "lpt2", "lpt3" };
    for ( const char *reserved : kReserved ) {
        if ( lower == reserved ) {
            out += "_";
            break;
        }
    }
    return out;
}

namespace {

// <base>, <base> 2, <base> 3 ... the first folder that does not exist.
std::string UniqueId( const std::wstring &root, const std::string &base ) {
    std::string id = base;
    for ( int suffix = 2; DirectoryExists( JoinPath( root, Utf8ToWide( id ) ) ); suffix++ ) {
        id = base + " " + std::to_string( suffix );
    }
    return id;
}

} // namespace

bool CreateProfile( const std::wstring &userData, const std::string &name, std::string &outId,
                    std::string &outError ) {
    std::string base = SanitizeProfileName( name );
    if ( base.empty() ) {
        outError = "Type a nickname for the new profile first.";
        return false;
    }
    const std::wstring root = ProfilesRoot( userData );
    outId = UniqueId( root, base );
    if ( !MakeDirs( JoinPath( root, Utf8ToWide( outId ) ) ) ) {
        outError = "Could not create the profile folder.";
        return false;
    }
    return true;
}

bool DuplicateProfile( const std::wstring &userData, const ProfileInfo &source,
                       std::string &outId, std::string &outError ) {
    const std::wstring root = ProfilesRoot( userData );
    const std::string base = SanitizeProfileName( source.Name + " copy" );
    outId = UniqueId( root, base.empty() ? std::string( "Player copy" ) : base );
    const std::wstring folder = JoinPath( root, Utf8ToWide( outId ) );
    if ( !MakeDirs( folder ) ) {
        outError = "Could not create the profile folder.";
        return false;
    }
    if ( source.bHasSave && !CopyFileTo( source.SavePath, JoinPath( folder, L"profile.sav" ) ) ) {
        outError = "Could not copy the save.";
        return false;
    }
    return true;
}

bool RenameProfile( const std::wstring &userData, const ProfileInfo &profile,
                    const std::string &newName, std::string &outId, std::string &outError ) {
    if ( profile.bMain ) {
        outError = "The main save cannot be renamed - duplicate it to a named profile instead.";
        return false;
    }
    const std::string base = SanitizeProfileName( newName );
    if ( base.empty() ) {
        outError = "Type a new nickname first.";
        return false;
    }
    if ( base == profile.Id ) {
        outId = profile.Id;
        return true;
    }
    const std::wstring root = ProfilesRoot( userData );
    outId = UniqueId( root, base );
    if ( !MoveFileW( profile.Folder.c_str(), JoinPath( root, Utf8ToWide( outId ) ).c_str() ) ) {
        outError = "Could not rename the folder (is the game running?).";
        return false;
    }
    return true;
}

bool DeleteProfile( const std::wstring &userData, const ProfileInfo &profile,
                    std::string &outError ) {
    if ( profile.bMain ) {
        // The main save is a single file: park it, never remove it.
        const std::wstring trash = JoinPath( ProfilesRoot( userData ), L".trash" );
        MakeDirs( trash );
        SYSTEMTIME st;
        GetLocalTime( &st );
        wchar_t stamp[64];
        swprintf( stamp, 64, L"main-%04d%02d%02d-%02d%02d%02d.sav", st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond );
        if ( !MoveFileW( profile.SavePath.c_str(), JoinPath( trash, stamp ).c_str() ) ) {
            outError = "Could not move the save to the trash folder.";
            return false;
        }
        return true;
    }
    const std::wstring trash = JoinPath( ProfilesRoot( userData ), L".trash" );
    if ( !MakeDirs( trash ) ) {
        outError = "Could not create the trash folder.";
        return false;
    }
    SYSTEMTIME st;
    GetLocalTime( &st );
    wchar_t stamp[64];
    swprintf( stamp, 64, L"-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
              st.wMinute, st.wSecond );
    const std::wstring target = JoinPath( trash, Utf8ToWide( profile.Id ) + stamp );
    if ( !MoveFileW( profile.Folder.c_str(), target.c_str() ) ) {
        outError = "Could not move the profile to the trash folder (is the game running?).";
        return false;
    }
    return true;
}

std::vector<BackupInfo> ListBackups( const std::wstring &userData, const ProfileInfo &profile ) {
    std::vector<BackupInfo> backups;
    const std::wstring folder = BackupFolderOf( userData, profile );
    WIN32_FIND_DATAW find = {};
    HANDLE handle = FindFirstFileW( JoinPath( folder, L"*.sav" ).c_str(), &find );
    if ( handle != INVALID_HANDLE_VALUE ) {
        do {
            BackupInfo info;
            info.Path = JoinPath( folder, find.cFileName );
            info.Size = ( static_cast<uint64_t>( find.nFileSizeHigh ) << 32 ) | find.nFileSizeLow;
            info.Time = ToTicks( find.ftLastWriteTime );
            info.Label = DateText( info.Time, true );
            const std::wstring name = find.cFileName;
            if ( name.find( L"pre-restore" ) != std::wstring::npos ) {
                info.Label += "  (before a restore)";
            }
            backups.push_back( info );
        } while ( FindNextFileW( handle, &find ) );
        FindClose( handle );
    }
    std::sort( backups.begin(), backups.end(),
               []( const BackupInfo &a, const BackupInfo &b ) { return a.Time > b.Time; } );
    return backups;
}

namespace {

bool BackupWithTag( const std::wstring &userData, const ProfileInfo &profile, const wchar_t *tag ) {
    if ( !profile.bHasSave || !FileExists( profile.SavePath ) ) {
        return false;
    }
    std::string current;
    if ( !ReadTextFile( profile.SavePath, current ) ) {
        return false;
    }
    const std::vector<BackupInfo> existing = ListBackups( userData, profile );
    if ( !existing.empty() && existing.front().Size == current.size() ) {
        std::string newest;
        if ( ReadTextFile( existing.front().Path, newest ) &&
            Fnv1a32( newest.data(), newest.size() ) == Fnv1a32( current.data(), current.size() ) ) {
            return false; // nothing changed since the last backup
        }
    }
    const std::wstring folder = BackupFolderOf( userData, profile );
    if ( !MakeDirs( folder ) ) {
        return false;
    }
    SYSTEMTIME st;
    GetLocalTime( &st );
    wchar_t name[96];
    swprintf( name, 96, L"profile-%04d%02d%02d-%02d%02d%02d%ls.sav", st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond, tag );
    if ( !CopyFileTo( profile.SavePath, JoinPath( folder, name ) ) ) {
        return false;
    }
    // keep the newest 12
    std::vector<BackupInfo> all = ListBackups( userData, profile );
    for ( size_t index = 12; index < all.size(); index++ ) {
        DeleteFileW( all[index].Path.c_str() );
    }
    return true;
}

} // namespace

bool BackupProfile( const std::wstring &userData, const ProfileInfo &profile ) {
    return BackupWithTag( userData, profile, L"" );
}

bool RestoreBackup( const std::wstring &userData, const ProfileInfo &profile,
                    const BackupInfo &backup, std::string &outError ) {
    BackupWithTag( userData, profile, L"-pre-restore" );
    if ( !CopyFileTo( backup.Path, profile.SavePath ) ) {
        outError = "Could not copy the backup over the save (is the game running?).";
        return false;
    }
    return true;
}

} // namespace Launcher

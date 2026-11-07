#include "game_installer.h"

#include "iso_archive.h"
#include "launcher_util.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace Launcher {

namespace {

bool IsUnder( const std::string &path, const std::string &prefix ) {
    if ( prefix.empty() ) {
        return true;
    }
    if ( path.size() < prefix.size() || path.compare( 0, prefix.size(), prefix ) != 0 ) {
        return false;
    }
    return path.size() == prefix.size() || path[prefix.size()] == '/';
}

std::string RelativeTo( const std::string &path, const std::string &prefix ) {
    if ( prefix.empty() ) {
        return path;
    }
    if ( path.size() == prefix.size() ) {
        return std::string();
    }
    return path.substr( prefix.size() + 1 );
}

std::wstring ToWindowsPath( const std::string &utf8 ) {
    std::wstring wide = Utf8ToWide( utf8 );
    std::replace( wide.begin(), wide.end(), L'/', L'\\' );
    return wide;
}

} // namespace

uint64_t FreeSpaceOf( const std::wstring &path ) {
    ULARGE_INTEGER freeBytes = {};
    if ( !GetDiskFreeSpaceExW( path.c_str(), &freeBytes, nullptr, nullptr ) ) {
        return 0;
    }
    return freeBytes.QuadPart;
}

IsoInspection InspectIso( const std::wstring &isoPath ) {
    IsoInspection result;
    IsoArchive archive;
    std::string error;
    if ( !archive.Open( isoPath, error ) ) {
        result.Error = error;
        result.Note = "Cannot read this image.";
        return result;
    }
    result.bOpened = true;
    result.Filesystem = archive.Volume().Filesystem;
    result.Label = archive.Volume().Label;
    result.ImageSize = archive.Volume().ImageSize;

    std::string gameRoot;
    bool bPacked = false;
    if ( archive.FindGameRoot( gameRoot, bPacked ) ) {
        result.bHasGameData = true;
        result.GameRoot = gameRoot;
        for ( const IsoFile &file : archive.Files() ) {
            if ( file.bDirectory || !IsUnder( file.Path, gameRoot ) ) {
                continue;
            }
            result.FileCount++;
            result.TotalBytes += file.Size;
        }
        result.Note = "Game data found" +
                      ( gameRoot.empty() ? std::string( " at the image root" )
                                         : ( " in \\" + gameRoot ) ) +
                      " (" + std::to_string( result.FileCount ) + " files, " +
                      FormatBytes( result.TotalBytes ) + ").";
    } else if ( bPacked ) {
        result.bPackedRetail = true;
        result.Note = "This is the retail packed install (bigfile_*.map + TDU2v*.big). "
                      "OpenTDU2 reads the unpacked Euro/Bnk layout only.";
    } else {
        // A retail installer disc: setup.exe / *.msi / data*.cab at the root.
        for ( const IsoFile &file : archive.Files() ) {
            if ( file.bDirectory || file.Path.find( '/' ) != std::string::npos ) {
                continue;
            }
            const std::string leaf = ToLowerAscii( file.Path );
            const bool bExtension =
                ( leaf.size() > 4 && ( leaf.compare( leaf.size() - 4, 4, ".msi" ) == 0 ||
                                       leaf.compare( leaf.size() - 4, 4, ".cab" ) == 0 ) );
            if ( leaf == "setup.exe" || leaf == "autorun.exe" || leaf == "install.exe" ||
                 leaf.rfind( "data", 0 ) == 0 || bExtension ) {
                result.bLooksLikeInstaller = true;
                break;
            }
        }
        result.Note = result.bLooksLikeInstaller
                          ? "This looks like the original installer disc. Install the game "
                            "once with its own setup, then point the launcher at the "
                            "installed Test Drive Unlimited 2 folder."
                          : "No Euro/Bnk folder in this image.";
    }
    return result;
}

bool ExtractGameFromIso( const std::wstring &isoPath, const std::string &gameRoot,
                         const std::wstring &destination, InstallProgress &progress ) {
    progress.bRunning = true;
    progress.bDone = false;
    progress.bOk = false;
    progress.BytesDone = 0;
    progress.FilesDone = 0;
    progress.FilesSkipped = 0;
    progress.SetError( std::string() );

    IsoArchive archive;
    std::string error;
    if ( !archive.Open( isoPath, error ) ) {
        progress.SetError( error );
        progress.bDone = true;
        progress.bRunning = false;
        return false;
    }

    std::vector<const IsoFile *> files;
    uint64_t totalBytes = 0;
    for ( const IsoFile &file : archive.Files() ) {
        if ( file.bDirectory || !IsUnder( file.Path, gameRoot ) ) {
            continue;
        }
        files.push_back( &file );
        totalBytes += file.Size;
    }
    progress.BytesTotal = totalBytes;
    progress.FilesTotal = static_cast<uint32_t>( files.size() );

    if ( files.empty() ) {
        progress.SetError( "The image holds no game files to extract." );
        progress.bDone = true;
        progress.bRunning = false;
        return false;
    }
    if ( !MakeDirs( destination ) ) {
        progress.SetError( "Cannot create the destination folder." );
        progress.bDone = true;
        progress.bRunning = false;
        return false;
    }
    const uint64_t freeBytes = FreeSpaceOf( destination );
    if ( freeBytes != 0 && totalBytes > freeBytes ) {
        progress.SetError( "Not enough free space: the game needs " +
                           FormatBytes( totalBytes ) + ", the destination has " +
                           FormatBytes( freeBytes ) + "." );
        progress.bDone = true;
        progress.bRunning = false;
        return false;
    }

    std::set<std::wstring> createdDirectories;
    std::vector<uint8_t> buffer( 1024 * 1024 );
    bool bOk = true;

    for ( const IsoFile *file : files ) {
        if ( progress.bCancel ) {
            progress.SetError( "Extraction cancelled." );
            bOk = false;
            break;
        }
        const std::string relative = RelativeTo( file->Path, gameRoot );
        if ( relative.empty() ) {
            continue;
        }
        progress.SetCurrentFile( relative );
        const std::wstring destPath = JoinPath( destination, ToWindowsPath( relative ) );
        const std::wstring parent = ParentDir( destPath );
        if ( createdDirectories.insert( parent ).second ) {
            MakeDirs( parent );
        }

        if ( FileExists( destPath ) && FileSizeOf( destPath ) == file->Size ) {
            progress.FilesSkipped++;
            progress.FilesDone++;
            progress.BytesDone += file->Size;
            continue;
        }

        FILE *out = _wfopen( destPath.c_str(), L"wb" );
        if ( out == nullptr ) {
            progress.SetError( "Cannot write " + WideToUtf8( destPath ) );
            bOk = false;
            break;
        }
        uint64_t offset = 0;
        bool bFileOk = true;
        while ( offset < file->Size ) {
            if ( progress.bCancel ) {
                bFileOk = false;
                break;
            }
            const uint32_t want = static_cast<uint32_t>(
                std::min<uint64_t>( buffer.size(), file->Size - offset ) );
            const uint32_t read = archive.ReadFile( *file, offset, buffer.data(), want );
            if ( read == 0 ) {
                bFileOk = false;
                break;
            }
            if ( fwrite( buffer.data(), 1, read, out ) != read ) {
                bFileOk = false;
                break;
            }
            offset += read;
            progress.BytesDone += read;
        }
        fclose( out );
        if ( !bFileOk ) {
            DeleteFileW( destPath.c_str() );
            if ( progress.bCancel ) {
                progress.SetError( "Extraction cancelled." );
            } else {
                progress.SetError( "Read error on " + relative );
            }
            bOk = false;
            break;
        }
        progress.FilesDone++;
    }

    progress.bOk = bOk;
    progress.bDone = true;
    progress.bRunning = false;
    progress.SetCurrentFile( std::string() );
    return bOk;
}

} // namespace Launcher


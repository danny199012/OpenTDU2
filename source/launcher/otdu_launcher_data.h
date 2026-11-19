#pragma once

// Profiles and mods for the integrated launcher (Android, and -launcher on
// desktop). A portable (std::filesystem, UTF-8) counterpart of
// tools/launcher/launcher_profiles.* and launcher_mods.*, which are Win32
// wstring code; the on-disk layout is identical so a user data folder moves
// between the PC and the phone unchanged:
//   <userdata>/profile.sav                  the legacy "Main save"
//   <userdata>/profiles/<id>/profile.sav    one save per profile
//   <userdata>/backups/<id|main>/profile-YYYYMMDD-HHMMSS.sav
//   <userdata>/mods/packages/<id>/mod.json  a mod; enabled ones in mods/enabled.txt

#include <cstdint>
#include <string>
#include <vector>

namespace OTDULauncher {

struct ProfileInfo {
    std::string Id;      // folder name under profiles/ ; empty = the main save
    std::string Name;    // nickname in the save (the folder name for folders)
    std::string Avatar;
    int StoryStep = 0;
    int64_t Money = 0;
    int Cars = 0;
    int Houses = 0;
    bool bHasSave = false;
    bool bMain = false;
    int64_t LastPlayed = 0; // unix seconds, 0 = never
    std::string SavePath;
    std::string Folder;
};

struct BackupInfo {
    std::string Path;
    std::string Label;
    int64_t Time = 0;
};

struct ModInfo {
    std::string Id;
    std::string Name;
    std::string Version;
    std::string Author;
    std::string Description;
    bool bEnabled = false;
    bool bNeedsImport = false; // an import mod with nothing imported yet (needs the PC launcher)
    int FileCount = 0;
};

std::string StoryStepLabel( int step );
std::string FormatLastPlayed( int64_t unixSeconds );

std::vector<ProfileInfo> ScanProfiles( const std::string &userData );
std::string SanitizeProfileName( const std::string &name );
bool CreateProfile( const std::string &userData, const std::string &name, std::string &outId, std::string &outError );
bool DuplicateProfile( const std::string &userData, const ProfileInfo &source, std::string &outId, std::string &outError );
bool RenameProfile( const std::string &userData, const ProfileInfo &profile, const std::string &newName,
                    std::string &outId, std::string &outError );
// Moves the folder to profiles/.trash (recoverable by hand), never a hard delete.
bool DeleteProfile( const std::string &userData, const ProfileInfo &profile, std::string &outError );
bool BackupProfile( const std::string &userData, const ProfileInfo &profile );
std::vector<BackupInfo> ListBackups( const std::string &userData, const ProfileInfo &profile );
bool RestoreBackup( const std::string &userData, const ProfileInfo &profile, const BackupInfo &backup,
                    std::string &outError );

std::vector<ModInfo> ScanMods( const std::string &userData );
bool SaveEnabledMods( const std::string &userData, const std::vector<ModInfo> &mods );

} // namespace OTDULauncher

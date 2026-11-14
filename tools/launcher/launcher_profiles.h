#pragma once

// The launcher's profile manager: several players / avatars, each with its own
// save. A profile is a folder <userdata>\profiles\<id>\ holding profile.sav;
// the game is pointed at it with -savefile. Options, shader caches and mods
// stay shared in the user data folder. The pre-existing <userdata>\profile.sav
// is listed as the "Main save" (id empty) - nothing is migrated or moved.

#include <cstdint>
#include <string>
#include <vector>

namespace Launcher {

struct ProfileInfo {
    std::string Id;       // folder name under profiles\ ; empty = the main save
    std::string Name;     // the nickname in the save (the folder name when unsaved)
    std::string Avatar;   // "Male" / "Female" ... (avatar_config without the noise)
    int StoryStep = 0;
    int64_t Money = 0;
    int Cars = 0;
    int Houses = 0;
    bool bHasSave = false;       // profile.sav exists (false: a new, empty profile)
    bool bMain = false;          // the legacy <userdata>\profile.sav
    uint64_t LastPlayed = 0;     // profile.sav write time, FILETIME ticks (0 = never)
    std::wstring SavePath;       // full path of profile.sav (exists or not)
    std::wstring Folder;         // the profile's folder
};

// "Prologue", "First Garage" ... or "Story step N" for the steps not named.
std::string StoryStepLabel( int step );
// "3 hours ago", "yesterday", "12 Nov 2026" - empty for 0.
std::string FormatLastPlayed( uint64_t fileTime );

std::wstring ProfilesRoot( const std::wstring &userData );
// Main save (when profile.sav exists) first, then every profiles\ subfolder
// sorted by last played, newest first.
std::vector<ProfileInfo> ScanProfiles( const std::wstring &userData );
// Fills the summary from a profile.sav (name, avatar, story step, money...).
bool ReadProfileSave( const std::wstring &savePath, ProfileInfo &info );

// A folder-safe id from a nickname ("Sam / Tess" -> "Sam _ Tess").
std::string SanitizeProfileName( const std::string &name );
// Creates the folder; outId gets the (unique) id. false + error on failure.
bool CreateProfile( const std::wstring &userData, const std::string &name, std::string &outId,
                    std::string &outError );
bool DuplicateProfile( const std::wstring &userData, const ProfileInfo &source,
                       std::string &outId, std::string &outError );
bool RenameProfile( const std::wstring &userData, const ProfileInfo &profile,
                    const std::string &newName, std::string &outId, std::string &outError );
// Moves the folder to profiles\.trash (recoverable by hand), never a hard delete.
bool DeleteProfile( const std::wstring &userData, const ProfileInfo &profile,
                    std::string &outError );

// Backups: <userdata>\backups\<id or "main">\profile-YYYYMMDD-HHMMSS.sav.
struct BackupInfo {
    std::wstring Path;
    std::string Label;   // "14 Nov 2026 04:36"
    uint64_t Size = 0;
    uint64_t Time = 0;
};
// Copies profile.sav into the backups folder unless the newest backup is
// identical; keeps the newest 12. Returns true when a copy was made.
bool BackupProfile( const std::wstring &userData, const ProfileInfo &profile );
std::vector<BackupInfo> ListBackups( const std::wstring &userData, const ProfileInfo &profile );
// Backs the current save up first ("pre-restore"), then copies the chosen backup over it.
bool RestoreBackup( const std::wstring &userData, const ProfileInfo &profile,
                    const BackupInfo &backup, std::string &outError );

} // namespace Launcher

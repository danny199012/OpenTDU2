#pragma once

// The launcher's mod manager (docs/re/dev_build_survey.md 4). A mod is a folder
// <userdata>\mods\packages\<id>\ holding a mod.json and, optionally, files that
// mirror the game layout (they shadow the game file of the same path). The
// enabled mods are listed, in priority order, in <userdata>\mods\enabled.txt,
// which the game reads at start (source/filesystem/gs_file.cpp).
//
// The project never ships Eden / Atari data. A mod either carries only its
// authors' own files, or declares "import": the files it needs are copied
// from a folder the user already owns (e.g. their own Test Drive Unlimited 2
// Dev Build) into the mod's folder on this computer.

#include <string>
#include <vector>

namespace Launcher {

struct ModInfo {
    std::string Id;           // folder name under packages (one path component)
    std::string Name;
    std::string Version;
    std::string Author;
    std::string Description;
    std::string ImportPrompt; // what folder to pick for an "import" mod
    std::vector<std::string> ImportFiles; // plain file names copied from the user's folder
    bool bEnabled = false;
    int FileCount = 0;        // files inside the mod's folder (without mod.json)
    std::wstring Folder;
    // An import mod with nothing imported yet cannot be enabled.
    bool NeedsImport() const { return !ImportFiles.empty() && FileCount == 0; }
};

std::wstring ModsRoot( const std::wstring &userData );
// Creates the bundled import mod(s) (mod.json only, no game data) when missing.
void EnsureBundledMods( const std::wstring &userData );
// Enabled mods first, in the order of enabled.txt, then the others by name.
std::vector<ModInfo> ScanMods( const std::wstring &userData );
// Writes enabled.txt (ids in priority order). False on a write failure.
bool SaveEnabledMods( const std::wstring &userData, const std::vector<ModInfo> &mods );
// Copies the mod's ImportFiles from `sourceFolder` into the mod's folder. Only
// plain file names, only the declared ones; false + error when one is missing.
bool ImportModFiles( const ModInfo &mod, const std::wstring &sourceFolder, std::string &error );
// A mod id / file name is one path component: no separators, no "..", not empty.
bool IsPlainName( const std::string &name );

} // namespace Launcher

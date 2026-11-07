#pragma once

#include <string>
#include <vector>

// Human labels for the game's assets, shared by the owner and the agents.
//
// The game names most assets by an opaque leaf (`fla_0047.2db`, `MESH_00A1`),
// but every bank entry already carries its original Eden authoring path
// (`BankEntry::Path`, e.g.
// `D:\Eden-Prog\...\EURO\FrontEnd\Cas_SlotMachine\fla_0047.2db`). The asset
// viewer (`-gm ASSET_VIEWER`) shows both and lets a human write the *meaning*
// down; this is where it goes:
//
//   docs/re/asset_labels.txt
//
// One asset per line, TAB separated, sorted by bank then asset so two people
// can merge it:
//
//   # comment
//   <bank, lowercase, '/' separators, relative to the game root> TAB
//   <asset filename with extension> TAB <label> TAB <note>
//
// The key is the *identity* (bank + filename), never a display name, so a label
// cannot drift onto another asset - and `-labellint` checks every key against
// the game data (a bank that is missing, or a filename that is not in it, is
// reported). That is the accuracy check: a label either points at a real asset
// or it is flagged.
struct AssetLabel {
    std::string Bank;  // normalized key
    std::string Asset; // filename with extension, as it is in the bank
    std::string Label; // "slot machine reel symbol"
    std::string Note;  // free text
};

// The file in use: `docs/re/asset_labels.txt` when the working directory has a
// `docs/re` (the launchers cd to the repo root), else
// `<userdata>/asset_labels.txt`.
const std::string &GetAssetLabelPath();
const std::vector<AssetLabel> &GetAssetLabels();

// Reads the file once (idempotent); logs the path and the count.
bool LoadAssetLabels();
// Writes the whole file back, sorted by bank then asset. True on success.
bool SaveAssetLabels();

// The label of an asset, or nullptr. The bank is normalized (lowercase, '/'),
// so callers may pass either separator or casing.
const AssetLabel *FindAssetLabel( const char *pBank, const char *pAsset );
// Sets a label (an empty label removes the entry). True when something changed.
bool SetAssetLabel( const char *pBank,
                    const char *pAsset,
                    const std::string &label,
                    const std::string &note );
// The label, or the asset name when there is none: what a log or a dump prints.
const char *AssetLabelOrName( const char *pBank, const char *pAsset );

// Normalizes a bank path to the key form (lowercase, '/', no leading slash).
std::string NormalizeAssetBank( const char *pBank );

// `-labellint`: every key must exist in the game data. Logs each problem and
// returns how many there were (0 = the file is accurate).
int32_t LintAssetLabels();

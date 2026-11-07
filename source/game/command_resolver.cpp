#include "shared.h"

#include "command_resolver.h"

#include "core/xmb.h"
#include "flash/flash_player.h"
#include "system/cmd_line.h"

#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace {
// Default_KB.xmb (PlayerKeyMapFile.KeyMap[]): ActionName -> its single key
// bindings {ChannelName, ChannelLocName} in file order (a ChannelNameAND
// combo keeps its ChannelLocName, e.g. "MAJ + F1"; it is not a key press).
// OpenTDU2 user-requested bindings (NOT game data): put before the
// Default_KB.xmb entries of the same action, so the #command: tip shows them
// (FUN_00d87cc0 takes the first entry of the action); the Default_KB keys
// keep working.
// - ACCESSORIES_LEFT (open the car door, Default_KB PAD_4 "4 (PAVE NUM.)")
//   also on the main-row 4 key, for keyboards without a numpad (Mac). The
//   channel / loc names follow the game's own main-row digit entries:
//   channel table at 0x00f73b08 (32-byte names indexed by DirectInput key
//   code, DIK_4 = 5 -> "4") [DATA], and Default_KB RADIO_CHANNEL /
//   RADIO_VOL_LESS / RADIO_VOL_MORE = "1" / "2" / "3" with ChannelLocName
//   "1" / "2" / "3" [DATA]. No Default_KB.xmb action uses the main-row "4".
struct UserBinding {
    const char *pAction;
    const char *pChannel;
    const char *pLocName;
};
constexpr UserBinding kUserBindings[] = { { "ACCESSORIES_LEFT", "4", "4" } };

std::unordered_map<std::string, std::vector<CommandKeyBinding>> &MutableKeyMap() {
    static std::unordered_map<std::string, std::vector<CommandKeyBinding>> map;
    static bool bLoaded = false;
    if ( !bLoaded ) {
        bLoaded = true;
        for ( const UserBinding &user : kUserBindings ) {
            map[user.pAction].push_back( CommandKeyBinding{ user.pChannel, user.pLocName } );
        }
        XmbDocument document;
        if ( LoadXmbFromFile( "Default_KB.xmb", document ) ) {
            XmbValue root = document.getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "KeyMap" ); i++ ) {
                XmbValue key = root.getArrayElement( "KeyMap", i );
                const std::string action = key["ActionName"].asString();
                const std::string channel = key["ChannelName"].asString();
                if ( channel.empty() ) {
                    continue;
                }
                map[action].push_back( CommandKeyBinding{ channel, key["ChannelLocName"].asString() } );
            }
        }
    }
    return map;
}

// DeviceX360Pad.xmb (the pad key map, DevSubType 3): ActionName -> the
// ChannelName of its first single-channel entry (FUN_00d87cc0 takes the
// first entry of the action).
const std::unordered_map<std::string, std::string> &PadKeyMap() {
    static std::unordered_map<std::string, std::string> map;
    static bool bLoaded = false;
    if ( !bLoaded ) {
        bLoaded = true;
        XmbDocument document;
        if ( LoadXmbFromFile( "DeviceX360Pad.xmb", document ) ) {
            XmbValue root = document.getRoot();
            for ( uint32_t i = 0; i < root.getArraySize( "KeyMap" ); i++ ) {
                XmbValue key = root.getArrayElement( "KeyMap", i );
                const std::string channel = key["ChannelName"].asString();
                if ( channel.empty() || !key["ChannelNameAND"].asString().empty() ) {
                    continue;
                }
                map.emplace( key["ActionName"].asString(), channel );
            }
        }
    }
    return map;
}

// The pad form of FUN_00d87cc0 (OpenTDU2: the prompts follow the Button
// prompts option / the device in use, FlashPlayer::SetPromptPlatform [INF]):
// the x360 bitmap of FUN_00d877b0, or its PS3_* twin. Empty when the action
// has no pad entry (the keyboard form is used instead).
std::string ResolvePadCommand( const std::string &action, bool bPS3 ) {
    // LEFT_PAD / RIGHT_PAD: the movement / look sticks (AVT_MOVE_* /
    // AVT_LOOK_* on the pad) [CODE].
    if ( action == "LEFT_PAD" ) {
        return bPS3 ? "#bitmap:PS3_L3#" : "#bitmap:x360_LS#";
    }
    if ( action == "RIGHT_PAD" ) {
        return bPS3 ? "#bitmap:PS3_R3#" : "#bitmap:x360_RS#";
    }
    const auto &map = PadKeyMap();
    auto it = map.find( action );
    if ( it == map.end() ) {
        return "";
    }
    // The D-pad as a numbered button or a POV hat (the names GSInput reads
    // from the same file) -> the UP / DOWN / LEFT / RIGHT of the table [INF].
    std::string channel = it->second;
    static const char *const kDPad[][2] = {
        { "BUTTON_12", "UP" },  { "BUTTON_13", "DOWN" },  { "BUTTON_14", "LEFT" },  { "BUTTON_15", "RIGHT" },
        { "POV_0_UP", "UP" },   { "POV_0_DOWN", "DOWN" }, { "POV_0_LEFT", "LEFT" }, { "POV_0_RIGHT", "RIGHT" },
    };
    for ( const auto &entry : kDPad ) {
        if ( channel == entry[0] ) {
            channel = entry[1];
        }
    }
    const std::string markup = FlashPlayer::PadChannelMarkup( channel ); // "#bitmap:x360_..#"
    if ( markup.size() < 10 || !bPS3 ) {
        return markup;
    }
    return "#bitmap:" + FlashPlayer::PS3BitmapName( markup.substr( 8, markup.size() - 9 ) ) + "#";
}

// '#command:<ACTION>#' (FUN_00d87cc0): keyboard form
// "#color:ff6600#[<ChannelLocName>]#endcolor#" of the first key map entry
// of the action (docs/re/text_icons_spec.md 1 [CODE]); with the Xbox or
// PlayStation prompts, the icon of its DeviceX360Pad.xmb entry.
std::string ResolveCommand( const std::string &action ) {
    const int32_t platform = FlashPlayer::GetPromptPlatform();
    if ( platform == 0 || platform == 2 ) {
        const std::string pad = ResolvePadCommand( action, platform == 2 );
        if ( !pad.empty() ) {
            return pad;
        }
    }
    auto &map = MutableKeyMap();
    auto it = map.find( action );
    if ( it == map.end() || it->second.empty() ) {
        return "";
    }
    const CommandKeyBinding &binding = it->second.front();
    const std::string &name = binding.LocName.empty() ? binding.Channel : binding.LocName;
    return "#color:ff6600#[" + name + "]#endcolor#";
}
} // namespace

const std::vector<CommandKeyBinding> &GetActionBindings( const std::string &action ) {
    static const std::vector<CommandKeyBinding> empty;
    auto &map = MutableKeyMap();
    auto it = map.find( action );
    return it != map.end() ? it->second : empty;
}

void SetDefaultCommandResolver() {
    FlashPlayer::SetCommandResolver( ResolveCommand );
}

void ClearDefaultCommandResolver() {
    FlashPlayer::SetCommandResolver( nullptr );
}

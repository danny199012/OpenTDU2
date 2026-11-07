#pragma once

#include <string>
#include <vector>

// The '#command:<ACTION>#' resolver of read only text fields
// (flash_player.cpp FUN_00849dd0): the action name becomes the coloured key
// name of its first Default_KB.xmb binding ("#color:ff6600#[B]#endcolor#",
// FUN_00d87cc0). Shared by the game modes that show command tips (the first
// walk mode doors, the FREERIDE back on track prompt); the resolver is global
// (FlashPlayer::SetCommandResolver) and each mode sets / clears it on
// enter / exit.
void SetDefaultCommandResolver();
void ClearDefaultCommandResolver();

// The Default_KB.xmb bindings of an action (file order, the OpenTDU2 user
// bindings first): channel name ("PAD_4", "B") and its localized name
// ("4 (PAVE NUM.)", "B"). The walk mode uses the channels to read the keys.
struct CommandKeyBinding {
    std::string Channel;
    std::string LocName;
};
const std::vector<CommandKeyBinding> &GetActionBindings( const std::string &action );

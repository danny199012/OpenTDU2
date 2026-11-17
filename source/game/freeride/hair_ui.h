#pragma once

#include <cstdint>
#include <string>

class FlashPlayer;

// The retail hairdresser screen: SHOPSAVATAR.vmf's hairdresser page (root labels
// hairdresser / hairdresser_stop / hairdresser_edit / close_hairdresser) driven
// by its own scripts; this class is the game's half of the `hadr:` FSCommand
// protocol [DATA: shopsavatar.vmf AVM1, docs/re/hairdresser_spec.md].
class HairUi {
public:
    void bind( FlashPlayer *pNewUi );
    inline bool isBound() const { return pUi != nullptr; }
    // Returns true when the command was a hadr: one.
    bool onCommand( const std::string &command );

private:
    FlashPlayer *pUi = nullptr;
};

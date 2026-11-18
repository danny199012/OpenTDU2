#pragma once

#include <cstdint>
#include <string>

class FlashPlayer;

// The retail tuning shop screen: SHOPSCAR.vmf's car_tuner page (root labels
// after_mkt -> tuner_stop -> tuner_edit -> tuner_block -> tuner_close) driven by
// its own scripts. This class is the game's half of the protocol [DATA: the AVM1
// of shopscar.vmf, docs/re/tuning_ui_spec.md]:
//   afmk            the page opened (label after_mkt): the car, the money, the squares
//   camn            the tuner's A (ItemType valid) - the squares menu opens
//   amtv            a square was chosen: /:UpgType = acceleration | top_speed | braking
//                   (the movie then calls setUpgList: it reads the four lines'
//                   /:LockUpgLevel<n> / /:UpgName<n> / /:UpgPrice<n>)
//   kida            the focus moved to another line (/:numUpl): the gauges preview it
//   evmo            A pressed: the game answers /:EnoughMoney (and /:already_buyed)
//   kiva            the "buy this upgrade?" dialog was confirmed: buy line /:UpgradeSelected
//   tune:rims:open  the Wheels page opened: /:num_item, /:nb_items, /:option_name, /:cur_op_price
//   chop            the rim choice moved (/:num_item changed)
//   tune:rims:vali  the rims dialog was confirmed: fit rim number /:num_item
//   tuci            the menu closed after a purchase (retail plays C_BUY_PART, not played yet)
// The levels are the port's own 0..3 per stat (PlayerProfile::OwnedCarTuning, the DBRewardCar
// range) - the movie draws four lines per stat, the fourth is shown locked.
class TuneUi {
public:
    // pUi: the movie (null unbinds). carId: the car the player drove in with.
    void bind( FlashPlayer *pNewUi, int32_t newCarId, const std::string &newCarName );
    void unbind() { pUi = nullptr; }
    inline bool isBound() const { return pUi != nullptr; }
    // Returns true when the command was one of the tuner page's.
    bool onCommand( const std::string &command );

private:
    void pushCar();
    void pushMoney();
    void pushSquares();
    void pushLines( int32_t stat );
    void pushPreview( int32_t stat, int32_t previewLevel );
    void pushRims();
    void pushRimText();
    int32_t statOf( const std::string &upgType ) const;
    int32_t levelOf( int32_t stat ) const;
    void buyLine( int32_t line );
    void fitRim();
    int32_t rimSizeOfItem( int32_t item ) const;
    std::string formatMoney( int32_t value ) const;

    FlashPlayer *pUi = nullptr;
    int32_t carId = -1;
    std::string carName;
    int32_t stat = 0; // the stat page up: 0 acceleration, 1 top speed, 2 braking
    bool bRimsPage = false; // the Wheels page is up (its A / chop answer the rim choice, not a level line)
    int32_t rimMin = 0, rimMax = 0, rimStock = 0;
};

#pragma once

#include "game/car_shop.h"
#include "game/cinematic/cinematic_car.h"

#include <string>
#include <vector>

class Renderer2D;
class Font;

// The test-drive pieces takeTestDrive hands over (the caller fills
// TestDriveRequest with them).
struct DealerTestDrive {
    int32_t CarId = -1;
    int32_t PaintIndex = -1;    // DBCarsAndColors row (-1: the model's paint)
    std::string ColorId;        // the row's _color_id_1 ("" with -1)
    uint64_t RimsHashcode = 0;  // the car's _default_rims (DBRims _hashcode)
};

// The car dealership menu (native; the retail one is the 1.3 MB shopscar movie
// with ~100 FSCommands, not driven). Opens at a dealer building, lists the
// DBCarShop cars with their DBCarPhysicsData prices, buys with the profile's
// money and pays the COLLECTION_CARS_DIF_<n> events [DATA].
//
// The retail presentation (the used-car-dealer screen, FUN_009f2c90's mini
// menu): the selected car's spec panel beside the list (Acceleration / Speed /
// Braking bars on the retail gauge ranges, the difficulty wheels, top speed /
// 0-100 / bhp), validating opens the entries Buy / Get into the car / Data /
// Compare / Test drive. Data is the full data sheet, Compare records the car
// the others are shown against (FUN_00b95840's CompareRecord) and Test drive
// hands the car to GM_TestCar (the freeride dealer passes a return to
// FREERIDE; the story shop keeps its own ReturnMode -1).
class DealerMenu {
public:
    void open( const CarShop *pShop );
    void close();
    inline bool isOpen() const { return pShop != nullptr; }
    // Keys: up / down select (car list, entries, colours), validate acts, back
    // steps back / closes. Call every frame while open.
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );

    // A purchase the caller has to deliver: the `_id_car` bought since the last
    // call (false when there is none). The shop hands the keys over - the car
    // is parked outside (CarDelivery, GMFreeRide::deliverBoughtCar).
    bool takeBoughtCar( int32_t &outCarId );
    // The car / colour to show, when it changed since the last call: outCarId
    // the selected car, outPaintIndex its DBCarsAndColors row in the colour
    // / entry steps (-1 in the car list: keep the showroom paint).
    bool takePreview( int32_t &outCarId, int32_t &outPaintIndex );
    // A test drive the caller has to start: the selected car, its colour and
    // default rims (false when none was requested). The caller fills
    // TestDriveRequest (player_profile.h) and switches to GM_TestCar.
    bool takeTestDrive( DealerTestDrive &out );
    // Inside a showroom: the panel at the right, the scene undimmed.
    void setSidePanel( bool bSide ) { bSidePanel = bSide; }

private:
    enum class eStep { Cars, Entries, Colors, Data, View };
    enum class eEntry { Buy, EnterCar, Data, Compare, TestDrive, Count };

    const ShopCar *selectedCar() const;
    bool canBuySelected();
    void chooseColour();
    void buySelected( int32_t paintIndex );
    void validateEntry();
    // The record the spec panel / data sheet compare against (Compare).
    void recordCompare();

    // draw() pieces (the retail panels, native Renderer2D).
    void drawSpecPanel( Renderer2D &renderer, const Font *pFont, float x, float y, float w, const ShopCar &car,
                        const CarSpec &spec, bool bWithCompare ) const;
    void drawEntryList( Renderer2D &renderer, const Font *pFont ) const;
    void drawDataSheet( Renderer2D &renderer, const Font *pFont, float x, float y, float w, float h,
                        const ShopCar &car, const CarSpec &spec ) const;

    const CarShop *pShop = nullptr;
    int32_t selected = 0;
    eStep step = eStep::Cars;
    eEntry selectedEntry = eEntry::Buy;
    std::vector<CinematicCar::PaintInfo> paints; // the selected car's catalogue (colour step)
    int32_t selectedPaint = 0;
    bool bPreviewDirty = false;
    bool bSidePanel = false;
    int32_t boughtCarId = -1; // takeBoughtCar
    bool bTestDrive = false;  // takeTestDrive
    DealerTestDrive testDrive;
    std::string message;
    float messageTimer = 0.0f;
    // The compare base (the retail record survives the screen; here it lives
    // as long as the menu is open).
    bool bCompareValid = false;
    std::string compareName;
    int32_t comparePrice = 0;
    CarSpec compareSpec;
};

// The shop of a dealer building (spot name ECD_AUDI, ECD_ITAL ...) on an island.
const CarShop *FindShopForSpot( const std::string &spotName, const std::string &island );

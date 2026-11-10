#include "shared.h"
#include "cheats.h"

#include "debug/gs_imgui.h"
#include "game/car_shop.h"
#include "game/clothes_shop.h"
#include "game/player_profile.h"
#include "game/skill_levels.h"
#include "game/units.h"
#include "system/cmd_line.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
// "$1,234,567": the same shape DealerMenu::FormatMoney draws with.
std::string FormatMoney( int32_t value ) {
    const bool bNegative = value < 0;
    const std::string digits =
        std::to_string( bNegative ? -static_cast<int64_t>( value ) : value );
    std::string out;
    for ( size_t i = 0; i < digits.size(); i++ ) {
        if ( i > 0 && ( digits.size() - i ) % 3 == 0 ) {
            out += ',';
        }
        out += digits[i];
    }
    return ( bNegative ? "-$" : "$" ) + out;
}

// The wallet fields are int32: keep a cheat from wrapping them.
int32_t ClampMoney( int64_t value ) {
    return static_cast<int32_t>( Clamp<int64_t>( value, 0, 999999999 ) );
}

// `-cheatspanel`: set by applyCommandLine (before any mode exists), read by the
// constructors, so the panel is up from the first frame in every mode.
bool gbOpenFromCommandLine = false;

// "CARS ON STOCK": the dealer menu treats every DBCarShop row as buyable
// (DealerMenu::buySelected reads it through Cheats::isAllCarsBuyable).
bool gbAllCarsBuyable = false;

// `-cheat houses` / `-cheat allroads` / `-cheat allspots`: the rows that need
// the mode's world (the island's spots, the map's road graph).
// applyCommandLine runs before any mode exists, so it flags them and the
// constructors pick the flags up.
bool gbPendingAllHousesFromCommandLine = false;
bool gbPendingAllRoadsFromCommandLine = false;
bool gbPendingAllSpotsFromCommandLine = false;

// The quick destinations of the teleport row. The bank names are the game's
// own (spots.bin: EAIR_A the airport, EGAMHA the gambling hall, ECD_FERR the
// Ferrari dealer...); the label is the port's. setSpots keeps the first spot
// whose bank starts with the prefix, so the row resolves on every island.
struct LandmarkDestination {
    const char *Name;
    const char *BankPrefix;
};
const LandmarkDestination kLandmarks[] = {
    { "Airport", "EAIR" },
    { "Casino", "EGAMHA" },
    { "Ferrari dealer", "ECD_FERR" },
    { "Any dealer", "ECD_" },
    { "Tuning shop", "ETUN_A" },
    { "Car wash", "ECW_A" },
    { "Race school", "ERS_A" },
    { "Clothes shop", "EAS_A" },
};

// `-tpdestination <name>`: a quick destination of the row, teleported to once
// the island's spots are in (the same request the button makes; it is how a
// headless run proves the row resolves to a real place).
std::string gsPendingDestination;
} // namespace

Cheats::Cheats() : bOpen( gbOpenFromCommandLine ) {
    bPendingAllHouses = gbPendingAllHousesFromCommandLine;
    bPendingAllRoads = gbPendingAllRoadsFromCommandLine;
    bPendingAllSpots = gbPendingAllSpotsFromCommandLine;
}

void Cheats::setSpots( std::vector<Spot> newSpots, const std::string &newIsland ) {
    spots = std::move( newSpots );
    island = newIsland;
    selectedSpot = -1;
    // The quick destinations: the first spot of each landmark's bank prefix.
    destinations.clear();
    for ( const LandmarkDestination &landmark : kLandmarks ) {
        const size_t prefixLength = strlen( landmark.BankPrefix );
        for ( size_t i = 0; i < spots.size(); i++ ) {
            if ( spots[i].Name.compare( 0, prefixLength, landmark.BankPrefix ) == 0 ) {
                Destination destination;
                destination.Name = landmark.Name;
                destination.SpotIndex = static_cast<int32_t>( i );
                destinations.push_back( std::move( destination ) );
                break;
            }
        }
    }
    OTDU_LOG_INFO( "Cheats: %zu spot(s) on '%s' for the teleport list\n", spots.size(),
                   island.c_str() );
    if ( !destinations.empty() ) {
        std::string names;
        for ( const Destination &destination : destinations ) {
            names += ( names.empty() ? "" : ", " ) + destination.Name;
        }
        OTDU_LOG_INFO( "Cheats: %zu quick destination(s): %s\n", destinations.size(), names.c_str() );
    }
    // -tpdestination <name>: the row's button, pressed once the island's spots
    // are here (an exact name first, then the first name containing it).
    if ( !gsPendingDestination.empty() ) {
        const std::string wanted = ToLower( gsPendingDestination );
        int32_t match = -1;
        for ( const Destination &destination : destinations ) {
            const std::string name = ToLower( destination.Name );
            if ( name == wanted ) {
                match = destination.SpotIndex;
                break;
            }
            if ( match < 0 && name.find( wanted ) != std::string::npos ) {
                match = destination.SpotIndex;
            }
        }
        if ( match >= 0 ) {
            teleportToSpot( match );
            OTDU_LOG_INFO( "Cheats: -tpdestination '%s' -> %s (%.0f, %.0f)\n", gsPendingDestination.c_str(),
                           spots[match].Name.c_str(), spots[match].Position.x, spots[match].Position.z );
        } else {
            OTDU_LOG_WARN( "Cheats: -tpdestination '%s' is not one of the row's destinations\n",
                           gsPendingDestination.c_str() );
        }
        gsPendingDestination.clear();
    }
}

void Cheats::setWeathers( std::vector<std::string> names, int32_t current ) {
    weathers = std::move( names );
    selectedWeather = current;
}

void Cheats::teleportToSpot( int32_t index ) {
    if ( index < 0 || index >= static_cast<int32_t>( spots.size() ) ) {
        return;
    }
    selectedSpot = index;
    teleportX = spots[index].Position.x;
    teleportZ = spots[index].Position.z;
    teleportHeading = spots[index].Heading;
    bTeleport = true;
    teleportTarget = spots[index].Position;
    bTeleportRequestHeading = false;
}

bool Cheats::takeTeleport( Vector3f &outTarget, float &outHeading, bool &outHasHeading ) {
    if ( !bTeleport ) {
        return false;
    }
    bTeleport = false;
    outTarget = teleportTarget;
    outHeading = teleportRequestHeading;
    outHasHeading = bTeleportRequestHeading;
    return true;
}

bool Cheats::takeWeather( int32_t &outIndex ) {
    if ( !bWeather ) {
        return false;
    }
    bWeather = false;
    outIndex = weatherIndex;
    return true;
}

bool Cheats::takeStoryStep( int32_t &outStep ) {
    if ( !bStoryStep ) {
        return false;
    }
    bStoryStep = false;
    outStep = storyStep;
    return true;
}

bool Cheats::takeBackOnTrack() {
    if ( !bBackOnTrack ) {
        return false;
    }
    bBackOnTrack = false;
    return true;
}

bool Cheats::takeUnlockAllRoads() {
    if ( !bPendingAllRoads ) {
        return false;
    }
    bPendingAllRoads = false;
    return true;
}

bool Cheats::takeUnlockAllSpots() {
    if ( !bPendingAllSpots ) {
        return false;
    }
    bPendingAllSpots = false;
    return true;
}

bool Cheats::takeTargetNearestLockedRoad() {
    if ( !bPendingNearestLockedRoad ) {
        return false;
    }
    bPendingNearestLockedRoad = false;
    return true;
}

void Cheats::setRoadStatus( int32_t percent, int32_t explored, int32_t total ) {
    roadPercent = percent;
    roadExplored = explored;
    roadTotal = total;
}

void Cheats::setSpotStatus( int32_t discovered, int32_t total ) {
    spotsDiscovered = discovered;
    spotsTotal = total;
}

void Cheats::feedPadCode( bool bUp, bool bRight, bool bDown, bool bLeft, bool bOption ) {
    if ( bOption ) {
        bPadOptionSeen = true; // "mashing Square / X"
    }
    // The direction that continues the circle at this step (up, right, down, left).
    int32_t direction = -1;
    if ( bUp ) {
        direction = 0;
    } else if ( bRight ) {
        direction = 1;
    } else if ( bDown ) {
        direction = 2;
    } else if ( bLeft ) {
        direction = 3;
    }
    if ( direction < 0 ) {
        return;
    }
    if ( direction == padCircleStep ) {
        padCircleStep = ( padCircleStep + 1 ) % 4;
        if ( padCircleStep == 0 ) {
            padCircles++;
            OTDU_LOG_INFO( "Cheats: pad code: %d circle(s)\n", padCircles );
            if ( padCircles >= 3 && bPadOptionSeen ) {
                padCircles = 0;
                bPadOptionSeen = false;
                bOpen = !bOpen;
                OTDU_LOG_INFO( "Cheats: panel %s (pad code)\n", bOpen ? "open" : "closed" );
            }
        }
        return;
    }
    // Out of order: start over (a lone 'up' restarts the circle).
    padCircleStep = direction == 0 ? 1 : 0;
    padCircles = 0;
}

void Cheats::unlockAllCars() {
    // "ALL CARS": every row of the DBCarShop table, the same list DealerMenu
    // sells (the starter car is not in OwnedCars, so NbCars counts it + 1).
    PlayerProfile &profile = GetPlayerProfile();
    int32_t added = 0;
    for ( const CarShop &shop : GetCarShops() ) {
        for ( const ShopCar &car : shop.Cars ) {
            if ( car.CarId == 0 ||
                 std::find( profile.OwnedCars.begin(), profile.OwnedCars.end(), car.CarId ) !=
                     profile.OwnedCars.end() ) {
                continue;
            }
            profile.OwnedCars.push_back( car.CarId );
            added++;
        }
    }
    if ( added > 0 ) {
        profile.NbCars = static_cast<int32_t>( profile.OwnedCars.size() ) + 1;
        SaveProfile( "cheat: all cars" );
    }
    OTDU_LOG_INFO( "Cheats: all cars: %d added, %zu owned, NbCars %d\n", added,
                   profile.OwnedCars.size(), profile.NbCars );
}

void Cheats::unlockAllClothes() {
    // "ALL CLOTHES": every DBClothe row (the same list ClothesMenu sells), so
    // the clothes shops and the hairdresser show everything as owned.
    PlayerProfile &profile = GetPlayerProfile();
    int32_t added = 0;
    for ( const ClotheItem &item : GetClothes() ) {
        if ( item.Id == 0 ||
             std::find( profile.OwnedClothes.begin(), profile.OwnedClothes.end(), item.Id ) !=
                 profile.OwnedClothes.end() ) {
            continue;
        }
        profile.OwnedClothes.push_back( item.Id );
        added++;
    }
    if ( added > 0 ) {
        SaveProfile( "cheat: all clothes" );
    }
    OTDU_LOG_INFO( "Cheats: all clothes: %d added, %zu owned\n", added,
                   profile.OwnedClothes.size() );
}

void Cheats::setNumHouses( int32_t count ) {
    PlayerProfile &profile = GetPlayerProfile();
    if ( count <= profile.NbHouses ) {
        OTDU_LOG_INFO( "Cheats: houses already %d\n", profile.NbHouses );
        return;
    }
    profile.NbHouses = count;
    SaveProfile( "cheat: all houses" );
    OTDU_LOG_INFO( "Cheats: houses = %d\n", profile.NbHouses );
}

void Cheats::setAllCarsBuyable( bool bEnable ) {
    gbAllCarsBuyable = bEnable;
    OTDU_LOG_INFO( "Cheats: all cars buyable = %s\n", bEnable ? "on" : "off" );
}

bool Cheats::isAllCarsBuyable() {
    return gbAllCarsBuyable;
}

void Cheats::awardAllAchievements() {
    // "ACHIEVEMENTS": every DBRewardXP event once, the same call the game makes
    // when the event happens (the levels / the HUD popups follow).
    int32_t awarded = 0;
    for ( const RewardXp &reward : GetRewardXp() ) {
        if ( !reward.Event.empty() && AwardSkillEvent( reward.Event.c_str() ) ) {
            awarded++;
        }
    }
    OTDU_LOG_INFO( "Cheats: achievements: %d of %zu event(s) awarded\n", awarded,
                   GetRewardXp().size() );
}

void Cheats::setMoney( int32_t value ) {
    PlayerProfile &profile = GetPlayerProfile();
    const int32_t clamped = ClampMoney( value );
    if ( profile.Money == clamped ) {
        return;
    }
    profile.Money = clamped;
    SaveProfile( "cheat: money" );
    OTDU_LOG_INFO( "Cheats: money = %s\n", FormatMoney( clamped ).c_str() );
}

void Cheats::addMoney( int32_t delta ) {
    setMoney( ClampMoney( static_cast<int64_t>( GetPlayerProfile().Money ) + delta ) );
}

void Cheats::setChips( int32_t value ) {
    PlayerProfile &profile = GetPlayerProfile();
    const int32_t clamped = ClampMoney( value );
    if ( profile.CasinoChips == clamped ) {
        return;
    }
    profile.CasinoChips = clamped;
    SaveProfile( "cheat: chips" );
    OTDU_LOG_INFO( "Cheats: casino chips = %d\n", clamped );
}

void Cheats::addChips( int32_t delta ) {
    setChips( ClampMoney( static_cast<int64_t>( GetPlayerProfile().CasinoChips ) + delta ) );
}

void Cheats::applyCommandLine() {
    // -money <n> / -chips <n>: the wallet at startup, so a scripted run can buy
    // a car (or sit at a casino table) without clicking.
    gbOpenFromCommandLine = CmdLine::HasArg( "-cheatspanel" );
    // -tpdestination <name>: the teleport row's destination, resolved once the
    // mode hands the island's spots over (setSpots).
    if ( const char *pDestination = CmdLine::GetArgValue( "-tpdestination" ) ) {
        gsPendingDestination = pDestination;
    }
    if ( const char *pMoney = CmdLine::GetArgValue( "-money" ) ) {
        setMoney( atoi( pMoney ) );
    }
    if ( const char *pChips = CmdLine::GetArgValue( "-chips" ) ) {
        setChips( atoi( pChips ) );
    }
    // -cheat <row>[,<row>]: the delock rows, so a test starts with them on.
    // The world-side rows (houses, allroads) are queued: the panel applies them
    // as soon as the mode has given it the island's spots (drawUI).
    if ( const char *pRows = CmdLine::GetArgValue( "-cheat" ) ) {
        std::string rows( pRows );
        size_t start = 0;
        while ( start <= rows.size() ) {
            const size_t comma = rows.find( ',', start );
            const std::string row =
                rows.substr( start, comma == std::string::npos ? std::string::npos : comma - start );
            if ( row == "allcars" ) {
                unlockAllCars();
            } else if ( row == "houses" ) {
                gbPendingAllHousesFromCommandLine = true;
            } else if ( row == "clothes" ) {
                unlockAllClothes();
            } else if ( row == "allroads" ) {
                gbPendingAllRoadsFromCommandLine = true;
            } else if ( row == "allspots" ) {
                gbPendingAllSpotsFromCommandLine = true;
            } else if ( row == "buyable" ) {
                setAllCarsBuyable( true );
            } else if ( row == "achievements" ) {
                awardAllAchievements();
            } else if ( !row.empty() ) {
                OTDU_LOG_WARN( "Cheats: unknown -cheat row '%s'\n", row.c_str() );
            }
            if ( comma == std::string::npos ) {
                break;
            }
            start = comma + 1;
        }
    }
}

void Cheats::drawUI( const char *pModeName ) {
    if ( !bOpen ) {
        return;
    }
    ImGui::SetNextWindowPos( DpiScaled( 20.0f, 90.0f ), ImGuiCond_FirstUseEver );
    ImGui::SetNextWindowSize( DpiScaled( 480.0f, 620.0f ), ImGuiCond_FirstUseEver );
    char title[96] = {};
    snprintf( title, sizeof( title ), "Cheats - %s", pModeName != nullptr ? pModeName : "game" );
    // "All houses" needs the island's spots: count the EPH_* buildings (the
    // houses of spots.bin) the moment the panel has them.
    if ( bPendingAllHouses && !spots.empty() ) {
        bPendingAllHouses = false;
        int32_t houses = 0;
        for ( const Spot &spot : spots ) {
            if ( spot.Name.compare( 0, 3, "EPH" ) == 0 ) {
                houses++;
            }
        }
        setNumHouses( houses > 0 ? houses : GetPlayerProfile().NbHouses );
    }
    if ( ImGui::Begin( title ) ) {
        ImGui::TextDisabled( "F4 or the pad code (D-pad in circles + X) toggles this panel" );
        ImGui::TextDisabled( "F5 on the map teleports to the cursor; -money / -chips / -cheat <rows>" );
        ImGui::Separator();

        // ---- The wallet (the profile: DealerMenu and the casino read it) ----
        PlayerProfile &profile = GetPlayerProfile();
        ImGui::Text( "money: %s", FormatMoney( profile.Money ).c_str() );
        ImGui::Text( "casino chips: %d", profile.CasinoChips );
        ImGui::SetNextItemWidth( DpiScale() * 130.0f );
        ImGui::InputInt( "##money", &moneyInput, 0, 0 );
        ImGui::SameLine();
        if ( ImGui::Button( "Set money" ) ) {
            setMoney( moneyInput );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "+1M" ) ) {
            addMoney( 1000000 );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "+10M" ) ) {
            addMoney( 10000000 );
        }
        ImGui::SetNextItemWidth( DpiScale() * 130.0f );
        ImGui::InputInt( "##chips", &chipsInput, 0, 0 );
        ImGui::SameLine();
        if ( ImGui::Button( "Set chips" ) ) {
            setChips( chipsInput );
        }
        ImGui::SameLine();
        if ( ImGui::Button( "+100k" ) ) {
            addChips( 100000 );
        }
        ImGui::Separator();

        // ---- Teleport (a world: the modes without one skip this) ----
        if ( !spots.empty() ) {
            ImGui::Text( "teleport - %s, %zu spot(s)",
                         island.empty() ? "(no island)" : island.c_str(), spots.size() );
            // A few destinations, one click each: the whole list below is the
            // island's own spots (79 on Ibiza), these are the ones a test
            // reaches for first. Four per row, the same shape as the delock
            // buttons further down.
            if ( !destinations.empty() ) {
                for ( size_t i = 0; i < destinations.size(); i++ ) {
                    if ( i % 4 != 0 ) {
                        ImGui::SameLine();
                    }
                    if ( ImGui::Button( destinations[i].Name.c_str() ) ) {
                        teleportToSpot( destinations[i].SpotIndex );
                    }
                }
                ImGui::TextDisabled( "destinations - the island's landmark spots" );
            }
            ImGui::SetNextItemWidth( -1.0f );
            ImGui::InputTextWithHint( "##spotfilter", "filter spots (ECD_FERR, EPH_L3DI...)",
                                      spotFilter, sizeof( spotFilter ) );
            const std::string filter = ToLower( spotFilter );
            ImGui::BeginChild( "##spots", ImVec2( -1.0f, DpiScale() * 150.0f ) );
            for ( int32_t i = 0; i < static_cast<int32_t>( spots.size() ); i++ ) {
                if ( !filter.empty() &&
                     ToLower( spots[i].Name ).find( filter ) == std::string::npos ) {
                    continue;
                }
                char text[160] = {};
                snprintf( text, sizeof( text ), "%s  (%.0f, %.0f)", spots[i].Name.c_str(),
                          spots[i].Position.x, spots[i].Position.z );
                if ( ImGui::Selectable( text, i == selectedSpot ) ) {
                    selectedSpot = i;
                    teleportX = spots[i].Position.x;
                    teleportZ = spots[i].Position.z;
                    teleportHeading = spots[i].Heading;
                }
                if ( ImGui::IsItemHovered() &&
                     ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) {
                    // Double click: go there now.
                    teleportToSpot( i );
                }
            }
            ImGui::EndChild();
            if ( selectedSpot >= 0 && selectedSpot < static_cast<int32_t>( spots.size() ) &&
                 ImGui::Button( "Teleport to the spot" ) ) {
                teleportToSpot( selectedSpot );
            }
            ImGui::SameLine();
            ImGui::TextDisabled( "or double click a row" );
            ImGui::SetNextItemWidth( DpiScale() * 100.0f );
            ImGui::InputFloat( "x", &teleportX, 0.0f, 0.0f, "%.1f" );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( DpiScale() * 100.0f );
            ImGui::InputFloat( "z", &teleportZ, 0.0f, 0.0f, "%.1f" );
            ImGui::SetNextItemWidth( DpiScale() * 90.0f );
            ImGui::InputFloat( "heading deg", &teleportHeading, 0.0f, 0.0f, "%.0f" );
            ImGui::SameLine();
            ImGui::Checkbox( "face it", &bUseHeading );
            if ( ImGui::Button( "Teleport to the coordinates" ) ) {
                bTeleport = true;
                teleportTarget = Vector3f( teleportX, 0.0f, teleportZ );
                teleportRequestHeading = teleportHeading * kDegToRad;
                bTeleportRequestHeading = bUseHeading;
            }
            ImGui::Separator();
        }

        // ---- Weather, story step, back on track ----
        if ( !weathers.empty() ) {
            ImGui::SetNextItemWidth( DpiScale() * 200.0f );
            if ( ImGui::BeginCombo( "##weather", selectedWeather >= 0 &&
                                                         selectedWeather <
                                                             static_cast<int32_t>( weathers.size() )
                                                     ? weathers[selectedWeather].c_str()
                                                     : "(weather)" ) ) {
                for ( int32_t i = 0; i < static_cast<int32_t>( weathers.size() ); i++ ) {
                    if ( ImGui::Selectable( weathers[i].c_str(), i == selectedWeather ) ) {
                        selectedWeather = i;
                        bWeather = true;
                        weatherIndex = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::TextDisabled( "weather" );
        }
        // Units: the game/units.h flag (options.ini); the speed digits, the
        // odometer, the fuel range and the GPS / detector lines follow it.
        {
            bool bImperial = IsImperialUnits();
            if ( ImGui::Checkbox( "mph / miles", &bImperial ) ) {
                SetImperialUnits( bImperial );
            }
            ImGui::SameLine();
            ImGui::TextDisabled( "units (speed, odometer, distances)" );
        }
        ImGui::SetNextItemWidth( DpiScale() * 120.0f );
        ImGui::InputInt( "##story", &storyStepInput, 0, 0 );
        ImGui::SameLine();
        if ( ImGui::Button( "Set story step" ) ) {
            bStoryStep = true;
            storyStep = storyStepInput;
        }
        ImGui::SameLine();
        ImGui::TextDisabled( "(a restart uses it)" );
        if ( !spots.empty() && ImGui::Button( "Back on track (nearest road)" ) ) {
            bBackOnTrack = true;
        }
        ImGui::Separator();

        // ---- The delock rows of the retail dev menu (the mod's CHEAT MENU) ----
        ImGui::TextDisabled( "delock - the retail dev menu's rows" );
        if ( ImGui::Button( "All cars" ) ) {
            unlockAllCars();
        }
        ImGui::SameLine();
        if ( ImGui::Button( "All clothes" ) ) {
            unlockAllClothes();
        }
        ImGui::SameLine();
        if ( !spots.empty() && ImGui::Button( "All houses" ) ) {
            bPendingAllHouses = true;
        }
        ImGui::SameLine();
        if ( !spots.empty() && ImGui::Button( "All roads" ) ) {
            bPendingAllRoads = true;
        }
        ImGui::SameLine();
        if ( ImGui::Button( "All spots" ) ) {
            bPendingAllSpots = true;
        }
        ImGui::SameLine();
        if ( !spots.empty() && ImGui::Button( "Nearest locked road" ) ) {
            bPendingNearestLockedRoad = true;
        }
        if ( ImGui::Button( "All achievements" ) ) {
            awardAllAchievements();
        }
        ImGui::SameLine();
        bool bBuyable = isAllCarsBuyable();
        if ( ImGui::Checkbox( "cars on stock (buyable)", &bBuyable ) ) {
            setAllCarsBuyable( bBuyable );
        }
        ImGui::Text( "cars owned: %zu, clothes: %zu, houses: %d, roads: %d%% (%d/%d), spots: %d/%d",
                     GetPlayerProfile().OwnedCars.size(), GetPlayerProfile().OwnedClothes.size(),
                     GetPlayerProfile().NbHouses, roadPercent, roadExplored, roadTotal, spotsDiscovered, spotsTotal );
        ImGui::TextDisabled( "no row for car dealers: the dealer buildings are always there" );

        // ---- The scale of every ImGui panel (cheats, advanced settings, the
        // ---- asset/world viewers): the monitor's scale by default, this
        // ---- slider overrides it for the session.
        ImGui::Separator();
        float uiScale = gpImGui != nullptr ? gpImGui->getScale() : 1.0f;
        if ( ImGui::SliderFloat( "UI scale", &uiScale, 0.75f, 3.0f, "%.2fx" ) ) {
            if ( gpImGui != nullptr ) {
                gpImGui->setScale( uiScale );
            }
        }
        ImGui::TextDisabled( "text and widgets rescale now; window sizes on the next open" );
    }
    ImGui::End();
}


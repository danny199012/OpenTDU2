#pragma once

#include "game/licenses.h"

#include <string>
#include <vector>

class Renderer2D;

// The racing school desk (native; the retail screen is the r_school.bnk
// movie, not driven): the school's licences on this island (DBLicense rows
// whose tests are RS<letter>..., docs/re/licenses_spec.md), then the tests of
// one licence with their target times and the player's best. The tests open
// in order: one is open when the test before it is passed [INF].
class SchoolMenu {
public:
    void open( char schoolLetter, int32_t island );
    void close();
    // Straight to the tests of the licence holding a test, on that test (the
    // way back from the result screen's Quit).
    void openAtTest( const std::string &test );
    inline bool isOpen() const { return bOpen; }
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // One shot: the test chosen (its .ig2 name), when the player started one.
    bool takeStartedTest( std::string &outTest );

private:
    enum class eStep { Licenses, Tests };
    struct TestRow {
        std::string Name;  // RSA7T01
        std::string Title; // the .ig2's dbna text
        float Target = 0.0f;
        bool bOpen = false;
    };

    void openLicense( int32_t index );
    bool isLicenseOpen( int32_t index ) const;

    bool bOpen = false;
    char school = 'A';
    std::vector<const LicenseInfo *> licenses;
    eStep step = eStep::Licenses;
    int32_t selected = 0;
    int32_t selectedTest = 0;
    std::vector<TestRow> tests;
    std::string startedTest;
    std::string message;
    float messageTimer = 0.0f;
};

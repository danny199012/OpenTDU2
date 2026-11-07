#pragma once

#include "flash/flash_player.h"
#include "game/licenses.h"

#include <string>
#include <vector>

class Renderer2D;

// The racing school desk: the retail R_SCHOOL.vmf (FrontEnd/HiRes/r_school.bnk),
// driven the way its scripts ask [DATA: tools/vmf_disasm.py, licenses_spec 11]:
//
//   root 'open_school' -> FSCommand rsin, the licence carousel ('licences')
//   rsch:stli  the game fills _root.li<n>_state ('done' / 'current' / 'locked'),
//              li<n>_gp1 / li<n>_gp2 (the class frames 'A7' ... or 'empty'),
//              nb_licences, num_licence, /:LicenceDate ("25SEP1986")
//   rsch:dili  the licence under the cursor changed (_root.num_licence)
//   rsch:dits  the test list opens: nb_tests_total, num_test and per line
//              line<i>/:t_name, :type (turn / turn90 / turn180 / braking /
//              slalom / circuit / mixed / overtaking ...), :meteo (sun / rain /
//              night), :place (r_school / road), :done
//   rsch:dtup  a line is highlighted: _root.TestDesc
//   rsch:tspr  a test was chosen (_root.num_test)
//   wmon / rsch:exit  back to the walk (B on the carousel)
class SchoolScreen {
public:
    bool load();
    void unload();
    inline bool isOpen() const { return bOpen; }

    // The school's licences (SchoolMenu's list: GetSchoolLicenses).
    void open( char schoolLetter, int32_t island );
    // Straight to a licence (the result screen's Quit): its carousel slot.
    void openAtTest( char schoolLetter, int32_t island, const std::string &test );
    void close();
    void tick( float deltaTime );
    void draw( Renderer2D &renderer );
    // One shot: the test chosen (its .ig2 name).
    bool takeStartedTest( std::string &outTest );

private:
    void onCommand( const std::string &command );
    void fillLicences();
    void fillTests();
    int32_t currentLicence();
    void setTestDescription();

    FlashPlayer player;
    bool bOpen = false;
    std::vector<const LicenseInfo *> licenses;
    int32_t startLicence = 0;
    std::string testsClip;  // the clip holding line0.. (found when it opens)
    std::vector<std::string> tests;
    std::string startedTest;
    float stickRepeat = 0.0f;
};

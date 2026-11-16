#include "shared.h"
#include "tuner.h"

#include "filesystem/gs_file.h"
#include "system/cmd_line.h"

bool TunerModEnabled() {
    if ( CmdLine::HasArg( "-notunermod" ) ) {
        return false;
    }
    if ( CmdLine::HasArg( "-tunermod" ) ) {
        return true;
    }
    // The package list is fixed at start; looked up once.
    static int32_t cached = -1;
    if ( cached < 0 ) {
        cached = ( gpFile != nullptr && gpFile->isModEnabled( "tuner-garage" ) ) ? 1 : 0;
        OTDU_LOG_INFO( "Tuner: the tuner-garage mod is %s\n", cached != 0 ? "enabled" : "off (vanilla)" );
    }
    return cached != 0;
}

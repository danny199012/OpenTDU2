#include "shared.h"
#include "cmd_line.h"

#include <deque>
#include <string>

namespace {
// Arguments added at run time; a deque keeps the c_str() pointers stable.
std::deque<std::string> gExtraArgs;
} // namespace

const char **gpCmdLineArgs = nullptr;
int32_t gCmdLineArgCount = 0;

void CmdLine::Initialize( int32_t argc, const char **argv ) {
    gpCmdLineArgs = argv;
    gCmdLineArgCount = argc;
}

void CmdLine::AddArg( const char *pArg, const char *pValue ) {
    gExtraArgs.emplace_back( pArg );
    if ( pValue != nullptr ) {
        gExtraArgs.emplace_back( pValue );
    }
}

bool CmdLine::HasArg( const char *pArg ) {
    for ( int32_t i = 1; i < gCmdLineArgCount; i++ ) {
        if ( StringCmpI( gpCmdLineArgs[i], pArg ) == 0 ) {
            return true;
        }
    }
    for ( const std::string &extra : gExtraArgs ) {
        if ( StringCmpI( extra.c_str(), pArg ) == 0 ) {
            return true;
        }
    }
    return false;
}

const char *CmdLine::GetArgValue( const char *pArg ) {
    for ( int32_t i = 1; i < gCmdLineArgCount - 1; i++ ) {
        if ( StringCmpI( gpCmdLineArgs[i], pArg ) == 0 ) {
            return gpCmdLineArgs[i + 1];
        }
    }
    for ( size_t i = 0; i + 1 < gExtraArgs.size(); i++ ) {
        if ( StringCmpI( gExtraArgs[i].c_str(), pArg ) == 0 ) {
            return gExtraArgs[i + 1].c_str();
        }
    }
    return nullptr;
}

int32_t CmdLine::GetArgInt( const char *pArg, int32_t defaultValue ) {
    const char *pValue = GetArgValue( pArg );
    return pValue != nullptr ? atoi( pValue ) : defaultValue;
}

#pragma once

namespace CmdLine {
void Initialize( int32_t argc, const char **argv );
bool HasArg( const char *pArg );
// Returns the token following pArg (e.g. "-game /path"), or nullptr.
const char *GetArgValue( const char *pArg );
// Adds an argument after the real command line (the Android launcher has no argv:
// it passes -savefile / -playername / -userdata this way). A null value adds a bare flag.
void AddArg( const char *pArg, const char *pValue );
int32_t GetArgInt( const char *pArg, int32_t defaultValue );
} // namespace CmdLine

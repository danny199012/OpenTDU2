// Android (SDL activity): SDL_main.h maps main() to SDL's SDL_RunApp entry.
// Other platforms keep the plain main(); the include is Android-only so the
// Windows build (no SDL3 main helper linked) stays untouched.
#if defined( __ANDROID__ )
#include <SDL3/SDL_main.h>
#endif

#include "shared.h"
#include "tdu_instance.h"

int main( int argc, char **argv ) {
    return TestDrive::InitAndRun( const_cast<const char **>( argv ), argc );
}

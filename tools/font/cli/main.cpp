// fontpack -- the font tool without a window (build scripts, CI). Commands: cli/font_cli.cpp, or
// `fontpack --help`.
#include "atlas_cli.h"   // utf8CommandLine
#include "font_cli.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int l_iArgc, char** l_pArgv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // the glyph list prints the characters themselves
#endif
    return f_FontCliMain(f_Utf8CommandLine(l_iArgc, l_pArgv));
}

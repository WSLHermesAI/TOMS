// fontpack -- the font tool without a window (build scripts, CI). Commands: cli/font_cli.cpp, or
// `fontpack --help`.
#include "atlas_cli.h"   // utf8CommandLine
#include "font_cli.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // the glyph list prints the characters themselves
#endif
    return fontCliMain(utf8CommandLine(argc, argv));
}

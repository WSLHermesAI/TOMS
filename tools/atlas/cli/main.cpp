// atlaspack -- the atlas tool without a window (build scripts, CI, the asset pipeline).
// Commands and options: cli/atlas_cli.cpp, or `atlaspack --help`.
#include "atlas_cli.h"

int main(int l_iArgc, char** l_pArgv) { return f_AtlasCliMain(f_Utf8CommandLine(l_iArgc, l_pArgv)); }

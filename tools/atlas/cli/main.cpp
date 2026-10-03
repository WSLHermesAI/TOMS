// atlaspack -- the atlas tool without a window (build scripts, CI, the asset pipeline).
// Commands and options: cli/atlas_cli.cpp, or `atlaspack --help`.
#include "atlas_cli.h"

int main(int argc, char** argv) { return atlasCliMain(utf8CommandLine(argc, argv)); }

// atlas_cli.h -- the atlas tool's command line (cli/atlas_cli.cpp), shared by atlaspack and the
// Qt editor's --headless mode.
#pragma once
#include <string>
#include <vector>

// args: the arguments after the program name, UTF-8. Returns the exit code
// (0 ok, 1 warnings with --strict, 2 errors, 3 bad command line).
int f_AtlasCliMain(const std::vector<std::string>& l_vArgs);
// The process's arguments (without the program name) as UTF-8 -- on Windows read from the wide
// command line, since argv cannot hold characters outside the ANSI code page.
std::vector<std::string> f_Utf8CommandLine(int l_iArgc, char** l_pArgv);

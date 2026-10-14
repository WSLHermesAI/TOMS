// font_cli.h -- the font tool's command line (cli/font_cli.cpp), shared by fontpack and the Qt
// editor's --headless mode.
#pragma once
#include <string>
#include <vector>

// args: the arguments after the program name, UTF-8. Returns the exit code
// (0 ok, 1 warnings, 2 errors, 3 bad command line).
int fontCliMain(const std::vector<std::string>& args);

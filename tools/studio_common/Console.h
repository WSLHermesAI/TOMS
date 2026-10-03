#pragma once

#include <string>
#include <vector>

// Command line helpers shared by the TOMS editors (tools/studio_common).
namespace Console {

// The editors are GUI-subsystem programs, so they start without a console. Output that is
// already redirected (a pipe or a file, as in build scripts) is kept; otherwise this borrows the
// console of the shell that started the program, so --headless messages show up there.
// No-op outside Windows.
void attachParent();

// The program's arguments (without the program name) as UTF-8, also on Windows where argv is in
// the ANSI code page.
std::vector<std::string> utf8Args(int argc, char** argv);

}  // namespace Console

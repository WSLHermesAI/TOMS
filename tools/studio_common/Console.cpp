#include "Console.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Console {

void attachParent()
{
#ifdef _WIN32
    auto usable = [](DWORD which) {
        HANDLE h = GetStdHandle(which);
        return h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN;
    };
    const bool out = usable(STD_OUTPUT_HANDLE), err = usable(STD_ERROR_HANDLE);
    if (out && err) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* f = nullptr;
    if (!out) freopen_s(&f, "CONOUT$", "w", stdout);
    if (!err) freopen_s(&f, "CONOUT$", "w", stderr);
#endif
}

std::vector<std::string> utf8Args(int argc, char** argv)
{
    std::vector<std::string> out;
#ifdef _WIN32
    int n = 0;
    if (LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n)) {
        for (int i = 1; i < n; i++) {
            const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
            std::string s(len > 0 ? size_t(len) - 1 : 0, '\0');
            if (len > 1) WideCharToMultiByte(CP_UTF8, 0, w[i], -1, &s[0], len, nullptr, nullptr);
            out.push_back(s);
        }
        LocalFree(w);
        SetConsoleOutputCP(CP_UTF8);
        return out;
    }
#endif
    for (int i = 1; i < argc; i++) out.push_back(argv[i]);
    return out;
}

}  // namespace Console

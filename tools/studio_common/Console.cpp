#include "Console.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace Console {

void f_AttachParent()
{
#ifdef _WIN32
    auto l_fnUsable = [](DWORD l_uWhich) {
        HANDLE l_pH = GetStdHandle(l_uWhich);
        return l_pH && l_pH != INVALID_HANDLE_VALUE && GetFileType(l_pH) != FILE_TYPE_UNKNOWN;
    };
    const bool l_bOut = l_fnUsable(STD_OUTPUT_HANDLE), l_bErr = l_fnUsable(STD_ERROR_HANDLE);
    if (l_bOut && l_bErr) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* l_pF = nullptr;
    if (!l_bOut) freopen_s(&l_pF, "CONOUT$", "w", stdout);
    if (!l_bErr) freopen_s(&l_pF, "CONOUT$", "w", stderr);
#endif
}

std::vector<std::string> f_Utf8Args(int l_iArgc, char** l_pArgv)
{
    std::vector<std::string> l_vOut;
#ifdef _WIN32
    int l_iN = 0;
    if (LPWSTR* l_pW = CommandLineToArgvW(GetCommandLineW(), &l_iN)) {
        for (int l_iI = 1; l_iI < l_iN; l_iI++) {
            const int l_iLen = WideCharToMultiByte(CP_UTF8, 0, l_pW[l_iI], -1, nullptr, 0, nullptr, nullptr);
            std::string l_strS(l_iLen > 0 ? size_t(l_iLen) - 1 : 0, '\0');
            if (l_iLen > 1) WideCharToMultiByte(CP_UTF8, 0, l_pW[l_iI], -1, &l_strS[0], l_iLen, nullptr, nullptr);
            l_vOut.push_back(l_strS);
        }
        LocalFree(l_pW);
        SetConsoleOutputCP(CP_UTF8);
        return l_vOut;
    }
#endif
    for (int l_iI = 1; l_iI < l_iArgc; l_iI++) l_vOut.push_back(l_pArgv[l_iI]);
    return l_vOut;
}

}  // namespace Console

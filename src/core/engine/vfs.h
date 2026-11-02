#pragma once
// vfs.h -- the one place the game reads a file it ships with.
//
// Why this exists: the same read must work three ways, and only one of them is a filesystem.
//   desktop  real files next to the exe            -> stdio
//   web      Emscripten's virtual FS (--preload-file) -> stdio (NOT ifstream: libc++ ifstream reports
//            the right size but reads nothing from preloaded data -- the reason readJsonFile already
//            used C stdio)
//   android  entries INSIDE the APK                -> AAssetManager (stdio cannot see them at all)
//
// Header-only on purpose: `inline` gives the single instance C++17 needs, so no CMake source list or
// link order changes are required for a platform-independent API. Desktop and web behaviour is
// byte-for-byte what it was before this existed -- that is what makes it safe to land before Android.

#include <cstdio>
#include <string>
#include <vector>
#if !defined(__ANDROID__)
#  include <filesystem>
#  include <system_error>
#endif

#if defined(__ANDROID__)
#  include <android/asset_manager.h>
#  include <android/asset_manager_jni.h>   // AAssetManager_fromJava, when the glue hands us the Java one
#endif

namespace toms {

#if defined(__ANDROID__)
// Set once from the Android glue (ndk + JNI): the app's AAssetManager. Until it is set, reads fall
// back to stdio, so the same binary still works if a path turns out to be an extracted real file.
inline AAssetManager* g_pVfsAssetManager = nullptr;
inline void f_VfsInit(AAssetManager* l_pAm) { g_pVfsAssetManager = l_pAm; }
#else
inline void f_VfsInit(void* = nullptr) {}   // no-op: desktop/web read the filesystem directly
#endif

#if defined(__ANDROID__)
// "assets/media/../data/x.json" -> "data/x.json": the APK entry name. AAssetManager neither
// resolves ".." nor accepts the leading "assets/" (entry names are relative to the APK's assets/).
inline std::string f_VfsAssetPath(const std::string& l_strPath) {
    std::vector<std::string> l_vParts;
    size_t l_nI = 0;
    while (l_nI <= l_strPath.size()) {
        const size_t l_nJ = l_strPath.find_first_of("/\\", l_nI);
        const std::string l_strPart = l_strPath.substr(l_nI, (l_nJ == std::string::npos ? l_strPath.size() : l_nJ) - l_nI);
        if (l_strPart == "..") { if (!l_vParts.empty()) l_vParts.pop_back(); }
        else if (!l_strPart.empty() && l_strPart != ".") l_vParts.push_back(l_strPart);
        if (l_nJ == std::string::npos) break;
        l_nI = l_nJ + 1;
    }
    if (!l_vParts.empty() && l_vParts[0] == "assets") l_vParts.erase(l_vParts.begin());
    std::string l_strOut;
    for (const std::string& l_strP : l_vParts) { if (!l_strOut.empty()) l_strOut += '/'; l_strOut += l_strP; }
    return l_strOut;
}
#endif

#if !defined(__ANDROID__)
// A UTF-8 path as a std::filesystem path, and back (C++17 and C++20 spell these differently).
inline std::filesystem::path f_VfsPath(const std::string& l_strUtf8) {
#  if defined(__cpp_char8_t)
    return std::filesystem::path(std::u8string(l_strUtf8.begin(), l_strUtf8.end()));
#  else
    return std::filesystem::u8path(l_strUtf8);
#  endif
}
inline std::string f_VfsUtf8(const std::filesystem::path& l_oP) {
    const auto l_strS = l_oP.u8string();
    return std::string(l_strS.begin(), l_strS.end());
}
#endif

// fopen for a UTF-8 path. Windows' narrow fopen reads the path in the ANSI code page, so a file or folder with
// non-ASCII letters (a Chinese folder name) would not open: there the path goes through std::filesystem as UTF-16.
inline FILE* f_VfsOpen(const std::string& l_strPath) {
#if defined(_WIN32)
    FILE* l_pFp = nullptr;
    if (_wfopen_s(&l_pFp, f_VfsPath(l_strPath).c_str(), L"rb") != 0) return nullptr;
    return l_pFp;
#else
    return fopen(l_strPath.c_str(), "rb");
#endif
}

// Reads a whole file into `out`. Returns false and leaves `out` unchanged on failure.
// `path` is the path as the game already writes it ("assets/data/story.json"). On Android it is
// turned into the APK entry name (vfsAssetPath: no leading "assets/", ".." resolved).
inline bool f_VfsReadAll(const std::string& l_strPath, std::string& l_strOut) {
#if defined(__ANDROID__)
    if (g_pVfsAssetManager) {
        const std::string l_strAssetPath = f_VfsAssetPath(l_strPath);
        if (AAsset* l_pA = AAssetManager_open(g_pVfsAssetManager, l_strAssetPath.c_str(), AASSET_MODE_BUFFER)) {
            const off64_t l_i64N = AAsset_getLength64(l_pA);
            if (l_i64N > 0) {
                l_strOut.resize((size_t)l_i64N);
                const int l_iRd = AAsset_read(l_pA, &l_strOut[0], (size_t)l_i64N);
                AAsset_close(l_pA);
                if (l_iRd > 0) { l_strOut.resize((size_t)l_iRd); return true; }
                return false;
            }
            AAsset_close(l_pA);
        }
        // fall through: it may be a real file (extracted assets, or a user override)
    }
#endif
    FILE* l_pFp = f_VfsOpen(l_strPath);
    if (!l_pFp) return false;
    fseek(l_pFp, 0, SEEK_END);
    const long l_iSz = ftell(l_pFp);
    fseek(l_pFp, 0, SEEK_SET);
    if (l_iSz <= 0) { fclose(l_pFp); return false; }
    std::string l_strBuf((size_t)l_iSz, '\0');
    const size_t l_nRd = fread(&l_strBuf[0], 1, (size_t)l_iSz, l_pFp);
    fclose(l_pFp);
    if (l_nRd == 0) return false;
    l_strBuf.resize(l_nRd);
    l_strOut.swap(l_strBuf);
    return true;
}

// True when the FILE exists (for a folder, ask vfsListDir: APK folders are not openable entries).
inline bool f_VfsExists(const std::string& l_strPath) {
#if defined(__ANDROID__)
    if (g_pVfsAssetManager) {
        const std::string l_strAssetPath = f_VfsAssetPath(l_strPath);
        if (AAsset* l_pA = AAssetManager_open(g_pVfsAssetManager, l_strAssetPath.c_str(), AASSET_MODE_BUFFER)) {
            AAsset_close(l_pA);
            return true;
        }
    }
#endif
    if (FILE* l_pFp = f_VfsOpen(l_strPath)) { fclose(l_pFp); return true; }
    return false;
}

// The FILE names (no folders, no path) directly inside `dir`, in no particular order; empty when
// the folder does not exist. Callers join `dir + "/" + name` and read it with vfsReadAll.
inline std::vector<std::string> f_VfsListDir(const std::string& l_strDir) {
    std::vector<std::string> l_vNames;
#if defined(__ANDROID__)
    if (g_pVfsAssetManager) {
        if (AAssetDir* l_pD = AAssetManager_openDir(g_pVfsAssetManager, f_VfsAssetPath(l_strDir).c_str())) {
            while (const char* l_pszN = AAssetDir_getNextFileName(l_pD)) l_vNames.push_back(l_pszN);   // files only
            AAssetDir_close(l_pD);
        }
        if (!l_vNames.empty()) return l_vNames;
    }
    (void)l_strDir;
    return l_vNames;
#else
    std::error_code l_oEc;
    for (auto l_oIt = std::filesystem::directory_iterator(f_VfsPath(l_strDir), l_oEc); !l_oEc && l_oIt != std::filesystem::directory_iterator(); l_oIt.increment(l_oEc))
        if (l_oIt->is_regular_file(l_oEc)) l_vNames.push_back(f_VfsUtf8(l_oIt->path().filename()));
    return l_vNames;
#endif
}

}  // namespace toms

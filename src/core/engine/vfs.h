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
inline AAssetManager* g_vfsAssetManager = nullptr;
inline void vfsInit(AAssetManager* am) { g_vfsAssetManager = am; }
#else
inline void vfsInit(void* = nullptr) {}   // no-op: desktop/web read the filesystem directly
#endif

#if defined(__ANDROID__)
// "assets/media/../data/x.json" -> "data/x.json": the APK entry name. AAssetManager neither
// resolves ".." nor accepts the leading "assets/" (entry names are relative to the APK's assets/).
inline std::string vfsAssetPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= path.size()) {
        const size_t j = path.find_first_of("/\\", i);
        const std::string part = path.substr(i, (j == std::string::npos ? path.size() : j) - i);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    if (!parts.empty() && parts[0] == "assets") parts.erase(parts.begin());
    std::string out;
    for (const std::string& p : parts) { if (!out.empty()) out += '/'; out += p; }
    return out;
}
#endif

// Reads a whole file into `out`. Returns false and leaves `out` unchanged on failure.
// `path` is the path as the game already writes it ("assets/data/story.json"). On Android it is
// turned into the APK entry name (vfsAssetPath: no leading "assets/", ".." resolved).
inline bool vfsReadAll(const std::string& path, std::string& out) {
#if defined(__ANDROID__)
    if (g_vfsAssetManager) {
        const std::string assetPath = vfsAssetPath(path);
        if (AAsset* a = AAssetManager_open(g_vfsAssetManager, assetPath.c_str(), AASSET_MODE_BUFFER)) {
            const off64_t n = AAsset_getLength64(a);
            if (n > 0) {
                out.resize((size_t)n);
                const int rd = AAsset_read(a, &out[0], (size_t)n);
                AAsset_close(a);
                if (rd > 0) { out.resize((size_t)rd); return true; }
                return false;
            }
            AAsset_close(a);
        }
        // fall through: it may be a real file (extracted assets, or a user override)
    }
#endif
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    const long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); return false; }
    std::string buf((size_t)sz, '\0');
    const size_t rd = fread(&buf[0], 1, (size_t)sz, fp);
    fclose(fp);
    if (rd == 0) return false;
    buf.resize(rd);
    out.swap(buf);
    return true;
}

// True when the FILE exists (for a folder, ask vfsListDir: APK folders are not openable entries).
inline bool vfsExists(const std::string& path) {
#if defined(__ANDROID__)
    if (g_vfsAssetManager) {
        const std::string assetPath = vfsAssetPath(path);
        if (AAsset* a = AAssetManager_open(g_vfsAssetManager, assetPath.c_str(), AASSET_MODE_BUFFER)) {
            AAsset_close(a);
            return true;
        }
    }
#endif
    if (FILE* fp = fopen(path.c_str(), "rb")) { fclose(fp); return true; }
    return false;
}

// The FILE names (no folders, no path) directly inside `dir`, in no particular order; empty when
// the folder does not exist. Callers join `dir + "/" + name` and read it with vfsReadAll.
inline std::vector<std::string> vfsListDir(const std::string& dir) {
    std::vector<std::string> names;
#if defined(__ANDROID__)
    if (g_vfsAssetManager) {
        if (AAssetDir* d = AAssetManager_openDir(g_vfsAssetManager, vfsAssetPath(dir).c_str())) {
            while (const char* n = AAssetDir_getNextFileName(d)) names.push_back(n);   // files only
            AAssetDir_close(d);
        }
        if (!names.empty()) return names;
    }
    (void)dir;
    return names;
#else
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) names.push_back(it->path().filename().string());
    return names;
#endif
}

}  // namespace toms

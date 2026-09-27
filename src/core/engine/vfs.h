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

// Reads a whole file into `out`. Returns false and leaves `out` unchanged on failure.
// `path` is the path as the game already writes it ("assets/data/story.json"). On Android the
// leading "assets/" is stripped, because asset paths inside the APK are relative to that root.
inline bool vfsReadAll(const std::string& path, std::string& out) {
#if defined(__ANDROID__)
    if (g_vfsAssetManager) {
        std::string assetPath = path;
        const std::string prefix = "assets/";
        if (assetPath.rfind(prefix, 0) == 0) assetPath = assetPath.substr(prefix.size());
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

inline bool vfsExists(const std::string& path) {
#if defined(__ANDROID__)
    if (g_vfsAssetManager) {
        std::string assetPath = path;
        const std::string prefix = "assets/";
        if (assetPath.rfind(prefix, 0) == 0) assetPath = assetPath.substr(prefix.size());
        if (AAsset* a = AAssetManager_open(g_vfsAssetManager, assetPath.c_str(), AASSET_MODE_BUFFER)) {
            AAsset_close(a);
            return true;
        }
    }
#endif
    if (FILE* fp = fopen(path.c_str(), "rb")) { fclose(fp); return true; }
    return false;
}

}  // namespace toms

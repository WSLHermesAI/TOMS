// shared_textures.h -- textures one renderer lends to another, by the file they came from.
//
// The map renderer (BgfxRenderer) uploads the sprite atlas page (media/atlas/game.png). The UI
// (RmlUi) draws its icons from the same page through the sprite sheet the game makes from the
// atlas, and would load that PNG a second time. Instead BgfxRenderer lends its texture here, and
// the UI's texture loader (rml_ui.cpp) borrows it when asked for that file: one GPU copy for both.
//
// The lender owns the texture. The version changes whenever what is lent changes (another art
// style's atlas), so the borrower drops what it holds before the old texture can be used again.
// Engine-internal; single-threaded like the rest of the renderer code.
#pragma once
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace toms::next {

struct SharedTexture {
    uint16_t handle = 0xffff;   // bgfx::TextureHandle::idx
    int w = 0, h = 0;
    bool straightAlpha = true;  // BgfxRenderer's textures are not premultiplied
};

namespace shared_detail {
struct Registry {
    std::map<std::string, SharedTexture> byPath;
    int version = 0;
};
inline Registry& registry() {
    static Registry r;
    return r;
}
}  // namespace shared_detail

// The key a path is matched by: '/' separators, "a/b/../c" folded, and lower case (Windows paths
// differ in case between the game's asset folder and the folder RmlUi joins a URL to).
inline std::string sharedTextureKey(const std::string& path) {
    std::vector<std::string> parts;
    std::string seg;
    auto flush = [&] {
        if (seg == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        seg.clear();
    };
    for (char c : path) {
        if (c == '/' || c == '\\') flush();
        else seg += (char)std::tolower((unsigned char)c);
    }
    flush();
    std::string out = !path.empty() && (path[0] == '/' || path[0] == '\\') ? "/" : "";
    for (size_t i = 0; i < parts.size(); i++) out += (i ? "/" : "") + parts[i];
    return out;
}

inline void lendTexture(const std::string& path, const SharedTexture& t) {
    shared_detail::registry().byPath[sharedTextureKey(path)] = t;
    shared_detail::registry().version++;
}

inline void withdrawAllTextures() {
    if (shared_detail::registry().byPath.empty()) return;
    shared_detail::registry().byPath.clear();
    shared_detail::registry().version++;
}

inline const SharedTexture* findLentTexture(const std::string& path) {
    auto& m = shared_detail::registry().byPath;
    auto it = m.find(sharedTextureKey(path));
    return it == m.end() ? nullptr : &it->second;
}

inline int lentTexturesVersion() { return shared_detail::registry().version; }

}  // namespace toms::next

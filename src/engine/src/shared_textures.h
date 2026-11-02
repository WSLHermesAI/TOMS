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
    uint16_t m_u16Handle = 0xffff;   // bgfx::TextureHandle::idx
    int m_iW = 0, m_iH = 0;
    bool m_bStraightAlpha = true;  // BgfxRenderer's textures are not premultiplied
};

namespace shared_detail {
struct Registry {
    std::map<std::string, SharedTexture> m_mapByPath;
    int m_iVersion = 0;
};
inline Registry& f_Registry() {
    static Registry l_oR;
    return l_oR;
}
}  // namespace shared_detail

// The key a path is matched by: '/' separators, "a/b/../c" folded, and lower case (Windows paths
// differ in case between the game's asset folder and the folder RmlUi joins a URL to).
inline std::string f_SharedTextureKey(const std::string& l_strPath) {
    std::vector<std::string> l_vParts;
    std::string l_strSeg;
    auto l_fnFlush = [&] {
        if (l_strSeg == ".." && !l_vParts.empty() && l_vParts.back() != "..") l_vParts.pop_back();
        else if (!l_strSeg.empty() && l_strSeg != ".") l_vParts.push_back(l_strSeg);
        l_strSeg.clear();
    };
    for (char l_cC : l_strPath) {
        if (l_cC == '/' || l_cC == '\\') l_fnFlush();
        else l_strSeg += (char)std::tolower((unsigned char)l_cC);
    }
    l_fnFlush();
    std::string l_strOut = !l_strPath.empty() && (l_strPath[0] == '/' || l_strPath[0] == '\\') ? "/" : "";
    for (size_t l_nI = 0; l_nI < l_vParts.size(); l_nI++) l_strOut += (l_nI ? "/" : "") + l_vParts[l_nI];
    return l_strOut;
}

inline void f_LendTexture(const std::string& l_strPath, const SharedTexture& l_oT) {
    shared_detail::f_Registry().m_mapByPath[f_SharedTextureKey(l_strPath)] = l_oT;
    shared_detail::f_Registry().m_iVersion++;
}

inline void f_WithdrawAllTextures() {
    if (shared_detail::f_Registry().m_mapByPath.empty()) return;
    shared_detail::f_Registry().m_mapByPath.clear();
    shared_detail::f_Registry().m_iVersion++;
}

inline const SharedTexture* f_FindLentTexture(const std::string& l_strPath) {
    auto& l_mapM = shared_detail::f_Registry().m_mapByPath;
    auto l_itIt = l_mapM.find(f_SharedTextureKey(l_strPath));
    return l_itIt == l_mapM.end() ? nullptr : &l_itIt->second;
}

inline int f_LentTexturesVersion() { return shared_detail::f_Registry().m_iVersion; }

}  // namespace toms::next

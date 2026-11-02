// art_styles.h — the selectable art styles (Settings > art style).
//
// A style is a cooked atlas assets/media/styles/<id>/atlas/game.atlas (built from the replacement PNGs in
// art_raw/styles/<id>/sprites/ by art_raw/atlas/game.atlasproj's variant <id>); a sprite the style does not have
// keeps the original art. The
// list lives in assets/media/styles/styles.json (folders cannot be listed inside the Android APK):
//
//   { "styles": [ { "id": "dark16", "name": { "zh_TW": "暗黑奇幻", "en": "Dark fantasy" } } ] }
//
// tools/art/make_variant.py --install writes both. The original art is always style 0 (id "").
// The chosen style is GameSettings::artStyle; Game::loadAssets reads it once at startup.
#pragma once
#include <json.hpp>
#include <string>
#include <vector>

namespace toms {

struct ArtStyle {
    std::string m_strId;          // folder name under styles/ ("" = the original art)
    nlohmann::json m_jName;     // {"zh_TW": ..., "en": ...} (Locale::field); empty for the original
};

// Style 0 is always the original art; styles.json entries follow (bad/duplicate ids are skipped).
inline std::vector<ArtStyle> f_ArtStylesFromJson(const nlohmann::json& l_jJ) {
    std::vector<ArtStyle> l_vOut{ArtStyle{}};
    if (!l_jJ.is_object() || !l_jJ.contains("styles") || !l_jJ["styles"].is_array()) return l_vOut;
    for (const auto& l_jS : l_jJ["styles"]) {
        if (!l_jS.is_object()) continue;
        std::string l_strId = l_jS.value("id", std::string());
        // a folder name only: no path separators or "..", so a style can never point outside styles/
        if (l_strId.empty() || l_strId.find_first_of("/\\.") != std::string::npos) continue;
        bool l_bDup = false;
        for (const auto& l_oO : l_vOut) l_bDup = l_bDup || l_oO.m_strId == l_strId;
        if (l_bDup) continue;
        l_vOut.push_back(ArtStyle{l_strId, l_jS.contains("name") ? l_jS["name"] : nlohmann::json(l_strId)});
    }
    return l_vOut;
}

// Index of `id` in `styles`, or 0 (the original art) when it is unknown, e.g. a style that was
// chosen once and later removed from styles.json.
inline int f_ArtStyleIndex(const std::vector<ArtStyle>& l_vStyles, const std::string& l_strId) {
    for (size_t l_nI = 0; l_nI < l_vStyles.size(); l_nI++)
        if (l_vStyles[l_nI].m_strId == l_strId) return (int)l_nI;
    return 0;
}

} // namespace toms

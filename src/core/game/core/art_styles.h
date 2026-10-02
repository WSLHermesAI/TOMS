// art_styles.h — the selectable art styles (Settings > art style).
//
// A style is a folder assets/media/styles/<id>/sprites/ holding replacement PNGs for some or all of
// the atlas sprites (SPRITE_ORDER); a sprite the style does not have keeps the original art. The
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
    std::string id;          // folder name under styles/ ("" = the original art)
    nlohmann::json name;     // {"zh_TW": ..., "en": ...} (Locale::field); empty for the original
};

// Style 0 is always the original art; styles.json entries follow (bad/duplicate ids are skipped).
inline std::vector<ArtStyle> artStylesFromJson(const nlohmann::json& j) {
    std::vector<ArtStyle> out{ArtStyle{}};
    if (!j.is_object() || !j.contains("styles") || !j["styles"].is_array()) return out;
    for (const auto& s : j["styles"]) {
        if (!s.is_object()) continue;
        std::string id = s.value("id", std::string());
        // a folder name only: no path separators or "..", so a style can never point outside styles/
        if (id.empty() || id.find_first_of("/\\.") != std::string::npos) continue;
        bool dup = false;
        for (const auto& o : out) dup = dup || o.id == id;
        if (dup) continue;
        out.push_back(ArtStyle{id, s.contains("name") ? s["name"] : nlohmann::json(id)});
    }
    return out;
}

// Index of `id` in `styles`, or 0 (the original art) when it is unknown, e.g. a style that was
// chosen once and later removed from styles.json.
inline int artStyleIndex(const std::vector<ArtStyle>& styles, const std::string& id) {
    for (size_t i = 0; i < styles.size(); i++)
        if (styles[i].id == id) return (int)i;
    return 0;
}

} // namespace toms

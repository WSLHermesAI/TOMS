// localization.h — language selection + key -> localized string lookup.
//
// The title phase's Settings page picks a language; every user-facing label that has been
// converted to a key goes through Locale::tr(). Strings live in data/text.json, so adding a
// language (or fixing a translation) is a content change, not a code change.
//
// Design notes:
//  - Standalone: no dependency on Game/renderer/Vulkan, so it is headless-testable
//    (title_screen_test) exactly like save_system is.
//  - A small built-in fallback table covers the title/menu/settings keys, so a missing or
//    broken data/text.json degrades to English instead of blanking the UI out.
//  - Missing keys return the key itself (visible in-game as "menu.new_game"), which makes an
//    untranslated string obvious rather than silently empty.
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace toms {

struct LanguageInfo {
    std::string m_strCode;   // "zh_TW", "en"
    std::string m_strName;   // endonym shown in Settings: 繁體中文 / English
    // UI font for this language (docs/gameplay/01_RMLUI.md, "Fonts"). Desktop: `font` is a .ttf/.otf/.ttc
    // (relative to assets/media, or absolute); used when set and the file exists, else the
    // bundled NotoSansCJKtc-TOMS.otf -- which also covers characters `font` lacks. Web: no font
    // file; the browser draws text with the CSS font list `webFont` (empty = its default).
    std::string m_strFont;
    std::string m_strWebFont;
};

class Locale {
public:
    Locale();

    // Replaces the string table from a parsed data/text.json. Returns false (leaving the
    // previous table + built-in fallbacks intact) if j is not an object with a "strings" map.
    // Its "offered" list limits the languages the player can pick; "default" is the starting one.
    bool f_LoadFromJson(const nlohmann::json& l_jJ);
    // Convenience wrapper: reads `path` (silently keeping the current table when unreadable).
    bool f_LoadFromFile(const std::string& l_strPath);

    const std::vector<LanguageInfo>& f_Languages() const { return m_vLangs; }
    int f_LanguageCount() const { return (int)m_vLangs.size(); }
    int f_LanguageIndex() const { return m_iIdx; }
    const std::string& f_LanguageCode() const;
    std::string f_LanguageName() const;

    // Unknown codes leave the current language unchanged (returns false).
    bool f_SetLanguage(const std::string& l_strCode);
    bool f_SetLanguageIndex(int l_iI);

    // Localized string for `key` in the current language; falls back to en, then to the
    // built-in table, then to `key` itself.
    std::string f_Tr(const std::string& l_strKey) const;
    bool f_Has(const std::string& l_strKey) const;
    // How many keys the loaded table defines (used by tests to prove the file was parsed).
    size_t f_StringCount() const { return m_mapStrings.size(); }

    // Resolves a CONTENT json field that may be either a plain string (not yet converted --
    // returned as-is, so migrating data/*.json to multi-language fields can happen file by
    // file) or a {code: text} object (current language -> en -> first available -> ""), the
    // same fallback order as tr(). Used for items/equipment/missions/dialogue/etc., where the
    // translatable text lives inline in content data rather than under a text.json key.
    std::string f_Field(const nlohmann::json& l_jJ) const;

private:
    std::vector<LanguageInfo> m_vLangs;
    // key -> (language code -> text)
    std::map<std::string, std::map<std::string, std::string>> m_mapStrings;
    int m_iIdx = 0;
};

} // namespace toms

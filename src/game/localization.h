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
    std::string code;   // "zh_TW", "en"
    std::string name;   // endonym shown in Settings: 繁體中文 / English
};

class Locale {
public:
    Locale();

    // Replaces the string table from a parsed data/text.json. Returns false (leaving the
    // previous table + built-in fallbacks intact) if j is not an object with a "strings" map.
    bool loadFromJson(const nlohmann::json& j);
    // Convenience wrapper: reads `path` (silently keeping the current table when unreadable).
    bool loadFromFile(const std::string& path);

    const std::vector<LanguageInfo>& languages() const { return langs_; }
    int languageCount() const { return (int)langs_.size(); }
    int languageIndex() const { return idx_; }
    const std::string& languageCode() const;
    std::string languageName() const;

    // Unknown codes leave the current language unchanged (returns false).
    bool setLanguage(const std::string& code);
    bool setLanguageIndex(int i);

    // Localized string for `key` in the current language; falls back to en, then to the
    // built-in table, then to `key` itself.
    std::string tr(const std::string& key) const;
    bool has(const std::string& key) const;
    // How many keys the loaded table defines (used by tests to prove the file was parsed).
    size_t stringCount() const { return strings_.size(); }

    // Resolves a CONTENT json field that may be either a plain string (not yet converted --
    // returned as-is, so migrating data/*.json to multi-language fields can happen file by
    // file) or a {code: text} object (current language -> en -> first available -> ""), the
    // same fallback order as tr(). Used for items/equipment/missions/dialogue/etc., where the
    // translatable text lives inline in content data rather than under a text.json key.
    std::string field(const nlohmann::json& j) const;

private:
    std::vector<LanguageInfo> langs_;
    // key -> (language code -> text)
    std::map<std::string, std::map<std::string, std::string>> strings_;
    int idx_ = 0;
};

} // namespace toms

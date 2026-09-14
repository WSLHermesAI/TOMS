#include "localization.h"
#include <cstdio>
#include <fstream>

namespace toms {

namespace {

// Last-resort table: the title phase must be usable even if data/text.json is missing or
// half-written (e.g. an interrupted build/deploy). Kept deliberately small — it only covers
// the strings this file's own UI needs, since content text lives in the JSON.
const char* const kFallbackKeys[][3] = {
    // key,          zh_TW,               en
    {"game.title",          "魔法塔",                    "Tower of the Sorcerer"},
    {"menu.new_game",       "新遊戲",                    "New Game"},
    {"menu.continue",       "繼續遊戲",                  "Continue"},
    {"menu.settings",       "設定",                      "Settings"},
    {"menu.back",           "返回",                      "Back"},
    {"continue.header",     "選擇存檔",                  "Select a Save File"},
    {"continue.empty",      "空",                        "Empty"},
    {"continue.hint",       "沒有存檔時請先開始新遊戲",   "No saves yet — start a new game first"},
    {"continue.new_game_confirm",      "要用存檔格 {slot} 開始新遊戲嗎？", "Use slot {slot} to start the game?"},
    {"continue.new_game_confirm.body", "這個存檔格是空的，選「是」會從第一層開始新遊戲。", "This slot is empty. Choosing Yes starts a new game from stage 1."},
    {"menu.yes",            "是",                        "Yes"},
    {"menu.no",             "否",                        "No"},
    {"settings.header",     "設定",                      "Settings"},
    {"settings.language",   "語言",                      "Language"},
    {"settings.hint",       "左右鍵切換  Esc 返回",        "Left/Right: change   Esc: back"},
    {"menu.hint",           "上下鍵選擇  Enter 確定  Esc 返回", "Up/Down: select   Enter: confirm   Esc: back"},
};

} // namespace

Locale::Locale() {
    langs_ = { {"zh_TW", "繁體中文"}, {"en", "English"} };
    for (auto& row : kFallbackKeys) {
        strings_[row[0]]["zh_TW"] = row[1];
        strings_[row[0]]["en"]    = row[2];
    }
}

bool Locale::loadFromJson(const nlohmann::json& j) {
    if (!j.is_object()) return false;
    bool ok = false;

    if (j.contains("languages") && j["languages"].is_array()) {
        std::vector<LanguageInfo> langs;
        for (auto& e : j["languages"]) {
            if (!e.is_object()) continue;
            LanguageInfo li;
            li.code = e.value("code", std::string());
            li.name = e.value("name", std::string());
            if (!li.code.empty()) langs.push_back(li);
        }
        if (!langs.empty()) { langs_ = langs; idx_ = 0; }
    }

    if (j.contains("strings") && j["strings"].is_object()) {
        for (auto& [key, langs] : j["strings"].items()) {
            if (!langs.is_object()) continue;
            for (auto& [code, text] : langs.items()) {
                if (!text.is_string()) continue;
                strings_[key][code] = text.get<std::string>();
                ok = true;
            }
        }
    }
    return ok;
}

bool Locale::loadFromFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "[locale] cannot open %s (using built-in strings)\n", path.c_str()); return false; }
    std::string buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.empty()) { fprintf(stderr, "[locale] empty %s (using built-in strings)\n", path.c_str()); return false; }
    try {
        return loadFromJson(nlohmann::json::parse(buf));
    } catch (const std::exception& e) {
        fprintf(stderr, "[locale] parse error in %s: %s (using built-in strings)\n", path.c_str(), e.what());
        return false;
    }
}

const std::string& Locale::languageCode() const {
    static const std::string empty;
    if (idx_ < 0 || idx_ >= (int)langs_.size()) return empty;
    return langs_[idx_].code;
}

std::string Locale::languageName() const {
    if (idx_ < 0 || idx_ >= (int)langs_.size()) return std::string();
    return langs_[idx_].name;
}

bool Locale::setLanguage(const std::string& code) {
    for (size_t i = 0; i < langs_.size(); i++)
        if (langs_[i].code == code) { idx_ = (int)i; return true; }
    return false;
}

bool Locale::setLanguageIndex(int i) {
    if (i < 0 || i >= (int)langs_.size()) return false;
    idx_ = i;
    return true;
}

bool Locale::has(const std::string& key) const { return strings_.find(key) != strings_.end(); }

std::string Locale::tr(const std::string& key) const {
    auto it = strings_.find(key);
    if (it == strings_.end()) return key;
    const std::string& code = languageCode();
    auto want = it->second.find(code);
    if (want != it->second.end()) return want->second;
    // Fall back to English, then to any translation at all, then to the key.
    auto en = it->second.find("en");
    if (en != it->second.end()) return en->second;
    if (!it->second.empty()) return it->second.begin()->second;
    return key;
}

std::string Locale::field(const nlohmann::json& j) const {
    if (j.is_string()) return j.get<std::string>();
    if (!j.is_object()) return std::string();
    const std::string& code = languageCode();
    if (auto it = j.find(code); it != j.end() && it->is_string()) return it->get<std::string>();
    if (auto it = j.find("en"); it != j.end() && it->is_string()) return it->get<std::string>();
    for (auto& [k, v] : j.items()) if (v.is_string()) return v.get<std::string>();
    return std::string();
}

} // namespace toms

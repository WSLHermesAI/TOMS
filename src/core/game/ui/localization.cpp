#include "../../engine/vfs.h"
#include "localization.h"
#include <cstdio>
#include <fstream>

namespace toms {

namespace {

// Last-resort table: the title phase must be usable even if data/text.json is missing or
// half-written (e.g. an interrupted build/deploy). Kept deliberately small — it only covers
// the strings this file's own UI needs, since content text lives in the JSON.
const char* const g_arrFallbackKeys[][3] = {
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
    m_vLangs = { {"zh_TW", "繁體中文"}, {"en", "English"} };
    m_iIdx = 1;   // English is the default language
    for (auto& l_arrRow : g_arrFallbackKeys) {
        m_mapStrings[l_arrRow[0]]["zh_TW"] = l_arrRow[1];
        m_mapStrings[l_arrRow[0]]["en"]    = l_arrRow[2];
    }
}

bool Locale::f_LoadFromJson(const nlohmann::json& l_jJ) {
    if (!l_jJ.is_object()) return false;
    bool l_bOk = false;

    if (l_jJ.contains("languages") && l_jJ["languages"].is_array()) {
        std::vector<LanguageInfo> l_vLangs;
        for (auto& l_jE : l_jJ["languages"]) {
            if (!l_jE.is_object()) continue;
            LanguageInfo l_oLi;
            l_oLi.m_strCode = l_jE.value("code", std::string());
            l_oLi.m_strName = l_jE.value("name", std::string());
            l_oLi.m_strFont = l_jE.value("font", std::string());
            l_oLi.m_strWebFont = l_jE.value("web_font", std::string());
            if (!l_oLi.m_strCode.empty()) l_vLangs.push_back(l_oLi);
        }
        // "offered": the languages the player can pick (the owner, 2026-11-02: Traditional Chinese and English for now;
        // the others keep their strings for later). Missing: every language listed.
        if (l_jJ.contains("offered") && l_jJ["offered"].is_array()) {
            std::vector<LanguageInfo> l_vKept;
            for (const LanguageInfo& l_oLi : l_vLangs)
                for (const auto& l_jO : l_jJ["offered"])
                    if (l_jO.is_string() && l_jO.get<std::string>() == l_oLi.m_strCode) { l_vKept.push_back(l_oLi); break; }
            if (!l_vKept.empty()) l_vLangs = l_vKept;
        }
        if (!l_vLangs.empty()) {
            m_vLangs = l_vLangs;
            m_iIdx = 0;
            // "default": the language a new player starts in (Traditional Chinese, 2026-11-02); else English
            const std::string l_strDef = l_jJ.value("default", std::string("en"));
            bool l_bFound = false;
            for (size_t l_nI = 0; l_nI < m_vLangs.size() && !l_bFound; l_nI++)
                if (m_vLangs[l_nI].m_strCode == l_strDef) { m_iIdx = (int)l_nI; l_bFound = true; }
            for (size_t l_nI = 0; l_nI < m_vLangs.size() && !l_bFound; l_nI++)
                if (m_vLangs[l_nI].m_strCode == "en") { m_iIdx = (int)l_nI; l_bFound = true; }
        }
    }

    if (l_jJ.contains("strings") && l_jJ["strings"].is_object()) {
        for (auto& [l_strKey, l_jLangs] : l_jJ["strings"].items()) {
            if (!l_jLangs.is_object()) continue;
            for (auto& [l_strCode, l_jText] : l_jLangs.items()) {
                if (!l_jText.is_string()) continue;
                m_mapStrings[l_strKey][l_strCode] = l_jText.get<std::string>();
                l_bOk = true;
            }
        }
    }
    return l_bOk;
}

bool Locale::f_LoadFromFile(const std::string& l_strPath) {
    std::string l_strBuf;
    if (!toms::f_VfsReadAll(l_strPath, l_strBuf)) { fprintf(stderr, "[locale] cannot open %s (using built-in strings)\n", l_strPath.c_str()); return false; }
    if (l_strBuf.empty()) { fprintf(stderr, "[locale] empty %s (using built-in strings)\n", l_strPath.c_str()); return false; }
    try {
        return f_LoadFromJson(nlohmann::json::parse(l_strBuf));
    } catch (const std::exception& l_oE) {
        fprintf(stderr, "[locale] parse error in %s: %s (using built-in strings)\n", l_strPath.c_str(), l_oE.what());
        return false;
    }
}

const std::string& Locale::f_LanguageCode() const {
    static const std::string l_strEmpty;
    if (m_iIdx < 0 || m_iIdx >= (int)m_vLangs.size()) return l_strEmpty;
    return m_vLangs[m_iIdx].m_strCode;
}

std::string Locale::f_LanguageName() const {
    if (m_iIdx < 0 || m_iIdx >= (int)m_vLangs.size()) return std::string();
    return m_vLangs[m_iIdx].m_strName;
}

bool Locale::f_SetLanguage(const std::string& l_strCode) {
    for (size_t l_nI = 0; l_nI < m_vLangs.size(); l_nI++)
        if (m_vLangs[l_nI].m_strCode == l_strCode) { m_iIdx = (int)l_nI; return true; }
    return false;
}

bool Locale::f_SetLanguageIndex(int l_iI) {
    if (l_iI < 0 || l_iI >= (int)m_vLangs.size()) return false;
    m_iIdx = l_iI;
    return true;
}

bool Locale::f_Has(const std::string& l_strKey) const { return m_mapStrings.find(l_strKey) != m_mapStrings.end(); }

std::string Locale::f_Tr(const std::string& l_strKey) const {
    auto l_itIt = m_mapStrings.find(l_strKey);
    if (l_itIt == m_mapStrings.end()) return l_strKey;
    const std::string& l_strCode = f_LanguageCode();
    auto l_itWant = l_itIt->second.find(l_strCode);
    if (l_itWant != l_itIt->second.end()) return l_itWant->second;
    // Fall back to English, then to any translation at all, then to the key.
    auto l_itEn = l_itIt->second.find("en");
    if (l_itEn != l_itIt->second.end()) return l_itEn->second;
    if (!l_itIt->second.empty()) return l_itIt->second.begin()->second;
    return l_strKey;
}

std::string Locale::f_Field(const nlohmann::json& l_jJ) const {
    if (l_jJ.is_string()) return l_jJ.get<std::string>();
    if (!l_jJ.is_object()) return std::string();
    const std::string& l_strCode = f_LanguageCode();
    if (auto l_oIt = l_jJ.find(l_strCode); l_oIt != l_jJ.end() && l_oIt->is_string()) return l_oIt->get<std::string>();
    if (auto l_oIt = l_jJ.find("en"); l_oIt != l_jJ.end() && l_oIt->is_string()) return l_oIt->get<std::string>();
    for (auto& [l_strK, l_jV] : l_jJ.items()) if (l_jV.is_string()) return l_jV.get<std::string>();
    return std::string();
}

} // namespace toms

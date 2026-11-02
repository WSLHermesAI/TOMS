// event_project.cpp -- see event_project.h. Ported from tools/web_editors/event_editor.html (readLiveData, fresh,
// problems, liveWrites, the talk preview) and make_data.py.
#include "event_project.h"

#include "stage_save.h"   // timeStamp

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>
#include <regex>

namespace fs = std::filesystem;

namespace stage {

const char* const g_arrLangs[6] = {"zh_TW", "en", "zh_CN", "ja", "ko", "es"};

namespace {

bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
bool f_EndsWith(const std::string& l_strS, const std::string& l_strP) { return l_strS.size() >= l_strP.size() && l_strS.compare(l_strS.size() - l_strP.size(), l_strP.size(), l_strP) == 0; }

std::vector<std::string> f_Names(const std::string& l_strDir, const std::string& l_strPrefix, const std::string& l_strSuffix) {
    std::vector<std::string> l_vOut;
    std::error_code l_oEc;
    for (const auto& l_oE : fs::directory_iterator(fs::u8path(l_strDir), l_oEc)) {
        if (!l_oE.is_regular_file(l_oEc)) continue;
        const std::string l_strN = l_oE.path().filename().u8string();
        if (f_StartsWith(l_strN, l_strPrefix.c_str()) && f_EndsWith(l_strN, l_strSuffix)) l_vOut.push_back(l_strN);
    }
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

const Json* f_Member(const Json& l_jJ, const char* l_pszKey) {
    if (!l_jJ.is_object()) return nullptr;
    auto l_oIt = l_jJ.find(l_pszKey);
    return l_oIt == l_jJ.end() ? nullptr : &*l_oIt;
}
std::string f_Str(const Json& l_jJ, const char* l_pszKey, const std::string& l_strDef = std::string()) {
    const Json* l_pV = f_Member(l_jJ, l_pszKey);
    return l_pV && l_pV->is_string() ? l_pV->get<std::string>() : l_strDef;
}
Json f_NoUnderscore(const Json& l_jO) {
    Json l_jOut = Json::object();
    if (l_jO.is_object())
        for (auto& [l_strK, l_jV] : l_jO.items()) if (!f_StartsWith(l_strK, "_")) l_jOut[l_strK] = l_jV;
    return l_jOut;
}
bool f_Contains(const Json& l_jArr, const std::string& l_strS) {
    return l_jArr.is_array() && std::find(l_jArr.begin(), l_jArr.end(), Json(l_strS)) != l_jArr.end();
}
bool f_Contains(const std::vector<std::string>& l_vV, const std::string& l_strS) { return std::find(l_vV.begin(), l_vV.end(), l_strS) != l_vV.end(); }
double f_Num(const Json& l_jV) {   // JS +v, with true -> 1
    if (l_jV.is_boolean()) return l_jV.get<bool>() ? 1 : 0;
    if (l_jV.is_number()) return l_jV.get<double>();
    if (l_jV.is_string()) { try { return std::stod(l_jV.get<std::string>()); } catch (...) { return 0; } }
    return 0;
}
bool f_Truthy(const Json& l_jV) {
    if (l_jV.is_null()) return false;
    if (l_jV.is_boolean()) return l_jV.get<bool>();
    if (l_jV.is_string()) return !l_jV.get_ref<const std::string&>().empty();
    if (l_jV.is_number()) return l_jV.get<double>() != 0;
    return true;
}
std::string f_Join(const std::vector<std::string>& l_vV, const char* l_pszSep) {
    std::string l_strOut;
    for (size_t l_nI = 0; l_nI < l_vV.size(); l_nI++) l_strOut += (l_nI ? l_pszSep : "") + l_vV[l_nI];
    return l_strOut;
}

const Json& f_DefaultKinds() {
    static const Json l_jK = Json::parse(R"({
  "relic":         { "name": { "zh_TW": "遺物", "en": "Relic" },          "color": "#e0b44c" },
  "whisper":       { "name": { "zh_TW": "低語", "en": "Whisper" },        "color": "#a98bff" },
  "cache":         { "name": { "zh_TW": "補給", "en": "Cache" },          "color": "#3fc2b5" },
  "trap":          { "name": { "zh_TW": "陷阱", "en": "Trap" },           "color": "#ff6b6b" },
  "rescue":        { "name": { "zh_TW": "救援", "en": "Rescue" },         "color": "#7bd86a" },
  "merchant_echo": { "name": { "zh_TW": "商人殘影", "en": "Merchant echo" }, "color": "#ff8fc7" },
  "memory_shard":  { "name": { "zh_TW": "記憶碎片", "en": "Memory shard" }, "color": "#74c6ff" }
})");
    return l_jK;
}

// event logic in the editor's shape (logicFrom)
void f_LogicInto(Json& l_jOut, const Json& l_jE) {
    const Json* l_pRq = f_Member(l_jE, "requires");
    Json l_jReq = Json::array(), l_jReqRaw = nullptr;
    if (l_pRq && l_pRq->is_object() && l_pRq->contains("type")) l_jReq.push_back(*l_pRq);
    else if (l_pRq && l_pRq->is_object() && l_pRq->contains("all") && (*l_pRq)["all"].is_array()
             && std::all_of((*l_pRq)["all"].begin(), (*l_pRq)["all"].end(), [](const Json& l_jX) { return l_jX.is_object() && l_jX.contains("type"); }))
        l_jReq = (*l_pRq)["all"];
    else if (l_pRq && !l_pRq->is_null()) l_jReqRaw = *l_pRq;
    l_jOut["trigger"] = f_Member(l_jE, "trigger") ? l_jE["trigger"] : Json{{"on", "step"}};
    l_jOut["req"] = l_jReq;
    l_jOut["reqRaw"] = l_jReqRaw;
    l_jOut["actions"] = f_Member(l_jE, "actions") && l_jE["actions"].is_array() ? l_jE["actions"] : Json::array();
    l_jOut["next"] = f_Member(l_jE, "next") && l_jE["next"].is_array() ? l_jE["next"] : Json::array();
    l_jOut["dialogue"] = f_Str(l_jE, "dialogue");
    l_jOut["afterDialogue"] = f_Str(l_jE, "afterDialogue");
    l_jOut["topicOrder"] = f_Member(l_jE, "topicOrder") && l_jE["topicOrder"].is_number_integer() ? l_jE["topicOrder"] : Json();
    l_jOut["once"] = !(f_Member(l_jE, "once") && l_jE["once"] == false);
    l_jOut["maxTimes"] = f_Member(l_jE, "maxTimes") && l_jE["maxTimes"].is_number_integer() ? l_jE["maxTimes"].get<int>() : 0;
}

// the line of a record in flags.json / counters.json (hand-aligned: each record on one line)
std::string f_OneLine(const Json& l_jV) {
    if (l_jV.is_array()) {
        std::string l_strS = "[";
        for (size_t l_nI = 0; l_nI < l_jV.size(); l_nI++) l_strS += (l_nI ? ", " : "") + f_OneLine(l_jV[l_nI]);
        return l_strS + "]";
    }
    if (l_jV.is_object()) {
        if (l_jV.empty()) return "{}";
        std::string l_strS = "{ ";
        bool l_bFirst = true;
        for (auto& [l_strK, l_jX] : l_jV.items()) { l_strS += (l_bFirst ? "" : ", ") + Json(l_strK).dump() + ": " + f_OneLine(l_jX); l_bFirst = false; }
        return l_strS + " }";
    }
    return l_jV.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string f_AsText(const Json& l_jObj, const std::string* l_pBefore) {
    std::string l_strS = l_jObj.dump(2, ' ', false, Json::error_handler_t::replace) + "\n";
    if (l_pBefore && l_pBefore->find("\r\n") != std::string::npos) {
        std::string l_strOut;
        for (char l_cC : l_strS) { if (l_cC == '\n') l_strOut += '\r'; l_strOut += l_cC; }
        return l_strOut;
    }
    return l_strS;
}

std::string f_RegexEscape(const std::string& l_strS) {
    static const std::string l_strSpecial = R"(.^$|()[]{}*+?\)";
    std::string l_strOut;
    for (char l_cC : l_strS) { if (l_strSpecial.find(l_cC) != std::string::npos) l_strOut += '\\'; l_strOut += l_cC; }
    return l_strOut;
}

// flags.json / counters.json: replace the changed entries' one-line records, add new ones before the closing brace;
// "" when the file cannot be patched that way (the caller rewrites it)
std::string f_PatchEntries(const std::string& l_strRaw, const Json& l_jObj, const std::vector<std::pair<std::string, Json>>& l_vChanges) {
    const std::string l_strNl = l_strRaw.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> l_vLines;
    {
        std::string l_strCur;
        for (size_t l_nI = 0; l_nI < l_strRaw.size(); l_nI++) {
            if (l_strRaw[l_nI] == '\r' && l_nI + 1 < l_strRaw.size() && l_strRaw[l_nI + 1] == '\n') continue;
            if (l_strRaw[l_nI] == '\n') { l_vLines.push_back(l_strCur); l_strCur.clear(); } else l_strCur += l_strRaw[l_nI];
        }
        l_vLines.push_back(l_strCur);
    }
    auto l_fnTrim = [](const std::string& l_strS) { size_t l_nA = l_strS.find_first_not_of(" \t"), l_nB = l_strS.find_last_not_of(" \t"); return l_nA == std::string::npos ? std::string() : l_strS.substr(l_nA, l_nB - l_nA + 1); };
    for (const auto& [l_strK, l_jV] : l_vChanges) {
        const std::regex l_oRe("^(\\s*)" + f_RegexEscape(Json(l_strK).dump()) + "\\s*:\\s*\\{.*\\}(,?)\\s*$");
        bool l_bDone = false;
        for (std::string& l_strL : l_vLines) {
            std::smatch l_oM;
            if (std::regex_match(l_strL, l_oM, l_oRe)) { l_strL = l_oM[1].str() + Json(l_strK).dump() + ": " + f_OneLine(l_jV) + l_oM[2].str(); l_bDone = true; break; }
        }
        if (l_bDone) continue;
        if (l_jObj.contains(l_strK)) return std::string();   // a multi-line record: rewrite the file
        int l_iJ = (int)l_vLines.size() - 1;
        while (l_iJ >= 0 && l_fnTrim(l_vLines[(size_t)l_iJ]) != "}") l_iJ--;
        int l_iP = l_iJ - 1;
        while (l_iP >= 0 && l_fnTrim(l_vLines[(size_t)l_iP]).empty()) l_iP--;
        if (l_iJ < 0 || l_iP < 0) return std::string();
        const std::string l_strLp = l_fnTrim(l_vLines[(size_t)l_iP]);
        if (!l_strLp.empty() && l_strLp.back() != ',' && l_strLp.back() != '{' && l_strLp.back() != '[') l_vLines[(size_t)l_iP] += ",";
        l_vLines.insert(l_vLines.begin() + l_iJ, "  " + Json(l_strK).dump() + ": " + f_OneLine(l_jV));
    }
    std::string l_strOut;
    for (size_t l_nI = 0; l_nI < l_vLines.size(); l_nI++) l_strOut += (l_nI ? l_strNl : "") + l_vLines[l_nI];
    Json l_jWant = l_jObj;
    for (const auto& [l_strK, l_jV] : l_vChanges) l_jWant[l_strK] = l_jV;
    const Json l_jGot = Json::parse(l_strOut, nullptr, false);
    return !l_jGot.is_discarded() && l_jGot == l_jWant ? l_strOut : std::string();
}

}  // namespace

// ---- rules and condition rows --------------------------------------------------------------------------------------

const std::vector<std::pair<std::string, int>>& f_EvRules() {
    static const std::vector<std::pair<std::string, int>> l_vR = {
        {"missing_event", 0}, {"missing_text", 0}, {"orphan", 1}, {"no_kind", 1}, {"placeholder", 1}, {"missing_floor", 0},
        {"bad_unlock", 0}, {"unreachable", 1}, {"unknown_kind", 1}, {"unknown_ref", 0}, {"bad_next", 0}, {"never_fires", 1},
        {"never_active", 1}, {"logic_cycle", 1}, {"unused_var", 2}, {"untranslated", 2}, {"missing_title", 1},
        {"missing_dialogue", 0}, {"bad_dialogue", 0}};
    return l_vR;
}

int f_RuleSev(const std::string& l_strRule) {
    for (const auto& [l_strK, l_iS] : f_EvRules()) if (l_strK == l_strRule) return l_iS;
    return 1;
}

std::string f_LeafRef(const Json& l_jL) {
    const std::string l_strT = f_Str(l_jL, "type");
    if (l_strT == "varAtLeast" || l_strT == "varEquals") return "var:" + f_Str(l_jL, "var");
    if (l_strT == "counterAtLeast") return "counter:" + f_Str(l_jL, "counter");
    if (l_strT == "runFlagSet") { const std::string l_strF = f_Str(l_jL, "flag"); return f_StartsWith(l_strF, "event_") ? "event:" + l_strF.substr(6) : "flag:" + l_strF; }
    return std::string();
}

std::string f_ActRef(const Json& l_jA) {
    const std::string l_strT = f_Str(l_jA, "type");
    if (l_strT == "addVar" || l_strT == "setVar") return "var:" + f_Str(l_jA, "var");
    if (l_strT == "addCounter") return "counter:" + f_Str(l_jA, "counter");
    if (l_strT == "setFlag") return "flag:" + f_Str(l_jA, "flag");
    if (l_strT == "give") return "item:" + f_Str(l_jA, "itemId");
    return std::string();
}

std::vector<std::pair<std::string, std::string>> f_OpsFor(const std::string& l_strK) {
    if (l_strK == "var") return {{">=", "≥"}, {"=", "="}};
    if (l_strK == "flag") return {{"set", "opSet"}, {"not", "opNotSet"}};
    if (l_strK == "event") return {{"set", "opDone"}, {"not", "opNotDone"}};
    if (l_strK == "floor") return {{"set", "opCleared"}, {"not", "opNotCleared"}};
    if (l_strK == "stage") return {{"set", "opOnStage"}, {"not", "opNotOnStage"}};
    if (l_strK == "choice") return {{"=", "opChose"}};
    return {{">=", "≥"}};
}

CondRow f_LeafToRow(const Json& l_jL) {
    CondRow l_oR;
    if (l_jL.is_object() && l_jL.contains("not") && l_jL["not"].is_object()) {
        CondRow l_oIn = f_LeafToRow(l_jL["not"]);
        const std::string l_strK = l_oIn.m_strSubj.substr(0, l_oIn.m_strSubj.find(':'));
        for (const auto& [l_strOp, l_strLb] : f_OpsFor(l_strK)) { (void)l_strLb; if (l_strOp == "not" && !l_oIn.m_strSubj.empty()) { l_oIn.m_strOp = "not"; return l_oIn; } }
        return CondRow{};
    }
    const std::string l_strT = f_Str(l_jL, "type");
    auto l_fnV = [&](const char* l_pszKey, const Json& l_jDef) { return l_jL.contains(l_pszKey) ? l_jL[l_pszKey] : l_jDef; };
    const auto l_fnVarNum = [](const Json& l_jX) { return Json(l_jX.is_boolean() ? (l_jX.get<bool>() ? 1 : 0) : (int)f_Num(l_jX)); };
    if (l_strT == "varAtLeast") return {"var:" + f_Str(l_jL, "var"), ">=", l_fnVarNum(l_fnV("value", 0))};
    if (l_strT == "varEquals") return {"var:" + f_Str(l_jL, "var"), "=", l_fnVarNum(l_fnV("value", 0))};
    if (l_strT == "counterAtLeast") return {"counter:" + f_Str(l_jL, "counter"), ">=", l_fnV("min", 0)};
    if (l_strT == "runFlagSet") {
        const std::string l_strF = f_Str(l_jL, "flag");
        return f_StartsWith(l_strF, "event_") ? CondRow{"event:" + l_strF.substr(6), "set", Json()} : CondRow{"flag:" + l_strF, "set", Json()};
    }
    if (l_strT == "talkCountAtLeast") return {"talks:" + f_Str(l_jL, "npc"), ">=", l_fnV("value", 0)};
    if (l_strT == "statAtLeast") return {"stat:" + f_Str(l_jL, "stat"), ">=", l_fnV("value", 0)};
    if (l_strT == "stageCleared") return {"floor:" + f_Str(l_jL, "stageId"), "set", Json()};
    if (l_strT == "itemHeld") return {"item:" + f_Str(l_jL, "itemId"), ">=", l_fnV("count", 1)};
    if (l_strT == "onStage") return {"stage:" + f_Str(l_jL, "stageId"), "set", Json()};
    if (l_strT == "choiceMade") return {"choice:" + f_Str(l_jL, "choiceId"), "=", l_fnV("optionId", "")};
    return l_oR;
}

Json f_RowToLeaf(const std::string& l_strSubj, const std::string& l_strOp, const Json& l_jVal) {
    const size_t l_nC = l_strSubj.find(':');
    const std::string l_strK = l_strSubj.substr(0, l_nC), l_strN = l_nC == std::string::npos ? std::string() : l_strSubj.substr(l_nC + 1);
    const int l_iV = l_jVal.is_number() ? (int)l_jVal.get<double>() : l_jVal.is_string() ? (int)f_Num(l_jVal) : 0;
    Json l_jLeaf;
    if (l_strK == "var") l_jLeaf = l_strOp == "=" ? Json{{"type", "varEquals"}, {"var", l_strN}, {"value", l_iV}} : Json{{"type", "varAtLeast"}, {"var", l_strN}, {"value", l_iV}};
    else if (l_strK == "counter") l_jLeaf = Json{{"type", "counterAtLeast"}, {"counter", l_strN}, {"min", l_iV}};
    else if (l_strK == "flag") l_jLeaf = Json{{"type", "runFlagSet"}, {"flag", l_strN}};
    else if (l_strK == "event") l_jLeaf = Json{{"type", "runFlagSet"}, {"flag", "event_" + l_strN}};
    else if (l_strK == "talks") l_jLeaf = Json{{"type", "talkCountAtLeast"}, {"npc", l_strN}, {"value", l_iV}};
    else if (l_strK == "stat") l_jLeaf = Json{{"type", "statAtLeast"}, {"stat", l_strN}, {"value", l_iV}};
    else if (l_strK == "floor") l_jLeaf = Json{{"type", "stageCleared"}, {"stageId", l_strN}};
    else if (l_strK == "item") l_jLeaf = Json{{"type", "itemHeld"}, {"itemId", l_strN}, {"count", std::max(1, l_iV)}};
    else if (l_strK == "stage") l_jLeaf = Json{{"type", "onStage"}, {"stageId", l_strN}};
    else l_jLeaf = Json{{"type", "choiceMade"}, {"choiceId", l_strN}, {"optionId", l_jVal.is_string() ? l_jVal.get<std::string>() : l_jVal.is_null() ? std::string() : l_jVal.dump()}};
    return l_strOp == "not" ? Json{{"not", l_jLeaf}} : l_jLeaf;
}

CondRows f_CondToRows(const Json& l_jC) {
    CondRows l_oR;
    if (l_jC.is_null()) return l_oR;
    auto l_fnRowable = [](const Json& l_jX) { return l_jX.is_object() && !f_LeafToRow(l_jX).m_strSubj.empty(); };
    if (l_fnRowable(l_jC)) { l_oR.m_vRows.push_back(l_jC); return l_oR; }
    for (const char* l_pszM : {"all", "any"})
        if (l_jC.is_object() && l_jC.contains(l_pszM) && l_jC[l_pszM].is_array() && std::all_of(l_jC[l_pszM].begin(), l_jC[l_pszM].end(), l_fnRowable)) {
            l_oR.m_strMode = l_pszM;
            l_oR.m_vRows.assign(l_jC[l_pszM].begin(), l_jC[l_pszM].end());
            return l_oR;
        }
    l_oR.m_bRaw = true;
    return l_oR;
}

Json f_RowsToCond(const std::string& l_strMode, const std::vector<Json>& l_vRows) {
    if (l_vRows.empty()) return Json();
    if (l_vRows.size() == 1) return l_vRows[0];
    Json l_jO = Json::object();
    l_jO[l_strMode] = l_vRows;
    return l_jO;
}

Json f_MakeAction(const std::string& l_strType, const std::string& l_strTarget, const Json& l_jVal) {
    int l_iN = 1;
    if (l_jVal.is_number()) l_iN = (int)l_jVal.get<double>();
    else if (l_jVal.is_boolean()) l_iN = l_jVal.get<bool>() ? 1 : 0;
    else if (l_jVal.is_string()) { try { l_iN = std::stoi(l_jVal.get<std::string>()); } catch (...) { l_iN = 1; } }
    if (l_strType == "addVar") return Json{{"type", l_strType}, {"var", l_strTarget}, {"delta", l_iN}};
    if (l_strType == "setVar") return Json{{"type", l_strType}, {"var", l_strTarget}, {"value", l_iN}};
    if (l_strType == "addCounter") return Json{{"type", l_strType}, {"counter", l_strTarget}, {"delta", l_iN}};
    if (l_strType == "setFlag") return Json{{"type", l_strType}, {"flag", l_strTarget}};
    if (l_strType == "addStat") return Json{{"type", l_strType}, {"stat", l_strTarget}, {"delta", l_iN}};
    if (l_strType == "take") return Json{{"type", l_strType}, {"itemId", l_strTarget}};
    return Json{{"type", "give"}, {"itemId", l_strTarget}};
}

std::string f_ActTarget(const Json& l_jA) {
    for (const char* l_pszK : {"var", "counter", "flag", "stat", "itemId"})
        if (l_jA.contains(l_pszK) && l_jA[l_pszK].is_string()) return l_jA[l_pszK].get<std::string>();
    return std::string();
}

Json f_ActValue(const Json& l_jA) {
    if (l_jA.contains("delta")) return l_jA["delta"];
    if (l_jA.contains("value")) return l_jA["value"];
    return Json("");
}

bool f_NameOk(const std::string& l_strS, bool l_bFile) {
    if (l_strS.empty() || l_strS.front() == ' ' || l_strS.back() == ' ') return false;
    for (unsigned char l_u8C : l_strS) {
        if (l_u8C < 0x20 || std::string("\"'<>&|\\/").find((char)l_u8C) != std::string::npos) return false;
        if (l_bFile && std::string(":*?").find((char)l_u8C) != std::string::npos) return false;
    }
    return true;
}

bool f_IdOk(const std::string& l_strS) {
    return !l_strS.empty() && std::all_of(l_strS.begin(), l_strS.end(), [](unsigned char l_u8C) { return std::isalnum(l_u8C) || l_u8C == '_' || l_u8C == '-'; });
}

// ---- loading -------------------------------------------------------------------------------------------------------

bool EventProject::f_Load(const std::string& l_strTomsRoot, std::string& l_strErr) {
    m_strRoot = l_strTomsRoot;
    while (!m_strRoot.empty() && (m_strRoot.back() == '/' || m_strRoot.back() == '\\')) m_strRoot.pop_back();
    std::replace(m_strRoot.begin(), m_strRoot.end(), '\\', '/');
    const std::string l_strA = m_strRoot + "/assets/data/";
    Json l_jPools = Json::array(), l_jEvents = Json::array(), l_jFloors = Json::array();
    Json l_jJ;
    for (const std::string& l_strName : f_Names(l_strA + "events", "pool_", ".json")) {
        if (!f_ReadJson(l_strA + "events/" + l_strName, l_jJ, nullptr, &l_strErr)) return false;
        const std::string l_strPid = l_strName.substr(0, l_strName.size() - 5);
        l_jPools.push_back(l_strPid);
        if (const Json* l_pEvs = f_Member(l_jJ, "events"); l_pEvs && l_pEvs->is_array())
            for (const Json& l_jE : *l_pEvs) {
                const std::string l_strId = f_Str(l_jE, "eventId");
                Json l_jRec = Json::object();
                l_jRec["id"] = l_strId;
                l_jRec["kind"] = f_Str(l_jE, "kind");
                l_jRec["pool"] = l_strPid;
                l_jRec["text"] = f_Str(l_jE, "text");
                l_jRec["title"] = f_Str(l_jE, "title", l_strId + ".title");
                l_jRec["desc"] = f_Str(l_jE, "desc", l_strId + ".desc");
                if (l_jE.contains("inLog")) l_jRec["inLog"] = l_jE["inLog"];
                f_LogicInto(l_jRec, l_jE);
                l_jEvents.push_back(l_jRec);
            }
    }
    std::map<std::string, std::string> l_mapPrev;   // a floor without a stairs list: up to its next, down to the floor whose next it is
    std::vector<Json> l_vSpecs;
    for (const std::string& l_strName : f_Names(l_strA + "story/floors", "", ".json")) {
        if (f_EndsWith(l_strName, ".stage.json")) continue;
        Json l_jD;
        if (!f_ReadJson(l_strA + "story/floors/" + l_strName, l_jD, nullptr, &l_strErr)) return false;
        l_vSpecs.push_back(l_jD);
        if (const std::string l_strNx = f_Str(l_jD, "nextFloor"); !l_strNx.empty()) l_mapPrev[l_strNx] = f_Str(l_jD, "id");
    }
    for (const Json& l_jD : l_vSpecs) {
        Json l_jF = Json::object();
        const std::string l_strId = f_Str(l_jD, "id");
        l_jF["id"] = l_strId;
        l_jF["act"] = f_Str(l_jD, "act");
        l_jF["role"] = f_Str(l_jD, "role", "normal");
        l_jF["events"] = Json::array();
        if (const Json* l_pEvs = f_Member(l_jD, "events"); l_pEvs && l_pEvs->is_array())
            for (const Json& l_jX : *l_pEvs) l_jF["events"].push_back(l_jX);
        const std::string l_strNx = f_Str(l_jD, "nextFloor");
        l_jF["next"] = l_strNx.empty() ? Json() : Json(l_strNx);
        const bool l_bHas = l_jD.contains("stairs") && l_jD["stairs"].is_array();
        l_jF["derived"] = !l_bHas;
        if (l_bHas) l_jF["stairs"] = l_jD["stairs"];
        else {
            Json l_jSt = Json::array();
            if (!l_strNx.empty()) l_jSt.push_back(Json{{"dir", "up"}, {"to", l_strNx}});
            if (l_mapPrev.count(l_strId)) l_jSt.push_back(Json{{"dir", "down"}, {"to", l_mapPrev[l_strId]}});
            l_jF["stairs"] = l_jSt;
        }
        l_jFloors.push_back(l_jF);
    }
    Json l_jStrings;
    if (!f_ReadJson(l_strA + "text.json", l_jJ, nullptr, &l_strErr)) return false;
    l_jStrings = f_Member(l_jJ, "strings") ? l_jJ["strings"] : Json::object();
    std::set<std::string> l_setKnown, l_setWanted;
    for (const Json& l_jE : l_jEvents) l_setKnown.insert(l_jE["id"].get<std::string>());
    for (const Json& l_jE : l_jEvents) for (const char* l_pszK : {"text", "title", "desc"}) l_setWanted.insert(l_jE[l_pszK].get<std::string>());
    for (const Json& l_jF : l_jFloors) for (const Json& l_jX : l_jF["events"]) if (l_jX.is_string() && !l_setKnown.count(l_jX.get<std::string>())) l_setWanted.insert(l_jX.get<std::string>());
    Json l_jNpcFiles = Json::object();
    for (const std::string& l_strN : f_Names(l_strA + "npcs", "", ".json"))
        if (f_ReadJson(l_strA + "npcs/" + l_strN, l_jJ)) l_jNpcFiles[l_strN.substr(0, l_strN.size() - 5)] = l_jJ;
    for (auto& [l_strK, l_jN] : l_jNpcFiles.items()) { (void)l_strK; for (const char* l_pszF : {"name", "greeting"}) if (!f_Str(l_jN, l_pszF).empty()) l_setWanted.insert(f_Str(l_jN, l_pszF)); }
    Json l_jTexts = Json::object();
    for (const std::string& l_strK : l_setWanted) if (l_jStrings.contains(l_strK)) l_jTexts[l_strK] = l_jStrings[l_strK];
    auto l_fnOpt = [&](const std::string& l_strP) { Json l_jO; return f_ReadJson(l_strA + l_strP, l_jO) ? f_NoUnderscore(l_jO) : Json::object(); };
    Json l_jVarsRaw;
    Json l_jCats = Json::array();
    if (f_ReadJson(l_strA + "story/vars.json", l_jVarsRaw) && l_jVarsRaw.contains("_categories") && l_jVarsRaw["_categories"].is_array()) l_jCats = l_jVarsRaw["_categories"];
    Json l_jKinds = l_fnOpt("events/kinds.json");
    Json l_jDialogues = Json::object();
    for (const std::string& l_strN : f_Names(l_strA + "dialogue", "", ".json"))
        if (f_ReadJson(l_strA + "dialogue/" + l_strN, l_jJ)) l_jDialogues[l_strN.substr(0, l_strN.size() - 5)] = l_jJ;

    m_vItems.clear();
    const Json l_jItemsJ = l_fnOpt("items.json");   // a named value: items() of a temporary would dangle
    for (auto& [l_strK, l_jV] : l_jItemsJ.items()) { (void)l_jV; m_vItems.push_back(l_strK); }
    std::sort(m_vItems.begin(), m_vItems.end());
    m_vStats = {"hp", "maxhp", "atk", "def", "gold", "exp", "lv"};
    if (f_ReadJson(l_strA + "stats.json", l_jJ) && l_jJ.contains("attributes") && l_jJ["attributes"].is_array())
        for (const Json& l_jA : l_jJ["attributes"]) if (!f_Str(l_jA, "id").empty()) m_vStats.push_back(f_Str(l_jA, "id"));
    m_vSprites.clear();
    {
        std::string l_strAtlas;
        if (f_ReadText(m_strRoot + "/assets/media/atlas/game.atlas", l_strAtlas)) {
            std::string l_strLine;
            size_t l_nPos = 0, l_nLi = 0;
            while (l_nPos <= l_strAtlas.size()) {
                const size_t l_nE = l_strAtlas.find('\n', l_nPos);
                l_strLine = l_strAtlas.substr(l_nPos, (l_nE == std::string::npos ? l_strAtlas.size() : l_nE) - l_nPos);
                if (!l_strLine.empty() && l_strLine.back() == '\r') l_strLine.pop_back();
                if (l_nLi++ > 0 && !l_strLine.empty() && l_strLine[0] != ' ' && l_strLine[0] != '\t' && l_strLine.find(':') == std::string::npos && !f_EndsWith(l_strLine, ".png"))
                    m_vSprites.push_back(l_strLine);
                if (l_nE == std::string::npos) break;
                l_nPos = l_nE + 1;
            }
            std::sort(m_vSprites.begin(), m_vSprites.end());
        }
    }
    // who can be talked to, and where: npc ids placed on each floor's grid (boss floors: stages/<handAuthoredStage>)
    m_mapNpcPlaces.clear();
    for (const std::string& l_strName : f_Names(l_strA + "story/floors", "", ".json")) {
        if (f_EndsWith(l_strName, ".stage.json")) continue;
        Json l_jSpec, l_jGrid;
        if (!f_ReadJson(l_strA + "story/floors/" + l_strName, l_jSpec)) continue;
        const std::string l_strHand = f_Str(l_jSpec, "handAuthoredStage");
        const std::string l_strGp = l_strHand.empty() ? l_strA + "story/floors/" + l_strName.substr(0, l_strName.size() - 5) + ".stage.json" : l_strA + "stages/" + l_strHand;
        if (!f_ReadJson(l_strGp, l_jGrid)) continue;
        std::string l_strCells;
        if (l_jGrid.contains("tiles")) for (const Json& l_jR : l_jGrid["tiles"]) if (l_jR.is_string()) l_strCells += l_jR.get<std::string>();
        if (l_jGrid.contains("legend") && l_jGrid["legend"].is_object())
            for (auto& [l_strCh, l_jSem] : l_jGrid["legend"].items())
                if (l_jSem.is_string() && f_StartsWith(l_jSem.get<std::string>(), "npc:") && l_strCh.size() == 1 && l_strCells.find(l_strCh[0]) != std::string::npos)
                    m_mapNpcPlaces[l_jSem.get<std::string>().substr(4)].push_back(f_Str(l_jSpec, "id"));
    }

    m_jS = Json::object();
    m_jS["pools"] = l_jPools;
    m_jS["events"] = l_jEvents;
    m_jS["vars"] = l_fnOpt("story/vars.json");
    m_jS["cats"] = l_jCats;
    m_jS["counters"] = l_fnOpt("story/counters.json");
    m_jS["flags"] = l_fnOpt("story/flags.json");
    m_jS["kinds"] = l_jKinds.empty() ? f_DefaultKinds() : l_jKinds;
    m_jS["floors"] = l_jFloors;
    m_jS["texts"] = l_jTexts;
    m_jS["npcs"] = l_jNpcFiles;
    m_jS["dialogues"] = l_jDialogues;
    m_jBase = m_jS;
    m_vUndoStack.clear();
    m_strSelected.clear();
    return true;
}

// ---- edits ---------------------------------------------------------------------------------------------------------

void EventProject::f_Edit(const std::string& l_strLabel, const std::function<void()>& l_fnFn) {
    m_vUndoStack.push_back({m_jS, m_strSelected, l_strLabel});
    if (m_vUndoStack.size() > 300) m_vUndoStack.erase(m_vUndoStack.begin());
    l_fnFn();
}

bool EventProject::f_Undo(std::string* l_pLabel) {
    if (m_vUndoStack.empty()) return false;
    Step l_oU = std::move(m_vUndoStack.back());
    m_vUndoStack.pop_back();
    m_jS = std::move(l_oU.m_jS);
    m_strSelected = l_oU.m_strSel;
    if (l_pLabel) *l_pLabel = l_oU.m_strLabel;
    return true;
}

// ---- lookups -------------------------------------------------------------------------------------------------------

Json* EventProject::f_Ev(const std::string& l_strId) {
    for (Json& l_jE : m_jS["events"]) if (l_jE["id"] == l_strId) return &l_jE;
    return nullptr;
}

Json* EventProject::f_Flo(const std::string& l_strId) {
    for (Json& l_jF : m_jS["floors"]) if (l_jF["id"] == l_strId) return &l_jF;
    return nullptr;
}

std::vector<const Json*> EventProject::f_FloorsOf(const std::string& l_strId) const {
    std::vector<const Json*> l_vOut;
    for (const Json& l_jF : m_jS["floors"]) if (f_Contains(l_jF["events"], l_strId)) l_vOut.push_back(&l_jF);
    return l_vOut;
}

bool EventProject::f_IsMissing(const std::string& l_strId) const { return !f_Ev(l_strId) && !f_FloorsOf(l_strId).empty(); }

bool EventProject::f_IsNewEvent(const std::string& l_strId) const {
    for (const Json& l_jE : m_jBase["events"]) if (l_jE["id"] == l_strId) return false;
    return true;
}
bool EventProject::f_IsNewPool(const std::string& l_strPool) const { return !f_Contains(m_jBase["pools"], l_strPool); }
bool EventProject::f_IsNewFloor(const std::string& l_strId) const {
    for (const Json& l_jF : m_jBase["floors"]) if (l_jF["id"] == l_strId) return false;
    return true;
}

// which floors the player can reach from F01: open stairs always, locked ones once their event can be done on a reached floor
std::set<std::string> EventProject::f_Reachable() const {
    std::set<std::string> l_setSeen, l_setDone;
    const Json* l_pStart = f_Flo("F01");
    if (!l_pStart) { if (m_jS["floors"].empty()) return l_setSeen; l_pStart = &m_jS["floors"][0]; }
    l_setSeen.insert((*l_pStart)["id"].get<std::string>());
    for (bool l_bChanged = true; l_bChanged;) {
        l_bChanged = false;
        for (const Json& l_jF : m_jS["floors"]) {
            if (!l_setSeen.count(l_jF["id"].get<std::string>())) continue;
            for (const Json& l_jE : l_jF["events"]) if (l_jE.is_string()) l_setDone.insert(l_jE.get<std::string>());
            for (const Json& l_jSt : l_jF["stairs"]) {
                const std::string l_strTo = f_Str(l_jSt, "to");
                bool l_bOpen = true;   // every event of its lock can be done (W38: all of them)
                for (const std::string& l_strLock : f_StairUnlockEvents(l_jSt)) if (!l_setDone.count(l_strLock)) l_bOpen = false;
                if (!l_strTo.empty() && f_Flo(l_strTo) && !l_setSeen.count(l_strTo) && l_bOpen) { l_setSeen.insert(l_strTo); l_bChanged = true; }
            }
        }
    }
    return l_setSeen;
}

Json EventProject::f_Text(const std::string& l_strKey) const {
    const Json* l_pT = f_Member(m_jS["texts"], l_strKey.c_str());
    return l_pT && l_pT->is_object() ? *l_pT : Json::object();
}

bool EventProject::f_IsPlaceholder(const std::string& l_strKey) const {
    const Json* l_pT = f_Member(m_jS["texts"], l_strKey.c_str());
    if (!l_pT || !l_pT->is_object()) return true;
    if (l_pT->contains("_todo") && f_Truthy((*l_pT)["_todo"])) return true;
    const std::string l_strZh = f_Str(*l_pT, "zh_TW");
    std::string l_strBare = l_strKey;
    if (f_EndsWith(l_strBare, ".text")) l_strBare.resize(l_strBare.size() - 5);
    return l_strZh.empty() || l_strZh == l_strKey || l_strZh == l_strBare;
}

Json EventProject::f_ReqJson(const Json& l_jE) const {
    if (!l_jE["reqRaw"].is_null()) return l_jE["reqRaw"];
    const Json& l_jR = l_jE["req"];
    if (l_jR.size() == 1) return l_jR[0];
    if (!l_jR.empty()) return Json{{"all", l_jR}};
    return Json();
}

bool EventProject::f_HasLogic(const Json& l_jE) const {
    return l_jE["trigger"].value("on", std::string("step")) != "step" || !l_jE["req"].empty() || !l_jE["reqRaw"].is_null() || !l_jE["actions"].empty() || !l_jE["next"].empty();
}

std::vector<std::string> EventProject::f_Pools() const {
    std::vector<std::string> l_vOut;
    for (const Json& l_jP : m_jS["pools"]) l_vOut.push_back(l_jP.get<std::string>());
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

std::vector<std::string> EventProject::f_KindsList() const {
    std::vector<std::string> l_vOut;
    for (auto& [l_strK, l_jV] : m_jS["kinds"].items()) { (void)l_jV; l_vOut.push_back(l_strK); }
    return l_vOut;
}

std::string EventProject::f_KindName(const std::string& l_strK, const std::string& l_strLang) const {
    const Json* l_pR = f_Member(m_jS["kinds"], l_strK.c_str());
    if (!l_pR || !l_pR->contains("name")) return std::string();
    const Json& l_jN = (*l_pR)["name"];
    for (const std::string& l_strL : {l_strLang, std::string("en"), std::string("zh_TW")}) if (!f_Str(l_jN, l_strL.c_str()).empty()) return f_Str(l_jN, l_strL.c_str());
    return std::string();
}

std::string EventProject::f_KindColor(const std::string& l_strK) const {
    const Json* l_pR = f_Member(m_jS["kinds"], l_strK.c_str());
    if (l_pR && !f_Str(*l_pR, "color").empty()) return f_Str(*l_pR, "color");
    const Json* l_pD = f_Member(f_DefaultKinds(), l_strK.c_str());
    return l_pD ? f_Str(*l_pD, "color") : std::string("#777777");
}

std::vector<std::string> EventProject::f_DialogueList() const {
    std::vector<std::string> l_vOut;
    for (auto& [l_strK, l_jV] : m_jS["dialogues"].items()) { (void)l_jV; l_vOut.push_back(l_strK); }
    std::sort(l_vOut.begin(), l_vOut.end());
    return l_vOut;
}

std::vector<Json> EventProject::f_DialogueActs(const std::string& l_strId) const {
    std::vector<Json> l_vOut;
    const Json* l_pD = f_Member(m_jS["dialogues"], l_strId.c_str());
    if (!l_pD || !l_pD->contains("nodes")) return l_vOut;
    for (auto& [l_strK, l_jN] : (*l_pD)["nodes"].items()) {
        (void)l_strK;
        if (!l_jN.is_object() || !l_jN.contains("choices")) continue;
        for (const Json& l_jC : l_jN["choices"]) {
            if (!l_jC.contains("actions")) continue;
            if (l_jC["actions"].is_array()) for (const Json& l_jA : l_jC["actions"]) l_vOut.push_back(l_jA);
            else if (l_jC["actions"].is_object()) l_vOut.push_back(l_jC["actions"]);
        }
    }
    return l_vOut;
}

std::vector<const Json*> EventProject::f_Changers(const std::string& l_strRef) const {
    std::vector<const Json*> l_vOut;
    for (const Json& l_jE : m_jS["events"]) {
        bool l_bHit = std::any_of(l_jE["actions"].begin(), l_jE["actions"].end(), [&](const Json& l_jA) { return f_ActRef(l_jA) == l_strRef; });
        if (!l_bHit && f_StartsWith(l_strRef, "flag:")) { const Json* l_pF = f_Member(m_jS["flags"], l_strRef.substr(5).c_str()); l_bHit = l_pF && f_Str(*l_pF, "setBy") == l_jE["id"].get<std::string>(); }
        if (!l_bHit && l_jE["trigger"].value("on", std::string()) == "talk" && !l_jE["dialogue"].get<std::string>().empty())
            for (const Json& l_jA : f_DialogueActs(l_jE["dialogue"].get<std::string>())) if (f_ActRef(l_jA) == l_strRef) { l_bHit = true; break; }
        if (l_bHit) l_vOut.push_back(&l_jE);
    }
    return l_vOut;
}

std::vector<const Json*> EventProject::f_ReadersOnly(const std::string& l_strRef) const {
    std::vector<const Json*> l_vOut;
    for (const Json& l_jE : m_jS["events"])
        if (std::any_of(l_jE["req"].begin(), l_jE["req"].end(), [&](const Json& l_jL) { return f_LeafRef(l_jL) == l_strRef; })) l_vOut.push_back(&l_jE);
    return l_vOut;
}

std::vector<const Json*> EventProject::f_Watchers(const std::string& l_strRef) const {
    std::vector<const Json*> l_vOut;
    if (!f_StartsWith(l_strRef, "var:")) return l_vOut;
    for (const Json& l_jE : m_jS["events"])
        if (l_jE["trigger"].value("on", std::string()) == "varChanged" && "var:" + l_jE["trigger"].value("var", std::string()) == l_strRef) l_vOut.push_back(&l_jE);
    return l_vOut;
}

std::vector<const Json*> EventProject::f_Readers(const std::string& l_strRef) const {
    std::vector<const Json*> l_vOut;
    for (const Json& l_jE : m_jS["events"]) {
        const bool l_bR = std::any_of(l_jE["req"].begin(), l_jE["req"].end(), [&](const Json& l_jL) { return f_LeafRef(l_jL) == l_strRef; });
        const bool l_bW = l_jE["trigger"].value("on", std::string()) == "varChanged" && "var:" + l_jE["trigger"].value("var", std::string()) == l_strRef;
        if (l_bR || l_bW) l_vOut.push_back(&l_jE);
    }
    return l_vOut;
}

std::vector<std::string> EventProject::f_LinkedValues(const std::string& l_strRef) const {
    std::vector<std::string> l_vOut;
    std::vector<const Json*> l_vEvs = f_Changers(l_strRef);
    for (const Json* l_pE : f_Readers(l_strRef)) l_vEvs.push_back(l_pE);
    for (const Json* l_pE : l_vEvs) {
        std::vector<std::string> l_vRefs;
        for (const Json& l_jA : (*l_pE)["actions"]) l_vRefs.push_back(f_ActRef(l_jA));
        for (const Json& l_jL : (*l_pE)["req"]) l_vRefs.push_back(f_LeafRef(l_jL));
        if ((*l_pE)["trigger"].value("on", std::string()) == "varChanged") l_vRefs.push_back("var:" + (*l_pE)["trigger"].value("var", std::string()));
        for (const std::string& l_strR : l_vRefs)
            if (!l_strR.empty() && l_strR != l_strRef && (f_StartsWith(l_strR, "var:") || f_StartsWith(l_strR, "counter:") || f_StartsWith(l_strR, "flag:")) && !f_Contains(l_vOut, l_strR)) l_vOut.push_back(l_strR);
    }
    return l_vOut;
}

bool EventProject::f_RefExists(const std::string& l_strR) const {
    const size_t l_nC = l_strR.find(':');
    const std::string l_strK = l_strR.substr(0, l_nC), l_strN = l_strR.substr(l_nC + 1);
    if (l_strK == "var") return m_jS["vars"].contains(l_strN);
    if (l_strK == "counter") return m_jS["counters"].contains(l_strN);
    if (l_strK == "flag") return m_jS["flags"].contains(l_strN);
    if (l_strK == "event") return f_Ev(l_strN) != nullptr;
    if (l_strK == "item") return f_Contains(m_vItems, l_strN);
    return true;
}

std::vector<std::string> EventProject::f_ValueRefs() const {
    std::vector<std::string> l_vOut;
    for (auto& [l_strK, l_jV] : m_jS["vars"].items()) { (void)l_jV; l_vOut.push_back("var:" + l_strK); }
    for (auto& [l_strK, l_jV] : m_jS["counters"].items()) { (void)l_jV; l_vOut.push_back("counter:" + l_strK); }
    for (auto& [l_strK, l_jV] : m_jS["flags"].items()) { (void)l_jV; l_vOut.push_back("flag:" + l_strK); }
    return l_vOut;
}

Json* EventProject::f_RecOf(const std::string& l_strRef) {
    const size_t l_nC = l_strRef.find(':');
    const std::string l_strK = l_strRef.substr(0, l_nC), l_strN = l_strRef.substr(l_nC + 1);
    Json& l_jO = m_jS[l_strK == "var" ? "vars" : l_strK == "counter" ? "counters" : "flags"];
    auto l_oIt = l_jO.find(l_strN);
    return l_oIt == l_jO.end() ? nullptr : &*l_oIt;
}

std::vector<std::string> EventProject::f_NpcIds() const {
    std::set<std::string> l_setIds;
    for (auto& [l_strK, l_jV] : m_jS["npcs"].items()) { (void)l_jV; l_setIds.insert(l_strK); }
    for (const auto& [l_strK, l_vV] : m_mapNpcPlaces) { (void)l_vV; l_setIds.insert(l_strK); }
    for (const Json& l_jE : m_jS["events"])
        if (l_jE["trigger"].value("on", std::string()) == "talk" && !l_jE["trigger"].value("npc", std::string()).empty()) l_setIds.insert(l_jE["trigger"]["npc"].get<std::string>());
    return std::vector<std::string>(l_setIds.begin(), l_setIds.end());
}

// the NPC's talk events in menu order: topicOrder first (lower first), then pool order
std::vector<Json*> EventProject::f_TopicsOf(const std::string& l_strNpc) {
    std::vector<std::pair<size_t, Json*>> l_vList;
    Json& l_jEvs = m_jS["events"];
    for (size_t l_nI = 0; l_nI < l_jEvs.size(); l_nI++)
        if (l_jEvs[l_nI]["trigger"].value("on", std::string()) == "talk" && l_jEvs[l_nI]["trigger"].value("npc", std::string()) == l_strNpc) l_vList.push_back({l_nI, &l_jEvs[l_nI]});
    const std::vector<std::string> l_vPs = f_Pools();
    auto l_fnOrder = [](const Json* l_pE) { return (*l_pE)["topicOrder"].is_number() ? (*l_pE)["topicOrder"].get<double>() : 1e9; };
    auto l_fnPi = [&](const Json* l_pE) { return std::find(l_vPs.begin(), l_vPs.end(), (*l_pE)["pool"].get<std::string>()) - l_vPs.begin(); };
    std::stable_sort(l_vList.begin(), l_vList.end(), [&](const auto& l_oA, const auto& l_oB) {
        if (l_fnOrder(l_oA.second) != l_fnOrder(l_oB.second)) return l_fnOrder(l_oA.second) < l_fnOrder(l_oB.second);
        if (l_fnPi(l_oA.second) != l_fnPi(l_oB.second)) return l_fnPi(l_oA.second) < l_fnPi(l_oB.second);
        return l_oA.first < l_oB.first;
    });
    std::vector<Json*> l_vOut;
    for (auto& l_pairX : l_vList) l_vOut.push_back(l_pairX.second);
    return l_vOut;
}

bool EventProject::f_Woken(const std::string& l_strId) const {
    for (const Json& l_jO : m_jS["events"]) if (f_Contains(l_jO["next"], l_strId)) return true;
    return false;
}

std::string EventProject::f_TalkFloorOf(const Json& l_jE, bool* l_pConditional) const {
    const CondRows l_oR = f_CondToRows(f_ReqJson(l_jE));
    if (l_pConditional) *l_pConditional = l_oR.m_bRaw || (l_oR.m_strMode == "any" && l_oR.m_vRows.size() > 1);
    if (l_oR.m_bRaw || (l_oR.m_strMode == "any" && l_oR.m_vRows.size() > 1)) return std::string();
    for (const Json& l_jL : l_oR.m_vRows) if (f_Str(l_jL, "type") == "onStage") return f_Str(l_jL, "stageId");
    return std::string();
}

void EventProject::f_SetEventReq(Json& l_jE, const Json& l_jC) {
    Json l_jTmp = Json::object();
    if (!l_jC.is_null()) l_jTmp["requires"] = l_jC;
    Json l_jParsed = Json::object();
    f_LogicInto(l_jParsed, l_jTmp);
    l_jE["req"] = l_jParsed["req"];
    l_jE["reqRaw"] = l_jParsed["reqRaw"];
}

void EventProject::f_SetTalkFloor(Json& l_jE, const std::string& l_strFloor) {
    const CondRows l_oR = f_CondToRows(f_ReqJson(l_jE));
    std::vector<Json> l_vKeep;
    for (const Json& l_jL : l_oR.m_vRows) if (f_Str(l_jL, "type") != "onStage") l_vKeep.push_back(l_jL);
    if (!l_strFloor.empty()) l_vKeep.push_back(Json{{"type", "onStage"}, {"stageId", l_strFloor}});
    f_SetEventReq(l_jE, f_RowsToCond(l_oR.m_strMode, l_vKeep));
}

std::string EventProject::f_GuessKind(const std::string& l_strId) const {
    static const std::regex l_oVariant("_v\\d+$");
    const std::string l_strBaseId = std::regex_replace(l_strId, l_oVariant, "");
    if (l_strBaseId != l_strId) if (const Json* l_pB = f_Ev(l_strBaseId)) return (*l_pB)["kind"].get<std::string>();
    for (const std::string& l_strK : f_KindsList()) {
        std::string l_strW = l_strK;
        if (f_StartsWith(l_strW, "memory_")) l_strW = l_strW.substr(7);
        if (l_strId.find(l_strW) != std::string::npos) return l_strK;
    }
    return std::string();
}

std::vector<std::string> EventProject::f_ChoiceIdsOf(const std::string& l_strEvId) const {
    std::vector<std::string> l_vOut;
    const Json* l_pE = f_Ev(l_strEvId);
    const Json* l_pD = l_pE ? f_Member(m_jS["dialogues"], (*l_pE)["dialogue"].get<std::string>().c_str()) : nullptr;
    if (!l_pD || !l_pD->contains("nodes")) return l_vOut;
    for (auto& [l_strK, l_jN] : (*l_pD)["nodes"].items()) {
        (void)l_strK;
        if (!l_jN.contains("choices")) continue;
        for (const Json& l_jC : l_jN["choices"]) { const std::string l_strId = f_Str(l_jC, "id"); if (!l_strId.empty() && !f_Contains(l_vOut, l_strId)) l_vOut.push_back(l_strId); }
    }
    return l_vOut;
}

// ---- problems ------------------------------------------------------------------------------------------------------

std::vector<EvProblem> EventProject::f_Problems() const {
    std::vector<EvProblem> l_vOut;
    auto l_fnAdd = [&](const std::string& l_strRule, const std::string& l_strId, const std::string& l_strCode, std::vector<std::string> l_vArgs) {
        EvProblem l_oP;
        l_oP.m_strRule = l_strRule;
        l_oP.m_strId = l_strId;
        l_oP.m_strWhereCode = l_strCode;
        l_oP.m_vWhereArgs = std::move(l_vArgs);
        l_vOut.push_back(l_oP);
        return &l_vOut.back();
    };
    std::set<std::string> l_setSeen;
    for (const Json& l_jF : m_jS["floors"])
        for (const Json& l_jX : l_jF["events"]) {
            const std::string l_strId = l_jX.get<std::string>();
            if (f_Ev(l_strId) || l_setSeen.count(l_strId)) continue;
            l_setSeen.insert(l_strId);
            std::vector<std::string> l_vFl;
            const auto l_vFs2 = f_FloorsOf(l_strId);
            for (const Json* l_pG : l_vFs2) l_vFl.push_back((*l_pG)["id"].get<std::string>());
            l_fnAdd("missing_event", l_strId, "", {f_Join(l_vFl, ", ")})->m_strAct = (*l_vFs2[0])["act"].get<std::string>();
        }
    const std::vector<std::string> l_vDlgs = f_DialogueList();
    for (const Json& l_jE : m_jS["events"]) {
        const std::string l_strId = l_jE["id"].get<std::string>(), l_strOn = l_jE["trigger"].value("on", std::string("step"));
        const Json* l_pTx = f_Member(m_jS["texts"], l_jE["text"].get<std::string>().c_str());
        if (f_FloorsOf(l_strId).empty() && l_strOn == "step") l_fnAdd("orphan", l_strId, "", {l_jE["pool"].get<std::string>()});
        if (l_strOn == "talk" && !l_vDlgs.empty())
            for (const char* l_pszK : {"dialogue", "afterDialogue"}) {
                const std::string l_strD = l_jE[l_pszK].get<std::string>();
                if (!l_strD.empty() && !f_Contains(l_vDlgs, l_strD)) l_fnAdd("missing_dialogue", l_strId, "", {"dialogue/" + l_strD + ".json"});
            }
        if (!l_pTx) l_fnAdd("missing_text", l_strId, "", {l_jE["text"].get<std::string>()});
        else if (f_IsPlaceholder(l_jE["text"].get<std::string>())) l_fnAdd("placeholder", l_strId, "", {l_jE["text"].get<std::string>()});
        else if (f_Str(*l_pTx, "en").empty() || f_Str(*l_pTx, "en") == f_Str(*l_pTx, "zh_TW")) l_fnAdd("untranslated", l_strId, "", {l_jE["text"].get<std::string>()});
        const bool l_bNoTi = f_Str(f_Text(l_jE["title"].get<std::string>()), "zh_TW").empty(), l_bNoDe = f_Str(f_Text(l_jE["desc"].get<std::string>()), "zh_TW").empty();
        if (l_bNoTi || l_bNoDe) l_fnAdd("missing_title", l_strId, "wNoTitle", {l_bNoTi && l_bNoDe ? "title / desc" : l_bNoTi ? "title" : "desc"});
        const std::string l_strKind = l_jE["kind"].get<std::string>();
        if (l_strKind.empty()) l_fnAdd("no_kind", l_strId, "", {l_jE["pool"].get<std::string>()});
        else if (!m_jS["kinds"].contains(l_strKind)) l_fnAdd("unknown_kind", l_strId, "wUnknownKind", {l_strKind});
    }
    // event logic: every reference must exist, every event must be able to fire, and links must not loop
    std::set<std::string> l_setLinked;
    for (const Json& l_jE : m_jS["events"]) for (const Json& l_jN : l_jE["next"]) l_setLinked.insert(l_jN.get<std::string>());
    for (const Json& l_jE : m_jS["events"]) {
        const std::string l_strId = l_jE["id"].get<std::string>(), l_strOn = l_jE["trigger"].value("on", std::string("step"));
        std::vector<std::string> l_vRefs;
        for (const Json& l_jL : l_jE["req"]) if (const std::string l_strR = f_LeafRef(l_jL); !l_strR.empty()) l_vRefs.push_back(l_strR);
        for (const Json& l_jA : l_jE["actions"]) if (const std::string l_strR = f_ActRef(l_jA); !l_strR.empty()) l_vRefs.push_back(l_strR);
        if (l_strOn == "varChanged") l_vRefs.push_back("var:" + l_jE["trigger"].value("var", std::string()));
        std::vector<std::string> l_vUniq;
        for (const std::string& l_strR : l_vRefs) if (!f_Contains(l_vUniq, l_strR)) l_vUniq.push_back(l_strR);
        for (const std::string& l_strR : l_vUniq)
            if (!f_RefExists(l_strR)) l_fnAdd("unknown_ref", l_strId, "wUnknown", {l_strR.substr(0, l_strR.find(':')), l_strR.substr(l_strR.find(':') + 1)});
        for (const Json& l_jN : l_jE["next"]) if (!f_Ev(l_jN.get<std::string>())) l_fnAdd("bad_next", l_strId, "wBadNext", {l_jN.get<std::string>()});
        if (l_strOn == "chain" && !l_setLinked.count(l_strId)) l_fnAdd("never_fires", l_strId, "wNoIncoming", {});
        if (l_strOn == "talk" && !m_mapNpcPlaces.count(l_jE["trigger"].value("npc", std::string()))) l_fnAdd("never_fires", l_strId, "wNpcNowhere", {l_jE["trigger"].value("npc", std::string())});
        for (const Json& l_jL : l_jE["req"]) {
            if (f_Str(l_jL, "type") != "varAtLeast") continue;
            const std::string l_strV = f_Str(l_jL, "var");
            const Json* l_pRec = f_Member(m_jS["vars"], l_strV.c_str());
            if (l_pRec && f_Changers("var:" + l_strV).empty() && f_Num(l_pRec->value("default", Json(0))) < f_Num(l_jL.value("value", Json(0))))
                l_fnAdd("never_active", l_strId, "wNeverActive", {l_strV, l_jL.value("value", Json(0)).dump()});
        }
    }
    std::set<std::string> l_setOnLoop;
    for (const Json& l_jE0 : m_jS["events"]) {
        const std::string l_strId0 = l_jE0["id"].get<std::string>();
        std::vector<std::pair<std::string, std::vector<std::string>>> l_vStack{{l_strId0, {l_strId0}}};
        std::set<std::string> l_setVis;
        while (!l_vStack.empty()) {
            auto [l_strId, l_vPath] = l_vStack.back();
            l_vStack.pop_back();
            const Json* l_pE = f_Ev(l_strId);
            if (!l_pE) continue;
            for (const Json& l_jNj : (*l_pE)["next"]) {
                const std::string l_strN = l_jNj.get<std::string>();
                if (l_strN == l_strId0 && !l_setOnLoop.count(l_strId0)) {
                    l_setOnLoop.insert(l_strId0);
                    std::vector<std::string> l_vP2 = l_vPath;
                    l_vP2.push_back(l_strN);
                    l_fnAdd("logic_cycle", l_strId0, "", {f_Join(l_vP2, " → ")});
                } else if (!l_setVis.count(l_strN)) {
                    l_setVis.insert(l_strN);
                    std::vector<std::string> l_vP2 = l_vPath;
                    l_vP2.push_back(l_strN);
                    l_vStack.push_back({l_strN, l_vP2});
                }
            }
        }
    }
    for (auto& [l_strV, l_jRec] : m_jS["vars"].items()) {
        (void)l_jRec;
        if (f_Changers("var:" + l_strV).empty() && f_Readers("var:" + l_strV).empty()) l_fnAdd("unused_var", l_strV, "wUnusedVar", {});
    }
    // floors: every stair must lead to a floor that exists, a lock must name a real event, every floor must be reachable
    for (const Json& l_jF : m_jS["floors"])
        for (const Json& l_jSt : l_jF["stairs"]) {
            const std::string l_strFid = l_jF["id"].get<std::string>(), l_strTo = f_Str(l_jSt, "to");
            if (!l_strTo.empty() && !f_Flo(l_strTo)) l_fnAdd("missing_floor", l_strFid, "stairWhere", {l_strFid, f_Str(l_jSt, "dir", "up"), l_strTo})->m_strTo = l_strTo;
            for (const std::string& l_strLock : f_StairUnlockEvents(l_jSt))
                if (!f_Ev(l_strLock)) l_fnAdd("bad_unlock", l_strFid, "badUnlockWhere", {l_strFid, l_strTo, l_strLock})->m_strTo = l_strTo;
        }
    const std::set<std::string> l_setReach = f_Reachable();
    for (const Json& l_jF : m_jS["floors"]) if (!l_setReach.count(l_jF["id"].get<std::string>())) l_fnAdd("unreachable", l_jF["id"].get<std::string>(), "unreachWhere", {});
    // dialogues: a start / next naming a node the file lacks
    for (auto& [l_strId, l_jD] : m_jS["dialogues"].items()) {
        const Json l_jNodes = l_jD.value("nodes", Json::object());
        std::vector<std::string> l_vStarts;
        if (l_jD.contains("start") && l_jD["start"].is_array()) { for (const Json& l_jS : l_jD["start"]) l_vStarts.push_back(f_Str(l_jS, "node")); }
        else l_vStarts.push_back(f_Str(l_jD, "start"));
        for (const std::string& l_strS : l_vStarts) if (!l_jNodes.contains(l_strS)) l_fnAdd("bad_dialogue", l_strId, "wDlgStart", {l_strS});
        for (auto& [l_strNid, l_jN] : l_jNodes.items())
            if (l_jN.is_object() && l_jN.contains("choices"))
                for (const Json& l_jC : l_jN["choices"]) {
                    const std::string l_strNx = f_Str(l_jC, "next");
                    if (!l_strNx.empty() && !l_jNodes.contains(l_strNx)) l_fnAdd("bad_dialogue", l_strId, "wDlgNext", {l_strNid, l_strNx});
                }
    }
    return l_vOut;
}

// ---- event actions -------------------------------------------------------------------------------------------------

std::string EventProject::f_NewEventId() {
    for (int l_iN = 1;; l_iN++) {
        const std::string l_strId = "ev_new_event_" + std::to_string(l_iN);
        if (!f_Ev(l_strId) && !m_jS["texts"].contains(l_strId + ".text")) return l_strId;
    }
}

static Json f_EmptyText() {
    Json l_jT = Json::object();
    for (const char* l_pszL : g_arrLangs) l_jT[l_pszL] = "";
    return l_jT;
}

Json EventProject::f_BlankEvent(const std::string& l_strId, const std::string& l_strPool) const {
    Json l_jE = Json::object();
    l_jE["id"] = l_strId;
    l_jE["kind"] = "";
    l_jE["pool"] = l_strPool;
    l_jE["text"] = l_strId + ".text";
    l_jE["title"] = l_strId + ".title";
    l_jE["desc"] = l_strId + ".desc";
    f_LogicInto(l_jE, Json::object());
    return l_jE;
}

// a missing id becomes a real event: ev_common_* variants go to pool_common, the rest to their chapter's pool. The text
// key is the placeholder string that already exists under the bare id, so text.json needs no new key.
void EventProject::f_MakeRecord(const std::string& l_strId, const std::string& l_strAct) {
    const std::string l_strPool = f_StartsWith(l_strId, "ev_common_") ? "pool_common" : "pool_act" + l_strAct.substr(std::min<size_t>(3, l_strAct.size()));
    if (!f_Contains(m_jS["pools"], l_strPool)) m_jS["pools"].push_back(l_strPool);
    const std::string l_strTextKey = m_jS["texts"].contains(l_strId) ? l_strId : l_strId + ".text";
    if (!m_jS["texts"].contains(l_strTextKey)) m_jS["texts"][l_strTextKey] = f_EmptyText();
    for (const char* l_pszF : {".title", ".desc"}) if (!m_jS["texts"].contains(l_strId + l_pszF)) m_jS["texts"][l_strId + l_pszF] = f_EmptyText();
    Json l_jE = f_BlankEvent(l_strId, l_strPool);
    l_jE["kind"] = f_GuessKind(l_strId);
    l_jE["text"] = l_strTextKey;
    m_jS["events"].push_back(l_jE);
}

void EventProject::f_RenameEvent(Json& l_jE, const std::string& l_strNu) {
    const std::string l_strOld = l_jE["id"].get<std::string>();
    for (Json& l_jF : m_jS["floors"]) for (Json& l_jX : l_jF["events"]) if (l_jX == l_strOld) l_jX = l_strNu;
    for (Json& l_jO : m_jS["events"]) {
        for (Json& l_jN : l_jO["next"]) if (l_jN == l_strOld) l_jN = l_strNu;
        for (Json& l_jL : l_jO["req"]) if (f_Str(l_jL, "type") == "runFlagSet" && f_Str(l_jL, "flag") == "event_" + l_strOld) l_jL["flag"] = "event_" + l_strNu;
    }
    Json l_jTx = m_jS["texts"].contains(l_jE["text"].get<std::string>()) ? m_jS["texts"][l_jE["text"].get<std::string>()] : Json::object();
    m_jS["texts"].erase(l_jE["text"].get<std::string>());
    l_jE["id"] = l_strNu;
    l_jE["text"] = l_strNu + ".text";
    m_jS["texts"][l_strNu + ".text"] = l_jTx;
    for (const char* l_pszF : {"title", "desc"}) {   // keys that follow the id move with it
        if (l_jE[l_pszF] != l_strOld + "." + l_pszF) continue;
        const std::string l_strOk = l_jE[l_pszF].get<std::string>();
        Json l_jV = m_jS["texts"].contains(l_strOk) ? m_jS["texts"][l_strOk] : Json();
        m_jS["texts"].erase(l_strOk);
        l_jE[l_pszF] = l_strNu + "." + l_pszF;
        if (!l_jV.is_null()) m_jS["texts"][l_strNu + "." + l_pszF] = l_jV;
    }
    m_strSelected = l_strNu;
}

std::string EventProject::f_IdError(const std::string& l_strId, const Json* l_pSelf) const {
    if (!f_NameOk(l_strId)) return "format";
    for (const Json& l_jE : m_jS["events"]) if (l_jE["id"] == l_strId && &l_jE != l_pSelf) return "dup:" + l_jE["pool"].get<std::string>();
    return std::string();
}

// ---- save ----------------------------------------------------------------------------------------------------------

static Json f_PoolRec(const EventProject& l_oP, const Json& l_jE) {
    Json l_jO = Json::object();
    l_jO["eventId"] = l_jE["id"];
    l_jO["kind"] = l_jE["kind"];
    l_jO["text"] = l_jE["text"];
    l_jO["title"] = l_jE["title"];
    l_jO["desc"] = l_jE["desc"];
    if (l_jE.contains("inLog")) l_jO["inLog"] = l_jE["inLog"];
    if (l_jE["trigger"].value("on", std::string("step")) != "step") l_jO["trigger"] = l_jE["trigger"];
    const Json l_jRq = l_oP.f_ReqJson(l_jE);
    if (!l_jRq.is_null()) l_jO["requires"] = l_jRq;
    if (!l_jE["actions"].empty()) l_jO["actions"] = l_jE["actions"];
    if (!l_jE["next"].empty()) l_jO["next"] = l_jE["next"];
    if (l_jE["once"] == false) { l_jO["once"] = false; if (l_jE["maxTimes"].get<int>() > 0) l_jO["maxTimes"] = l_jE["maxTimes"]; }
    if (l_jE["trigger"].value("on", std::string()) == "talk") {
        if (!l_jE["dialogue"].get<std::string>().empty()) l_jO["dialogue"] = l_jE["dialogue"];
        if (!l_jE["afterDialogue"].get<std::string>().empty()) l_jO["afterDialogue"] = l_jE["afterDialogue"];
        if (l_jE["topicOrder"].is_number_integer()) l_jO["topicOrder"] = l_jE["topicOrder"];
    }
    return l_jO;
}

std::vector<EventProject::FileWrite> EventProject::f_LiveWrites(const std::map<std::string, std::string>& l_mapComments) const {
    const std::string l_strA = "assets/data/";
    std::vector<FileWrite> l_vOut;
    auto l_fnPut = [&](const std::string& l_strPath, const std::string* l_pBefore, const std::string& l_strText) {
        if (l_pBefore && *l_pBefore == l_strText) return;
        l_vOut.push_back({l_strPath, l_pBefore ? *l_pBefore : std::string(), l_strText, l_pBefore != nullptr});
    };
    auto l_fnRead = [&](const std::string& l_strPath, std::string& l_strS) { return f_ReadText(m_strRoot + "/" + l_strPath, l_strS); };
    // pools
    std::set<std::string> l_setAllPools;
    for (const Json& l_jP : m_jBase["pools"]) l_setAllPools.insert(l_jP.get<std::string>());
    for (const Json& l_jP : m_jS["pools"]) l_setAllPools.insert(l_jP.get<std::string>());
    for (const std::string& l_strPid : l_setAllPools) {
        std::vector<const Json*> l_vBEv, l_vSEv;
        for (const Json& l_jE : m_jBase["events"]) if (l_jE["pool"] == l_strPid) l_vBEv.push_back(&l_jE);
        for (const Json& l_jE : m_jS["events"]) if (l_jE["pool"] == l_strPid) l_vSEv.push_back(&l_jE);
        Json l_jA = Json::array(), l_jB = Json::array();
        for (const Json* l_pE : l_vBEv) l_jA.push_back(f_PoolRec(*this, *l_pE));
        for (const Json* l_pE : l_vSEv) l_jB.push_back(f_PoolRec(*this, *l_pE));
        if (l_jA == l_jB && f_Contains(m_jBase["pools"], l_strPid)) continue;
        const std::string l_strPath = l_strA + "events/" + l_strPid + ".json";
        std::string l_strBefore;
        const bool l_bHad = l_fnRead(l_strPath, l_strBefore);
        Json l_jObj = l_bHad ? Json::parse(l_strBefore, nullptr, false) : Json{{"events", Json::array()}};
        if (l_jObj.is_discarded()) l_jObj = Json{{"events", Json::array()}};
        Json l_jDisk = l_jObj.value("events", Json::array());
        std::set<std::string> l_setMine;
        for (const Json* l_pE : l_vBEv) l_setMine.insert((*l_pE)["id"].get<std::string>());
        for (const Json* l_pE : l_vSEv) l_setMine.insert((*l_pE)["id"].get<std::string>());
        Json l_jEvs = Json::array();
        for (const Json* l_pE : l_vSEv) {
            Json l_jR = Json::object();
            for (const Json& l_jD : l_jDisk) if (l_jD.value("eventId", std::string()) == (*l_pE)["id"].get<std::string>()) { l_jR = l_jD; break; }
            for (const char* l_pszK : {"trigger", "requires", "actions", "next", "inLog", "dialogue", "afterDialogue", "topicOrder", "once", "maxTimes"}) l_jR.erase(l_pszK);
            const Json l_jRec = f_PoolRec(*this, *l_pE);
            for (auto& [l_strK, l_jV] : l_jRec.items()) l_jR[l_strK] = l_jV;   // the editor's fields win; a removed inLog goes
            l_jEvs.push_back(l_jR);
        }
        for (const Json& l_jD : l_jDisk) if (!l_setMine.count(l_jD.value("eventId", std::string()))) l_jEvs.push_back(l_jD);   // added by someone else since
        l_jObj["events"] = l_jEvs;
        l_fnPut(l_strPath, l_bHad ? &l_strBefore : nullptr, f_AsText(l_jObj, l_bHad ? &l_strBefore : nullptr));
    }
    // floors
    for (const Json& l_jF : m_jS["floors"]) {
        const std::string l_strId = l_jF["id"].get<std::string>();
        const Json* l_pBf = nullptr;
        for (const Json& l_jX : m_jBase["floors"]) if (l_jX["id"] == l_strId) l_pBf = &l_jX;
        const bool l_bStairsChanged = !l_jF["derived"].get<bool>() && (!l_pBf || (*l_pBf)["derived"].get<bool>() || (*l_pBf)["stairs"] != l_jF["stairs"]);
        if (l_pBf && (*l_pBf)["events"] == l_jF["events"] && (*l_pBf)["act"] == l_jF["act"] && !l_bStairsChanged) continue;
        const std::string l_strPath = l_strA + "story/floors/" + l_strId + ".json";
        std::string l_strBefore;
        const bool l_bHad = l_fnRead(l_strPath, l_strBefore);
        Json l_jObj = l_bHad ? Json::parse(l_strBefore, nullptr, false) : Json();
        if (!l_bHad || l_jObj.is_discarded()) l_jObj = Json{{"id", l_strId}, {"act", l_jF["act"]}, {"role", l_jF["role"]}, {"events", Json::array()}};
        if (!l_pBf || !l_bHad) l_jObj["events"] = l_jF["events"];
        else if ((*l_pBf)["events"] != l_jF["events"]) {
            const Json l_jDiskEv = l_jObj.value("events", Json::array());
            std::vector<std::string> l_vAdd, l_vDel;
            for (const Json& l_jX : l_jF["events"]) if (!f_Contains((*l_pBf)["events"], l_jX.get<std::string>())) l_vAdd.push_back(l_jX.get<std::string>());
            for (const Json& l_jX : (*l_pBf)["events"]) if (!f_Contains(l_jF["events"], l_jX.get<std::string>())) l_vDel.push_back(l_jX.get<std::string>());
            if (l_jDiskEv == (*l_pBf)["events"]) l_jObj["events"] = l_jF["events"];
            else {
                Json l_jM = Json::array();
                for (const Json& l_jX : l_jDiskEv) if (!l_jX.is_string() || !f_Contains(l_vDel, l_jX.get<std::string>())) l_jM.push_back(l_jX);
                for (const std::string& l_strX : l_vAdd) if (!f_Contains(l_jDiskEv, l_strX)) l_jM.push_back(l_strX);
                l_jObj["events"] = l_jM;
            }
        }
        if (!l_pBf || (*l_pBf)["act"] != l_jF["act"]) l_jObj["act"] = l_jF["act"];
        if (l_bStairsChanged) l_jObj["stairs"] = l_jF["stairs"];
        l_fnPut(l_strPath, l_bHad ? &l_strBefore : nullptr, f_AsText(l_jObj, l_bHad ? &l_strBefore : nullptr));
    }
    // text.json
    {
        std::set<std::string> l_setKeys;
        for (auto& [l_strK, l_jV] : m_jBase["texts"].items()) { (void)l_jV; l_setKeys.insert(l_strK); }
        for (auto& [l_strK, l_jV] : m_jS["texts"].items()) { (void)l_jV; l_setKeys.insert(l_strK); }
        std::vector<std::string> l_vChanged;
        for (const std::string& l_strK : l_setKeys) {
            const Json l_jA = m_jBase["texts"].contains(l_strK) ? m_jBase["texts"][l_strK] : Json(), l_jB = m_jS["texts"].contains(l_strK) ? m_jS["texts"][l_strK] : Json();
            if (l_jA != l_jB) l_vChanged.push_back(l_strK);
        }
        if (!l_vChanged.empty()) {
            const std::string l_strPath = l_strA + "text.json";
            std::string l_strBefore;
            if (l_fnRead(l_strPath, l_strBefore)) {
                Json l_jObj = Json::parse(l_strBefore, nullptr, false);
                if (!l_jObj.is_discarded()) {
                    for (const std::string& l_strK : l_vChanged) {
                        if (m_jS["texts"].contains(l_strK) && !m_jS["texts"][l_strK].is_null()) l_jObj["strings"][l_strK] = m_jS["texts"][l_strK];
                        else l_jObj["strings"].erase(l_strK);
                    }
                    l_fnPut(l_strPath, &l_strBefore, f_AsText(l_jObj, &l_strBefore));
                }
            }
        }
    }
    // vars.json, kinds.json: a head of "_" keys, then the records
    for (const auto& [l_strKey, l_strFile] : std::vector<std::pair<std::string, std::string>>{{"vars", "story/vars.json"}, {"kinds", "events/kinds.json"}}) {
        const bool l_bCatsChanged = l_strKey == "vars" && m_jBase["cats"] != m_jS["cats"];
        if (m_jBase[l_strKey] == m_jS[l_strKey] && !l_bCatsChanged) continue;
        const std::string l_strPath = l_strA + l_strFile;
        std::string l_strBefore;
        const bool l_bHad = l_fnRead(l_strPath, l_strBefore);
        Json l_jOld = l_bHad ? Json::parse(l_strBefore, nullptr, false) : Json::object();
        if (l_jOld.is_discarded()) l_jOld = Json::object();
        Json l_jO = Json::object();
        if (l_bHad) { for (auto& [l_strK, l_jV] : l_jOld.items()) if (f_StartsWith(l_strK, "_")) l_jO[l_strK] = l_jV; }
        else { auto l_itC = l_mapComments.find(l_strKey); l_jO["_comment"] = Json::array({l_itC != l_mapComments.end() ? l_itC->second : std::string()}); }
        if (l_strKey == "vars") { if (!m_jS["cats"].empty()) l_jO["_categories"] = m_jS["cats"]; else l_jO.erase("_categories"); }
        for (auto& [l_strK, l_jV] : m_jS[l_strKey].items()) l_jO[l_strK] = l_jV;
        l_fnPut(l_strPath, l_bHad ? &l_strBefore : nullptr, f_AsText(l_jO, l_bHad ? &l_strBefore : nullptr));
    }
    // NPC and dialogue files: whole files
    for (const auto& [l_strKey, l_strFolder] : std::vector<std::pair<std::string, std::string>>{{"npcs", "npcs"}, {"dialogues", "dialogue"}})
        for (auto& [l_strId, l_jV] : m_jS[l_strKey].items()) {
            if (m_jBase[l_strKey].contains(l_strId) && m_jBase[l_strKey][l_strId] == l_jV) continue;
            const std::string l_strPath = l_strA + l_strFolder + "/" + l_strId + ".json";
            std::string l_strBefore;
            const bool l_bHad = l_fnRead(l_strPath, l_strBefore);
            l_fnPut(l_strPath, l_bHad ? &l_strBefore : nullptr, f_AsText(l_jV, l_bHad ? &l_strBefore : nullptr));
        }
    // flags.json, counters.json: hand-aligned, so only the changed entries' lines are replaced
    for (const std::string l_strKey : {"flags", "counters"}) {
        std::vector<std::pair<std::string, Json>> l_vChanges;
        for (auto& [l_strK, l_jV] : m_jS[l_strKey].items()) if (!m_jBase[l_strKey].contains(l_strK) || m_jBase[l_strKey][l_strK] != l_jV) l_vChanges.push_back({l_strK, l_jV});
        if (l_vChanges.empty()) continue;
        const std::string l_strPath = l_strA + "story/" + l_strKey + ".json";
        std::string l_strBefore;
        if (!l_fnRead(l_strPath, l_strBefore)) continue;
        Json l_jObj = Json::parse(l_strBefore, nullptr, false);
        if (l_jObj.is_discarded()) continue;
        std::string l_strText = f_PatchEntries(l_strBefore, l_jObj, l_vChanges);
        if (l_strText.empty()) { for (auto& [l_strK, l_jV] : l_vChanges) l_jObj[l_strK] = l_jV; l_strText = f_AsText(l_jObj, &l_strBefore); }
        l_fnPut(l_strPath, &l_strBefore, l_strText);
    }
    return l_vOut;
}

bool EventProject::f_WriteAll(const std::vector<FileWrite>& l_vFiles, const std::string& l_strStamp, std::string& l_strErr) {
    for (const FileWrite& l_oF : l_vFiles)
        if (l_oF.m_bExisted && !f_WriteText(m_strRoot + "/Build/editor_backups/" + l_strStamp + "/" + l_oF.m_strPath, l_oF.m_strBefore)) {
            l_strErr = "Build/editor_backups/" + l_strStamp + "/" + l_oF.m_strPath + ": cannot write the backup";
            return false;
        }
    for (const FileWrite& l_oF : l_vFiles)
        if (!f_WriteText(m_strRoot + "/" + l_oF.m_strPath, l_oF.m_strText)) { l_strErr = l_oF.m_strPath + ": cannot write"; return false; }
    m_jBase = m_jS;
    return true;
}

// ---- the talk preview ----------------------------------------------------------------------------------------------

void TalkPreview::f_Reset(EventProject& l_oP, const std::string& l_strId) {
    *this = TalkPreview();
    m_strNpc = l_strId;
    for (auto& [l_strK, l_jV] : l_oP.m_jS["vars"].items()) m_mapVars[l_strK] = f_Num(l_jV.value("default", Json(0)));
    m_mapStats = {{"hp", 100}, {"atk", 10}, {"def", 5}, {"gold", 0}, {"lv", 1}};
    auto l_itIt = l_oP.m_mapNpcPlaces.find(l_strId);
    m_strStage = l_itIt != l_oP.m_mapNpcPlaces.end() && !l_itIt->second.empty() ? l_itIt->second[0] : std::string();
}

bool TalkPreview::f_Eval(const Json& l_jC) const {
    if (l_jC.is_null()) return true;
    if (l_jC.contains("all") && l_jC["all"].is_array()) return std::all_of(l_jC["all"].begin(), l_jC["all"].end(), [&](const Json& l_jX) { return f_Eval(l_jX); });
    if (l_jC.contains("any") && l_jC["any"].is_array()) return std::any_of(l_jC["any"].begin(), l_jC["any"].end(), [&](const Json& l_jX) { return f_Eval(l_jX); });
    if (l_jC.contains("not")) return !f_Eval(l_jC["not"]);
    const std::string l_strT = f_Str(l_jC, "type");
    auto l_fnGet = [](const std::map<std::string, double>& l_mapM, const std::string& l_strK) { auto l_itIt = l_mapM.find(l_strK); return l_itIt == l_mapM.end() ? 0.0 : l_itIt->second; };
    if (l_strT == "runFlagSet") { const std::string l_strF = f_Str(l_jC, "flag"); return f_StartsWith(l_strF, "event_") && m_setFinished.count(l_strF.substr(6)); }
    if (l_strT == "varAtLeast") return l_fnGet(m_mapVars, f_Str(l_jC, "var")) >= f_Num(l_jC.value("value", Json(0)));
    if (l_strT == "varEquals") return l_fnGet(m_mapVars, f_Str(l_jC, "var")) == f_Num(l_jC.value("value", Json(0)));
    if (l_strT == "counterAtLeast") return l_fnGet(m_mapCounters, f_Str(l_jC, "counter")) >= f_Num(l_jC.value("min", Json(0)));
    if (l_strT == "talkCountAtLeast") { auto l_itIt = m_mapTalks.find(f_Str(l_jC, "npc")); return (l_itIt == m_mapTalks.end() ? 0 : l_itIt->second.m_iCount) >= f_Num(l_jC.value("value", Json(0))); }
    if (l_strT == "stageCleared") return m_setCleared.count(f_Str(l_jC, "stageId")) > 0;
    if (l_strT == "statAtLeast") return l_fnGet(m_mapStats, f_Str(l_jC, "stat")) >= f_Num(l_jC.value("value", Json(0)));
    if (l_strT == "always") return true;
    if (l_strT == "onStage") return m_strStage == f_Str(l_jC, "stageId");
    return false;   // as the game: an unknown leaf (or one the preview cannot know, e.g. itemHeld) fails
}

std::vector<TalkPreview::MenuItem> TalkPreview::f_Menu(EventProject& l_oP, const std::string& l_strId) {
    std::vector<MenuItem> l_vOpen, l_vDone;
    for (Json* l_pE : l_oP.f_TopicsOf(l_strId)) {
        const std::string l_strEid = (*l_pE)["id"].get<std::string>();
        const int l_iCnt = std::max(m_mapCounts.count(l_strEid) ? m_mapCounts[l_strEid] : 0, m_setFinished.count(l_strEid) ? 1 : 0);
        const bool l_bLeft = (*l_pE)["once"] == false && ((*l_pE)["maxTimes"].get<int>() == 0 || l_iCnt < (*l_pE)["maxTimes"].get<int>());
        if (m_setFinished.count(l_strEid) && !l_bLeft) { if (!(*l_pE)["afterDialogue"].get<std::string>().empty()) l_vDone.push_back({l_strEid, true}); }
        else if ((!l_oP.f_Woken(l_strEid) || m_setStarted.count(l_strEid) || m_setFinished.count(l_strEid)) && f_Eval(l_oP.f_ReqJson(*l_pE))) l_vOpen.push_back({l_strEid, false});
    }
    l_vOpen.insert(l_vOpen.end(), l_vDone.begin(), l_vDone.end());
    return l_vOpen;
}

int TalkPreview::f_Chat(const Json& l_jF) {
    const std::string l_strId = f_Str(l_jF, "npcId");
    TalkState& l_oSt = m_mapTalks[l_strId];
    std::vector<Json> l_vLines;
    if (l_jF.contains("chat") && l_jF["chat"].contains("lines"))
        for (const Json& l_jL : l_jF["chat"]["lines"]) l_vLines.push_back(l_jL.is_string() ? Json{{"dialogue", l_jL}} : l_jL);
    std::vector<int> l_vAvail;
    for (size_t l_nI = 0; l_nI < l_vLines.size(); l_nI++) if (f_Eval(l_vLines[l_nI].value("requires", Json()))) l_vAvail.push_back((int)l_nI);
    if (l_vAvail.empty()) return -1;
    const std::string l_strOrder = l_jF.contains("chat") ? f_Str(l_jF["chat"], "order", "loop") : std::string("loop");
    const int l_iN = (int)l_vAvail.size();
    if (l_iN == 1) { if (l_strOrder == "loop") l_oSt.m_iIndex++; return l_vAvail[0]; }
    if (l_strOrder == "random") {
        static std::mt19937 l_oRng{std::random_device{}()};
        int l_iP = (int)(l_oRng() % (unsigned)l_iN);
        if (l_vAvail[(size_t)l_iP] == l_oSt.m_iLast) l_iP = (l_iP + 1) % l_iN;
        l_oSt.m_iLast = l_vAvail[(size_t)l_iP];
        return l_vAvail[(size_t)l_iP];
    }
    const int l_iAt = l_strOrder == "stay" ? std::min(l_oSt.m_iIndex, l_iN - 1) : l_oSt.m_iIndex;
    const int l_iLine = l_vAvail[(size_t)(l_iAt % l_iN)];
    if (l_strOrder == "loop" || l_oSt.m_iIndex < l_iN - 1) l_oSt.m_iIndex++;
    return l_iLine;
}

std::string TalkPreview::f_FirstText(const EventProject& l_oP, const std::string& l_strDlg, const std::string& l_strLang, bool* l_pMissing) const {
    const Json* l_pD = f_Member(l_oP.m_jS["dialogues"], l_strDlg.c_str());
    if (l_pMissing) *l_pMissing = !l_pD;
    if (!l_pD) return std::string();
    std::string l_strStart;
    if (l_pD->contains("start") && (*l_pD)["start"].is_array()) {
        for (const Json& l_jS : (*l_pD)["start"]) if (f_Eval(l_jS.value("requires", Json()))) { l_strStart = f_Str(l_jS, "node"); break; }
    } else l_strStart = f_Str(*l_pD, "start");
    const Json* l_pNode = l_pD->contains("nodes") ? f_Member((*l_pD)["nodes"], l_strStart.c_str()) : nullptr;
    if (!l_pNode) return "?";
    const Json l_jT = l_pNode->value("text", Json());
    if (l_jT.is_string()) return l_jT.get<std::string>();
    for (const std::string& l_strL : {l_strLang, std::string("en")}) if (!f_Str(l_jT, l_strL.c_str()).empty()) return f_Str(l_jT, l_strL.c_str());
    return std::string();
}

void TalkPreview::f_Talk(EventProject& l_oP, const std::string& l_strId, const std::string& l_strLang) {
    Json l_jF = l_oP.m_jS["npcs"].contains(l_strId) ? l_oP.m_jS["npcs"][l_strId] : Json{{"npcId", l_strId}, {"chat", Json{{"lines", Json::array({Json{{"dialogue", l_strId}}})}}}};
    if (!l_jF.contains("npcId")) l_jF["npcId"] = l_strId;
    TalkState& l_oSt = m_mapTalks[l_strId];
    const std::vector<MenuItem> l_vM = f_Menu(l_oP, l_strId);
    int l_iOpen = 0;
    for (const MenuItem& l_oX : l_vM) if (!l_oX.m_bFinished) l_iOpen++;
    Entry l_oE;
    l_oE.m_iN = l_oSt.m_iCount + 1;
    const bool l_bAlways = l_jF.contains("topics") && f_Str(l_jF["topics"], "menu") == "always";
    if (l_vM.empty()) {
        const int l_iL = f_Chat(l_jF);
        l_oE.m_strKind = "chat";
        if (l_iL >= 0) {
            const Json& l_jLine = l_jF["chat"]["lines"][(size_t)l_iL];
            l_oE.m_strDialogue = l_jLine.is_string() ? l_jLine.get<std::string>() : f_Str(l_jLine, "dialogue");
            bool l_bMiss = false;
            l_oE.m_strText = f_FirstText(l_oP, l_oE.m_strDialogue, l_strLang, &l_bMiss);
            if (l_bMiss) l_oE.m_strText = "\x01missing";
        }
    } else if (l_vM.size() == 1 && l_iOpen == 1 && !l_bAlways) { l_oE.m_strKind = "topic"; l_oE.m_vMenu = l_vM; }
    else { l_oE.m_strKind = "menu"; l_oE.m_vMenu = l_vM; }
    m_mapTalks[l_strId].m_iCount++;
    m_vLog.insert(m_vLog.begin(), l_oE);
}

void TalkPreview::f_Finish(EventProject& l_oP, const std::string& l_strId) {
    const Json* l_pE = l_oP.f_Ev(l_strId);
    if (!l_pE) return;
    const bool l_bAgain = m_setFinished.count(l_strId) > 0;
    m_mapCounts[l_strId] = std::max(m_mapCounts.count(l_strId) ? m_mapCounts[l_strId] : 0, l_bAgain ? 1 : 0) + 1;
    m_setFinished.insert(l_strId);
    m_setStarted.insert(l_strId);
    if (!l_bAgain) for (const Json& l_jX : (*l_pE)["next"]) m_setStarted.insert(l_jX.get<std::string>());
    for (const Json& l_jA : (*l_pE)["actions"]) {
        const std::string l_strT = f_Str(l_jA, "type");
        if (l_strT == "setVar") m_mapVars[f_Str(l_jA, "var")] = f_Num(l_jA.value("value", Json(0)));
        if (l_strT == "addVar") m_mapVars[f_Str(l_jA, "var")] += f_Num(l_jA.value("delta", Json(0)));
        if (l_strT == "addCounter") m_mapCounters[f_Str(l_jA, "counter")] += f_Num(l_jA.value("delta", Json(0)));
    }
}

void TalkPreview::f_Tick(EventProject& l_oP, const std::string& l_strId, bool l_bOn) {
    if (!l_bOn) { m_setFinished.erase(l_strId); return; }
    m_setFinished.insert(l_strId);
    m_setStarted.insert(l_strId);
    if (const Json* l_pE = l_oP.f_Ev(l_strId)) for (const Json& l_jX : (*l_pE)["next"]) m_setStarted.insert(l_jX.get<std::string>());
}

}  // namespace stage

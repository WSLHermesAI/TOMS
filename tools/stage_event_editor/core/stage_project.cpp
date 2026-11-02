// stage_project.cpp -- see stage_project.h. Ported from tools/web_editors/stage_editor.html and make_bundle.py;
// the two must keep the same rules (a floor saved by either editor reads the same in the other).
#include "stage_project.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace stage {

namespace {

const char* const g_pszFreeChars = "EFGIJKLMNOPQSTVWXefgijlnoqtuwxz7890ACM$%&*+=?!^~<>";

bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }
bool f_EndsWith(const std::string& l_strS, const std::string& l_strP) { return l_strS.size() >= l_strP.size() && l_strS.compare(l_strS.size() - l_strP.size(), l_strP.size(), l_strP) == 0; }

std::pair<std::string, std::string> f_SplitSem(const std::string& l_strSem) {
    const size_t l_nC = l_strSem.find(':');
    return l_nC == std::string::npos ? std::make_pair(l_strSem, std::string()) : std::make_pair(l_strSem.substr(0, l_nC), l_strSem.substr(l_nC + 1));
}

// file names in a folder matching a prefix / suffix, sorted (as the HTML editors' sort())
std::vector<std::string> f_ListFiles(const std::string& l_strDir, const std::string& l_strPrefix, const std::string& l_strSuffix) {
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

std::string f_CellKey(int l_iX, int l_iY) { return std::to_string(l_iX) + "," + std::to_string(l_iY); }

const std::map<std::string, std::pair<const char*, const char*>>& f_BuiltinNpcNames() {
    static const std::map<std::string, std::pair<const char*, const char*>> l_mapM = {
        {"sorcerer", {"Sorcerer", "法師"}}, {"villager", {"Villager", "村民"}}, {"king", {"King", "國王"}},
        {"princess", {"Princess", "公主"}}, {"handmaiden", {"Handmaiden", "侍女"}}, {"skeleton_scholar", {"Skeleton scholar", "骷髏學者"}}};
    return l_mapM;
}

}  // namespace

const std::map<std::string, char>& f_ReservedChars() {
    static const std::map<std::string, char> l_mapM = {
        {"door:yellow", 'y'}, {"door:blue", 'b'}, {"door:red", 'r'}, {"stairs_up", 'U'}, {"stairs_down", 'D'}};
    return l_mapM;
}

const std::map<std::string, char>& f_PreferredChars() {
    static const std::map<std::string, char> l_mapM = {
        {"wall", '#'}, {"floor", '.'}, {"player_start", '@'}, {"stairs_up", 'U'}, {"stairs_down", 'D'},
        {"door:yellow", 'y'}, {"door:blue", 'b'}, {"door:red", 'r'}, {"key:yellow", 'Y'}, {"key:blue", 'B'}, {"key:red", 'R'},
        {"npc:sorcerer", 's'}, {"npc:villager", 'v'}, {"npc:king", 'k'}, {"npc:princess", 'p'}, {"npc:handmaiden", 'm'},
        {"monster:slime", '1'}, {"monster:bat", '2'}, {"monster:golem", '3'}, {"monster:skeleton", '4'}, {"monster:wraith", '5'},
        {"monster:demon", '6'}, {"monster:demonlord_vorkath", 'Z'}, {"item:gem_atk", 'a'}, {"item:gem_def", 'd'},
        {"item:potion_red", 'h'}, {"item:potion_blue", 'H'}, {"item:coin", 'c'}};
    return l_mapM;
}

// ---- files ---------------------------------------------------------------------------------------------------------

std::string f_FmtJson(const Json& l_jJ, const Eol& l_oEol, int l_iIndent) {
    std::string l_strS = l_jJ.dump(l_iIndent, ' ', false, Json::error_handler_t::replace);
    if (l_oEol.m_bFinalNewline) l_strS += '\n';
    if (!l_oEol.m_bCrlf) return l_strS;
    std::string l_strOut;
    l_strOut.reserve(l_strS.size() + l_strS.size() / 16);
    for (char l_cC : l_strS) {
        if (l_cC == '\n') l_strOut += '\r';
        l_strOut += l_cC;
    }
    return l_strOut;
}

bool f_ReadText(const std::string& l_strPath, std::string& l_strOut) {
    std::ifstream l_oF(fs::u8path(l_strPath), std::ios::binary);
    if (!l_oF) return false;
    std::ostringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    l_strOut = l_oSs.str();
    return true;
}

bool f_WriteText(const std::string& l_strPath, const std::string& l_strText) {
    std::error_code l_oEc;
    fs::create_directories(fs::u8path(l_strPath).parent_path(), l_oEc);
    std::ofstream l_oF(fs::u8path(l_strPath), std::ios::binary | std::ios::trunc);
    if (!l_oF) return false;
    l_oF.write(l_strText.data(), (std::streamsize)l_strText.size());
    return (bool)l_oF;
}

bool f_ReadJson(const std::string& l_strPath, Json& l_jOut, Eol* l_pEol, std::string* l_pErr) {
    std::string l_strRaw;
    if (!f_ReadText(l_strPath, l_strRaw)) { if (l_pErr) *l_pErr = l_strPath + ": cannot read"; return false; }
    l_jOut = Json::parse(l_strRaw, nullptr, false);
    if (l_jOut.is_discarded()) { if (l_pErr) *l_pErr = l_strPath + ": not valid JSON"; return false; }
    if (l_pEol) {
        l_pEol->m_bCrlf = l_strRaw.find("\r\n") != std::string::npos;
        l_pEol->m_bFinalNewline = !l_strRaw.empty() && l_strRaw.back() == '\n';
    }
    return true;
}

// ---- loading -------------------------------------------------------------------------------------------------------

bool Project::f_Load(const std::string& l_strTomsRoot, std::string& l_strErr) {
    m_strRoot = l_strTomsRoot;
    while (!m_strRoot.empty() && (m_strRoot.back() == '/' || m_strRoot.back() == '\\')) m_strRoot.pop_back();
    std::replace(m_strRoot.begin(), m_strRoot.end(), '\\', '/');
    const std::string l_strData = m_strRoot + "/assets/data/";
    m_vFloors.clear();
    m_mapDocs.clear();
    for (const std::string& l_strN : f_ListFiles(l_strData + "story/floors", "", ".json")) {
        if (f_EndsWith(l_strN, ".stage.json")) continue;
        Floor l_oF;
        l_oF.m_strSpecPath = "assets/data/story/floors/" + l_strN;
        if (!f_ReadJson(f_Abs(l_oF.m_strSpecPath), l_oF.m_jSpec, &l_oF.m_oSpecEol, &l_strErr)) return false;
        const std::string l_strHand = f_Str(l_oF.m_jSpec, "handAuthoredStage");
        l_oF.m_strStagePath = l_strHand.empty() ? l_oF.m_strSpecPath.substr(0, l_oF.m_strSpecPath.size() - 5) + ".stage.json" : "assets/data/stages/" + l_strHand;
        if (!fs::exists(fs::u8path(f_Abs(l_oF.m_strStagePath)))) continue;
        if (!f_ReadJson(f_Abs(l_oF.m_strStagePath), l_oF.m_jStage, &l_oF.m_oStageEol, &l_strErr)) return false;
        l_oF.m_strId = f_Str(l_oF.m_jSpec, "id", l_strN.substr(0, l_strN.size() - 5));
        const Json* l_pTiles = f_Member(l_oF.m_jStage, "tiles");
        if (!l_pTiles || !l_pTiles->is_array() || l_pTiles->empty()) { l_strErr = l_oF.m_strStagePath + ": no tiles"; return false; }
        for (const Json& l_jRow : *l_pTiles)
            if (!l_jRow.is_string() || std::any_of(l_jRow.get_ref<const std::string&>().begin(), l_jRow.get_ref<const std::string&>().end(),
                                                [](char l_cC) { return (unsigned char)l_cC >= 0x80; })) {
                l_strErr = l_oF.m_strStagePath + ": a tiles row is not plain ASCII";
                return false;
            }
        m_vFloors.push_back(std::move(l_oF));
    }
    if (m_vFloors.empty()) { l_strErr = m_strRoot + ": no floors in assets/data/story/floors (pick the TOMS folder)"; return false; }

    Json l_jJ;
    m_jFootprints = Json::object();
    if (f_ReadJson(l_strData + "footprints.json", l_jJ))
        for (auto& [l_strK, l_jV] : l_jJ.items()) if (!f_StartsWith(l_strK, "_")) m_jFootprints[l_strK] = l_jV;
    m_jKinds = Json::object();
    if (f_ReadJson(l_strData + "events/kinds.json", l_jJ))
        for (auto& [l_strK, l_jV] : l_jJ.items()) if (!f_StartsWith(l_strK, "_")) m_jKinds[l_strK] = l_jV;
    // the sprite names the game can draw: the regions of the cooked atlas (a region is a line with no indent and no
    // ':'; the page lines end in ".png"). The raw PNGs live in art_raw/ and are not shipped.
    m_vSprites.clear();
    {
        std::string l_strAtlasText;
        if (f_ReadText(m_strRoot + "/assets/media/atlas/game.atlas", l_strAtlasText)) {
            std::istringstream l_oIn(l_strAtlasText);
            for (std::string l_strLine; std::getline(l_oIn, l_strLine);) {
                if (!l_strLine.empty() && l_strLine.back() == '\r') l_strLine.pop_back();
                if (l_strLine.empty() || l_strLine[0] == ' ' || l_strLine[0] == '\t' || l_strLine.find(':') != std::string::npos ||
                    f_EndsWith(l_strLine, ".png"))
                    continue;
                m_vSprites.push_back(l_strLine);
            }
        }
    }

    // the palette's type records, whole, for the detail windows (written back by Save)
    m_mapDataFiles.clear();
    m_mapDataOrig.clear();
    std::vector<std::string> l_vTypePaths = {f_EnemiesPath(), f_ItemsPath(), f_PalettePath()};   // (the palette file may be absent)
    for (const std::string& l_strN : f_ListFiles(l_strData + "npcs", "", ".json")) l_vTypePaths.push_back("assets/data/npcs/" + l_strN);
    for (const std::string& l_strP : l_vTypePaths) {
        DataFile l_oDf;
        if (!f_ReadJson(f_Abs(l_strP), l_oDf.m_jObj, &l_oDf.m_oEol)) continue;
        m_mapDataOrig[l_strP] = f_FmtJson(l_oDf.m_jObj, l_oDf.m_oEol);
        m_mapDataFiles[l_strP] = std::move(l_oDf);
    }
    // each .anim file's clip names (a type's or an object's animation: "anim/<file>.anim#<clip>")
    m_iDataVer++;
    m_vAnims.clear();
    for (const std::string& l_strN : f_ListFiles(m_strRoot + "/assets/media/anim", "", ".anim")) {
        std::vector<std::string> l_vClips;
        if (f_ReadJson(m_strRoot + "/assets/media/anim/" + l_strN, l_jJ) && f_Member(l_jJ, "clips"))
            for (const Json& l_jC : l_jJ["clips"]) { const std::string l_strNm = f_Str(l_jC, "name"); if (!l_strNm.empty()) l_vClips.push_back(l_strNm); }
        m_vAnims.emplace_back("anim/" + l_strN, std::move(l_vClips));
    }
    return f_ReloadEvents(l_strErr);
}

bool Project::f_ReloadEvents(std::string& l_strErr) {
    const std::string l_strData = m_strRoot + "/assets/data/";
    Json l_jJ;
    std::vector<Event> l_vFresh;
    for (const std::string& l_strN : f_ListFiles(l_strData + "events", "pool_", ".json")) {
        if (!f_ReadJson(l_strData + "events/" + l_strN, l_jJ, nullptr, &l_strErr)) return false;
        const Json* l_pList = f_Member(l_jJ, "events");
        if (!l_pList || !l_pList->is_array()) continue;
        for (const Json& l_jE : *l_pList) {
            Event l_oEv;
            l_oEv.m_strId = f_Str(l_jE, "eventId");
            l_oEv.m_strKind = f_Str(l_jE, "kind");
            l_oEv.m_strPool = l_strN.substr(0, l_strN.size() - 5);
            l_oEv.m_strText = f_Str(l_jE, "text");
            l_oEv.m_strTitle = f_Str(l_jE, "title", l_oEv.m_strId + ".title");
            l_oEv.m_jRec = Json::object();
            for (const char* l_pszK : {"trigger", "requires", "actions", "next", "dialogue", "afterDialogue", "topicOrder", "once", "maxTimes"})
                if (const Json* l_pV = f_Member(l_jE, l_pszK)) l_oEv.m_jRec[l_pszK] = *l_pV;
            l_vFresh.push_back(std::move(l_oEv));
        }
    }
    for (const Event& l_oP : m_vEvents)   // place mode's not-yet-saved events stay until a pool defines them
        if (l_oP.m_bPending && std::none_of(l_vFresh.begin(), l_vFresh.end(), [&](const Event& l_oE) { return l_oE.m_strId == l_oP.m_strId; })) l_vFresh.push_back(l_oP);
    m_vEvents = std::move(l_vFresh);
    f_IndexEvents();

    if (!f_ReadJson(l_strData + "text.json", l_jJ, nullptr, &l_strErr)) return false;
    m_jStrings = f_Member(l_jJ, "strings") ? l_jJ["strings"] : Json::object();

    m_mapNpcs.clear();
    for (const std::string& l_strN : f_ListFiles(l_strData + "npcs", "", ".json"))
        if (f_ReadJson(l_strData + "npcs/" + l_strN, l_jJ)) m_mapNpcs[l_strN.substr(0, l_strN.size() - 5)] = l_jJ;
    // what a talk event can change: every action its dialogue's choices run (the story check's variable reach)
    m_mapDialogueActions.clear();
    for (const std::string& l_strN : f_ListFiles(l_strData + "dialogue", "", ".json")) {
        if (!f_ReadJson(l_strData + "dialogue/" + l_strN, l_jJ)) continue;
        std::vector<Json> l_vActs;
        if (const Json* l_pNodes = f_Member(l_jJ, "nodes"))
            for (auto& [l_strK, l_jNode] : l_pNodes->items()) {
                (void)l_strK;
                const Json* l_pChoices = f_Member(l_jNode, "choices");
                if (!l_pChoices || !l_pChoices->is_array()) continue;
                for (const Json& l_jC : *l_pChoices) {
                    const Json* l_pA = f_Member(l_jC, "actions");
                    if (!l_pA) continue;
                    if (l_pA->is_array()) for (const Json& l_jX : *l_pA) l_vActs.push_back(l_jX);
                    else if (l_pA->is_object()) l_vActs.push_back(*l_pA);
                }
            }
        if (!l_vActs.empty()) m_mapDialogueActions[l_strN.substr(0, l_strN.size() - 5)] = std::move(l_vActs);
    }
    // NPC files the palette edits: the ones on disk now, unless changed here (Save would write them)
    for (const auto& [l_strId, l_jObj] : m_mapNpcs) {
        const std::string l_strP = f_NpcPath(l_strId);
        auto l_itIt = m_mapDataFiles.find(l_strP);
        if (l_itIt == m_mapDataFiles.end()) {
            DataFile l_oDf;
            f_ReadJson(f_Abs(l_strP), l_oDf.m_jObj, &l_oDf.m_oEol);
            m_mapDataOrig[l_strP] = f_FmtJson(l_oDf.m_jObj, l_oDf.m_oEol);
            m_mapDataFiles[l_strP] = std::move(l_oDf);
        } else if (!l_itIt->second.m_bDeleted && f_FmtJson(l_itIt->second.m_jObj, l_itIt->second.m_oEol) == m_mapDataOrig[l_strP]) {
            f_ReadJson(f_Abs(l_strP), l_itIt->second.m_jObj, &l_itIt->second.m_oEol);
            m_mapDataOrig[l_strP] = f_FmtJson(l_itIt->second.m_jObj, l_itIt->second.m_oEol);
        }
    }
    m_iStoryVer++;
    m_iDataVer++;
    return true;
}

int Project::f_RefreshCleanFloors() {
    int l_iN = 0;
    for (Floor& l_oF : m_vFloors) {
        if (f_IsDirty(l_oF.m_strId)) continue;
        Floor l_oFresh = l_oF;
        if (!f_ReadJson(f_Abs(l_oF.m_strSpecPath), l_oFresh.m_jSpec, &l_oFresh.m_oSpecEol) || !f_ReadJson(f_Abs(l_oF.m_strStagePath), l_oFresh.m_jStage, &l_oFresh.m_oStageEol)) continue;
        if (l_oFresh.m_jSpec == l_oF.m_jSpec && l_oFresh.m_jStage == l_oF.m_jStage && l_oFresh.m_oSpecEol.m_bCrlf == l_oF.m_oSpecEol.m_bCrlf) continue;
        l_oF = std::move(l_oFresh);
        m_mapDocs.erase(l_oF.m_strId);   // rebuilt from the new files on next use
        l_iN++;
    }
    if (l_iN) m_iStoryVer++;
    return l_iN;
}

void Project::f_IndexEvents() {
    m_mapEvIndex.clear();
    for (size_t l_nI = 0; l_nI < m_vEvents.size(); l_nI++) m_mapEvIndex.emplace(m_vEvents[l_nI].m_strId, l_nI);
}

const Floor* Project::f_Floor(const std::string& l_strId) const {
    for (const Floor& l_oF : m_vFloors) if (l_oF.m_strId == l_strId) return &l_oF;
    return nullptr;
}

int Project::f_FloorIndex(const std::string& l_strId) const {
    for (size_t l_nI = 0; l_nI < m_vFloors.size(); l_nI++) if (m_vFloors[l_nI].m_strId == l_strId) return (int)l_nI;
    return -1;
}

StageDoc& Project::f_Doc(const std::string& l_strFid) {
    auto l_itIt = m_mapDocs.find(l_strFid);
    if (l_itIt != m_mapDocs.end()) return *l_itIt->second;
    auto l_upD = std::make_unique<StageDoc>();
    const Floor* l_pF = f_Floor(l_strFid);
    l_upD->m_pF = l_pF;
    l_upD->m_jSt = l_pF->m_jStage;
    l_upD->m_jSpec = l_pF->m_jSpec;
    for (const Json& l_jRow : l_pF->m_jStage.at("tiles")) l_upD->m_vGrid.push_back(l_jRow.get<std::string>());
    l_upD->m_strOrigStage = f_FmtJson(l_pF->m_jStage, l_pF->m_oStageEol);
    l_upD->m_strOrigSpec = f_FmtJson(l_pF->m_jSpec, l_pF->m_oSpecEol);
    return *m_mapDocs.emplace(l_strFid, std::move(l_upD)).first->second;
}

const Event* Project::f_Event(const std::string& l_strId) const {
    auto l_itIt = m_mapEvIndex.find(l_strId);
    return l_itIt == m_mapEvIndex.end() ? nullptr : &m_vEvents[l_itIt->second];
}

Event& Project::f_AddPendingEvent(const std::string& l_strId, const std::string& l_strKind) {
    Event l_oE;
    l_oE.m_strId = l_strId;
    l_oE.m_strKind = l_strKind;
    l_oE.m_strPool = "(event editor)";
    l_oE.m_strText = l_strId + ".text";
    l_oE.m_strTitle = l_strId + ".title";
    l_oE.m_jRec = Json::object();
    l_oE.m_bPending = true;
    m_vEvents.push_back(std::move(l_oE));
    f_IndexEvents();
    return m_vEvents.back();
}

std::vector<std::string> Project::f_Pools() const {
    std::vector<std::string> l_vOut;
    for (const Event& l_oE : m_vEvents) if (std::find(l_vOut.begin(), l_vOut.end(), l_oE.m_strPool) == l_vOut.end()) l_vOut.push_back(l_oE.m_strPool);
    return l_vOut;
}

// ---- text and names --------------------------------------------------------------------------------------------------

std::string Project::f_Localized(const Json& l_jO) const {
    if (l_jO.is_string()) {
        const Json l_jT = f_TextOf(l_jO.get<std::string>());
        return l_jT.empty() ? l_jO.get<std::string>() : f_Localized(l_jT);
    }
    if (!l_jO.is_object()) return std::string();
    for (const char* l_pszL : {m_strLang.c_str(), "en", "zh_TW"}) {
        const std::string l_strS = f_Str(l_jO, l_pszL);
        if (!l_strS.empty()) return l_strS;
    }
    return std::string();
}

Json Project::f_TextOf(const std::string& l_strKey) const {
    auto l_itIt = m_mapTextEdits.find(l_strKey);
    if (l_itIt != m_mapTextEdits.end()) return l_itIt->second;
    const Json* l_pV = f_Member(m_jStrings, l_strKey.c_str());
    return l_pV && l_pV->is_object() ? *l_pV : Json::object();
}

std::string Project::f_NameOf(const std::string& l_strSem) const {
    const bool l_bZh = m_strLang == "zh_TW";
    static const std::map<std::string, std::pair<const char*, const char*>> l_mapBasic = {
        {"wall", {"Wall", "牆壁"}}, {"floor", {"Floor", "地板"}}, {"player_start", {"Player start", "玩家起點"}},
        {"stairs_up", {"Stairs up", "往上樓梯"}}, {"stairs_down", {"Stairs down", "往下樓梯"}}};
    static const std::map<std::string, std::pair<const char*, const char*>> l_mapColors = {
        {"yellow", {"yellow", "黃"}}, {"blue", {"blue", "藍"}}, {"red", {"red", "紅"}}};
    auto l_itB = l_mapBasic.find(l_strSem);
    if (l_itB != l_mapBasic.end()) return l_bZh ? l_itB->second.second : l_itB->second.first;
    const auto [l_strCat, l_strId] = f_SplitSem(l_strSem);
    if (l_strCat == "monster") {
        const Json* l_pR = f_TypeRec(l_strSem);
        const std::string l_strN = l_pR ? f_Localized(l_pR->value("name", Json())) : std::string();
        return l_strN.empty() ? l_strId : l_strN;
    }
    if (l_strCat == "item" || l_strCat == "gate") {
        const Json* l_pR = f_TypeRec("item:" + l_strId);
        std::string l_strN = l_pR ? f_Localized(l_pR->value("name", Json())) : std::string();
        if (l_strN.empty()) l_strN = l_strId;
        if (l_strCat == "item") return l_strN;
        return l_bZh ? "專屬鑰匙門（" + l_strN + "）" : "Gate (" + l_strN + ")";
    }
    if (l_strCat == "npc") {
        const Json* l_pR = f_TypeRec(l_strSem);
        if (l_pR && f_Member(*l_pR, "name")) { const std::string l_strN = f_Localized((*l_pR)["name"]); if (!l_strN.empty()) return l_strN; }
        auto l_itBn = f_BuiltinNpcNames().find(l_strId);
        return l_itBn != f_BuiltinNpcNames().end() ? (l_bZh ? l_itBn->second.second : l_itBn->second.first) : l_strId;
    }
    if (l_strCat == "door" || l_strCat == "key") {
        auto l_itC = l_mapColors.find(l_strId);
        const std::string l_strCol = l_itC != l_mapColors.end() ? (l_bZh ? l_itC->second.second : l_itC->second.first) : l_strId;
        return l_bZh ? l_strCol + (l_strCat == "door" ? "門" : "鑰匙") : l_strCol + (l_strCat == "door" ? " door" : " key");
    }
    if (l_strCat == "event") return l_strId;
    return l_strSem;
}

// ---- types ---------------------------------------------------------------------------------------------------------

Json* Project::f_TypeRec(const std::string& l_strSem) {
    const auto [l_strCat, l_strRawId] = f_SplitSem(l_strSem);
    // a map key "key:<colour>" is the item key_<colour> (items.json), as the game reads it (stage.h): its look and tint
    // are that record's
    const std::string l_strId = l_strCat == "key" ? "key_" + l_strRawId : l_strRawId;
    auto l_fnInFile = [&](const std::string& l_strP, bool l_bWhole) -> Json* {
        auto l_itIt = m_mapDataFiles.find(l_strP);
        if (l_itIt == m_mapDataFiles.end() || l_itIt->second.m_bDeleted) return nullptr;
        if (l_bWhole) return &l_itIt->second.m_jObj;
        Json& l_jO = l_itIt->second.m_jObj;
        if (!l_jO.is_object()) return nullptr;
        auto l_oF = l_jO.find(l_strId);
        return l_oF == l_jO.end() || !l_oF->is_object() ? nullptr : &*l_oF;
    };
    if (l_strCat == "monster") return l_fnInFile(f_EnemiesPath(), false);
    if (l_strCat == "item" || l_strCat == "gate" || l_strCat == "key") return l_fnInFile(f_ItemsPath(), false);
    if (l_strCat == "npc") return l_fnInFile(f_NpcPath(l_strId), true);
    return nullptr;
}

// ---- the palette's own categories ---------------------------------------------------------------------------------

std::vector<std::string> Project::f_PaletteGroups() const {
    std::vector<std::string> l_vOut;
    auto l_itIt = m_mapDataFiles.find(f_PalettePath());
    if (l_itIt == m_mapDataFiles.end() || l_itIt->second.m_bDeleted) return l_vOut;
    if (const Json* l_pG = f_Member(l_itIt->second.m_jObj, "groups"); l_pG && l_pG->is_array())
        for (const Json& l_jN : *l_pG)
            if (l_jN.is_string() && !l_jN.get<std::string>().empty()) l_vOut.push_back(l_jN.get<std::string>());
    return l_vOut;
}

void Project::f_SetPaletteGroups(const std::vector<std::string>& l_vGroups) {
    auto l_itIt = m_mapDataFiles.find(f_PalettePath());
    if (l_vGroups.empty() && (l_itIt == m_mapDataFiles.end() || !m_mapDataOrig.count(f_PalettePath()))) {   // never was a file: nothing to write
        if (l_itIt != m_mapDataFiles.end()) m_mapDataFiles.erase(l_itIt);
        return;
    }
    DataFile& l_oDf = m_mapDataFiles[f_PalettePath()];
    l_oDf.m_bDeleted = false;
    if (!l_oDf.m_jObj.is_object()) l_oDf.m_jObj = Json::object();
    l_oDf.m_jObj["groups"] = l_vGroups;
}

std::string Project::f_GroupOf(const std::string& l_strSem) const {
    const Json* l_pR = f_TypeRec(l_strSem);
    return l_pR ? f_Str(*l_pR, "group", "") : std::string();
}

bool Project::f_SetGroup(const std::string& l_strSem, const std::string& l_strGroup) {
    Json* l_pR = f_TypeRec(l_strSem);
    if (!l_pR) return false;
    if (l_strGroup.empty()) l_pR->erase("group");
    else (*l_pR)["group"] = l_strGroup;
    return true;
}

namespace {
// every type record: the monsters, the items, each NPC file
template <class F> void f_ForEachRecord(std::map<std::string, DataFile>& l_mapFiles, F&& l_oF) {
    for (auto& [l_strPath, l_oDf] : l_mapFiles) {
        if (l_oDf.m_bDeleted || !l_oDf.m_jObj.is_object()) continue;
        if (l_strPath == Project::f_EnemiesPath() || l_strPath == Project::f_ItemsPath()) {
            for (auto& [l_strK, l_jV] : l_oDf.m_jObj.items()) if (l_jV.is_object()) l_oF(l_jV);
        } else if (f_StartsWith(l_strPath, "assets/data/npcs/")) {
            l_oF(l_oDf.m_jObj);
        }
    }
}
}  // namespace

int Project::f_RenameGroup(const std::string& l_strFrom, const std::string& l_strTo) {
    int l_iN = 0;
    f_ForEachRecord(m_mapDataFiles, [&](Json& l_jR) { if (f_Str(l_jR, "group", "") == l_strFrom) { l_jR["group"] = l_strTo; l_iN++; } });
    std::vector<std::string> l_vG = f_PaletteGroups();
    for (std::string& l_strS : l_vG) if (l_strS == l_strFrom) l_strS = l_strTo;
    f_SetPaletteGroups(l_vG);
    return l_iN;
}

int Project::f_ClearGroup(const std::string& l_strGroup) {
    int l_iN = 0;
    f_ForEachRecord(m_mapDataFiles, [&](Json& l_jR) { if (f_Str(l_jR, "group", "") == l_strGroup) { l_jR.erase("group"); l_iN++; } });
    return l_iN;
}

Json Project::f_TypeLook(const std::string& l_strSem) const {
    Json l_jL = Json::object();
    if (l_strSem.rfind("gate:", 0) == 0) return l_jL;   // a gate is a door (spriteOf), not its key's picture: as the game draws it
    const Json* l_pR = f_TypeRec(l_strSem);
    if (!l_pR) return l_jL;
    if (const Json* l_pS = f_Member(*l_pR, "sprite"); l_pS && l_pS->is_string()) l_jL["sprite"] = *l_pS;
    if (const Json* l_pLk = f_Member(*l_pR, "look"); l_pLk && l_pLk->is_object())
        for (auto& [l_strK, l_jV] : l_pLk->items()) l_jL[l_strK] = l_jV;
    return l_jL;
}

std::string Project::f_SpriteOf(const std::string& l_strSem) const {
    auto l_fnHas = [&](const std::string& l_strN) { return std::find(m_vSprites.begin(), m_vSprites.end(), l_strN) != m_vSprites.end(); };
    if (l_strSem == "player_start") return "player";
    if (l_strSem == "wall" || l_strSem == "floor" || l_strSem == "stairs_up" || l_strSem == "stairs_down")   // the floor's theme, if it has one
        return !m_strTheme.empty() && l_fnHas(l_strSem + "_" + m_strTheme) ? l_strSem + "_" + m_strTheme : l_strSem;
    const auto [l_strCat, l_strId] = f_SplitSem(l_strSem);
    if (l_strCat == "monster" || l_strCat == "item" || l_strCat == "npc" || l_strCat == "key") {   // a type's own sprite (its record)
        std::string l_strOwn = f_Str(f_TypeLook(l_strSem), "sprite");
        if (f_EndsWith(l_strOwn, ".png")) l_strOwn.resize(l_strOwn.size() - 4);
        if (!l_strOwn.empty() && l_fnHas(l_strOwn)) return l_strOwn;
    }
    if (l_strCat == "door" || l_strCat == "key") return l_strCat + "_" + l_strId;
    if (l_strCat == "gate") {   // its key item's own door picture, else the yellow door with the key's colour over it
        const Json* l_pR = f_TypeRec(l_strSem);
        const std::string l_strDoor = l_pR ? f_Str(*l_pR, "door", "") : std::string();
        return !l_strDoor.empty() && l_fnHas(l_strDoor) ? l_strDoor : std::string("door_yellow");
    }
    if (l_strCat == "monster") return l_strId == "demonlord_vorkath" ? "boss_demonlord" : l_strId;
    if (l_strCat == "npc") return l_fnHas("npc_" + l_strId) ? "npc_" + l_strId : "skeleton";
    if (l_strCat == "item") return l_strId;
    return std::string();   // events are drawn as plates
}

std::vector<std::string> Project::f_EnemyIds() const {
    std::vector<std::string> l_vOut;
    auto l_itIt = m_mapDataFiles.find(f_EnemiesPath());
    if (l_itIt != m_mapDataFiles.end() && l_itIt->second.m_jObj.is_object())
        for (auto& [l_strK, l_jV] : l_itIt->second.m_jObj.items()) if (!f_StartsWith(l_strK, "_") && l_jV.is_object()) l_vOut.push_back(l_strK);
    return l_vOut;
}

std::vector<std::string> Project::f_ItemIds() const {
    std::vector<std::string> l_vOut;
    auto l_itIt = m_mapDataFiles.find(f_ItemsPath());
    if (l_itIt != m_mapDataFiles.end() && l_itIt->second.m_jObj.is_object())
        for (auto& [l_strK, l_jV] : l_itIt->second.m_jObj.items()) if (!f_StartsWith(l_strK, "_") && l_jV.is_object()) l_vOut.push_back(l_strK);
    return l_vOut;
}

std::string Project::f_ItemType(const std::string& l_strId) const {
    const Json* l_pR = f_TypeRec("item:" + l_strId);
    return l_pR ? f_Str(*l_pR, "type", "consumable") : std::string("consumable");
}

std::vector<std::string> Project::f_NpcIds() const {
    std::vector<std::string> l_vOut = {"sorcerer", "villager", "king", "princess", "handmaiden"};
    auto l_fnAdd = [&](const std::string& l_strId) { if (std::find(l_vOut.begin(), l_vOut.end(), l_strId) == l_vOut.end()) l_vOut.push_back(l_strId); };
    for (const auto& [l_strP, l_oDf] : m_mapDataFiles) {
        const std::string l_strPre = "assets/data/npcs/";
        if (f_StartsWith(l_strP, l_strPre.c_str()) && !l_oDf.m_bDeleted) l_fnAdd(l_strP.substr(l_strPre.size(), l_strP.size() - l_strPre.size() - 5));
    }
    for (const Floor& l_oF : m_vFloors)
        if (const Json* l_pLg = f_Member(l_oF.m_jStage, "legend"))
            for (auto& [l_strC, l_jV] : l_pLg->items()) { (void)l_strC; if (l_jV.is_string() && f_StartsWith(l_jV.get<std::string>(), "npc:")) l_fnAdd(l_jV.get<std::string>().substr(4)); }
    return l_vOut;
}

std::vector<std::string> Project::f_TypeUses(const std::string& l_strSem) {
    std::vector<std::string> l_vOut;
    const bool l_bItem = f_StartsWith(l_strSem, "item:");
    const std::string l_strGate = l_bItem ? "gate:" + l_strSem.substr(5) : std::string();
    // the item key_<colour> is also every "key:<colour>" cell
    const std::string l_strMapKey = l_bItem && f_StartsWith(l_strSem, "item:key_") ? "key:" + l_strSem.substr(9) : std::string();
    for (const Floor& l_oF : m_vFloors) {
        const bool l_bOpen = f_HasDoc(l_oF.m_strId);
        const Json* l_pLgP = f_Member(l_bOpen ? f_Doc(l_oF.m_strId).m_jSt : l_oF.m_jStage, "legend");
        if (!l_pLgP) continue;
        std::string l_strTiles;
        if (l_bOpen) for (const std::string& l_strR : f_Doc(l_oF.m_strId).m_vGrid) l_strTiles += l_strR;
        else for (const Json& l_jR : l_oF.m_jStage.at("tiles")) l_strTiles += l_jR.get<std::string>();
        for (auto& [l_strC, l_jV] : l_pLgP->items()) {
            if (!l_jV.is_string() || l_strC.size() != 1) continue;
            const std::string l_strS = l_jV.get<std::string>();
            if ((l_strS == l_strSem || (l_bItem && l_strS == l_strGate) || (!l_strMapKey.empty() && l_strS == l_strMapKey)) && l_strTiles.find(l_strC[0]) != std::string::npos) {
                l_vOut.push_back(l_oF.m_strId);
                break;
            }
        }
    }
    return l_vOut;
}

// ---- cells ---------------------------------------------------------------------------------------------------------

std::string Project::f_SemAt(const StageDoc& l_oD, int l_iX, int l_iY) const {
    if (!l_oD.f_Inside(l_iX, l_iY)) return std::string();
    const char l_cC = l_oD.f_At(l_iX, l_iY);
    if (l_cC == '#') return "wall";
    if (l_cC == '.') return "floor";
    if (l_cC == '@') return "player_start";
    const Json* l_pLg = f_Member(l_oD.m_jSt, "legend");
    const Json* l_pV = l_pLg ? f_Member(*l_pLg, std::string(1, l_cC).c_str()) : nullptr;
    return l_pV && l_pV->is_string() ? l_pV->get<std::string>() : std::string();
}

std::map<std::string, int> Project::f_PlacedEvents(const StageDoc& l_oD) const {
    int l_arrCount[256] = {};
    for (const std::string& l_strR : l_oD.m_vGrid) for (char l_cC : l_strR) l_arrCount[(unsigned char)l_cC]++;
    std::map<std::string, int> l_mapOut;
    const Json* l_pLg = f_Member(l_oD.m_jSt, "legend");
    if (!l_pLg || !l_pLg->is_object()) return l_mapOut;
    for (const auto& [l_strK, l_jV] : l_pLg->items())
        if (l_strK.size() == 1 && l_arrCount[(unsigned char)l_strK[0]] && l_jV.is_string()) {
            const std::string& l_strS = l_jV.get_ref<const std::string&>();
            if (l_strS.rfind("event:", 0) == 0) l_mapOut[l_strS.substr(6)] += l_arrCount[(unsigned char)l_strK[0]];
        }
    return l_mapOut;
}

char Project::f_CharFor(StageDoc& l_oD, const std::string& l_strSem) const {
    if (l_strSem == "wall") return '#';
    if (l_strSem == "floor") return '.';
    if (l_strSem == "player_start") return '@';
    Json& l_jLg = l_oD.m_jSt["legend"];
    auto l_fnUsedChars = [&] {
        std::set<char> l_setUsed;
        for (auto& [l_strK, l_jV] : l_jLg.items()) { (void)l_jV; if (l_strK.size() == 1) l_setUsed.insert(l_strK[0]); }
        for (const std::string& l_strR : l_oD.m_vGrid) l_setUsed.insert(l_strR.begin(), l_strR.end());
        return l_setUsed;
    };
    auto l_fnFirstFree = [&](const std::set<char>& l_setUsed) {
        for (const char* l_pszP = g_pszFreeChars; *l_pszP; l_pszP++) if (!l_setUsed.count(*l_pszP)) return *l_pszP;
        return '\0';
    };
    auto l_itRes = f_ReservedChars().find(l_strSem);
    if (l_itRes != f_ReservedChars().end()) {   // a door or stair always gets its own char; whatever used it moves to a free one
        const char l_cC = l_itRes->second;
        const std::string l_strCk(1, l_cC);
        if (l_jLg.contains(l_strCk) && l_jLg[l_strCk] != l_strSem) {
            const Json l_jOther = l_jLg[l_strCk];
            const char l_cNc = l_fnFirstFree(l_fnUsedChars());
            l_jLg.erase(l_strCk);
            if (l_cNc) {
                l_jLg[std::string(1, l_cNc)] = l_jOther;
                for (std::string& l_strR : l_oD.m_vGrid) std::replace(l_strR.begin(), l_strR.end(), l_cC, l_cNc);
            }
        }
        l_jLg[l_strCk] = l_strSem;
        return l_cC;
    }
    for (auto& [l_strK, l_jV] : l_jLg.items()) if (l_strK.size() == 1 && l_jV == l_strSem) return l_strK[0];
    const std::set<char> l_setUsed = l_fnUsedChars();
    auto l_itPref = f_PreferredChars().find(l_strSem);
    const char l_cPick = l_itPref != f_PreferredChars().end() && !l_setUsed.count(l_itPref->second) ? l_itPref->second : l_fnFirstFree(l_setUsed);
    if (!l_cPick) return '\0';
    l_jLg[std::string(1, l_cPick)] = l_strSem;
    return l_cPick;
}

bool Project::f_PutSem(StageDoc& l_oD, int l_iX, int l_iY, const std::string& l_strSem, bool* l_pListed) const {
    if (!l_oD.f_Inside(l_iX, l_iY)) return false;
    if (l_strSem == "player_start")
        for (std::string& l_strR : l_oD.m_vGrid) std::replace(l_strR.begin(), l_strR.end(), '@', '.');
    const char l_cC = f_CharFor(l_oD, l_strSem);
    if (!l_cC || l_oD.m_vGrid[(size_t)l_iY][(size_t)l_iX] == l_cC) return false;
    l_oD.m_vGrid[(size_t)l_iY][(size_t)l_iX] = l_cC;
    f_DropObj(l_oD, l_iX, l_iY);
    if (f_StartsWith(l_strSem, "event:")) {
        const std::string l_strId = l_strSem.substr(6);
        if (!l_oD.m_jSpec.contains("events") || !l_oD.m_jSpec["events"].is_array()) l_oD.m_jSpec["events"] = Json::array();
        Json& l_jEvs = l_oD.m_jSpec["events"];
        if (std::find(l_jEvs.begin(), l_jEvs.end(), Json(l_strId)) == l_jEvs.end()) {
            l_jEvs.push_back(l_strId);
            if (l_pListed) *l_pListed = true;
        }
    }
    return true;
}

Footprint Project::f_FootprintOf(const StageDoc& l_oD, char l_cCh, const std::string& l_strSem) const {
    const Json* l_pOwn = f_Member(l_oD.m_jSt, "footprints");
    const Json* l_pO = l_pOwn && l_cCh ? f_Member(*l_pOwn, std::string(1, l_cCh).c_str()) : nullptr;
    const Json* l_pF = l_pO && l_pO->is_object() ? l_pO : f_Member(m_jFootprints, l_strSem.c_str());
    Footprint l_oFp;
    if (l_pF && l_pF->is_object()) {
        l_oFp.m_iW = std::max(1, l_pF->value("w", 1));
        l_oFp.m_iH = std::max(1, l_pF->value("h", 1));
    }
    return l_oFp;
}

std::vector<Cell> Project::f_FpCells(const StageDoc& l_oD, const std::string& l_strSem, int l_iX, int l_iY) const {
    if (!f_StartsWith(l_strSem, "monster:")) return {{l_iX, l_iY}};
    char l_cCh = 0;
    if (const Json* l_pLg = f_Member(l_oD.m_jSt, "legend"))
        for (auto& [l_strK, l_jV] : l_pLg->items()) if (l_strK.size() == 1 && l_jV == l_strSem) { l_cCh = l_strK[0]; break; }
    const Footprint l_oFp = f_FootprintOf(l_oD, l_cCh, l_strSem);
    std::vector<Cell> l_vOut;
    for (int l_iDy = 0; l_iDy < l_oFp.m_iH; l_iDy++)
        for (int l_iDx = 0; l_iDx < l_oFp.m_iW; l_iDx++) l_vOut.push_back({l_iX + l_iDx, l_iY + l_iDy});
    return l_vOut;
}

PlaceCheck Project::f_PlaceCheck(const StageDoc& l_oD, int l_iX, int l_iY, const std::string& l_strSem, const Cell* l_pIgnore) const {
    PlaceCheck l_oR;
    l_oR.m_vCells = f_FpCells(l_oD, l_strSem, l_iX, l_iY);
    if (!f_IsEntity(l_strSem)) return l_oR;
    if (!l_pIgnore && f_SemAt(l_oD, l_iX, l_iY) == l_strSem) { l_oR.m_bSame = true; return l_oR; }   // painting over the same object does nothing
    // cells covered by the extra part of big monsters (their anchor cell already holds their char)
    std::map<Cell, std::string> l_mapCover;
    for (int l_iYy = 0; l_iYy < l_oD.f_Height(); l_iYy++)
        for (int l_iXx = 0; l_iXx < l_oD.f_Width(); l_iXx++) {
            const std::string l_strS2 = f_SemAt(l_oD, l_iXx, l_iYy);
            if (!f_StartsWith(l_strS2, "monster:") || (l_pIgnore && l_pIgnore->m_iX == l_iXx && l_pIgnore->m_iY == l_iYy)) continue;
            const std::vector<Cell> l_vCs = f_FpCells(l_oD, l_strS2, l_iXx, l_iYy);
            for (size_t l_nI = 1; l_nI < l_vCs.size(); l_nI++) l_mapCover[l_vCs[l_nI]] = l_strS2;
        }
    for (const Cell& l_oC : l_oR.m_vCells) {
        if (!l_oD.f_Inside(l_oC.m_iX, l_oC.m_iY)) { l_oR.m_bOk = false; l_oR.m_eWhy = PlaceCheck::Edge; return l_oR; }
        const bool l_bOwn = l_pIgnore && l_oC.m_iX == l_pIgnore->m_iX && l_oC.m_iY == l_pIgnore->m_iY;
        const char l_cCh = l_oD.f_At(l_oC.m_iX, l_oC.m_iY);
        if (!l_bOwn && l_cCh == '#') { l_oR.m_bOk = false; l_oR.m_eWhy = PlaceCheck::Wall; l_oR.m_iX = l_oC.m_iX; l_oR.m_iY = l_oC.m_iY; return l_oR; }
        if (!l_bOwn && l_cCh != '.') {
            l_oR.m_bOk = false; l_oR.m_eWhy = PlaceCheck::Obj; l_oR.m_iX = l_oC.m_iX; l_oR.m_iY = l_oC.m_iY;
            const std::string l_strS2 = f_SemAt(l_oD, l_oC.m_iX, l_oC.m_iY);
            l_oR.m_strWhat = f_NameOf(l_strS2.empty() ? "?" : l_strS2);
            return l_oR;
        }
        auto l_itCv = l_mapCover.find(l_oC);
        if (l_itCv != l_mapCover.end()) { l_oR.m_bOk = false; l_oR.m_eWhy = PlaceCheck::Obj; l_oR.m_iX = l_oC.m_iX; l_oR.m_iY = l_oC.m_iY; l_oR.m_strWhat = f_NameOf(l_itCv->second); return l_oR; }
    }
    return l_oR;
}

Json* Project::f_ObjOf(StageDoc& l_oD, int l_iX, int l_iY) {
    auto l_oO = l_oD.m_jSt.find("objects");
    if (l_oO == l_oD.m_jSt.end() || !l_oO->is_object()) return nullptr;
    auto l_oIt = l_oO->find(f_CellKey(l_iX, l_iY));
    return l_oIt == l_oO->end() ? nullptr : &*l_oIt;
}

const Json* Project::f_ObjOf(const StageDoc& l_oD, int l_iX, int l_iY) { return f_ObjOf(const_cast<StageDoc&>(l_oD), l_iX, l_iY); }

void Project::f_DropObj(StageDoc& l_oD, int l_iX, int l_iY) {
    auto l_oO = l_oD.m_jSt.find("objects");
    if (l_oO == l_oD.m_jSt.end() || !l_oO->is_object()) return;
    l_oO->erase(f_CellKey(l_iX, l_iY));
    if (l_oO->empty()) l_oD.m_jSt.erase("objects");
}

Json Project::f_LookAt(const StageDoc& l_oD, int l_iX, int l_iY, const std::string& l_strSem) const {
    Json l_jL = f_TypeLook(l_strSem);
    if (const Json* l_pO = f_ObjOf(l_oD, l_iX, l_iY))
        if (const Json* l_pLk = f_Member(*l_pO, "look"); l_pLk && l_pLk->is_object())
            for (auto& [l_strK, l_jV] : l_pLk->items()) l_jL[l_strK] = l_jV;
    return l_jL;
}

std::optional<Cell> Project::f_Find(const StageDoc& l_oD, const std::string& l_strSem) const {
    for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) if (f_SemAt(l_oD, l_iX, l_iY) == l_strSem) return Cell{l_iX, l_iY};
    return std::nullopt;
}

// ---- undo ----------------------------------------------------------------------------------------------------------

std::string Project::f_Snapshot(const StageDoc& l_oD) {
    Json l_jS = Json::object();
    l_jS["grid"] = l_oD.m_vGrid;
    l_jS["legend"] = l_oD.m_jSt.value("legend", Json::object());
    l_jS["spec"] = l_oD.m_jSpec;
    if (l_oD.m_jSt.contains("connect")) l_jS["connect"] = l_oD.m_jSt["connect"];
    if (l_oD.m_jSt.contains("name")) l_jS["name"] = l_oD.m_jSt["name"];
    if (l_oD.m_jSt.contains("objects")) l_jS["objects"] = l_oD.m_jSt["objects"];
    return l_jS.dump();
}

void Project::f_Restore(StageDoc& l_oD, const std::string& l_strSnap) {
    const Json l_jS = Json::parse(l_strSnap);
    l_oD.m_vGrid = l_jS["grid"].get<std::vector<std::string>>();
    l_oD.m_jSt["legend"] = l_jS["legend"];
    l_oD.m_jSpec = l_jS["spec"];
    for (const char* l_pszK : {"connect", "name", "objects"}) {
        if (l_jS.contains(l_pszK)) l_oD.m_jSt[l_pszK] = l_jS[l_pszK];
        else l_oD.m_jSt.erase(l_pszK);
    }
}

bool Project::f_SavesState(const std::string& l_strSem) {
    for (const char* l_pszK : {"monster:", "item:", "key:", "event:", "gate:", "door:"})
        if (f_StartsWith(l_strSem, l_pszK)) return true;
    return false;
}

void Project::f_EnsureUids(StageDoc& l_oD) const {
    std::set<std::string> l_setUsed;
    long long l_i64Next = 1;
    const auto l_fnNum = [](const std::string& l_strU) -> long long {
        return l_strU.size() > 1 && l_strU[0] == 'o' && std::all_of(l_strU.begin() + 1, l_strU.end(), ::isdigit) ? std::atoll(l_strU.c_str() + 1) : 0;
    };
    if (const Json* l_pObjs = f_Member(l_oD.m_jSt, "objects"); l_pObjs && l_pObjs->is_object())
        for (auto& [l_strK, l_jO] : l_pObjs->items())
            if (const Json* l_pU = f_Member(l_jO, "uid"); l_pU && l_pU->is_string()) l_i64Next = std::max(l_i64Next, l_fnNum(l_pU->get<std::string>()) + 1);
    for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) {
            if (!f_SavesState(f_SemAt(l_oD, l_iX, l_iY))) continue;
            Json* l_pO = f_ObjOf(l_oD, l_iX, l_iY);
            const Json* l_pU = l_pO ? f_Member(*l_pO, "uid") : nullptr;
            std::string l_strUid = l_pU && l_pU->is_string() ? l_pU->get<std::string>() : std::string();
            if (!l_strUid.empty() && l_setUsed.insert(l_strUid).second) continue;
            l_strUid = "o" + std::to_string(l_i64Next++);
            l_setUsed.insert(l_strUid);
            if (!l_oD.m_jSt.contains("objects") || !l_oD.m_jSt["objects"].is_object()) l_oD.m_jSt["objects"] = Json::object();
            l_oD.m_jSt["objects"][f_CellKey(l_iX, l_iY)]["uid"] = l_strUid;
        }
}

bool Project::f_CommitEdit(StageDoc& l_oD, const std::string& l_strLabel) {
    if (!l_oD.m_optPending) return false;
    f_EnsureUids(l_oD);
    std::string l_strBefore = std::move(*l_oD.m_optPending);
    l_oD.m_optPending.reset();
    if (l_strBefore == f_Snapshot(l_oD)) return false;
    l_oD.m_vUndo.push_back({std::move(l_strBefore), l_strLabel});
    l_oD.m_vRedo.clear();
    l_oD.m_iVer++;
    m_iStoryVer++;
    return true;
}

bool Project::f_Undo(StageDoc& l_oD, std::string* l_pLabel) {
    if (l_oD.m_vUndo.empty()) return false;
    StageDoc::Step l_oU = std::move(l_oD.m_vUndo.back());
    l_oD.m_vUndo.pop_back();
    l_oD.m_vRedo.push_back({f_Snapshot(l_oD), l_oU.m_strLabel});
    f_Restore(l_oD, l_oU.m_strSnap);
    if (l_pLabel) *l_pLabel = l_oU.m_strLabel;
    l_oD.m_iVer++;
    m_iStoryVer++;
    return true;
}

bool Project::f_Redo(StageDoc& l_oD, std::string* l_pLabel) {
    if (l_oD.m_vRedo.empty()) return false;
    StageDoc::Step l_oR = std::move(l_oD.m_vRedo.back());
    l_oD.m_vRedo.pop_back();
    l_oD.m_vUndo.push_back({f_Snapshot(l_oD), l_oR.m_strLabel});
    f_Restore(l_oD, l_oR.m_strSnap);
    if (l_pLabel) *l_pLabel = l_oR.m_strLabel;
    l_oD.m_iVer++;
    m_iStoryVer++;
    return true;
}

// ---- saving --------------------------------------------------------------------------------------------------------

// The stage as it would be written: rows back to strings, legend in its order. A char this session added and no cell
// uses any more is dropped; entries the file already had stay (hand-made stages list them all).
Json Project::f_StageOut(const StageDoc& l_oD) const {
    Json l_jSt = l_oD.m_jSt;
    l_jSt["tiles"] = l_oD.m_vGrid;
    l_jSt["height"] = l_oD.f_Height();
    l_jSt["width"] = l_oD.f_Width();
    std::set<char> l_setUsed;
    for (const std::string& l_strR : l_oD.m_vGrid) l_setUsed.insert(l_strR.begin(), l_strR.end());
    static const Json l_jNone = Json::object();
    const Json* l_pOrigP = f_Member(l_oD.m_pF->m_jStage, "legend");
    const Json& l_jOrig = l_pOrigP ? *l_pOrigP : l_jNone;
    Json l_jLg = Json::object();
    if (l_jSt.contains("legend"))
        for (auto& [l_strC, l_jV] : l_jSt["legend"].items())
            if ((l_strC.size() == 1 && l_setUsed.count(l_strC[0])) || (l_jOrig.contains(l_strC) && l_jOrig.at(l_strC) == l_jV)) l_jLg[l_strC] = l_jV;
    l_jSt["legend"] = l_jLg;
    return l_jSt;
}

bool Project::f_IsDirty(const std::string& l_strFid) const {
    auto l_itIt = m_mapDocs.find(l_strFid);
    if (l_itIt == m_mapDocs.end()) return false;
    const StageDoc& l_oD = *l_itIt->second;
    if (l_oD.m_iDirtyAt != l_oD.m_iVer) {
        l_oD.m_bDirtyVal = f_StageText(l_oD) != l_oD.m_strOrigStage || f_SpecText(l_oD) != l_oD.m_strOrigSpec;
        l_oD.m_iDirtyAt = l_oD.m_iVer;
    }
    return l_oD.m_bDirtyVal;
}

bool Project::f_DataDirty() const {
    if (!m_mapTextEdits.empty()) return true;
    if (m_iDataDirtyAt != m_iDataVer) {
        m_bDataDirtyVal = false;
        for (const auto& [l_strPath, l_oDf] : m_mapDataFiles) {
            auto l_itOrig = m_mapDataOrig.find(l_strPath);
            if (l_oDf.m_bDeleted ? l_itOrig != m_mapDataOrig.end() : (l_itOrig == m_mapDataOrig.end() || f_FmtJson(l_oDf.m_jObj, l_oDf.m_oEol) != l_itOrig->second)) { m_bDataDirtyVal = true; break; }
        }
        m_iDataDirtyAt = m_iDataVer;
    }
    return m_bDataDirtyVal;
}

bool Project::f_AnyDirty() const {
    for (const auto& [l_strId, l_upD] : m_mapDocs) { (void)l_upD; if (f_IsDirty(l_strId)) return true; }
    return false;
}

}  // namespace stage

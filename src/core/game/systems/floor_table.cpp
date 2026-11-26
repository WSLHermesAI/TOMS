// floor_table.cpp — see floor_table.h.
#include "field_keys.h"    // toms::field: every data field by its constant
#include "../../engine/vfs.h"
#include "floor_table.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>

namespace toms {

namespace {

// "ch_07" -> 7, so the act's ordinal comes from the same string the chapters use.
int f_ActOrdinal(const std::string& l_strAct) {
    int l_iValue = 0;
    bool l_bHaveDigit = false;
    for (char l_cC : l_strAct) {
        if (l_cC >= '0' && l_cC <= '9') { l_iValue = l_iValue * 10 + (l_cC - '0'); l_bHaveDigit = true; }
        else if (l_bHaveDigit) break;               // stop at the end of the numeric run
    }
    return l_bHaveDigit ? l_iValue : 1;
}

nlohmann::json f_ReadJson(const std::string& l_strPath) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(l_strPath, l_strText)) return nlohmann::json();
    if (l_strText.empty()) return nlohmann::json();
    try {
        return nlohmann::json::parse(l_strText);
    } catch (...) {
        return nlohmann::json();                 // a malformed spec is skipped, not fatal
    }
}

} // namespace

FloorTable FloorTable::f_FromSpecs(const std::vector<nlohmann::json>& l_vSpecs) {
    FloorTable l_oTable;
    std::map<std::string, FloorInfo> l_mapById;
    std::vector<std::string> l_vOrder;              // chain order, filled while walking nextFloor

    for (const auto& l_jJ : l_vSpecs) {
        if (!l_jJ.is_object() || !l_jJ.contains(toms::field::g_pszId)) continue;
        FloorInfo l_oF;
        l_oF.m_strId = l_jJ.value(toms::field::g_pszId, std::string());
        if (l_oF.m_strId.empty()) continue;
        l_oF.m_strAct = l_jJ.value(toms::field::g_pszAct, std::string());
        l_oF.m_iActIndex = f_ActOrdinal(l_oF.m_strAct);
        for (char l_cCh : l_oF.m_strAct) if (l_cCh != '_') l_oF.m_strActKey += l_cCh;   // "ch_01" -> "ch01"
        l_oF.m_iIndexInAct = l_jJ.value(toms::field::g_pszIndexInAct, 1);
        l_oF.m_strRole = l_jJ.value(toms::field::g_pszRole, std::string("normal"));
        l_oF.m_strSeal = l_jJ.value(toms::field::g_pszSeal, std::string());
        l_oF.m_strHandAuthoredStage = l_jJ.value(toms::field::g_pszHandAuthoredStage, std::string());
        if (l_jJ.contains(toms::field::g_pszNextFloor) && l_jJ[toms::field::g_pszNextFloor].is_string()) l_oF.m_strNextFloor = l_jJ[toms::field::g_pszNextFloor].get<std::string>();
        if (l_jJ.contains(toms::field::g_pszName)) l_oF.m_strNameKey = l_jJ[toms::field::g_pszName].is_string() ? l_jJ[toms::field::g_pszName].get<std::string>() : std::string();
        if (l_jJ.contains(toms::field::g_pszEvents) && l_jJ[toms::field::g_pszEvents].is_array()) {
            l_oF.m_iEventCount = (int)l_jJ[toms::field::g_pszEvents].size();
            for (const auto& l_jEv : l_jJ[toms::field::g_pszEvents]) if (l_jEv.is_string()) l_oF.m_vEventIds.push_back(l_jEv.get<std::string>());
        }
        if (l_jJ.contains(toms::field::g_pszStairs) && l_jJ[toms::field::g_pszStairs].is_array()) {
            l_oF.m_vStairs = f_StairLinksFromJson(l_jJ[toms::field::g_pszStairs]);
            l_oF.m_bHasStairs = true;
        }
        if (l_jJ.contains(toms::field::g_pszStory) && l_jJ[toms::field::g_pszStory].is_object()) {
            const auto& l_jSt = l_jJ[toms::field::g_pszStory];
            if (l_jSt.contains(toms::field::g_pszIntro) && l_jSt[toms::field::g_pszIntro].is_string()) l_oF.m_strIntroKey = l_jSt[toms::field::g_pszIntro].get<std::string>();
            if (l_jSt.contains(toms::field::g_pszAmbient) && l_jSt[toms::field::g_pszAmbient].is_array())
                for (const auto& l_jA : l_jSt[toms::field::g_pszAmbient]) if (l_jA.is_string()) l_oF.m_vAmbientKeys.push_back(l_jA.get<std::string>());
        }
        l_mapById[l_oF.m_strId] = l_oF;
    }
    if (l_mapById.empty()) return l_oTable;

    // Order by walking the nextFloor chain from the first floor (the one nothing points at). That
    // way `seq` -- and therefore the HUD counter -- always agrees with the way the run actually
    // progresses, instead of trusting the file names to be in order.
    std::map<std::string, int> l_mapIncoming;
    for (auto& l_pairKv : l_mapById) if (!l_pairKv.second.m_strNextFloor.empty()) l_mapIncoming[l_pairKv.second.m_strNextFloor]++;
    std::string l_strCursor;
    for (auto& l_pairKv : l_mapById) {
        if (l_mapIncoming.count(l_pairKv.first) == 0) { l_strCursor = l_pairKv.first; break; }   // the entrance
    }
    if (l_strCursor.empty()) {                        // cyclic/degenerate data: fall back to id order
        for (auto& l_pairKv : l_mapById) l_vOrder.push_back(l_pairKv.first);
        std::sort(l_vOrder.begin(), l_vOrder.end());
    } else {
        while (!l_strCursor.empty() && l_mapById.count(l_strCursor) && l_vOrder.size() <= l_mapById.size()) {
            l_vOrder.push_back(l_strCursor);
            l_strCursor = l_mapById[l_strCursor].m_strNextFloor;
        }
        // Any spec the chain did not visit (a broken link would strand a floor) is appended in id
        // order rather than dropped: an unreachable floor must show up in the table so a test can
        // report it, not silently vanish from the tower.
        std::vector<std::string> l_vRest;
        for (auto& l_pairKv : l_mapById)
            if (std::find(l_vOrder.begin(), l_vOrder.end(), l_pairKv.first) == l_vOrder.end()) l_vRest.push_back(l_pairKv.first);
        std::sort(l_vRest.begin(), l_vRest.end());
        l_vOrder.insert(l_vOrder.end(), l_vRest.begin(), l_vRest.end());
    }

    int l_iSeq = 1;
    for (const std::string& l_strId : l_vOrder) {
        FloorInfo l_oF = l_mapById[l_strId];
        l_oF.m_iSeq = l_iSeq++;
        l_oTable.m_vFloors.push_back(l_oF);
    }
    return l_oTable;
}

FloorTable FloorTable::f_LoadFromDirectory(const std::string& l_strDir) {
    std::vector<nlohmann::json> l_vSpecs;
    for (const std::string& l_strName : toms::f_VfsListDir(l_strDir)) {     // vfs: APK folders on Android
        if (l_strName.empty() || l_strName[0] != 'F') continue;
        if (l_strName.size() >= 11 && l_strName.compare(l_strName.size() - 11, 11, ".stage.json") == 0) continue;  // the map
        if (l_strName.size() < 5 || l_strName.compare(l_strName.size() - 5, 5, ".json") != 0) continue;
        nlohmann::json l_jJ = f_ReadJson(l_strDir + "/" + l_strName);
        if (l_jJ.is_object()) l_vSpecs.push_back(l_jJ);
    }
    return f_FromSpecs(l_vSpecs);
}

const FloorInfo& FloorTable::f_At(int l_iSeq) const {
    static const FloorInfo l_oEmpty;
    if (m_vFloors.empty()) return l_oEmpty;
    if (l_iSeq < 1) l_iSeq = 1;
    if (l_iSeq > (int)m_vFloors.size()) l_iSeq = (int)m_vFloors.size();
    return m_vFloors[l_iSeq - 1];
}

const FloorInfo* FloorTable::f_Find(const std::string& l_strId) const {
    for (const FloorInfo& l_oF : m_vFloors) if (l_oF.m_strId == l_strId) return &l_oF;
    return nullptr;
}

FloorInfo* FloorTable::f_Edit(const std::string& l_strId) {
    for (FloorInfo& l_oF : m_vFloors) if (l_oF.m_strId == l_strId) return &l_oF;
    return nullptr;
}

std::string FloorTable::f_Next(const std::string& l_strId) const {
    const FloorInfo* l_pF = f_Find(l_strId);
    return l_pF ? l_pF->m_strNextFloor : std::string();
}

std::string FloorTable::f_Prev(const std::string& l_strId) const {
    for (int l_iI = 0; l_iI < (int)m_vFloors.size(); l_iI++) {
        if (m_vFloors[l_iI].m_strId == l_strId) return l_iI == 0 ? std::string() : m_vFloors[l_iI - 1].m_strId;
    }
    return std::string();
}

int FloorTable::f_SeqOf(const std::string& l_strId) const {
    for (const FloorInfo& l_oF : m_vFloors) if (l_oF.m_strId == l_strId) return l_oF.m_iSeq;
    return 0;
}

std::string FloorTable::f_MapRelPath(const std::string& l_strId) const {
    const FloorInfo* l_pF = f_Find(l_strId);
    if (!l_pF) return std::string();
    if (!l_pF->m_strHandAuthoredStage.empty()) return "data/stages/" + l_pF->m_strHandAuthoredStage;
    return "data/story/floors/" + l_pF->m_strId + ".stage.json";
}

} // namespace toms

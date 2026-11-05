// bed_logic.cpp -- see bed_logic.h.
#include "bed_logic.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace bed {

// ---- the growth calculator ----

std::vector<int> f_LevelPoints(int l_iTotal, int l_iMaxLevel, double l_dK) {
    std::vector<int> l_vOut;
    if (l_iMaxLevel < 2 || l_iTotal < 0) return l_vOut;
    l_dK = std::max(0.05, l_dK);
    const double l_dDenom = std::pow((double)(l_iMaxLevel - 1), l_dK);
    std::vector<double> l_vRaw;
    for (int l_iN = 2; l_iN <= l_iMaxLevel; l_iN++)
        l_vRaw.push_back(l_iTotal * (std::pow((double)(l_iN - 1), l_dK) - std::pow((double)(l_iN - 2), l_dK)) / l_dDenom);
    int l_iSum = 0;
    for (double l_dR : l_vRaw) { l_vOut.push_back((int)std::floor(l_dR)); l_iSum += l_vOut.back(); }
    std::vector<size_t> l_vOrder(l_vRaw.size());
    std::iota(l_vOrder.begin(), l_vOrder.end(), 0);
    std::stable_sort(l_vOrder.begin(), l_vOrder.end(), [&](size_t l_nA, size_t l_nB) {
        const double l_dFa = l_vRaw[l_nA] - std::floor(l_vRaw[l_nA]), l_dFb = l_vRaw[l_nB] - std::floor(l_vRaw[l_nB]);
        if (std::fabs(l_dFa - l_dFb) > 1e-9) return l_dFa > l_dFb;
        return l_nA > l_nB;   // a tie: the later level
    });
    for (size_t l_nI = 0; l_nI < l_vOrder.size() && l_iSum < l_iTotal; l_nI++, l_iSum++) l_vOut[l_vOrder[l_nI]]++;
    return l_vOut;
}

int f_PointsAt(const ojson& l_jRows, int l_iLevel) {
    int l_iBest = -1, l_iPts = 0;
    if (!l_jRows.is_array()) return 0;
    for (const auto& l_jR : l_jRows) {
        const int l_iFrom = l_jR.value("from", 0);
        if (l_iFrom <= l_iLevel && l_iFrom > l_iBest) { l_iBest = l_iFrom; l_iPts = l_jR.value("points", 0); }
    }
    return l_iPts;
}

ojson f_PointsRows(const std::vector<int>& l_vPerLevel, int l_iMaxLevel, const ojson& l_jOldRows) {
    ojson l_jRows = ojson::array();
    for (size_t l_nI = 0; l_nI < l_vPerLevel.size(); l_nI++) {
        const int l_iLv = (int)l_nI + 2;
        if (!l_jRows.empty() && l_jRows.back()["points"].get<int>() == l_vPerLevel[l_nI]) continue;
        ojson l_jR = ojson::object();
        l_jR["from"] = l_iLv;
        l_jR["points"] = l_vPerLevel[l_nI];
        l_jRows.push_back(l_jR);
    }
    // past the calculator's levels the old table goes on
    const int l_iAfter = f_PointsAt(l_jOldRows, l_iMaxLevel + 1);
    if (l_jRows.empty() || l_jRows.back()["points"].get<int>() != l_iAfter) {
        ojson l_jR = ojson::object();
        l_jR["from"] = l_iMaxLevel + 1;
        l_jR["points"] = l_iAfter;
        l_jRows.push_back(l_jR);
    }
    if (l_jOldRows.is_array())
        for (const auto& l_jR : l_jOldRows)
            if (l_jR.value("from", 0) > l_iMaxLevel + 1 && l_jRows.back()["points"].get<int>() != l_jR.value("points", 0)) l_jRows.push_back(l_jR);
    return l_jRows;
}

// ---- the test setup ----

ojson f_SetupToScenario(const Setup& l_oS) {
    ojson l_jJ = ojson::object();
    l_jJ["_comment"] = "battle_editor's test setup \"" + l_oS.m_strName + "\" (docs/gameplay/13_BATTLE_EDITOR.md §2): not a test, an editor file.";
    l_jJ["scene"] = "battle";
    ojson l_jP = ojson::object();
    if (l_oS.m_oPlayer.m_strWeapon.empty()) l_jP["unarmed"] = true;
    else {
        l_jP["give"] = ojson::array({l_oS.m_oPlayer.m_strWeapon});
        l_jP["equip"] = ojson::array({l_oS.m_oPlayer.m_strWeapon});
        ojson l_jLevel = ojson::object();
        l_jLevel[l_oS.m_oPlayer.m_strWeapon] = l_oS.m_oPlayer.m_iWeaponLevel;
        l_jP["weaponLevel"] = l_jLevel;
    }
    l_jP["lv"] = l_oS.m_oPlayer.m_iLv;
    l_jP["maxhp"] = l_oS.m_oPlayer.m_iMaxhp;
    l_jP["hp"] = std::min(l_oS.m_oPlayer.m_iHp, l_oS.m_oPlayer.m_iMaxhp);
    if (!l_oS.m_oPlayer.m_jAttrs.empty()) l_jP["attrs"] = l_oS.m_oPlayer.m_jAttrs;
    if (l_oS.m_oPlayer.m_iAttrPoints > 0) l_jP["attrPoints"] = l_oS.m_oPlayer.m_iAttrPoints;
    l_jJ["player"] = l_jP;
    ojson l_jB = ojson::object();
    if (!l_oS.m_vFoes.empty()) {
        l_jB["enemy"] = l_oS.m_vFoes[0].m_strId;
        for (auto l_oIt = l_oS.m_vFoes[0].m_jOverrides.begin(); l_oIt != l_oS.m_vFoes[0].m_jOverrides.end(); ++l_oIt) l_jB[l_oIt.key()] = l_oIt.value();
        ojson l_jParty = ojson::array();
        for (size_t l_nI = 1; l_nI < l_oS.m_vFoes.size(); l_nI++) {
            ojson l_jM = ojson::object();
            l_jM["enemy"] = l_oS.m_vFoes[l_nI].m_strId;
            for (auto l_oIt = l_oS.m_vFoes[l_nI].m_jOverrides.begin(); l_oIt != l_oS.m_vFoes[l_nI].m_jOverrides.end(); ++l_oIt) l_jM[l_oIt.key()] = l_oIt.value();
            l_jParty.push_back(l_jM);
        }
        if (!l_jParty.empty()) l_jB["party"] = l_jParty;
    }
    if (!l_oS.m_strPace.empty()) l_jB["pace"] = l_oS.m_strPace;
    l_jB["play"] = "weapon";   // the weapon's own play (bow, sword, staff), as in the game
    l_jJ["battle"] = l_jB;
    ojson l_jEd = ojson::object();   // the setup as the editor holds it
    l_jEd["name"] = l_oS.m_strName;
    l_jEd["pace"] = l_oS.m_strPace;
    ojson l_jFoes = ojson::array();
    for (const SetupFoe& l_oF : l_oS.m_vFoes) l_jFoes.push_back(l_oF.m_strId);
    l_jEd["foes"] = l_jFoes;
    ojson l_jPl = ojson::object();
    l_jPl["lv"] = l_oS.m_oPlayer.m_iLv; l_jPl["hp"] = l_oS.m_oPlayer.m_iHp; l_jPl["maxhp"] = l_oS.m_oPlayer.m_iMaxhp;
    l_jPl["weapon"] = l_oS.m_oPlayer.m_strWeapon; l_jPl["weaponLevel"] = l_oS.m_oPlayer.m_iWeaponLevel;
    l_jPl["attrs"] = l_oS.m_oPlayer.m_jAttrs; l_jPl["attrPoints"] = l_oS.m_oPlayer.m_iAttrPoints;
    l_jEd["player"] = l_jPl;
    l_jJ["_editor"] = l_jEd;
    return l_jJ;
}

bool f_SetupFromScenario(const ojson& l_jJ, Setup& l_oOut) {
    if (!l_jJ.is_object() || !l_jJ.contains("_editor") || !l_jJ["_editor"].is_object()) return false;
    const ojson& l_jEd = l_jJ["_editor"];
    Setup l_oS;
    l_oS.m_strName = l_jEd.value("name", std::string());
    l_oS.m_strPace = l_jEd.value("pace", std::string());
    if (l_jEd.contains("foes") && l_jEd["foes"].is_array())
        for (const auto& l_jId : l_jEd["foes"])
            if (l_jId.is_string()) l_oS.m_vFoes.push_back({l_jId.get<std::string>(), ojson::object()});
    if (l_jEd.contains("player") && l_jEd["player"].is_object()) {
        const ojson& l_jP = l_jEd["player"];
        l_oS.m_oPlayer.m_iLv = l_jP.value("lv", 1);
        l_oS.m_oPlayer.m_iHp = l_jP.value("hp", 120);
        l_oS.m_oPlayer.m_iMaxhp = l_jP.value("maxhp", 120);
        l_oS.m_oPlayer.m_strWeapon = l_jP.value("weapon", std::string("bow"));
        l_oS.m_oPlayer.m_iWeaponLevel = l_jP.value("weaponLevel", 1);
        if (l_jP.contains("attrs") && l_jP["attrs"].is_object()) l_oS.m_oPlayer.m_jAttrs = l_jP["attrs"];
        l_oS.m_oPlayer.m_iAttrPoints = l_jP.value("attrPoints", 0);
    }
    l_oOut = l_oS;
    return true;
}

// ---- writing the data files back ----

namespace {

std::string f_OneLine(const ojson& l_jJ) {
    if (l_jJ.is_object()) {
        if (l_jJ.empty()) return "{}";
        std::string l_strS = "{ ";
        bool l_bFirst = true;
        for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
            if (!l_bFirst) l_strS += ", ";
            l_bFirst = false;
            l_strS += ojson(l_oIt.key()).dump(-1, ' ', false) + ": " + f_OneLine(l_oIt.value());
        }
        return l_strS + " }";
    }
    if (l_jJ.is_array()) {
        if (l_jJ.empty()) return "[]";
        std::string l_strS = "[";
        bool l_bFirst = true;
        for (const auto& l_jV : l_jJ) {
            if (!l_bFirst) l_strS += ", ";
            l_bFirst = false;
            l_strS += f_OneLine(l_jV);
        }
        return l_strS + "]";
    }
    return l_jJ.dump(-1, ' ', false);
}

int f_Utf8Length(const std::string& l_strS) {   // characters, not bytes (the width is what the eye sees)
    int l_iN = 0;
    for (unsigned char l_u8C : l_strS) l_iN += (l_u8C & 0xC0) != 0x80 ? 1 : 0;
    return l_iN;
}

void f_DumpTo(std::string& l_strOut, const ojson& l_jJ, int l_iDepth, int l_iInlineFrom, int l_iWidth) {
    const std::string l_strPad((size_t)l_iDepth * 2, ' ');
    if (!l_jJ.is_structured() || l_jJ.empty()) { l_strOut += f_OneLine(l_jJ); return; }
    if (l_iDepth >= l_iInlineFrom) {
        const std::string l_strOne = f_OneLine(l_jJ);
        if (f_Utf8Length(l_strOne) + (int)l_strPad.size() <= l_iWidth) { l_strOut += l_strOne; return; }
    }
    const std::string l_strInner((size_t)(l_iDepth + 1) * 2, ' ');
    if (l_jJ.is_object()) {
        l_strOut += "{\n";
        bool l_bFirst = true;
        for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
            if (!l_bFirst) l_strOut += ",\n";
            l_bFirst = false;
            l_strOut += l_strInner + ojson(l_oIt.key()).dump(-1, ' ', false) + ": ";
            f_DumpTo(l_strOut, l_oIt.value(), l_iDepth + 1, l_iInlineFrom, l_iWidth);
        }
        l_strOut += "\n" + l_strPad + "}";
    } else {
        l_strOut += "[\n";
        bool l_bFirst = true;
        for (const auto& l_jV : l_jJ) {
            if (!l_bFirst) l_strOut += ",\n";
            l_bFirst = false;
            l_strOut += l_strInner;
            f_DumpTo(l_strOut, l_jV, l_iDepth + 1, l_iInlineFrom, l_iWidth);
        }
        l_strOut += "\n" + l_strPad + "]";
    }
}

std::string f_Trim(const std::string& l_strS) {
    const size_t l_nA = l_strS.find_first_not_of(" \t\r\n");
    if (l_nA == std::string::npos) return std::string();
    return l_strS.substr(l_nA, l_strS.find_last_not_of(" \t\r\n") - l_nA + 1);
}

std::vector<std::string> f_SplitComma(const std::string& l_strText) {
    std::vector<std::string> l_vOut;
    std::stringstream l_oSs(l_strText);
    std::string l_strPart;
    while (std::getline(l_oSs, l_strPart, ',')) {
        l_strPart = f_Trim(l_strPart);
        if (!l_strPart.empty()) l_vOut.push_back(l_strPart);
    }
    return l_vOut;
}

} // namespace

std::string f_DumpCompact(const ojson& l_jJ, int l_iInlineFrom, int l_iWidth) {
    std::string l_strOut;
    f_DumpTo(l_strOut, l_jJ, 0, l_iInlineFrom, l_iWidth);
    return l_strOut + "\n";
}

bool f_ReplaceLevelPoints(std::string& l_strText, const ojson& l_jRows) {
    const size_t l_nLu = l_strText.find("\"levelUp\"");
    if (l_nLu == std::string::npos) return false;
    const size_t l_nKey = l_strText.find("\"points\"", l_nLu);
    if (l_nKey == std::string::npos) return false;
    const size_t l_nOpen = l_strText.find('[', l_nKey);
    if (l_nOpen == std::string::npos) return false;
    int l_iDepth = 0;
    size_t l_nClose = std::string::npos;
    for (size_t l_nI = l_nOpen; l_nI < l_strText.size(); l_nI++) {
        if (l_strText[l_nI] == '[') l_iDepth++;
        else if (l_strText[l_nI] == ']' && --l_iDepth == 0) { l_nClose = l_nI; break; }
    }
    if (l_nClose == std::string::npos) return false;
    // the rows one per line, indented as the "points" key is
    const size_t l_nLineStart = l_strText.rfind('\n', l_nKey) + 1;
    const std::string l_strPad = l_strText.substr(l_nLineStart, l_nKey - l_nLineStart);
    std::string l_strArr = "[\n";
    for (size_t l_nI = 0; l_nI < l_jRows.size(); l_nI++)
        l_strArr += l_strPad + "  " + f_OneLine(l_jRows[l_nI]) + (l_nI + 1 < l_jRows.size() ? ",\n" : "\n");
    l_strArr += l_strPad + "]";
    l_strText.replace(l_nOpen, l_nClose - l_nOpen + 1, l_strArr);
    return true;
}

ojson f_MovesFromText(const std::string& l_strText) {
    ojson l_jOut = ojson::array();
    for (const std::string& l_strPart : f_SplitComma(l_strText)) {
        const size_t l_nColon = l_strPart.find(':');
        if (l_nColon == std::string::npos) { l_jOut.push_back(l_strPart); continue; }
        ojson l_jM = ojson::object();
        l_jM["id"] = f_Trim(l_strPart.substr(0, l_nColon));
        l_jM["weight"] = std::max(0, std::atoi(l_strPart.substr(l_nColon + 1).c_str()));
        l_jOut.push_back(l_jM);
    }
    return l_jOut;
}

std::string f_MovesToText(const ojson& l_jMoves) {
    std::string l_strS;
    if (!l_jMoves.is_array()) return l_strS;
    for (const auto& l_jM : l_jMoves) {
        if (!l_strS.empty()) l_strS += ", ";
        if (l_jM.is_string()) l_strS += l_jM.get<std::string>();
        else if (l_jM.is_object()) l_strS += l_jM.value("id", std::string()) + ":" + std::to_string(l_jM.value("weight", 1));
    }
    return l_strS;
}

ojson f_IdsFromText(const std::string& l_strText) {
    ojson l_jOut = ojson::array();
    for (const std::string& l_strPart : f_SplitComma(l_strText)) l_jOut.push_back(l_strPart);
    return l_jOut;
}

std::string f_IdsToText(const ojson& l_jIds) {
    std::string l_strS;
    if (!l_jIds.is_array()) return l_strS;
    for (const auto& l_jM : l_jIds)
        if (l_jM.is_string()) l_strS += (l_strS.empty() ? "" : ", ") + l_jM.get<std::string>();
    return l_strS;
}

} // namespace bed

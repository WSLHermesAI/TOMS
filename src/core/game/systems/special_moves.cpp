// skills.cpp -- see special_moves.h.
#include "special_moves.h"

#include "gesture.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {

ShowBeat f_BeatFromJson(const nlohmann::json& l_jJ) {
    ShowBeat l_oB;
    l_oB.m_iAtMs = std::max(0, l_jJ.value("at", 0));
    l_oB.m_strWhat = l_jJ.value("do", std::string());
    l_oB.m_strFx = l_jJ.value("fx", std::string());
    l_oB.m_strCorner = l_jJ.value("corner", std::string());
    l_oB.m_strShape = l_jJ.value("shape", std::string());
    l_oB.m_strClip = l_jJ.value("clip", std::string());
    l_oB.m_strText = l_jJ.value("text", std::string());
    l_oB.m_strOn = l_jJ.value("on", std::string());
    l_oB.m_iMs = std::max(0, l_jJ.value("ms", 0));
    l_oB.m_iCount = std::max(0, l_jJ.value("count", 0));
    l_oB.m_iGapMs = std::max(0, l_jJ.value("gapMs", 100));
    l_oB.m_fValue = l_jJ.value("value", 0.0f);
    l_oB.m_fZoom = std::max(1.0f, l_jJ.value("zoom", 1.0f));
    return l_oB;
}

std::vector<ShowBeat> f_BeatsFromJson(const nlohmann::json& l_jJ, const char* l_pszKey) {
    std::vector<ShowBeat> l_vV;
    if (l_jJ.contains(l_pszKey) && l_jJ[l_pszKey].is_array())
        for (const auto& l_jB : l_jJ[l_pszKey]) if (l_jB.is_object()) l_vV.push_back(f_BeatFromJson(l_jB));
    std::stable_sort(l_vV.begin(), l_vV.end(), [](const ShowBeat& l_oA, const ShowBeat& l_oB) { return l_oA.m_iAtMs < l_oB.m_iAtMs; });
    return l_vV;
}

}  // namespace

const SpecialMove* SpecialMoveTable::f_Find(const std::string& l_strId) const {
    auto l_itIt = m_mapSkills.find(l_strId);
    return l_itIt == m_mapSkills.end() ? nullptr : &l_itIt->second;
}

const SpecialMove* SpecialMoveTable::f_ForWeapon(const std::string& l_strWeapon, const std::string& l_strUse) const {
    for (const auto& [l_strId, l_oS] : m_mapSkills)
        if (l_oS.m_strWeapon == l_strWeapon && l_oS.m_strUse == l_strUse) return &l_oS;
    return nullptr;
}

SpecialMoveTable f_SpecialMovesFromJson(const nlohmann::json& l_jJ) {
    SpecialMoveTable l_oT;
    if (!l_jJ.is_object()) return l_oT;
    for (auto l_oIt = l_jJ.begin(); l_oIt != l_jJ.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;
        const nlohmann::json& l_jE = l_oIt.value();
        SpecialMove l_oS;
        l_oS.m_strId = l_oIt.key();
        l_oS.m_strName = l_jE.value("name", std::string());
        l_oS.m_strWeapon = l_jE.value("weapon", std::string());
        l_oS.m_strUse = l_jE.value("use", l_oS.m_strUse);
        l_oS.m_strPrompt = l_jE.value("prompt", std::string());
        l_oS.m_fDamage = std::max(0.0f, l_jE.value("damage", l_oS.m_fDamage));
        if (l_jE.contains("play") && l_jE["play"].is_object()) {
            const nlohmann::json& l_jP = l_jE["play"];
            l_oS.m_oPlay.m_strKind = l_jP.value("kind", l_oS.m_oPlay.m_strKind);
            l_oS.m_oPlay.m_strShape = l_jP.value("shape", l_oS.m_oPlay.m_strShape);
            l_oS.m_oPlay.m_iPoints = std::max(1, l_jP.value("points", l_oS.m_oPlay.m_iPoints));
            l_oS.m_oPlay.m_iTimeMs = std::max(500, l_jP.value("timeMs", l_oS.m_oPlay.m_iTimeMs));
            l_oS.m_oPlay.m_fSize = std::max(40.0f, l_jP.value("size", l_oS.m_oPlay.m_fSize));
            l_oS.m_oPlay.m_fCornerRadius = std::max(8.0f, l_jP.value("cornerRadius", l_oS.m_oPlay.m_fCornerRadius));
            if (l_jP.contains("cues") && l_jP["cues"].is_array())
                for (const auto& l_jC : l_jP["cues"]) if (l_jC.is_string()) l_oS.m_oPlay.m_vCues.push_back(l_jC.get<std::string>());
        }
        if (l_oS.m_oPlay.m_strKind == "shape") {   // a shape's steps: its corners after the first (a pentagram: 5)
            const int l_iN = (int)f_ShapeCorners(l_oS.m_oPlay.m_strShape).size();
            if (l_iN > 1) l_oS.m_oPlay.m_iPoints = l_iN - 1;
        }
        if (l_oS.m_oPlay.m_strKind == "slice" && !l_oS.m_oPlay.m_vCues.empty()) l_oS.m_oPlay.m_iPoints = (int)l_oS.m_oPlay.m_vCues.size();
        l_oS.m_vDemo = f_BeatsFromJson(l_jE, "demo");
        l_oS.m_vStep = f_BeatsFromJson(l_jE, "step");
        l_oS.m_vFinish = f_BeatsFromJson(l_jE, "finish");
        l_oT.m_mapSkills[l_oS.m_strId] = std::move(l_oS);
    }
    return l_oT;
}

int f_ShowLength(const std::vector<ShowBeat>& l_vBeats) {
    int l_iEnd = 0;
    for (const ShowBeat& l_oB : l_vBeats) {
        int l_iLen = l_oB.m_iMs;
        if (l_oB.m_strWhat == "hits") l_iLen = std::max(l_iLen, l_oB.m_iCount * l_oB.m_iGapMs);
        l_iEnd = std::max(l_iEnd, l_oB.m_iAtMs + l_iLen);
    }
    return l_iEnd;
}

std::vector<const ShowBeat*> f_BeatsDue(const std::vector<ShowBeat>& l_vBeats, int l_iFromMs, int l_iToMs) {
    std::vector<const ShowBeat*> l_vV;
    for (const ShowBeat& l_oB : l_vBeats)
        if (l_oB.m_iAtMs > l_iFromMs && l_oB.m_iAtMs <= l_iToMs) l_vV.push_back(&l_oB);
    return l_vV;
}

std::string f_NamePart(const std::string& l_strName, int l_iDone, int l_iTotal) {
    std::vector<size_t> l_vStarts;   // each letter's first byte
    for (size_t l_nI = 0; l_nI < l_strName.size(); l_nI++)
        if (((unsigned char)l_strName[l_nI] & 0xC0) != 0x80) l_vStarts.push_back(l_nI);
    if (l_iTotal <= 0 || l_iDone >= l_iTotal) return l_strName;
    if (l_iDone <= 0) return std::string();
    const size_t l_nLetters = (size_t)std::lround((double)l_vStarts.size() * l_iDone / l_iTotal);
    return l_nLetters >= l_vStarts.size() ? l_strName : l_strName.substr(0, l_vStarts[l_nLetters]);
}

} // namespace toms

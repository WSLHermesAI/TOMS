// skills.cpp -- see special_moves.h.
#include "field_keys.h"    // toms::field: every data field by its constant
#include "special_moves.h"

#include "gesture.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {

ShowBeat f_BeatFromJson(const nlohmann::json& l_jJ) {
    ShowBeat l_oB;
    l_oB.m_iAtMs = std::max(0, l_jJ.value(toms::field::g_pszAt, 0));
    l_oB.m_strWhat = l_jJ.value(toms::field::g_pszDo, std::string());
    l_oB.m_strFx = l_jJ.value(toms::field::g_pszFx, std::string());
    l_oB.m_strCorner = l_jJ.value(toms::field::g_pszCorner, std::string());
    l_oB.m_strShape = l_jJ.value(toms::field::g_pszShape, std::string());
    l_oB.m_strClip = l_jJ.value(toms::field::g_pszClip, std::string());
    l_oB.m_strText = l_jJ.value(toms::field::g_pszText, std::string());
    l_oB.m_strOn = l_jJ.value(toms::field::g_pszOn, std::string());
    l_oB.m_iMs = std::max(0, l_jJ.value(toms::field::g_pszMs, 0));
    l_oB.m_iCount = std::max(0, l_jJ.value(toms::field::g_pszCount, 0));
    l_oB.m_iGapMs = std::max(0, l_jJ.value(toms::field::g_pszGapMs, 100));
    l_oB.m_fValue = l_jJ.value(toms::field::g_pszValue, 0.0f);
    l_oB.m_fZoom = std::max(1.0f, l_jJ.value(toms::field::g_pszZoom, 1.0f));
    l_oB.m_strMove = l_jJ.value(toms::field::g_pszMove, std::string());
    l_oB.m_fDeg = l_jJ.value(toms::field::g_pszDeg, 30.0f);
    l_oB.m_strWhen = l_jJ.value(toms::field::g_pszWhen, std::string());
    l_oB.m_fSpeed = std::clamp(l_jJ.value(toms::field::g_pszSpeed, 1.0f), 0.1f, 10.0f);
    l_oB.m_strProjectile = l_jJ.value(toms::field::g_pszProjectile, std::string());
    l_oB.m_strImpact = l_jJ.value(toms::field::g_pszImpact, std::string());
    l_oB.m_iFlightMs = std::max(1, l_jJ.value(toms::field::g_pszFlightMs, l_oB.m_iFlightMs));
    l_oB.m_iExtra = std::clamp(l_jJ.value(toms::field::g_pszExtra, 0), 0, 8);
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
        l_oS.m_strName = l_jE.value(toms::field::g_pszName, std::string());
        l_oS.m_strWeapon = l_jE.value(toms::field::g_pszWeapon, std::string());
        l_oS.m_strUse = l_jE.value(toms::field::g_pszUse, l_oS.m_strUse);
        l_oS.m_strPrompt = l_jE.value(toms::field::g_pszPrompt, std::string());
        l_oS.m_fDamage = std::max(0.0f, l_jE.value(toms::field::g_pszDamage, l_oS.m_fDamage));
        if (l_jE.contains(toms::field::g_pszPlay) && l_jE[toms::field::g_pszPlay].is_object()) {
            const nlohmann::json& l_jP = l_jE[toms::field::g_pszPlay];
            l_oS.m_oPlay.m_eType = f_PlayTypeFromString(l_jP.value(toms::field::g_pszKind, std::string("drawShape")));
            l_oS.m_oPlay.m_fHitDamage = std::max(0.0f, l_jP.value(toms::field::g_pszHitDamage, l_oS.m_oPlay.m_fHitDamage));
            l_oS.m_oPlay.m_strShape = l_jP.value(toms::field::g_pszShape, l_oS.m_oPlay.m_strShape);
            l_oS.m_oPlay.m_iPoints = std::max(1, l_jP.value(toms::field::g_pszPoints, l_oS.m_oPlay.m_iPoints));
            l_oS.m_oPlay.m_iTimeMs = std::max(500, l_jP.value(toms::field::g_pszTimeMs, l_oS.m_oPlay.m_iTimeMs));
            l_oS.m_oPlay.m_fSize = std::max(40.0f, l_jP.value(toms::field::g_pszSize, l_oS.m_oPlay.m_fSize));
            l_oS.m_oPlay.m_fCornerRadius = std::max(8.0f, l_jP.value(toms::field::g_pszCornerRadius, l_oS.m_oPlay.m_fCornerRadius));
            l_oS.m_oPlay.m_iMinHits = std::max(0, l_jP.value(toms::field::g_pszMinHits, 0));
            l_oS.m_oPlay.m_strGuideFx = l_jP.value(toms::field::g_pszGuideFx, std::string());
            l_oS.m_oPlay.m_iGuidePeriodMs = std::max(200, l_jP.value(toms::field::g_pszGuidePeriodMs, l_oS.m_oPlay.m_iGuidePeriodMs));
            l_oS.m_oPlay.m_fRingY = std::clamp(l_jP.value(toms::field::g_pszRingY, l_oS.m_oPlay.m_fRingY), -1.0f, 1.0f);
            l_oS.m_oPlay.m_strAround = l_jP.value(toms::field::g_pszAround, l_oS.m_oPlay.m_strAround);
            if (l_jP.contains(toms::field::g_pszCues) && l_jP[toms::field::g_pszCues].is_array())
                for (const auto& l_jC : l_jP[toms::field::g_pszCues]) if (l_jC.is_string()) l_oS.m_oPlay.m_vCues.push_back(l_jC.get<std::string>());
        }
        if (l_oS.m_oPlay.m_eType == PlayType::eDRAW_SHAPE) {   // a shape's steps: its corners after the first (a pentagram: 5)
            const int l_iN = (int)f_ShapeCorners(l_oS.m_oPlay.m_strShape).size();
            if (l_iN > 1) l_oS.m_oPlay.m_iPoints = l_iN - 1;
        }
        if ((f_IsSwipePlay(l_oS.m_oPlay.m_eType) || l_oS.m_oPlay.m_eType == PlayType::eSPIN_CIRCLE) && !l_oS.m_oPlay.m_vCues.empty()) l_oS.m_oPlay.m_iPoints = (int)l_oS.m_oPlay.m_vCues.size();
        l_oS.m_vDemo = f_BeatsFromJson(l_jE, toms::field::g_pszDemo);
        l_oS.m_vStep = f_BeatsFromJson(l_jE, toms::field::g_pszStep);
        l_oS.m_vFinish = f_BeatsFromJson(l_jE, toms::field::g_pszFinish);
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

bool f_BeatOn(const ShowBeat& l_oB, bool l_bFull, bool l_bLanded) {
    if (l_oB.m_strWhen.empty()) return true;
    if (l_oB.m_strWhen == "full") return l_bFull;
    if (l_oB.m_strWhen == "landed") return l_bLanded || l_bFull;
    return !l_bFull && l_bLanded;   // "partial": it landed, not in full (a fizzle shows only its miss word)
}

int f_ShowLength(const std::vector<ShowBeat>& l_vBeats, bool l_bFull, bool l_bLanded) {
    std::vector<ShowBeat> l_vOn;
    for (const ShowBeat& l_oB : l_vBeats) if (f_BeatOn(l_oB, l_bFull, l_bLanded)) l_vOn.push_back(l_oB);
    return f_ShowLength(l_vOn);
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

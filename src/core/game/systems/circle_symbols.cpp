// circle_symbols.cpp -- see circle_symbols.h.
#include "circle_symbols.h"

#include "field_keys.h"    // toms::field: every data field by its constant
#include "save_system.h"   // f_ReadJsonFileSafe

#include <algorithm>

namespace toms {

CircleSymbolTable f_CircleSymbolsFromJson(const nlohmann::json& l_jJ) {
    CircleSymbolTable l_oT;
    if (!l_jJ.is_object()) return l_oT;
    const nlohmann::json l_jS = l_jJ.value(toms::field::g_pszSymbols, nlohmann::json::object());
    for (auto l_oIt = l_jS.begin(); l_oIt != l_jS.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_object()) continue;   // "_comment"
        const nlohmann::json& l_jE = l_oIt.value();
        CircleSymbol l_oS;
        l_oS.m_strId = l_oIt.key();
        for (const auto& l_jP : l_jE.value(toms::field::g_pszPoints, nlohmann::json::array()))
            if (l_jP.is_array() && l_jP.size() == 2 && l_jP[0].is_number() && l_jP[1].is_number())
                l_oS.m_vPoints.push_back({std::clamp(l_jP[0].get<float>(), 0.0f, 1.0f), std::clamp(l_jP[1].get<float>(), 0.0f, 1.0f)});
        for (const auto& l_jT : l_jE.value(toms::field::g_pszTimes, nlohmann::json::array()))
            if (l_jT.is_number()) l_oS.m_vTimesMs.push_back(std::max(0, l_jT.get<int>()));
        if (l_oS.m_vTimesMs.size() != l_oS.m_vPoints.size()) l_oS.m_vTimesMs.clear();   // one a point, or none
        l_oS.m_bRotate = l_jE.value(toms::field::g_pszRotate, false);
        l_oS.m_bReverse = l_jE.value(toms::field::g_pszReverse, false);
        l_oS.m_bMirror = l_jE.value(toms::field::g_pszMirror, false);
        if (!l_oS.m_vPoints.empty()) l_oT.m_mapSymbols[l_oS.m_strId] = l_oS;
    }
    const nlohmann::json l_jW = l_jJ.value(toms::field::g_pszWeapons, nlohmann::json::object());
    for (auto l_oIt = l_jW.begin(); l_oIt != l_jW.end(); ++l_oIt) {
        if (l_oIt.key().empty() || l_oIt.key()[0] == '_' || !l_oIt.value().is_array()) continue;
        std::vector<CircleSymbolRow>& l_vRows = l_oT.m_mapWeapons[l_oIt.key()];
        for (const auto& l_jR : l_oIt.value()) {
            if (!l_jR.is_object() || !l_jR.contains(toms::field::g_pszLv)) continue;   // (a row of only "_comment")
            CircleSymbolRow l_oR;
            l_oR.m_iLv = std::max(1, l_jR.value(toms::field::g_pszLv, 1));
            if (l_jR.contains(toms::field::g_pszChance)) l_oR.m_fChance = std::clamp(l_jR.value(toms::field::g_pszChance, 0.0f), 0.0f, 100.0f);
            const nlohmann::json l_jWt = l_jR.value(toms::field::g_pszSymbols, nlohmann::json::object());
            for (auto l_oW = l_jWt.begin(); l_oW != l_jWt.end(); ++l_oW)
                if (l_oW.value().is_number()) l_oR.m_mapWeights[l_oW.key()] = std::max(0.0f, l_oW.value().get<float>());
            l_vRows.push_back(l_oR);
        }
        std::sort(l_vRows.begin(), l_vRows.end(), [](const auto& l_oA, const auto& l_oB) { return l_oA.m_iLv < l_oB.m_iLv; });
    }
    return l_oT;
}

CircleSymbolTable f_LoadCircleSymbols(const std::string& l_strPath) { return f_CircleSymbolsFromJson(f_ReadJsonFileSafe(l_strPath)); }

CircleSymbolRow CircleSymbolTable::f_At(const std::string& l_strWeapon, int l_iLv) const {
    CircleSymbolRow l_oOut;
    l_oOut.m_fChance = 0.0f;
    const auto l_itW = m_mapWeapons.find(l_strWeapon);
    if (l_itW == m_mapWeapons.end()) return l_oOut;
    for (const CircleSymbolRow& l_oR : l_itW->second) {
        if (l_oR.m_iLv > l_iLv) break;
        l_oOut.m_iLv = l_oR.m_iLv;
        if (l_oR.m_fChance >= 0.0f) l_oOut.m_fChance = l_oR.m_fChance;
        if (!l_oR.m_mapWeights.empty()) l_oOut.m_mapWeights = l_oR.m_mapWeights;
    }
    return l_oOut;
}

std::vector<glm::vec2> CircleSymbolTable::f_Pick(const std::string& l_strWeapon, int l_iLv, int l_iPoints, std::mt19937& l_oRng,
                                                 std::string* l_pId, std::vector<int>* l_pTimesMs) const {
    if (l_pTimesMs) l_pTimesMs->clear();
    std::uniform_real_distribution<float> l_oU(0.0f, 1.0f);
    const CircleSymbolRow l_oRow = f_At(l_strWeapon, l_iLv);
    if (l_iPoints <= 0 || l_oU(l_oRng) * 100.0f >= l_oRow.m_fChance) return {};
    // those with exactly that many points; none: those with more (their first l_iPoints)
    std::vector<std::pair<const CircleSymbol*, float>> l_vCand;
    for (int l_iPass = 0; l_iPass < 2 && l_vCand.empty(); l_iPass++)
        for (const auto& [l_strId, l_fW] : l_oRow.m_mapWeights) {
            const auto l_itS = m_mapSymbols.find(l_strId);
            if (l_fW <= 0.0f || l_itS == m_mapSymbols.end()) continue;
            const int l_iN = (int)l_itS->second.m_vPoints.size();
            if (l_iPass == 0 ? l_iN == l_iPoints : l_iN > l_iPoints) l_vCand.push_back({&l_itS->second, l_fW});
        }
    if (l_vCand.empty()) return {};
    float l_fTotal = 0.0f;
    for (const auto& l_pairC : l_vCand) l_fTotal += l_pairC.second;
    float l_fAt = l_oU(l_oRng) * l_fTotal;
    const CircleSymbol* l_pS = l_vCand.back().first;
    for (const auto& l_pairC : l_vCand) {
        if (l_fAt < l_pairC.second) { l_pS = l_pairC.first; break; }
        l_fAt -= l_pairC.second;
    }
    std::vector<glm::vec2> l_vP = l_pS->m_vPoints;
    if (l_pS->m_bRotate) std::rotate(l_vP.begin(), l_vP.begin() + std::uniform_int_distribution<int>(0, (int)l_vP.size() - 1)(l_oRng), l_vP.end());
    if (l_pS->m_bReverse && l_oU(l_oRng) < 0.5f) std::reverse(l_vP.begin(), l_vP.end());
    if (l_pS->m_bMirror && l_oU(l_oRng) < 0.5f) for (glm::vec2& l_v2P : l_vP) l_v2P.x = 1.0f - l_v2P.x;
    l_vP.resize((size_t)l_iPoints);
    if (l_pId) *l_pId = l_pS->m_strId;
    if (l_pTimesMs && !l_pS->m_vTimesMs.empty())   // the k-th lit at the k-th time: the rhythm stays, whichever way it runs
        l_pTimesMs->assign(l_pS->m_vTimesMs.begin(), l_pS->m_vTimesMs.begin() + l_iPoints);
    return l_vP;
}

}  // namespace toms

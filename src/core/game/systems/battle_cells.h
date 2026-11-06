// battle_cells.h -- the trial of moving in battle (docs/gameplay/19 §2.1): the hero stands on one of six cells, and a monster's
// move may name a danger shape (moves.json "shape"). Before the hit, the cells in danger light up; a hero standing
// clear when it lands takes nothing. The shapes are data (battle.json "cells").
//
//     0 1 2    the row near the monster
//     3 4 5    the row far from it
//
// Pure: no Game dependency.
#pragma once
#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace toms {

constexpr int g_iCellCols = 3, g_iCellRows = 2;

struct CellRules {
    int m_iWarnMs = 1400;   // the cells light up this long before the hit
    int m_iStart = 4;       // where the hero starts
    float m_fShiftX = 55.0f, m_fShiftNear = 40.0f, m_fShiftY = 18.0f;   // how far the hero's picture moves for a cell
    std::map<std::string, std::vector<std::vector<int>>> m_mapShapes;   // a shape: its variants, each the cells it covers
};

inline CellRules f_CellRulesFromJson(const nlohmann::json& l_jBattle) {
    CellRules l_oR;
    if (!l_jBattle.is_object() || !l_jBattle.contains("cells") || !l_jBattle["cells"].is_object()) return l_oR;
    const nlohmann::json& l_jC = l_jBattle["cells"];
    l_oR.m_iWarnMs = std::max(100, l_jC.value("warnMs", l_oR.m_iWarnMs));
    l_oR.m_iStart = std::clamp(l_jC.value("start", l_oR.m_iStart), 0, g_iCellCols * g_iCellRows - 1);
    l_oR.m_fShiftX = l_jC.value("shiftX", l_oR.m_fShiftX);
    l_oR.m_fShiftNear = l_jC.value("shiftNear", l_oR.m_fShiftNear);
    l_oR.m_fShiftY = l_jC.value("shiftY", l_oR.m_fShiftY);
    if (l_jC.contains("shapes") && l_jC["shapes"].is_object())
        for (auto l_oIt = l_jC["shapes"].begin(); l_oIt != l_jC["shapes"].end(); ++l_oIt)
            if (l_oIt.value().is_array())
                for (const auto& l_jV : l_oIt.value()) {
                    std::vector<int> l_vCells;
                    if (l_jV.is_array()) for (const auto& l_jN : l_jV) if (l_jN.is_number_integer()) l_vCells.push_back(l_jN.get<int>());
                    l_oR.m_mapShapes[l_oIt.key()].push_back(l_vCells);
                }
    return l_oR;
}

// Which cells a move covers: a variant reaching the hero if there is one (roll 0..1 picks among them), so a threat is
// always real. Unknown shape: none.
inline uint32_t f_PickDanger(const CellRules& l_oR, const std::string& l_strShape, int l_iHeroCell, float l_fRoll) {
    auto l_itIt = l_oR.m_mapShapes.find(l_strShape);
    if (l_itIt == l_oR.m_mapShapes.end() || l_itIt->second.empty()) return 0u;
    std::vector<const std::vector<int>*> l_vReach;
    for (const auto& l_vV : l_itIt->second)
        if (std::find(l_vV.begin(), l_vV.end(), l_iHeroCell) != l_vV.end()) l_vReach.push_back(&l_vV);
    if (l_vReach.empty()) for (const auto& l_vV : l_itIt->second) l_vReach.push_back(&l_vV);
    const size_t l_nPick = std::min(l_vReach.size() - 1, (size_t)(std::clamp(l_fRoll, 0.0f, 1.0f) * (float)l_vReach.size()));
    uint32_t l_uMask = 0u;
    for (int l_iCell : *l_vReach[l_nPick]) if (l_iCell >= 0 && l_iCell < 32) l_uMask |= 1u << l_iCell;
    return l_uMask;
}

// One step on the grid, stopping at its edges.
inline int f_CellStep(int l_iCell, int l_iDx, int l_iDy) {
    const int l_iCol = std::clamp(l_iCell % g_iCellCols + l_iDx, 0, g_iCellCols - 1);
    const int l_iRow = std::clamp(l_iCell / g_iCellCols + l_iDy, 0, g_iCellRows - 1);
    return l_iRow * g_iCellCols + l_iCol;
}

} // namespace toms

// battle_cells.h -- moving in battle (docs/gameplay/19 §2.1): the hero stands on a cell of a grid, and a monster's move may
// name a danger shape (moves.json "shape"). Before the hit, the cells in danger light up; a hero standing clear when it
// lands takes nothing. The grid and the shapes are data (battle.json "cells"; a monster's "cellGrid": [cols, rows]).
//
//     0 1 2    a cell's number: row * cols + col; row 0 is the row near the monster
//     3 4 5
//
// A shape is a list of variants, and a variant fits any grid:
//     {"row": 0}  a row (from the end when negative)      {"rows": "each"}  every row, one variant each
//     {"col": 1}  a column (from the end when negative)   {"cols": "each"}  every column, one variant each
//     {"allBut": "start"}  everything but the hero's starting cell
//     [0, 1, 2]   these cell numbers
//
// Pure: no Game dependency.
#pragma once
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
#include <json.hpp>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace toms {

constexpr int g_iCellMaxCount = 32;   // the danger is a 32-bit mask

struct CellRules {
    int m_iWarnMs = 1400;   // the cells light up this long before the hit
    int m_iCols = 3, m_iRows = 2;   // the grid; a monster's "cellGrid" replaces it for its fight
    int m_iStart = -1;      // where the hero starts (-1: the far row's middle, on any grid)
    float m_fAreaW = 450.0f, m_fRowH = 58.0f, m_fGap = 6.0f, m_fMaxH = 190.0f;   // on screen: the grid's width; a row's height, less when the rows would pass maxH
    float m_fShiftX = 55.0f, m_fShiftNear = 40.0f, m_fShiftY = 18.0f;   // how far the hero's picture moves for a cell
    // the cells show only while a monster's special attack is coming (its danger lit; the owner, 2026-11-25): after the
    // last danger goes out they stay this long, then shrink away (popMs); the next danger pops them up again
    int m_iHideAfterMs = 700;
    // the first danger of a fight stops everything and shows the monster (battle.json cells.intro)
    int m_iIntroMs = 2200, m_iIntroUiMs = 1200, m_iPopMs = 350;   // how long; when the cells pop up; how long the pop takes
    float m_fIntroZoom = 1.25f;
    float m_fPerspective = 0.14f;   // how much narrower the near row is than the far one
    std::string m_strIntroClip = toms::state::g_pszSpecial, m_strIntroFx, m_strIntroText;
    std::map<std::string, nlohmann::json> m_mapShapes;   // a shape: its variants, each the cells it covers
};

inline CellRules f_CellRulesFromJson(const nlohmann::json& l_jBattle) {
    CellRules l_oR;
    if (!l_jBattle.is_object() || !l_jBattle.contains(toms::field::g_pszCells) || !l_jBattle[toms::field::g_pszCells].is_object()) return l_oR;
    const nlohmann::json& l_jC = l_jBattle[toms::field::g_pszCells];
    l_oR.m_iWarnMs = std::max(100, l_jC.value(toms::field::g_pszWarnMs, l_oR.m_iWarnMs));
    l_oR.m_iCols = std::clamp(l_jC.value(toms::field::g_pszCols, l_oR.m_iCols), 1, g_iCellMaxCount);
    l_oR.m_iRows = std::clamp(l_jC.value(toms::field::g_pszRows, l_oR.m_iRows), 1, g_iCellMaxCount / l_oR.m_iCols);
    l_oR.m_iStart = l_jC.value(toms::field::g_pszStart, l_oR.m_iStart);
    l_oR.m_fAreaW = l_jC.value(toms::field::g_pszAreaW, l_oR.m_fAreaW);
    l_oR.m_fRowH = l_jC.value(toms::field::g_pszRowH, l_oR.m_fRowH);
    l_oR.m_fGap = l_jC.value(toms::field::g_pszGap, l_oR.m_fGap);
    l_oR.m_fMaxH = l_jC.value(toms::field::g_pszMaxH, l_oR.m_fMaxH);
    l_oR.m_fPerspective = std::clamp(l_jC.value(toms::field::g_pszPerspective, l_oR.m_fPerspective), 0.0f, 0.6f);
    l_oR.m_fShiftX = l_jC.value(toms::field::g_pszShiftX, l_oR.m_fShiftX);
    l_oR.m_fShiftNear = l_jC.value(toms::field::g_pszShiftNear, l_oR.m_fShiftNear);
    l_oR.m_fShiftY = l_jC.value(toms::field::g_pszShiftY, l_oR.m_fShiftY);
    l_oR.m_iHideAfterMs = std::max(0, l_jC.value(toms::field::g_pszHideAfterMs, l_oR.m_iHideAfterMs));
    if (l_jC.contains(toms::field::g_pszIntro) && l_jC[toms::field::g_pszIntro].is_object()) {
        const nlohmann::json& l_jI = l_jC[toms::field::g_pszIntro];
        l_oR.m_iIntroMs = std::max(0, l_jI.value(toms::field::g_pszMs, l_oR.m_iIntroMs));
        l_oR.m_iIntroUiMs = std::clamp(l_jI.value(toms::field::g_pszUiAtMs, l_oR.m_iIntroUiMs), 0, l_oR.m_iIntroMs);
        l_oR.m_iPopMs = std::max(1, l_jI.value(toms::field::g_pszPopMs, l_oR.m_iPopMs));
        l_oR.m_fIntroZoom = l_jI.value(toms::field::g_pszZoom, l_oR.m_fIntroZoom);
        l_oR.m_strIntroClip = l_jI.value(toms::field::g_pszClip, l_oR.m_strIntroClip);
        l_oR.m_strIntroFx = l_jI.value(toms::field::g_pszFx, l_oR.m_strIntroFx);
        l_oR.m_strIntroText = l_jI.value(toms::field::g_pszText, l_oR.m_strIntroText);
    }
    if (l_jC.contains(toms::field::g_pszShapes) && l_jC[toms::field::g_pszShapes].is_object())
        for (auto l_oIt = l_jC[toms::field::g_pszShapes].begin(); l_oIt != l_jC[toms::field::g_pszShapes].end(); ++l_oIt)
            if (l_oIt.value().is_array()) l_oR.m_mapShapes[l_oIt.key()] = l_oIt.value();
    return l_oR;
}

// Where the hero starts on this grid: the data's cell if it fits, else the far row's middle.
inline int f_CellStart(const CellRules& l_oR, int l_iCols, int l_iRows) {
    if (l_oR.m_iStart >= 0 && l_oR.m_iStart < l_iCols * l_iRows) return l_oR.m_iStart;
    return (l_iRows - 1) * l_iCols + l_iCols / 2;
}

// A shape's variants on this grid, as cell numbers (the forms: the top of this file). Cells off the grid are dropped.
inline std::vector<std::vector<int>> f_ShapeVariants(const CellRules& l_oR, const std::string& l_strShape, int l_iCols, int l_iRows) {
    std::vector<std::vector<int>> l_vOut;
    auto l_itIt = l_oR.m_mapShapes.find(l_strShape);
    if (l_itIt == l_oR.m_mapShapes.end()) return l_vOut;
    const int l_iCount = l_iCols * l_iRows;
    const auto l_fnRow = [&](int l_iRow) { std::vector<int> l_vV; for (int l_iC = 0; l_iC < l_iCols; l_iC++) l_vV.push_back(l_iRow * l_iCols + l_iC); return l_vV; };
    const auto l_fnCol = [&](int l_iCol) { std::vector<int> l_vV; for (int l_iR = 0; l_iR < l_iRows; l_iR++) l_vV.push_back(l_iR * l_iCols + l_iCol); return l_vV; };
    for (const nlohmann::json& l_jV : l_itIt->second) {
        if (l_jV.is_array()) {
            std::vector<int> l_vV;
            for (const nlohmann::json& l_jN : l_jV) if (l_jN.is_number_integer() && l_jN.get<int>() >= 0 && l_jN.get<int>() < l_iCount) l_vV.push_back(l_jN.get<int>());
            if (!l_vV.empty()) l_vOut.push_back(l_vV);
        } else if (l_jV.is_object()) {
            if (l_jV.contains(toms::field::g_pszRow) && l_jV[toms::field::g_pszRow].is_number_integer()) {
                const int l_iR = l_jV[toms::field::g_pszRow].get<int>() < 0 ? l_iRows + l_jV[toms::field::g_pszRow].get<int>() : l_jV[toms::field::g_pszRow].get<int>();
                if (l_iR >= 0 && l_iR < l_iRows) l_vOut.push_back(l_fnRow(l_iR));
            }
            if (l_jV.contains(toms::field::g_pszCol) && l_jV[toms::field::g_pszCol].is_number_integer()) {
                const int l_iC = l_jV[toms::field::g_pszCol].get<int>() < 0 ? l_iCols + l_jV[toms::field::g_pszCol].get<int>() : l_jV[toms::field::g_pszCol].get<int>();
                if (l_iC >= 0 && l_iC < l_iCols) l_vOut.push_back(l_fnCol(l_iC));
            }
            if (l_jV.value(toms::field::g_pszRows, std::string()) == "each") for (int l_iR = 0; l_iR < l_iRows; l_iR++) l_vOut.push_back(l_fnRow(l_iR));
            if (l_jV.value(toms::field::g_pszCols, std::string()) == "each") for (int l_iC = 0; l_iC < l_iCols; l_iC++) l_vOut.push_back(l_fnCol(l_iC));
            if (l_jV.value(toms::field::g_pszAllBut, std::string()) == toms::place::g_pszStart) {
                std::vector<int> l_vV;
                const int l_iSafe = f_CellStart(l_oR, l_iCols, l_iRows);
                for (int l_iN = 0; l_iN < l_iCount; l_iN++) if (l_iN != l_iSafe) l_vV.push_back(l_iN);
                if (!l_vV.empty()) l_vOut.push_back(l_vV);
            }
        }
    }
    return l_vOut;
}

// Which cells a move covers: a variant reaching the hero if there is one (roll 0..1 picks among them), so a threat is
// always real. Unknown shape: none.
inline uint32_t f_PickDanger(const CellRules& l_oR, const std::string& l_strShape, int l_iHeroCell, float l_fRoll, int l_iCols = 0, int l_iRows = 0) {
    if (l_iCols <= 0 || l_iRows <= 0) { l_iCols = l_oR.m_iCols; l_iRows = l_oR.m_iRows; }
    const std::vector<std::vector<int>> l_vVariants = f_ShapeVariants(l_oR, l_strShape, l_iCols, l_iRows);
    if (l_vVariants.empty()) return 0u;
    std::vector<const std::vector<int>*> l_vReach;
    for (const auto& l_vV : l_vVariants)
        if (std::find(l_vV.begin(), l_vV.end(), l_iHeroCell) != l_vV.end()) l_vReach.push_back(&l_vV);
    if (l_vReach.empty()) for (const auto& l_vV : l_vVariants) l_vReach.push_back(&l_vV);
    const size_t l_nPick = std::min(l_vReach.size() - 1, (size_t)(std::clamp(l_fRoll, 0.0f, 1.0f) * (float)l_vReach.size()));
    uint32_t l_uMask = 0u;
    for (int l_iCell : *l_vReach[l_nPick]) if (l_iCell >= 0 && l_iCell < g_iCellMaxCount) l_uMask |= 1u << l_iCell;
    return l_uMask;
}

// One step on the grid, stopping at its edges.
inline int f_CellStep(int l_iCell, int l_iDx, int l_iDy, int l_iCols = 3, int l_iRows = 2) {
    const int l_iCol = std::clamp(l_iCell % l_iCols + l_iDx, 0, l_iCols - 1);
    const int l_iRow = std::clamp(l_iCell / l_iCols + l_iDy, 0, l_iRows - 1);
    return l_iRow * l_iCols + l_iCol;
}

} // namespace toms

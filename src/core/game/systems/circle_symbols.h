// circle_symbols.h -- the staff's circles laid out as a symbol (data/circle_symbols.json; the owner, 2026-11-27: "a json
// file to keep special symbol position for different level ... so I can add any symbol and its chance without
// rewriting the code"; docs/gameplay/22 §5.1).
//
// "times" (ms, one a point, the first 0): when each circle lights up as they are shown, so a symbol has its own rhythm;
// without, they light battle.json "plays" circleCountMs apart. "curve" is the battle editor's: the curve the circles
// were sampled from (its control points, times and LOD), kept so it can be edited again; the game does not read it.
// A symbol is the circles' places in tap order, each 0..1 across and down the play's area (TouchPlay places them, its
// circles' radius inside the area's edge). It may be turned (`rotate`: start at any of its points, keep going round),
// run backwards (`reverse`) or flipped left to right (`mirror`), each half the time. Each weapon has rows by level: the
// chance a play shows a symbol at all (else its circles go anywhere, apart) and the symbols it may show with their
// weights. The circles are always numbered 1, 2, 3 in their order; a symbol may itself be a letter (A5, B6, C5 ...). A row sets only what it has; a level takes the rows up to it.
#pragma once
#include <glm/glm.hpp>
#include <json.hpp>

#include <map>
#include <random>
#include <string>
#include <vector>

namespace toms {

struct CircleSymbol {
    std::string m_strId;
    std::vector<glm::vec2> m_vPoints;   // 0..1 in the area, in tap order
    std::vector<int> m_vTimesMs;        // "times": when each lights up as they are shown, ms from the first (empty: evenly, battle.json circleCountMs)
    bool m_bRotate = false, m_bReverse = false, m_bMirror = false;
};

struct CircleSymbolRow {
    int m_iLv = 1;
    float m_fChance = -1.0f;                        // %; -1: unset (a level with none set: 0)
    std::map<std::string, float> m_mapWeights;      // empty: unset
};

struct CircleSymbolTable {
    std::map<std::string, CircleSymbol> m_mapSymbols;
    std::map<std::string, std::vector<CircleSymbolRow>> m_mapWeapons;   // each sorted by lv

    CircleSymbolRow f_At(const std::string& l_strWeapon, int l_iLv) const;
    // A play of l_iPoints circles: the chance rolled, then a symbol by weight among those with exactly that many points
    // (else those with more: their first l_iPoints), turned / reversed / mirrored. Empty: no symbol (anywhere).
    // l_pTimesMs: the picked symbol's times for those circles (empty: evenly).
    std::vector<glm::vec2> f_Pick(const std::string& l_strWeapon, int l_iLv, int l_iPoints, std::mt19937& l_oRng,
                                  std::string* l_pId = nullptr, std::vector<int>* l_pTimesMs = nullptr) const;
};

CircleSymbolTable f_CircleSymbolsFromJson(const nlohmann::json& l_jJ);
CircleSymbolTable f_LoadCircleSymbols(const std::string& l_strPath);   // a missing or broken file: an empty table

}  // namespace toms

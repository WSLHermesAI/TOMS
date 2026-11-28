// tower_run.h -- the tower in climbs (the owner, 2026-11-28; docs/gameplay/27_ATTACK_MOMENT_AND_TOWER_RUN.md §3):
// the stamina a fight costs and its real-time clock, the stories and their chapters on the main screen, and a climb's
// score and rank. Pure logic: no Game, so it is unit-tested (tower_run_test.cpp). data/tower_run.json.
#pragma once
#include <json.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace toms {

// Stamina (體力, Q137, the owner): each fight costs perFight; one point comes back every everySec, also while the game is
// closed (the clock is real time); full: no clock.
struct StaminaRules {
    int m_iMax = 60;
    int m_iEverySec = 300;
    int m_iPerFight = 1;
};

// What the stamina file keeps (the owner: "a file with how much stamina now and the next recovery time"): the points now
// and when the next comes (seconds since 1970; 0: full, no timer).
struct StaminaClock {
    int m_iNow = 60;
    long long m_llNextAt = 0;
    // The time passed (since the last look, or while the game was closed) brings its points; the next timer after them.
    // True: something changed (write the file).
    bool f_Catch(const StaminaRules& l_oR, long long l_llNow);
    // Pay for a fight: false (and nothing paid) when there is not enough. Spending from full starts the timer.
    bool f_Spend(const StaminaRules& l_oR, long long l_llNow, int l_iCost);
    int f_SecondsToNext(long long l_llNow) const { return m_llNextAt <= 0 ? 0 : (int)std::max(0LL, m_llNextAt - l_llNow); }
};
nlohmann::json f_StaminaToJson(const StaminaClock& l_oC);
StaminaClock f_StaminaFromJson(const nlohmann::json& l_jJ, const StaminaRules& l_oR);   // a missing file: full

// One chapter on the main screen: a story chapter (story/chapters/<id>.json) or a challenge on its floors.
struct TowerChapter {
    std::string m_strId;                 // "ch_01", or a challenge's own id ("c_01")
    std::string m_strFloorsOf;           // a challenge: the chapter whose floors it climbs ("ch_01"); a story chapter: itself
    std::string m_strAfter;              // opens when this chapter is cleared ("": the first)
    std::string m_strTitle;              // a text key ("": the chapter file's)
    float m_fHpX = 1.0f, m_fAtkX = 1.0f, m_fGoldX = 1.0f;   // a challenge's monsters: stronger, and they pay more
    int m_iCrystals = 0;                 // a challenge cleared: crystals on top of the rank's
};
struct TowerStory {
    std::string m_strId;                 // "orphan", "challenge"
    std::string m_strTitle;              // a text key
    bool m_bChallenge = false;           // its chapters are challenges (no story)
    std::vector<TowerChapter> m_vChapters;
};

// A climb's numbers for its score (Q139).
struct ClimbStats {
    int m_iFloors = 0, m_iMonsters = 0, m_iBestCombo = 0, m_iPerfects = 0, m_iDamageTaken = 0, m_iSeconds = 0;
};
struct RankRule {
    std::string m_strRank;               // "S", "A", ...
    int m_iMinScore = 0;
    int m_iCrystals = 0;                 // what the rank gives (Q139)
};

struct TowerRunRules {
    bool m_bOn = true;                   // the main screen after the title, climbs by chapter ("off": the old one long run)
    StaminaRules m_oStamina;
    float m_fKeepGoldOnDeath = 0.5f;     // a death: this share of the climb's gold and crystals is kept (Q134, Q143)
    float m_fKeepCrystalsOnDeath = 0.5f;
    // the score: each floor, monster, combo step, perfect (dodge or parry) adds; each damage point and second takes
    int m_iPerFloor = 100, m_iPerMonster = 10, m_iPerCombo = 5, m_iPerPerfect = 20;
    float m_fPerDamage = 0.1f, m_fPerSecond = 0.05f;
    std::vector<RankRule> m_vRanks;      // best first
    std::vector<TowerStory> m_vStories;
};
TowerRunRules f_TowerRunFromJson(const nlohmann::json& l_jJ);

int f_ClimbScore(const ClimbStats& l_oS, const TowerRunRules& l_oR);
const RankRule* f_RankFor(int l_iScore, const TowerRunRules& l_oR);   // null: no ranks
const TowerChapter* f_FindChapter(const TowerRunRules& l_oR, const std::string& l_strId, const TowerStory** l_ppStory = nullptr);

}  // namespace toms

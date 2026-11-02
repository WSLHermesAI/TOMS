// battle_party.h -- a battle's monsters (docs/gameplay/13_BATTLE_EDITOR.md §4.6; the owner 2026-10-25: one to three
// monsters in a battle, now): which one the player's attacks go to, and the battle's event log (what happened, when,
// to whom), which the battle editor shows and the monster events (§4.4) will listen to.
//
// Pure logic, no Game dependency (systems/*).
#pragma once
#include <string>
#include <vector>

namespace toms {

constexpr int g_iMaxFoes = 3;   // monsters in one battle

// The foe the target moves to: from `from`, `dir` steps (+1 next, -1 previous; 0 = stay), skipping the fallen, wrapping
// around. `hp` is each foe's HP (<= 0: fallen). -1 when every foe has fallen.
int f_NextStanding(const std::vector<int>& l_vHp, int l_iFrom, int l_iDir);
bool f_AllDown(const std::vector<int>& l_vHp);

// One thing that happened in a battle. `who` / `whom`: -1 the player, 0.. a foe (by its place in the battle).
struct BattleEvent {
    int m_iMs = 0;               // battle time
    std::string m_strType;         // start | combo | hit | guard | down | target | super | active | win | lose
    int m_iWho = -1, m_iWhom = -1;
    int m_iValue = 0;            // the damage (hit), the guard's power (guard), the points matched (combo)
    std::string m_strNote;         // free text for the log ("3/4", the monster's id, ...)
};

// The battle's log as text, one line an event: "  2.40  player -> foe 1 (bat): hit 12".
std::string f_BattleEventLine(const BattleEvent& l_oE, const std::vector<std::string>& l_vFoeIds);

} // namespace toms

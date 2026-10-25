// battle_party.h -- a battle's monsters (docs/gameplay/13_BATTLE_EDITOR.md §4.6; the owner 2026-10-25: one to three
// monsters in a battle, now): which one the player's attacks go to, and the battle's event log (what happened, when,
// to whom), which the battle editor shows and the monster events (§4.4) will listen to.
//
// Pure logic, no Game dependency (systems/*).
#pragma once
#include <string>
#include <vector>

namespace toms {

constexpr int kMaxFoes = 3;   // monsters in one battle

// The foe the target moves to: from `from`, `dir` steps (+1 next, -1 previous; 0 = stay), skipping the fallen, wrapping
// around. `hp` is each foe's HP (<= 0: fallen). -1 when every foe has fallen.
int nextStanding(const std::vector<int>& hp, int from, int dir);
bool allDown(const std::vector<int>& hp);

// One thing that happened in a battle. `who` / `whom`: -1 the player, 0.. a foe (by its place in the battle).
struct BattleEvent {
    int ms = 0;               // battle time
    std::string type;         // start | combo | hit | guard | down | target | super | active | win | lose
    int who = -1, whom = -1;
    int value = 0;            // the damage (hit), the guard's power (guard), the points matched (combo)
    std::string note;         // free text for the log ("3/4", the monster's id, ...)
};

// The battle's log as text, one line an event: "  2.40  player -> foe 1 (bat): hit 12".
std::string battleEventLine(const BattleEvent& e, const std::vector<std::string>& foeIds);

} // namespace toms

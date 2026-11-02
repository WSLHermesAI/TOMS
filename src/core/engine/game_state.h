// game_state.h — the master Game State Machine.
// See docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §4 and
// docs/architecture/IMPLEMENTATION_ROADMAP.md Milestone 1. Names every screen/mode the game can be in and
// the legal transitions between them. Pure data + validation; owns no gameplay state itself —
// Game::currentState() (src/game/game.cpp) derives the *current* GameState from the game's real
// flags for observability, but does not yet route control flow through tryTransition() for the
// states that have no real screen behind them yet (StageSelect, Paused, etc. — later milestones).
#pragma once
#include <utility>
#include <vector>

namespace toms {

enum class GameState {
    Boot = 0,
    MainMenu,
    Settings,
    StageSelect,
    StageLoading,
    Explore,
    EncounterResolve,
    DirectBattle,
    Dialogue,
    Merchant,
    Paused,
    StageComplete,
    Ending,
    Credits,
    Count   // sentinel — not a real state; used to size iteration in tests
};

inline const char* f_ToString(GameState l_eS) {
    switch (l_eS) {
        case GameState::Boot:             return "Boot";
        case GameState::MainMenu:         return "MainMenu";
        case GameState::Settings:         return "Settings";
        case GameState::StageSelect:      return "StageSelect";
        case GameState::StageLoading:     return "StageLoading";
        case GameState::Explore:          return "Explore";
        case GameState::EncounterResolve: return "EncounterResolve";
        case GameState::DirectBattle:     return "DirectBattle";
        case GameState::Dialogue:         return "Dialogue";
        case GameState::Merchant:         return "Merchant";
        case GameState::Paused:           return "Paused";
        case GameState::StageComplete:    return "StageComplete";
        case GameState::Ending:           return "Ending";
        case GameState::Credits:          return "Credits";
        default:                          return "?";
    }
}

// The legal transition table, matching the flow diagram in
// docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §4.2. Every state appears at least once as a
// "from" (no dead ends) and every state is reachable from Boot (no unreachable states) —
// both invariants are asserted by state_machine_test rather than just assumed here.
inline const std::vector<std::pair<GameState, GameState>>& f_TransitionTable() {
    static const std::vector<std::pair<GameState, GameState>> l_vTable = {
        {GameState::Boot,             GameState::MainMenu},

        {GameState::MainMenu,         GameState::StageSelect},
        {GameState::MainMenu,         GameState::Settings},

        {GameState::Settings,         GameState::MainMenu},

        {GameState::StageSelect,      GameState::StageLoading},

        {GameState::StageLoading,     GameState::Explore},

        {GameState::Explore,          GameState::EncounterResolve},
        {GameState::Explore,          GameState::Merchant},
        {GameState::Explore,          GameState::Paused},
        {GameState::Explore,          GameState::StageComplete},

        {GameState::EncounterResolve, GameState::DirectBattle},
        {GameState::EncounterResolve, GameState::Dialogue},
        {GameState::EncounterResolve, GameState::Merchant},

        {GameState::DirectBattle,     GameState::Explore},

        {GameState::Dialogue,         GameState::DirectBattle},
        {GameState::Dialogue,         GameState::Explore},

        {GameState::Merchant,         GameState::Explore},

        {GameState::Paused,           GameState::Explore},
        {GameState::Paused,           GameState::MainMenu},

        {GameState::StageComplete,    GameState::StageSelect},
        {GameState::StageComplete,    GameState::Ending},

        {GameState::Ending,           GameState::Credits},

        {GameState::Credits,          GameState::MainMenu},
    };
    return l_vTable;
}

inline bool f_IsLegalTransition(GameState l_eFrom, GameState l_eTo) {
    for (auto& l_pairE : f_TransitionTable())
        if (l_pairE.first == l_eFrom && l_pairE.second == l_eTo) return true;
    return false;
}

// Owns the current state plus a bounded history (useful for a future debug overlay, Milestone 2).
class GameStateMachine {
public:
    explicit GameStateMachine(GameState l_eInitial = GameState::Boot) : m_eCurrent(l_eInitial) {
        m_vHistory.push_back(l_eInitial);
    }
    GameState f_Current() const { return m_eCurrent; }
    const std::vector<GameState>& f_History() const { return m_vHistory; }

    // Applies the transition if (and only if) it's legal; returns whether it happened.
    // On rejection, current()/history() are left completely unchanged.
    bool f_TryTransition(GameState l_eTo) {
        if (!f_IsLegalTransition(m_eCurrent, l_eTo)) return false;
        m_eCurrent = l_eTo;
        m_vHistory.push_back(l_eTo);
        if (m_vHistory.size() > 256) m_vHistory.erase(m_vHistory.begin());
        return true;
    }

private:
    GameState m_eCurrent;
    std::vector<GameState> m_vHistory;
};

} // namespace toms

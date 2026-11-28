// game_gauntlet.cpp -- a boss's fight as a sequence (docs/gameplay/25_BOSS_GAUNTLET.md; the owner, 2026-11-22): its
// guards one at a time, then the boss. A team of monsters fights together on the map (their "party"), but not before a
// boss: there each guard is its own fight; after a win (the result screen closed) the 3D stage travels on to the next
// monster's place; when all are down, the boss comes, with its introduction. A loss ends the sequence; the boss stays on
// its tile and the next try starts from the first guard. A ladder on the battle screen (battle.rml #gauntlet) shows
// every fight: black until it is reached, then the monster's picture; the boss's at the bottom.
//
// The guards: the boss's own list (a stage object's "gauntlet", else enemies.json "gauntlet"), its "party" (it never fights
// beside a boss now), else up to battle.json "gauntlet" "count" kinds of the floor's other monsters, in the map's order.
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
#include "game_internal.h"

using namespace toms::game_detail;

bool Game::f_IsGauntletBoss(const std::string& l_strId) const {
    const auto l_itT = m_mapEnemyTpl.find(l_strId);
    return l_itT != m_mapEnemyTpl.end() && l_itT->second.value(toms::field::g_pszIntro, false);
}

std::vector<EnemyInst> Game::f_GauntletGuards(const EnemyInst& l_oBoss, const std::vector<std::string>& l_vStageList) {
    const nlohmann::json l_jG = m_jBattleJson.value(toms::field::g_pszGauntlet, nlohmann::json::object());
    std::vector<std::string> l_vIds = l_vStageList;
    const auto l_itT = m_mapEnemyTpl.find(l_oBoss.m_strId);
    if (l_vIds.empty() && l_itT != m_mapEnemyTpl.end())
        for (const char* l_pszKey : {toms::field::g_pszGauntlet, toms::field::g_pszParty}) {
            if (!l_vIds.empty()) break;
            for (const auto& l_jId : l_itT->second.value(l_pszKey, nlohmann::json::array())) if (l_jId.is_string()) l_vIds.push_back(l_jId.get<std::string>());
        }
    if (l_vIds.empty() && l_jG.value(toms::field::g_pszFromFloor, true)) {   // the floor's other monsters, each kind once, in the map's order
        const int l_iCount = std::clamp(l_jG.value(toms::field::g_pszCount, 3), 0, 8);
        for (const Entity& l_oE : m_oSt.m_vEntities) {
            if ((int)l_vIds.size() >= l_iCount) break;
            if (l_oE.m_strKind.rfind("monster:", 0) != 0 || l_oE.m_strId == l_oBoss.m_strId || f_IsGauntletBoss(l_oE.m_strId)) continue;
            if (std::find(l_vIds.begin(), l_vIds.end(), l_oE.m_strId) == l_vIds.end()) l_vIds.push_back(l_oE.m_strId);
        }
    }
    std::vector<EnemyInst> l_vOut;
    for (const std::string& l_strId : l_vIds) {
        EnemyInst l_oM;
        if (f_EnemyFromData(l_strId, nlohmann::json::object(), l_oM)) l_vOut.push_back(l_oM);   // (not on the map: x, y -1)
    }
    return l_vOut;
}

void Game::f_StartGauntlet(const EnemyInst& l_oBoss, const std::vector<EnemyInst>& l_vGuards) {
    m_oGauntlet = Gauntlet{};
    m_oGauntlet.m_bOn = true;
    // the rounds (the owner, 2026-11-28; docs/gameplay/27 §3.3): battle.json "gauntlet" "rounds" [1, 2, 4]: one monster, then
    // two together, then four, taken from the guards' kinds in turn. No "rounds": each guard alone, as before (doc 25).
    const nlohmann::json l_jRounds = m_jBattleJson.value(toms::field::g_pszGauntlet, nlohmann::json::object()).value(toms::field::g_pszRounds, nlohmann::json());
    if (l_jRounds.is_array() && !l_jRounds.empty() && !l_vGuards.empty()) {
        size_t l_nNext = 0;
        for (const auto& l_jN : l_jRounds) {
            const int l_iN = std::clamp(l_jN.is_number() ? l_jN.get<int>() : 1, 1, toms::g_iMaxFoes);
            m_oGauntlet.m_vSteps.push_back(l_vGuards[l_nNext++ % l_vGuards.size()]);
            std::vector<EnemyInst> l_vParty;
            for (int l_iK = 1; l_iK < l_iN; l_iK++) l_vParty.push_back(l_vGuards[l_nNext++ % l_vGuards.size()]);
            m_oGauntlet.m_vParties.push_back(l_vParty);
        }
    } else {
        m_oGauntlet.m_vSteps = l_vGuards;
        m_oGauntlet.m_vParties.assign(l_vGuards.size(), std::vector<EnemyInst>{});
    }
    m_oGauntlet.m_vSteps.push_back(l_oBoss);   // the last: the boss (on the map: winning it clears its tile)
    m_oGauntlet.m_vParties.push_back(std::vector<EnemyInst>{});
    m_v3BattleTravel = glm::vec3(0.0f);
    f_BattleEvent(toms::battle_event::g_pszGauntlet, -1, -1, (int)m_oGauntlet.m_vSteps.size());
    f_StartCombat(m_oGauntlet.m_vSteps[0], m_oGauntlet.m_vParties[0]);
}

// The result screen closed: a sequence goes on to its next monster -- the stage travels there (everything waits), then it
// fights; after the boss, or with no sequence, back to the map.
void Game::f_DismissVictory() {
    m_oCs.m_bWon = false;
    m_oCs.m_strLog.clear();
    if (!m_oGauntlet.m_bOn) return;
    if (m_oGauntlet.m_iStep + 1 >= (int)m_oGauntlet.m_vSteps.size()) { f_EndGauntlet(); return; }
    m_oGauntlet.m_iStep++;
    const nlohmann::json l_jG = m_jBattleJson.value(toms::field::g_pszGauntlet, nlohmann::json::object());
    m_v3TravelFrom = m_v3BattleTravel;
    m_v3TravelTo = f_GauntletStepVec() * (float)m_oGauntlet.m_iStep;
    const int l_iSuper = m_oCs.m_iSuperCharge;   // the charge points go on to the next fight (the owner, 2026-11-23)
    f_StartCombat(m_oGauntlet.m_vSteps[(size_t)m_oGauntlet.m_iStep], m_oGauntlet.m_vParties[(size_t)m_oGauntlet.m_iStep]);
    m_oCs.m_iSuperCharge = l_iSuper;
    m_iRunPuffStep = -1;
    // the hero back to his idle pose (the last fight's win pose holds only while its result shows)
    m_strBattleRigState.clear();
    f_SetBattleRigState(toms::state::g_pszIdle, true);
    m_oCs.m_iTravelMs = m_oCs.m_iTravelMsMax = std::max(0, l_jG.value(toms::field::g_pszTravelMs, 1500));
    if (m_oCs.m_iTravelMs <= 0) m_v3BattleTravel = m_v3TravelTo;
    f_BattleEvent(toms::battle_event::g_pszTravel, m_oGauntlet.m_iStep, -1, (int)m_oGauntlet.m_vSteps.size());
}

void Game::f_EndGauntlet() {
    m_oGauntlet = Gauntlet{};
    m_v3BattleTravel = m_v3TravelFrom = m_v3TravelTo = glm::vec3(0.0f);
}

// Each battle step while travelling: the stage's offset eases from the last monster's place to the next one's.
void Game::f_StepGauntletTravel(int l_iDtMs) {
    m_oCs.m_iTravelMs = std::max(0, m_oCs.m_iTravelMs - l_iDtMs);
    const float l_fP = 1.0f - (float)m_oCs.m_iTravelMs / (float)std::max(1, m_oCs.m_iTravelMsMax);
    const float l_fK = l_fP * l_fP * (3.0f - 2.0f * l_fP);
    m_v3BattleTravel = glm::mix(m_v3TravelFrom, m_v3TravelTo, l_fK);
}

// The ladder (battle.rml #gauntlet): one block a fight, top to bottom; the boss's last.
void Game::f_FillGauntletUi(toms::UiBattle& l_oB) const {
    l_oB.m_vSteps.clear();
    if (!m_oGauntlet.m_bOn) return;
    for (size_t l_nI = 0; l_nI < m_oGauntlet.m_vSteps.size(); l_nI++) {
        const EnemyInst& l_oE = m_oGauntlet.m_vSteps[l_nI];
        toms::UiGauntletStep l_oS;
        l_oS.m_bBoss = l_nI + 1 == m_oGauntlet.m_vSteps.size();
        l_oS.m_strSprite = f_UiSprite(l_oE.m_bBoss ? std::string("boss_demonlord") : f_TypeSprite("monster", l_oE.m_strId));
        const int l_iI = (int)l_nI;
        l_oS.m_strState = l_iI < m_oGauntlet.m_iStep ? toms::gauntlet_step::g_pszDone : l_iI == m_oGauntlet.m_iStep ? toms::gauntlet_step::g_pszNow : toms::gauntlet_step::g_pszNext;
        l_oS.m_strName = l_oS.m_strState == toms::gauntlet_step::g_pszNext ? (l_oS.m_bBoss ? m_oLocale.f_Tr(toms::text::g_pszBattleGauntletBoss) : std::string("?")) : l_oE.m_strName;
        if (l_nI < m_oGauntlet.m_vParties.size() && !m_oGauntlet.m_vParties[l_nI].empty())   // a round of several: how many
            l_oS.m_strName += " x" + std::to_string(m_oGauntlet.m_vParties[l_nI].size() + 1);
        l_oB.m_vSteps.push_back(l_oS);
    }
}

glm::vec3 Game::f_GauntletStepVec() const {
    const nlohmann::json l_jStep = m_jBattleJson.value(toms::field::g_pszGauntlet, nlohmann::json::object()).value(toms::field::g_pszStep, nlohmann::json::array({0.0, 0.0, -3.5}));
    return l_jStep.is_array() && l_jStep.size() >= 3 ? glm::vec3(l_jStep[0].get<float>(), l_jStep[1].get<float>(), l_jStep[2].get<float>())
                                                     : glm::vec3(0.0f, 0.0f, -3.5f);
}

// The monsters still to come (the owner, 2026-11-22, Q126: "show them in the distance"): each standing at its own place
// further on, as a dark shape -- who it is shows when it is reached (the ladder keeps it black too). On the 3D stage only.
void Game::f_DrawGauntletWaiting() {
    if (!m_oGauntlet.m_bOn || !f_Battle3DSpritesOn() || m_oCs.m_vFoes.empty()) return;
    const glm::vec3 l_v3Base = f_Battle3DMark(0) - m_v3TravelTo;   // the first monster's place
    const nlohmann::json l_jH = m_jBattle3DStage.value(toms::field::g_pszHeights, nlohmann::json::object());
    const nlohmann::json l_jG = m_jBattleJson.value(toms::field::g_pszGauntlet, nlohmann::json::object());
    const float l_fShade = l_jG.value(toms::field::g_pszWaitingShade, 0.0f);
    const float l_arrTint[4] = {l_fShade, l_fShade * 0.9f, l_fShade * 1.2f, l_jG.value(toms::field::g_pszWaitingAlpha, 0.85f)};
    for (size_t l_nK = (size_t)m_oGauntlet.m_iStep + 1; l_nK < m_oGauntlet.m_vSteps.size(); l_nK++) {
        const EnemyInst& l_oE = m_oGauntlet.m_vSteps[l_nK];
        const std::string l_strSprite = l_oE.m_bBoss ? std::string("boss_demonlord") : f_TypeSprite("monster", l_oE.m_strId);
        const auto l_itT = m_mapEnemyTpl.find(l_oE.m_strId);
        const bool l_bBig = l_oE.m_bBoss || (l_itT != m_mapEnemyTpl.end() && l_itT->second.value(toms::field::g_pszBig, false));
        const float l_fH = l_bBig ? l_jH.value(toms::field::g_pszBig, 3.0f) : l_jH.value(toms::field::g_pszMonster, 1.6f);
        // each a little to the side of the line, left and right in turn: the one behind is not hidden by the one in front
        const float l_fSide = l_jG.value(toms::field::g_pszWaitingSide, 0.9f) * (((l_nK - (size_t)m_oGauntlet.m_iStep) % 2 == 1) ? 1.0f : -1.0f);
        const glm::vec3 l_v3At = l_v3Base + f_GauntletStepVec() * (float)l_nK + glm::vec3(l_fSide, 0.0f, 0.0f);
        const glm::mat4 l_mat4M = f_Battle3DSpriteMatrix(glm::vec2(50.0f, 100.0f), l_v3At, l_fH / 100.0f);
        m_pRen->f_BeginWorld(&l_mat4M[0][0], false);
        m_pRen->f_DrawSprite(f_SpriteQuad(0.0f, 0.0f, 100.0f, 100.0f, f_SpriteLayer(l_strSprite), l_arrTint));
        m_pRen->f_EndWorld();
    }
}

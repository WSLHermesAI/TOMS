// game_moment.cpp -- a monster attack's beat (the owner, 2026-11-28; docs/gameplay/27_ATTACK_MOMENT_AND_TOWER_RUN.md §2).
//
// Every monster attack that is not a special (cells), a guard, or a volley's next hit plays a short beat:
//   1. the camera comes in on the monster (zoomMs, 0.3 s); the player's attack stops (a play running is cut off: the
//      hits so far count, Q128) and no new attack starts;
//   2. the monster's pose winds up (made in code from its picture: lunge, slam, sweep_low, sweep_high, cast; Q131) while
//      the player answers (answerMs, 0.5 s): the guard button, a swipe the attack's way, a tap on the beat, N taps, a
//      shape, or a swipe per wave of magic ("move"); "noDodge" shows "can't dodge" (guard only), "noGuard" "can't guard";
//   3. the hit lands, guarded or dodged (f_ResolveEnemyClockFire with the answer), and the result shows (resultMs, 0.5 s;
//      a dodge dodgeResultMs, shorter); the camera goes back.
// Everything else waits meanwhile (f_BattleFrame: the battle's time is 0). The attack is picked from the monster's
// enemies.json "attacks" (each with its "chance"), else battle.json "attackMoment" "default".
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"    // toms::answer, toms::pose, toms::way4, ... (no hard-coded name)
#include "game_internal.h"
#include "gesture.h"
#include "ui_names.h"       // toms::ui: the battle screen's classes
#include "res_keys.h"

using namespace toms::game_detail;

namespace {
// a swipe's way, of four
std::string f_Way4(glm::vec2 l_v2D) {
    if (std::fabs(l_v2D.x) >= std::fabs(l_v2D.y)) return l_v2D.x >= 0.0f ? toms::way4::g_pszRight : toms::way4::g_pszLeft;
    return l_v2D.y >= 0.0f ? toms::way4::g_pszDown : toms::way4::g_pszUp;
}
glm::vec2 f_Way4Vec(const std::string& l_strWay) {
    if (l_strWay == toms::way4::g_pszLeft) return {-1.0f, 0.0f};
    if (l_strWay == toms::way4::g_pszUp) return {0.0f, -1.0f};
    if (l_strWay == toms::way4::g_pszDown) return {0.0f, 1.0f};
    return {1.0f, 0.0f};
}
float f_Ease(float l_fT) { l_fT = std::clamp(l_fT, 0.0f, 1.0f); return l_fT * l_fT * (3.0f - 2.0f * l_fT); }
}  // namespace

bool Game::f_UseAttackMoment(int l_iFoe) const {
    const nlohmann::json l_jR = m_jBattleJson.value(toms::field::g_pszAttackMoment, nlohmann::json::object());
    if (!l_jR.is_object() || l_jR.empty() || l_jR.value(toms::field::g_pszOff, false)) return false;
    if (l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size() || f_MomentOn() || m_oCs.m_oSkill.m_bOn) return false;
    if (f_TutorialStepNow()) return false;   // the trial fight's lessons keep their own guard
    const CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
    if (l_oF.m_bPowered || l_oF.m_iVolleyHit > 0) return false;   // a special has its own rundown; a volley's next hit just lands
    const toms::MoveDef& l_oM = m_oMoves.f_Get(l_oF.m_strMove);
    return l_oM.m_eKind != toms::MoveKind::Guard && l_oM.m_strShape.empty() && l_oM.m_strGuardPlay.empty();
}

void Game::f_StartAttackMoment(int l_iFoe) {
    const nlohmann::json l_jR = m_jBattleJson.value(toms::field::g_pszAttackMoment, nlohmann::json::object());
    CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    l_oMo = CombatState::Moment{};
    l_oMo.m_iFoe = l_iFoe;
    // its attack: one of the monster's "attacks" by chance, else the default
    nlohmann::json l_jA = l_jR.value(toms::field::g_pszDefault, nlohmann::json::object());
    const auto l_itT = m_mapEnemyTpl.find(m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strId);
    if (l_itT != m_mapEnemyTpl.end() && l_itT->second.contains(toms::field::g_pszAttacks) && l_itT->second[toms::field::g_pszAttacks].is_array()) {
        const nlohmann::json& l_jList = l_itT->second[toms::field::g_pszAttacks];
        float l_fSum = 0.0f;
        for (const auto& l_jE : l_jList) if (l_jE.is_object()) l_fSum += std::max(0.0f, l_jE.value(toms::field::g_pszChance, 1.0f));
        float l_fRoll = std::uniform_real_distribution<float>(0.0f, std::max(0.0001f, l_fSum))(m_oMoveRng);
        for (const auto& l_jE : l_jList) {
            if (!l_jE.is_object()) continue;
            l_jA = l_jE;
            l_fRoll -= std::max(0.0f, l_jE.value(toms::field::g_pszChance, 1.0f));
            if (l_fRoll <= 0.0f) break;
        }
    }
    l_oMo.m_strAnswer = l_jA.value(toms::field::g_pszAnswer, std::string(toms::answer::g_pszGuard));
    l_oMo.m_strDodge = l_jA.value(toms::field::g_pszDodge, std::string(toms::way4::g_pszRight));
    l_oMo.m_strPose = l_jA.value(toms::field::g_pszPose, std::string(toms::pose::g_pszLunge));
    l_oMo.m_strShape = l_jA.value(toms::field::g_pszShape, std::string(toms::gesture::g_pszCircle));
    l_oMo.m_bTell = l_jA.value(toms::field::g_pszTell, false);
    l_oMo.m_bNoDodge = l_jA.value(toms::field::g_pszNoDodge, false);
    l_oMo.m_bNoGuard = l_jA.value(toms::field::g_pszNoGuard, false);
    l_oMo.m_iNeed = std::max(1, l_jA.value(toms::field::g_pszNeed, 3));
    l_oMo.m_iZoomMs = std::max(0, l_jR.value(toms::field::g_pszZoomMs, 300));
    l_oMo.m_iAnswerMs = std::max(150, l_jR.value(toms::field::g_pszAnswerMs, 500));
    if (l_oMo.m_strAnswer == toms::answer::g_pszShape) l_oMo.m_iAnswerMs = std::max(300, l_jR.value(toms::field::g_pszShapeMs, 1400));
    if (l_oMo.m_strAnswer == toms::answer::g_pszTaps) l_oMo.m_iAnswerMs = std::max(300, l_jR.value(toms::field::g_pszTapsMs, 1000));
    if (l_oMo.m_strAnswer == toms::answer::g_pszMove) {   // waves: each its own way, left or right (or the attack's own list)
        l_oMo.m_iWaveMs = std::max(200, l_jR.value(toms::field::g_pszWaveMs, 500));
        const nlohmann::json l_jW = l_jA.value(toms::field::g_pszWaves, nlohmann::json(3));
        if (l_jW.is_array()) { for (const auto& l_jWay : l_jW) if (l_jWay.is_string()) l_oMo.m_vWaves.push_back(l_jWay.get<std::string>()); }
        else for (int l_iK = 0; l_iK < std::clamp(l_jW.is_number() ? l_jW.get<int>() : 3, 1, 8); l_iK++)
            l_oMo.m_vWaves.push_back(std::uniform_int_distribution<int>(0, 1)(m_oMoveRng) ? toms::way4::g_pszLeft : toms::way4::g_pszRight);
        if (l_oMo.m_vWaves.empty()) l_oMo.m_vWaves.push_back(toms::way4::g_pszRight);
        l_oMo.m_iAnswerMs = l_oMo.m_iWaveMs * (int)l_oMo.m_vWaves.size();
    }
    l_oMo.m_iAnswerMs = std::max(150, l_jA.value(toms::field::g_pszAnswerMs, l_oMo.m_iAnswerMs));   // a skill attack: as long as its animation (owner, point 6)
    l_oMo.m_iResultMs = std::max(0, l_jR.value(toms::field::g_pszResultMs, 500));
    // the player's attack stops: a play running is cut off, its hits so far counting (Q128); a held pull is let go
    if (m_oCs.m_oTouch.f_Running()) {
        if (m_oCs.m_bTouchPaid) {
            const int l_iIndex = m_oCs.m_oTouch.f_Index(), l_iHits = m_oCs.m_oTouch.f_Result().m_iHits;
            m_oCs.m_oTouch.f_EndNow();
            f_TouchPlayed(l_iIndex, l_iHits);
        } else m_oCs.m_oTouch.f_CancelHold();
    } else if (f_AttackRunning()) f_InterruptCombo(l_iFoe);
    m_bTouchHeld = false;
    m_oCs.m_iGuardAtMs = -1;
    // the camera in on it, for the whole beat (it eases out over its last part)
    const float l_fZoom = l_jR.value(toms::field::g_pszZoom, 1.25f);
    m_oCs.m_fCamZoom = m_oSettings.m_bReduceEffects ? 1.0f + (l_fZoom - 1.0f) * 0.4f : std::max(1.01f, l_fZoom);
    m_oCs.m_iCamMs = m_oCs.m_iCamMsMax = l_oMo.m_iZoomMs + l_oMo.m_iAnswerMs + l_oMo.m_iResultMs + std::max(0, l_jR.value(toms::field::g_pszBackMs, 250));
    m_oCs.m_iCamOn = l_iFoe;
    m_oCs.m_strCamMove.clear();
    m_oCs.m_fCamPan = 0.0f;
    f_BattleEvent(toms::battle_event::g_pszMoment, l_iFoe, -1, l_oMo.m_iAnswerMs, l_oMo.m_strAnswer);
}

void Game::f_StepAttackMoment(int l_iDtMs) {
    CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    if (l_oMo.m_iFoe < 0) return;
    if (!m_oCs.m_bActive || l_oMo.m_iFoe >= (int)m_oCs.m_vFoes.size() || (l_oMo.m_iPhase < 2 && m_oCs.m_vFoes[(size_t)l_oMo.m_iFoe].f_Down())) {
        l_oMo = CombatState::Moment{};
        return;
    }
    l_oMo.m_iMs += l_iDtMs;
    if (l_oMo.m_iPhase == 0) {
        if (l_oMo.m_iMs >= l_oMo.m_iZoomMs) { l_oMo.m_iPhase = 1; l_oMo.m_iMs = 0; }
        return;
    }
    if (l_oMo.m_iPhase == 1) {
        if (l_oMo.m_iOutcome == 3 || l_oMo.m_iMs >= l_oMo.m_iAnswerMs) f_MomentImpact();   // a dodge goes at once (shorter); else on the beat
        return;
    }
    if (l_oMo.m_iMs >= l_oMo.m_iResultMs) {   // the result shown: the camera goes back, the fight goes on
        m_oCs.m_iCamMs = std::min(m_oCs.m_iCamMs, std::max(1, m_jBattleJson.value(toms::field::g_pszAttackMoment, nlohmann::json::object()).value(toms::field::g_pszBackMs, 250)));
        m_oCs.m_iCamMsMax = std::max(m_oCs.m_iCamMsMax, m_oCs.m_iCamMs);
        l_oMo = CombatState::Moment{};
    }
}

void Game::f_MomentImpact() {
    CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    const nlohmann::json l_jR = m_jBattleJson.value(toms::field::g_pszAttackMoment, nlohmann::json::object());
    int l_iOutcome = std::max(0, l_oMo.m_iOutcome);
    float l_fShare = 1.0f;
    if (l_oMo.m_strAnswer == toms::answer::g_pszMove && l_iOutcome == 0) {   // the waves: those not dodged hit, each its share
        const int l_iN = (int)l_oMo.m_vWaves.size();
        if (l_oMo.m_iWavesDodged >= l_iN) l_iOutcome = 3;
        else l_fShare = (float)(l_iN - l_oMo.m_iWavesDodged) / (float)std::max(1, l_iN);
    }
    l_oMo.m_iOutcome = l_iOutcome;
    if (l_iOutcome == 1) { f_PopWord(m_oLocale.f_Tr(toms::text::g_pszBattleWordBlock), -1, toms::pop_style::g_pszBlock); f_FeelMoment(toms::moment::g_pszBlock); }
    if (l_iOutcome == 2) { f_PopWord(m_oLocale.f_Tr(toms::text::g_pszBattleWordParry), -1, toms::pop_style::g_pszParry); f_FeelMoment(toms::moment::g_pszParry); }
    if (l_iOutcome == 3 && l_oMo.m_strHeroWay.empty()) l_oMo.m_strHeroWay = l_oMo.m_strDodge;
    if (l_iOutcome == 1 || l_iOutcome == 2) f_PlayerLook(toms::state::g_pszParry);
    if (m_oClimb.m_bOn && (l_iOutcome == 2 || l_iOutcome == 3)) m_oClimb.m_iPerfects++;   // a climb's perfects (27 §3.1)
    m_oCs.m_iMomentOutcome = l_iOutcome;
    m_oCs.m_fMomentDamage = l_fShare;
    const int l_iFoe = l_oMo.m_iFoe;
    f_ResolveEnemyClockFire(l_iFoe);   // its look, its effects, the damage (or "Dodged!"), its next move
    m_oCs.m_iMomentOutcome = -1;
    m_oCs.m_fMomentDamage = 1.0f;
    l_oMo.m_iPhase = 2;
    l_oMo.m_iMs = 0;
    if (l_iOutcome == 3) l_oMo.m_iResultMs = std::max(0, l_jR.value(toms::field::g_pszDodgeResultMs, 250));
}

void Game::f_MomentGuard() {
    CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    if (l_oMo.m_iPhase != 1 || l_oMo.m_iOutcome >= 0 || l_oMo.m_bSpent) return;
    if (l_oMo.m_bNoGuard) { m_oAudio.f_Play(toms::key::g_pszSfxDeny); l_oMo.m_bSpent = true; return; }   // "can't guard"
    const int l_iLeft = l_oMo.m_iAnswerMs - l_oMo.m_iMs;
    l_oMo.m_iOutcome = l_iLeft <= m_oGuard.m_iParryMs ? 2 : 1;   // just before it lands: a parry; else a block
    m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
}

void Game::f_MomentPointer(int l_iPhase, float l_fX, float l_fY) {
    CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    const glm::vec2 l_v2P(l_fX, l_fY);
    if (l_iPhase == 0) { l_oMo.m_bDown = true; l_oMo.m_v2Down = l_v2P; l_oMo.m_vStroke.assign(1, l_v2P); return; }
    if (!l_oMo.m_bDown) return;
    if (l_iPhase == 1) { l_oMo.m_vStroke.push_back(l_v2P); return; }
    l_oMo.m_bDown = false;
    l_oMo.m_vStroke.push_back(l_v2P);
    if (l_oMo.m_iPhase != 1 || l_oMo.m_iOutcome >= 0 || l_oMo.m_bSpent) return;
    const nlohmann::json l_jR = m_jBattleJson.value(toms::field::g_pszAttackMoment, nlohmann::json::object());
    const glm::vec2 l_v2D = l_v2P - l_oMo.m_v2Down;
    const bool l_bSwipe = glm::length(l_v2D) >= l_jR.value(toms::field::g_pszSwipeMin, 40.0f);
    const std::string& l_strA = l_oMo.m_strAnswer;
    auto l_fnWrong = [&] { l_oMo.m_bSpent = true; m_oAudio.f_Play(toms::key::g_pszSfxDeny); };
    if (l_strA == toms::answer::g_pszMove) {   // the wave on now: its way
        if (!l_bSwipe) return;
        const int l_iWave = std::min((int)l_oMo.m_vWaves.size() - 1, l_oMo.m_iMs / std::max(1, l_oMo.m_iWaveMs));
        if (l_iWave >= l_oMo.m_iWavesDodged && f_Way4(l_v2D) == l_oMo.m_vWaves[(size_t)l_iWave]) {
            l_oMo.m_iWavesDodged = l_iWave + 1;
            l_oMo.m_strHeroWay = l_oMo.m_vWaves[(size_t)l_iWave];
            m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
            if (l_oMo.m_iWavesDodged >= (int)l_oMo.m_vWaves.size()) l_oMo.m_iOutcome = 3;
        } else m_oAudio.f_Play(toms::key::g_pszSfxDeny);
        return;
    }
    if (l_oMo.m_bNoDodge) { if (l_bSwipe || l_strA != toms::answer::g_pszGuard) l_fnWrong(); return; }   // "can't dodge": only the guard
    if (l_strA == toms::answer::g_pszDodge || l_strA == toms::answer::g_pszGuard) {   // a guard attack can still be dodged its way
        if (!l_bSwipe) return;
        if (f_Way4(l_v2D) == l_oMo.m_strDodge) { l_oMo.m_iOutcome = 3; l_oMo.m_strHeroWay = l_oMo.m_strDodge; }
        else l_fnWrong();
        return;
    }
    if (l_strA == toms::answer::g_pszTap) {   // on the beat: when the ring closes (the hit), within tapWindowMs
        if (l_bSwipe) return;
        if (std::abs(l_oMo.m_iAnswerMs - l_oMo.m_iMs) <= l_jR.value(toms::field::g_pszTapWindowMs, 160)) l_oMo.m_iOutcome = 3;
        else l_fnWrong();
        return;
    }
    if (l_strA == toms::answer::g_pszTaps) {
        if (l_bSwipe) return;
        if (++l_oMo.m_iDone >= l_oMo.m_iNeed) l_oMo.m_iOutcome = 3;
        else m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
        return;
    }
    if (l_strA == toms::answer::g_pszShape) {
        if (toms::f_MatchGesture(l_oMo.m_vStroke, l_oMo.m_strShape)) l_oMo.m_iOutcome = 3;
        else l_fnWrong();
    }
}

glm::vec4 Game::f_MomentPose(int l_iFoe) const {
    const CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    if (l_oMo.m_iFoe != l_iFoe || l_oMo.m_iPhase == 0) return {0.0f, 0.0f, 1.0f, 1.0f};
    const float l_fWind = l_oMo.m_iPhase == 1 ? f_Ease((float)l_oMo.m_iMs / (float)std::max(1, l_oMo.m_iAnswerMs)) : 1.0f;
    const float l_fQ = l_oMo.m_iPhase == 2 ? std::clamp((float)l_oMo.m_iMs / 260.0f, 0.0f, 1.0f) : 0.0f;   // the strike and back
    auto l_fnStrike = [&](float l_fFrom, float l_fTo) { return l_fQ < 0.35f ? l_fFrom + (l_fTo - l_fFrom) * (l_fQ / 0.35f) : l_fTo * (1.0f - f_Ease((l_fQ - 0.35f) / 0.65f)); };
    const bool l_bStrike = l_oMo.m_iPhase == 2;
    const std::string& l_strP = l_oMo.m_strPose;
    if (l_strP == toms::pose::g_pszSlam) {   // it rises, then drops
        const float l_fDy = l_bStrike ? l_fnStrike(-45.0f, 12.0f) : -45.0f * l_fWind;
        const float l_fSy = l_bStrike ? (l_fQ < 0.5f ? 0.85f : 1.0f) : 1.0f + 0.08f * l_fWind;
        return {0.0f, l_fDy, 2.0f - l_fSy, l_fSy};
    }
    if (l_strP == toms::pose::g_pszSweepLow) {   // it crouches back, then sweeps low across
        const float l_fDx = l_bStrike ? l_fnStrike(18.0f, -95.0f) : 18.0f * l_fWind;
        return {l_fDx, (l_bStrike ? 12.0f * (1.0f - l_fQ) : 12.0f * l_fWind), 1.0f + 0.06f * l_fWind, 1.0f - 0.12f * l_fWind};
    }
    if (l_strP == toms::pose::g_pszSweepHigh) {   // it rears up, then swings high across
        const float l_fDx = l_bStrike ? l_fnStrike(18.0f, -95.0f) : 18.0f * l_fWind;
        return {l_fDx, (l_bStrike ? -24.0f * (1.0f - l_fQ) : -24.0f * l_fWind), 1.0f, 1.0f + 0.06f * l_fWind};
    }
    if (l_strP == toms::pose::g_pszCast) {   // it trembles and swells, then lets go
        const float l_fShake = l_bStrike ? 0.0f : std::sin((float)l_oMo.m_iMs * 0.07f) * 5.0f * l_fWind;
        const float l_fS = l_bStrike ? 1.0f + 0.12f * (1.0f - l_fQ) : 1.0f + 0.1f * l_fWind;
        return {l_fShake, 0.0f, l_fS, l_fS};
    }
    // lunge: it leans back, then lunges across at the hero
    const float l_fDx = l_bStrike ? l_fnStrike(28.0f, -120.0f) : 28.0f * l_fWind;
    return {l_fDx, 0.0f, 1.0f - 0.05f * l_fWind, 1.0f + 0.05f * l_fWind};
}

glm::vec2 Game::f_MomentHeroStep() const {
    const CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    if (l_oMo.m_iFoe < 0 || l_oMo.m_strHeroWay.empty()) return glm::vec2(0.0f);
    const float l_fPx = m_jBattleJson.value(toms::field::g_pszAttackMoment, nlohmann::json::object()).value(toms::field::g_pszStepPx, 70.0f);
    const float l_fBack = l_oMo.m_iPhase == 2 ? std::clamp((float)l_oMo.m_iMs / std::max(1.0f, (float)l_oMo.m_iResultMs), 0.0f, 1.0f) : 0.0f;
    glm::vec2 l_v2W = f_Way4Vec(l_oMo.m_strHeroWay);
    l_v2W.y *= 0.6f;   // up and down: a hop or a duck, smaller
    return l_v2W * l_fPx * (1.0f - f_Ease(l_fBack));
}

void Game::f_MomentUi(toms::UiBattle& l_oB) const {
    const CombatState::Moment& l_oMo = m_oCs.m_oMoment;
    if (l_oMo.m_iFoe < 0 || l_oMo.m_iPhase != 1) return;
    auto l_fnAdd = [&](const std::string& l_strCls, float l_fCx, float l_fCy, float l_fW, float l_fH, float l_fRot = 0.0f, float l_fRadius = 0.0f,
                       float l_fOpacity = 1.0f, std::string l_strText = {}) {
        l_oB.m_vTouch.push_back({l_strCls, l_fCx - l_fW * 0.5f, l_fCy - l_fH * 0.5f, l_fW, l_fH, l_fRot, l_fRadius, l_fOpacity, std::move(l_strText)});
    };
    const glm::vec4 l_v4Fb = f_FighterBox(l_oMo.m_iFoe), l_v4Hb = f_FighterBox(-1);
    (void)l_v4Hb;
    const float l_fW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
    const glm::vec2 l_v2Mid(l_fW * 0.5f, l_fH * 0.24f);   // the answer in the screen's middle, under the fight's title
    const float l_fLeft = 1.0f - (float)l_oMo.m_iMs / (float)std::max(1, l_oMo.m_iAnswerMs);
    auto l_fnArrow = [&](glm::vec2 l_v2At, const std::string& l_strWay, float l_fS) {
        const glm::vec2 l_v2W = f_Way4Vec(l_strWay);
        const float l_fAng = std::atan2(l_v2W.y, l_v2W.x) * 57.29578f;
        l_fnAdd(toms::ui::g_pszArrowNowOpen, l_v2At.x - l_v2W.x * 8.0f * l_fS, l_v2At.y - l_v2W.y * 8.0f * l_fS, 56 * l_fS, 12 * l_fS, l_fAng, 6 * l_fS);
        l_fnAdd(std::string(toms::ui::g_pszArrowNowOpen) + " head", l_v2At.x + l_v2W.x * 16.0f * l_fS, l_v2At.y + l_v2W.y * 16.0f * l_fS, 26 * l_fS, 26 * l_fS, l_fAng + 45.0f);
    };
    std::string l_strWord;
    const std::string& l_strA = l_oMo.m_strAnswer;
    if (l_strA == toms::answer::g_pszGuard) l_strWord = m_oLocale.f_Tr(toms::text::g_pszBattleMomentGuard);
    else if (l_strA == toms::answer::g_pszDodge) l_strWord = m_oLocale.f_Tr(toms::text::g_pszBattleMomentDodge);
    else if (l_strA == toms::answer::g_pszTap) l_strWord = m_oLocale.f_Tr(toms::text::g_pszBattleMomentTap);
    else if (l_strA == toms::answer::g_pszTaps) l_strWord = f_TrParam(m_oLocale.f_Tr(toms::text::g_pszBattleMomentTaps), toms::field::g_pszN, std::to_string(std::max(0, l_oMo.m_iNeed - l_oMo.m_iDone)));
    else if (l_strA == toms::answer::g_pszShape) l_strWord = m_oLocale.f_Tr(toms::text::g_pszBattleMomentShape);
    else if (l_strA == toms::answer::g_pszMove) l_strWord = m_oLocale.f_Tr(toms::text::g_pszBattleMomentMove);
    if (l_oMo.m_iOutcome < 0 && !l_oMo.m_bSpent) l_fnAdd(toms::ui::g_pszMomentText, l_v2Mid.x, l_v2Mid.y, 260, 44, 0.0f, 0.0f, 1.0f, l_strWord);
    if (l_oMo.m_bNoDodge) l_fnAdd(toms::ui::g_pszMomentSign, l_v2Mid.x, l_v2Mid.y + 40.0f, 150, 34, 0.0f, 8.0f, 1.0f, m_oLocale.f_Tr(toms::text::g_pszBattleMomentNoDodge));
    if (l_oMo.m_bNoGuard) l_fnAdd(toms::ui::g_pszMomentSign, l_v2Mid.x, l_v2Mid.y + 40.0f, 150, 34, 0.0f, 8.0f, 1.0f, m_oLocale.f_Tr(toms::text::g_pszBattleMomentNoGuard));
    const glm::vec2 l_v2Foe(l_v4Fb.x + l_v4Fb.z * 0.5f, l_v4Fb.y + l_v4Fb.w * 0.45f);
    if (l_strA == toms::answer::g_pszDodge && l_oMo.m_bTell && l_oMo.m_iOutcome < 0 && !l_oMo.m_bSpent)   // a tell: the way to swipe
        l_fnArrow({l_v2Mid.x, l_v2Mid.y + 70.0f}, l_oMo.m_strDodge, 1.4f);
    if (l_strA == toms::answer::g_pszTap && l_oMo.m_iOutcome < 0) {   // the ring closes on the monster: tap as it meets it
        const float l_fR = 34.0f + 90.0f * l_fLeft;
        l_fnAdd(toms::ui::g_pszMomentRing, l_v2Foe.x, l_v2Foe.y, 2 * l_fR, 2 * l_fR, 0.0f, l_fR);
        l_fnAdd(toms::ui::g_pszMomentRing, l_v2Foe.x, l_v2Foe.y, 68, 68, 0.0f, 34, 0.5f);
    }
    if (l_strA == toms::answer::g_pszTaps)
        for (int l_iK = 0; l_iK < l_oMo.m_iNeed; l_iK++)
            l_fnAdd(l_iK < l_oMo.m_iDone ? toms::ui::g_pszMomentDotNow : toms::ui::g_pszMomentDot, l_v2Mid.x + ((float)l_iK - (float)(l_oMo.m_iNeed - 1) * 0.5f) * 24.0f, l_v2Mid.y + 70.0f, 16, 16, 0.0f, 8);
    if (l_strA == toms::answer::g_pszShape && l_oMo.m_iOutcome < 0) {   // the shape to draw, faint over the middle
        const std::vector<glm::vec2> l_vC = toms::f_ShapeCorners(l_oMo.m_strShape);
        const glm::vec2 l_v2At(l_v2Mid.x, l_v2Mid.y + 110.0f);
        for (size_t l_nI = 1; l_nI < l_vC.size(); l_nI++) {
            const glm::vec2 l_v2A = l_v2At + (l_vC[l_nI - 1] - glm::vec2(0.5f)) * 90.0f, l_v2C = l_v2At + (l_vC[l_nI] - glm::vec2(0.5f)) * 90.0f;
            const glm::vec2 l_v2D = l_v2C - l_v2A;
            const float l_fLen = glm::length(l_v2D);
            if (l_fLen >= 2.0f) l_fnAdd(toms::ui::g_pszArrowNowOpen, (l_v2A.x + l_v2C.x) * 0.5f, (l_v2A.y + l_v2C.y) * 0.5f, l_fLen, 6, std::atan2(l_v2D.y, l_v2D.x) * 57.29578f, 3, 0.7f);
        }
    }
    if (l_strA == toms::answer::g_pszMove) {   // the wave on now: its way (the magic comes from the other side)
        const int l_iWave = std::min((int)l_oMo.m_vWaves.size() - 1, l_oMo.m_iMs / std::max(1, l_oMo.m_iWaveMs));
        if (l_iWave >= l_oMo.m_iWavesDodged) l_fnArrow({l_v2Mid.x, l_v2Mid.y + 70.0f}, l_oMo.m_vWaves[(size_t)l_iWave], 1.4f);
        for (int l_iK = 0; l_iK < (int)l_oMo.m_vWaves.size(); l_iK++)
            l_fnAdd(l_iK < l_oMo.m_iWavesDodged ? toms::ui::g_pszMomentDotNow : toms::ui::g_pszMomentDot, l_v2Mid.x + ((float)l_iK - (float)(l_oMo.m_vWaves.size() - 1) * 0.5f) * 24.0f, l_v2Mid.y + 112.0f, 16, 16, 0.0f, 8);
    }
    if (l_oMo.m_iOutcome < 0) l_fnAdd(toms::ui::g_pszMomentTime, l_v2Mid.x, l_v2Mid.y + 24.0f, 200.0f * l_fLeft, 6, 0.0f, 3.0f);
    if (l_oMo.m_bDown && l_oMo.m_vStroke.size() > 1)   // the finger's line
        for (size_t l_nI = 1; l_nI < l_oMo.m_vStroke.size(); l_nI++) {
            const glm::vec2 l_v2A = l_oMo.m_vStroke[l_nI - 1], l_v2C = l_oMo.m_vStroke[l_nI], l_v2D = l_v2C - l_v2A;
            const float l_fLen = glm::length(l_v2D);
            if (l_fLen >= 2.0f) l_fnAdd(toms::ui::g_pszSwipe, (l_v2A.x + l_v2C.x) * 0.5f, (l_v2A.y + l_v2C.y) * 0.5f, l_fLen, 8, std::atan2(l_v2D.y, l_v2D.x) * 57.29578f, 4);
        }
}

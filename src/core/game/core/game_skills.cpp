// game_skills.cpp -- the plays, skills and the training ground of docs/gameplay/16_SKILLS_AND_PLAYS.md, on the reusable
// blocks (cue_track.h, gesture.h, special_moves.h, touch_play.h):
//   - each weapon's own play (touch_play.h): armed when the attack is ready; the sword's cuts and the staff's spells
//     hit as they are answered (playHit), the play's end resolves the rest through resolveCombo;
//   - the "warn" guard and the monsters' rhythm: the "!" (foeWarning), a guard press stops the player's attack;
//   - skills (special_moves.json): a weapon's ultimate or a monster's special guard play, run as demo -> play -> finish;
//   - the training ground: a card spent for fights in a row with more EXP and weapon wins.
#include "res_keys.h"
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
#include "game_internal.h"

#include <ctime>

using namespace toms::game_detail;

// ---- each weapon's own play ----

toms::PlayType Game::f_BattlePlayType() const {
    if (!m_strPlayOverride.empty() && m_strPlayOverride != "weapon") return toms::f_PlayTypeFromString(m_strPlayOverride);
    return toms::f_PlayTypeFromString(f_WeaponPlayOf(f_WeaponKey()).m_strPlay);
}

glm::vec4 Game::f_TouchArea() const {
    // Never over a monster (the owner, 2026-11-12), and on the player's head (2026-11-13: "the command to attack monster
    // please show on player's head"): one area for every play -- the bow's target, the sword's prompts
    // (TouchPlay::promptAt: the area's bottom middle), the staff's circles, a mash -- centred over the hero, its bottom a
    // quarter of his height over his box (his painted head reaches about a fifth above it: his face stays clear); kept on the screen. The monsters' names and HP sit on their own heads.
    const float l_fW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
    const float l_fMargin = 12.0f, l_fTop = 8.0f;
    const float l_fAreaW = std::min(420.0f, l_fW - 2.0f * l_fMargin), l_fAreaH = l_fH > l_fW ? 160.0f : 120.0f;   // portrait: a little taller
    const glm::vec4 l_v4Hero = f_FighterBox(-1);
    // the weapon's "around" (weapon_play.json): "between": halfway between the hero and the monster he aims at, over their
    // heads (the bow, the owner 2026-11-23: "around the center"); "monster": round the monster aimed at (the staff, the
    // owner 2026-11-23: "show around the monster, not above the player"); else over the hero's head
    const std::string l_strAround = f_WeaponPlayOf(f_WeaponKey()).m_strAround;
    if (l_strAround == toms::around::g_pszCenter) {   // the middle of the screen, as wide as fits (the staff, the owner, 2026-11-27)
        const float l_fAw = std::min(560.0f, l_fW - 2.0f * l_fMargin);
        const float l_fAh = std::min(340.0f, std::max(120.0f, l_fH - 170.0f - 2.0f * l_fTop));
        const float l_fCy = std::clamp(l_fH * 0.45f - l_fAh * 0.5f, l_fTop, std::max(l_fTop, l_fH - 170.0f - l_fAh));
        return glm::vec4((l_fW - l_fAw) * 0.5f, l_fCy, l_fAw, l_fAh);
    }
    if (l_strAround == "monster" && !m_oCs.m_vFoes.empty()) {
        const glm::vec4 l_v4Foe = f_FighterBox(std::clamp(m_oCs.m_iTarget, 0, (int)m_oCs.m_vFoes.size() - 1));
        // as wide and tall as the monster's picture and a little more, at least 360 x 240 (room for the circles round it)
        const float l_fAw = std::min(l_fW - 2.0f * l_fMargin, std::max(360.0f, l_v4Foe.z * 1.6f));
        const float l_fAh = std::min(l_fH - 2.0f * l_fTop, std::max(240.0f, l_v4Foe.w * 1.2f));
        const float l_fCx = std::clamp(l_v4Foe.x + l_v4Foe.z * 0.5f - l_fAw * 0.5f, l_fMargin, std::max(l_fMargin, l_fW - l_fMargin - l_fAw));
        const float l_fCy = std::clamp(l_v4Foe.y + l_v4Foe.w * 0.5f - l_fAh * 0.5f, l_fTop, std::max(l_fTop, l_fH - 170.0f - l_fAh));
        return glm::vec4(l_fCx, l_fCy, l_fAw, l_fAh);
    }
    if (l_strAround == toms::around::g_pszBetween && !m_oCs.m_vFoes.empty()) {
        const glm::vec4 l_v4Foe = f_FighterBox(std::clamp(m_oCs.m_iTarget, 0, (int)m_oCs.m_vFoes.size() - 1));
        const float l_fMid = (l_v4Hero.x + l_v4Hero.z * 0.5f + l_v4Foe.x + l_v4Foe.z * 0.5f) * 0.5f;
        const float l_fHeads = std::min(l_v4Hero.y, l_v4Foe.y);
        const float l_fCx = std::clamp(l_fMid - l_fAreaW * 0.5f, l_fMargin, std::max(l_fMargin, l_fW - l_fMargin - l_fAreaW));
        const float l_fCy = std::clamp(std::min(l_fH * 0.5f - l_fAreaH, l_fHeads - l_fAreaH * 0.5f), l_fTop, std::max(l_fTop, l_fH - 170.0f - l_fAreaH));
        return glm::vec4(l_fCx, l_fCy, l_fAreaW, l_fAreaH);
    }
    const float l_fX = std::clamp(l_v4Hero.x + l_v4Hero.z * 0.5f - l_fAreaW * 0.5f, l_fMargin, std::max(l_fMargin, l_fW - l_fMargin - l_fAreaW));
    const float l_fY = std::clamp(l_v4Hero.y - l_v4Hero.w * 0.25f - l_fAreaH, l_fTop, std::max(l_fTop, l_fH - 170.0f - l_fAreaH));
    return glm::vec4(l_fX, l_fY, l_fAreaW, l_fAreaH);
}

toms::TouchPlaySettings Game::f_TouchSettingsFor(toms::PlayType l_eType) const {
    toms::TouchPlaySettings l_oS = m_oTouchSettings;
    // DEX widens every play's tolerance (17 §4.4), each with its own limit
    const int l_iDex = std::max(0, f_AttrValue("dex"));
    const float l_fDexDeg = (float)l_iDex * l_oS.m_fDexWayDeg;
    l_oS.m_fSliceTolerance = std::max(l_oS.m_fSliceTolerance, std::min(l_oS.m_fSliceTolerance + l_fDexDeg / 57.29578f, l_oS.m_fDexWayMaxDeg / 57.29578f));
    l_oS.m_fGestureExtraDeg = std::min(l_fDexDeg, l_oS.m_fDexGestureMaxDeg);
    l_oS.m_fShapeTolerance = std::min(0.35f, l_oS.m_fShapeTolerance + (float)l_iDex * l_oS.m_fDexCornerPct / 100.0f);
    l_oS.m_fCornerBonus = std::min(0.5f, (float)l_iDex * l_oS.m_fDexCornerPct / 100.0f);
    l_oS.m_oSliceCue.m_iInputMs = std::max(l_oS.m_oSliceCue.m_iInputMs, std::min(l_oS.m_oSliceCue.m_iInputMs + l_iDex * l_oS.m_iDexWindowMs, l_oS.m_oSliceCue.m_iCueMs - 100));
    l_oS.m_iCircleTapMs += l_iDex * l_oS.m_iDexWindowMs;
    l_oS.m_iRecallAnswerMs += l_iDex * l_oS.m_iDexWindowMs;   // the memory blade's answer time and a chain's (22 §2, §3)
    l_oS.m_iChainTimeMs += l_iDex * l_oS.m_iDexWindowMs;
    l_oS.m_iSpinTimeMs += l_iDex * l_oS.m_iDexWindowMs;     // a spin's time (22 §4)
    if (l_eType == toms::PlayType::ePULL_RELEASE) {   // the long bow (docs/gameplay/26): its line's growth by level, DEX and STR into its rings
        const std::string l_strKey = f_WeaponKey();
        l_oS.m_fBowGrowPerSec = f_WeaponPlayOf(l_strKey).f_AimGrowAt(f_WeaponLevel(l_strKey));
        if (l_oS.m_fBowGrowPerSec > 0.0f) {
            const float l_fDex = (float)std::max(0, f_AttrValue("dex")), l_fStr = (float)std::max(0, f_AttrValue("str"));
            l_oS.m_fBowRingScale = 1.0f + l_oS.m_v2BowDex.x * l_fDex / (l_fDex + l_oS.m_v2BowDex.y);
            l_oS.m_fBowStrBonus = 1.0f + l_oS.m_v2BowStr.x * l_fStr / (l_fStr + l_oS.m_v2BowStr.y);
            l_oS.m_v2BowOrigin = f_WeaponPoint();
        }
    }
    if (l_eType == toms::PlayType::ePULL_RELEASE) {   // STR holds the pull longer, DEX keeps the aim still (16 §3.2)
        l_oS.m_iBowHoldMs += std::max(0, f_AttrValue("str")) * l_oS.m_iBowHoldPerStr;
        l_oS.m_fBowShakePx /= 1.0f + (float)std::max(0, f_AttrValue("dex")) * l_oS.m_fBowShakePerDex;
    }
    return l_oS;
}

void Game::f_ArmTouchPlay(bool l_bUltimate, int l_iPoints) {
    const toms::PlayType l_eType = f_BattlePlayType();
    if (l_eType == toms::PlayType::ePULL_RELEASE && !l_bUltimate) l_iPoints = 1;   // a normal attack is one arrow (16 §3.2)
    toms::TouchPlaySettings l_oS = f_TouchSettingsFor(l_eType);
    if (toms::f_IsSwipePlay(l_eType)) {   // the level's gesture prompts take the last places (17 §4.2)
        const std::string l_strKey = f_WeaponKey();
        const std::vector<std::string> l_vShapes = f_WeaponPlayOf(l_strKey).f_SwipeCuesAt(f_WeaponLevel(l_strKey));
        l_oS.m_vSliceCues.assign((size_t)l_iPoints, l_eType == toms::PlayType::eSWIPE_GESTURE ? toms::gesture::g_pszV : toms::gesture::g_pszWay);
        for (size_t l_nK = 0; l_nK < l_vShapes.size() && l_nK < l_oS.m_vSliceCues.size(); l_nK++) l_oS.m_vSliceCues[l_oS.m_vSliceCues.size() - l_vShapes.size() + l_nK] = l_vShapes[l_nK];
        if (l_eType == toms::PlayType::eSWIPE_CHAIN)   // all shown at once (the memory blade): each shape adds its drawing time
            for (const std::string& l_strCue : l_oS.m_vSliceCues) {
                const auto l_it = l_oS.m_mapCueExtraMs.find(l_strCue);
                if (l_it != l_oS.m_mapCueExtraMs.end()) l_oS.m_iChainTimeMs += l_it->second;
            }
        if (l_eType == toms::PlayType::eSWIPE_RECALL) {   // the memory blade's timing by its level (weapon_play.json "recall", 22 §2)
            const toms::WeaponPlay::Recall l_oR = f_WeaponPlayOf(l_strKey).f_RecallAt(f_WeaponLevel(l_strKey));
            const int l_iDexMs = l_oS.m_iRecallAnswerMs - m_oTouchSettings.m_iRecallAnswerMs;   // (what DEX added above)
            if (l_oR.m_iShowMs >= 0) l_oS.m_iRecallShowMs = l_oR.m_iShowMs;
            if (l_oR.m_iAnswerMs >= 0) l_oS.m_iRecallAnswerMs = l_oR.m_iAnswerMs + l_iDexMs;
            if (l_oR.m_iPerCueMs >= 0) l_oS.m_iRecallPerCueMs = l_oR.m_iPerCueMs;
            if (l_oR.m_fReverse >= 0.0f) l_oS.m_fRecallReverse = l_oR.m_fReverse;
        }
    }
    if (l_eType == toms::PlayType::eSEQUENCE_CLICK) {   // the circles as a symbol, by the weapon's level (data/circle_symbols.json)
        const std::string l_strKey = f_WeaponKey();
        const int l_iLv = f_WeaponLevel(l_strKey);
        l_oS.m_vCircleSymbol = m_oCircleSymbols.f_Pick(l_strKey, l_iLv, l_iPoints, m_oPlayRng, nullptr, &l_oS.m_vCircleTimesMs);
        if (!m_vDebugSymbolPoints.empty()) {   // the battle editor's Try: its symbol, with as many circles as it has
            l_iPoints = (int)m_vDebugSymbolPoints.size();
            l_oS.m_vCircleSymbol = m_vDebugSymbolPoints;
            l_oS.m_vCircleTimesMs = m_vDebugSymbolTimesMs.size() == m_vDebugSymbolPoints.size() ? m_vDebugSymbolTimesMs : std::vector<int>();
        }
    }
    m_v4TouchArmedArea = f_TouchArea();
    m_oCs.m_oTouch.f_Start(l_eType, l_iPoints, l_bUltimate, m_v4TouchArmedArea, (uint32_t)m_oPlayRng(), l_oS);   // new places and order each attack
    m_oCs.m_iPlayDealt = 0;
    if (!f_WeaponPlayOf(f_WeaponKey()).m_bComboKeep) m_oCs.m_iComboCount = 0;   // a new attack, a new combo (the bow's goes on)
    m_oCs.m_bTouchPaid = l_bUltimate;   // the ultimate costs no stamina and runs at once with a full charge
    m_oCs.m_fTouchCharge = l_bUltimate ? 1.0f : 0.0f;
    if (!l_bUltimate && m_oCs.m_oTouch.f_Started()) {   // the sword and the staff start by themselves: the attack starts now
        m_oCs.m_bTouchPaid = true;
        m_oCs.m_fTouchCharge = f_ComboCharge();
        if (m_oCs.m_ePace == toms::Pace::Stamina) m_oCs.m_fStamina = std::max(0.0f, m_oCs.m_fStamina - (float)m_oPaceSettings.m_iAttackCost);
    }
    m_bTouchHeld = false;
}

void Game::f_BattlePointer(int l_iPhase, float l_fX, float l_fY) {
    if (f_MomentOn()) { m_bTouchHeld = false; f_MomentPointer(l_iPhase, l_fX, l_fY); return; }   // a monster attack's beat: the finger answers it, no attack (27 §2.2)
    if (m_oCs.m_iIntroFoeFire >= 0) { m_bTouchHeld = false; return; }   // a monster is about to attack: no attack now (the owner, 2026-11-27)
    if (l_iPhase == 0 && f_CellIntroOn()) return;   // no new press while everything waits; a pull already held can still let go
    if (f_HeroDown()) { m_bTouchHeld = false; return; }   // knocked down (f_KnockDownHero): his play was stopped, nothing new
    if (!m_oCs.m_bActive || m_oCs.m_bWinPending || !m_oCs.m_oTouch.f_Running()) { m_bTouchHeld = false; return; }
    if (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase != CombatState::SkillRun::Phase::Play) return;   // a move's show: watch it
    if (l_iPhase == 0 && !m_oCs.m_oTouch.f_Started() && m_oCs.m_ePace == toms::Pace::Stamina && m_oCs.m_fStamina < (float)m_oPaceSettings.m_iAttackCost) {
        m_oCs.m_strLog = m_oLocale.f_Tr(toms::text::g_pszBattleNoStamina);   // the stamina pace: starting an attack costs it (13 §6.3)
        m_oAudio.f_Play(toms::key::g_pszSfxDeny);
        return;
    }
    if (l_iPhase == 0) m_bTouchHeld = true;
    else if (!m_bTouchHeld) return;   // a press that began on a button, or before the play
    if (l_iPhase == 2) m_bTouchHeld = false;
    const int l_iIndex = m_oCs.m_oTouch.f_Index(), l_iHits = m_oCs.m_oTouch.f_Result().m_iHits;
    const bool l_bWasStarted = m_oCs.m_oTouch.f_Started();
    m_oCs.m_oTouch.f_Pointer(l_iPhase, l_fX, l_fY);
    if (m_oCs.m_oTouch.f_Started() && !l_bWasStarted && !m_oCs.m_bTouchPaid) {   // the bow's first pull: the attack starts (07 §4)
        m_oCs.m_bTouchPaid = true;
        m_oCs.m_fTouchCharge = f_ComboCharge();
        if (m_oCs.m_ePace == toms::Pace::Stamina) m_oCs.m_fStamina = std::max(0.0f, m_oCs.m_fStamina - (float)m_oPaceSettings.m_iAttackCost);
    }
    f_TouchPlayed(l_iIndex, l_iHits);
}

// Every frame, the fight waiting or not: a bow's target follows its area round the monster, the long bow's line starts at
// the hero's hand, wherever the camera has put them (a monster's special attack comes close on it and pans; the owner,
// 2026-11-27: "the red target and the aim line become weird ... make the aim line to the red target still attackable").
void Game::f_FollowBowPlay() {
    toms::TouchPlay& l_oP = m_oCs.m_oTouch;
    if (!m_oCs.m_bActive || !l_oP.f_Running() || l_oP.f_Type() != toms::PlayType::ePULL_RELEASE || m_oCs.m_oSkill.m_bOn || !m_bBattleBoxesKnown) return;
    const glm::vec4 l_v4A = f_TouchArea();
    if (std::fabs(l_v4A.x - m_v4TouchArmedArea.x) + std::fabs(l_v4A.y - m_v4TouchArmedArea.y) + std::fabs(l_v4A.z - m_v4TouchArmedArea.z) + std::fabs(l_v4A.w - m_v4TouchArmedArea.w) > 0.5f) {
        l_oP.f_FollowArea(l_v4A);
        m_v4TouchArmedArea = l_v4A;
    }
    if (l_oP.f_GrowMode()) l_oP.f_SetBowOrigin(f_WeaponPoint());
}

void Game::f_StepTouchPlay(int l_iDtMs) {
    const int l_iIndex = m_oCs.m_oTouch.f_Index(), l_iHits = m_oCs.m_oTouch.f_Result().m_iHits;
    if (m_oCs.m_oTouch.f_GrowMode()) m_oCs.m_oTouch.f_SetBowOrigin(f_WeaponPoint());   // the long bow's line starts at his bow hand
    m_oCs.m_oTouch.f_Update(l_iDtMs);
    {   // the long bow's line has reached the target's distance: a click, steer now (26 §4)
        const bool l_bStopped = m_oCs.m_oTouch.f_GrowMode() && m_oCs.m_oTouch.f_Aiming() && m_oCs.m_oTouch.f_AimStopped();
        if (l_bStopped && !m_bBowStopHeard) m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick, 1.4f, 0.6f);
        m_bBowStopHeard = l_bStopped;
    }
    f_BowGather(l_iDtMs);
    f_TouchPlayed(l_iIndex, l_iHits);
}

// The staff's hints (docs/gameplay/20 §2): each one counted is bigger and higher than the one before; all go at once with a
// pop; then they come back one by one, each with a soft note.
void Game::f_WandCues() {
    const toms::TouchPlay& l_oP = m_oCs.m_oTouch;
    if (l_oP.f_Type() != toms::PlayType::eSEQUENCE_CLICK || !l_oP.f_Running() || m_oCs.m_oSkill.m_bOn) { m_oWandSeen = WandSeen{}; return; }
    if (l_oP.f_ClockMs() < m_oWandSeen.m_iClockMs) m_oWandSeen = WandSeen{};   // a new play
    m_oWandSeen.m_iClockMs = l_oP.f_ClockMs();
    const auto& l_vC = l_oP.f_Circles();
    const int l_iN = (int)l_vC.size();
    const nlohmann::json l_jW = f_FeelFx().value(toms::field::g_pszWand, nlohmann::json::object());
    const float l_fStep = l_jW.value(toms::field::g_pszPitchStep, 2.0f);   // semitones up per hint
    const int l_iTiers = std::max(1, l_jW.value(toms::field::g_pszTiers, 4));
    auto l_fnPitch = [l_fStep](int l_iK) { return std::pow(2.0f, (float)l_iK * l_fStep / 12.0f); };
    const int l_iPhase = (int)l_oP.f_CirclePhase();
    if (l_oP.f_CirclePhase() == toms::TouchPlay::CirclePhase::Count) {
        const int l_iLit = l_oP.f_CirclesLit();
        for (int l_iK = m_oWandSeen.m_iLit; l_iK < l_iLit && l_iK < l_iN; l_iK++) {
            const int l_iTier = 1 + (l_iN > 1 ? l_iK * (l_iTiers - 1) / (l_iN - 1) : 0);
            f_PlayBattleFxAt("wand_hint_" + std::to_string(l_iTier), l_vC[(size_t)l_iK].m_fX, l_vC[(size_t)l_iK].m_fY, true, 0, 1.0f + 0.08f * (float)l_iK);
            m_oAudio.f_Play(l_jW.value(toms::field::g_pszHintSound, std::string(toms::key::g_pszSfxWandHint)), l_fnPitch(l_iK), 0.8f);
        }
        m_oWandSeen.m_iLit = std::max(m_oWandSeen.m_iLit, l_iLit);
    } else if (m_oWandSeen.m_iPhase == (int)toms::TouchPlay::CirclePhase::Count) {   // the count is over: all go at once
        for (const toms::TouchPlay::Circle& l_oC : l_vC) f_PlayBattleFxAt("wand_vanish", l_oC.m_fX, l_oC.m_fY, true);
        m_oAudio.f_Play(l_jW.value(toms::field::g_pszVanishSound, std::string(toms::key::g_pszSfxWandVanish)));
    }
    if (l_oP.f_CirclePhase() == toms::TouchPlay::CirclePhase::Tap) {
        const int l_iShown = l_oP.f_CirclesShown();
        for (int l_iK = m_oWandSeen.m_iShown; l_iK < l_iShown && l_iK < l_iN; l_iK++) {
            f_PlayBattleFxAt("wand_return", l_vC[(size_t)l_iK].m_fX, l_vC[(size_t)l_iK].m_fY, true);
            m_oAudio.f_Play(l_jW.value(toms::field::g_pszHintSound, std::string(toms::key::g_pszSfxWandHint)), l_fnPitch(l_iK) * 2.0f, 0.35f);
        }
        m_oWandSeen.m_iShown = std::max(m_oWandSeen.m_iShown, l_iShown);
    }
    m_oWandSeen.m_iPhase = l_iPhase;
}

// The bow's pull (docs/gameplay/20 §4): particles drawn into the arrow, more and faster as the charge grows, with a rising hum.
void Game::f_BowGather(int l_iDtMs) {
    const toms::TouchPlay& l_oP = m_oCs.m_oTouch;
    if (l_oP.f_Type() != toms::PlayType::ePULL_RELEASE || !l_oP.f_Aiming() || m_oCs.m_oSkill.m_bOn) { m_iBowGatherMs = 0; m_iBowGatherBeats = 0; return; }
    const nlohmann::json l_jB = f_FeelFx().value(toms::field::g_pszBow, nlohmann::json::object());
    const int l_iEvery = std::max(30, l_jB.value(toms::field::g_pszGatherEveryMs, 90));
    m_iBowGatherMs += l_iDtMs;
    while (m_iBowGatherMs >= l_iEvery) {
        m_iBowGatherMs -= l_iEvery;
        const float l_fCh = l_oP.f_Charge();
        const int l_iTier = l_fCh < 0.34f ? 1 : l_fCh < 0.67f ? 2 : 3;
        const glm::vec2 l_v2W = f_WeaponPoint();
        f_PlayBattleFxAt("bow_gather_" + std::to_string(l_iTier), l_v2W.x, l_v2W.y, false, 0, 0.8f + 0.6f * l_fCh);
        if (m_iBowGatherBeats++ % 3 == 0) m_oAudio.f_Play(l_jB.value(toms::field::g_pszChargeSound, std::string(toms::key::g_pszSfxBowCharge)), 0.8f + 0.9f * l_fCh, 0.5f);
    }
}

// A hit's particles (docs/gameplay/20 §3): a spray on the monster, a bigger one the longer the combo; the staff's more. The
// sword leaves its slash: streaks along the cut, growing and shrinking, one after another (afterimages).
void Game::f_HitSpray(int l_iFoe, const toms::TouchPlay::Hit& l_oH) {
    const nlohmann::json l_jF = f_FeelFx();
    const int l_iCombo = std::max(1, m_oCs.m_iComboCount);
    const bool l_bStaff = m_oCs.m_oTouch.f_Type() == toms::PlayType::eSEQUENCE_CLICK;
    const int l_iTiers = std::max(1, l_jF.value(toms::field::g_pszSprayTiers, 5));
    const int l_iTier = std::clamp(1 + (l_iCombo - 1) / std::max(1, l_jF.value(toms::field::g_pszSprayComboStep, 2)) + (l_bStaff ? 1 : 0), 1, l_iTiers);
    const glm::vec2 l_v2C = f_FighterCentre(l_iFoe);
    f_PlayBattleFxAt("combo_spray_" + std::to_string(l_iTier), l_v2C.x, l_v2C.y, false, 0, 1.0f + 0.04f * (float)std::min(l_iCombo, 12));
    if (toms::f_IsSwipePlay(m_oCs.m_oTouch.f_Type())) {
        f_PlayBattleFxAt("sword_slash", l_v2C.x, l_v2C.y, false, 0, 1.0f + 0.03f * (float)std::min(l_iCombo, 10), l_oH.m_fAngle);
        m_oAudio.f_Play(l_jF.value(toms::field::g_pszSwordSound, std::string(toms::key::g_pszSfxSwordSlash)), 0.95f + 0.04f * (float)std::min(l_iCombo, 8));
    }
    if (l_bStaff) m_oAudio.f_Play(l_jF.value(toms::field::g_pszWand, nlohmann::json::object()).value(toms::field::g_pszCastSound, std::string(toms::key::g_pszSfxWandCast)),
                                std::pow(2.0f, (float)l_oH.m_iOrder * 2.0f / 12.0f), 0.8f);
}

void Game::f_TouchPlayed(int l_iIndexBefore, int l_iHitsBefore) {
    const toms::PlayType l_eType = m_oCs.m_oTouch.f_Type();
    const bool l_bSkillPlay = m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase == CombatState::SkillRun::Phase::Play;
    f_WandCues();
    for (const toms::TouchPlay::Hit& l_oH : m_oCs.m_oTouch.f_TakeHits()) {
        if (l_bSkillPlay) {   // a move's step: its burst, "2/5", the next part of its name (the skill's "step" show)
            if (!l_oH.m_bOk) { m_oAudio.f_Play(toms::key::g_pszSfxDeny); continue; }
            m_oCs.m_oSkill.m_iSteps++;
            for (const toms::ShowBeat& l_oB : m_oCs.m_oSkill.m_pDef->m_vStep) f_SkillBeat(l_oB, {l_oH.m_fX, l_oH.m_fY});
            m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
            if (m_oCs.m_oSkill.m_iOwner < 0) {   // the player's move: an attack clip per answer, growing with the combo (17 I1, §19)
                m_oCs.m_iComboCount++;
                if (l_eType != toms::PlayType::eSPIN_CIRCLE) f_PlayAnswerClip();   // (a spin gathers its power: the shots come in the finish)
                f_ComboWord();
            }
            if (toms::f_IsFreePlay(l_eType) && f_FreeHit(l_oH)) return;   // the last monster fell: the move is over
        } else if (toms::f_IsSwipePlay(l_eType) || l_eType == toms::PlayType::eSEQUENCE_CLICK) {
            f_PlayHit(l_oH);   // each cut, each spell hits at once (16 §3.1, §3.3)
        } else if (l_eType == toms::PlayType::ePULL_RELEASE && l_oH.m_bOk) {   // the arrow bursts where it hits (20 §4)
            const float l_fCh = m_oCs.m_oTouch.f_ShotCharge();
            f_PlayBattleFxAt("bow_explode", l_oH.m_fX, l_oH.m_fY, false, 0, 0.8f + 0.8f * l_fCh);
            m_oAudio.f_Play(f_FeelFx().value(toms::field::g_pszBow, nlohmann::json::object()).value(toms::field::g_pszExplodeSound, std::string(toms::key::g_pszSfxExplosion)), 1.15f - 0.3f * l_fCh, 0.85f);
        }
    }
    if (!l_bSkillPlay && !toms::f_IsSwipePlay(l_eType) && l_eType != toms::PlayType::eSEQUENCE_CLICK && m_oCs.m_oTouch.f_Index() > l_iIndexBefore)
        m_oAudio.f_Play(m_oCs.m_oTouch.f_Result().m_iHits > l_iHitsBefore ? toms::key::g_pszSfxConfirmClick : toms::key::g_pszSfxDeny);
    if (l_eType == toms::PlayType::ePULL_RELEASE && m_oCs.m_oTouch.f_Index() > l_iIndexBefore && !m_oSettings.m_bBowTutorialDone) {
        m_oSettings.m_bBowTutorialDone = true;   // the first shot: the tutorial has done its job
        toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
        toms::f_SaveGameSettings(toms::f_SettingsPath(), m_oSettings);
    }
    if (m_oCs.m_oSkill.m_bOn) return;   // a move's play ends in updateSkill
    if (!m_oCs.m_bTouchPaid) return;
    const bool l_bTargetFell = m_oCs.m_iTarget < (int)m_oCs.m_vFoes.size() && m_oCs.m_vFoes[(size_t)m_oCs.m_iTarget].f_Down();
    if (!m_oCs.m_oTouch.f_Done() && !l_bTargetFell) return;
    m_oCs.m_bTouchPaid = false;   // resolved once (the done play stays, its marks fading, until the next is armed)
    m_oCs.m_oTouch.f_Stop();
    toms::ComboResult l_oR = m_oCs.m_oTouch.f_Result();
    l_oR.m_fCharge = l_eType == toms::PlayType::ePULL_RELEASE ? m_oCs.m_oTouch.f_ShotCharge() : m_oCs.m_fTouchCharge;   // the bow: its pull's charge
    if (l_eType == toms::PlayType::ePULL_RELEASE) l_oR.m_fShotMul = m_oCs.m_oTouch.f_ShotMul();   // the long bow: how close to the middle
    if (toms::f_IsSwipePlay(l_eType)) {   // the commands cut: their kinds' worth on average (the memory blade's circles and squares)
        const toms::WeaponPlay& l_oWp = f_WeaponPlayOf(f_WeaponKey());
        const auto& l_vMarks = m_oCs.m_oTouch.f_Marks();
        float l_fSum = 0.0f;
        int l_iCut = 0;
        for (int l_iK = 0; l_iK < (int)l_vMarks.size() && l_iK < m_oCs.m_oTouch.f_Points(); l_iK++)
            if (l_vMarks[(size_t)l_iK].m_bOk) { l_fSum += l_oWp.f_CueDamage(m_oCs.m_oTouch.f_CueKind(l_iK)); l_iCut++; }
        if (l_iCut > 0) l_oR.m_fCueMul = l_fSum / (float)l_iCut;
    }
    f_ResolveCombo(&l_oR);
}

int Game::f_NormalHitDamage(int l_iFoe) const {
    const CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)std::clamp(l_iFoe, 0, (int)m_oCs.m_vFoes.size() - 1)];
    const int l_iBaseHit = std::max(1, f_EffectiveAtk() - (int)std::lround(l_oF.m_oEnemy.m_iDef * l_oF.m_fDefMul));
    return toms::f_ComputeAttackDamage(l_iBaseHit, 100.0f, toms::f_EffectiveMaxMult(m_oEquipped, m_mapEquipmentDefs));
}

// An answer's attack clip, started again each time, so every cut and every spell shows (17 I1); the play's end keeps its big clip.
void Game::f_PlayAnswerClip() {
    if (!f_BattleRigReady()) return;
    const toms::ComboTier* l_pTier = f_ComboTier();
    const bool l_bHas = l_pTier && m_upPlayerView && !l_pTier->m_strClip.empty() && m_upPlayerView->f_Has(l_pTier->m_strClip);
    f_SetBattleRigState(toms::state::g_pszAttack, true, l_bHas ? l_pTier->m_strClip : std::string());
}

const toms::ComboTier* Game::f_ComboTier() const { return f_WeaponPlayOf(f_WeaponKey()).f_ComboTierAt(m_oCs.m_iComboCount); }

void Game::f_ComboWord() {
    if (m_oCs.m_iComboCount >= 2) f_PopWord(f_TrParam(m_oLocale.f_Tr(toms::text::g_pszBattleWordCombo), toms::field::g_pszN, std::to_string(m_oCs.m_iComboCount)), -1, toms::pop_style::g_pszChain);
}

// One swipe or tap of a free mode (17 §5): a hit at once, of the move's hitDamage times a normal attack. True: the fight is won.
bool Game::f_FreeHit(const toms::TouchPlay::Hit& l_oH) {
    const int l_iT = m_oCs.m_iTarget;
    if (l_iT < 0 || l_iT >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iT].f_Down() || !m_oCs.m_oSkill.m_pDef) return false;
    const int l_iDmg = std::max(1, (int)std::lround((float)f_NormalHitDamage(l_iT) * m_oCs.m_oSkill.m_pDef->m_oPlay.m_fHitDamage * m_oCs.m_vFoes[(size_t)l_iT].m_fStaggerDamage));
    f_BattleEvent(toms::battle_event::g_pszHit, -1, l_iT, l_iDmg, "free");
    const toms::ComboTier* l_pTier = f_ComboTier();
    if (l_pTier && !l_pTier->m_strFx.empty()) f_PlayBattleFx(l_pTier->m_strFx, l_iT);
    else if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx(true)) f_PlayBattleFx(l_pFx->m_strHit, l_iT);
    f_FeelMoment(l_pTier && !l_pTier->m_strFeel.empty() ? l_pTier->m_strFeel : std::string(toms::moment::g_pszHit));
    f_PopDamage(l_iDmg, l_iT, toms::pop_style::g_pszHit);
    f_DamageFoe(l_iT, l_iDmg);
    if (!m_oCs.m_bActive) return true;
    if (!m_oCs.m_vFoes[(size_t)l_iT].f_Down()) return false;
    const std::string l_strWin = f_TrParam(m_oLocale.f_Tr(toms::text::g_pszBattleCritWin), toms::field::g_pszDmg, std::to_string(l_iDmg));
    if (!toms::f_AllDown(m_oCs.f_FoeHp())) {   // another stands: the target moves on and the move goes on
        f_AfterPlayerHit(std::string(), l_strWin);
        return false;
    }
    m_oCs.m_oSkill.m_fShare = 1.0f;
    f_EndSkill();
    f_AfterPlayerHit(std::string(), l_strWin);
    return true;
}

void Game::f_PlayHit(const toms::TouchPlay::Hit& l_oH) {
    if (!l_oH.m_bOk) { m_oCs.m_iComboCount = 0; m_oAudio.f_Play(toms::key::g_pszSfxDeny); return; }   // a miss ends the combo
    const int l_iT = m_oCs.m_iTarget;
    if (l_iT < 0 || l_iT >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iT].f_Down()) return;
    // half a normal hit shared over the answers; the play's end deals the rest of the attack (resolveCombo). The staff's
    // share grows along the chain (docs/gameplay/20 §3): the k-th spell's is k^ramp of the whole, so the first are weak and
    // the last strong, and the play's total stays the same.
    const CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iT];
    const int l_iPoints = std::max(1, m_oCs.m_oTouch.f_Points());
    float l_fShare = 1.0f / (float)l_iPoints;
    if (m_oCs.m_oTouch.f_Type() == toms::PlayType::eSEQUENCE_CLICK) {
        const float l_fRamp = std::max(0.0f, f_FeelFx().value(toms::field::g_pszWand, nlohmann::json::object()).value(toms::field::g_pszDamageRamp, 1.6f));
        float l_fSum = 0.0f;
        for (int l_iK = 1; l_iK <= l_iPoints; l_iK++) l_fSum += std::pow((float)l_iK, l_fRamp);
        l_fShare = std::pow((float)std::clamp(l_oH.m_iOrder + 1, 1, l_iPoints), l_fRamp) / std::max(l_fSum, 1e-6f);
    }
    float l_fCueX = 1.0f;   // a harder command hits harder (weapon_play.json cueDamage)
    if (toms::f_IsSwipePlay(m_oCs.m_oTouch.f_Type()) && l_oH.m_iOrder >= 0 && l_oH.m_iOrder < m_oCs.m_oTouch.f_Points())
        l_fCueX = f_WeaponPlayOf(f_WeaponKey()).f_CueDamage(m_oCs.m_oTouch.f_CueKind(l_oH.m_iOrder));
    int l_iDmg = (int)std::lround((float)f_NormalHitDamage(l_iT) * 0.5f * l_fShare * l_fCueX * (l_oH.m_bPerfect ? 1.3f : 1.0f) * l_oF.m_fStaggerDamage);
    l_iDmg = std::max(1, l_iDmg);
    m_oCs.m_iPlayDealt += l_iDmg;
    f_BattleEvent(toms::battle_event::g_pszHit, -1, l_iT, l_iDmg, "answer");
    if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx()) {
        if (m_oCs.m_oTouch.f_Type() == toms::PlayType::eSEQUENCE_CLICK) f_PlayBattleFxAt(l_pFx->m_strRelease, l_oH.m_fX, l_oH.m_fY, true);   // the spell leaves the circle
        const toms::ComboTier* l_pHitTier = f_WeaponPlayOf(f_WeaponKey()).f_ComboTierAt(m_oCs.m_iComboCount + 1);
        f_PlayBattleFx(l_pHitTier && !l_pHitTier->m_strFx.empty() ? l_pHitTier->m_strFx : l_pFx->m_strHit, l_iT);
    }
    m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick);
    m_oCs.m_iComboCount++;
    {   // the feel grows with the combo too; a perfect answer is at least "great"
        const toms::ComboTier* l_pTier = f_ComboTier();
        const std::string l_strFeel = l_pTier && !l_pTier->m_strFeel.empty() ? l_pTier->m_strFeel : std::string(toms::moment::g_pszHit);
        f_FeelMoment(l_oH.m_bPerfect && l_strFeel == toms::moment::g_pszHit ? std::string(toms::moment::g_pszGreat) : l_strFeel);
    }
    f_PlayAnswerClip();
    f_HitSpray(l_iT, l_oH);
    f_ComboWord();
    f_PopDamage(l_iDmg, l_iT, l_oH.m_bPerfect ? toms::pop_style::g_pszGreat : toms::pop_style::g_pszHit);
    f_DamageFoe(l_iT, l_iDmg);
}

void Game::f_StopOwnAttack() {
    if (!f_AttackRunning()) return;
    m_oCs.m_oCombo = toms::ComboPlay{};
    m_oCs.m_oTouch.f_Stop();
    m_oCs.m_bTouchPaid = false;
    m_oCs.m_iPlayDealt = 0;
    m_oCs.m_iChargeMs = 0;
    if (m_oCs.m_ePace == toms::Pace::Cooldown) { m_oCs.m_oAtkBar.m_bCooling = true; m_oCs.m_oAtkBar.m_iCooldownMs = f_BarCooldownMs(); }
    f_BattleEvent(toms::battle_event::g_pszInterrupt, -1, -1, 0, toms::battle_event::g_pszGuard);
}

// ---- the monsters' rhythm and the "!" (16 §4) ----

int Game::f_MoveWarnMs(const toms::MoveDef& l_oM) const { return l_oM.m_iWarnMs >= 0 ? l_oM.m_iWarnMs : m_oGuard.m_iWarnMs; }

bool Game::f_FoeWarning(int l_iFoe) const {
    if (l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size()) return false;
    const CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
    if (!m_oCs.m_bActive || l_oF.f_Down() || l_oF.m_iStaggerMs > 0) return false;
    const toms::MoveDef& l_oM = m_oMoves.f_Get(l_oF.m_strMove);
    if (l_oM.m_eKind == toms::MoveKind::Guard) return false;
    const int l_iWarn = f_MoveWarnMs(l_oM);
    if (l_oF.m_iVolleyHit > 0) return l_oF.m_iVolleyMs <= l_iWarn;
    return l_oF.m_iMoveMs - l_oF.m_iClockMs <= l_iWarn;
}

// ---- skills (16 §6; special_moves.json) ----

glm::vec2 Game::f_SkillCentre() const {
    const glm::vec4 l_v4P = m_v4PlayerBox, l_v4F = f_FighterBox(std::clamp(m_oCs.m_iTarget, 0, std::max(0, (int)m_oCs.m_vFoes.size() - 1)));
    return {(l_v4P.x + l_v4P.z * 0.5f + l_v4F.x + l_v4F.z * 0.5f) * 0.5f, std::max(l_v4P.y + l_v4P.w, l_v4F.y + l_v4F.w) - 10.0f};
}

// A move's shape (drawShape) or ring (spinCircle) is drawn on the hero, where his attacks are drawn (the owner, 2026-11-19:
// the ring on his head; 2026-11-20: every weapon the same): his box's middle, ringY of its height down from its top, kept
// on the screen whole (size / 2 from each edge).
glm::vec2 Game::f_SkillShapeCentre(const toms::SpecialMove& l_oD) const {
    const glm::vec4 l_v4Hero = f_FighterBox(-1);
    const float l_fR = l_oD.m_oPlay.m_fSize * 0.5f + 6.0f;
    const float l_fW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
    if (l_oD.m_oPlay.m_strAround == toms::around::g_pszCenter)   // the middle of the screen (the bow's Cyclone Volley, the owner, 2026-11-25)
        return {l_fW * 0.5f, std::clamp(l_fH * 0.45f, l_fR, std::max(l_fR, l_fH - l_fR))};
    if (l_oD.m_oPlay.m_strAround == "monster" && !m_oCs.m_vFoes.empty()) {   // on the monster aimed at (the staff's Star Seal, 2026-11-23)
        const glm::vec4 l_v4Foe = f_FighterBox(std::clamp(m_oCs.m_iTarget, 0, (int)m_oCs.m_vFoes.size() - 1));
        return {std::clamp(l_v4Foe.x + l_v4Foe.z * 0.5f, l_fR, std::max(l_fR, l_fW - l_fR)),
                std::clamp(l_v4Foe.y + l_v4Foe.w * 0.5f, l_fR, std::max(l_fR, l_fH - l_fR))};
    }
    return {std::clamp(l_v4Hero.x + l_v4Hero.z * 0.5f, l_fR, std::max(l_fR, l_fW - l_fR)),
            std::clamp(l_v4Hero.y + l_v4Hero.w * l_oD.m_oPlay.m_fRingY, l_fR, std::max(l_fR, l_fH - l_fR))};
}

std::string Game::f_SkillName() const {
    if (!m_oCs.m_oSkill.m_pDef) return std::string();
    const std::string& l_strK = m_oCs.m_oSkill.m_pDef->m_strName;
    return m_oLocale.f_Has(l_strK) ? m_oLocale.f_Tr(l_strK) : l_strK;
}

void Game::f_StartSkill(const toms::SpecialMove& l_oDef, int l_iOwner) {
    f_StopOwnAttack();
    m_oCs.m_oSkill = CombatState::SkillRun{};
    m_oCs.m_oSkill.m_bOn = true;
    m_oCs.m_oSkill.m_pDef = &l_oDef;
    m_oCs.m_oSkill.m_iOwner = l_iOwner;
    if (l_iOwner < 0) m_oCs.m_iComboCount = 0;   // a move builds its own combo
    f_BattleEvent(toms::battle_event::g_pszSkill, l_iOwner, m_oCs.m_iTarget, 0, l_oDef.m_strId);
    for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oDef.m_vDemo, -1, 0)) f_SkillBeat(*l_pB, f_SkillCentre());
}

void Game::f_SkillBeat(const toms::ShowBeat& l_oB, glm::vec2 l_v2At) {
    using P = CombatState::SkillRun::Phase;
    const toms::SpecialMove& l_oD = *m_oCs.m_oSkill.m_pDef;
    const int l_iT = m_oCs.m_oSkill.m_iOwner >= 0 ? m_oCs.m_oSkill.m_iOwner : m_oCs.m_iTarget;
    if (l_oB.m_strWhat == "pathFx") {   // particles along the shape in its drawing order, a bigger burst at each corner
        const std::string l_strShape = l_oB.m_strShape.empty() ? l_oD.m_oPlay.m_strShape : l_oB.m_strShape;
        const glm::vec2 l_v2C = f_SkillShapeCentre(l_oD);   // where the shape is traced
        const int l_iN = std::max(8, l_oB.m_iMs / 60);
        for (int l_iK = 0; l_iK < l_iN; l_iK++) {
            const float l_fF = (float)l_iK / (float)(l_iN - 1);
            const glm::vec2 l_v2P = toms::f_ShapePoint(l_strShape, l_v2C, l_oD.m_oPlay.m_fSize, l_fF);
            f_PlayBattleFxAt(l_oB.m_strFx, l_v2P.x, l_v2P.y, true, (int)std::lround(l_fF * (float)l_oB.m_iMs));
        }
        const std::vector<glm::vec2> l_vCorners = toms::f_ShapeCorners(l_strShape);
        float l_fTotal = 0.0f, l_fDone = 0.0f;
        for (size_t l_nK = 1; l_nK < l_vCorners.size(); l_nK++) l_fTotal += glm::length(l_vCorners[l_nK] - l_vCorners[l_nK - 1]);
        for (size_t l_nK = 1; l_nK < l_vCorners.size() && l_fTotal > 0.0f; l_nK++) {
            l_fDone += glm::length(l_vCorners[l_nK] - l_vCorners[l_nK - 1]);
            const glm::vec2 l_v2P = l_v2C + (l_vCorners[l_nK] - glm::vec2(0.5f)) * l_oD.m_oPlay.m_fSize;
            f_PlayBattleFxAt(l_oB.m_strCorner, l_v2P.x, l_v2P.y, true, (int)std::lround(l_fDone / l_fTotal * (float)l_oB.m_iMs));
        }
    } else if (l_oB.m_strWhat == "fx") {
        if (l_oB.m_strOn == "player") f_PlayBattleFx(l_oB.m_strFx, -1);
        else if (l_oB.m_strOn == "monster") f_PlayBattleFx(l_oB.m_strFx, l_iT);
        else f_PlayBattleFxAt(l_oB.m_strFx, l_v2At.x, l_v2At.y, true);
    } else if (l_oB.m_strWhat == "dim") {
        m_oCs.m_iDimMs = m_oSettings.m_bReduceEffects ? 0 : std::max(700, l_oB.m_iMs > 0 ? l_oB.m_iMs : 60000);   // until the move ends (endSkill)
    } else if (l_oB.m_strWhat == "banner") {
        f_ShowBanner(m_oLocale.f_Has(l_oB.m_strText) ? m_oLocale.f_Tr(l_oB.m_strText) : l_oB.m_strText, m_oCs.m_oSkill.m_iOwner >= 0 ? toms::banner::g_pszLine : toms::banner::g_pszUltimate, l_oB.m_iMs > 0 ? l_oB.m_iMs : 1500);
    } else if (l_oB.m_strWhat == "intro") {   // the special attack's introduction (22 §4.2): its maker's banner, the label, the name typed in
        m_oCs.m_iHeroIntroMs = m_oCs.m_iHeroIntroMsMax = l_oB.m_iMs > 0 ? l_oB.m_iMs : 1600;
        m_oCs.m_iIntroOwner = m_oCs.m_oSkill.m_iOwner;
        m_oCs.m_strHeroIntroLabel = !l_oB.m_strText.empty() ? l_oB.m_strText : m_oCs.m_oSkill.m_iOwner < 0 ? std::string(toms::text::g_pszBattleUltimateLabel) : std::string(toms::text::g_pszBattleFoeSpecialLabel);
        m_oCs.m_strIntroName = f_SkillName();
    } else if (l_oB.m_strWhat == "name") {
        f_ShowBanner(f_SkillName(), toms::banner::g_pszUltimate, l_oB.m_iMs > 0 ? l_oB.m_iMs : 1400);
    } else if (l_oB.m_strWhat == "namePart") {
        f_ShowBanner(toms::f_NamePart(f_SkillName(), m_oCs.m_oSkill.m_iSteps, l_oD.m_oPlay.m_iPoints), toms::banner::g_pszUltimate, l_oB.m_iMs > 0 ? l_oB.m_iMs : 900);
    } else if (l_oB.m_strWhat == "count") {
        f_PopWord(std::to_string(m_oCs.m_oSkill.m_iSteps) + "/" + std::to_string(l_oD.m_oPlay.m_iPoints), -1, toms::pop_style::g_pszChain);
    } else if (l_oB.m_strWhat == "camera") {
        m_oCs.m_fCamZoom = m_oSettings.m_bReduceEffects ? 1.0f + (l_oB.m_fZoom - 1.0f) * 0.4f : l_oB.m_fZoom;
        m_oCs.m_iCamMs = m_oCs.m_iCamMsMax = std::max(300, l_oB.m_iMs);
        m_oCs.m_iCamOn = l_oB.m_strOn == "monster" ? l_iT : -1;
        m_oCs.m_strCamMove = m_oSettings.m_bReduceEffects ? std::string() : l_oB.m_strMove;   // (an orbit is a lot of motion)
        m_oCs.m_fCamDeg = l_oB.m_fDeg;
        m_oCs.m_fCamPan = std::clamp(l_oB.m_fValue, 0.0f, 1.0f);
    } else if (l_oB.m_strWhat == "clip") {
        if (m_oCs.m_oSkill.m_iOwner >= 0) f_FoeLook(l_iT, l_oB.m_strClip);   // a monster's move: its own look's state (its clip, else the shared one)
        else if (f_BattleRigReady()) {
            const bool l_bHas = m_upPlayerView && !l_oB.m_strClip.empty() && m_upPlayerView->f_Has(l_oB.m_strClip);
            f_SetBattleRigState(toms::state::g_pszAttack, true, l_bHas ? l_oB.m_strClip : std::string(), l_oB.m_fSpeed);
        }
    } else if (l_oB.m_strWhat == "hits") {   // the finish's hits share the move's damage (set when the finish began)
        if (m_oCs.m_oSkill.m_ePhase == P::Finish && m_oCs.m_oSkill.m_iOwner < 0 && m_oCs.m_oSkill.m_iHitDmg > 0) {
            const int l_iCount = std::max(1, l_oB.m_iCount);
            m_oCs.m_oSkill.m_iHitDmg = std::max(1, m_oCs.m_oSkill.m_iHitDmg / l_iCount);
            m_oCs.m_oSkill.m_iHitsLeft = l_iCount;
            m_oCs.m_oSkill.m_iHitGapMs = std::max(30, l_oB.m_iGapMs);
            m_oCs.m_oSkill.m_iHitMs = 0;
            m_oCs.m_oSkill.m_strHitClip = l_oB.m_strClip;   // each hit its own shot (the bow's volley, 22 §4)
            m_oCs.m_oSkill.m_strHitFx = l_oB.m_strFx;
            m_oCs.m_oSkill.m_fHitClipSpeed = l_oB.m_fSpeed;
            m_oCs.m_oSkill.m_pRain = nullptr;
            if (!l_oB.m_strProjectile.empty()) {   // the shots go up and fall as projectiles: each hit lands when its projectile does
                m_oCs.m_oSkill.m_pRain = &l_oB;
                m_oCs.m_oSkill.m_iRainLeft = l_iCount;
                m_oCs.m_oSkill.m_iRainMs = 0;
                m_oCs.m_oSkill.m_iHitMs = l_oB.m_iFlightMs;
            }
        }
    } else if (l_oB.m_strWhat == "shake") {
        if (!m_oSettings.m_bReduceEffects) {
            m_oCs.m_fShakePx = l_oB.m_fValue > 0.0f ? l_oB.m_fValue : 8.0f;
            m_oCs.m_iShakeMs = m_oCs.m_iShakeMsMax = l_oB.m_iMs > 0 ? l_oB.m_iMs : 300;
        }
    } else if (l_oB.m_strWhat == "sound") {
        if (m_oCs.m_oSkill.m_iOwner >= 0) f_PlayFoeSound(l_iT, l_oB.m_strFx);   // a monster's: bound to it (the 2026-11-13 rule)
        else m_oAudio.f_Play(l_oB.m_strFx);
    }
}

void Game::f_UpdateSkill(int l_iDtMs) {
    using P = CombatState::SkillRun::Phase;
    CombatState::SkillRun& l_oS = m_oCs.m_oSkill;
    const toms::SpecialMove& l_oD = *l_oS.m_pDef;
    const int l_iBefore = l_oS.m_iPhaseMs;
    l_oS.m_iPhaseMs += l_iDtMs;
    if (l_oS.m_ePhase == P::Demo) {
        for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oD.m_vDemo, l_iBefore, l_oS.m_iPhaseMs)) f_SkillBeat(*l_pB, f_SkillCentre());
        if (l_oS.m_iPhaseMs < toms::f_ShowLength(l_oD.m_vDemo)) return;
        // the play (16 §6.1 step 4): the move's own kind and numbers
        const toms::PlayType l_eType = l_oD.m_oPlay.m_eType;
        toms::TouchPlaySettings l_oTs = f_TouchSettingsFor(l_eType);
        l_oTs.m_strShape = l_oD.m_oPlay.m_strShape;
        l_oTs.m_fShapeSize = l_oD.m_oPlay.m_fSize;
        l_oTs.m_fShapeCornerRadius = l_oD.m_oPlay.m_fCornerRadius;
        l_oTs.m_iShapeTimeMs = l_oTs.m_iMashTimeMs = l_oTs.m_iFreeTimeMs = l_oD.m_oPlay.m_iTimeMs;
        l_oTs.m_iChainTimeMs = l_oD.m_oPlay.m_iTimeMs + (l_oTs.m_iChainTimeMs - m_oTouchSettings.m_iChainTimeMs);   // its time, and what DEX adds
        l_oTs.m_iSpinTimeMs = l_oD.m_oPlay.m_iTimeMs + (l_oTs.m_iSpinTimeMs - m_oTouchSettings.m_iSpinTimeMs);
        l_oTs.m_vSliceCues = l_oD.m_oPlay.m_vCues;
        l_oTs.m_bSpinFree = l_oD.m_oPlay.m_bAnyWay;   // the Taichi: either way
        const glm::vec2 l_v2C = f_SkillShapeCentre(l_oD);   // a shape or a ring: on the hero's head (22 §4)
        glm::vec4 l_v4Area = l_eType == toms::PlayType::eDRAW_SHAPE ? glm::vec4(l_v2C.x - 100.0f, l_v2C.y - 100.0f, 200.0f, 200.0f) : f_TouchArea();
        if (l_eType == toms::PlayType::eSPIN_CIRCLE) {   // its size from the move
            l_oTs.m_fSpinRadius = l_oD.m_oPlay.m_fSize * 0.5f;
            l_v4Area = glm::vec4(l_v2C.x - l_oTs.m_fSpinRadius, l_v2C.y - l_oTs.m_fSpinRadius, 2.0f * l_oTs.m_fSpinRadius, 2.0f * l_oTs.m_fSpinRadius);
        }
        m_oCs.m_oTouch.f_Start(l_eType, l_oD.m_oPlay.m_iPoints, true, l_v4Area, (uint32_t)m_oPlayRng(), l_oTs);
        m_bTouchHeld = false;
        l_oS.m_ePhase = P::Play;
        l_oS.m_iPhaseMs = 0;
        return;
    }
    if (l_oS.m_ePhase == P::Play) {
        if (l_oD.m_oPlay.m_eType == toms::PlayType::eSPIN_CIRCLE && !l_oD.m_oPlay.m_strGuideFx.empty() && m_oCs.m_oTouch.f_Running()) {
            // the guide: three sparks going round the ring the way the turn due goes (22 §4), new ones every 60 ms
            const toms::TouchPlay& l_oP = m_oCs.m_oTouch;
            for (l_oS.m_iGuideMs += l_iDtMs; l_oS.m_iGuideMs >= 60; l_oS.m_iGuideMs -= 60) {
                const float l_fWay = l_oP.f_SpinClockwise(l_oP.f_Index()) ? 1.0f : -1.0f;
                const float l_fBase = -90.0f + l_fWay * 360.0f * (float)l_oP.f_ClockMs() / (float)l_oD.m_oPlay.m_iGuidePeriodMs;
                for (int l_iK = 0; l_iK < 3; l_iK++) {
                    const float l_fRad = (l_fBase + 120.0f * (float)l_iK) / 57.29578f;
                    const glm::vec2 l_v2At = l_oP.f_SpinCentre() + glm::vec2(std::cos(l_fRad), std::sin(l_fRad)) * l_oP.f_SpinRadius();
                    f_PlayBattleFxAt(l_oD.m_oPlay.m_strGuideFx, l_v2At.x, l_v2At.y, true);
                }
            }
        }
        f_StepTouchPlay(l_iDtMs);
        if (m_oCs.m_oTouch.f_Running()) return;
        // the play is over: what was done counts (Q45: a share of the hits and the damage)
        l_oS.m_fShare = (float)m_oCs.m_oTouch.f_Result().m_iHits / (float)std::max(1, m_oCs.m_oTouch.f_Points());
        if (m_oCs.m_oTouch.f_Result().m_iHits < l_oD.m_oPlay.m_iMinHits) l_oS.m_fShare = 0.0f;   // too few: it fizzles (22 §3)
        if (!l_oD.m_oPlay.m_vTurnDamage.empty())   // the Taichi (26 §6): any turns from the table's first row are the whole show
            l_oS.m_fShare = toms::f_TurnDamage(l_oD.m_oPlay, m_oCs.m_oTouch.f_Result().m_iHits) > 0.0f ? 1.0f : 0.0f;
        l_oS.m_ePhase = P::Finish;
        l_oS.m_iPhaseMs = 0;
        m_oCs.m_iDimMs = 0;
        if (l_oS.m_iOwner < 0) {
            const int l_iT = m_oCs.m_iTarget;
            const float l_fStagger = l_iT < (int)m_oCs.m_vFoes.size() ? m_oCs.m_vFoes[(size_t)l_iT].m_fStaggerDamage : 1.0f;
            const float l_fMoveX = l_oD.m_oPlay.m_vTurnDamage.empty() ? l_oD.m_fDamage   // the Taichi: x by the turns done
                                                                         : toms::f_TurnDamage(l_oD.m_oPlay, m_oCs.m_oTouch.f_Result().m_iHits);
            l_oS.m_iHitDmg = (int)std::lround((float)f_NormalHitDamage(l_iT) * l_fMoveX * l_oS.m_fShare * l_fStagger);
            if (l_oS.m_fShare <= 0.0f) f_PopWord(m_oLocale.f_Tr(toms::text::g_pszBattleMoveDefaultMiss), -1, toms::pop_style::g_pszMoveMiss);
        }
        const bool l_bFull0 = l_oS.m_fShare >= 1.0f;   // a finish beat may be for the full play only, or the partial one (22 §3)
        if (l_oS.m_iOwner < 0) f_HeroShout(l_bFull0 ? "full" : l_oS.m_fShare > 0.0f ? "partial" : "fizzle");
        for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oD.m_vFinish, -1, 0)) if (toms::f_BeatOn(*l_pB, l_bFull0, l_oS.m_fShare > 0.0f)) f_SkillBeat(*l_pB, f_SkillCentre());
        return;
    }
    const bool l_bFull = l_oS.m_fShare >= 1.0f;
    const bool l_bLanded = l_oS.m_fShare > 0.0f;
    for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oD.m_vFinish, l_iBefore, l_oS.m_iPhaseMs)) if (toms::f_BeatOn(*l_pB, l_bFull, l_bLanded)) f_SkillBeat(*l_pB, f_SkillCentre());
    if (l_oS.m_pRain && l_oS.m_iRainLeft > 0) {   // a shot goes up, its projectile falls on the monster (the bow's meteors)
        for (l_oS.m_iRainMs -= l_iDtMs; l_oS.m_iRainMs <= 0 && l_oS.m_iRainLeft > 0; l_oS.m_iRainMs += std::max(30, l_oS.m_iHitGapMs)) {
            l_oS.m_iRainLeft--;
            if (!l_oS.m_strHitClip.empty() && f_BattleRigReady() && l_oS.m_iOwner < 0 && m_upPlayerView && m_upPlayerView->f_Has(l_oS.m_strHitClip))
                f_SetBattleRigState(toms::state::g_pszAttack, true, l_oS.m_strHitClip, l_oS.m_fHitClipSpeed);
            if (!l_oS.m_strHitFx.empty()) f_PlayBattleFx(l_oS.m_strHitFx, -1);
            f_LaunchRain(*l_oS.m_pRain, m_oCs.m_iTarget);
        }
    }
    if (l_oS.m_iHitsLeft > 0) {
        l_oS.m_iHitMs -= l_iDtMs;
        while (l_oS.m_iHitMs <= 0 && l_oS.m_iHitsLeft > 0 && m_oCs.m_oSkill.m_bOn) {
            l_oS.m_iHitsLeft--;
            l_oS.m_iHitMs += l_oS.m_iHitGapMs;
            const int l_iT = m_oCs.m_iTarget;
            if (l_iT < 0 || l_iT >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iT].f_Down()) { l_oS.m_iHitsLeft = 0; break; }
            if (!l_oS.m_pRain) {   // (with projectiles, the shot went up at its launch)
                if (!l_oS.m_strHitClip.empty() && f_BattleRigReady() && l_oS.m_iOwner < 0 && m_upPlayerView && m_upPlayerView->f_Has(l_oS.m_strHitClip))
                    f_SetBattleRigState(toms::state::g_pszAttack, true, l_oS.m_strHitClip, l_oS.m_fHitClipSpeed);   // a shot per hit, from its start
                if (!l_oS.m_strHitFx.empty()) f_PlayBattleFx(l_oS.m_strHitFx, -1);
            }
            f_BattleEvent(toms::battle_event::g_pszHit, -1, l_iT, l_oS.m_iHitDmg, toms::battle_event::g_pszSkill);
            f_PopDamage(l_oS.m_iHitDmg, l_iT, toms::pop_style::g_pszPerfect);
            f_FeelMoment(toms::moment::g_pszHit);
            if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx(true)) f_PlayBattleFx(l_pFx->m_strHit, l_iT);
            f_DamageFoe(l_iT, l_oS.m_iHitDmg);
            if (m_oCs.m_vFoes[(size_t)l_iT].f_Down()) {   // it fell: the rest of the show is cut short
                const int l_iDmg = l_oS.m_iHitDmg;
                f_EndSkill();   // (resets cs.skill, so s with it)
                f_AfterPlayerHit(std::string(), f_TrParam(m_oLocale.f_Tr(toms::text::g_pszBattleCritWin), toms::field::g_pszDmg, std::to_string(l_iDmg)));
                return;
            }
        }
    }
    if (m_oCs.m_oSkill.m_bOn && l_oS.m_iPhaseMs >= toms::f_ShowLength(l_oD.m_vFinish, l_bFull, l_bLanded) && l_oS.m_iHitsLeft <= 0) f_EndSkill();
}

// The hero's shout when his move's play is over (the owner, 2026-11-25: "use human speak for the special attack ... Eat
// this, See my power ... more excited when it succeeds; when not all match a meme sound"): one line at random from
// battle.json "heroVoice" "full" / "partial" / "fizzle" (sound keys, resources.json), at its "volume".
void Game::f_HeroShout(const std::string& l_strOutcome) {
    const nlohmann::json l_jV = m_jBattleJson.value(toms::field::g_pszHeroVoice, nlohmann::json::object());
    if (l_jV.value(toms::field::g_pszOff, false)) return;
    const nlohmann::json l_jPool = l_jV.value(l_strOutcome, nlohmann::json::array());
    if (!l_jPool.is_array() || l_jPool.empty()) return;
    const size_t l_nPick = (size_t)std::uniform_int_distribution<int>(0, (int)l_jPool.size() - 1)(m_oPlayRng);
    if (!l_jPool[l_nPick].is_string()) return;
    m_oAudio.f_Play(l_jPool[l_nPick].get<std::string>(), 1.0f, std::clamp(l_jV.value(toms::field::g_pszVolume, 1.0f), 0.0f, 2.0f));
    f_BattleEvent(toms::battle_event::g_pszShout, -1, -1, 0, l_strOutcome);   // (a scenario counts it; the runs are silent)
}

// One falling projectile at the monster, from high up and to its left (the hero's side), its impact where it lands; and
// the beat's "extra" ones round it, for show (no damage). The hit itself lands in updateSkill, flightMs later.
void Game::f_LaunchRain(const toms::ShowBeat& l_oB, int l_iFoe) {
    if (l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size()) return;
    const glm::vec4 l_v4F = f_FighterBox(l_iFoe);
    std::uniform_real_distribution<float> l_oU(0.0f, 1.0f);
    for (int l_iK = 0; l_iK <= l_oB.m_iExtra; l_iK++) {
        const bool l_bMain = l_iK == 0;
        const glm::vec2 l_v2To(l_v4F.x + l_v4F.z * (l_bMain ? 0.35f + 0.3f * l_oU(m_oPlayRng) : -0.2f + 1.4f * l_oU(m_oPlayRng)),
                               l_v4F.y + l_v4F.w * (l_bMain ? 0.35f + 0.25f * l_oU(m_oPlayRng) : 0.55f + 0.4f * l_oU(m_oPlayRng)));
        const glm::vec2 l_v2From(l_v2To.x - 180.0f - 160.0f * l_oU(m_oPlayRng), -60.0f - 80.0f * l_oU(m_oPlayRng));
        const int l_iFlight = l_bMain ? l_oB.m_iFlightMs : (int)((float)l_oB.m_iFlightMs * (0.8f + 0.5f * l_oU(m_oPlayRng)));
        f_LaunchBattleFx(l_bMain ? l_oB.m_strProjectile : l_oB.m_strProjectile + "_small", l_v2From, l_v2To, l_iFlight,
                       l_bMain ? l_oB.m_strImpact : l_oB.m_strImpact + "_small", std::string());
    }
}

void Game::f_EndSkill() {
    const CombatState::SkillRun l_oS = m_oCs.m_oSkill;
    m_oCs.m_oSkill = CombatState::SkillRun{};
    m_oCs.m_iDimMs = 0;
    m_oCs.m_iHeroIntroMs = 0;
    m_oCs.m_iIntroOwner = -1;
    m_oCs.m_oTouch.f_Stop();
    m_oCs.m_bTouchPaid = false;
    if (!l_oS.m_pDef) return;
    if (l_oS.m_iOwner < 0) {   // the player's move: counted like an attack (the grade, the chain), then the cooldown
        m_oCs.m_bUltimate = false;
        m_oCs.m_iAttacksDone++;
        if (l_oS.m_fShare >= 1.0f) { m_oCs.m_iPerfects++; m_oCs.m_iChain = std::min(m_oFeel.m_iChainMax, m_oCs.m_iChain + 1); }
        else if (l_oS.m_fShare <= 0.0f) m_oCs.m_iChain = 0;
        m_oCs.m_iBestChain = std::max(m_oCs.m_iBestChain, m_oCs.m_iChain);
        const int l_iT = m_oCs.m_iTarget;
        if (m_oCs.m_bActive && l_iT < (int)m_oCs.m_vFoes.size() && !m_oCs.m_vFoes[(size_t)l_iT].f_Down() && m_oCs.m_vFoes[(size_t)l_iT].m_iStaggerMs <= 0)
            if (toms::f_AddBreak(m_oCs.m_vFoes[(size_t)l_iT].m_fBreakGauge, (float)l_oS.m_iSteps, f_FoeBreakPoints(l_iT)))
                f_StunFoe(l_iT, m_oBreakSettings.m_iBrokenMs, m_oBreakSettings.m_fBrokenDamage, true);
        if (m_oCs.m_bActive && m_oCs.m_ePace == toms::Pace::Cooldown) { m_oCs.m_oAtkBar.m_bCooling = true; m_oCs.m_oAtkBar.m_iCooldownMs = f_BarCooldownMs(); }
        return;
    }
    // a monster's special guard play: its waiting hit lands now, less by the share done (16 §4.3)
    if (l_oS.m_iOwner < (int)m_oCs.m_vFoes.size() && m_oCs.m_bActive) {
        m_oCs.m_vFoes[(size_t)l_oS.m_iOwner].m_fGuardShare = l_oS.m_fShare;
        f_ResolveEnemyClockFire(l_oS.m_iOwner);
    }
}

// ---- the training ground (16 §5) ----

namespace {
std::string f_TodayString() {
    const std::time_t l_i64Now = std::time(nullptr);
    std::tm l_oTm{};
#ifdef _WIN32
    localtime_s(&l_oTm, &l_i64Now);
#else
    localtime_r(&l_i64Now, &l_oTm);
#endif
    char l_pszB[16];
    std::strftime(l_pszB, sizeof l_pszB, "%Y-%m-%d", &l_oTm);
    return l_pszB;
}
}  // namespace

int Game::f_TrainingRunsLeft() const {
    const nlohmann::json l_jTr = m_jBattleJson.value(toms::field::g_pszTraining, nlohmann::json::object());
    const int l_iPerDay = std::max(0, l_jTr.value(toms::field::g_pszRunsPerDay, 3));
    const int l_iUsed = m_oMeta.m_strTrainDay == f_TodayString() ? m_oMeta.m_iTrainRuns : 0;
    return std::max(0, l_iPerDay - l_iUsed);
}

bool Game::f_StartTraining(const std::string& l_strMonster) {
    const nlohmann::json l_jTr = m_jBattleJson.value(toms::field::g_pszTraining, nlohmann::json::object());
    const int l_iCost = std::max(0, l_jTr.value(toms::field::g_pszCards, 1));
    auto l_itIt = m_oMeta.m_mapCards.find(l_strMonster);
    EnemyInst l_oProbe;
    if (l_itIt == m_oMeta.m_mapCards.end() || l_itIt->second.m_iCopies < l_iCost || f_TrainingRunsLeft() <= 0 ||
        !f_EnemyFromData(l_strMonster, nlohmann::json::object(), l_oProbe)) {
        m_oAudio.f_Play(toms::key::g_pszSfxDeny);
        return false;
    }
    l_itIt->second.m_iCopies -= l_iCost;   // the run spends the card (the owner's rule; Q44)
    const std::string l_strToday = f_TodayString();
    if (m_oMeta.m_strTrainDay != l_strToday) { m_oMeta.m_strTrainDay = l_strToday; m_oMeta.m_iTrainRuns = 0; }
    m_oMeta.m_iTrainRuns++;
    f_MarkProgressDirty();
    if (m_bInGameMenuOpen) f_ClosePlayerMenu();
    const int l_iHp = m_oPl.m_iHp;
    m_oArena = Arena{};
    m_oArena.m_bOn = true;
    m_oArena.m_bTraining = true;
    m_oArena.m_vEnemies = {l_strMonster};
    m_oArena.m_jOverrides = nlohmann::json::object();   // (the monster as it is; a null here would throw in enemyFromData)
    m_oArena.m_iFightsMax = std::max(1, l_jTr.value(toms::field::g_pszFights, 3));
    m_oArena.m_iHpBefore = l_iHp;
    f_ArenaNextFight();
    m_oCs.m_strLog = f_TrParam(f_TrParam(m_oLocale.f_Tr(toms::text::g_pszTrainingStart), toms::field::g_pszName, l_oProbe.m_strName), toms::field::g_pszN, std::to_string(m_oArena.m_iFightsMax));
    return true;
}

void Game::f_EndTraining() {
    const Arena l_oA = m_oArena;
    m_oArena = Arena{};
    m_oCs = CombatState{};
    m_oPl.m_iHp = l_oA.m_iHpBefore;
    f_ShowNotice(f_TrParam(f_TrParam(f_TrParam(m_oLocale.f_Tr(toms::text::g_pszTrainingDone), toms::field::g_pszW, std::to_string(l_oA.m_iWins)), toms::field::g_pszExp, std::to_string(l_oA.m_iExpGained)),
                       "uses", std::to_string(l_oA.m_iWeaponExpGained)));
    f_MarkProgressDirty();
}

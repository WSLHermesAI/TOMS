// game_skills.cpp -- the plays, skills and the training ground of docs/gameplay/16_SKILLS_AND_PLAYS.md, on the reusable
// blocks (cue_track.h, gesture.h, special_moves.h, touch_play.h):
//   - each weapon's own play (touch_play.h): armed when the attack is ready; the sword's cuts and the staff's spells
//     hit as they are answered (playHit), the play's end resolves the rest through resolveCombo;
//   - the "warn" guard and the monsters' rhythm: the "!" (foeWarning), a guard press stops the player's attack;
//   - skills (special_moves.json): a weapon's ultimate or a monster's special guard play, run as demo -> play -> finish;
//   - the training ground: a card spent for fights in a row with more EXP and weapon wins.
#include "game_internal.h"

#include <ctime>

using namespace toms::game_detail;

// ---- each weapon's own play ----

toms::PlayType Game::f_BattlePlayType() const {
    if (!m_strPlayOverride.empty() && m_strPlayOverride != "weapon") return toms::f_PlayTypeFromString(m_strPlayOverride);
    return toms::f_PlayTypeFromString(f_WeaponPlayOf(f_WeaponKey()).m_strPlay);
}

glm::vec4 Game::f_TouchArea() const {
    const glm::vec4 l_v4B = f_FighterBox(std::clamp(m_oCs.m_iTarget, 0, std::max(0, (int)m_oCs.m_vFoes.size() - 1)));
    // the bow's targets on the monster; the circles from it down into the free space under the fighters; the
    // sword's prompts in that space (TouchPlay::promptAt), clear of the monster
    glm::vec4 l_v4A = f_BattlePlayType() == toms::PlayType::ePULL_RELEASE && !m_oCs.m_oSkill.m_bOn ? glm::vec4(l_v4B.x - 30.0f, l_v4B.y - 20.0f, l_v4B.z + 60.0f, l_v4B.w + 40.0f)
                                                                            : glm::vec4(l_v4B.x - 120.0f, l_v4B.y - 10.0f, l_v4B.z + 240.0f, l_v4B.w + 210.0f);
    // On the screen, where a finger can reach it: a monster at the edge (the third of three) would push it past the
    // side; the bottom keeps clear of the HP bar and the ultimate's row. Moved in, not shrunk, so the
    // circles keep their room.
    const float l_fW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
    const float l_fMargin = 12.0f, l_fTop = 60.0f;
    float l_fBottom = l_fH - 170.0f;
    l_v4A.z = std::min(l_v4A.z, l_fW - 2.0f * l_fMargin);
    if (m_oCs.m_bCellsOn) l_fBottom = std::min(l_fBottom, l_fH >= 1300.0f ? 800.0f : l_fH > l_fW ? 590.0f : 540.0f);   // above the cells: a press on one moves the hero
    l_v4A.w = std::min(l_v4A.w, std::max(40.0f, l_fBottom - l_fTop));
    l_v4A.x = std::clamp(l_v4A.x, l_fMargin, std::max(l_fMargin, l_fW - l_fMargin - l_v4A.z));
    l_v4A.y = std::clamp(l_v4A.y, l_fTop, std::max(l_fTop, l_fBottom - l_v4A.w));
    return l_v4A;
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
        l_oS.m_vSliceCues.assign((size_t)l_iPoints, l_eType == toms::PlayType::eSWIPE_GESTURE ? "v" : "way");
        for (size_t l_nK = 0; l_nK < l_vShapes.size() && l_nK < l_oS.m_vSliceCues.size(); l_nK++) l_oS.m_vSliceCues[l_oS.m_vSliceCues.size() - l_vShapes.size() + l_nK] = l_vShapes[l_nK];
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
    if (!m_oCs.m_bActive || m_oCs.m_bWinPending || !m_oCs.m_oTouch.f_Running()) { m_bTouchHeld = false; return; }
    if (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase != CombatState::SkillRun::Phase::Play) return;   // a move's show: watch it
    if (l_iPhase == 0 && !m_oCs.m_oTouch.f_Started() && m_oCs.m_ePace == toms::Pace::Stamina && m_oCs.m_fStamina < (float)m_oPaceSettings.m_iAttackCost) {
        m_oCs.m_strLog = m_oLocale.f_Tr("battle.no_stamina");   // the stamina pace: starting an attack costs it (13 §6.3)
        m_oAudio.f_Play("deny");
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

void Game::f_StepTouchPlay(int l_iDtMs) {
    const int l_iIndex = m_oCs.m_oTouch.f_Index(), l_iHits = m_oCs.m_oTouch.f_Result().m_iHits;
    m_oCs.m_oTouch.f_Update(l_iDtMs);
    f_TouchPlayed(l_iIndex, l_iHits);
}

void Game::f_TouchPlayed(int l_iIndexBefore, int l_iHitsBefore) {
    const toms::PlayType l_eType = m_oCs.m_oTouch.f_Type();
    const bool l_bSkillPlay = m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase == CombatState::SkillRun::Phase::Play;
    for (const toms::TouchPlay::Hit& l_oH : m_oCs.m_oTouch.f_TakeHits()) {
        if (l_bSkillPlay) {   // a move's step: its burst, "2/5", the next part of its name (the skill's "step" show)
            if (!l_oH.m_bOk) { m_oAudio.f_Play("deny"); continue; }
            m_oCs.m_oSkill.m_iSteps++;
            for (const toms::ShowBeat& l_oB : m_oCs.m_oSkill.m_pDef->m_vStep) f_SkillBeat(l_oB, {l_oH.m_fX, l_oH.m_fY});
            m_oAudio.f_Play("confirm_click");
            if (m_oCs.m_oSkill.m_iOwner < 0) {   // the player's move: an attack clip per answer, growing with the combo (17 I1, §19)
                m_oCs.m_iComboCount++;
                f_PlayAnswerClip();
                f_ComboWord();
            }
            if (toms::f_IsFreePlay(l_eType) && f_FreeHit(l_oH)) return;   // the last monster fell: the move is over
        } else if (toms::f_IsSwipePlay(l_eType) || l_eType == toms::PlayType::eSEQUENCE_CLICK) {
            f_PlayHit(l_oH);   // each cut, each spell hits at once (16 §3.1, §3.3)
        }
    }
    if (!l_bSkillPlay && !toms::f_IsSwipePlay(l_eType) && l_eType != toms::PlayType::eSEQUENCE_CLICK && m_oCs.m_oTouch.f_Index() > l_iIndexBefore)
        m_oAudio.f_Play(m_oCs.m_oTouch.f_Result().m_iHits > l_iHitsBefore ? "confirm_click" : "deny");
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
    f_SetBattleRigState("attack", true, l_bHas ? l_pTier->m_strClip : std::string());
}

const toms::ComboTier* Game::f_ComboTier() const { return f_WeaponPlayOf(f_WeaponKey()).f_ComboTierAt(m_oCs.m_iComboCount); }

void Game::f_ComboWord() {
    if (m_oCs.m_iComboCount >= 2) f_PopWord(f_TrParam(m_oLocale.f_Tr("battle.word_combo"), "n", std::to_string(m_oCs.m_iComboCount)), -1, "chain");
}

// One swipe or tap of a free mode (17 §5): a hit at once, of the move's hitDamage times a normal attack. True: the fight is won.
bool Game::f_FreeHit(const toms::TouchPlay::Hit& l_oH) {
    const int l_iT = m_oCs.m_iTarget;
    if (l_iT < 0 || l_iT >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iT].f_Down() || !m_oCs.m_oSkill.m_pDef) return false;
    const int l_iDmg = std::max(1, (int)std::lround((float)f_NormalHitDamage(l_iT) * m_oCs.m_oSkill.m_pDef->m_oPlay.m_fHitDamage * m_oCs.m_vFoes[(size_t)l_iT].m_fStaggerDamage));
    f_BattleEvent("hit", -1, l_iT, l_iDmg, "free");
    const toms::ComboTier* l_pTier = f_ComboTier();
    if (l_pTier && !l_pTier->m_strFx.empty()) f_PlayBattleFx(l_pTier->m_strFx, l_iT);
    else if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx(true)) f_PlayBattleFx(l_pFx->m_strHit, l_iT);
    f_FeelMoment(l_pTier && !l_pTier->m_strFeel.empty() ? l_pTier->m_strFeel : std::string("hit"));
    f_PopDamage(l_iDmg, l_iT, "hit");
    f_DamageFoe(l_iT, l_iDmg);
    if (!m_oCs.m_bActive) return true;
    if (!m_oCs.m_vFoes[(size_t)l_iT].f_Down()) return false;
    const std::string l_strWin = f_TrParam(m_oLocale.f_Tr("battle.crit_win"), "dmg", std::to_string(l_iDmg));
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
    if (!l_oH.m_bOk) { m_oCs.m_iComboCount = 0; m_oAudio.f_Play("deny"); return; }   // a miss ends the combo
    const int l_iT = m_oCs.m_iTarget;
    if (l_iT < 0 || l_iT >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iT].f_Down()) return;
    // half a normal hit shared over the answers; the play's end deals the rest of the attack (resolveCombo)
    const CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iT];
    int l_iDmg = (int)std::lround((float)f_NormalHitDamage(l_iT) * 0.5f / (float)std::max(1, m_oCs.m_oTouch.f_Points()) *
                               (l_oH.m_bPerfect ? 1.3f : 1.0f) * l_oF.m_fStaggerDamage);
    l_iDmg = std::max(1, l_iDmg);
    m_oCs.m_iPlayDealt += l_iDmg;
    f_BattleEvent("hit", -1, l_iT, l_iDmg, "answer");
    if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx()) {
        if (m_oCs.m_oTouch.f_Type() == toms::PlayType::eSEQUENCE_CLICK) f_PlayBattleFxAt(l_pFx->m_strRelease, l_oH.m_fX, l_oH.m_fY, true);   // the spell leaves the circle
        const toms::ComboTier* l_pHitTier = f_WeaponPlayOf(f_WeaponKey()).f_ComboTierAt(m_oCs.m_iComboCount + 1);
        f_PlayBattleFx(l_pHitTier && !l_pHitTier->m_strFx.empty() ? l_pHitTier->m_strFx : l_pFx->m_strHit, l_iT);
    }
    m_oAudio.f_Play("confirm_click");
    m_oCs.m_iComboCount++;
    {   // the feel grows with the combo too; a perfect answer is at least "great"
        const toms::ComboTier* l_pTier = f_ComboTier();
        const std::string l_strFeel = l_pTier && !l_pTier->m_strFeel.empty() ? l_pTier->m_strFeel : std::string("hit");
        f_FeelMoment(l_oH.m_bPerfect && l_strFeel == "hit" ? std::string("great") : l_strFeel);
    }
    f_PlayAnswerClip();
    f_ComboWord();
    f_PopDamage(l_iDmg, l_iT, l_oH.m_bPerfect ? "great" : "hit");
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
    f_BattleEvent("interrupt", -1, -1, 0, "guard");
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
    f_BattleEvent("skill", l_iOwner, m_oCs.m_iTarget, 0, l_oDef.m_strId);
    for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oDef.m_vDemo, -1, 0)) f_SkillBeat(*l_pB, f_SkillCentre());
}

void Game::f_SkillBeat(const toms::ShowBeat& l_oB, glm::vec2 l_v2At) {
    using P = CombatState::SkillRun::Phase;
    const toms::SpecialMove& l_oD = *m_oCs.m_oSkill.m_pDef;
    const int l_iT = m_oCs.m_oSkill.m_iOwner >= 0 ? m_oCs.m_oSkill.m_iOwner : m_oCs.m_iTarget;
    if (l_oB.m_strWhat == "pathFx") {   // particles along the shape in its drawing order, a bigger burst at each corner
        const std::string l_strShape = l_oB.m_strShape.empty() ? l_oD.m_oPlay.m_strShape : l_oB.m_strShape;
        const glm::vec2 l_v2C = f_SkillCentre();
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
        f_ShowBanner(m_oLocale.f_Has(l_oB.m_strText) ? m_oLocale.f_Tr(l_oB.m_strText) : l_oB.m_strText, m_oCs.m_oSkill.m_iOwner >= 0 ? "line" : "ultimate", l_oB.m_iMs > 0 ? l_oB.m_iMs : 1500);
    } else if (l_oB.m_strWhat == "name") {
        f_ShowBanner(f_SkillName(), "ultimate", l_oB.m_iMs > 0 ? l_oB.m_iMs : 1400);
    } else if (l_oB.m_strWhat == "namePart") {
        f_ShowBanner(toms::f_NamePart(f_SkillName(), m_oCs.m_oSkill.m_iSteps, l_oD.m_oPlay.m_iPoints), "ultimate", l_oB.m_iMs > 0 ? l_oB.m_iMs : 900);
    } else if (l_oB.m_strWhat == "count") {
        f_PopWord(std::to_string(m_oCs.m_oSkill.m_iSteps) + "/" + std::to_string(l_oD.m_oPlay.m_iPoints), -1, "chain");
    } else if (l_oB.m_strWhat == "camera") {
        m_oCs.m_fCamZoom = m_oSettings.m_bReduceEffects ? 1.0f + (l_oB.m_fZoom - 1.0f) * 0.4f : l_oB.m_fZoom;
        m_oCs.m_iCamMs = m_oCs.m_iCamMsMax = std::max(300, l_oB.m_iMs);
        m_oCs.m_iCamOn = l_oB.m_strOn == "monster" ? l_iT : -1;
    } else if (l_oB.m_strWhat == "clip") {
        if (f_BattleRigReady() && m_oCs.m_oSkill.m_iOwner < 0) {
            const bool l_bHas = m_upPlayerView && !l_oB.m_strClip.empty() && m_upPlayerView->f_Has(l_oB.m_strClip);
            f_SetBattleRigState("attack", true, l_bHas ? l_oB.m_strClip : std::string());
        }
    } else if (l_oB.m_strWhat == "hits") {   // the finish's hits share the move's damage (set when the finish began)
        if (m_oCs.m_oSkill.m_ePhase == P::Finish && m_oCs.m_oSkill.m_iOwner < 0 && m_oCs.m_oSkill.m_iHitDmg > 0) {
            const int l_iCount = std::max(1, l_oB.m_iCount);
            m_oCs.m_oSkill.m_iHitDmg = std::max(1, m_oCs.m_oSkill.m_iHitDmg / l_iCount);
            m_oCs.m_oSkill.m_iHitsLeft = l_iCount;
            m_oCs.m_oSkill.m_iHitGapMs = std::max(30, l_oB.m_iGapMs);
            m_oCs.m_oSkill.m_iHitMs = 0;
        }
    } else if (l_oB.m_strWhat == "shake") {
        if (!m_oSettings.m_bReduceEffects) {
            m_oCs.m_fShakePx = l_oB.m_fValue > 0.0f ? l_oB.m_fValue : 8.0f;
            m_oCs.m_iShakeMs = m_oCs.m_iShakeMsMax = l_oB.m_iMs > 0 ? l_oB.m_iMs : 300;
        }
    } else if (l_oB.m_strWhat == "sound") {
        m_oAudio.f_Play(l_oB.m_strFx);
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
        l_oTs.m_vSliceCues = l_oD.m_oPlay.m_vCues;
        const glm::vec2 l_v2C = f_SkillCentre();
        const glm::vec4 l_v4Area = l_eType == toms::PlayType::eDRAW_SHAPE ? glm::vec4(l_v2C.x - 100.0f, l_v2C.y - 100.0f, 200.0f, 200.0f) : f_TouchArea();
        m_oCs.m_oTouch.f_Start(l_eType, l_oD.m_oPlay.m_iPoints, true, l_v4Area, (uint32_t)m_oPlayRng(), l_oTs);
        m_bTouchHeld = false;
        l_oS.m_ePhase = P::Play;
        l_oS.m_iPhaseMs = 0;
        return;
    }
    if (l_oS.m_ePhase == P::Play) {
        f_StepTouchPlay(l_iDtMs);
        if (m_oCs.m_oTouch.f_Running()) return;
        // the play is over: what was done counts (Q45: a share of the hits and the damage)
        l_oS.m_fShare = (float)m_oCs.m_oTouch.f_Result().m_iHits / (float)std::max(1, m_oCs.m_oTouch.f_Points());
        l_oS.m_ePhase = P::Finish;
        l_oS.m_iPhaseMs = 0;
        m_oCs.m_iDimMs = 0;
        if (l_oS.m_iOwner < 0) {
            const int l_iT = m_oCs.m_iTarget;
            const float l_fStagger = l_iT < (int)m_oCs.m_vFoes.size() ? m_oCs.m_vFoes[(size_t)l_iT].m_fStaggerDamage : 1.0f;
            l_oS.m_iHitDmg = (int)std::lround((float)f_NormalHitDamage(l_iT) * l_oD.m_fDamage * l_oS.m_fShare * l_fStagger);
            if (l_oS.m_fShare <= 0.0f) f_PopWord(m_oLocale.f_Tr("battle.move.default.miss"), -1, "move-miss");
        }
        for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oD.m_vFinish, -1, 0)) f_SkillBeat(*l_pB, f_SkillCentre());
        return;
    }
    for (const toms::ShowBeat* l_pB : toms::f_BeatsDue(l_oD.m_vFinish, l_iBefore, l_oS.m_iPhaseMs)) f_SkillBeat(*l_pB, f_SkillCentre());
    if (l_oS.m_iHitsLeft > 0) {
        l_oS.m_iHitMs -= l_iDtMs;
        while (l_oS.m_iHitMs <= 0 && l_oS.m_iHitsLeft > 0 && m_oCs.m_oSkill.m_bOn) {
            l_oS.m_iHitsLeft--;
            l_oS.m_iHitMs += l_oS.m_iHitGapMs;
            const int l_iT = m_oCs.m_iTarget;
            if (l_iT < 0 || l_iT >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iT].f_Down()) { l_oS.m_iHitsLeft = 0; break; }
            f_BattleEvent("hit", -1, l_iT, l_oS.m_iHitDmg, "skill");
            f_PopDamage(l_oS.m_iHitDmg, l_iT, "perfect");
            f_FeelMoment("hit");
            if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx(true)) f_PlayBattleFx(l_pFx->m_strHit, l_iT);
            f_DamageFoe(l_iT, l_oS.m_iHitDmg);
            if (m_oCs.m_vFoes[(size_t)l_iT].f_Down()) {   // it fell: the rest of the show is cut short
                const int l_iDmg = l_oS.m_iHitDmg;
                f_EndSkill();   // (resets cs.skill, so s with it)
                f_AfterPlayerHit(std::string(), f_TrParam(m_oLocale.f_Tr("battle.crit_win"), "dmg", std::to_string(l_iDmg)));
                return;
            }
        }
    }
    if (m_oCs.m_oSkill.m_bOn && l_oS.m_iPhaseMs >= toms::f_ShowLength(l_oD.m_vFinish) && l_oS.m_iHitsLeft <= 0) f_EndSkill();
}

void Game::f_EndSkill() {
    const CombatState::SkillRun l_oS = m_oCs.m_oSkill;
    m_oCs.m_oSkill = CombatState::SkillRun{};
    m_oCs.m_iDimMs = 0;
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
    const nlohmann::json l_jTr = m_jBattleJson.value("training", nlohmann::json::object());
    const int l_iPerDay = std::max(0, l_jTr.value("runsPerDay", 3));
    const int l_iUsed = m_oMeta.m_strTrainDay == f_TodayString() ? m_oMeta.m_iTrainRuns : 0;
    return std::max(0, l_iPerDay - l_iUsed);
}

bool Game::f_StartTraining(const std::string& l_strMonster) {
    const nlohmann::json l_jTr = m_jBattleJson.value("training", nlohmann::json::object());
    const int l_iCost = std::max(0, l_jTr.value("cards", 1));
    auto l_itIt = m_oMeta.m_mapCards.find(l_strMonster);
    EnemyInst l_oProbe;
    if (l_itIt == m_oMeta.m_mapCards.end() || l_itIt->second.m_iCopies < l_iCost || f_TrainingRunsLeft() <= 0 ||
        !f_EnemyFromData(l_strMonster, nlohmann::json::object(), l_oProbe)) {
        m_oAudio.f_Play("deny");
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
    m_oArena.m_iFightsMax = std::max(1, l_jTr.value("fights", 3));
    m_oArena.m_iHpBefore = l_iHp;
    f_ArenaNextFight();
    m_oCs.m_strLog = f_TrParam(f_TrParam(m_oLocale.f_Tr("training.start"), "name", l_oProbe.m_strName), "n", std::to_string(m_oArena.m_iFightsMax));
    return true;
}

void Game::f_EndTraining() {
    const Arena l_oA = m_oArena;
    m_oArena = Arena{};
    m_oCs = CombatState{};
    m_oPl.m_iHp = l_oA.m_iHpBefore;
    f_ShowNotice(f_TrParam(f_TrParam(f_TrParam(m_oLocale.f_Tr("training.done"), "w", std::to_string(l_oA.m_iWins)), "exp", std::to_string(l_oA.m_iExpGained)),
                       "uses", std::to_string(l_oA.m_iWeaponExpGained)));
    f_MarkProgressDirty();
}

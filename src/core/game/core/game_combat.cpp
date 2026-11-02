// game_combat.cpp — battle logic: start/finish, and Battle v2's action resolution
// (tap-to-freeze attack/defense bars, the enemy clock, super attack). Split out of game.cpp 2026-09-13.
#include "game_internal.h"

#include <limits>

using namespace toms::game_detail;

void Game::f_StartCombat(const EnemyInst& l_oE, const std::vector<EnemyInst>& l_vParty) {
    // the lead (the map's monster) and its party, up to three (docs/gameplay/13 §4.6)
    m_oCs.m_vFoes.clear();
    m_oCs.m_vFoes.reserve(toms::g_iMaxFoes);   // a summon never moves them (references stay good)
    auto l_fnAdd = [this](const EnemyInst& l_oM) {
        CombatState::Foe l_oF;
        l_oF.m_oEnemy = l_oM;
        if (l_oF.m_oEnemy.m_strMapId.empty()) l_oF.m_oEnemy.m_strMapId = l_oM.m_strId;
        l_oF.m_iHp = l_oM.m_iHp;
        l_oF.m_vEvents = toms::f_MonsterEventsFromJson(l_oM.m_jEvents);
        m_oCs.m_vFoes.push_back(l_oF);
    };
    l_fnAdd(l_oE);
    for (const EnemyInst& l_oM : l_vParty)
        if ((int)m_oCs.m_vFoes.size() < toms::g_iMaxFoes) l_fnAdd(l_oM);
    // the party's clocks start a little apart, so they do not all attack at the same moment
    for (size_t l_nI = 1; l_nI < m_oCs.m_vFoes.size(); l_nI++) m_oCs.m_vFoes[l_nI].m_iClockMs = -700 * (int)l_nI;
    m_oCs.m_iTarget = 0;
    m_oCs.m_vEvents.clear();
    m_oCs.m_iClockMs = 0;
    // the revamp (docs/gameplay/15): no moment running, a fresh chain and grade, the guard unpressed
    m_oCs.m_iHitStopMs = m_oCs.m_iSlowMs = m_oCs.m_iShakeMs = m_oCs.m_iDimMs = m_oCs.m_iBannerMs = 0;
    m_oCs.m_fSlowScale = 1.0f; m_oCs.m_fSlowCarry = 0.0f; m_oCs.m_bWinPending = false; m_oCs.m_fShakePx = 0.0f;
    m_oCs.m_strBanner.clear(); m_oCs.m_strBannerKind.clear();
    m_oCs.m_iChain = m_oCs.m_iBestChain = m_oCs.m_iPerfects = m_oCs.m_iAttacksDone = m_oCs.m_iDamageTaken = 0;
    m_oCs.m_iGuardAtMs = -1; m_oCs.m_iGuardCdMs = 0; m_oCs.m_bUltimate = false;
    m_oCs.m_iGuardLockMs = 0; m_oCs.m_iPlayDealt = 0; m_oCs.m_oSkill = CombatState::SkillRun{};
    m_oCs.m_fCamZoom = 1.0f; m_oCs.m_iCamMs = m_oCs.m_iCamMsMax = 0; m_oCs.m_iCamOn = -1;
    m_oCs.m_oResult = CombatState::Result{};
    m_oCs.m_ePace = f_EffectivePace();
    m_oCs.m_fStamina = (float)m_oPaceSettings.m_iStaminaMax;
    m_oMoveRng.seed(20261025u);   // the same battle goes the same way
    m_oPlayRng.seed(m_iScenarioTestFrames > 0 ? 20261031u : std::random_device{}());   // the plays: different every battle
    m_oCs.m_iPlayerHP = m_oPl.m_iHp; m_oCs.m_bActive = true; m_oCs.m_bWon = false;
    m_oCs.m_oAtkBar = CombatState::AutoBar{};
    m_oCs.m_oDefBar = CombatState::AutoBar{};
    m_oCs.m_oCombo = toms::ComboPlay{};
    m_oCs.m_oTouch = toms::TouchPlay{};
    m_bBattleBoxesKnown = false;   // the plays wait for this battle's layout (the screen reports it a frame later)
    m_oCs.m_fTouchCharge = 0.0f;
    m_oCs.m_bTouchPaid = false;
    m_bTouchHeld = false;
    m_oCs.m_iChargeMs = 0;
    m_oCs.m_bShieldBanked = false; m_oCs.m_fShieldPower = 0.0f;
    m_oCs.m_iSuperCharge = 0;
    m_oCs.m_mapActiveRuntime = toms::f_MakeBattleRuntime(m_mapActiveDefs);
    m_oCs.m_oActiveStatus = toms::ActiveStatusEffects{};
    m_oCs.m_bNextAttackGuaranteedCrit = false;
    m_oCs.m_fNextAttackCritDamageMult = 1.0f;
    m_oCs.m_bSurvivalArmed = false;
    m_oCs.m_iResultPauseMs = 0;
    m_oCs.m_iAttackPauseMs = 0;
    m_oCs.m_vPopups.clear();
    m_oBattleTimers.f_Reset();   // a number still waiting from the last fight never lands in this one
    m_vBattleFx.clear();
    m_uBattleFxSeed = 0;
    m_vFoeRigs.clear();        // the new monsters start idle (updateMonsterRig)
    m_vFoeRigs.resize(m_oCs.m_vFoes.size());
    m_oCs.m_strLog = m_oLocale.f_Tr("battle.start");
    std::string l_strIds;
    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_strIds += (l_strIds.empty() ? "" : " + ") + l_oF.m_oEnemy.m_strId;
    f_BattleEvent("start", -1, -1, (int)m_oCs.m_vFoes.size(), l_strIds + " (" + toms::f_PaceName(m_oCs.m_ePace) + ")");
    for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) f_ChooseFoeMove((int)l_nI);
    // the monsters' first events: before the battle (its first line), then its start (docs/gameplay/13 §4.4)
    const std::string l_strStartLog = m_oCs.m_strLog;
    for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size() && m_oCs.m_bActive; l_nI++) {
        toms::EventCheck l_oC;
        l_oC.m_strOn = "beforeBattle";
        f_RunFoeEvents((int)l_nI, l_oC);
    }
    if (m_oCs.m_strLog != l_strStartLog) m_oCs.m_iAttackPauseMs = std::max(m_oCs.m_iAttackPauseMs, 1200);   // a line to read before it starts
    for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size() && m_oCs.m_bActive; l_nI++) {
        toms::EventCheck l_oC;
        l_oC.m_strOn = "battleStart";
        f_RunFoeEvents((int)l_nI, l_oC);
    }
}

// ---- the battle's rules (docs/gameplay/13; battle_rules.h) ----

// ---- the revamp: moments, words, the banner, the win (docs/gameplay/15_BATTLE_REVAMP.md) ----

// The battle's time this frame: a hit stop freezes it, the last hit's slow motion slows it (then the win). The
// screen's effects (shake, dim, banner, the guard's wait) run on real time.
int Game::f_BattleFrame(int l_iDtMs) {
    m_oCs.m_iShakeMs = std::max(0, m_oCs.m_iShakeMs - l_iDtMs);
    m_oCs.m_iDimMs = std::max(0, m_oCs.m_iDimMs - l_iDtMs);
    m_oCs.m_iBannerMs = std::max(0, m_oCs.m_iBannerMs - l_iDtMs);
    m_oCs.m_iGuardCdMs = std::max(0, m_oCs.m_iGuardCdMs - l_iDtMs);
    m_oCs.m_iGuardLockMs = std::max(0, m_oCs.m_iGuardLockMs - l_iDtMs);
    m_oCs.m_iCamMs = std::max(0, m_oCs.m_iCamMs - l_iDtMs);
    int l_iBdt = l_iDtMs;
    if (m_oCs.m_iHitStopMs > 0) {
        m_oCs.m_iHitStopMs = std::max(0, m_oCs.m_iHitStopMs - l_iDtMs);
        l_iBdt = 0;
    } else if (m_oCs.m_iSlowMs > 0) {
        m_oCs.m_iSlowMs -= l_iDtMs;
        m_oCs.m_fSlowCarry += l_iDtMs * m_oCs.m_fSlowScale;
        l_iBdt = (int)m_oCs.m_fSlowCarry;
        m_oCs.m_fSlowCarry -= (float)l_iBdt;
        if (m_oCs.m_iSlowMs <= 0) {
            m_oCs.m_iSlowMs = 0;
            if (m_oCs.m_bWinPending) { m_oCs.m_bWinPending = false; f_FinishCombatWin(); }
        }
    }
    m_oCs.m_iFrameDt = l_iBdt;
    return l_iBdt;
}

void Game::f_FeelMoment(const std::string& l_strM) {
    const bool l_bReduced = m_oSettings.m_bReduceEffects;
    m_oCs.m_iHitStopMs = std::max(m_oCs.m_iHitStopMs, m_oFeel.f_Stop(l_strM, l_bReduced));
    const float l_fPx = m_oFeel.f_Shake(l_strM, l_bReduced);
    const float l_fNow = m_oCs.m_iShakeMs > 0 ? m_oCs.m_fShakePx * (float)m_oCs.m_iShakeMs / (float)m_oCs.m_iShakeMsMax : 0.0f;
    if (l_fPx > l_fNow) { m_oCs.m_fShakePx = l_fPx; m_oCs.m_iShakeMs = m_oCs.m_iShakeMsMax = std::max(1, m_oFeel.m_iShakeMs); }
    const std::string l_strS = m_oFeel.f_Sound(l_strM);
    if (!l_strS.empty()) m_oAudio.f_Play(l_strS);
}

void Game::f_PopWord(const std::string& l_strText, int l_iWho, const std::string& l_strStyle) {
    CombatState::Popup l_oP;
    l_oP.m_strText = l_strText; l_oP.m_iWho = l_iWho; l_oP.m_strStyle = l_strStyle; l_oP.m_iLift = 46;
    l_oP.m_oRise = toms::Tween(0.0f, 1.0f, 1100, toms::Ease::OutCubic);
    m_oCs.m_vPopups.push_back(l_oP);
}

void Game::f_ShowBanner(const std::string& l_strText, const std::string& l_strKind, int l_iMs) {
    m_oCs.m_strBanner = l_strText; m_oCs.m_strBannerKind = l_strKind;
    m_oCs.m_iBannerMs = m_oCs.m_iBannerMsMax = std::max(1, l_iMs);
}

// The last monster fell (15 §3.8): the moment plays in slow motion, then the win and its result screen.
void Game::f_StartWinMoment() {
    const int l_iMs = m_oSettings.m_bReduceEffects ? 0 : (int)std::lround(m_oFeel.m_iSlowMoMs * std::min(1.0f, m_oFeel.m_fStrength));
    if (l_iMs <= 0 || m_oArena.m_bOn) { f_FinishCombatWin(); return; }   // (the arena: the next fight at once, as before)
    m_oCs.m_bWinPending = true;
    m_oCs.m_iSlowMs = l_iMs;
    m_oCs.m_fSlowScale = m_oFeel.m_fSlowMoScale;
    m_oCs.m_fSlowCarry = 0.0f;
}

void Game::f_SetReduceEffects(bool l_bOn) {
    m_oSettings.m_bReduceEffects = l_bOn;
    toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
    toms::f_SaveGameSettings(toms::f_SettingsPath(), m_oSettings);
}

toms::Pace Game::f_EffectivePace() const {
    toms::Pace l_eP = m_oPaceSettings.m_ePace;
    l_eP = toms::f_PaceFromString(f_WeaponPlayOf(f_WeaponKey()).m_strPace, l_eP);
    return toms::f_PaceFromString(m_strPaceOverride, l_eP);
}

void Game::f_SetBattlePace(const std::string& l_strPace) {
    m_strPaceOverride = l_strPace;
    if (!m_oCs.m_bActive) return;
    const toms::Pace l_eP = f_EffectivePace();
    if (l_eP == m_oCs.m_ePace) return;
    m_oCs.m_ePace = l_eP;
    m_oCs.m_oAtkBar.m_bCooling = m_oCs.m_oDefBar.m_bCooling = false;   // switching: no wait carried over
    m_oCs.m_fStamina = (float)m_oPaceSettings.m_iStaminaMax;
    f_BattleEvent("pace", -1, -1, 0, toms::f_PaceName(l_eP));
}

int Game::f_FoeBreakPoints(int l_iFoe) const {
    const int l_iOwn = m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_iBreakPoints;
    return l_iOwn > 0 ? l_iOwn : m_oBreakSettings.m_iPoints;
}

std::string Game::f_FoeLogName(int l_iFoe) const {
    return l_iFoe >= 0 && l_iFoe < (int)m_oCs.m_vFoes.size() ? m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strName : std::string();
}

float Game::f_PlayerHpPct() const {
    return 100.0f * (float)std::max(0, m_oCs.m_iPlayerHP) / (float)std::max(1, f_EffectiveMaxHp());
}

void Game::f_ChooseFoeMove(int l_iFoe) {
    CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
    const float l_fRoll = std::uniform_real_distribution<float>(0.0f, 1.0f)(m_oMoveRng);
    l_oF.m_strMove = toms::f_PickMove(l_oF.m_oEnemy.m_vMoves, l_fRoll);
    const toms::MoveDef& l_oM = m_oMoves.f_Get(l_oF.m_strMove);
    l_oF.m_iMoveMs = l_oM.m_iChargeMs > 0 ? std::max(300, (int)std::lround(l_oM.m_iChargeMs * l_oF.m_fIntervalMul))   // its wind-up in ms (16 §4.1)
                              : std::max(300, (int)std::lround(std::max(500, l_oF.m_oEnemy.m_iAtkIntervalMs) * l_oM.m_fTime * l_oF.m_fIntervalMul));
    if (!l_oF.m_oEnemy.m_vMoves.empty()) f_BattleEvent("intent", l_iFoe, -1, l_oF.m_iMoveMs, l_oF.m_strMove);   // (a plain striker: always the same)
}

void Game::f_StepFoes(int l_iDtMs) {
    for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size() && m_oCs.m_bActive; l_nI++) {
        const int l_iFoe = (int)l_nI;
        {
            toms::EventCheck l_oC;   // timed events run on battle time
            l_oC.m_strOn = "everyMs";
            l_oC.m_iMsBefore = m_oCs.m_iClockMs - l_iDtMs;
            l_oC.m_iMsAfter = m_oCs.m_iClockMs;
            if (!m_oCs.m_vFoes[l_nI].f_Down()) f_RunFoeEvents(l_iFoe, l_oC);
        }
        if (!m_oCs.m_bActive) return;
        CombatState::Foe& l_oF = m_oCs.m_vFoes[l_nI];
        if (l_oF.f_Down()) continue;
        if (l_oF.m_iStaggerMs > 0) {   // stunned: its clock stops
            l_oF.m_iStaggerMs -= l_iDtMs;
            if (l_oF.m_iStaggerMs <= 0) { l_oF.m_iStaggerMs = 0; l_oF.m_fStaggerDamage = 1.0f; l_oF.m_bBroken = false; }
            continue;
        }
        // the owner 2026-10-26: they keep charging while the player's combo runs (their attack lands in the middle of
        // it; quick jabs also cut it short). data/battle.json "combo_holds_monsters": true brings back the old wait.
        if (l_oF.m_iVolleyHit > 0) {   // a run of hits (16 §4.1): the next one after its gap; the clock waits
            l_oF.m_iVolleyMs -= l_iDtMs;
            if (l_oF.m_iVolleyMs <= 0) f_ResolveEnemyClockFire(l_iFoe);
            continue;
        }
        if (m_bComboHoldsMonsters && f_AttackRunning() && m_oMoves.f_Get(l_oF.m_strMove).m_eKind != toms::MoveKind::Jabs) continue;
        l_oF.m_iClockMs += l_iDtMs;
        if (l_oF.m_iClockMs >= l_oF.m_iMoveMs) {
            l_oF.m_iClockMs = 0;
            f_ResolveEnemyClockFire(l_iFoe);
        }
    }
}

void Game::f_StunFoe(int l_iFoe, int l_iMs, float l_fDamage, bool l_bBroken) {
    if (l_iMs <= 0) return;
    CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
    l_oF.m_iStaggerMs = std::max(l_oF.m_iStaggerMs, l_iMs);
    l_oF.m_fStaggerDamage = std::max(l_oF.m_fStaggerDamage, l_fDamage);
    l_oF.m_bBroken = l_oF.m_bBroken || l_bBroken;
    f_BattleEvent(l_bBroken ? "break" : "stagger", l_iFoe, -1, l_iMs);
    f_FoeLook(l_iFoe, l_bBroken ? "broken" : "parry");
    if (l_bBroken) {   // the break is a moment (15 §3.4): the banner, the screen dims, a stop and a shake
        f_ShowBanner(m_oLocale.f_Tr("battle.word_break"), "break", 1300);
        m_oCs.m_iDimMs = m_oSettings.m_bReduceEffects ? 0 : 700;
        f_FeelMoment("break");
    }
}

void Game::f_DamageFoe(int l_iFoe, int l_iDmg) {
    CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
    const float l_fMaxHp = (float)std::max(1, l_oF.m_oEnemy.m_iHp);
    const float l_fBefore = 100.0f * (float)std::max(0, l_oF.m_iHp) / l_fMaxHp;
    l_oF.m_iHp -= l_iDmg;
    toms::EventCheck l_oC;
    l_oC.m_strOn = "hpBelow";
    l_oC.m_fHpPctBefore = l_fBefore;
    l_oC.m_fHpPctAfter = 100.0f * (float)std::max(0, m_oCs.m_vFoes[(size_t)l_iFoe].m_iHp) / l_fMaxHp;
    f_RunFoeEvents(l_iFoe, l_oC);
    if (m_oCs.m_bActive && m_oCs.m_vFoes[(size_t)l_iFoe].m_iHp <= 0) {   // its last words, or a heal that stands it up again
        toms::EventCheck l_oD;
        l_oD.m_strOn = "falls";
        f_RunFoeEvents(l_iFoe, l_oD);
    }
}

void Game::f_RunFoeEvents(int l_iFoe, const toms::EventCheck& l_oCheck) {
    if (l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size()) return;
    std::vector<toms::MonsterEvent>& l_vEvs = m_oCs.m_vFoes[(size_t)l_iFoe].m_vEvents;
    std::vector<nlohmann::json> l_vTodo;   // copied: an action may replace the monster and its events
    for (int l_iIdx : toms::f_FireMonsterEvents(l_vEvs, l_oCheck)) {
        f_BattleEvent("event", l_iFoe, -1, l_iIdx, l_oCheck.m_strOn);
        l_vTodo.push_back(l_vEvs[(size_t)l_iIdx].m_jActions);
    }
    for (const nlohmann::json& l_jA : l_vTodo) {
        if (!m_oCs.m_bActive) return;
        f_RunFoeActions(l_iFoe, l_jA);
    }
}

void Game::f_RunFoeActions(int l_iFoe, const nlohmann::json& l_jActions) {
    if (!l_jActions.is_array()) return;
    bool l_bFled = false;
    for (const nlohmann::json& l_jA : l_jActions) {
        if (!l_jA.is_object() || !m_oCs.m_bActive) continue;
        CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
        if (l_jA.contains("say") && l_jA["say"].is_string()) {
            const std::string l_strS = l_jA["say"].get<std::string>();
            m_oCs.m_strLog = m_oLocale.f_Has(l_strS) ? m_oLocale.f_Tr(l_strS) : l_strS;
            f_ShowBanner(m_oCs.m_strLog, "line", 2000);   // a phase change is a moment too (15 §3.5)
            f_FeelMoment("phase");
            f_BattleEvent("say", l_iFoe, -1, 0, l_strS);
        }
        if (l_jA.contains("stat") && l_jA["stat"].is_string()) {
            const std::string l_strSt = l_jA["stat"].get<std::string>();
            const float l_fMul = l_jA.value("mul", 1.0f);
            const int l_iAdd = l_jA.value("add", 0);
            if (l_strSt == "atk") { l_oF.m_fAtkMul *= l_fMul; l_oF.m_oEnemy.m_iAtk += l_iAdd; }
            else if (l_strSt == "def") { l_oF.m_fDefMul *= l_fMul; l_oF.m_oEnemy.m_iDef += l_iAdd; }
            else if (l_strSt == "interval") { l_oF.m_fIntervalMul *= l_fMul; l_oF.m_oEnemy.m_iAtkIntervalMs += l_iAdd; }
            f_BattleEvent("stat", l_iFoe, -1, l_iAdd, l_strSt + " x" + std::to_string(l_fMul).substr(0, 4));
        }
        if (l_jA.contains("transformInto") && l_jA["transformInto"].is_string()) {
            EnemyInst l_oN;
            const std::string l_strId = l_jA["transformInto"].get<std::string>();
            if (f_EnemyFromData(l_strId, nlohmann::json::object(), l_oN)) {
                const std::string l_strFrom = l_oF.m_oEnemy.m_strName;
                const float l_fShare = (float)std::max(0, l_oF.m_iHp) / (float)std::max(1, l_oF.m_oEnemy.m_iHp);
                l_oN.m_iX = l_oF.m_oEnemy.m_iX; l_oN.m_iY = l_oF.m_oEnemy.m_iY; l_oN.m_strMapId = l_oF.m_oEnemy.m_strMapId;
                l_oF.m_iHp = l_jA.value("hp", std::string("keepShare")) == "full" ? l_oN.m_iHp
                                                                        : std::max(1, (int)std::ceil(l_fShare * (float)l_oN.m_iHp));
                l_oF.m_oEnemy = l_oN;
                l_oF.m_vEvents = toms::f_MonsterEventsFromJson(l_oN.m_jEvents);
                l_oF.m_fAtkMul = l_oF.m_fDefMul = l_oF.m_fIntervalMul = 1.0f;
                l_oF.m_fBreakGauge = 0.0f;
                l_oF.m_iClockMs = 0;
                f_ChooseFoeMove(l_iFoe);
                if (l_iFoe < (int)m_vFoeRigs.size()) m_vFoeRigs[(size_t)l_iFoe].m_strState.clear();   // the new monster's clips
                f_FoeLook(l_iFoe, "transform");
                m_oCs.m_strLog = f_TrParam(f_TrParam(m_oLocale.f_Tr("battle.transform"), "name", l_strFrom), "to", l_oN.m_strName);
                f_BattleEvent("transform", l_iFoe, -1, l_oF.m_iHp, l_strId);
            }
        }
        if (l_jA.contains("heal")) {
            const int l_iHeal = (int)std::lround(l_oF.m_oEnemy.m_iHp * l_jA.value("heal", 0.0f) / 100.0f);
            l_oF.m_iHp = std::min(l_oF.m_oEnemy.m_iHp, std::max(0, l_oF.m_iHp) + l_iHeal);
            f_BattleEvent("heal", l_iFoe, -1, l_iHeal);
        }
        if (l_jA.contains("summon") && l_jA["summon"].is_string() && (int)m_oCs.m_vFoes.size() < toms::g_iMaxFoes) {
            EnemyInst l_oN;
            if (f_EnemyFromData(l_jA["summon"].get<std::string>(), nlohmann::json::object(), l_oN)) {
                CombatState::Foe l_oS;
                l_oS.m_oEnemy = l_oN;
                l_oS.m_iHp = l_oN.m_iHp;
                l_oS.m_vEvents = toms::f_MonsterEventsFromJson(l_oN.m_jEvents);
                m_oCs.m_vFoes.push_back(l_oS);
                m_vFoeRigs.resize(m_oCs.m_vFoes.size());
                f_ChooseFoeMove((int)m_oCs.m_vFoes.size() - 1);
                m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.summon"), "name", l_oN.m_strName);
                f_BattleEvent("summon", l_iFoe, (int)m_oCs.m_vFoes.size() - 1, 0, l_oN.m_strId);
            }
        }
        if (l_jA.value("flee", false)) {
            CombatState::Foe& l_oG = m_oCs.m_vFoes[(size_t)l_iFoe];
            l_oG.m_bFled = true;
            l_oG.m_iHp = 0;
            l_bFled = true;
            m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.flee"), "name", l_oG.m_oEnemy.m_strName);
            f_BattleEvent("flee", l_iFoe, -1);
        }
        if (l_jA.contains("anim") && l_jA["anim"].is_string()) f_FoeLook(l_iFoe, l_jA["anim"].get<std::string>());
        if (l_jA.contains("fx") && l_jA["fx"].is_string()) f_PlayBattleFx(l_jA["fx"].get<std::string>(), l_iFoe);
        if (l_jA.contains("sound") && l_jA["sound"].is_string()) m_oAudio.f_Play(l_jA["sound"].get<std::string>());
        if (l_jA.contains("flag") && l_jA["flag"].is_string()) toms::f_SetStoryFlag(m_oMeta, l_jA["flag"].get<std::string>());
        if (l_jA.contains("dialogue"))   // (not yet: a dialogue in the middle of a battle)
            std::fprintf(stderr, "[battle] %s: a \"dialogue\" action is not supported yet; use \"say\"\n",
                         m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strId.c_str());
    }
    if (l_bFled && m_oCs.m_bActive) {   // it left: the battle may be over, or the target moves on
        if (toms::f_AllDown(m_oCs.f_FoeHp())) f_FinishCombatWin();
        else if (m_oCs.m_vFoes[(size_t)m_oCs.m_iTarget].f_Down()) m_oCs.m_iTarget = toms::f_NextStanding(m_oCs.f_FoeHp(), m_oCs.m_iTarget, +1);
    }
}

void Game::f_DebugForceWin() {
    if (!m_oCs.m_bActive) return;
    for (CombatState::Foe& l_oF : m_oCs.m_vFoes) l_oF.m_iHp = std::min(l_oF.m_iHp, 0);
    f_BattleEvent("win", -1, -1, 0, "by hand");
    f_FinishCombatWin();
}

void Game::f_DebugUpdateMonster(const std::string& l_strId, const nlohmann::json& l_jM) {
    if (!m_oCs.m_bActive || !l_jM.is_object()) return;
    for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) {
        CombatState::Foe& l_oF = m_oCs.m_vFoes[l_nI];
        if (l_oF.m_oEnemy.m_strId != l_strId || l_oF.f_Down()) continue;
        EnemyInst& l_oE = l_oF.m_oEnemy;
        if (l_jM.contains("name")) l_oE.m_strName = m_oLocale.f_Field(l_jM["name"]);
        const int l_iMaxHp = std::max(1, l_jM.value("hp", l_oE.m_iHp));
        if (l_iMaxHp != l_oE.m_iHp) {   // its HP keeps its share of the new top
            l_oF.m_iHp = std::max(1, (int)std::lround((double)l_oF.m_iHp * l_iMaxHp / std::max(1, l_oE.m_iHp)));
            l_oE.m_iHp = l_iMaxHp;
        }
        l_oE.m_iAtk = l_jM.value("atk", l_oE.m_iAtk);
        l_oE.m_iDef = l_jM.value("def", l_oE.m_iDef);
        l_oE.m_iExp = l_jM.value("exp", l_oE.m_iExp);
        l_oE.m_iGold = l_jM.value("gold", l_oE.m_iGold);
        l_oE.m_strLook = l_jM.value("look", std::string("monster"));
        l_oE.m_iBreakPoints = l_jM.value("break", 0);
        l_oE.m_vMoves = toms::f_MoveChoicesFromJson(l_jM.value("moves", nlohmann::json::array()));   // from its next move on
        const int l_iInterval = l_jM.value("atk_interval_ms", 4000);
        if (l_iInterval != l_oE.m_iAtkIntervalMs) {   // the wind-up under way keeps how far along it is
            const double l_dDone = l_oF.m_iMoveMs > 0 ? std::clamp((double)l_oF.m_iClockMs / l_oF.m_iMoveMs, 0.0, 1.0) : 0.0;
            l_oE.m_iAtkIntervalMs = l_iInterval;
            const toms::MoveDef& l_oMv = m_oMoves.f_Get(l_oF.m_strMove);
            l_oF.m_iMoveMs = std::max(300, (int)std::lround(std::max(500, l_oE.m_iAtkIntervalMs) * l_oMv.m_fTime * l_oF.m_fIntervalMul));
            if (l_oF.m_iClockMs >= 0) l_oF.m_iClockMs = (int)std::lround(l_dDone * l_oF.m_iMoveMs);
        }
        const nlohmann::json l_jEv = l_jM.value("events", nlohmann::json::array());
        if (l_jEv != l_oE.m_jEvents) { l_oE.m_jEvents = l_jEv; l_oF.m_vEvents = toms::f_MonsterEventsFromJson(l_jEv); }   // fresh: none fired yet
        f_BattleEvent("edited", (int)l_nI, -1, l_oE.m_iAtkIntervalMs, "by the editor");
    }
}

void Game::f_DebugFireFoeEvent(int l_iFoe, int l_iEvent) {
    if (!m_oCs.m_bActive || l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size()) return;
    const std::vector<toms::MonsterEvent>& l_vEvs = m_oCs.m_vFoes[(size_t)l_iFoe].m_vEvents;
    if (l_iEvent < 0 || l_iEvent >= (int)l_vEvs.size()) return;
    f_BattleEvent("event", l_iFoe, -1, l_iEvent, "by hand");
    const nlohmann::json l_jActions = l_vEvs[(size_t)l_iEvent].m_jActions;
    f_RunFoeActions(l_iFoe, l_jActions);
}

void Game::f_FoeLook(int l_iFoe, const std::string& l_strState) {
    if (l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size()) return;
    if (const toms::LookState* l_pLs = m_oLooks.f_Find(m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strLook, l_strState)) {
        if (!l_pLs->m_strSound.empty()) m_oAudio.f_Play(l_pLs->m_strSound);
        if (!l_pLs->m_strFx.empty()) f_PlayBattleFx(l_pLs->m_strFx, l_iFoe);
    }
    if (l_strState != "idle" && l_iFoe < (int)m_vFoeRigs.size()) m_vFoeRigs[(size_t)l_iFoe].m_strRequest = l_strState;
}

void Game::f_PlayerLook(const std::string& l_strState) {
    if (const toms::LookState* l_pLs = m_oLooks.f_Find("player", l_strState, "player")) {
        if (!l_pLs->m_strSound.empty()) m_oAudio.f_Play(l_pLs->m_strSound);
        if (!l_pLs->m_strFx.empty()) f_PlayBattleFx(l_pLs->m_strFx, -1);
    }
}

void Game::f_InterruptCombo(int l_iFoe) {
    if (!f_AttackRunning()) return;
    m_oCs.m_oCombo = toms::ComboPlay{};
    m_oCs.m_oTouch.f_Stop();
    m_oCs.m_iChargeMs = 0;
    if (m_oCs.m_ePace == toms::Pace::Cooldown) { m_oCs.m_oAtkBar.m_bCooling = true; m_oCs.m_oAtkBar.m_iCooldownMs = f_BarCooldownMs(); }
    if (f_BattleRigReady()) f_SetBattleRigState("miss", true);
    m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.interrupted"), "name", f_FoeLogName(l_iFoe));
    f_BattleEvent("interrupt", l_iFoe, -1);
}

// ---- the battle's log (docs/gameplay/13 §4.5) and its monsters ----

void Game::f_BattleEvent(const std::string& l_strType, int l_iWho, int l_iWhom, int l_iValue, const std::string& l_strNote) {
    toms::BattleEvent l_oE;
    l_oE.m_iMs = m_oCs.m_iClockMs; l_oE.m_strType = l_strType; l_oE.m_iWho = l_iWho; l_oE.m_iWhom = l_iWhom; l_oE.m_iValue = l_iValue; l_oE.m_strNote = l_strNote;
    m_oCs.m_vEvents.push_back(l_oE);
    if (m_bBattleLogToStderr) {
        std::vector<std::string> l_vIds;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_vIds.push_back(l_oF.m_oEnemy.m_strId);
        std::fprintf(stderr, "[battle] %s\n", toms::f_BattleEventLine(l_oE, l_vIds).c_str());
    }
}

void Game::f_BattleSetTarget(int l_iFoe) {
    if (!m_oCs.m_bActive || l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size() || m_oCs.m_vFoes[(size_t)l_iFoe].f_Down() || l_iFoe == m_oCs.m_iTarget) return;
    m_oCs.m_iTarget = l_iFoe;
    f_BattleEvent("target", -1, l_iFoe);
    m_oAudio.f_Play("confirm_click");
}

void Game::f_BattleCycleTarget(int l_iDir) {
    if (!m_oCs.m_bActive || m_oCs.m_vFoes.size() < 2) return;
    const int l_iNext = toms::f_NextStanding(m_oCs.f_FoeHp(), m_oCs.m_iTarget, l_iDir);
    if (l_iNext >= 0) f_BattleSetTarget(l_iNext);
}

// A foe fell (the battle goes on: the caller has checked that some still stand): it plays its die clip and stays down,
// and the target moves to the next one standing.
void Game::f_FoeDown(int l_iFoe) {
    f_BattleEvent("down", l_iFoe, -1, 0, m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strId);
    f_FoeLook(l_iFoe, "die");
    if (l_iFoe == m_oCs.m_iTarget) {
        const int l_iNext = toms::f_NextStanding(m_oCs.f_FoeHp(), l_iFoe, +1);
        if (l_iNext >= 0) { m_oCs.m_iTarget = l_iNext; f_BattleEvent("target", -1, l_iNext); }
    }
}

// ---- the battle rig (Game::battleRigState) ----

void Game::f_LoadBattleRig() {
    if (m_strBattleRigWeapon == m_oEquipped.m_strWeaponId && (m_upPlayerView || m_strBattleRigWeapon.empty())) return;
    m_strBattleRigWeapon = m_oEquipped.m_strWeaponId;
    m_upPlayerView.reset();
    m_strBattleRigState.clear();
    auto l_itIt = m_mapEquipmentDefs.find(m_oEquipped.m_strWeaponId);
    if (l_itIt == m_mapEquipmentDefs.end() || !m_upRes) return;
    toms::FighterLook l_oLook;
    l_oLook.m_strRender = l_itIt->second.m_strBattleRender;
    l_oLook.m_strCharacter = l_itIt->second.m_strBattleCharacterAnim;
    l_oLook.m_strHeld = l_itIt->second.m_strBattleHeldAnim;
    l_oLook.m_strModel = l_itIt->second.m_strBattleModel;
    m_upPlayerView = f_MakeFighterView(l_oLook, [this](const toms::anim::Clip& l_oC) { return f_FitClipToCharacter(l_oC, f_PlayerSprite()); },
                                  32.0f, false);
}

bool Game::f_BattleRigReady() const { return m_upPlayerView != nullptr; }

std::unique_ptr<toms::FighterView> Game::f_MakeFighterView(const toms::FighterLook& l_oLook, toms::AnimFighterView::Fit l_fnFit,
                                                         float l_fUnitsTall, bool l_bBoxSide) {
    if (!m_upRes) return nullptr;
    if (l_oLook.f_Is3d()) {   // the switch is in the data; the 3D view is not built yet (docs/gameplay/18)
        static bool l_bSaid = false;
        if (!l_bSaid) std::fprintf(stderr, "[battle] %s: 3D fighters are not drawn yet; the look's 2D clips are used\n",
                                l_oLook.m_strModel.c_str());
        l_bSaid = true;
    }
    auto l_fnLoad = [this](const std::string& l_strPath) -> std::shared_ptr<toms::res::AnimRes> {
        if (l_strPath.empty()) return nullptr;
        auto l_spA = m_upRes->f_Get<toms::res::AnimRes>(l_strPath);
        if (!l_spA->f_Ok()) { std::fprintf(stderr, "[battle] %s: %s\n", l_strPath.c_str(), l_spA->f_Error().c_str()); return nullptr; }
        return l_spA;
    };
    auto l_spCharacter = l_fnLoad(l_oLook.m_strCharacter), l_spHeld = l_fnLoad(l_oLook.m_strHeld);
    if (!l_spCharacter && !l_spHeld) return nullptr;
    return std::make_unique<toms::AnimFighterView>(l_spCharacter, l_spHeld, std::move(l_fnFit), l_fUnitsTall, l_bBoxSide);
}

void Game::f_SetBattleRigState(const std::string& l_strState, bool l_bRestart, const std::string& l_strClip) {
    if (l_strState == m_strBattleRigState && !l_bRestart) return;
    m_strBattleRigState = l_strState;
    // The weapon's clip for this status (items.json "battleAnim" "clips"; docs/gameplay/07 phase 2), else the status's own name.
    auto l_itDef = m_mapEquipmentDefs.find(m_oEquipped.m_strWeaponId);
    const std::string l_strClipName = !l_strClip.empty() ? l_strClip : l_itDef != m_mapEquipmentDefs.end() ? l_itDef->second.f_BattleClip(l_strState) : l_strState;
    const bool l_bFound = m_upPlayerView && m_upPlayerView->f_Play(l_strClipName, true);
    if (!l_bFound && m_upPlayerView) m_upPlayerView->f_Stop();
    m_strBattleRigClip = l_bFound ? l_strClipName : std::string();   // what plays (a test scenario checks it)
}

void Game::f_UpdateBattleRig(int l_iDtMs) {
    const bool l_bShown = m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0;
    if (!l_bShown) {
        if (!m_strBattleRigState.empty()) {
            if (m_upPlayerView) m_upPlayerView->f_Stop();
            m_strBattleRigState.clear();
            m_strBattleRigClip.clear();
        }
        return;
    }
    f_LoadBattleRig();
    if (!f_BattleRigReady()) return;
    // An attack or a miss plays to its end; then the bar decides: waiting for the tap = charge, recovering = idle.
    const bool l_bShooting = (m_strBattleRigState == "attack" || m_strBattleRigState == "miss") && !m_upPlayerView->f_Finished();
    if (!l_bShooting) f_SetBattleRigState(m_oCs.m_bActive && !m_oCs.m_oAtkBar.m_bCooling ? "charge" : "idle");
    m_upPlayerView->f_Update(l_iDtMs);   // (its events: the battle's own timers place the numbers)
}

void Game::f_DrawBattleRig() {
    // The battle screen's backdrop (common.rcss .focus: black at 80%). The UI draws over the game, so with the backdrop
    // in battle.rml the player and the monster would sit under it, drawn at a fifth of their brightness; it is drawn
    // here instead, under them (battle.rml has none).
    Quad l_oBackdrop;
    l_oBackdrop.m_arrRect[0] = -4096.0f; l_oBackdrop.m_arrRect[1] = -4096.0f; l_oBackdrop.m_arrRect[2] = 16384.0f; l_oBackdrop.m_arrRect[3] = 16384.0f;
    l_oBackdrop.m_arrUv[0] = 0; l_oBackdrop.m_arrUv[1] = 0; l_oBackdrop.m_arrUv[2] = 1; l_oBackdrop.m_arrUv[3] = 1;
    l_oBackdrop.m_bSolid = true;
    l_oBackdrop.m_arrTint[0] = l_oBackdrop.m_arrTint[1] = l_oBackdrop.m_arrTint[2] = 0.0f;
    l_oBackdrop.m_arrTint[3] = std::min(1.0f, 0xcc / 255.0f + 0.15f * (float)m_oCs.m_iDimMs / 700.0f);   // a break dims it a moment
    if (m_pRen) m_pRen->f_DrawSprite(l_oBackdrop);
    // the screen shake (docs/gameplay/15 §3.8): the fighters and their effects move together, fading out
    float l_fOx = 0.0f, l_fOy = 0.0f;
    if (m_oCs.m_iShakeMs > 0 && m_oCs.m_fShakePx > 0.0f) {
        const float l_fAmp = m_oCs.m_fShakePx * (float)m_oCs.m_iShakeMs / (float)m_oCs.m_iShakeMsMax;
        std::uniform_real_distribution<float> l_oU(-l_fAmp, l_fAmp);
        l_fOx = l_oU(m_oShakeRng); l_fOy = l_oU(m_oShakeRng);
    }
    // the camera's close-up (docs/gameplay/16 §6.1): the fighters and their effects scaled about the one it is on
    float l_fZoom = 1.0f;
    glm::vec2 l_v2Focus(0.0f);
    if (m_oCs.m_iCamMs > 0 && m_oCs.m_fCamZoom > 1.0f) {
        const float l_fIn = std::min(1.0f, (float)(m_oCs.m_iCamMsMax - m_oCs.m_iCamMs) / 200.0f), l_fOut = std::min(1.0f, (float)m_oCs.m_iCamMs / 300.0f);
        const float l_fK = std::min(l_fIn, l_fOut);
        l_fZoom = 1.0f + (m_oCs.m_fCamZoom - 1.0f) * l_fK * (2.0f - l_fK);
        const glm::vec4 l_v4Fb = f_FighterBox(m_oCs.m_iCamOn);
        l_v2Focus = {l_v4Fb.x + l_v4Fb.z * 0.5f, l_v4Fb.y + l_v4Fb.w * 0.5f};
    }
    auto l_fnZoomed = [&](glm::vec4 l_v4B) {
        return glm::vec4(l_v2Focus.x + (l_v4B.x - l_v2Focus.x) * l_fZoom + l_fOx, l_v2Focus.y + (l_v4B.y - l_v2Focus.y) * l_fZoom + l_fOy, l_v4B.z * l_fZoom, l_v4B.w * l_fZoom);
    };
    // The player's place on the battle screen (battle.rcss #player, wherever the layout put it: playerBox_) and each
    // monster's (battle.rcss .foe .fbox: foeBox_): the view fills its box, the feet at the bottom centre.
    if (f_BattleRigReady() && !m_strBattleRigState.empty()) m_upPlayerView->f_Draw(m_pRen, l_fnZoomed(m_v4PlayerBox));
    for (size_t l_nI = 0; l_nI < m_vFoeRigs.size() && l_nI < m_oCs.m_vFoes.size(); l_nI++) {
        const FoeRig& l_oFr = m_vFoeRigs[l_nI];
        if (l_oFr.m_upView && !l_oFr.m_strState.empty()) l_oFr.m_upView->f_Draw(m_pRen, l_fnZoomed(f_FighterBox((int)l_nI)));
    }
    if (m_spBattleFxFile) {
        glm::mat3 l_mat3Shake(1.0f);   // (and the close-up's scale)
        l_mat3Shake[0][0] = l_mat3Shake[1][1] = l_fZoom;
        l_mat3Shake[2][0] = l_v2Focus.x * (1.0f - l_fZoom) + l_fOx; l_mat3Shake[2][1] = l_v2Focus.y * (1.0f - l_fZoom) + l_fOy;
        for (BattleFx& l_oB : m_vBattleFx)
            if (!l_oB.m_bOverUi) l_oB.m_oFx.f_Draw(m_pRen, m_spBattleFxFile->f_Atlases(), l_mat3Shake, nullptr);
        m_pRen->f_SetLayer(1);   // the track's hints: over the battle screen's UI
        for (BattleFx& l_oB : m_vBattleFx)
            if (l_oB.m_bOverUi) l_oB.m_oFx.f_Draw(m_pRen, m_spBattleFxFile->f_Atlases(), glm::mat3(1.0f), nullptr);
        m_pRen->f_SetLayer(0);
    }
}

// ---- the battle's particles (Game::battleFx_) ----

const toms::WeaponFx* Game::f_BattleWeaponFx(bool l_bTop) const {
    const std::string l_strKey = f_WeaponKey();
    const int l_iLv = l_bTop ? std::numeric_limits<int>::max() : f_WeaponLevel(l_strKey);
    if (const toms::WeaponFx* l_pF = toms::f_WeaponFxEntry(f_WeaponPlayOf(l_strKey), l_iLv)) return l_pF;
    return toms::f_WeaponFxEntry(m_oWeaponPlay.f_Get("default"), l_iLv);   // a weapon without its own: the default's
}

// One-shot, at the player's weapon (a little ahead of the character, in its box) or a monster's middle.
void Game::f_PlayBattleFx(const std::string& l_strEffect, int l_iWho, int l_iDelayMs) {
    const glm::vec4 l_v4B = f_FighterBox(l_iWho);
    if (l_iWho >= 0) f_PlayBattleFxAt(l_strEffect, l_v4B.x + l_v4B.z * 0.5f, l_v4B.y + l_v4B.w * 0.5f, false, l_iDelayMs);
    else f_PlayBattleFxAt(l_strEffect, l_v4B.x + l_v4B.z * 0.731f, l_v4B.y + l_v4B.w * 0.493f, false, l_iDelayMs);
}

void Game::f_PlayBattleFxAt(const std::string& l_strEffect, float l_fX, float l_fY, bool l_bOverUi, int l_iDelayMs) {
    if (l_strEffect.empty()) return;
    if (!m_spBattleFxFile && !m_bBattleFxTried && m_upRes) {   // once: without the file the battle has no particles
        m_bBattleFxTried = true;
        auto l_spF = m_upRes->f_Get<toms::res::ParticleRes>(m_pszBattleFxFile);
        if (l_spF->f_Ok()) m_spBattleFxFile = l_spF;
        else std::fprintf(stderr, "[battle] %s: %s\n", m_pszBattleFxFile, l_spF->f_Error().c_str());
    }
    if (!m_spBattleFxFile) return;
    const toms::fx::Effect* l_pE = m_spBattleFxFile->f_Find(l_strEffect);
    if (!l_pE) { std::fprintf(stderr, "[battle] %s has no effect '%s'\n", m_pszBattleFxFile, l_strEffect.c_str()); return; }
    auto l_fnStart = [this, l_pE, l_fX, l_fY, l_bOverUi] {
        BattleFx l_oB;
        l_oB.m_bOverUi = l_bOverUi;
        l_oB.m_oFx.f_SetTransform(toms::anim::f_Placement(l_fX, l_fY));
        if (!l_bOverUi) l_oB.m_oFx.f_SetGpuSimulation(m_pRen, f_ParticleGpuThreshold());   // the top layer draws quads only
        l_oB.m_oFx.f_Play(l_pE, ++m_uBattleFxSeed);
        m_vBattleFx.push_back(std::move(l_oB));
    };
    if (l_iDelayMs > 0) m_oTimers.f_After(m_oBattleTimers, l_iDelayMs, l_fnStart);   // dies with the battle, as the damage numbers
    else l_fnStart();
}

void Game::f_UpdateBattleFx(int l_iDtMs) {
    if (!(m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0)) { m_vBattleFx.clear(); return; }
    for (BattleFx& l_oB : m_vBattleFx) l_oB.m_oFx.f_Update(l_iDtMs / 1000.0f);
    m_vBattleFx.erase(std::remove_if(m_vBattleFx.begin(), m_vBattleFx.end(), [](const BattleFx& l_oB) { return l_oB.m_oFx.f_Finished(); }),
                    m_vBattleFx.end());
}

// The marker came into a point's window (hit = false): a sound and a flash on the point, "tap now". A point hit: a
// burst on it. On the track's points as the UI laid them out (setComboPointsOnScreen).
void Game::f_ComboHint(int l_iPoint, bool l_bHit) {
    if (!l_bHit) m_oAudio.f_Play("combo_ready");
    if (l_iPoint < 0 || l_iPoint >= (int)m_vComboPointsOnScreen.size()) return;
    const glm::vec2 l_v2P = m_vComboPointsOnScreen[(size_t)l_iPoint];
    f_PlayBattleFxAt(l_bHit ? "combo_point_hit" : "combo_ready", l_v2P.x, l_v2P.y, /*overUi=*/true);
}

// ---- the monsters on the battle screen (Game::foeRigs_) ----

std::string Game::f_MonsterSprite(int l_iFoe) const {
    if (l_iFoe < 0 || l_iFoe >= (int)m_oCs.m_vFoes.size()) return std::string();
    const EnemyInst& l_oE = m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy;
    return l_oE.m_bBoss ? std::string("boss_demonlord") : f_TypeSprite("monster", l_oE.m_strId);
}

toms::FighterLook Game::f_MonsterLook(int l_iFoe) const {
    // The look's (looks.json "render" / "character" / "model", following "base"), else the shared monster clips.
    toms::FighterLook l_oLook;
    l_oLook.m_strCharacter = m_pszMonsterBattleAnim;
    const std::string& l_strOwn = m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strLook;
    std::string l_strName = l_strOwn.empty() ? std::string("monster") : l_strOwn;
    bool l_bCharacter = false, l_bRender = false, l_bModel = false;
    for (int l_iStep = 0; l_iStep < 6 && !l_strName.empty(); l_iStep++) {
        auto l_itIt = m_oLooks.m_mapLooks.find(l_strName);
        if (l_itIt == m_oLooks.m_mapLooks.end()) break;
        const toms::Look& l_oL = l_itIt->second;
        if (!l_bCharacter && !l_oL.m_strCharacter.empty()) { l_oLook.m_strCharacter = l_oL.m_strCharacter; l_bCharacter = true; }
        if (!l_bRender && !l_oL.m_strRender.empty()) { l_oLook.m_strRender = l_oL.m_strRender; l_bRender = true; }
        if (!l_bModel && !l_oL.m_strModel.empty()) { l_oLook.m_strModel = l_oL.m_strModel; l_bModel = true; }
        l_strName = l_oL.m_strBase;
    }
    return l_oLook;
}

void Game::f_SetFoeRigState(int l_iFoe, const std::string& l_strState) {
    FoeRig& l_oFr = m_vFoeRigs[(size_t)l_iFoe];
    l_oFr.m_strState = l_strState;
    // the view, made again when the monster changed (transformInto): its sprite's longer side fills the box (a 2-wide
    // golem as wide as the box), as the picture it replaces
    const std::string l_strSprite = f_MonsterSprite(l_iFoe);
    if (!l_oFr.m_upView || l_oFr.m_strSprite != l_strSprite) {
        l_oFr.m_strSprite = l_strSprite;
        const toms::AtlasRegion* l_pR = m_oSpriteAtlas.f_Find(l_strSprite);
        const float l_fLonger = l_pR ? (float)std::max(std::max(l_pR->m_iOrigW, l_pR->m_iOrigH), 1) : 32.0f;
        l_oFr.m_upView = f_MakeFighterView(f_MonsterLook(l_iFoe),
                                  [this, l_strSprite](const toms::anim::Clip& l_oC) { return f_FitClipToCharacter(l_oC, l_strSprite); },
                                  l_fLonger, true);
    }
    l_oFr.m_strClip.clear();
    if (!l_oFr.m_upView) return;
    // the look's clip for this status (data/looks.json), the monster's own version of it first, else the shared one
    const std::string& l_strId = m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strId;
    const toms::LookState* l_pLs = m_oLooks.f_Find(m_oCs.m_vFoes[(size_t)l_iFoe].m_oEnemy.m_strLook, l_strState);
    const std::string l_strClip = l_pLs && !l_pLs->m_strClip.empty() ? l_pLs->m_strClip : l_strState;
    if (l_oFr.m_upView->f_Play(l_strClip + "_" + l_strId, true) || l_oFr.m_upView->f_Play(l_strClip, true) || l_oFr.m_upView->f_Play(l_strState, true))
        l_oFr.m_strClip = l_oFr.m_upView->f_Clip();
    else
        l_oFr.m_upView->f_Stop();
}

void Game::f_UpdateMonsterRig(int l_iDtMs) {
    const bool l_bShown = m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0;
    if (!l_bShown) {
        for (FoeRig& l_oFr : m_vFoeRigs) {
            if (l_oFr.m_upView) l_oFr.m_upView->f_Stop();
            l_oFr.m_strState.clear(); l_oFr.m_strClip.clear(); l_oFr.m_strRequest.clear();
        }
        return;
    }
    if (m_vFoeRigs.size() != m_oCs.m_vFoes.size()) m_vFoeRigs.resize(m_oCs.m_vFoes.size());
    for (size_t l_nI = 0; l_nI < m_vFoeRigs.size(); l_nI++) {
        FoeRig& l_oFr = m_vFoeRigs[l_nI];
        const int l_iFoe = (int)l_nI;
        if (!l_oFr.m_strRequest.empty() && l_oFr.m_strState != "die") {   // an attack, a hit or its fall: from the start
            const std::string l_strReq = l_oFr.m_strRequest;
            l_oFr.m_strRequest.clear();
            f_SetFoeRigState(l_iFoe, l_strReq);
        } else {
            l_oFr.m_strRequest.clear();
            // every status but idle plays to its end; then the fight decides: down or won = die (it stays gone), else idle
            const bool l_bOneShot = !l_oFr.m_strState.empty() && l_oFr.m_strState != "idle";
            if (l_oFr.m_strState != "die" && !(l_bOneShot && l_oFr.m_upView && !l_oFr.m_upView->f_Finished())) {
                const std::string l_strNext = m_oCs.m_bWon || m_oCs.m_vFoes[l_nI].f_Down() ? "die" : "idle";
                if (l_strNext != l_oFr.m_strState) f_SetFoeRigState(l_iFoe, l_strNext);
            }
        }
        if (l_oFr.m_upView) l_oFr.m_upView->f_Update(l_iDtMs);   // (its events: the battle's own timers place the numbers)
    }
}

// The owner's rule (docs/gameplay/07_GAMEPLAY_PLAN.md §4): an attack pauses both sides' clocks until it is over.
void Game::f_StartAttackPause() { m_oCs.m_iAttackPauseMs = std::max(m_oCs.m_iAttackPauseMs, m_iAttackPauseMs); }

// The number appears when the swing lands, not when the button is pressed: a safe timer owned by this battle.
void Game::f_PopDamage(int l_iDmg, int l_iWho, const std::string& l_strStyle) {
    m_oTimers.f_After(m_oBattleTimers, m_iHitNumberDelayMs, [this, l_iDmg, l_iWho, l_strStyle] {
        CombatState::Popup l_oP;
        l_oP.m_strText = l_iDmg > 0 ? std::to_string(l_iDmg) : m_oLocale.f_Tr("battle.popup_guard");
        l_oP.m_iWho = l_iWho;
        l_oP.m_strStyle = l_iDmg > 0 ? l_strStyle : "guard";
        l_oP.m_oRise = toms::Tween(0.0f, 1.0f, 900, toms::Ease::OutCubic);
        m_oCs.m_vPopups.push_back(l_oP);
        // the monster flinches as the number appears (a fallen one keeps falling)
        if (l_iWho >= 0 && l_iWho < (int)m_vFoeRigs.size() && l_iWho < (int)m_oCs.m_vFoes.size() && l_iDmg > 0 &&
            m_vFoeRigs[(size_t)l_iWho].m_strRequest != "die" && !m_oCs.m_vFoes[(size_t)l_iWho].f_Down())
            f_FoeLook(l_iWho, "hurt");
    });
}

// Shared win/lose resolution -- identical to the pre-Milestone-6 auto-combat's own win/lose
// handling (rewards, level-up, boss warp, respawn), just extracted so resolveAttackTap/
// battleTapSuper (win) and resolveEnemyClockFire (lose) can all reach it without duplicating it.
void Game::f_FinishCombatWin() {
    if (f_ArenaEndFight(true)) return;   // the weapon test arena: no rewards, the next fight follows
    m_oCs.m_bActive = false; m_oCs.m_bWon = true; m_oCs.m_bWinPending = false;
    f_BattleEvent("win", -1, -1);
    m_oPl.m_iHp = m_oCs.m_iPlayerHP;
    // the grade (docs/gameplay/15 §3.6): the time, the HP lost, the perfects and the chain
    CombatState::Result& l_oRes = m_oCs.m_oResult;
    l_oRes = CombatState::Result{};
    l_oRes.m_bValid = true;
    l_oRes.m_fSeconds = m_oCs.m_iClockMs / 1000.0f;
    {
        toms::GradeInput l_oGi;
        l_oGi.m_fSeconds = l_oRes.m_fSeconds;
        l_oGi.m_fLostShare = (float)m_oCs.m_iDamageTaken / (float)std::max(1, f_EffectiveMaxHp());
        l_oGi.m_fPerfectRate = m_oCs.m_iAttacksDone > 0 ? (float)m_oCs.m_iPerfects / (float)m_oCs.m_iAttacksDone : 0.0f;
        l_oGi.m_iBestChain = m_oCs.m_iBestChain;
        l_oRes.m_strGrade = toms::f_BattleGrade(l_oGi, m_oGrade, &l_oRes.m_iScore);
    }
    const bool l_bGradeS = l_oRes.m_strGrade == "S";
    const std::string l_strWinSound = m_oFeel.f_Sound("win");
    if (!l_strWinSound.empty()) m_oAudio.f_Play(l_strWinSound);
    int l_iGold = 0, l_iExp = 0;   // every monster of the battle pays
    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes)
        if (!l_oF.m_bFled) { l_iGold += l_oF.m_oEnemy.m_iGold; l_iExp += l_oF.m_oEnemy.m_iExp; }
    l_oRes.m_iGold = (int)std::lround(l_iGold * (1.0f + f_AttrEffect("goldGain") / 100.0f) * (l_bGradeS ? 1.0f + m_oGrade.m_fSGold : 1.0f));   // LUK, an S
    l_oRes.m_iExp = l_iExp;
    m_oPl.m_iGold += l_oRes.m_iGold;
    f_GainExp(l_iExp);   // level-ups by data/stats.json
    // Monster cards (docs/gameplay/07_GAMEPLAY_PLAN.md §3): each win is counted, and each card may drop (enemies.json "card").
    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) {
        auto l_itT = m_mapEnemyTpl.find(l_oF.m_oEnemy.m_strId);
        if (l_itT == m_mapEnemyTpl.end() || l_oF.m_bFled) continue;
        float l_fRoll = std::uniform_real_distribution<float>(0.0f, 1.0f)(m_oCardRng);
        if (l_bGradeS) l_fRoll *= 1.0f - m_oGrade.m_fSCard;   // an S: a likelier card
        if (toms::f_RecordCardWin(m_oMeta.m_mapCards, l_oF.m_oEnemy.m_strId, toms::f_CardChanceFromJson(l_itT->second), l_fRoll)) {
            const int l_iN = m_oMeta.m_mapCards[l_oF.m_oEnemy.m_strId].m_iCopies;   // (the result screen says it; no notice over it)
            if (l_oRes.m_strCard.empty())
                l_oRes.m_strCard = l_iN == 1 ? f_TrParam(m_oLocale.f_Tr("cards.new"), "name", l_oF.m_oEnemy.m_strName)
                                  : f_TrParam(f_TrParam(m_oLocale.f_Tr("cards.more"), "name", l_oF.m_oEnemy.m_strName), "n", std::to_string(l_iN));
            m_bNewsCards = true;
        }
    }
    // remove the lead's entity from the stage -- only when it stands on the map (a test scenario's enemy does not: x, y -1;
    // its party never does)
    const EnemyInst& l_oLead = m_oCs.f_Lead().m_oEnemy;
    const bool l_bOnMap = l_oLead.m_iY >= 0 && l_oLead.m_iY < (int)m_oSt.m_vTiles.size() && l_oLead.m_iX >= 0 && l_oLead.m_iX < (int)m_oSt.m_vTiles[l_oLead.m_iY].size();
    if (l_bOnMap) {
        for (auto& l_oE : m_oSt.m_vEntities) if (l_oE.m_iX==l_oLead.m_iX && l_oE.m_iY==l_oLead.m_iY && l_oE.m_strId==l_oLead.m_strMapId) { l_oE.m_bConsumed=true; f_FireLinkedEvents(l_oE); }
        m_oSt.m_vTiles[l_oLead.m_iY][l_oLead.m_iX] = '.';
        // Milestone 7: persist the clear so it survives a reload of this floor (stairs or the Stage
        // Select hub) -- see entityStatus_'s declaration in game.h.
        toms::f_SetEntityStatus(m_mapEntityStatus, f_StatusKeyAt(l_oLead.m_iX, l_oLead.m_iY), toms::EntityStatus::Defeated);
    }
    {   // the weapon's win (docs/gameplay/07 phase 1): enough of them raise its level -- more points, bigger bonuses
        const std::string l_strKey = f_WeaponKey();
        const int l_iBefore = f_WeaponLevel(l_strKey);
        m_mapWeaponUses[l_strKey]++;
        const int l_iAfter = f_WeaponLevel(l_strKey);
        l_oRes.m_strWeapon = l_strKey; l_oRes.m_iLvBefore = l_iBefore; l_oRes.m_iLvAfter = l_iAfter; l_oRes.m_iUses = m_mapWeaponUses[l_strKey];   // (the result shows it)
    }
    bool l_bBoss = false;
    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) {
        if (l_oF.m_bFled) continue;
        toms::f_GlobalEventBus().f_Publish(toms::EnemyDefeated{l_oF.m_oEnemy.m_strId, m_strCurStage});
        l_bBoss = l_bBoss || l_oF.m_oEnemy.m_bBoss;
    }
    if (l_bBoss) { m_oCs.m_strLog = m_oLocale.f_Tr("battle.boss_win"); f_LoadStage("stage_11"); }
    f_MarkProgressDirty();   // gold/exp/level/entity-status all changed: autosave will flush it
}

void Game::f_FinishCombatLose() {
    if (f_ArenaEndFight(false)) return;   // the weapon test arena: no death counted, no respawn
    m_oCs.m_bActive = false; m_oCs.m_bWon = false;
    f_BattleEvent("lose", -1, -1);
    // M7 (first slice): noteDeath() existed since S3 (schemaVersion 3's deathsTotal/deathsNonBoss)
    // but nothing ever actually called it -- a real, pre-existing gap found while scoping this
    // slice, since e_13's condition depends entirely on this counter being real.
    bool l_bBoss = false;
    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) {
        l_bBoss = l_bBoss || l_oF.m_oEnemy.m_bBoss;
        toms::f_RecordCardLoss(m_oMeta.m_mapCards, l_oF.m_oEnemy.m_strId);   // each monster's card record counts it
    }
    m_oRun.f_NoteDeath(l_bBoss);
    // docs/story/STORY_DATA_SCHEMA.md §7.3's wipe row: only e_10 (cycleIndex>=9 -- reachable now
    // that M8's rebirth actually increments it) and e_13 (12 non-boss deaths) are ever checked on a
    // plain wipe -- never the full 15-ending resolver (see checkNamedEndings's own comment for why:
    // the guaranteed fallback ending would otherwise wrongly end the run on literally every death).
    GameConditionContext l_oEndingCtx(m_oPl, m_oMeta, m_mapMissionTrackers, m_oRun, m_oEquipped);
    const toms::EndingDefinition* l_pEnding = toms::f_CheckNamedEndings(m_oEndingsTable, {"e_10", "e_13"}, l_oEndingCtx);
    if (l_pEnding) {
        f_TriggerEnding(l_pEnding->m_strId);
        return;   // the run is over -- no respawn, no "you fell" pause
    }
    m_oCs.m_iResultPauseMs = 300;   // keep the "you fell" message on screen briefly (see CombatState)
    m_oPl.m_iHp = f_EffectiveMaxHp()/2; // respawn at stage start
    f_LoadStage(m_strCurStage); // reset monsters/items
    f_MarkProgressDirty();
}

// Attack bar tapped: apply damage to the enemy from wherever its auto-moving marker currently is
// (docs/design/FIGHT_SCENE_DESIGN.md §4's power_mult curve, with the equipped weapon's maxMult and
// Berserker's Red-zone-deals-0 rule per docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.2). A kill ends the fight
// immediately -- there's no "the enemy still gets to retaliate this round" any more, since the
// enemy's own clock is what decides when it attacks, independent of this tap.
void Game::f_ResolveAttackTap() {
    toms::PowerBarParams l_oBar = f_AttackBarParams();   // DEX widens its hit zone
    float l_fPos = m_oCs.m_oAtkBar.m_fPos;
    // S7 (equipment actives): an armed "guaranteed_crit_next_attack" bypasses the real marker
    // position entirely -- it's a scripted perfect release, not a well-timed one, so it also
    // bypasses the Berserker red-zone-zeroes-attacks rule below (a badly timed tap couldn't
    // otherwise ever be "guaranteed").
    bool l_bActiveCrit = m_oCs.m_bNextAttackGuaranteedCrit;
    float l_fPower = l_bActiveCrit ? 100.0f : toms::f_PowerFromPosition(l_oBar, l_fPos);
    float l_fMaxMult = toms::f_EffectiveMaxMult(m_oEquipped, m_mapEquipmentDefs);
    const int l_iEffAtk = f_EffectiveAtk();   // equipment, skills (half after a rebirth), STR
    CombatState::Foe& l_oFoe = m_oCs.m_vFoes[(size_t)m_oCs.m_iTarget];
    int l_iBaseHit = std::max(1, l_iEffAtk - (int)std::lround(l_oFoe.m_oEnemy.m_iDef * l_oFoe.m_fDefMul));
    int l_iDmg = (int)std::lround(toms::f_ComputeAttackDamage(l_iBaseHit, l_fPower, l_fMaxMult) * l_oFoe.m_fStaggerDamage);
    if (!l_bActiveCrit && toms::f_ZoneFromPosition(l_oBar, l_fPos) == toms::PowerZone::Red && toms::f_TalentZerosRedZoneAttacks(m_oEquipped.m_strTalentId))
        l_iDmg = 0;   // Berserker: a high ceiling, but Red-zone releases deal nothing
    if (l_bActiveCrit) {
        l_iDmg = (int)std::lround(l_iDmg * m_oCs.m_fNextAttackCritDamageMult);
        m_oCs.m_bNextAttackGuaranteedCrit = false;
        m_oCs.m_fNextAttackCritDamageMult = 1.0f;
    }

    f_BattleEvent("hit", -1, m_oCs.m_iTarget, l_iDmg);
    f_DamageFoe(m_oCs.m_iTarget, l_iDmg);
    m_iLastAttackDamage = l_iDmg;   // the battle rig shows attack or miss
    if (l_iDmg > 0) m_oCs.m_iSuperCharge = std::min(m_oRun.f_SuperMax(), m_oCs.m_iSuperCharge + 1);   // M8: superMax is per-run, halved by rebirth()
    f_PlayerLook("attack");
    f_StartAttackPause();
    f_PopDamage(l_iDmg, m_oCs.m_iTarget, "hit");
    f_AfterPlayerHit(f_TrParam(m_oLocale.f_Tr("battle.hit"), "dmg", std::to_string(l_iDmg)), f_TrParam(m_oLocale.f_Tr("battle.crit_win"), "dmg", std::to_string(l_iDmg)));
}

// After the player's hit on the target: it may have fallen. The last one standing falling wins the battle (winLog);
// another falling moves the target on (the log says who fell); else the hit's own log.
bool Game::f_AfterPlayerHit(const std::string& l_strHitLog, const std::string& l_strWinLog) {
    if (!m_oCs.m_bActive) return true;   // a monster event already ended it (the last one fled)
    const int l_iT = m_oCs.m_iTarget;
    if (!m_oCs.m_vFoes[(size_t)l_iT].f_Down()) { m_oCs.m_strLog = l_strHitLog; return false; }
    if (toms::f_AllDown(m_oCs.f_FoeHp())) {
        f_BattleEvent("down", l_iT, -1, 0, m_oCs.m_vFoes[(size_t)l_iT].m_oEnemy.m_strId);
        m_oCs.m_strLog = l_strWinLog;
        f_FoeLook(l_iT, "die");
        f_StartWinMoment();   // slow motion, then finishCombatWin (15 §3.8)
        return true;
    }
    const std::string l_strName = m_oCs.m_vFoes[(size_t)l_iT].m_oEnemy.m_strName;
    f_FoeDown(l_iT);
    m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.foe_down"), "name", l_strName);
    return false;
}

// Defense bar tapped: doesn't apply any damage itself -- it just banks whatever power the
// auto-moving marker currently reads as a shield (mitigation curve, Perfect Guard, and Guardian's
// flat mitigation floor per docs/design/MAIN_BATTLE_SCENE_DESIGN.md §4.2 are all applied later, in
// resolveEnemyClockFire(), when that shield is actually spent). A second tap before the enemy's
// clock fires simply overwrites the banked value -- most recent tap wins, no stacking.
void Game::f_ResolveDefenseTap() {
    toms::PowerBarParams l_oBar = toms::f_EffectiveDefenseBar(m_oEquipped, m_mapEquipmentDefs);
    float l_fPower = toms::f_PowerFromPosition(l_oBar, m_oCs.m_oDefBar.m_fPos);
    m_oCs.m_bShieldBanked = true;
    m_oCs.m_fShieldPower = l_fPower;
    m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.shield_log"), "pct", std::to_string((int)std::lround(l_fPower)));
}

// The enemy's own real-time clock fired (see update()): spend whatever shield is currently
// banked (if any) and apply the resulting damage to the player -- an un-shielded hit resolves at
// power=0, i.e. full damage, identical to whiffing the old press-and-hold Defense Bar entirely.
void Game::f_ResolveEnemyClockFire(int l_iFoe) {
    CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iFoe];
    const toms::MoveDef& l_oM = m_oMoves.f_Get(l_oF.m_strMove);   // its move, shown as its intent (docs/gameplay/13 §6.1, 6.2)
    // a special guard play (docs/gameplay/16 §4.3): the hit waits for it; endSkill comes back here with its result
    if (!l_oM.m_strGuardPlay.empty() && l_oF.m_fGuardShare < 0.0f && l_oF.m_iVolleyHit == 0 && !m_oCs.m_oSkill.m_bOn)
        if (const toms::SpecialMove* l_pD = m_oSpecialMoves.f_Find(l_oM.m_strGuardPlay)) { f_StartSkill(*l_pD, l_iFoe); return; }
    const bool l_bVolley = !l_oM.m_vGapsMs.empty() && l_oM.m_iHits > 1;   // its hits one by one, each with its "!" (16 §4.1)
    if (l_oF.m_iVolleyHit == 0) l_oF.m_iAttacks++;
    auto l_fnAfterMove = [this, l_iFoe] {   // the next move, then its "attacks" events
        if (!m_oCs.m_bActive) return;
        f_ChooseFoeMove(l_iFoe);
        toms::EventCheck l_oC;
        l_oC.m_strOn = "attacks";
        l_oC.m_iAttacks = m_oCs.m_vFoes[(size_t)l_iFoe].m_iAttacks;
        f_RunFoeEvents(l_iFoe, l_oC);
    };
    if (l_oM.m_eKind == toms::MoveKind::Guard) {   // its guard is over: no attack
        f_BattleEvent("guardEnd", l_iFoe, -1);
        l_fnAfterMove();
        return;
    }
    const int l_iEffDef = f_EffectiveDef();   // equipment, skills (half after a rebirth), attributes
    const int l_iAtk = (int)std::lround(l_oF.m_oEnemy.m_iAtk * l_oF.m_fAtkMul * l_oM.m_fDamage);
    int l_iIncoming = std::max(1, l_iAtk - l_iEffDef);
    float l_fFloorMitigation = toms::f_TalentDefenseMitigationFloor(m_oEquipped.m_strTalentId);
    float l_fPower = m_oCs.m_bShieldBanked ? m_oCs.m_fShieldPower : 0.0f;
    bool l_bGuarded = m_oCs.m_bShieldBanked;
    bool l_bParried = l_bGuarded && l_fPower >= (float)m_oBreakSettings.m_iParryPower;
    m_oCs.m_bShieldBanked = false; m_oCs.m_fShieldPower = 0.0f;
    if (m_oGuard.m_strMode == "timing" || m_oGuard.m_strMode == "warn") {   // one button: just before the hit (15 §3.2), or while its "!" shows (16 §4.2)
        const int l_iSince = m_oCs.m_iGuardAtMs < 0 ? -1 : m_oCs.m_iClockMs - m_oCs.m_iGuardAtMs;
        const toms::GuardTiming l_eGt = m_oGuard.m_strMode == "warn" ? toms::f_GuardWarn(l_iSince, f_MoveWarnMs(l_oM), m_oGuard)
                                                           : toms::f_GuardTiming(l_iSince, m_oGuard);
        m_oCs.m_iGuardAtMs = -1;   // a press answers one move
        l_bGuarded = l_eGt == toms::GuardTiming::Parry || l_eGt == toms::GuardTiming::Block;
        l_bParried = l_eGt == toms::GuardTiming::Parry;
        l_fPower = l_bParried ? 100.0f : l_eGt == toms::GuardTiming::Block ? m_oGuard.m_fBlockPower : 0.0f;
        if (l_bParried) { f_PopWord(m_oLocale.f_Tr("battle.word_parry"), -1, "parry"); f_FeelMoment("parry"); }
        else if (l_eGt == toms::GuardTiming::Block) { f_PopWord(m_oLocale.f_Tr("battle.word_block"), -1, "block"); f_FeelMoment("block"); }
        else if (l_eGt == toms::GuardTiming::Early) f_PopWord(m_oLocale.f_Tr("battle.word_early"), -1, "early");
    }
    if (l_oM.m_eKind == toms::MoveKind::Heavy && l_bGuarded && l_bParried) {
        // a perfect guard parries a heavy blow: no damage, it staggers and takes more (13 §6.2)
        f_BattleEvent("parry", -1, l_iFoe, (int)std::lround(l_fPower));
        f_PlayerLook("parry");
        if (m_oGuard.m_strMode == "bar") f_FeelMoment("parry");
        l_oF.m_iVolleyHit = 0;
        f_StartAttackPause();
        f_PopDamage(0, -1, "guard");
        f_StunFoe(l_iFoe, m_oBreakSettings.m_iParryMs, m_oBreakSettings.m_fParryDamage, false);
        if (toms::f_AddBreak(m_oCs.m_vFoes[(size_t)l_iFoe].m_fBreakGauge, (float)m_oBreakSettings.m_iParryBreak, f_FoeBreakPoints(l_iFoe)))
            f_StunFoe(l_iFoe, m_oBreakSettings.m_iBrokenMs, m_oBreakSettings.m_fBrokenDamage, true);
        m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.parry"), "name", l_oF.m_oEnemy.m_strName);
        l_fnAfterMove();
        return;
    }
    if (l_oM.m_eKind == toms::MoveKind::Spell) l_fPower *= 0.5f;   // a guard only halves a spell
    int l_iDmg = 0;
    const int l_iHits = l_bVolley ? 1 : l_oM.m_eKind == toms::MoveKind::Jabs ? l_oM.m_iHits : 1;
    for (int l_iK = 0; l_iK < l_iHits; l_iK++) l_iDmg += toms::f_ComputeDefenseDamage(l_iIncoming, l_fPower, l_fFloorMitigation);
    if (l_oF.m_fGuardShare >= 0.0f) {   // the special guard play's result: what was done takes that much off (16 §4.3)
        l_iDmg = (int)std::lround((float)l_iDmg * (1.0f - std::clamp(l_oF.m_fGuardShare, 0.0f, 1.0f)));
        l_bGuarded = l_oF.m_fGuardShare > 0.0f;
        if (l_oF.m_fGuardShare >= 1.0f) {
            f_PopWord(m_oLocale.f_Tr("battle.word_parry"), -1, "parry");
            f_StunFoe(l_iFoe, m_oBreakSettings.m_iParryMs, m_oBreakSettings.m_fParryDamage, false);
        }
        l_oF.m_fGuardShare = -1.0f;
    }
    if (l_oM.m_eKind == toms::MoveKind::Jabs) f_InterruptCombo(l_iFoe);

    const float l_fHpBefore = f_PlayerHpPct();
    m_oCs.m_iPlayerHP -= l_iDmg;
    m_oCs.m_iDamageTaken += l_iDmg;
    if (l_iDmg > 0) f_FeelMoment(l_oM.m_eKind == toms::MoveKind::Heavy || l_oM.m_eKind == toms::MoveKind::Spell ? "heavyHurt" : "hurt");
    f_BattleEvent("hit", l_iFoe, -1, l_iDmg, l_oF.m_strMove + (l_bGuarded ? " guarded " + std::to_string((int)std::lround(l_fPower)) : std::string()));
    f_FoeLook(l_iFoe, "attack");   // its sound; the lunge lands as the number appears
    f_StartAttackPause();
    f_PopDamage(l_iDmg, -1, "hurt");

    if (m_oCs.m_iPlayerHP <= 0 && m_oCs.m_bSurvivalArmed) {
        // a_sanctuary_echo: once per battle, a lethal hit leaves 1 HP instead of ending the fight.
        m_oCs.m_bSurvivalArmed = false;
        m_oCs.m_iPlayerHP = 1;
        m_oCs.m_strLog = m_oLocale.f_Tr("battle.sanctuary_survive");
    } else if (m_oCs.m_iPlayerHP <= 0) {
        m_oCs.m_strLog = m_oLocale.f_Tr("battle.player_down");
        f_BattleEvent("down", -1, -1);
        f_FinishCombatLose();
    } else if (!f_AttackRunning() || l_oM.m_eKind != toms::MoveKind::Jabs) {
        m_oCs.m_strLog = l_iDmg == 0 ? m_oLocale.f_Tr("battle.perfect_guard") : f_TrParam(m_oLocale.f_Tr("battle.guard_hit"), "dmg", std::to_string(l_iDmg));
    }
    if (!m_oCs.m_bActive) return;
    if (l_bVolley && l_oF.m_iVolleyHit + 1 < l_oM.m_iHits) {   // more hits to come: the next after its gap (its "!" before it)
        l_oF.m_iVolleyHit++;
        l_oF.m_iVolleyMs = l_oM.f_GapBefore(l_oF.m_iVolleyHit);
    } else {
        l_oF.m_iVolleyHit = 0;
    }
    {
        toms::EventCheck l_oC;   // every monster hears the player's HP fall
        l_oC.m_strOn = "playerHpBelow";
        l_oC.m_fHpPctBefore = l_fHpBefore;
        l_oC.m_fHpPctAfter = f_PlayerHpPct();
        for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size() && m_oCs.m_bActive; l_nI++)
            if (!m_oCs.m_vFoes[l_nI].f_Down()) f_RunFoeEvents((int)l_nI, l_oC);
    }
    if (m_oCs.m_bActive && m_oCs.m_vFoes[(size_t)l_iFoe].m_iVolleyHit == 0) l_fnAfterMove();   // its whole move is done
}

// Battle v3 (docs/gameplay/07 phase 1): the first tap starts the combo with the charge built so far; the next taps hit the
// points. The combo's end (all hit, the sweeps run out, or two misses) resolves the attack.
void Game::f_BattleTapAttack() {
    if (!m_oCs.m_bActive || m_oCs.m_bWinPending || (m_oCs.m_ePace == toms::Pace::Cooldown && m_oCs.m_oAtkBar.m_bCooling)) return;
    if (f_BattlePlayKind() != toms::PlayKind::Track) return;   // the weapon's own play: by touch (battlePointer)
    if (!m_oCs.m_oCombo.f_Running()) {
        if (m_oCs.m_ePace == toms::Pace::Stamina) {   // the stamina pace: starting an attack costs it (docs/gameplay/13 §6.3)
            if (m_oCs.m_fStamina < (float)m_oPaceSettings.m_iAttackCost) {
                m_oCs.m_strLog = m_oLocale.f_Tr("battle.no_stamina");
                m_oAudio.f_Play("deny");
                return;
            }
            m_oCs.m_fStamina -= (float)m_oPaceSettings.m_iAttackCost;
        }
        const std::string l_strKey = f_WeaponKey();
        m_oCs.m_oCombo.f_Start(toms::f_ComboSettings(f_WeaponPlayOf(l_strKey), f_WeaponLevel(l_strKey)), f_ComboCharge());
        m_oCs.m_strLog = m_oLocale.f_Tr("battle.combo_go");
        m_oAudio.f_Play("confirm_click");
        return;
    }
    const std::vector<bool> l_vBefore = m_oCs.m_oCombo.f_PointsHit();
    switch (m_oCs.m_oCombo.f_Tap()) {
    case toms::ComboPlay::Tap::Hit:
        m_oAudio.f_Play("confirm_click");
        for (size_t l_nI = 0; l_nI < l_vBefore.size() && l_nI < m_oCs.m_oCombo.f_PointsHit().size(); l_nI++)
            if (!l_vBefore[l_nI] && m_oCs.m_oCombo.f_PointsHit()[l_nI]) f_ComboHint((int)l_nI, /*hit=*/true);
        break;
    case toms::ComboPlay::Tap::Miss: m_oAudio.f_Play("deny"); break;
    case toms::ComboPlay::Tap::Ignored: break;
    }
    if (m_oCs.m_oCombo.f_Done()) f_ResolveCombo();
}

void Game::f_ResolveCombo(const toms::ComboResult* l_pPlayed) {
    const toms::ComboResult l_oR = l_pPlayed ? *l_pPlayed : m_oCs.m_oCombo.f_Result();
    m_oCs.m_oCombo = toms::ComboPlay{};
    m_oCs.m_iChargeMs = 0;
    const bool l_bActiveCrit = m_oCs.m_bNextAttackGuaranteedCrit;   // a scripted perfect release: every point counts
    const float l_fPower = l_bActiveCrit ? 100.0f : l_oR.f_Power();
    const float l_fMaxMult = toms::f_EffectiveMaxMult(m_oEquipped, m_mapEquipmentDefs);
    const int l_iT = m_oCs.m_iTarget;   // the player's attacks go to the target
    const CombatState::Foe& l_oFoe = m_oCs.m_vFoes[(size_t)l_iT];
    const toms::MoveDef& l_oFm = m_oMoves.f_Get(l_oFoe.m_strMove);
    const int l_iBaseHit = std::max(1, f_EffectiveAtk() - (int)std::lround(l_oFoe.m_oEnemy.m_iDef * l_oFoe.m_fDefMul));
    int l_iDmg = toms::f_ComputeAttackDamage(l_iBaseHit, l_fPower, l_fMaxMult);
    l_iDmg = (int)std::lround(l_iDmg * (1.0f + f_WeaponPlayOf(f_WeaponKey()).m_fChargeDamage * l_oR.m_fCharge));   // the charge
    // the result by points matched (weapon_play.json "results", docs/gameplay/13 §4.2): the weapon's, else the default's
    const toms::WeaponPlay& l_oWp = f_WeaponPlayOf(f_WeaponKey());
    // (a scripted perfect release counts every point)
    const toms::WeaponResult* l_pRes = toms::f_WeaponResultFor(l_oWp.m_vResults.empty() ? m_oWeaponPlay.f_Get("default") : l_oWp,
                                                          l_bActiveCrit ? l_oR.m_iPoints : l_oR.m_iHits, l_oR.m_iPoints, l_bActiveCrit ? false : l_oR.m_bFailed);
    if (l_pRes) l_iDmg = (int)std::lround(l_iDmg * l_pRes->m_fDamage);
    // the answers to its move (13 §6.2): a guard takes most of a hit unless a full charge breaks it; a spell breaks
    // when enough points match; a stunned monster takes more
    bool l_bGuardBroken = false;
    if (l_oFm.m_eKind == toms::MoveKind::Guard && l_oFoe.m_iStaggerMs <= 0) {
        if (l_oR.m_fCharge >= 0.99f && !l_oR.m_bFailed && l_oR.m_iHits > 0) l_bGuardBroken = true;
        else l_iDmg = (int)std::lround(l_iDmg * l_oFm.m_fReduce);
    }
    const bool l_bSpellBroken = l_oFm.m_eKind == toms::MoveKind::Spell && !l_oR.m_bFailed && l_oR.m_iHits >= l_oFm.m_iBreakHits;
    if (l_oFoe.m_iStaggerMs > 0) l_iDmg = (int)std::lround(l_iDmg * l_oFoe.m_fStaggerDamage);
    const bool l_bGuardedHit = l_oFm.m_eKind == toms::MoveKind::Guard && !l_bGuardBroken && l_oFoe.m_iStaggerMs <= 0;
    if (!l_bActiveCrit && l_oR.m_bFailed && toms::f_TalentZerosRedZoneAttacks(m_oEquipped.m_strTalentId)) l_iDmg = 0;   // Berserker: a fail deals nothing
    if (l_bActiveCrit) {
        l_iDmg = (int)std::lround(l_iDmg * m_oCs.m_fNextAttackCritDamageMult);
        m_oCs.m_bNextAttackGuaranteedCrit = false;
        m_oCs.m_fNextAttackCritDamageMult = 1.0f;
    }
    // the revamp (docs/gameplay/15 §3.3, §3.4): the result's tier, the perfect chain, the ultimate
    const bool l_bUlt = m_oCs.m_bUltimate;
    m_oCs.m_bUltimate = false;
    std::string l_strTier = l_oR.m_bFailed || l_oR.m_iHits == 0 ? "miss"
                     : l_pRes && (l_pRes->m_strName == "perfect" || l_pRes->m_strName == "great") ? l_pRes->m_strName : "hit";
    if (l_oR.m_iPerfects >= 0 && l_strTier != "miss")   // a touch play (16 §3): perfect when all hit and half were perfect
        l_strTier = l_oR.m_iHits >= l_oR.m_iPoints && l_oR.m_iPerfects * 2 >= l_oR.m_iPoints ? "perfect" : l_oR.m_iHits >= l_oR.m_iPoints || l_oR.m_iHits * 5 >= l_oR.m_iPoints * 3 ? "great" : "hit";
    if (l_strTier == "perfect") m_oCs.m_iChain = std::min(m_oFeel.m_iChainMax, m_oCs.m_iChain + 1);
    else if (l_strTier == "miss") m_oCs.m_iChain = 0;
    m_oCs.m_iBestChain = std::max(m_oCs.m_iBestChain, m_oCs.m_iChain);
    if (l_strTier == "perfect") m_oCs.m_iPerfects++;
    m_oCs.m_iAttacksDone++;
    if (m_oCs.m_iChain > 1) l_iDmg = (int)std::lround(l_iDmg * (1.0f + m_oFeel.m_fChainStep * (float)(m_oCs.m_iChain - 1)));
    if (l_bUlt) l_iDmg = (int)std::lround(l_iDmg * (l_oFoe.m_iStaggerMs > 0 ? 4.0f : 2.0f));   // ×2, ×4 in a break
    f_BattleEvent("combo", -1, l_iT, l_oR.m_iHits, std::to_string(l_oR.m_iHits) + "/" + std::to_string(l_oR.m_iPoints) + (l_oR.m_bFailed ? " failed" : "") +
                                          (l_pRes && !l_pRes->m_strName.empty() ? " " + l_pRes->m_strName : std::string()));
    const int l_iDealt = m_oCs.m_iPlayDealt;   // the play's answers already hit (16 §3): this is the rest of the attack
    m_oCs.m_iPlayDealt = 0;
    if (l_iDealt > 0) l_iDmg = std::max(0, l_iDmg - l_iDealt);
    f_BattleEvent("hit", -1, l_iT, l_iDmg);
    m_iLastAttackDamage = l_oR.m_bFailed || l_oR.m_iHits == 0 ? 0 : std::max(l_iDmg, l_iDealt);   // the battle rig: attack, or miss for a fail
    if (l_iDmg > 0 && !l_oR.m_bFailed) m_oCs.m_iSuperCharge = std::min(m_oRun.f_SuperMax(), m_oCs.m_iSuperCharge + 1);
    if (l_pRes && !l_pRes->m_strSound.empty()) m_oAudio.f_Play(l_pRes->m_strSound);
    else f_PlayerLook("attack");
    f_StartAttackPause();
    f_FeelMoment(l_bUlt ? "ultimate" : l_strTier);
    if (l_iDmg > 0 || l_iDealt == 0) f_PopDamage(l_iDmg, l_iT, l_bUlt ? "perfect" : l_strTier);
    // its named move (07 §5.4): the result row's "move", over the monster; the chain by the player
    if (l_pRes && !l_pRes->m_strMove.empty() && m_oLocale.f_Has(l_pRes->m_strMove)) f_PopWord(m_oLocale.f_Tr(l_pRes->m_strMove), l_iT, "move-" + l_strTier);
    if (m_oCs.m_iChain > 1) f_PopWord(f_TrParam(m_oLocale.f_Tr("battle.word_chain"), "n", std::to_string(m_oCs.m_iChain)), -1, "chain");
    if (m_iLastAttackDamage > 0) {   // the weapon's particles for its level (or the result's): off it now, on the monster with the number
        if (const toms::WeaponFx* l_pFx = f_BattleWeaponFx(l_bUlt)) {
            f_PlayBattleFx(l_pFx->m_strRelease, -1);
            f_PlayBattleFx(l_pRes && !l_pRes->m_strFx.empty() ? l_pRes->m_strFx : l_pFx->m_strHit, l_iT, m_iHitNumberDelayMs);
        }
    } else if (l_pRes && !l_pRes->m_strFx.empty()) {
        f_PlayBattleFx(l_pRes->m_strFx, -1);   // a miss: the result's effect at the player (weapon_play.json "miss_puff")
    }
    if (f_BattleRigReady()) {
        const std::string l_strBigClip = l_bUlt ? std::string("attack_big") : l_pRes ? l_pRes->m_strAnim : std::string();   // the ultimate: the big clip
        const bool l_bBig = m_iLastAttackDamage > 0 && !l_strBigClip.empty() && m_upPlayerView && m_upPlayerView->f_Has(l_strBigClip);
        f_SetBattleRigState(m_iLastAttackDamage > 0 ? "attack" : "miss", true, l_bBig ? l_strBigClip : std::string());
    }
    f_DamageFoe(l_iT, l_iDmg);
    const std::string l_strCombo = f_TrParam(f_TrParam(m_oLocale.f_Tr(l_oR.m_bFailed ? "battle.combo_fail" : "battle.combo_hits"), "hits",
                                              std::to_string(l_oR.m_iHits)), "n", std::to_string(l_oR.m_iPoints));
    std::string l_strHitText = f_TrParam(m_oLocale.f_Tr(l_bGuardedHit ? "battle.guarding" : "battle.hit"), "dmg", std::to_string(l_iDmg));
    l_strHitText = f_TrParam(l_strHitText, "name", f_FoeLogName(l_iT));
    if (l_pRes && !l_pRes->m_strName.empty() && m_oLocale.f_Has("battle.result." + l_pRes->m_strName))
        l_strHitText = m_oLocale.f_Tr("battle.result." + l_pRes->m_strName) + " " + l_strHitText;
    if (f_AfterPlayerHit(l_strCombo + " " + l_strHitText, l_strCombo + " " + f_TrParam(m_oLocale.f_Tr("battle.crit_win"), "dmg", std::to_string(l_iDmg))))
        return;
    if (!m_oCs.m_vFoes[(size_t)l_iT].f_Down()) {
        CombatState::Foe& l_oF = m_oCs.m_vFoes[(size_t)l_iT];
        std::string l_strExtra;
        if (l_bGuardBroken) {
            l_strExtra = f_TrParam(m_oLocale.f_Tr("battle.guard_broken"), "name", l_oF.m_oEnemy.m_strName);
            f_BattleEvent("guardBreak", -1, l_iT);
            l_oF.m_iClockMs = 0;
            f_ChooseFoeMove(l_iT);
        } else if (l_bSpellBroken) {
            l_strExtra = f_TrParam(m_oLocale.f_Tr("battle.spell_broken"), "name", l_oF.m_oEnemy.m_strName);
            f_BattleEvent("spellBreak", -1, l_iT);
            l_oF.m_iClockMs = 0;
            f_ChooseFoeMove(l_iT);
        }
        if (l_pRes) {
            if (l_pRes->m_iStunMs > 0) f_StunFoe(l_iT, l_pRes->m_iStunMs, 1.0f, false);
            if (l_pRes->m_iPushMs > 0) m_oCs.m_vFoes[(size_t)l_iT].m_iClockMs -= l_pRes->m_iPushMs;
        }
        // the break gauge (13 §6.4): matched points fill it; full, the monster is stunned and takes more
        const float l_fAdd = l_oR.m_bFailed ? 0.0f : (float)l_oR.m_iHits + (l_pRes ? l_pRes->m_fBreakAdd : 0.0f) +
                                                (l_bGuardBroken ? (float)m_oBreakSettings.m_iGuardBreak : 0.0f);
        if (m_oCs.m_vFoes[(size_t)l_iT].m_iStaggerMs <= 0 || !m_oCs.m_vFoes[(size_t)l_iT].m_bBroken)
            if (toms::f_AddBreak(m_oCs.m_vFoes[(size_t)l_iT].m_fBreakGauge, l_fAdd, f_FoeBreakPoints(l_iT))) {
                f_StunFoe(l_iT, m_oBreakSettings.m_iBrokenMs, m_oBreakSettings.m_fBrokenDamage, true);
                l_strExtra = f_TrParam(m_oLocale.f_Tr("battle.break"), "name", m_oCs.m_vFoes[(size_t)l_iT].m_oEnemy.m_strName);
            }
        if (l_oR.m_iHits == l_oR.m_iPoints && !l_oR.m_bFailed) {
            toms::EventCheck l_oC;
            l_oC.m_strOn = "perfectHit";
            f_RunFoeEvents(l_iT, l_oC);
        }
        if (!l_strExtra.empty() && m_oCs.m_bActive) m_oCs.m_strLog += " " + l_strExtra;
    }
    if (!m_oCs.m_bActive) return;
    if (m_oCs.m_ePace == toms::Pace::Cooldown) {
        m_oCs.m_oAtkBar.m_bCooling = true;
        m_oCs.m_oAtkBar.m_iCooldownMs = f_BarCooldownMs();   // AGI shortens it
    }
}

void Game::f_BattleTapDefense() {
    if (!m_oCs.m_bActive || m_oCs.m_bWinPending || m_oCs.m_oSkill.m_bOn) return;
    if (m_oGuard.m_strMode == "warn") {   // only while an "!" shows over a monster (16 §4.2); it stops the player's attack
        if (m_oCs.m_iGuardLockMs > 0 || m_oCs.m_iGuardAtMs >= 0) return;
        f_StopOwnAttack();
        bool l_bWarned = false;
        for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) l_bWarned = l_bWarned || f_FoeWarning((int)l_nI);
        if (!l_bWarned) {   // no "!": too early; the guard locks a moment
            f_PopWord(m_oLocale.f_Tr("battle.word_early"), -1, "early");
            m_oCs.m_iGuardLockMs = m_oGuard.m_iMissLockMs;
            m_oAudio.f_Play("deny");
            return;
        }
        if (m_oCs.m_ePace == toms::Pace::Stamina) {
            if (m_oCs.m_fStamina < (float)m_oPaceSettings.m_iGuardCost) { m_oCs.m_strLog = m_oLocale.f_Tr("battle.no_stamina"); m_oAudio.f_Play("deny"); return; }
            m_oCs.m_fStamina -= (float)m_oPaceSettings.m_iGuardCost;
        }
        m_oCs.m_iGuardAtMs = m_oCs.m_iClockMs;
        m_oAudio.f_Play("confirm_click");
        return;
    }
    if (m_oGuard.m_strMode == "timing") {   // one button (docs/gameplay/15 §3.2): it answers the next hit, judged when it lands
        if (m_oCs.m_iGuardCdMs > 0) return;
        if (m_oCs.m_ePace == toms::Pace::Stamina) {
            if (m_oCs.m_fStamina < (float)m_oPaceSettings.m_iGuardCost) { m_oCs.m_strLog = m_oLocale.f_Tr("battle.no_stamina"); m_oAudio.f_Play("deny"); return; }
            m_oCs.m_fStamina -= (float)m_oPaceSettings.m_iGuardCost;
        }
        m_oCs.m_iGuardAtMs = m_oCs.m_iClockMs;
        m_oCs.m_iGuardCdMs = m_oGuard.m_iCooldownMs;
        m_oAudio.f_Play("confirm_click");
        return;
    }
    if (m_oCs.m_ePace == toms::Pace::Stamina) {   // each guard tap costs stamina, no wait (docs/gameplay/13 §6.3)
        if (m_oCs.m_fStamina < (float)m_oPaceSettings.m_iGuardCost) {
            m_oCs.m_strLog = m_oLocale.f_Tr("battle.no_stamina");
            m_oAudio.f_Play("deny");
            return;
        }
        m_oCs.m_fStamina -= (float)m_oPaceSettings.m_iGuardCost;
        f_ResolveDefenseTap();
        return;
    }
    if (m_oCs.m_oDefBar.m_bCooling) return;
    f_ResolveDefenseTap();
    m_oCs.m_oDefBar.m_bCooling = true;
    m_oCs.m_oDefBar.m_iCooldownMs = f_BarCooldownMs();
}

// Super Attack: a guaranteed, no-timing-required strong hit once the gauge is full (§4 of
// docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md) -- resolved at power=100 against the Attack Bar's own
// (equipment-tuned) maxMult ceiling, the same number a manual Perfect release already reaches.
// Independent of the Attack bar's own cooldown; doesn't touch it.
// The ultimate (docs/gameplay/15 §3.4; 14's 絕招): when the super charge is full, the weapon's finisher. A cut-in holds the
// fight a moment, then a harder play starts at once with a full charge (two more points); its hit does ×2, ×4 in a
// break, with the weapon's best effects and its big clip (resolveCombo).
void Game::f_BattleTapSuper() {
    if (!m_oCs.m_bActive || m_oCs.m_bWinPending || m_oCs.m_iSuperCharge < m_oRun.f_SuperMax() || m_oCs.m_oCombo.f_Running() || m_oCs.m_oSkill.m_bOn) return;
    m_oCs.m_iSuperCharge = 0;
    m_oCs.m_bUltimate = true;
    const std::string l_strKey = f_WeaponKey();
    if (const toms::SpecialMove* l_pD = m_oSpecialMoves.f_ForWeapon(l_strKey, "ultimate")) {   // the weapon's own move (16 §6): it cuts in
        const std::string l_strS = m_oFeel.f_Sound("ultimate");
        if (!l_strS.empty()) m_oAudio.f_Play(l_strS);
        f_StartSkill(*l_pD, -1);
        return;
    }
    const std::string l_strName = m_mapEquipmentDefs.count(l_strKey) ? f_ItemName(l_strKey) : m_oLocale.f_Tr("battle.unarmed");
    f_ShowBanner(f_TrParam(m_oLocale.f_Tr("battle.word_ultimate"), "name", l_strName), "ultimate", 900);
    m_oCs.m_iHitStopMs = std::max(m_oCs.m_iHitStopMs, m_oSettings.m_bReduceEffects ? 200 : 600);   // the cut-in holds the fight
    const std::string l_strS = m_oFeel.f_Sound("ultimate");
    if (!l_strS.empty()) m_oAudio.f_Play(l_strS);
    toms::ComboSettings l_oCset = toms::f_ComboSettings(f_WeaponPlayOf(l_strKey), f_WeaponLevel(l_strKey));
    l_oCset.m_iPoints = std::min(toms::g_iMaxComboPoints, l_oCset.m_iPoints + 2);
    m_oCs.m_oAtkBar.m_bCooling = false;
    if (f_BattlePlayKind() != toms::PlayKind::Track) {   // the weapon's own play, harder (it cuts in over the normal one)
        m_oCs.m_oTouch.f_Stop();
        m_oCs.m_bTouchPaid = false;
        m_oCs.m_iPlayDealt = 0;
        f_ArmTouchPlay(true, l_oCset.m_iPoints);
    } else {
        m_oCs.m_oCombo.f_Start(l_oCset, 1.0f);
    }
    f_BattleEvent("super", -1, m_oCs.m_iTarget, l_oCset.m_iPoints, "ultimate");
    m_oCs.m_strLog = m_oLocale.f_Tr("battle.combo_go");
}

// (each weapon's own play, skills and the training ground: game_skills.cpp)

// Equipment actives (2026-09-17, rewired onto equipment_actives.h -- see CombatState's own
// comment for why): a no-op unless the currently equipped gear grants an active
// (equippedActives()) and toms::canUseActive() says this battle still has a use of it, off
// cooldown, with no st_silence in effect. Only one active fires at a time -- the first slice's
// single on-screen button -- matching ids[0], same as before this rewire. "guaranteedCrit" arms
// a flag consumed by the next resolveAttackTap() (a buff on the next attack, not an attack in its
// own right, per a_qingxiao_edge's "下一次攻擊..." description); "surviveLethal" arms a flag
// consumed by the next resolveEnemyClockFire() that would otherwise end the fight.
// ActiveDefinition carries no display name yet (a real, still-open content gap) -- the log falls
// back to the raw id rather than inventing a name lookup this rewire doesn't own.
void Game::f_BattleTapActive() {
    if (!m_oCs.m_bActive) return;
    auto l_vIds = toms::f_EquippedActives(m_oEquipped, m_mapEquipmentDefs);
    if (l_vIds.empty()) return;
    const std::string& l_strId = l_vIds[0];
    if (!toms::f_CanUseActive(l_strId, m_mapActiveDefs, m_oCs.m_mapActiveRuntime, m_oCs.m_oActiveStatus)) return;
    toms::ActiveEffect l_oEff = toms::f_UseActive(l_strId, m_mapActiveDefs, m_oCs.m_mapActiveRuntime, m_oCs.m_oActiveStatus);
    if (!l_oEff.m_bFired) return;
    if (l_oEff.m_bGuaranteedCrit) {
        m_oCs.m_bNextAttackGuaranteedCrit = true;
        m_oCs.m_fNextAttackCritDamageMult = l_oEff.m_fDamageMultiplier;
    }
    if (l_oEff.m_bSurviveLethal) m_oCs.m_bSurvivalArmed = true;
    m_oCs.m_strLog = f_TrParam(m_oLocale.f_Tr("battle.active_armed"), "name", l_strId);
    f_BattleEvent("active", -1, -1, 0, l_strId);
    m_oAudio.f_Play("confirm_click");
}

// game.h — game logic: player, movement, auto combat, talking, stage flow, world drawing.
// The UI (title, HUD, battle, menus, ...) is RmlUi: buildUiState()/uiEvent() below, ui/ui_state.h.
#pragma once
#include <array>
#include <memory>
#include <vector>
#include <set>
#include <string>
#include <map>
#include <random>
#include <json.hpp>
#include "object.h"      // Trackable base: Player/EnemyInst/CombatState/Game are tracked
#include "render_iface.h"
#include "game_state.h"  // toms::GameState — see Game::currentState()
#include "save_slots.h"  // toms::SlotSummary -- the Save / Load screen's slot cards
#include "save_system.h" // toms::MetaSaveData — see Game::meta_ (Milestone 3; disk persistence is Milestone 5's job)
#include "mission_system.h" // toms::MissionDefinition/MissionTracker — see Game::missionDefs_/missionTrackers_
#include "power_bar.h"
#include "camera.h"       // toms::Camera — maze camera (own file/class)       // toms::PowerBarParams/simulatePosition/... — see CombatState's Phase
#include "equipment_system.h" // toms::EquippedSet/EquipmentDefinition — see Game::equipped_/equipmentDefs_
#include "skill_system.h"     // toms::SkillDefinition — see Game::skillDefs_ (S4: the skill tree)
#include "forge_system.h"     // toms::ForgeRecipeDefinition — see Game::forgeDefs_ (S5: forging)
#include "hub_system.h"       // toms::HubLocationDefinition — see Game::hubDefs_ (S6: village hub)
#include "equipment_actives.h" // toms::ActiveDefinition — see Game::activeDefs_ (S7: equipment actives)
#include "ending_system.h"    // toms::EndingsTable — see Game::endingsTable_ (M7: the 15-ending resolver)
#include "cycle_system.h"     // toms::CyclesConfig — see Game::cyclesConfig_ (M8: rebirth)
#include "entity_status.h"   // toms::EntityStatus/entityStatusKey — see Game::entityStatus_
#include "roamer.h"          // toms::Roamer — S1: floor wanderers that chase the player
#include "run_state.h"       // toms::RunStoryState — S3: choices/counters/side stories/flags/shards
#include "talk_system.h"     // toms::talk -- NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md)
#include "floor_table.h"     // toms::FloorTable — S3.5: the 70-floor tower as ordered data
#include "stage.h"
#include "ui_state.h"       // toms::UiState -- what the UI shows (see buildUiState)
#include "title_screen.h"    // toms::TitleScreen/TitleAction — the title phase (New Game/Continue/Settings)
#include "game_settings.h"   // toms::GameSettings — persisted preferences (language, slots)
#include "fighter_view.h"     // toms::FighterView — a fighter on the battle screen, 2D or 3D (docs/gameplay/18)
#include "event_bus.h"       // toms::Subscription -- Game::busSubs_
#include "safe_timer.h"      // toms::TimerQueue / TimerOwner -- delayed calls that die with their owner (battle numbers)
#include "tween.h"           // toms::Tween -- the damage numbers' rise
#include "player_menu_rules.h" // toms::LevelPoints -- attribute points by level (stats.json levelUp.points)
#include "res_keys.h"        // toms::key / toms::pattern -- every file by key (assets/media/resources.json)
#include "weapon_play.h"     // toms::WeaponPlayTable -- data/weapon_play.json (docs/gameplay/07_GAMEPLAY_PLAN.md §5.6)
#include "combo_play.h"      // toms::ComboPlay -- Battle v3's point play (docs/gameplay/07 §4, §5.1)
#include "touch_play.h"      // toms::TouchPlay -- each weapon's own play: bow, slice, circles (docs/gameplay/15 §3.3)
#include "special_moves.h"          // toms::SpecialMoveTable -- special moves and special guards as data (docs/gameplay/16 §6)
#include "battle_party.h"    // toms::nextStanding, BattleEvent -- 1-3 monsters, the battle log (docs/gameplay/13 §4.6)
#include "battle_rules.h"    // toms::LooksTable, MonsterEvent, MovesTable, PaceSettings, BreakSettings (docs/gameplay/13)
#include "battle_cells.h"
#include "mini_game.h"       // toms::MiniGameTable -- reusable reward events (docs/gameplay/07 phase 6)
#include "art_styles.h"      // toms::ArtStyle — the selectable art styles (assets/media/styles)
#include "atlas_file.h"      // toms::AtlasFile — the prebuilt sprite atlas (tools/atlas)
#include "particle_fx.h"
#include "render_object.h"   // toms::RenderObject -- what a floor object draws for a status (map_objects.cpp)     // toms::fx — particle effects on the atlases (docs/engine/07_PARTICLES.md)
#include "anim_player.h"     // toms::anim — node animations on that atlas (anim_clip.h)
#include "resource_types.h"  // toms::res — the resource cache: atlases, clips, effects (docs/engine/12_RESOURCE_MANAGER.md)
#include "map_objects.h"     // MapObject / MapFloor — the floor's things, a class per kind (docs/engine/12 §4.10)
#include "localization.h"    // toms::Locale — key -> localized string (data/text.json)
#include "Audio.h"        // SFX (miniaudio) -- native device backends on desktop, Web Audio in the browser

struct Player : public Trackable {
    int m_iHp, m_iMaxhp, m_iAtk, m_iDef, m_iGold, m_iExp, m_iLv;
    int m_iKeyYellow = 0, m_iKeyBlue = 0, m_iKeyRed = 0;
    int m_iCrystals = 0;   // the second currency (docs/gameplay/07 phase 6): earned by the reward events (mini_game.h)
    int m_iX = 1, m_iY = 1;
    // inventory: list of item ids (a 9-grid, extendable UI). Keys/coins are NOT
    // stored here (they apply immediately); usable items (gems/potions/exp/scroll) are.
    std::vector<std::string> m_vInv;
    // The attributes of data/stats.json (str, dex, agi, ...) by id: base + level-ups + items used.
    // Worn equipment adds on top (Game::attrValue). Nothing in battle reads them yet.
    std::map<std::string, int> m_mapAttrs;
    int m_iAttrPoints = 0;   // attribute points not placed yet (stats.json levelUp.freePoints a level)
    Player() : m_iHp(100), m_iMaxhp(100), m_iAtk(10), m_iDef(5), m_iGold(0), m_iExp(0), m_iLv(1) {}
    TOMS_OBJECT(Player)
};

struct EnemyInst : public Trackable {
    std::string m_strId; std::string m_strName; int m_iHp, m_iAtk, m_iDef, m_iExp, m_iGold; int m_iX, m_iY; bool m_bBoss=false;
    int m_iWeaponExp = 1;   // what beating it gives the weapon used (enemies.json "weaponExp")
    std::vector<std::string> m_vStageParty;   // the stage's own company for it (objects."x,y".party); empty: enemies.json "party"
    // Battle System v2 (docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md): how often, in ms, this enemy attacks
    // on its own real-time clock -- independent of anything the player does. Read from
    // data/enemies.json's "atk_interval_ms" (engageMonster()); defaults to 4000 when absent.
    int m_iAtkIntervalMs = 4000;
    // The battle's rules for it (docs/gameplay/13; enemies.json, a scenario's "battle" can override each):
    std::string m_strMapId;                       // the map monster's id (a transformed monster keeps it: its tile is cleared)
    int m_iCellCols = 0, m_iCellRows = 0;         // "cellGrid": [cols, rows], its own grid (0: battle.json's)
    std::string m_strLook = "monster";            // "look": its status map in data/looks.json
    std::vector<toms::MoveChoice> m_vMoves;     // "moves": what it may do next (data/moves.json); none: a plain strike
    int m_iBreakPoints = 0;                     // "break": matched points that break it (0: data/battle.json's)
    nlohmann::json m_jEvents = nlohmann::json::array();   // "events": its monster events (13 §4.4)
    TOMS_OBJECT(EnemyInst)
};

struct DialogueNode : public Trackable {
    std::string m_strText; std::vector<std::pair<std::string,std::string>> m_vChoices;
    TOMS_OBJECT(DialogueNode)
};

// Milestone 4: a parsed dialogue choice, now carrying its optional `action` alongside the
// existing label/next — see Game::enterNode / Game::chooseDialogue / Game::runDialogueAction.
struct DialogueChoice {
    std::string m_strLabel, m_strNext;
    nlohmann::json m_jAction;   // null if the choice has no action (the common case today)
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 6.2): story actions (story_actions.h),
    // whether picking it finishes the dialogue's event, the events it wakes, and its id for `choiceMade`.
    nlohmann::json m_jActions;
    bool m_bFinish = false;
    std::vector<std::string> m_vFire;
    std::string m_strId;
};

// Battle System v2 (docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md): replaces the old strictly-alternating
// "Attack Bar then Defense Bar, press-and-hold-to-charge" round with three real-time clocks
// running simultaneously and independently for the whole fight:
//  - The Attack and Defense bars each move on their own, continuously, forever -- there is no
//    "your turn"; tapping either one (battleTapAttack/battleTapDefense) freezes its marker where
//    it is, resolves that action immediately from the current position, then that bar cools down
//    for kBarCooldownMs before resuming. Both are always available and independent of each other
//    (genuinely concurrent -- a two-finger tap on both at once, on touch, resolves both).
//  - A Defense tap doesn't need an incoming hit to exist yet: it immediately banks whatever power
//    it lands on as a "shield" (shieldBanked/shieldPower). The enemy's clock spends and clears it
//    whenever it next fires; a second tap before that just overwrites the banked value.
//  - The enemy fires on its own fixed timer (enemy.atkIntervalMs), ticked by enemyClockMs,
//    completely independent of either bar's state -- see Game::update()/resolveEnemyClockFire().
struct CombatState : public Trackable {
    // One auto-moving bar's live state. Reuses the exact same cubic ease-in speed curve and zone
    // geometry (toms::PowerBarParams) that today's Power Bar formulas already use -- only how the
    // marker is driven changes (auto-bounce + tap-to-freeze instead of hold-to-charge).
    struct AutoBar {
        float m_fPos = 0.0f;     // marker position, in the bar's own [0, 2*redOuter] units
        int m_iDir = 1;          // +1 or -1
        float m_fLegMs = 0.0f;   // ms since the marker's current leg (one edge-to-edge sweep) began
        bool m_bCooling = false; // true while frozen after a tap, waiting to resume auto-moving
        int m_iCooldownMs = 0;   // ms remaining in that cooldown
    };
    static constexpr int m_iBarCooldownMs = 1500;   // default for data/battle.json "bar_cooldown_ms"

    // The battle's monsters (docs/gameplay/13 §4.6; the owner 2026-10-25: one to three): [0] the lead (the map's
    // monster, whose tile a win clears), then its party. Each has its own HP and its own attack clock.
    struct Foe {
        EnemyInst m_oEnemy;
        int m_iHp = 0;
        int m_iClockMs = 0;       // counts up to moveMs, then it does its move and starts again
        // Its next move (docs/gameplay/13 §6.1): shown over it with the time left (its intent).
        std::string m_strMove = "strike";
        int m_iMoveMs = 4000;
        uint32_t m_uDanger = 0u;         // the cells its coming hit covers, lit (battle_cells.h); 0: not yet
        // A special attack's wind-up (battle.json feelFx "specialAttack", when its danger cells light): first its power-up
        // (the "power" clip, the camera on it; its clock waits powerMs), then it charges (the "charge" clip, energy drawn
        // in) until the hit, which goes off with a burst and its shout (f_UnleashFoePower).
        // What it started is bound to it (CLAUDE.md "Battle effects belong to their monster"): its particle emitters
        // (f_PlayBattleFx with it: its power-up, charge and release, its cast, its look's) and its sounds (f_PlayFoeSound).
        // When it falls each is checked (f_FoeEffectsOnDeath, battle.json "onFoeDeath"): stopped at once, faded, or kept
        // playing to its end. (Its animation is its rig's: it plays "die".)
        struct OwnedFx { std::string m_strName; toms::fx::EffectInstance m_oFx; };
        struct OwnedSound { uint32_t m_uId = 0; std::string m_strKey; };
        std::vector<OwnedFx> m_vFx;
        std::vector<OwnedSound> m_vSounds;
        int m_iPowerMs = 0;              // the power-up's time left
        bool m_bPowered = false;         // powering up or charging: its next hit is the special one
        int m_iChargeFxMs = 0;           // the next charge particles
        int m_iStaggerMs = 0;            // stunned (a parry, a break, a result's stun): its clock stops...
        float m_fStaggerDamage = 1.0f;   // ...and hits on it do this much more
        bool m_bBroken = false;          // the stun came from its break gauge (the intent shows "Broken!")
        float m_fBreakGauge = 0.0f;      // matched points so far (13 §6.4)
        float m_fAtkMul = 1.0f, m_fDefMul = 1.0f, m_fIntervalMul = 1.0f;   // monster events' stat changes
        int m_iAttacks = 0;              // its attacks so far (the "attacks" event)
        bool m_bFled = false;            // left the battle (no reward for it)
        std::vector<toms::MonsterEvent> m_vEvents;
        // its move's rhythm (docs/gameplay/16 §4.1): the hit of a run of hits (0: none running) and the time to it
        int m_iVolleyHit = 0, m_iVolleyMs = 0;
        float m_fGuardShare = -1.0f;     // a special guard play's result for the hit waiting on it (-1: none played)
        bool f_Down() const { return m_iHp <= 0; }
    };
    std::vector<Foe> m_vFoes;
    int m_iTarget = 0;            // the foe the player's attacks go to (tap a monster, or Left / Right)
    int m_iPlayerHP = 0;
    Foe& f_Lead() { return m_vFoes.front(); }
    const Foe& f_Lead() const { return m_vFoes.front(); }
    std::vector<int> f_FoeHp() const { std::vector<int> l_vV; for (const Foe& l_oF : m_vFoes) l_vV.push_back(l_oF.m_iHp); return l_vV; }
    // What happened, in order (battle_party.h): the battle editor's log, and what monster events will listen to.
    std::vector<toms::BattleEvent> m_vEvents;
    int m_iClockMs = 0;           // battle time (while it is active)
    // The pace (docs/gameplay/13 §6.3): cooldowns after a tap (as before), or stamina that both actions spend.
    toms::Pace m_ePace = toms::Pace::Cooldown;
    float m_fStamina = 0.0f;
    bool m_bActive = false;
    std::string m_strLog;          // last exchange text
    bool m_bWon = false;

    AutoBar m_oAtkBar, m_oDefBar;
    // Battle v3 (docs/gameplay/07 phase 1): the attack is the point play. While the attack waits (not cooling, no combo), charge
    // builds up (chargeMs); the first tap starts the combo with it.
    toms::ComboPlay m_oCombo;
    // each weapon's own play (touch_play.h; weapon_play.json "play"): armed over the target when the attack is ready,
    // started by its first touch, which takes the charge so far (touchCharge) and the stamina (touchPaid)
    toms::TouchPlay m_oTouch;
    float m_fTouchCharge = 0.0f;
    bool m_bTouchPaid = false;
    int m_iChargeMs = 0;
    bool m_bShieldBanked = false;
    float m_fShieldPower = 0.0f;

    // Super-Attack Gauge: +1 per successful Attack tap, unlocks a guaranteed strong hit
    // (battleTapSuper()) at RunStoryState::superMax() (M8: per-run, halved by rebirth() -- no
    // longer a compile-time constant here), then resets.
    int m_iSuperCharge = 0;

    // Equipment actives (2026-09-17: rewired onto the richer equipment_actives.h pure-logic
    // layer -- real per-active uses-per-battle + cooldown + st_echo/st_silence status hooks,
    // reconciled from a parallel branch's implementation of the same S7 slice this session's own
    // active_system.h had stubbed more simply). activeRuntime is reset fresh every battle by
    // toms::makeBattleRuntime(activeDefs_); activeStatus is reset too since nothing anywhere sets
    // st_echo/st_silence yet (a real, still-open gap, not this rewire's job to close).
    std::map<std::string, toms::ActiveRuntime> m_mapActiveRuntime;
    toms::ActiveStatusEffects m_oActiveStatus;
    // "Armed" state for the two effect kinds this project's actives use -- guaranteedCrit arms the
    // NEXT attack (consumed by resolveAttackTap()), surviveLethal arms against the NEXT lethal hit
    // (consumed by resolveEnemyClockFire()). Both come from toms::ActiveEffect, not stored there
    // directly, since CombatState -- not equipment_actives.h -- owns how/when an effect actually
    // applies to this game's specific attack/defense-tap combat model.
    bool m_bNextAttackGuaranteedCrit = false;
    float m_fNextAttackCritDamageMult = 1.0f;
    bool m_bSurvivalArmed = false;

    // resultPauseMs: only needed for a LOSE (finishCombatLose() sets active=false immediately,
    // but the "you fell" message should stay on screen briefly rather than vanish the same
    // frame) -- a WIN uses `won` to stay open until dismissed instead. See draw()'s showBattle.
    int m_iResultPauseMs = 0;
    // Phase 0 (docs/gameplay/07_GAMEPLAY_PLAN.md §4, the owner's rule 2026-10-20): while the player or the enemy is attacking,
    // BOTH attack clocks pause -- the bars do not move or cool down and the enemy's clock does not run -- until the
    // attack is over. Set by an attack (data/battle.json "attack_pause_ms"), counted down by Game::update.
    int m_iAttackPauseMs = 0;
    // Damage numbers rising over a fighter: added when the hit lands (a safe timer after the attack starts).
    // A damage number or a word rising over a fighter. who: -1 the player, 0.. a foe. style: battle.rcss's .popup.<style>
    // (hit, great, perfect, miss, guard; the words parry, block, early, chain, move-<tier>); lift: words sit higher.
    struct Popup { std::string m_strText; int m_iWho = 0; std::string m_strStyle = "hit"; int m_iLift = 0; int m_iAgeMs = 0; toms::Tween m_oRise; };
    std::vector<Popup> m_vPopups;
    // ---- the revamp (docs/gameplay/15_BATTLE_REVAMP.md) ----
    int m_iFrameDt = 0;               // the battle's time this frame: 0 in a hit stop, slower in the last hit's slow motion
    int m_iHitStopMs = 0;             // both sides freeze (15 §3.8)
    int m_iCellIntroMs = 0;           // the first danger's moment: time left; everything waits, only the monster moves
    bool m_bCellIntroStarted = false, m_bCellsShown = false;   // (the cells stay hidden until it reveals them)
    int m_iCellsPopMs = 0;            // how long since they popped up
    bool m_bCellsOn = false;          // a monster here has moves with a danger shape: the hero moves between cells (docs/gameplay/19 §2.1)
    int m_iHeroCell = 4;
    int m_iCellCols = 3, m_iCellRows = 2;   // this fight's grid
    int m_iSlowMs = 0;                // the last hit in slow motion, then the win
    int m_iEndLeftMs = 0;             // the longest the end may still wait for its clips (battle.json feel.endMaxMs)
    bool m_bLosePending = false;      // m_bWinPending's end is a loss: the hero falls, then finishCombatLose
    bool m_bEndClipDone = false;      // the end clip played out (or there is none to play)
    float m_fSlowScale = 1.0f, m_fSlowCarry = 0.0f;
    bool m_bWinPending = false;       // won, the slow motion still running (finishCombatWin follows)
    float m_fShakePx = 0.0f;          // the screen shakes this far, fading over shakeMsMax
    int m_iShakeMs = 0, m_iShakeMsMax = 1;
    int m_iDimMs = 0;                 // a break dims the screen a moment
    std::string m_strBanner, m_strBannerKind;   // across the screen: "BREAK!" (break), a monster's line (line), the ultimate (ultimate)
    int m_iBannerMs = 0, m_iBannerMsMax = 1;
    int m_iComboCount = 0;   // hits in a row: which comboTiers row plays (docs/gameplay/17 §19)
    int m_iChain = 0, m_iBestChain = 0;  // perfects in a row (15 §3.4); a miss resets it
    int m_iPerfects = 0, m_iAttacksDone = 0, m_iDamageTaken = 0;   // for the grade
    int m_iGuardAtMs = -1;            // the timed guard: when it was pressed (battle time), -1 none
    int m_iGuardCdMs = 0;
    int m_iGuardLockMs = 0;           // the "warn" guard pressed with no "!": locked a moment (docs/gameplay/16 §4.2)
    bool m_bUltimate = false;         // the running combo is the ultimate
    int m_iPlayDealt = 0;             // what the running play's answers dealt at once (its finish deals the rest; 16 §3)
    // A skill running (docs/gameplay/16 §6; special_moves.json): the player's move, or a monster's special guard play.
    struct SkillRun {
        bool m_bOn = false;
        const toms::SpecialMove* m_pDef = nullptr;   // in Game::specialMoves_
        int m_iOwner = -1;                        // -1: the player's move; 0..: the monster whose hit it answers
        enum class Phase { Demo, Play, Finish } m_ePhase = Phase::Demo;
        int m_iPhaseMs = 0;
        int m_iSteps = 0;                         // steps done in the play
        float m_fShare = 0.0f;                    // steps / all, when the play ended
        int m_iHitsLeft = 0, m_iHitGapMs = 0, m_iHitMs = 0, m_iHitDmg = 0;   // the finish's hits
    } m_oSkill;
    // the battle camera's close-up (16 §6.1): zoom on a fighter, eased in and out
    float m_fCamZoom = 1.0f;
    int m_iCamMs = 0, m_iCamMsMax = 0, m_iCamOn = -1;
    // The hero knocked down by a monster's special attack (battle.json feelFx "specialAttack" "downMs"): his "hurt_big"
    // clip plays and no attack can start (the track, the weapon's play, the ultimate, the active) until this is 0.
    int m_iHeroDownMs = 0;
    std::string m_strCamMove;   // on a 3D stage: "" push in, "orbit" (special_moves.json camera "move")
    float m_fCamDeg = 30.0f;
    // The result screen of a win (15 §3.6): filled by finishCombatWin.
    struct Result {
        bool m_bValid = false;
        std::string m_strGrade;         // S / A / B / C
        int m_iScore = 0;
        float m_fSeconds = 0.0f;
        int m_iGold = 0, m_iExp = 0;
        std::string m_strCard;          // its line ("New card: Slime"), or ""
        std::string m_strWeapon;        // the weapon's key in weapon_play.json
        int m_iLvBefore = 1, m_iLvAfter = 1, m_iWeaponExp = 0, m_iWeaponExpGained = 0;
    } m_oResult;
    TOMS_OBJECT(CombatState)
};

// A store entry, parsed from data/store.json: which item of data/items.json it sells and how its
// price grows. cost = the item's "price" * growth^purchases (growth 2 doubles it each time, 1 keeps it).
struct StoreItemDef {
    std::string m_strId;            // an id in data/items.json (a consumable, or a weapon/armor/talent)
    int m_iPrice = 2;             // the item's "price", copied when the store loads
    int m_iGrowth = 2;
    int m_iPurchases = 0;         // how many times already bought (drives the price growth)
    int f_LiveCost() const { int l_iC = m_iPrice; for (int l_iI = 0; l_iI < m_iPurchases; l_iI++) l_iC *= m_iGrowth; return l_iC; }
};

// M9 (stair alignment): which tile a stage load should place the player on. Every floor's
// stairs_down is now forced to sit exactly where the previous floor's stairs_up landed (see
// docs/story/STAIR_ALIGNMENT.md), so arriving via a specific staircase has a real, correct tile
// to land on instead of always falling back to the floor's '@'/default spawn.
enum class StageArrival { Fresh, FromBelow, FromAbove };

class Game : public Trackable {
    // The floor's objects run the rules of their kind (touching a monster starts the battle, ...) and draw themselves:
    // they reach into the game's state the way the code they came from (movePlayer, draw) did.
    friend class MapObject;
    friend class MonsterObject;
    friend class ItemObject;
    friend class EventObject;
    friend class NpcObject;
    friend class GateObject;
    friend class DoorObject;
    friend class PlayerObject;
    friend class MapFloor;
    friend class map_detail::RenderLook;
    friend struct map_detail::Builder;
public:
    bool f_LoadAssets(const std::string& l_strAssetDir);
    void f_LoadStage(const std::string& l_strId, StageArrival l_eArrival = StageArrival::Fresh);
    void f_Update(int l_iDtMs);                 // advances combat timer etc.
    void f_Draw();                          // render the world (map, entities, player) -- UI is RmlUi
    // A click/tap that no UI element took (design space 1024x768; phase 0 = down, 2 = up): on the
    // map it walks there (click-to-move) or, on the player's own tile, interacts.
    void f_HandleTouch(float l_fPx, float l_fPy, int l_iPhase);
    // ---- UI (RmlUi, see ui/ui_state.h) ----
    // buildUiState(): what every screen shows this frame. uiEvent(): a button in one of the
    // assets/media/ui/*.rml documents was pressed ("inv_use", "menu_row" with the row, ...).
    void f_BuildUiState(toms::UiState& l_oOut) const;
    void f_UiEvent(const std::string& l_strName, int l_iArg);
    void f_SaveFrame(const std::string& l_strPath);
    // input (scripted for headless)
    void f_MovePlayer(int l_iDx, int l_iDy);
    // Milestone 9 polish: "keep moving while held" instead of exactly one tile per key
    // press/tap -- classic dungeon-crawler feel. Call every frame with the currently-held
    // direction on each axis (dir in {-1,0,1}, 0 = not held): GameSession does this for the
    // keyboard (level state, not the edge-triggered up/down/left/rightPressed used for
    // menu/inventory-cursor navigation, which should NOT auto-repeat), handleTouch() does it
    // for the virtual/touch d-pad (0 on press-release). X and Y repeat independently -- both
    // held at once keeps moving diagonally, same as the old one-tap version allowed. The
    // actual repeat timer lives in update(); this only registers what's currently held and
    // fires the immediate first step on a fresh press/direction-change.
    void f_SetMoveHeldX(int l_iDir);
    void f_SetMoveHeldY(int l_iDir);
    void f_StopMoveHeld() { f_SetMoveHeldX(0); f_SetMoveHeldY(0); }
    // Click-to-move: a tap/click on a map tile walks there (handleTouch). The route is a shortest
    // path over plain floor only -- it never opens a door, fights, picks up, talks or takes stairs
    // on the way -- while the clicked tile itself may be anything: the last step goes into it
    // exactly like an arrow-key step would (a monster = fight, a door = open with a key, ...).
    // One step every kMoveRepeatMs. Any key/d-pad move, a new click, a modal (battle, dialogue,
    // stairs confirm) or a stage change cancels it. False when the tile can't be reached.
    bool f_WalkTo(int l_iTx, int l_iTy);
    void f_CancelWalk() { m_vWalkPath.clear(); m_iWalkTargetX = m_iWalkTargetY = -1; m_iWalkTimerMs = 0; }
    bool f_Walking() const { return !m_vWalkPath.empty(); }
    // Design-space point -> the map tile drawn there (same camera maths as draw()).
    bool f_ScreenToTile(float l_fPx, float l_fPy, int& l_iTx, int& l_iTy) const;
    void f_Interact();                       // talk to NPC / trigger dialogue on current cell
    void f_ChooseDialogue(int l_iIdx);          // pick a dialogue choice
    // Bugfix: the old desktop main never had any keyboard binding that moved dlgSel at all -- with
    // only ever one choice ever shown before this session's content, that went
    // unnoticed; now that real dialogue has 2-5 choices, the player had no way to
    // reach anything but choice 0. Wraps like invMoveSel's cursor.
    void f_DlgMoveSel(int l_iDelta) {
        int l_iN = (int)m_vDlgChoices.size();
        if (l_iN <= 0) return;
        m_iDlgSel = ((m_iDlgSel + l_iDelta) % l_iN + l_iN) % l_iN;
    }
    void f_StartDialogue(const std::string& l_strNpc);   // open a dialogue file by id (public for tests)
    // Talk to an NPC (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): a chat line, one topic, or the topic menu.
    void f_TalkTo(const std::string& l_strNpcId);
    std::string f_DialogueSpeaker() const { return m_strDlgSpeaker; }
    void f_EnterNode(const std::string& l_strNode);      // jump to a dialogue node (public for tests)
    void f_StartCombat(const EnemyInst& l_oE, const std::vector<EnemyInst>& l_vParty = {});   // party: up to 2 more (kMaxFoes)
    // Verification hooks (debugStartNearestBattle .. debugWarpPlayer): the old web entry exposed
    // them to a page test harness. No host calls them now; a future test harness can re-expose them.
    // Start a fight with the nearest monster, to exercise the battle scene without walking the maze.
    bool f_DebugStartNearestBattle();
    // Verification hook: equip an item directly by id,
    // bypassing the Store/Forge entirely, so a page harness can reach equipment (and whatever
    // actives it grants) that isn't purchasable/craftable through any content authored yet.
    // Silently does nothing for an unknown id, matching every other debug hook's fail-soft rule.
    bool f_DebugEquip(const std::string& l_strEquipmentId);
    // Test hook (toms_game --give=...): an item reaches the player as if picked up (receiveItem); "gold:<n>" adds gold, "exp:<n>" EXP (with its level-ups), "points:<n>" attribute points to place.
    void f_DebugGive(const std::string& l_strId);
    // Verification hook: runs the exact same
    // finishCombatLose() path a real battle loss would (noteDeath, the e_10/e_13 wipe-check,
    // ending trigger) without needing to actually lose `deathsNonBoss` real fights first.
    void f_DebugForceLose();
    // Verification hook: sets a run flag directly, skipping
    // whatever real trigger (a floor event tile, a dialogue action) would normally set it, so a
    // page harness can reach a flag-gated dialogue choice without re-driving an already-proven
    // upstream mechanic.
    void f_DebugSetFlag(const std::string& l_strFlag) { m_oRun.f_SetFlag(l_strFlag); }
    // Verification hook: sets the player's tile
    // directly, skipping a full maze walk, so a page harness can stand next to a specific stairs
    // tile and then take ONE real step onto it -- exercising the real transition trigger
    // (movePlayer's 'U'/'D' check) and the real arrival logic (loadStage's StageArrival) without
    // having to path through an entire generated maze (and its monsters) first.
    void f_DebugWarpPlayer(int l_iX, int l_iY) { m_oPl.m_iX = l_iX; m_oPl.m_iY = l_iY; }
    // Battle System v2 (see CombatState's comment): each is a single instantaneous tap, safe to
    // call any time -- a no-op when combat isn't active or that specific bar is still cooling
    // down / the Super gauge isn't full, so callers (GameSession, handleTouch)
    // don't need to track any battle state themselves, just forward the tap.
    void f_BattleTapAttack();
    void f_BattleTapDefense();
    void f_BattleTapSuper();
    // The pointer over the battle (design pixels; 0 down, 1 held, 2 up), every frame: the weapon's own play
    // (touch_play.h) reads it. The session leaves out a press that lands on a button (battle.rml .btn).
    void f_BattlePointer(int l_iPhase, float l_fX, float l_fY);
    // S7 (equipment actives, first slice): the 4th battle action -- a no-op unless the currently
    // equipped gear grants one (equippedActives()) and it hasn't been spent this battle already.
    void f_BattleTapActive();
    // Several monsters (docs/gameplay/13 §4.6): the target is the next (+1) / previous (-1) monster standing, or that one.
    void f_BattleCycleTarget(int l_iDir);
    void f_BattleSetTarget(int l_iFoe);
    const std::vector<toms::BattleEvent>& f_BattleEvents() const { return m_oCs.m_vEvents; }
    // The pace (docs/gameplay/13 §6.3): "cooldown" / "stamina" for the battles from now on (and the one running); ""
    // goes back to the weapon's or data/battle.json's. The battle editor's switch; a scenario's "battle": {"pace"}.
    void f_SetBattlePace(const std::string& l_strPace);
    std::string f_BattlePace() const { return toms::f_PaceName(m_oCs.m_ePace); }
    // The battle editor: end the battle as won (every monster falls) / fire a monster's event now (its index in
    // enemies.json "events"). Both no-ops without a battle.
    void f_DebugForceWin();
    // Settings > "Reduce shaking and flashes" (docs/gameplay/15 §3.8): hit stops, shakes and flashes a quarter as
    // strong, no slow motion. Saved in the settings file.
    bool f_ReduceEffects() const { return m_oSettings.m_bReduceEffects; }
    bool f_BattleCellsOn() const { return m_oCs.m_bActive && m_oCs.m_bCellsOn; }
    bool f_CellIntroOn() const { return m_oCs.m_iCellIntroMs > 0; }
    void f_StartCellIntro(int l_iFoe);
    void f_BattleMoveToCell(int l_iCell);                // the hero steps there (docs/gameplay/19 §2.1)
    void f_BattleStepCell(int l_iDx, int l_iDy);         // by the arrow keys
    bool f_FreePlayRunning() const { return m_oCs.m_oTouch.f_Running() && toms::f_IsFreePlay(m_oCs.m_oTouch.f_Type()); }
    void f_SetReduceEffects(bool l_bOn);
    void f_DebugFireFoeEvent(int l_iFoe, int l_iEvent);
    int f_BattleFoeCount() const { return m_oCs.m_bActive ? (int)m_oCs.m_vFoes.size() : 0; }
    // The battle editor: a monster's enemies.json record, edited while it fights. Every foe of that id in the running
    // battle takes the new numbers at once: HP (its current HP keeps its share), ATK, DEF, the attack interval (the
    // wind-up under way keeps how far along it is), moves, look, break points and events. No battle: nothing.
    void f_DebugUpdateMonster(const std::string& l_strId, const nlohmann::json& l_jRecord);
    int f_BattleFoeMoveMs(int l_iFoe) const {   // the wait before its next move (the editor's check)
        return m_oCs.m_bActive && l_iFoe >= 0 && l_iFoe < (int)m_oCs.m_vFoes.size() ? m_oCs.m_vFoes[(size_t)l_iFoe].m_iMoveMs : 0;
    }
    std::vector<std::string> f_BattleFoeIds() const {   // the log's names (battleEventLine)
        std::vector<std::string> l_vV;
        for (const CombatState::Foe& l_oF : m_oCs.m_vFoes) l_vV.push_back(l_oF.m_oEnemy.m_strId);
        return l_vV;
    }
    void f_SetBattleLogToStderr(bool l_bOn) { m_bBattleLogToStderr = l_bOn; }   // --battle-log: every battle event as it happens
    // ---- the player menu (docs/gameplay/03_PLAYER_MENU.md): the walking scene's one button (≡, Esc) ----
    // Five tabs: Status, Equipment, Items, Events, System. Everything that was a button of its own
    // (the backpack, the store button, the old ≡ list, the web page's fullscreen button) is in here.
    enum class MenuTab { Status, Gear, Items, Events, Cards, System };   // player.rml shows them by number
    static constexpr int m_iMenuTabs = 6;
    bool f_PlayerMenuOpen() const { return m_bInGameMenuOpen; }
    MenuTab f_PlayerMenuTab() const { return m_eMenuTab; }
    void f_OpenPlayerMenu(int l_iTab = -1);     // -1: the last tab used
    void f_ClosePlayerMenu();
    void f_PlayerMenuTab(int l_iDelta);         // Q / E / Tab: the previous / next tab
    void f_PlayerMenuMove(int l_iDx, int l_iDy);   // arrows: move the selection on the current tab
    void f_PlayerMenuActivate();             // Enter: the selected entry's first action
    void f_PlayerMenuBack();                 // Esc: confirm -> sub-page -> close
    // I: the Items tab (closes the menu when it is already on Items). The web harness calls it too.
    void f_ToggleInventory();
    bool f_InventoryOpen() const { return m_bInGameMenuOpen && m_eMenuTab == MenuTab::Items; }
    // Save / Load: its own screen over the menu (System tab -> Save / Load). Every save and every load
    // asks first. While it is open the player menu's keys drive it (playerMenuMove/Activate/Back/Tab).
    bool f_SaveLoadOpen() const { return m_bSaveLoadOpen; }
    const std::vector<std::string>& f_Inventory() const { return m_oPl.m_vInv; }
    // The player's attribute `id` (data/stats.json) with worn equipment included.
    int f_AttrValue(const std::string& l_strId) const;
    const std::vector<std::string>& f_GearOwned() const { return m_vGearOwned; }
    // Fullscreen lives on the host (an SDL window, the browser page): the host reports whether it
    // can and whether it is on, and takes the player's request from the System tab's row.
    void f_SetFullscreenState(bool l_bAvailable, bool l_bOn) { m_bFullscreenAvailable = l_bAvailable; m_bFullscreenOn = l_bOn; }
    bool f_TakeFullscreenRequest() { const bool l_bR = m_bFullscreenRequest; m_bFullscreenRequest = false; return l_bR; }
    // Milestone 7 bugfix: cs.won (the post-victory "press any key to continue" pause) must count
    // as a modal overlay too, not just cs.active -- otherwise the world underneath (movement,
    // NPC interact, the store icon, Tab/B shortcuts) keeps responding to input while the victory
    // screen is still up. See docs/project/progress_report/PROGRESS_REPORT.md's Milestone 7 log for the report this fixes.
    bool f_ModalActive() const { return m_oCs.m_bActive || m_oCs.m_bWon || m_oMiniGame.m_bOpen || m_bInDialogue || m_bStoreOpen || m_bStoreUnlockDlg || m_bStageSelectOpen || m_bStairsConfirmOpen || m_oTitle.f_IsOpen() || m_bInGameMenuOpen || f_EndingActive() || !m_vNotices.empty(); }  // any overlay open (combat/dialogue/inventory/store/stage-select/stairs-confirm/title/in-game menu/ending)
    bool f_CombatWon() const { return m_oCs.m_bWon; }
    bool f_CombatActive() const { return m_oCs.m_bActive; }
    // Dismisses the post-victory pause (mirrors handleTouch's existing tap-to-dismiss) -- the
    // keyboard path (Enter/Space) had no equivalent before this fix, so "press any key to
    // continue" never actually worked from a keyboard.
    void f_DismissVictory() { m_oCs.m_bWon = false; m_oCs.m_strLog.clear(); }
    bool f_InDialogueFlag() const { return m_bInDialogue; }
    int f_DialogueSel() const { return m_iDlgSel; }
    int f_DialogueChoiceCount() const { return (int)m_vDlgChoices.size(); }
    std::string f_DialogueNode() const { return m_strDlgNode; }
    bool f_StoreModal() const { return m_bStoreOpen || m_bStoreUnlockDlg; }  // store overlay (incl. unlock dialog) is the topmost modal
    // store system
    bool f_StoreUnlocked() const { return m_bStoreUnlocked; }
    bool f_StoreOpenFlag() const { return m_bStoreOpen; }
    const std::vector<StoreItemDef>& f_StoreItems() const { return m_vStoreItems; }
    int f_StoreSel() const { return m_iStoreSel; }
    const std::string& f_ToastMsg() const { return m_strToastMsg; }
    void f_StoreKey(int l_iKey);                // keyboard nav/confirm inside the store (and its unlock popup)
    void f_OpenStore();                      // open the store overlay
    void f_CloseStore();                     // close the store overlay
    // Milestone 5: Stage Select hub -- every floor the player has ever reached becomes a
    // replayable, individually-selectable entry (architecture-doc §10). Purely additive: does
    // not change the existing boot flow (still auto-loads stage01), only adds an in-game hub.
    bool f_StageSelectOpen() const { return m_bStageSelectOpen; }
    void f_OpenStageSelect();
    void f_CloseStageSelect();
    // Milestone 9: a maze's dead ends force backtracking through the same corridor
    // (Wilson's algorithm produces a tree -- there's no alternate route around
    // anything), and a stairs tile sitting on that backtrack path could get walked
    // over by accident (owner report). Reaching 'U'/'D' now opens a Yes/No confirm
    // instead of transitioning immediately -- see movePlayer()'s call to
    // requestStageTransition() and confirmStageTransition()/cancelStageTransition().
    bool f_StairsConfirmOpen() const { return m_bStairsConfirmOpen; }
    // Q7: an OK popup ("Stairs unlocked: the stairs on floor 3 ..."), queued; Enter / Esc / its button close it.
    bool f_NoticeOpen() const { return !m_vNotices.empty(); }
    void f_DismissNotice();
    // Test hooks (toms_game --stair-lock=F01:ev_x, --events-done=ev_a,ev_b): lock a floor's first stair up behind
    // an event; finish events as if the player had (the unlock dialog included).
    void f_DebugLockStairs(const std::string& l_strFloor, const std::string& l_strEventId);
    void f_DebugFinishEvent(const std::string& l_strEventId);
    bool f_StairsConfirmIsUp() const { return m_bStairsConfirmIsUp; }
    void f_ConfirmStageTransition();
    void f_CancelStageTransition();
    // Maze camera mode (see toms::Camera::Mode in camera.h). Applying a change re-targets the
    // camera immediately (no confirm dialog needed -- unlike language, this is a low-stakes,
    // instantly-visible, freely-reversible preference).
    int f_CameraModeIndex() const { return m_oCam.f_ModeIndex(); }
    void f_SetCameraModeIndex(int l_iM);
    // The player menu's System tab (the old in-game menu): Store, Skills, Forge, Village, Save,
    // Settings, Fullscreen, Back to title. inGameMenuOpen() is true while the player menu is open.
    bool f_InGameMenuOpen() const { return m_bInGameMenuOpen; }
    void f_OpenInGameMenu();             // the player menu on its System tab
    void f_InGameMenuMove(int l_iDelta);    // Up/Down or Left/Right
    void f_InGameMenuActivate();         // Enter/Space
    void f_InGameMenuBack();             // Esc: dialog->list, Settings->Main, Main->closed
    Player& f_Player() { return m_oPl; }
    IRenderer* f_Renderer() { return m_pRen; }   // for batch-metric inspection (demo)
    // The resource cache (made in loadAssets). Hot reload: files that change on disk (art, clips, effects) are read
    // again about once a second -- the editors and debug builds turn it on; a shipped game never looks.
    toms::res::ResourceManager* f_Resources() { return m_upRes.get(); }
    void f_SetHotReload(bool l_bOn) { m_bHotReload = l_bOn; }
    void f_ShowNotice(const std::string& l_strMsg) { f_PushNotification(l_strMsg); }   // a few seconds at the top (live UI reload)
    // The UI editor's language switch: the text in another language, without touching settings.json.
    void f_PreviewLanguage(const std::string& l_strCode) { m_oLocale.f_SetLanguage(l_strCode); m_oTitle.f_SetLanguageIndex(m_oLocale.f_LanguageIndex()); }

    // ---- Title phase (Boot screen: New Game / Continue / Settings) ----
    // The title phase is the game's boot screen: Game::loadAssets() opens it, and nothing in
    // the world responds to input until the player picks something (modalActive() includes it).
    // New Game resets progress and loads stage01; Continue lists the save slots on disk and
    // resumes one; Settings currently offers the language. See src/game/title_screen.h for the
    // page/selection state machine and docs/design/TITLE_PHASE.md for the flow.
    bool f_TitleOpen() const { return m_oTitle.f_IsOpen(); }
    void f_SetShowVersion(bool l_bOn) { m_bShowVersion = l_bOn; }   // the title's corner; off in tests, or their reference pictures change with each publish
    const toms::TitleScreen& f_Title() const { return m_oTitle; }
    // Keyboard input, in the same shape the other overlays use: a direction move, a confirm and
    // a cancel. Each routes to the current title page (mouse/touch: uiEvent("title_row", ...)).
    void f_TitleMove(int l_iDx, int l_iDy);
    void f_TitleConfirm();
    void f_TitleCancel();
    // Starts a brand new run: default player stats, cleared meta/story/entity progress, and a
    // fresh slot (or the lowest-numbered slot when all are taken). Writes the slot immediately.
    // The overload starts the run in a specific slot -- what the Continue page's "start a new
    // game in this (empty) slot?" dialog needs.
    void f_NewGame();
    void f_NewGame(int l_iSlot);                // slot -1: a test scenario's run -- no slot, never written
    // ---- Test scenarios (docs/gameplay/08_TEST_SCENARIOS.md, game_scenario.cpp) ----
    // Starts the game straight in one scene from a scenario file (or a folder holding scenario.json): an optional save,
    // the floor, the player, finished events and flags, then the scene itself (a battle, a dialogue, the store ...).
    // False (with the reason) when the file cannot be read or names something that does not exist.
    bool f_StartScenario(const std::string& l_strPath, std::string& l_strError);
    // The same from a setting already read (toms_game --arena builds one); file names in it must be absolute.
    bool f_StartScenarioJson(const nlohmann::json& l_jScenario, const std::string& l_strName, std::string& l_strError);
    bool f_ArenaOn() const { return m_oArena.m_bOn; }
    // After the run: is the scene the scenario expects showing, and do its "expect" values hold? report says what failed.
    bool f_ScenarioCheck(std::string& l_strReport) const;
    // The scenario's "test" block: frames to run and scripted keys (toms_game --scenario-test), or 0 / "".
    int f_ScenarioTestFrames() const { return m_iScenarioTestFrames; }
    const std::string& f_ScenarioTestKeys() const { return m_strScenarioTestKeys; }
    const std::string& f_ScenarioTestClicks() const { return m_strScenarioTestClicks; }
    const std::string& f_ScenarioTestDrags() const { return m_strScenarioTestDrags; }
    // Resumes slot N (1-based). Returns false (and leaves the title open) when that slot has no
    // save, so the caller can show "no save here" instead of dropping the player into nothing.
    bool f_ContinueFromSlot(int l_iSlot);
    // Re-reads the slot files into the title's Continue list.
    void f_RefreshSlots();
    // Writes the current run to activeSlot() atomically (no-op when no slot is active).
    void f_SaveCurrentRun();
    // Applies settings_.language to the string table and persists settings.json.
    void f_ApplyLanguage();
    // Remembers art style `idx` (into artStyles()) in settings.json. On the title screen it is
    // applied at once (refreshArtStyle); chosen during a run, it applies on the way back to the title.
    void f_ApplyArtStyle(int l_iIdx);
    // Loads the chosen style's sprites if they are not the ones in use -- only while the title
    // screen is up (returnToTitle and the title's Settings page call it).
    void f_RefreshArtStyle();
    const std::vector<toms::ArtStyle>& f_ArtStyles() const { return m_vArtStyles; }
    int f_LoadedArtStyle() const { return m_iLoadedArtStyle; }                  // in use since startup
    // The prebuilt sprite atlas in use (tools/atlas): look a sprite up by name for its child rect,
    // pivot, 9-slice or tags. Empty when the game fell back to the runtime grid.
    const toms::AtlasFile& f_SpriteAtlas() const { return m_oSpriteAtlas; }
    // Developer/test hook (toms_game --anim=<file>#<clip>): plays one clip of an .anim file over
    // whatever is on screen, centred, looping; its events are logged. Draws with the real
    // AnimPlayer through the sprite batch, so a screenshot shows exactly what the game would draw.
    bool f_PlayPreviewAnim(const std::string& l_strFile, const std::string& l_strClip, std::string& l_strError);
    // The character a character clip (docs/engine/05_ANIMATION.md "Characters": `body` / `hand` nodes) is shown with: a sprite
    // of the game atlas ("player_mage"; toms_game --character=). "" = the clip as made. Set before playPreviewAnim.
    void f_SetPreviewCharacter(const std::string& l_strSprite) { m_strPreviewCharacter = l_strSprite; }
    // A held thing's clips (an .anim file, e.g. media/anim/weapon_bow.anim; toms_game --held=): its clip of the preview
    // clip's name is drawn at the character clip's `hand` (CharacterRig). "" = none. Set before playPreviewAnim.
    void f_SetPreviewHeld(const std::string& l_strFile) { m_strPreviewHeldFile = l_strFile; }
    // A benchmark (toms_game --anim-crowd=N): the preview clip N times in a grid, each its own AnimPlayer started a little
    // later than the one before, drawn the way the game draws clips (AnimPlayer::draw). Set before playPreviewAnim.
    void f_SetPreviewCrowd(int l_iN) { m_iPreviewCrowdN = l_iN; }
    // The CPU time of the last frame's preview drawing (the crowd or the one clip), milliseconds.
    double f_PreviewDrawMs() const { return m_dPreviewDrawMs; }
    // The battle picture of a weapon with a "battleAnim" (equipment_system.h; docs/engine/05_ANIMATION.md "Characters"): the
    // player's character clip with the weapon's clip at its hand, both on the clip the battle's status picks --
    //   charge   the attack bar waits for the tap (held, shaking, for as long as it waits)
    //   attack   a tap that hit; miss: a tap that dealt nothing (played to its end)
    //   idle     the bar recovers for the next round (and after the fight)
    // It replaces the battle screen's player picture. battleRigState(): the status shown ("" = none: no such weapon).
    const std::string& f_BattleRigState() const { return m_strBattleRigState; }
    bool f_BattleRigReady() const;   // the equipped weapon has battle clips and they loaded
    // A character clip played by `sprite`: fitted to its visible pixels (fitCharacter), plus its nudge from
    // assets/data/character_fit.json ({"player_mage": {"hand": [x, y]}}, pixels; none = no nudge).
    toms::anim::Clip f_FitClipToCharacter(const toms::anim::Clip& l_oClip, const std::string& l_strSprite) const;
    // The same for a particle effect (toms_game --fx=<file>#<effect>): centred, a one-shot effect
    // starts again 0.5 s after it ends. The seed is the effect's, else 1, so screenshots repeat.
    bool f_PlayPreviewFx(const std::string& l_strFile, const std::string& l_strEffect, std::string& l_strError);
    // The renderer's triangle meshes (IRenderer::drawMesh, docs/engine/16_MESH_ANIMATION.md; toms_game --mesh-test=<sprite>):
    // that sprite of the game atlas as an 8 x 8 grid mesh waving in time, the plain sprite beside it.
    bool f_PlayPreviewMesh(const std::string& l_strSprite, std::string& l_strError);
    int f_PreviewFxLive() const { return m_oPreviewFx.f_LiveCount(); }   // the --fx effect's live particles (HUD)
    int f_PreviewFxGpuEmitters() const { return m_oPreviewFx.f_GpuEmitters(); }
    int f_PreviewFxEmitters() const { return m_oPreviewFx.f_Effect() ? (int)m_oPreviewFx.f_Effect()->m_vEmitters.size() : 0; }
    // Particle emitters above this many particles run on the GPU (GameSettings::particleGpuThreshold;
    // `toms_game --fx-gpu-threshold=N` overrides it for this run without saving it).
    int f_ParticleGpuThreshold() const { return m_iFxGpuThresholdOverride >= 0 ? m_iFxGpuThresholdOverride : m_oSettings.m_iParticleGpuThreshold; }
    void f_OverrideParticleGpuThreshold(int l_iN) { m_iFxGpuThresholdOverride = l_iN; }
    // Tests (toms_game --battle-tuning=default): the battle bars' built-in values instead of data/battle.json, so a
    // screenshot of a fight does not change whenever the tuning does.
    void f_UseDefaultBattleTuning() { m_fBarSlowScale = 1.0f; m_fBarFastScale = 1.0f; m_iBarCooldownMs = CombatState::m_iBarCooldownMs;
                                    m_iAttackPauseMs = 450; m_iHitNumberDelayMs = 180; }
    const toms::WeaponPlayTable& f_WeaponPlay() const { return m_oWeaponPlay; }
    // Where the battle screen's attack track shows its points, in design pixels (GameUi reads its layout each frame), so
    // the track's hint particles land on them.
    void f_SetComboPointsOnScreen(std::vector<glm::vec2> l_vPts) { m_vComboPointsOnScreen = std::move(l_vPts); }
    // Where the battle screen laid out the player's and the monsters' boxes (battle.rcss #player / .foe .fbox: x, y, w, h in
    // design pixels; GameUi reads them each frame): the fighters, their particles and damage numbers are placed in them.
    void f_SetBattleBoxes(const glm::vec4& l_v4Player, const std::vector<glm::vec4>& l_vFoes) {
        m_bBattleBoxesKnown = m_oCs.m_bActive && l_vFoes.size() >= m_oCs.m_vFoes.size();   // this battle's layout (all its monsters) is in
        m_v4PlayerBox = m_v4UiPlayerBox = l_v4Player;
        for (size_t l_nI = 0; l_nI < l_vFoes.size() && l_nI < (size_t)toms::g_iMaxFoes; l_nI++) m_arrFoeBox[l_nI] = m_arrUiFoeBox[l_nI] = l_vFoes[l_nI];
        if (m_bBattle3DOn) f_PlaceBattle3DBoxes();   // in 3D the boxes stand on the stage's marks (only their shape is the layout's)
    }

    // ---- the battle in 3D (docs/gameplay/21_BATTLE_3D.md): a 3D stage drawn by the host, the 2D fighters standing in it ----
    // The host can draw a 3D stage (mesh shaders, not --battle-3d=off): set once when it starts.
    void f_SetBattle3DAllowed(bool l_bAllowed) { m_bBattle3DAllowed = l_bAllowed; }
    // This battle's stage (battle.json "stages3d", chosen by floor like "backdrops") while a battle is shown and 3D is
    // allowed; null: none (the painted backdrop).
    const nlohmann::json* f_Battle3DStage() const;
    // The stage of the floor the player is on (null: none, or 3D not allowed), battle or not: the host loads it in the
    // background as soon as the floor is entered, so a battle opens at once.
    nlohmann::json f_Battle3DStageForFloor() const;
    struct Battle3DCamera {
        glm::vec3 m_v3Eye{0.0f, 1.5f, 4.0f}, m_v3Look{0.0f, 0.9f, 0.0f};
        float m_fFovYDeg = 45.0f;
        float m_fDim = 0.0f;   // 0..1: a special move darkens the stage (the 2D backdrop's black veil, as light)
        float m_fMoon = 1.0f;  // the moonlight now (clouds passing: battle.json "clouds")
    };
    // A point light on the stage this frame: a brazier's fire (flickering), a hit's flash (docs/gameplay/21 §4.4).
    struct Battle3DLight {
        glm::vec3 m_v3Pos{0.0f}, m_v3Color{1.0f};
        float m_fIntensity = 1.0f, m_fRange = 6.0f;
    };
    // The fires' and the flashes' lights, strongest first (the renderer takes a few).
    std::vector<Battle3DLight> f_Battle3DLights() const;
    // The fighters as objects of the 3D stage (docs/gameplay/21 §4.3): each fighter's 2D sprites are drawn in a world
    // group (IRenderer::f_BeginWorld) -- every pixel placed in metres on its feet, upright, facing the camera -- and the
    // host draws them in its scene: lit by the stage's lights, depth tested, nothing cut. Off: drawn on the screen.
    bool f_Battle3DSpritesOn() const;          // on a 3D stage, the renderer can place sprites in it, and the stage wants that
    // The camera for a screen of this shape (width / height): the stage's shot (a narrow screen widens the view so
    // both fighters stay in it) and its slow breathing.
    // device: the backbuffer's size; canvas: where the design canvas sits in it (x, y, w, h device pixels). The stage
    // fills the whole backbuffer, but the fighters are drawn only on the canvas: the view widens until the canvas
    // holds the whole shot (else a hero near its edge is cut there, the owner 2026-11-11).
    Battle3DCamera f_Battle3DCamera(const glm::vec2& l_v2Device, const glm::vec4& l_v4Canvas) const;
    // The host drew the stage this frame through viewProj into the whole device (its size in pixels); the design canvas
    // sits at designInDevice (x, y, w, h in device pixels). on = false: no stage this frame.
    void f_SetBattle3DView(bool l_bOn, const glm::mat4& l_mat4ViewProj, const glm::vec2& l_v2Device, const glm::vec4& l_v4DesignInDevice);
    bool f_Battle3DOn() const { return m_bBattle3DOn; }
    // The host is still loading this battle's stage: no painted backdrop meanwhile, only darkness (then the stage fades in).
    void f_SetBattle3DComing(bool l_bComing) { m_bBattle3DComing = l_bComing; }
    // Where the shown fighters stand: x, y, z their feet, w the shadow's radius (metres).
    std::vector<glm::vec4> f_Battle3DShadows() const;
    // The design canvas changed (the screen turned, canvas.h): the camera fits the new one at once, without easing.
    void f_CanvasChanged();
    // Files by key, never by path (assets/media/resources.json; the keys: res_keys.h, made by tools/make_res_keys.py).
    // pathOf: a key's path, for what reads the file itself (a loader, RmlUi, the sound folder). pathFor: one file of a
    // family ("stage", "stage01"). Both are paths under assets/media's folder, as the resource manager reads them.
    std::string f_PathOf(const char* l_pszKey) const { return m_upRes ? m_upRes->f_Resolve(l_pszKey) : std::string(); }
    std::string f_PathFor(const char* l_pszPattern, const std::string& l_strId) const { return m_upRes ? m_upRes->f_PathFor(l_pszPattern, l_strId) : std::string(); }
    // The UI's sprite sheet: RCSS made from spriteAtlas_, which GameUi serves to RmlUi as
    // "_atlas.rcss" -- so the UI draws from the same packed texture as the map. Empty without a
    // prebuilt atlas. The revision changes whenever another atlas is loaded (art style switch).
    std::string f_UiSpritesheet() const;
    int f_SpriteAtlasRevision() const { return m_iSpriteAtlasRevision; }
    // Sprites the UI or the data (items, store) can ask for that the atlas lacks; empty = fine.
    std::vector<std::string> f_MissingUiSprites() const;
    int f_ChosenArtStyle() const { return toms::f_ArtStyleIndex(m_vArtStyles, m_oSettings.m_strArtStyle); }
    // The player's sprite: the setting's look when the atlas has it, else "player".
    std::string f_PlayerSprite() const {
        return !m_oSettings.m_strPlayerLook.empty() && m_mapIdToLayer.count(m_oSettings.m_strPlayerLook) ? m_oSettings.m_strPlayerLook : std::string("player");
    }
    int f_ActiveSlot() const { return m_iActiveSlot; }
    int f_PlayTimeSec() const { return m_iPlayTimeSec; }
    const toms::Locale& f_Locale() const { return m_oLocale; }
    const toms::GameSettings& f_Settings() const { return m_oSettings; }
    toms::GameSettings& f_MutableSettings() { return m_oSettings; }
    // Derived, read-only classification of "what screen/mode is the game in right now",
    // computed from the existing modal flags — see docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md
    // §4 and docs/architecture/IMPLEMENTATION_ROADMAP.md Milestone 1. Does not change control flow; the
    // states with no real screen behind them yet (StageSelect, Paused, ...) are simply never
    // returned today.
    toms::GameState f_CurrentState() const;
    // ---- ImGui dev windows (M2: ImGui is wired into every backend, so these build on web too) ----
    // Milestone 2 dev-only debug overlay (Dear ImGui: stat sliders, node-filter toggle, last
    // combat log line). The caller decides when to show it (GameSession, F1); this just builds
    // the ImGui:: window content for the current frame.
    void f_DrawDebugOverlay();
    // UI settings: applies uiFontScale_ to ImGui's global font scale. Call once per frame,
    // right after ImGui's NewFrame(), so it's in effect before anything else draws that frame.
    void f_ApplyUiSettings();
    // Milestone 2 styling spike (a prerequisite the roadmap flags before Milestone 5 commits real
    // screens to the hybrid UI approach): proves a transparent, undecorated ImGui window laid
    // exactly over a scene-graph-drawn backdrop rect reads as one panel, not two overlapping
    // things — see docs/project/progress_report/PROGRESS_REPORT.md's M2 log entry for why this needed checking before
    // any real screen was built on the assumption. Dev-only, F2 to toggle.
    void f_DrawStylingSpike();
    void f_SetStylingSpikeVisible(bool l_bV) { m_bStylingSpikeVisible = l_bV; }
    // DEBUG: hide individual overlay subsystems to bisect stray-sprite bugs.
    // bit 1 = combat overlay, bit 2 = dialogue overlay, bit 4 = inventory UI.
    int m_iHideMask = 0;
    CombatState& f_Combat() { return m_oCs; }
    Stage& f_Stage() { return m_oSt; }
    const nlohmann::json& f_EnemyTemplate(const std::string& l_strId) const { return m_mapEnemyTpl.at(l_strId); }
private:
    Quad f_SpriteQuad(float l_fX, float l_fY, float l_fW, float l_fH, int l_iLayer, const float l_arrTint[4]);
    void f_SpriteUV(int l_iLayer, float l_arrUv[4]) const;
    int f_SpriteLayer(const std::string& l_strId) const; // index into sprite grid
    void f_ApplyItem(const std::string& l_strId);
    // Battle System v2: resolveAttackTap/resolveDefenseTap fire the instant a bar is tapped,
    // reading its current (just-frozen) position -- resolveAttackTap applies damage to the enemy
    // directly (and calls finishCombatWin on a kill); resolveDefenseTap only banks a shield, it
    // doesn't apply damage itself. resolveEnemyClockFire runs whenever the enemy's own real-time
    // clock completes: it spends (and clears) whatever shield is currently banked, or applies full
    // damage if none is, and calls finishCombatLose on a kill. The UI shows either bar at its live
    // auto-moving position (frozen while cooling down after a tap).
    void f_ResolveAttackTap();
    // Battle v3: the combo ended -- its damage, then the cooldown. played: a touch play's result (else the track's)
    void f_ResolveCombo(const toms::ComboResult* l_pPlayed = nullptr);
    toms::PlayType f_BattlePlayType() const;          // the worn weapon's play; a scenario's "play" (scenarios: the track)
    bool f_AttackRunning() const { return m_oCs.m_oCombo.f_Running() || (m_oCs.m_oTouch.f_Running() && m_oCs.m_oTouch.f_Started()); }
    glm::vec4 f_TouchArea() const;                    // where a touch play's targets go: over the hero's head (never over a monster)
    void f_ArmTouchPlay(bool l_bUltimate, int l_iPoints);   // a new touch play over the target (waits for its first touch)
    toms::TouchPlaySettings f_TouchSettingsFor(toms::PlayType l_eType) const;   // battle.json "plays", the weapon's, STR / DEX
    toms::TouchPlaySettings m_oTouchSettings;         // battle.json "plays"
    glm::vec4 m_v4TouchArmedArea{0.0f};                // where the waiting play was armed (re-armed if the layout moves)
    bool m_bBattleBoxesKnown = false;                 // the battle screen has laid out this battle's fighters (a play waits for it)
    int f_NormalHitDamage(int l_iFoe) const;             // a normal attack's full hit on the monster (the moves' unit)
    toms::CellRules m_oCellRules;                        // battle.json "cells"
    glm::vec2 m_v2HeroShift{0.0f};                       // the hero's picture, easing toward its cell
    // the battle in 3D (f_SetBattle3DView)
    bool m_bBattle3DAllowed = false, m_bBattle3DOn = false, m_bBattle3DComing = false;
    glm::mat4 m_mat4Battle3DViewProj{1.0f};
    glm::vec4 m_v4Battle3DDesign{0.0f, 0.0f, 1.0f, 1.0f};
    glm::vec2 m_v2Battle3DDevice{0.0f};
    nlohmann::json m_jBattle3DStage;                     // this battle's "stages3d" row (null: none); chosen when it opens
    int m_iBattle3DTimeMs = 0;                           // the camera's breathing
    glm::vec4 m_v4UiPlayerBox{262, 84, 134, 134};        // the layout's boxes (their shape is kept in 3D)
    glm::vec4 m_arrUiFoeBox[toms::g_iMaxFoes] = {{652, 84, 134, 134}, {652, 84, 134, 134}, {652, 84, 134, 134}};
    void f_PlaceBattle3DBoxes();
    // Design pixels -> world metres for a 2D drawing standing in the scene: pixel `anchor` at `at`, `metresPerPx` a pixel,
    // upright, parallel to the screen (facing the camera). Column 2 is the plane's normal.
    glm::mat4 f_Battle3DSpriteMatrix(const glm::vec2& l_v2Anchor, const glm::vec3& l_v3At, float l_fMetresPerPx) const;
    void f_DrawBattle3DFighter(int l_iWho);             // -1 the hero, else monster `who`: its view in a world group at its feet
    glm::vec3 f_Battle3DHeroFeet() const;               // the hero's mark and its move between the cells, in metres
    // A stage point in design pixels (ok = in front of the camera); pxPerMetre: how big a metre is there.
    glm::vec2 f_Battle3DToDesign(const glm::vec3& l_v3P, bool& l_bOk, float* l_pPxPerMetre = nullptr) const;
    struct Battle3DFlash { glm::vec3 m_v3Pos{0.0f}, m_v3Color{1.0f}; float m_fIntensity = 8.0f, m_fRange = 5.0f; int m_iDelayMs = 0, m_iMs = 0, m_iMsMax = 1; };
    std::vector<Battle3DFlash> m_vBattle3DFlashes;       // hits lighting the stage a moment
    void f_AddBattle3DFlash(int l_iWho, int l_iDelayMs);
    void f_Battle3DDangerShot(int l_iFoe, bool l_bIntro);   // a special attack coming: the low close-up on its monster
    void f_StartFoePower(int l_iFoe, bool l_bIntro);    // ...its power-up: the clip, the particles, the rumble, the clock waits
    void f_UnleashFoePower(int l_iFoe);                 // ...its hit goes off: the burst, the shout, the shake
    bool f_FoeCharging(int l_iFoe) const;               // powering up or charging (its rig holds "charge" between clips)
    void f_KnockDownHero();                             // ...its hit hurt the hero: hurt_big, and no attack until it ends
    bool f_HeroDown() const { return m_oCs.m_iHeroDownMs > 0; }
    std::vector<toms::fx::EffectInstance> m_vStageFireFx; // the braziers' fire (battle.json stage "fires"), one each
    void f_DrawBattle3DStageFx();                         // the fires' particles and the floor's ambient motes, on the stage
    glm::vec3 f_Battle3DMark(int l_iWho) const;          // a fighter's feet on the stage (-1: the hero)
    float f_Battle3DHeight(int l_iWho) const;            // its height (metres)
    int f_BattleFloorNumber() const;                     // the floor's number, for battle.json's from..to rows
    void f_StartLoseMoment();                            // HP gone: the fall, then finishCombatLose (17 §19)
    std::string f_EndClip() const;                       // the clip for this end: big win, win, par win or lose
    void f_PlayAnswerClip();                             // an attack clip for one answer, by the combo so far (17 I1, §19)
    const toms::ComboTier* f_ComboTier() const;          // the weapon's tier for the combo so far
    void f_ComboWord();                                  // "3 Combo!" from the second hit
    bool f_FreeHit(const toms::TouchPlay::Hit& l_oH);    // one swipe or tap of a free mode (17 §5); true: the fight is won
    void f_PlayHit(const toms::TouchPlay::Hit& l_oH);    // a play's answer that hits at once (the sword's cut, the staff's spell)
    void f_StopOwnAttack();                           // the "warn" guard stops the player's attack (16 §4.2)
    // the monsters' rhythm and the "!" (16 §4): its "!" shows; the "!" length of its move
    bool f_FoeWarning(int l_iFoe) const;
    int f_MoveWarnMs(const toms::MoveDef& l_oM) const;
    // skills (16 §6): start one (owner -1: the player's; a monster index: its special guard), its time, a show's beat
    toms::SpecialMoveTable m_oSpecialMoves;                       // data/special_moves.json
    void f_StartSkill(const toms::SpecialMove& l_oDef, int l_iOwner);
    void f_UpdateSkill(int l_iDtMs);
    void f_SkillBeat(const toms::ShowBeat& l_oB, glm::vec2 l_v2At);
    void f_EndSkill();
    glm::vec2 f_SkillCentre() const;                  // where a move's shape is drawn: between the fighters
    std::string f_SkillName() const;                  // the running skill's name, in the language
    // the training ground (16 §5): a monster's card spent for fights in a row with more EXP and weapon wins
public:
    int f_TrainingRunsLeft() const;
    bool f_StartTraining(const std::string& l_strMonster);
private:
    void f_EndTraining();
    void f_StepTouchPlay(int l_iDtMs);                   // its time, while it runs
    void f_TouchPlayed(int l_iIndexBefore, int l_iHitsBefore);   // after its pointer or time: a sound per arrow, resolve when done
    bool m_bTouchHeld = false;                        // the press that is held began on the play (not on a button)
    // Weapon levels (docs/gameplay/07 phase 1): the equipped weapon's key in weapon_play.json ("unarmed" for none), its level
    // from the wins with it, and its play settings.
    std::string f_WeaponKey() const;
    int f_WeaponLevel(const std::string& l_strKey) const;
    const toms::WeaponPlay& f_WeaponPlayOf(const std::string& l_strKey) const;
    float f_ComboCharge() const;                      // 0 .. 1, the charge built while the attack waits
    void f_ResolveDefenseTap();
    void f_ResolveEnemyClockFire(int l_iFoe);
    // ---- the battle's rules (docs/gameplay/13; battle_rules.h) ----
    void f_ChooseFoeMove(int l_iFoe);                     // its next move (its intent) and the wait before it
    void f_StepFoes(int l_iDtMs);                         // Game::update: the monsters' clocks, stuns and timed events
    // The player's hit of `dmg` on a foe: its HP, then its HP events (hpBelow, falls). The caller then checks the fall.
    void f_DamageFoe(int l_iFoe, int l_iDmg);
    void f_RunFoeEvents(int l_iFoe, const toms::EventCheck& l_oCheck);
    void f_RunFoeActions(int l_iFoe, const nlohmann::json& l_jActions);
    void f_StunFoe(int l_iFoe, int l_iMs, float l_fDamage, bool l_bBroken);
    void f_FoeLook(int l_iFoe, const std::string& l_strState);  // its look's sound and particle for a status, and its clip
    void f_PlayerLook(const std::string& l_strState);         // the player's ("player" in looks.json)
    void f_InterruptCombo(int l_iFoe);                      // quick jabs cut a running combo short
    std::string f_FoeLogName(int l_iFoe) const;
    toms::Pace f_EffectivePace() const;   // the override, else the weapon's, else data/battle.json's
    int f_FoeBreakPoints(int l_iFoe) const;
    float f_PlayerHpPct() const;
    // A foe fell (its HP reached 0): its die clip, the log; the target moves on; the last one wins the battle.
    void f_FoeDown(int l_iFoe);
    std::vector<EnemyInst> f_PartyOf(const EnemyInst& l_oLead);
    bool f_AfterPlayerHit(const std::string& l_strHitLog, const std::string& l_strWinLog);   // true: the battle was won
    // ---- the revamp (docs/gameplay/15) ----
    float f_BattleSpeed() const;                          // the player's setting, or a scenario's (17 I10)
    void f_CycleBattleSpeed();                            // Settings: the next of battle.json "battleSpeeds"
    int f_BattleFrame(int l_iDtMs);                       // the battle's time this frame (hit stop, slow motion) and its effects
    void f_FeelMoment(const std::string& l_strMoment);      // a moment's hit stop, shake and sound (data/battle.json "feel")
    void f_PopWord(const std::string& l_strText, int l_iWho, const std::string& l_strStyle);   // a word over a fighter, now
    void f_TouchPlayUi(toms::UiBattle& l_oB) const;      // the weapon's own play, drawn (GameUi: battle.rml .tp)
    void f_ShowBanner(const std::string& l_strText, const std::string& l_strKind, int l_iMs);
    void f_StartWinMoment();                           // the last one fell: slow motion, then finishCombatWin
    void f_BattleEvent(const std::string& l_strType, int l_iWho, int l_iWhom, int l_iValue = 0, const std::string& l_strNote = std::string());
    bool m_bBattleLogToStderr = false;
    // Builds the EnemyInst for a monster tile and starts the fight (or its dialogue gate); shared
    // by the bump-to-fight path in movePlayer() and the harness hook debugStartNearestBattle().
    void f_EngageMonster(const Entity& l_oE);
    // ---- S1: 佔格 / roamers --------------------------------------------------------------------
    // True when the entity's footprint covers tile (x,y) -- the anchor is its top-left tile, so a
    // 2x2 boss "is" on all four of its tiles for blocking (F4/F6), bump-to-fight (F6) and pickup
    // purposes. One predicate, used by movement, interaction and the roamer grid query alike.
    bool f_EntityCovers(const Entity& l_oE, int l_iX, int l_iY) const {
        return toms::f_FootprintCovers(l_oE.m_iX, l_oE.m_iY, l_oE.m_oFp, l_iX, l_iY);
    }
    // F5 y-sort key (bottom-most occupied row) for an entity -- and the same thing for a loose
    // position/footprint pair, so the player (always 1x1) sorts in the same pass as the entities.
    int f_FootprintSortKeyT(const Entity& l_oE) const { return toms::f_FootprintSortKey(l_oE.m_iY, l_oE.m_oFp); }
    int f_FootprintSortKeyT(int /*x*/, int l_iY, const toms::Footprint& l_oFp) const {
        return toms::f_FootprintSortKey(l_iY, l_oFp);
    }
    // Advance every roamer one turn (the game is turn-based: one step per player step). Called by
    // movePlayer() -- a bump into a monster consumes the turn too, so a roamer can corner you.
    void f_AdvanceRoamers();
    // entity index -> brain, rebuilt by loadStage(). Kept as indices (not Entity*) because a stage
    // reload replaces the whole entity vector (stairs, Stage Select) and stale pointers would
    // silently drive the wrong monster.
    std::vector<std::pair<int, toms::Roamer>> m_vRoamers;
    // data/footprints.json -- character-level 佔格 (doc F8). Keyed by the legend kind, i.e.
    // "monster:golem", so a floor file never has to repeat the same tier for every copy of a type.
    std::map<std::string, toms::FootprintSpec> m_mapTypeFootprints;
    // S3: the run's story state (choices, counters, side-story states, run flags, shards, floor
    // progress, deaths). GameConditionContext reads it, the run save persists it, and rebirth resets
    // it (docs/story/STORY_DATA_SCHEMA.md sections 8/9).
    toms::RunStoryState m_oRun;
    // S3.5: the 70-floor tower as ordered data (data/story/floors/*.json). Empty when that data is
    // absent, and every use is guarded -- so a checkout without the floors still plays the eleven
    // hand-authored stages exactly as before.
    toms::FloorTable m_oFloors;
    // S3.5 (c): what the footer says while the run is on a floor -- the floor's own intro line, which
    // then rotates through its ambient lines as the player moves (storyLineIndex, floor_table.h).
    std::string m_strStoryIntroKey;
    std::vector<std::string> m_vStoryAmbientKeys;
    int m_iStoryTurns = 0;
    // S3.5 (c): the act card shown for a couple of seconds when a floor with indexInAct == 1 loads.
    float m_fChapterCardMs = 0.0f;
    std::string m_strChapterCardTitle;
    // S3.5 (e): the act whose palette tints this floor's tiles (floorThemeTint, floor_table.h).
    int m_iThemeActIndex = 1;
    std::string m_strCurFloorId;              // "F07" while the run is on a generated/boss floor, else ""
public:
    // S3: read-only access for tests (run_state_test.cpp) and debug tools -- the run state itself
    // stays owned by Game.
    const toms::RunStoryState& f_RunState() const { return m_oRun; }
    // S4: the skill tree. skillDefs() is read-only content (the UI enumerates it to draw the tree);
    // tryUnlockSkill() is the one path that can ever spend a point -- it re-checks
    // canUnlockSkill() itself rather than trusting the caller, so a UI bug (a stale/duplicate
    // click) can never double-spend or bypass a prerequisite.
    const std::map<std::string, toms::SkillDefinition>& f_SkillDefs() const { return m_mapSkillDefs; }
    bool f_TryUnlockSkill(const std::string& l_strSkillId);
    // The skill-tree menu's row order: grouped by lineage in story order (yinqi/yuqi/yuqi's
    // ch_01/02/03 sequence), root before its own deeper tiers within a lineage. A pure function of
    // skillDefs_ (which never changes after loadAssets), so the in-game menu's draw code and its
    // input handlers can both call it and never disagree about which id is at which row.
    std::vector<std::string> f_SkillMenuOrder() const;
    // The atk/def combat actually uses once equipment AND skills both stack (see game_combat.cpp's
    // three call sites) -- exposed read-only so the UI (or a verification probe) can show/check the
    // real number instead of just the base Player stat, which never itself changes for either layer.
    // M8: after at least one rebirth (cycleIndex > 1), a tier>=1 skill's own bonus applies at half
    // effect (STORY_BIBLE.md §8) -- see applySkillEffects's own comment for the root-node carve-out.
    // INT (data/stats.json "skillPower") adds to what skills give, on top of the rebirth halving.
    float f_SkillEffectScale() const { return (m_oMeta.m_iCycleIndex > 1 ? m_oCyclesConfig.m_fSkillEffectScale : 1.0f) * (1.0f + f_AttrEffect("skillPower") / 100.0f); }
    // M8 verification hook: read-only, matching runState()'s own "expose the meta save's public
    // facts for tests/harness probes" rule.
    int f_MetaCycleIndex() const { return m_oMeta.m_iCycleIndex; }
    // Base + equipment + skills + attributes (data/stats.json effects "atk" / "def", rounded down).
    int f_EffectiveAtk() const { int l_iA=m_oPl.m_iAtk,l_iD=m_oPl.m_iDef; toms::f_ApplyEquipmentStats(m_oEquipped,m_mapEquipmentDefs,l_iA,l_iD); toms::f_ApplySkillEffects(m_mapSkillDefs, m_oRun.f_SkillsOwned(), l_iA, l_iD, f_SkillEffectScale()); return l_iA + (int)std::floor(f_AttrEffect("atk")); }
    int f_EffectiveDef() const { int l_iA=m_oPl.m_iAtk,l_iD=m_oPl.m_iDef; toms::f_ApplyEquipmentStats(m_oEquipped,m_mapEquipmentDefs,l_iA,l_iD); toms::f_ApplySkillEffects(m_mapSkillDefs, m_oRun.f_SkillsOwned(), l_iA, l_iD, f_SkillEffectScale()); return l_iD + (int)std::floor(f_AttrEffect("def")); }
    // P3 (docs/gameplay/03_PLAYER_MENU.md section 12): what the attributes do. attrEffect(stat) sums, over every attribute,
    // (its value - its base) * per for the effects naming `stat`; so a Lv 1 character with no bonuses gets 0.
    float f_AttrEffect(const std::string& l_strStat) const;
    int f_EffectiveMaxHp() const { return std::max(1, m_oPl.m_iMaxhp + (int)std::floor(f_AttrEffect("maxhp"))); }   // VIT
    toms::PowerBarParams f_AttackBarParams() const;   // the worn weapon's bar, its hit zone widened by DEX
    int f_BarCooldownMs() const;                      // battle.json's wait after a tap, shortened by AGI
    void f_SpendAttrPoint(int l_iIndex);                 // + on the Status tab: one point into the draft (attrDraft_)
    void f_UnspendAttrPoint(int l_iIndex);               // -: one point back out of the draft
    void f_ConfirmAttrPoints();                       // Confirm: the draft becomes the attributes, and the run is saved
    void f_ResetAttrDraft();                          // Reset: the draft is dropped
    // S5: forging. forgeDefs() is read-only content; tryCraft() re-checks canCraft() itself (same
    // "never trust the caller" rule as tryUnlockSkill) -- it deducts gold/materials and equips the
    // result in one atomic step, so a UI bug can never spend materials without getting the item.
    const std::map<std::string, toms::ForgeRecipeDefinition>& f_ForgeDefs() const { return m_mapForgeDefs; }
    bool f_TryCraft(const std::string& l_strRecipeId);
    // The forge menu's row order: plain id order (no lineage grouping concept for recipes) -- a
    // pure function of forgeDefs_, same reasoning as skillMenuOrder() above.
    std::vector<std::string> f_ForgeMenuOrder() const;
    // Meta-scope: which recipes the player has actually learned (survives rebirth). Read-only --
    // the only writer is Game::applyChapterGrants().
    const std::vector<std::string>& f_ForgeRecipesKnown() const { return m_oMeta.m_vForgeRecipesKnown; }
    // S6: the village hub. hubDefs() is read-only content; activateHubLocation() re-checks
    // hubLocationUnlocked() itself (same "never trust the caller" rule as tryUnlockSkill/tryCraft)
    // before dispatching the location's action (today, only "talk" -> startDialogue() exists).
    const std::map<std::string, toms::HubLocationDefinition>& f_HubDefs() const { return m_mapHubDefs; }
    bool f_ActivateHubLocation(const std::string& l_strLocationId);
    // The hub menu's row order: plain id order (no lineage grouping concept, same reasoning as
    // forgeMenuOrder() above) -- a pure function of hubDefs_.
    std::vector<std::string> f_HubMenuOrder() const;
    // S7 (equipment actives, first slice): read-only content. battleTapActive() is the only path
    // that can spend one -- see its own declaration above.
    const std::map<std::string, toms::ActiveDefinition>& f_ActiveDefs() const { return m_mapActiveDefs; }
    // Which active id(s) the currently equipped gear grants (equipment_system.h's
    // equippedActives(), applied to the live equipped_/equipmentDefs_) -- exposed read-only so a
    // page harness (jsActiveInfo) can ask "what's granted right now" without a private-member
    // accessor for equipped_/equipmentDefs_ themselves.
    std::vector<std::string> f_GrantedActiveIds() const { return toms::f_EquippedActives(m_oEquipped, m_mapEquipmentDefs); }
    // M7 (first slice): the 15-ending table is read-only content; the only writer of
    // activeEndingId_ is Game::triggerEnding(), called from finishCombatLose() today (the only
    // trigger point this slice wires -- see its own comment for why F70/F69's rows aren't reachable
    // yet). An active ending takes over the whole screen (see modalActive()).
    const toms::EndingsTable& f_EndingsTable() const { return m_oEndingsTable; }
    bool f_EndingActive() const { return !m_strActiveEndingId.empty(); }
    const std::string& f_ActiveEndingId() const { return m_strActiveEndingId; }
    // M8: whether the CURRENTLY showing ending offers 輪迴 at all (STORY_BIBLE.md §8.1: every
    // ending except e_10 does) AND the cycle cap hasn't already been reached -- the one place both
    // checks live, so the ending screen's draw code and its input handlers can't disagree about
    // whether a second button even exists. False (never true) while no ending is showing.
    bool f_RebirthOffered() const;
    // The ending screen's two exits. dismissEndingScreen() always returns to the title (the
    // "start over" choice, or the only choice when rebirthOffered() is false). rebirth() re-checks
    // rebirthOffered() itself (never trusts the caller, same rule as tryUnlockSkill/tryCraft) and
    // is a no-op otherwise; on success it carries STORY_BIBLE.md §8's table forward (skills at half
    // power, memory shards, forge/actives knowledge already meta-scoped so untouched, +1 cycle)
    // and resets everything else, landing back on F01.
    void f_DismissEndingScreen();
    bool f_Rebirth();
    // S3.5 step (a)/(b): the run's progression source. `floorMode()` is false only when the floor
    // data is missing; `totalStages`/the HUD counter/the hub all read the table when it is present.
    bool f_FloorMode() const { return !m_oFloors.f_Empty(); }
    const toms::FloorTable& f_FloorTable() const { return m_oFloors; }
    const std::string& f_CurrentFloorId() const { return m_strCurFloorId; }
    // S3.5 verification probes (also used by the browser harness): which story line is showing, and
    // whether the act card is up.
    int f_StoryLineIndex() const { return toms::f_StoryLineIndex(m_iStoryTurns, (int)m_vStoryAmbientKeys.size()); }
    bool f_ChapterCardVisible() const { return m_fChapterCardMs > 0.0f; }
    int f_ThemeAct() const { return m_iThemeActIndex; }
    // Mobile: enlarge the on-screen pad (1.0 on desktop) and the dialogue box.
    void f_SetUiScale(float l_fS) { if (l_fS > 0.0f) m_fUiScale = l_fS; }
    // saveCurrentRun() itself stays where it was (private); this is the one public entry the web
    // harness probe jsSaveNow() needs, without widening the existing declaration's access.
    void f_SaveRunNow() { f_SaveCurrentRun(); }
private:
    void f_FinishCombatWin();
    void f_FinishCombatLose();
    std::string f_ItemName(const std::string& l_strId) const;
    std::string f_ItemDesc(const std::string& l_strId) const;
    // data/items.json lookups. txt(): a text.json key (the all-item file's name/desc) or an inline
    // {lang: text} object, in the current language.
    std::string f_Txt(const nlohmann::json& l_jJ) const;
    const nlohmann::json* f_ItemDef(const std::string& l_strId) const;
    std::string f_ItemType(const std::string& l_strId) const;        // consumable | key | weapon | armor | talent | story
    bool f_ItemImportant(const std::string& l_strId) const;
    int f_ItemPrice(const std::string& l_strId) const;                // -1 = none
    const nlohmann::json& f_ItemStats(const std::string& l_strId) const;
    bool f_IsGearType(const std::string& l_strType) const { return l_strType == "weapon" || l_strType == "armor" || l_strType == "talent"; }
    // A new item reaches the player (picked up, a reward, a gift): keys and autoUse items (coins) apply
    // at once, weapons / armor / talents join the owned gear, everything else goes into the backpack.
    void f_ReceiveItem(const std::string& l_strId);
    // Applies an item's stats (hp, atk, def, gold, exp, keys, attributes, warp) to the player.
    void f_ApplyStats(const std::string& l_strId, const nlohmann::json& l_jStats);
    // Adds a weapon / armor / talent to the owned gear; equip = also wear it.
    void f_OwnGear(const std::string& l_strId, bool l_bEquip);
    void f_EquipGear(const std::string& l_strId);
    // EXP, with as many level-ups as it pays for (data/stats.json "levelUp").
    void f_GainExp(int l_iExp);
    void f_ResetAttrs();                     // base + perLevel for each level above 1
    std::string f_ItemSprite(const std::string& l_strId) const;         // the item's icon (uiSprite name)
    // A sprite id (or "<id>.png") as the .rml files name it: <img data-attr-sprite="..."/>.
    std::string f_UiSprite(std::string l_strId) const;
    std::string f_ArtStyleName(int l_iIdx) const;                     // localized, "Original" for 0
    void f_AppendArtStyleRows(std::vector<toms::UiRow>& l_vRows, int l_iSelected) const;   // Settings pages
    int f_SettingsStyleRowCount() const { return m_vArtStyles.size() > 1 ? (int)m_vArtStyles.size() : 0; }
    bool f_LoadSpriteAtlas(int l_iStyle);   // builds + uploads the atlas for one art style
    // The prebuilt atlas (<dir>/atlas/game.atlas from tools/atlas) for the style, if there is one.
    bool f_LoadPrebuiltSpriteAtlas(int l_iStyle);
    std::string f_ItemEffectSummary(const std::string& l_strId) const;  // "HP +40 • DEF +1"
    void f_BuildMenuUi(toms::UiMenu& l_oOut) const;                   // the System tab part of buildUiState
    void f_BuildPlayerUi(toms::UiState& l_oU) const;                  // the player menu (game_player_menu.cpp)
    // The Items tab's grid / the Equipment tab's grid, in display order.
    struct ItemCell { enum Kind { Inv, Key, Gear } m_eKind; std::string m_strId; int m_iCount = 1; };
    std::vector<ItemCell> f_ItemCells() const;
    std::vector<std::string> f_GearCells() const;                  // owned gear of the selected slot
    enum class ItemAction { Use, Equip, Unequip, Drop };
    struct ItemButton { ItemAction m_eAction; bool m_bEnabled; };
    std::vector<ItemButton> f_ItemButtons(const ItemCell& l_oC) const;
    void f_RunItemAction(const ItemCell& l_oC, ItemAction l_eA);
    void f_PlayerMenuEvent(const std::string& l_strName, int l_iArg);   // the pm_* buttons of player.rml
    // Save / Load (game_player_menu.cpp): mode 0 save, 1 load.
    void f_OpenSaveLoad(int l_iMode);
    void f_SaveLoadAsk(int l_iIndex);                 // a slot was chosen: open its confirmation
    void f_SaveLoadAnswer(bool l_bYes);               // the confirmation's answer: save / load, or nothing
    void f_BuildSaveLoadUi(toms::UiSaveLoad& l_oOut) const;
    // The Events tab: chapters reached and missions accepted, in progress first.
    // kind 0 chapter, 1 mission, 2 event. connected: the names of linked events ("？？？" for one not met yet).
    struct LogEntry { int m_iKind = 0; std::string m_strId, m_strTitle, m_strDesc; bool m_bDone = false; std::vector<std::string> m_vConnected; };
    std::vector<LogEntry> f_EventLog(bool l_bAll) const;
    // store system
    void f_LoadStore();   // parse data/store.json
    // Milestone 8: the store's items are split into tabs of <=3 cards each. Returns the indices
    // into storeItems_ that belong to the current storeTab_ (0=potions,1=weapons,2=armor,3=talents).
    std::vector<int> f_StoreTabIndices() const;
    void f_BuyStoreItem(int l_iIdx);                      // purchase + apply effect (or toast if poor)

    IRenderer* m_pRen = nullptr;        // the bgfx renderer, created in loadAssets
    // Every atlas, clip file and particle file the game draws, loaded once and shared (docs/engine/12_RESOURCE_MANAGER.md).
    // Declared before what holds its resources, so it goes last and frees their textures while the renderer is up.
    std::unique_ptr<toms::res::ResourceManager> m_upRes;
    // Makes render objects from JSON (render_object.h): everything the floor's objects draw (map_objects.cpp's looks).
    // A description used for many objects is made once and cloned (instance()).
    toms::RenderObjectFactory m_oRenderFactory;
    bool m_bHotReload = false;         // setHotReload: reload changed files about once a second
    int m_iHotReloadMs = 0;
    void f_ProvideSpriteAtlas();       // the loaded sprite atlas, as the cache's kSpriteAtlas
    Player m_oPl;
    Stage m_oSt;
    CombatState m_oCs;
    // sprite layer registry (order matches loadAssets)
    std::vector<std::string> m_vSpriteIds;
    std::map<std::string,int> m_mapIdToLayer;
    int m_iSpriteGridCols = 9;
    // With a prebuilt atlas: every sprite's UVs by layer, and the whole atlas (child sprites,
    // pivots, 9-slices and tags by name). Empty = the runtime grid (spriteGridCols).
    std::vector<std::array<float, 4>> m_vSpriteUVs;
    std::vector<std::array<float, 4>> m_vSpriteTrim;   // x, y, w, h of the kept pixels, as fractions of the original
    toms::AtlasFile m_oSpriteAtlas;
    std::map<std::string, toms::SpriteBox> m_mapSpriteBoxes;   // each sprite's visible pixels (fitCharacter)
    toms::SpriteMeshFile m_oSpriteMeshes;   // its meshes (game.mesh.json beside it, written by the anim tools; docs/engine/16)
    nlohmann::json m_jCharacterFit;                          // assets/data/character_fit.json (read at startup)
    std::string m_strBattleRigClip;   // the clip the battle picture plays for battleRigState_ ("" = none found)
    std::shared_ptr<toms::res::AnimRes> m_spPreviewAnim;   // playPreviewAnim (its atlases come with it)
    std::string m_strPreviewClip;
    std::string m_strPreviewCharacter;                      // setPreviewCharacter
    // The player on the battle screen (battleRigState above): the weapon its look was made for, and the view playing
    // it (FighterView: 2D clips or 3D, the weapon's battleAnim decides).
    std::string m_strBattleRigWeapon;
    std::unique_ptr<toms::FighterView> m_upPlayerView;
    std::string m_strBattleRigState;
    int m_iLastAttackDamage = 0;                          // the last attack tap's damage (attack or miss)
    void f_LoadBattleRig();                               // (re)loads the clips when the weapon changes
    // clip: plays that clip of the character's .anim for the status (a result's "anim"), else the weapon's for it
    void f_SetBattleRigState(const std::string& l_strState, bool l_bRestart = false, const std::string& l_strClip = std::string());
    void f_UpdateBattleRig(int l_iDtMs);                     // picks the status; Game::update
    void f_DrawBattleRig();                               // the backdrop, the player and the monster; Game::draw
    std::string m_strVersionLabel;                        // data/version.json as the title shows it (version_label.h)
    bool m_bShowVersion = true;
    // The monster on the battle screen (assets/media/anim/monster_battle.anim, tools/make_monster_battle_anims.py): the
    // clip for the battle's status -- idle; attack (its clock fired); hurt (the player's hit landed); die (the fight is
    // won) -- with the enemy's own sprite in its `body` (fitCharacter). "<status>_<enemy id>" in the file wins over the
    // shared clip. monsterRigState(): the status shown ("" = none: the file did not load, the UI's picture is drawn).
    static constexpr const char* m_pszMonsterBattleAnim = toms::key::g_pszAnimMonsterBattle;
    // One rig a foe (cs.foes, the same order).
    struct FoeRig {
        std::unique_ptr<toms::FighterView> m_upView;        // its look (looks.json: 2D clips or 3D), made on its first state
        std::string m_strState, m_strClip;
        std::string m_strSprite;                             // the sprite the view was made for (transformInto: a new one)
        std::string m_strRequest;                            // attack / hurt / die: played from the start on the next update
    };
    std::vector<FoeRig> m_vFoeRigs;
    std::string f_MonsterSprite(int l_iFoe) const;           // the foe's sprite (the boss's own; else its type's)
    void f_SetFoeRigState(int l_iFoe, const std::string& l_strState);
    toms::FighterLook f_MonsterLook(int l_iFoe) const;       // its look's render / files (looks.json), else the shared clips
    // A fighter's view from its look (docs/gameplay/18): 2D clips (fit: fitted to this character; unitsTall: clip units
    // at the box's height; boxSide: the box's shorter side instead), or 3D. Null: the look's files did not load.
    std::unique_ptr<toms::FighterView> f_MakeFighterView(const toms::FighterLook& l_oLook, toms::AnimFighterView::Fit l_fnFit,
                                                       float l_fUnitsTall, bool l_bBoxSide);
    void f_UpdateMonsterRig(int l_iDtMs);                    // Game::update
    // The battle's particles (media/fx/battle.particle; weapon_play.json "fx" picks them by weapon level): one-shot
    // effects at the player's weapon or the monster, drawn over both. Cleared when a battle starts or the screen closes.
    static constexpr const char* m_pszBattleFxFile = toms::key::g_pszFxBattle;
    // the battle's background (battle.json "backdrops", by floor; docs/gameplay/17 §19): an .anim covering the screen, particles over it
    std::string m_strBackdropAnim;
    std::shared_ptr<toms::res::AnimRes> m_spBackdrop;
    toms::anim::AnimPlayer m_oBackdrop;
    std::vector<toms::fx::EffectInstance> m_vBackdropFx;
    float m_fBackdropW = 1.0f, m_fBackdropH = 1.0f;
    void f_UpdateBattleBackdrop(int l_iDtMs);
    bool f_DrawBattleBackdrop();                          // false: none (the plain dark backdrop)
    void f_LoadBattleFxFile();
    std::shared_ptr<toms::res::ParticleRes> m_spBattleFxFile;
    bool m_bBattleFxTried = false;
    struct BattleFx {
        toms::fx::EffectInstance m_oFx;
        bool m_bOverUi = false;   // drawn over the UI (a track hint)
        // A projectile (docs/gameplay/20 §5): it flies from `from` to `to` on a low arc; there it stops making particles (its
        // trail fades) and its impact plays, with a sound.
        bool m_bFlying = false;
        glm::vec2 m_v2From{0.0f}, m_v2To{0.0f};
        int m_iFlightMs = 0, m_iAgeMs = 0;
        std::string m_strImpact, m_strImpactSound;
    };
    std::vector<BattleFx> m_vBattleFx;
    uint32_t m_uBattleFxSeed = 0;   // each effect of a battle its own seed, from 1 again each battle (the same fight looks the same)
    const toms::WeaponFx* f_BattleWeaponFx(bool l_bTop = false) const;   // the worn weapon's at its level (top: its best)
    void f_PlayBattleFx(const std::string& l_strEffect, int l_iWho, int l_iDelayMs = 0);   // who: -1 the player's weapon, 0.. a foe (its own)
    void f_FoeEffectsOnDeath(int l_iFoe);            // a foe fell: each of its effects and sounds stopped, faded or kept ("onFoeDeath")
    void f_PlayFoeSound(int l_iFoe, const std::string& l_strKey, float l_fPitch = 1.0f);   // a sound of the foe's, bound to it
    nlohmann::json m_jOnFoeDeath = nlohmann::json::object();   // battle.json "onFoeDeath": "fxRules" / "soundRules": name -> stop, fade, keep
    // At a point of the screen (design pixels); overUi: over the battle screen's UI (the attack track).
    void f_PlayBattleFxAt(const std::string& l_strEffect, float l_fX, float l_fY, bool l_bOverUi, int l_iDelayMs = 0, float l_fScale = 1.0f,
                          float l_fRotationDeg = 0.0f);
    // The attack feel (docs/gameplay/20; data/battle.json "feelFx").
    void f_LaunchBattleFx(const std::string& l_strEffect, glm::vec2 l_v2From, glm::vec2 l_v2To, int l_iFlightMs, const std::string& l_strImpact,
                          const std::string& l_strImpactSound);
    void f_FoeAttackFx(int l_iFoe, const toms::MoveDef& l_oMove, bool l_bReaches);   // its cast, a projectile or a strike, its sound (§5)
    bool f_FoeIsBig(int l_iFoe) const;              // a boss, or enemies.json "big": its attacks fly at the hero (§5)
    void f_HitSpray(int l_iFoe, const toms::TouchPlay::Hit& l_oH);   // a hit's particles, more with the combo; the sword's slash (§3, §4)
    void f_WandCues();                              // the staff's hints: louder and higher one after another, gone at once, back (§2)
    void f_BowGather(int l_iDtMs);                  // the bow's pull: particles drawn in, more as it charges (§4)
    glm::vec2 f_FighterCentre(int l_iWho) const { const glm::vec4 l_v4B = f_FighterBox(l_iWho); return {l_v4B.x + l_v4B.z * 0.5f, l_v4B.y + l_v4B.w * 0.5f}; }
    glm::vec2 f_WeaponPoint() const { const glm::vec4 l_v4B = f_FighterBox(-1); return {l_v4B.x + l_v4B.z * 0.731f, l_v4B.y + l_v4B.w * 0.493f}; }
    const nlohmann::json& f_FeelFx() const { return m_jFeelFx; }
    // battle.json "bodyScale": a monster's body type (enemies.json "body", 1..5) -> how many times the stage's monster height
    nlohmann::json m_jBodyScale = nlohmann::json::object();
    nlohmann::json m_jFeelFx = nlohmann::json::object();   // data/battle.json "feelFx", as the battle's rules have it (a scenario patches them)
    struct WandSeen { int m_iClockMs = -1, m_iLit = 0, m_iShown = 0, m_iPhase = -1; } m_oWandSeen;   // what f_WandCues has played
    int m_iBowGatherMs = 0, m_iBowGatherBeats = 0;
    // The attack track's hint (the owner 2026-10-24: the timing was hard): a point's window entered, or a point hit.
    void f_ComboHint(int l_iPoint, bool l_bHit);
    std::vector<glm::vec2> m_vComboPointsOnScreen;   // where the UI drew the track's points (GameUi, a frame late)
    // setBattleBoxes; the 1024 x 768 layout with one monster until told
    glm::vec4 m_v4PlayerBox{262, 84, 134, 134};
    glm::vec4 m_arrFoeBox[toms::g_iMaxFoes] = {{652, 84, 134, 134}, {652, 84, 134, 134}, {652, 84, 134, 134}};
    glm::vec4 f_FighterBox(int l_iWho) const { return l_iWho < 0 ? m_v4PlayerBox : m_arrFoeBox[std::min(l_iWho, toms::g_iMaxFoes - 1)]; }
    void f_UpdateBattleFx(int l_iDtMs);                      // Game::update
    std::string m_strPreviewHeldFile;                       // setPreviewHeld
    std::shared_ptr<toms::res::AnimRes> m_spPreviewHeld;   // its clips
    toms::anim::CharacterRig m_oPreviewRig;               // the preview clip with the held clip at its hand
    toms::anim::Clip m_oPreviewFitted;                    // the preview clip fitted to previewCharacter_
    void f_StartPreviewClip();                            // (re)starts the preview clip, fitted when a character is set
    int m_iPreviewAnimVer = -1;                           // previewAnim_'s version when the clip was started
    toms::anim::AnimPlayer m_oPreviewPlayer;
    int m_iPreviewCrowdN = 0;                             // setPreviewCrowd
    std::vector<toms::anim::AnimPlayer> m_vPreviewCrowd;
    double m_dPreviewDrawMs = 0;
    std::vector<toms::anim::NodePose> m_vPreviewPoses;   // per-frame scratch
    toms::anim::DrawList m_oPreviewDraws;
    std::vector<std::string> m_vPreviewMissing;          // sprites already reported missing
    std::string m_strPreviewMeshSprite;                    // playPreviewMesh
    double m_dPreviewMeshMs = 0;
    std::vector<MeshVertex> m_vPreviewMeshVerts;         // per-frame scratch
    std::vector<uint16_t> m_vPreviewMeshIdx;
    void f_DrawPreviewMesh();
    void f_DrawPreviewAnim();
    // The looping clips on the map (a monster's idle clip "idle_<enemy id>" from the manifest's "anim.monster_idle",
    // an object's own clip): one clock, each tile at its own phase (map_objects.cpp).
    std::vector<toms::anim::NodePose> m_vIdlePoses;   // per-frame scratch (drawClipInRect)
    toms::anim::DrawList m_oIdleDraws;
    double m_dIdleClockMs = 0;
    // Objects' looks (docs/gameplay/02_STAGE_EDITOR.md): an object's own "look" over its type's ("look" in enemies.json /
    // items.json / its NPC file), field by field -- read once per floor, when its objects are made (map_objects.cpp).
    // typeSprite: the sprite a type is drawn with (its look, else the old table).
    nlohmann::json f_LookOf(const Entity& l_oE) const;
    nlohmann::json f_TypeLook(const std::string& l_strKind, const std::string& l_strId) const;
    std::string f_TypeSprite(const std::string& l_strKind, const std::string& l_strId) const;
    // An object's linked events, when the player interacts with it: a talk event wakes (a topic), the others run when
    // their `requires` holds (with their title as a notice), else they are started.
    void f_FireLinkedEvents(const Entity& l_oE);
    // A clip at time t placed on a map rect: its (0, 0) on the rect's bottom centre, scaled so a sprite of
    // spriteW x spriteH pixels fills the rect. False = nothing drawn.
    bool f_DrawClipInRect(const toms::anim::Clip& l_oClip, float l_fT, const toms::anim::AtlasSet& l_oSet, float l_fX, float l_fY, float l_fW, float l_fH,
                        float l_fSpriteW, float l_fSpriteH, const float l_arrTint[4]);
    bool f_DrawClipInRect(const toms::anim::Clip& l_oClip, float l_fT, const toms::anim::AtlasSet& l_oSet, float l_fX, float l_fY, float l_fW, float l_fH,
                        float l_fSpriteW, float l_fSpriteH);
    // A resource file the game asked for is missing or broken: a message on screen (once per file) and in the log.
    void f_ResourceMissing(const std::string& l_strName, const std::string& l_strPath, const std::string& l_strError);
    // Its messages wait here until the map is on screen (startup loads happen behind the title, and a new game or a
    // load clears the notifications).
    std::vector<std::string> m_vResourceMessages;
    // The floor's things, a class per kind (map_objects.h), made when a floor loads; they draw themselves by status.
    MapFloor m_oFloor;
    void f_DrawTileLayer(const MapView& l_oV, int l_iStartX, int l_iEndX, int l_iStartY, int l_iEndY);   // tiles, locked stairs, walk target
    // A door the player walks into with its key: the key is used, the door opens (its clip plays), the tile becomes
    // floor and stays open on later visits (EntityStatus::Opened). The player stays where they were.
    void f_OpenDoor(int l_iX, int l_iY, char l_cC);
    std::shared_ptr<toms::res::ParticleRes> m_spPreviewFxFile;   // playPreviewFx (its atlases come with it)
    std::string m_strPreviewFxName;
    int m_iPreviewFxVer = -1;
    toms::fx::EffectInstance m_oPreviewFx;
    float m_fPreviewFxIdle = 0;                         // seconds since a one-shot effect finished
    int m_iFxGpuThresholdOverride = -1;                 // --fx-gpu-threshold (-1: the setting)
    // art styles (assets/media/styles/styles.json); style 0 = the original art
    std::vector<toms::ArtStyle> m_vArtStyles{toms::ArtStyle{}};
    int m_iLoadedArtStyle = 0;                  // the one whose sprites were loaded at startup
    int m_iSpriteAtlasRevision = 0;             // +1 per atlas load (uiSpritesheet changed)
    // item definitions (id -> json from data/items.json, the all-item file)
    std::map<std::string, nlohmann::json> m_mapItemDefs;
    // data/stats.json: the attributes and the level-up numbers.
    struct AttrDef { std::string m_strId, m_strShortName, m_strNameKey, m_strDescKey; int m_iBase = 5, m_iPerLevel = 1;
                     std::vector<std::pair<std::string, float>> m_vEffects; };   // (stat, per point above base)
    // maxLevel: the top level; points: the attribute points each level gives (by level reached), freePoints below them.
    struct LevelUpRule { int m_iExpPerLevel = 30, m_iAtk = 2, m_iDef = 1, m_iMaxhp = 10, m_iFreePoints = 0, m_iMaxLevel = 99;
                         std::vector<toms::LevelPoints> m_vPoints; };
    std::vector<AttrDef> m_vAttrDefs;
    LevelUpRule m_oLevelUp;
    // The weapons / armor / talents the player owns (bought or forged), in the order they were got.
    std::vector<std::string> m_vGearOwned;
    std::map<std::string, int> m_mapWeaponExp;   // weapon EXP of each weapon (weaponKey()); its level comes from it
    // ---- the player menu's state (inGameMenuOpen_ = it is open) ----
    MenuTab m_eMenuTab = MenuTab::Status;
    int m_iAttrSel = -1;          // Status: the attribute whose description shows (-1 none)
    // Status: points placed with + / - but not confirmed yet, by attribute id (the owner 2026-10-24: the points count
    // only after Confirm, which also saves). Closing the menu drops them.
    std::map<std::string, int> m_mapAttrDraft;
    int f_AttrDraftTotal() const { int l_iN = 0; for (const auto& [l_strId, l_iK] : m_mapAttrDraft) l_iN += l_iK; return l_iN; }
    int m_iGearSlot = 0;          // Equipment: 0 weapon, 1 armor
    int m_iGearSel = 0;
    int m_iItemFilter = 0;        // Items: 0 all, 1 consumables, 2 gear, 3 keys
    int m_iItemSel = 0;
    int m_iCardSel = 0;          // the Cards tab's selected card
    bool m_bNewsCards = false;   // a dot on the Cards tab: a card was got since it was last opened
    std::mt19937 m_oCardRng{20261021u};   // card drops (fixed seed: scripted test runs repeat)
    std::vector<std::string> f_CardCells() const;   // the monsters with a record, easiest first
    int m_iEventFilter = 0;       // Events: 0 unfinished, 1 all
    int m_iEventSel = 0;
    bool m_bDropConfirmOpen = false;
    bool m_bDropConfirmYes = false;
    bool m_bStoreFromMenu = false;   // the store was opened from the System tab: closing it goes back there
    bool m_bSaveLoadOpen = false;
    int m_iSaveLoadMode = 0;         // 0 save, 1 load
    int m_iSaveLoadSel = 0;          // the slot (0-based)
    bool m_bSaveLoadConfirm = false, m_bSaveLoadConfirmYes = false;
    std::vector<toms::SlotSummary> m_vSaveLoadSlots;   // read from disk when the screen opens and after a save
    // "Something new inside" dots on ≡ and on a tab.
    bool m_bNewsStore = false, m_bNewsGear = false, m_bNewsEvents = false;
    size_t m_nEventLogSeen = 0;      // the event log's last count (+1; 0 = take it without news), update()
    // The event pools (data/events/pool_*.json), for the player's event log (docs/gameplay/event_editor/06_EVENT_LOGIC.md
    // section 7): titles, descriptions, links, and whether the event is listed (its kind's "inLog" in
    // events/kinds.json, or its own). Floor events with no pool record still fire; they are just not listed.
    struct EventDef {
        std::string m_strKind, m_strTitleKey, m_strDescKey;
        std::string m_strStairsFrom, m_strStairsTo;     // a "go to the stairs" event (Q7): its text is built from these
        std::vector<std::string> m_vNext;
        nlohmann::json m_jRequires;              // null = none
        nlohmann::json m_jActions;               // null = none: story actions (story_actions.h), run once
        std::string m_strTrigger = "step";         // "step" / "talk" / "chain" / "varChanged" (06 section 1)
        std::string m_strTriggerVar;               // "varChanged": the variable
        bool m_bOnce = true;                     // false: repeatable, until maxTimes (0 = no limit); next wakes once
        int m_iMaxTimes = 0;
        std::string m_strTextKey;                  // "text"
        bool m_bInLog = true;
    };
    // A gate ("gate:<item id>" in a floor's legend): opens only for that item, which it takes; else says what
    // it needs. Opened gates stay open (entity status Opened).
    void f_BumpGate(Entity& l_oGate);
    std::map<std::string, EventDef> m_mapEventDefs;
    std::map<std::string, std::vector<std::string>> m_mapChapterEvents;   // chapter id -> its story events (beats)
    void f_LoadEventDefs();
    // Q7: stairs locked by an event. stairUnlocks_: event id -> the stairs it opens (floor, target), from every
    // floor spec's `stairs`; loadStairLocks also adds the "go to the stairs" events. finishEvent is every
    // finished event (the run flag, the log, `next`, and the stairs it opens with their dialog).
    struct StairUnlock { std::string m_strFloor, m_strTo; };
    std::map<std::string, std::vector<StairUnlock>> m_mapStairUnlocks;
    void f_LoadStairLocks();
    void f_FinishEvent(const std::string& l_strEvId, int l_iDepth = 0);
    // W38: a stair waits for all of its events. A boss guards this floor's up stair too (the owner, 2026-11-13): while a
    // monster with enemies.json "guardsStairs" stands here unbeaten, the stair up stays locked (f_StairGuard).
    bool f_StairLocked(const toms::StairLink& l_oS) const {
        for (const std::string& l_strE : l_oS.m_vUnlockEvents) if (!m_oRun.f_EventFinished(l_strE)) return true;
        const bool l_bHere = !m_oSt.m_vStairs.empty() && &l_oS >= &m_oSt.m_vStairs.front() && &l_oS <= &m_oSt.m_vStairs.back();
        return l_bHere && l_oS.m_bUp && !f_StairGuard().empty();
    }
    std::string f_StairGuard() const;                  // the unbeaten stair guard's monster id on this floor; "" none
    std::string f_EnemyDisplayName(const std::string& l_strId) const;   // enemies.json "name", in the language
    // The floor's stairs that are open but not taken yet since they opened (their "go to the stairs" event is
    // started, not finished): the map points at them with an arrow (W38).
    bool f_StairJustOpened(const std::string& l_strFloor, const toms::StairLink& l_oS) const;
    const toms::StairLink* f_FindStair(const std::string& l_strFloor, const std::string& l_strTo) const;
    std::map<std::string, std::pair<int, int>> m_mapStairSpots;   // "F01>F02" -> the stair's tile, from floors loaded so far
    bool f_StepOntoStairs(int l_iX, int l_iY);       // the stair at (x, y), if open: ask to take it
    void f_BumpLockedStairs(const toms::StairLink& l_oS);
    std::string m_strStairsConfirmGoEvent;       // the "go to the stairs" event taking the asked stair finishes
    struct Notice { std::string m_strTitle, m_strBody; };
    std::vector<Notice> m_vNotices;
    // The ids an event is linked to: its `next`, the events whose `next` names it, and the events its
    // `requires` waits for (runFlagSet "event_<id>" / eventDone).
    std::vector<std::string> f_ConnectedEvents(const std::string& l_strEventId) const;
    // data/story/counters.json's state words (insight, resolve, humanity), for the Status tab.
    std::map<std::string, nlohmann::json> m_mapCounterLabels;
    bool m_bFullscreenAvailable = false, m_bFullscreenOn = false, m_bFullscreenRequest = false;
    // dialogue state
    bool m_bInDialogue = false;
    int m_iDlgSel = 0;                     // currently highlighted dialogue choice (gamepad nav)
    std::string m_strDlgNpc;
    std::string m_strDlgNode = "root";
    nlohmann::json m_jDlgData;
    std::vector<DialogueChoice> m_vDlgChoices;
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md, talk_system.h): the NPC files, the events bound
    // to each NPC (in menu order), and what the open dialogue is for: a plain dialogue, an NPC's chat line, an
    // event's topic, a finished event's after-dialogue, or the topic menu.
    enum class DlgMode { Plain, Chat, Topic, After, Menu };
    DlgMode m_eDlgMode = DlgMode::Plain;
    std::string m_strDlgEvent;              // Topic / After: the event
    std::string m_strDlgSpeaker;            // the name on the dialogue box (empty: none)
    std::string m_strDlgTalkNpc;            // the NPC being talked to (the menu's "Just chat")
    bool m_bDlgFinishOnClose = false;     // Topic: the file's "finishOnClose"
    std::map<std::string, toms::talk::NpcDef> m_mapNpcDefs;
    std::map<std::string, std::vector<toms::talk::Topic>> m_mapNpcTopics;
    std::mt19937 m_oTalkRng{20261016u};   // "random" chat lines (fixed seed: scripted test runs repeat)
    void f_LoadNpcs();
    bool f_Holds(const nlohmann::json& l_jCondition) const;   // a `requires` against the run, with the current stage
    const toms::talk::NpcDef& f_NpcDef(const std::string& l_strNpcId);
    nlohmann::json f_ReadDialogue(const std::string& l_strId) const;
    bool f_OpenDialogue(const nlohmann::json& l_jData, DlgMode l_eMode);   // from its start node; false = nothing to show
    void f_PlayChatLine(const std::string& l_strNpcId, int l_iLine);
    void f_OpenTopic(const std::string& l_strEventId, bool l_bFinished);
    void f_OpenTopicMenu(const std::string& l_strNpcId, const std::vector<toms::talk::MenuEntry>& l_vMenu);
    void f_EndDialogue();                 // closes it (Topic + finishOnClose: finishes the event)
    void f_FinishTalkEvent(const std::string& l_strEventId);
    // Wakes an event (an event's `next`, a choice's `fire`, the fireEvent action): it is started; a "chain" event
    // whose `requires` holds runs at once (its actions, then finished, which wakes its own `next`).
    void f_WakeEvent(const std::string& l_strEventId, int l_iDepth = 0);
    // Every story action in the game goes through here (story_actions.h): events, chat lines, dialogue choices.
    void f_RunStoryActions(const nlohmann::json& l_jActions);
    // One run of an event (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 11.1): its record's actions, counted; the first
    // time it is also finished (flag, log, `next`, stairs). A finished one-off does nothing (false); a repeatable one
    // ("once": false) runs again until "maxTimes".
    bool f_CompleteEvent(const std::string& l_strEventId, int l_iDepth = 0);
    bool f_CanRepeat(const std::string& l_strEventId) const;
    // An event variable changed: the "varChanged" events on it whose `requires` holds run (with their title as a notice).
    void f_VarChanged(const std::string& l_strVar);
    int m_iVarChangeDepth = 0;            // a variable changed by a varChanged event's own actions: at most 16 deep
    void f_NoticeEventTitle(const std::string& l_strEventId);   // an event with no tile and no NPC ran: its title as a notice
    std::string f_NpcSprite(const std::string& l_strNpcId) const;   // its NPC file's "sprite", else npc_<id>, else the old table
    // enemy templates
    std::map<std::string, nlohmann::json> m_mapEnemyTpl;
    // The battle scene's tuning, from data/battle.json (loadAssets). Without the file or a key, the defaults below.
    // A bar starts slow (v0) and speeds up to its top speed (vmax) on a cubic curve (power_bar.h); the two are scaled apart.
    float m_fBarSlowScale = 1.0f;                           // "bar_slow_speed_scale": multiplies the slow start (v0)
    float m_fBarFastScale = 1.0f;                           // "bar_fast_speed_scale": multiplies the top speed (vmax)
    int m_iBarCooldownMs = CombatState::m_iBarCooldownMs;     // "bar_cooldown_ms": the wait after a tap before a bar moves again
    int m_iAttackPauseMs = 450;     // "attack_pause_ms": both sides' clocks pause this long for each attack (0: off)
    int m_iHitNumberDelayMs = 180;  // "hit_number_delay_ms": the damage number appears this long after the attack starts
    // Delayed calls of the scenes (the battle's damage numbers). battleTimers_ owns the battle's: reset when a battle
    // starts, so a number from the last fight never lands in the next one.
    toms::TimerQueue m_oTimers;
    toms::TimerOwner m_oBattleTimers;
    toms::WeaponPlayTable m_oWeaponPlay;   // data/weapon_play.json
    toms::LooksTable m_oLooks;             // data/looks.json: the status map (docs/gameplay/11 §4.2)
    toms::MovesTable m_oMoves;             // data/moves.json: the monsters' moves (docs/gameplay/13 §6.1)
    toms::PaceSettings m_oPaceSettings;    // data/battle.json "pace", "stamina"
    toms::BreakSettings m_oBreakSettings;  // data/battle.json "break"
    toms::FeelSettings m_oFeel;            // data/battle.json "feel" (docs/gameplay/15 §3.8)
    toms::GradeSettings m_oGrade;          // "grade" (§3.6)
    toms::GuardSettings m_oGuard;          // "guard" (§3.2): the timed button, or the bar
    std::mt19937 m_oShakeRng{20261031u};
    std::string m_strUltimateOverride;       // a scenario's battle "ultimate": a move id instead of the weapon's
    float m_fScenarioBattleSpeed = 0.0f;     // a scenario's battle "speed" (a test keeps 1 whatever the player chose); 0: the setting
    std::vector<float> m_vBattleSpeeds{0.75f, 1.0f, 1.25f, 1.5f};   // battle.json "battleSpeeds"
    std::string m_strPlayOverride;           // a scenario's battle "play" ("" = the weapon's; scenarios: "track" unless set)
    std::string m_strPaceOverride;           // setBattlePace / a scenario's "pace" ("" = the weapon's or the file's)
    bool m_bComboHoldsMonsters = false;    // data/battle.json "combo_holds_monsters": their clocks wait for a running combo
    bool m_bComboRunsThroughAttacks = true;   // "combo_runs_through_attacks": a combo's marker keeps moving in an attack pause
    nlohmann::json m_jBattleJson = nlohmann::json::object();   // data/battle.json as read (a scenario's "rules" patch it)
    std::mt19937 m_oMoveRng{20261025u};    // the monsters' next moves (reseeded each battle: the same fight goes the same way)
    // the plays' places and orders (the staff's circles, the sword's prompts, the bow's targets): new each battle and
    // each attack; a scenario test (its "test" frames) seeds it the same way every run, so its results repeat
    std::mt19937 m_oPlayRng{20261031u};
    void f_StartAttackPause();
    void f_PopDamage(int l_iDmg, int l_iWho, const std::string& l_strStyle);   // a damage number over who (-1 the player, 0.. a foe), hitNumberDelayMs_ from now
    // current stage id
    std::string m_strCurStage;
    std::string m_strDataDir;
    // Milestone 3: story flags + main-story beat, evaluated by GameConditionContext (game.cpp)
    // for door/key gating and dialogue `requires` gating. In-memory only this milestone — reading
    // and writing this to an actual save file on disk is Milestone 5's job (Stage Select's
    // "Continue"), per docs/architecture/IMPLEMENTATION_ROADMAP.md.
    toms::MetaSaveData m_oMeta;
    // Milestone 7: per-tile enemy/item status (Milestone 3's Entity Status System, wired in for
    // real here) -- a defeated monster or collected item stays cleared when the floor is
    // reloaded (stairs, or Milestone 5's Stage Select hub), matching the owner's decision that
    // cleared floors don't repopulate. Keyed by entityStatusKey(stageId, x, y); in-memory only
    // for now, same "persistence is later work" caveat as meta_ and missionTrackers_. An opened
    // door is recorded too (EntityStatus::Opened, see openDoor): it stays open, and costs its key once.
    std::map<std::string, std::string> m_mapEntityStatus;
    // Milestone 6: equipment layer over the Power Bar/damage formulas (docs/design/MAIN_BATTLE_SCENE_DESIGN.md
    // §4). equipmentDefs_ is intentionally empty until Milestone 8 loads data/equipment.json --
    // equipped_ starts fully empty (nothing equipped), so effectiveAttackBar/effectiveDefenseBar/
    // effectiveMaxMult all fall back to baseline geometry/2.0x until real items exist to equip.
    toms::EquippedSet m_oEquipped;
    std::map<std::string, toms::EquipmentDefinition> m_mapEquipmentDefs;
    // S4: the skill tree's content (data/skills.json) -- loaded once at boot, mirroring
    // equipmentDefs_ immediately above. What's actually OWNED lives on run_ (RunStoryState),
    // not here, matching the same Game-owns-state/content-is-data split as equipped_/equipmentDefs_.
    std::map<std::string, toms::SkillDefinition> m_mapSkillDefs;
    // S5: forging's content (data/forge.json) -- loaded once at boot, same shape as skillDefs_
    // above. What's actually KNOWN lives on meta_ (MetaSaveData::forgeRecipesKnown, permanent,
    // unlike skillsOwned() which is run-scoped) -- see forge_system.h's own comment for why.
    std::map<std::string, toms::ForgeRecipeDefinition> m_mapForgeDefs;
    // S6: the village hub's content (data/hub.json) -- loaded once at boot, same shape as
    // skillDefs_/forgeDefs_ above. A hub location has no "owned" state of its own (see
    // hub_system.h's own comment) -- unlocking is just reading the run flag its unlockFlag names.
    std::map<std::string, toms::HubLocationDefinition> m_mapHubDefs;
    // S7 (equipment actives, first slice): content loaded once at boot, same shape as
    // skillDefs_/forgeDefs_/hubDefs_ above. There is no separate "known" list yet (unlike
    // forgeRecipesKnown) -- nothing in this slice's content grants an active independently of the
    // equipment that carries it.
    std::map<std::string, toms::ActiveDefinition> m_mapActiveDefs;
    // M7 (first slice): content loaded once at boot, same shape as skillDefs_/forgeDefs_/hubDefs_/
    // activeDefs_ above.
    toms::EndingsTable m_oEndingsTable;
    // Empty = no ending showing. The one writer is Game::triggerEnding() (game_combat.cpp);
    // dismissEndingScreen()/rebirth() (public, see their declarations above) both clear it.
    std::string m_strActiveEndingId;
    void f_TriggerEnding(const std::string& l_strEndingId);
    // M8 (first slice): the rebirth config, same "load once at boot" shape as the above.
    toms::CyclesConfig m_oCyclesConfig;
    // The starting stats newGame() AND Game::rebirth() both need -- named once here instead of the
    // literals 12/4/120 living independently in two places (rebirth() computes a HALVED bonus
    // *over* these, so it needs the exact same baseline newGame() used, not just "looks about
    // right").
    static constexpr int m_iStartingAtk = 12, m_iStartingDef = 4, m_iStartingHp = 120;
    // The weapon a new game (and a rebirth) starts with, owned and equipped: the bow (owner, 2026-10-22). In battle the
    // player is drawn as the archer with it (its "battleAnim" in data/items.json).
    static constexpr const char* m_pszStartingWeapon = "bow";
    // Milestone 4 (Encounter Resolution): when a monster tile resolves to EncounterKind::
    // DialogueGate, the enemy instance is stashed here so a later `action.enterBattle` dialogue
    // choice knows what to fight. hasPendingEncounter_ guards against acting on a stale/unset
    // EnemyInst (whose numeric fields are otherwise uninitialized garbage, matching CombatState's
    // own `enemy` member convention — see startCombat()).
    EnemyInst m_oPendingEncounterEnemy;
    bool m_bHasPendingEncounter = false;
    // Milestone 4 (Mission System): missionDefs_ is intentionally empty until Milestone 8 loads
    // data/missions.json -- the tracker map is still real, live state (a mission "started" by id
    // is meaningful even before content defines what that id means).
    std::map<std::string, toms::MissionDefinition> m_mapMissionDefs;
    std::map<std::string, toms::MissionTracker> m_mapMissionTrackers;
    void f_StartMission(const std::string& l_strId);
    // Milestone 8: grants a Completed mission's reward and flips it to Claimed. Idempotent --
    // a no-op unless the tracker's state is exactly Completed, so re-triggering this (e.g. the
    // dialogue choice that calls it stays visible after claiming) can never double-grant.
    void f_ClaimMission(const std::string& l_strId);
    void f_RunDialogueAction(const nlohmann::json& l_jAction);
    // S4: applies a chapter's top-level `grants` (skills/skillPoints; hub/unlocks as flags for a
    // later phase to read) exactly once -- called from loadStage() the first time a floor in that
    // chapter is reached. Idempotent via a reserved run flag ("_chapter_granted.<id>"), the same
    // "reuse the existing mechanism instead of a parallel one" choice this project keeps making
    // (see entity-status reuse, M3's log). A no-op for a chapter id with no file yet (ch_04+),
    // so this is safe to call unconditionally as soon as any floor names its chapter.
    void f_ApplyChapterGrants(const std::string& l_strChapterId);
    void f_WireMissionEvents();   // subscribes mission-progress handlers to the global EventBus once
    // This Game's handlers on the global EventBus: destroyed with the Game, which removes them (event_bus.h).
    std::vector<toms::Subscription> m_vBusSubs;
    // Milestone 5: daily-mission reset check (architecture-doc §8.3), run once per session start
    // (see loadAssets). Inert today since missionDefs_ is empty until Milestone 8, but pushes a
    // real notification the moment a tracked daily mission actually rolls to Available.
    void f_RollDailyMissions();
    // Milestone 5: transient toast queue -- {message, remaining_ms}, decremented in update().
    std::vector<std::pair<std::string, int>> m_vNotifications;
    void f_PushNotification(const std::string& l_strMsg);
    // Milestone 5: Stage Select hub state.
    bool m_bStageSelectOpen = false;
    // Milestone 8: `preview` is the recommended-stats blurb shown in Stage Select, authored per
    // stage JSON's optional top-level "preview" string (empty for a file that doesn't set one).
    struct StageInfo { std::string m_strId; std::string m_strName; int m_iIndex = 0; std::string m_strPreview;
                       std::string m_strFileStem; };   // filename stem, e.g. "stage01" (the id inside
                                                  // the JSON is "stage_01" -- the game loads by
                                                  // stem, so matching needs both)
    std::vector<StageInfo> m_vStageList;
    bool m_bStageListLoaded = false;
    void f_EnsureStageListLoaded();
    // Milestone 9: stairs confirm-before-transition state (see stairsConfirmOpen()'s
    // declaration above for why). requestStageTransition() is what movePlayer() now
    // calls instead of loadStage() directly when the player steps onto 'U'/'D'.
    bool m_bStairsConfirmOpen = false;
    bool m_bStairsConfirmIsUp = false;
    std::string m_strStairsConfirmTarget;
    void f_RequestStageTransition(const std::string& l_strTarget, bool l_bIsUp);
    // Milestone 9 polish: per-axis "held direction" state for setMoveHeldX/Y (declared
    // above) -- holdMs/repeating are ticked in update(). kMoveInitialDelayMs is the pause
    // after the immediate first step before repeating starts (long enough that a quick tap
    // never double-steps); kMoveRepeatMs is the steady repeat rate once it's going.
    struct MoveHoldAxis { int m_iDir = 0; int m_iHoldMs = 0; bool m_bRepeating = false; };
    MoveHoldAxis m_oMoveHoldX, m_oMoveHoldY;
    static constexpr int m_iMoveInitialDelayMs = 220;
    static constexpr int m_iMoveRepeatMs = 110;
    // Click-to-move state (see walkTo()): the remaining steps, first = next tile.
    std::vector<std::pair<int, int>> m_vWalkPath;
    int m_iWalkTargetX = -1, m_iWalkTargetY = -1;
    int m_iWalkTimerMs = 0;
    bool f_WalkPassable(int l_iX, int l_iY) const;   // a tile the route may cross (plain floor, nothing on it)
    bool f_PlanWalk(int l_iTx, int l_iTy, std::vector<std::pair<int, int>>& l_vOut) const;
    void f_WalkStep();
    // M2 styling spike backdrop state. Declared unguarded so Game::draw() — shared between the
    // desktop and web builds — can call drawStylingSpikeBackdrop() unconditionally; it is a no-op
    // whenever stylingSpikeVisible_ is false. GameSession drives it (F2) on desktop and web.
    bool m_bStylingSpikeVisible = false;
    float m_arrStylingSpikeRect[4] = {0, 0, 0, 0};   // x,y,w,h — set by drawStylingSpike(), read by the backdrop
    void f_DrawStylingSpikeBackdrop();
    int m_iTotalStages = 10;   // highest stage index (derived from data/stages at loadStage)

    // ---- maze camera ----
    // The camera itself lives in src/game/camera.{h,cpp} as toms::Camera: mode (Follow/Rooms),
    // zoom (viewCols), the eased position and its target, plus the viewport math and the edge
    // clamping. Deliberately free of renderer/game coupling (pure math, unit-tested by
    // camera_test.cpp) -- this class only hands it the drawing area in pixels and the focus tile.
    toms::Camera m_oCam;
    // Adapter: the camera needs the drawing area in pixels (the 1024x768 design canvas unless the
    // renderer reports otherwise), which only Game knows. Kept as one call site so draw() and
    // update() can never compute a different viewport.
    void f_CameraViewportTiles(float& l_fTs, int& l_iCols, int& l_iRows) const;
    // store system state
    std::vector<StoreItemDef> m_vStoreItems;
    int m_iStoreUnlockStage = 3;   // stage index at which the shop unlocks (from store.json)
    bool m_bStoreUnlocked = false;
    bool m_bStoreUnlockDlg = false; // "shop unlocked!" popup showing (with confirm button)
    bool m_bStoreOpen = false;      // store overlay open
    int m_iStoreSel = 0;           // selected card index (keyboard nav), local to the current tab
    int m_iStoreTab = 0;           // 0=potions,1=weapons,2=armor,3=talents (see storeTabIndices())
    std::string m_strToastMsg;        // transient message ("金錢不足")
    int m_iToastTimer = 0;         // ms remaining for toast
    int m_iShakeTimer = 0;         // ms remaining for "not enough gold" shake
    // Raw json (string or {code:text}) from store.json, resolved via Locale::field() at draw
    // time -- loadStore() runs before the title phase sets locale_ to the player's saved
    // language, so resolving here (instead of once at load) is what makes it show correctly.
    nlohmann::json m_jStoreTitle = "道具商店";

    // ---- in-game menu (walking-phase HUD gear icon): Save / Settings (language) / Back to
    // Title. Built directly as Game state (not a separate testable class, unlike TitleScreen)
    // to match how every other in-game modal (store/inventory/stairs-confirm) already works in
    // this file -- there's no headless-test need here the way the title phase has.
    bool m_bInGameMenuOpen = false;
    enum class InGameMenuPage { Main, Settings, Skills, Forge, Hub };
    InGameMenuPage m_eInGameMenuPage = InGameMenuPage::Main;
    int m_iInGameMenuSel = 0;               // highlighted row on whichever page (keyboard nav)
    bool m_bInGameLangConfirmOpen = false;  // "switch to XXX?" sub-dialog, mirrors the title's own
    int m_iInGameLangConfirmIdx = 0;
    bool m_bInGameLangConfirmYes = true;
    bool m_bInGameConfirmIsStyle = false;   // that dialog is about an art style (idx = style index)
    // S5/S6: Forge and Village are each their OWN conditional row, gated on their own hub.* flag
    // (set by Game::applyChapterGrants) -- they only appear once the player has actually reached
    // them narratively, rather than showing an always-empty screen from floor 1 the way Skills
    // does (S4 chose the opposite trade-off there; these two have real gating data to read).
    // Nothing enforces which of the two unlocks first (today's content happens to grant Village at
    // F7 before Forge at F21), so mainMenuOrder() below re-derives the Main page's row LIST fresh
    // every call instead of two independent hardcoded row-count branches -- with two conditional
    // rows, "N booleans -> a hand-picked row count" stops being a small special case.
    bool f_ForgeMenuUnlocked() const { return m_oRun.f_Flag("hub.forge"); }
    bool f_HubMenuUnlocked() const { return m_oRun.f_Flag("hub.village"); }
    // The Main page's row order/count as a pure function of the two flags above (plus the three
    // always-present rows) -- so draw and every input handler below call this ONE function instead
    // of separately re-deriving "is Forge row 3 or row 4 this time," the same principle
    // skillMenuOrder()/forgeMenuOrder() already apply to their own sub-pages.
    enum class MainMenuRow { Store, Save, Settings, Skills, Village, Forge, Fullscreen, BackToTitle };
    std::vector<MainMenuRow> f_MainMenuOrder() const;
    void f_CloseInGameMenu();                // closes the whole menu (any page/dialog)
    int f_MainMenuRowIndex(MainMenuRow l_eR) const;   // its row on the System tab (0 if absent)
    // Saves, tears down transient modal/run-in-progress state, and reopens the title (the
    // inverse of newGame()/applyLoadedRun() -- see their resets for what this mirrors).
    void f_ReturnToTitle();

    // miniaudio backs both desktop (native device backends) and the browser
    // build (Web Audio via Emscripten) behind this one interface -- no-op
    // when no audio device/context is available (e.g. headless CI).
    Audio m_oAudio;

    // ---- Title phase state (see the public title block above) ----
    toms::TitleScreen m_oTitle;
    toms::GameSettings m_oSettings;
    toms::Locale m_oLocale;
    int m_iActiveSlot = 0;      // 0 = none chosen yet; set by newGame()/continueFromSlot()
    bool m_bSlotReadOnly = false;   // the active slot holds a newer game's save: played, never written (docs/gameplay/05)
    int m_iPlayTimeSec = 0;     // this run's accumulated play time (shown in the Continue list)
    bool m_bSaveDirty = false;  // progress changed since the last slot write
    int m_iSaveFlushMs = 0;     // countdown used to throttle autosave (see update())
    static constexpr int m_iAutosaveIntervalMs = 3000;   // flush at most this often, when dirty
    void f_MarkProgressDirty() { m_bSaveDirty = true; }
    // The save key of the object at (x, y) on the open floor: by its stable uid when the stage file gives one, else
    // by its position (entity_status.h).
    std::string f_StatusKeyAt(int l_iX, int l_iY) const;
    // Old saves keyed this floor's objects by position (and some by the file name rather than the floor id): move
    // those statuses to the keys statusKeyAt uses now. Called when a floor loads.
    void f_MigrateStatusKeys();
public:
    // Writes the run now if it has unsaved changes -- the host calls it when the window closes or the app goes to
    // the background, so the autosave's last few seconds are not lost (docs/gameplay/05_SAVE_SYSTEM.md).
    void f_FlushSave() { if (m_bSaveDirty && !m_oTitle.f_IsOpen()) f_SaveCurrentRun(); }
private:
    // Performs whatever the title phase asked for (see title_screen.h's TitleAction).
    void f_HandleTitleAction(toms::TitleAction l_eA);
    std::string f_StageDisplayName(const std::string& l_strId) const;
    toms::RunSaveData f_RunSaveFromState() const;
    // Applies a loaded slot to the live game state, then loads its stage.
    void f_ApplyLoadedRun(const toms::MetaSaveData& l_oM, const toms::RunSaveData& l_oR, int l_iSlot, int l_iPlayTimeSec);
    float m_fUiScale = 1.0f;    // setUiScale(): dialogue box size on phones
    // Test scenarios (startScenario): what was asked for, checked by scenarioCheck.
    std::string m_strScenarioName, m_strScenarioScene;
    nlohmann::json m_jScenarioExpect;
    int m_iScenarioTestFrames = 0;
    std::string m_strScenarioTestKeys;
    std::string m_strScenarioTestClicks;
    std::string m_strScenarioTestDrags;
    // The weapon test arena (scene "arena", toms_game --arena; docs/gameplay/07_GAMEPLAY_PLAN.md phase 0): fight after fight
    // against the listed enemies in turn, full HP each time, no rewards and no story effects (no deaths counted).
    struct Arena {
        bool m_bOn = false;
        std::vector<std::string> m_vEnemies;
        nlohmann::json m_jOverrides;     // "battle"'s hp / atk / def / atk_interval_ms, for every fight
        size_t m_nNext = 0;
        int m_iFights = 0, m_iWins = 0, m_iLosses = 0;
        bool m_bTrial = false;           // the start story's weapon trial (docs/gameplay/07 phase 3): one fight, then back to the master
        bool m_bTraining = false;        // the training ground (docs/gameplay/16 §5): fightsMax fights, EXP and weapon wins x2
        int m_iFightsMax = 0, m_iHpBefore = 0, m_iExpGained = 0, m_iWeaponExpGained = 0;
        int m_iTutorialStep = -1, m_iTutorialCount = 0;   // the new game's tutorial: this fight's step (-1: none)
        int m_iTutorialPopMs = 0;        // how long since its box popped up
    };
    Arena m_oArena;
    toms::BattleTutorial m_oTutorial;      // battle.json "tutorial" (docs/gameplay/19 B6)
    std::set<int> m_setTutorialSeen;       // its "once" steps done since this new game began
    const toms::TutorialStep* f_TutorialStepNow() const;
    void f_TutorialGoTo(int l_iStep);
    void f_TutorialEvent(const std::string& l_strType, int l_iWho, const std::string& l_strDetail);
public:
    // The start story (docs/gameplay/07 phase 3): the weapon master lets the player try the weapons (dialogue/weapon_master.json),
    // a trial fight each, until one is chosen; the run then begins on stage 1. Opened by the title's New Game.
    void f_StartWeaponTrial();
    // --skip-start-story (the screenshot tests): New Game goes straight to stage 1, as before the start story.
    void f_SetSkipStartStory(bool l_bOn) { m_bSkipStartStory = l_bOn; }
    // Reward events (docs/gameplay/07 phase 6, data/minigames.json): opened by the dialogue action {"type": "miniGame", "id": ...}.
    // Enter / a tap: start, then tap the points, then close; Esc closes when not playing.
    bool f_MiniGameOpen() const { return m_oMiniGame.m_bOpen; }
    void f_OpenMiniGame(const std::string& l_strId);
    void f_MiniGameTap();
    void f_CloseMiniGame();
private:
    bool m_bSkipStartStory = false;
    struct MiniGameState {
        bool m_bOpen = false;
        std::string m_strId;
        int m_iPhase = 0;              // 0: the text, waiting to start; 1: playing; 2: the result
        toms::ComboPlay m_oPlay;
        int m_iReward = 0;
    };
    MiniGameState m_oMiniGame;
    toms::MiniGameTable m_oMiniGames;   // data/minigames.json
    void f_FinishMiniGame();
    void f_TryWeapon(const std::string& l_strWeaponId);   // dialogue action "tryWeapon": wear it, fight the training slime
    void f_ChooseWeapon();                           // dialogue action "chooseWeapon": keep the worn weapon, begin
    void f_EndWeaponTrialFight();                    // the trial fight is over: back to the master's "after" node
    bool f_EnemyFromData(const std::string& l_strId, const nlohmann::json& l_jOverrides, EnemyInst& l_oOut) const;
    void f_ArenaNextFight();
    bool f_ArenaEndFight(bool l_bWon);   // true: the arena took the fight's end (finishCombatWin / Lose return at once)
    TOMS_OBJECT(Game)
};

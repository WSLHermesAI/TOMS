// game.h — game logic: player, movement, auto combat, talking, stage flow, world drawing.
// The UI (title, HUD, battle, menus, ...) is RmlUi: buildUiState()/uiEvent() below, ui/ui_state.h.
#pragma once
#include <array>
#include <vector>
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
#include "event_bus.h"       // toms::Subscription -- Game::busSubs_
#include "safe_timer.h"      // toms::TimerQueue / TimerOwner -- delayed calls that die with their owner (battle numbers)
#include "tween.h"           // toms::Tween -- the damage numbers' rise
#include "player_menu_rules.h" // toms::LevelPoints -- attribute points by level (stats.json levelUp.points)
#include "res_keys.h"        // toms::key / toms::pattern -- every file by key (assets/media/resources.json)
#include "weapon_play.h"     // toms::WeaponPlayTable -- data/weapon_play.json (docs/gameplay/07_GAMEPLAY_PLAN.md §5.6)
#include "combo_play.h"      // toms::ComboPlay -- Battle v3's point play (docs/gameplay/07 §4, §5.1)
#include "touch_play.h"      // toms::TouchPlay -- each weapon's own play: bow, slice, circles (docs/gameplay/15 §3.3)
#include "battle_party.h"    // toms::nextStanding, BattleEvent -- 1-3 monsters, the battle log (docs/gameplay/13 §4.6)
#include "battle_rules.h"    // toms::LooksTable, MonsterEvent, MovesTable, PaceSettings, BreakSettings (docs/gameplay/13)
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
    int hp, maxhp, atk, def, gold, exp, lv;
    int key_yellow = 0, key_blue = 0, key_red = 0;
    int crystals = 0;   // the second currency (docs/gameplay/07 phase 6): earned by the reward events (mini_game.h)
    int x = 1, y = 1;
    // inventory: list of item ids (a 9-grid, extendable UI). Keys/coins are NOT
    // stored here (they apply immediately); usable items (gems/potions/exp/scroll) are.
    std::vector<std::string> inv;
    // The attributes of data/stats.json (str, dex, agi, ...) by id: base + level-ups + items used.
    // Worn equipment adds on top (Game::attrValue). Nothing in battle reads them yet.
    std::map<std::string, int> attrs;
    int attrPoints = 0;   // attribute points not placed yet (stats.json levelUp.freePoints a level)
    Player() : hp(100), maxhp(100), atk(10), def(5), gold(0), exp(0), lv(1) {}
    TOMS_OBJECT(Player)
};

struct EnemyInst : public Trackable {
    std::string id; std::string name; int hp, atk, def, exp, gold; int x, y; bool boss=false;
    // Battle System v2 (docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md): how often, in ms, this enemy attacks
    // on its own real-time clock -- independent of anything the player does. Read from
    // data/enemies.json's "atk_interval_ms" (engageMonster()); defaults to 4000 when absent.
    int atkIntervalMs = 4000;
    // The battle's rules for it (docs/gameplay/13; enemies.json, a scenario's "battle" can override each):
    std::string mapId;                       // the map monster's id (a transformed monster keeps it: its tile is cleared)
    std::string look = "monster";            // "look": its status map in data/looks.json
    std::vector<toms::MoveChoice> moves;     // "moves": what it may do next (data/moves.json); none: a plain strike
    int breakPoints = 0;                     // "break": matched points that break it (0: data/battle.json's)
    nlohmann::json events = nlohmann::json::array();   // "events": its monster events (13 §4.4)
    TOMS_OBJECT(EnemyInst)
};

struct DialogueNode : public Trackable {
    std::string text; std::vector<std::pair<std::string,std::string>> choices;
    TOMS_OBJECT(DialogueNode)
};

// Milestone 4: a parsed dialogue choice, now carrying its optional `action` alongside the
// existing label/next — see Game::enterNode / Game::chooseDialogue / Game::runDialogueAction.
struct DialogueChoice {
    std::string label, next;
    nlohmann::json action;   // null if the choice has no action (the common case today)
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 6.2): story actions (story_actions.h),
    // whether picking it finishes the dialogue's event, the events it wakes, and its id for `choiceMade`.
    nlohmann::json actions;
    bool finish = false;
    std::vector<std::string> fire;
    std::string id;
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
        float pos = 0.0f;     // marker position, in the bar's own [0, 2*redOuter] units
        int dir = 1;          // +1 or -1
        float legMs = 0.0f;   // ms since the marker's current leg (one edge-to-edge sweep) began
        bool cooling = false; // true while frozen after a tap, waiting to resume auto-moving
        int cooldownMs = 0;   // ms remaining in that cooldown
    };
    static constexpr int kBarCooldownMs = 1500;   // default for data/battle.json "bar_cooldown_ms"

    // The battle's monsters (docs/gameplay/13 §4.6; the owner 2026-10-25: one to three): [0] the lead (the map's
    // monster, whose tile a win clears), then its party. Each has its own HP and its own attack clock.
    struct Foe {
        EnemyInst enemy;
        int hp = 0;
        int clockMs = 0;       // counts up to moveMs, then it does its move and starts again
        // Its next move (docs/gameplay/13 §6.1): shown over it with the time left (its intent).
        std::string move = "strike";
        int moveMs = 4000;
        int staggerMs = 0;            // stunned (a parry, a break, a result's stun): its clock stops...
        float staggerDamage = 1.0f;   // ...and hits on it do this much more
        bool broken = false;          // the stun came from its break gauge (the intent shows "Broken!")
        float breakGauge = 0.0f;      // matched points so far (13 §6.4)
        float atkMul = 1.0f, defMul = 1.0f, intervalMul = 1.0f;   // monster events' stat changes
        int attacks = 0;              // its attacks so far (the "attacks" event)
        bool fled = false;            // left the battle (no reward for it)
        std::vector<toms::MonsterEvent> events;
        bool down() const { return hp <= 0; }
    };
    std::vector<Foe> foes;
    int target = 0;            // the foe the player's attacks go to (tap a monster, or Left / Right)
    int playerHP = 0;
    Foe& lead() { return foes.front(); }
    const Foe& lead() const { return foes.front(); }
    std::vector<int> foeHp() const { std::vector<int> v; for (const Foe& f : foes) v.push_back(f.hp); return v; }
    // What happened, in order (battle_party.h): the battle editor's log, and what monster events will listen to.
    std::vector<toms::BattleEvent> events;
    int clockMs = 0;           // battle time (while it is active)
    // The pace (docs/gameplay/13 §6.3): cooldowns after a tap (as before), or stamina that both actions spend.
    toms::Pace pace = toms::Pace::Cooldown;
    float stamina = 0.0f;
    bool active = false;
    std::string log;          // last exchange text
    bool won = false;

    AutoBar atkBar, defBar;
    // Battle v3 (docs/gameplay/07 phase 1): the attack is the point play. While the attack waits (not cooling, no combo), charge
    // builds up (chargeMs); the first tap starts the combo with it.
    toms::ComboPlay combo;
    // each weapon's own play (touch_play.h; weapon_play.json "play"): armed over the target when the attack is ready,
    // started by its first touch, which takes the charge so far (touchCharge) and the stamina (touchPaid)
    toms::TouchPlay touch;
    float touchCharge = 0.0f;
    bool touchPaid = false;
    int chargeMs = 0;
    bool shieldBanked = false;
    float shieldPower = 0.0f;

    // Super-Attack Gauge: +1 per successful Attack tap, unlocks a guaranteed strong hit
    // (battleTapSuper()) at RunStoryState::superMax() (M8: per-run, halved by rebirth() -- no
    // longer a compile-time constant here), then resets.
    int superCharge = 0;

    // Equipment actives (2026-09-17: rewired onto the richer equipment_actives.h pure-logic
    // layer -- real per-active uses-per-battle + cooldown + st_echo/st_silence status hooks,
    // reconciled from a parallel branch's implementation of the same S7 slice this session's own
    // active_system.h had stubbed more simply). activeRuntime is reset fresh every battle by
    // toms::makeBattleRuntime(activeDefs_); activeStatus is reset too since nothing anywhere sets
    // st_echo/st_silence yet (a real, still-open gap, not this rewire's job to close).
    std::map<std::string, toms::ActiveRuntime> activeRuntime;
    toms::ActiveStatusEffects activeStatus;
    // "Armed" state for the two effect kinds this project's actives use -- guaranteedCrit arms the
    // NEXT attack (consumed by resolveAttackTap()), surviveLethal arms against the NEXT lethal hit
    // (consumed by resolveEnemyClockFire()). Both come from toms::ActiveEffect, not stored there
    // directly, since CombatState -- not equipment_actives.h -- owns how/when an effect actually
    // applies to this game's specific attack/defense-tap combat model.
    bool nextAttackGuaranteedCrit = false;
    float nextAttackCritDamageMult = 1.0f;
    bool survivalArmed = false;

    // resultPauseMs: only needed for a LOSE (finishCombatLose() sets active=false immediately,
    // but the "you fell" message should stay on screen briefly rather than vanish the same
    // frame) -- a WIN uses `won` to stay open until dismissed instead. See draw()'s showBattle.
    int resultPauseMs = 0;
    // Phase 0 (docs/gameplay/07_GAMEPLAY_PLAN.md §4, the owner's rule 2026-10-20): while the player or the enemy is attacking,
    // BOTH attack clocks pause -- the bars do not move or cool down and the enemy's clock does not run -- until the
    // attack is over. Set by an attack (data/battle.json "attack_pause_ms"), counted down by Game::update.
    int attackPauseMs = 0;
    // Damage numbers rising over a fighter: added when the hit lands (a safe timer after the attack starts).
    // A damage number or a word rising over a fighter. who: -1 the player, 0.. a foe. style: battle.rcss's .popup.<style>
    // (hit, great, perfect, miss, guard; the words parry, block, early, chain, move-<tier>); lift: words sit higher.
    struct Popup { std::string text; int who = 0; std::string style = "hit"; int lift = 0; int ageMs = 0; toms::Tween rise; };
    std::vector<Popup> popups;
    // ---- the revamp (docs/gameplay/15_BATTLE_REVAMP.md) ----
    int frameDt = 0;               // the battle's time this frame: 0 in a hit stop, slower in the last hit's slow motion
    int hitStopMs = 0;             // both sides freeze (15 §3.8)
    int slowMs = 0;                // the last hit in slow motion, then the win
    float slowScale = 1.0f, slowCarry = 0.0f;
    bool winPending = false;       // won, the slow motion still running (finishCombatWin follows)
    float shakePx = 0.0f;          // the screen shakes this far, fading over shakeMsMax
    int shakeMs = 0, shakeMsMax = 1;
    int dimMs = 0;                 // a break dims the screen a moment
    std::string banner, bannerKind;   // across the screen: "BREAK!" (break), a monster's line (line), the ultimate (ultimate)
    int bannerMs = 0, bannerMsMax = 1;
    int chain = 0, bestChain = 0;  // perfects in a row (15 §3.4); a miss resets it
    int perfects = 0, attacksDone = 0, damageTaken = 0;   // for the grade
    int guardAtMs = -1;            // the timed guard: when it was pressed (battle time), -1 none
    int guardCdMs = 0;
    bool ultimate = false;         // the running combo is the ultimate
    // The result screen of a win (15 §3.6): filled by finishCombatWin.
    struct Result {
        bool valid = false;
        std::string grade;         // S / A / B / C
        int score = 0;
        float seconds = 0.0f;
        int gold = 0, exp = 0;
        std::string card;          // its line ("New card: Slime"), or ""
        std::string weapon;        // the weapon's key in weapon_play.json
        int lvBefore = 1, lvAfter = 1, uses = 0;
    } result;
    TOMS_OBJECT(CombatState)
};

// A store entry, parsed from data/store.json: which item of data/items.json it sells and how its
// price grows. cost = the item's "price" * growth^purchases (growth 2 doubles it each time, 1 keeps it).
struct StoreItemDef {
    std::string id;            // an id in data/items.json (a consumable, or a weapon/armor/talent)
    int price = 2;             // the item's "price", copied when the store loads
    int growth = 2;
    int purchases = 0;         // how many times already bought (drives the price growth)
    int liveCost() const { int c = price; for (int i = 0; i < purchases; i++) c *= growth; return c; }
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
    bool loadAssets(const std::string& assetDir);
    void loadStage(const std::string& id, StageArrival arrival = StageArrival::Fresh);
    void update(int dtMs);                 // advances combat timer etc.
    void draw();                          // render the world (map, entities, player) -- UI is RmlUi
    // A click/tap that no UI element took (design space 1024x768; phase 0 = down, 2 = up): on the
    // map it walks there (click-to-move) or, on the player's own tile, interacts.
    void handleTouch(float px, float py, int phase);
    // ---- UI (RmlUi, see ui/ui_state.h) ----
    // buildUiState(): what every screen shows this frame. uiEvent(): a button in one of the
    // assets/media/ui/*.rml documents was pressed ("inv_use", "menu_row" with the row, ...).
    void buildUiState(toms::UiState& out) const;
    void uiEvent(const std::string& name, int arg);
    void saveFrame(const std::string& path);
    // input (scripted for headless)
    void movePlayer(int dx, int dy);
    // Milestone 9 polish: "keep moving while held" instead of exactly one tile per key
    // press/tap -- classic dungeon-crawler feel. Call every frame with the currently-held
    // direction on each axis (dir in {-1,0,1}, 0 = not held): GameSession does this for the
    // keyboard (level state, not the edge-triggered up/down/left/rightPressed used for
    // menu/inventory-cursor navigation, which should NOT auto-repeat), handleTouch() does it
    // for the virtual/touch d-pad (0 on press-release). X and Y repeat independently -- both
    // held at once keeps moving diagonally, same as the old one-tap version allowed. The
    // actual repeat timer lives in update(); this only registers what's currently held and
    // fires the immediate first step on a fresh press/direction-change.
    void setMoveHeldX(int dir);
    void setMoveHeldY(int dir);
    void stopMoveHeld() { setMoveHeldX(0); setMoveHeldY(0); }
    // Click-to-move: a tap/click on a map tile walks there (handleTouch). The route is a shortest
    // path over plain floor only -- it never opens a door, fights, picks up, talks or takes stairs
    // on the way -- while the clicked tile itself may be anything: the last step goes into it
    // exactly like an arrow-key step would (a monster = fight, a door = open with a key, ...).
    // One step every kMoveRepeatMs. Any key/d-pad move, a new click, a modal (battle, dialogue,
    // stairs confirm) or a stage change cancels it. False when the tile can't be reached.
    bool walkTo(int tx, int ty);
    void cancelWalk() { walkPath_.clear(); walkTargetX_ = walkTargetY_ = -1; walkTimerMs_ = 0; }
    bool walking() const { return !walkPath_.empty(); }
    // Design-space point -> the map tile drawn there (same camera maths as draw()).
    bool screenToTile(float px, float py, int& tx, int& ty) const;
    void interact();                       // talk to NPC / trigger dialogue on current cell
    void chooseDialogue(int idx);          // pick a dialogue choice
    // Bugfix: the old desktop main never had any keyboard binding that moved dlgSel at all -- with
    // only ever one choice ever shown before this session's content, that went
    // unnoticed; now that real dialogue has 2-5 choices, the player had no way to
    // reach anything but choice 0. Wraps like invMoveSel's cursor.
    void dlgMoveSel(int delta) {
        int n = (int)dlgChoices.size();
        if (n <= 0) return;
        dlgSel = ((dlgSel + delta) % n + n) % n;
    }
    void startDialogue(const std::string& npc);   // open a dialogue file by id (public for tests)
    // Talk to an NPC (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): a chat line, one topic, or the topic menu.
    void talkTo(const std::string& npcId);
    std::string dialogueSpeaker() const { return dlgSpeaker_; }
    void enterNode(const std::string& node);      // jump to a dialogue node (public for tests)
    void startCombat(const EnemyInst& e, const std::vector<EnemyInst>& party = {});   // party: up to 2 more (kMaxFoes)
    // Verification hooks (debugStartNearestBattle .. debugWarpPlayer): the old web entry exposed
    // them to a page test harness. No host calls them now; a future test harness can re-expose them.
    // Start a fight with the nearest monster, to exercise the battle scene without walking the maze.
    bool debugStartNearestBattle();
    // Verification hook: equip an item directly by id,
    // bypassing the Store/Forge entirely, so a page harness can reach equipment (and whatever
    // actives it grants) that isn't purchasable/craftable through any content authored yet.
    // Silently does nothing for an unknown id, matching every other debug hook's fail-soft rule.
    bool debugEquip(const std::string& equipmentId);
    // Test hook (toms_game --give=...): an item reaches the player as if picked up (receiveItem); "gold:<n>" adds gold, "exp:<n>" EXP (with its level-ups), "points:<n>" attribute points to place.
    void debugGive(const std::string& id);
    // Verification hook: runs the exact same
    // finishCombatLose() path a real battle loss would (noteDeath, the e_10/e_13 wipe-check,
    // ending trigger) without needing to actually lose `deathsNonBoss` real fights first.
    void debugForceLose();
    // Verification hook: sets a run flag directly, skipping
    // whatever real trigger (a floor event tile, a dialogue action) would normally set it, so a
    // page harness can reach a flag-gated dialogue choice without re-driving an already-proven
    // upstream mechanic.
    void debugSetFlag(const std::string& flag) { run_.setFlag(flag); }
    // Verification hook: sets the player's tile
    // directly, skipping a full maze walk, so a page harness can stand next to a specific stairs
    // tile and then take ONE real step onto it -- exercising the real transition trigger
    // (movePlayer's 'U'/'D' check) and the real arrival logic (loadStage's StageArrival) without
    // having to path through an entire generated maze (and its monsters) first.
    void debugWarpPlayer(int x, int y) { pl.x = x; pl.y = y; }
    // Battle System v2 (see CombatState's comment): each is a single instantaneous tap, safe to
    // call any time -- a no-op when combat isn't active or that specific bar is still cooling
    // down / the Super gauge isn't full, so callers (GameSession, handleTouch)
    // don't need to track any battle state themselves, just forward the tap.
    void battleTapAttack();
    void battleTapDefense();
    void battleTapSuper();
    // The pointer over the battle (design pixels; 0 down, 1 held, 2 up), every frame: the weapon's own play
    // (touch_play.h) reads it. The session leaves out a press that lands on a button (battle.rml .btn).
    void battlePointer(int phase, float x, float y);
    // S7 (equipment actives, first slice): the 4th battle action -- a no-op unless the currently
    // equipped gear grants one (equippedActives()) and it hasn't been spent this battle already.
    void battleTapActive();
    // Several monsters (docs/gameplay/13 §4.6): the target is the next (+1) / previous (-1) monster standing, or that one.
    void battleCycleTarget(int dir);
    void battleSetTarget(int foe);
    const std::vector<toms::BattleEvent>& battleEvents() const { return cs.events; }
    // The pace (docs/gameplay/13 §6.3): "cooldown" / "stamina" for the battles from now on (and the one running); ""
    // goes back to the weapon's or data/battle.json's. The battle editor's switch; a scenario's "battle": {"pace"}.
    void setBattlePace(const std::string& pace);
    std::string battlePace() const { return toms::paceName(cs.pace); }
    // The battle editor: end the battle as won (every monster falls) / fire a monster's event now (its index in
    // enemies.json "events"). Both no-ops without a battle.
    void debugForceWin();
    // Settings > "Reduce shaking and flashes" (docs/gameplay/15 §3.8): hit stops, shakes and flashes a quarter as
    // strong, no slow motion. Saved in the settings file.
    bool reduceEffects() const { return settings_.reduceEffects; }
    void setReduceEffects(bool on);
    void debugFireFoeEvent(int foe, int event);
    int battleFoeCount() const { return cs.active ? (int)cs.foes.size() : 0; }
    // The battle editor: a monster's enemies.json record, edited while it fights. Every foe of that id in the running
    // battle takes the new numbers at once: HP (its current HP keeps its share), ATK, DEF, the attack interval (the
    // wind-up under way keeps how far along it is), moves, look, break points and events. No battle: nothing.
    void debugUpdateMonster(const std::string& id, const nlohmann::json& record);
    int battleFoeMoveMs(int foe) const {   // the wait before its next move (the editor's check)
        return cs.active && foe >= 0 && foe < (int)cs.foes.size() ? cs.foes[(size_t)foe].moveMs : 0;
    }
    std::vector<std::string> battleFoeIds() const {   // the log's names (battleEventLine)
        std::vector<std::string> v;
        for (const CombatState::Foe& f : cs.foes) v.push_back(f.enemy.id);
        return v;
    }
    void setBattleLogToStderr(bool on) { battleLogToStderr_ = on; }   // --battle-log: every battle event as it happens
    // ---- the player menu (docs/gameplay/03_PLAYER_MENU.md): the walking scene's one button (≡, Esc) ----
    // Five tabs: Status, Equipment, Items, Events, System. Everything that was a button of its own
    // (the backpack, the store button, the old ≡ list, the web page's fullscreen button) is in here.
    enum class MenuTab { Status, Gear, Items, Events, Cards, System };   // player.rml shows them by number
    static constexpr int kMenuTabs = 6;
    bool playerMenuOpen() const { return inGameMenuOpen_; }
    MenuTab playerMenuTab() const { return menuTab_; }
    void openPlayerMenu(int tab = -1);     // -1: the last tab used
    void closePlayerMenu();
    void playerMenuTab(int delta);         // Q / E / Tab: the previous / next tab
    void playerMenuMove(int dx, int dy);   // arrows: move the selection on the current tab
    void playerMenuActivate();             // Enter: the selected entry's first action
    void playerMenuBack();                 // Esc: confirm -> sub-page -> close
    // I: the Items tab (closes the menu when it is already on Items). The web harness calls it too.
    void toggleInventory();
    bool inventoryOpen() const { return inGameMenuOpen_ && menuTab_ == MenuTab::Items; }
    // Save / Load: its own screen over the menu (System tab -> Save / Load). Every save and every load
    // asks first. While it is open the player menu's keys drive it (playerMenuMove/Activate/Back/Tab).
    bool saveLoadOpen() const { return saveLoadOpen_; }
    const std::vector<std::string>& inventory() const { return pl.inv; }
    // The player's attribute `id` (data/stats.json) with worn equipment included.
    int attrValue(const std::string& id) const;
    const std::vector<std::string>& gearOwned() const { return gearOwned_; }
    // Fullscreen lives on the host (an SDL window, the browser page): the host reports whether it
    // can and whether it is on, and takes the player's request from the System tab's row.
    void setFullscreenState(bool available, bool on) { fullscreenAvailable_ = available; fullscreenOn_ = on; }
    bool takeFullscreenRequest() { const bool r = fullscreenRequest_; fullscreenRequest_ = false; return r; }
    // Milestone 7 bugfix: cs.won (the post-victory "press any key to continue" pause) must count
    // as a modal overlay too, not just cs.active -- otherwise the world underneath (movement,
    // NPC interact, the store icon, Tab/B shortcuts) keeps responding to input while the victory
    // screen is still up. See docs/project/progress_report/PROGRESS_REPORT.md's Milestone 7 log for the report this fixes.
    bool modalActive() const { return cs.active || cs.won || miniGame_.open || inDialogue || storeOpen || storeUnlockDlg || stageSelectOpen_ || stairsConfirmOpen_ || title_.isOpen() || inGameMenuOpen_ || endingActive() || !notices_.empty(); }  // any overlay open (combat/dialogue/inventory/store/stage-select/stairs-confirm/title/in-game menu/ending)
    bool combatWon() const { return cs.won; }
    bool combatActive() const { return cs.active; }
    // Dismisses the post-victory pause (mirrors handleTouch's existing tap-to-dismiss) -- the
    // keyboard path (Enter/Space) had no equivalent before this fix, so "press any key to
    // continue" never actually worked from a keyboard.
    void dismissVictory() { cs.won = false; cs.log.clear(); }
    bool inDialogueFlag() const { return inDialogue; }
    int dialogueSel() const { return dlgSel; }
    int dialogueChoiceCount() const { return (int)dlgChoices.size(); }
    std::string dialogueNode() const { return dlgNode; }
    bool storeModal() const { return storeOpen || storeUnlockDlg; }  // store overlay (incl. unlock dialog) is the topmost modal
    // store system
    bool storeUnlocked() const { return storeUnlocked_; }
    bool storeOpenFlag() const { return storeOpen; }
    const std::vector<StoreItemDef>& storeItems() const { return storeItems_; }
    int storeSel() const { return storeSel_; }
    const std::string& toastMsg() const { return toastMsg_; }
    void storeKey(int key);                // keyboard nav/confirm inside the store (and its unlock popup)
    void openStore();                      // open the store overlay
    void closeStore();                     // close the store overlay
    // Milestone 5: Stage Select hub -- every floor the player has ever reached becomes a
    // replayable, individually-selectable entry (architecture-doc §10). Purely additive: does
    // not change the existing boot flow (still auto-loads stage01), only adds an in-game hub.
    bool stageSelectOpen() const { return stageSelectOpen_; }
    void openStageSelect();
    void closeStageSelect();
    // Milestone 9: a maze's dead ends force backtracking through the same corridor
    // (Wilson's algorithm produces a tree -- there's no alternate route around
    // anything), and a stairs tile sitting on that backtrack path could get walked
    // over by accident (owner report). Reaching 'U'/'D' now opens a Yes/No confirm
    // instead of transitioning immediately -- see movePlayer()'s call to
    // requestStageTransition() and confirmStageTransition()/cancelStageTransition().
    bool stairsConfirmOpen() const { return stairsConfirmOpen_; }
    // Q7: an OK popup ("Stairs unlocked: the stairs on floor 3 ..."), queued; Enter / Esc / its button close it.
    bool noticeOpen() const { return !notices_.empty(); }
    void dismissNotice();
    // Test hooks (toms_game --stair-lock=F01:ev_x, --events-done=ev_a,ev_b): lock a floor's first stair up behind
    // an event; finish events as if the player had (the unlock dialog included).
    void debugLockStairs(const std::string& floor, const std::string& eventId);
    void debugFinishEvent(const std::string& eventId);
    bool stairsConfirmIsUp() const { return stairsConfirmIsUp_; }
    void confirmStageTransition();
    void cancelStageTransition();
    // Maze camera mode (see toms::Camera::Mode in camera.h). Applying a change re-targets the
    // camera immediately (no confirm dialog needed -- unlike language, this is a low-stakes,
    // instantly-visible, freely-reversible preference).
    int cameraModeIndex() const { return cam_.modeIndex(); }
    void setCameraModeIndex(int m);
    // The player menu's System tab (the old in-game menu): Store, Skills, Forge, Village, Save,
    // Settings, Fullscreen, Back to title. inGameMenuOpen() is true while the player menu is open.
    bool inGameMenuOpen() const { return inGameMenuOpen_; }
    void openInGameMenu();             // the player menu on its System tab
    void inGameMenuMove(int delta);    // Up/Down or Left/Right
    void inGameMenuActivate();         // Enter/Space
    void inGameMenuBack();             // Esc: dialog->list, Settings->Main, Main->closed
    Player& player() { return pl; }
    IRenderer* renderer() { return ren; }   // for batch-metric inspection (demo)
    // The resource cache (made in loadAssets). Hot reload: files that change on disk (art, clips, effects) are read
    // again about once a second -- the editors and debug builds turn it on; a shipped game never looks.
    toms::res::ResourceManager* resources() { return res_.get(); }
    void setHotReload(bool on) { hotReload_ = on; }
    void showNotice(const std::string& msg) { pushNotification(msg); }   // a few seconds at the top (live UI reload)
    // The UI editor's language switch: the text in another language, without touching settings.json.
    void previewLanguage(const std::string& code) { locale_.setLanguage(code); title_.setLanguageIndex(locale_.languageIndex()); }

    // ---- Title phase (Boot screen: New Game / Continue / Settings) ----
    // The title phase is the game's boot screen: Game::loadAssets() opens it, and nothing in
    // the world responds to input until the player picks something (modalActive() includes it).
    // New Game resets progress and loads stage01; Continue lists the save slots on disk and
    // resumes one; Settings currently offers the language. See src/game/title_screen.h for the
    // page/selection state machine and docs/design/TITLE_PHASE.md for the flow.
    bool titleOpen() const { return title_.isOpen(); }
    const toms::TitleScreen& title() const { return title_; }
    // Keyboard input, in the same shape the other overlays use: a direction move, a confirm and
    // a cancel. Each routes to the current title page (mouse/touch: uiEvent("title_row", ...)).
    void titleMove(int dx, int dy);
    void titleConfirm();
    void titleCancel();
    // Starts a brand new run: default player stats, cleared meta/story/entity progress, and a
    // fresh slot (or the lowest-numbered slot when all are taken). Writes the slot immediately.
    // The overload starts the run in a specific slot -- what the Continue page's "start a new
    // game in this (empty) slot?" dialog needs.
    void newGame();
    void newGame(int slot);                // slot -1: a test scenario's run -- no slot, never written
    // ---- Test scenarios (docs/gameplay/08_TEST_SCENARIOS.md, game_scenario.cpp) ----
    // Starts the game straight in one scene from a scenario file (or a folder holding scenario.json): an optional save,
    // the floor, the player, finished events and flags, then the scene itself (a battle, a dialogue, the store ...).
    // False (with the reason) when the file cannot be read or names something that does not exist.
    bool startScenario(const std::string& path, std::string& error);
    // The same from a setting already read (toms_game --arena builds one); file names in it must be absolute.
    bool startScenarioJson(const nlohmann::json& scenario, const std::string& name, std::string& error);
    bool arenaOn() const { return arena_.on; }
    // After the run: is the scene the scenario expects showing, and do its "expect" values hold? report says what failed.
    bool scenarioCheck(std::string& report) const;
    // The scenario's "test" block: frames to run and scripted keys (toms_game --scenario-test), or 0 / "".
    int scenarioTestFrames() const { return scenarioTestFrames_; }
    const std::string& scenarioTestKeys() const { return scenarioTestKeys_; }
    const std::string& scenarioTestClicks() const { return scenarioTestClicks_; }
    // Resumes slot N (1-based). Returns false (and leaves the title open) when that slot has no
    // save, so the caller can show "no save here" instead of dropping the player into nothing.
    bool continueFromSlot(int slot);
    // Re-reads the slot files into the title's Continue list.
    void refreshSlots();
    // Writes the current run to activeSlot() atomically (no-op when no slot is active).
    void saveCurrentRun();
    // Applies settings_.language to the string table and persists settings.json.
    void applyLanguage();
    // Remembers art style `idx` (into artStyles()) in settings.json. On the title screen it is
    // applied at once (refreshArtStyle); chosen during a run, it applies on the way back to the title.
    void applyArtStyle(int idx);
    // Loads the chosen style's sprites if they are not the ones in use -- only while the title
    // screen is up (returnToTitle and the title's Settings page call it).
    void refreshArtStyle();
    const std::vector<toms::ArtStyle>& artStyles() const { return artStyles_; }
    int loadedArtStyle() const { return loadedArtStyle_; }                  // in use since startup
    // The prebuilt sprite atlas in use (tools/atlas): look a sprite up by name for its child rect,
    // pivot, 9-slice or tags. Empty when the game fell back to the runtime grid.
    const toms::AtlasFile& spriteAtlas() const { return spriteAtlas_; }
    // Developer/test hook (toms_game --anim=<file>#<clip>): plays one clip of an .anim file over
    // whatever is on screen, centred, looping; its events are logged. Draws with the real
    // AnimPlayer through the sprite batch, so a screenshot shows exactly what the game would draw.
    bool playPreviewAnim(const std::string& file, const std::string& clip, std::string& error);
    // The character a character clip (docs/engine/05_ANIMATION.md "Characters": `body` / `hand` nodes) is shown with: a sprite
    // of the game atlas ("player_mage"; toms_game --character=). "" = the clip as made. Set before playPreviewAnim.
    void setPreviewCharacter(const std::string& sprite) { previewCharacter_ = sprite; }
    // A held thing's clips (an .anim file, e.g. media/anim/weapon_bow.anim; toms_game --held=): its clip of the preview
    // clip's name is drawn at the character clip's `hand` (CharacterRig). "" = none. Set before playPreviewAnim.
    void setPreviewHeld(const std::string& file) { previewHeldFile_ = file; }
    // A benchmark (toms_game --anim-crowd=N): the preview clip N times in a grid, each its own AnimPlayer started a little
    // later than the one before, drawn the way the game draws clips (AnimPlayer::draw). Set before playPreviewAnim.
    void setPreviewCrowd(int n) { previewCrowdN_ = n; }
    // The CPU time of the last frame's preview drawing (the crowd or the one clip), milliseconds.
    double previewDrawMs() const { return previewDrawMs_; }
    // The battle picture of a weapon with a "battleAnim" (equipment_system.h; docs/engine/05_ANIMATION.md "Characters"): the
    // player's character clip with the weapon's clip at its hand, both on the clip the battle's status picks --
    //   charge   the attack bar waits for the tap (held, shaking, for as long as it waits)
    //   attack   a tap that hit; miss: a tap that dealt nothing (played to its end)
    //   idle     the bar recovers for the next round (and after the fight)
    // It replaces the battle screen's player picture. battleRigState(): the status shown ("" = none: no such weapon).
    const std::string& battleRigState() const { return battleRigState_; }
    bool battleRigReady() const;   // the equipped weapon has battle clips and they loaded
    // A character clip played by `sprite`: fitted to its visible pixels (fitCharacter), plus its nudge from
    // assets/data/character_fit.json ({"player_mage": {"hand": [x, y]}}, pixels; none = no nudge).
    toms::anim::Clip fitClipToCharacter(const toms::anim::Clip& clip, const std::string& sprite) const;
    // The same for a particle effect (toms_game --fx=<file>#<effect>): centred, a one-shot effect
    // starts again 0.5 s after it ends. The seed is the effect's, else 1, so screenshots repeat.
    bool playPreviewFx(const std::string& file, const std::string& effect, std::string& error);
    // The renderer's triangle meshes (IRenderer::drawMesh, docs/engine/16_MESH_ANIMATION.md; toms_game --mesh-test=<sprite>):
    // that sprite of the game atlas as an 8 x 8 grid mesh waving in time, the plain sprite beside it.
    bool playPreviewMesh(const std::string& sprite, std::string& error);
    int previewFxLive() const { return previewFx_.liveCount(); }   // the --fx effect's live particles (HUD)
    int previewFxGpuEmitters() const { return previewFx_.gpuEmitters(); }
    int previewFxEmitters() const { return previewFx_.effect() ? (int)previewFx_.effect()->emitters.size() : 0; }
    // Particle emitters above this many particles run on the GPU (GameSettings::particleGpuThreshold;
    // `toms_game --fx-gpu-threshold=N` overrides it for this run without saving it).
    int particleGpuThreshold() const { return fxGpuThresholdOverride_ >= 0 ? fxGpuThresholdOverride_ : settings_.particleGpuThreshold; }
    void overrideParticleGpuThreshold(int n) { fxGpuThresholdOverride_ = n; }
    // Tests (toms_game --battle-tuning=default): the battle bars' built-in values instead of data/battle.json, so a
    // screenshot of a fight does not change whenever the tuning does.
    void useDefaultBattleTuning() { barSlowScale_ = 1.0f; barFastScale_ = 1.0f; barCooldownMs_ = CombatState::kBarCooldownMs;
                                    attackPauseMs_ = 450; hitNumberDelayMs_ = 180; }
    const toms::WeaponPlayTable& weaponPlay() const { return weaponPlay_; }
    // Where the battle screen's attack track shows its points, in design pixels (GameUi reads its layout each frame), so
    // the track's hint particles land on them.
    void setComboPointsOnScreen(std::vector<glm::vec2> pts) { comboPointsOnScreen_ = std::move(pts); }
    // Where the battle screen laid out the player's and the monsters' boxes (battle.rcss #player / .foe .fbox: x, y, w, h in
    // design pixels; GameUi reads them each frame): the fighters, their particles and damage numbers are placed in them.
    void setBattleBoxes(const glm::vec4& player, const std::vector<glm::vec4>& foes) {
        playerBox_ = player;
        for (size_t i = 0; i < foes.size() && i < (size_t)toms::kMaxFoes; i++) foeBox_[i] = foes[i];
    }
    // The design canvas changed (the screen turned, canvas.h): the camera fits the new one at once, without easing.
    void canvasChanged();
    // Files by key, never by path (assets/media/resources.json; the keys: res_keys.h, made by tools/make_res_keys.py).
    // pathOf: a key's path, for what reads the file itself (a loader, RmlUi, the sound folder). pathFor: one file of a
    // family ("stage", "stage01"). Both are paths under assets/media's folder, as the resource manager reads them.
    std::string pathOf(const char* key) const { return res_ ? res_->resolve(key) : std::string(); }
    std::string pathFor(const char* pattern, const std::string& id) const { return res_ ? res_->pathFor(pattern, id) : std::string(); }
    // The UI's sprite sheet: RCSS made from spriteAtlas_, which GameUi serves to RmlUi as
    // "_atlas.rcss" -- so the UI draws from the same packed texture as the map. Empty without a
    // prebuilt atlas. The revision changes whenever another atlas is loaded (art style switch).
    std::string uiSpritesheet() const;
    int spriteAtlasRevision() const { return spriteAtlasRevision_; }
    // Sprites the UI or the data (items, store) can ask for that the atlas lacks; empty = fine.
    std::vector<std::string> missingUiSprites() const;
    int chosenArtStyle() const { return toms::artStyleIndex(artStyles_, settings_.artStyle); }
    // The player's sprite: the setting's look when the atlas has it, else "player".
    std::string playerSprite() const {
        return !settings_.playerLook.empty() && idToLayer.count(settings_.playerLook) ? settings_.playerLook : std::string("player");
    }
    int activeSlot() const { return activeSlot_; }
    int playTimeSec() const { return playTimeSec_; }
    const toms::Locale& locale() const { return locale_; }
    const toms::GameSettings& settings() const { return settings_; }
    toms::GameSettings& mutableSettings() { return settings_; }
    // Derived, read-only classification of "what screen/mode is the game in right now",
    // computed from the existing modal flags — see docs/architecture/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md
    // §4 and docs/architecture/IMPLEMENTATION_ROADMAP.md Milestone 1. Does not change control flow; the
    // states with no real screen behind them yet (StageSelect, Paused, ...) are simply never
    // returned today.
    toms::GameState currentState() const;
    // ---- ImGui dev windows (M2: ImGui is wired into every backend, so these build on web too) ----
    // Milestone 2 dev-only debug overlay (Dear ImGui: stat sliders, node-filter toggle, last
    // combat log line). The caller decides when to show it (GameSession, F1); this just builds
    // the ImGui:: window content for the current frame.
    void drawDebugOverlay();
    // UI settings: applies uiFontScale_ to ImGui's global font scale. Call once per frame,
    // right after ImGui's NewFrame(), so it's in effect before anything else draws that frame.
    void applyUiSettings();
    // Milestone 2 styling spike (a prerequisite the roadmap flags before Milestone 5 commits real
    // screens to the hybrid UI approach): proves a transparent, undecorated ImGui window laid
    // exactly over a scene-graph-drawn backdrop rect reads as one panel, not two overlapping
    // things — see docs/project/progress_report/PROGRESS_REPORT.md's M2 log entry for why this needed checking before
    // any real screen was built on the assumption. Dev-only, F2 to toggle.
    void drawStylingSpike();
    void setStylingSpikeVisible(bool v) { stylingSpikeVisible_ = v; }
    // DEBUG: hide individual overlay subsystems to bisect stray-sprite bugs.
    // bit 1 = combat overlay, bit 2 = dialogue overlay, bit 4 = inventory UI.
    int hideMask = 0;
    CombatState& combat() { return cs; }
    Stage& stage() { return st; }
    const nlohmann::json& enemyTemplate(const std::string& id) const { return enemyTpl.at(id); }
private:
    Quad spriteQuad(float x, float y, float w, float h, int layer, const float tint[4]);
    void spriteUV(int layer, float uv[4]) const;
    int spriteLayer(const std::string& id) const; // index into sprite grid
    void applyItem(const std::string& id);
    // Battle System v2: resolveAttackTap/resolveDefenseTap fire the instant a bar is tapped,
    // reading its current (just-frozen) position -- resolveAttackTap applies damage to the enemy
    // directly (and calls finishCombatWin on a kill); resolveDefenseTap only banks a shield, it
    // doesn't apply damage itself. resolveEnemyClockFire runs whenever the enemy's own real-time
    // clock completes: it spends (and clears) whatever shield is currently banked, or applies full
    // damage if none is, and calls finishCombatLose on a kill. The UI shows either bar at its live
    // auto-moving position (frozen while cooling down after a tap).
    void resolveAttackTap();
    // Battle v3: the combo ended -- its damage, then the cooldown. played: a touch play's result (else the track's)
    void resolveCombo(const toms::ComboResult* played = nullptr);
    toms::PlayKind battlePlayKind() const;          // the worn weapon's play; a scenario's "play" (scenarios: the track)
    bool attackRunning() const { return cs.combo.running() || (cs.touch.running() && cs.touch.started()); }
    glm::vec4 touchArea() const;                    // where a touch play's targets go: around the target monster
    void armTouchPlay(bool ultimate, int points);   // a new touch play over the target (waits for its first touch)
    void stepTouchPlay(int dtMs);                   // its time, while it runs
    void touchPlayed(int indexBefore, int hitsBefore);   // after its pointer or time: a sound per arrow, resolve when done
    bool touchHeld_ = false;                        // the press that is held began on the play (not on a button)
    // Weapon levels (docs/gameplay/07 phase 1): the equipped weapon's key in weapon_play.json ("unarmed" for none), its level
    // from the wins with it, and its play settings.
    std::string weaponKey() const;
    int weaponLevel(const std::string& key) const;
    const toms::WeaponPlay& weaponPlayOf(const std::string& key) const;
    float comboCharge() const;                      // 0 .. 1, the charge built while the attack waits
    void resolveDefenseTap();
    void resolveEnemyClockFire(int foe);
    // ---- the battle's rules (docs/gameplay/13; battle_rules.h) ----
    void chooseFoeMove(int foe);                     // its next move (its intent) and the wait before it
    void stepFoes(int dtMs);                         // Game::update: the monsters' clocks, stuns and timed events
    // The player's hit of `dmg` on a foe: its HP, then its HP events (hpBelow, falls). The caller then checks the fall.
    void damageFoe(int foe, int dmg);
    void runFoeEvents(int foe, const toms::EventCheck& check);
    void runFoeActions(int foe, const nlohmann::json& actions);
    void stunFoe(int foe, int ms, float damage, bool broken);
    void foeLook(int foe, const std::string& state);  // its look's sound and particle for a status, and its clip
    void playerLook(const std::string& state);         // the player's ("player" in looks.json)
    void interruptCombo(int foe);                      // quick jabs cut a running combo short
    std::string foeLogName(int foe) const;
    toms::Pace effectivePace() const;   // the override, else the weapon's, else data/battle.json's
    int foeBreakPoints(int foe) const;
    float playerHpPct() const;
    // A foe fell (its HP reached 0): its die clip, the log; the target moves on; the last one wins the battle.
    void foeDown(int foe);
    std::vector<EnemyInst> partyOf(const EnemyInst& lead);
    bool afterPlayerHit(const std::string& hitLog, const std::string& winLog);   // true: the battle was won
    // ---- the revamp (docs/gameplay/15) ----
    int battleFrame(int dtMs);                       // the battle's time this frame (hit stop, slow motion) and its effects
    void feelMoment(const std::string& moment);      // a moment's hit stop, shake and sound (data/battle.json "feel")
    void popWord(const std::string& text, int who, const std::string& style);   // a word over a fighter, now
    void touchPlayUi(toms::UiBattle& b) const;      // the weapon's own play, drawn (GameUi: battle.rml .tp)
    void showBanner(const std::string& text, const std::string& kind, int ms);
    void startWinMoment();                           // the last one fell: slow motion, then finishCombatWin
    void battleEvent(const std::string& type, int who, int whom, int value = 0, const std::string& note = std::string());
    bool battleLogToStderr_ = false;
    // Builds the EnemyInst for a monster tile and starts the fight (or its dialogue gate); shared
    // by the bump-to-fight path in movePlayer() and the harness hook debugStartNearestBattle().
    void engageMonster(const Entity& e);
    // ---- S1: 佔格 / roamers --------------------------------------------------------------------
    // True when the entity's footprint covers tile (x,y) -- the anchor is its top-left tile, so a
    // 2x2 boss "is" on all four of its tiles for blocking (F4/F6), bump-to-fight (F6) and pickup
    // purposes. One predicate, used by movement, interaction and the roamer grid query alike.
    bool entityCovers(const Entity& e, int x, int y) const {
        return toms::footprintCovers(e.x, e.y, e.fp, x, y);
    }
    // F5 y-sort key (bottom-most occupied row) for an entity -- and the same thing for a loose
    // position/footprint pair, so the player (always 1x1) sorts in the same pass as the entities.
    int footprintSortKey_T(const Entity& e) const { return toms::footprintSortKey(e.y, e.fp); }
    int footprintSortKey_T(int /*x*/, int y, const toms::Footprint& fp) const {
        return toms::footprintSortKey(y, fp);
    }
    // Advance every roamer one turn (the game is turn-based: one step per player step). Called by
    // movePlayer() -- a bump into a monster consumes the turn too, so a roamer can corner you.
    void advanceRoamers();
    // entity index -> brain, rebuilt by loadStage(). Kept as indices (not Entity*) because a stage
    // reload replaces the whole entity vector (stairs, Stage Select) and stale pointers would
    // silently drive the wrong monster.
    std::vector<std::pair<int, toms::Roamer>> roamers_;
    // data/footprints.json -- character-level 佔格 (doc F8). Keyed by the legend kind, i.e.
    // "monster:golem", so a floor file never has to repeat the same tier for every copy of a type.
    std::map<std::string, toms::FootprintSpec> typeFootprints_;
    // S3: the run's story state (choices, counters, side-story states, run flags, shards, floor
    // progress, deaths). GameConditionContext reads it, the run save persists it, and rebirth resets
    // it (docs/story/STORY_DATA_SCHEMA.md sections 8/9).
    toms::RunStoryState run_;
    // S3.5: the 70-floor tower as ordered data (data/story/floors/*.json). Empty when that data is
    // absent, and every use is guarded -- so a checkout without the floors still plays the eleven
    // hand-authored stages exactly as before.
    toms::FloorTable floors_;
    // S3.5 (c): what the footer says while the run is on a floor -- the floor's own intro line, which
    // then rotates through its ambient lines as the player moves (storyLineIndex, floor_table.h).
    std::string storyIntroKey_;
    std::vector<std::string> storyAmbientKeys_;
    int storyTurns_ = 0;
    // S3.5 (c): the act card shown for a couple of seconds when a floor with indexInAct == 1 loads.
    float chapterCardMs_ = 0.0f;
    std::string chapterCardTitle_;
    // S3.5 (e): the act whose palette tints this floor's tiles (floorThemeTint, floor_table.h).
    int themeActIndex_ = 1;
    std::string curFloorId_;              // "F07" while the run is on a generated/boss floor, else ""
public:
    // S3: read-only access for tests (run_state_test.cpp) and debug tools -- the run state itself
    // stays owned by Game.
    const toms::RunStoryState& runState() const { return run_; }
    // S4: the skill tree. skillDefs() is read-only content (the UI enumerates it to draw the tree);
    // tryUnlockSkill() is the one path that can ever spend a point -- it re-checks
    // canUnlockSkill() itself rather than trusting the caller, so a UI bug (a stale/duplicate
    // click) can never double-spend or bypass a prerequisite.
    const std::map<std::string, toms::SkillDefinition>& skillDefs() const { return skillDefs_; }
    bool tryUnlockSkill(const std::string& skillId);
    // The skill-tree menu's row order: grouped by lineage in story order (yinqi/yuqi/yuqi's
    // ch_01/02/03 sequence), root before its own deeper tiers within a lineage. A pure function of
    // skillDefs_ (which never changes after loadAssets), so the in-game menu's draw code and its
    // input handlers can both call it and never disagree about which id is at which row.
    std::vector<std::string> skillMenuOrder() const;
    // The atk/def combat actually uses once equipment AND skills both stack (see game_combat.cpp's
    // three call sites) -- exposed read-only so the UI (or a verification probe) can show/check the
    // real number instead of just the base Player stat, which never itself changes for either layer.
    // M8: after at least one rebirth (cycleIndex > 1), a tier>=1 skill's own bonus applies at half
    // effect (STORY_BIBLE.md §8) -- see applySkillEffects's own comment for the root-node carve-out.
    // INT (data/stats.json "skillPower") adds to what skills give, on top of the rebirth halving.
    float skillEffectScale() const { return (meta_.cycleIndex > 1 ? cyclesConfig_.skillEffectScale : 1.0f) * (1.0f + attrEffect("skillPower") / 100.0f); }
    // M8 verification hook: read-only, matching runState()'s own "expose the meta save's public
    // facts for tests/harness probes" rule.
    int metaCycleIndex() const { return meta_.cycleIndex; }
    // Base + equipment + skills + attributes (data/stats.json effects "atk" / "def", rounded down).
    int effectiveAtk() const { int a=pl.atk,d=pl.def; toms::applyEquipmentStats(equipped_,equipmentDefs_,a,d); toms::applySkillEffects(skillDefs_, run_.skillsOwned(), a, d, skillEffectScale()); return a + (int)std::floor(attrEffect("atk")); }
    int effectiveDef() const { int a=pl.atk,d=pl.def; toms::applyEquipmentStats(equipped_,equipmentDefs_,a,d); toms::applySkillEffects(skillDefs_, run_.skillsOwned(), a, d, skillEffectScale()); return d + (int)std::floor(attrEffect("def")); }
    // P3 (docs/gameplay/03_PLAYER_MENU.md section 12): what the attributes do. attrEffect(stat) sums, over every attribute,
    // (its value - its base) * per for the effects naming `stat`; so a Lv 1 character with no bonuses gets 0.
    float attrEffect(const std::string& stat) const;
    int effectiveMaxHp() const { return std::max(1, pl.maxhp + (int)std::floor(attrEffect("maxhp"))); }   // VIT
    toms::PowerBarParams attackBarParams() const;   // the worn weapon's bar, its hit zone widened by DEX
    int barCooldownMs() const;                      // battle.json's wait after a tap, shortened by AGI
    void spendAttrPoint(int index);                 // + on the Status tab: one point into the draft (attrDraft_)
    void unspendAttrPoint(int index);               // -: one point back out of the draft
    void confirmAttrPoints();                       // Confirm: the draft becomes the attributes, and the run is saved
    void resetAttrDraft();                          // Reset: the draft is dropped
    // S5: forging. forgeDefs() is read-only content; tryCraft() re-checks canCraft() itself (same
    // "never trust the caller" rule as tryUnlockSkill) -- it deducts gold/materials and equips the
    // result in one atomic step, so a UI bug can never spend materials without getting the item.
    const std::map<std::string, toms::ForgeRecipeDefinition>& forgeDefs() const { return forgeDefs_; }
    bool tryCraft(const std::string& recipeId);
    // The forge menu's row order: plain id order (no lineage grouping concept for recipes) -- a
    // pure function of forgeDefs_, same reasoning as skillMenuOrder() above.
    std::vector<std::string> forgeMenuOrder() const;
    // Meta-scope: which recipes the player has actually learned (survives rebirth). Read-only --
    // the only writer is Game::applyChapterGrants().
    const std::vector<std::string>& forgeRecipesKnown() const { return meta_.forgeRecipesKnown; }
    // S6: the village hub. hubDefs() is read-only content; activateHubLocation() re-checks
    // hubLocationUnlocked() itself (same "never trust the caller" rule as tryUnlockSkill/tryCraft)
    // before dispatching the location's action (today, only "talk" -> startDialogue() exists).
    const std::map<std::string, toms::HubLocationDefinition>& hubDefs() const { return hubDefs_; }
    bool activateHubLocation(const std::string& locationId);
    // The hub menu's row order: plain id order (no lineage grouping concept, same reasoning as
    // forgeMenuOrder() above) -- a pure function of hubDefs_.
    std::vector<std::string> hubMenuOrder() const;
    // S7 (equipment actives, first slice): read-only content. battleTapActive() is the only path
    // that can spend one -- see its own declaration above.
    const std::map<std::string, toms::ActiveDefinition>& activeDefs() const { return activeDefs_; }
    // Which active id(s) the currently equipped gear grants (equipment_system.h's
    // equippedActives(), applied to the live equipped_/equipmentDefs_) -- exposed read-only so a
    // page harness (jsActiveInfo) can ask "what's granted right now" without a private-member
    // accessor for equipped_/equipmentDefs_ themselves.
    std::vector<std::string> grantedActiveIds() const { return toms::equippedActives(equipped_, equipmentDefs_); }
    // M7 (first slice): the 15-ending table is read-only content; the only writer of
    // activeEndingId_ is Game::triggerEnding(), called from finishCombatLose() today (the only
    // trigger point this slice wires -- see its own comment for why F70/F69's rows aren't reachable
    // yet). An active ending takes over the whole screen (see modalActive()).
    const toms::EndingsTable& endingsTable() const { return endingsTable_; }
    bool endingActive() const { return !activeEndingId_.empty(); }
    const std::string& activeEndingId() const { return activeEndingId_; }
    // M8: whether the CURRENTLY showing ending offers 輪迴 at all (STORY_BIBLE.md §8.1: every
    // ending except e_10 does) AND the cycle cap hasn't already been reached -- the one place both
    // checks live, so the ending screen's draw code and its input handlers can't disagree about
    // whether a second button even exists. False (never true) while no ending is showing.
    bool rebirthOffered() const;
    // The ending screen's two exits. dismissEndingScreen() always returns to the title (the
    // "start over" choice, or the only choice when rebirthOffered() is false). rebirth() re-checks
    // rebirthOffered() itself (never trusts the caller, same rule as tryUnlockSkill/tryCraft) and
    // is a no-op otherwise; on success it carries STORY_BIBLE.md §8's table forward (skills at half
    // power, memory shards, forge/actives knowledge already meta-scoped so untouched, +1 cycle)
    // and resets everything else, landing back on F01.
    void dismissEndingScreen();
    bool rebirth();
    // S3.5 step (a)/(b): the run's progression source. `floorMode()` is false only when the floor
    // data is missing; `totalStages`/the HUD counter/the hub all read the table when it is present.
    bool floorMode() const { return !floors_.empty(); }
    const toms::FloorTable& floorTable() const { return floors_; }
    const std::string& currentFloorId() const { return curFloorId_; }
    // S3.5 verification probes (also used by the browser harness): which story line is showing, and
    // whether the act card is up.
    int storyLineIndex() const { return toms::storyLineIndex(storyTurns_, (int)storyAmbientKeys_.size()); }
    bool chapterCardVisible() const { return chapterCardMs_ > 0.0f; }
    int themeAct() const { return themeActIndex_; }
    // Mobile: enlarge the on-screen pad (1.0 on desktop) and the dialogue box.
    void setUiScale(float s) { if (s > 0.0f) uiScale_ = s; }
    // saveCurrentRun() itself stays where it was (private); this is the one public entry the web
    // harness probe jsSaveNow() needs, without widening the existing declaration's access.
    void saveRunNow() { saveCurrentRun(); }
private:
    void finishCombatWin();
    void finishCombatLose();
    std::string itemName(const std::string& id) const;
    std::string itemDesc(const std::string& id) const;
    // data/items.json lookups. txt(): a text.json key (the all-item file's name/desc) or an inline
    // {lang: text} object, in the current language.
    std::string txt(const nlohmann::json& j) const;
    const nlohmann::json* itemDef(const std::string& id) const;
    std::string itemType(const std::string& id) const;        // consumable | key | weapon | armor | talent | story
    bool itemImportant(const std::string& id) const;
    int itemPrice(const std::string& id) const;                // -1 = none
    const nlohmann::json& itemStats(const std::string& id) const;
    bool isGearType(const std::string& type) const { return type == "weapon" || type == "armor" || type == "talent"; }
    // A new item reaches the player (picked up, a reward, a gift): keys and autoUse items (coins) apply
    // at once, weapons / armor / talents join the owned gear, everything else goes into the backpack.
    void receiveItem(const std::string& id);
    // Applies an item's stats (hp, atk, def, gold, exp, keys, attributes, warp) to the player.
    void applyStats(const std::string& id, const nlohmann::json& stats);
    // Adds a weapon / armor / talent to the owned gear; equip = also wear it.
    void ownGear(const std::string& id, bool equip);
    void equipGear(const std::string& id);
    // EXP, with as many level-ups as it pays for (data/stats.json "levelUp").
    void gainExp(int exp);
    void resetAttrs();                     // base + perLevel for each level above 1
    std::string itemSprite(const std::string& id) const;         // the item's icon (uiSprite name)
    // A sprite id (or "<id>.png") as the .rml files name it: <img data-attr-sprite="..."/>.
    std::string uiSprite(std::string id) const;
    std::string artStyleName(int idx) const;                     // localized, "Original" for 0
    void appendArtStyleRows(std::vector<toms::UiRow>& rows, int selected) const;   // Settings pages
    int settingsStyleRowCount() const { return artStyles_.size() > 1 ? (int)artStyles_.size() : 0; }
    bool loadSpriteAtlas(int style);   // builds + uploads the atlas for one art style
    // The prebuilt atlas (<dir>/atlas/game.atlas from tools/atlas) for the style, if there is one.
    bool loadPrebuiltSpriteAtlas(int style);
    std::string itemEffectSummary(const std::string& id) const;  // "HP +40 • DEF +1"
    void buildMenuUi(toms::UiMenu& out) const;                   // the System tab part of buildUiState
    void buildPlayerUi(toms::UiState& u) const;                  // the player menu (game_player_menu.cpp)
    // The Items tab's grid / the Equipment tab's grid, in display order.
    struct ItemCell { enum Kind { Inv, Key, Gear } kind; std::string id; int count = 1; };
    std::vector<ItemCell> itemCells() const;
    std::vector<std::string> gearCells() const;                  // owned gear of the selected slot
    enum class ItemAction { Use, Equip, Unequip, Drop };
    struct ItemButton { ItemAction action; bool enabled; };
    std::vector<ItemButton> itemButtons(const ItemCell& c) const;
    void runItemAction(const ItemCell& c, ItemAction a);
    void playerMenuEvent(const std::string& name, int arg);   // the pm_* buttons of player.rml
    // Save / Load (game_player_menu.cpp): mode 0 save, 1 load.
    void openSaveLoad(int mode);
    void saveLoadAsk(int index);                 // a slot was chosen: open its confirmation
    void saveLoadAnswer(bool yes);               // the confirmation's answer: save / load, or nothing
    void buildSaveLoadUi(toms::UiSaveLoad& out) const;
    // The Events tab: chapters reached and missions accepted, in progress first.
    // kind 0 chapter, 1 mission, 2 event. connected: the names of linked events ("？？？" for one not met yet).
    struct LogEntry { int kind = 0; std::string id, title, desc; bool done = false; std::vector<std::string> connected; };
    std::vector<LogEntry> eventLog(bool all) const;
    // store system
    void loadStore();   // parse data/store.json
    // Milestone 8: the store's items are split into tabs of <=3 cards each. Returns the indices
    // into storeItems_ that belong to the current storeTab_ (0=potions,1=weapons,2=armor,3=talents).
    std::vector<int> storeTabIndices() const;
    void buyStoreItem(int idx);                      // purchase + apply effect (or toast if poor)

    IRenderer* ren = nullptr;        // the bgfx renderer, created in loadAssets
    // Every atlas, clip file and particle file the game draws, loaded once and shared (docs/engine/12_RESOURCE_MANAGER.md).
    // Declared before what holds its resources, so it goes last and frees their textures while the renderer is up.
    std::unique_ptr<toms::res::ResourceManager> res_;
    // Makes render objects from JSON (render_object.h): everything the floor's objects draw (map_objects.cpp's looks).
    // A description used for many objects is made once and cloned (instance()).
    toms::RenderObjectFactory renderFactory_;
    bool hotReload_ = false;         // setHotReload: reload changed files about once a second
    int hotReloadMs_ = 0;
    void provideSpriteAtlas();       // the loaded sprite atlas, as the cache's kSpriteAtlas
    Player pl;
    Stage st;
    CombatState cs;
    // sprite layer registry (order matches loadAssets)
    std::vector<std::string> spriteIds;
    std::map<std::string,int> idToLayer;
    int spriteGridCols = 9;
    // With a prebuilt atlas: every sprite's UVs by layer, and the whole atlas (child sprites,
    // pivots, 9-slices and tags by name). Empty = the runtime grid (spriteGridCols).
    std::vector<std::array<float, 4>> spriteUVs_;
    std::vector<std::array<float, 4>> spriteTrim_;   // x, y, w, h of the kept pixels, as fractions of the original
    toms::AtlasFile spriteAtlas_;
    std::map<std::string, toms::SpriteBox> spriteBoxes_;   // each sprite's visible pixels (fitCharacter)
    toms::SpriteMeshFile spriteMeshes_;   // its meshes (game.mesh.json beside it, written by the anim tools; docs/engine/16)
    nlohmann::json characterFit_;                          // assets/data/character_fit.json (read at startup)
    std::string battleRigClip_;   // the clip the battle picture plays for battleRigState_ ("" = none found)
    std::shared_ptr<toms::res::AnimRes> previewAnim_;   // playPreviewAnim (its atlases come with it)
    std::string previewClip_;
    std::string previewCharacter_;                      // setPreviewCharacter
    // The battle rig (battleRigState above): the weapon its clips were loaded for, the two files, the character clip
    // fitted to the player's character, and the rig playing them.
    std::string battleRigWeapon_;
    std::shared_ptr<toms::res::AnimRes> battleCharacterAnim_, battleHeldAnim_;
    toms::anim::Clip battleFitted_;
    toms::anim::CharacterRig battleRig_;
    std::string battleRigState_;
    int lastAttackDamage_ = 0;                          // the last attack tap's damage (attack or miss)
    void loadBattleRig();                               // (re)loads the clips when the weapon changes
    // clip: plays that clip of the character's .anim for the status (a result's "anim"), else the weapon's for it
    void setBattleRigState(const std::string& state, bool restart = false, const std::string& clip = std::string());
    void updateBattleRig(int dtMs);                     // picks the status; Game::update
    void drawBattleRig();                               // the backdrop, the player and the monster; Game::draw
    // The monster on the battle screen (assets/media/anim/monster_battle.anim, tools/make_monster_battle_anims.py): the
    // clip for the battle's status -- idle; attack (its clock fired); hurt (the player's hit landed); die (the fight is
    // won) -- with the enemy's own sprite in its `body` (fitCharacter). "<status>_<enemy id>" in the file wins over the
    // shared clip. monsterRigState(): the status shown ("" = none: the file did not load, the UI's picture is drawn).
    static constexpr const char* kMonsterBattleAnim = toms::key::kAnimMonsterBattle;
    std::shared_ptr<toms::res::AnimRes> monsterAnim_;
    bool monsterAnimTried_ = false;
    // One rig a foe (cs.foes, the same order).
    struct FoeRig {
        toms::anim::Clip fitted;
        toms::anim::CharacterRig rig;
        std::string state, clip;
        std::string request;                            // attack / hurt / die: played from the start on the next update
    };
    std::vector<FoeRig> foeRigs_;
    bool monsterRigReady() const { return monsterAnim_ != nullptr; }
    std::string monsterSprite(int foe) const;           // the foe's sprite (the boss's own; else its type's)
    void setFoeRigState(int foe, const std::string& state);
    void updateMonsterRig(int dtMs);                    // Game::update
    // The battle's particles (media/fx/battle.particle; weapon_play.json "fx" picks them by weapon level): one-shot
    // effects at the player's weapon or the monster, drawn over both. Cleared when a battle starts or the screen closes.
    static constexpr const char* kBattleFxFile = toms::key::kFxBattle;
    std::shared_ptr<toms::res::ParticleRes> battleFxFile_;
    bool battleFxTried_ = false;
    struct BattleFx { toms::fx::EffectInstance fx; bool overUi = false; };   // overUi: drawn over the UI (a track hint)
    std::vector<BattleFx> battleFx_;
    uint32_t battleFxSeed_ = 0;   // each effect of a battle its own seed, from 1 again each battle (the same fight looks the same)
    const toms::WeaponFx* battleWeaponFx(bool top = false) const;   // the worn weapon's at its level (top: its best)
    void playBattleFx(const std::string& effect, int who, int delayMs = 0);   // who: -1 the player's weapon, 0.. a foe
    // At a point of the screen (design pixels); overUi: over the battle screen's UI (the attack track).
    void playBattleFxAt(const std::string& effect, float x, float y, bool overUi, int delayMs = 0);
    // The attack track's hint (the owner 2026-10-24: the timing was hard): a point's window entered, or a point hit.
    void comboHint(int point, bool hit);
    std::vector<glm::vec2> comboPointsOnScreen_;   // where the UI drew the track's points (GameUi, a frame late)
    // setBattleBoxes; the 1024 x 768 layout with one monster until told
    glm::vec4 playerBox_{262, 84, 134, 134};
    glm::vec4 foeBox_[toms::kMaxFoes] = {{652, 84, 134, 134}, {652, 84, 134, 134}, {652, 84, 134, 134}};
    glm::vec4 fighterBox(int who) const { return who < 0 ? playerBox_ : foeBox_[std::min(who, toms::kMaxFoes - 1)]; }
    void updateBattleFx(int dtMs);                      // Game::update
    std::string previewHeldFile_;                       // setPreviewHeld
    std::shared_ptr<toms::res::AnimRes> previewHeld_;   // its clips
    toms::anim::CharacterRig previewRig_;               // the preview clip with the held clip at its hand
    toms::anim::Clip previewFitted_;                    // the preview clip fitted to previewCharacter_
    void startPreviewClip();                            // (re)starts the preview clip, fitted when a character is set
    int previewAnimVer_ = -1;                           // previewAnim_'s version when the clip was started
    toms::anim::AnimPlayer previewPlayer_;
    int previewCrowdN_ = 0;                             // setPreviewCrowd
    std::vector<toms::anim::AnimPlayer> previewCrowd_;
    double previewDrawMs_ = 0;
    std::vector<toms::anim::NodePose> previewPoses_;   // per-frame scratch
    toms::anim::DrawList previewDraws_;
    std::vector<std::string> previewMissing_;          // sprites already reported missing
    std::string previewMeshSprite_;                    // playPreviewMesh
    double previewMeshMs_ = 0;
    std::vector<MeshVertex> previewMeshVerts_;         // per-frame scratch
    std::vector<uint16_t> previewMeshIdx_;
    void drawPreviewMesh();
    void drawPreviewAnim();
    // The looping clips on the map (a monster's idle clip "idle_<enemy id>" from the manifest's "anim.monster_idle",
    // an object's own clip): one clock, each tile at its own phase (map_objects.cpp).
    std::vector<toms::anim::NodePose> idlePoses_;   // per-frame scratch (drawClipInRect)
    toms::anim::DrawList idleDraws_;
    double idleClockMs_ = 0;
    // Objects' looks (docs/gameplay/02_STAGE_EDITOR.md): an object's own "look" over its type's ("look" in enemies.json /
    // items.json / its NPC file), field by field -- read once per floor, when its objects are made (map_objects.cpp).
    // typeSprite: the sprite a type is drawn with (its look, else the old table).
    nlohmann::json lookOf(const Entity& e) const;
    nlohmann::json typeLook(const std::string& kind, const std::string& id) const;
    std::string typeSprite(const std::string& kind, const std::string& id) const;
    // An object's linked events, when the player interacts with it: a talk event wakes (a topic), the others run when
    // their `requires` holds (with their title as a notice), else they are started.
    void fireLinkedEvents(const Entity& e);
    // A clip at time t placed on a map rect: its (0, 0) on the rect's bottom centre, scaled so a sprite of
    // spriteW x spriteH pixels fills the rect. False = nothing drawn.
    bool drawClipInRect(const toms::anim::Clip& clip, float t, const toms::anim::AtlasSet& set, float x, float y, float w, float h,
                        float spriteW, float spriteH, const float tint[4]);
    bool drawClipInRect(const toms::anim::Clip& clip, float t, const toms::anim::AtlasSet& set, float x, float y, float w, float h,
                        float spriteW, float spriteH);
    // A resource file the game asked for is missing or broken: a message on screen (once per file) and in the log.
    void resourceMissing(const std::string& name, const std::string& path, const std::string& error);
    // Its messages wait here until the map is on screen (startup loads happen behind the title, and a new game or a
    // load clears the notifications).
    std::vector<std::string> resourceMessages_;
    // The floor's things, a class per kind (map_objects.h), made when a floor loads; they draw themselves by status.
    MapFloor floor_;
    void drawTileLayer(const MapView& v, int startX, int endX, int startY, int endY);   // tiles, locked stairs, walk target
    // A door the player walks into with its key: the key is used, the door opens (its clip plays), the tile becomes
    // floor and stays open on later visits (EntityStatus::Opened). The player stays where they were.
    void openDoor(int x, int y, char c);
    std::shared_ptr<toms::res::ParticleRes> previewFxFile_;   // playPreviewFx (its atlases come with it)
    std::string previewFxName_;
    int previewFxVer_ = -1;
    toms::fx::EffectInstance previewFx_;
    float previewFxIdle_ = 0;                         // seconds since a one-shot effect finished
    int fxGpuThresholdOverride_ = -1;                 // --fx-gpu-threshold (-1: the setting)
    // art styles (assets/media/styles/styles.json); style 0 = the original art
    std::vector<toms::ArtStyle> artStyles_{toms::ArtStyle{}};
    int loadedArtStyle_ = 0;                  // the one whose sprites were loaded at startup
    int spriteAtlasRevision_ = 0;             // +1 per atlas load (uiSpritesheet changed)
    // item definitions (id -> json from data/items.json, the all-item file)
    std::map<std::string, nlohmann::json> itemDefs;
    // data/stats.json: the attributes and the level-up numbers.
    struct AttrDef { std::string id, shortName, nameKey, descKey; int base = 5, perLevel = 1;
                     std::vector<std::pair<std::string, float>> effects; };   // (stat, per point above base)
    // maxLevel: the top level; points: the attribute points each level gives (by level reached), freePoints below them.
    struct LevelUpRule { int expPerLevel = 30, atk = 2, def = 1, maxhp = 10, freePoints = 0, maxLevel = 99;
                         std::vector<toms::LevelPoints> points; };
    std::vector<AttrDef> attrDefs_;
    LevelUpRule levelUp_;
    // The weapons / armor / talents the player owns (bought or forged), in the order they were got.
    std::vector<std::string> gearOwned_;
    std::map<std::string, int> weaponUses_;   // wins with each weapon (weaponKey()); its level comes from them
    // ---- the player menu's state (inGameMenuOpen_ = it is open) ----
    MenuTab menuTab_ = MenuTab::Status;
    int attrSel_ = -1;          // Status: the attribute whose description shows (-1 none)
    // Status: points placed with + / - but not confirmed yet, by attribute id (the owner 2026-10-24: the points count
    // only after Confirm, which also saves). Closing the menu drops them.
    std::map<std::string, int> attrDraft_;
    int attrDraftTotal() const { int n = 0; for (const auto& [id, k] : attrDraft_) n += k; return n; }
    int gearSlot_ = 0;          // Equipment: 0 weapon, 1 armor
    int gearSel_ = 0;
    int itemFilter_ = 0;        // Items: 0 all, 1 consumables, 2 gear, 3 keys
    int itemSel_ = 0;
    int cardSel_ = 0;          // the Cards tab's selected card
    bool newsCards_ = false;   // a dot on the Cards tab: a card was got since it was last opened
    std::mt19937 cardRng_{20261021u};   // card drops (fixed seed: scripted test runs repeat)
    std::vector<std::string> cardCells() const;   // the monsters with a record, easiest first
    int eventFilter_ = 0;       // Events: 0 unfinished, 1 all
    int eventSel_ = 0;
    bool dropConfirmOpen_ = false;
    bool dropConfirmYes_ = false;
    bool storeFromMenu_ = false;   // the store was opened from the System tab: closing it goes back there
    bool saveLoadOpen_ = false;
    int saveLoadMode_ = 0;         // 0 save, 1 load
    int saveLoadSel_ = 0;          // the slot (0-based)
    bool saveLoadConfirm_ = false, saveLoadConfirmYes_ = false;
    std::vector<toms::SlotSummary> saveLoadSlots_;   // read from disk when the screen opens and after a save
    // "Something new inside" dots on ≡ and on a tab.
    bool newsStore_ = false, newsGear_ = false, newsEvents_ = false;
    size_t eventLogSeen_ = 0;      // the event log's last count (+1; 0 = take it without news), update()
    // The event pools (data/events/pool_*.json), for the player's event log (docs/gameplay/event_editor/06_EVENT_LOGIC.md
    // section 7): titles, descriptions, links, and whether the event is listed (its kind's "inLog" in
    // events/kinds.json, or its own). Floor events with no pool record still fire; they are just not listed.
    struct EventDef {
        std::string kind, titleKey, descKey;
        std::string stairsFrom, stairsTo;     // a "go to the stairs" event (Q7): its text is built from these
        std::vector<std::string> next;
        nlohmann::json requires;              // null = none
        nlohmann::json actions;               // null = none: story actions (story_actions.h), run once
        std::string trigger = "step";         // "step" / "talk" / "chain" / "varChanged" (06 section 1)
        std::string triggerVar;               // "varChanged": the variable
        bool once = true;                     // false: repeatable, until maxTimes (0 = no limit); next wakes once
        int maxTimes = 0;
        std::string textKey;                  // "text"
        bool inLog = true;
    };
    // A gate ("gate:<item id>" in a floor's legend): opens only for that item, which it takes; else says what
    // it needs. Opened gates stay open (entity status Opened).
    void bumpGate(Entity& gate);
    std::map<std::string, EventDef> eventDefs_;
    std::map<std::string, std::vector<std::string>> chapterEvents_;   // chapter id -> its story events (beats)
    void loadEventDefs();
    // Q7: stairs locked by an event. stairUnlocks_: event id -> the stairs it opens (floor, target), from every
    // floor spec's `stairs`; loadStairLocks also adds the "go to the stairs" events. finishEvent is every
    // finished event (the run flag, the log, `next`, and the stairs it opens with their dialog).
    struct StairUnlock { std::string floor, to; };
    std::map<std::string, std::vector<StairUnlock>> stairUnlocks_;
    void loadStairLocks();
    void finishEvent(const std::string& evId, int depth = 0);
    // W38: a stair waits for all of its events.
    bool stairLocked(const toms::StairLink& s) const {
        for (const std::string& e : s.unlockEvents) if (!run_.eventFinished(e)) return true;
        return false;
    }
    // The floor's stairs that are open but not taken yet since they opened (their "go to the stairs" event is
    // started, not finished): the map points at them with an arrow (W38).
    bool stairJustOpened(const std::string& floor, const toms::StairLink& s) const;
    const toms::StairLink* findStair(const std::string& floor, const std::string& to) const;
    std::map<std::string, std::pair<int, int>> stairSpots_;   // "F01>F02" -> the stair's tile, from floors loaded so far
    bool stepOntoStairs(int x, int y);       // the stair at (x, y), if open: ask to take it
    void bumpLockedStairs(const toms::StairLink& s);
    std::string stairsConfirmGoEvent_;       // the "go to the stairs" event taking the asked stair finishes
    struct Notice { std::string title, body; };
    std::vector<Notice> notices_;
    // The ids an event is linked to: its `next`, the events whose `next` names it, and the events its
    // `requires` waits for (runFlagSet "event_<id>" / eventDone).
    std::vector<std::string> connectedEvents(const std::string& eventId) const;
    // data/story/counters.json's state words (insight, resolve, humanity), for the Status tab.
    std::map<std::string, nlohmann::json> counterLabels_;
    bool fullscreenAvailable_ = false, fullscreenOn_ = false, fullscreenRequest_ = false;
    // dialogue state
    bool inDialogue = false;
    int dlgSel = 0;                     // currently highlighted dialogue choice (gamepad nav)
    std::string dlgNpc;
    std::string dlgNode = "root";
    nlohmann::json dlgData;
    std::vector<DialogueChoice> dlgChoices;
    // NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md, talk_system.h): the NPC files, the events bound
    // to each NPC (in menu order), and what the open dialogue is for: a plain dialogue, an NPC's chat line, an
    // event's topic, a finished event's after-dialogue, or the topic menu.
    enum class DlgMode { Plain, Chat, Topic, After, Menu };
    DlgMode dlgMode_ = DlgMode::Plain;
    std::string dlgEvent_;              // Topic / After: the event
    std::string dlgSpeaker_;            // the name on the dialogue box (empty: none)
    std::string dlgTalkNpc_;            // the NPC being talked to (the menu's "Just chat")
    bool dlgFinishOnClose_ = false;     // Topic: the file's "finishOnClose"
    std::map<std::string, toms::talk::NpcDef> npcDefs_;
    std::map<std::string, std::vector<toms::talk::Topic>> npcTopics_;
    std::mt19937 talkRng_{20261016u};   // "random" chat lines (fixed seed: scripted test runs repeat)
    void loadNpcs();
    bool holds(const nlohmann::json& condition) const;   // a `requires` against the run, with the current stage
    const toms::talk::NpcDef& npcDef(const std::string& npcId);
    nlohmann::json readDialogue(const std::string& id) const;
    bool openDialogue(const nlohmann::json& data, DlgMode mode);   // from its start node; false = nothing to show
    void playChatLine(const std::string& npcId, int line);
    void openTopic(const std::string& eventId, bool finished);
    void openTopicMenu(const std::string& npcId, const std::vector<toms::talk::MenuEntry>& menu);
    void endDialogue();                 // closes it (Topic + finishOnClose: finishes the event)
    void finishTalkEvent(const std::string& eventId);
    // Wakes an event (an event's `next`, a choice's `fire`, the fireEvent action): it is started; a "chain" event
    // whose `requires` holds runs at once (its actions, then finished, which wakes its own `next`).
    void wakeEvent(const std::string& eventId, int depth = 0);
    // Every story action in the game goes through here (story_actions.h): events, chat lines, dialogue choices.
    void runStoryActions(const nlohmann::json& actions);
    // One run of an event (docs/gameplay/event_editor/08_NPC_DIALOGUE.md section 11.1): its record's actions, counted; the first
    // time it is also finished (flag, log, `next`, stairs). A finished one-off does nothing (false); a repeatable one
    // ("once": false) runs again until "maxTimes".
    bool completeEvent(const std::string& eventId, int depth = 0);
    bool canRepeat(const std::string& eventId) const;
    // An event variable changed: the "varChanged" events on it whose `requires` holds run (with their title as a notice).
    void varChanged(const std::string& var);
    int varChangeDepth_ = 0;            // a variable changed by a varChanged event's own actions: at most 16 deep
    void noticeEventTitle(const std::string& eventId);   // an event with no tile and no NPC ran: its title as a notice
    std::string npcSprite(const std::string& npcId) const;   // its NPC file's "sprite", else npc_<id>, else the old table
    // enemy templates
    std::map<std::string, nlohmann::json> enemyTpl;
    // The battle scene's tuning, from data/battle.json (loadAssets). Without the file or a key, the defaults below.
    // A bar starts slow (v0) and speeds up to its top speed (vmax) on a cubic curve (power_bar.h); the two are scaled apart.
    float barSlowScale_ = 1.0f;                           // "bar_slow_speed_scale": multiplies the slow start (v0)
    float barFastScale_ = 1.0f;                           // "bar_fast_speed_scale": multiplies the top speed (vmax)
    int barCooldownMs_ = CombatState::kBarCooldownMs;     // "bar_cooldown_ms": the wait after a tap before a bar moves again
    int attackPauseMs_ = 450;     // "attack_pause_ms": both sides' clocks pause this long for each attack (0: off)
    int hitNumberDelayMs_ = 180;  // "hit_number_delay_ms": the damage number appears this long after the attack starts
    // Delayed calls of the scenes (the battle's damage numbers). battleTimers_ owns the battle's: reset when a battle
    // starts, so a number from the last fight never lands in the next one.
    toms::TimerQueue timers_;
    toms::TimerOwner battleTimers_;
    toms::WeaponPlayTable weaponPlay_;   // data/weapon_play.json
    toms::LooksTable looks_;             // data/looks.json: the status map (docs/gameplay/11 §4.2)
    toms::MovesTable moves_;             // data/moves.json: the monsters' moves (docs/gameplay/13 §6.1)
    toms::PaceSettings paceSettings_;    // data/battle.json "pace", "stamina"
    toms::BreakSettings breakSettings_;  // data/battle.json "break"
    toms::FeelSettings feel_;            // data/battle.json "feel" (docs/gameplay/15 §3.8)
    toms::GradeSettings grade_;          // "grade" (§3.6)
    toms::GuardSettings guard_;          // "guard" (§3.2): the timed button, or the bar
    std::mt19937 shakeRng_{20261031u};
    std::string playOverride_;           // a scenario's battle "play" ("" = the weapon's; scenarios: "track" unless set)
    std::string paceOverride_;           // setBattlePace / a scenario's "pace" ("" = the weapon's or the file's)
    bool comboHoldsMonsters_ = false;    // data/battle.json "combo_holds_monsters": their clocks wait for a running combo
    bool comboRunsThroughAttacks_ = true;   // "combo_runs_through_attacks": a combo's marker keeps moving in an attack pause
    nlohmann::json battleJson_ = nlohmann::json::object();   // data/battle.json as read (a scenario's "rules" patch it)
    std::mt19937 moveRng_{20261025u};    // the monsters' next moves (reseeded each battle: the same fight goes the same way)
    void startAttackPause();
    void popDamage(int dmg, int who, const std::string& style);   // a damage number over who (-1 the player, 0.. a foe), hitNumberDelayMs_ from now
    // current stage id
    std::string curStage;
    std::string dataDir;
    // Milestone 3: story flags + main-story beat, evaluated by GameConditionContext (game.cpp)
    // for door/key gating and dialogue `requires` gating. In-memory only this milestone — reading
    // and writing this to an actual save file on disk is Milestone 5's job (Stage Select's
    // "Continue"), per docs/architecture/IMPLEMENTATION_ROADMAP.md.
    toms::MetaSaveData meta_;
    // Milestone 7: per-tile enemy/item status (Milestone 3's Entity Status System, wired in for
    // real here) -- a defeated monster or collected item stays cleared when the floor is
    // reloaded (stairs, or Milestone 5's Stage Select hub), matching the owner's decision that
    // cleared floors don't repopulate. Keyed by entityStatusKey(stageId, x, y); in-memory only
    // for now, same "persistence is later work" caveat as meta_ and missionTrackers_. An opened
    // door is recorded too (EntityStatus::Opened, see openDoor): it stays open, and costs its key once.
    std::map<std::string, std::string> entityStatus_;
    // Milestone 6: equipment layer over the Power Bar/damage formulas (docs/design/MAIN_BATTLE_SCENE_DESIGN.md
    // §4). equipmentDefs_ is intentionally empty until Milestone 8 loads data/equipment.json --
    // equipped_ starts fully empty (nothing equipped), so effectiveAttackBar/effectiveDefenseBar/
    // effectiveMaxMult all fall back to baseline geometry/2.0x until real items exist to equip.
    toms::EquippedSet equipped_;
    std::map<std::string, toms::EquipmentDefinition> equipmentDefs_;
    // S4: the skill tree's content (data/skills.json) -- loaded once at boot, mirroring
    // equipmentDefs_ immediately above. What's actually OWNED lives on run_ (RunStoryState),
    // not here, matching the same Game-owns-state/content-is-data split as equipped_/equipmentDefs_.
    std::map<std::string, toms::SkillDefinition> skillDefs_;
    // S5: forging's content (data/forge.json) -- loaded once at boot, same shape as skillDefs_
    // above. What's actually KNOWN lives on meta_ (MetaSaveData::forgeRecipesKnown, permanent,
    // unlike skillsOwned() which is run-scoped) -- see forge_system.h's own comment for why.
    std::map<std::string, toms::ForgeRecipeDefinition> forgeDefs_;
    // S6: the village hub's content (data/hub.json) -- loaded once at boot, same shape as
    // skillDefs_/forgeDefs_ above. A hub location has no "owned" state of its own (see
    // hub_system.h's own comment) -- unlocking is just reading the run flag its unlockFlag names.
    std::map<std::string, toms::HubLocationDefinition> hubDefs_;
    // S7 (equipment actives, first slice): content loaded once at boot, same shape as
    // skillDefs_/forgeDefs_/hubDefs_ above. There is no separate "known" list yet (unlike
    // forgeRecipesKnown) -- nothing in this slice's content grants an active independently of the
    // equipment that carries it.
    std::map<std::string, toms::ActiveDefinition> activeDefs_;
    // M7 (first slice): content loaded once at boot, same shape as skillDefs_/forgeDefs_/hubDefs_/
    // activeDefs_ above.
    toms::EndingsTable endingsTable_;
    // Empty = no ending showing. The one writer is Game::triggerEnding() (game_combat.cpp);
    // dismissEndingScreen()/rebirth() (public, see their declarations above) both clear it.
    std::string activeEndingId_;
    void triggerEnding(const std::string& endingId);
    // M8 (first slice): the rebirth config, same "load once at boot" shape as the above.
    toms::CyclesConfig cyclesConfig_;
    // The starting stats newGame() AND Game::rebirth() both need -- named once here instead of the
    // literals 12/4/120 living independently in two places (rebirth() computes a HALVED bonus
    // *over* these, so it needs the exact same baseline newGame() used, not just "looks about
    // right").
    static constexpr int kStartingAtk = 12, kStartingDef = 4, kStartingHp = 120;
    // The weapon a new game (and a rebirth) starts with, owned and equipped: the bow (owner, 2026-10-22). In battle the
    // player is drawn as the archer with it (its "battleAnim" in data/items.json).
    static constexpr const char* kStartingWeapon = "bow";
    // Milestone 4 (Encounter Resolution): when a monster tile resolves to EncounterKind::
    // DialogueGate, the enemy instance is stashed here so a later `action.enterBattle` dialogue
    // choice knows what to fight. hasPendingEncounter_ guards against acting on a stale/unset
    // EnemyInst (whose numeric fields are otherwise uninitialized garbage, matching CombatState's
    // own `enemy` member convention — see startCombat()).
    EnemyInst pendingEncounterEnemy_;
    bool hasPendingEncounter_ = false;
    // Milestone 4 (Mission System): missionDefs_ is intentionally empty until Milestone 8 loads
    // data/missions.json -- the tracker map is still real, live state (a mission "started" by id
    // is meaningful even before content defines what that id means).
    std::map<std::string, toms::MissionDefinition> missionDefs_;
    std::map<std::string, toms::MissionTracker> missionTrackers_;
    void startMission(const std::string& id);
    // Milestone 8: grants a Completed mission's reward and flips it to Claimed. Idempotent --
    // a no-op unless the tracker's state is exactly Completed, so re-triggering this (e.g. the
    // dialogue choice that calls it stays visible after claiming) can never double-grant.
    void claimMission(const std::string& id);
    void runDialogueAction(const nlohmann::json& action);
    // S4: applies a chapter's top-level `grants` (skills/skillPoints; hub/unlocks as flags for a
    // later phase to read) exactly once -- called from loadStage() the first time a floor in that
    // chapter is reached. Idempotent via a reserved run flag ("_chapter_granted.<id>"), the same
    // "reuse the existing mechanism instead of a parallel one" choice this project keeps making
    // (see entity-status reuse, M3's log). A no-op for a chapter id with no file yet (ch_04+),
    // so this is safe to call unconditionally as soon as any floor names its chapter.
    void applyChapterGrants(const std::string& chapterId);
    void wireMissionEvents();   // subscribes mission-progress handlers to the global EventBus once
    // This Game's handlers on the global EventBus: destroyed with the Game, which removes them (event_bus.h).
    std::vector<toms::Subscription> busSubs_;
    // Milestone 5: daily-mission reset check (architecture-doc §8.3), run once per session start
    // (see loadAssets). Inert today since missionDefs_ is empty until Milestone 8, but pushes a
    // real notification the moment a tracked daily mission actually rolls to Available.
    void rollDailyMissions();
    // Milestone 5: transient toast queue -- {message, remaining_ms}, decremented in update().
    std::vector<std::pair<std::string, int>> notifications_;
    void pushNotification(const std::string& msg);
    // Milestone 5: Stage Select hub state.
    bool stageSelectOpen_ = false;
    // Milestone 8: `preview` is the recommended-stats blurb shown in Stage Select, authored per
    // stage JSON's optional top-level "preview" string (empty for a file that doesn't set one).
    struct StageInfo { std::string id; std::string name; int index = 0; std::string preview;
                       std::string fileStem; };   // filename stem, e.g. "stage01" (the id inside
                                                  // the JSON is "stage_01" -- the game loads by
                                                  // stem, so matching needs both)
    std::vector<StageInfo> stageList_;
    bool stageListLoaded_ = false;
    void ensureStageListLoaded();
    // Milestone 9: stairs confirm-before-transition state (see stairsConfirmOpen()'s
    // declaration above for why). requestStageTransition() is what movePlayer() now
    // calls instead of loadStage() directly when the player steps onto 'U'/'D'.
    bool stairsConfirmOpen_ = false;
    bool stairsConfirmIsUp_ = false;
    std::string stairsConfirmTarget_;
    void requestStageTransition(const std::string& target, bool isUp);
    // Milestone 9 polish: per-axis "held direction" state for setMoveHeldX/Y (declared
    // above) -- holdMs/repeating are ticked in update(). kMoveInitialDelayMs is the pause
    // after the immediate first step before repeating starts (long enough that a quick tap
    // never double-steps); kMoveRepeatMs is the steady repeat rate once it's going.
    struct MoveHoldAxis { int dir = 0; int holdMs = 0; bool repeating = false; };
    MoveHoldAxis moveHoldX_, moveHoldY_;
    static constexpr int kMoveInitialDelayMs = 220;
    static constexpr int kMoveRepeatMs = 110;
    // Click-to-move state (see walkTo()): the remaining steps, first = next tile.
    std::vector<std::pair<int, int>> walkPath_;
    int walkTargetX_ = -1, walkTargetY_ = -1;
    int walkTimerMs_ = 0;
    bool walkPassable(int x, int y) const;   // a tile the route may cross (plain floor, nothing on it)
    bool planWalk(int tx, int ty, std::vector<std::pair<int, int>>& out) const;
    void walkStep();
    // M2 styling spike backdrop state. Declared unguarded so Game::draw() — shared between the
    // desktop and web builds — can call drawStylingSpikeBackdrop() unconditionally; it is a no-op
    // whenever stylingSpikeVisible_ is false. GameSession drives it (F2) on desktop and web.
    bool stylingSpikeVisible_ = false;
    float stylingSpikeRect_[4] = {0, 0, 0, 0};   // x,y,w,h — set by drawStylingSpike(), read by the backdrop
    void drawStylingSpikeBackdrop();
    int totalStages = 10;   // highest stage index (derived from data/stages at loadStage)

    // ---- maze camera ----
    // The camera itself lives in src/game/camera.{h,cpp} as toms::Camera: mode (Follow/Rooms),
    // zoom (viewCols), the eased position and its target, plus the viewport math and the edge
    // clamping. Deliberately free of renderer/game coupling (pure math, unit-tested by
    // camera_test.cpp) -- this class only hands it the drawing area in pixels and the focus tile.
    toms::Camera cam_;
    // Adapter: the camera needs the drawing area in pixels (the 1024x768 design canvas unless the
    // renderer reports otherwise), which only Game knows. Kept as one call site so draw() and
    // update() can never compute a different viewport.
    void cameraViewportTiles(float& ts, int& cols, int& rows) const;
    // store system state
    std::vector<StoreItemDef> storeItems_;
    int storeUnlockStage_ = 3;   // stage index at which the shop unlocks (from store.json)
    bool storeUnlocked_ = false;
    bool storeUnlockDlg = false; // "shop unlocked!" popup showing (with confirm button)
    bool storeOpen = false;      // store overlay open
    int storeSel_ = 0;           // selected card index (keyboard nav), local to the current tab
    int storeTab_ = 0;           // 0=potions,1=weapons,2=armor,3=talents (see storeTabIndices())
    std::string toastMsg_;        // transient message ("金錢不足")
    int toastTimer_ = 0;         // ms remaining for toast
    int shakeTimer_ = 0;         // ms remaining for "not enough gold" shake
    // Raw json (string or {code:text}) from store.json, resolved via Locale::field() at draw
    // time -- loadStore() runs before the title phase sets locale_ to the player's saved
    // language, so resolving here (instead of once at load) is what makes it show correctly.
    nlohmann::json storeTitle_ = "道具商店";

    // ---- in-game menu (walking-phase HUD gear icon): Save / Settings (language) / Back to
    // Title. Built directly as Game state (not a separate testable class, unlike TitleScreen)
    // to match how every other in-game modal (store/inventory/stairs-confirm) already works in
    // this file -- there's no headless-test need here the way the title phase has.
    bool inGameMenuOpen_ = false;
    enum class InGameMenuPage { Main, Settings, Skills, Forge, Hub };
    InGameMenuPage inGameMenuPage_ = InGameMenuPage::Main;
    int inGameMenuSel_ = 0;               // highlighted row on whichever page (keyboard nav)
    bool inGameLangConfirmOpen_ = false;  // "switch to XXX?" sub-dialog, mirrors the title's own
    int inGameLangConfirmIdx_ = 0;
    bool inGameLangConfirmYes_ = true;
    bool inGameConfirmIsStyle_ = false;   // that dialog is about an art style (idx = style index)
    // S5/S6: Forge and Village are each their OWN conditional row, gated on their own hub.* flag
    // (set by Game::applyChapterGrants) -- they only appear once the player has actually reached
    // them narratively, rather than showing an always-empty screen from floor 1 the way Skills
    // does (S4 chose the opposite trade-off there; these two have real gating data to read).
    // Nothing enforces which of the two unlocks first (today's content happens to grant Village at
    // F7 before Forge at F21), so mainMenuOrder() below re-derives the Main page's row LIST fresh
    // every call instead of two independent hardcoded row-count branches -- with two conditional
    // rows, "N booleans -> a hand-picked row count" stops being a small special case.
    bool forgeMenuUnlocked() const { return run_.flag("hub.forge"); }
    bool hubMenuUnlocked() const { return run_.flag("hub.village"); }
    // The Main page's row order/count as a pure function of the two flags above (plus the three
    // always-present rows) -- so draw and every input handler below call this ONE function instead
    // of separately re-deriving "is Forge row 3 or row 4 this time," the same principle
    // skillMenuOrder()/forgeMenuOrder() already apply to their own sub-pages.
    enum class MainMenuRow { Store, Save, Settings, Skills, Village, Forge, Fullscreen, BackToTitle };
    std::vector<MainMenuRow> mainMenuOrder() const;
    void closeInGameMenu();                // closes the whole menu (any page/dialog)
    int mainMenuRowIndex(MainMenuRow r) const;   // its row on the System tab (0 if absent)
    // Saves, tears down transient modal/run-in-progress state, and reopens the title (the
    // inverse of newGame()/applyLoadedRun() -- see their resets for what this mirrors).
    void returnToTitle();

    // miniaudio backs both desktop (native device backends) and the browser
    // build (Web Audio via Emscripten) behind this one interface -- no-op
    // when no audio device/context is available (e.g. headless CI).
    Audio audio;

    // ---- Title phase state (see the public title block above) ----
    toms::TitleScreen title_;
    toms::GameSettings settings_;
    toms::Locale locale_;
    int activeSlot_ = 0;      // 0 = none chosen yet; set by newGame()/continueFromSlot()
    bool slotReadOnly_ = false;   // the active slot holds a newer game's save: played, never written (docs/gameplay/05)
    int playTimeSec_ = 0;     // this run's accumulated play time (shown in the Continue list)
    bool saveDirty_ = false;  // progress changed since the last slot write
    int saveFlushMs_ = 0;     // countdown used to throttle autosave (see update())
    static constexpr int kAutosaveIntervalMs = 3000;   // flush at most this often, when dirty
    void markProgressDirty() { saveDirty_ = true; }
    // The save key of the object at (x, y) on the open floor: by its stable uid when the stage file gives one, else
    // by its position (entity_status.h).
    std::string statusKeyAt(int x, int y) const;
    // Old saves keyed this floor's objects by position (and some by the file name rather than the floor id): move
    // those statuses to the keys statusKeyAt uses now. Called when a floor loads.
    void migrateStatusKeys();
public:
    // Writes the run now if it has unsaved changes -- the host calls it when the window closes or the app goes to
    // the background, so the autosave's last few seconds are not lost (docs/gameplay/05_SAVE_SYSTEM.md).
    void flushSave() { if (saveDirty_ && !title_.isOpen()) saveCurrentRun(); }
private:
    // Performs whatever the title phase asked for (see title_screen.h's TitleAction).
    void handleTitleAction(toms::TitleAction a);
    std::string stageDisplayName(const std::string& id) const;
    toms::RunSaveData runSaveFromState() const;
    // Applies a loaded slot to the live game state, then loads its stage.
    void applyLoadedRun(const toms::MetaSaveData& m, const toms::RunSaveData& r, int slot, int playTimeSec);
    float uiScale_ = 1.0f;    // setUiScale(): dialogue box size on phones
    // Test scenarios (startScenario): what was asked for, checked by scenarioCheck.
    std::string scenarioName_, scenarioScene_;
    nlohmann::json scenarioExpect_;
    int scenarioTestFrames_ = 0;
    std::string scenarioTestKeys_;
    std::string scenarioTestClicks_;
    // The weapon test arena (scene "arena", toms_game --arena; docs/gameplay/07_GAMEPLAY_PLAN.md phase 0): fight after fight
    // against the listed enemies in turn, full HP each time, no rewards and no story effects (no deaths counted).
    struct Arena {
        bool on = false;
        std::vector<std::string> enemies;
        nlohmann::json overrides;     // "battle"'s hp / atk / def / atk_interval_ms, for every fight
        size_t next = 0;
        int fights = 0, wins = 0, losses = 0;
        bool trial = false;           // the start story's weapon trial (docs/gameplay/07 phase 3): one fight, then back to the master
    };
    Arena arena_;
public:
    // The start story (docs/gameplay/07 phase 3): the weapon master lets the player try the weapons (dialogue/weapon_master.json),
    // a trial fight each, until one is chosen; the run then begins on stage 1. Opened by the title's New Game.
    void startWeaponTrial();
    // --skip-start-story (the screenshot tests): New Game goes straight to stage 1, as before the start story.
    void setSkipStartStory(bool on) { skipStartStory_ = on; }
    // Reward events (docs/gameplay/07 phase 6, data/minigames.json): opened by the dialogue action {"type": "miniGame", "id": ...}.
    // Enter / a tap: start, then tap the points, then close; Esc closes when not playing.
    bool miniGameOpen() const { return miniGame_.open; }
    void openMiniGame(const std::string& id);
    void miniGameTap();
    void closeMiniGame();
private:
    bool skipStartStory_ = false;
    struct MiniGameState {
        bool open = false;
        std::string id;
        int phase = 0;              // 0: the text, waiting to start; 1: playing; 2: the result
        toms::ComboPlay play;
        int reward = 0;
    };
    MiniGameState miniGame_;
    toms::MiniGameTable miniGames_;   // data/minigames.json
    void finishMiniGame();
    void tryWeapon(const std::string& weaponId);   // dialogue action "tryWeapon": wear it, fight the training slime
    void chooseWeapon();                           // dialogue action "chooseWeapon": keep the worn weapon, begin
    void endWeaponTrialFight();                    // the trial fight is over: back to the master's "after" node
    bool enemyFromData(const std::string& id, const nlohmann::json& overrides, EnemyInst& out) const;
    void arenaNextFight();
    bool arenaEndFight(bool won);   // true: the arena took the fight's end (finishCombatWin / Lose return at once)
    TOMS_OBJECT(Game)
};

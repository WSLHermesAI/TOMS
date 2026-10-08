// game.h — game logic: player, movement, auto combat, talking, stage flow, world drawing.
// The UI (title, HUD, battle, menus, ...) is RmlUi: buildUiState()/uiEvent() below, ui/ui_state.h.
#pragma once
#include <array>
#include <vector>
#include <string>
#include <map>
#include <json.hpp>
#include "object.h"      // Trackable base: Player/EnemyInst/CombatState/Game are tracked
#include "render_iface.h"
#include "game_state.h"  // toms::GameState — see Game::currentState()
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
#include "floor_table.h"     // toms::FloorTable — S3.5: the 70-floor tower as ordered data
#include "stage.h"
#include "ui_state.h"       // toms::UiState -- what the UI shows (see buildUiState)
#include "title_screen.h"    // toms::TitleScreen/TitleAction — the title phase (New Game/Continue/Settings)
#include "game_settings.h"   // toms::GameSettings — persisted preferences (language, slots)
#include "art_styles.h"      // toms::ArtStyle — the selectable art styles (assets/media/styles)
#include "atlas_file.h"      // toms::AtlasFile — the prebuilt sprite atlas (tools/atlas)
#include "particle_fx.h"     // toms::fx — particle effects on the atlases (docs/17_PARTICLES.md)
#include "anim_player.h"     // toms::anim — node animations on that atlas (anim_clip.h)
#include "localization.h"    // toms::Locale — key -> localized string (data/text.json)
#include "Audio.h"        // SFX (miniaudio) -- native device backends on desktop, Web Audio in the browser

struct Player : public Trackable {
    int hp, maxhp, atk, def, gold, exp, lv;
    int key_yellow = 0, key_blue = 0, key_red = 0;
    int x = 1, y = 1;
    // inventory: list of item ids (a 9-grid, extendable UI). Keys/coins are NOT
    // stored here (they apply immediately); usable items (gems/potions/exp/scroll) are.
    std::vector<std::string> inv;
    Player() : hp(100), maxhp(100), atk(10), def(5), gold(0), exp(0), lv(1) {}
    TOMS_OBJECT(Player)
};

struct EnemyInst : public Trackable {
    std::string id; std::string name; int hp, atk, def, exp, gold; int x, y; bool boss=false;
    // Battle System v2 (docs/design/BATTLE_SYSTEM_V2_PROPOSALS.md): how often, in ms, this enemy attacks
    // on its own real-time clock -- independent of anything the player does. Read from
    // data/enemies.json's "atk_interval_ms" (engageMonster()); defaults to 4000 when absent.
    int atkIntervalMs = 4000;
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

    EnemyInst enemy;
    int playerHP, enemyHP;
    bool active = false;
    std::string log;          // last exchange text
    bool won = false;

    AutoBar atkBar, defBar;
    bool shieldBanked = false;
    float shieldPower = 0.0f;
    int enemyClockMs = 0;      // counts up to enemy.atkIntervalMs, then fires and resets to 0

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
    TOMS_OBJECT(CombatState)
};

// A store item, parsed from data/store.json. cost = cost_base * cost_multiplier^purchases.
struct StoreItemDef {
    std::string id;
    // Raw json (either a plain string or a {code: text} multi-language map) -- resolved via
    // Locale::field() at display time so a language switch updates the store instantly instead
    // of needing loadStore() to re-run.
    nlohmann::json name;
    std::string sprite;       // sprite id (into the atlas) for the icon
    std::string icon_path;    // original asset path stored in json
    nlohmann::json desc;
    nlohmann::json effect;     // {hp:..} / {str:..} / {def:..}
    nlohmann::json effect_text;
    int cost_base = 2;
    int cost_multiplier = 2;
    int purchases = 0;         // how many times already bought (drives the doubling price)
    // Milestone 8: non-empty for a weapon/armor/talent sold here instead of a consumable --
    // an id into equipmentDefs_. buyStoreItem() branches on this instead of applying `effect`.
    std::string equipmentId;
    int liveCost() const { int c = cost_base; for (int i=1;i<=purchases;i++) c *= cost_multiplier; return c; }
};

// M9 (stair alignment): which tile a stage load should place the player on. Every floor's
// stairs_down is now forced to sit exactly where the previous floor's stairs_up landed (see
// docs/story/STAIR_ALIGNMENT.md), so arriving via a specific staircase has a real, correct tile
// to land on instead of always falling back to the floor's '@'/default spawn.
enum class StageArrival { Fresh, FromBelow, FromAbove };

class Game : public Trackable {
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
    void startDialogue(const std::string& npc);   // open an NPC dialogue (public for tests)
    void enterNode(const std::string& node);      // jump to a dialogue node (public for tests)
    void startCombat(const EnemyInst& e);
    // Verification hooks (debugStartNearestBattle .. debugWarpPlayer): the old web entry exposed
    // them to a page test harness. No host calls them now; a future test harness can re-expose them.
    // Start a fight with the nearest monster, to exercise the battle scene without walking the maze.
    bool debugStartNearestBattle();
    // Verification hook: equip an item directly by id,
    // bypassing the Store/Forge entirely, so a page harness can reach equipment (and whatever
    // actives it grants) that isn't purchasable/craftable through any content authored yet.
    // Silently does nothing for an unknown id, matching every other debug hook's fail-soft rule.
    bool debugEquip(const std::string& equipmentId);
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
    // S7 (equipment actives, first slice): the 4th battle action -- a no-op unless the currently
    // equipped gear grants one (equippedActives()) and it hasn't been spent this battle already.
    void battleTapActive();
    // inventory UI (9-grid, extendable)
    void toggleInventory();
    void invMoveSel(int dx, int dy);        // move the selection cursor
    bool invUseSelected();                 // use the highlighted item (returns true if used)
    void invDropSelected();                // discard the highlighted item
    bool inventoryOpen() const { return invOpen; }
    const std::vector<std::string>& inventory() const { return pl.inv; }
    int invSelection() const { return invSel; }
    // Milestone 7 bugfix: cs.won (the post-victory "press any key to continue" pause) must count
    // as a modal overlay too, not just cs.active -- otherwise the world underneath (movement,
    // NPC interact, the store icon, Tab/B shortcuts) keeps responding to input while the victory
    // screen is still up. See docs/progress_report/PROGRESS_REPORT.md's Milestone 7 log for the report this fixes.
    bool modalActive() const { return cs.active || cs.won || inDialogue || invOpen || storeOpen || storeUnlockDlg || stageSelectOpen_ || stairsConfirmOpen_ || title_.isOpen() || inGameMenuOpen_ || endingActive(); }  // any overlay open (combat/dialogue/inventory/store/stage-select/stairs-confirm/title/in-game menu/ending)
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
    bool stairsConfirmIsUp() const { return stairsConfirmIsUp_; }
    void confirmStageTransition();
    void cancelStageTransition();
    // Maze camera mode (see toms::Camera::Mode in camera.h). Applying a change re-targets the
    // camera immediately (no confirm dialog needed -- unlike language, this is a low-stakes,
    // instantly-visible, freely-reversible preference).
    int cameraModeIndex() const { return cam_.modeIndex(); }
    void setCameraModeIndex(int m);
    // In-game menu (walking-phase HUD gear icon): Save / Settings (language) / Back to Title.
    bool inGameMenuOpen() const { return inGameMenuOpen_; }
    void openInGameMenu();
    void inGameMenuMove(int delta);    // Up/Down or Left/Right
    void inGameMenuActivate();         // Enter/Space
    void inGameMenuBack();             // Esc: dialog->list, Settings->Main, Main->closed
    Player& player() { return pl; }
    IRenderer* renderer() { return ren; }   // for batch-metric inspection (demo)

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
    void newGame(int slot);
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
    // The same for a particle effect (toms_game --fx=<file>#<effect>): centred, a one-shot effect
    // starts again 0.5 s after it ends. The seed is the effect's, else 1, so screenshots repeat.
    bool playPreviewFx(const std::string& file, const std::string& effect, std::string& error);
    int previewFxLive() const { return previewFx_.liveCount(); }   // the --fx effect's live particles (HUD)
    int previewFxGpuEmitters() const { return previewFx_.gpuEmitters(); }
    int previewFxEmitters() const { return previewFx_.effect() ? (int)previewFx_.effect()->emitters.size() : 0; }
    // Particle emitters above this many particles run on the GPU (GameSettings::particleGpuThreshold;
    // `toms_game --fx-gpu-threshold=N` overrides it for this run without saving it).
    int particleGpuThreshold() const { return fxGpuThresholdOverride_ >= 0 ? fxGpuThresholdOverride_ : settings_.particleGpuThreshold; }
    void overrideParticleGpuThreshold(int n) { fxGpuThresholdOverride_ = n; }
    // The UI's sprite sheet: RCSS made from spriteAtlas_, which GameUi serves to RmlUi as
    // "_atlas.rcss" -- so the UI draws from the same packed texture as the map. Empty without a
    // prebuilt atlas. The revision changes whenever another atlas is loaded (art style switch).
    std::string uiSpritesheet() const;
    int spriteAtlasRevision() const { return spriteAtlasRevision_; }
    // Sprites the UI or the data (items, store) can ask for that the atlas lacks; empty = fine.
    std::vector<std::string> missingUiSprites() const;
    int chosenArtStyle() const { return toms::artStyleIndex(artStyles_, settings_.artStyle); }
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
    // things — see docs/progress_report/PROGRESS_REPORT.md's M2 log entry for why this needed checking before
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
    void resolveDefenseTap();
    void resolveEnemyClockFire();
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
    float skillEffectScale() const { return meta_.cycleIndex > 1 ? cyclesConfig_.skillEffectScale : 1.0f; }
    // M8 verification hook: read-only, matching runState()'s own "expose the meta save's public
    // facts for tests/harness probes" rule.
    int metaCycleIndex() const { return meta_.cycleIndex; }
    int effectiveAtk() const { int a=pl.atk,d=pl.def; toms::applyEquipmentStats(equipped_,equipmentDefs_,a,d); toms::applySkillEffects(skillDefs_, run_.skillsOwned(), a, d, skillEffectScale()); return a; }
    int effectiveDef() const { int a=pl.atk,d=pl.def; toms::applyEquipmentStats(equipped_,equipmentDefs_,a,d); toms::applySkillEffects(skillDefs_, run_.skillsOwned(), a, d, skillEffectScale()); return d; }
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
    void buildMenuUi(toms::UiMenu& out) const;                   // the in-game menu part of buildUiState
    // store system
    void loadStore(const std::string& assetDir);   // parse data/store.json
    // Milestone 8: the store's items are split into tabs of <=3 cards each. Returns the indices
    // into storeItems_ that belong to the current storeTab_ (0=potions,1=weapons,2=armor,3=talents).
    std::vector<int> storeTabIndices() const;
    void buyStoreItem(int idx);                      // purchase + apply effect (or toast if poor)

    IRenderer* ren = nullptr;        // the bgfx renderer, created in loadAssets
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
    toms::anim::AnimFile previewAnim_;        // playPreviewAnim
    toms::anim::AnimPlayer previewPlayer_;
    toms::anim::AtlasSet previewSet_;                               // its atlases, in lookup order
    std::vector<std::unique_ptr<toms::AtlasFile>> previewAtlases_;  // the ones that are not the game's
    std::vector<uint16_t> previewTextures_;                         // their pages (ren->loadTexture)
    std::vector<toms::anim::NodePose> previewPoses_;   // per-frame scratch
    std::vector<Quad> previewQuads_;
    std::vector<std::string> previewMissing_;          // sprites already reported missing
    void drawPreviewAnim();
    // The atlases a preview file names (relative to it), into `set`; a ".../atlas/game.atlas" is
    // the game's own loaded atlas. Loaded ones are kept in `owned` / `textures`.
    void loadPreviewAtlases(const std::string& file, const std::vector<toms::anim::AtlasRef>& refs, toms::anim::AtlasSet& set,
                            std::vector<std::unique_ptr<toms::AtlasFile>>& owned, std::vector<uint16_t>& textures);
    // Monster idle animations: assets/media/anim/monster_idle.anim (made in anim_editor), clip "idle_<enemy id>".
    // Every monster on the map plays its clip in a loop instead of standing still, each at its own phase.
    toms::anim::AnimFile idleAnim_;
    toms::anim::AtlasSet idleSet_;
    std::vector<std::unique_ptr<toms::AtlasFile>> idleAtlases_;
    std::vector<uint16_t> idleTextures_;
    std::vector<toms::anim::NodePose> idlePoses_;   // per-frame scratch
    std::vector<Quad> idleQuads_;
    double idleClockMs_ = 0;
    void loadIdleAnims();
    void loadDoorAnims();
    // Draws the monster's idle clip into the rect; false = no clip (or nothing drawn): draw the static sprite.
    bool drawIdleAnim(const std::string& enemyId, float x, float y, float w, float h, int tileX, int tileY);
    // A clip at time t placed on a map rect: its (0, 0) on the rect's bottom centre, scaled so a sprite of
    // spriteW x spriteH pixels fills the rect. False = nothing drawn.
    bool drawClipInRect(const toms::anim::Clip& clip, float t, const toms::anim::AtlasSet& set, float x, float y, float w, float h,
                        float spriteW, float spriteH);
    // Doors opening: assets/media/anim/door_open.anim (made in anim_editor), clip "door_open_<colour>", played once on
    // the door's tile while the tile is already floor (openDoor).
    toms::anim::AnimFile doorAnim_;
    toms::anim::AtlasSet doorSet_;
    std::vector<std::unique_ptr<toms::AtlasFile>> doorAtlases_;
    std::vector<uint16_t> doorTextures_;
    struct MapFx { const toms::anim::Clip* clip; int x, y; std::string sprite; float ms; };
    std::vector<MapFx> mapFx_;   // one-shot clips playing on the map (cleared when a floor loads)
    // A door the player walks into with its key: the key is used, the door opens (its clip plays), the tile becomes
    // floor and stays open on later visits (EntityStatus::Opened). The player stays where they were.
    void openDoor(int x, int y, char c);
    toms::fx::ParticleFile previewFxFile_;    // playPreviewFx
    toms::fx::EffectInstance previewFx_;
    toms::anim::AtlasSet previewFxSet_;
    std::vector<std::unique_ptr<toms::AtlasFile>> previewFxAtlases_;
    std::vector<uint16_t> previewFxTextures_;
    float previewFxIdle_ = 0;                         // seconds since a one-shot effect finished
    int fxGpuThresholdOverride_ = -1;                 // --fx-gpu-threshold (-1: the setting)
    // art styles (assets/media/styles/styles.json); style 0 = the original art
    std::vector<toms::ArtStyle> artStyles_{toms::ArtStyle{}};
    int loadedArtStyle_ = 0;                  // the one whose sprites were loaded at startup
    int spriteAtlasRevision_ = 0;             // +1 per atlas load (uiSpritesheet changed)
    // item definitions (id -> json from data/items.json)
    std::map<std::string, nlohmann::json> itemDefs;
    // inventory UI state
    bool invOpen = false;
    int invSel = 0;            // selected slot index
    // dialogue state
    bool inDialogue = false;
    int dlgSel = 0;                     // currently highlighted dialogue choice (gamepad nav)
    std::string dlgNpc;
    std::string dlgNode = "root";
    nlohmann::json dlgData;
    std::vector<DialogueChoice> dlgChoices;
    // enemy templates
    std::map<std::string, nlohmann::json> enemyTpl;
    // The battle scene's tuning, from data/battle.json (loadAssets). Without the file or a key, the defaults below.
    // A bar starts slow (v0) and speeds up to its top speed (vmax) on a cubic curve (power_bar.h); the two are scaled apart.
    float barSlowScale_ = 1.0f;                           // "bar_slow_speed_scale": multiplies the slow start (v0)
    float barFastScale_ = 1.0f;                           // "bar_fast_speed_scale": multiplies the top speed (vmax)
    int barCooldownMs_ = CombatState::kBarCooldownMs;     // "bar_cooldown_ms": the wait after a tap before a bar moves again
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
    enum class MainMenuRow { Save, Settings, Skills, Village, Forge, BackToTitle };
    std::vector<MainMenuRow> mainMenuOrder() const;
    void closeInGameMenu();                // closes the whole menu (any page/dialog)
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
    int playTimeSec_ = 0;     // this run's accumulated play time (shown in the Continue list)
    bool saveDirty_ = false;  // progress changed since the last slot write
    int saveFlushMs_ = 0;     // countdown used to throttle autosave (see update())
    static constexpr int kAutosaveIntervalMs = 3000;   // flush at most this often, when dirty
    void markProgressDirty() { saveDirty_ = true; }
    // Performs whatever the title phase asked for (see title_screen.h's TitleAction).
    void handleTitleAction(toms::TitleAction a);
    std::string stageDisplayName(const std::string& id) const;
    toms::RunSaveData runSaveFromState() const;
    // Applies a loaded slot to the live game state, then loads its stage.
    void applyLoadedRun(const toms::MetaSaveData& m, const toms::RunSaveData& r, int slot, int playTimeSec);
    float uiScale_ = 1.0f;    // setUiScale(): dialogue box size on phones
    TOMS_OBJECT(Game)
};

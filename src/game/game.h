// game.h — game logic: player, movement, auto combat, talking, stage flow, draw.
#pragma once
#include <vector>
#include <string>
#include <map>
#include <json.hpp>
#include "object.h"      // Trackable base: Player/EnemyInst/CombatState/Game are tracked
#include "render_iface.h"
#include "game_state.h"  // toms::GameState — see Game::currentState()
#include "save_system.h" // toms::MetaSaveData — see Game::meta_ (Milestone 3; disk persistence is Milestone 5's job)
#include "mission_system.h" // toms::MissionDefinition/MissionTracker — see Game::missionDefs_/missionTrackers_
#include "power_bar.h"       // toms::PowerBarParams/simulatePosition/... — see CombatState's Phase
#include "equipment_system.h" // toms::EquippedSet/EquipmentDefinition — see Game::equipped_/equipmentDefs_
#include "entity_status.h"   // toms::EntityStatus/entityStatusKey — see Game::entityStatus_
#include "stage.h"
#include "title_screen.h"    // toms::TitleScreen/TitleAction — the title phase (New Game/Continue/Settings)
#include "game_settings.h"   // toms::GameSettings — persisted preferences (language, slots)
#include "localization.h"    // toms::Locale — key -> localized string (data/text.json)
#include "font.h"        // runtime TTF -> atlas (stb_truetype), replaces offline font_atlas.png
#include <stb_truetype.h> // complete stbtt_fontinfo for ~Font (unique_ptr member)
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

// Milestone 6: a battle round is now player-timed via the Attack/Defense Power Bar
// (FIGHT_SCENE_DESIGN.md §6's round pseudocode), not an automatic 700ms timer. One round:
// AwaitAttackPress -> AttackCharging -> AttackResultPause -> (enemy still alive?)
// AwaitDefensePress -> DefenseCharging -> DefenseResultPause -> back to AwaitAttackPress.
// If the Attack Bar's hit kills the enemy, the round ends there (no Defense Bar that round) --
// matching "the enemy retaliates after every hit except the killing blow."
struct CombatState : public Trackable {
    enum class Phase { AwaitAttackPress, AttackCharging, AttackResultPause, AwaitDefensePress, DefenseCharging, DefenseResultPause };
    EnemyInst enemy;
    int playerHP, enemyHP;
    int round = 0;
    bool active = false;
    std::string log;          // last exchange text
    bool won = false;

    Phase phase = Phase::AwaitAttackPress;
    bool charging = false;     // true while the action button is actively held for the current bar
    int chargeMs = 0;          // accumulated hold duration for the current charge (ms)
    float lastPosition = 0.0f; // marker position at the last release (for rendering the frozen bar)
    float lastPower = 0.0f;    // marker power % at the last release (for the result text)
    int lastDamage = 0;        // damage dealt/taken at the last release (for the result text)
    int resultPauseMs = 0;     // countdown after a release before the next bar starts (readability beat)
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

class Game : public Trackable {
public:
    bool loadAssets(const std::string& assetDir);
    void loadStage(const std::string& id);
    void update(int dtMs);                 // advances combat timer etc.
    void draw();                          // render current frame
    void drawGamepad();                   // on-canvas touch controls (web build)
    void handleTouch(float px, float py, int phase); // px,py in 1024x768 buffer space; phase 0=down 1=repeat 2=up
    void saveFrame(const std::string& path);
    // input (scripted for headless)
    void movePlayer(int dx, int dy);
    // Milestone 9 polish: "keep moving while held" instead of exactly one tile per key
    // press/tap -- classic dungeon-crawler feel. Call every frame with the currently-held
    // direction on each axis (dir in {-1,0,1}, 0 = not held): main.cpp does this for the
    // keyboard (level state, not the edge-triggered up/down/left/rightPressed used for
    // menu/inventory-cursor navigation, which should NOT auto-repeat), handleTouch() does it
    // for the virtual/touch d-pad (0 on press-release). X and Y repeat independently -- both
    // held at once keeps moving diagonally, same as the old one-tap version allowed. The
    // actual repeat timer lives in update(); this only registers what's currently held and
    // fires the immediate first step on a fresh press/direction-change.
    void setMoveHeldX(int dir);
    void setMoveHeldY(int dir);
    void stopMoveHeld() { setMoveHeldX(0); setMoveHeldY(0); }
    void interact();                       // talk to NPC / trigger dialogue on current cell
    void chooseDialogue(int idx);          // pick a dialogue choice
    // Bugfix: main.cpp never had any keyboard binding that moved dlgSel at all -- with
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
    // Verification hook (web build, see jsDebugBattle in emscripten_main.cpp): start a fight with
    // the nearest monster so a page harness can exercise the battle scene without walking the maze.
    bool debugStartNearestBattle();
    // Milestone 6: press-and-hold Power Bar input (see CombatState::Phase). The caller (main.cpp)
    // calls these on the action button's press/release edges; both are safe no-ops when combat
    // isn't active or a charge isn't currently allowed (e.g. mid-ResultPause), so main.cpp does
    // not need to track battle phase itself.
    void battleChargeStart();
    void battleChargeRelease();
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
    // screen is still up. See docs/PROGRESS_REPORT.md's Milestone 7 log for the report this fixes.
    bool modalActive() const { return cs.active || cs.won || inDialogue || invOpen || storeOpen || storeUnlockDlg || stageSelectOpen_ || stairsConfirmOpen_ || title_.isOpen() || inGameMenuOpen_; }  // any overlay open (combat/dialogue/inventory/store/stage-select/stairs-confirm/title/in-game menu)
    bool combatWon() const { return cs.won; }
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
    // input for the store (mouse: pixel coords; keyboard: vk key code / ascii)
    void storeClick(float x, float y);     // click on the store icon or inside the store UI
    void storeKey(int key);                // keyboard nav/confirm inside the store UI
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
    // Maze camera mode (see cameraMode_'s declaration for what each value does). Applying a
    // change re-targets the camera immediately (no confirm dialog needed -- unlike language,
    // this is a low-stakes, instantly-visible, freely-reversible preference).
    int cameraModeIndex() const { return (int)cameraMode_; }
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
    // page/selection state machine and docs/TITLE_PHASE.md for the flow.
    bool titleOpen() const { return title_.isOpen(); }
    const toms::TitleScreen& title() const { return title_; }
    // Input, in the same shape the other overlays use: a direction move, a confirm, a cancel,
    // and a design-space (1024x768) tap. Each routes to the current title page.
    void titleMove(int dx, int dy);
    void titleConfirm();
    void titleCancel();
    void titleClick(float x, float y);
    void drawTitleScreen();               // draws the title overlay; no-op unless titleOpen()
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
    int activeSlot() const { return activeSlot_; }
    int playTimeSec() const { return playTimeSec_; }
    const toms::Locale& locale() const { return locale_; }
    const toms::GameSettings& settings() const { return settings_; }
    toms::GameSettings& mutableSettings() { return settings_; }
    // Derived, read-only classification of "what screen/mode is the game in right now",
    // computed from the existing modal flags — see docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md
    // §4 and docs/IMPLEMENTATION_ROADMAP.md Milestone 1. Does not change control flow; the
    // states with no real screen behind them yet (StageSelect, Paused, ...) are simply never
    // returned today.
    toms::GameState currentState() const;
#ifndef __EMSCRIPTEN__
    // Milestone 2 dev-only debug overlay (Dear ImGui: stat sliders, node-filter toggle, last
    // combat log line). Desktop/Vulkan only — the caller (main.cpp) decides when to show it;
    // this just builds the ImGui:: window content for the current frame.
    void drawDebugOverlay();
    // UI settings: applies uiFontScale_ to ImGui's global font scale. Call once per frame,
    // right after ImGui's NewFrame(), so it's in effect before anything else draws that frame.
    void applyUiSettings();
    // Milestone 2 styling spike (a prerequisite the roadmap flags before Milestone 5 commits real
    // screens to the hybrid UI approach): proves a transparent, undecorated ImGui window laid
    // exactly over a scene-graph-drawn backdrop rect reads as one panel, not two overlapping
    // things — see docs/PROGRESS_REPORT.md's M2 log entry for why this needed checking before
    // any real screen was built on the assumption. Desktop/Vulkan only, dev-only, F2 to toggle.
    void drawStylingSpike();
    void setStylingSpikeVisible(bool v) { stylingSpikeVisible_ = v; }
    // Milestone 5: transient toast notifications (level-up, daily mission available, ...),
    // driven by pushNotification() (private, called from the real gameplay events that trigger
    // one). Always drawn when active, not gated behind a dev toggle like F1/F2.
    void drawNotifications();
    // Milestone 5: the Stage Select hub screen content -- call when stageSelectOpen() is true.
    void drawStageSelect();
#endif
    // DEBUG: hide individual overlay subsystems to bisect stray-sprite bugs.
    // bit 1 = combat overlay, bit 2 = dialogue overlay, bit 4 = inventory UI.
    int hideMask = 0;
    // text drawing used by TextNode (render-bound text in the scene graph)
    void drawTextPublic(const std::string& s, float x, float y, float sz, const float* t);
    float measureText(const std::string& s, float size) const;   // width in px (for centering)
    CombatState& combat() { return cs; }
    Stage& stage() { return st; }
    const nlohmann::json& enemyTemplate(const std::string& id) const { return enemyTpl.at(id); }
private:
    void drawText(const std::string& s, float x, float y, float size, const float tint[4]);
    void drawBar(float x, float y, float w, float h, float frac, const float col[4]);
    Quad spriteQuad(float x, float y, float w, float h, int layer, const float tint[4]);
    void spriteUV(int layer, float uv[4]) const;
    int spriteLayer(const std::string& id) const; // index into sprite grid
    void applyItem(const std::string& id);
    // Milestone 6: Power Bar round resolution (replaces the old auto-attack resolveCombatRound).
    // resolveAttackRelease/resolveDefenseRelease apply a released bar's damage and call
    // finishCombatWin/finishCombatLose when that ends the fight; drawPowerBar renders the
    // currently-active bar (live while charging, frozen at lastPosition during ResultPause).
    void resolveAttackRelease(float heldSeconds);
    void resolveDefenseRelease(float heldSeconds);
    // Builds the EnemyInst for a monster tile and starts the fight (or its dialogue gate); shared
    // by the bump-to-fight path in movePlayer() and the harness hook debugStartNearestBattle().
    void engageMonster(const Entity& e);
    void finishCombatWin();
    void finishCombatLose();
    void drawPowerBar(float x, float y, float w, float h, const toms::PowerBarParams& bar, float position);
    void drawInventory();
    int spriteForItem(const std::string& id) const;
    std::string itemName(const std::string& id) const;
    std::string itemDesc(const std::string& id) const;
    // store system
    void loadStore(const std::string& assetDir);   // parse data/store.json
    void drawStoreIcon();                            // HUD icon (clickable after unlock)
    void drawStoreUnlockDialog();                   // "store unlocked" popup (confirm button)
    void drawStoreUI();                              // the shop overlay (card grid)
    void drawStoreToast();                           // transient "gold not enough" toast
    // compute the on-screen rects of store UI elements (icon / cards / buttons) for hit-testing
    void storeCardRects(std::vector<float>& rects, int n) const;  // 4 floats per card: x,y,w,h
    // Milestone 8: the store outgrew a single row of cards once equipment joined the 3 potions
    // (12 items total vs. the layout's real capacity of ~3 per row) -- rather than reworking the
    // existing per-card pixel layout (risky to get right without visual verification), items are
    // split into tabs of <=3 each, reusing the untouched card layout per tab. Returns the indices
    // into storeItems_ that belong to the current storeTab_ (0=potions,1=weapons,2=armor,3=talents).
    std::vector<int> storeTabIndices() const;
    void buyStoreItem(int idx);                      // purchase + apply effect (or toast if poor)
    // shared full-screen focus splash (black, alpha 0.5) used by combat / dialogue /
    // inventory so the player focuses on the active scene. Also gates background
    // input (movePlayer/interact) while any modal overlay is open.
    void drawFocusSplash();
    void drawStoreSplash();   // store modal: full-screen black splash at alpha 0.5 (topmost layer)

    IRenderer* ren = nullptr;        // backend chosen in loadAssets (Vulkan / WebGL)
    Player pl;
    Stage st;
    CombatState cs;
    // sprite layer registry (order matches loadAssets)
    std::vector<std::string> spriteIds;
    std::map<std::string,int> idToLayer;
    int spriteGridCols = 9;
    // item definitions (id -> json from data/items.json)
    std::map<std::string, nlohmann::json> itemDefs;
    // inventory UI state
    bool invOpen = false;
    int invSel = 0;            // selected slot index
    bool gpOn = true;         // on-canvas gamepad: ALWAYS visible by default; toggle button hides it
    // font atlas map: codepoint -> uv rect
    std::map<uint32_t, std::array<float,4>> fontMap;
    std::shared_ptr<Font> font_;   // runtime TTF atlas (replaces font_atlas.png)
    int fontCols = 32, fontCell = 32, fontW = 0, fontH = 0;
    // dialogue state
    bool inDialogue = false;
    int dlgSel = 0;                     // currently highlighted dialogue choice (gamepad nav)
    std::string dlgNpc;
    std::string dlgNode = "root";
    nlohmann::json dlgData;
    std::vector<DialogueChoice> dlgChoices;
    // enemy templates
    std::map<std::string, nlohmann::json> enemyTpl;
    // current stage id
    std::string curStage;
    std::string dataDir;
    // Milestone 3: story flags + main-story beat, evaluated by GameConditionContext (game.cpp)
    // for door/key gating and dialogue `requires` gating. In-memory only this milestone — reading
    // and writing this to an actual save file on disk is Milestone 5's job (Stage Select's
    // "Continue"), per docs/IMPLEMENTATION_ROADMAP.md.
    toms::MetaSaveData meta_;
    // Milestone 7: per-tile enemy/item status (Milestone 3's Entity Status System, wired in for
    // real here) -- a defeated monster or collected item stays cleared when the floor is
    // reloaded (stairs, or Milestone 5's Stage Select hub), matching the owner's decision that
    // cleared floors don't repopulate. Keyed by entityStatusKey(stageId, x, y); in-memory only
    // for now, same "persistence is later work" caveat as meta_ and missionTrackers_. Doors are
    // deliberately NOT covered by this (a separate, pre-existing, still-open behavior: reusing
    // an already-unlocked door tile currently consumes another key every time) -- out of scope
    // for this fix, which is scoped to what the owner's decision was actually about.
    std::map<std::string, std::string> entityStatus_;
    // Milestone 6: equipment layer over the Power Bar/damage formulas (MAIN_BATTLE_SCENE_DESIGN.md
    // §4). equipmentDefs_ is intentionally empty until Milestone 8 loads data/equipment.json --
    // equipped_ starts fully empty (nothing equipped), so effectiveAttackBar/effectiveDefenseBar/
    // effectiveMaxMult all fall back to baseline geometry/2.0x until real items exist to equip.
    toms::EquippedSet equipped_;
    std::map<std::string, toms::EquipmentDefinition> equipmentDefs_;
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
    void wireMissionEvents();   // subscribes mission-progress handlers to the global EventBus once
    // Milestone 5: daily-mission reset check (architecture-doc §8.3), run once per session start
    // (see loadAssets). Inert today since missionDefs_ is empty until Milestone 8, but pushes a
    // real notification the moment a tracked daily mission actually rolls to Available.
    void rollDailyMissions();
    // Milestone 5: transient toast queue -- {message, remaining_ms}, decremented in update().
    std::vector<std::pair<std::string, int>> notifications_;
    void pushNotification(const std::string& msg);
    // UI setting: multiplier applied to ImGui's base font size (applyUiSettings(), declared
    // above under the desktop/ImGui guard). Default is bigger than ImGui's own 1.0x default,
    // since the un-scaled size read as too small against this game's other text. Adjustable via
    // the slider in drawStageSelect(). In-memory only for now -- no settings file exists yet, so
    // this resets to default each launch (same "persistence is later work" pattern as meta_).
    float uiFontScale_ = 1.5f;
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
    int stairsConfirmYesRect_[4] = {0,0,0,0};
    int stairsConfirmNoRect_[4] = {0,0,0,0};
    void requestStageTransition(const std::string& target, bool isUp);
    void drawStairsConfirmDialog();
    // Milestone 9 polish: per-axis "held direction" state for setMoveHeldX/Y (declared
    // above) -- holdMs/repeating are ticked in update(). kMoveInitialDelayMs is the pause
    // after the immediate first step before repeating starts (long enough that a quick tap
    // never double-steps); kMoveRepeatMs is the steady repeat rate once it's going.
    struct MoveHoldAxis { int dir = 0; int holdMs = 0; bool repeating = false; };
    MoveHoldAxis moveHoldX_, moveHoldY_;
    static constexpr int kMoveInitialDelayMs = 220;
    static constexpr int kMoveRepeatMs = 110;
    // M2 styling spike backdrop state. Declared unguarded (unlike drawStylingSpike()/
    // setStylingSpikeVisible(), which are desktop/ImGui-only) so Game::draw() — shared between
    // the desktop and web builds — can call drawStylingSpikeBackdrop() unconditionally; it's a
    // correct no-op on web, where stylingSpikeVisible_ can never be set true (nothing calls
    // setStylingSpikeVisible() there).
    bool stylingSpikeVisible_ = false;
    float stylingSpikeRect_[4] = {0, 0, 0, 0};   // x,y,w,h — set by drawStylingSpike(), read by the backdrop
    void drawStylingSpikeBackdrop();
    int totalStages = 10;   // highest stage index (derived from data/stages at loadStage)

    // ---- maze camera ----
    // Stage grids range from 19x16 to 34x31 tiles (data/stages/*.json) -- too big to keep
    // shrinking tile size to fit the whole grid on screen (that's what made the maze illegible/
    // hard to tap on mobile). A fixed tile size + scrolling viewport fixes that at any grid size.
    // Follow: the viewport pans to keep the player centered (clamped to the grid edges).
    // Rooms: the grid is divided into fixed viewport-sized sections; the camera slides to
    // whichever section currently contains the player, only when they cross into a new one.
    enum class CameraMode { Follow = 0, Rooms = 1 };
    CameraMode cameraMode_ = CameraMode::Follow;
    // How many tile columns are visible across the (always 1024px-wide) design canvas; tile
    // size and row count are both derived from this (see cameraViewportTiles()), so it's a
    // single "how zoomed in is the camera" knob. Adjustable live from the F1 debug overlay
    // (real-time, for testing what actually fits on a small screen) and persisted like any
    // other setting.
    int viewCols_ = 13;
    float camX_ = 0, camY_ = 0;                 // current viewport origin, in tile units (floats
                                                 // so a pan/slide can be mid-tile between frames)
    float camTargetX_ = 0, camTargetY_ = 0;     // where camX_/camY_ are easing toward
    // Derives tile size (ts) and viewport size in tiles (cols/rows) from viewCols_ + the
    // (always 1024x768) design canvas -- the one place this math happens, shared by
    // update()'s camera targeting and draw()'s actual rendering so they can never disagree.
    void cameraViewportTiles(float& ts, int& cols, int& rows) const;
    // Recomputes camTargetX_/Y_ from pl.x/y + cameraMode_ (clamped to the stage's edges).
    void updateCameraTarget();
    // Jumps the camera straight to its target (no pan) -- called right after loadStage() so a
    // floor change never visibly scrolls in from the previous floor's camera position.
    void snapCamera();
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
    int storeIconRect[4] = {0,0,0,0}; // on-screen rect of the HUD store icon (for hit-test)
    // Raw json (string or {code:text}) from store.json, resolved via Locale::field() at draw
    // time -- loadStore() runs before the title phase sets locale_ to the player's saved
    // language, so resolving here (instead of once at load) is what makes it show correctly.
    nlohmann::json storeTitle_ = "道具商店";
    int storeUnlockBtnRect_[4] = {0,0,0,0};   // unlock dialog confirm button rect
    std::vector<float> storeBtnRects_;        // per-card buy-button rects (4 floats each)
    std::vector<float> storeTabRects_;        // Milestone 8: per-tab button rects (4 floats each)
    int storeCloseRect_[4] = {0,0,0,0};       // store close button rect

    // ---- in-game menu (walking-phase HUD gear icon): Save / Settings (language) / Back to
    // Title. Built directly as Game state (not a separate testable class, unlike TitleScreen)
    // to match how every other in-game modal (store/inventory/stairs-confirm) already works in
    // this file -- there's no headless-test need here the way the title phase has.
    bool inGameMenuOpen_ = false;
    enum class InGameMenuPage { Main, Settings };
    InGameMenuPage inGameMenuPage_ = InGameMenuPage::Main;
    int inGameMenuSel_ = 0;               // highlighted row on whichever page (keyboard nav)
    bool inGameLangConfirmOpen_ = false;  // "switch to XXX?" sub-dialog, mirrors the title's own
    int inGameLangConfirmIdx_ = 0;
    bool inGameLangConfirmYes_ = true;
    int menuIconRect_[4] = {0,0,0,0};         // HUD gear button (opens the menu)
    int igmSaveRect_[4] = {0,0,0,0};
    int igmSettingsRect_[4] = {0,0,0,0};
    int igmBackToTitleRect_[4] = {0,0,0,0};
    int igmCloseRect_[4] = {0,0,0,0};
    std::vector<float> igmLangRowRects_;      // per-language rects on the Settings sub-page
    int igmCameraRowRect_[4] = {0,0,0,0};     // the camera-mode toggle row (always last)
    int igmBackRect_[4] = {0,0,0,0};          // Settings sub-page's own Back-to-Main button
    int igmLangYesRect_[4] = {0,0,0,0};
    int igmLangNoRect_[4] = {0,0,0,0};
    void closeInGameMenu();                // closes the whole menu (any page/dialog)
    void inGameMenuClick(float x, float y);
    void drawMenuIcon();
    void drawInGameMenu();
    // Saves, tears down transient modal/run-in-progress state, and reopens the title (the
    // inverse of newGame()/applyLoadedRun() -- see their resets for what this mirrors).
    void returnToTitle();

    // backpack/inventory overlay hit-test rects (rebuilt each frame in drawInventory)
    std::vector<float> invCardRects_;         // per-item card rects (4 floats each)
    int invUseRect_[4] = {0,0,0,0};
    int invDropRect_[4] = {0,0,0,0};
    int invCloseRect_[4] = {0,0,0,0};
    // Battle scene: the on-canvas Power Bar action button (rebuilt each frame in draw()). It is
    // an affordance plus an exact target for clients without a keyboard (web/touch) -- handleTouch
    // treats the whole battle scene as the same press-and-hold surface, so this only decides what
    // gets highlighted, never whether input works at all.
    int combatBtnRect_[4] = {0,0,0,0};
    // Title screen animation clock (ms, advanced in update()). Drives the pulsing selection
    // highlight / sliding cursor so the title never looks like a static (crashed) frame.
    float titleAnimMs_ = 0.0f;
    // miniaudio backs both desktop (native device backends) and the browser
    // build (Web Audio via Emscripten) behind this one interface -- no-op
    // when no audio device/context is available (e.g. headless CI).
    Audio audio;

    // ---- Title phase state (see the public title block above) ----
    toms::TitleScreen title_;
    toms::GameSettings settings_;
    toms::Locale locale_;
    // Rebuilt every drawTitleScreen() and read back by titleClick(), so a tap always lands on
    // the row that was actually drawn (one layout function, two consumers).
    toms::TitleLayout titleLayout_;
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
    // One title-row button: framed panel + label + optional sub-label, highlighted when selected.
    // `pulse` (0..1, from titleAnimMs_) animates the selected row so a fully loaded title screen
    // never looks like a frozen/crashed frame.
    void drawTitleButton(const toms::TitleRow& r, const std::string& label,
                         const std::string& sub, bool selected, const float accent[4], float pulse);
    // The "start a new game in this (empty) slot?" prompt, drawn over the Continue page.
    void drawTitleConfirmDialog();
    // The "switch to XXX language?" prompt, drawn over the Settings page.
    void drawLanguageConfirmDialog();
    TOMS_OBJECT(Game)
};

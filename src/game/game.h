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
#include "stage.h"
#include "font.h"        // runtime TTF -> atlas (stb_truetype), replaces offline font_atlas.png
#include <stb_truetype.h> // complete stbtt_fontinfo for ~Font (unique_ptr member)
#ifndef __EMSCRIPTEN__
#include "Audio.h"      // desktop SFX (miniaudio); excluded from the browser build
#endif

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

struct CombatState : public Trackable {
    EnemyInst enemy;
    int playerHP, enemyHP;
    int round = 0;
    bool active = false;
    int ticks = 0;            // ms accumulator
    std::string log;          // last exchange text
    bool won = false;
    TOMS_OBJECT(CombatState)
};

// A store item, parsed from data/store.json. cost = cost_base * cost_multiplier^purchases.
struct StoreItemDef {
    std::string id;
    std::string name;
    std::string sprite;       // sprite id (into the atlas) for the icon
    std::string icon_path;    // original asset path stored in json
    std::string desc;
    nlohmann::json effect;     // {hp:..} / {str:..} / {def:..}
    std::string effect_text;
    int cost_base = 2;
    int cost_multiplier = 2;
    int purchases = 0;         // how many times already bought (drives the doubling price)
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
    void interact();                       // talk to NPC / trigger dialogue on current cell
    void chooseDialogue(int idx);          // pick a dialogue choice
    void startDialogue(const std::string& npc);   // open an NPC dialogue (public for tests)
    void enterNode(const std::string& node);      // jump to a dialogue node (public for tests)
    void startCombat(const EnemyInst& e);
    // inventory UI (9-grid, extendable)
    void toggleInventory();
    void invMoveSel(int dx, int dy);        // move the selection cursor
    bool invUseSelected();                 // use the highlighted item (returns true if used)
    void invDropSelected();                // discard the highlighted item
    bool inventoryOpen() const { return invOpen; }
    const std::vector<std::string>& inventory() const { return pl.inv; }
    int invSelection() const { return invSel; }
    bool modalActive() const { return cs.active || inDialogue || invOpen || storeOpen || storeUnlockDlg || stageSelectOpen_; }  // any overlay open (combat/dialogue/inventory/store/stage-select)
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
    Player& player() { return pl; }
    IRenderer* renderer() { return ren; }   // for batch-metric inspection (demo)
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
    void resolveCombatRound();
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
    void storeCardRects(std::vector<float>& rects) const;  // 4 floats per card: x,y,w,h
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
    void runDialogueAction(const nlohmann::json& action);
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
    struct StageInfo { std::string id; std::string name; int index = 0; };
    std::vector<StageInfo> stageList_;
    bool stageListLoaded_ = false;
    void ensureStageListLoaded();
    // M2 styling spike backdrop state. Declared unguarded (unlike drawStylingSpike()/
    // setStylingSpikeVisible(), which are desktop/ImGui-only) so Game::draw() — shared between
    // the desktop and web builds — can call drawStylingSpikeBackdrop() unconditionally; it's a
    // correct no-op on web, where stylingSpikeVisible_ can never be set true (nothing calls
    // setStylingSpikeVisible() there).
    bool stylingSpikeVisible_ = false;
    float stylingSpikeRect_[4] = {0, 0, 0, 0};   // x,y,w,h — set by drawStylingSpike(), read by the backdrop
    void drawStylingSpikeBackdrop();
    int totalStages = 10;   // highest stage index (derived from data/stages at loadStage)
    // store system state
    std::vector<StoreItemDef> storeItems_;
    int storeUnlockStage_ = 3;   // stage index at which the shop unlocks (from store.json)
    bool storeUnlocked_ = false;
    bool storeUnlockDlg = false; // "shop unlocked!" popup showing (with confirm button)
    bool storeOpen = false;      // store overlay open
    int storeSel_ = 0;           // selected card index (keyboard nav)
    std::string toastMsg_;        // transient message ("金錢不足")
    int toastTimer_ = 0;         // ms remaining for toast
    int shakeTimer_ = 0;         // ms remaining for "not enough gold" shake
    int storeIconRect[4] = {0,0,0,0}; // on-screen rect of the HUD store icon (for hit-test)
    std::string storeTitle_ = "道具商店";     // title from store.json
    int storeUnlockBtnRect_[4] = {0,0,0,0};   // unlock dialog confirm button rect
    std::vector<float> storeBtnRects_;        // per-card buy-button rects (4 floats each)
    int storeCloseRect_[4] = {0,0,0,0};       // store close button rect
    // backpack/inventory overlay hit-test rects (rebuilt each frame in drawInventory)
    std::vector<float> invCardRects_;         // per-item card rects (4 floats each)
    int invUseRect_[4] = {0,0,0,0};
    int invDropRect_[4] = {0,0,0,0};
    int invCloseRect_[4] = {0,0,0,0};
#ifndef __EMSCRIPTEN__
    Audio audio;           // SFX subsystem (no-op when no audio device)
#else
    // Browser build: no audio subsystem (kept isolated from desktop/Linux).
    struct AudioStub : public Trackable {
        void init(const std::string&) {} void play(const std::string&) {}
        TOMS_OBJECT(AudioStub)
    } audio;
#endif
    TOMS_OBJECT(Game)
};

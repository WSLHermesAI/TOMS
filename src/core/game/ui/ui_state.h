#pragma once
// ui_state.h -- everything the game's UI shows, as plain data.
//
// Game::buildUiState() fills one UiState per frame from the game's own state (all text already
// translated), and the host binds it to the RmlUi documents in assets/media/ui/*.rml (data model
// "game", see src/game/src/game_ui.cpp). Buttons in those documents call Game::uiEvent(name, arg).
// So Game keeps every rule and every string, the .rml/.rcss files own every pixel, and this
// header is the contract between them. No RmlUi (or renderer) types here: the core code stays
// free of UI-library dependencies.
//
// Naming: members are snake_case because they are the names used inside the .rml files
// ({{hud.hp_text}}, data-if="battle.active", ...).
#include <string>
#include <vector>

namespace toms {

// One selectable row / button in a list (title pages, menus, dialogue choices, stage select).
struct UiRow {
    std::string label;
    std::string sub;          // second, dimmer line ("" = none)
    bool selected = false;    // keyboard cursor / hover
    bool enabled = true;      // false = shown dimmed (locked skill, unaffordable recipe, ...)
    std::string section;      // a heading drawn above this row ("" = none): Settings' Language / Art style
};

// A world-anchored name plate over a boss or roaming monster (design-space position).
struct UiBanner {
    std::string text;
    float x = 0, y = 0;       // centre of the plate's bottom edge
};

struct UiTitle {
    bool visible = false;
    bool scene = false;                // a 3D scene is drawn behind the title (the host's TitleScene): see-through body
    std::string title, subtitle, hint;
    int page = 0;                      // 0 menu, 1 continue (save slots), 2 settings (languages)
    std::string header, subheader;     // page heading ("" on the menu page)
    std::vector<UiRow> rows;           // the current page's rows
    bool show_back = false;
    std::string back_label;
    bool show_empty_hint = false;      // continue page with no saves at all
    std::string empty_hint;
    // "Start a new game in this slot?" / "Switch to <language>?"
    bool confirm_open = false;
    std::string confirm_question, confirm_body, yes_label, no_label;
    bool confirm_yes = true;           // which answer the keyboard cursor is on
};

struct UiHud {
    bool visible = false;              // the walking scene (map) is on screen
    std::string title_line;            // "魔法塔 — <floor> (n/70)"
    float hp_pct = 0;                  // 0..100
    std::string hp_text, stats_line, res_line, footer;
    // The walking scene's one button, ≡: it opens the player menu (docs/20_PLAYER_MENU.md).
    std::string menu_label;
    bool menu_news = false;            // a dot on it: something new inside (store, gear, events, skill points)
    bool chapter_card = false;
    std::string chapter_title, chapter_sub;
    float chapter_alpha = 0;           // 0..1, fades out
    std::vector<UiBanner> banners;
};

struct UiPowerBar {                    // a Battle v2 timing bar; zones and marker in percent
    std::string prompt, button_label;
    bool cooling = false;
    float red_l = 0, blue_l = 0, green = 0, blue_r = 0, red_r = 0;   // zone widths (sum 100)
    float marker = 0;                  // 0..100
};

struct UiBattle {
    bool visible = false;
    bool active = false;               // fighting (false: won, showing "tap to continue")
    std::string title, player_sprite, enemy_sprite;   // sprite names (_atlas.rcss)
    float player_hp_pct = 0, enemy_hp_pct = 0;
    std::string player_hp_text, enemy_hp_text, log, continue_hint;
    std::string clock_label;
    float clock_pct = 0;
    UiPowerBar attack, defense;
    std::string shield_text;
    bool shield_armed = false;
    bool crit_armed = false;
    std::string crit_hint;
    std::string super_label;
    std::vector<int> super_dots;       // 1 = charged
    bool super_ready = false;
    std::string super_button;
    bool active_ready = false;
    std::string active_button;
};

struct UiDialogue {
    bool visible = false;
    std::string text;
    std::vector<UiRow> choices;
    float scale = 1.0f;                // Game::setUiScale (phones)
    float side = 40.0f;                // the box's left/right margin in design px, so that scaled it still fits the screen
};

// ---- the player menu (player.rml): Status / Equipment / Items / Events / System ----
struct UiTab {                         // a tab, or a filter chip
    std::string label;
    bool active = false;
    bool news = false;                 // a dot: something new on it
};

struct UiCell {                        // one cell of a 3-per-row grid (Items, Equipment)
    std::string icon, name, badge;     // icon: a sprite name; badge: "x2" / ""
    bool selected = false, worn = false, locked = false;   // locked: an important item (cannot be dropped)
};

struct UiLine {                        // "label  value" (and, comparing gear, "-> extra")
    std::string label, value, extra;
    int tone = 0;                      // the comparison: 1 better, -1 worse, 0 the same / not compared
};

struct UiButton {
    std::string label;
    bool enabled = true;
};

struct UiPlayer {
    bool visible = false;
    int tab = 0;                       // 0 status, 1 equipment, 2 items, 3 events, 4 system
    std::vector<UiTab> tabs;
    std::string hint, close_label;
    // Status
    std::string level, hp_text, exp_text;
    float hp_pct = 0, exp_pct = 0;
    std::string combat_title, attr_title, attr_desc;
    std::vector<UiLine> combat;        // ATK, DEF, Gold, Keys, Floor, Chapter
    std::vector<UiLine> more;          // Weapon, Armor, Skills, Memory shards, State of mind
    std::vector<UiRow> attrs;          // label "STR  力量", sub = the value
    // Equipment and Items: filter chips, the grid, the selected entry
    std::vector<UiTab> filters;
    std::string worn_title;
    std::vector<UiLine> worn;          // Equipment: what each slot wears (click = that slot)
    std::vector<UiCell> cells;
    std::string empty_text;            // the grid is empty
    bool detail = false;               // an entry is selected
    std::string select_hint;           // ... and when none is
    std::string d_icon, d_name, d_desc, d_note;
    std::vector<UiLine> d_lines;       // Effect, Price, You have
    bool d_compare = false;            // gear: compared with what is worn now
    std::string d_now_label, d_with_label;
    std::vector<UiLine> d_compare_lines;
    std::vector<UiButton> d_buttons;   // Use / Equip / Unequip / Drop
    // "Drop <item>?"
    bool confirm_open = false;
    std::string confirm_question, yes_label, no_label;
    bool confirm_yes = false;
    // Events (its own lists: every tab's data is there every frame, so a hidden tab never reads a
    // list that another tab emptied)
    std::vector<UiTab> ev_filters;     // Unfinished (n) / All (n)
    std::vector<UiRow> ev_rows;        // label: the mark and the title; sub: "done"; enabled=false: finished (greyed)
    bool ev_detail = false;
    std::string ev_empty, ev_hint;
    std::string ev_kind, ev_title, ev_desc, ev_connected_title;
    std::vector<std::string> ev_connected;
    // System: its rows and pages are menu.* (UiMenu)
    std::string sys_hint;
};

struct UiStoreTab { std::string label; bool active = false; };
struct UiStoreItem {
    std::string icon, name, desc, effect, status, price;   // icon: a sprite name (_atlas.rcss)
    bool selected = false, equipped = false;
};

struct UiStore {
    bool visible = false;
    std::string title, hint, gold_label, close_label, effect_label, price_label, buy_label;
    int gold = 0;
    std::vector<UiStoreTab> tabs;
    std::vector<UiStoreItem> items;
    // "Store unlocked!" popup (shown once, when the store unlocks)
    bool unlocked_dialog = false;
    std::string unlocked_title, unlocked_body, unlocked_ok;
};

struct UiMenu {                        // the player menu's System tab (the old in-game menu)
    bool visible = false;
    int page = 0;                      // 0 main, 1 settings, 2 skills, 3 forge, 4 village
    std::vector<UiRow> main_rows;      // the left list: Store, Skills, ... Back to title (always)
    std::string header, subheader;     // the open page (page != 0), shown on the right
    std::vector<UiRow> rows;           // its rows
    std::string close_label;           // main page: Close; sub-pages: Back
    bool confirm_open = false;         // "Switch to <language>?" / "Switch the art style to ...?"
    std::string confirm_question, confirm_body, yes_label, no_label;
    bool confirm_yes = true;
};

struct UiStairs {
    bool visible = false;
    std::string question, yes_label, no_label;
};

struct UiStageSelect {
    bool visible = false;
    std::string title, hint, close_label;
    std::vector<UiRow> rows;           // enabled=false: locked (sub = why / preview text)
};

struct UiEnding {
    bool visible = false;
    std::string name, text, hint;
    bool rebirth = false;              // two buttons instead of "tap to continue"
    std::string rebirth_label, title_label;
};

struct UiOverlay {                     // always on top
    bool toast = false;                // store purchase / "not enough gold" / "saved"
    std::string toast_text;
    std::vector<std::string> notifications;   // level up, mission available, floor events, ...
};

struct UiState {
    UiTitle title;
    UiHud hud;
    UiBattle battle;
    UiDialogue dialogue;
    UiPlayer player;
    UiStore store;
    UiMenu menu;
    UiStairs stairs;
    UiStageSelect stage_select;
    UiEnding ending;
    UiOverlay overlay;
};

}  // namespace toms

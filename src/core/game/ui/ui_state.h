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
};

// A world-anchored name plate over a boss or roaming monster (design-space position).
struct UiBanner {
    std::string text;
    float x = 0, y = 0;       // centre of the plate's bottom edge
};

struct UiTitle {
    bool visible = false;
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
    bool icons = false;                // menu + store buttons (hidden while the in-game menu is up)
    bool store_unlocked = false;
    std::string store_label, menu_label;
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
    std::string title, player_sprite, enemy_sprite;
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
};

struct UiInvItem {
    std::string icon, name, effect;
    bool selected = false;
};

struct UiInventory {
    bool visible = false;
    std::string title, hint, empty_title, empty_hint, detail_title;
    std::string use_label, drop_label, close_label, footer_hint, icon_label, stats_label;
    bool empty = true;
    std::vector<UiInvItem> items;
    std::string d_icon, d_name, d_id, d_icon_file, d_desc;   // the selected item's detail pane
    std::vector<std::string> d_pills;
};

struct UiStoreTab { std::string label; bool active = false; };
struct UiStoreItem {
    std::string icon, name, desc, effect, status, price;
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

struct UiMenu {                        // in-game menu (Esc / the ≡ button)
    bool visible = false;
    int page = 0;                      // 0 main, 1 settings, 2 skills, 3 forge, 4 village
    std::string header, subheader;
    std::vector<UiRow> rows;
    std::string close_label;           // main page: Close; sub-pages: Back
    bool confirm_open = false;         // "Switch to <language>?"
    std::string confirm_question, yes_label, no_label;
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
    UiInventory inventory;
    UiStore store;
    UiMenu menu;
    UiStairs stairs;
    UiStageSelect stage_select;
    UiEnding ending;
    UiOverlay overlay;
};

}  // namespace toms

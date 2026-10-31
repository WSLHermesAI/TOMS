// game_ui.cpp -- see game_ui.h.
#include "game_ui.h"

#include "game.h"
#include "rml_ui.h"

#include <RmlUi/Core.h>

#include <cstring>
#include <filesystem>

namespace toms::next {

namespace {

// Which document shows when, by its key in assets/media/resources.json (res_keys.h). Order = load order; the stacking
// itself is each document's `z-index` in its .rcss (the overlay document is always on top).
struct DocSpec { const char* key; bool (*visible)(const toms::UiState&); };
const DocSpec kDocs[] = {
    {toms::key::kUiHud,          [](const toms::UiState& s) { return s.hud.visible; }},
    {toms::key::kUiBattle,       [](const toms::UiState& s) { return s.battle.visible; }},
    {toms::key::kUiDialogue,     [](const toms::UiState& s) { return s.dialogue.visible; }},
    {toms::key::kUiStore,        [](const toms::UiState& s) { return s.store.visible; }},
    {toms::key::kUiStageSelect,  [](const toms::UiState& s) { return s.stage_select.visible; }},
    {toms::key::kUiPlayer,       [](const toms::UiState& s) { return s.player.visible; }},
    {toms::key::kUiSaveload,     [](const toms::UiState& s) { return s.saveload.visible; }},
    {toms::key::kUiDialogs,      [](const toms::UiState& s) { return s.stairs.visible || s.store.unlocked_dialog || s.notice.visible; }},
    {toms::key::kUiTitle,        [](const toms::UiState& s) { return s.title.visible; }},
    {toms::key::kUiEnding,       [](const toms::UiState& s) { return s.ending.visible; }},
    {toms::key::kUiMinigame,     [](const toms::UiState& s) { return s.minigame.visible; }},
    {toms::key::kUiOverlay,      [](const toms::UiState&)   { return true; }},
};

// Registers every UiState type with the data model. Member names are the ones the .rml files use.
void registerTypes(Rml::DataModelConstructor& c) {
    using namespace toms;
    c.RegisterArray<std::vector<std::string>>();
    c.RegisterArray<std::vector<int>>();
    if (auto s = c.RegisterStruct<UiRow>()) {
        s.RegisterMember("label", &UiRow::label);
        s.RegisterMember("sub", &UiRow::sub);
        s.RegisterMember("selected", &UiRow::selected);
        s.RegisterMember("enabled", &UiRow::enabled);
        s.RegisterMember("section", &UiRow::section);
        s.RegisterMember("extra", &UiRow::extra);
    }
    c.RegisterArray<std::vector<UiRow>>();
    if (auto s = c.RegisterStruct<UiBanner>()) {
        s.RegisterMember("text", &UiBanner::text);
        s.RegisterMember("x", &UiBanner::x);
        s.RegisterMember("y", &UiBanner::y);
    }
    c.RegisterArray<std::vector<UiBanner>>();
    if (auto s = c.RegisterStruct<UiTitle>()) {
        s.RegisterMember("visible", &UiTitle::visible);
        s.RegisterMember("scene", &UiTitle::scene);
        s.RegisterMember("title", &UiTitle::title);
        s.RegisterMember("subtitle", &UiTitle::subtitle);
        s.RegisterMember("hint", &UiTitle::hint);
        s.RegisterMember("page", &UiTitle::page);
        s.RegisterMember("header", &UiTitle::header);
        s.RegisterMember("subheader", &UiTitle::subheader);
        s.RegisterMember("rows", &UiTitle::rows);
        s.RegisterMember("show_back", &UiTitle::show_back);
        s.RegisterMember("back_label", &UiTitle::back_label);
        s.RegisterMember("show_empty_hint", &UiTitle::show_empty_hint);
        s.RegisterMember("empty_hint", &UiTitle::empty_hint);
        s.RegisterMember("confirm_open", &UiTitle::confirm_open);
        s.RegisterMember("confirm_question", &UiTitle::confirm_question);
        s.RegisterMember("confirm_body", &UiTitle::confirm_body);
        s.RegisterMember("yes_label", &UiTitle::yes_label);
        s.RegisterMember("no_label", &UiTitle::no_label);
        s.RegisterMember("confirm_yes", &UiTitle::confirm_yes);
    }
    if (auto s = c.RegisterStruct<UiHud>()) {
        s.RegisterMember("visible", &UiHud::visible);
        s.RegisterMember("title_line", &UiHud::title_line);
        s.RegisterMember("hp_pct", &UiHud::hp_pct);
        s.RegisterMember("hp_text", &UiHud::hp_text);
        s.RegisterMember("stats_line", &UiHud::stats_line);
        s.RegisterMember("res_line", &UiHud::res_line);
        s.RegisterMember("footer", &UiHud::footer);
        s.RegisterMember("menu_label", &UiHud::menu_label);
        s.RegisterMember("menu_news", &UiHud::menu_news);
        s.RegisterMember("chapter_card", &UiHud::chapter_card);
        s.RegisterMember("chapter_title", &UiHud::chapter_title);
        s.RegisterMember("chapter_sub", &UiHud::chapter_sub);
        s.RegisterMember("chapter_alpha", &UiHud::chapter_alpha);
        s.RegisterMember("banners", &UiHud::banners);
    }
    if (auto s = c.RegisterStruct<UiPowerBar>()) {
        s.RegisterMember("prompt", &UiPowerBar::prompt);
        s.RegisterMember("button_label", &UiPowerBar::button_label);
        s.RegisterMember("cooling", &UiPowerBar::cooling);
        s.RegisterMember("red_l", &UiPowerBar::red_l);
        s.RegisterMember("blue_l", &UiPowerBar::blue_l);
        s.RegisterMember("green", &UiPowerBar::green);
        s.RegisterMember("blue_r", &UiPowerBar::blue_r);
        s.RegisterMember("red_r", &UiPowerBar::red_r);
        s.RegisterMember("marker", &UiPowerBar::marker);
    }
    if (auto s = c.RegisterStruct<UiComboPoint>()) {
        s.RegisterMember("pos", &UiComboPoint::pos);
        s.RegisterMember("hit", &UiComboPoint::hit);
        s.RegisterMember("near", &UiComboPoint::near);
    }
    c.RegisterArray<std::vector<UiComboPoint>>();
    if (auto s = c.RegisterStruct<UiCombo>()) {
        s.RegisterMember("prompt", &UiCombo::prompt);
        s.RegisterMember("button_label", &UiCombo::button_label);
        s.RegisterMember("level_text", &UiCombo::level_text);
        s.RegisterMember("running", &UiCombo::running);
        s.RegisterMember("cooling", &UiCombo::cooling);
        s.RegisterMember("waiting", &UiCombo::waiting);
        s.RegisterMember("charge_pct", &UiCombo::charge_pct);
        s.RegisterMember("marker", &UiCombo::marker);
        s.RegisterMember("points", &UiCombo::points);
    }
    if (auto s = c.RegisterStruct<UiMiniGame>()) {
        s.RegisterMember("visible", &UiMiniGame::visible);
        s.RegisterMember("title", &UiMiniGame::title);
        s.RegisterMember("text", &UiMiniGame::text);
        s.RegisterMember("prompt", &UiMiniGame::prompt);
        s.RegisterMember("button_label", &UiMiniGame::button_label);
        s.RegisterMember("result", &UiMiniGame::result);
        s.RegisterMember("close_label", &UiMiniGame::close_label);
        s.RegisterMember("running", &UiMiniGame::running);
        s.RegisterMember("done", &UiMiniGame::done);
        s.RegisterMember("marker", &UiMiniGame::marker);
        s.RegisterMember("points", &UiMiniGame::points);
    }
    if (auto s = c.RegisterStruct<UiPopup>()) {
        s.RegisterMember("text", &UiPopup::text);
        s.RegisterMember("x", &UiPopup::x);
        s.RegisterMember("y", &UiPopup::y);
        s.RegisterMember("opacity", &UiPopup::opacity);
        s.RegisterMember("style", &UiPopup::style);
    }
    c.RegisterArray<std::vector<UiPopup>>();
    if (auto s = c.RegisterStruct<UiTouchItem>()) {
        s.RegisterMember("cls", &UiTouchItem::cls);
        s.RegisterMember("x", &UiTouchItem::x);
        s.RegisterMember("y", &UiTouchItem::y);
        s.RegisterMember("w", &UiTouchItem::w);
        s.RegisterMember("h", &UiTouchItem::h);
        s.RegisterMember("rot", &UiTouchItem::rot);
        s.RegisterMember("radius", &UiTouchItem::radius);
        s.RegisterMember("opacity", &UiTouchItem::opacity);
        s.RegisterMember("text", &UiTouchItem::text);
    }
    c.RegisterArray<std::vector<UiTouchItem>>();
    if (auto s = c.RegisterStruct<UiFoe>()) {
        s.RegisterMember("sprite", &UiFoe::sprite);
        s.RegisterMember("anim", &UiFoe::anim);
        s.RegisterMember("hp_pct", &UiFoe::hp_pct);
        s.RegisterMember("hp_text", &UiFoe::hp_text);
        s.RegisterMember("name", &UiFoe::name);
        s.RegisterMember("hp_line", &UiFoe::hp_line);
        s.RegisterMember("target", &UiFoe::target);
        s.RegisterMember("down", &UiFoe::down);
        s.RegisterMember("intent", &UiFoe::intent);
        s.RegisterMember("intent_kind", &UiFoe::intent_kind);
        s.RegisterMember("intent_pct", &UiFoe::intent_pct);
        s.RegisterMember("break_pct", &UiFoe::break_pct);
    }
    c.RegisterArray<std::vector<UiFoe>>();
    if (auto s = c.RegisterStruct<UiBattle>()) {
        s.RegisterMember("visible", &UiBattle::visible);
        s.RegisterMember("active", &UiBattle::active);
        s.RegisterMember("title", &UiBattle::title);
        s.RegisterMember("player_sprite", &UiBattle::player_sprite);
        s.RegisterMember("player_anim", &UiBattle::player_anim);
        s.RegisterMember("enemy_anim", &UiBattle::enemy_anim);
        s.RegisterMember("enemy_sprite", &UiBattle::enemy_sprite);
        s.RegisterMember("player_hp_pct", &UiBattle::player_hp_pct);
        s.RegisterMember("enemy_hp_pct", &UiBattle::enemy_hp_pct);
        s.RegisterMember("player_hp_text", &UiBattle::player_hp_text);
        s.RegisterMember("enemy_hp_text", &UiBattle::enemy_hp_text);
        s.RegisterMember("log", &UiBattle::log);
        s.RegisterMember("continue_hint", &UiBattle::continue_hint);
        s.RegisterMember("clock_label", &UiBattle::clock_label);
        s.RegisterMember("clock_pct", &UiBattle::clock_pct);
        s.RegisterMember("attack", &UiBattle::attack);
        s.RegisterMember("defense", &UiBattle::defense);
        s.RegisterMember("combo", &UiBattle::combo);
        s.RegisterMember("shield_text", &UiBattle::shield_text);
        s.RegisterMember("shield_armed", &UiBattle::shield_armed);
        s.RegisterMember("crit_armed", &UiBattle::crit_armed);
        s.RegisterMember("crit_hint", &UiBattle::crit_hint);
        s.RegisterMember("super_label", &UiBattle::super_label);
        s.RegisterMember("super_dots", &UiBattle::super_dots);
        s.RegisterMember("super_ready", &UiBattle::super_ready);
        s.RegisterMember("super_button", &UiBattle::super_button);
        s.RegisterMember("active_ready", &UiBattle::active_ready);
        s.RegisterMember("active_button", &UiBattle::active_button);
        s.RegisterMember("popups", &UiBattle::popups);
        s.RegisterMember("touch_kind", &UiBattle::touch_kind);
        s.RegisterMember("touch", &UiBattle::touch);
        s.RegisterMember("foes", &UiBattle::foes);
        s.RegisterMember("stamina_pace", &UiBattle::stamina_pace);
        for (auto [name, member] : std::initializer_list<std::pair<const char*, std::string UiBattle::*>>{
                 {"banner", &UiBattle::banner}, {"banner_kind", &UiBattle::banner_kind}, {"chain_text", &UiBattle::chain_text},
                 {"guard_label", &UiBattle::guard_label}, {"guard_hint", &UiBattle::guard_hint},
                 {"attack_state", &UiBattle::attack_state}, {"attack_label", &UiBattle::attack_label},
                 {"attack_hint", &UiBattle::attack_hint}, {"grade", &UiBattle::grade}, {"result_title", &UiBattle::result_title},
                 {"result_stats", &UiBattle::result_stats}, {"result_weapon", &UiBattle::result_weapon},
                 {"result_progress", &UiBattle::result_progress}, {"result_next", &UiBattle::result_next},
                 {"result_gold", &UiBattle::result_gold}, {"result_card", &UiBattle::result_card},
                 {"result_levelup", &UiBattle::result_levelup}})
            s.RegisterMember(name, member);
        s.RegisterMember("banner_opacity", &UiBattle::banner_opacity);
        s.RegisterMember("banner_shift", &UiBattle::banner_shift);
        s.RegisterMember("attack_pct", &UiBattle::attack_pct);
        s.RegisterMember("result_pct", &UiBattle::result_pct);
        s.RegisterMember("guard_timing", &UiBattle::guard_timing);
        s.RegisterMember("guard_lit", &UiBattle::guard_lit);
        s.RegisterMember("guard_cooling", &UiBattle::guard_cooling);
        s.RegisterMember("show_clock", &UiBattle::show_clock);
        s.RegisterMember("result_shown", &UiBattle::result_shown);
        s.RegisterMember("grade_s", &UiBattle::grade_s);
        s.RegisterMember("attack_short", &UiBattle::attack_short);
        s.RegisterMember("guard_short", &UiBattle::guard_short);
    }
    if (auto s = c.RegisterStruct<UiDialogue>()) {
        s.RegisterMember("visible", &UiDialogue::visible);
        s.RegisterMember("speaker", &UiDialogue::speaker);
        s.RegisterMember("text", &UiDialogue::text);
        s.RegisterMember("choices", &UiDialogue::choices);
        s.RegisterMember("scale", &UiDialogue::scale);
        s.RegisterMember("side", &UiDialogue::side);
    }
    if (auto s = c.RegisterStruct<UiTab>()) {
        s.RegisterMember("label", &UiTab::label);
        s.RegisterMember("active", &UiTab::active);
        s.RegisterMember("news", &UiTab::news);
    }
    c.RegisterArray<std::vector<UiTab>>();
    if (auto s = c.RegisterStruct<UiCell>()) {
        s.RegisterMember("icon", &UiCell::icon);
        s.RegisterMember("name", &UiCell::name);
        s.RegisterMember("badge", &UiCell::badge);
        s.RegisterMember("selected", &UiCell::selected);
        s.RegisterMember("worn", &UiCell::worn);
        s.RegisterMember("locked", &UiCell::locked);
    }
    c.RegisterArray<std::vector<UiCell>>();
    if (auto s = c.RegisterStruct<UiLine>()) {
        s.RegisterMember("label", &UiLine::label);
        s.RegisterMember("value", &UiLine::value);
        s.RegisterMember("extra", &UiLine::extra);
        s.RegisterMember("tone", &UiLine::tone);
    }
    c.RegisterArray<std::vector<UiLine>>();
    if (auto s = c.RegisterStruct<UiButton>()) {
        s.RegisterMember("label", &UiButton::label);
        s.RegisterMember("enabled", &UiButton::enabled);
    }
    c.RegisterArray<std::vector<UiButton>>();
    if (auto s = c.RegisterStruct<UiPlayer>()) {
        s.RegisterMember("visible", &UiPlayer::visible);
        s.RegisterMember("tab", &UiPlayer::tab);
        s.RegisterMember("tabs", &UiPlayer::tabs);
        s.RegisterMember("hint", &UiPlayer::hint);
        s.RegisterMember("close_label", &UiPlayer::close_label);
        s.RegisterMember("level", &UiPlayer::level);
        s.RegisterMember("hp_text", &UiPlayer::hp_text);
        s.RegisterMember("exp_text", &UiPlayer::exp_text);
        s.RegisterMember("hp_pct", &UiPlayer::hp_pct);
        s.RegisterMember("exp_pct", &UiPlayer::exp_pct);
        s.RegisterMember("combat_title", &UiPlayer::combat_title);
        s.RegisterMember("attr_title", &UiPlayer::attr_title);
        s.RegisterMember("attr_desc", &UiPlayer::attr_desc);
        s.RegisterMember("attr_per", &UiPlayer::attr_per);
        s.RegisterMember("attr_now", &UiPlayer::attr_now);
        s.RegisterMember("attr_points", &UiPlayer::attr_points);
        s.RegisterMember("attr_points_hint", &UiPlayer::attr_points_hint);
        s.RegisterMember("attr_can_add", &UiPlayer::attr_can_add);
        s.RegisterMember("attr_draft", &UiPlayer::attr_draft);
        s.RegisterMember("attr_confirm", &UiPlayer::attr_confirm);
        s.RegisterMember("attr_reset", &UiPlayer::attr_reset);
        s.RegisterMember("attr_confirm_sel", &UiPlayer::attr_confirm_sel);
        s.RegisterMember("attr_reset_sel", &UiPlayer::attr_reset_sel);
        s.RegisterMember("combat", &UiPlayer::combat);
        s.RegisterMember("more", &UiPlayer::more);
        s.RegisterMember("attrs", &UiPlayer::attrs);
        s.RegisterMember("filters", &UiPlayer::filters);
        s.RegisterMember("worn_title", &UiPlayer::worn_title);
        s.RegisterMember("worn", &UiPlayer::worn);
        s.RegisterMember("cells", &UiPlayer::cells);
        s.RegisterMember("empty_text", &UiPlayer::empty_text);
        s.RegisterMember("detail", &UiPlayer::detail);
        s.RegisterMember("select_hint", &UiPlayer::select_hint);
        s.RegisterMember("d_icon", &UiPlayer::d_icon);
        s.RegisterMember("d_name", &UiPlayer::d_name);
        s.RegisterMember("d_desc", &UiPlayer::d_desc);
        s.RegisterMember("d_note", &UiPlayer::d_note);
        s.RegisterMember("d_lines", &UiPlayer::d_lines);
        s.RegisterMember("d_compare", &UiPlayer::d_compare);
        s.RegisterMember("d_now_label", &UiPlayer::d_now_label);
        s.RegisterMember("d_with_label", &UiPlayer::d_with_label);
        s.RegisterMember("d_compare_lines", &UiPlayer::d_compare_lines);
        s.RegisterMember("d_buttons", &UiPlayer::d_buttons);
        s.RegisterMember("confirm_open", &UiPlayer::confirm_open);
        s.RegisterMember("confirm_question", &UiPlayer::confirm_question);
        s.RegisterMember("yes_label", &UiPlayer::yes_label);
        s.RegisterMember("no_label", &UiPlayer::no_label);
        s.RegisterMember("confirm_yes", &UiPlayer::confirm_yes);
        s.RegisterMember("ev_filters", &UiPlayer::ev_filters);
        s.RegisterMember("ev_rows", &UiPlayer::ev_rows);
        s.RegisterMember("ev_detail", &UiPlayer::ev_detail);
        s.RegisterMember("ev_empty", &UiPlayer::ev_empty);
        s.RegisterMember("ev_hint", &UiPlayer::ev_hint);
        s.RegisterMember("ev_kind", &UiPlayer::ev_kind);
        s.RegisterMember("ev_title", &UiPlayer::ev_title);
        s.RegisterMember("ev_desc", &UiPlayer::ev_desc);
        s.RegisterMember("ev_connected_title", &UiPlayer::ev_connected_title);
        s.RegisterMember("ev_connected", &UiPlayer::ev_connected);
        s.RegisterMember("sys_hint", &UiPlayer::sys_hint);
    }
    if (auto s = c.RegisterStruct<UiSlot>()) {
        s.RegisterMember("title", &UiSlot::title);
        s.RegisterMember("line1", &UiSlot::line1);
        s.RegisterMember("line2", &UiSlot::line2);
        s.RegisterMember("tag", &UiSlot::tag);
        s.RegisterMember("empty", &UiSlot::empty);
        s.RegisterMember("selected", &UiSlot::selected);
        s.RegisterMember("enabled", &UiSlot::enabled);
    }
    c.RegisterArray<std::vector<UiSlot>>();
    if (auto s = c.RegisterStruct<UiSaveLoad>()) {
        s.RegisterMember("visible", &UiSaveLoad::visible);
        s.RegisterMember("title", &UiSaveLoad::title);
        s.RegisterMember("hint", &UiSaveLoad::hint);
        s.RegisterMember("close_label", &UiSaveLoad::close_label);
        s.RegisterMember("modes", &UiSaveLoad::modes);
        s.RegisterMember("cards", &UiSaveLoad::cards);
        s.RegisterMember("confirm_open", &UiSaveLoad::confirm_open);
        s.RegisterMember("confirm_question", &UiSaveLoad::confirm_question);
        s.RegisterMember("confirm_body", &UiSaveLoad::confirm_body);
        s.RegisterMember("yes_label", &UiSaveLoad::yes_label);
        s.RegisterMember("no_label", &UiSaveLoad::no_label);
        s.RegisterMember("confirm_yes", &UiSaveLoad::confirm_yes);
    }
    if (auto s = c.RegisterStruct<UiStoreTab>()) {
        s.RegisterMember("label", &UiStoreTab::label);
        s.RegisterMember("active", &UiStoreTab::active);
    }
    c.RegisterArray<std::vector<UiStoreTab>>();
    if (auto s = c.RegisterStruct<UiStoreItem>()) {
        s.RegisterMember("icon", &UiStoreItem::icon);
        s.RegisterMember("name", &UiStoreItem::name);
        s.RegisterMember("desc", &UiStoreItem::desc);
        s.RegisterMember("effect", &UiStoreItem::effect);
        s.RegisterMember("status", &UiStoreItem::status);
        s.RegisterMember("price", &UiStoreItem::price);
        s.RegisterMember("selected", &UiStoreItem::selected);
        s.RegisterMember("equipped", &UiStoreItem::equipped);
    }
    c.RegisterArray<std::vector<UiStoreItem>>();
    if (auto s = c.RegisterStruct<UiStore>()) {
        s.RegisterMember("visible", &UiStore::visible);
        s.RegisterMember("title", &UiStore::title);
        s.RegisterMember("hint", &UiStore::hint);
        s.RegisterMember("gold_label", &UiStore::gold_label);
        s.RegisterMember("close_label", &UiStore::close_label);
        s.RegisterMember("effect_label", &UiStore::effect_label);
        s.RegisterMember("price_label", &UiStore::price_label);
        s.RegisterMember("buy_label", &UiStore::buy_label);
        s.RegisterMember("gold", &UiStore::gold);
        s.RegisterMember("tabs", &UiStore::tabs);
        s.RegisterMember("items", &UiStore::items);
        s.RegisterMember("unlocked_dialog", &UiStore::unlocked_dialog);
        s.RegisterMember("unlocked_title", &UiStore::unlocked_title);
        s.RegisterMember("unlocked_body", &UiStore::unlocked_body);
        s.RegisterMember("unlocked_ok", &UiStore::unlocked_ok);
    }
    if (auto s = c.RegisterStruct<UiMenu>()) {
        s.RegisterMember("visible", &UiMenu::visible);
        s.RegisterMember("page", &UiMenu::page);
        s.RegisterMember("main_rows", &UiMenu::main_rows);
        s.RegisterMember("header", &UiMenu::header);
        s.RegisterMember("subheader", &UiMenu::subheader);
        s.RegisterMember("rows", &UiMenu::rows);
        s.RegisterMember("close_label", &UiMenu::close_label);
        s.RegisterMember("confirm_open", &UiMenu::confirm_open);
        s.RegisterMember("confirm_question", &UiMenu::confirm_question);
        s.RegisterMember("confirm_body", &UiMenu::confirm_body);
        s.RegisterMember("yes_label", &UiMenu::yes_label);
        s.RegisterMember("no_label", &UiMenu::no_label);
        s.RegisterMember("confirm_yes", &UiMenu::confirm_yes);
    }
    if (auto s = c.RegisterStruct<UiNotice>()) {
        s.RegisterMember("visible", &UiNotice::visible);
        s.RegisterMember("title", &UiNotice::title);
        s.RegisterMember("body", &UiNotice::body);
        s.RegisterMember("ok", &UiNotice::ok);
    }
    if (auto s = c.RegisterStruct<UiStairs>()) {
        s.RegisterMember("visible", &UiStairs::visible);
        s.RegisterMember("question", &UiStairs::question);
        s.RegisterMember("yes_label", &UiStairs::yes_label);
        s.RegisterMember("no_label", &UiStairs::no_label);
    }
    if (auto s = c.RegisterStruct<UiStageSelect>()) {
        s.RegisterMember("visible", &UiStageSelect::visible);
        s.RegisterMember("title", &UiStageSelect::title);
        s.RegisterMember("hint", &UiStageSelect::hint);
        s.RegisterMember("close_label", &UiStageSelect::close_label);
        s.RegisterMember("rows", &UiStageSelect::rows);
    }
    if (auto s = c.RegisterStruct<UiEnding>()) {
        s.RegisterMember("visible", &UiEnding::visible);
        s.RegisterMember("name", &UiEnding::name);
        s.RegisterMember("text", &UiEnding::text);
        s.RegisterMember("hint", &UiEnding::hint);
        s.RegisterMember("rebirth", &UiEnding::rebirth);
        s.RegisterMember("rebirth_label", &UiEnding::rebirth_label);
        s.RegisterMember("title_label", &UiEnding::title_label);
    }
    if (auto s = c.RegisterStruct<UiOverlay>()) {
        s.RegisterMember("toast", &UiOverlay::toast);
        s.RegisterMember("toast_text", &UiOverlay::toast_text);
        s.RegisterMember("notifications", &UiOverlay::notifications);
    }
}

}  // namespace

bool GameUi::init(Rml::Context* ctx, Game* game, const std::string& uiDir, std::string& error) {
    ctx_ = ctx;
    game_ = game;
    // generic_string: RmlUi joins relative URLs (images, stylesheets) on '/' only.
    dir_ = std::filesystem::path(uiDir).generic_string();

    Rml::DataModelConstructor c = ctx_->CreateDataModel("game");
    if (!c) { error = "RmlUi: could not create the \"game\" data model"; return false; }
    model_ = true;
    registerTypes(c);
    c.Bind("title", &state_.title);
    c.Bind("hud", &state_.hud);
    c.Bind("battle", &state_.battle);
    c.Bind("dialogue", &state_.dialogue);
    c.Bind("player", &state_.player);
    c.Bind("saveload", &state_.saveload);
    c.Bind("store", &state_.store);
    c.Bind("menu", &state_.menu);
    c.Bind("stairs", &state_.stairs);
    c.Bind("notice", &state_.notice);
    c.Bind("stage_select", &state_.stage_select);
    c.Bind("ending", &state_.ending);
    c.Bind("minigame", &state_.minigame);
    c.Bind("overlay", &state_.overlay);
    // act('name') / act('name', n): the one event every button uses. The innermost handler wins
    // (a choice inside the dialogue box must not also count as a click on the box).
    c.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
        ev.StopPropagation();
        if (args.empty()) return;
        game_->uiEvent(args[0].Get<Rml::String>(), args.size() > 1 ? args[1].Get<int>(0) : 0);
    });

    game_->buildUiState(state_);   // the documents bind to a filled model
    publishSprites();
    if (!loadDocuments(error)) return false;
    sync();                        // show the right documents before the first frame is drawn
    return true;
}

bool GameUi::loadDocuments(std::string& error) {
    docs_.clear();
    for (const DocSpec& spec : kDocs) {
        Doc d;
        d.file = game_->pathOf(spec.key);
        d.doc = ctx_->LoadDocument(d.file);
        if (!d.doc) { error = "RmlUi could not load " + d.file + " (see the console log)"; return false; }
        if (family_ != "toms") d.doc->SetProperty("font-family", family_);
        docs_.push_back(d);
    }
    return true;
}

// Every document that shows sprites links "_atlas.rcss": the RmlUi @spritesheet for the atlas the
// game loaded (Game::uiSpritesheet). It is made in memory from that atlas, so the UI and the map
// always agree, and a .rml file only names sprites (<img data-attr-sprite="..."/>).
void GameUi::publishSprites() {
    spriteRevision_ = game_->spriteAtlasRevision();
    const std::string sheet = game_->uiSpritesheet();
    if (sheet.empty()) Rml::Log::Message(Rml::Log::LT_ERROR, "no prebuilt sprite atlas: the UI has no icons (assets/media/atlas)");
    for (const std::string& name : game_->missingUiSprites())
        Rml::Log::Message(Rml::Log::LT_ERROR, "sprite '%s' is used by the game data but is not in the atlas", name.c_str());
    RmlUi::setVirtualFile("_atlas.rcss", sheet);
}

void GameUi::setFontFamily(const std::string& family) {
    if (family == family_) return;
    family_ = family;
    for (Doc& d : docs_)
        if (d.doc) d.doc->SetProperty("font-family", family_);
}

void GameUi::shutdown() {
    for (Doc& d : docs_) if (d.doc) d.doc->Close();
    docs_.clear();
    if (ctx_ && model_) ctx_->RemoveDataModel("game");
    model_ = false;
    ctx_ = nullptr;
}

void GameUi::reload() {
    if (!ctx_) return;
    for (Doc& d : docs_) if (d.doc) d.doc->Close();
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    std::string error;
    if (!loadDocuments(error)) Rml::Log::Message(Rml::Log::LT_ERROR, "%s", error.c_str());
}

std::string GameUi::reloadIfChanged() {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::string changed;
    std::map<std::string, fs::file_time_type> now;
    for (const fs::directory_entry& e : fs::directory_iterator(fs::u8path(dir_), ec)) {
        const std::string ext = e.path().extension().string();
        if (ext != ".rml" && ext != ".rcss") continue;
        const fs::file_time_type t = e.last_write_time(ec);
        if (ec) continue;
        const std::string name = e.path().filename().u8string();
        now[name] = t;
        if (stamped_ && (!stamps_.count(name) || stamps_[name] != t)) changed = name;
    }
    if (stamped_ && changed.empty() && now.size() != stamps_.size()) changed = "(a file removed)";
    stamps_ = std::move(now);
    stamped_ = true;
    if (!changed.empty()) reload();
    return changed;
}

void GameUi::sync() {
    if (!ctx_ || !game_) return;
    if (game_->spriteAtlasRevision() != spriteRevision_) {   // another art style: new sheet, reload
        publishSprites();
        reload();
    }
    // A screen that is closing keeps its last values while it hides: its rows would otherwise
    // vanish under elements that are still bound to them (and flash empty for a frame).
    toms::UiState next;
    game_->buildUiState(next);
    auto take = [](auto& cur, auto& nxt) { if (nxt.visible) cur = std::move(nxt); else cur.visible = false; };
    take(state_.title, next.title);
    state_.title.scene = titleScene_;
    take(state_.hud, next.hud);
    take(state_.battle, next.battle);
    // Where the battle screen drew the attack track's points (last frame's layout), for the track's hint particles,
    // which the game draws over the UI (Game::comboHint).
    for (size_t i = 0; i < docs_.size() && i < std::size(kDocs); i++) {
        if (std::strcmp(kDocs[i].key, toms::key::kUiBattle) != 0 || !docs_[i].doc || !docs_[i].shown) continue;
        Rml::ElementList pts;
        docs_[i].doc->QuerySelectorAll(pts, ".track .pt");
        std::vector<glm::vec2> at;
        for (Rml::Element* e : pts) {
            const Rml::Vector2f o = e->GetAbsoluteOffset(Rml::BoxArea::Border), sz = e->GetBox().GetSize(Rml::BoxArea::Border);
            at.push_back({o.x + sz.x * 0.5f, o.y + sz.y * 0.5f});
        }
        game_->setComboPointsOnScreen(std::move(at));
        auto box = [](Rml::Element* e) {   // the fighters' boxes (battle.rcss #player / .foe .fbox)
            if (!e) return glm::vec4(0.0f);
            const Rml::Vector2f o = e->GetAbsoluteOffset(Rml::BoxArea::Border), sz = e->GetBox().GetSize(Rml::BoxArea::Border);
            return glm::vec4(o.x, o.y, sz.x, sz.y);
        };
        Rml::ElementList fb;
        docs_[i].doc->QuerySelectorAll(fb, ".foe .fbox");
        std::vector<glm::vec4> foes;
        for (Rml::Element* e : fb) foes.push_back(box(e));
        const glm::vec4 pb = box(docs_[i].doc->GetElementById("player"));
        if (pb.z > 0 && !foes.empty() && foes[0].z > 0) game_->setBattleBoxes(pb, foes);
    }
    take(state_.dialogue, next.dialogue);
    take(state_.player, next.player);
    take(state_.saveload, next.saveload);
    take(state_.menu, next.menu);
    take(state_.stairs, next.stairs);
    take(state_.notice, next.notice);
    take(state_.stage_select, next.stage_select);
    take(state_.ending, next.ending);
    take(state_.minigame, next.minigame);   // a reward event (docs/gameplay/07 phase 6)
    if (next.store.visible || next.store.unlocked_dialog) state_.store = std::move(next.store);
    else { state_.store.visible = false; state_.store.unlocked_dialog = false; }
    state_.overlay = std::move(next.overlay);
    // Everything is re-evaluated each frame: RmlUi only touches elements whose value changed, and
    // data-for keeps its existing elements, so this is cheap and never restarts a transition.
    if (Rml::DataModelHandle h = ctx_->GetDataModel("game").GetModelHandle()) h.DirtyAllVariables();
    {   // which entry the player menu has selected: a change (keyboard or click) scrolls it into view
        std::string key = std::to_string(state_.player.tab) + "|";
        auto at = [&](const auto& v) { for (size_t i = 0; i < v.size(); i++) if (v[i].selected) return (int)i; return -1; };
        key += std::to_string(at(state_.player.cells)) + "|" + std::to_string(at(state_.player.ev_rows)) + "|" +
               std::to_string(at(state_.player.attrs)) + "|" + std::to_string(at(state_.menu.rows));
        if (key != playerSelKey_) { playerSelKey_ = key; scrollFrames_ = 2; }
    }
    for (size_t i = 0; i < docs_.size(); i++) {
        Doc& d = docs_[i];
        if (!d.doc) continue;
        const bool want = kDocs[i].visible(state_);
        // The player menu's grids and lists scroll: an entry selected with the keyboard is scrolled
        // into view. The selection is laid out by the next context update, so this looks one frame later.
        if (want && d.shown && scrollFrames_ > 0 && std::strcmp(kDocs[i].key, toms::key::kUiPlayer) == 0) {
            --scrollFrames_;
            for (Rml::Element* sel : {d.doc->QuerySelector(".cell.selected"), d.doc->QuerySelector(".evrow.selected"),
                                      d.doc->QuerySelector(".attr.selected"), d.doc->QuerySelector("#sys-page .row.selected")})
                if (sel) sel->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
                                                                       Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest));
        }
        if (want == d.shown) continue;
        if (want) d.doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        else d.doc->Hide();
        d.shown = want;
    }
}

}  // namespace toms::next

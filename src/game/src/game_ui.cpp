// game_ui.cpp -- see game_ui.h.
#include "game_ui.h"

#include "game.h"

#include <RmlUi/Core.h>

#include <filesystem>

namespace toms::next {

namespace {

// Which document shows when. Order = load order; the stacking itself is each document's
// `z-index` in its .rcss (the overlay document is always on top).
struct DocSpec { const char* file; bool (*visible)(const toms::UiState&); };
const DocSpec kDocs[] = {
    {"hud.rml",          [](const toms::UiState& s) { return s.hud.visible; }},
    {"battle.rml",       [](const toms::UiState& s) { return s.battle.visible; }},
    {"dialogue.rml",     [](const toms::UiState& s) { return s.dialogue.visible; }},
    {"inventory.rml",    [](const toms::UiState& s) { return s.inventory.visible; }},
    {"store.rml",        [](const toms::UiState& s) { return s.store.visible; }},
    {"stage_select.rml", [](const toms::UiState& s) { return s.stage_select.visible; }},
    {"menu.rml",         [](const toms::UiState& s) { return s.menu.visible; }},
    {"dialogs.rml",      [](const toms::UiState& s) { return s.stairs.visible || s.store.unlocked_dialog; }},
    {"title.rml",        [](const toms::UiState& s) { return s.title.visible; }},
    {"ending.rml",       [](const toms::UiState& s) { return s.ending.visible; }},
    {"overlay.rml",      [](const toms::UiState&)   { return true; }},
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
        s.RegisterMember("icons", &UiHud::icons);
        s.RegisterMember("store_unlocked", &UiHud::store_unlocked);
        s.RegisterMember("store_label", &UiHud::store_label);
        s.RegisterMember("store_icon", &UiHud::store_icon);
        s.RegisterMember("menu_label", &UiHud::menu_label);
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
    if (auto s = c.RegisterStruct<UiBattle>()) {
        s.RegisterMember("visible", &UiBattle::visible);
        s.RegisterMember("active", &UiBattle::active);
        s.RegisterMember("title", &UiBattle::title);
        s.RegisterMember("player_sprite", &UiBattle::player_sprite);
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
    }
    if (auto s = c.RegisterStruct<UiDialogue>()) {
        s.RegisterMember("visible", &UiDialogue::visible);
        s.RegisterMember("text", &UiDialogue::text);
        s.RegisterMember("choices", &UiDialogue::choices);
        s.RegisterMember("scale", &UiDialogue::scale);
    }
    if (auto s = c.RegisterStruct<UiInvItem>()) {
        s.RegisterMember("icon", &UiInvItem::icon);
        s.RegisterMember("name", &UiInvItem::name);
        s.RegisterMember("effect", &UiInvItem::effect);
        s.RegisterMember("selected", &UiInvItem::selected);
    }
    c.RegisterArray<std::vector<UiInvItem>>();
    if (auto s = c.RegisterStruct<UiInventory>()) {
        s.RegisterMember("visible", &UiInventory::visible);
        s.RegisterMember("title", &UiInventory::title);
        s.RegisterMember("hint", &UiInventory::hint);
        s.RegisterMember("empty_title", &UiInventory::empty_title);
        s.RegisterMember("empty_hint", &UiInventory::empty_hint);
        s.RegisterMember("detail_title", &UiInventory::detail_title);
        s.RegisterMember("use_label", &UiInventory::use_label);
        s.RegisterMember("drop_label", &UiInventory::drop_label);
        s.RegisterMember("close_label", &UiInventory::close_label);
        s.RegisterMember("footer_hint", &UiInventory::footer_hint);
        s.RegisterMember("icon_label", &UiInventory::icon_label);
        s.RegisterMember("stats_label", &UiInventory::stats_label);
        s.RegisterMember("empty", &UiInventory::empty);
        s.RegisterMember("items", &UiInventory::items);
        s.RegisterMember("d_icon", &UiInventory::d_icon);
        s.RegisterMember("d_name", &UiInventory::d_name);
        s.RegisterMember("d_id", &UiInventory::d_id);
        s.RegisterMember("d_icon_file", &UiInventory::d_icon_file);
        s.RegisterMember("d_desc", &UiInventory::d_desc);
        s.RegisterMember("d_pills", &UiInventory::d_pills);
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
    c.Bind("inventory", &state_.inventory);
    c.Bind("store", &state_.store);
    c.Bind("menu", &state_.menu);
    c.Bind("stairs", &state_.stairs);
    c.Bind("stage_select", &state_.stage_select);
    c.Bind("ending", &state_.ending);
    c.Bind("overlay", &state_.overlay);
    // act('name') / act('name', n): the one event every button uses. The innermost handler wins
    // (a choice inside the dialogue box must not also count as a click on the box).
    c.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event& ev, const Rml::VariantList& args) {
        ev.StopPropagation();
        if (args.empty()) return;
        game_->uiEvent(args[0].Get<Rml::String>(), args.size() > 1 ? args[1].Get<int>(0) : 0);
    });

    game_->buildUiState(state_);   // the documents bind to a filled model
    if (!loadDocuments(error)) return false;
    sync();                        // show the right documents before the first frame is drawn
    return true;
}

bool GameUi::loadDocuments(std::string& error) {
    docs_.clear();
    for (const DocSpec& spec : kDocs) {
        Doc d;
        d.file = dir_ + "/" + spec.file;
        d.doc = ctx_->LoadDocument(d.file);
        if (!d.doc) { error = "RmlUi could not load " + d.file + " (see the console log)"; return false; }
        if (family_ != "toms") d.doc->SetProperty("font-family", family_);
        docs_.push_back(d);
    }
    return true;
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

void GameUi::sync() {
    if (!ctx_ || !game_) return;
    // A screen that is closing keeps its last values while it hides: its rows would otherwise
    // vanish under elements that are still bound to them (and flash empty for a frame).
    toms::UiState next;
    game_->buildUiState(next);
    auto take = [](auto& cur, auto& nxt) { if (nxt.visible) cur = std::move(nxt); else cur.visible = false; };
    take(state_.title, next.title);
    take(state_.hud, next.hud);
    take(state_.battle, next.battle);
    take(state_.dialogue, next.dialogue);
    take(state_.inventory, next.inventory);
    take(state_.menu, next.menu);
    take(state_.stairs, next.stairs);
    take(state_.stage_select, next.stage_select);
    take(state_.ending, next.ending);
    if (next.store.visible || next.store.unlocked_dialog) state_.store = std::move(next.store);
    else { state_.store.visible = false; state_.store.unlocked_dialog = false; }
    state_.overlay = std::move(next.overlay);
    // Everything is re-evaluated each frame: RmlUi only touches elements whose value changed, and
    // data-for keeps its existing elements, so this is cheap and never restarts a transition.
    if (Rml::DataModelHandle h = ctx_->GetDataModel("game").GetModelHandle()) h.DirtyAllVariables();
    for (size_t i = 0; i < docs_.size(); i++) {
        Doc& d = docs_[i];
        if (!d.doc) continue;
        const bool want = kDocs[i].visible(state_);
        if (want == d.shown) continue;
        if (want) d.doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        else d.doc->Hide();
        d.shown = want;
    }
}

}  // namespace toms::next

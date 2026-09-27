// store_screen.cpp -- see store_screen.h.
#include "store_screen.h"

#include "game.h"

#include <RmlUi/Core.h>

#include <filesystem>

namespace toms::next {

namespace {
struct TabVM  { Rml::String label; bool active = false; };
struct ItemVM {
    Rml::String icon, name, desc, effect, status, price;
    bool selected = false, equipped = false;
};
}  // namespace

struct StoreScreen::Model {
    Rml::DataModelHandle handle;
    Rml::String title, hint, gold_label, close_label, effect_label, price_label, buy_label, toast;
    int gold = 0;
    bool toast_visible = false;
    std::vector<TabVM> tabs;
    std::vector<ItemVM> items;
    std::vector<int> itemIndex;   // items[i] -> index into Game::storeItems()
    std::string signature;        // everything above, flattened; unchanged = nothing to dirty
};

bool StoreScreen::init(Rml::Context* ctx, Game* game, const std::string& uiDir, std::string& error) {
    ctx_ = ctx;
    game_ = game;
    // generic_string: RmlUi joins relative URLs (the card icons) on "/" only.
    path_ = (std::filesystem::path(uiDir) / "store.rml").generic_string();
    if (!std::filesystem::exists(path_)) { error = "missing " + path_; return false; }

    Rml::DataModelConstructor c = ctx_->CreateDataModel("store");
    if (!c) { error = "RmlUi: could not create the \"store\" data model"; return false; }
    model_ = new Model();
    Model& m = *model_;
    if (auto s = c.RegisterStruct<TabVM>()) {
        s.RegisterMember("label", &TabVM::label);
        s.RegisterMember("active", &TabVM::active);
    }
    c.RegisterArray<std::vector<TabVM>>();
    if (auto s = c.RegisterStruct<ItemVM>()) {
        s.RegisterMember("icon", &ItemVM::icon);
        s.RegisterMember("name", &ItemVM::name);
        s.RegisterMember("desc", &ItemVM::desc);
        s.RegisterMember("effect", &ItemVM::effect);
        s.RegisterMember("status", &ItemVM::status);
        s.RegisterMember("price", &ItemVM::price);
        s.RegisterMember("selected", &ItemVM::selected);
        s.RegisterMember("equipped", &ItemVM::equipped);
    }
    c.RegisterArray<std::vector<ItemVM>>();
    c.Bind("title", &m.title);
    c.Bind("hint", &m.hint);
    c.Bind("gold_label", &m.gold_label);
    c.Bind("close_label", &m.close_label);
    c.Bind("effect_label", &m.effect_label);
    c.Bind("price_label", &m.price_label);
    c.Bind("buy_label", &m.buy_label);
    c.Bind("gold", &m.gold);
    c.Bind("toast", &m.toast);
    c.Bind("toast_visible", &m.toast_visible);
    c.Bind("tabs", &m.tabs);
    c.Bind("items", &m.items);

    auto argInt = [](const Rml::VariantList& a) { return a.empty() ? -1 : a[0].Get<int>(-1); };
    c.BindEventCallback("close", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        game_->closeStore();
    });
    c.BindEventCallback("select_tab", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        game_->storeSelectTab(argInt(a));
    });
    c.BindEventCallback("hover", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        const int i = argInt(a);
        if (i >= 0 && i < (int)model_->itemIndex.size()) game_->storeSetSelection(i);
    });
    c.BindEventCallback("buy", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        const int i = argInt(a);
        if (i < 0 || i >= (int)model_->itemIndex.size()) return;
        game_->storeSetSelection(i);
        game_->storeBuy(model_->itemIndex[i]);
    });
    m.handle = c.GetModelHandle();

    sync();   // fill the model before the document binds to it
    doc_ = ctx_->LoadDocument(path_);
    if (!doc_) { error = "RmlUi could not load " + path_ + " (see the console log)"; shutdown(); return false; }
    return true;
}

void StoreScreen::shutdown() {
    if (doc_) { doc_->Close(); doc_ = nullptr; }
    if (ctx_ && model_) ctx_->RemoveDataModel("store");
    delete model_;
    model_ = nullptr;
    ctx_ = nullptr;
    visible_ = false;
}

void StoreScreen::reload() {
    if (!ctx_) return;
    if (doc_) doc_->Close();
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    doc_ = ctx_->LoadDocument(path_);
    visible_ = false;   // sync() shows it again if the store is open
}

void StoreScreen::sync() {
    if (!model_ || !game_) return;
    Game& g = *game_;
    Model& m = *model_;
    const toms::Locale& L = g.locale();

    m.title = g.storeTitleText();
    m.hint = L.tr("store.controls_hint");
    m.gold_label = L.tr("store.gold_label");
    m.close_label = L.tr("inventory.close");
    m.effect_label = L.tr("store.effect_label");
    m.price_label = L.tr("store.price_label");
    m.buy_label = L.tr("store.buy_button");
    m.gold = g.playerGold();
    m.toast = g.toastMsg();
    m.toast_visible = g.toastRemainingMs() > 0 && !m.toast.empty();

    static const char* tabKeys[4] = {"store.tab.potion", "store.tab.weapon", "store.tab.armor", "store.tab.talent"};
    m.tabs.resize(4);
    for (int t = 0; t < 4; ++t) { m.tabs[t].label = L.tr(tabKeys[t]); m.tabs[t].active = (t == g.storeTab()); }

    m.itemIndex = g.storeVisibleItems();
    const auto& defs = g.storeItems();
    m.items.resize(m.itemIndex.size());
    for (size_t i = 0; i < m.itemIndex.size(); ++i) {
        const StoreItemDef& d = defs[m.itemIndex[i]];
        ItemVM& vm = m.items[i];
        std::string sprite = d.sprite.empty() ? "coin" : d.sprite;
        if (sprite.size() < 4 || sprite.compare(sprite.size() - 4, 4, ".png") != 0) sprite += ".png";
        vm.icon = "../sprites/" + sprite;   // relative to store.rml
        vm.name = L.field(d.name);
        vm.desc = L.field(d.desc);
        vm.effect = L.field(d.effect_text);
        vm.equipped = g.storeItemEquipped(d);
        if (!d.equipmentId.empty()) vm.status = vm.equipped ? L.tr("store.equipped") : L.tr("store.tap_to_equip");
        else vm.status = L.tr("store.purchased_label") + " x" + std::to_string(d.purchases);
        vm.price = std::to_string(d.liveCost());
        vm.selected = ((int)i == g.storeSel());
    }

    // Dirty only on change: re-evaluating data-for every frame would rebuild the cards and
    // restart their hover transitions.
    std::string sig = m.title + '\x1f' + m.hint + '\x1f' + m.gold_label + '\x1f' + m.buy_label + '\x1f' +
                      std::to_string(m.gold) + '\x1f' + (m.toast_visible ? m.toast : "") + '\x1f';
    for (auto& t : m.tabs) sig += t.label + (t.active ? "*" : "") + '\x1f';
    for (auto& it : m.items)
        sig += it.icon + it.name + it.desc + it.effect + it.status + it.price + (it.selected ? "*" : "") + '\x1f';
    if (sig != m.signature) { m.signature = std::move(sig); m.handle.DirtyAllVariables(); }

    const bool want = g.storeOpenFlag() && g.storeUiExternal();
    if (doc_ && want != visible_) {
        if (want) doc_->Show(); else doc_->Hide();
        visible_ = want;
    }
}

}  // namespace toms::next

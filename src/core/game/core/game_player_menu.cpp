// game_player_menu.cpp — the player menu (docs/20_PLAYER_MENU.md): the walking scene's one button (≡,
// Esc) opens five tabs -- Status, Equipment, Items, Events, System. This file keeps its state, its
// keyboard and mouse handling, and builds its part of the UI (UiPlayer; the System tab's rows are
// UiMenu, built by buildMenuUi in game_ui.cpp). The screen itself is assets/media/ui/player.rml.
#include "game_internal.h"
#include "player_menu_rules.h"   // stacking, the grid cursor, dropping (unit-tested)

using namespace toms::game_detail;

namespace {

int clampSel(int sel, int n) { return n <= 0 ? 0 : std::max(0, std::min(sel, n - 1)); }

}  // namespace
using toms::gridMove;

// ---------- opening, closing, tabs ----------

void Game::openPlayerMenu(int tab) {
    if (!inGameMenuOpen_) {
        inGameMenuOpen_ = true;
        inGameMenuPage_ = InGameMenuPage::Main;
        inGameMenuSel_ = 0;
        inGameLangConfirmOpen_ = false;
        dropConfirmOpen_ = false;
        audio.play("confirm_click");
    }
    if (tab >= 0 && tab < kMenuTabs) menuTab_ = (MenuTab)tab;
    playerMenuTab(0);   // marks the tab's news as seen
}

void Game::closePlayerMenu() {
    if (!inGameMenuOpen_) return;
    inGameMenuOpen_ = false;
    inGameMenuPage_ = InGameMenuPage::Main;
    inGameLangConfirmOpen_ = false;
    dropConfirmOpen_ = false;
    audio.play("close_ui");
}

void Game::toggleInventory() {
    if (inGameMenuOpen_ && menuTab_ == MenuTab::Items) { closePlayerMenu(); return; }
    if (inGameMenuOpen_) { menuTab_ = MenuTab::Items; playerMenuTab(0); return; }
    if (modalActive()) return;
    cancelWalk();
    openPlayerMenu((int)MenuTab::Items);
}

void Game::playerMenuTab(int delta) {
    if (!inGameMenuOpen_) return;
    if (delta != 0) {
        menuTab_ = (MenuTab)((((int)menuTab_ + delta) % kMenuTabs + kMenuTabs) % kMenuTabs);
        dropConfirmOpen_ = false;
        inGameLangConfirmOpen_ = false;
        audio.play("confirm_click");
    }
    if (menuTab_ == MenuTab::Gear) newsGear_ = false;
    if (menuTab_ == MenuTab::Events) newsEvents_ = false;
}

// ---------- the Items and Equipment grids ----------

std::vector<Game::ItemCell> Game::itemCells() const {
    std::vector<ItemCell> out;
    const bool all = itemFilter_ == 0;
    if (all || itemFilter_ == 1)                       // the backpack, copies stacked
        for (const toms::StackedItem& s : toms::stackItems(pl.inv)) out.push_back({ItemCell::Inv, s.id, s.count});
    if (all || itemFilter_ == 3) {                     // keys are counters on the player, not backpack items
        const std::pair<const char*, int> keys[] = {{"key_yellow", pl.key_yellow}, {"key_blue", pl.key_blue}, {"key_red", pl.key_red}};
        for (const auto& k : keys)
            if (k.second > 0) out.push_back({ItemCell::Key, k.first, k.second});
    }
    if (all || itemFilter_ == 2)                       // owned weapons, armor, talents
        for (const std::string& id : gearOwned_) out.push_back({ItemCell::Gear, id, 1});
    return out;
}

std::vector<std::string> Game::gearCells() const {
    const toms::EquipmentSlot want = gearSlot_ == 0 ? toms::EquipmentSlot::Weapon : toms::EquipmentSlot::Armor;
    std::vector<std::string> out;
    for (const std::string& id : gearOwned_) {
        auto it = equipmentDefs_.find(id);
        if (it != equipmentDefs_.end() && it->second.slot == want) out.push_back(id);
    }
    return out;
}

static bool worn(const toms::EquippedSet& eq, const std::string& id) {
    return !id.empty() && (id == eq.weaponId || id == eq.armorId || id == eq.talentId);
}

std::vector<Game::ItemButton> Game::itemButtons(const ItemCell& c) const {
    std::vector<ItemButton> out;
    const bool important = itemImportant(c.id) || c.kind == ItemCell::Key;
    if (c.kind == ItemCell::Inv) {
        out.push_back({ItemAction::Use, true});
        out.push_back({ItemAction::Drop, toms::canDrop(important, false)});
    } else if (c.kind == ItemCell::Key) {
        out.push_back({ItemAction::Drop, false});
    } else {
        const bool isWorn = worn(equipped_, c.id);
        auto it = equipmentDefs_.find(c.id);
        const bool weapon = it != equipmentDefs_.end() && it->second.slot == toms::EquipmentSlot::Weapon;
        if (!isWorn) out.push_back({ItemAction::Equip, true});
        else if (!weapon) out.push_back({ItemAction::Unequip, true});   // a weapon is swapped, never taken off
        out.push_back({ItemAction::Drop, toms::canDrop(important, isWorn)});
    }
    return out;
}

void Game::runItemAction(const ItemCell& c, ItemAction a) {
    switch (a) {
        case ItemAction::Use: {
            // The last copy goes, so the stack keeps its place in the grid (cells follow first pickups).
            auto it = std::find(pl.inv.rbegin(), pl.inv.rend(), c.id);
            if (it == pl.inv.rend()) return;
            pl.inv.erase(std::next(it).base());
            const std::string stage = curStage;
            applyItem(c.id);
            markProgressDirty();
            if (curStage != stage) closePlayerMenu();   // a scroll took the player to another floor
            break;
        }
        case ItemAction::Equip:
            equipGear(c.id);
            audio.play("confirm_click");
            break;
        case ItemAction::Unequip: {
            if (equipped_.armorId == c.id) equipped_.armorId.clear();
            if (equipped_.talentId == c.id) equipped_.talentId.clear();
            markProgressDirty();
            audio.play("close_ui");
            break;
        }
        case ItemAction::Drop: {
            if (c.kind == ItemCell::Inv) {
                auto it = std::find(pl.inv.rbegin(), pl.inv.rend(), c.id);
                if (it != pl.inv.rend()) pl.inv.erase(std::next(it).base());
            } else if (c.kind == ItemCell::Gear && !worn(equipped_, c.id)) {
                gearOwned_.erase(std::remove(gearOwned_.begin(), gearOwned_.end(), c.id), gearOwned_.end());
            }
            markProgressDirty();
            audio.play("close_ui");
            break;
        }
    }
}

// ---------- the Events tab ----------

std::vector<Game::LogEntry> Game::eventLog(bool all) const {
    std::vector<LogEntry> open, done;
    auto text = [&](const std::string& key) {
        const std::string t = locale_.tr(key);
        return t == key ? std::string() : t;
    };
    // Chapters: listed once the player has set foot in one of their floors; finished when their last
    // floor (the boss floor) is cleared -- the chapters' exitConditions, `stageCleared <boss floor>`.
    if (floorMode()) {
        const std::string here = curFloorId_.empty() ? run_.floor() : curFloorId_;
        std::vector<std::string> acts;
        for (const toms::FloorInfo& f : floors_.all())
            if (std::find(acts.begin(), acts.end(), f.act) == acts.end()) acts.push_back(f.act);
        for (const std::string& act : acts) {
            bool reached = false;
            std::string last, key;
            for (const toms::FloorInfo& f : floors_.all()) {
                if (f.act != act) continue;
                last = f.id; key = f.actKey;
                if (f.id == here || run_.floorCleared(f.id)) reached = true;
            }
            if (!reached) continue;
            LogEntry e;
            e.kind = 0;
            e.id = act;
            e.title = text("story." + key + ".title");
            if (e.title.empty()) e.title = act;
            e.desc = text("story." + key + ".desc");
            e.done = run_.floorCleared(last);
            (e.done ? done : open).push_back(e);
        }
        std::reverse(done.begin(), done.end());   // the newest finished chapter first
    }
    // Missions: listed once accepted; finished once the reward is claimed.
    for (const auto& [id, t] : missionTrackers_) {
        if (t.state != toms::MissionState::Active && t.state != toms::MissionState::Completed &&
            t.state != toms::MissionState::Claimed) continue;
        LogEntry e;
        e.kind = 1;
        e.id = id;
        e.title = text("mission." + id + ".title");
        if (e.title.empty()) e.title = id;
        e.desc = text("mission." + id + ".desc");
        e.done = t.state == toms::MissionState::Claimed;
        (e.done ? done : open).push_back(e);
    }
    if (all) open.insert(open.end(), done.begin(), done.end());
    return open;
}

// ---------- keyboard ----------

void Game::playerMenuMove(int dx, int dy) {
    if (!inGameMenuOpen_) return;
    if (dropConfirmOpen_) { if (dx != 0 || dy != 0) dropConfirmYes_ = !dropConfirmYes_; return; }
    switch (menuTab_) {
        case MenuTab::Status: {
            const int n = (int)attrDefs_.size();
            if (dy != 0 && n > 0) attrSel_ = attrSel_ < 0 ? 0 : ((attrSel_ + dy) % n + n) % n;
            break;
        }
        case MenuTab::Gear: {
            const int n = (int)gearCells().size();
            gearSel_ = clampSel(gearSel_, n);
            if (!gridMove(gearSel_, n, dx, dy)) { gearSlot_ = 1 - gearSlot_; gearSel_ = 0; }
            break;
        }
        case MenuTab::Items: {
            const int n = (int)itemCells().size();
            itemSel_ = clampSel(itemSel_, n);
            if (!gridMove(itemSel_, n, dx, dy)) { itemFilter_ = ((itemFilter_ + (dx > 0 ? 1 : -1)) % 4 + 4) % 4; itemSel_ = 0; }
            break;
        }
        case MenuTab::Events: {
            if (dx != 0) { eventFilter_ = 1 - eventFilter_; eventSel_ = 0; break; }
            const int n = (int)eventLog(eventFilter_ == 1).size();
            if (n > 0) eventSel_ = ((clampSel(eventSel_, n) + dy) % n + n) % n;
            break;
        }
        case MenuTab::System:
            if (dy != 0) inGameMenuMove(dy);
            else if (dx != 0 && inGameLangConfirmOpen_) inGameMenuMove(dx);
            break;
    }
}

void Game::playerMenuActivate() {
    if (!inGameMenuOpen_) return;
    if (dropConfirmOpen_) { playerMenuEvent("pm_confirm", dropConfirmYes_ ? 1 : 0); return; }
    switch (menuTab_) {
        case MenuTab::Gear: {
            const auto cells = gearCells();
            if (cells.empty()) return;
            const ItemCell c{ItemCell::Gear, cells[clampSel(gearSel_, (int)cells.size())], 1};
            for (const ItemButton& b : itemButtons(c))
                if (b.enabled && b.action != ItemAction::Drop) { runItemAction(c, b.action); return; }
            return;
        }
        case MenuTab::Items: {
            const auto cells = itemCells();
            if (cells.empty()) return;
            const ItemCell c = cells[clampSel(itemSel_, (int)cells.size())];
            const auto buttons = itemButtons(c);
            for (size_t i = 0; i < buttons.size(); i++)
                if (buttons[i].enabled) { playerMenuEvent("pm_btn", (int)i); return; }
            return;
        }
        case MenuTab::System:
            inGameMenuActivate();
            return;
        default:
            return;
    }
}

void Game::playerMenuBack() {
    if (!inGameMenuOpen_) return;
    if (dropConfirmOpen_) { dropConfirmOpen_ = false; audio.play("close_ui"); return; }
    if (menuTab_ == MenuTab::System) { inGameMenuBack(); return; }   // confirm -> page -> close
    closePlayerMenu();
}

// ---------- mouse / touch (the pm_* buttons of player.rml) ----------

void Game::playerMenuEvent(const std::string& name, int arg) {
    if (!inGameMenuOpen_) return;
    if (name == "pm_close") { closePlayerMenu(); return; }
    if (name == "pm_confirm") {
        if (!dropConfirmOpen_) return;
        dropConfirmOpen_ = false;
        if (arg && menuTab_ == MenuTab::Items) {
            const auto cells = itemCells();
            if (!cells.empty()) {
                const ItemCell c = cells[clampSel(itemSel_, (int)cells.size())];
                for (const ItemButton& b : itemButtons(c))
                    if (b.action == ItemAction::Drop && b.enabled) { runItemAction(c, ItemAction::Drop); break; }
                itemSel_ = clampSel(itemSel_, (int)itemCells().size());
            }
        } else {
            audio.play("close_ui");
        }
        return;
    }
    if (dropConfirmOpen_) return;   // the dialog is modal
    if (name == "pm_tab") {
        if (arg >= 0 && arg < kMenuTabs && arg != (int)menuTab_) { menuTab_ = (MenuTab)arg; playerMenuTab(0); audio.play("confirm_click"); }
        return;
    }
    if (name == "pm_attr") { if (arg >= 0 && arg < (int)attrDefs_.size()) attrSel_ = arg; return; }
    if (name == "pm_slot") { if ((arg == 0 || arg == 1) && arg != gearSlot_) { gearSlot_ = arg; gearSel_ = 0; } return; }
    if (name == "pm_filter") {
        if (menuTab_ == MenuTab::Items && arg >= 0 && arg < 4 && arg != itemFilter_) { itemFilter_ = arg; itemSel_ = 0; }
        if (menuTab_ == MenuTab::Gear && (arg == 0 || arg == 1) && arg != gearSlot_) { gearSlot_ = arg; gearSel_ = 0; }
        if (menuTab_ == MenuTab::Events && (arg == 0 || arg == 1) && arg != eventFilter_) { eventFilter_ = arg; eventSel_ = 0; }
        return;
    }
    if (name == "pm_cell") {
        if (menuTab_ == MenuTab::Items) itemSel_ = clampSel(arg, (int)itemCells().size());
        if (menuTab_ == MenuTab::Gear) gearSel_ = clampSel(arg, (int)gearCells().size());
        return;
    }
    if (name == "pm_btn") {
        ItemCell c;
        if (menuTab_ == MenuTab::Items) {
            const auto cells = itemCells();
            if (cells.empty()) return;
            c = cells[clampSel(itemSel_, (int)cells.size())];
        } else if (menuTab_ == MenuTab::Gear) {
            const auto cells = gearCells();
            if (cells.empty()) return;
            c = {ItemCell::Gear, cells[clampSel(gearSel_, (int)cells.size())], 1};
        } else {
            return;
        }
        const auto buttons = itemButtons(c);
        if (arg < 0 || arg >= (int)buttons.size() || !buttons[arg].enabled) { audio.play("deny"); return; }
        if (buttons[arg].action == ItemAction::Drop) { dropConfirmOpen_ = true; dropConfirmYes_ = false; return; }
        runItemAction(c, buttons[arg].action);
        if (menuTab_ == MenuTab::Items) itemSel_ = clampSel(itemSel_, (int)itemCells().size());
        return;
    }
    if (name == "pm_event") { eventSel_ = clampSel(arg, (int)eventLog(eventFilter_ == 1).size()); return; }
    if (name == "pm_sys") {   // a row of the System tab's left list (a page may be open on the right)
        if (inGameLangConfirmOpen_) return;
        inGameMenuPage_ = InGameMenuPage::Main;
        inGameMenuSel_ = arg;
        inGameMenuActivate();
        return;
    }
    if (name == "pm_sys_hover") { if (inGameMenuPage_ == InGameMenuPage::Main && !inGameLangConfirmOpen_) inGameMenuSel_ = arg; return; }
}

// ---------- the UI ----------

void Game::buildPlayerUi(toms::UiState& u) const {
    const toms::Locale& L = locale_;
    toms::UiPlayer& p = u.player;
    p.visible = true;
    p.tab = (int)menuTab_;
    p.hint = L.tr("player.hint");
    p.close_label = L.tr("inventory.close");
    static const char* kTabKeys[kMenuTabs] = {"player.tab.status", "player.tab.gear", "player.tab.items", "player.tab.events", "player.tab.system"};
    const bool tabNews[kMenuTabs] = {false, newsGear_, false, newsEvents_, newsStore_ || run_.skillPoints() > 0};
    for (int i = 0; i < kMenuTabs; i++) p.tabs.push_back({L.tr(kTabKeys[i]), i == (int)menuTab_, tabNews[i] && i != (int)menuTab_});
    const std::string none = L.tr("gear.none");
    auto signedNum = [](int v) { return (v >= 0 ? "+" : "") + std::to_string(v); };

    // The gear comparison: a piece against what its slot wears now.
    auto compare = [&](const std::string& id) {
        auto it = equipmentDefs_.find(id);
        if (it == equipmentDefs_.end() || it->second.slot == toms::EquipmentSlot::Talent) return;
        const toms::EquipmentDefinition& d = it->second;
        const bool weapon = d.slot == toms::EquipmentSlot::Weapon;
        const std::string& wornId = weapon ? equipped_.weaponId : equipped_.armorId;
        const toms::EquipmentDefinition* cur = nullptr;
        if (auto w = equipmentDefs_.find(wornId); w != equipmentDefs_.end()) cur = &w->second;
        p.d_compare = true;
        p.d_now_label = L.tr("gear.now");
        p.d_with_label = L.tr("gear.with");
        auto line = [&](const std::string& label, double now, double with, bool higherBetter, int decimals, const std::string& unit) {
            char a[32], b[32];
            std::snprintf(a, sizeof a, "%.*f%s", decimals, now, unit.c_str());
            std::snprintf(b, sizeof b, "%.*f%s", decimals, with, unit.c_str());
            int tone = 0;
            if (with > now + 1e-6) tone = higherBetter ? 1 : -1;
            if (with < now - 1e-6) tone = higherBetter ? -1 : 1;
            p.d_compare_lines.push_back({label, a, b, tone});
        };
        if (weapon) {
            const int base = effectiveAtk() - (cur ? cur->statAtk : 0);
            line("ATK", effectiveAtk(), base + d.statAtk, true, 0, "");
        } else {
            const int base = effectiveDef() - (cur ? cur->statDef : 0);
            line("DEF", effectiveDef(), base + d.statDef, true, 0, "");
        }
        const toms::PowerBarParams nowBar = cur ? cur->bar : toms::PowerBarParams{};
        line(L.tr("gear.ramp"), nowBar.rampTime, d.bar.rampTime, false, 1, "s");
        line(L.tr("gear.zone"), nowBar.greenHalf, d.bar.greenHalf, true, 0, "");
        if (weapon) line(L.tr("gear.mult"), cur ? cur->maxMult : 2.0f, d.maxMult, true, 1, "x");
        for (const AttrDef& a : attrDefs_) {
            const int nowBonus = cur && cur->stats.count(a.id) ? cur->stats.at(a.id) : 0;
            const int newBonus = d.stats.count(a.id) ? d.stats.at(a.id) : 0;
            if (nowBonus == 0 && newBonus == 0) continue;
            const int v = attrValue(a.id);
            line(a.shortName, v, v - nowBonus + newBonus, true, 0, "");
        }
        auto activeNames = [&](const toms::EquipmentDefinition* e) {
            std::string s;
            if (e) for (const std::string& a : e->actives) s += (s.empty() ? "" : ", ") + a;
            return s.empty() ? none : s;
        };
        const std::string na = activeNames(cur), nb = activeNames(&d);
        if (na != none || nb != none) p.d_compare_lines.push_back({L.tr("gear.skill"), na, nb, 0});
    };
    auto buttonsUi = [&](const std::vector<ItemButton>& bs) {
        for (const ItemButton& b : bs) {
            const char* key = b.action == ItemAction::Use ? "inventory.use" : b.action == ItemAction::Equip ? "gear.equip"
                            : b.action == ItemAction::Unequip ? "gear.unequip" : "inventory.drop";
            p.d_buttons.push_back({L.tr(key), b.enabled});
        }
    };
    auto itemDetail = [&](const ItemCell& c) {
        p.detail = true;
        p.d_icon = itemSprite(c.id);
        p.d_name = itemName(c.id);
        p.d_desc = itemDesc(c.id);
        p.d_lines.push_back({L.tr("store.effect_label"), itemEffectSummary(c.id), "", 0});
        const int price = itemPrice(c.id);
        p.d_lines.push_back({L.tr("store.price_label"),
                             price >= 0 ? trParam(L.tr("items.price_value"), "n", std::to_string(price)) : L.tr("items.not_sold"), "", 0});
        p.d_lines.push_back({L.tr("items.have"), std::to_string(c.count), "", 0});
        if (c.kind == ItemCell::Gear && !worn(equipped_, c.id)) compare(c.id);
        buttonsUi(itemButtons(c));
        if (c.kind == ItemCell::Key || itemImportant(c.id)) p.d_note = L.tr("items.important");
        else if (c.kind == ItemCell::Gear && worn(equipped_, c.id)) p.d_note = L.tr("items.worn");
    };

    // Status, Events and System are filled every frame whichever tab shows: their lists sit in
    // documents that stay bound while hidden. Equipment and Items share one block, filled for the tab shown.
    {
        {
            const int need = pl.lv * std::max(1, levelUp_.expPerLevel);
            p.level = "Lv " + std::to_string(pl.lv);
            p.hp_text = "HP " + std::to_string(pl.hp) + " / " + std::to_string(pl.maxhp);
            p.hp_pct = pl.maxhp > 0 ? std::max(0.0f, std::min(100.0f, 100.0f * pl.hp / pl.maxhp)) : 0.0f;
            p.exp_text = "EXP " + std::to_string(pl.exp) + " / " + std::to_string(need);
            p.exp_pct = std::max(0.0f, std::min(100.0f, 100.0f * pl.exp / need));
            p.combat_title = L.tr("status.combat");
            auto withBonus = [&](int eff, int base) { return std::to_string(eff) + (eff != base ? "  (" + signedNum(eff - base) + ")" : ""); };
            p.combat.push_back({"ATK", withBonus(effectiveAtk(), pl.atk), "", 0});
            p.combat.push_back({"DEF", withBonus(effectiveDef(), pl.def), "", 0});
            p.combat.push_back({L.tr("status.gold"), std::to_string(pl.gold), "", 0});
            p.combat.push_back({L.tr("hud.keys"), "Y " + std::to_string(pl.key_yellow) + "   B " + std::to_string(pl.key_blue) +
                                "   R " + std::to_string(pl.key_red), "", 0});
            p.combat.push_back({L.tr("status.floor"), st.name + " (" + std::to_string(st.index) + "/" + std::to_string(totalStages) + ")", "", 0});
            for (const LogEntry& e : eventLog(false))
                if (e.kind == 0) { p.combat.push_back({L.tr("status.chapter"), e.title, "", 0}); break; }
            auto gearName = [&](const std::string& id) { return id.empty() ? none : itemName(id); };
            p.more.push_back({L.tr("status.weapon"), gearName(equipped_.weaponId), "", 0});
            p.more.push_back({L.tr("status.armor"), gearName(equipped_.armorId), "", 0});
            std::string skills;
            for (const std::string& id : run_.skillsOwned()) {
                auto it = skillDefs_.find(id);
                skills += (skills.empty() ? "" : " · ") + (it == skillDefs_.end() ? id : L.field(it->second.name));
            }
            p.more.push_back({L.tr("ingame_menu.skills"), skills.empty() ? none : skills, "", 0});
            p.more.push_back({L.tr("status.shards"), std::to_string(run_.shardCount()), "", 0});
            std::string mind;
            for (const auto& [name, label] : counterLabels_)
                if (run_.counterVisible(name)) mind += (mind.empty() ? "" : " · ") + L.field(label);
            if (!mind.empty()) p.more.push_back({L.tr("status.story"), mind, "", 0});
            p.attr_title = L.tr("status.attributes");
            for (size_t i = 0; i < attrDefs_.size(); i++) {
                const AttrDef& a = attrDefs_[i];
                const int base = pl.attrs.count(a.id) ? pl.attrs.at(a.id) : 0, v = attrValue(a.id);
                toms::UiRow r;
                r.label = a.shortName + "   " + L.tr(a.nameKey);
                r.sub = std::to_string(v) + (v != base ? "  (" + signedNum(v - base) + ")" : "");
                r.selected = (int)i == attrSel_;
                p.attrs.push_back(r);
            }
            p.attr_desc = (attrSel_ >= 0 && attrSel_ < (int)attrDefs_.size()) ? L.tr(attrDefs_[attrSel_].descKey) : L.tr("status.attr_hint");
        }
        {
            const auto unfinished = eventLog(false), all = eventLog(true);
            p.ev_filters.push_back({L.tr("events.unfinished") + " (" + std::to_string(unfinished.size()) + ")", eventFilter_ == 0, false});
            p.ev_filters.push_back({L.tr("events.all") + " (" + std::to_string(all.size()) + ")", eventFilter_ == 1, false});
            const auto& log = eventFilter_ == 1 ? all : unfinished;
            const int sel = clampSel(eventSel_, (int)log.size());
            for (size_t i = 0; i < log.size(); i++) {
                const LogEntry& e = log[i];
                toms::UiRow r;
                r.label = std::string(e.done ? "✓ " : (e.kind == 0 ? "★ " : "◆ ")) + e.title;
                r.sub = e.done ? L.tr("events.done") : L.tr(e.kind == 0 ? "events.kind.chapter" : "events.kind.mission");
                r.enabled = !e.done;
                r.selected = (int)i == sel;
                p.ev_rows.push_back(r);
            }
            p.ev_empty = L.tr(eventFilter_ == 1 ? "events.empty_all" : "events.empty_unfinished");
            p.ev_hint = L.tr("events.select");
            if (!log.empty()) {
                const LogEntry& e = log[sel];
                p.ev_detail = true;
                p.ev_kind = L.tr(e.kind == 0 ? "events.kind.chapter" : "events.kind.mission") + (e.done ? "  ·  " + L.tr("events.done") : "");
                p.ev_title = e.title;
                p.ev_desc = e.desc;
                p.ev_connected_title = L.tr("events.connected");   // shown only when ev_connected has entries (P2)
            }
        }
        buildMenuUi(u.menu);
        p.sys_hint = L.tr("system.select");
    }
    switch (menuTab_) {
        case MenuTab::Gear: {
            p.filters.push_back({L.tr("status.weapon"), gearSlot_ == 0, false});
            p.filters.push_back({L.tr("status.armor"), gearSlot_ == 1, false});
            p.worn_title = L.tr("gear.worn");
            p.worn.push_back({L.tr("status.weapon"), equipped_.weaponId.empty() ? none : itemName(equipped_.weaponId), "", gearSlot_ == 0 ? 1 : 0});
            p.worn.push_back({L.tr("status.armor"), equipped_.armorId.empty() ? none : itemName(equipped_.armorId), "", gearSlot_ == 1 ? 1 : 0});
            const auto cells = gearCells();
            const int sel = clampSel(gearSel_, (int)cells.size());
            for (size_t i = 0; i < cells.size(); i++)
                p.cells.push_back({itemSprite(cells[i]), itemName(cells[i]), "", (int)i == sel, worn(equipped_, cells[i]), false});
            if (cells.empty()) p.empty_text = trParam(L.tr("gear.empty"), "slot", L.tr(gearSlot_ == 0 ? "status.weapon" : "status.armor"));
            p.select_hint = L.tr("gear.select");
            if (!cells.empty()) {
                const ItemCell c{ItemCell::Gear, cells[sel], 1};
                p.detail = true;
                p.d_icon = itemSprite(c.id);
                p.d_name = itemName(c.id);
                p.d_desc = itemDesc(c.id);
                if (!worn(equipped_, c.id)) compare(c.id);
                else p.d_note = L.tr("gear.worn");
                std::vector<ItemButton> bs;
                for (const ItemButton& b : itemButtons(c)) if (b.action != ItemAction::Drop) bs.push_back(b);
                buttonsUi(bs);
            }
            break;
        }
        case MenuTab::Items: {
            static const char* kFilters[4] = {"items.all", "items.use", "player.tab.gear", "hud.keys"};
            for (int i = 0; i < 4; i++) p.filters.push_back({L.tr(kFilters[i]), i == itemFilter_, false});
            const auto cells = itemCells();
            const int sel = clampSel(itemSel_, (int)cells.size());
            for (size_t i = 0; i < cells.size(); i++) {
                const ItemCell& c = cells[i];
                p.cells.push_back({itemSprite(c.id), itemName(c.id), c.count > 1 ? "x" + std::to_string(c.count) : "", (int)i == sel,
                                   c.kind == ItemCell::Gear && worn(equipped_, c.id), c.kind == ItemCell::Key || itemImportant(c.id)});
            }
            if (cells.empty()) p.empty_text = L.tr("inventory.empty_title");
            p.select_hint = L.tr("items.select");
            if (!cells.empty()) itemDetail(cells[sel]);
            if (dropConfirmOpen_ && !cells.empty()) {
                p.confirm_open = true;
                p.confirm_question = trParam(L.tr("items.drop_confirm"), "name", itemName(cells[sel].id));
                p.yes_label = L.tr("inventory.drop");
                p.no_label = L.tr("menu.back");
                p.confirm_yes = dropConfirmYes_;
            }
            break;
        }
        default:
            break;
    }
}

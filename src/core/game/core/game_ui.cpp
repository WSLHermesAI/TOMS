// game_ui.cpp -- the UI contract (see ui/ui_state.h): buildUiState() describes every screen as
// plain data each frame, uiEvent() carries out what a button in assets/media/ui/*.rml asked for.
// Replaced the hand-drawn UI (drawTitleScreen, drawInventory, drawStoreUI, ...) on 2026-09-27;
// every rule still lives in the Game functions these call.
#include "game_internal.h"

#include <cmath>

using namespace toms::game_detail;

namespace {

std::string spritePath(std::string id) {        // atlas sprite id -> image path used by the .rml files
    if (id.empty()) id = "coin";
    if (id.size() < 4 || id.compare(id.size() - 4, 4, ".png") != 0) id += ".png";
    return "../sprites/" + id;                   // relative to assets/media/ui/
}

std::string fmtPlayTime(int sec) {
    if (sec < 0) sec = 0;
    char b[32];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    return b;
}

float pct(float num, float den) {
    if (den <= 0.0f) return 0.0f;
    return std::max(0.0f, std::min(100.0f, 100.0f * num / den));
}

toms::UiPowerBar powerBar(const toms::PowerBarParams& p, const CombatState::AutoBar& bar,
                          const std::string& prompt, const std::string& label) {
    toms::UiPowerBar out;
    out.prompt = prompt;
    out.button_label = label;
    out.cooling = bar.cooling;
    const float span = 2.0f * p.redOuter;
    if (span <= 0.0f) return out;
    out.red_l = out.red_r = pct(p.redOuter - p.blueOuter, span);
    out.blue_l = out.blue_r = pct(p.blueOuter - p.greenHalf, span);
    out.green = pct(2.0f * p.greenHalf, span);
    out.marker = pct(bar.pos, span);
    return out;
}

}  // namespace

std::string Game::itemSpritePath(const std::string& id) const {
    auto it = itemDefs.find(id);
    return spritePath(it == itemDefs.end() ? "coin" : it->second.value("sprite", std::string("coin.png")));
}

std::string Game::itemEffectSummary(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end() || !it->second.contains("effect")) return locale_.tr("inventory.no_effect");
    const nlohmann::json& eff = it->second["effect"];
    std::vector<std::string> parts;
    auto add = [&](const char* key, const char* label) {
        if (eff.contains(key)) {
            int v = eff[key].get<int>();
            parts.push_back(std::string(label) + (v >= 0 ? " +" : " ") + std::to_string(v));
        }
    };
    add("str", "STR"); add("atk", "ATK"); add("def", "DEF"); add("hp", "HP"); add("mp", "MP");
    add("exp", "EXP"); add("gold", "Gold");
    if (eff.contains("warp")) parts.push_back("Warp");
    if (parts.empty()) parts.push_back(locale_.tr("inventory.no_effect"));
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? " • " : "") + parts[i];
    return out;
}

void Game::buildUiState(toms::UiState& u) const {
    u = toms::UiState{};
    const toms::Locale& L = locale_;

    // ---- title (the boot screen takes the whole screen) ----
    if (title_.isOpen()) {
        toms::UiTitle& t = u.title;
        t.visible = true;
        t.title = L.tr("game.title");
        t.subtitle = L.tr("game.subtitle");
        t.page = (int)title_.page();
        t.back_label = L.tr("menu.back");
        t.yes_label = L.tr("menu.yes");
        t.no_label = L.tr("menu.no");
        switch (title_.page()) {
            case toms::TitlePage::Menu: {
                static const char* keys[3] = {"menu.new_game", "menu.continue", "menu.settings"};
                for (int i = 0; i < 3; i++)
                    t.rows.push_back({L.tr(keys[i]), L.tr(std::string(keys[i]) + ".desc"), title_.menuSelection() == i, true});
                t.hint = L.tr("menu.hint");
                break;
            }
            case toms::TitlePage::Continue: {
                t.header = L.tr("continue.header");
                const auto& sums = title_.summaries();
                bool any = false;
                for (int i = 0; i < title_.slotCount(); i++) {
                    toms::UiRow r;
                    r.label = L.tr("continue.slot") + " " + std::to_string(i + 1);
                    if (i < (int)sums.size() && sums[i].exists) {
                        const toms::SlotSummary& s = sums[i];
                        any = true;
                        r.label += "   " + (s.stageName.empty() ? s.stageId : s.stageName) + "   " + L.tr("hud.level") +
                                   " " + std::to_string(s.lv) + "   HP " + std::to_string(s.hp) + "/" +
                                   std::to_string(s.maxhp) + "   " + std::to_string(s.gold) + "G";
                        r.sub = L.tr("continue.saved_at") + " " + s.savedAt + "    " + L.tr("continue.play_time") +
                                " " + fmtPlayTime(s.playTimeSec);
                    } else {
                        r.label += "   [" + L.tr("continue.empty") + "]";
                    }
                    r.selected = title_.slotSelection() == i;
                    t.rows.push_back(r);
                }
                t.show_back = true;
                t.show_empty_hint = !any;
                t.empty_hint = L.tr("continue.hint");
                t.hint = L.tr("menu.hint");
                if (title_.newGameConfirmOpen()) {
                    t.confirm_open = true;
                    t.confirm_question = trParam(L.tr("continue.new_game_confirm"), "slot",
                                                 std::to_string(title_.newGameConfirmSlot()));
                    t.confirm_body = L.tr("continue.new_game_confirm.body");
                    t.confirm_yes = title_.newGameConfirmYesSelected();
                }
                break;
            }
            case toms::TitlePage::Settings: {
                t.header = L.tr("settings.header");
                t.subheader = L.tr("settings.language");
                const auto& langs = L.languages();
                for (int i = 0; i < (int)langs.size(); i++)
                    t.rows.push_back({std::string(i == L.languageIndex() ? "[x] " : "[ ] ") + langs[i].name, "",
                                      title_.settingsSelection() == i, true});
                t.show_back = true;
                t.hint = L.tr("settings.hint");
                if (title_.languageConfirmOpen()) {
                    const int idx = title_.languageConfirmIndex();
                    t.confirm_open = true;
                    t.confirm_question = trParam(L.tr("settings.language_confirm"), "lang",
                                                 (idx >= 0 && idx < (int)langs.size()) ? langs[idx].name : "");
                    t.confirm_yes = title_.languageConfirmYesSelected();
                }
                break;
            }
        }
        return;
    }

    // ---- ending (also takes the whole screen) ----
    if (endingActive()) {
        toms::UiEnding& e = u.ending;
        e.visible = true;
        const toms::EndingDefinition* def = nullptr;
        for (auto& d : endingsTable_.endings) if (d.id == activeEndingId_) { def = &d; break; }
        e.name = def ? L.tr(def->nameKey) : activeEndingId_;
        e.text = def ? L.tr(def->textKey) : "";
        e.rebirth = rebirthOffered();
        e.rebirth_label = L.tr("ending.rebirth_button");
        e.title_label = L.tr("ending.title_button");
        e.hint = L.tr("ending.dismiss_hint");
        return;
    }

    // Which scene is up -- the same precedence Game::draw() uses for the world.
    const bool showStore = storeModal();
    const bool showBattle = !showStore && (hideMask & 1) == 0 && (cs.active || cs.won || cs.resultPauseMs > 0);
    const bool showTalk = !showStore && !showBattle && (hideMask & 2) == 0 && inDialogue;
    const bool showInv = !showStore && !showBattle && !showTalk && (hideMask & 4) == 0 && invOpen;
    const bool showWalk = !showStore && !showBattle && !showTalk && !showInv;

    // ---- HUD (over the map) ----
    if (showWalk) {
        toms::UiHud& h = u.hud;
        h.visible = true;
        h.title_line = L.tr("game.title") + " — " + st.name + " (" + std::to_string(st.index) + "/" +
                       std::to_string(totalStages) + ")";
        h.hp_pct = pct((float)pl.hp, (float)pl.maxhp);
        h.hp_text = "HP " + std::to_string(pl.hp) + "/" + std::to_string(pl.maxhp);
        h.stats_line = "ATK " + std::to_string(pl.atk) + "   DEF " + std::to_string(pl.def) + "   LV " + std::to_string(pl.lv);
        h.res_line = "GOLD " + std::to_string(pl.gold) + "   EXP " + std::to_string(pl.exp) + "   " + L.tr("hud.keys") +
                     " Y" + std::to_string(pl.key_yellow) + " B" + std::to_string(pl.key_blue) + " R" +
                     std::to_string(pl.key_red) + "   " + L.tr("hud.items") + " x" + std::to_string(pl.inv.size()) + " (I)";
        // The floor's story line: its intro, then its ambient lines as the player moves.
        h.footer = st.story_note;
        if (!storyIntroKey_.empty()) {
            const int idx = storyLineIndex();
            const std::string& key = (idx == 0 || storyAmbientKeys_.empty())
                                     ? storyIntroKey_ : storyAmbientKeys_[(size_t)(idx - 1) % storyAmbientKeys_.size()];
            std::string tr = L.tr(key);
            if (!tr.empty() && tr.rfind("story.", 0) != 0) h.footer = tr;
        }
        h.icons = !inGameMenuOpen_;
        h.store_unlocked = storeUnlocked_;
        h.store_label = L.tr("store.icon_label");
        h.menu_label = L.tr("ingame_menu.title");
        if (chapterCardMs_ > 0.0f && !chapterCardTitle_.empty()) {
            h.chapter_card = true;
            h.chapter_title = chapterCardTitle_;
            h.chapter_sub = st.name;
            h.chapter_alpha = std::min(1.0f, chapterCardMs_ / 600.0f);
        }
        // Name plates over bosses and roamers, at the same place the map draws them.
        float ts; int cols, rows; cameraViewportTiles(ts, cols, rows);
        const float ox = -cam_.x() * ts, oy = 60.0f - cam_.y() * ts, inset = ts / 6.0f;
        const float W = ren ? (float)ren->width() : 1024.0f, H = ren ? (float)ren->height() : 768.0f;
        for (const auto& e : st.entities) {
            if (e.consumed || !(e.fp.bossTier() || e.roamer)) continue;
            const float x = ox + e.x * ts + inset, y = oy + e.y * ts + inset, w = e.fp.w * ts - 2.0f * inset;
            if (x + w < 0 || x > W || y < 0 || y > H) continue;
            h.banners.push_back({e.displayName.empty() ? e.id : e.displayName, x + w * 0.5f, y - 2.0f});
        }

        u.pad.visible = !modalActive() && !cs.won;
        u.pad.on = gpOn;
        u.pad.scale = kPadScale;

        if (stairsConfirmOpen_) {
            u.stairs.visible = true;
            u.stairs.question = L.tr(stairsConfirmIsUp_ ? "stairs.confirm_up" : "stairs.confirm_down");
            u.stairs.yes_label = L.tr("menu.yes") + " (Enter)";
            u.stairs.no_label = L.tr("menu.no") + " (Esc)";
        }
        if (inGameMenuOpen_) buildMenuUi(u.menu);
        if (stageSelectOpen_) {
            toms::UiStageSelect& s = u.stage_select;
            s.visible = true;
            s.title = L.tr("stageselect.title");
            s.hint = L.tr("stageselect.hint");
            s.close_label = L.tr("inventory.close");
            for (size_t i = 0; i < stageList_.size(); i++) {
                const StageInfo& info = stageList_[i];
                auto has = [&](const std::string& id) {
                    return std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), id) != meta_.unlockedStages.end();
                };
                const bool reached = has(info.id);
                const bool locked = info.index > 1 && i > 0 && !has(stageList_[i - 1].id);
                toms::UiRow r;
                r.label = std::to_string(info.index) + ". " + info.name;
                if (reached) r.label += "  " + L.tr("stageselect.reached_tag");
                else if (!locked) r.label += "  " + L.tr("stageselect.new_tag");
                r.enabled = !locked;
                r.sub = locked ? trParam(L.tr("stageselect.locked_hint"), "floor", std::to_string(info.index - 1)) : "";
                if (!info.preview.empty()) r.sub += (r.sub.empty() ? "" : "   ") + info.preview;
                s.rows.push_back(r);
            }
        }
    }

    // ---- battle ----
    if (showBattle) {
        toms::UiBattle& b = u.battle;
        b.visible = true;
        b.active = cs.active;
        b.title = L.tr("battle.title") + " " + cs.enemy.name;
        b.player_sprite = spritePath("player");
        b.enemy_sprite = spritePath(cs.enemy.boss ? "boss_demonlord" : entSprite(cs.enemy.id));
        b.player_hp_pct = pct((float)cs.playerHP, (float)pl.maxhp);
        b.enemy_hp_pct = pct((float)std::max(0, cs.enemyHP), (float)cs.enemy.hp);
        b.player_hp_text = L.tr("battle.you") + " HP " + std::to_string(cs.playerHP);
        b.enemy_hp_text = cs.enemy.name + " HP " + std::to_string(std::max(0, cs.enemyHP));
        b.log = cs.log;
        b.continue_hint = L.tr("battle.continue_hint");
        if (cs.active) {
            b.clock_label = L.tr("battle.enemy_clock");
            b.clock_pct = pct((float)cs.enemyClockMs, (float)std::max(500, cs.enemy.atkIntervalMs));
            b.attack = powerBar(toms::effectiveAttackBar(equipped_, equipmentDefs_), cs.atkBar, L.tr("battle.attack_prompt"),
                                L.tr(cs.atkBar.cooling ? "battle.btn_charging" : "battle.btn_hold_attack"));
            b.defense = powerBar(toms::effectiveDefenseBar(equipped_, equipmentDefs_), cs.defBar, L.tr("battle.defense_prompt"),
                                 L.tr(cs.defBar.cooling ? "battle.btn_charging" : "battle.btn_hold_defense"));
            b.shield_armed = cs.shieldBanked;
            b.shield_text = cs.shieldBanked
                ? trParam(L.tr("battle.shield_armed"), "pct", std::to_string((int)std::lround(cs.shieldPower)))
                : L.tr("battle.shield_none");
            b.crit_armed = cs.nextAttackGuaranteedCrit;
            b.crit_hint = L.tr("battle.active_armed_hint");
            b.super_label = L.tr("battle.super_label");
            for (int i = 0; i < run_.superMax(); i++) b.super_dots.push_back(i < cs.superCharge ? 1 : 0);
            b.super_ready = cs.superCharge >= run_.superMax();
            b.super_button = L.tr("battle.super_ready");
            auto activeIds = toms::equippedActives(equipped_, equipmentDefs_);
            if (!activeIds.empty() && toms::canUseActive(activeIds[0], activeDefs_, cs.activeRuntime, cs.activeStatus)) {
                b.active_ready = true;   // (ActiveDefinition has no display name yet: the id is shown)
                b.active_button = trParam(L.tr("battle.active_ready"), "name", activeIds[0]);
            }
        }
    }

    // ---- dialogue ----
    if (showTalk) {
        toms::UiDialogue& d = u.dialogue;
        d.visible = true;
        d.scale = uiScale_;
        if (dlgData.contains("nodes") && dlgData["nodes"].contains(dlgNode) && dlgData["nodes"][dlgNode].contains("text"))
            d.text = L.field(dlgData["nodes"][dlgNode]["text"]);
        for (size_t i = 0; i < dlgChoices.size(); i++)
            d.choices.push_back({dlgChoices[i].label, "", (int)i == dlgSel, true});
    }

    // ---- inventory ----
    if (showInv) {
        toms::UiInventory& v = u.inventory;
        v.visible = true;
        v.title = L.tr("inventory.title");
        v.hint = L.tr("inventory.hint");
        v.empty_title = L.tr("inventory.empty_title");
        v.empty_hint = L.tr("inventory.empty_hint");
        v.detail_title = L.tr("inventory.detail_title");
        v.use_label = L.tr("inventory.use");
        v.drop_label = L.tr("inventory.drop");
        v.close_label = L.tr("inventory.close");
        v.footer_hint = L.tr("inventory.footer_hint");
        v.icon_label = L.tr("inventory.icon_label");
        v.stats_label = L.tr("inventory.stats_label");
        v.empty = pl.inv.empty();
        for (size_t i = 0; i < pl.inv.size(); i++)
            v.items.push_back({itemSpritePath(pl.inv[i]), itemName(pl.inv[i]), itemEffectSummary(pl.inv[i]), (int)i == invSel});
        if (!pl.inv.empty()) {
            const std::string& id = pl.inv[std::max(0, std::min(invSel, (int)pl.inv.size() - 1))];
            v.d_icon = itemSpritePath(id);
            v.d_name = itemName(id);
            v.d_id = "ID: " + id;
            auto it = itemDefs.find(id);
            v.d_icon_file = it == itemDefs.end() ? "" : it->second.value("sprite", std::string());
            v.d_desc = itemDesc(id);
            if (it != itemDefs.end() && it->second.contains("effect")) {
                const nlohmann::json& eff = it->second["effect"];
                static const char* keys[][2] = {{"str", "STR"}, {"atk", "ATK"}, {"def", "DEF"}, {"hp", "HP"},
                                                {"mp", "MP"}, {"exp", "EXP"}, {"gold", "Gold"}};
                for (auto& k : keys)
                    if (eff.contains(k[0])) v.d_pills.push_back(std::string(k[1]) + " +" + std::to_string(eff[k[0]].get<int>()));
                if (eff.contains("warp")) v.d_pills.push_back("Warp");
            }
            if (v.d_pills.empty()) v.d_pills.push_back(L.tr("inventory.no_effect"));
        }
    }

    // ---- store ----
    if (storeOpen) {
        toms::UiStore& s = u.store;
        s.visible = true;
        s.title = L.field(storeTitle_);
        s.hint = L.tr("store.controls_hint");
        s.gold_label = L.tr("store.gold_label");
        s.close_label = L.tr("inventory.close");
        s.effect_label = L.tr("store.effect_label");
        s.price_label = L.tr("store.price_label");
        s.buy_label = L.tr("store.buy_button");
        s.gold = pl.gold;
        static const char* tabKeys[4] = {"store.tab.potion", "store.tab.weapon", "store.tab.armor", "store.tab.talent"};
        for (int i = 0; i < 4; i++) s.tabs.push_back({L.tr(tabKeys[i]), i == storeTab_});
        const std::vector<int> idx = storeTabIndices();
        for (size_t i = 0; i < idx.size(); i++) {
            const StoreItemDef& d = storeItems_[idx[i]];
            toms::UiStoreItem it;
            it.icon = spritePath(d.sprite);
            it.name = L.field(d.name);
            it.desc = L.field(d.desc);
            it.effect = L.field(d.effect_text);
            it.equipped = !d.equipmentId.empty() && (d.equipmentId == equipped_.weaponId ||
                          d.equipmentId == equipped_.armorId || d.equipmentId == equipped_.talentId);
            it.status = !d.equipmentId.empty() ? L.tr(it.equipped ? "store.equipped" : "store.tap_to_equip")
                                               : L.tr("store.purchased_label") + " x" + std::to_string(d.purchases);
            it.price = std::to_string(d.liveCost());
            it.selected = (int)i == storeSel_;
            s.items.push_back(it);
        }
    }
    if (storeUnlockDlg) {
        u.store.unlocked_dialog = true;
        u.store.unlocked_title = L.tr("store.unlocked_title");
        u.store.unlocked_body = L.tr("store.unlocked_body");
        u.store.unlocked_ok = L.tr("store.confirm_hint");
    }

    // ---- always on top ----
    u.overlay.toast = toastTimer_ > 0 && !toastMsg_.empty();
    u.overlay.toast_text = toastMsg_;
    for (const auto& n : notifications_) u.overlay.notifications.push_back(n.first);
}

void Game::buildMenuUi(toms::UiMenu& m) const {
    const toms::Locale& L = locale_;
    m.visible = true;
    m.yes_label = L.tr("menu.yes");
    m.no_label = L.tr("menu.no");
    m.close_label = L.tr("menu.back");
    auto sel = [&](size_t i) { return (int)i == inGameMenuSel_; };
    switch (inGameMenuPage_) {
        case InGameMenuPage::Main: {
            m.page = 0;
            m.header = L.tr("ingame_menu.title");
            m.close_label = L.tr("inventory.close");
            auto order = mainMenuOrder();
            for (size_t i = 0; i < order.size(); i++) {
                const char* key = "ingame_menu.save";
                switch (order[i]) {
                    case MainMenuRow::Save:        key = "ingame_menu.save"; break;
                    case MainMenuRow::Settings:    key = "menu.settings"; break;
                    case MainMenuRow::Skills:      key = "ingame_menu.skills"; break;
                    case MainMenuRow::Village:     key = "ingame_menu.village"; break;
                    case MainMenuRow::Forge:       key = "ingame_menu.forge"; break;
                    case MainMenuRow::BackToTitle: key = "ingame_menu.back_to_title"; break;
                }
                m.rows.push_back({L.tr(key), "", sel(i), true});
            }
            break;
        }
        case InGameMenuPage::Settings: {
            m.page = 1;
            m.header = L.tr("settings.header");
            m.subheader = L.tr("settings.language");
            const auto& langs = L.languages();
            for (size_t i = 0; i < langs.size(); i++)
                m.rows.push_back({std::string((int)i == L.languageIndex() ? "[x] " : "[ ] ") + langs[i].name, "", sel(i), true});
            m.rows.push_back({L.tr(cam_.mode() == toms::Camera::Mode::Rooms ? "settings.camera_rooms" : "settings.camera_follow"),
                              "", sel(langs.size()), true});
            break;
        }
        case InGameMenuPage::Skills: {
            m.page = 2;
            m.header = L.tr("ingame_menu.skills");
            m.subheader = trParam(L.tr("skill.points_label"), "n", std::to_string(run_.skillPoints()));
            auto order = skillMenuOrder();
            for (size_t i = 0; i < order.size(); i++) {
                const auto& def = skillDefs_.at(order[i]);
                const bool owned = run_.hasSkill(order[i]);
                const bool unlockable = !owned && toms::canUnlockSkill(skillDefs_, run_.skillsOwned(), run_.skillPoints(), order[i]);
                const std::string mark = owned ? "[x] " : (unlockable ? "[ ] " : "[--] ");
                const std::string name = L.field(def.name);
                m.rows.push_back({owned ? mark + name
                                        : trParam(mark + name + " (" + L.tr("skill.cost_suffix") + ")", "n", std::to_string(def.cost)),
                                  "", sel(i), owned || unlockable});
            }
            break;
        }
        case InGameMenuPage::Forge: {
            m.page = 3;
            m.header = L.tr("ingame_menu.forge");
            m.subheader = trParam(L.tr("forge.gold_label"), "n", std::to_string(pl.gold));
            auto order = forgeMenuOrder();
            for (size_t i = 0; i < order.size(); i++) {
                const auto& def = forgeDefs_.at(order[i]);
                const bool known = std::find(meta_.forgeRecipesKnown.begin(), meta_.forgeRecipesKnown.end(), order[i]) !=
                                   meta_.forgeRecipesKnown.end();
                const bool craftable = known && toms::canCraft(forgeDefs_, meta_.forgeRecipesKnown, pl.inv, pl.gold, order[i]);
                std::string label = std::string(!known ? "[--] " : (craftable ? "[ ] " : "[!] ")) +
                                    (known ? L.field(def.name) : L.tr("forge.unknown_recipe"));
                if (known) label += " (" + std::to_string(def.costGold) + "g)";
                m.rows.push_back({label, "", sel(i), craftable});
            }
            break;
        }
        case InGameMenuPage::Hub: {
            m.page = 4;
            m.header = L.tr("ingame_menu.village");
            auto order = hubMenuOrder();
            auto hasFlag = [this](const std::string& f) { return run_.flag(f); };
            for (size_t i = 0; i < order.size(); i++) {
                const bool unlocked = toms::hubLocationUnlocked(hubDefs_, hasFlag, order[i]);
                m.rows.push_back({std::string(unlocked ? "[ ] " : "[--] ") +
                                  (unlocked ? L.field(hubDefs_.at(order[i]).name) : L.tr("hub.unknown_location")),
                                  "", sel(i), unlocked});
            }
            break;
        }
    }
    if (inGameLangConfirmOpen_) {
        const auto& langs = L.languages();
        m.confirm_open = true;
        m.confirm_question = trParam(L.tr("settings.language_confirm"), "lang",
                                     (inGameLangConfirmIdx_ >= 0 && inGameLangConfirmIdx_ < (int)langs.size())
                                     ? langs[inGameLangConfirmIdx_].name : "");
        m.confirm_yes = inGameLangConfirmYes_;
    }
}

void Game::uiEvent(const std::string& name, int arg) {
    // ---- title ----
    if (name == "title_row")     { if (title_.isOpen()) handleTitleAction(title_.clickRow(arg)); return; }
    if (name == "title_hover")   { if (title_.isOpen()) title_.hoverRow(arg); return; }
    if (name == "title_back")    { if (title_.isOpen()) handleTitleAction(title_.cancel()); return; }
    if (name == "title_confirm") { if (title_.isOpen()) handleTitleAction(title_.answerConfirm(arg != 0)); return; }
    // ---- ending ----
    if (name == "ending_rebirth") { if (endingActive()) rebirth(); return; }
    if (name == "ending_title")   { if (endingActive()) dismissEndingScreen(); return; }
    if (name == "ending_dismiss") { if (endingActive() && !rebirthOffered()) dismissEndingScreen(); return; }
    // ---- HUD buttons ----
    if (name == "hud_menu")      { if (!modalActive()) { cancelWalk(); openInGameMenu(); } return; }
    if (name == "hud_store")     { if (!modalActive()) { cancelWalk(); openStore(); } return; }
    if (name == "hud_inventory") { if (!modalActive()) { cancelWalk(); toggleInventory(); } return; }
    // ---- virtual gamepad (press/release: holding a plate keeps walking) ----
    if (name == "pad_dir") {
        if (modalActive()) return;
        if (arg == 0) setMoveHeldY(-1); else if (arg == 1) setMoveHeldY(1);
        else if (arg == 2) setMoveHeldX(-1); else if (arg == 3) setMoveHeldX(1);
        return;
    }
    if (name == "pad_release") { stopMoveHeld(); return; }
    if (name == "pad_a")       { cancelWalk(); interact(); return; }
    if (name == "pad_toggle")  { gpOn = !gpOn; return; }
    // ---- battle ----
    if (name == "battle_attack")   { battleTapAttack(); return; }
    if (name == "battle_defend")   { battleTapDefense(); return; }
    if (name == "battle_super")    { battleTapSuper(); return; }
    if (name == "battle_active")   { battleTapActive(); return; }
    if (name == "battle_continue") { if (cs.won) dismissVictory(); return; }
    // ---- dialogue ----
    if (name == "dlg_choose")  { if (inDialogue && arg >= 0 && arg < (int)dlgChoices.size()) { dlgSel = arg; chooseDialogue(arg); } return; }
    if (name == "dlg_hover")   { if (inDialogue && arg >= 0 && arg < (int)dlgChoices.size()) dlgSel = arg; return; }
    if (name == "dlg_advance") { if (inDialogue) chooseDialogue(dlgSel); return; }
    // ---- inventory ----
    if (name == "inv_select") { if (invOpen && arg >= 0 && arg < (int)pl.inv.size()) invSel = arg; return; }
    if (name == "inv_use")    { invUseSelected(); return; }
    if (name == "inv_drop")   { invDropSelected(); return; }
    if (name == "inv_close")  { if (invOpen) toggleInventory(); return; }
    // ---- store ----
    if (name == "store_tab") {
        if (storeOpen && arg >= 0 && arg < 4 && arg != storeTab_) { storeTab_ = arg; storeSel_ = 0; audio.play("confirm_click"); }
        return;
    }
    if (name == "store_hover") { if (storeOpen && arg >= 0 && arg < (int)storeTabIndices().size()) storeSel_ = arg; return; }
    if (name == "store_buy") {
        const std::vector<int> idx = storeTabIndices();
        if (storeOpen && arg >= 0 && arg < (int)idx.size()) { storeSel_ = arg; buyStoreItem(idx[arg]); }
        return;
    }
    if (name == "store_close")       { if (storeOpen) closeStore(); return; }
    if (name == "store_unlocked_ok") { if (storeUnlockDlg) storeKey(13); return; }
    // ---- in-game menu ----
    if (name == "menu_row")     { if (inGameMenuOpen_ && !inGameLangConfirmOpen_) { inGameMenuSel_ = arg; inGameMenuActivate(); } return; }
    if (name == "menu_hover")   { if (inGameMenuOpen_ && !inGameLangConfirmOpen_) inGameMenuSel_ = arg; return; }
    if (name == "menu_close")   { if (inGameMenuOpen_) { if (inGameMenuPage_ == InGameMenuPage::Main) closeInGameMenu(); else inGameMenuBack(); } return; }
    if (name == "menu_confirm") { if (inGameLangConfirmOpen_) { inGameLangConfirmYes_ = arg != 0; inGameMenuActivate(); } return; }
    // ---- stairs ----
    if (name == "stairs") { if (stairsConfirmOpen_) { if (arg) confirmStageTransition(); else cancelStageTransition(); } return; }
    // ---- stage select ----
    if (name == "stage_pick") {
        if (!stageSelectOpen_ || arg < 0 || arg >= (int)stageList_.size()) return;
        const StageInfo& info = stageList_[arg];
        const bool locked = info.index > 1 && arg > 0 &&
            std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), stageList_[arg - 1].id) == meta_.unlockedStages.end();
        if (locked) return;
        loadStage(info.id);
        closeStageSelect();
        return;
    }
    if (name == "stage_close") { if (stageSelectOpen_) closeStageSelect(); return; }
}

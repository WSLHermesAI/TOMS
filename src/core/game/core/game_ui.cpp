// game_ui.cpp -- the UI contract (see ui/ui_state.h): buildUiState() describes every screen as
// plain data each frame, uiEvent() carries out what a button in assets/media/ui/*.rml asked for.
// Replaced the hand-drawn UI (drawTitleScreen, drawInventory, drawStoreUI, ...) on 2026-09-27;
// every rule still lives in the Game functions these call.
#include "game_internal.h"

#include <cmath>

using namespace toms::game_detail;

namespace {

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

// atlas sprite id (or "<id>.png") -> the sprite name the .rml files use (uiSpritesheet). The art
// style in use is the atlas loaded, so the UI matches the map without naming the style.
std::string Game::uiSprite(std::string id) const {
    if (id.empty()) id = "coin";
    if (id.size() > 4 && id.compare(id.size() - 4, 4, ".png") == 0) id.resize(id.size() - 4);
    return toms::rmlSpriteName(id);
}

// The page images are named as seen from assets/media/ui/, where the .rml documents are.
std::string Game::uiSpritesheet() const {
    if (spriteAtlas_.pages.empty()) return std::string();
    const std::string dir = loadedArtStyle_ > 0 ? "../styles/" + artStyles_[loadedArtStyle_].id + "/atlas/" : "../atlas/";
    return toms::rmlSpritesheets(spriteAtlas_, dir);
}

std::vector<std::string> Game::missingUiSprites() const {
    std::vector<std::string> names(SPRITE_ORDER, SPRITE_ORDER + N_SPRITES);
    for (const auto& kv : itemDefs) names.push_back(kv.second.value("sprite", std::string()));

    std::vector<std::string> missing;
    for (std::string n : names) {
        if (n.empty()) continue;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".png") == 0) n.resize(n.size() - 4);
        if (!spriteAtlas_.find(n) && std::find(missing.begin(), missing.end(), n) == missing.end()) missing.push_back(n);
    }
    return missing;
}

std::string Game::artStyleName(int idx) const {
    if (idx <= 0 || idx >= (int)artStyles_.size()) return locale_.tr("settings.style_original");
    return locale_.field(artStyles_[idx].name);
}

// The Settings pages' art style section (none when only the original art exists). `selected` is
// the highlighted style index (out of range = none). The chosen style is ticked; while it is not
// the one in use (chosen during a run), its row says it applies back on the title screen.
void Game::appendArtStyleRows(std::vector<toms::UiRow>& rows, int selected) const {
    if (artStyles_.size() < 2) return;
    const int chosen = chosenArtStyle();
    for (int i = 0; i < (int)artStyles_.size(); i++) {
        toms::UiRow r;
        if (i == 0) r.section = locale_.tr("settings.style_section");
        r.label = std::string(i == chosen ? "[x] " : "[ ] ") + artStyleName(i);
        if (i == chosen && chosen != loadedArtStyle_) r.sub = locale_.tr("settings.style_pending");
        r.selected = i == selected;
        rows.push_back(r);
    }
}

std::string Game::itemSprite(const std::string& id) const {
    auto it = itemDefs.find(id);
    return uiSprite(it == itemDefs.end() ? "coin" : it->second.value("sprite", std::string("coin.png")));
}

std::string Game::itemEffectSummary(const std::string& id) const {
    // "HP +40 • ATK +1 • STR +2": the item's stats (data/items.json), in a fixed order, then the
    // attributes of data/stats.json by their short names.
    const nlohmann::json& eff = itemStats(id);
    std::vector<std::string> parts;
    auto add = [&](const std::string& key, const std::string& label) {
        if (eff.contains(key) && eff[key].is_number()) {
            int v = eff[key].get<int>();
            if (v == 0 && isGearType(itemType(id))) return;   // a worn piece's "atk: 0" says nothing
            parts.push_back(label + (v >= 0 ? " +" : " ") + std::to_string(v));
        }
    };
    add("hp", "HP"); add("maxhp", "Max HP"); add("atk", "ATK"); add("def", "DEF");
    add("exp", "EXP"); add("gold", locale_.tr("status.gold"));
    for (const char* k : {"key_yellow", "key_blue", "key_red"}) add(k, itemName(k));
    for (const AttrDef& a : attrDefs_) add(a.id, a.shortName);
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
                        if (s.fromBackup) r.sub += "    [" + L.tr("continue.from_backup") + "]";
                        if (s.newer) r.sub += "    [" + L.tr("continue.newer") + "]";
                    } else if (i < (int)sums.size() && sums[i].damaged) {
                        r.label += "   [" + L.tr("continue.damaged") + "]";
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
                const auto& langs = L.languages();
                for (int i = 0; i < (int)langs.size(); i++)
                    t.rows.push_back({std::string(i == L.languageIndex() ? "[x] " : "[ ] ") + langs[i].name, "",
                                      title_.settingsSelection() == i, true});
                if (!t.rows.empty()) t.rows[0].section = L.tr("settings.language");   // section 1: language
                appendArtStyleRows(t.rows, title_.settingsSelection() - (int)langs.size());   // section 2: art style
                t.show_back = true;
                t.hint = L.tr("settings.hint");
                if (title_.styleConfirmOpen()) {
                    t.confirm_open = true;
                    t.confirm_question = trParam(L.tr("settings.style_confirm"), "style",
                                                 artStyleName(title_.styleConfirmIndex()));
                    t.confirm_body = L.tr("settings.style_apply_now");   // the title reloads the images at once
                    t.confirm_yes = title_.styleConfirmYesSelected();
                }
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
    const bool showWalk = !showStore && !showBattle && !showTalk;

    // ---- HUD (over the map) ----
    if (showWalk) {
        toms::UiHud& h = u.hud;
        h.visible = !inGameMenuOpen_;   // the player menu covers the screen
        h.title_line = L.tr("game.title") + " — " + st.name + " (" + std::to_string(st.index) + "/" +
                       std::to_string(totalStages) + ")";
        h.hp_pct = pct((float)pl.hp, (float)effectiveMaxHp());
        h.hp_text = "HP " + std::to_string(pl.hp) + "/" + std::to_string(effectiveMaxHp());
        h.stats_line = "ATK " + std::to_string(effectiveAtk()) + "   DEF " + std::to_string(effectiveDef()) + "   LV " + std::to_string(pl.lv);
        h.res_line = "GOLD " + std::to_string(pl.gold) + "   " + L.tr("hud.crystals") + " " + std::to_string(pl.crystals) +
                     "   EXP " + std::to_string(pl.exp) + "   " + L.tr("hud.keys") +
                     " Y" + std::to_string(pl.key_yellow) + " B" + std::to_string(pl.key_blue) + " R" +
                     std::to_string(pl.key_red);
        // The floor's story line: its intro, then its ambient lines as the player moves.
        h.footer = st.story_note;
        if (!storyIntroKey_.empty()) {
            const int idx = storyLineIndex();
            const std::string& key = (idx == 0 || storyAmbientKeys_.empty())
                                     ? storyIntroKey_ : storyAmbientKeys_[(size_t)(idx - 1) % storyAmbientKeys_.size()];
            std::string tr = L.tr(key);
            if (!tr.empty() && tr.rfind("story.", 0) != 0) h.footer = tr;
        }
        h.menu_label = L.tr("ingame_menu.title");
        h.menu_news = newsStore_ || newsGear_ || newsEvents_ || run_.skillPoints() > 0;
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


        if (stairsConfirmOpen_) {
            u.stairs.visible = true;
            u.stairs.question = L.tr(stairsConfirmIsUp_ ? "stairs.confirm_up" : "stairs.confirm_down");
            u.stairs.yes_label = L.tr("menu.yes") + " (Enter)";
            u.stairs.no_label = L.tr("menu.no") + " (Esc)";
        }
        if (inGameMenuOpen_) buildPlayerUi(u);
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
        // "Slime", or "Slime +2" with a party (each name is under its monster)
        b.title = L.tr("battle.title") + " " + (cs.foes.empty() ? std::string() : cs.foes[0].enemy.name) +
                  (cs.foes.size() > 1 ? " +" + std::to_string(cs.foes.size() - 1) : std::string());
        b.player_sprite = uiSprite(playerSprite());
        b.player_anim = battleRigReady() && !battleRigState_.empty();   // drawBattleRig draws the player instead
        for (size_t i = 0; i < cs.foes.size(); i++) {   // the monsters (battle.rml .foe)
            const CombatState::Foe& f = cs.foes[i];
            toms::UiFoe uf;
            uf.sprite = uiSprite(monsterSprite((int)i));
            uf.anim = monsterRigReady() && i < foeRigs_.size() && !foeRigs_[i].state.empty();   // drawBattleRig draws it
            uf.hp_pct = pct((float)std::max(0, f.hp), (float)f.enemy.hp);
            uf.name = f.enemy.name;
            uf.hp_line = "HP " + std::to_string(std::max(0, f.hp));
            uf.hp_text = uf.name + " " + uf.hp_line;
            uf.target = cs.active && cs.foes.size() > 1 && (int)i == cs.target && !f.down();
            uf.down = f.down();
            if (cs.active && !f.down()) {   // its intent (docs/gameplay/13 §6.1)
                const toms::MoveDef& m = moves_.get(f.move);
                if (f.staggerMs > 0) {
                    uf.intent_kind = f.broken ? "broken" : "stagger";
                    uf.intent = L.tr(f.broken ? "battle.intent.broken" : "battle.intent.stagger");
                    uf.intent_pct = 0.0f;
                } else {
                    uf.intent_kind = toms::moveKindName(m.kind);
                    const int left = std::max(0, f.moveMs - std::max(0, f.clockMs));
                    char secs[16];
                    std::snprintf(secs, sizeof secs, " %.1fs", left / 1000.0);
                    uf.intent = (L.has(m.text) ? L.tr(m.text) : m.text) + secs;
                    uf.intent_pct = pct((float)std::max(0, f.clockMs), (float)std::max(1, f.moveMs));
                }
                uf.break_pct = pct(f.breakGauge, (float)foeBreakPoints((int)i));
            }
            b.foes.push_back(uf);
        }
        b.enemy_sprite = b.foes.empty() ? std::string() : b.foes[0].sprite;
        b.enemy_anim = !b.foes.empty() && b.foes[0].anim;
        b.player_hp_pct = pct((float)cs.playerHP, (float)effectiveMaxHp());
        b.enemy_hp_pct = b.foes.empty() ? 0.0f : b.foes[0].hp_pct;
        b.player_hp_text = L.tr("battle.you") + " HP " + std::to_string(cs.playerHP);
        b.enemy_hp_text = b.foes.empty() ? std::string() : b.foes[0].hp_text;
        b.log = cs.log;
        b.continue_hint = L.tr("battle.continue_hint");
        for (const auto& p : cs.popups) {   // over the fighter's box (battle.rcss #player / .foe .fbox: fighterBox)
            toms::UiPopup q;
            q.text = p.text;
            q.style = p.style;
            const float r = p.rise.value();            // 0 -> 1 as it rises
            const glm::vec4 box = fighterBox(p.who);
            q.x = box.x + box.z * 0.5f - 150.0f;       // the 300 px box (a word is wider than a number), centred over it
            q.y = box.y + 26.0f - (float)p.lift - 70.0f * r;
            q.opacity = r < 0.6f ? 1.0f : std::max(0.0f, 1.0f - (r - 0.6f) / 0.4f);
            b.popups.push_back(q);
        }
        if (cs.active) {
            b.clock_label = L.tr("battle.enemy_clock");
            // the clock of the monster that attacks next (the nearest to its move); the stamina pace shows the stamina
            float best = 0.0f;
            for (const CombatState::Foe& f : cs.foes)
                if (!f.down()) best = std::max(best, pct((float)std::max(0, f.clockMs), (float)std::max(1, f.moveMs)));
            b.clock_pct = best;
            b.stamina_pace = cs.pace == toms::Pace::Stamina;
            if (b.stamina_pace) {
                b.clock_label = L.tr("battle.stamina");
                b.clock_pct = pct(cs.stamina, (float)paceSettings_.staminaMax);
                b.attack_short = !attackRunning() && cs.stamina < (float)paceSettings_.attackCost;
                b.guard_short = cs.stamina < (float)paceSettings_.guardCost;
            }
            {   // Battle v3 (docs/gameplay/07 phase 1): the charge, the track with the weapon level's points, the marker
                toms::UiCombo& c = b.combo;
                const std::string key = weaponKey();
                const int lv = weaponLevel(key);
                const toms::WeaponPlay& wp = weaponPlayOf(key);
                c.running = cs.combo.running();
                c.cooling = cs.atkBar.cooling;
                c.waiting = cs.combo.waiting();
                c.charge_pct = c.running ? 100.0f * cs.combo.result().charge : 100.0f * comboCharge();
                if (c.running) {
                    c.charge_pct = 0;   // spent on this combo
                    c.marker = 100.0f * cs.combo.marker();
                    const int near = cs.combo.nearPoint();
                    for (size_t i = 0; i < cs.combo.pointsAt().size(); i++)
                        c.points.push_back({100.0f * cs.combo.pointsAt()[i], (bool)cs.combo.pointsHit()[i], (int)i == near});
                } else {   // the points this combo will have, before it starts
                    const int n = toms::weaponLevelEntry(wp, lv).points;
                    for (int i = 0; i < n; i++) c.points.push_back({100.0f * float(i + 1) / float(n + 1), false});
                }
                const std::string name = equipmentDefs_.count(key) ? itemName(key) : L.tr("battle.unarmed");
                const int next = toms::usesToNextLevel(wp, lv);
                int done = 0;
                if (auto u = weaponUses_.find(key); u != weaponUses_.end()) {
                    done = u->second;
                    for (int l = 1; l < lv; l++) done -= toms::usesToNextLevel(wp, l);
                }
                c.level_text = trParam(trParam(L.tr("battle.weapon_level"), "name", name), "lv", std::to_string(lv)) +
                               (next > 0 ? "  " + std::to_string(std::max(0, done)) + "/" + std::to_string(next) : std::string());
                c.prompt = L.tr(c.running ? "battle.combo_prompt_run" : "battle.combo_prompt");
                c.button_label = L.tr(c.cooling ? "battle.btn_charging" : c.running ? "battle.combo_tap" : "battle.combo_start");
            }
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
            b.super_ready = cs.superCharge >= run_.superMax() && !attackRunning() && !cs.winPending;
            b.super_button = L.tr("battle.super_ready");
            // the revamp (docs/gameplay/15): the attack is the play, its panel only says how it is; one guard button
            b.show_clock = cs.pace == toms::Pace::Stamina;
            b.guard_timing = guard_.mode == "timing";
            b.guard_lit = cs.guardAtMs >= 0 && cs.clockMs - cs.guardAtMs <= guard_.blockMs;
            b.guard_cooling = cs.guardCdMs > 0 || b.guard_short;
            b.guard_label = L.tr("battle.guard_button");
            b.guard_hint = L.tr("battle.guard_hint");
            const toms::PlayKind play = battlePlayKind();
            if (attackRunning()) { b.attack_state = "running"; b.attack_label = L.tr("battle.attack_running"); }
            else if (cs.pace == toms::Pace::Cooldown && cs.atkBar.cooling) {
                b.attack_state = "recovering";
                b.attack_label = L.tr("battle.attack_recovering");
                b.attack_pct = 100.0f * (1.0f - (float)cs.atkBar.cooldownMs / (float)std::max(1, barCooldownMs()));
            } else if (b.attack_short) { b.attack_state = "short"; b.attack_label = L.tr("battle.no_stamina"); }
            else {
                b.attack_state = "ready";
                b.attack_label = L.tr("battle.attack_ready");
                b.attack_hint = L.tr(play == toms::PlayKind::Track ? "battle.attack_ready_hint"
                                     : std::string("battle.play_hint_") + toms::playKindName(play));
            }
            if (play != toms::PlayKind::Track) touchPlayUi(b);
            if (cs.chain > 1) b.chain_text = trParam(L.tr("battle.word_chain"), "n", std::to_string(cs.chain));
            auto activeIds = toms::equippedActives(equipped_, equipmentDefs_);
            if (!activeIds.empty() && toms::canUseActive(activeIds[0], activeDefs_, cs.activeRuntime, cs.activeStatus)) {
                b.active_ready = true;   // (ActiveDefinition has no display name yet: the id is shown)
                b.active_button = trParam(L.tr("battle.active_ready"), "name", activeIds[0]);
            }
        }
    }

    // ---- the battle's moments and its result (docs/gameplay/15 §3.4, §3.6) ----
    if (showBattle) {
        toms::UiBattle& b = u.battle;
        if (cs.bannerMs > 0 && !cs.banner.empty()) {   // across the screen: slides in, holds, slides out
            const float t = 1.0f - (float)cs.bannerMs / (float)cs.bannerMsMax;
            b.banner = cs.banner;
            b.banner_kind = cs.bannerKind;
            b.banner_opacity = t < 0.12f ? t / 0.12f : t > 0.85f ? (1.0f - t) / 0.15f : 1.0f;
            b.banner_shift = t < 0.12f ? (1.0f - t / 0.12f) * 500.0f : t > 0.85f ? -(t - 0.85f) / 0.15f * 500.0f : 0.0f;
        }
        if (cs.won && cs.result.valid) {
            const CombatState::Result& r = cs.result;
            b.result_shown = true;
            b.grade = r.grade;
            b.grade_s = r.grade == "S";
            b.result_title = L.tr("battle.result_title");
            char secs[16];
            std::snprintf(secs, sizeof secs, "%.1f", r.seconds);
            b.result_stats = trParam(trParam(trParam(L.tr("battle.result_stats"), "t", secs), "c", std::to_string(cs.bestChain)),
                                     "p", std::to_string(cs.perfects));
            const toms::WeaponPlay& wp = weaponPlayOf(r.weapon);
            const std::string name = equipmentDefs_.count(r.weapon) ? itemName(r.weapon) : L.tr("battle.unarmed");
            b.result_weapon = trParam(trParam(L.tr("battle.result_weapon"), "name", name), "lv", std::to_string(r.lvAfter));
            const int need = toms::usesToNextLevel(wp, r.lvAfter);
            int done = r.uses;
            for (int l = 1; l < r.lvAfter; l++) done -= toms::usesToNextLevel(wp, l);
            if (need > 0) {   // the wins to the next level, and what that level adds (07 pillar 2: the reason to go on)
                b.result_pct = pct((float)std::max(0, done), (float)need);
                b.result_progress = trParam(trParam(L.tr("battle.result_progress"), "n", std::to_string(std::max(0, done))), "m",
                                            std::to_string(need));
                const toms::WeaponLevel& cur = toms::weaponLevelEntry(wp, r.lvAfter);
                const toms::WeaponLevel& nxt = toms::weaponLevelEntry(wp, r.lvAfter + 1);
                std::string what;
                if (nxt.points > cur.points) what = trParam(L.tr("battle.result_points"), "n", std::to_string(nxt.points));
                for (const auto& [stat, v] : nxt.stats) {
                    const int was = cur.stats.count(stat) ? cur.stats.at(stat) : 0;
                    if (v > was) {
                        std::string up = stat;
                        for (char& ch : up) ch = (char)std::toupper((unsigned char)ch);
                        what += (what.empty() ? "" : ", ") + up + " +" + std::to_string(v - was);
                    }
                }
                if (!what.empty()) b.result_next = trParam(L.tr("battle.result_next"), "what", what);
            } else {
                b.result_pct = 100.0f;
                b.result_progress = L.tr("battle.result_top");
            }
            if (r.lvAfter > r.lvBefore)
                b.result_levelup = trParam(trParam(L.tr("battle.result_levelup"), "name", name), "lv", std::to_string(r.lvAfter));
            b.result_gold = trParam(trParam(L.tr("battle.result_gold"), "gold", std::to_string(r.gold)), "exp", std::to_string(r.exp));
            b.result_card = r.card;
        }
    }

    // ---- a reward event (docs/gameplay/07 phase 6) ----
    if (miniGame_.open) {
        if (const toms::MiniGameDef* def = miniGames_.find(miniGame_.id)) {
            toms::UiMiniGame& m = u.minigame;
            m.visible = true;
            m.title = L.field(def->title);
            m.text = L.field(def->text);
            m.running = miniGame_.phase == 1;
            m.done = miniGame_.phase == 2;
            const std::string unit = L.tr(def->currency == "crystals" ? "minigame.crystals" : "minigame.gold");
            m.prompt = m.running ? L.tr("battle.combo_prompt_run")
                                 : trParam(trParam(trParam(L.tr("minigame.rule"), "n", std::to_string(def->perHit)), "unit", unit),
                                           "bonus", std::to_string(def->bonusAll));
            m.button_label = L.tr(m.done ? "minigame.close" : m.running ? "battle.combo_tap" : "minigame.start");
            m.close_label = L.tr("minigame.close");
            if (m.done)
                m.result = miniGame_.reward > 0
                    ? trParam(trParam(L.tr("minigame.result"), "n", std::to_string(miniGame_.reward)), "unit", unit)
                    : L.tr("minigame.result_none");
            if (m.running) {
                m.marker = 100.0f * miniGame_.play.marker();
                for (size_t i = 0; i < miniGame_.play.pointsAt().size(); i++)
                    m.points.push_back({100.0f * miniGame_.play.pointsAt()[i], (bool)miniGame_.play.pointsHit()[i]});
            } else {
                const int n = def->play.points;
                for (int i = 0; i < n; i++)
                    m.points.push_back({100.0f * float(i + 1) / float(n + 1), m.done && i < miniGame_.play.result().hits});
            }
        }
    }

    // ---- dialogue ----
    if (showTalk) {
        toms::UiDialogue& d = u.dialogue;
        d.visible = true;
        d.scale = uiScale_;
        // The box spans the canvas width minus 40 px a side and scales about its bottom centre; scaled up (phones) it is
        // narrowed first, so it keeps that width on screen instead of running off both edges.
        const float cw = ren ? (float)ren->width() : 1024.0f;
        d.side = (cw - (cw - 80.0f) / std::max(1.0f, uiScale_)) * 0.5f;
        d.speaker = dlgSpeaker_;
        if (dlgData.contains("nodes") && dlgData["nodes"].contains(dlgNode) && dlgData["nodes"][dlgNode].contains("text"))
            d.text = L.field(dlgData["nodes"][dlgNode]["text"]);
        for (size_t i = 0; i < dlgChoices.size(); i++)
            d.choices.push_back({dlgChoices[i].label, "", (int)i == dlgSel, true});
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
            const bool gear = isGearType(itemType(d.id));
            const bool owned = gear && std::find(gearOwned_.begin(), gearOwned_.end(), d.id) != gearOwned_.end();
            toms::UiStoreItem it;
            it.icon = itemSprite(d.id);
            it.name = itemName(d.id);
            it.desc = itemDesc(d.id);
            it.effect = itemEffectSummary(d.id);
            it.equipped = gear && (d.id == equipped_.weaponId || d.id == equipped_.armorId || d.id == equipped_.talentId);
            it.status = gear ? L.tr(it.equipped ? "store.equipped" : "store.tap_to_equip")
                             : L.tr("store.purchased_label") + " x" + std::to_string(d.purchases);
            it.price = owned ? "-" : std::to_string(d.liveCost());
            it.selected = (int)i == storeSel_;
            s.items.push_back(it);
        }
    }
    if (!notices_.empty()) {
        u.notice.visible = true;
        u.notice.title = notices_.front().title;
        u.notice.body = notices_.front().body;
        u.notice.ok = L.tr("notice.ok");
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
    // The pick-up / level-up / event lines wait while the player menu is open: they would cover its
    // right side (they still time out).
    if (!inGameMenuOpen_)
        for (const auto& n : notifications_) u.overlay.notifications.push_back(n.first);
}

void Game::buildMenuUi(toms::UiMenu& m) const {
    const toms::Locale& L = locale_;
    m.visible = true;
    m.yes_label = L.tr("menu.yes");
    m.no_label = L.tr("menu.no");
    m.close_label = L.tr("menu.back");
    auto sel = [&](size_t i) { return (int)i == inGameMenuSel_; };
    // The left list is always there; a page (Settings, Skills, Forge, Village) opens on the right,
    // and while it is open the keyboard moves on its rows and the row that opened it stays lit.
    {
        auto order = mainMenuOrder();
        int lit = inGameMenuSel_;
        if (inGameMenuPage_ == InGameMenuPage::Settings) lit = mainMenuRowIndex(MainMenuRow::Settings);
        if (inGameMenuPage_ == InGameMenuPage::Skills)   lit = mainMenuRowIndex(MainMenuRow::Skills);
        if (inGameMenuPage_ == InGameMenuPage::Forge)    lit = mainMenuRowIndex(MainMenuRow::Forge);
        if (inGameMenuPage_ == InGameMenuPage::Hub)      lit = mainMenuRowIndex(MainMenuRow::Village);
        for (size_t i = 0; i < order.size(); i++) {
            toms::UiRow r;
            switch (order[i]) {
                case MainMenuRow::Store:
                    r.label = L.tr("store.icon_label");
                    r.enabled = storeUnlocked_;
                    if (!storeUnlocked_) r.sub = L.tr("system.locked");
                    else if (newsStore_) r.sub = "•";
                    break;
                case MainMenuRow::Save:        r.label = L.tr("saveload.row"); break;
                case MainMenuRow::Settings:    r.label = L.tr("menu.settings"); break;
                case MainMenuRow::Skills:
                    r.label = L.tr("ingame_menu.skills");
                    if (run_.skillPoints() > 0) r.sub = trParam(L.tr("skill.points_label"), "n", std::to_string(run_.skillPoints()));
                    break;
                case MainMenuRow::Village:     r.label = L.tr("ingame_menu.village"); break;
                case MainMenuRow::Forge:       r.label = L.tr("ingame_menu.forge"); break;
                case MainMenuRow::Fullscreen:
                    r.label = L.tr("system.fullscreen");
                    r.sub = L.tr(fullscreenOn_ ? "system.on" : "system.off");
                    break;
                case MainMenuRow::BackToTitle: r.label = L.tr("ingame_menu.back_to_title"); break;
            }
            r.selected = (int)i == lit;
            m.main_rows.push_back(r);
        }
    }
    switch (inGameMenuPage_) {
        case InGameMenuPage::Main: {
            m.page = 0;
            m.close_label = L.tr("inventory.close");
            break;
        }
        case InGameMenuPage::Settings: {
            m.page = 1;
            m.header = L.tr("settings.header");
            const auto& langs = L.languages();
            for (size_t i = 0; i < langs.size(); i++)
                m.rows.push_back({std::string((int)i == L.languageIndex() ? "[x] " : "[ ] ") + langs[i].name, "", sel(i), true});
            if (!m.rows.empty()) m.rows[0].section = L.tr("settings.language");   // section: language
            appendArtStyleRows(m.rows, inGameMenuSel_ - (int)langs.size());       // section: art style
            const size_t cam = m.rows.size();                                      // section: other
            m.rows.push_back({L.tr(cam_.mode() == toms::Camera::Mode::Rooms ? "settings.camera_rooms" : "settings.camera_follow"),
                              "", sel(cam), true});
            m.rows[cam].section = L.tr("settings.other_section");
            m.rows.push_back({L.tr(settings_.reduceEffects ? "settings.reduce_effects_on" : "settings.reduce_effects_off"),
                              "", sel(cam + 1), true});   // docs/gameplay/15 §3.8
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
    if (inGameLangConfirmOpen_ && inGameConfirmIsStyle_) {
        m.confirm_open = true;
        m.confirm_question = trParam(L.tr("settings.style_confirm"), "style", artStyleName(inGameLangConfirmIdx_));
        m.confirm_body = L.tr("settings.style_apply_title");   // a run never changes look under the player
        m.confirm_yes = inGameLangConfirmYes_;
    } else if (inGameLangConfirmOpen_) {
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
    if (name == "hud_menu")      { if (!modalActive()) { cancelWalk(); openPlayerMenu(); } return; }
    // ---- battle ----
    if (name == "battle_attack")   { battleTapAttack(); return; }
    if (name == "minigame_tap")    { miniGameTap(); return; }
    if (name == "minigame_close")  { closeMiniGame(); return; }
    if (name == "battle_defend")   { battleTapDefense(); return; }
    if (name == "battle_super")    { battleTapSuper(); return; }
    if (name == "battle_active")   { battleTapActive(); return; }
    if (name == "battle_target")   { battleSetTarget(arg); return; }
    if (name == "battle_continue") { if (cs.won) dismissVictory(); return; }
    // ---- dialogue ----
    if (name == "dlg_choose")  { if (inDialogue && arg >= 0 && arg < (int)dlgChoices.size()) { dlgSel = arg; chooseDialogue(arg); } return; }
    if (name == "dlg_hover")   { if (inDialogue && arg >= 0 && arg < (int)dlgChoices.size()) dlgSel = arg; return; }
    if (name == "dlg_advance") { if (inDialogue) chooseDialogue(dlgSel); return; }
    // ---- the player menu (game_player_menu.cpp) ----
    if (name.rfind("pm_", 0) == 0) { playerMenuEvent(name, arg); return; }
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
    if (name == "menu_close")   { if (inGameMenuOpen_) inGameMenuBack(); return; }
    if (name == "menu_confirm") { if (inGameLangConfirmOpen_) { inGameLangConfirmYes_ = arg != 0; inGameMenuActivate(); } return; }
    if (name == "notice_ok") { dismissNotice(); return; }
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

// Each weapon's own play (touch_play.h; docs/gameplay/15 §3.3), drawn over the monster as plain boxes (battle.rcss .tp):
// the bow's target, its pull and the crosshair; the sword's arrows (a bar and a chevron head) and the swipe; the staff's
// circles and their closing rings; and where each one landed (a ring, or a cross), fading.
void Game::touchPlayUi(toms::UiBattle& b) const {
    const toms::TouchPlay& p = cs.touch;
    b.touch_kind = toms::playKindName(battlePlayKind());
    auto add = [&](const std::string& cls, float cx, float cy, float w, float h, float rot = 0.0f, float radius = 0.0f,
                   float opacity = 1.0f, std::string text = {}) {
        b.touch.push_back({cls, cx - w * 0.5f, cy - h * 0.5f, w, h, rot, radius, opacity, std::move(text)});
    };
    auto line = [&](const std::string& cls, glm::vec2 a, glm::vec2 c, float thick) {
        const glm::vec2 d = c - a;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len >= 2.0f) add(cls, (a.x + c.x) * 0.5f, (a.y + c.y) * 0.5f, len, thick, std::atan2(d.y, d.x) * 57.29578f, thick * 0.5f);
    };
    for (const toms::TouchPlay::Mark& m : p.marks()) {
        if (m.ageMs >= 500) continue;
        const float k = (float)m.ageMs / 500.0f, s = 34.0f + 30.0f * k;
        if (m.ok) add("mark ok", m.x, m.y, s, s, 0.0f, s * 0.5f, 1.0f - k);
        else { add("mark bad", m.x, m.y, 40, 7, 45.0f, 3.5f, 1.0f - k); add("mark bad", m.x, m.y, 40, 7, -45.0f, 3.5f, 1.0f - k); }
    }
    if (!p.running()) return;
    const std::string count = std::to_string(std::min(p.index() + 1, p.points())) + "/" + std::to_string(p.points());
    if (p.kind() == toms::PlayKind::Bow) {
        const toms::TouchPlay::Target& t = p.target();
        add(p.started() ? "target" : "target idle", t.x, t.y, 2 * t.r, 2 * t.r, 0.0f, t.r);
        add("bull", t.x, t.y, 0.8f * t.r, 0.8f * t.r, 0.0f, 0.4f * t.r);
        add("count", t.x, t.y - t.r - 18.0f, 80, 24, 0.0f, 0.0f, 1.0f, count);
        if (p.started()) add("time", t.x, t.y + t.r + 10.0f, 2 * t.r * p.timeLeft(), 5, 0.0f, 2.5f);
        if (p.aiming()) {
            const glm::vec2 c = p.crosshair();
            line("pull", p.aimFrom(), p.aimTo(), 5);
            line("shot", p.aimFrom(), c, 2);
            add("anchor", p.aimFrom().x, p.aimFrom().y, 14, 14, 0.0f, 7);
            add("cross", c.x, c.y, 28, 28, 0.0f, 14);
        }
    } else if (p.kind() == toms::PlayKind::Slice) {
        const glm::vec4 a = touchArea();
        const float cx = a.x + a.z * 0.5f, cy = a.y + a.w - 80.0f;   // under the monster and its HP
        const auto& arrows = p.arrows();
        const float gap = 84.0f, x0 = cx - gap * 0.5f * (float)(arrows.size() - 1);
        for (size_t k = 0; k < arrows.size(); k++) {
            const toms::TouchPlay::Arrow& ar = arrows[k];
            const float x = x0 + gap * (float)k;
            if (ar.state == 0 && !p.arrowsShown()) { add("arrow hidden", x, cy, 18, 18, 0.0f, 9); continue; }
            const std::string cls = ar.state == 1 ? "arrow cut" : ar.state == 2 ? "arrow missed"
                                  : (int)k == p.index() ? "arrow now" : "arrow";
            const float ang = (float)ar.dir * 45.0f, rad = ang / 57.29578f;
            const glm::vec2 dir(std::cos(rad), std::sin(rad));
            add(cls, x - dir.x * 6.0f, cy - dir.y * 6.0f, 46, 10, ang, 5);
            add(cls + " head", x + dir.x * 12.0f, cy + dir.y * 12.0f, 22, 22, ang + 45.0f);
        }
        add("count", cx, cy - 44.0f, 80, 24, 0.0f, 0.0f, 1.0f, count);
        if (p.started()) add("time", cx, cy + 40.0f, 160.0f * p.timeLeft(), 5, 0.0f, 2.5f);
        if (p.swiping()) line("swipe", p.aimFrom(), p.aimTo(), 8);
    } else if (p.kind() == toms::PlayKind::Circles) {
        const auto& circles = p.circles();
        const float r = 34.0f;
        for (size_t k = 0; k < circles.size(); k++) {
            const toms::TouchPlay::Circle& c = circles[k];
            if (c.state != 0) continue;
            // the ring closes on the circle over the last 900 ms (before the first tap: every circle waits, no ring)
            const float lead = p.started() ? (float)(c.atMs - p.clockMs()) / 900.0f : 2.0f;
            add(p.started() ? "circle" : "circle idle", c.x, c.y, 2 * r, 2 * r, 0.0f, r, 1.0f, std::to_string(k + 1));
            if (lead <= 1.0f) {
                const float s = 2 * r * (1.0f + 1.6f * std::max(0.0f, lead));
                add("ring", c.x, c.y, s, s, 0.0f, s * 0.5f, std::clamp(1.2f - lead, 0.3f, 1.0f));
            }
        }
    }
}

// game_input.cpp — input routing: touch/mouse/on-screen-pad hit testing, held-direction
// movement, world interaction, and bump-to-fight. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::handleTouch(float px, float py, int phase) {
    // Guard: ignore invalid coordinates (NaN from a zero-size canvas rect, or
    // out-of-range). Prevents bad state / out-of-bounds in the hit-test below.
    if (!(px == px) || !(py == py)) return;            // NaN check
    if (px < 0 || px > 1024 || py < 0 || py > 768) return;
    // Title phase: every tap goes to the title's own hit-testing (menu rows / save slots /
    // language rows / Back). Checked first — while it is up, it is the only interactive screen.
    if (title_.isOpen()) { if (phase == 0) titleClick(px, py); return; }
    // M7 (first slice): a tap anywhere dismisses the ending screen -- there is nothing else to
    // interact with once a run has actually ended, matching the post-victory tap-to-dismiss
    // convention elsewhere in this file.
    // M8: when the ending offers 輪迴, it has two REAL buttons -- a tap must land on one of them,
    // not "anywhere" (there is now an actual choice to make). Only falls back to tap-anywhere
    // dismiss when rebirth isn't on offer, matching the single-exit screen's own simpler contract.
    if (endingActive()) {
        if (phase != 0) return;
        if (rebirthOffered()) {
            auto hit = [&](const int r[4]) { return px>=r[0] && px<=r[0]+r[2] && py>=r[1] && py<=r[1]+r[3]; };
            if (hit(endingRebirthBtnRect_)) rebirth();
            else if (hit(endingTitleBtnRect_)) dismissEndingScreen();
            return;
        }
        dismissEndingScreen();
        return;
    }
    // --- Store overlay: route ALL taps to storeClick. Store buttons (icon / buy / close)
    //     are NOT gamepad rects, so this must run BEFORE the gamepad hit-test below,
    //     otherwise taps on store UI hit `id<0` and are dropped. ---
    if (storeModal()) { if (phase == 0) storeClick(px, py); return; }
    // In-game menu (Save/Settings/Back to Title): swallow every tap while it is up, same
    // pattern as the store overlay above.
    if (inGameMenuOpen_) { if (phase == 0) inGameMenuClick(px, py); return; }
    // Normal play: a tap on the gear icon (top-right, left of the store icon) opens the
    // in-game menu; a tap on the store icon opens the shop.
    if (!modalActive() && phase == 0) {
        if (px >= menuIconRect_[0] && px <= menuIconRect_[0]+menuIconRect_[2] &&
            py >= menuIconRect_[1] && py <= menuIconRect_[1]+menuIconRect_[3]) {
            openInGameMenu();
            return;
        }
        storeClick(px, py);
        if (storeOpen) return;   // icon tapped -> store opened; consume this tap
    }
    int id = -1;
    for (int i = 0; i < GP_N; i++) {
        const GPadBtn b = gpadBtn(i);
        if (px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h) { id = i; break; }
    }
    if (id == 7) { if (phase == 0) gpOn = !gpOn; return; }  // toggle show/hide
    // ---- Battle scene (Battle System v2): each tap is a single, instantaneous action -- there is
    // no press-and-hold or release to track any more, so (unlike the old model) only phase==0
    // matters here, and it matters WHICH rect was hit, since Attack/Defend/Super are three
    // independent actions now instead of one shared "the" action button. Two fingers landing on
    // two different rects in the same instant (multi-touch) each fire their own handleTouch call
    // independently, so concurrent attack+defend just falls out of this naturally. Deliberately
    // handled this early, before the `id` gamepad-rect loop below, so a tap on any of these three
    // rects (which are not gamepad rects) is never dropped by the `if (id < 0) return;` further
    // down. cs.won is excluded so the post-victory tap-anywhere-to-dismiss below still works.
    if (cs.active && !cs.won && phase == 0) {
        auto hit = [&](const int r[4]) { return px>=r[0] && px<=r[0]+r[2] && py>=r[1] && py<=r[1]+r[3]; };
        if (hit(atkBtnRect_)) { battleTapAttack(); return; }
        if (hit(defBtnRect_)) { battleTapDefense(); return; }
        if (cs.superCharge >= run_.superMax() && hit(superBtnRect_)) { battleTapSuper(); return; }
        // S7 (equipment actives, first slice): same "no button at all until it's actually
        // available" convention as Super above.
        if (!cs.activeUsed && !toms::equippedActives(equipped_, equipmentDefs_).empty() && hit(activeBtnRect_)) { battleTapActive(); return; }
        return;   // a tap elsewhere in the battle scene does nothing now (no more "whole scene is the surface")
    }
    // Milestone 9 polish: releasing a d-pad button stops "keep moving while held" (see
    // setMoveHeldX/Y's declaration in game.h). Checked here, before the generic phase==2
    // return right below (which would otherwise swallow it), and unconditionally regardless
    // of what else might be open -- harmless when nothing is currently held, and this is the
    // one case where a release must never be dropped (an un-cleared held direction would
    // otherwise keep stepping forever).
    if (id >= 0 && id <= 3 && phase == 2) { stopMoveHeld(); return; }
    if (phase == 2) return;                 // touchend on a game button: nothing
    // Victory screen: tap anywhere to dismiss (cs.won is set on win and must be cleared
    // or the combat overlay keeps painting forever -> "stuck after defeating enemy").
    if (cs.won && phase == 0) { dismissVictory(); return; }
    // Milestone 9: stairs confirm -- Yes/No buttons aren't gamepad rects, so `id` stays
    // -1 for a desktop mouse click on them; must be checked before `if (id < 0) return;`
    // below (the exact bug this fixes for dialogue too, right after this block).
    if (stairsConfirmOpen_) {
        if (phase == 0) {
            auto hit = [&](const int r[4]) { return px>=r[0] && px<=r[0]+r[2] && py>=r[1] && py<=r[1]+r[3]; };
            if (hit(stairsConfirmYesRect_)) { confirmStageTransition(); return; }
            if (hit(stairsConfirmNoRect_))  { cancelStageTransition(); return; }
        }
        return;
    }
    // HUD stats line (carries the "(I)" inventory indicator) — tap to open inventory.
    if (!inventoryOpen() && !inDialogue && !modalActive() && phase == 0) {
        if (py >= 74 && py <= 104 && px >= 16 && px <= 560) { toggleInventory(); return; }
    }
    if (inventoryOpen()) {
        if (phase == 0) {
            auto hit = [&](const int r[4]) {
                return px >= r[0] && px <= r[0] + r[2] && py >= r[1] && py <= r[1] + r[3];
            };
            for (size_t i = 0; i + 3 < invCardRects_.size(); i += 4) {
                int r[4] = { (int)invCardRects_[i+0], (int)invCardRects_[i+1], (int)invCardRects_[i+2], (int)invCardRects_[i+3] };
                if (hit(r)) { invSel = (int)(i / 4); return; }
            }
            if (hit(invUseRect_)) { invUseSelected(); return; }
            if (hit(invDropRect_)) { invDropSelected(); return; }
            if (hit(invCloseRect_)) { toggleInventory(); return; }
        }
        if (id >= 0) {
            const GPadBtn b = gpadBtn(id);
            if (id <= 3) invMoveSel(b.dx, b.dy);
            else if (id == 4 && phase == 0) invUseSelected();
            else if (id == 5 && phase == 0) invDropSelected();
            else if (id == 6 && phase == 0) toggleInventory();
        }
        return;
    }
    // Milestone 9 bugfix: dialogue tap-to-select never actually worked from a desktop
    // mouse. `if (id < 0) return;` used to run BEFORE this block -- a click on a
    // dialogue choice line isn't inside any gamepad-button rect, so `id` was always -1
    // there and every such click was silently dropped before ever reaching this code.
    // Moved above that guard; the gamepad D-pad fallback below still needs `id`, which
    // is already computed further up, so nothing else about it changes.
    if (inDialogue) {                        // dialogue: tap a choice line to select+confirm
        int n = (int)dlgChoices.size();
        // Tap directly on a choice line (drawn at dialogueRowY(), x >= 60) selects & confirms it --
        // same helper the draw side uses (game_helpers.h), so this can't silently drift from what's
        // actually on screen.
        if (phase == 0 && n > 0) {
            float W = (float)ren->width(), H = (float)ren->height();
            // C step 2: ask the SAME layout the drawing used (dialogueLayoutFor), so the row a tap
            // selects is the row that was drawn there -- identical by construction, at any UI scale.
            // (The old hand-written +-22px zone duplicated these numbers; nearest-baseline wins now.)
            toms::DialogueLayout dl = dialogueLayoutFor(W, H, n);
            int row = dl.rowAt(uiRoot_, glm::vec2(px, py));
            if (row >= 0) { dlgSel = row; chooseDialogue(row); return; }
            // tap elsewhere on the dialogue box = advance to next (keep current selection)
            chooseDialogue(dlgSel); return;
        }
        // (gamepad D-pad still works too)
        if (id == 0 && phase == 0 && n > 0) dlgSel = (dlgSel - 1 + n) % n;   // up = prev choice
        else if (id == 1 && phase == 0 && n > 0) dlgSel = (dlgSel + 1) % n;  // down = next choice
        else if (id == 4 && phase == 0) chooseDialogue(dlgSel);               // A = select
        else if (id == 5 && phase == 0) inDialogue = false;                  // B = close
        return;
    }
    if (id < 0) return;
    // (The battle scene's hold-to-charge input is handled much earlier, before the generic
    // `phase == 2` return above -- see the comment there.)
    if (modalActive()) {                     // other modal (store handled above): block world input
        return;
    }
    const GPadBtn b = gpadBtn(id);
    // Milestone 9 polish: press-and-hold now keeps stepping (setMoveHeldX/Y + update()'s
    // repeat timer) instead of exactly one tile per tap; the matching release is handled
    // above, before the generic phase==2 return. GP[0]/[1] are the Y axis (up/down), GP[2]/[3]
    // the X axis (left/right) -- each button only ever sets one axis (see GP[]'s own dx/dy).
    if (id <= 3 && phase == 0) {
        if (b.dy != 0) setMoveHeldY(b.dy); else setMoveHeldX(b.dx);
    }
    else if (id == 4 && phase == 0) interact();
}

// Milestone 9 polish: see setMoveHeldX/Y's declaration in game.h. A fresh press (or switching
// directions on the same axis) fires one immediate step, same feel as the old one-tap-one-tile
// behavior; the repeat while still held is ticked in update().
void Game::setMoveHeldX(int dir) {
    if (dir == moveHoldX_.dir) return;
    moveHoldX_.dir = dir; moveHoldX_.holdMs = 0; moveHoldX_.repeating = false;
    if (dir != 0) movePlayer(dir, 0);
}

void Game::setMoveHeldY(int dir) {
    if (dir == moveHoldY_.dir) return;
    moveHoldY_.dir = dir; moveHoldY_.holdMs = 0; moveHoldY_.repeating = false;
    if (dir != 0) movePlayer(0, dir);
}

void Game::movePlayer(int dx, int dy) {
    if (modalActive()) return;   // any modal overlay (combat/dialogue/inventory) blocks world input
    int nx = pl.x + dx, ny = pl.y + dy;
    char c = st.at(nx, ny);
    if (c == '#') return;
    // Doors need keys -- gate routed through the shared Condition Evaluator (condition.h) so
    // this uses the same "can the player do X" engine as dialogue `requires` gating, per
    // Milestone 3's retrofit. Same outcome as the three bespoke inline checks this replaces:
    // each door color needs >=1 of its matching key, expressed declaratively instead.
    static const std::map<char, std::string> kDoorKeyItem = {
        {'y', "key_yellow"}, {'b', "key_blue"}, {'r', "key_red"}
    };
    if (auto doorIt = kDoorKeyItem.find(c); doorIt != kDoorKeyItem.end()) {
        nlohmann::json req = { {"type", "itemHeld"}, {"itemId", doorIt->second}, {"count", 1} };
        if (!toms::evaluate(req, GameConditionContext(pl, meta_, missionTrackers_, run_, equipped_))) return;
    }
    if (c == 'y') pl.key_yellow--;
    if (c == 'b') pl.key_blue--;
    if (c == 'r') pl.key_red--;
    // S1: a multi-grid monster is NOT walked into. A 1x1 monster keeps the shipped behavior (the
    // player steps onto the tile and the fight happens there), but entering a 4-grid boss's cell
    // would put the player *inside* the boss -- so big footprints are pure bumps: fight from the
    // adjacent tile and let the boss keep its space (doc F4: the player can only circle it).
    for (const auto& e : st.entities) {
        if (e.consumed || !e.fp.big()) continue;
        if (e.kind.rfind("monster:", 0) != 0) continue;
        if (entityCovers(e, nx, ny)) { advanceRoamers(); engageMonster(e); return; }
    }
    pl.x = nx; pl.y = ny;
    audio.play("walk");
    // The player acted -> the world takes its turn (roamers step once; S1).
    advanceRoamers();
    // check entity at new cell
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        if (entityCovers(e, nx, ny)) {
            if (e.kind.rfind("monster:",0)==0) {
                engageMonster(e);
                return;
            } else if (e.kind.rfind("item:",0)==0) {
                // Keys/coins apply immediately (not stored in the 9-grid UI).
                // Usable items (gems/potions/exp/scroll) go into the inventory.
                bool immediate = (e.id.rfind("key_",0)==0) || e.id=="coin";
                if (immediate) applyItem(e.id);
                else pl.inv.push_back(e.id);
                e.consumed = true;
                st.tiles[e.y][e.x] = '.'; // clear from grid
                // Milestone 7: persist the clear so it survives a reload of this floor (stairs
                // or the Stage Select hub) -- see entityStatus_'s declaration in game.h.
                toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, e.x, e.y), toms::EntityStatus::Collected);
                toms::globalEventBus().publish(toms::ItemCollected{e.id, curStage});
            } else if (e.kind.rfind("event:",0)==0) {
                // S3.5 (d): a floor event. Its text comes from the i18n table (generated for every
                // event id by tools/gen_story_i18n.py, authored for acts 1-3), and the effect is
                // derived from the event id's own vocabulary until the pools are loaded at runtime:
                // shards feed the memory-shard state, traps cost HP, caches/relics give a little back.
                std::string evId = e.id;
                std::string text = locale_.tr(evId + ".text");
                if (text.empty() || text == evId + ".text") text = evId;   // missing -> the id, not blank
                if (evId.find("shard") != std::string::npos) {
                    run_.addShard(evId);
                    notifications_.push_back({text + "  [memory shard " + std::to_string(run_.shardCount()) + "]", 4200});
                } else if (evId.find("trap") != std::string::npos) {
                    pl.hp = std::max(1, pl.hp - 8);
                    notifications_.push_back({text + "  [-8 HP]", 3800});
                } else if (evId.find("cache") != std::string::npos || evId.find("relic") != std::string::npos) {
                    pl.hp = std::min(pl.maxhp, pl.hp + 10);
                    pl.gold += 12;
                    notifications_.push_back({text + "  [+10 HP, +12 GOLD]", 3800});
                } else {
                    run_.setFlag("event_" + evId, true);        // whispers/rescues: remembered, no stat
                    notifications_.push_back({text, 4200});
                }
                e.consumed = true;
                st.tiles[e.y][e.x] = '.'; // clear from grid
                toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, e.x, e.y), toms::EntityStatus::Collected);
                return;
            } else if (e.kind.rfind("npc:",0)==0) {
                // start dialogue
                dlgNpc = "enemy_"+e.id; // fallback; real npc ids below
                if (e.id=="villager") dlgNpc="villager_elder";
                else if (e.id=="sorcerer") dlgNpc="sorcerer_teacher";
                else if (e.id=="king") dlgNpc="king_lieutenant";
                else if (e.id=="princess") dlgNpc=(curStage=="stage_11")?"princess_victory":"princess_liora";
                else if (e.id=="handmaiden") dlgNpc="handmaiden";
                else if (e.id=="skeleton_scholar") dlgNpc="skeleton_scholar";
                startDialogue(dlgNpc);
            } else if (c=='U' && !st.up.empty()) { requestStageTransition(st.up, true); return; }
            else if (c=='D' && !st.down.empty()) { requestStageTransition(st.down, false); return; }
        }
    }
    // stairs check (cell char)
    ++storyTurns_;                 // S3.5 (c): the footer line rotates with movement, not with time
    if (c=='U' && !st.up.empty()) requestStageTransition(st.up, true);
    else if (c=='D' && !st.down.empty()) requestStageTransition(st.down, false);
}

void Game::setPadScale(float s) {
    // Clamped: a runaway value would push the pad off its own screen edge.
    toms::game_detail::kPadScale = (s < 0.8f) ? 0.8f : ((s > 1.8f) ? 1.8f : s);
}

void Game::interact() {
    if (modalActive()) return;   // any modal overlay blocks world interaction
    // find NPC on player's cell or adjacent
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        if (e.kind.rfind("npc:",0)!=0) continue;
        if (entityCovers(e, pl.x, pl.y)) {
            std::string npc;
            if (e.id=="villager") npc="villager_elder";
            else if (e.id=="sorcerer") npc="sorcerer_teacher";
            else if (e.id=="king") npc="king_lieutenant";
            else if (e.id=="princess") npc=(curStage=="stage_11")?"princess_victory":"princess_liora";
            else if (e.id=="handmaiden") npc="handmaiden";
            else if (e.id=="skeleton_scholar") npc="skeleton_scholar";
            else continue;
            startDialogue(npc);
            return;
        }
    }
}

// S1: one turn for every room-wandering event monster. Walkability is answered here (the roamer
// class stays pure logic -- see roamer.h) and excludes tiles held by other live entities, so two
// roamers cannot stack and a roamer cannot walk into the player or an item.
void Game::advanceRoamers() {
    if (roamers_.empty()) return;
    const int gw = (int)st.width, gh = (int)st.height;
    for (auto& [entIndex, brain] : roamers_) {
        if (entIndex < 0 || entIndex >= (int)st.entities.size()) continue;
        const Entity& self = st.entities[entIndex];
        if (self.consumed) continue;   // fought and cleared: it stops roaming
        struct Query : toms::GridQuery {
            const Stage& s; const Entity& me; int myIndex; int px, py;
            Query(const Stage& s_, const Entity& me_, int idx, int px_, int py_)
                : s(s_), me(me_), myIndex(idx), px(px_), py(py_) {}
            bool walkable(int x, int y) const override {
                if (s.at(x, y) == '#') return false;
                if (x == px && y == py) return false;   // the player blocks (captured by value:
                                                        // the world doesn't move mid-turn)
                for (int i = 0; i < (int)s.entities.size(); ++i) {
                    const Entity& o = s.entities[i];
                    if (i == myIndex || o.consumed) continue;
                    if (o.kind.rfind("monster:", 0) != 0) continue;   // items/NPCs are not obstacles
                    if (toms::footprintCovers(o.x, o.y, o.fp, x, y)) return false;
                }
                (void)me;
                return true;
            }
        } query(st, self, entIndex, pl.x, pl.y);
        brain.step(query, pl.x, pl.y, gw, gh);
        // The brain owns the position; write it back so drawing, blocking and bump-to-fight all see
        // the roamer where it actually is.
        st.entities[entIndex].x = brain.x();
        st.entities[entIndex].y = brain.y();
    }
}

// Shared by movePlayer() (bumping a monster tile) and debugStartNearestBattle() (verification
// harnesses): builds the EnemyInst from the entity and starts the fight -- or its dialogue gate.
void Game::engageMonster(const Entity& e) {
    EnemyInst en;
    auto& t = enemyTpl[e.id];
    en.id=e.id; en.name=locale_.field(t["name"]); en.hp=t["hp"]; en.atk=t["atk"]; en.def=t["def"];
    en.exp=t["exp"]; en.gold=t["gold"]; en.x=e.x; en.y=e.y; en.boss=t.value("boss",false);
    en.atkIntervalMs = t.value("atk_interval_ms", 4000);
    // Milestone 4 (Encounter Resolution): resolveEncounterKind returns DirectBattle for every
    // monster tile in every shipped stage today (no stage sets encounter_overrides yet), so this is
    // byte-for-byte the same behavior as before unless/until a stage opts a tile into dialogue_gate.
    toms::EncounterKind ek = toms::resolveEncounterKind(e.kind, e.encounterOverride);
    if (ek == toms::EncounterKind::DialogueGate) {
        pendingEncounterEnemy_ = en;
        hasPendingEncounter_ = true;
        dlgNpc = "enemy_" + e.id;   // so ChoiceMade{dlgNpc,...} carries the right id
        startDialogue(dlgNpc);
    } else {
        startCombat(en);
    }
}

// Verification hook for the browser build's deploy harness (exposed as jsDebugBattle in
// emscripten_main.cpp): start a fight with the nearest un-consumed monster so the battle scene and
// its input can be driven without walking the maze first. Returns true when a fight is running.
bool Game::debugStartNearestBattle() {
    if (cs.active) return true;
    int bestD = 1 << 30;
    const Entity* best = nullptr;
    for (const auto& e : st.entities) {
        if (e.consumed || e.kind.rfind("monster:", 0) != 0) continue;
        int d = std::abs(e.x - pl.x) + std::abs(e.y - pl.y);
        if (d < bestD) { bestD = d; best = &e; }
    }
    if (!best) return false;
    engageMonster(*best);
    return cs.active;
}

bool Game::debugEquip(const std::string& equipmentId) {
    auto it = equipmentDefs_.find(equipmentId);
    if (it == equipmentDefs_.end()) return false;
    switch (it->second.slot) {
        case toms::EquipmentSlot::Weapon: equipped_.weaponId = equipmentId; break;
        case toms::EquipmentSlot::Armor:  equipped_.armorId  = equipmentId; break;
        case toms::EquipmentSlot::Talent: equipped_.talentId = equipmentId; break;
    }
    return true;
}

void Game::debugForceLose() {
    cs.enemy.boss = false;   // a non-boss death is what deathsNonBoss (e_13) actually counts
    finishCombatLose();
}

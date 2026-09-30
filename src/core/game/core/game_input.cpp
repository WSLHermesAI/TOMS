// game_input.cpp — input: map clicks (click-to-move), held-direction
// movement, world interaction, and bump-to-fight. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::handleTouch(float px, float py, int phase) {
    // Only clicks that no UI element took arrive here (the RmlUi documents handle every button),
    // so the one thing left is the map: click-to-move, or interact on the player's own tile.
    if (!(px == px) || !(py == py)) return;             // NaN (zero-size canvas)
    if (px < 0 || px > 1024 || py < 0 || py > 768) return;
    if (phase != 0 || modalActive()) return;
    int tx, ty;
    if (!screenToTile(px, py, tx, ty)) return;
    if (tx == pl.x && ty == pl.y) { cancelWalk(); interact(); }
    else walkTo(tx, ty);
}

// Milestone 9 polish: see setMoveHeldX/Y's declaration in game.h. A fresh press (or switching
// directions on the same axis) fires one immediate step, same feel as the old one-tap-one-tile
// behavior; the repeat while still held is ticked in update().
void Game::setMoveHeldX(int dir) {
    if (dir != 0) cancelWalk();   // steering by hand takes over from click-to-move
    if (dir == moveHoldX_.dir) return;
    moveHoldX_.dir = dir; moveHoldX_.holdMs = 0; moveHoldX_.repeating = false;
    if (dir != 0) movePlayer(dir, 0);
}

void Game::setMoveHeldY(int dir) {
    if (dir != 0) cancelWalk();
    if (dir == moveHoldY_.dir) return;
    moveHoldY_.dir = dir; moveHoldY_.holdMs = 0; moveHoldY_.repeating = false;
    if (dir != 0) movePlayer(0, dir);
}

// ---- Click-to-move (see walkTo()'s declaration in game.h) ----

bool Game::screenToTile(float px, float py, int& tx, int& ty) const {
    if (!ren) return false;
    float ts; int cols, rows; cameraViewportTiles(ts, cols, rows);
    if (ts <= 0.0f) return false;
    // draw(): tile (x,y) is at (-camX*ts + x*ts, 60 - camY*ts + y*ts).
    tx = (int)std::floor(px / ts + cam_.x());
    ty = (int)std::floor((py - 60.0f) / ts + cam_.y());
    return tx >= 0 && ty >= 0 && tx < (int)st.width && ty < (int)st.height;
}

bool Game::walkPassable(int x, int y) const {
    const char c = st.at(x, y);
    if (c == '#' || c == 'y' || c == 'b' || c == 'r' || c == 'U' || c == 'D') return false;
    for (const auto& e : st.entities)
        if (!e.consumed && entityCovers(e, x, y)) return false;   // monsters, items, NPCs, events
    return true;
}

// Breadth-first search from the player to (tx,ty): shortest in steps, 4-way like the arrows.
bool Game::planWalk(int tx, int ty, std::vector<std::pair<int, int>>& out) const {
    out.clear();
    const int gw = (int)st.width, gh = (int)st.height;
    if (tx < 0 || ty < 0 || tx >= gw || ty >= gh || st.at(tx, ty) == '#') return false;
    if (tx == pl.x && ty == pl.y) return false;
    std::vector<int> prev((size_t)gw * gh, -1);
    std::vector<int> queue;
    const int start = pl.y * gw + pl.x, goal = ty * gw + tx;
    prev[start] = start;
    queue.push_back(start);
    static const int kDx[4] = {0, 0, -1, 1}, kDy[4] = {-1, 1, 0, 0};
    for (size_t head = 0; head < queue.size() && prev[goal] < 0; ++head) {
        const int cx = queue[head] % gw, cy = queue[head] / gw;
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + kDx[d], ny = cy + kDy[d];
            if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
            const int n = ny * gw + nx;
            if (prev[n] >= 0) continue;
            if (n != goal && !walkPassable(nx, ny)) continue;   // the target itself may be anything
            prev[n] = queue[head];
            queue.push_back(n);
        }
    }
    if (prev[goal] < 0) return false;
    for (int n = goal; n != start; n = prev[n]) out.emplace_back(n % gw, n / gw);
    std::reverse(out.begin(), out.end());
    return true;
}

bool Game::walkTo(int tx, int ty) {
    cancelWalk();
    if (modalActive()) return false;
    std::vector<std::pair<int, int>> path;
    if (!planWalk(tx, ty, path)) return false;
    walkPath_ = std::move(path);
    walkTargetX_ = tx; walkTargetY_ = ty;
    walkStep();   // first step right away, like a key press; update() paces the rest
    return true;
}

void Game::walkStep() {
    if (walkPath_.empty()) return;
    // The world moves between steps (roamers): if the next tile is no longer free, re-plan.
    if (walkPath_.size() > 1 && !walkPassable(walkPath_.front().first, walkPath_.front().second)) {
        std::vector<std::pair<int, int>> path;
        if (!planWalk(walkTargetX_, walkTargetY_, path)) { cancelWalk(); return; }
        walkPath_ = std::move(path);
    }
    const auto [nx, ny] = walkPath_.front();
    if (std::abs(nx - pl.x) + std::abs(ny - pl.y) != 1) { cancelWalk(); return; }
    walkPath_.erase(walkPath_.begin());
    movePlayer(nx - pl.x, ny - pl.y);
    // Didn't get there (a locked door, a bumped boss) or arrived: stop. A battle, dialogue or the
    // stairs dialog it may have opened is caught by update()'s modal check.
    if (pl.x != nx || pl.y != ny || walkPath_.empty()) cancelWalk();
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

// Verification hook (see game.h; no host calls it at the moment): start a fight with the nearest un-consumed monster so the battle scene and
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

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

void Game::openDoor(int x, int y, char c) {
    if (c == 'y') pl.key_yellow--;
    if (c == 'b') pl.key_blue--;
    if (c == 'r') pl.key_red--;
    st.tiles[y][x] = '.';
    for (auto& e : st.entities)   // the door's own map entry ("door:yellow" in the legend) no longer blocks or draws
        if (!e.consumed && e.x == x && e.y == y && e.kind.rfind("door:", 0) == 0) e.consumed = true;
    toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, x, y), toms::EntityStatus::Opened);
    for (const auto& e : st.entities) if (e.x == x && e.y == y && e.kind.rfind("door:", 0) == 0) fireLinkedEvents(e);
    audio.play("confirm_click");
    (void)c;
    if (MapObject* door = floor_.objectAt(x, y, "door:")) door->play("opening");   // its clip plays on the tile
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
        openDoor(nx, ny, c);   // uses the key; the door opens and vanishes; the player stays put this turn
        return;
    }
    // Something that is bumped instead of stepped onto (a gate: it opens for its key; the player stays put).
    for (const auto& o : floor_.objects())
        if (o->bumpBeforeStep(nx, ny)) return;
    // Q7: a locked stair is not stepped on: it says which event opens it.
    if (const toms::StairLink* s = toms::stairAt(st.stairs, nx, ny); s && stairLocked(*s)) {
        bumpLockedStairs(*s);
        return;
    }
    // S1: a multi-grid monster is not walked into; it is fought from the adjacent tile (MonsterObject::bumpBig).
    for (const auto& o : floor_.objects())
        if (o->bumpBig(nx, ny)) return;
    pl.x = nx; pl.y = ny;
    audio.play("walk");
    // The player acted -> the world takes its turn (roamers step once; S1).
    advanceRoamers();
    // What is on the new cell, in the stage file's order: each object does what its kind does (a monster's battle,
    // an item picked up, an event, an NPC's talk, a stair); Stop ends the step. A copy of the list, and a stop if the
    // floor changed under it (an event's action, a stair).
    const int floorGen = floor_.generation();
    const std::vector<std::shared_ptr<MapObject>> here = floor_.objects();
    for (const auto& o : here) {
        const Entity* e = o->entity();
        if (!e || e->consumed || !o->covers(nx, ny)) continue;
        if (o->onTouch(nx, ny) == MapObject::Touch::Stop || floor_.generation() != floorGen) return;
    }
    // stairs check (cell char)
    ++storyTurns_;                 // S3.5 (c): the footer line rotates with movement, not with time
    stepOntoStairs(nx, ny);
}

// Q7 (stair_links.h): the stair under (x, y) leads where its link says; a locked one is never reached here
// (movePlayer stops at it).
bool Game::stepOntoStairs(int x, int y) {
    const toms::StairLink* s = toms::stairAt(st.stairs, x, y);
    if (!s || s->to.empty() || stairLocked(*s)) return false;
    requestStageTransition(s->to, s->up);
    stairsConfirmGoEvent_ = toms::goStairsEventId(st.id, s->to);
    return true;
}

void Game::bumpLockedStairs(const toms::StairLink& s) {
    auto d = eventDefs_.find(s.unlockEvent);
    std::string name = d == eventDefs_.end() ? std::string() : locale_.tr(d->second.titleKey);
    if (name.empty() || (d != eventDefs_.end() && name == d->second.titleKey)) name = s.unlockEvent;
    // Before the event is met, its name is a spoiler: the event log shows "???" for it too.
    if (!run_.eventStarted(s.unlockEvent)) name = locale_.tr("events.unknown");
    const std::string msg = toms::fillText(locale_.tr("stairs.locked"), {{"event", name}});
    if (notifications_.empty() || notifications_.back().first != msg) notifications_.push_back({msg, 3200});
    audio.play("confirm_click");
}

void Game::finishEvent(const std::string& evId, int depth) {
    const bool already = run_.eventFinished(evId);
    run_.setFlag("event_" + evId, true);
    run_.markEventFinished(evId);
    auto def = eventDefs_.find(evId);
    if (def != eventDefs_.end() && !already)
        for (const std::string& n : def->second.next) wakeEvent(n, depth);   // started; a "chain" event runs now
    // Q7: the stairs this event opens. A dialog names each one, and its "go to the stairs" event starts.
    if (auto u = stairUnlocks_.find(evId); u != stairUnlocks_.end() && !already)
        for (const StairUnlock& s : u->second) {
            run_.markEventStarted(toms::goStairsEventId(s.floor, s.to));
            const std::map<std::string, std::string> vars = {{"from", toms::floorLabel(s.floor)}, {"to", toms::floorLabel(s.to)}};
            notices_.push_back({locale_.tr("stairs.unlocked.title"), toms::fillText(locale_.tr("stairs.unlocked.body"), vars)});
        }
    markProgressDirty();
}

void Game::bumpGate(Entity& gate) {
    const std::string& keyId = gate.id;
    auto it = std::find(pl.inv.begin(), pl.inv.end(), keyId);
    const std::map<std::string, std::string> vars = {{"item", itemName(keyId)}};
    if (it == pl.inv.end()) {
        const std::string msg = toms::fillText(locale_.tr("gate.locked"), vars);
        if (notifications_.empty() || notifications_.back().first != msg) notifications_.push_back({msg, 3200});
        audio.play("confirm_click");
        return;
    }
    pl.inv.erase(it);   // the key goes into the lock
    gate.consumed = true;
    st.tiles[gate.y][gate.x] = '.';
    toms::setEntityStatus(entityStatus_, toms::entityStatusKey(curStage, gate.x, gate.y), toms::EntityStatus::Opened);
    fireLinkedEvents(gate);
    notifications_.push_back({toms::fillText(locale_.tr("gate.opened"), vars), 3200});
    audio.play("confirm_click");
    markProgressDirty();   // (GateObject plays its opening clip)
}

void Game::dismissNotice() {
    if (notices_.empty()) return;
    notices_.erase(notices_.begin());
    audio.play("confirm_click");
}

void Game::loadStairLocks() {
    stairUnlocks_.clear();
    for (auto it = eventDefs_.begin(); it != eventDefs_.end();)
        it = it->second.stairsFrom.empty() ? std::next(it) : eventDefs_.erase(it);
    for (const toms::FloorInfo& f : floors_.all())
        for (const toms::StairLink& s : f.stairs) {
            if (s.unlockEvent.empty()) continue;
            stairUnlocks_[s.unlockEvent].push_back({f.id, s.to});
            EventDef d;
            d.kind = "stairs";
            d.stairsFrom = f.id;
            d.stairsTo = s.to;
            eventDefs_[toms::goStairsEventId(f.id, s.to)] = d;
        }
}

void Game::debugLockStairs(const std::string& floor, const std::string& eventId) {
    toms::FloorInfo* f = floors_.edit(floor);
    if (!f || eventId.empty()) return;
    if (!f->hasStairs) {
        f->stairs = toms::defaultStairLinks(f->nextFloor, floors_.prev(f->id));
        f->hasStairs = true;
    }
    for (toms::StairLink& s : f->stairs)
        if (s.up) { s.unlockEvent = eventId; break; }
    loadStairLocks();
    if (st.id == floor)   // the floor the player is on: its stairs take the lock now
        for (toms::StairLink& s : st.stairs)
            if (s.up) { s.unlockEvent = eventId; break; }
}

void Game::debugFinishEvent(const std::string& eventId) {
    if (!eventId.empty()) finishEvent(eventId);
}


void Game::interact() {
    if (modalActive()) return;   // any modal overlay blocks world interaction
    // what can be talked to on the player's tile (an NPC)
    for (const auto& o : floor_.objects())
        if (o->onInteract()) return;
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
    if (equipmentDefs_.find(equipmentId) == equipmentDefs_.end()) return false;
    ownGear(equipmentId, true);
    return true;
}

void Game::debugForceLose() {
    cs.enemy.boss = false;   // a non-boss death is what deathsNonBoss (e_13) actually counts
    finishCombatLose();
}

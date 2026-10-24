// map_objects.cpp -- see map_objects.h. The rules here moved from Game::movePlayer / interact / openDoor / bumpGate
// and the drawing from Game::draw's object pass, unchanged; they now live with the kind of object they belong to.
#include "map_objects.h"

#include "game_internal.h"

#include <set>

using namespace toms::game_detail;

namespace map_detail {
const float kWhite[4] = {1, 1, 1, 1};

// A status's picture: render objects (render_object.h, made from JSON by Game::renderFactory_) placed in the object's
// rect each frame -- the one way anything on the floor is drawn (docs/engine/12_RESOURCE_MANAGER.md §4.10). A look has
// parts, drawn in order, each in the rect shrunk by `inset` (a fraction of it) on every side:
//  - a sprite: its whole original frame stretched over the rect (the same quad as Game::spriteQuad);
//  - anything else (a clip, an effect, text, a group): its origin on the rect's bottom centre, scaled so `size`
//    (art pixels: a 32 x 32 sprite's clip by default) fills the rect (as Game::drawClipInRect).
// A clip at the top of a part plays on the floor's clock: looping, the game's idle clock at the tile's own phase (a row
// of slimes out of step); once, from the start of its status. Everything else runs on its own (update).
class RenderLook : public MapLook {
public:
    enum class Clock { Idle, Status };
    explicit RenderLook(Clock clock = Clock::Idle) : clock_(clock) {}

    void add(std::shared_ptr<toms::RenderObject> o, float sizeW = 32, float sizeH = 32, float inset = 0) {
        if (!o) return;
        Part p;
        p.sprite = dynamic_cast<toms::SpriteObject*>(o.get());
        p.clip = dynamic_cast<toms::ClipObject*>(o.get());
        p.obj = std::move(o);
        p.sw = sizeW; p.sh = sizeH; p.inset = inset;
        parts_.push_back(std::move(p));
    }
    bool empty() const { return parts_.empty(); }

    // A render object for a description, cloned from one made before (many slimes share one); null when it cannot be made
    // (the reason is logged once per description).
    static std::shared_ptr<toms::RenderObject> renderObject(Game& g, const nlohmann::json& j) {
        if (!g.res_) return nullptr;
        std::string err;
        std::shared_ptr<toms::RenderObject> o = g.renderFactory_.instance(j, *g.res_, &err);
        if (!o) {
            static std::set<std::string> told;
            if (told.insert(j.dump()).second) std::fprintf(stderr, "[map] look %s: %s\n", j.dump().c_str(), err.c_str());
        }
        return o;
    }

    // A sprite of the game's atlas (the art style's), stretched over the rect; a sprite the atlas lacks draws as the floor,
    // as before. solid: one flat colour in its shape.
    static std::shared_ptr<toms::RenderObject> spriteObject(Game& g, std::string name, const float tint[4], bool solid = false) {
        if (name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0) name.resize(name.size() - 4);
        if (!g.idToLayer.count(name)) name = "floor";
        nlohmann::json j = {{"type", "sprite"}, {"name", name}, {"tint", {tint[0], tint[1], tint[2], tint[3]}}};
        if (solid) j["solid"] = true;
        return renderObject(g, j);
    }

    static std::shared_ptr<toms::RenderObject> clipObject(Game& g, const std::string& file, const std::string& clip, const float tint[4]) {
        return renderObject(g, {{"type", "clip"}, {"anim", file}, {"clip", clip}, {"tint", {tint[0], tint[1], tint[2], tint[3]}}});
    }

    bool draw(Game& g, const MapView& v, const MapRect& r, int tileX, int tileY, float statusMs) const override {
        // the first part decides whether there is anything to draw (a clip without its art: the next look is tried)
        if (parts_.empty() || !parts_.front().obj->ok()) return false;
        if (parts_.front().clip && g.spriteAtlas_.pages.empty()) return false;
        // placed on the map (the rect without the camera), seen through the camera: an effect stays where it was born
        const glm::mat3 camera = toms::anim::placement(v.ox, v.oy);
        for (const Part& p : parts_) {
            const MapRect q{r.x - v.ox + p.inset * r.w, r.y - v.oy + p.inset * r.h, r.w * (1 - 2 * p.inset), r.h * (1 - 2 * p.inset)};
            p.obj->setView(camera);
            if (p.sprite) {
                const toms::AtlasRegion* a = p.sprite->atlas()->find(p.sprite->spriteName());
                if (!a || a->origW <= 0 || a->origH <= 0) continue;
                p.sprite->setPos(q.x + a->pivot[0] * q.w, q.y + a->pivot[1] * q.h);
                p.sprite->setScale(q.w / (float)a->origW, q.h / (float)a->origH);
            } else {
                p.obj->setPos(q.x + q.w * 0.5f, q.y + q.h);
                p.obj->setScale(q.w / p.sw, q.h / p.sh);
            }
            if (p.clip) {
                const float len = p.clip->duration();
                float t;
                if (clock_ == Clock::Idle) {
                    const double phase = (double)(((unsigned)tileX * 73856093u ^ (unsigned)tileY * 19349663u) % 997u) / 997.0 * len;
                    t = len > 0 ? (float)std::fmod(g.idleClockMs_ / 1000.0 + phase, (double)len) : 0.0f;
                } else {
                    t = std::min(statusMs / 1000.0f, len);
                }
                p.clip->setTime(t);
            }
            p.obj->RenderTree(g.ren);
        }
        return true;
    }
    float onceSeconds() const override {
        const toms::ClipObject* c = clock_ == Clock::Status && !parts_.empty() ? parts_.front().clip : nullptr;
        return c ? c->duration() : 0.0f;
    }
    void update(float dt) override {
        for (const Part& p : parts_)
            if (!p.clip) p.obj->update(dt);   // effects and groups; a top clip follows the floor's clock instead
    }

private:
    struct Part {
        std::shared_ptr<toms::RenderObject> obj;
        toms::SpriteObject* sprite = nullptr;   // obj, when it is one (placed by its frame)
        toms::ClipObject* clip = nullptr;       // obj, when it is one (set to the floor's clock)
        float sw = 32, sh = 32, inset = 0;
    };
    Clock clock_;
    std::vector<Part> parts_;
};

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

void readTint(const nlohmann::json& j, float out[4]) {
    if (j.is_object() && j.contains("tint") && j["tint"].is_array())
        for (int i = 0; i < 4 && i < (int)j["tint"].size(); i++)
            if (j["tint"][i].is_number()) out[i] = j["tint"][i].get<float>();
}

}  // namespace map_detail

using namespace map_detail;

// ---- LookChain -----------------------------------------------------------------------------------------------------

bool LookChain::draw(Game& g, const MapView& v, const MapRect& r, int tileX, int tileY, float statusMs) const {
    for (const auto& l : looks)
        if (l->draw(g, v, r, tileX, tileY, statusMs)) return true;
    return false;
}

float LookChain::onceSeconds() const { return looks.empty() ? 0.0f : looks.front()->onceSeconds(); }

// ---- MapObject -----------------------------------------------------------------------------------------------------

MapObject::MapObject(Game& g, int entityIndex) : g_(g), index_(entityIndex) {}

Entity* MapObject::entity() { return index_ >= 0 && index_ < (int)g_.st.entities.size() ? &g_.st.entities[(size_t)index_] : nullptr; }
const Entity* MapObject::entity() const {
    return index_ >= 0 && index_ < (int)g_.st.entities.size() ? &g_.st.entities[(size_t)index_] : nullptr;
}

std::string MapObject::status() const {
    const Entity* e = entity();
    if (status_ == "idle" && e && e->consumed) return "gone";
    return status_;
}

void MapObject::setStatus(const std::string& s) {
    status_ = s;
    statusMs_ = 0;
}

bool MapObject::play(const std::string& s) {
    const LookChain* c = look(s);
    if (!c || c->empty()) return false;
    setStatus(s);
    return true;
}

const LookChain* MapObject::look(const std::string& status) const {
    auto it = looks_.find(status);
    return it == looks_.end() ? nullptr : &it->second;
}

void MapObject::setLook(const std::string& status, LookChain chain) { looks_[status] = std::move(chain); }

std::vector<std::string> MapObject::lookStatuses() const {
    std::vector<std::string> out;
    for (const auto& [s, c] : looks_) out.push_back(s);
    return out;
}

const std::vector<std::string>& MapObject::events() const {
    static const std::vector<std::string> none;
    const Entity* e = entity();
    return e ? e->links : none;
}

// A one-shot clip moves to its next status when it has played out (a door: opening -> gone).
void MapObject::update(int dtMs) {
    statusMs_ += (float)dtMs;
    if (const LookChain* c = look(status_)) {
        c->update(dtMs / 1000.0f);   // effects in its look move on
        const float once = c->onceSeconds();
        if (once > 0 && statusMs_ / 1000.0f > once) setStatus(c->next.empty() ? "gone" : c->next);
    }
}

MapRect MapObject::rect(const MapView& v) const {
    const Entity* e = entity();
    MapRect r;
    if (!e) return r;
    r.w = e->fp.w * v.ts - 2.0f * v.inset;
    r.h = e->fp.h * v.ts - 2.0f * v.inset;
    r.x = v.ox + e->x * v.ts + v.inset;
    r.y = v.oy + e->y * v.ts + v.inset;
    return r;
}

int MapObject::sortKey() const {
    const Entity* e = entity();
    return e ? g_.footprintSortKey_T(*e) : 0;
}

int MapObject::tieX() const {
    const Entity* e = entity();
    return e ? e->x : 0;
}

void MapObject::render(const MapView& v) {
    const std::string s = status();
    if (s == "gone") return;
    const LookChain* c = look(s);
    if (!c) c = look("idle");   // a status with no look of its own looks idle
    const Entity* e = entity();
    if (c) c->draw(g_, v, rect(v), e ? e->x : 0, e ? e->y : 0, statusMs_);
}

bool MapObject::covers(int x, int y) const {
    const Entity* e = entity();
    return e && g_.entityCovers(*e, x, y);
}

// Anything else on the tile (a stair, an unknown kind): only the stairs have something to say.
MapObject::Touch MapObject::onTouch(int x, int y) { return g_.stepOntoStairs(x, y) ? Touch::Stop : Touch::Pass; }

nlohmann::json MapObject::details() const {
    nlohmann::json j = nlohmann::json::object();
    j["class"] = Type();
    j["status"] = status();
    if (const Entity* e = entity()) {
        j["kind"] = e->kind;
        j["id"] = e->id;
        j["cell"] = {e->x, e->y};
        if (e->fp.w != 1 || e->fp.h != 1) j["footprint"] = {e->fp.w, e->fp.h};
        if (e->roamer) j["roamer"] = true;
        if (!e->links.empty()) j["events"] = e->links;
        if (e->look.is_object()) j["ownLook"] = e->look;
    }
    j["looks"] = lookStatuses();
    return j;
}

// ---- the kinds -----------------------------------------------------------------------------------------------------

// S1: a multi-grid monster is not walked into: entering a 4-grid boss's cell would put the player inside it, so big
// footprints are fought from the adjacent tile (doc F4: the player can only circle it).
bool MonsterObject::bumpBig(int x, int y) {
    const Entity* e = entity();
    if (!e || e->consumed || !e->fp.big() || !covers(x, y)) return false;
    g_.advanceRoamers();
    g_.engageMonster(*entity());
    return true;
}

MapObject::Touch MonsterObject::onTouch(int, int) {
    g_.engageMonster(*entity());
    return Touch::Stop;
}

nlohmann::json MonsterObject::details() const {
    nlohmann::json j = MapObject::details();
    if (const Entity* e = entity()) {
        auto t = g_.enemyTpl.find(e->id);
        if (t != g_.enemyTpl.end())
            for (const char* k : {"hp", "atk", "def", "exp", "gold"})
                if (t->second.contains(k)) j[k] = t->second[k];
    }
    return j;
}

// Keys and coins apply at once; usable items (gems, potions, scrolls) go into the inventory.
MapObject::Touch ItemObject::onTouch(int, int) {
    Entity& e = *entity();
    g_.receiveItem(e.id);
    e.consumed = true;
    g_.st.tiles[(size_t)e.y][(size_t)e.x] = '.';
    // Milestone 7: the clear survives a reload of this floor (entityStatus_).
    toms::setEntityStatus(g_.entityStatus_, g_.statusKeyAt(e.x, e.y), toms::EntityStatus::Collected);
    toms::globalEventBus().publish(toms::ItemCollected{e.id, g_.curStage});
    g_.fireLinkedEvents(e);
    return Touch::Pass;
}

// S3.5 (d): a floor event. Its text comes from the i18n table; its effect from the event id's own vocabulary until
// the pools are loaded at runtime: shards feed the memory-shard state, traps cost HP, caches / relics give a little back.
MapObject::Touch EventObject::onTouch(int, int) {
    Entity& e = *entity();
    Game& g = g_;
    const std::string evId = e.id;
    // An event whose `requires` does not hold yet only becomes "started": it stays on its tile, and the event log
    // lists it as in progress (docs/gameplay/event_editor/06_EVENT_LOGIC.md section 7).
    auto def = g.eventDefs_.find(evId);
    if (def != g.eventDefs_.end() && !def->second.requires.is_null() &&
        !toms::evaluate(def->second.requires, GameConditionContext(g.pl, g.meta_, g.missionTrackers_, g.run_, g.equipped_))) {
        if (!g.run_.eventStarted(evId)) {
            g.run_.markEventStarted(evId);
            const std::string title = g.locale_.tr(def->second.titleKey);
            g.notifications_.push_back({title == def->second.titleKey ? evId : title, 3000});
            g.markProgressDirty();
        }
        return Touch::Stop;
    }
    std::string text = g.locale_.tr(evId + ".text");
    if (text.empty() || text == evId + ".text") text = evId;   // missing -> the id, not blank
    if (evId.find("shard") != std::string::npos) {
        g.run_.addShard(evId);
        g.notifications_.push_back({text + "  [memory shard " + std::to_string(g.run_.shardCount()) + "]", 4200});
    } else if (evId.find("trap") != std::string::npos) {
        g.pl.hp = std::max(1, g.pl.hp - 8);
        g.notifications_.push_back({text + "  [-8 HP]", 3800});
    } else if (evId.find("cache") != std::string::npos || evId.find("relic") != std::string::npos) {
        g.pl.hp = std::min(g.effectiveMaxHp(), g.pl.hp + 10);
        g.pl.gold += 12;
        g.notifications_.push_back({text + "  [+10 HP, +12 GOLD]", 3800});
    } else {
        g.notifications_.push_back({text, 4200});   // whispers / rescues: remembered, no stat
    }
    // The record's `actions`, then finished the first time (06 section 6 item 2): the run flag, the event log, its
    // `next` events woken, and the stairs it opens say so (Q7).
    const int gen = g.floor_.generation();
    g.completeEvent(evId);
    if (g.floor_.generation() != gen) return Touch::Stop;   // its actions took the player to another floor
    g.fireLinkedEvents(*entity());
    // A repeatable event ("once": false) stays on its tile until its last run (08 section 11.1).
    if (g.canRepeat(evId)) return Touch::Stop;
    Entity& done = *entity();
    done.consumed = true;
    g.st.tiles[(size_t)done.y][(size_t)done.x] = '.';
    toms::setEntityStatus(g.entityStatus_, g.statusKeyAt(done.x, done.y), toms::EntityStatus::Collected);
    return Touch::Stop;
}

// NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): the linked events first (a linked talk event is then
// one of this talk's topics), then its NPC file decides between a chat line, a topic and the topic menu.
MapObject::Touch NpcObject::onTouch(int, int) {
    g_.fireLinkedEvents(*entity());
    g_.talkTo(entity()->id);
    return Touch::Pass;
}

bool NpcObject::onInteract() {
    const Entity* e = entity();
    if (!e || e->consumed || !covers(g_.pl.x, g_.pl.y)) return false;
    g_.fireLinkedEvents(*e);
    g_.talkTo(e->id);
    return true;
}

MapRect GateObject::rect(const MapView& v) const {
    const Entity* e = entity();
    MapRect r;
    if (e) { r.x = v.ox + e->x * v.ts; r.y = v.oy + e->y * v.ts; r.w = v.ts; r.h = v.ts; }
    return r;
}

// A gate opens only for its own key item; the player stays put this turn either way.
bool GateObject::bumpBeforeStep(int x, int y) {
    const Entity* e = entity();
    if (!e || e->consumed || e->x != x || e->y != y) return false;
    g_.bumpGate(*entity());
    if (entity()->consumed) play("opening");   // the key went in: the door-opening clip plays on its tile
    return true;
}

void GateObject::renderUnderlay(const MapView& v) {
    if (status_ != "opening") return;
    if (const LookChain* c = look("opening")) c->draw(g_, v, rect(v), entity()->x, entity()->y, statusMs_);
}

MapRect DoorObject::rect(const MapView& v) const {   // the full tile
    const Entity* e = entity();
    MapRect r;
    if (e) { r.x = v.ox + e->x * v.ts; r.y = v.oy + e->y * v.ts; r.w = v.ts; r.h = v.ts; }
    return r;
}

void DoorObject::renderUnderlay(const MapView& v) {
    if (status_ != "opening") return;
    if (const LookChain* c = look("opening")) c->draw(g_, v, rect(v), entity()->x, entity()->y, statusMs_);
}

MapRect PlayerObject::rect(const MapView& v) const {
    MapRect r;
    r.w = v.sSize;
    r.h = v.sSize;
    r.x = v.ox + g_.pl.x * v.ts + v.inset;
    r.y = v.oy + g_.pl.y * v.ts + v.inset;
    return r;
}

int PlayerObject::sortKey() const { return g_.footprintSortKey_T(g_.pl.x, g_.pl.y, toms::Footprint{}); }
int PlayerObject::tieX() const { return g_.pl.x; }

nlohmann::json PlayerObject::details() const {
    nlohmann::json j = MapObject::details();
    j["cell"] = {g_.pl.x, g_.pl.y};
    j["hp"] = g_.pl.hp;
    j["atk"] = g_.pl.atk;
    j["def"] = g_.pl.def;
    j["gold"] = g_.pl.gold;
    return j;
}

// ---- building a floor's objects ------------------------------------------------------------------------------------

// Makes a floor's objects and their looks (a friend of Game: it reads the type tables and the loaded art).
struct map_detail::Builder {
// The original size of a game-atlas sprite (what a clip made around it is scaled by); 32 x 32 when unknown.
static void spriteSize(const Game& g, const std::string& sprite, float& w, float& h) {
    const toms::AtlasRegion* r = g.spriteAtlas_.find(sprite);
    w = r && r->origW > 0 ? (float)r->origW : 32.0f;
    h = r && r->origH > 0 ? (float)r->origH : 32.0f;
}

static std::shared_ptr<MapObject> makeObject(Game& g, int index, const Entity& e) {
    if (startsWith(e.kind, "monster:")) return std::make_shared<MonsterObject>(g, index);
    if (startsWith(e.kind, "item:")) return std::make_shared<ItemObject>(g, index);
    if (startsWith(e.kind, "event:")) return std::make_shared<EventObject>(g, index);
    if (startsWith(e.kind, "npc:")) return std::make_shared<NpcObject>(g, index);
    if (startsWith(e.kind, "gate:")) return std::make_shared<GateObject>(g, index);
    if (startsWith(e.kind, "door:")) return std::make_shared<DoorObject>(g, index);
    if (e.kind == "stairs_up" || e.kind == "stairs_down" || e.kind == "player_start") return std::make_shared<StairsObject>(g, index);
    return std::make_shared<MapObject>(g, index);
}

// A look from the data's "looks" table: any render object (render_object.h): {"type": "sprite", "name": ...}, "clip",
// "particle", "text" or "group" -- plus "next" (the status after a one-shot clip), "loop" (a clip: on the idle clock,
// else once from the start of the status) and "size" ([w, h]: the art pixels that fill the rect, default 32 x 32).
// A mistake draws the red "missing" box, so it is seen. Empty = nothing drawn.
static LookChain lookFromJson(Game& g, const nlohmann::json& d) {
    LookChain c;
    if (!d.is_object() || !g.res_) return c;
    c.next = d.value("next", std::string());
    float tint[4] = {1, 1, 1, 1};
    readTint(d, tint);
    const std::string type = d.value("type", std::string());
    auto look = std::make_unique<RenderLook>(type == "clip" && !d.value("loop", false) ? RenderLook::Clock::Status
                                                                                     : RenderLook::Clock::Idle);
    float sw = 32, sh = 32;
    if (d.contains("size") && d["size"].is_array() && d["size"].size() == 2) { sw = d["size"][0].get<float>(); sh = d["size"][1].get<float>(); }
    if (type == "sprite") {
        look->add(RenderLook::spriteObject(g, d.value("name", std::string()), tint));
    } else {
        nlohmann::json j = d;
        j.erase("next"); j.erase("size");
        if (type == "clip") j.erase("loop");   // the floor's clock plays it (RenderLook)
        std::string err;
        look->add(g.renderFactory_.makeOrMissing(j, *g.res_, &err), sw, sh);
        if (!err.empty()) std::fprintf(stderr, "[map] look %s: %s\n", d.dump().c_str(), err.c_str());
    }
    if (!look->empty()) c.looks.push_back(std::move(look));
    return c;
}

// The idle look the old fields give (what Game::draw computed every frame): the object's own clip, then the
// monster's idle clip, then its sprite -- or an event's story plate.
static LookChain legacyIdleLook(Game& g, const Entity& e) {
    LookChain c;
    const bool npc = startsWith(e.kind, "npc:");
    std::string sprite = npc ? g.npcSprite(e.id) : entSprite(e.id);
    if (startsWith(e.kind, "event:")) {   // an event's story plate: a gold tile with a darker inset (S3.5 d)
        static const float gold[4] = {0.86f, 0.72f, 0.36f, 0.92f};
        static const float inner[4] = {0.28f, 0.34f, 0.46f, 0.95f};
        const int layer = g.spriteLayer(sprite);
        auto plate = std::make_unique<RenderLook>();
        plate->add(RenderLook::spriteObject(g, sprite, gold));
        if (layer + 1 < (int)g.spriteIds.size()) plate->add(RenderLook::spriteObject(g, g.spriteIds[(size_t)layer + 1], inner), 32, 32, 0.22f);
        c.looks.push_back(std::move(plate));
        return c;
    }
    const nlohmann::json look = g.lookOf(e);
    bool ownSprite = false;
    if (look.contains("sprite") && look["sprite"].is_string()) {
        std::string s = look["sprite"].get<std::string>();
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0) s.resize(s.size() - 4);
        // only a placed object's own sprite replaces the idle clip (a type's sprite is what its clip is made of)
        if (g.spriteAtlas_.find(s)) { sprite = s; ownSprite = e.look.is_object() && e.look.contains("sprite"); }
    }
    float tint[4] = {1, 1, 1, 1};
    readTint(look, tint);
    float wash[4] = {0, 0, 0, 0};
    if (startsWith(e.kind, "gate:")) {
        // a gate is a full-tile door with its key item's "tint" (data/items.json) washed over it, so it reads as no
        // colour of key door (yellow / blue / red)
        const nlohmann::json* d = g.itemDef(e.id);
        const std::string door = d && d->contains("door") && (*d)["door"].is_string() ? (*d)["door"].get<std::string>() : std::string();
        if (!door.empty() && g.idToLayer.count(door)) {   // its own door picture (docs/engine/14_AI_SPRITES.md)
            sprite = door;
        } else {
            sprite = "door_yellow";
            wash[0] = 0.6f; wash[1] = 0.3f; wash[2] = 0.9f; wash[3] = 0.6f;
            if (d && d->contains("tint") && (*d)["tint"].is_array())
                for (int i = 0; i < 3 && i < (int)(*d)["tint"].size(); i++) wash[i] = (*d)["tint"][i].get<float>();
        }
    }
    if (look.contains("anim") && look["anim"].is_string() && g.res_) {
        const std::string ref = look["anim"].get<std::string>();
        const size_t h = ref.find('#');
        if (h != std::string::npos) {
            auto own = std::make_unique<RenderLook>();
            own->add(RenderLook::clipObject(g, ref.substr(0, h), ref.substr(h + 1), tint));
            if (!own->empty()) c.looks.push_back(std::move(own));
        }
    }
    if (startsWith(e.kind, "monster:") && !ownSprite && g.res_) {   // its own sprite: no idle_<id> clip
        std::shared_ptr<toms::res::AnimRes> idle = g.res_->get<toms::res::AnimRes>(toms::key::kAnimMonsterIdle);
        if (idle->ok() && idle->find("idle_" + e.id)) {
            float sw, sh;
            spriteSize(g, entSprite(e.id), sw, sh);
            auto clip = std::make_unique<RenderLook>();
            clip->add(RenderLook::clipObject(g, toms::key::kAnimMonsterIdle, "idle_" + e.id, tint), sw, sh);
            if (!clip->empty()) c.looks.push_back(std::move(clip));
        }
    }
    // an NPC's idle clip "idle_<npc id>" (assets/media/anim/npc_idle.anim; tools/make_idle_clips.py); none: its sprite
    if (npc && !ownSprite && g.res_) {
        std::shared_ptr<toms::res::AnimRes> idle = g.res_->get<toms::res::AnimRes>(toms::key::kAnimNpcIdle);
        if (idle->ok() && idle->find("idle_" + e.id)) {
            float sw, sh;
            spriteSize(g, g.npcSprite(e.id), sw, sh);
            auto clip = std::make_unique<RenderLook>();
            clip->add(RenderLook::clipObject(g, toms::key::kAnimNpcIdle, "idle_" + e.id, tint), sw, sh);
            if (!clip->empty()) c.looks.push_back(std::move(clip));
        }
    }
    auto still = std::make_unique<RenderLook>();
    still->add(RenderLook::spriteObject(g, sprite, tint));
    if (wash[3] > 0) still->add(RenderLook::spriteObject(g, sprite, wash, /*solid=*/true));   // the key colour over it
    c.looks.push_back(std::move(still));
    return c;
}

// A door's / gate's opening clip (assets/media/anim/door_open.anim), on the full tile, once.
static LookChain openingLook(Game& g, const std::string& colour) {
    LookChain c;
    c.next = "gone";
    if (!g.res_) return c;
    std::shared_ptr<toms::res::AnimRes> door = g.res_->get<toms::res::AnimRes>(toms::key::kAnimDoorOpen);
    if (!door->ok() || !door->find("door_open_" + colour)) return c;
    float sw, sh;
    spriteSize(g, "door_" + colour, sw, sh);
    auto opening = std::make_unique<RenderLook>(RenderLook::Clock::Status);
    opening->add(RenderLook::clipObject(g, toms::key::kAnimDoorOpen, "door_open_" + colour, kWhite), sw, sh);
    if (!opening->empty()) c.looks.push_back(std::move(opening));
    return c;
}

// The "looks" table of a type's record and of the object itself: each status's look (the object's win).
static void tableLooks(Game& g, MapObject& o, const nlohmann::json* table) {
    if (!table || !table->is_object()) return;
    for (auto& [status, d] : table->items()) o.setLook(status, lookFromJson(g, d));
}

static void build(Game& g, MapFloor& f, toms::Node& root, std::vector<std::shared_ptr<MapObject>>& objects,
                  std::shared_ptr<PlayerObject>& player);
};

void MapFloor::clear() {
    objects_.clear();
    player_.reset();
    drawList_.clear();
    generation_++;
}

void MapFloor::build(Game& g) {
    clear();
    Builder::build(g, *this, root_, objects_, player_);
}

void map_detail::Builder::build(Game& g, MapFloor& f, toms::Node& root, std::vector<std::shared_ptr<MapObject>>& objects,
                                std::shared_ptr<PlayerObject>& player) {
    (void)f;
    root.SetName("Floor " + g.st.id);
    for (int i = 0; i < (int)g.st.entities.size(); ++i) {
        const Entity& e = g.st.entities[(size_t)i];
        std::shared_ptr<MapObject> o = makeObject(g, i, e);
        o->SetName(e.kind + " (" + std::to_string(e.x) + "," + std::to_string(e.y) + ")");
        if (startsWith(e.kind, "door:")) {
            o->setLook("opening", openingLook(g, e.id));
        } else {
            o->setLook("idle", legacyIdleLook(g, e));
            // the yellow door's opening clip, for a gate drawn as the yellow door (one with its own door just opens)
            const nlohmann::json* key = startsWith(e.kind, "gate:") ? g.itemDef(e.id) : nullptr;
            if (key && !(key->contains("door") && (*key)["door"].is_string())) o->setLook("opening", openingLook(g, "yellow"));
        }
        // the data's own looks per status: the type's record, then the placed object's
        const size_t colon = e.kind.find(':');
        const std::string cat = e.kind.substr(0, colon);
        const nlohmann::json* rec = nullptr;
        if (cat == "monster") { auto t = g.enemyTpl.find(e.id); if (t != g.enemyTpl.end()) rec = &t->second; }
        else if (cat == "item") rec = g.itemDef(e.id);
        if (rec && rec->contains("looks")) tableLooks(g, *o, &(*rec)["looks"]);
        if (e.look.is_object() && e.look.contains("looks")) tableLooks(g, *o, &e.look["looks"]);
        root.AddChildToLast(o.get(), false);
        objects.push_back(std::move(o));
    }
    player = std::make_shared<PlayerObject>(g);
    player->SetName("player");
    {
        LookChain c;
        auto still = std::make_unique<RenderLook>();
        still->add(RenderLook::spriteObject(g, g.playerSprite(), kWhite));
        c.looks.push_back(std::move(still));
        player->setLook("idle", std::move(c));
    }
    root.AddChildToLast(player.get(), false);
}

void MapFloor::update(int dtMs) {
    for (const auto& o : objects_) o->update(dtMs);
}

MapObject* MapFloor::objectAt(int x, int y, const char* kindPrefix) {
    for (const auto& o : objects_) {
        const Entity* e = o->entity();
        if (e && e->x == x && e->y == y && e->kind.rfind(kindPrefix, 0) == 0) return o.get();
    }
    return nullptr;
}

// The underlays (an opening door), then every object of the sorted pass and the player in one pass by the bottom row
// each covers (S1 F5): a 2x2 boss stands behind what is below it, the player behind the monsters below it. The same
// order as before: the stage file's order, then the player, stably sorted.
void MapFloor::render(const MapView& v) {
    for (const auto& o : objects_) o->renderUnderlay(v);
    drawList_.clear();
    for (const auto& o : objects_)
        if (o->sorted() && o->status() != "gone") drawList_.push_back(o.get());
    if (player_) drawList_.push_back(player_.get());
    std::stable_sort(drawList_.begin(), drawList_.end(), [](const MapObject* a, const MapObject* b) {
        const int ka = a->sortKey(), kb = b->sortKey();
        if (ka != kb) return ka < kb;
        return a->tieX() < b->tieX();
    });
    for (MapObject* o : drawList_) o->render(v);
}

// map_objects.h -- the things on a floor, each its own class (docs/25_RESOURCE_MANAGER.md §4.10).
//
// Every entity of the floor (Stage::entities, parsed from the stage file) gets a MapObject made for its kind:
//
//   MonsterObject  touch: the battle (or its dialogue gate); a 2x2 boss is fought from the next tile
//   ItemObject     touch: picked up
//   EventObject    touch: the floor event (its text, its effect, finished or only started); drawn as a story plate
//   NpcObject      touch / Enter: its linked events, then the talk
//   GateObject     bump: opens with its key item (the opening clip plays)
//   DoorObject     a door tile: the tile layer draws it closed; opening it plays its clip
//   StairsObject   a stair tile (the tile layer draws it); stepping on it changes floor
//   PlayerObject   the player (not an entity), sorted with the others
//
// Each object has a status ("idle", "opening", "gone", or any name the data uses) and a look per status, built once
// when the floor loads from the type's record and the object's own settings -- nothing is read from JSON while
// drawing. A look is a chain tried in order: the object's own clip, its idle clip, its sprite; the first that draws
// wins (a clip with no art falls through to the sprite).
//
// Looks per status in the data (all optional; the old "sprite" / "look" fields are the "idle" look):
//   "looks": {"idle":    {"type": "clip", "anim": "anim/monster_idle.anim", "clip": "idle_slime", "loop": true},
//             "opening": {"type": "clip", "anim": "anim/door_open.anim", "clip": "door_open_yellow", "next": "gone"},
//             "hurt":    {"type": "sprite", "name": "slime", "tint": [1, 0.4, 0.4, 1]},
//             "defeated": null}
// A one-shot clip ("loop" false) moves to its "next" status when it ends.
//
// The objects are Nodes under the floor's root (MapFloor::root), so the floor is a tree a hierarchy view can walk.
#pragma once
#include "node.h"
#include "render_iface.h"
#include "resource_types.h"

#include <json.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

class Game;
struct Entity;
class MapFloor;

namespace map_detail {   // map_objects.cpp: the looks and the builder (friends of Game)
class SpriteLook;
class PlateLook;
class ClipLook;
struct Builder;
}  // namespace map_detail

// Where the floor is on screen this frame (Game::draw's camera).
struct MapView {
    float ox = 0, oy = 0;    // pixel of tile (0, 0)
    float ts = 0;            // tile size
    float inset = 0;         // objects are inset this much into their tiles
    float sSize = 0;         // the player's sprite size (ts - 2 * inset)
};

struct MapRect {
    float x = 0, y = 0, w = 0, h = 0;
};

// One way to draw a status, in the object's rect. False: nothing drawn (the next look of the chain is tried).
class MapLook {
public:
    virtual ~MapLook() = default;
    virtual bool draw(Game& g, const MapRect& r, int tileX, int tileY, float statusMs) const = 0;
    virtual float onceSeconds() const { return 0; }   // a one-shot clip: its length (0 = holds)
};

// The looks of one status, tried in order.
struct LookChain {
    std::vector<std::unique_ptr<MapLook>> looks;
    std::string next;        // the status after a one-shot clip ends ("" = stays)
    bool draw(Game& g, const MapRect& r, int tileX, int tileY, float statusMs) const;
    float onceSeconds() const;
    bool empty() const { return looks.empty(); }
};

class MapObject : public toms::Node {
    TOMS_OBJECT(MapObject)
public:
    enum class Touch { Pass, Stop };   // after touching it: look at the next object on the tile, or the step ends

    MapObject(Game& g, int entityIndex);
    ~MapObject() override = default;

    Entity* entity();                  // null for the player
    const Entity* entity() const;
    int entityIndex() const { return index_; }

    // "gone" once the entity is used up (defeated, collected, opened) unless a status is still playing out.
    std::string status() const;
    void setStatus(const std::string& s);   // from the start of its look
    bool play(const std::string& s);        // setStatus when that status has something to draw (a clip with art)
    float statusMs() const { return statusMs_; }
    const LookChain* look(const std::string& status) const;
    void setLook(const std::string& status, LookChain chain);
    std::vector<std::string> lookStatuses() const;
    const std::vector<std::string>& events() const;   // its linked events (the stage editor's "events")

    // ---- the frame ----
    virtual void update(int dtMs);
    virtual bool sorted() const { return true; }        // drawn in the floor's row-sorted pass
    virtual MapRect rect(const MapView& v) const;
    virtual int sortKey() const;                        // the bottom row it covers
    virtual int tieX() const;
    virtual void render(const MapView& v);              // its status's look, in the sorted pass
    virtual void renderUnderlay(const MapView& v) { (void)v; }   // before the sorted pass (a door opening)

    // ---- what the player does to it ----
    bool covers(int x, int y) const;
    virtual bool bumpBeforeStep(int x, int y) { (void)x; (void)y; return false; }   // true: the step ends (a gate)
    virtual bool bumpBig(int x, int y) { (void)x; (void)y; return false; }          // true: fought from beside it
    virtual Touch onTouch(int x, int y);                // the player stepped onto it
    virtual bool onInteract() { return false; }         // Enter on its tile

    // For the hierarchy / details view: what it is, in a few fields.
    virtual nlohmann::json details() const;

protected:
    Game& g_;
    int index_;
    std::string status_ = "idle";
    float statusMs_ = 0;
    std::map<std::string, LookChain> looks_;
};

class MonsterObject : public MapObject {
    TOMS_OBJECT(MonsterObject)
public:
    using MapObject::MapObject;
    bool bumpBig(int x, int y) override;
    Touch onTouch(int x, int y) override;
    nlohmann::json details() const override;
};

class ItemObject : public MapObject {
    TOMS_OBJECT(ItemObject)
public:
    using MapObject::MapObject;
    Touch onTouch(int x, int y) override;
};

class EventObject : public MapObject {
    TOMS_OBJECT(EventObject)
public:
    using MapObject::MapObject;
    Touch onTouch(int x, int y) override;
};

class NpcObject : public MapObject {
    TOMS_OBJECT(NpcObject)
public:
    using MapObject::MapObject;
    Touch onTouch(int x, int y) override;
    bool onInteract() override;
};

class GateObject : public MapObject {
    TOMS_OBJECT(GateObject)
public:
    using MapObject::MapObject;
    MapRect rect(const MapView& v) const override;      // the full tile, like a door
    bool bumpBeforeStep(int x, int y) override;
    void renderUnderlay(const MapView& v) override;
};

class DoorObject : public MapObject {
    TOMS_OBJECT(DoorObject)
public:
    using MapObject::MapObject;
    bool sorted() const override { return false; }      // the tile layer draws a closed door
    MapRect rect(const MapView& v) const override;
    void renderUnderlay(const MapView& v) override;
};

class StairsObject : public MapObject {
    TOMS_OBJECT(StairsObject)
public:
    using MapObject::MapObject;
    bool sorted() const override { return false; }      // a tile
};

class PlayerObject : public MapObject {
    TOMS_OBJECT(PlayerObject)
public:
    explicit PlayerObject(Game& g) : MapObject(g, -1) {}
    MapRect rect(const MapView& v) const override;
    int sortKey() const override;
    int tieX() const override;
    nlohmann::json details() const override;
};

// The floor's objects, in the stage file's order, under one root node.
class MapFloor {
public:
    void build(Game& g);               // after Game::loadStage has the entities and their statuses
    void clear();
    void update(int dtMs);
    void render(const MapView& v);     // the underlays, then every sorted object (and the player) by row
    // The object of an entity whose kind starts with `kindPrefix` at (x, y), or null.
    MapObject* objectAt(int x, int y, const char* kindPrefix);
    const std::vector<std::shared_ptr<MapObject>>& objects() const { return objects_; }
    PlayerObject* player() const { return player_.get(); }
    toms::Node& root() { return root_; }
    int generation() const { return generation_; }   // +1 per build: a loop over objects stops if the floor changed

private:
    toms::Node root_;                  // declared first: the objects unparent from it when they go
    std::vector<std::shared_ptr<MapObject>> objects_;
    std::shared_ptr<PlayerObject> player_;
    std::vector<MapObject*> drawList_; // scratch
    int generation_ = 0;
};

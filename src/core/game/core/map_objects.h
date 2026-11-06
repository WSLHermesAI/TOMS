// map_objects.h -- the things on a floor, each its own class (docs/engine/12_RESOURCE_MANAGER.md §4.10).
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
class RenderLook;
struct Builder;
}  // namespace map_detail

// Where the floor is on screen this frame (Game::draw's camera).
struct MapView {
    float m_fOx = 0, m_fOy = 0;    // pixel of tile (0, 0)
    float m_fTs = 0;            // tile size
    float m_fInset = 0;         // objects are inset this much into their tiles
    float m_fSSize = 0;         // the player's sprite size (ts - 2 * inset)
};

struct MapRect {
    float m_fX = 0, m_fY = 0, m_fW = 0, m_fH = 0;
};

// One way to draw a status, in the object's rect. False: nothing drawn (the next look of the chain is tried).
class MapLook {
public:
    virtual ~MapLook() = default;
    // v: the camera (v.ox, v.oy); r: the object's rect on screen.
    virtual bool f_Draw(Game& l_oG, const MapView& l_oV, const MapRect& l_oR, int l_iTileX, int l_iTileY, float l_fStatusMs) const = 0;
    virtual float f_OnceSeconds() const { return 0; }   // a one-shot clip: its length (0 = holds)
    virtual void f_Update(float l_fDt) { (void)l_fDt; }       // seconds: what runs on its own (an effect) moves on
};

// The looks of one status, tried in order.
struct LookChain {
    std::vector<std::unique_ptr<MapLook>> m_vLooks;
    std::string m_strNext;        // the status after a one-shot clip ends ("" = stays)
    bool f_Draw(Game& l_oG, const MapView& l_oV, const MapRect& l_oR, int l_iTileX, int l_iTileY, float l_fStatusMs) const;
    float f_OnceSeconds() const;
    void f_Update(float l_fDt) const { for (const auto& l_upL : m_vLooks) l_upL->f_Update(l_fDt); }
    bool f_Empty() const { return m_vLooks.empty(); }
};

class MapObject : public toms::Node {
    TOMS_OBJECT(MapObject)
public:
    enum class Touch { Pass, Stop };   // after touching it: look at the next object on the tile, or the step ends

    MapObject(Game& l_oG, int l_iEntityIndex);
    ~MapObject() override = default;

    Entity* f_Entity();                  // null for the player
    const Entity* f_Entity() const;
    int f_EntityIndex() const { return m_iIndex; }

    // "gone" once the entity is used up (defeated, collected, opened) unless a status is still playing out.
    std::string f_Status() const;
    void f_SetStatus(const std::string& l_strS);   // from the start of its look
    bool f_Play(const std::string& l_strS);        // setStatus when that status has something to draw (a clip with art)
    float f_StatusMs() const { return m_fStatusMs; }
    const LookChain* f_Look(const std::string& l_strStatus) const;
    void f_SetLook(const std::string& l_strStatus, LookChain l_oChain);
    std::vector<std::string> f_LookStatuses() const;
    const std::vector<std::string>& f_Events() const;   // its linked events (the stage editor's "events")

    // ---- the frame ----
    virtual void f_Update(int l_iDtMs);
    virtual bool f_Sorted() const { return true; }        // drawn in the floor's row-sorted pass
    virtual MapRect f_Rect(const MapView& l_oV) const;
    virtual int f_SortKey() const;                        // the bottom row it covers
    virtual int f_TieX() const;
    virtual void f_Render(const MapView& l_oV);              // its status's look, in the sorted pass
    virtual void f_RenderUnderlay(const MapView& l_oV) { (void)l_oV; }   // before the sorted pass (a door opening)

    // ---- what the player does to it ----
    bool f_Covers(int l_iX, int l_iY) const;
    virtual bool f_BumpBeforeStep(int l_iX, int l_iY) { (void)l_iX; (void)l_iY; return false; }   // true: the step ends (a gate)
    virtual bool f_BumpBig(int l_iX, int l_iY) { (void)l_iX; (void)l_iY; return false; }          // true: fought from beside it
    virtual Touch f_OnTouch(int l_iX, int l_iY);                // the player stepped onto it
    virtual bool f_OnInteract() { return false; }         // Enter on its tile

    // For the hierarchy / details view: what it is, in a few fields.
    virtual nlohmann::json f_Details() const;

protected:
    Game& m_oG;
    int m_iIndex;
    std::string m_strStatus = "idle";
    float m_fStatusMs = 0;
    std::map<std::string, LookChain> m_mapLooks;
};

class MonsterObject : public MapObject {
    TOMS_OBJECT(MonsterObject)
public:
    using MapObject::MapObject;
    void f_Render(const MapView& l_oV) override;   // its look, and a hint when others fight beside it
    bool f_BumpBig(int l_iX, int l_iY) override;
    Touch f_OnTouch(int l_iX, int l_iY) override;
    nlohmann::json f_Details() const override;
private:
    LookChain m_oPartyHint;          // made once, when first needed
    bool m_bPartyHintMade = false;
};

class ItemObject : public MapObject {
    TOMS_OBJECT(ItemObject)
public:
    using MapObject::MapObject;
    Touch f_OnTouch(int l_iX, int l_iY) override;
};

class EventObject : public MapObject {
    TOMS_OBJECT(EventObject)
public:
    using MapObject::MapObject;
    Touch f_OnTouch(int l_iX, int l_iY) override;
};

class NpcObject : public MapObject {
    TOMS_OBJECT(NpcObject)
public:
    using MapObject::MapObject;
    Touch f_OnTouch(int l_iX, int l_iY) override;
    bool f_OnInteract() override;
};

class GateObject : public MapObject {
    TOMS_OBJECT(GateObject)
public:
    using MapObject::MapObject;
    MapRect f_Rect(const MapView& l_oV) const override;      // the full tile, like a door
    bool f_BumpBeforeStep(int l_iX, int l_iY) override;
    void f_RenderUnderlay(const MapView& l_oV) override;
};

class DoorObject : public MapObject {
    TOMS_OBJECT(DoorObject)
public:
    using MapObject::MapObject;
    bool f_Sorted() const override { return false; }      // the tile layer draws a closed door
    MapRect f_Rect(const MapView& l_oV) const override;
    void f_RenderUnderlay(const MapView& l_oV) override;
};

class StairsObject : public MapObject {
    TOMS_OBJECT(StairsObject)
public:
    using MapObject::MapObject;
    bool f_Sorted() const override { return false; }      // a tile
};

class PlayerObject : public MapObject {
    TOMS_OBJECT(PlayerObject)
public:
    explicit PlayerObject(Game& l_oG) : MapObject(l_oG, -1) {}
    MapRect f_Rect(const MapView& l_oV) const override;
    int f_SortKey() const override;
    int f_TieX() const override;
    nlohmann::json f_Details() const override;
};

// The floor's objects, in the stage file's order, under one root node.
class MapFloor {
public:
    void f_Build(Game& l_oG);               // after Game::loadStage has the entities and their statuses
    void f_Clear();
    void f_Update(int l_iDtMs);
    void f_Render(const MapView& l_oV);     // the underlays, then every sorted object (and the player) by row
    // The object of an entity whose kind starts with `kindPrefix` at (x, y), or null.
    MapObject* f_ObjectAt(int l_iX, int l_iY, const char* l_pszKindPrefix);
    const std::vector<std::shared_ptr<MapObject>>& f_Objects() const { return m_vObjects; }
    PlayerObject* f_Player() const { return m_spPlayer.get(); }
    toms::Node& f_Root() { return m_oRoot; }
    int f_Generation() const { return m_iGeneration; }   // +1 per build: a loop over objects stops if the floor changed

private:
    toms::Node m_oRoot;                  // declared first: the objects unparent from it when they go
    std::vector<std::shared_ptr<MapObject>> m_vObjects;
    std::shared_ptr<PlayerObject> m_spPlayer;
    std::vector<MapObject*> m_vDrawList; // scratch
    int m_iGeneration = 0;
};

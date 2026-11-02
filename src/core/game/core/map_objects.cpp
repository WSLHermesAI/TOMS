// map_objects.cpp -- see map_objects.h. The rules here moved from Game::movePlayer / interact / openDoor / bumpGate
// and the drawing from Game::draw's object pass, unchanged; they now live with the kind of object they belong to.
#include "map_objects.h"

#include "game_internal.h"

#include <set>

using namespace toms::game_detail;

namespace map_detail {
const float g_arrWhite[4] = {1, 1, 1, 1};

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
    explicit RenderLook(Clock l_eClock = Clock::Idle) : m_eClock(l_eClock) {}

    void f_Add(std::shared_ptr<toms::RenderObject> l_spO, float l_fSizeW = 32, float l_fSizeH = 32, float l_fInset = 0) {
        if (!l_spO) return;
        Part l_oP;
        l_oP.m_pSprite = dynamic_cast<toms::SpriteObject*>(l_spO.get());
        l_oP.m_pClip = dynamic_cast<toms::ClipObject*>(l_spO.get());
        l_oP.m_spObj = std::move(l_spO);
        l_oP.m_fSw = l_fSizeW; l_oP.m_fSh = l_fSizeH; l_oP.m_fInset = l_fInset;
        m_vParts.push_back(std::move(l_oP));
    }
    bool f_Empty() const { return m_vParts.empty(); }

    // A render object for a description, cloned from one made before (many slimes share one); null when it cannot be made
    // (the reason is logged once per description).
    static std::shared_ptr<toms::RenderObject> f_RenderObject(Game& l_oG, const nlohmann::json& l_jJ) {
        if (!l_oG.m_upRes) return nullptr;
        std::string l_strErr;
        std::shared_ptr<toms::RenderObject> l_spO = l_oG.m_oRenderFactory.f_Instance(l_jJ, *l_oG.m_upRes, &l_strErr);
        if (!l_spO) {
            static std::set<std::string> l_setTold;
            if (l_setTold.insert(l_jJ.dump()).second) std::fprintf(stderr, "[map] look %s: %s\n", l_jJ.dump().c_str(), l_strErr.c_str());
        }
        return l_spO;
    }

    // A sprite of the game's atlas (the art style's), stretched over the rect; a sprite the atlas lacks draws as the floor,
    // as before. solid: one flat colour in its shape.
    static std::shared_ptr<toms::RenderObject> f_SpriteObject(Game& l_oG, std::string l_strName, const float l_arrTint[4], bool l_bSolid = false) {
        if (l_strName.size() > 4 && l_strName.compare(l_strName.size() - 4, 4, ".png") == 0) l_strName.resize(l_strName.size() - 4);
        if (!l_oG.m_mapIdToLayer.count(l_strName)) l_strName = "floor";
        nlohmann::json l_jJ = {{"type", "sprite"}, {"name", l_strName}, {"tint", {l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]}}};
        if (l_bSolid) l_jJ["solid"] = true;
        return f_RenderObject(l_oG, l_jJ);
    }

    static std::shared_ptr<toms::RenderObject> f_ClipObject(Game& l_oG, const std::string& l_strFile, const std::string& l_strClip, const float l_arrTint[4]) {
        return f_RenderObject(l_oG, {{"type", "clip"}, {"anim", l_strFile}, {"clip", l_strClip}, {"tint", {l_arrTint[0], l_arrTint[1], l_arrTint[2], l_arrTint[3]}}});
    }

    bool f_Draw(Game& l_oG, const MapView& l_oV, const MapRect& l_oR, int l_iTileX, int l_iTileY, float l_fStatusMs) const override {
        // the first part decides whether there is anything to draw (a clip without its art: the next look is tried)
        if (m_vParts.empty() || !m_vParts.front().m_spObj->f_Ok()) return false;
        if (m_vParts.front().m_pClip && l_oG.m_oSpriteAtlas.m_vPages.empty()) return false;
        // placed on the map (the rect without the camera), seen through the camera: an effect stays where it was born
        const glm::mat3 l_mat3Camera = toms::anim::f_Placement(l_oV.m_fOx, l_oV.m_fOy);
        for (const Part& l_oP : m_vParts) {
            const MapRect l_oQ{l_oR.m_fX - l_oV.m_fOx + l_oP.m_fInset * l_oR.m_fW, l_oR.m_fY - l_oV.m_fOy + l_oP.m_fInset * l_oR.m_fH, l_oR.m_fW * (1 - 2 * l_oP.m_fInset), l_oR.m_fH * (1 - 2 * l_oP.m_fInset)};
            l_oP.m_spObj->f_SetView(l_mat3Camera);
            if (l_oP.m_pSprite) {
                const toms::AtlasRegion* l_pA = l_oP.m_pSprite->f_Atlas()->f_Find(l_oP.m_pSprite->f_SpriteName());
                if (!l_pA || l_pA->m_iOrigW <= 0 || l_pA->m_iOrigH <= 0) continue;
                l_oP.m_pSprite->f_SetPos(l_oQ.m_fX + l_pA->m_arrPivot[0] * l_oQ.m_fW, l_oQ.m_fY + l_pA->m_arrPivot[1] * l_oQ.m_fH);
                l_oP.m_pSprite->f_SetScale(l_oQ.m_fW / (float)l_pA->m_iOrigW, l_oQ.m_fH / (float)l_pA->m_iOrigH);
            } else {
                l_oP.m_spObj->f_SetPos(l_oQ.m_fX + l_oQ.m_fW * 0.5f, l_oQ.m_fY + l_oQ.m_fH);
                l_oP.m_spObj->f_SetScale(l_oQ.m_fW / l_oP.m_fSw, l_oQ.m_fH / l_oP.m_fSh);
            }
            if (l_oP.m_pClip) {
                const float l_fLen = l_oP.m_pClip->f_Duration();
                float l_fT;
                if (m_eClock == Clock::Idle) {
                    const double l_dPhase = (double)(((unsigned)l_iTileX * 73856093u ^ (unsigned)l_iTileY * 19349663u) % 997u) / 997.0 * l_fLen;
                    l_fT = l_fLen > 0 ? (float)std::fmod(l_oG.m_dIdleClockMs / 1000.0 + l_dPhase, (double)l_fLen) : 0.0f;
                } else {
                    l_fT = std::min(l_fStatusMs / 1000.0f, l_fLen);
                }
                l_oP.m_pClip->f_SetTime(l_fT);
            }
            l_oP.m_spObj->f_RenderTree(l_oG.m_pRen);
        }
        return true;
    }
    float f_OnceSeconds() const override {
        const toms::ClipObject* l_pC = m_eClock == Clock::Status && !m_vParts.empty() ? m_vParts.front().m_pClip : nullptr;
        return l_pC ? l_pC->f_Duration() : 0.0f;
    }
    void f_Update(float l_fDt) override {
        for (const Part& l_oP : m_vParts)
            if (!l_oP.m_pClip) l_oP.m_spObj->f_Update(l_fDt);   // effects and groups; a top clip follows the floor's clock instead
    }

private:
    struct Part {
        std::shared_ptr<toms::RenderObject> m_spObj;
        toms::SpriteObject* m_pSprite = nullptr;   // obj, when it is one (placed by its frame)
        toms::ClipObject* m_pClip = nullptr;       // obj, when it is one (set to the floor's clock)
        float m_fSw = 32, m_fSh = 32, m_fInset = 0;
    };
    Clock m_eClock;
    std::vector<Part> m_vParts;
};

bool f_StartsWith(const std::string& l_strS, const char* l_pszP) { return l_strS.rfind(l_pszP, 0) == 0; }

void f_ReadTint(const nlohmann::json& l_jJ, float l_arrOut[4]) {
    if (l_jJ.is_object() && l_jJ.contains("tint") && l_jJ["tint"].is_array())
        for (int l_iI = 0; l_iI < 4 && l_iI < (int)l_jJ["tint"].size(); l_iI++)
            if (l_jJ["tint"][l_iI].is_number()) l_arrOut[l_iI] = l_jJ["tint"][l_iI].get<float>();
}

}  // namespace map_detail

using namespace map_detail;

// ---- LookChain -----------------------------------------------------------------------------------------------------

bool LookChain::f_Draw(Game& l_oG, const MapView& l_oV, const MapRect& l_oR, int l_iTileX, int l_iTileY, float l_fStatusMs) const {
    for (const auto& l_upL : m_vLooks)
        if (l_upL->f_Draw(l_oG, l_oV, l_oR, l_iTileX, l_iTileY, l_fStatusMs)) return true;
    return false;
}

float LookChain::f_OnceSeconds() const { return m_vLooks.empty() ? 0.0f : m_vLooks.front()->f_OnceSeconds(); }

// ---- MapObject -----------------------------------------------------------------------------------------------------

MapObject::MapObject(Game& l_oG, int l_iEntityIndex) : m_oG(l_oG), m_iIndex(l_iEntityIndex) {}

Entity* MapObject::f_Entity() { return m_iIndex >= 0 && m_iIndex < (int)m_oG.m_oSt.m_vEntities.size() ? &m_oG.m_oSt.m_vEntities[(size_t)m_iIndex] : nullptr; }
const Entity* MapObject::f_Entity() const {
    return m_iIndex >= 0 && m_iIndex < (int)m_oG.m_oSt.m_vEntities.size() ? &m_oG.m_oSt.m_vEntities[(size_t)m_iIndex] : nullptr;
}

std::string MapObject::f_Status() const {
    const Entity* l_pE = f_Entity();
    if (m_strStatus == "idle" && l_pE && l_pE->m_bConsumed) return "gone";
    return m_strStatus;
}

void MapObject::f_SetStatus(const std::string& l_strS) {
    m_strStatus = l_strS;
    m_fStatusMs = 0;
}

bool MapObject::f_Play(const std::string& l_strS) {
    const LookChain* l_pC = f_Look(l_strS);
    if (!l_pC || l_pC->f_Empty()) return false;
    f_SetStatus(l_strS);
    return true;
}

const LookChain* MapObject::f_Look(const std::string& l_strStatus) const {
    auto l_itIt = m_mapLooks.find(l_strStatus);
    return l_itIt == m_mapLooks.end() ? nullptr : &l_itIt->second;
}

void MapObject::f_SetLook(const std::string& l_strStatus, LookChain l_oChain) { m_mapLooks[l_strStatus] = std::move(l_oChain); }

std::vector<std::string> MapObject::f_LookStatuses() const {
    std::vector<std::string> l_vOut;
    for (const auto& [l_strS, l_oC] : m_mapLooks) l_vOut.push_back(l_strS);
    return l_vOut;
}

const std::vector<std::string>& MapObject::f_Events() const {
    static const std::vector<std::string> l_vNone;
    const Entity* l_pE = f_Entity();
    return l_pE ? l_pE->m_vLinks : l_vNone;
}

// A one-shot clip moves to its next status when it has played out (a door: opening -> gone).
void MapObject::f_Update(int l_iDtMs) {
    m_fStatusMs += (float)l_iDtMs;
    if (const LookChain* l_pC = f_Look(m_strStatus)) {
        l_pC->f_Update(l_iDtMs / 1000.0f);   // effects in its look move on
        const float l_fOnce = l_pC->f_OnceSeconds();
        if (l_fOnce > 0 && m_fStatusMs / 1000.0f > l_fOnce) f_SetStatus(l_pC->m_strNext.empty() ? "gone" : l_pC->m_strNext);
    }
}

MapRect MapObject::f_Rect(const MapView& l_oV) const {
    const Entity* l_pE = f_Entity();
    MapRect l_oR;
    if (!l_pE) return l_oR;
    l_oR.m_fW = l_pE->m_oFp.m_iW * l_oV.m_fTs - 2.0f * l_oV.m_fInset;
    l_oR.m_fH = l_pE->m_oFp.m_iH * l_oV.m_fTs - 2.0f * l_oV.m_fInset;
    l_oR.m_fX = l_oV.m_fOx + l_pE->m_iX * l_oV.m_fTs + l_oV.m_fInset;
    l_oR.m_fY = l_oV.m_fOy + l_pE->m_iY * l_oV.m_fTs + l_oV.m_fInset;
    return l_oR;
}

int MapObject::f_SortKey() const {
    const Entity* l_pE = f_Entity();
    return l_pE ? m_oG.f_FootprintSortKeyT(*l_pE) : 0;
}

int MapObject::f_TieX() const {
    const Entity* l_pE = f_Entity();
    return l_pE ? l_pE->m_iX : 0;
}

void MapObject::f_Render(const MapView& l_oV) {
    const std::string l_strS = f_Status();
    if (l_strS == "gone") return;
    const LookChain* l_pC = f_Look(l_strS);
    if (!l_pC) l_pC = f_Look("idle");   // a status with no look of its own looks idle
    const Entity* l_pE = f_Entity();
    if (l_pC) l_pC->f_Draw(m_oG, l_oV, f_Rect(l_oV), l_pE ? l_pE->m_iX : 0, l_pE ? l_pE->m_iY : 0, m_fStatusMs);
}

bool MapObject::f_Covers(int l_iX, int l_iY) const {
    const Entity* l_pE = f_Entity();
    return l_pE && m_oG.f_EntityCovers(*l_pE, l_iX, l_iY);
}

// Anything else on the tile (a stair, an unknown kind): only the stairs have something to say.
MapObject::Touch MapObject::f_OnTouch(int l_iX, int l_iY) { return m_oG.f_StepOntoStairs(l_iX, l_iY) ? Touch::Stop : Touch::Pass; }

nlohmann::json MapObject::f_Details() const {
    nlohmann::json l_jJ = nlohmann::json::object();
    l_jJ["class"] = f_Type();
    l_jJ["status"] = f_Status();
    if (const Entity* l_pE = f_Entity()) {
        l_jJ["kind"] = l_pE->m_strKind;
        l_jJ["id"] = l_pE->m_strId;
        l_jJ["cell"] = {l_pE->m_iX, l_pE->m_iY};
        if (l_pE->m_oFp.m_iW != 1 || l_pE->m_oFp.m_iH != 1) l_jJ["footprint"] = {l_pE->m_oFp.m_iW, l_pE->m_oFp.m_iH};
        if (l_pE->m_bRoamer) l_jJ["roamer"] = true;
        if (!l_pE->m_vLinks.empty()) l_jJ["events"] = l_pE->m_vLinks;
        if (l_pE->m_jLook.is_object()) l_jJ["ownLook"] = l_pE->m_jLook;
    }
    l_jJ["looks"] = f_LookStatuses();
    return l_jJ;
}

// ---- the kinds -----------------------------------------------------------------------------------------------------

// S1: a multi-grid monster is not walked into: entering a 4-grid boss's cell would put the player inside it, so big
// footprints are fought from the adjacent tile (doc F4: the player can only circle it).
bool MonsterObject::f_BumpBig(int l_iX, int l_iY) {
    const Entity* l_pE = f_Entity();
    if (!l_pE || l_pE->m_bConsumed || !l_pE->m_oFp.f_Big() || !f_Covers(l_iX, l_iY)) return false;
    m_oG.f_AdvanceRoamers();
    m_oG.f_EngageMonster(*f_Entity());
    return true;
}

MapObject::Touch MonsterObject::f_OnTouch(int, int) {
    m_oG.f_EngageMonster(*f_Entity());
    return Touch::Stop;
}

nlohmann::json MonsterObject::f_Details() const {
    nlohmann::json l_jJ = MapObject::f_Details();
    if (const Entity* l_pE = f_Entity()) {
        auto l_itT = m_oG.m_mapEnemyTpl.find(l_pE->m_strId);
        if (l_itT != m_oG.m_mapEnemyTpl.end())
            for (const char* l_pszK : {"hp", "atk", "def", "exp", "gold"})
                if (l_itT->second.contains(l_pszK)) l_jJ[l_pszK] = l_itT->second[l_pszK];
    }
    return l_jJ;
}

// Keys and coins apply at once; usable items (gems, potions, scrolls) go into the inventory.
MapObject::Touch ItemObject::f_OnTouch(int, int) {
    Entity& l_oE = *f_Entity();
    m_oG.f_ReceiveItem(l_oE.m_strId);
    l_oE.m_bConsumed = true;
    m_oG.m_oSt.m_vTiles[(size_t)l_oE.m_iY][(size_t)l_oE.m_iX] = '.';
    // Milestone 7: the clear survives a reload of this floor (entityStatus_).
    toms::f_SetEntityStatus(m_oG.m_mapEntityStatus, m_oG.f_StatusKeyAt(l_oE.m_iX, l_oE.m_iY), toms::EntityStatus::Collected);
    toms::f_GlobalEventBus().f_Publish(toms::ItemCollected{l_oE.m_strId, m_oG.m_strCurStage});
    m_oG.f_FireLinkedEvents(l_oE);
    return Touch::Pass;
}

// S3.5 (d): a floor event. Its text comes from the i18n table; its effect from the event id's own vocabulary until
// the pools are loaded at runtime: shards feed the memory-shard state, traps cost HP, caches / relics give a little back.
MapObject::Touch EventObject::f_OnTouch(int, int) {
    Entity& l_oE = *f_Entity();
    Game& l_oG = m_oG;
    const std::string l_strEvId = l_oE.m_strId;
    // An event whose `requires` does not hold yet only becomes "started": it stays on its tile, and the event log
    // lists it as in progress (docs/gameplay/event_editor/06_EVENT_LOGIC.md section 7).
    auto l_itDef = l_oG.m_mapEventDefs.find(l_strEvId);
    if (l_itDef != l_oG.m_mapEventDefs.end() && !l_itDef->second.m_jRequires.is_null() &&
        !toms::f_Evaluate(l_itDef->second.m_jRequires, GameConditionContext(l_oG.m_oPl, l_oG.m_oMeta, l_oG.m_mapMissionTrackers, l_oG.m_oRun, l_oG.m_oEquipped))) {
        if (!l_oG.m_oRun.f_EventStarted(l_strEvId)) {
            l_oG.m_oRun.f_MarkEventStarted(l_strEvId);
            const std::string l_strTitle = l_oG.m_oLocale.f_Tr(l_itDef->second.m_strTitleKey);
            l_oG.m_vNotifications.push_back({l_strTitle == l_itDef->second.m_strTitleKey ? l_strEvId : l_strTitle, 3000});
            l_oG.f_MarkProgressDirty();
        }
        return Touch::Stop;
    }
    std::string l_strText = l_oG.m_oLocale.f_Tr(l_strEvId + ".text");
    if (l_strText.empty() || l_strText == l_strEvId + ".text") l_strText = l_strEvId;   // missing -> the id, not blank
    if (l_strEvId.find("shard") != std::string::npos) {
        l_oG.m_oRun.f_AddShard(l_strEvId);
        l_oG.m_vNotifications.push_back({l_strText + "  [memory shard " + std::to_string(l_oG.m_oRun.f_ShardCount()) + "]", 4200});
    } else if (l_strEvId.find("trap") != std::string::npos) {
        l_oG.m_oPl.m_iHp = std::max(1, l_oG.m_oPl.m_iHp - 8);
        l_oG.m_vNotifications.push_back({l_strText + "  [-8 HP]", 3800});
    } else if (l_strEvId.find("cache") != std::string::npos || l_strEvId.find("relic") != std::string::npos) {
        l_oG.m_oPl.m_iHp = std::min(l_oG.f_EffectiveMaxHp(), l_oG.m_oPl.m_iHp + 10);
        l_oG.m_oPl.m_iGold += 12;
        l_oG.m_vNotifications.push_back({l_strText + "  [+10 HP, +12 GOLD]", 3800});
    } else {
        l_oG.m_vNotifications.push_back({l_strText, 4200});   // whispers / rescues: remembered, no stat
    }
    // The record's `actions`, then finished the first time (06 section 6 item 2): the run flag, the event log, its
    // `next` events woken, and the stairs it opens say so (Q7).
    const int l_iGen = l_oG.m_oFloor.f_Generation();
    l_oG.f_CompleteEvent(l_strEvId);
    if (l_oG.m_oFloor.f_Generation() != l_iGen) return Touch::Stop;   // its actions took the player to another floor
    l_oG.f_FireLinkedEvents(*f_Entity());
    // A repeatable event ("once": false) stays on its tile until its last run (08 section 11.1).
    if (l_oG.f_CanRepeat(l_strEvId)) return Touch::Stop;
    Entity& l_oDone = *f_Entity();
    l_oDone.m_bConsumed = true;
    l_oG.m_oSt.m_vTiles[(size_t)l_oDone.m_iY][(size_t)l_oDone.m_iX] = '.';
    toms::f_SetEntityStatus(l_oG.m_mapEntityStatus, l_oG.f_StatusKeyAt(l_oDone.m_iX, l_oDone.m_iY), toms::EntityStatus::Collected);
    return Touch::Stop;
}

// NPCs and their events (docs/gameplay/event_editor/08_NPC_DIALOGUE.md): the linked events first (a linked talk event is then
// one of this talk's topics), then its NPC file decides between a chat line, a topic and the topic menu.
MapObject::Touch NpcObject::f_OnTouch(int, int) {
    m_oG.f_FireLinkedEvents(*f_Entity());
    m_oG.f_TalkTo(f_Entity()->m_strId);
    return Touch::Pass;
}

bool NpcObject::f_OnInteract() {
    const Entity* l_pE = f_Entity();
    if (!l_pE || l_pE->m_bConsumed || !f_Covers(m_oG.m_oPl.m_iX, m_oG.m_oPl.m_iY)) return false;
    m_oG.f_FireLinkedEvents(*l_pE);
    m_oG.f_TalkTo(l_pE->m_strId);
    return true;
}

MapRect GateObject::f_Rect(const MapView& l_oV) const {
    const Entity* l_pE = f_Entity();
    MapRect l_oR;
    if (l_pE) { l_oR.m_fX = l_oV.m_fOx + l_pE->m_iX * l_oV.m_fTs; l_oR.m_fY = l_oV.m_fOy + l_pE->m_iY * l_oV.m_fTs; l_oR.m_fW = l_oV.m_fTs; l_oR.m_fH = l_oV.m_fTs; }
    return l_oR;
}

// A gate opens only for its own key item; the player stays put this turn either way.
bool GateObject::f_BumpBeforeStep(int l_iX, int l_iY) {
    const Entity* l_pE = f_Entity();
    if (!l_pE || l_pE->m_bConsumed || l_pE->m_iX != l_iX || l_pE->m_iY != l_iY) return false;
    m_oG.f_BumpGate(*f_Entity());
    if (f_Entity()->m_bConsumed) f_Play("opening");   // the key went in: the door-opening clip plays on its tile
    return true;
}

void GateObject::f_RenderUnderlay(const MapView& l_oV) {
    if (m_strStatus != "opening") return;
    if (const LookChain* l_pC = f_Look("opening")) l_pC->f_Draw(m_oG, l_oV, f_Rect(l_oV), f_Entity()->m_iX, f_Entity()->m_iY, m_fStatusMs);
}

MapRect DoorObject::f_Rect(const MapView& l_oV) const {   // the full tile
    const Entity* l_pE = f_Entity();
    MapRect l_oR;
    if (l_pE) { l_oR.m_fX = l_oV.m_fOx + l_pE->m_iX * l_oV.m_fTs; l_oR.m_fY = l_oV.m_fOy + l_pE->m_iY * l_oV.m_fTs; l_oR.m_fW = l_oV.m_fTs; l_oR.m_fH = l_oV.m_fTs; }
    return l_oR;
}

void DoorObject::f_RenderUnderlay(const MapView& l_oV) {
    if (m_strStatus != "opening") return;
    if (const LookChain* l_pC = f_Look("opening")) l_pC->f_Draw(m_oG, l_oV, f_Rect(l_oV), f_Entity()->m_iX, f_Entity()->m_iY, m_fStatusMs);
}

MapRect PlayerObject::f_Rect(const MapView& l_oV) const {
    MapRect l_oR;
    l_oR.m_fW = l_oV.m_fSSize;
    l_oR.m_fH = l_oV.m_fSSize;
    l_oR.m_fX = l_oV.m_fOx + m_oG.m_oPl.m_iX * l_oV.m_fTs + l_oV.m_fInset;
    l_oR.m_fY = l_oV.m_fOy + m_oG.m_oPl.m_iY * l_oV.m_fTs + l_oV.m_fInset;
    return l_oR;
}

int PlayerObject::f_SortKey() const { return m_oG.f_FootprintSortKeyT(m_oG.m_oPl.m_iX, m_oG.m_oPl.m_iY, toms::Footprint{}); }
int PlayerObject::f_TieX() const { return m_oG.m_oPl.m_iX; }

nlohmann::json PlayerObject::f_Details() const {
    nlohmann::json l_jJ = MapObject::f_Details();
    l_jJ["cell"] = {m_oG.m_oPl.m_iX, m_oG.m_oPl.m_iY};
    l_jJ["hp"] = m_oG.m_oPl.m_iHp;
    l_jJ["atk"] = m_oG.m_oPl.m_iAtk;
    l_jJ["def"] = m_oG.m_oPl.m_iDef;
    l_jJ["gold"] = m_oG.m_oPl.m_iGold;
    return l_jJ;
}

// ---- building a floor's objects ------------------------------------------------------------------------------------

// Makes a floor's objects and their looks (a friend of Game: it reads the type tables and the loaded art).
struct map_detail::Builder {
// The original size of a game-atlas sprite (what a clip made around it is scaled by); 32 x 32 when unknown.
static void f_SpriteSize(const Game& l_oG, const std::string& l_strSprite, float& l_fW, float& l_fH) {
    const toms::AtlasRegion* l_pR = l_oG.m_oSpriteAtlas.f_Find(l_strSprite);
    l_fW = l_pR && l_pR->m_iOrigW > 0 ? (float)l_pR->m_iOrigW : 32.0f;
    l_fH = l_pR && l_pR->m_iOrigH > 0 ? (float)l_pR->m_iOrigH : 32.0f;
}

static std::shared_ptr<MapObject> f_MakeObject(Game& l_oG, int l_iIndex, const Entity& l_oE) {
    if (f_StartsWith(l_oE.m_strKind, "monster:")) return std::make_shared<MonsterObject>(l_oG, l_iIndex);
    if (f_StartsWith(l_oE.m_strKind, "item:")) return std::make_shared<ItemObject>(l_oG, l_iIndex);
    if (f_StartsWith(l_oE.m_strKind, "event:")) return std::make_shared<EventObject>(l_oG, l_iIndex);
    if (f_StartsWith(l_oE.m_strKind, "npc:")) return std::make_shared<NpcObject>(l_oG, l_iIndex);
    if (f_StartsWith(l_oE.m_strKind, "gate:")) return std::make_shared<GateObject>(l_oG, l_iIndex);
    if (f_StartsWith(l_oE.m_strKind, "door:")) return std::make_shared<DoorObject>(l_oG, l_iIndex);
    if (l_oE.m_strKind == "stairs_up" || l_oE.m_strKind == "stairs_down" || l_oE.m_strKind == "player_start") return std::make_shared<StairsObject>(l_oG, l_iIndex);
    return std::make_shared<MapObject>(l_oG, l_iIndex);
}

// A look from the data's "looks" table: any render object (render_object.h): {"type": "sprite", "name": ...}, "clip",
// "particle", "text" or "group" -- plus "next" (the status after a one-shot clip), "loop" (a clip: on the idle clock,
// else once from the start of the status) and "size" ([w, h]: the art pixels that fill the rect, default 32 x 32).
// A mistake draws the red "missing" box, so it is seen. Empty = nothing drawn.
static LookChain f_LookFromJson(Game& l_oG, const nlohmann::json& l_jD) {
    LookChain l_oC;
    if (!l_jD.is_object() || !l_oG.m_upRes) return l_oC;
    l_oC.m_strNext = l_jD.value("next", std::string());
    float l_arrTint[4] = {1, 1, 1, 1};
    f_ReadTint(l_jD, l_arrTint);
    const std::string l_strType = l_jD.value("type", std::string());
    auto l_upLook = std::make_unique<RenderLook>(l_strType == "clip" && !l_jD.value("loop", false) ? RenderLook::Clock::Status
                                                                                     : RenderLook::Clock::Idle);
    float l_fSw = 32, l_fSh = 32;
    if (l_jD.contains("size") && l_jD["size"].is_array() && l_jD["size"].size() == 2) { l_fSw = l_jD["size"][0].get<float>(); l_fSh = l_jD["size"][1].get<float>(); }
    if (l_strType == "sprite") {
        l_upLook->f_Add(RenderLook::f_SpriteObject(l_oG, l_jD.value("name", std::string()), l_arrTint));
    } else {
        nlohmann::json l_jJ = l_jD;
        l_jJ.erase("next"); l_jJ.erase("size");
        if (l_strType == "clip") l_jJ.erase("loop");   // the floor's clock plays it (RenderLook)
        std::string l_strErr;
        l_upLook->f_Add(l_oG.m_oRenderFactory.f_MakeOrMissing(l_jJ, *l_oG.m_upRes, &l_strErr), l_fSw, l_fSh);
        if (!l_strErr.empty()) std::fprintf(stderr, "[map] look %s: %s\n", l_jD.dump().c_str(), l_strErr.c_str());
    }
    if (!l_upLook->f_Empty()) l_oC.m_vLooks.push_back(std::move(l_upLook));
    return l_oC;
}

// The idle look the old fields give (what Game::draw computed every frame): the object's own clip, then the
// monster's idle clip, then its sprite -- or an event's story plate.
static LookChain f_LegacyIdleLook(Game& l_oG, const Entity& l_oE) {
    LookChain l_oC;
    const bool l_bNpc = f_StartsWith(l_oE.m_strKind, "npc:");
    std::string l_strSprite = l_bNpc ? l_oG.f_NpcSprite(l_oE.m_strId) : f_EntSprite(l_oE.m_strId);
    if (f_StartsWith(l_oE.m_strKind, "event:")) {   // an event's story plate: a gold tile with a darker inset (S3.5 d)
        static const float l_arrGold[4] = {0.86f, 0.72f, 0.36f, 0.92f};
        static const float l_arrInner[4] = {0.28f, 0.34f, 0.46f, 0.95f};
        const int l_iLayer = l_oG.f_SpriteLayer(l_strSprite);
        auto l_upPlate = std::make_unique<RenderLook>();
        l_upPlate->f_Add(RenderLook::f_SpriteObject(l_oG, l_strSprite, l_arrGold));
        if (l_iLayer + 1 < (int)l_oG.m_vSpriteIds.size()) l_upPlate->f_Add(RenderLook::f_SpriteObject(l_oG, l_oG.m_vSpriteIds[(size_t)l_iLayer + 1], l_arrInner), 32, 32, 0.22f);
        l_oC.m_vLooks.push_back(std::move(l_upPlate));
        return l_oC;
    }
    const nlohmann::json l_jLook = l_oG.f_LookOf(l_oE);
    bool l_bOwnSprite = false;
    if (l_jLook.contains("sprite") && l_jLook["sprite"].is_string()) {
        std::string l_strS = l_jLook["sprite"].get<std::string>();
        if (l_strS.size() > 4 && l_strS.compare(l_strS.size() - 4, 4, ".png") == 0) l_strS.resize(l_strS.size() - 4);
        // only a placed object's own sprite replaces the idle clip (a type's sprite is what its clip is made of)
        if (l_oG.m_oSpriteAtlas.f_Find(l_strS)) { l_strSprite = l_strS; l_bOwnSprite = l_oE.m_jLook.is_object() && l_oE.m_jLook.contains("sprite"); }
    }
    float l_arrTint[4] = {1, 1, 1, 1};
    f_ReadTint(l_jLook, l_arrTint);
    float l_arrWash[4] = {0, 0, 0, 0};
    if (f_StartsWith(l_oE.m_strKind, "gate:")) {
        // a gate is a full-tile door with its key item's "tint" (data/items.json) washed over it, so it reads as no
        // colour of key door (yellow / blue / red)
        const nlohmann::json* l_pD = l_oG.f_ItemDef(l_oE.m_strId);
        const std::string l_strDoor = l_pD && l_pD->contains("door") && (*l_pD)["door"].is_string() ? (*l_pD)["door"].get<std::string>() : std::string();
        if (!l_strDoor.empty() && l_oG.m_mapIdToLayer.count(l_strDoor)) {   // its own door picture (docs/engine/14_AI_SPRITES.md)
            l_strSprite = l_strDoor;
        } else {
            l_strSprite = "door_yellow";
            l_arrWash[0] = 0.6f; l_arrWash[1] = 0.3f; l_arrWash[2] = 0.9f; l_arrWash[3] = 0.6f;
            if (l_pD && l_pD->contains("tint") && (*l_pD)["tint"].is_array())
                for (int l_iI = 0; l_iI < 3 && l_iI < (int)(*l_pD)["tint"].size(); l_iI++) l_arrWash[l_iI] = (*l_pD)["tint"][l_iI].get<float>();
        }
    }
    if (l_jLook.contains("anim") && l_jLook["anim"].is_string() && l_oG.m_upRes) {
        const std::string l_strRef = l_jLook["anim"].get<std::string>();
        const size_t l_nH = l_strRef.find('#');
        if (l_nH != std::string::npos) {
            auto l_upOwn = std::make_unique<RenderLook>();
            l_upOwn->f_Add(RenderLook::f_ClipObject(l_oG, l_strRef.substr(0, l_nH), l_strRef.substr(l_nH + 1), l_arrTint));
            if (!l_upOwn->f_Empty()) l_oC.m_vLooks.push_back(std::move(l_upOwn));
        }
    }
    if (f_StartsWith(l_oE.m_strKind, "monster:") && !l_bOwnSprite && l_oG.m_upRes) {   // its own sprite: no idle_<id> clip
        std::shared_ptr<toms::res::AnimRes> l_spIdle = l_oG.m_upRes->f_Get<toms::res::AnimRes>(toms::key::g_pszAnimMonsterIdle);
        if (l_spIdle->f_Ok() && l_spIdle->f_Find("idle_" + l_oE.m_strId)) {
            float l_fSw, l_fSh;
            f_SpriteSize(l_oG, f_EntSprite(l_oE.m_strId), l_fSw, l_fSh);
            auto l_upClip = std::make_unique<RenderLook>();
            l_upClip->f_Add(RenderLook::f_ClipObject(l_oG, toms::key::g_pszAnimMonsterIdle, "idle_" + l_oE.m_strId, l_arrTint), l_fSw, l_fSh);
            if (!l_upClip->f_Empty()) l_oC.m_vLooks.push_back(std::move(l_upClip));
        }
    }
    // an NPC's idle clip "idle_<npc id>" (assets/media/anim/npc_idle.anim; tools/make_idle_clips.py); none: its sprite
    if (l_bNpc && !l_bOwnSprite && l_oG.m_upRes) {
        std::shared_ptr<toms::res::AnimRes> l_spIdle = l_oG.m_upRes->f_Get<toms::res::AnimRes>(toms::key::g_pszAnimNpcIdle);
        if (l_spIdle->f_Ok() && l_spIdle->f_Find("idle_" + l_oE.m_strId)) {
            float l_fSw, l_fSh;
            f_SpriteSize(l_oG, l_oG.f_NpcSprite(l_oE.m_strId), l_fSw, l_fSh);
            auto l_upClip = std::make_unique<RenderLook>();
            l_upClip->f_Add(RenderLook::f_ClipObject(l_oG, toms::key::g_pszAnimNpcIdle, "idle_" + l_oE.m_strId, l_arrTint), l_fSw, l_fSh);
            if (!l_upClip->f_Empty()) l_oC.m_vLooks.push_back(std::move(l_upClip));
        }
    }
    auto l_upStill = std::make_unique<RenderLook>();
    l_upStill->f_Add(RenderLook::f_SpriteObject(l_oG, l_strSprite, l_arrTint));
    if (l_arrWash[3] > 0) l_upStill->f_Add(RenderLook::f_SpriteObject(l_oG, l_strSprite, l_arrWash, /*solid=*/true));   // the key colour over it
    l_oC.m_vLooks.push_back(std::move(l_upStill));
    return l_oC;
}

// A door's / gate's opening clip (assets/media/anim/door_open.anim), on the full tile, once.
static LookChain f_OpeningLook(Game& l_oG, const std::string& l_strColour) {
    LookChain l_oC;
    l_oC.m_strNext = "gone";
    if (!l_oG.m_upRes) return l_oC;
    std::shared_ptr<toms::res::AnimRes> l_spDoor = l_oG.m_upRes->f_Get<toms::res::AnimRes>(toms::key::g_pszAnimDoorOpen);
    if (!l_spDoor->f_Ok() || !l_spDoor->f_Find("door_open_" + l_strColour)) return l_oC;
    float l_fSw, l_fSh;
    f_SpriteSize(l_oG, "door_" + l_strColour, l_fSw, l_fSh);
    auto l_upOpening = std::make_unique<RenderLook>(RenderLook::Clock::Status);
    l_upOpening->f_Add(RenderLook::f_ClipObject(l_oG, toms::key::g_pszAnimDoorOpen, "door_open_" + l_strColour, g_arrWhite), l_fSw, l_fSh);
    if (!l_upOpening->f_Empty()) l_oC.m_vLooks.push_back(std::move(l_upOpening));
    return l_oC;
}

// The "looks" table of a type's record and of the object itself: each status's look (the object's win).
static void f_TableLooks(Game& l_oG, MapObject& l_oO, const nlohmann::json* l_pTable) {
    if (!l_pTable || !l_pTable->is_object()) return;
    for (auto& [l_strStatus, l_jD] : l_pTable->items()) l_oO.f_SetLook(l_strStatus, f_LookFromJson(l_oG, l_jD));
}

static void f_Build(Game& l_oG, MapFloor& l_oF, toms::Node& l_oRoot, std::vector<std::shared_ptr<MapObject>>& l_vObjects,
                  std::shared_ptr<PlayerObject>& l_spPlayer);
};

void MapFloor::f_Clear() {
    m_vObjects.clear();
    m_spPlayer.reset();
    m_vDrawList.clear();
    m_iGeneration++;
}

void MapFloor::f_Build(Game& l_oG) {
    f_Clear();
    Builder::f_Build(l_oG, *this, m_oRoot, m_vObjects, m_spPlayer);
}

void map_detail::Builder::f_Build(Game& l_oG, MapFloor& l_oF, toms::Node& l_oRoot, std::vector<std::shared_ptr<MapObject>>& l_vObjects,
                                std::shared_ptr<PlayerObject>& l_spPlayer) {
    (void)l_oF;
    l_oRoot.f_SetName("Floor " + l_oG.m_oSt.m_strId);
    for (int l_iI = 0; l_iI < (int)l_oG.m_oSt.m_vEntities.size(); ++l_iI) {
        const Entity& l_oE = l_oG.m_oSt.m_vEntities[(size_t)l_iI];
        std::shared_ptr<MapObject> l_spO = f_MakeObject(l_oG, l_iI, l_oE);
        l_spO->f_SetName(l_oE.m_strKind + " (" + std::to_string(l_oE.m_iX) + "," + std::to_string(l_oE.m_iY) + ")");
        if (f_StartsWith(l_oE.m_strKind, "door:")) {
            l_spO->f_SetLook("opening", f_OpeningLook(l_oG, l_oE.m_strId));
        } else {
            l_spO->f_SetLook("idle", f_LegacyIdleLook(l_oG, l_oE));
            // the yellow door's opening clip, for a gate drawn as the yellow door (one with its own door just opens)
            const nlohmann::json* l_pKey = f_StartsWith(l_oE.m_strKind, "gate:") ? l_oG.f_ItemDef(l_oE.m_strId) : nullptr;
            if (l_pKey && !(l_pKey->contains("door") && (*l_pKey)["door"].is_string())) l_spO->f_SetLook("opening", f_OpeningLook(l_oG, "yellow"));
        }
        // the data's own looks per status: the type's record, then the placed object's
        const size_t l_nColon = l_oE.m_strKind.find(':');
        const std::string l_strCat = l_oE.m_strKind.substr(0, l_nColon);
        const nlohmann::json* l_pRec = nullptr;
        if (l_strCat == "monster") { auto l_itT = l_oG.m_mapEnemyTpl.find(l_oE.m_strId); if (l_itT != l_oG.m_mapEnemyTpl.end()) l_pRec = &l_itT->second; }
        else if (l_strCat == "item") l_pRec = l_oG.f_ItemDef(l_oE.m_strId);
        if (l_pRec && l_pRec->contains("looks")) f_TableLooks(l_oG, *l_spO, &(*l_pRec)["looks"]);
        if (l_oE.m_jLook.is_object() && l_oE.m_jLook.contains("looks")) f_TableLooks(l_oG, *l_spO, &l_oE.m_jLook["looks"]);
        l_oRoot.f_AddChildToLast(l_spO.get(), false);
        l_vObjects.push_back(std::move(l_spO));
    }
    l_spPlayer = std::make_shared<PlayerObject>(l_oG);
    l_spPlayer->f_SetName("player");
    {
        LookChain l_oC;
        auto l_upStill = std::make_unique<RenderLook>();
        l_upStill->f_Add(RenderLook::f_SpriteObject(l_oG, l_oG.f_PlayerSprite(), g_arrWhite));
        l_oC.m_vLooks.push_back(std::move(l_upStill));
        l_spPlayer->f_SetLook("idle", std::move(l_oC));
    }
    l_oRoot.f_AddChildToLast(l_spPlayer.get(), false);
}

void MapFloor::f_Update(int l_iDtMs) {
    for (const auto& l_spO : m_vObjects) l_spO->f_Update(l_iDtMs);
}

MapObject* MapFloor::f_ObjectAt(int l_iX, int l_iY, const char* l_pszKindPrefix) {
    for (const auto& l_spO : m_vObjects) {
        const Entity* l_pE = l_spO->f_Entity();
        if (l_pE && l_pE->m_iX == l_iX && l_pE->m_iY == l_iY && l_pE->m_strKind.rfind(l_pszKindPrefix, 0) == 0) return l_spO.get();
    }
    return nullptr;
}

// The underlays (an opening door), then every object of the sorted pass and the player in one pass by the bottom row
// each covers (S1 F5): a 2x2 boss stands behind what is below it, the player behind the monsters below it. The same
// order as before: the stage file's order, then the player, stably sorted.
void MapFloor::f_Render(const MapView& l_oV) {
    for (const auto& l_spO : m_vObjects) l_spO->f_RenderUnderlay(l_oV);
    m_vDrawList.clear();
    for (const auto& l_spO : m_vObjects)
        if (l_spO->f_Sorted() && l_spO->f_Status() != "gone") m_vDrawList.push_back(l_spO.get());
    if (m_spPlayer) m_vDrawList.push_back(m_spPlayer.get());
    std::stable_sort(m_vDrawList.begin(), m_vDrawList.end(), [](const MapObject* l_pA, const MapObject* l_pB) {
        const int l_iKa = l_pA->f_SortKey(), l_iKb = l_pB->f_SortKey();
        if (l_iKa != l_iKb) return l_iKa < l_iKb;
        return l_pA->f_TieX() < l_pB->f_TieX();
    });
    for (MapObject* l_pO : m_vDrawList) l_pO->f_Render(l_oV);
}

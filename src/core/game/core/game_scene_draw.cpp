// game_scene_draw.cpp — the per-frame world: draw() (the map, its entities and the player; the UI
// on top is RmlUi -- see game_ui.cpp) and the two ImGui developer tools (F1 debug overlay, F2
// styling spike). Split out of game.cpp 2026-09-13; UI drawing moved to RmlUi 2026-09-27.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::draw() {
    ren->begin();
    // Title phase (Boot screen): drawn INSTEAD of the world, then nothing else. There is
    // nothing meaningful to show behind it yet, and skipping the world draw keeps the title's
    // layout independent of whatever stage happens to be loaded underneath it.
    if (title_.isOpen()) { ren->end(); return; }
    // M7 (first slice): an active ending takes over the whole screen the same way the title does
    // -- nothing behind it is meaningful once a run has actually ended (see triggerEnding()).
    if (endingActive()) { ren->end(); return; }
    // Milestone 9 originally shrank tile size to fit the whole (up to 34x31) grid onto one
    // fixed-size screen -- legible on desktop, but tiny/hard-to-tap on mobile once stages grew
    // past the smallest ones. Now tile size is derived from viewCols_ (how many columns should
    // be visible -- see its declaration in game.h) and a camera (camX_/camY_, eased toward
    // updateCameraTarget()'s result every frame in update()) scrolls a viewport over the grid
    // instead -- see cameraMode_'s declaration in game.h for the two modes.
    int gw = st.width, gh = st.height;
    float oy = 60.0f;
    float ts; int viewCols, viewRows; cameraViewportTiles(ts, viewCols, viewRows);
    float ox = -cam_.x() * ts;
    oy -= cam_.y() * ts;
    static const float white[4] = {1,1,1,1};

    const bool showStore = storeModal();
    // Milestone 7 bugfix (still relevant under Battle System v2): this used to fall back to
    // scanning cs.log for the substring "倒下" to keep the overlay open during the brief post-lose
    // result-pause (cs.active and cs.won are both already false by then). That substring also
    // appears in the WIN message ("...敵人倒下了！" -- the enemy fell down), so after a win was
    // dismissed (cs.won reset to false) the overlay would never actually close, since the old log
    // text still matched. cs.resultPauseMs is the actual, non-fragile signal for "a loss was just
    // resolved and needs its readability beat" -- see finishCombatLose() and update()'s own
    // comment on this countdown.
    const bool showBattle = !showStore && (hideMask & 1) == 0 && (cs.active || cs.won || cs.resultPauseMs > 0);
    const bool showTalk   = !showStore && !showBattle && (hideMask & 2) == 0 && inDialogue;
    const bool showInv    = !showStore && !showBattle && !showTalk && (hideMask & 4) == 0 && invOpen;
    const bool showWalk   = !showStore && !showBattle && !showTalk && !showInv;

    // ---- walking scene: STAGE + CHARACTER ----
    if (showWalk) {
        ren->setNode(NODE_STAGE);
        // Only draw tiles the camera can actually see (+1 tile of padding on every side so a
        // mid-pan fractional camX_/camY_ never pops a row/column in right at the edge) --
        // stage_11 is 34x31 (1054 tiles) against a ~21x13 viewport, so this is a real ~4x cut
        // in quads submitted, not just tidiness.
        int startX = std::max(0, (int)std::floor(cam_.x()) - 1);
        int endX   = std::min(gw, (int)std::ceil(cam_.x()) + viewCols + 1);
        int startY = std::max(0, (int)std::floor(cam_.y()) - 1);
        int endY   = std::min(gh, (int)std::ceil(cam_.y()) + viewRows + 1);
        // S3.5 (e): the act's palette tints the map tiles. One atlas, ten looks -- no new art needed,
        // and the maze stays readable because the tint is a light wash rather than a replace.
        float themeTint[4];
        toms::floorThemeTint(themeActIndex_, themeTint);
        for (int y = startY; y < endY; y++) for (int x = startX; x < endX; x++) {
            char c = st.at(x,y);
            int layer = spriteLayer(cellSprite(c));
            ren->drawSprite(spriteQuad(ox + x*ts, oy + y*ts, ts, ts, layer, themeTint));
        }
        // Click-to-move destination: a soft gold plate on the target tile while walking there.
        if (walking() && walkTargetX_ >= 0) {
            const float m = ts * 0.12f;
            Quad t; t.rect[0] = ox + walkTargetX_*ts + m; t.rect[1] = oy + walkTargetY_*ts + m;
            t.rect[2] = ts - 2*m; t.rect[3] = ts - 2*m;
            t.uv[0] = 0; t.uv[1] = 0; t.uv[2] = 1; t.uv[3] = 1; t.solid = true;
            t.tint[0] = 1.0f; t.tint[1] = 0.85f; t.tint[2] = 0.3f; t.tint[3] = 0.35f;
            ren->drawSprite(t);
        }
        // Entity/player sprites were inset by a fixed 8px into their 48px tile before
        // Milestone 9; now that ts varies per stage, the inset scales with it (same
        // ~1/6 ratio, so this renders identically to before at ts=48).
        float inset = ts / 6.0f, sSize = ts - 2.0f * inset;
        // S1 (docs/design/ART_AND_ABILITY_DESIGN.md section 1.7):
        //   * a multi-grid entity is drawn at footprint * tile size, anchored on its top-left
        //     occupied tile (F2: each grid keeps its own 32px art density -- this is more detail,
        //     not a scaled-up sprite);
        //   * everything on the floor is drawn in ONE y-sorted pass keyed on the bottom-most
        //     occupied row (F5). Before S1 entities simply drew in file order and the player drew
        //     last, which only read correctly while every sprite was one tile tall: a 2x2 boss
        //     would have drawn over the wall above it and the player would always float on top of
        //     monsters instead of standing behind the ones below them;
        //   * 4-grid entities (and roamers) get a name banner (F6) -- drawn by the HUD (RmlUi, see
        //     buildUiState's hud.banners), at this same position.
        struct FloorSprite {
            int   sortKey;         // bottom-most occupied row
            int   tieX;            // stable, left-to-right tie-break within a row
            float x, y, w, h;      // pixel rect
            int   layer;
            bool  plate = false;   // S3.5 (d): an event marker -> solid story plate, not an atlas sprite
        };
        std::vector<FloorSprite> floor;
        floor.reserve(st.entities.size() + 1);
        for (auto& e : st.entities) {
            if (e.consumed) continue;
            FloorSprite f;
            f.sortKey = footprintSortKey_T(e);
            f.tieX = e.x;
            f.w = e.fp.w * ts - 2.0f * inset;
            f.h = e.fp.h * ts - 2.0f * inset;
            f.x = ox + e.x*ts + inset;
            f.y = oy + e.y*ts + inset;
            f.layer = spriteLayer(entSprite(e.id));
            // S3.5 (d): an event marker is drawn as a solid story plate rather than an atlas sprite --
            // the atlas has no "relic/whisper/cache" art yet (S8 owns that), and a plate reads as
            // "something to read here" without pretending to be a creature or an item.
            f.plate = e.kind.rfind("event:", 0) == 0;
            floor.push_back(f);
        }
        {
            FloorSprite f;
            f.sortKey = footprintSortKey_T(pl.x, pl.y, toms::Footprint{});
            f.tieX = pl.x;
            f.w = sSize; f.h = sSize;
            f.x = ox + pl.x*ts + inset;
            f.y = oy + pl.y*ts + inset;
            f.layer = spriteLayer("player");
            floor.push_back(f);
        }
        std::stable_sort(floor.begin(), floor.end(), [](const FloorSprite& a, const FloorSprite& b) {
            if (a.sortKey != b.sortKey) return a.sortKey < b.sortKey;
            return a.tieX < b.tieX;
        });
        for (auto& f : floor) {
            if (f.plate) {
                // S3.5 (d): a story plate -- a solid tile-sized marker with a lighter inset, so an
                // event cell is visibly "something to read" on the map.
                float gold[4] = {0.86f, 0.72f, 0.36f, 0.92f};
                float inner[4] = {0.28f, 0.34f, 0.46f, 0.95f};
                ren->drawSprite(spriteQuad(f.x, f.y, f.w, f.h, f.layer, gold));
                float in = f.w * 0.22f;
                ren->drawSprite(spriteQuad(f.x + in, f.y + in, f.w - 2*in, f.h - 2*in, f.layer + 1, inner));
            } else {
                ren->drawSprite(spriteQuad(f.x, f.y, f.w, f.h, f.layer, white));
            }
        }
        drawStylingSpikeBackdrop();
    }
    // Battle / dialogue / inventory / store: the RmlUi documents fill the screen, no world behind.
    ren->end();
}

// ---------------------------------------------------------------------------------------------
// ImGui developer tools -- compiled on BOTH desktop and web: the font-scale setter, the F1 debug
// overlay and the F2 styling spike. The ImGui backend is bgfx (src/engine/src/imgui_bgfx.*),
// driven by GameSession on both platforms. Nothing here touches gameplay
// state a player sees unless they press F1/F2, so the browser build behaves exactly as before.
void Game::applyUiSettings() {
    ImGui::GetIO().FontGlobalScale = 1.5f;   // ImGui's default font reads small at this resolution
}

void Game::drawDebugOverlay() {
    ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("TOMS Debug (F1 to toggle)");
    ImGui::Text("State: %s", toms::toString(currentState()));
    ImGui::Text("Stage: %s", curStage.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted("Player stats (hand-trace bestiary fights live)");
    ImGui::SliderInt("HP", &pl.hp, 0, pl.maxhp > 0 ? pl.maxhp : 999);
    ImGui::SliderInt("Max HP", &pl.maxhp, 1, 999);
    ImGui::SliderInt("ATK", &pl.atk, 0, 99);
    ImGui::SliderInt("DEF", &pl.def, 0, 99);
    ImGui::SliderInt("Level", &pl.lv, 1, 20);
    ImGui::SliderInt("EXP", &pl.exp, 0, 999);
    ImGui::SliderInt("Gold", &pl.gold, 0, 999);
    ImGui::Separator();
    if (ren) {
        static const char* nodeNames[] = { "All", "Stage", "Char", "Talk", "Battle", "Store" };
        static int nodeSel = 0;
        if (ImGui::Combo("Node filter (diagnostic)", &nodeSel, nodeNames, IM_ARRAYSIZE(nodeNames)))
            ren->setNodeFilter((uint8_t)nodeSel);
    }
    ImGui::Separator();
    // Maze camera: how many tile columns are visible (tile size + row count both derive from
    // this -- see cameraViewportTiles()). Applies live every frame (nothing to "apply"), and
    // persists to settings.json when you release the slider so a chosen value survives restart
    // -- IsItemDeactivatedAfterEdit() fires once per drag instead of once per dragged pixel.
    ImGui::TextUnformatted("Maze camera (see also: in-game menu > Settings > Camera mode)");
    int visibleCells = cam_.viewCols();
    if (ImGui::SliderInt("Visible cells", &visibleCells, 6, 40)) cam_.setViewCols(visibleCells);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings_.viewCols = cam_.viewCols();
        toms::ensureSaveDir(toms::defaultSaveDir());
        toms::saveGameSettings(toms::defaultSaveDir() + "/settings.json", settings_);
    }
    ImGui::Separator();
    ImGui::TextWrapped("Last combat log: %s", cs.log.empty() ? "(none)" : cs.log.c_str());
    ImGui::Text("Inventory: %d item(s)", (int)pl.inv.size());
    ImGui::End();
}

// M2 styling spike: an undecorated, transparent-background ImGui window positioned at the exact
// same rect as a scene-graph-drawn backdrop (drawStylingSpikeBackdrop(), called from draw()).
// If this reads as one seamless panel on screen rather than two overlapping things, it confirms
// the hybrid rendering approach Milestone 5 is about to commit real screens to.
void Game::drawStylingSpike() {
    if (!ren) return;
    float W = (float)ren->width(), H = (float)ren->height();
    // Arbitrary "a panel-sized rect" for this spike -- unlike the pause menu's sub-page sizes
    // (see computeSubPageLayout() in game_helpers.h), nothing else in the codebase shares this
    // size, so it is named here rather than promoted to a shared constant.
    constexpr float kSpikeW = 420.0f, kSpikeH = 260.0f;
    float w = kSpikeW, h = kSpikeH;
    float x = (W - w) * 0.5f, y = (H - h) * 0.5f;
    stylingSpikeRect_[0] = x; stylingSpikeRect_[1] = y; stylingSpikeRect_[2] = w; stylingSpikeRect_[3] = h;
    stylingSpikeVisible_ = true;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                             ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("StylingSpike", nullptr, flags);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.7f, 1.0f), "M2 Styling Spike (F2 to toggle)");
    ImGui::TextWrapped("This text and button are drawn by ImGui with NoBackground. The panel "
                        "behind them is drawn by the scene-graph/renderer as a solid-tint quad "
                        "(standing in for real 9-slice art) at the exact same rect.");
    static int clicks = 0;
    if (ImGui::Button("Click me")) clicks++;
    ImGui::SameLine();
    ImGui::Text("clicks: %d", clicks);
    ImGui::End();
}

void Game::drawStylingSpikeBackdrop() {
    if (!stylingSpikeVisible_ || !ren) return;
    float x = stylingSpikeRect_[0], y = stylingSpikeRect_[1], w = stylingSpikeRect_[2], h = stylingSpikeRect_[3];
    // Outer rect (darker "border") + inset inner rect (lighter "fill") -- a cheap two-rect
    // stand-in for a real 9-slice panel; the point of this spike is the ImGui/scene-graph
    // alignment technique, not authoring actual 9-slice art.
    Quad outer; outer.rect[0]=x; outer.rect[1]=y; outer.rect[2]=w; outer.rect[3]=h;
    outer.uv[0]=0;outer.uv[1]=0;outer.uv[2]=1;outer.uv[3]=1; outer.solid=true;
    outer.tint[0]=0.15f; outer.tint[1]=0.13f; outer.tint[2]=0.22f; outer.tint[3]=0.96f;
    ren->drawSprite(outer);
    float inset = 6.0f;
    Quad inner; inner.rect[0]=x+inset; inner.rect[1]=y+inset; inner.rect[2]=w-2*inset; inner.rect[3]=h-2*inset;
    inner.uv[0]=0;inner.uv[1]=0;inner.uv[2]=1;inner.uv[3]=1; inner.solid=true;
    inner.tint[0]=0.10f; inner.tint[1]=0.09f; inner.tint[2]=0.16f; inner.tint[3]=0.96f;
    ren->drawSprite(inner);
}

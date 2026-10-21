// game_scene_draw.cpp — the per-frame world: draw() (the map, its entities and the player; the UI
// on top is RmlUi -- see game_ui.cpp) and the two ImGui developer tools (F1 debug overlay, F2
// styling spike). Split out of game.cpp 2026-09-13; UI drawing moved to RmlUi 2026-09-27.
#include "game_internal.h"

#include <stb_image.h>   // declarations only (game_assets.cpp has the implementation)
#include <memory>

using namespace toms::game_detail;

void Game::draw() {
    ren->begin();
    // Title phase (Boot screen): drawn INSTEAD of the world, then nothing else. There is
    // nothing meaningful to show behind it yet, and skipping the world draw keeps the title's
    // layout independent of whatever stage happens to be loaded underneath it.
    if (title_.isOpen()) { drawPreviewAnim(); ren->end(); return; }
    // M7 (first slice): an active ending takes over the whole screen the same way the title does
    // -- nothing behind it is meaningful once a run has actually ended (see triggerEnding()).
    if (endingActive()) { drawPreviewAnim(); ren->end(); return; }
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
    const bool showWalk   = !showStore && !showBattle && !showTalk;   // the player menu draws over the map

    // ---- walking scene: the floor's tiles, then its objects (each draws itself by status: map_objects.h) ----
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
        MapView view;
        view.ox = ox; view.oy = oy; view.ts = ts;
        // Entity/player sprites were inset by a fixed 8px into their 48px tile before Milestone 9; now that ts varies
        // per stage, the inset scales with it (same ~1/6 ratio, so this renders identically to before at ts=48).
        view.inset = ts / 6.0f;
        view.sSize = ts - 2.0f * view.inset;
        drawTileLayer(view, startX, endX, startY, endY);
        floor_.render(view);
        drawStylingSpikeBackdrop();
    }
    // Battle / dialogue / inventory / store: the RmlUi documents fill the screen, no world behind.
    drawPreviewAnim(); ren->end();
}

// The floor's tiles (the act's colour washed over them), the locked stairs' padlocks and the click-to-move target.
void Game::drawTileLayer(const MapView& v, int startX, int endX, int startY, int endY) {
    const float ox = v.ox, oy = v.oy, ts = v.ts;
    // S3.5 (e): the act's palette tints the map tiles. One atlas, ten looks -- no new art needed,
    // and the maze stays readable because the tint is a light wash rather than a replace.
    float themeTint[4];
    toms::floorThemeTint(themeActIndex_, themeTint);
    // the floor's theme: its own wall / floor / stairs pictures where the atlas has them
    auto themed = [&](const std::string& name) {
        if (st.theme.empty() || (name != "wall" && name != "floor" && name != "stairs_up" && name != "stairs_down")) return name;
        const std::string t = name + "_" + st.theme;
        return idToLayer.count(t) ? t : name;
    };
    for (int y = startY; y < endY; y++) for (int x = startX; x < endX; x++) {
        char c = st.at(x,y);
        int layer = spriteLayer(themed(cellSprite(c)));
        ren->drawSprite(spriteQuad(ox + x*ts, oy + y*ts, ts, ts, layer, themeTint));
    }
    // Q7: a locked stair stays visible, dimmed, with a padlock on it until its event is finished.
    auto solid = [&](float x, float y, float w, float h, float r, float g, float b, float a) {
        Quad q; q.rect[0] = x; q.rect[1] = y; q.rect[2] = w; q.rect[3] = h;
        q.uv[0] = 0; q.uv[1] = 0; q.uv[2] = 1; q.uv[3] = 1; q.solid = true;
        q.tint[0] = r; q.tint[1] = g; q.tint[2] = b; q.tint[3] = a;
        ren->drawSprite(q);
    };
    for (const toms::StairLink& s : st.stairs) {
        if (!stairLocked(s) || s.x < startX || s.x >= endX || s.y < startY || s.y >= endY) continue;
        const float x0 = ox + s.x * ts, y0 = oy + s.y * ts, u = ts / 16.0f;
        solid(x0, y0, ts, ts, 0.0f, 0.0f, 0.0f, 0.45f);                                   // dimmed stair
        solid(x0 + 5 * u, y0 + 3 * u, 1.5f * u, 5 * u, 0.20f, 0.16f, 0.08f, 1.0f);       // shackle, left
        solid(x0 + 9.5f * u, y0 + 3 * u, 1.5f * u, 5 * u, 0.20f, 0.16f, 0.08f, 1.0f);    // shackle, right
        solid(x0 + 5 * u, y0 + 2.5f * u, 6 * u, 1.5f * u, 0.20f, 0.16f, 0.08f, 1.0f);    // shackle, top
        solid(x0 + 3.5f * u, y0 + 7 * u, 9 * u, 7 * u, 0.20f, 0.16f, 0.08f, 1.0f);       // body outline
        solid(x0 + 4.5f * u, y0 + 8 * u, 7 * u, 5 * u, 0.98f, 0.80f, 0.30f, 1.0f);       // body
        solid(x0 + 7.5f * u, y0 + 9 * u, 1 * u, 2.5f * u, 0.20f, 0.16f, 0.08f, 1.0f);    // keyhole
    }
    // W38: a stair that just opened (its "go to the stairs" event is open) has a gold arrow bobbing over it, so the
    // player sees where to go; it goes once the stair is taken.
    for (const toms::StairLink& s : st.stairs) {
        if (!stairJustOpened(st.id, s) || s.x < 0) continue;
        const float u = ts / 16.0f;
        if (s.x < startX || s.x >= endX || s.y < startY || s.y >= endY) {
            // off screen: a gold badge at the edge of the view, its arrow pointing the way to the stair
            const int tx = std::clamp(s.x, startX + 1, std::max(startX + 1, endX - 2));
            const int ty = std::clamp(s.y, startY + 1, std::max(startY + 1, endY - 2));
            const float bx = ox + tx * ts, by = oy + ty * ts;
            const int dx = s.x - tx, dy = s.y - ty;
            const bool horiz = std::abs(dx) >= std::abs(dy);
            const int sign = horiz ? (dx > 0 ? 1 : -1) : (dy > 0 ? 1 : -1);
            const float pulse = 0.75f + 0.25f * std::sin((float)std::fmod(idleClockMs_, 800.0) / 800.0f * 6.2831853f);
            solid(bx + 2 * u, by + 2 * u, 12 * u, 12 * u, 0.20f, 0.16f, 0.08f, 0.9f);                  // badge outline
            solid(bx + 3 * u, by + 3 * u, 10 * u, 10 * u, 1.0f, 0.85f, 0.3f, pulse);                   // badge
            for (int r = 0; r < 4; r++) {                                                             // the arrow head
                const float len = (7.0f - 2.0f * r) * u, off = (sign > 0 ? 9.0f + r : 6.0f - r) * u;
                if (horiz) solid(bx + off, by + 8 * u - len * 0.5f, u, len, 0.20f, 0.16f, 0.08f, 1.0f);
                else       solid(bx + 8 * u - len * 0.5f, by + off, len, u, 0.20f, 0.16f, 0.08f, 1.0f);
            }
            if (horiz) solid(bx + (sign > 0 ? 4 : 10) * u, by + 7.5f * u, 5 * u, u, 0.20f, 0.16f, 0.08f, 1.0f);   // the shaft
            else       solid(bx + 7.5f * u, by + (sign > 0 ? 4 : 10) * u, u, 5 * u, 0.20f, 0.16f, 0.08f, 1.0f);
            continue;
        }
        const float bob = std::sin((float)std::fmod(idleClockMs_, 1000.0) / 1000.0f * 6.2831853f) * 2.0f * u;
        const float cx = ox + s.x * ts + ts * 0.5f, top = oy + s.y * ts - 9.0f * u + bob;
        solid(ox + s.x * ts, oy + s.y * ts, ts, ts, 1.0f, 0.85f, 0.3f, 0.22f);                    // the stair glows
        solid(cx - 1.5f * u, top, 3.0f * u, 5.0f * u, 0.20f, 0.16f, 0.08f, 1.0f);                 // shaft outline
        solid(cx - 0.75f * u, top + 0.5f * u, 1.5f * u, 4.5f * u, 1.0f, 0.85f, 0.3f, 1.0f);       // shaft
        for (int r = 0; r < 4; r++) {                                                             // the head, row by row
            const float w = (7.0f - 2.0f * r) * u, y = top + 5.0f * u + r * u;
            solid(cx - w * 0.5f - 0.5f * u, y, w + u, u, 0.20f, 0.16f, 0.08f, 1.0f);
            solid(cx - w * 0.5f, y, w, u, 1.0f, 0.85f, 0.3f, 1.0f);
        }
    }
    // Click-to-move destination: a soft gold plate on the target tile while walking there.
    if (walking() && walkTargetX_ >= 0) {
        const float m = ts * 0.12f;
        solid(ox + walkTargetX_ * ts + m, oy + walkTargetY_ * ts + m, ts - 2 * m, ts - 2 * m, 1.0f, 0.85f, 0.3f, 0.35f);
    }
}

// The cache's error for a file, worded as the previews always reported it.
static std::string resourceError(const std::string& file, const toms::res::Resource& r) {
    return r.error() == "cannot read" ? "cannot read " + file : file + ": " + r.error();
}

// A preview's file comes from the command line: relative to the working folder, not to assets/media (the cache's root).
static std::string fromWorkingDir(const std::string& file) {
#if !defined(__ANDROID__)
    if (!file.empty() && file[0] != '/' && file[0] != '\\' && !(file.size() > 1 && file[1] == ':')) {
        std::error_code ec;
        const std::filesystem::path abs = std::filesystem::absolute(toms::vfsPath(file), ec);
        if (!ec) return toms::vfsUtf8(abs);
    }
#endif
    return file;
}

bool Game::playPreviewAnim(const std::string& file, const std::string& clip, std::string& error) {
    if (!res_) { error = "the renderer is not up yet"; return false; }
    std::shared_ptr<toms::res::AnimRes> a = res_->get<toms::res::AnimRes>(fromWorkingDir(file));
    res_->refresh(a);   // the anim editor may have saved it since it was last played
    if (!a->ok()) { error = resourceError(file, *a); return false; }
    if (!a->find(clip)) { error = file + " has no clip '" + clip + "'"; return false; }
    previewAnim_ = a;
    previewClip_ = clip;
    previewAnimVer_ = a->version();
    previewPlayer_.play(a->find(clip));
    previewMissing_.clear();
    std::fprintf(stderr, "[anim] preview %s#%s (%.2fs, %zu atlas(es))\n", file.c_str(), clip.c_str(),
                 a->find(clip)->duration(), a->atlases().entries.size());
    return true;
}

bool Game::playPreviewFx(const std::string& file, const std::string& effect, std::string& error) {
    if (!res_) { error = "the renderer is not up yet"; return false; }
    std::shared_ptr<toms::res::ParticleRes> f = res_->get<toms::res::ParticleRes>(fromWorkingDir(file));
    res_->refresh(f);   // the particle editor may have saved it since
    if (!f->ok()) { error = resourceError(file, *f); return false; }
    if (!f->find(effect)) { error = file + " has no effect '" + effect + "'"; return false; }
    for (const toms::fx::Problem& p : toms::fx::checkParticles(f->file(), &f->atlases()))
        std::fprintf(stderr, "[fx] %s %s: %s\n", p.warning ? "warning" : "error", p.where.c_str(), p.text.c_str());
    previewFxFile_ = f;
    previewFxName_ = effect;
    previewFxVer_ = f->version();
    const toms::fx::Effect* e = f->find(effect);
    // Placed before play(): the prewarm already makes world-space particles where the effect is.
    previewFx_.setTransform(toms::anim::placement(ren->width() / 2.0f, ren->height() / 2.0f));
    previewFx_.setGpuSimulation(ren, particleGpuThreshold());   // big emitters on the GPU (setting)
    previewFx_.play(e, e->seed ? e->seed : 1u);
    previewFxIdle_ = 0;
    std::fprintf(stderr, "[fx] preview %s#%s (%zu emitter(s), %d on the GPU above %d particles, seed %u)\n", file.c_str(),
                 effect.c_str(), e->emitters.size(), previewFx_.gpuEmitters(), particleGpuThreshold(), previewFx_.seed());
    return true;
}

// The game's loaded sprite atlas, as the cache's kSpriteAtlas: clips and effects that name ".../atlas/game.atlas" (or
// no atlas) draw from it with the renderer's sprite texture, so they follow the chosen art style. Called whenever it
// is (re)loaded; the cached object stays the same and its version goes up.
void Game::provideSpriteAtlas() {
    if (!res_) return;
    res_->provide<toms::res::AtlasRes>(toms::res::ResourceManager::kSpriteAtlas,
                                       [&](toms::res::AtlasRes& a) { a.setProvided(spriteAtlas_); });
}

// ---- monster idle animations ----
// assets/media/anim/monster_idle.anim holds one looping clip per enemy, "idle_<enemy id>", made in anim_editor. A clip
// is authored around the monster's sprite with its pivot at the bottom centre (0, 0); here it is placed on the bottom
// centre of the monster's rect and scaled so the sprite fills that rect exactly as the static sprite did (a 2x1 golem
// stretches the clip the same way). A missing file or clip, or the runtime sprite grid (no prebuilt atlas), keeps the
// static sprite.
// ---- objects' looks (the stage editor's "objects" and the types' "look"; docs/19_STAGE_EDITOR.md) ----
nlohmann::json Game::typeLook(const std::string& kind, const std::string& id) const {
    const std::string k = kind.substr(0, kind.find(':'));
    const nlohmann::json* src = nullptr;
    if (k == "monster") { auto it = enemyTpl.find(id); if (it != enemyTpl.end()) src = &it->second; }
    else if (k == "item") src = itemDef(id);
    else if (k == "npc") {
        auto it = npcDefs_.find(id);
        if (it != npcDefs_.end()) { nlohmann::json l = it->second.look.is_object() ? it->second.look : nlohmann::json::object();
            if (!l.contains("sprite") && !it->second.sprite.empty()) l["sprite"] = it->second.sprite;
            return l; }
    }
    // a monster's / item's "sprite" (enemies.json / items.json, "slime.png"), then its "look" (anim, tint, ...)
    nlohmann::json l = nlohmann::json::object();
    if (src && src->contains("sprite") && (*src)["sprite"].is_string()) l["sprite"] = (*src)["sprite"];
    if (src && src->contains("look") && (*src)["look"].is_object()) for (auto& [k, v] : (*src)["look"].items()) l[k] = v;
    return l;
}

nlohmann::json Game::lookOf(const Entity& e) const {
    nlohmann::json l = typeLook(e.kind, e.id);
    if (e.look.is_object()) for (auto& [k, v] : e.look.items()) l[k] = v;   // field by field: the object's own win
    return l;
}

std::string Game::typeSprite(const std::string& kind, const std::string& id) const {
    const nlohmann::json l = typeLook(kind, id);
    if (l.contains("sprite") && l["sprite"].is_string()) {
        std::string s = l["sprite"].get<std::string>();
        if (s.size() > 4 && s.compare(s.size() - 4, 4, ".png") == 0) s.resize(s.size() - 4);
        if (spriteAtlas_.find(s)) return s;
    }
    return kind.rfind("npc", 0) == 0 ? npcSprite(id) : entSprite(id);
}

bool Game::drawClipInRect(const toms::anim::Clip& clip, float t, const toms::anim::AtlasSet& set, float x, float y, float w, float h,
                          float spriteW, float spriteH) {
    static const float kWhite[4] = {1, 1, 1, 1};
    return drawClipInRect(clip, t, set, x, y, w, h, spriteW, spriteH, kWhite);
}

bool Game::drawClipInRect(const toms::anim::Clip& clip, float t, const toms::anim::AtlasSet& set, float x, float y, float w, float h,
                          float spriteW, float spriteH, const float tint[4]) {
    toms::anim::evaluate(clip, t, idlePoses_);
    const glm::mat3 place =
        toms::anim::flatten(toms::anim::compose({x + w * 0.5f, y + h, 0}, glm::vec3(0), {w / spriteW, h / spriteH, 1}));
    idleQuads_.clear();
    const bool tinted = tint && (tint[0] != 1 || tint[1] != 1 || tint[2] != 1 || tint[3] != 1);
    toms::anim::appendQuads(idlePoses_, set, place, tinted ? tint : nullptr, idleQuads_);
    if (idleQuads_.empty()) return false;
    for (const Quad& q : idleQuads_) ren->drawSprite(q);
    return true;
}

// The preview clip and/or effect, centred on the screen, over the world (before ren->end()).
void Game::drawPreviewAnim() {
    if (previewFx_.effect() && previewFxFile_) {
        const size_t known = previewMissing_.size();
        previewFx_.setTransform(toms::anim::placement(ren->width() / 2.0f, ren->height() / 2.0f));
        previewFx_.draw(ren, previewFxFile_->atlases(), glm::mat3(1.0f), nullptr, &previewMissing_);   // CPU quads + GPU emitters
        for (size_t i = known; i < previewMissing_.size(); i++)
            std::fprintf(stderr, "[fx] sprite '%s' is not in the atlas\n", previewMissing_[i].c_str());
    }
    if (!previewPlayer_.showing() || !previewAnim_) return;
    const size_t known = previewMissing_.size();
    toms::anim::evaluate(*previewPlayer_.clip(), previewPlayer_.poseTime(), previewPoses_);
    previewQuads_.clear();
    toms::anim::appendQuads(previewPoses_, previewAnim_->atlases(), toms::anim::placement(ren->width() / 2.0f, ren->height() / 2.0f),
                            nullptr, previewQuads_, &previewMissing_);
    for (const Quad& q : previewQuads_) ren->drawSprite(q);
    for (size_t i = known; i < previewMissing_.size(); i++)
        std::fprintf(stderr, "[anim] sprite '%s' is not in the atlas\n", previewMissing_[i].c_str());
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

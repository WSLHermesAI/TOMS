// game_scene_draw.cpp — the per-frame scene: draw() (walking/battle/dialogue/inventory scene
// selection + the walking scene itself), the virtual gamepad overlay, notifications,
// stage-select preview, styling spike and the F1 debug overlay. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

// map a stage cell char to a sprite id (floor/wall/door/stairs)
// entity id -> sprite id

void Game::draw() {
    ren->begin();
    // Title phase (Boot screen): drawn INSTEAD of the world, then nothing else. There is
    // nothing meaningful to show behind it yet, and skipping the world draw keeps the title's
    // layout independent of whatever stage happens to be loaded underneath it.
    if (title_.isOpen()) {
        ren->setNode(NODE_UNSPEC);
        drawTitleScreen();
        ren->end();
        return;
    }
    float W = (float)ren->width(), H = (float)ren->height();
    // Milestone 9 originally shrank tile size to fit the whole (up to 34x31) grid onto one
    // fixed-size screen -- legible on desktop, but tiny/hard-to-tap on mobile once stages grew
    // past the smallest ones. Now tile size is derived from viewCols_ (how many columns should
    // be visible -- see its declaration in game.h) and a camera (camX_/camY_, eased toward
    // updateCameraTarget()'s result every frame in update()) scrolls a viewport over the grid
    // instead -- see cameraMode_'s declaration in game.h for the two modes.
    int gw = st.width, gh = st.height;
    float oy = 60.0f, bottomMargin = 50.0f;
    float ts; int viewCols, viewRows; cameraViewportTiles(ts, viewCols, viewRows);
    float ox = -cam_.x() * ts;
    oy -= cam_.y() * ts;
    static const float white[4] = {1,1,1,1};
    float tint[4] = {1,1,1,1};

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
        for (int y = startY; y < endY; y++) for (int x = startX; x < endX; x++) {
            char c = st.at(x,y);
            int layer = spriteLayer(cellSprite(c));
            ren->drawSprite(spriteQuad(ox + x*ts, oy + y*ts, ts, ts, layer, white));
        }
        // Entity/player sprites were inset by a fixed 8px into their 48px tile before
        // Milestone 9; now that ts varies per stage, the inset scales with it (same
        // ~1/6 ratio, so this renders identically to before at ts=48).
        float inset = ts / 6.0f, sSize = ts - 2.0f * inset;
        // S1 (docs/ART_AND_ABILITY_DESIGN.md section 1.7):
        //   * a multi-grid entity is drawn at footprint * tile size, anchored on its top-left
        //     occupied tile (F2: each grid keeps its own 32px art density -- this is more detail,
        //     not a scaled-up sprite);
        //   * everything on the floor is drawn in ONE y-sorted pass keyed on the bottom-most
        //     occupied row (F5). Before S1 entities simply drew in file order and the player drew
        //     last, which only read correctly while every sprite was one tile tall: a 2x2 boss
        //     would have drawn over the wall above it and the player would always float on top of
        //     monsters instead of standing behind the ones below them;
        //   * 4-grid entities (and roamers) get a name banner (F6: a boss you can only circle needs
        //     to be identifiable without opening a menu).
        struct FloorSprite {
            int   sortKey;         // bottom-most occupied row
            int   tieX;            // stable, left-to-right tie-break within a row
            float x, y, w, h;      // pixel rect
            int   layer;
            std::string banner;    // empty = no banner
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
            // The banner is for the boss/event tier and for roamers; the text prefers the stage's
            // own displayName and falls back to the entity id, so a floor file can name its boss
            // without any new data file having to exist yet.
            if (e.fp.bossTier() || e.roamer)
                f.banner = e.displayName.empty() ? e.id : e.displayName;
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
            ren->drawSprite(spriteQuad(f.x, f.y, f.w, f.h, f.layer, white));
            if (!f.banner.empty()) {
                // Name banner: a translucent plate centred above the sprite, then the name. Drawn
                // with the game's own text renderer (CJK-capable), not ImGui -- see the M2 note in
                // imgui_web.h for why player-facing text never goes through ImGui.
                const float labelW = (float)f.banner.size() * 13.0f + 16.0f;
                const float lx = f.x + f.w * 0.5f - labelW * 0.5f;
                const float ly = f.y - 22.0f;
                Quad plate; plate.rect[0]=lx; plate.rect[1]=ly; plate.rect[2]=labelW; plate.rect[3]=20.0f;
                plate.uv[0]=0;plate.uv[1]=0;plate.uv[2]=1;plate.uv[3]=1; plate.solid=true;
                plate.tint[0]=0.06f;plate.tint[1]=0.05f;plate.tint[2]=0.09f;plate.tint[3]=0.78f;
                // Stays in whatever node the floor is already drawing in: the plate and the text
                // must land in the same batch as the sprites they annotate, and there is no
                // NODE_TEXT in the render-node enum (NODE_UNSPEC/STAGE/CHAR/TALK/BATTLE/STORE).
                ren->drawSprite(plate);
                drawText(f.banner, lx + 8.0f, ly + 2.0f, 16, C4(1.0f, 0.86f, 0.55f, 1.0f));
            }
        }

        ren->setNode(NODE_CHAR);
        drawText(locale_.tr("game.title") + " — " + st.name + " (" + std::to_string(st.index) + "/" + std::to_string(totalStages) + ")", 16, 16, 22, tint);
        float bx = 16, by = 44;
        drawBar(bx, by, 200, 14, (float)pl.hp/pl.maxhp, C4(0.9f,0.2f,0.2f,1));
        drawText("HP " + std::to_string(pl.hp) + "/" + std::to_string(pl.maxhp), bx+210, by, 18, tint);
        drawText("ATK " + std::to_string(pl.atk) + "  DEF " + std::to_string(pl.def) + "  LV " + std::to_string(pl.lv), bx, by+20, 18, tint);
        drawText("GOLD " + std::to_string(pl.gold) + "  EXP " + std::to_string(pl.exp) + "  " + locale_.tr("hud.keys") + " Y"+std::to_string(pl.key_yellow)+" B"+std::to_string(pl.key_blue)+" R"+std::to_string(pl.key_red) + "  " + locale_.tr("hud.items") + "x" + std::to_string(pl.inv.size()) + " (I)", bx, by+42, 16, tint);
        drawText(st.story_note, 16, H-30, 16, C4(0.8f,0.85f,1.0f,1));

        ren->setNode(NODE_STORE);
        if (!storeOpen) drawStoreIcon();
        if (!storeOpen && !inGameMenuOpen_) drawMenuIcon();
        storeBtnRects_.clear();
        drawStoreToast();
        drawGamepad();
        drawStylingSpikeBackdrop();
        if (stairsConfirmOpen_) drawStairsConfirmDialog();
        if (inGameMenuOpen_) drawInGameMenu();
        ren->end();
        return;
    }

    // ---- battle scene ----
    if (showBattle) {
        ren->setNode(NODE_BATTLE);
        drawFocusSplash();
        float cx = W/2 - 250;
        drawText(locale_.tr("battle.title") + " " + cs.enemy.name, cx, 120, 26, C4(1,0.6f,0.4f,1));
        ren->drawSprite(spriteQuad(cx, 170, 96, 96, spriteLayer("player"), white));
        ren->drawSprite(spriteQuad(cx+350, 170, 96, 96, spriteLayer(cs.enemy.boss?"boss_demonlord":entSprite(cs.enemy.id)), white));
        drawBar(cx, 280, 200, 16, (float)cs.playerHP/pl.maxhp, C4(0.3f,0.9f,0.4f,1));
        drawText(locale_.tr("battle.you") + " HP " + std::to_string(cs.playerHP), cx+210, 280, 18, tint);
        drawBar(cx+350, 280, 200, 16, (float)std::max(0,cs.enemyHP)/cs.enemy.hp, C4(0.9f,0.3f,0.3f,1));
        drawText(cs.enemy.name + " HP " + std::to_string(std::max(0,cs.enemyHP)), cx+560, 280, 18, tint);
        drawText(cs.log, cx, 320, 18, tint);
        // Battle System v2 (docs/BATTLE_SYSTEM_V2_PROPOSALS.md): the Attack and Defense bars move
        // on their own, continuously and independently, all the time -- there's no "your turn" to
        // display, just wherever each marker currently is. A tap (handleTouch/battleTapAttack/
        // battleTapDefense) resolves instantly from that live position, so unlike the old
        // press-and-hold model there is nothing here to compute from an accumulated duration.
        if (cs.active) {
            // The enemy's own real-time clock -- how close it is to its next attack, completely
            // independent of either bar below.
            float enemyFrac = (float)cs.enemyClockMs / (float)std::max(500, cs.enemy.atkIntervalMs);
            drawText(locale_.tr("battle.enemy_clock"), cx, 338, 14, C4(0.9f, 0.75f, 0.5f, 1));
            drawBar(cx, 353, 500, 10, enemyFrac, C4(0.85f, 0.45f, 0.2f, 1));

            float colW = 230.0f, atkX = cx, defX = cx + colW + 40.0f;
            auto drawAutoBar = [&](float x, CombatState::AutoBar& bar, const toms::PowerBarParams& params,
                                    int rect[4], const std::string& promptKey, const std::string& idleLabelKey) {
                drawText(locale_.tr(promptKey), x, 372, 15, C4(0.9f, 0.9f, 1.0f, 1));
                drawPowerBar(x, 390, colW, 20, params, bar.pos);
                if (bar.cooling) {
                    Quad dim; dim.rect[0]=x; dim.rect[1]=390; dim.rect[2]=colW; dim.rect[3]=20;
                    dim.uv[0]=0; dim.uv[1]=0; dim.uv[2]=1; dim.uv[3]=1; dim.solid=true;
                    dim.tint[0]=0.08f; dim.tint[1]=0.08f; dim.tint[2]=0.1f; dim.tint[3]=0.55f;
                    ren->drawSprite(dim);
                }
                float bY = 414, bH = 44;
                rect[0]=(int)x; rect[1]=(int)bY; rect[2]=(int)colW; rect[3]=(int)bH;
                Quad cb; cb.rect[0]=x; cb.rect[1]=bY; cb.rect[2]=colW; cb.rect[3]=bH;
                cb.uv[0]=0; cb.uv[1]=0; cb.uv[2]=1; cb.uv[3]=1; cb.solid=true;
                if (bar.cooling) { cb.tint[0]=0.16f; cb.tint[1]=0.18f; cb.tint[2]=0.22f; cb.tint[3]=1; }
                else             { cb.tint[0]=0.24f; cb.tint[1]=0.32f; cb.tint[2]=0.46f; cb.tint[3]=1; }
                ren->drawSprite(cb);
                drawText(bar.cooling ? locale_.tr("battle.btn_charging") : locale_.tr(idleLabelKey),
                          x + 12, bY + 14, 14, C4(1,1,1,1));
            };
            drawAutoBar(atkX, cs.atkBar, toms::effectiveAttackBar(equipped_, equipmentDefs_),
                        atkBtnRect_, "battle.attack_prompt", "battle.btn_hold_attack");
            drawAutoBar(defX, cs.defBar, toms::effectiveDefenseBar(equipped_, equipmentDefs_),
                        defBtnRect_, "battle.defense_prompt", "battle.btn_hold_defense");

            std::string shieldTxt = cs.shieldBanked
                ? trParam(locale_.tr("battle.shield_armed"), "pct", std::to_string((int)std::lround(cs.shieldPower)))
                : locale_.tr("battle.shield_none");
            drawText(shieldTxt, defX, 462, 14, cs.shieldBanked ? C4(0.55f,0.75f,1.0f,1) : C4(0.6f,0.63f,0.7f,1));

            // Super-Attack Gauge (§4): a row of dots (filled = one banked charge toward a free,
            // no-timing-required strong hit) plus a button that only appears once full. Tapping
            // it doesn't touch either bar's own cooldown above.
            drawText(locale_.tr("battle.super_label"), cx, 486, 14, C4(0.9f, 0.9f, 1.0f, 1));
            float dotX = cx + 110, dotY = 486, dotSz = 12, dotGap = 6;
            for (int i = 0; i < CombatState::kSuperThreshold; i++) {
                Quad d; d.rect[0]=dotX + i*(dotSz+dotGap); d.rect[1]=dotY; d.rect[2]=dotSz; d.rect[3]=dotSz;
                d.uv[0]=0; d.uv[1]=0; d.uv[2]=1; d.uv[3]=1; d.solid=true;
                if (i < cs.superCharge) { d.tint[0]=1.0f; d.tint[1]=0.6f; d.tint[2]=0.25f; d.tint[3]=1; }
                else                    { d.tint[0]=0.2f; d.tint[1]=0.2f; d.tint[2]=0.25f; d.tint[3]=1; }
                ren->drawSprite(d);
            }
            bool superReady = cs.superCharge >= CombatState::kSuperThreshold;
            if (superReady) {
                float sX = cx, sY = 508, sW = 500, sH = 36;
                superBtnRect_[0]=(int)sX; superBtnRect_[1]=(int)sY; superBtnRect_[2]=(int)sW; superBtnRect_[3]=(int)sH;
                Quad sb; sb.rect[0]=sX; sb.rect[1]=sY; sb.rect[2]=sW; sb.rect[3]=sH;
                sb.uv[0]=0; sb.uv[1]=0; sb.uv[2]=1; sb.uv[3]=1; sb.solid=true;
                sb.tint[0]=0.7f; sb.tint[1]=0.25f; sb.tint[2]=0.2f; sb.tint[3]=1;
                ren->drawSprite(sb);
                drawText(locale_.tr("battle.super_ready"), sX + 150, sY + 8, 16, C4(1,1,1,1));
            } else {
                superBtnRect_[0]=superBtnRect_[1]=superBtnRect_[2]=superBtnRect_[3]=0;
            }
        } else {
            drawText(locale_.tr("battle.continue_hint"), cx, 350, 16, C4(1,1,0.6f,1));
        }
        drawStylingSpikeBackdrop();
        ren->end();
        return;
    }

    // ---- dialogue scene ----
    if (showTalk) {
        ren->setNode(NODE_TALK);
        drawFocusSplash();
        Quad box; box.rect[0]=40; box.rect[1]=H-200; box.rect[2]=W-80; box.rect[3]=170;
        box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
        box.tint[0]=0.1f;box.tint[1]=0.12f;box.tint[2]=0.2f;box.tint[3]=0.95f; ren->drawSprite(box);
        std::string txt = locale_.field(dlgData["nodes"][dlgNode]["text"]);
        drawText(txt, 60, H-180, 20, tint);
        for (size_t i = 0; i < dlgChoices.size(); i++) {
            float ty = H-140 + (float)i*24;
            if ((int)i == dlgSel) drawText("▶ " + dlgChoices[i].label, 60, ty, 18, C4(1,1.0f,0.6f,1));
            else                   drawText("  " + dlgChoices[i].label, 60, ty, 18, C4(1,0.9f,0.5f,1));
        }
        drawStylingSpikeBackdrop();
        ren->end();
        return;
    }

    // ---- inventory scene ----
    if (showInv) {
        ren->setNode(NODE_CHAR);
        drawInventory();
        drawStylingSpikeBackdrop();
        ren->end();
        return;
    }

    // ---- store scene ----
    ren->setNode(NODE_STORE);
    if (!storeOpen) drawStoreIcon();
    storeBtnRects_.clear();
    if (storeUnlockDlg) drawStoreUnlockDialog();
    else if (storeOpen) drawStoreUI();
    drawStoreToast();
    drawGamepad();

    drawStylingSpikeBackdrop();
    ren->end();
}

void Game::drawGamepad() {
    // Hide the whole gamepad (including its P toggle) while a top-layer UI is
    // active (battle / dialogue / inventory / store). Those UIs are driven by
    // direct touch/keyboard on their own elements, so the on-canvas D-pad + A
    // would just clutter the screen and could be mis-tapped.
    if (modalActive() || cs.won) return;
    ren->setNode(NODE_CHAR);
    float t[4] = {1,1,1,1};
    // P toggle button (drawn in normal play; modals already returned above).
    {
        const GPadBtn& b = GP[7];
        Quad q; q.rect[0]=b.x; q.rect[1]=b.y; q.rect[2]=b.w; q.rect[3]=b.h;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=b.col[0]; q.tint[1]=b.col[1]; q.tint[2]=b.col[2]; q.tint[3]=b.col[3];
        ren->drawSprite(q);
        drawText(b.label, b.x + b.w/2 - 9, b.y + b.h/2 - 13, 28, t);
    }
    if (!gpOn) return;                 // gamepad hidden: only the toggle remains
    for (int i = 0; i < 7; i++) {
        if ((i == 5 || i == 6) && !inventoryOpen()) continue;  // B/drop + I/close only show inside inventory
        const GPadBtn& b = GP[i];
        Quad q; q.rect[0]=b.x; q.rect[1]=b.y; q.rect[2]=b.w; q.rect[3]=b.h;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=b.col[0]; q.tint[1]=b.col[1]; q.tint[2]=b.col[2]; q.tint[3]=b.col[3];
        ren->drawSprite(q);
        drawText(b.label, b.x + b.w/2 - 11, b.y + b.h/2 - 13, 30, t);
    }
}

// ---------------------------------------------------------------------------------------------
// ImGui UI, part 1 -- compiled on BOTH desktop and web (M2: ImGui is wired into the Vulkan, WebGL2
// and WebGPU backends): the font-scale setter, the F1 debug overlay and the F2 styling spike. On
// the browser build the backend is src/engine/imgui_web.{h,cpp} (OpenGL3/ES3 + an Emscripten DOM
// event bridge); on desktop it is imgui_layer.cpp (GLFW + Vulkan). Nothing here touches gameplay
// state a player sees unless they press F1/F2, so the browser build behaves exactly as before.
void Game::applyUiSettings() {
    ImGui::GetIO().FontGlobalScale = uiFontScale_;
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
    float w = 420, h = 260;
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

// ImGui UI, part 2 -- still DESKTOP-ONLY (deliberate, see imgui_web.h's scope note): the toast
// windows and the ImGui stage-select window. Both draw locale strings (CJK), and ImGui's built-in
// font has no CJK glyphs -- they would render as tofu boxes -- while the browser build already has
// its own stage-select UI and toast drawing in the game renderer. Enabling these on web would
// change what a browser player sees, which the M2 scope explicitly avoids.
#ifndef __EMSCRIPTEN__

// Milestone 5: transient toast notifications, stacked top-right, each with its own fade-free
// fixed 3-second lifetime (see pushNotification()/update()'s countdown). Purely additive --
// nothing else reads/depends on this window.
void Game::drawNotifications() {
    if (notifications_.empty() || !ren) return;
    float W = (float)ren->width();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_AlwaysAutoResize;
    float y = 90.0f;   // below the HUD stat block and store icon
    for (size_t i = 0; i < notifications_.size(); i++) {
        ImGui::SetNextWindowPos(ImVec2(W - 260.0f, y), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.14f, 0.10f, 0.9f));
        std::string winId = "##toast" + std::to_string(i);
        ImGui::Begin(winId.c_str(), nullptr, flags);
        ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.6f, 1.0f), "%s", notifications_[i].first.c_str());
        ImGui::End();
        ImGui::PopStyleColor();
        y += 48.0f;
    }
}

// Milestone 5: the Stage Select hub -- every floor the player has ever reached is individually
// selectable; a locked stage shows why (architecture-doc §10). Tab to open (see main.cpp),
// blocks background input while open (modalActive() includes stageSelectOpen_).
void Game::drawStageSelect() {
    if (!ren) return;
    float W = (float)ren->width(), H = (float)ren->height();
    ImGui::SetNextWindowPos(ImVec2(W * 0.5f, H * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(W - 120.0f, H - 100.0f), ImGuiCond_Always);
    ImGui::Begin(locale_.tr("stageselect.title").c_str(), nullptr,
                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::TextWrapped("%s", locale_.tr("stageselect.hint").c_str());
    ImGui::Separator();
    // UI settings: font size, applied globally via applyUiSettings() (called every frame from
    // main.cpp). In-memory only for now -- see uiFontScale_'s declaration in game.h for why.
    if (ImGui::CollapsingHeader(locale_.tr("stageselect.ui_settings_header").c_str())) {
        ImGui::SliderFloat(locale_.tr("stageselect.font_size_label").c_str(), &uiFontScale_, 0.5f, 2.5f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::Button(locale_.tr("stageselect.reset_button").c_str())) uiFontScale_ = 1.5f;
    }
    ImGui::Separator();
    for (size_t i = 0; i < stageList_.size(); i++) {
        const StageInfo& info = stageList_[i];
        bool reached = std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), info.id) != meta_.unlockedStages.end();
        bool locked = false;
        if (info.index > 1 && i > 0) {
            const StageInfo& prev = stageList_[i - 1];
            locked = std::find(meta_.unlockedStages.begin(), meta_.unlockedStages.end(), prev.id) == meta_.unlockedStages.end();
        }
        bool isNew = !locked && !reached;

        ImGui::PushID((int)i);
        ImGui::BeginDisabled(locked);
        std::string label = std::to_string(info.index) + ". " + info.name;
        if (reached) label += "  " + locale_.tr("stageselect.reached_tag");
        if (isNew)   label += "  " + locale_.tr("stageselect.new_tag");
        if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) {
            loadStage(info.id);
            closeStageSelect();
        }
        ImGui::EndDisabled();
        // ImGuiHoveredFlags_AllowWhenDisabled: BeginDisabled() suppresses hover reporting by
        // default, so the lock-reason tooltip needs this explicit override to show at all.
        if (locked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", trParam(locale_.tr("stageselect.locked_hint"), "floor", std::to_string(info.index - 1)).c_str());
        // Milestone 8: recommended-stats preview text, shown under every row that has one
        // (locked rows too -- it's exactly the info a player needs to decide whether to go grind
        // first, per the roadmap's own "Stage Select preview text (recommended stats)" ask).
        if (!info.preview.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.8f, 0.9f, 0.85f));
            ImGui::TextWrapped("  %s", info.preview.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// Draws the backdrop rect for the M2 styling spike (see drawStylingSpike() above). Declared
// unguarded so Game::draw() -- shared between the desktop and web builds -- can call it
// unconditionally: stylingSpikeVisible_ can only ever become true via the desktop-only
// setStylingSpikeVisible()/drawStylingSpike(), so this is a correct no-op on the web build.
#endif  // !__EMSCRIPTEN__ (native-only ImGui UI above; no stubs -- the browser
        // build simply has no F1 overlay / spike / ImGui stage select)

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

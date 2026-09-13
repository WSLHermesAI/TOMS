// game_store.cpp — the shop and the pause menu: store data/UI/purchase, stage select,
// stairs confirm, and the in-game menu (save / settings / back to title). Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

// ---------- store system ----------
void Game::loadStore(const std::string& assetDir) {
    nlohmann::json j = readJsonFile(assetDir + "/../data/store.json");
    if (j.is_null() || j.empty()) { return; }
    if (j.contains("store_title")) storeTitle_ = j["store_title"];
    if (j.contains("unlockstage")) storeUnlockStage_ = j["unlockstage"].get<int>();
    for (auto& it : j["items"]) {
        StoreItemDef d;
        d.id = it.value("id", "");
        d.name = it.contains("name") ? it["name"] : nlohmann::json(d.id);
        d.sprite = it.value("sprite", "");
        // strip a trailing ".png" if present so it matches the atlas sprite id
        if (d.sprite.size() > 4 && d.sprite.substr(d.sprite.size()-4) == ".png")
            d.sprite = d.sprite.substr(0, d.sprite.size()-4);
        d.icon_path = it.value("icon_path", "");
        d.desc = it.contains("desc") ? it["desc"] : nlohmann::json("");
        if (it.contains("effect")) d.effect = it["effect"];
        d.effect_text = it.contains("effect_text") ? it["effect_text"] : nlohmann::json("");
        d.cost_base = it.value("cost_base", 2);
        d.cost_multiplier = it.value("cost_multiplier", 2);
        d.purchases = 0;
        // Milestone 8: an equipment-selling entry names its item instead of carrying an `effect`.
        d.equipmentId = it.value("equipmentId", std::string());
        storeItems_.push_back(d);
    }
}

void Game::openStore() {
    if (!storeUnlocked_) return;
    storeOpen = true;
    storeSel_ = 0;
    audio.play("confirm_click");
}

void Game::closeStore() {
    storeOpen = false;
    audio.play("close_ui");
}

// Milestone 5: scans data/stages/ once for every stage's id/name/index, so the Stage Select hub
// can list all of them (locked or not) without re-parsing JSON every frame.
void Game::ensureStageListLoaded() {
    if (stageListLoaded_) return;
    stageListLoaded_ = true;
    std::string dir = dataDir + "/../data/stages/";
    if (!std::filesystem::exists(dir)) return;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        try {
            nlohmann::json j = readJsonFile(e.path().string());
            if (j.is_null() || !j.contains("id")) continue;
            StageInfo info;
            info.id = j.value("id", std::string());
            info.name = j.contains("name") ? locale_.field(j["name"]) : info.id;
            info.index = j.value("index", 0);
            info.fileStem = e.path().stem().string();
            // Milestone 8: recommended-stats blurb, authored per stage JSON's optional top-level
            // "preview" string -- empty for a file that doesn't set one (none did before M8).
            info.preview = j.contains("preview") ? locale_.field(j["preview"]) : std::string();
            if (!info.id.empty()) stageList_.push_back(info);
        } catch (...) {}
    }
    std::sort(stageList_.begin(), stageList_.end(),
              [](const StageInfo& a, const StageInfo& b) { return a.index < b.index; });
}

void Game::openStageSelect() {
    ensureStageListLoaded();
    stageSelectOpen_ = true;
    audio.play("confirm_click");
}

void Game::closeStageSelect() {
    stageSelectOpen_ = false;
    audio.play("close_ui");
}

void Game::storeCardRects(std::vector<float>& rects, int n) const {
    rects.clear();
    float W = (float)ren->width(), H = (float)ren->height();
    float cw = 300, ch = 300, gap = 24;
    float totalW = n * cw + (n - 1) * gap;
    float ox = (W - totalW) / 2.0f;
    float oy = (H - ch) / 2.0f - 10;
    for (int i = 0; i < n; i++) {
        rects.push_back(ox + i * (cw + gap));
        rects.push_back(oy);
        rects.push_back(cw);
        rects.push_back(ch);
    }
}

void Game::drawStoreIcon() {
    float W = (float)ren->width();
    // top-right of the HUD bar (after GOLD/EXP text). Put it at x = W-60.
    int x = (int)(W - 56), y = 14, s = 42;
    storeIconRect[0] = x; storeIconRect[1] = y; storeIconRect[2] = s; storeIconRect[3] = s;
    // background panel
    Quad bg; bg.rect[0]=x-4; bg.rect[1]=y-4; bg.rect[2]=s+8; bg.rect[3]=s+8;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    if (storeUnlocked_) { bg.tint[0]=0.16f; bg.tint[1]=0.22f; bg.tint[2]=0.34f; bg.tint[3]=1; }
    else { bg.tint[0]=0.12f; bg.tint[1]=0.12f; bg.tint[2]=0.14f; bg.tint[3]=0.5f; } // locked = dim
    ren->drawSprite(bg);
    // icon: a shop/coin sprite. Use SP_GOLD (coin) as the store glyph.
    ren->drawSprite(spriteQuad((float)x, (float)y, (float)s, (float)s, spriteLayer("coin"), C4(1,1,1, storeUnlocked_?1.0f:0.4f)));
    // label
    drawText(locale_.tr("store.icon_label") + (storeUnlocked_ ? "" : "🔒"), (float)x-4, (float)y + s + 2, 12, C4(1,1,1, storeUnlocked_?1:0.4f));
}

void Game::drawStoreUnlockDialog() {
    float W = (float)ren->width(), H = (float)ren->height();
    drawStoreSplash();
    // centered dialog box
    float bw = 420, bh = 180;
    float bx = (W - bw)/2, by = (H - bh)/2;
    Quad box; box.rect[0]=bx; box.rect[1]=by; box.rect[2]=bw; box.rect[3]=bh;
    box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
    box.tint[0]=0.12f;box.tint[1]=0.16f;box.tint[2]=0.26f;box.tint[3]=0.97f; ren->drawSprite(box);
    drawText(locale_.tr("store.unlocked_title"), bx+30, by+34, 24, C4(1,0.9f,0.5f,1));
    drawText(locale_.tr("store.unlocked_body"), bx+30, by+74, 15, C4(1,1,1,1));
    // confirm button (bottom-right of box)
    float btnW = 120, btnH = 40;
    float btnX = bx + bw - btnW - 24, btnY = by + bh - btnH - 20;
    storeUnlockBtnRect_[0] = (int)btnX; storeUnlockBtnRect_[1] = (int)btnY;
    storeUnlockBtnRect_[2] = (int)btnW; storeUnlockBtnRect_[3] = (int)btnH;
    Quad btn; btn.rect[0]=btnX; btn.rect[1]=btnY; btn.rect[2]=btnW; btn.rect[3]=btnH;
    btn.uv[0]=0;btn.uv[1]=0;btn.uv[2]=1;btn.uv[3]=1; btn.solid=true;
    btn.tint[0]=0.2f;btn.tint[1]=0.5f;btn.tint[2]=0.3f;btn.tint[3]=1; ren->drawSprite(btn);
    drawText(locale_.tr("store.confirm_hint"), btnX+12, btnY+11, 15, C4(1,1,1,1));
}

// Milestone 9: confirm-before-transition dialog (see stairsConfirmOpen_'s declaration
// in game.h). Drawn as an overlay on top of the walking scene, not its own scene --
// the map/HUD stay visible (dimmed) behind it, unlike the store dialogs' own splash.
void Game::drawStairsConfirmDialog() {
    float W = (float)ren->width(), H = (float)ren->height();
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.55f; ren->drawSprite(dim);

    float bw = 380, bh = 160;
    float bx = (W-bw)/2, by = (H-bh)/2;
    Quad box; box.rect[0]=bx; box.rect[1]=by; box.rect[2]=bw; box.rect[3]=bh;
    box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
    box.tint[0]=0.12f;box.tint[1]=0.16f;box.tint[2]=0.26f;box.tint[3]=0.97f; ren->drawSprite(box);

    drawText(stairsConfirmIsUp_ ? locale_.tr("stairs.confirm_up") : locale_.tr("stairs.confirm_down"), bx+30, by+30, 22, C4(1,0.95f,0.6f,1));

    float btnW=130, btnH=40, gap=20;
    float by2 = by+bh-btnH-24;
    float yesX = bx + (bw-(btnW*2+gap))/2, noX = yesX+btnW+gap;
    stairsConfirmYesRect_[0]=(int)yesX; stairsConfirmYesRect_[1]=(int)by2; stairsConfirmYesRect_[2]=(int)btnW; stairsConfirmYesRect_[3]=(int)btnH;
    stairsConfirmNoRect_[0]=(int)noX;   stairsConfirmNoRect_[1]=(int)by2; stairsConfirmNoRect_[2]=(int)btnW; stairsConfirmNoRect_[3]=(int)btnH;

    Quad yes; yes.rect[0]=yesX; yes.rect[1]=by2; yes.rect[2]=btnW; yes.rect[3]=btnH;
    yes.uv[0]=0;yes.uv[1]=0;yes.uv[2]=1;yes.uv[3]=1; yes.solid=true;
    yes.tint[0]=0.2f;yes.tint[1]=0.5f;yes.tint[2]=0.3f;yes.tint[3]=1; ren->drawSprite(yes);
    drawText(locale_.tr("menu.yes") + " (Enter)", yesX+18, by2+11, 16, C4(1,1,1,1));

    Quad no; no.rect[0]=noX; no.rect[1]=by2; no.rect[2]=btnW; no.rect[3]=btnH;
    no.uv[0]=0;no.uv[1]=0;no.uv[2]=1;no.uv[3]=1; no.solid=true;
    no.tint[0]=0.5f;no.tint[1]=0.2f;no.tint[2]=0.2f;no.tint[3]=1; ren->drawSprite(no);
    drawText(locale_.tr("menu.no") + " (Esc)", noX+22, by2+11, 16, C4(1,1,1,1));
}

// ---------- in-game menu (walking-phase HUD gear icon) ----------
// Save / Settings (language) / Back to Title. A lighter-weight sibling of the title's own
// Menu/Settings flow, built directly as Game state to match every other in-game modal here.

void Game::drawMenuIcon() {
    float W = (float)ren->width();
    // Left of the store icon (store sits at x = W-56) so the two HUD buttons sit side by side
    // without overlapping, same y/size as drawStoreIcon().
    int x = (int)(W - 112), y = 14, s = 42;
    menuIconRect_[0] = x; menuIconRect_[1] = y; menuIconRect_[2] = s; menuIconRect_[3] = s;
    Quad bg; bg.rect[0]=x-4; bg.rect[1]=y-4; bg.rect[2]=s+8; bg.rect[3]=s+8;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.16f; bg.tint[1]=0.16f; bg.tint[2]=0.20f; bg.tint[3]=1;
    ren->drawSprite(bg);
    // No dedicated gear sprite exists in the atlas -- reuse the coin sprite's slot would be
    // confusing (already the store icon), so draw a plain glyph label instead, same as other
    // icon-less HUD buttons in this file.
    drawText("=", (float)x + s*0.5f - 6.0f, (float)y + 6.0f, 26, C4(1,1,1,1));
    drawText(locale_.tr("ingame_menu.title"), (float)x-10, (float)y + s + 2, 12, C4(1,1,1,1));
}

void Game::openInGameMenu() {
    inGameMenuOpen_ = true;
    inGameMenuPage_ = InGameMenuPage::Main;
    inGameMenuSel_ = 0;
    inGameLangConfirmOpen_ = false;
    audio.play("confirm_click");
}

void Game::closeInGameMenu() {
    inGameMenuOpen_ = false;
    inGameMenuPage_ = InGameMenuPage::Main;
    inGameLangConfirmOpen_ = false;
    audio.play("close_ui");
}

void Game::inGameMenuMove(int delta) {
    if (delta == 0 || !inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) { inGameLangConfirmYes_ = !inGameLangConfirmYes_; return; }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        int n = 3;   // Save / Settings / Back to Title
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    } else {
        int n = std::max(1, locale_.languageCount() + 1);   // languages + the camera toggle row
        inGameMenuSel_ = ((inGameMenuSel_ + delta) % n + n) % n;
    }
}

void Game::inGameMenuActivate() {
    if (!inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) {
        const bool yes = inGameLangConfirmYes_;
        const int idx = inGameLangConfirmIdx_;
        inGameLangConfirmOpen_ = false;
        if (yes) { title_.setLanguageIndex(idx); applyLanguage(); }
        audio.play("confirm_click");
        return;
    }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        switch (inGameMenuSel_) {
            case 0:   // Save
                saveCurrentRun();
                toastMsg_ = locale_.tr("save.saved");
                toastTimer_ = 1600;
                audio.play("confirm_click");
                break;
            case 1:   // Settings
                inGameMenuPage_ = InGameMenuPage::Settings;
                inGameMenuSel_ = locale_.languageIndex();
                audio.play("confirm_click");
                break;
            case 2:   // Back to Title
                returnToTitle();
                break;
        }
    } else if (inGameMenuSel_ >= 0 && inGameMenuSel_ < locale_.languageCount()) {
        // Settings page: activating a language row opens the "switch to XXX?" dialog,
        // mirroring the title screen's own confirm dialog (see AskLanguageChange).
        inGameLangConfirmOpen_ = true;
        inGameLangConfirmIdx_ = inGameMenuSel_;
        inGameLangConfirmYes_ = true;
        audio.play("confirm_click");
    } else if (inGameMenuSel_ == locale_.languageCount()) {
        // The camera row (always last): cycles instantly, no confirm dialog needed -- unlike
        // language, this is a low-stakes preference whose effect the player already sees behind
        // this very menu (the maze keeps scrolling/paging under the scrim).
        setCameraModeIndex(1 - cameraModeIndex());
        audio.play("confirm_click");
    }
}

void Game::inGameMenuBack() {
    if (!inGameMenuOpen_) return;
    if (inGameLangConfirmOpen_) { inGameLangConfirmOpen_ = false; audio.play("close_ui"); return; }
    if (inGameMenuPage_ == InGameMenuPage::Settings) {
        inGameMenuPage_ = InGameMenuPage::Main;
        inGameMenuSel_ = 1;   // land back on the "Settings" row
        audio.play("close_ui");
        return;
    }
    closeInGameMenu();
}

void Game::inGameMenuClick(float x, float y) {
    if (!inGameMenuOpen_) return;
    auto hit = [&](const int r[4]) { return x>=r[0] && x<=r[0]+r[2] && y>=r[1] && y<=r[1]+r[3]; };
    if (inGameLangConfirmOpen_) {
        if (hit(igmLangYesRect_)) { inGameLangConfirmYes_ = true; inGameMenuActivate(); }
        else if (hit(igmLangNoRect_)) { inGameLangConfirmYes_ = false; inGameMenuActivate(); }
        return;   // the dialog swallows all other taps
    }
    if (inGameMenuPage_ == InGameMenuPage::Main) {
        if (hit(igmSaveRect_)) { inGameMenuSel_ = 0; inGameMenuActivate(); return; }
        if (hit(igmSettingsRect_)) { inGameMenuSel_ = 1; inGameMenuActivate(); return; }
        if (hit(igmBackToTitleRect_)) { inGameMenuSel_ = 2; inGameMenuActivate(); return; }
        if (hit(igmCloseRect_)) { closeInGameMenu(); return; }
    } else {
        int n = std::min((int)igmLangRowRects_.size()/4, locale_.languageCount());
        for (int i = 0; i < n; i++) {
            const float* r4 = &igmLangRowRects_[i*4];
            int r[4] = {(int)r4[0], (int)r4[1], (int)r4[2], (int)r4[3]};
            if (hit(r)) { inGameMenuSel_ = i; inGameMenuActivate(); return; }
        }
        if (hit(igmCameraRowRect_)) { inGameMenuSel_ = locale_.languageCount(); inGameMenuActivate(); return; }
        if (hit(igmBackRect_)) { inGameMenuBack(); return; }
    }
}

// Saves, tears down transient modal/run-in-progress state, and reopens the title -- the
// inverse of newGame()/applyLoadedRun() (see their resets for what this mirrors). Unlike
// newGame(), this deliberately leaves pl/meta_/entityStatus_/activeSlot_/curStage alone: the
// run is only SUSPENDED, not discarded, so Continue can pick it back up.
void Game::returnToTitle() {
    saveCurrentRun();   // flush before leaving so nothing is lost
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    closeInGameMenu();
    missionTrackers_.clear();
    notifications_.clear();
    title_.setRunInProgress(false);
    title_.open();
    refreshSlots();
}

void Game::drawInGameMenu() {
    if (!ren || !inGameMenuOpen_) return;
    const float W = (float)ren->width(), H = (float)ren->height();
    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};
    static const float white[4] = {1,1,1,1};

    Quad scrim;
    scrim.rect[0]=0; scrim.rect[1]=0; scrim.rect[2]=W; scrim.rect[3]=H;
    scrim.uv[0]=0;scrim.uv[1]=0;scrim.uv[2]=1;scrim.uv[3]=1; scrim.solid=true;
    scrim.tint[0]=0; scrim.tint[1]=0; scrim.tint[2]=0; scrim.tint[3]=0.62f;
    ren->drawSprite(scrim);

    auto panel = [&](float x, float y, float w, float h) {
        Quad p; p.rect[0]=x; p.rect[1]=y; p.rect[2]=w; p.rect[3]=h;
        p.uv[0]=0;p.uv[1]=0;p.uv[2]=1;p.uv[3]=1; p.solid=true;
        p.tint[0]=0.13f; p.tint[1]=0.14f; p.tint[2]=0.21f; p.tint[3]=0.98f;
        ren->drawSprite(p);
        Quad rule; rule.rect[0]=x; rule.rect[1]=y; rule.rect[2]=w; rule.rect[3]=3.0f;
        rule.uv[0]=0;rule.uv[1]=0;rule.uv[2]=1;rule.uv[3]=1; rule.solid=true;
        rule.tint[0]=gold[0]; rule.tint[1]=gold[1]; rule.tint[2]=gold[2]; rule.tint[3]=0.9f;
        ren->drawSprite(rule);
    };
    auto button = [&](int rectOut[4], float x, float y, float w, float h,
                       const std::string& label, bool selected) {
        rectOut[0]=(int)x; rectOut[1]=(int)y; rectOut[2]=(int)w; rectOut[3]=(int)h;
        Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=h;
        q.uv[0]=0;q.uv[1]=0;q.uv[2]=1;q.uv[3]=1; q.solid=true;
        if (selected) { q.tint[0]=0.24f; q.tint[1]=0.30f; q.tint[2]=0.44f; q.tint[3]=1; }
        else          { q.tint[0]=0.16f; q.tint[1]=0.17f; q.tint[2]=0.24f; q.tint[3]=0.96f; }
        ren->drawSprite(q);
        drawText(label, x + 18, y + (h-20)*0.5f, 18, white);
    };

    if (inGameLangConfirmOpen_) {
        const float bw=560, bh=210, bx=(W-bw)*0.5f, by=(H-bh)*0.5f;
        panel(bx, by, bw, bh);
        const auto& langs = locale_.languages();
        int idx = inGameLangConfirmIdx_;
        std::string langName = (idx >= 0 && idx < (int)langs.size()) ? langs[idx].name : "";
        std::string question = trParam(locale_.tr("settings.language_confirm"), "lang", langName);
        drawText(question, bx + (bw - measureText(question, 24)) * 0.5f, by + 70.0f, 24, gold);
        const float btnW=160, btnH=56, gap=40, groupW=btnW*2+gap;
        const float bx0 = bx + (bw-groupW)*0.5f, byy = by + bh - btnH - 26.0f;
        const bool yes = inGameLangConfirmYes_;
        button(igmLangYesRect_, bx0, byy, btnW, btnH, locale_.tr("menu.yes"), yes);
        button(igmLangNoRect_,  bx0+btnW+gap, byy, btnW, btnH, locale_.tr("menu.no"), !yes);
        return;
    }

    if (inGameMenuPage_ == InGameMenuPage::Main) {
        const float bw=420, bh=380, bx=(W-bw)*0.5f, by=(H-bh)*0.5f;
        panel(bx, by, bw, bh);
        const std::string head = locale_.tr("ingame_menu.title");
        drawText(head, bx + (bw - measureText(head, 28)) * 0.5f, by + 28.0f, 28, gold);
        const float rw = bw - 60, rh = 64, rx = bx + 30, gap = 16;
        float ry = by + 90;
        button(igmSaveRect_, rx, ry, rw, rh, locale_.tr("ingame_menu.save"), inGameMenuSel_ == 0);
        ry += rh + gap;
        button(igmSettingsRect_, rx, ry, rw, rh, locale_.tr("menu.settings"), inGameMenuSel_ == 1);
        ry += rh + gap;
        button(igmBackToTitleRect_, rx, ry, rw, rh, locale_.tr("ingame_menu.back_to_title"), inGameMenuSel_ == 2);
        const float cw=140, ch=48, cx=bx+(bw-cw)*0.5f, cy=by+bh-ch-20;
        button(igmCloseRect_, cx, cy, cw, ch, locale_.tr("inventory.close"), false);
    } else {
        const auto& langs = locale_.languages();
        int n = std::max(1, (int)langs.size());
        const float rh = 48, gap = 10;
        // +1 row for the camera-mode toggle, always last, below the language list.
        const float bw = 420, bh = 130.0f + (n + 1) * (rh + gap) + 70.0f;
        const float bx = (W-bw)*0.5f, by = (H-bh)*0.5f;
        panel(bx, by, bw, bh);
        const std::string head = locale_.tr("settings.header");
        drawText(head, bx + (bw - measureText(head, 26)) * 0.5f, by + 26.0f, 26, gold);
        const std::string langLabel = locale_.tr("settings.language");
        drawText(langLabel, bx + (bw - measureText(langLabel, 16)) * 0.5f, by + 62.0f, 16, dim);
        const float rw = bw - 60, rx = bx + 30;
        float ry = by + 96;
        igmLangRowRects_.assign((size_t)n * 4, 0.0f);
        int scratch[4];   // button() wants an int[4] out-param; the real rect is stored below
        for (int i = 0; i < (int)langs.size(); i++) {
            const bool active = (i == locale_.languageIndex());
            std::string label = std::string(active ? "[x] " : "[ ] ") + langs[i].name;
            button(scratch, rx, ry, rw, rh, label, inGameMenuSel_ == i);
            igmLangRowRects_[i*4+0] = rx; igmLangRowRects_[i*4+1] = ry;
            igmLangRowRects_[i*4+2] = rw; igmLangRowRects_[i*4+3] = rh;
            ry += rh + gap;
        }
        // Camera-mode toggle: a single row that cycles on tap/Enter, no confirm dialog (see
        // inGameMenuActivate()'s handling of inGameMenuSel_ == languageCount()).
        std::string camLabel = (cam_.mode() == toms::Camera::Mode::Rooms)
                              ? locale_.tr("settings.camera_rooms") : locale_.tr("settings.camera_follow");
        button(igmCameraRowRect_, rx, ry, rw, rh, camLabel, inGameMenuSel_ == n);
        ry += rh + gap;
        const float cw=140, ch=44, cx=bx+(bw-cw)*0.5f;
        button(igmBackRect_, cx, ry + 10, cw, ch, locale_.tr("menu.back"), false);
    }
}

void Game::drawStoreUI() {
    float W = (float)ren->width(), H = (float)ren->height();
    drawStoreSplash();
    // panel background
    Quad bg; bg.rect[0]=40; bg.rect[1]=40; bg.rect[2]=W-80; bg.rect[3]=H-80;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.08f;bg.tint[1]=0.1f;bg.tint[2]=0.16f;bg.tint[3]=0.96f; ren->drawSprite(bg);
    // title + gold
    drawText(locale_.field(storeTitle_), 60, 64, 26, C4(1,0.9f,0.5f,1));
    drawText(locale_.tr("store.gold_label") + ": " + std::to_string(pl.gold), W-260, 64, 20, C4(1,0.95f,0.4f,1));
    drawText(locale_.tr("store.controls_hint"), 60, 94, 14, C4(0.8f,0.85f,1,0.9f));

    // Milestone 8: tab bar (see storeTabIndices()'s declaration in game.h for why tabs exist at
    // all -- keeps every tab's card count at the layout's original, unchanged capacity of ~3).
    const std::string kTabLabels[4] = {
        locale_.tr("store.tab.potion"), locale_.tr("store.tab.weapon"),
        locale_.tr("store.tab.armor"), locale_.tr("store.tab.talent"),
    };
    storeTabRects_.clear();
    {
        float tbw = 100, tbh = 34, tgap = 12;
        float tx = 60, ty = 110;
        for (int t = 0; t < 4; t++) {
            storeTabRects_.push_back(tx); storeTabRects_.push_back(ty);
            storeTabRects_.push_back(tbw); storeTabRects_.push_back(tbh);
            Quad tb; tb.rect[0]=tx; tb.rect[1]=ty; tb.rect[2]=tbw; tb.rect[3]=tbh;
            tb.uv[0]=0;tb.uv[1]=0;tb.uv[2]=1;tb.uv[3]=1; tb.solid=true;
            bool activeTab = (t == storeTab_);
            if (activeTab) { tb.tint[0]=0.24f; tb.tint[1]=0.30f; tb.tint[2]=0.44f; tb.tint[3]=1; }
            else           { tb.tint[0]=0.13f; tb.tint[1]=0.14f; tb.tint[2]=0.20f; tb.tint[3]=0.9f; }
            ren->drawSprite(tb);
            drawText(kTabLabels[t], tx + 22, ty + 8, 17, activeTab ? C4(1,0.95f,0.6f,1) : C4(0.8f,0.82f,0.9f,1));
            tx += tbw + tgap;
        }
    }

    std::vector<int> idx = storeTabIndices();
    std::vector<float> rects; storeCardRects(rects, (int)idx.size());
    storeBtnRects_.clear();
    for (int i = 0; i < (int)idx.size(); i++) {
        float cx = rects[i*4], cy = rects[i*4+1], cw = rects[i*4+2], ch = rects[i*4+3];
        const StoreItemDef& d = storeItems_[idx[i]];
        int cost = d.liveCost();
        // card bg
        Quad card; card.rect[0]=cx; card.rect[1]=cy; card.rect[2]=cw; card.rect[3]=ch;
        card.uv[0]=0;card.uv[1]=0;card.uv[2]=1;card.uv[3]=1; card.solid=true;
        bool sel = (i == storeSel_);
        if (sel) { card.tint[0]=0.22f; card.tint[1]=0.26f; card.tint[2]=0.38f; card.tint[3]=1; }
        else     { card.tint[0]=0.14f; card.tint[1]=0.16f; card.tint[2]=0.24f; card.tint[3]=0.96f; }
        ren->drawSprite(card);
        // selected highlight border
        if (sel) {
            Quad hi; hi.rect[0]=cx-3; hi.rect[1]=cy-3; hi.rect[2]=cw+6; hi.rect[3]=ch+6;
            hi.uv[0]=0;hi.uv[1]=0;hi.uv[2]=1;hi.uv[3]=1; hi.solid=true;
            hi.tint[0]=1;hi.tint[1]=0.85f;hi.tint[2]=0.2f;hi.tint[3]=1; ren->drawSprite(hi);
        }
        // icon (sprite)
        ren->drawSprite(spriteQuad(cx + cw/2 - 46, cy + 22, 92, 92, spriteLayer(d.sprite.empty()?"coin":d.sprite), C4(1,1,1,1)));
        drawText(locale_.field(d.name), cx + 16, cy + 124, 26, C4(1,1,1,1));
        drawText(locale_.field(d.desc), cx + 16, cy + 162, 16, C4(0.85f,0.9f,1,1));
        drawText(locale_.tr("store.effect_label") + ": " + locale_.field(d.effect_text), cx + 16, cy + 188, 18, C4(0.6f,1,0.7f,1));
        // Milestone 8: an equipment card shows whether it's the one currently in its slot instead
        // of a purchase counter (buying it re-equips it -- "已購買 x3" would be misleading).
        bool isEquipped = !d.equipmentId.empty() &&
            (d.equipmentId == equipped_.weaponId || d.equipmentId == equipped_.armorId || d.equipmentId == equipped_.talentId);
        if (!d.equipmentId.empty())
            drawText(isEquipped ? locale_.tr("store.equipped") : locale_.tr("store.tap_to_equip"), cx + 16, cy + 216, 15, isEquipped ? C4(0.5f,1,0.6f,1) : C4(0.7f,0.7f,0.8f,1));
        else
            drawText(locale_.tr("store.purchased_label") + " x" + std::to_string(d.purchases), cx + 16, cy + 216, 15, C4(0.7f,0.7f,0.8f,1));
        // price tag
        drawText(locale_.tr("store.price_label") + ": " + std::to_string(cost) + " G", cx + 16, cy + 240, 22, C4(1,0.95f,0.4f,1));
        // buy button
        float btnW = cw - 32, btnH = 38;
        float btnX = cx + 16, btnY = cy + ch - btnH - 12;
        storeBtnRects_.push_back(btnX); storeBtnRects_.push_back(btnY); storeBtnRects_.push_back(btnW); storeBtnRects_.push_back(btnH);
        Quad btn; btn.rect[0]=btnX; btn.rect[1]=btnY; btn.rect[2]=btnW; btn.rect[3]=btnH;
        btn.uv[0]=0;btn.uv[1]=0;btn.uv[2]=1;btn.uv[3]=1; btn.solid=true;
        btn.tint[0]=0.2f;btn.tint[1]=0.5f;btn.tint[2]=0.3f;btn.tint[3]=1; ren->drawSprite(btn);
        drawText(locale_.tr("store.buy_button"), btnX + 16, btnY + 10, 16, C4(1,1,1,1));
    }
    // close button (top-right corner of panel)
    float cw2 = 90, ch2 = 34;
    storeCloseRect_[0] = (int)(W-40-cw2); storeCloseRect_[1] = 56; storeCloseRect_[2] = (int)cw2; storeCloseRect_[3] = (int)ch2;
    Quad cbtn; cbtn.rect[0]=storeCloseRect_[0]; cbtn.rect[1]=storeCloseRect_[1]; cbtn.rect[2]=cw2; cbtn.rect[3]=ch2;
    cbtn.uv[0]=0;cbtn.uv[1]=0;cbtn.uv[2]=1;cbtn.uv[3]=1; cbtn.solid=true;
    cbtn.tint[0]=0.5f;cbtn.tint[1]=0.2f;cbtn.tint[2]=0.2f;cbtn.tint[3]=1; ren->drawSprite(cbtn);
    drawText(locale_.tr("inventory.close") + " X", storeCloseRect_[0]+14, storeCloseRect_[1]+9, 16, C4(1,1,1,1));
}

void Game::drawStoreToast() {
    if (toastTimer_ <= 0 || toastMsg_.empty()) return;
    float W = (float)ren->width(), H = (float)ren->height();
    float tw = 260, th = 48;
    float tx = (W - tw)/2, ty = H - 160;
    // "not enough gold" -> simple shake effect on the toast
    if (shakeTimer_ > 0) tx += (float)(6.0f * std::sin((float)shakeTimer_ * 0.06f));
    Quad t; t.rect[0]=tx; t.rect[1]=ty; t.rect[2]=tw; t.rect[3]=th;
    t.uv[0]=0;t.uv[1]=0;t.uv[2]=1;t.uv[3]=1; t.solid=true;
    t.tint[0]=0.4f;t.tint[1]=0.1f;t.tint[2]=0.1f;t.tint[3]=0.95f; ren->drawSprite(t);
    drawText(toastMsg_, tx + 20, ty + 14, 18, C4(1,0.8f,0.8f,1));
}

void Game::storeClick(float x, float y) {
    // unlock dialog takes priority
    if (storeUnlockDlg) {
        // confirm button?
        if (x >= storeUnlockBtnRect_[0] && x <= storeUnlockBtnRect_[0]+storeUnlockBtnRect_[2] &&
            y >= storeUnlockBtnRect_[1] && y <= storeUnlockBtnRect_[1]+storeUnlockBtnRect_[3]) {
            storeUnlockDlg = false; audio.play("confirm_click");
        }
        return; // modal: swallow all other clicks
    }
    if (storeOpen) {
        // close button
        if (x >= storeCloseRect_[0] && x <= storeCloseRect_[0]+storeCloseRect_[2] &&
            y >= storeCloseRect_[1] && y <= storeCloseRect_[1]+storeCloseRect_[3]) {
            closeStore(); return;
        }
        // Milestone 8: tab buttons (checked before the buy buttons, which are per-tab now).
        for (int t = 0; t + 3 < (int)storeTabRects_.size() / 4 * 4; t += 4) {
            float bx = storeTabRects_[t], by = storeTabRects_[t+1], bw = storeTabRects_[t+2], bh = storeTabRects_[t+3];
            if (x >= bx && x <= bx+bw && y >= by && y <= by+bh) {
                storeTab_ = t / 4; storeSel_ = 0; audio.play("confirm_click"); return;
            }
        }
        // buy buttons -- storeBtnRects_ is rebuilt per-tab each frame in drawStoreUI(), so index i
        // here maps through the same tab filter to the real storeItems_ index.
        std::vector<int> idx = storeTabIndices();
        int nBtn = (int)storeBtnRects_.size() / 4;
        for (int i = 0; i < nBtn && i < (int)idx.size(); i++) {
            float bx = storeBtnRects_[i*4], by = storeBtnRects_[i*4+1], bw = storeBtnRects_[i*4+2], bh = storeBtnRects_[i*4+3];
            if (x >= bx && x <= bx+bw && y >= by && y <= by+bh) {
                buyStoreItem(idx[i]); return;
            }
        }
        // clicking a card selects it (and if it's the buy area handled above). Clicking
        // outside the panel closes the store.
        bool insidePanel = (x >= 40 && x <= (float)ren->width()-40 && y >= 40 && y <= (float)ren->height()-40);
        if (!insidePanel) closeStore();
        return;
    }
    // not in any store overlay -> clicking the HUD icon opens the store (only if unlocked)
    if (storeUnlocked_ &&
        x >= storeIconRect[0] && x <= storeIconRect[0]+storeIconRect[2] &&
        y >= storeIconRect[1] && y <= storeIconRect[1]+storeIconRect[3]) {
        openStore();
    }
}

void Game::storeKey(int key) {
    // unlock dialog: Enter/Esc/Space confirms
    if (storeUnlockDlg) {
        if (key == 13 || key == 27 || key == 32) { storeUnlockDlg = false; audio.play("confirm_click"); }
        return;
    }
    if (!storeOpen) return;
    // Milestone 8: storeSel_ is an index into the current tab's filtered list, not storeItems_
    // directly (see storeTabIndices()) -- map through it before buying.
    std::vector<int> idxList = storeTabIndices();
    int n = (int)idxList.size();
    if (key == 27) { closeStore(); return; }                       // Esc closes
    if (key == 13 || key == 32) {                                  // Enter/Space buys
        if (storeSel_ >= 0 && storeSel_ < n) buyStoreItem(idxList[storeSel_]);
        return;
    }
    if (key == 262 || key == 263) {                                // right/left arrow
        storeSel_ += (key == 262) ? 1 : -1;
        if (storeSel_ < 0) storeSel_ = n-1; if (storeSel_ >= n) storeSel_ = 0;
        return;
    }
    if (key >= '1' && key <= '9') {                                // number keys select
        int sel = key - '1';
        if (sel < n) storeSel_ = sel;
    }
}

void Game::buyStoreItem(int idx) {
    if (idx < 0 || idx >= (int)storeItems_.size()) return;
    StoreItemDef& d = storeItems_[idx];
    int cost = d.liveCost();
    if (pl.gold < cost) {
        // not enough gold -> toast + shake (simple effect), no purchase
        toastMsg_ = trParam(locale_.tr("store.toast_insufficient_gold"), "cost", std::to_string(cost));
        toastTimer_ = 1600;
        shakeTimer_ = 350;
        audio.play("deny");
        return;
    }
    pl.gold -= cost;
    // Milestone 8: an equipment entry replaces whatever was in that slot instead of applying a
    // generic {hp/str/def} effect -- equipping is a swap, not a stacking buff, so re-buying the
    // same item (or a different one in the same slot) is a harmless, idempotent re-equip.
    if (!d.equipmentId.empty()) {
        auto it = equipmentDefs_.find(d.equipmentId);
        if (it != equipmentDefs_.end()) {
            switch (it->second.slot) {
                case toms::EquipmentSlot::Weapon: equipped_.weaponId = d.equipmentId; break;
                case toms::EquipmentSlot::Armor:  equipped_.armorId  = d.equipmentId; break;
                case toms::EquipmentSlot::Talent: equipped_.talentId = d.equipmentId; break;
            }
        }
    } else {
        // apply effect immediately (per spec: bought item is used right away)
        const nlohmann::json& eff = d.effect;
        if (eff.contains("hp"))  pl.hp   = std::min(pl.maxhp, pl.hp + (int)eff["hp"]);
        if (eff.contains("str")) pl.atk  += (int)eff["str"];
        if (eff.contains("def")) pl.def  += (int)eff["def"];
    }
    d.purchases++;
    toastMsg_ = trParam(locale_.tr("store.toast_purchase_success"), "name", locale_.field(d.name));
    toastTimer_ = 1400;
    audio.play("get_item");
    markProgressDirty();   // a purchase changes gold/stats/equipment: autosave will flush it
}

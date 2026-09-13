// game_title_glue.cpp — title phase + save slots glue: title menu state, slot summaries,
// new game / continue, autosave, language switching, and the title pages' drawing.
// Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

// =====================================================================================
// Title phase — New Game / Continue / Settings (see src/game/title_screen.h)
// =====================================================================================

// "HH:MM:SS" for the Continue list's play-time column.
static std::string fmtPlayTime(int sec) {
    if (sec < 0) sec = 0;
    char b[32];
    std::snprintf(b, sizeof(b), "%02d:%02d:%02d", sec / 3600, (sec / 60) % 60, sec % 60);
    return std::string(b);
}

std::string Game::stageDisplayName(const std::string& id) const {
    // Match either identifier: the game loads stages by FILE STEM ("stage01"), while the id
    // inside the JSON is "stage_01" -- so a pure id comparison silently missed and the Continue
    // list showed the raw stem instead of the stage's real name.
    for (const auto& s : stageList_)
        if (s.id == id || s.fileStem == id) return s.name.empty() ? id : s.name;
    return id;
}

// Snapshots the live game state into the Run-save schema (save_system.h) for writing to a slot.
toms::RunSaveData Game::runSaveFromState() const {
    toms::RunSaveData r;
    r.currentStageId = curStage;
    r.player.hp = pl.hp; r.player.maxhp = pl.maxhp;
    r.player.atk = pl.atk; r.player.def = pl.def;
    r.player.gold = pl.gold; r.player.exp = pl.exp; r.player.lv = pl.lv;
    r.player.key_yellow = pl.key_yellow; r.player.key_blue = pl.key_blue; r.player.key_red = pl.key_red;
    r.player.inv = pl.inv;
    r.entityStatus = entityStatus_;
    // S3: the run's story state travels with the run save (schemaVersion 3) -- choices, counters,
    // side-story states, run flags, shards, floor progress and deaths.
    run_.writeInto(r);
    return r;
}

void Game::saveCurrentRun() {
    if (activeSlot_ <= 0) return;          // no slot chosen yet (title still up at boot)
    ensureStageListLoaded();
    const std::string dir = toms::defaultSaveDir();
    if (toms::writeSlotSave(dir, activeSlot_, meta_, runSaveFromState(), playTimeSec_,
                            toms::nowStamp(), stageDisplayName(curStage))) {
        saveDirty_ = false;
    }
    if (title_.isOpen()) refreshSlots();
}

void Game::refreshSlots() {
    std::vector<toms::SlotSummary> v;
    const std::string dir = toms::defaultSaveDir();
    for (int i = 1; i <= title_.slotCount(); i++) v.push_back(toms::summarizeSlot(dir, i));
    title_.setSummaries(std::move(v));
}

void Game::applyLanguage() {
    // The Settings page owns the selection -- mirror it into the persisted preference. Reading
    // it back from title_ (rather than re-applying the old settings_.language) is what makes
    // the choice actually stick: without this the freshly-picked language was overwritten by
    // the stale preference on the very same call, so the UI snapped back.
    const int idx = title_.languageIndex();
    if (idx >= 0 && idx < locale_.languageCount())
        settings_.language = locale_.languages()[idx].code;
    locale_.setLanguage(settings_.language);
    settings_.language = locale_.languageCode();   // normalize (e.g. an unknown code -> default)
    title_.setLanguageIndex(locale_.languageIndex());
    toms::ensureSaveDir(toms::defaultSaveDir());
    toms::saveGameSettings(toms::defaultSaveDir() + "/settings.json", settings_);
}

void Game::applyLoadedRun(const toms::MetaSaveData& m, const toms::RunSaveData& r,
                          int slot, int playSec) {
    meta_ = m;
    entityStatus_ = r.entityStatus;
    run_.readFrom(r);   // S3: restore the run's story state (defaults for a pre-v3 file)
    // Anything that belongs to the *previous* run must not leak into the loaded one.
    missionTrackers_.clear();
    notifications_.clear();
    cs = CombatState{};
    inDialogue = false; dlgChoices.clear(); dlgNode = "root";
    invOpen = false; storeOpen = false; storeUnlockDlg = false;
    stageSelectOpen_ = false; stairsConfirmOpen_ = false;
    pl.hp = r.player.hp; pl.maxhp = r.player.maxhp;
    pl.atk = r.player.atk; pl.def = r.player.def;
    pl.gold = r.player.gold; pl.exp = r.player.exp; pl.lv = r.player.lv;
    pl.key_yellow = r.player.key_yellow; pl.key_blue = r.player.key_blue; pl.key_red = r.player.key_red;
    pl.inv = r.player.inv;
    activeSlot_ = slot;
    playTimeSec_ = playSec;
    loadStage(r.currentStageId.empty() ? std::string("stage01") : r.currentStageId);
    title_.close();
    title_.setRunInProgress(true);
}

bool Game::continueFromSlot(int slot) {
    if (slot <= 0) return false;
    toms::MetaSaveData m;
    toms::RunSaveData r;
    int playSec = 0;
    if (!toms::readSlotSave(toms::defaultSaveDir(), slot, m, r, &playSec, nullptr)) {
        // Deliberately silent: the Continue row already reads "[空]/[Empty]" and the page shows
        // "no saves yet -- start a new game first", and the toast path draws with ImGui's
        // default font, which has no CJK glyphs (it would render as a "?" box). The title stays
        // open, so there is nothing to resume from.
        refreshSlots();
        return false;
    }
    applyLoadedRun(m, r, slot, playSec);
    return true;
}

void Game::newGame() { newGame(0); }

void Game::handleTitleAction(toms::TitleAction a) {
    switch (a) {
        case toms::TitleAction::NewGame:             newGame(); break;
        case toms::TitleAction::StartNewGameInSlot:  newGame(title_.pendingSlot()); break;
        case toms::TitleAction::LoadSlot:            continueFromSlot(title_.pendingSlot()); break;
        case toms::TitleAction::OpenContinue:        refreshSlots(); break;   // always show current files
        case toms::TitleAction::SetLanguage:         applyLanguage(); break;
        // The confirm dialogs are drawn and answered by the title itself; these are informational.
        case toms::TitleAction::AskNewGameInSlot:
        case toms::TitleAction::DismissNewGameConfirm:
        case toms::TitleAction::AskLanguageChange:
        case toms::TitleAction::DismissLanguageConfirm:
        case toms::TitleAction::OpenSettings:
        case toms::TitleAction::Back:
        case toms::TitleAction::None:                break;
    }
}

void Game::titleMove(int dx, int dy) {
    if (!title_.isOpen()) return;
    toms::TitleAction a = toms::TitleAction::None;
    if (dy != 0) a = title_.moveVertical(dy);
    if (dx != 0) {
        toms::TitleAction h = title_.moveHorizontal(dx);   // Settings: left/right switches language
        if (a == toms::TitleAction::None) a = h;
    }
    handleTitleAction(a);
}

void Game::titleConfirm() {
    if (title_.isOpen()) handleTitleAction(title_.activate());
}

void Game::titleCancel() {
    if (title_.isOpen()) handleTitleAction(title_.cancel());
}

void Game::titleClick(float x, float y) {
    if (!title_.isOpen()) return;
    handleTitleAction(title_.click(x, y, titleLayout_));
}

// One title row: framed panel + left accent bar + label (+ optional dim sub-label), with the
// highlight reserved for the row the cursor is on. Solid-tint quads only -- the title screen
// ships no art, so it renders identically on every backend for free.
void Game::drawTitleButton(const toms::TitleRow& r, const std::string& label, const std::string& sub,
                           bool selected, const float accent[4], float pulse) {
    static const float hi[4]  = {1.00f, 0.97f, 0.86f, 1.0f};
    static const float dflt[4]= {0.86f, 0.88f, 0.94f, 1.0f};
    static const float dim[4] = {0.60f, 0.64f, 0.76f, 1.0f};

    // Subtle breathing on the selected row (panel brightness, accent alpha, cursor slide) so the
    // screen is visibly live even when nothing is being pressed -- a static title reads as a crash.
    const float glow = selected ? (0.90f + 0.18f * pulse) : 1.0f;
    const float accentA = selected ? (0.68f + 0.32f * pulse) : 0.35f;

    Quad panel;
    panel.rect[0] = r.x; panel.rect[1] = r.y; panel.rect[2] = r.w; panel.rect[3] = r.h;
    panel.uv[0] = 0; panel.uv[1] = 0; panel.uv[2] = 1; panel.uv[3] = 1;
    panel.solid = true;
    panel.tint[0] = (selected ? 0.20f : 0.11f) * glow;
    panel.tint[1] = (selected ? 0.21f : 0.12f) * glow;
    panel.tint[2] = (selected ? 0.30f : 0.18f) * glow;
    panel.tint[3] = 0.96f;
    ren->drawSprite(panel);

    Quad bar;
    bar.rect[0] = r.x; bar.rect[1] = r.y; bar.rect[2] = 6.0f; bar.rect[3] = r.h;
    bar.uv[0] = 0; bar.uv[1] = 0; bar.uv[2] = 1; bar.uv[3] = 1;
    bar.solid = true;
    bar.tint[0] = accent[0]; bar.tint[1] = accent[1]; bar.tint[2] = accent[2];
    bar.tint[3] = accentA;
    ren->drawSprite(bar);

    const float labelSize = 26.0f;
    const bool hasSub = !sub.empty();
    const float ly = hasSub ? r.y + 7.0f : r.y + (r.h - labelSize) * 0.5f;
    drawText(label, r.x + 22.0f, ly, labelSize, selected ? hi : dflt);
    if (hasSub) drawText(sub, r.x + 22.0f, ly + labelSize + 1.0f, 15.0f, dim);
    if (selected) {
        float cursor[4] = { accent[0], accent[1], accent[2], accentA };
        drawText(">", r.x - 26.0f - 6.0f * pulse, ly, labelSize, cursor);
    }
}

void Game::drawTitleScreen() {
    if (!ren) return;
    const float W = (float)ren->width(), H = (float)ren->height();
    // Same layout function titleClick() hit-tests against -> a tap always lands on what was drawn.
    titleLayout_ = toms::computeTitleLayout((int)W, (int)H, title_.slotCount(), locale_.languageCount());

    // Animation clock for the whole page: a slow ~1.4 s pulse drives the selected row's glow, its
    // accent alpha and the sliding cursor, and gently breathes the two frame rules. Without it the
    // title screen is a perfectly static image, which players read as "it crashed".
    const float pulse = 0.5f + 0.5f * std::sin(titleAnimMs_ * 0.0045f);

    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};

    // Full-screen backdrop + two horizontal rules: a framed look with no art assets.
    Quad bg;
    bg.rect[0] = 0; bg.rect[1] = 0; bg.rect[2] = W; bg.rect[3] = H;
    bg.uv[0] = 0; bg.uv[1] = 0; bg.uv[2] = 1; bg.uv[3] = 1;
    bg.solid = true;
    bg.tint[0] = 0.05f; bg.tint[1] = 0.05f; bg.tint[2] = 0.09f; bg.tint[3] = 1.0f;
    ren->drawSprite(bg);

    Quad rule;
    rule.rect[0] = 0; rule.rect[2] = W; rule.rect[3] = 2.0f;
    rule.uv[0] = 0; rule.uv[1] = 0; rule.uv[2] = 1; rule.uv[3] = 1;
    rule.solid = true;
    rule.tint[0] = gold[0]; rule.tint[1] = gold[1]; rule.tint[2] = gold[2];
    rule.tint[3] = 0.40f + 0.30f * pulse;
    rule.rect[1] = 74.0f;  ren->drawSprite(rule);
    rule.rect[1] = H - 62.0f; ren->drawSprite(rule);

    const std::string title = locale_.tr("game.title");
    float tw = measureText(title, 52);
    drawText(title, (W - tw) * 0.5f, 92.0f, 52, gold);
    const std::string subtitle = locale_.tr("game.subtitle");
    float sw = measureText(subtitle, 17);
    drawText(subtitle, (W - sw) * 0.5f, 152.0f, 17, dim);

    switch (title_.page()) {
        case toms::TitlePage::Menu: {
            static const char* keys[3] = {"menu.new_game", "menu.continue", "menu.settings"};
            static const char* desc[3] = {"menu.new_game.desc", "menu.continue.desc", "menu.settings.desc"};
            for (int i = 0; i < toms::TitleLayout::kMaxMenuRows; i++)
                drawTitleButton(titleLayout_.menuRow[i], locale_.tr(keys[i]), locale_.tr(desc[i]),
                                title_.menuSelection() == i, gold, pulse);
            break;
        }
        case toms::TitlePage::Continue: {
            // The renderer draws ALL sprites first, then ALL text glyphs in one later pass
            // (see WebGLRenderer::end()/flush() -- two texture batches, sprites then text), so
            // text can never be visually covered by a later sprite (the dialog's scrim/panel):
            // it always ends up on top. Skip drawing the rows this dialog would otherwise sit
            // on top of, rather than relying on the scrim to hide their labels.
            if (!title_.newGameConfirmOpen()) {
                const std::string head = locale_.tr("continue.header");
                drawText(head, (W - measureText(head, 30)) * 0.5f, 196.0f, 30, gold);
                const auto& sums = title_.summaries();
                bool any = false;
                for (int i = 0; i < titleLayout_.slotRowCount; i++) {
                    std::string label = locale_.tr("continue.slot") + " " + std::to_string(i + 1);
                    std::string sub;
                    if (i < (int)sums.size() && sums[i].exists) {
                        const toms::SlotSummary& s = sums[i];
                        any = true;
                        label += "   " + (s.stageName.empty() ? s.stageId : s.stageName)
                               + "   " + locale_.tr("hud.level") + " " + std::to_string(s.lv)
                               + "   HP " + std::to_string(s.hp) + "/" + std::to_string(s.maxhp)
                               + "   " + std::to_string(s.gold) + "G";
                        sub = locale_.tr("continue.saved_at") + " " + s.savedAt
                            + "    " + locale_.tr("continue.play_time") + " " + fmtPlayTime(s.playTimeSec);
                    } else {
                        label += "   [" + locale_.tr("continue.empty") + "]";
                    }
                    drawTitleButton(titleLayout_.slotRow[i], label, sub,
                                    title_.slotSelection() == i, gold, pulse);
                }
                if (!any) {
                    const std::string hint = locale_.tr("continue.hint");
                    drawText(hint, (W - measureText(hint, 18)) * 0.5f, H - 178.0f, 18, dim);
                }
                drawTitleButton(titleLayout_.backButton, locale_.tr("menu.back"), "", false, dim, pulse);
            }
            break;
        }
        case toms::TitlePage::Settings: {
            // Same reasoning as Continue above: hide the rows the language-confirm dialog
            // would otherwise (ineffectively) sit on top of.
            if (!title_.languageConfirmOpen()) {
                const std::string head = locale_.tr("settings.header");
                drawText(head, (W - measureText(head, 30)) * 0.5f, 262.0f, 30, gold);
                const std::string langLabel = locale_.tr("settings.language");
                drawText(langLabel, (W - measureText(langLabel, 20)) * 0.5f, 300.0f, 20, dim);
                const auto& langs = locale_.languages();
                for (int i = 0; i < titleLayout_.langRowCount && i < (int)langs.size(); i++) {
                    const bool active = (i == locale_.languageIndex());
                    std::string label = std::string(active ? "[x] " : "[ ] ") + langs[i].name;
                    drawTitleButton(titleLayout_.langRow[i], label, "",
                                    title_.settingsSelection() == i, gold, pulse);
                }
                // Touch/mobile has no Esc key -- without a drawn Back button, Settings was a
                // dead end on the web build (the hit-test already worked, it was just invisible).
                drawTitleButton(titleLayout_.backButton, locale_.tr("menu.back"), "", false, dim, pulse);
            }
            break;
        }
    }

    const std::string hint = (title_.page() == toms::TitlePage::Settings)
                             ? locale_.tr("settings.hint") : locale_.tr("menu.hint");
    drawText(hint, (W - measureText(hint, 16)) * 0.5f, H - 44.0f, 16, dim);

    // The confirm prompts are the topmost thing on the title (see drawTitleConfirmDialog() /
    // drawLanguageConfirmDialog()). The two pages they belong to are mutually exclusive, so at
    // most one of these is ever open at once.
    if (title_.newGameConfirmOpen()) drawTitleConfirmDialog();
    if (title_.languageConfirmOpen()) drawLanguageConfirmDialog();
}

// "Use this slot to start the game?" -- shown when an EMPTY Continue slot is activated, so the
// button never just does nothing. Yes starts a fresh run in that specific slot.
void Game::drawTitleConfirmDialog() {
    if (!ren || !title_.newGameConfirmOpen()) return;
    const float W = (float)ren->width();
    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};

    // Dim the list behind the prompt so it reads as modal.
    Quad scrim;
    scrim.rect[0] = 0; scrim.rect[1] = 0; scrim.rect[2] = W; scrim.rect[3] = (float)ren->height();
    scrim.uv[0] = 0; scrim.uv[1] = 0; scrim.uv[2] = 1; scrim.uv[3] = 1;
    scrim.solid = true;
    scrim.tint[0] = 0.0f; scrim.tint[1] = 0.0f; scrim.tint[2] = 0.0f; scrim.tint[3] = 0.62f;
    ren->drawSprite(scrim);

    const toms::TitleRow& box = titleLayout_.confirmBox;
    Quad panel;
    panel.rect[0] = box.x; panel.rect[1] = box.y; panel.rect[2] = box.w; panel.rect[3] = box.h;
    panel.uv[0] = 0; panel.uv[1] = 0; panel.uv[2] = 1; panel.uv[3] = 1;
    panel.solid = true;
    panel.tint[0] = 0.13f; panel.tint[1] = 0.14f; panel.tint[2] = 0.21f; panel.tint[3] = 0.98f;
    ren->drawSprite(panel);

    Quad rule;
    rule.rect[0] = box.x; rule.rect[1] = box.y; rule.rect[2] = box.w; rule.rect[3] = 3.0f;
    rule.uv[0] = 0; rule.uv[1] = 0; rule.uv[2] = 1; rule.uv[3] = 1;
    rule.solid = true;
    rule.tint[0] = gold[0]; rule.tint[1] = gold[1]; rule.tint[2] = gold[2]; rule.tint[3] = 0.9f;
    ren->drawSprite(rule);

    // "{slot}" placeholder substitution -- keeps the wording in data/text.json instead of
    // hardcoding the sentence per language here.
    std::string question = trParam(locale_.tr("continue.new_game_confirm"), "slot",
                                    std::to_string(title_.newGameConfirmSlot()));
    drawText(question, box.x + (box.w - measureText(question, 26)) * 0.5f, box.y + 34.0f, 26, gold);

    const std::string body = locale_.tr("continue.new_game_confirm.body");
    drawText(body, box.x + (box.w - measureText(body, 16)) * 0.5f, box.y + 86.0f, 16, dim);

    const bool yes = title_.newGameConfirmYesSelected();
    const float pulse = 0.5f + 0.5f * std::sin(titleAnimMs_ * 0.0045f);   // same live pulse as the list
    drawTitleButton(titleLayout_.confirmYes, locale_.tr("menu.yes"), "", yes, gold, pulse);
    drawTitleButton(titleLayout_.confirmNo,  locale_.tr("menu.no"),  "", !yes, gold, pulse);
}

// "Switch to XXX language?" -- shown when a language row is activated/tapped, instead of
// applying it instantly (a single mis-tap used to have no way back on a touch device with no
// keyboard/Esc). Reuses the same confirmBox/confirmYes/confirmNo layout as
// drawTitleConfirmDialog(): the two dialogs only ever appear on different pages (Continue vs.
// Settings), so they never need to coexist.
void Game::drawLanguageConfirmDialog() {
    if (!ren || !title_.languageConfirmOpen()) return;
    const float W = (float)ren->width();
    static const float gold[4] = {0.95f, 0.82f, 0.45f, 1.0f};
    static const float dim[4]  = {0.60f, 0.64f, 0.76f, 1.0f};

    Quad scrim;
    scrim.rect[0] = 0; scrim.rect[1] = 0; scrim.rect[2] = W; scrim.rect[3] = (float)ren->height();
    scrim.uv[0] = 0; scrim.uv[1] = 0; scrim.uv[2] = 1; scrim.uv[3] = 1;
    scrim.solid = true;
    scrim.tint[0] = 0.0f; scrim.tint[1] = 0.0f; scrim.tint[2] = 0.0f; scrim.tint[3] = 0.62f;
    ren->drawSprite(scrim);

    const toms::TitleRow& box = titleLayout_.confirmBox;
    Quad panel;
    panel.rect[0] = box.x; panel.rect[1] = box.y; panel.rect[2] = box.w; panel.rect[3] = box.h;
    panel.uv[0] = 0; panel.uv[1] = 0; panel.uv[2] = 1; panel.uv[3] = 1;
    panel.solid = true;
    panel.tint[0] = 0.13f; panel.tint[1] = 0.14f; panel.tint[2] = 0.21f; panel.tint[3] = 0.98f;
    ren->drawSprite(panel);

    Quad rule;
    rule.rect[0] = box.x; rule.rect[1] = box.y; rule.rect[2] = box.w; rule.rect[3] = 3.0f;
    rule.uv[0] = 0; rule.uv[1] = 0; rule.uv[2] = 1; rule.uv[3] = 1;
    rule.solid = true;
    rule.tint[0] = gold[0]; rule.tint[1] = gold[1]; rule.tint[2] = gold[2]; rule.tint[3] = 0.9f;
    ren->drawSprite(rule);

    // Name the target language in ITS OWN endonym (matches the row label's own style, e.g.
    // "日本語" not "Japanese") so it reads correctly regardless of the CURRENT UI language.
    const auto& langs = locale_.languages();
    int idx = title_.languageConfirmIndex();
    std::string langName = (idx >= 0 && idx < (int)langs.size()) ? langs[idx].name : "";
    std::string question = trParam(locale_.tr("settings.language_confirm"), "lang", langName);
    drawText(question, box.x + (box.w - measureText(question, 24)) * 0.5f, box.y + 70.0f, 24, gold);

    const bool yes = title_.languageConfirmYesSelected();
    const float pulse = 0.5f + 0.5f * std::sin(titleAnimMs_ * 0.0045f);
    drawTitleButton(titleLayout_.confirmYes, locale_.tr("menu.yes"), "", yes, gold, pulse);
    drawTitleButton(titleLayout_.confirmNo,  locale_.tr("menu.no"),  "", !yes, gold, pulse);
}

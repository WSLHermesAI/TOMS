// game_inventory.cpp — items: pickup effects, the backpack UI and its cursor/use/drop
// actions, item name/description lookups. Split out of game.cpp 2026-09-13.
#include "game_internal.h"

using namespace toms::game_detail;

void Game::applyItem(const std::string& id) {
    // Item definitions come from data/items.json (loaded in loadAssets).
    auto it = itemDefs.find(id);
    if (it == itemDefs.end()) { return; }
    const nlohmann::json& eff = it->second["effect"];
    if (eff.contains("hp"))    pl.hp    = std::min(pl.maxhp, pl.hp + (int)eff["hp"]);
    if (eff.contains("atk"))   pl.atk   += (int)eff["atk"];
    if (eff.contains("def"))   pl.def   += (int)eff["def"];
    if (eff.contains("gold"))  pl.gold  += (int)eff["gold"];
    if (eff.contains("key_yellow")) pl.key_yellow += (int)eff["key_yellow"];
    if (eff.contains("key_blue"))   pl.key_blue   += (int)eff["key_blue"];
    if (eff.contains("key_red"))    pl.key_red    += (int)eff["key_red"];
    if (eff.contains("exp")) {
        pl.exp += (int)eff["exp"];
        int need = pl.lv * 30;
        while (pl.exp >= need) {
            pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30;
            pushNotification(locale_.tr("battle.levelup") + std::to_string(pl.lv));
        }
    }
    if (eff.contains("warp")) {
        std::string dst = it->second.value("warp_to", std::string(""));
        if (!dst.empty() && std::filesystem::exists(dataDir + "/../data/stages/" + dst + ".json"))
            loadStage(dst);
    }
    audio.play("get_item");
}

// ---------- inventory UI ----------
void Game::toggleInventory() {
    invOpen = !invOpen;
    if (invSel >= (int)pl.inv.size()) invSel = (int)pl.inv.size() - 1;
    if (invSel < 0) invSel = 0;
}

void Game::invMoveSel(int dx, int dy) {
    if (!invOpen || pl.inv.empty()) return;
    int cols = 3;
    int n = (int)pl.inv.size();
    int r = invSel / cols, c = invSel % cols;
    c += dx; r += dy;
    if (c < 0) c = 0; if (c >= cols) c = cols - 1;
    if (r < 0) r = 0;
    int maxr = (n + cols - 1) / cols - 1;
    if (r > maxr) r = maxr;
    int idx = r * cols + c;
    if (idx >= n) idx = n - 1;
    invSel = idx;
}

bool Game::invUseSelected() {
    if (!invOpen || invSel < 0 || invSel >= (int)pl.inv.size()) return false;
    std::string id = pl.inv[invSel];
    applyItem(id);                 // applies effect (exp/warp/hp/atk/def/gold)
    pl.inv.erase(pl.inv.begin() + invSel);
    if (invSel >= (int)pl.inv.size()) invSel = (int)pl.inv.size() - 1;
    if (invSel < 0) invSel = 0;
    return true;
}

void Game::invDropSelected() {
    if (!invOpen || invSel < 0 || invSel >= (int)pl.inv.size()) return;
    pl.inv.erase(pl.inv.begin() + invSel);
    if (invSel >= (int)pl.inv.size()) invSel = (int)pl.inv.size() - 1;
    if (invSel < 0) invSel = 0;
}

int Game::spriteForItem(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end()) return 0;
    std::string sp = it->second.value("sprite", std::string("coin.png"));
    if (sp.size() > 4 && sp.substr(sp.size()-4) == ".png") sp = sp.substr(0, sp.size()-4);
    int layer = spriteLayer(sp);
    return layer;
}

std::string Game::itemName(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end() || !it->second.contains("name")) return id;
    std::string s = locale_.field(it->second["name"]);
    return s.empty() ? id : s;
}

std::string Game::itemDesc(const std::string& id) const {
    auto it = itemDefs.find(id);
    if (it == itemDefs.end() || !it->second.contains("desc")) return "";
    return locale_.field(it->second["desc"]);
}

void Game::drawInventory() {
    if (!invOpen) {
        invCardRects_.clear();
        return;
    }

    float W = (float)ren->width(), H = (float)ren->height();
    drawFocusSplash();

    auto effectSummary = [&](const std::string& id) -> std::string {
        auto it = itemDefs.find(id);
        if (it == itemDefs.end() || !it->second.contains("effect")) return locale_.tr("inventory.no_effect");
        const nlohmann::json& eff = it->second["effect"];
        std::vector<std::string> parts;
        auto add = [&](const char* key, const char* label) {
            if (eff.contains(key)) {
                int v = eff[key].get<int>();
                parts.push_back(std::string(label) + (v >= 0 ? "+" : "") + std::to_string(v));
            }
        };
        add("str", "STR");
        add("def", "DEF");
        add("hp",  "HP");
        add("mp",  "MP");
        add("exp", "EXP");
        add("gold","Gold");
        if (eff.contains("warp")) parts.push_back("Warp");
        if (parts.empty()) parts.push_back(locale_.tr("inventory.no_effect"));
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i) out += " • ";
            out += parts[i];
        }
        return out;
    };

    // Main modal panel
    float pw = std::min(960.0f, W - 40.0f);
    float ph = std::min(580.0f, H - 40.0f);
    float px = (W - pw) * 0.5f;
    float py = (H - ph) * 0.5f;

    Quad outer; outer.rect[0]=px; outer.rect[1]=py; outer.rect[2]=pw; outer.rect[3]=ph;
    outer.uv[0]=0; outer.uv[1]=0; outer.uv[2]=1; outer.uv[3]=1; outer.solid=true;
    outer.tint[0]=0.10f; outer.tint[1]=0.12f; outer.tint[2]=0.18f; outer.tint[3]=0.96f;
    ren->drawSprite(outer);

    Quad titleBar; titleBar.rect[0]=px; titleBar.rect[1]=py; titleBar.rect[2]=pw; titleBar.rect[3]=58;
    titleBar.uv[0]=0; titleBar.uv[1]=0; titleBar.uv[2]=1; titleBar.uv[3]=1; titleBar.solid=true;
    titleBar.tint[0]=0.14f; titleBar.tint[1]=0.17f; titleBar.tint[2]=0.25f; titleBar.tint[3]=1.0f;
    ren->drawSprite(titleBar);

    drawText(locale_.tr("inventory.title"), px + 22, py + 18, 26, C4(1,0.92f,0.55f,1));
    drawText(locale_.tr("inventory.hint"), px + 22, py + 38, 14, C4(0.82f,0.88f,1,1));

    const std::vector<std::string>& inv = pl.inv;
    int n = (int)inv.size();
    if (n <= 0) {
        Quad empty; empty.rect[0]=px+22; empty.rect[1]=py+80; empty.rect[2]=pw-44; empty.rect[3]=ph-102;
        empty.uv[0]=0; empty.uv[1]=0; empty.uv[2]=1; empty.uv[3]=1; empty.solid=true;
        empty.tint[0]=0.08f; empty.tint[1]=0.09f; empty.tint[2]=0.13f; empty.tint[3]=0.92f;
        ren->drawSprite(empty);
        drawText(locale_.tr("inventory.empty_title"), px+40, py+120, 22, C4(1,1,1,1));
        drawText(locale_.tr("inventory.empty_hint"), px+40, py+152, 16, C4(0.8f,0.85f,1,1));
        invCardRects_.clear();
        invCloseRect_[0] = (int)(px + pw - 120);
        invCloseRect_[1] = (int)(py + ph - 58);
        invCloseRect_[2] = 90;
        invCloseRect_[3] = 34;
        Quad cbtn; cbtn.rect[0]=invCloseRect_[0]; cbtn.rect[1]=invCloseRect_[1]; cbtn.rect[2]=invCloseRect_[2]; cbtn.rect[3]=invCloseRect_[3];
        cbtn.uv[0]=0; cbtn.uv[1]=0; cbtn.uv[2]=1; cbtn.uv[3]=1; cbtn.solid=true;
        cbtn.tint[0]=0.5f; cbtn.tint[1]=0.2f; cbtn.tint[2]=0.2f; cbtn.tint[3]=1;
        ren->drawSprite(cbtn);
        drawText(locale_.tr("inventory.close"), invCloseRect_[0] + 22, invCloseRect_[1] + 9, 16, C4(1,1,1,1));
        return;
    }

    // Layout close to the demo: left item list, right detail panel + action buttons.
    float leftX = px + 22, leftY = py + 80;
    float leftW = pw * 0.58f - 30.0f;
    float leftH = ph - 104.0f;
    float rightX = leftX + leftW + 24.0f;
    float rightY = leftY;
    float rightW = pw - (rightX - px) - 22.0f;
    float rightH = leftH;

    Quad lpanel; lpanel.rect[0]=leftX; lpanel.rect[1]=leftY; lpanel.rect[2]=leftW; lpanel.rect[3]=leftH;
    lpanel.uv[0]=0; lpanel.uv[1]=0; lpanel.uv[2]=1; lpanel.uv[3]=1; lpanel.solid=true;
    lpanel.tint[0]=0.08f; lpanel.tint[1]=0.10f; lpanel.tint[2]=0.15f; lpanel.tint[3]=0.96f; ren->drawSprite(lpanel);
    Quad rpanel; rpanel.rect[0]=rightX; rpanel.rect[1]=rightY; rpanel.rect[2]=rightW; rpanel.rect[3]=rightH;
    rpanel.uv[0]=0; rpanel.uv[1]=0; rpanel.uv[2]=1; rpanel.uv[3]=1; rpanel.solid=true;
    rpanel.tint[0]=0.09f; rpanel.tint[1]=0.11f; rpanel.tint[2]=0.18f; rpanel.tint[3]=0.98f; ren->drawSprite(rpanel);

    // Left list of items
    invCardRects_.clear();
    const float cardH = 66.0f;
    const float gap = 10.0f;
    const float cardW = leftW - 20.0f;
    const float sx = leftX + 10.0f;
    float sy = leftY + 10.0f;
    for (int i = 0; i < n; ++i) {
        const std::string& id = inv[i];
        float cy = sy + i * (cardH + gap);
        if (cy + cardH > leftY + leftH - 10.0f) break;   // fixed demo-style window
        invCardRects_.push_back(sx);
        invCardRects_.push_back(cy);
        invCardRects_.push_back(cardW);
        invCardRects_.push_back(cardH);
        bool sel = (i == invSel);
        Quad card; card.rect[0]=sx; card.rect[1]=cy; card.rect[2]=cardW; card.rect[3]=cardH;
        card.uv[0]=0; card.uv[1]=0; card.uv[2]=1; card.uv[3]=1; card.solid=true;
        if (sel) { card.tint[0]=0.22f; card.tint[1]=0.25f; card.tint[2]=0.38f; card.tint[3]=1; }
        else     { card.tint[0]=0.14f; card.tint[1]=0.16f; card.tint[2]=0.24f; card.tint[3]=0.98f; }
        ren->drawSprite(card);
        if (sel) {
            Quad hi; hi.rect[0]=sx-2; hi.rect[1]=cy-2; hi.rect[2]=cardW+4; hi.rect[3]=cardH+4;
            hi.uv[0]=0; hi.uv[1]=0; hi.uv[2]=1; hi.uv[3]=1; hi.solid=true;
            hi.tint[0]=1; hi.tint[1]=0.85f; hi.tint[2]=0.2f; hi.tint[3]=1; ren->drawSprite(hi);
        }
        int layer = spriteForItem(id);
        ren->drawSprite(spriteQuad(sx + 10.0f, cy + 10.0f, 46.0f, 46.0f, layer, C4(1,1,1,1)));
        drawText(itemName(id), sx + 66.0f, cy + 9.0f, 18, C4(1,1,1,1));
        drawText(effectSummary(id), sx + 66.0f, cy + 34.0f, 13, C4(0.7f,1,0.78f,1));
        drawText("x" + std::to_string(1), sx + cardW - 24.0f, cy + 22.0f, 18, C4(1,0.95f,0.5f,1));
    }

    // Clamp selection to visible item count.
    if (invSel >= n) invSel = n - 1;
    if (invSel < 0) invSel = 0;
    const std::string& sid = inv[invSel];

    // Detail pane.
    drawText(locale_.tr("inventory.detail_title"), rightX + 18, rightY + 16, 20, C4(1,0.95f,0.55f,1));
    ren->drawSprite(spriteQuad(rightX + 18, rightY + 54, 76, 76, spriteForItem(sid), C4(1,1,1,1)));
    drawText(itemName(sid), rightX + 108, rightY + 56, 26, C4(1,1,1,1));
    drawText("ID: " + sid, rightX + 108, rightY + 86, 14, C4(0.75f,0.8f,0.95f,1));
    drawText(locale_.tr("inventory.icon_label") + ": " + itemDefs[sid].value("sprite", std::string("")), rightX + 18, rightY + 136, 13, C4(0.7f,0.8f,0.95f,1));
    drawText(itemDesc(sid), rightX + 18, rightY + 162, 16, C4(0.88f,0.92f,1,1));
    drawText(locale_.tr("inventory.stats_label"), rightX + 18, rightY + 240, 13, C4(0.7f,0.8f,0.95f,1));

    const nlohmann::json& eff = itemDefs[sid]["effect"];
    float effY = rightY + 264;
    auto pill = [&](const std::string& s, float x, float y, float w) {
        Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=28;
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=0.12f; q.tint[1]=0.24f; q.tint[2]=0.16f; q.tint[3]=1;
        ren->drawSprite(q);
        drawText(s, x + 10, y + 7, 14, C4(0.85f,1,0.88f,1));
    };
    int pillRow = 0;
    if (eff.contains("str")) { pill("STR +" + std::to_string((int)eff["str"]), rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("def")) { pill("DEF +" + std::to_string((int)eff["def"]), rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("hp"))  { pill("HP +" + std::to_string((int)eff["hp"]),  rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("mp"))  { pill("MP +" + std::to_string((int)eff["mp"]),  rightX + 18, effY + pillRow*34, 110); pillRow++; }
    if (eff.contains("exp")) { pill("EXP +" + std::to_string((int)eff["exp"]), rightX + 18, effY + pillRow*34, 120); pillRow++; }
    if (eff.contains("gold")){ pill("Gold +" + std::to_string((int)eff["gold"]),rightX + 18, effY + pillRow*34, 126); pillRow++; }
    if (eff.contains("warp")){ pill("Warp", rightX + 18, effY + pillRow*34, 90); pillRow++; }
    if (pillRow == 0) pill(locale_.tr("inventory.no_effect"), rightX + 18, effY, 90);

    // Action buttons.
    float by = rightY + rightH - 48;
    float bw = 88, bh = 34;
    invUseRect_[0] = (int)(rightX + 18); invUseRect_[1] = (int)by; invUseRect_[2] = (int)bw; invUseRect_[3] = (int)bh;
    invDropRect_[0] = (int)(rightX + 116); invDropRect_[1] = (int)by; invDropRect_[2] = (int)bw; invDropRect_[3] = (int)bh;
    invCloseRect_[0] = (int)(rightX + rightW - 104); invCloseRect_[1] = (int)by; invCloseRect_[2] = 86; invCloseRect_[3] = (int)bh;

    auto drawBtn = [&](const int r[4], const std::string& txt, float cr, float cg, float cb) {
        Quad q; q.rect[0]=r[0]; q.rect[1]=r[1]; q.rect[2]=r[2]; q.rect[3]=r[3];
        q.uv[0]=0; q.uv[1]=0; q.uv[2]=1; q.uv[3]=1; q.solid=true;
        q.tint[0]=cr; q.tint[1]=cg; q.tint[2]=cb; q.tint[3]=1;
        ren->drawSprite(q);
        drawText(txt, (float)r[0] + 16, (float)r[1] + 9, 15, C4(1,1,1,1));
    };
    drawBtn(invUseRect_, locale_.tr("inventory.use"),   0.20f, 0.50f, 0.30f);
    drawBtn(invDropRect_, locale_.tr("inventory.drop"),  0.60f, 0.28f, 0.26f);
    drawBtn(invCloseRect_, locale_.tr("inventory.close"), 0.28f, 0.28f, 0.34f);

    drawText(locale_.tr("inventory.footer_hint"), rightX + 18, rightY + rightH - 76, 13, C4(0.75f,0.82f,0.95f,1));
}

// game.cpp — implementation of Game.
#include "game.h"
#ifdef __EMSCRIPTEN__
  #ifdef WEBGPU
    #include "renderer_webgpu.h"   // WebGPU backend (browser build only)
  #else
    #include "renderer_webgl.h"   // WebGL2 backend (browser build only)
  #endif
#else
#include "renderer.h"         // Vulkan backend (desktop build only)
#endif
#ifndef __EMSCRIPTEN__
#include "vk_util.h"   // Vulkan helpers — desktop build only
#endif
#include <json.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static const float* C4(float a,float b,float c,float d){ static float v[4]; v[0]=a; v[1]=b; v[2]=c; v[3]=d; return v; }

// ---- sprite id order (atlas grid position; must match gen_textures manifest) ----
static const char* SPRITE_ORDER[] = {
    "floor","wall","stairs_up","stairs_down","door_yellow","door_blue","door_red",
    "gem_atk","gem_def","potion_red","potion_blue","coin",
    "key_yellow","key_blue","key_red",
    "player","slime","bat","golem","skeleton","wraith","demon","boss_demonlord",
    "npc_sorcerer","npc_villager","npc_princess","npc_king","npc_handmaiden",
    "exp_up","scroll"          // item icons (added for the inventory UI)
};
static const int N_SPRITES = 29;

bool Game::loadAssets(const std::string& assetDir) {
    dataDir = assetDir;
    // Create the backend renderer. Desktop = Vulkan; Emscripten = WebGL2 or WebGPU.
#ifndef __EMSCRIPTEN__
    ren = new Renderer();                  // Vulkan (Windows / Linux)
#else
  #ifdef WEBGPU
    ren = new WebGPURenderer();           // WebGPU (browser)
  #else
    ren = new WebGLRenderer();            // WebGL2 (browser, default)
  #endif
#endif
    ren->init(1024, 768);
    // load sprites into a single 32x32-uniform atlas (GRID_COLS x GRID_ROWS grid)
    const int SW = 32, SH = 32, COLS = 9, ROWS = 3;
    spriteGridCols = COLS;
    std::vector<std::vector<uint8_t>> layers;
    layers.reserve(N_SPRITES);
    for (int i = 0; i < N_SPRITES; i++) {
        std::string name = SPRITE_ORDER[i];
        std::string path = assetDir + "/sprites/" + name + ".png";
        int w,h,ch; unsigned char* d = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!d) { std::fprintf(stderr, "load fail %s\n", path.c_str()); return false; }
        layers.emplace_back(d, d + w*h*4);
        idToLayer[name] = i;
        stbi_image_free(d);
    }
    // upload full sprite atlas (backend packs the grid + uploads)
    ren->loadSprites(layers, SW, SH);
    // load font atlas
    {
        int w,h,ch; unsigned char* d = stbi_load((assetDir+"/font_atlas.png").c_str(), &w,&h,&ch,4);
        if (!d) { std::fprintf(stderr, "font fail\n"); return false; }
        std::vector<uint8_t> px(d, d + w*h*4); fontW=w; fontH=h;
        ren->loadFont(px, w, h);
        stbi_image_free(d);
        std::ifstream f(assetDir+"/font_atlas.json"); nlohmann::json j; f>>j;
        fontCols = j["cols"]; fontCell = j["cell"];
        for (auto& [ch2, rc] : j["chars"].items()) {
            // decode first codepoint of the (possibly multi-byte UTF-8) key
            uint32_t code = 0; const std::string& ks = ch2; size_t i = 0;
            if (i < ks.size()) {
                unsigned char c0 = (unsigned char)ks[i];
                if (c0 < 0x80) code = c0;
                else if ((c0 & 0xE0) == 0xC0) code = ((c0 & 0x1F) << 6) | (ks[i+1] & 0x3F);
                else if ((c0 & 0xF0) == 0xE0) code = ((c0 & 0x0F) << 12) | ((ks[i+1] & 0x3F) << 6) | (ks[i+2] & 0x3F);
                else if ((c0 & 0xF8) == 0xF0) code = ((c0 & 0x07) << 18) | ((ks[i+1] & 0x3F) << 12) | ((ks[i+2] & 0x3F) << 6) | (ks[i+3] & 0x3F);
            }
            std::array<float,4> a = {rc["u0"], rc["v0"], rc["u1"], rc["v1"]};
            fontMap[code] = a;
        }
    }
    // load enemy templates
    std::ifstream ef(assetDir+"/../data/enemies.json"); nlohmann::json ej; ef>>ej;
    for (auto& [k,v] : ej.items()) enemyTpl[k] = v;
    // load item definitions
    {
        std::ifstream itf(assetDir+"/../data/items.json"); nlohmann::json ij; itf>>ij;
        for (auto& [k,v] : ij.items()) itemDefs[k] = v;
    }
    // init player
    pl.maxhp = 120; pl.hp = 120; pl.atk = 12; pl.def = 4; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    // init SFX subsystem (no-op if no audio device; headless-safe). Disabled on
    // the Emscripten/WebGL build to keep the browser bundle free of audio deps.
#ifndef __EMSCRIPTEN__
    audio.init(assetDir + "/sfx");
#endif
    return true;
}

int Game::spriteLayer(const std::string& id) const {
    auto it = idToLayer.find(id);
    return it == idToLayer.end() ? 0 : it->second;
}

// UV rect for a sprite in the uniform grid atlas.
void Game::spriteUV(int layer, float uv[4]) const {
    int cols = spriteGridCols;
    int gx = layer % cols, gy = layer / cols;
    float u0 = (float)gx / cols, v0 = (float)gy / (float)((N_SPRITES + cols - 1) / cols);
    float u1 = (float)(gx + 1) / cols, v1 = (float)(gy + 1) / (float)((N_SPRITES + cols - 1) / cols);
    uv[0]=u0; uv[1]=v0; uv[2]=u1; uv[3]=v1;
}

Quad Game::spriteQuad(float x, float y, float w, float h, int layer, const float tint[4]) {
    Quad q; q.rect[0]=x; q.rect[1]=y; q.rect[2]=w; q.rect[3]=h;
    spriteUV(layer, q.uv);
    q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
    return q;
}

void Game::loadStage(const std::string& id) {
    curStage = id;
    st = parseStage(dataDir + "/../data/stages/" + id + ".json");
    // derive total stage count from the stages directory (max index)
    totalStages = 1;
    std::string dir = dataDir + "/../data/stages/";
    if (std::filesystem::exists(dir)) {
        for (auto& e : std::filesystem::directory_iterator(dir)) {
            try {
                nlohmann::json j = nlohmann::json::parse(std::ifstream(e.path()));
                int idx = j.value("index", 0);
                if (idx > totalStages) totalStages = idx;
            } catch (...) {}
        }
    }
    // place player at '@' or default
    pl.x = 1; pl.y = (int)st.height - 2;
    for (auto& e : st.entities)
        if (e.raw == "@") { pl.x = e.x; pl.y = e.y; }
}

// Proper UTF-8 decode -> code points, then draw each glyph from font atlas.
static std::u32string utf8_to_utf32(const std::string& s) {
    std::u32string out; size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        uint32_t cp; int len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { i++; continue; }
        for (int k = 1; k < len && i + k < s.size(); k++)
            cp = (cp << 6) | ((unsigned char)s[i + k] & 0x3F);
        out.push_back(cp); i += len;
    }
    return out;
}

void Game::drawText(const std::string& s, float x, float y, float size, const float tint[4]) {
    float cx = x;
    std::u32string cps = utf8_to_utf32(s);
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        if (it == fontMap.end()) { cx += size; continue; }
        auto& uv = it->second;
        Quad q; q.rect[0]=cx; q.rect[1]=y; q.rect[2]=size; q.rect[3]=size;
        q.uv[0]=uv[0]; q.uv[1]=uv[1]; q.uv[2]=uv[2]; q.uv[3]=uv[3];
        q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
        ren->drawText(q);
        cx += size;
    }
}

void Game::drawBar(float x, float y, float w, float h, float frac, const float col[4]) {
    // background
    Quad bg; bg.rect[0]=x; bg.rect[1]=y; bg.rect[2]=w; bg.rect[3]=h;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1;
    bg.tint[0]=0.2f;bg.tint[1]=0.2f;bg.tint[2]=0.25f;bg.tint[3]=1; ren->drawSprite(bg);
    float fw = w * std::max(0.0f, std::min(1.0f, frac));
    if (fw > 0) {
        Quad fg; fg.rect[0]=x; fg.rect[1]=y; fg.rect[2]=fw; fg.rect[3]=h;
        fg.uv[0]=0;fg.uv[1]=0;fg.uv[2]=1;fg.uv[3]=1;
        fg.tint[0]=col[0];fg.tint[1]=col[1];fg.tint[2]=col[2];fg.tint[3]=1; ren->drawSprite(fg);
    }
}

// map a stage cell char to a sprite id (floor/wall/door/stairs)
static std::string cellSprite(char c) {
    switch (c) {
        case '#': return "wall";
        case 'y': return "door_yellow"; case 'b': return "door_blue"; case 'r': return "door_red";
        case 'U': return "stairs_up"; case 'D': return "stairs_down";
        default: return "floor";
    }
}
// entity id -> sprite id
static std::string entSprite(const std::string& id) {
    if (id == "slime") return "slime"; if (id=="bat") return "bat"; if (id=="golem") return "golem";
    if (id=="skeleton") return "skeleton"; if (id=="wraith") return "wraith"; if (id=="demon") return "demon";
    if (id=="demonlord_vorkath") return "boss_demonlord";
    if (id=="gem_atk") return "gem_atk"; if (id=="gem_def") return "gem_def";
    if (id=="potion_red") return "potion_red"; if (id=="potion_blue") return "potion_blue"; if (id=="coin") return "coin";
    if (id=="key_yellow") return "key_yellow"; if (id=="key_blue") return "key_blue"; if (id=="key_red") return "key_red";
    if (id=="villager") return "npc_villager"; if (id=="sorcerer") return "npc_sorcerer";
    if (id=="princess") return "npc_princess"; if (id=="king") return "npc_king"; if (id=="handmaiden") return "npc_handmaiden";
    return "floor";
}

void Game::draw() {
    ren->begin();
    float W = (float)ren->width(), H = (float)ren->height();
    // scene: 13x11 grid, tile size 48, centered
    int gw = st.width, gh = st.height;
    float ts = 48.0f;
    float ox = (W - gw*ts)/2.0f, oy = 60.0f;
    static const float white[4] = {1,1,1,1};
    // tiles
    for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
        char c = st.at(x,y);
        int layer = spriteLayer(cellSprite(c));
        ren->drawSprite(spriteQuad(ox + x*ts, oy + y*ts, ts, ts, layer, white));
    }
    // entities
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        int layer = spriteLayer(entSprite(e.id));
        ren->drawSprite(spriteQuad(ox + e.x*ts + 8, oy + e.y*ts + 8, ts-16, ts-16, layer, white));
    }
    // player
    ren->drawSprite(spriteQuad(ox + pl.x*ts + 8, oy + pl.y*ts + 8, ts-16, ts-16, spriteLayer("player"), white));

    // HUD top bar
    float tint[4] = {1,1,1,1};
    drawText("魔法塔 Tower of the Sorcerer — " + st.name + " (" + std::to_string(st.index) + "/" + std::to_string(totalStages) + ")", 16, 16, 22, tint);
    // HP/ATK/DEF bars + text
    float bx = 16, by = 44;
    drawBar(bx, by, 200, 14, (float)pl.hp/pl.maxhp, C4(0.9f,0.2f,0.2f,1));
    drawText("HP " + std::to_string(pl.hp) + "/" + std::to_string(pl.maxhp), bx+210, by, 18, tint);
    drawText("ATK " + std::to_string(pl.atk) + "  DEF " + std::to_string(pl.def) + "  LV " + std::to_string(pl.lv), bx, by+20, 18, tint);
    drawText("GOLD " + std::to_string(pl.gold) + "  EXP " + std::to_string(pl.exp) + "  鑰匙 Y"+std::to_string(pl.key_yellow)+" B"+std::to_string(pl.key_blue)+" R"+std::to_string(pl.key_red) + "  道具x" + std::to_string(pl.inv.size()) + " (I)", bx, by+42, 16, tint);

    // story note
    drawText(st.story_note, 16, H-30, 16, C4(0.8f,0.85f,1.0f,1));

    // combat overlay
    if (cs.active || (cs.won || cs.log.find("倒下")!=std::string::npos)) {
        // dim
        Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
        dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1;
        dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.55f; ren->drawSprite(dim);
        float cx = W/2 - 250;
        drawText("⚔ 戰鬥！ " + cs.enemy.name, cx, 120, 26, C4(1,0.6f,0.4f,1));
        // player vs enemy boxes
        ren->drawSprite(spriteQuad(cx, 170, 96, 96, spriteLayer("player"), white));
        ren->drawSprite(spriteQuad(cx+350, 170, 96, 96, spriteLayer(cs.enemy.boss?"boss_demonlord":entSprite(cs.enemy.id)), white));
        drawBar(cx, 280, 200, 16, (float)cs.playerHP/pl.maxhp, C4(0.3f,0.9f,0.4f,1));
        drawText("你 HP " + std::to_string(cs.playerHP), cx+210, 280, 18, tint);
        drawBar(cx+350, 280, 200, 16, (float)std::max(0,cs.enemyHP)/cs.enemy.hp, C4(0.9f,0.3f,0.3f,1));
        drawText(cs.enemy.name + " HP " + std::to_string(std::max(0,cs.enemyHP)), cx+560, 280, 18, tint);
        drawText(cs.log, cx, 320, 18, tint);
        if (!cs.active) drawText("（按任意鍵繼續）", cx, 350, 16, C4(1,1,0.6f,1));
    }

    // dialogue overlay
    if (inDialogue) {
        Quad box; box.rect[0]=40; box.rect[1]=H-200; box.rect[2]=W-80; box.rect[3]=170;
        box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1;
        box.tint[0]=0.1f;box.tint[1]=0.12f;box.tint[2]=0.2f;box.tint[3]=0.95f; ren->drawSprite(box);
        std::string txt = dlgData["nodes"][dlgNode]["text"];
        drawText(txt, 60, H-180, 20, tint);
        for (size_t i = 0; i < dlgChoices.size(); i++) {
            drawText("▶ " + dlgChoices[i].first, 60, H-140 + (float)i*24, 18, C4(1,0.9f,0.5f,1));
        }
    }

    // inventory UI (9-grid, extendable) — toggle with I
    drawInventory();

    ren->end();
}

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
        while (pl.exp >= need) { pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30; }
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
    return it == itemDefs.end() ? id : it->second.value("name", id);
}
std::string Game::itemDesc(const std::string& id) const {
    auto it = itemDefs.find(id);
    return it == itemDefs.end() ? "" : it->second.value("desc", "");
}
void Game::drawInventory() {
    if (!invOpen) return;
    float W = (float)ren->width(), H = (float)ren->height();
    // full-screen black splash (transparent 50%) so the player focuses on the item UI
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.5f; ren->drawSprite(dim);

    static const float white[4] = {1,1,1,1};
    int cols = 3, cell = 56, gap = 6;
    int rows = std::max(3, (int)((pl.inv.size() + cols - 1) / cols)); // at least 9 slots
    int gw = cols * cell + (cols - 1) * gap;
    float ox = (W - gw) / 2.0f;
    float oy = H / 2.0f - (rows * cell + (rows - 1) * gap) / 2.0f;

    // panel background
    Quad panel; panel.rect[0]=ox-12; panel.rect[1]=oy-44; panel.rect[2]=gw+24; panel.rect[3]=(rows*cell+(rows-1)*gap)+92;
    panel.uv[0]=0;panel.uv[1]=0;panel.uv[2]=1;panel.uv[3]=1;
    panel.tint[0]=0.12f;panel.tint[1]=0.14f;panel.tint[2]=0.22f;panel.tint[3]=0.96f; ren->drawSprite(panel);

    drawText("背包 Inventory (方向鍵選擇 · Enter 使用 · D 丟棄 · I 關閉)", ox-4, oy-34, 16, white);

    for (int i = 0; i < (int)pl.inv.size(); i++) {
        int r = i / cols, c = i % cols;
        float x = ox + c * (cell + gap), y = oy + r * (cell + gap);
        // slot box
        Quad slot; slot.rect[0]=x; slot.rect[1]=y; slot.rect[2]=cell; slot.rect[3]=cell;
        slot.uv[0]=0;slot.uv[1]=0;slot.uv[2]=1;slot.uv[3]=1;
        slot.tint[0]=0.18f;slot.tint[1]=0.2f;slot.tint[2]=0.28f;slot.tint[3]=1; ren->drawSprite(slot);
        // icon
        ren->drawSprite(spriteQuad(x+8, y+8, cell-16, cell-16, spriteForItem(pl.inv[i]), white));
        // selection highlight
        if (i == invSel) {
            Quad hl; hl.rect[0]=x-2; hl.rect[1]=y-2; hl.rect[2]=cell+4; hl.rect[3]=cell+4;
            hl.uv[0]=0;hl.uv[1]=0;hl.uv[2]=1;hl.uv[3]=1;
            hl.tint[0]=1;hl.tint[1]=0.85f;hl.tint[2]=0.2f;hl.tint[3]=1; ren->drawSprite(hl);
        }
    }
    // empty-slot placeholders to show the 9-grid (extendable) layout
    for (int i = (int)pl.inv.size(); i < cols*rows; i++) {
        int r = i / cols, c = i % cols;
        float x = ox + c * (cell + gap), y = oy + r * (cell + gap);
        Quad slot; slot.rect[0]=x; slot.rect[1]=y; slot.rect[2]=cell; slot.rect[3]=cell;
        slot.uv[0]=0;slot.uv[1]=0;slot.uv[2]=1;slot.uv[3]=1;
        slot.tint[0]=0.1f;slot.tint[1]=0.11f;slot.tint[2]=0.16f;slot.tint[3]=0.9f; ren->drawSprite(slot);
    }

    // description panel for the selected item
    if (!pl.inv.empty()) {
        int si = std::max(0, std::min(invSel, (int)pl.inv.size()-1));
        std::string id = pl.inv[si];
        float dy2 = oy + rows*(cell+gap) + 6;
        drawText(itemName(id), ox-4, dy2, 18, C4(1,0.9f,0.5f,1));
        drawText(itemDesc(id), ox-4, dy2+24, 15, white);
        drawText("按 Enter 使用 / D 丟棄", ox-4, dy2+48, 14, C4(0.7f,1,0.7f,1));
    }
}

void Game::movePlayer(int dx, int dy) {
    if (cs.active || inDialogue) return;
    int nx = pl.x + dx, ny = pl.y + dy;
    char c = st.at(nx, ny);
    if (c == '#') return;
    // doors need keys
    if (c == 'y' && pl.key_yellow <= 0) return;
    if (c == 'b' && pl.key_blue <= 0) return;
    if (c == 'r' && pl.key_red <= 0) return;
    if (c == 'y') pl.key_yellow--;
    if (c == 'b') pl.key_blue--;
    if (c == 'r') pl.key_red--;
    pl.x = nx; pl.y = ny;
    audio.play("walk");
    // check entity at new cell
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        if (e.x == nx && e.y == ny) {
            if (e.kind.rfind("monster:",0)==0) {
                EnemyInst en;
                auto& t = enemyTpl[e.id];
                en.id=e.id; en.name=t["name"]; en.hp=t["hp"]; en.atk=t["atk"]; en.def=t["def"];
                en.exp=t["exp"]; en.gold=t["gold"]; en.x=e.x; en.y=e.y; en.boss=t.value("boss",false);
                startCombat(en);
                return;
            } else if (e.kind.rfind("item:",0)==0) {
                // Keys/coins apply immediately (not stored in the 9-grid UI).
                // Usable items (gems/potions/exp/scroll) go into the inventory.
                bool immediate = (e.id.rfind("key_",0)==0) || e.id=="coin";
                if (immediate) applyItem(e.id);
                else pl.inv.push_back(e.id);
                e.consumed = true;
                st.tiles[e.y][e.x] = '.'; // clear from grid
            } else if (e.kind.rfind("npc:",0)==0) {
                // start dialogue
                dlgNpc = "enemy_"+e.id; // fallback; real npc ids below
                if (e.id=="villager") dlgNpc="villager_elder";
                else if (e.id=="sorcerer") dlgNpc="sorcerer_teacher";
                else if (e.id=="king") dlgNpc="king_lieutenant";
                else if (e.id=="princess") dlgNpc=(curStage=="stage_11")?"princess_victory":"princess_liora";
                else if (e.id=="handmaiden") dlgNpc="handmaiden";
                startDialogue(dlgNpc);
            } else if (c=='U' && !st.up.empty()) { loadStage(st.up); return; }
            else if (c=='D' && !st.down.empty()) { loadStage(st.down); return; }
        }
    }
    // stairs check (cell char)
    if (c=='U' && !st.up.empty()) loadStage(st.up);
    else if (c=='D' && !st.down.empty()) loadStage(st.down);
}

void Game::startDialogue(const std::string& npc) {
    std::ifstream f(dataDir + "/../data/dialogue/" + npc + ".json");
    if (!f) { inDialogue=false; return; }
    f >> dlgData;
    inDialogue = true; dlgNode = dlgData["start"];
    enterNode(dlgNode);
    audio.play("dialogue_popup");
}
void Game::enterNode(const std::string& node) {
    auto& n = dlgData["nodes"][node];
    dlgChoices.clear();
    for (auto& c : n["choices"]) {
        dlgChoices.push_back({c["label"], c["next"].is_null()?std::string(""):(std::string)c["next"]});
    }
}
void Game::chooseDialogue(int idx) {
    if (!inDialogue) return;
    if (idx < 0 || idx >= (int)dlgChoices.size()) return;
    audio.play("confirm_click");
    auto& ch = dlgChoices[idx];
    if (ch.second.empty()) { inDialogue = false; return; }
    dlgNode = ch.second; enterNode(dlgNode);
}

void Game::interact() {
    if (cs.active || inDialogue) return;
    // find NPC on player's cell or adjacent
    for (auto& e : st.entities) {
        if (e.consumed) continue;
        if (e.kind.rfind("npc:",0)!=0) continue;
        if (e.x==pl.x && e.y==pl.y) {
            std::string npc;
            if (e.id=="villager") npc="villager_elder";
            else if (e.id=="sorcerer") npc="sorcerer_teacher";
            else if (e.id=="king") npc="king_lieutenant";
            else if (e.id=="princess") npc=(curStage=="stage_11")?"princess_victory":"princess_liora";
            else if (e.id=="handmaiden") npc="handmaiden";
            else continue;
            startDialogue(npc);
            return;
        }
    }
}

void Game::startCombat(const EnemyInst& e) {
    cs.enemy = e; cs.playerHP = pl.hp; cs.enemyHP = e.hp; cs.round = 0; cs.active = true; cs.ticks = 0; cs.won = false;
    cs.log = "戰鬥開始！";
}
void Game::resolveCombatRound() {
    int dmgToEnemy = std::max(1, pl.atk - cs.enemy.def);
    int hits = (cs.enemyHP + dmgToEnemy - 1) / dmgToEnemy;
    int dmgToPlayer = (hits - 1) * std::max(1, cs.enemy.atk - pl.def);
    cs.enemyHP -= dmgToEnemy;       // this round's hit
    cs.playerHP -= std::max(1, cs.enemy.atk - pl.def); // enemy retaliates once
    cs.round++;
    audio.play("player_attack");
    if (dmgToPlayer > 0) audio.play("enemy_attack");
    if (cs.enemyHP <= 0) {
        cs.active = false; cs.won = true;
        pl.hp = cs.playerHP;
        pl.gold += cs.enemy.gold; pl.exp += cs.enemy.exp;
        // level up
        int need = pl.lv * 30;
        while (pl.exp >= need) { pl.exp -= need; pl.lv++; pl.atk += 2; pl.def += 1; pl.maxhp += 10; need = pl.lv*30; }
        cs.log = "勝利！獲得 EXP " + std::to_string(cs.enemy.exp);
        // remove monster entity from stage
        for (auto& e : st.entities) if (e.x==cs.enemy.x && e.y==cs.enemy.y && e.id==cs.enemy.id) e.consumed=true;
        st.tiles[cs.enemy.y][cs.enemy.x] = '.';
        if (cs.enemy.boss) { cs.log = "你擊敗了 Vorkath！安穩之星重燃。"; loadStage("stage_11"); }
    } else if (cs.playerHP <= 0) {
        cs.active = false; cs.won = false;
        pl.hp = pl.maxhp/2; // respawn at stage start
        cs.log = "你倒下了……返回本層起點。";
        loadStage(curStage); // reset monsters/items
    }
}
void Game::update(int dtMs) {
    if (cs.active) {
        cs.ticks += dtMs;
        if (cs.ticks >= 700) { cs.ticks = 0; resolveCombatRound(); }
    }
}

void Game::saveFrame(const std::string& path) {
    ren->savePNG(path);
}

// game.cpp — implementation of Game.
#include "game.h"
#include "node.h"   // 2D scene-graph Node (parent/child + local/world transform)
#include "scene.h"  // render binding: GameObject / SpriteNode / TextNode / FullScreenSplash

// TextNode draws through Game's font; bind it once to the live Game instance.
namespace { Game* g_textGame = nullptr; }
void toms_TextNodeDraw(const std::string& s, float x, float y, float sz, const float* t) {
    if (g_textGame) g_textGame->drawTextPublic(s, x, y, sz, t);
}
namespace toms { TextNode::DrawFn TextNode::Draw = ::toms_TextNodeDraw; }
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

// Robust JSON file load. NOTE: Emscripten's libc++ std::ifstream is unreliable
// for preloaded files (tellg reports the right size but read/>> return empty), so
// we use C stdio (fopen/fread) which reads preloaded data correctly, then json::parse.
static nlohmann::json readJsonFile(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { fprintf(stderr, "[readJsonFile] cannot open %s\n", path.c_str()); return {}; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (sz <= 0) { fclose(fp); fprintf(stderr, "[readJsonFile] empty %s\n", path.c_str()); return {}; }
    std::string buf((size_t)sz, '\0');
    size_t rd = fread(&buf[0], 1, (size_t)sz, fp);
    fclose(fp);
    if (rd == 0) { fprintf(stderr, "[readJsonFile] read 0 bytes %s\n", path.c_str()); return {}; }
    try { return nlohmann::json::parse(buf); }
    catch (const std::exception& e) {
        fprintf(stderr, "[readJsonFile] parse error %s: %s\n", path.c_str(), e.what());
        return {};
    }
}
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
static const int N_SPRITES = 30;

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
    // ---- build the font atlas ----
    // Desktop: runtime TTF -> atlas via stb_truetype (no offline PIL step needed).
    // Web: load the small pre-baked font_atlas.png + .json (TTF is too big to ship).
#ifndef __EMSCRIPTEN__
    {
        std::string ttf = assetDir + "/wqy-zenhei.ttc";
        if (const char* e = std::getenv("TOMS_FONT")) ttf = e;
        else if (!std::filesystem::exists(ttf))
            ttf = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc";
        // collect every codepoint used by shipped JSON + HUD labels
        std::vector<std::string> jsonFiles;
        for (auto& p : std::filesystem::recursive_directory_iterator(dataDir + "/../data"))
            if (p.path().extension() == ".json") jsonFiles.push_back(p.path().string());
        // Comprehensive pre-bake: every glyph the game can display (dialogue +
        // store + HUD + gamepad labels) so drawText never triggers a runtime
        // glyph bake -> no mid-frame font-texture re-upload -> no WebGL abort.
        std::string hud = " #'()*+,-./0123456789:<>@ABCDEFGHIKLOPRSTUVXYZ^_abcdefghijklmnopqrstuvwxy·—…→▶、。「」『』一上下不世並中主久之也了予亡交人什仇仍他付以件份但低住你使來侍便保信們倒值做傳價先入全公共兵具再凋凡出切列別利到則前副力加動勝化匙十升卡印即卷去反取受口可史右司吃合同向否吧吸吾告周命和咒咕唯商啟嘶嚕囚回國圖土在地堅塔墓外大奪女她如姆字存學它守安官定室宮家容寄寶封將對小少展層屬嵌巨巫已希師帶幣平年序店座廳廷弱強形影後徑得從復必怎怕思性怨怪恐恢恨惡意感懂懼成我戰所才打承把拉拯拳持接提揭援損撲撼擇擊擋攀收攻放效救敗教敢散敵數方於明星是晶暗書曾最會有望本村林枚果枯格森樓標機橫檻歐正此歸殿毅每民水永求決沃沉泉法注洞活流消淨深源準滿災為焉煉燃營物獲獻王玩現瓶生用留疊白的目直看真睡知石碎確示祝神祭禁禍福禦穩穴窟立章第等糊紅純紙級終給經緣繼續翅習老者而聖能自與莉莊萊萎著藍藏藥處蝙蝠血行被要見親角解言託記試話該語謝證護變讓買購贈走起足路跳踏身軍軸輕輪迎送透這逝進遇道達選還那重量金錢鍵鑰鑲長門閉開閣關防降陛除雙離零需露靈頂須頭願驗骷髏體高鬥魂魔麼黃黎黑點！（），：；？";
        std::vector<uint32_t> cps = Font::collectFromFiles(jsonFiles, hud);
        font_ = std::make_shared<Font>("game-font");
        if (!font_->buildFromFile(ttf, cps, 32, 24)) {
            std::fprintf(stderr, "font build failed: %s\n", ttf.c_str()); return false;
        }
        ren->loadFont(font_->atlas(), font_->atlasW(), font_->atlasH());
        fontW = (int)font_->atlasW(); fontH = (int)font_->atlasH();
        fontCols = 32; fontCell = 32;
        for (uint32_t cp : cps) {
            const std::array<float,4>* uv = font_->uv(cp);
            if (uv) fontMap[cp] = *uv;
        }
    }
#else
    {
        int w,h,ch; unsigned char* d = stbi_load((assetDir+"/font_atlas.png").c_str(), &w,&h,&ch,4);
        if (!d) { std::fprintf(stderr, "font fail\n"); return false; }
        std::vector<uint8_t> px(d, d + w*h*4); fontW=w; fontH=h;
        ren->loadFont(px, w, h);
        stbi_image_free(d);
        std::ifstream f(assetDir+"/font_atlas.png"); // (PNG loaded above; json via readJsonFile)
        nlohmann::json j = readJsonFile(assetDir+"/font_atlas.json");
        fontCols = j["cols"]; fontCell = j["cell"];
        for (auto& [ch2, rc] : j["chars"].items()) {
            uint32_t code = 0; const std::string& ks = ch2;
            // New atlas: keys are "U+XXXX" (or "UXXXX") hex codepoints (unambiguous).
            if (ks.size() >= 3 && ks[0] == 'U') {
                size_t hexStart = (ks[1] == '+') ? 2 : 1;
                code = (uint32_t)std::strtoul(ks.substr(hexStart).c_str(), nullptr, 16);
            } else {
                // Legacy atlas: literal UTF-8 char key -> decode first codepoint.
                size_t i = 0;
                if (i < ks.size()) {
                    unsigned char c0 = (unsigned char)ks[i];
                    if (c0 < 0x80) code = c0;
                    else if ((c0 & 0xE0) == 0xC0) code = ((c0 & 0x1F) << 6) | (ks[i+1] & 0x3F);
                    else if ((c0 & 0xF0) == 0xE0) code = ((c0 & 0x0F) << 12) | ((ks[i+1] & 0x3F) << 6) | (ks[i+2] & 0x3F);
                    else if ((c0 & 0xF8) == 0xF0) code = ((c0 & 0x07) << 18) | ((ks[i+1] & 0x3F) << 12) | ((ks[i+2] & 0x3F) << 6) | (ks[i+3] & 0x3F);
                }
            }
            if (code == 0) continue;
            std::array<float,4> a = {rc["u0"], rc["v0"], rc["u1"], rc["v1"]};
            fontMap[code] = a;
        }
    }
#endif

    // load enemy templates
    std::ifstream ef(assetDir+"/../data/enemies.json"); // (read via readJsonFile)
    nlohmann::json ej = readJsonFile(assetDir+"/../data/enemies.json");
    for (auto& [k,v] : ej.items()) enemyTpl[k] = v;
    // load item definitions
    {
        std::ifstream itf(assetDir+"/../data/items.json"); // (read via readJsonFile)
        nlohmann::json ij = readJsonFile(assetDir+"/../data/items.json");
        for (auto& [k,v] : ij.items()) itemDefs[k] = v;
    }
    // load store definitions (data/store.json) -> unlock stage + items + cost rule
    loadStore(assetDir);
    // init player
    pl.maxhp = 120; pl.hp = 120; pl.atk = 12; pl.def = 4; pl.gold = 0; pl.exp = 0; pl.lv = 1;
    // init SFX subsystem (no-op if no audio device; headless-safe). Disabled on
    // the Emscripten/WebGL build to keep the browser bundle free of audio deps.
#ifndef __EMSCRIPTEN__
    audio.init(assetDir + "/sfx");
#endif
    g_textGame = this;   // bind TextNode text drawing to this instance
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
    // Resolve the stage JSON. Data ids in connect.up/down use "stage_02" (underscore)
    // while the shipped files are named "stage02.json" (no underscore) — and the
    // initial load uses "stage01". Normalize so both forms resolve instead of
    // opening a non-existent path (which makes ifstream fail -> parse throw -> crash
    // when the player steps on stairs / a warp tile).
    std::string path = dataDir + "/../data/stages/" + id + ".json";
    if (!std::filesystem::exists(path)) {
        std::string noUs = id;
        noUs.erase(std::remove(noUs.begin(), noUs.end(), '_'), noUs.end());
        std::string alt = dataDir + "/../data/stages/" + noUs + ".json";
        if (std::filesystem::exists(alt)) path = alt;
    }
    st = parseStage(path);
    // derive total stage count from the stages directory (max index)
    totalStages = 1;
    std::string dir = dataDir + "/../data/stages/";
    if (std::filesystem::exists(dir)) {
        for (auto& e : std::filesystem::directory_iterator(dir)) {
            try {
                nlohmann::json j = readJsonFile(e.path().string());
                int idx = j.value("index", 0);
                if (idx > totalStages) totalStages = idx;
            } catch (...) {}
        }
    }
    // store unlock: when entering the configured unlock stage for the first time,
    // mark the shop as unlocked and pop a one-time "shop unlocked!" dialog.
    if (st.index >= storeUnlockStage_ && !storeUnlocked_) {
        storeUnlocked_ = true;
        storeUnlockDlg = true;
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
    bool fontChanged = false;
    // First glyph's top bearing establishes the baseline reference (FM79979
    // FreetypeGlypth.cpp L145: YOffset = -firstGlyph->Offset.y). All glyphs are
    // then placed top-aligned to that, at their NATURAL size — no cell-centering,
    // no shrink. This is exactly how the reference lays text out.
    float firstTop = 0;
    const std::array<float,4>* m0 = nullptr;
    if (font_ && !cps.empty()) {
        m0 = font_->glyphMetrics(cps[0]);
        if (m0) firstTop = (*m0)[1];   // topBearing
    }
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        if (it == fontMap.end()) {
            // realtime fallback: try to pull the glyph from the primary font or a
            // system font and bake it into the atlas on the fly.
            if (font_ && font_->ensure(cp)) {
                const std::array<float,4>* uv = font_->uv(cp);
                if (uv) {
                    fontMap[cp] = *uv; it = fontMap.find(cp); fontChanged = true;
                    if (cp == cps[0]) { m0 = font_->glyphMetrics(cp); if (m0) firstTop = (*m0)[1]; }
                }
            }
        }
        if (it == fontMap.end()) { cx += size; continue; }   // truly unknown glyph
        auto& uv = it->second;
        // Design metrics from the font are stored in ATLAS pixels (relative to the
        // bake cell, cell_). To place the quad at the requested `size` we must
        // scale them up to display pixels: scale = size / cell_. This is exactly
        // what the reference does (its Size.x/Size.y are at m_iFontSize, then the
        // whole vertex buffer is scaled by m_fScale). Without this, every glyph
        // renders at cell_ px instead of `size`, and the per-glyph topBearing (which
        // differs for e.g. 'e' vs 'h') shifts glyphs by the wrong amount -> they
        // fall off the baseline and clip.
        float leftB = 0, topB = 0, sx = size, sy = size;
        float s = (font_ ? size / (float)font_->cellSize() : 1.0f);
        if (font_) {
            if (auto* m = font_->glyphMetrics(cp)) {
                leftB = (*m)[0]*s; topB = (*m)[1]*s; sx = (*m)[2]*s; sy = (*m)[3]*s;
            }
        }
        float qw = sx, qh = sy;                 // natural glyph size (display px)
        // Vertical placement matches FM79979 RenderFont: vertex.y = YOffset +
        // glyphOffset.y = -firstOffset.y + glyphOffset.y. In our stored metrics
        // (glyphM_[1] = -top_stb = top_freetype = -Offset.y_ref) this is
        //   qy = y + (glyphM_[first][1] - glyphM_[cp][1]) * s
        // (NOT y - firstTop*s + glyphM_[cp][1]*s, which would negate the bearing).
        float qx = cx + leftB;                  // advance then place by bearing
        float qy = y + (firstTop*s) - topB;     // baseline-aligned (ref sign)
        Quad q; q.rect[0]=qx; q.rect[1]=qy; q.rect[2]=qw; q.rect[3]=qh;
        q.uv[0]=uv[0]; q.uv[1]=uv[1]; q.uv[2]=uv[2]; q.uv[3]=uv[3];
        q.tint[0]=tint[0]; q.tint[1]=tint[1]; q.tint[2]=tint[2]; q.tint[3]=tint[3];
        ren->drawText(q);
        cx += leftB + sx;                       // reference advance = Offset.x + Size.x
    }
    // If any glyph was added to the atlas this frame, re-upload it so it shows up.
    if (fontChanged && font_) {
        ren->updateFont(font_->atlas(), font_->atlasW(), font_->atlasH());
    }
}

// Public wrapper so TextNode (render-bound text) can draw through Game's font.
void Game::drawTextPublic(const std::string& s, float x, float y, float sz, const float* t) {
    drawText(s, x, y, sz, t);
}

float Game::measureText(const std::string& s, float size) const {
    float w = 0;
    std::u32string cps = utf8_to_utf32(s);
    float s2 = (font_ ? size / (float)font_->cellSize() : 1.0f);
    for (uint32_t cp : cps) {
        auto it = fontMap.find(cp);
        float leftB = 0, sx = (font_ ? (float)font_->cellSize() : 32.0f);
        if (font_ && it != fontMap.end()) {
            if (auto* m = font_->glyphMetrics(cp)) { leftB = (*m)[0]; sx = (*m)[2]; }
        }
        w += (leftB + sx) * s2;   // matches drawText advance (Offset.x + Size.x), scaled to display px
    }
    return w;
}

void Game::drawBar(float x, float y, float w, float h, float frac, const float col[4]) {
    // background
    Quad bg; bg.rect[0]=x; bg.rect[1]=y; bg.rect[2]=w; bg.rect[3]=h;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.2f;bg.tint[1]=0.2f;bg.tint[2]=0.25f;bg.tint[3]=1; ren->drawSprite(bg);
    float fw = w * std::max(0.0f, std::min(1.0f, frac));
    if (fw > 0) {
        Quad fg; fg.rect[0]=x; fg.rect[1]=y; fg.rect[2]=fw; fg.rect[3]=h;
        fg.uv[0]=0;fg.uv[1]=0;fg.uv[2]=1;fg.uv[3]=1; fg.solid=true;
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
    // ---- node 1: STAGE (map tiles + entities + player) ----
    ren->setNode(NODE_STAGE);
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

    // ---- node 2: CHARACTER (HUD top bar: stats / HP-ATK-DEF / story note) ----
    ren->setNode(NODE_CHAR);
    float tint[4] = {1,1,1,1};
    if (!storeModal()) {
    // HUD top bar
    drawText("魔法塔 Tower of the Sorcerer — " + st.name + " (" + std::to_string(st.index) + "/" + std::to_string(totalStages) + ")", 16, 16, 22, tint);
    // HP/ATK/DEF bars + text
    float bx = 16, by = 44;
    drawBar(bx, by, 200, 14, (float)pl.hp/pl.maxhp, C4(0.9f,0.2f,0.2f,1));
    drawText("HP " + std::to_string(pl.hp) + "/" + std::to_string(pl.maxhp), bx+210, by, 18, tint);
    drawText("ATK " + std::to_string(pl.atk) + "  DEF " + std::to_string(pl.def) + "  LV " + std::to_string(pl.lv), bx, by+20, 18, tint);
    drawText("GOLD " + std::to_string(pl.gold) + "  EXP " + std::to_string(pl.exp) + "  鑰匙 Y"+std::to_string(pl.key_yellow)+" B"+std::to_string(pl.key_blue)+" R"+std::to_string(pl.key_red) + "  道具x" + std::to_string(pl.inv.size()) + " (I)", bx, by+42, 16, tint);

    // story note
    drawText(st.story_note, 16, H-30, 16, C4(0.8f,0.85f,1.0f,1));
    }

    // ---- node 4: BATTLE (combat overlay) ----
    ren->setNode(NODE_BATTLE);
    // combat overlay (suppressed while the store modal is the topmost layer, so the
    // death hint "你倒下了" never bleeds through at the same level as the store)
    if (!storeModal() && (hideMask & 1) == 0 && (cs.active || (cs.won || cs.log.find("倒下")!=std::string::npos))) {
        drawFocusSplash();
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

    // ---- node 3: TALK (dialogue overlay) ----
    ren->setNode(NODE_TALK);
    // dialogue overlay (suppressed while store modal is open)
    if (!storeModal() && (hideMask & 2) == 0 && inDialogue) {
        drawFocusSplash();
        Quad box; box.rect[0]=40; box.rect[1]=H-200; box.rect[2]=W-80; box.rect[3]=170;
        box.uv[0]=0;box.uv[1]=0;box.uv[2]=1;box.uv[3]=1; box.solid=true;
        box.tint[0]=0.1f;box.tint[1]=0.12f;box.tint[2]=0.2f;box.tint[3]=0.95f; ren->drawSprite(box);
        std::string txt = dlgData["nodes"][dlgNode]["text"];
        drawText(txt, 60, H-180, 20, tint);
        for (size_t i = 0; i < dlgChoices.size(); i++) {
            float ty = H-140 + (float)i*24;
            if ((int)i == dlgSel) drawText("▶ " + dlgChoices[i].first, 60, ty, 18, C4(1,1.0f,0.6f,1));   // highlighted choice (gamepad-selected)
            else                   drawText("  " + dlgChoices[i].first, 60, ty, 18, C4(1,0.9f,0.5f,1));
        }
    }

    // inventory UI (9-grid, extendable) — toggle with I (suppressed while store modal is open)
    ren->setNode(NODE_CHAR);   // inventory is a character/UI screen
    if (!storeModal() && (hideMask & 4) == 0) drawInventory();

    // ---- store system (NODE_STORE) — drawn LAST so it is the topmost layer ----
    ren->setNode(NODE_STORE);
    if (!storeOpen) drawStoreIcon();          // HUD icon is always visible (dim+locked until unlocked)
    // reset per-frame button-rect scratch (rebuilt during draw)
    storeBtnRects_.clear();
    if (storeUnlockDlg) drawStoreUnlockDialog();
    else if (storeOpen) drawStoreUI();
    drawStoreToast();

    drawGamepad();   // on-canvas touch controls; auto-hidden while a top-layer UI (battle/dialogue/inventory/store) is active

    ren->end();
}

// ---- on-canvas virtual gamepad (web build, touch only) ----
// Single source of truth: same rects used for drawing AND hit-testing.
// Buffer space is 1024x768, y-down (matches the rest of draw()).
struct GPadBtn { int id; float x,y,w,h; const char* label; float col[4]; int dx,dy; };
static const GPadBtn GP[] = {
    {0, 104,510, 72,72, "^", 0.40f,0.45f,0.55f,0.70f, 0,-1}, // up
    {1, 104,626, 72,72, "v", 0.40f,0.45f,0.55f,0.70f, 0, 1}, // down
    {2,  20,568, 72,72, "<", 0.40f,0.45f,0.55f,0.70f,-1, 0}, // left
    {3, 188,568, 72,72, ">", 0.40f,0.45f,0.55f,0.70f, 1, 0}, // right
    {4, 880,626, 68,68, "A", 0.30f,0.70f,0.35f,0.80f, 0, 0}, // interact / use
    {5, 792,566, 64,64, "B", 0.75f,0.30f,0.30f,0.80f, 0, 0}, // drop
    {6, 880,526, 56,56, "I", 0.30f,0.35f,0.75f,0.80f, 0, 0}, // inventory
    {7,  20,490, 48,48, "P", 0.25f,0.25f,0.25f,0.85f, 0, 0}, // toggle: show/hide gamepad
};
static const int GP_N = 8;

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

void Game::handleTouch(float px, float py, int phase) {
    // Guard: ignore invalid coordinates (NaN from a zero-size canvas rect, or
    // out-of-range). Prevents bad state / out-of-bounds in the hit-test below.
    if (!(px == px) || !(py == py)) return;            // NaN check
    if (px < 0 || px > 1024 || py < 0 || py > 768) return;
    // --- Store overlay: route ALL taps to storeClick. Store buttons (icon / buy / close)
    //     are NOT gamepad rects, so this must run BEFORE the gamepad hit-test below,
    //     otherwise taps on store UI hit `id<0` and are dropped. ---
    if (storeModal()) { if (phase == 0) storeClick(px, py); return; }
    // Normal play: a tap on the store icon (top-right) opens the shop.
    if (!modalActive() && phase == 0) {
        storeClick(px, py);
        if (storeOpen) return;   // icon tapped -> store opened; consume this tap
    }
    int id = -1;
    for (int i = 0; i < GP_N; i++) {
        const GPadBtn& b = GP[i];
        if (px >= b.x && px <= b.x + b.w && py >= b.y && py <= b.y + b.h) { id = i; break; }
    }
    if (id < 0) return;
    if (id == 7) { if (phase == 0) gpOn = !gpOn; return; }  // toggle show/hide
    if (phase == 2) return;                 // touchend on a game button: nothing
    // Victory screen: tap anywhere to dismiss (cs.won is set on win and must be cleared
    // or the combat overlay keeps painting forever -> "stuck after defeating enemy").
    if (cs.won && phase == 0) { cs.won = false; return; }
    // HUD stats line (carries the "(I)" inventory indicator) — tap to open inventory.
    if (!inventoryOpen() && !inDialogue && !modalActive() && phase == 0) {
        if (py >= 74 && py <= 104 && px >= 16 && px <= 560) { toggleInventory(); return; }
    }
    const GPadBtn& b = GP[id];
    if (inventoryOpen()) {
        if (id <= 3) invMoveSel(b.dx, b.dy);
        else if (id == 4 && phase == 0) invUseSelected();
        else if (id == 5 && phase == 0) invDropSelected();
        else if (id == 6 && phase == 0) toggleInventory();
        return;
    }
    if (inDialogue) {                        // dialogue: tap a choice line to select+confirm
        int n = (int)dlgChoices.size();
        // Tap directly on a choice line (drawn at y = H-140 + i*24, x >= 60) selects & confirms it.
        if (phase == 0 && n > 0) {
            float W = (float)ren->width(), H = (float)ren->height();
            for (int i = 0; i < n; i++) {
                float ty = H - 140 + (float)i * 24;
                if (py >= ty - 12 && py <= ty + 12 && px >= 56 && px <= W - 56) {
                    dlgSel = i; chooseDialogue(i); return;
                }
            }
            // tap elsewhere on the dialogue box = advance to next (keep current selection)
            chooseDialogue(dlgSel); return;
        }
        // (gamepad D-pad still works too)
        if (id == 0 && phase == 0 && n > 0) dlgSel = (dlgSel - 1 + n) % n;   // up = prev choice
        else if (id == 1 && phase == 0 && n > 0) dlgSel = (dlgSel + 1) % n;  // down = next choice
        else if (id == 4 && phase == 0) chooseDialogue(dlgSel);               // A = select
        else if (id == 5 && phase == 0) inDialogue = false;                  // B = close
        return;
    }
    if (cs.active) {                         // combat in progress: tap = continue / next phase
        if (phase == 0) interact();
        return;
    }
    if (modalActive()) {                     // other modal (store handled above): block world input
        return;
    }
    if (id <= 3 && phase == 0) movePlayer(b.dx, b.dy);   // dpad: one step per tap (phase 1 repeat is for hold, ignored here so a tap = exactly 1 step)
    else if (id == 4 && phase == 0) interact();
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
void Game::drawFocusSplash() {
    if (std::getenv("TOMS_NOSPLASH")) return;  // TEST: toggle splash off to isolate bug
    float W = (float)ren->width(), H = (float)ren->height();
    // full-screen black splash (transparent 50%) so the player focuses on the
    // active modal scene (combat / dialogue / inventory).
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.8f; ren->drawSprite(dim);
}
// store modal uses a full-screen black splash at alpha 0.5 (per design) and is the
// topmost layer, drawn last in Game::draw(); so it sits above all other overlays.
void Game::drawStoreSplash() {
    if (std::getenv("TOMS_NOSPLASH")) return;
    float W = (float)ren->width(), H = (float)ren->height();
    Quad dim; dim.rect[0]=0; dim.rect[1]=0; dim.rect[2]=W; dim.rect[3]=H;
    dim.uv[0]=0;dim.uv[1]=0;dim.uv[2]=1;dim.uv[3]=1; dim.solid=true;
    dim.tint[0]=0;dim.tint[1]=0;dim.tint[2]=0;dim.tint[3]=0.5f; ren->drawSprite(dim);
}
void Game::drawInventory() {
    if (!invOpen) return;
    float W = (float)ren->width(), H = (float)ren->height();
    int cols = 3, cell = 56, gap = 6;
    int rows = std::max(3, (int)((pl.inv.size() + cols - 1) / cols)); // at least 9 slots
    int gw = cols * cell + (cols - 1) * gap;
    float ox = (W - gw) / 2.0f;
    float oy = H / 2.0f - (rows * cell + (rows - 1) * gap) / 2.0f;

    // ---- Build the item UI as a RENDER-BOUND node tree ----
    // modalRoot is visible ONLY while the inventory is open: setting it false
    // (toggleInventory) skips the whole subtree (splash + panel + slots) in one
    // flag. It is the LAST node drawn in Game::draw(), so it renders on top.
    toms::GameObject modalRoot("itemUI");
    modalRoot.SetVisible(invOpen);

    // 1) full-screen focus splash (child of modalRoot -> hidden when closed too)
    toms::FullScreenSplash splash;
    splash.w = W; splash.h = H; splash.tint[3] = 0.8f;
    modalRoot.AddChild(&splash);

    // 2) panel node positioned at screen center; children are laid out in its LOCAL space
    toms::GameObject panel("panel");
    panel.SetLocalPosition(glm::vec2(ox, oy));
    modalRoot.AddChild(&panel);

    // panel background (a SpriteNode = common render: just assign size+tint)
    toms::SpriteNode panelBg;
    panelBg.solid = true;   // flat color panel, not a textured sprite
    panelBg.SetLocalPosition(glm::vec2(-12.0f, -44.0f));
    panelBg.size[0] = (float)(gw + 24); panelBg.size[1] = (float)(rows*cell + (rows-1)*gap + 92);
    panelBg.tint[0]=0.12f; panelBg.tint[1]=0.14f; panelBg.tint[2]=0.22f; panelBg.tint[3]=0.96f;
    panel.AddChild(&panelBg);

    // title text (render-bound TextNode)
    toms::TextNode title;
    title.SetLocalPosition(glm::vec2(-4.0f, -34.0f)); title.size = 16.0f; title.text = "背包 Inventory (方向鍵選擇 · Enter 使用 · D 丟棄 · I 關閉)";
    panel.AddChild(&title);

    // 3) slot nodes (SpriteNode = common render). Icon is a child SpriteNode of the slot.
    std::vector<toms::SpriteNode> slots(rows * cols, toms::SpriteNode());
    std::vector<toms::SpriteNode> icons(rows * cols, toms::SpriteNode());
    std::vector<toms::SpriteNode> hi(rows * cols, toms::SpriteNode());
    for (int i = 0; i < (int)slots.size(); i++) {
        int r = i / cols, c = i % cols;
        slots[i].SetLocalPosition(glm::vec2((float)(c*(cell+gap)), (float)(r*(cell+gap))));
        slots[i].size[0] = slots[i].size[1] = (float)cell;
        slots[i].solid = true;   // colored cell background, not a texture
        bool occupied = (i < (int)pl.inv.size());
        if (occupied) { slots[i].tint[0]=0.18f; slots[i].tint[1]=0.2f; slots[i].tint[2]=0.28f; slots[i].tint[3]=1; }
        else          { slots[i].tint[0]=0.1f; slots[i].tint[1]=0.11f; slots[i].tint[2]=0.16f; slots[i].tint[3]=0.9f; }
        panel.AddChild(&slots[i]);
        if (occupied) {
            // icon: child of slot, offset (8,8), size cell-16, uv = item icon
            int layer = spriteForItem(pl.inv[i]);
            icons[i].SetLocalPosition(glm::vec2(8.0f, 8.0f));
            icons[i].size[0] = icons[i].size[1] = (float)(cell-16);
            spriteUV(layer, icons[i].uv);
            slots[i].AddChild(&icons[i]);
            if (i == invSel) {
                hi[i].SetLocalPosition(glm::vec2(-2.0f, -2.0f));
                hi[i].size[0] = hi[i].size[1] = (float)(cell+4);
                hi[i].solid = true;   // colored highlight border
                hi[i].tint[0]=1; hi[i].tint[1]=0.85f; hi[i].tint[2]=0.2f; hi[i].tint[3]=1;
                slots[i].AddChild(&hi[i]);
            }
        }
    }

    // 4) description texts (render-bound) under the panel
    if (!pl.inv.empty()) {
        int si = std::max(0, std::min(invSel, (int)pl.inv.size()-1));
        std::string id = pl.inv[si];
        float dy = (float)(rows*(cell+gap) + 6);
        toms::TextNode dn; dn.SetLocalPosition(glm::vec2(-4.0f, dy));        dn.size=18; dn.text=itemName(id); dn.tint[0]=1; dn.tint[1]=0.9f; dn.tint[2]=0.5f; panel.AddChild(&dn);
        toms::TextNode dd; dd.SetLocalPosition(glm::vec2(-4.0f, dy+24));     dd.size=15; dd.text=itemDesc(id); panel.AddChild(&dd);
        toms::TextNode dh; dh.SetLocalPosition(glm::vec2(-4.0f, dy+48));     dh.size=14; dh.text="按 Enter 使用 / D 丟棄"; dh.tint[0]=0.7f; dh.tint[1]=1; dh.tint[2]=0.7f; panel.AddChild(&dh);
    }

    // Single render call for the whole modal UI (visibility-culled subtree).
    modalRoot.RenderTree(ren);
}

// ---------- store system ----------
void Game::loadStore(const std::string& assetDir) {
    nlohmann::json j = readJsonFile(assetDir + "/../data/store.json");
    if (j.is_null() || j.empty()) { return; }
    if (j.contains("store_title")) storeTitle_ = j["store_title"].get<std::string>();
    if (j.contains("unlockstage")) storeUnlockStage_ = j["unlockstage"].get<int>();
    for (auto& it : j["items"]) {
        StoreItemDef d;
        d.id = it.value("id", "");
        d.name = it.value("name", d.id);
        d.sprite = it.value("sprite", "");
        // strip a trailing ".png" if present so it matches the atlas sprite id
        if (d.sprite.size() > 4 && d.sprite.substr(d.sprite.size()-4) == ".png")
            d.sprite = d.sprite.substr(0, d.sprite.size()-4);
        d.icon_path = it.value("icon_path", "");
        d.desc = it.value("desc", "");
        if (it.contains("effect")) d.effect = it["effect"];
        d.effect_text = it.value("effect_text", "");
        d.cost_base = it.value("cost_base", 2);
        d.cost_multiplier = it.value("cost_multiplier", 2);
        d.purchases = 0;
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

void Game::storeCardRects(std::vector<float>& rects) const {
    rects.clear();
    float W = (float)ren->width(), H = (float)ren->height();
    int n = (int)storeItems_.size();
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
    drawText(storeUnlocked_ ? "商店" : "商店🔒", (float)x-4, (float)y + s + 2, 12, C4(1,1,1, storeUnlocked_?1:0.4f));
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
    drawText("道具商店已經開放！", bx+30, by+34, 24, C4(1,0.9f,0.5f,1));
    drawText("你現在可以在關卡中點擊右上角的商店圖示來購買道具。", bx+30, by+74, 15, C4(1,1,1,1));
    // confirm button (bottom-right of box)
    float btnW = 120, btnH = 40;
    float btnX = bx + bw - btnW - 24, btnY = by + bh - btnH - 20;
    storeUnlockBtnRect_[0] = (int)btnX; storeUnlockBtnRect_[1] = (int)btnY;
    storeUnlockBtnRect_[2] = (int)btnW; storeUnlockBtnRect_[3] = (int)btnH;
    Quad btn; btn.rect[0]=btnX; btn.rect[1]=btnY; btn.rect[2]=btnW; btn.rect[3]=btnH;
    btn.uv[0]=0;btn.uv[1]=0;btn.uv[2]=1;btn.uv[3]=1; btn.solid=true;
    btn.tint[0]=0.2f;btn.tint[1]=0.5f;btn.tint[2]=0.3f;btn.tint[3]=1; ren->drawSprite(btn);
    drawText("確定 (Enter/點擊)", btnX+12, btnY+11, 15, C4(1,1,1,1));
}

void Game::drawStoreUI() {
    float W = (float)ren->width(), H = (float)ren->height();
    drawStoreSplash();
    // panel background
    Quad bg; bg.rect[0]=40; bg.rect[1]=40; bg.rect[2]=W-80; bg.rect[3]=H-80;
    bg.uv[0]=0;bg.uv[1]=0;bg.uv[2]=1;bg.uv[3]=1; bg.solid=true;
    bg.tint[0]=0.08f;bg.tint[1]=0.1f;bg.tint[2]=0.16f;bg.tint[3]=0.96f; ren->drawSprite(bg);
    // title + gold
    drawText(storeTitle_, 60, 64, 26, C4(1,0.9f,0.5f,1));
    drawText("金錢 GOLD: " + std::to_string(pl.gold), W-260, 64, 20, C4(1,0.95f,0.4f,1));
    drawText("（方向鍵/數字選擇 · Enter 購買 · Esc 關閉）", 60, 94, 14, C4(0.8f,0.85f,1,0.9f));

    std::vector<float> rects; storeCardRects(rects);
    for (int i = 0; i < (int)storeItems_.size(); i++) {
        float cx = rects[i*4], cy = rects[i*4+1], cw = rects[i*4+2], ch = rects[i*4+3];
        const StoreItemDef& d = storeItems_[i];
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
        drawText(d.name, cx + 16, cy + 124, 26, C4(1,1,1,1));
        drawText(d.desc, cx + 16, cy + 162, 16, C4(0.85f,0.9f,1,1));
        drawText("效果: " + d.effect_text, cx + 16, cy + 188, 18, C4(0.6f,1,0.7f,1));
        drawText("已購買 x" + std::to_string(d.purchases), cx + 16, cy + 216, 15, C4(0.7f,0.7f,0.8f,1));
        // price tag
        drawText("價格: " + std::to_string(cost) + " G", cx + 16, cy + 240, 22, C4(1,0.95f,0.4f,1));
        // buy button
        float btnW = cw - 32, btnH = 38;
        float btnX = cx + 16, btnY = cy + ch - btnH - 12;
        storeBtnRects_.push_back(btnX); storeBtnRects_.push_back(btnY); storeBtnRects_.push_back(btnW); storeBtnRects_.push_back(btnH);
        Quad btn; btn.rect[0]=btnX; btn.rect[1]=btnY; btn.rect[2]=btnW; btn.rect[3]=btnH;
        btn.uv[0]=0;btn.uv[1]=0;btn.uv[2]=1;btn.uv[3]=1; btn.solid=true;
        btn.tint[0]=0.2f;btn.tint[1]=0.5f;btn.tint[2]=0.3f;btn.tint[3]=1; ren->drawSprite(btn);
        drawText("購買 (Enter)", btnX + 16, btnY + 10, 16, C4(1,1,1,1));
    }
    // close button (top-right corner of panel)
    float cw2 = 90, ch2 = 34;
    storeCloseRect_[0] = (int)(W-40-cw2); storeCloseRect_[1] = 56; storeCloseRect_[2] = (int)cw2; storeCloseRect_[3] = (int)ch2;
    Quad cbtn; cbtn.rect[0]=storeCloseRect_[0]; cbtn.rect[1]=storeCloseRect_[1]; cbtn.rect[2]=cw2; cbtn.rect[3]=ch2;
    cbtn.uv[0]=0;cbtn.uv[1]=0;cbtn.uv[2]=1;cbtn.uv[3]=1; cbtn.solid=true;
    cbtn.tint[0]=0.5f;cbtn.tint[1]=0.2f;cbtn.tint[2]=0.2f;cbtn.tint[3]=1; ren->drawSprite(cbtn);
    drawText("關閉 X", storeCloseRect_[0]+14, storeCloseRect_[1]+9, 16, C4(1,1,1,1));
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
        // buy buttons
        std::vector<float> rects; storeCardRects(rects);
        int nBtn = (int)storeBtnRects_.size() / 4;
        for (int i = 0; i < nBtn && i < (int)storeItems_.size(); i++) {
            float bx = storeBtnRects_[i*4], by = storeBtnRects_[i*4+1], bw = storeBtnRects_[i*4+2], bh = storeBtnRects_[i*4+3];
            if (x >= bx && x <= bx+bw && y >= by && y <= by+bh) {
                buyStoreItem(i); return;
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
    int n = (int)storeItems_.size();
    if (key == 27) { closeStore(); return; }                       // Esc closes
    if (key == 13 || key == 32) { buyStoreItem(storeSel_); return; } // Enter/Space buys
    if (key == 262 || key == 263) {                                // right/left arrow
        storeSel_ += (key == 262) ? 1 : -1;
        if (storeSel_ < 0) storeSel_ = n-1; if (storeSel_ >= n) storeSel_ = 0;
        return;
    }
    if (key >= '1' && key <= '9') {                                // number keys select
        int idx = key - '1';
        if (idx < n) storeSel_ = idx;
    }
}

void Game::buyStoreItem(int idx) {
    if (idx < 0 || idx >= (int)storeItems_.size()) return;
    StoreItemDef& d = storeItems_[idx];
    int cost = d.liveCost();
    if (pl.gold < cost) {
        // not enough gold -> toast + shake (simple effect), no purchase
        toastMsg_ = "金錢不足！需要 " + std::to_string(cost) + " G";
        toastTimer_ = 1600;
        shakeTimer_ = 350;
        audio.play("deny");
        return;
    }
    pl.gold -= cost;
    // apply effect immediately (per spec: bought item is used right away)
    const nlohmann::json& eff = d.effect;
    if (eff.contains("hp"))  pl.hp   = std::min(pl.maxhp, pl.hp + (int)eff["hp"]);
    if (eff.contains("str")) pl.atk  += (int)eff["str"];
    if (eff.contains("def")) pl.def  += (int)eff["def"];
    d.purchases++;
    toastMsg_ = "購買成功：" + d.name + "！";
    toastTimer_ = 1400;
    audio.play("get_item");
}


void Game::movePlayer(int dx, int dy) {
    if (modalActive()) return;   // any modal overlay (combat/dialogue/inventory) blocks world input
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
    dlgData = readJsonFile(dataDir + "/../data/dialogue/" + npc + ".json");
    if (dlgData.is_null() || dlgData.empty()) { inDialogue=false; return; }
    if (!dlgData.contains("start") || !dlgData["start"].is_string()) { inDialogue = false; return; }
    inDialogue = true; dlgNode = dlgData["start"];
    dlgSel = 0;
    enterNode(dlgNode);
    audio.play("dialogue_popup");
}
void Game::enterNode(const std::string& node) {
    dlgChoices.clear();
    if (!dlgData.contains("nodes") || !dlgData["nodes"].contains(node)) {
        inDialogue = false; return;   // missing node -> close safely (no nlohmann throw / abort)
    }
    const auto& n = dlgData["nodes"][node];
    if (!n.is_object() || !n.contains("choices")) return;
    for (auto& c : n["choices"]) {
        if (!c.is_object()) continue;
        std::string label = c.contains("label") && c["label"].is_string() ? (std::string)c["label"] : "";
        std::string next  = c.contains("next")  && !c["next"].is_null()  ? (std::string)c["next"]  : "";
        dlgChoices.push_back({label, next});
    }
    dlgSel = 0;
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
    if (modalActive()) return;   // any modal overlay blocks world interaction
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
        cs.log = "";        // clear so the "倒下" combat overlay stops painting after respawn
    }
}
void Game::update(int dtMs) {
    if (cs.active) {
        cs.ticks += dtMs;
        if (cs.ticks >= 700) { cs.ticks = 0; resolveCombatRound(); }
    }
    // store UI timers (toast / shake) tick down regardless of combat
    if (toastTimer_ > 0) { toastTimer_ -= dtMs; if (toastTimer_ < 0) toastTimer_ = 0; }
    if (shakeTimer_ > 0) { shakeTimer_ -= dtMs; if (shakeTimer_ < 0) shakeTimer_ = 0; }
}

void Game::saveFrame(const std::string& path) {
    ren->savePNG(path);
}

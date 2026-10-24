// anim_character_test.cpp -- character clips played by any character (anim_clip.h fitCharacter, sprite_mesh.h
// visibleBox; docs/engine/05_ANIMATION.md "Characters"): the visible box of a sprite, where the hand goes on another body, the
// fitted copy (sprite, hand and its keys, the nudge), and the shipped archer_bow.anim fitted to every player sprite of
// the game atlas: the bow stays in front of each body at the same share of its height. Runs in assets/. No GPU.
#include "anim_player.h"
#include "atlas_file.h"
#include "sprite_mesh.h"
#include "vfs.h"

#include <stb_image.h>

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace toms::anim;

namespace {
int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_fail; } } while (0)
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

toms::SpriteBox box(float l, float t, float r, float b) {
    toms::SpriteBox x;
    x.left = l; x.top = t; x.right = r; x.bottom = b; x.origW = 32; x.origH = 32;
    return x;
}

Clip rig() {
    Clip c;
    c.name = "shoot";
    c.root.name = "archer";
    Node lean;
    lean.name = "lean";
    Node body;
    body.name = "body";
    body.sprite = "game:player";
    body.hasPivot = true;
    body.pivot = {0.5f, 1.0f};
    Node hand;
    hand.name = "hand";
    hand.pos = {9, -14, 0};
    Key<glm::vec3> k;
    k.t = 0.5f;
    k.v = {10, -15, 0};
    hand.posKeys.push_back(k);
    lean.children = {body, hand};
    c.root.children.push_back(lean);
    return c;
}
}  // namespace

int main() {
    // ---- the visible box ----
    std::vector<uint8_t> px(16 * 8 * 4, 0);
    for (int y = 2; y < 7; y++)
        for (int x = 3; x < 11; x++) px[((size_t)y * 16 + x) * 4 + 3] = 200;
    const toms::SpriteBox b = toms::visibleBox(px.data(), 16, 16, 8, 4, 1, 32, 32);
    CHECK(near(b.left, 7) && near(b.top, 3) && near(b.right, 15) && near(b.bottom, 8) && near(b.origW, 32),
          "the visible pixels, moved by the trim offset, in original pixels");
    std::vector<uint8_t> clear(8 * 8 * 4, 0);
    CHECK(toms::visibleBox(clear.data(), 8, 8, 8, 0, 0, 8, 8).empty(), "no visible pixel: an empty box");

    // ---- where the hand goes ----
    // "player" is visible x 8..25, y 3..32; the knight girl x 4..28, y 2..32 (both feet on the bottom row).
    const toms::SpriteBox player = box(8, 3, 25, 32), knight = box(4, 2, 28, 32);
    const glm::vec2 off = characterHandOffset({9, -14}, player, knight);
    CHECK(near(off.x, 3), "x: the same distance in front of the wider body's right edge (25 -> 28)");
    // height: 15 of the player's 29 visible pixels above its feet -> the same share of the knight's 30
    CHECK(near(off.y, -30.0f + (15.0f / 29.0f) * 30.0f + 14.0f), "y: the same share of the visible body's height");
    CHECK(near(characterHandOffset({9, -14}, player, player).x, 0) && near(characterHandOffset({9, -14}, player, player).y, 0),
          "the same body: no move");
    CHECK(near(characterHandOffset({9, -14}, player, toms::SpriteBox()).y, 0), "an unknown body: no move");

    // ---- the fitted copy ----
    std::map<std::string, toms::SpriteBox> boxes = {{"player", player}, {"player_knight_girl", knight}};
    auto boxOf = [&](const std::string& ref, toms::SpriteBox& out) {
        auto it = boxes.find(parseSpriteRef(ref).name);
        if (it == boxes.end()) return false;
        out = it->second;
        return true;
    };
    const Clip made = rig();
    const Clip fitted = fitCharacter(made, "player_knight_girl", boxOf);
    const Node* fb = findNode(fitted.root, "body");
    const Node* fh = findNode(fitted.root, "hand");
    CHECK(fb && fb->sprite == "game:player_knight_girl", "the body draws the new character, from the body's atlas");
    CHECK(fh && near(fh->pos.x, 12) && near(fh->pos.y, -14 + off.y), "the hand moved");
    CHECK(fh && fh->posKeys.size() == 1 && near(fh->posKeys[0].v.x, 13) && near(fh->posKeys[0].v.y, -15 + off.y),
          "the hand's keys moved with it");
    CHECK(made.root.children[0].children[1].pos == glm::vec3(9, -14, 0), "the clip itself is not changed");
    const Clip nudged = fitCharacter(made, "player_knight_girl", boxOf, {0, -1});
    CHECK(near(findNode(nudged.root, "hand")->pos.y, -15 + off.y), "a nudge (character_fit.json) is added");
    const Clip unknown = fitCharacter(made, "game:somebody", boxOf);
    CHECK(findNode(unknown.root, "body")->sprite == "game:somebody" && findNode(unknown.root, "hand")->pos == glm::vec3(9, -14, 0),
          "an unmeasured character: the sprite changes, the hand stays");

    // ---- the binding: a held clip drawn at the character's hand (CharacterRig) ----
    {
        toms::AtlasFile atlas2;
        std::string err;
        toms::parseAtlas("page.png\nsize: 64, 64\nplayer\n  bounds: 0, 0, 32, 32\nthing\n  bounds: 32, 0, 8, 8\n", atlas2, &err);
        AtlasSet set(atlas2);
        Clip body = rig();
        Node& bodyHand = body.root.children[0].children[1];
        bodyHand.scale = {0.5f, 0.5f, 1};   // the hand's scale: held things at half size
        Key<glm::vec3> h0, h1;               // the hand moves 2 px right over 1 s
        h0.v = {9, -14, 0};
        h1.t = 1;
        h1.v = {11, -14, 0};
        bodyHand.posKeys = {h0, h1};
        Clip held;
        held.name = "shoot";
        held.root.name = "thing";
        held.root.sprite = "thing";
        struct Recorder : IRenderer {
            std::vector<Quad> quads;
            void init(uint32_t, uint32_t) override {}
            void loadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
            void loadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
            void begin() override {}
            void drawSprite(const Quad& q) override { quads.push_back(q); }
            void end() override {}
            uint32_t width() const override { return 1; }
            uint32_t height() const override { return 1; }
        } r;
        CharacterRig rigPlayer;
        rigPlayer.play(&body, &held);
        rigPlayer.draw(&r, set, set, placement(100, 200));
        CHECK(r.quads.size() == 2, "the character, then the held thing");
        if (r.quads.size() == 2) {
            // the 8-pixel thing, centred on the hand (100 + 9, 200 - 14) at half size: 4 x 4 pixels
            const Quad& q = r.quads[1];
            CHECK(near(q.rect[0], 107) && near(q.rect[1], 184) && near(q.rect[2], 4) && near(q.rect[3], 4),
                  "the held clip is drawn at the hand, with the hand's scale");
        }
        // The hand moves (its keys): the held thing goes with it.
        rigPlayer.update(1000);
        r.quads.clear();
        rigPlayer.draw(&r, set, set, placement(100, 200));
        CHECK(r.quads.size() == 2 && near(r.quads[1].rect[0], 109) && near(r.quads[1].rect[1], 184), "it follows the hand's keys");
        std::vector<NodePose> poses;
        evaluate(body, 0, poses);
        glm::mat3 at(1.0f);
        CHECK(attachPlacement(poses, "hand", placement(0, 0), at) && near(at[2][0], 9) && near(at[2][1], -14),
              "attachPlacement: the hand's world transform");
        CHECK(!attachPlacement(poses, "paw", placement(0, 0), at), "attachPlacement: no such node");
    }

    // ---- the shipped archer clips, every character of the game atlas ----
    std::string animText, atlasText, png;
    AnimFile archer;
    toms::AtlasFile atlas;
    if (toms::vfsReadAll("media/anim/archer_bow.anim", animText) && parseAnim(animText, archer) &&
        toms::vfsReadAll("media/atlas/game.atlas", atlasText) && toms::parseAtlas(atlasText, atlas) &&
        toms::vfsReadAll("media/atlas/game.png", png)) {
        int w = 0, h = 0, ch = 0;
        stbi_uc* page = stbi_load_from_memory((const stbi_uc*)png.data(), (int)png.size(), &w, &h, &ch, 4);
        std::map<std::string, toms::SpriteBox> game;
        for (const toms::AtlasRegion& r : atlas.regions)
            if (page && r.name.rfind("player", 0) == 0 && !r.rotated && r.x + r.w <= w && r.y + r.h <= h)
                game[r.name] = toms::visibleBox(page + ((size_t)r.y * w + r.x) * 4, w, r.w, r.h, r.offX, r.offY, r.origW, r.origH);
        if (page) stbi_image_free(page);
        auto gameBox = [&](const std::string& ref, toms::SpriteBox& out) {
            auto it = game.find(parseSpriteRef(ref).name);
            if (it == game.end()) return false;
            out = it->second;
            return true;
        };
        CHECK(archer.clips.size() == 4 && archer.find("idle") && archer.find("charge") && archer.find("attack") && archer.find("miss"),
              "archer_bow.anim: idle, charge, attack, miss");
        CHECK(game.size() >= 11, "the game atlas has the player sprites");
        const Clip* charge = archer.find("charge");
        const Node* madeHand = charge ? findNode(charge->root, "hand") : nullptr;
        toms::SpriteBox ref;
        const bool haveRef = gameBox("player", ref);
        int checked = 0, off = 0;
        for (const auto& [name, vis] : game) {
            if (!charge || !madeHand || !haveRef) break;
            const Clip c = fitCharacter(*charge, name, gameBox);
            const Node* hand = findNode(c.root, "hand");
            const float shareMade = (madeHand->pos.y - (ref.top - ref.origH)) / (ref.bottom - ref.top);
            const float share = (hand->pos.y - (vis.top - vis.origH)) / (vis.bottom - vis.top);
            const float gapMade = madeHand->pos.x - (ref.right - ref.origW * 0.5f);
            const float gap = hand->pos.x - (vis.right - vis.origW * 0.5f);
            checked++;
            if (!near(share, shareMade, 1e-3f) || !near(gap, gapMade, 1e-3f)) {
                std::printf("  %s: hand share %.3f (made %.3f), gap to the right edge %.2f (made %.2f)\n", name.c_str(), share,
                            shareMade, gap, gapMade);
                off++;
            }
        }
        CHECK(checked >= 11 && off == 0, "every character holds the bow at the same place on its body");
    } else {
        std::printf("FAIL: media/anim/archer_bow.anim or media/atlas/game.* not found (run in assets/)\n");
        ++g_fail;
    }

    if (g_fail) { std::printf("anim_character_test: %d failure(s)\n", g_fail); return 1; }
    std::printf("anim_character_test: OK\n");
    return 0;
}

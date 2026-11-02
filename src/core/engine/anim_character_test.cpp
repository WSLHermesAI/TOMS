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
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)
bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-3f) { return std::fabs(l_fA - l_fB) <= l_fEps; }

toms::SpriteBox f_Box(float l_fL, float l_fT, float l_fR, float l_fB) {
    toms::SpriteBox l_oX;
    l_oX.m_fLeft = l_fL; l_oX.m_fTop = l_fT; l_oX.m_fRight = l_fR; l_oX.m_fBottom = l_fB; l_oX.m_fOrigW = 32; l_oX.m_fOrigH = 32;
    return l_oX;
}

Clip f_Rig() {
    Clip l_oC;
    l_oC.m_strName = "shoot";
    l_oC.m_oRoot.m_strName = "archer";
    Node l_oLean;
    l_oLean.m_strName = "lean";
    Node l_oBody;
    l_oBody.m_strName = "body";
    l_oBody.m_strSprite = "game:player";
    l_oBody.m_bHasPivot = true;
    l_oBody.m_v2Pivot = {0.5f, 1.0f};
    Node l_oHand;
    l_oHand.m_strName = "hand";
    l_oHand.m_v3Pos = {9, -14, 0};
    Key<glm::vec3> l_oK;
    l_oK.m_fT = 0.5f;
    l_oK.m_oV = {10, -15, 0};
    l_oHand.m_vPosKeys.push_back(l_oK);
    l_oLean.m_vChildren = {l_oBody, l_oHand};
    l_oC.m_oRoot.m_vChildren.push_back(l_oLean);
    return l_oC;
}
}  // namespace

int main() {
    // ---- the visible box ----
    std::vector<uint8_t> l_vPx(16 * 8 * 4, 0);
    for (int l_iY = 2; l_iY < 7; l_iY++)
        for (int l_iX = 3; l_iX < 11; l_iX++) l_vPx[((size_t)l_iY * 16 + l_iX) * 4 + 3] = 200;
    const toms::SpriteBox l_oB = toms::f_VisibleBox(l_vPx.data(), 16, 16, 8, 4, 1, 32, 32);
    CHECK(f_Near(l_oB.m_fLeft, 7) && f_Near(l_oB.m_fTop, 3) && f_Near(l_oB.m_fRight, 15) && f_Near(l_oB.m_fBottom, 8) && f_Near(l_oB.m_fOrigW, 32),
          "the visible pixels, moved by the trim offset, in original pixels");
    std::vector<uint8_t> l_vClear(8 * 8 * 4, 0);
    CHECK(toms::f_VisibleBox(l_vClear.data(), 8, 8, 8, 0, 0, 8, 8).f_Empty(), "no visible pixel: an empty box");

    // ---- where the hand goes ----
    // "player" is visible x 8..25, y 3..32; the knight girl x 4..28, y 2..32 (both feet on the bottom row).
    const toms::SpriteBox l_oPlayer = f_Box(8, 3, 25, 32), l_oKnight = f_Box(4, 2, 28, 32);
    const glm::vec2 l_v2Off = f_CharacterHandOffset({9, -14}, l_oPlayer, l_oKnight);
    CHECK(f_Near(l_v2Off.x, 3), "x: the same distance in front of the wider body's right edge (25 -> 28)");
    // height: 15 of the player's 29 visible pixels above its feet -> the same share of the knight's 30
    CHECK(f_Near(l_v2Off.y, -30.0f + (15.0f / 29.0f) * 30.0f + 14.0f), "y: the same share of the visible body's height");
    CHECK(f_Near(f_CharacterHandOffset({9, -14}, l_oPlayer, l_oPlayer).x, 0) && f_Near(f_CharacterHandOffset({9, -14}, l_oPlayer, l_oPlayer).y, 0),
          "the same body: no move");
    CHECK(f_Near(f_CharacterHandOffset({9, -14}, l_oPlayer, toms::SpriteBox()).y, 0), "an unknown body: no move");

    // ---- the fitted copy ----
    std::map<std::string, toms::SpriteBox> l_mapBoxes = {{"player", l_oPlayer}, {"player_knight_girl", l_oKnight}};
    auto l_fnBoxOf = [&](const std::string& l_strRef, toms::SpriteBox& l_oOut) {
        auto l_itIt = l_mapBoxes.find(f_ParseSpriteRef(l_strRef).m_strName);
        if (l_itIt == l_mapBoxes.end()) return false;
        l_oOut = l_itIt->second;
        return true;
    };
    const Clip l_oMade = f_Rig();
    const Clip l_oFitted = f_FitCharacter(l_oMade, "player_knight_girl", l_fnBoxOf);
    const Node* l_pFb = f_FindNode(l_oFitted.m_oRoot, "body");
    const Node* l_pFh = f_FindNode(l_oFitted.m_oRoot, "hand");
    CHECK(l_pFb && l_pFb->m_strSprite == "game:player_knight_girl", "the body draws the new character, from the body's atlas");
    CHECK(l_pFh && f_Near(l_pFh->m_v3Pos.x, 12) && f_Near(l_pFh->m_v3Pos.y, -14 + l_v2Off.y), "the hand moved");
    CHECK(l_pFh && l_pFh->m_vPosKeys.size() == 1 && f_Near(l_pFh->m_vPosKeys[0].m_oV.x, 13) && f_Near(l_pFh->m_vPosKeys[0].m_oV.y, -15 + l_v2Off.y),
          "the hand's keys moved with it");
    CHECK(l_oMade.m_oRoot.m_vChildren[0].m_vChildren[1].m_v3Pos == glm::vec3(9, -14, 0), "the clip itself is not changed");
    const Clip l_oNudged = f_FitCharacter(l_oMade, "player_knight_girl", l_fnBoxOf, {0, -1});
    CHECK(f_Near(f_FindNode(l_oNudged.m_oRoot, "hand")->m_v3Pos.y, -15 + l_v2Off.y), "a nudge (character_fit.json) is added");
    const Clip l_oUnknown = f_FitCharacter(l_oMade, "game:somebody", l_fnBoxOf);
    CHECK(f_FindNode(l_oUnknown.m_oRoot, "body")->m_strSprite == "game:somebody" && f_FindNode(l_oUnknown.m_oRoot, "hand")->m_v3Pos == glm::vec3(9, -14, 0),
          "an unmeasured character: the sprite changes, the hand stays");

    // ---- the binding: a held clip drawn at the character's hand (CharacterRig) ----
    {
        toms::AtlasFile l_oAtlas2;
        std::string l_strErr;
        toms::f_ParseAtlas("page.png\nsize: 64, 64\nplayer\n  bounds: 0, 0, 32, 32\nthing\n  bounds: 32, 0, 8, 8\n", l_oAtlas2, &l_strErr);
        AtlasSet l_oSet(l_oAtlas2);
        Clip l_oBody = f_Rig();
        Node& l_oBodyHand = l_oBody.m_oRoot.m_vChildren[0].m_vChildren[1];
        l_oBodyHand.m_v3Scale = {0.5f, 0.5f, 1};   // the hand's scale: held things at half size
        Key<glm::vec3> l_oH0, l_oH1;               // the hand moves 2 px right over 1 s
        l_oH0.m_oV = {9, -14, 0};
        l_oH1.m_fT = 1;
        l_oH1.m_oV = {11, -14, 0};
        l_oBodyHand.m_vPosKeys = {l_oH0, l_oH1};
        Clip l_oHeld;
        l_oHeld.m_strName = "shoot";
        l_oHeld.m_oRoot.m_strName = "thing";
        l_oHeld.m_oRoot.m_strSprite = "thing";
        struct Recorder : IRenderer {
            std::vector<Quad> m_vQuads;
            void f_Init(uint32_t, uint32_t) override {}
            void f_LoadSprites(const std::vector<std::vector<uint8_t>>&, uint32_t, uint32_t) override {}
            void f_LoadSpriteAtlas(const std::vector<uint8_t>&, uint32_t, uint32_t, const std::string&) override {}
            void begin() override {}
            void f_DrawSprite(const Quad& l_oQ) override { m_vQuads.push_back(l_oQ); }
            void end() override {}
            uint32_t f_Width() const override { return 1; }
            uint32_t f_Height() const override { return 1; }
        } l_oR;
        CharacterRig l_oRigPlayer;
        l_oRigPlayer.f_Play(&l_oBody, &l_oHeld);
        l_oRigPlayer.f_Draw(&l_oR, l_oSet, l_oSet, f_Placement(100, 200));
        CHECK(l_oR.m_vQuads.size() == 2, "the character, then the held thing");
        if (l_oR.m_vQuads.size() == 2) {
            // the 8-pixel thing, centred on the hand (100 + 9, 200 - 14) at half size: 4 x 4 pixels
            const Quad& l_oQ = l_oR.m_vQuads[1];
            CHECK(f_Near(l_oQ.m_arrRect[0], 107) && f_Near(l_oQ.m_arrRect[1], 184) && f_Near(l_oQ.m_arrRect[2], 4) && f_Near(l_oQ.m_arrRect[3], 4),
                  "the held clip is drawn at the hand, with the hand's scale");
        }
        // The hand moves (its keys): the held thing goes with it.
        l_oRigPlayer.f_Update(1000);
        l_oR.m_vQuads.clear();
        l_oRigPlayer.f_Draw(&l_oR, l_oSet, l_oSet, f_Placement(100, 200));
        CHECK(l_oR.m_vQuads.size() == 2 && f_Near(l_oR.m_vQuads[1].m_arrRect[0], 109) && f_Near(l_oR.m_vQuads[1].m_arrRect[1], 184), "it follows the hand's keys");
        std::vector<NodePose> l_vPoses;
        f_Evaluate(l_oBody, 0, l_vPoses);
        glm::mat3 l_mat3At(1.0f);
        CHECK(f_AttachPlacement(l_vPoses, "hand", f_Placement(0, 0), l_mat3At) && f_Near(l_mat3At[2][0], 9) && f_Near(l_mat3At[2][1], -14),
              "attachPlacement: the hand's world transform");
        CHECK(!f_AttachPlacement(l_vPoses, "paw", f_Placement(0, 0), l_mat3At), "attachPlacement: no such node");
    }

    // ---- the shipped archer clips, every character of the game atlas ----
    std::string l_strAnimText, l_strAtlasText, l_strPng;
    AnimFile l_oArcher;
    toms::AtlasFile l_oAtlas;
    if (toms::f_VfsReadAll("media/anim/archer_bow.anim", l_strAnimText) && f_ParseAnim(l_strAnimText, l_oArcher) &&
        toms::f_VfsReadAll("media/atlas/game.atlas", l_strAtlasText) && toms::f_ParseAtlas(l_strAtlasText, l_oAtlas) &&
        toms::f_VfsReadAll("media/atlas/game.png", l_strPng)) {
        int l_iW = 0, l_iH = 0, l_iCh = 0;
        stbi_uc* l_pPage = stbi_load_from_memory((const stbi_uc*)l_strPng.data(), (int)l_strPng.size(), &l_iW, &l_iH, &l_iCh, 4);
        std::map<std::string, toms::SpriteBox> l_mapGame;
        for (const toms::AtlasRegion& l_oR : l_oAtlas.m_vRegions)
            if (l_pPage && l_oR.m_strName.rfind("player", 0) == 0 && !l_oR.m_bRotated && l_oR.m_iX + l_oR.m_iW <= l_iW && l_oR.m_iY + l_oR.m_iH <= l_iH)
                l_mapGame[l_oR.m_strName] = toms::f_VisibleBox(l_pPage + ((size_t)l_oR.m_iY * l_iW + l_oR.m_iX) * 4, l_iW, l_oR.m_iW, l_oR.m_iH, l_oR.m_iOffX, l_oR.m_iOffY, l_oR.m_iOrigW, l_oR.m_iOrigH);
        if (l_pPage) stbi_image_free(l_pPage);
        auto l_fnGameBox = [&](const std::string& l_strRef, toms::SpriteBox& l_oOut) {
            auto l_itIt = l_mapGame.find(f_ParseSpriteRef(l_strRef).m_strName);
            if (l_itIt == l_mapGame.end()) return false;
            l_oOut = l_itIt->second;
            return true;
        };
        CHECK(l_oArcher.m_vClips.size() == 5 && l_oArcher.f_Find("idle") && l_oArcher.f_Find("charge") && l_oArcher.f_Find("attack") && l_oArcher.f_Find("miss") &&
                  l_oArcher.f_Find("attack_big"),
              "archer_bow.anim: idle, charge, attack, miss, attack_big");
        CHECK(l_mapGame.size() >= 11, "the game atlas has the player sprites");
        const Clip* l_pCharge = l_oArcher.f_Find("charge");
        const Node* l_pMadeHand = l_pCharge ? f_FindNode(l_pCharge->m_oRoot, "hand") : nullptr;
        toms::SpriteBox l_oRef;
        const bool l_bHaveRef = l_fnGameBox("player", l_oRef);
        int l_iChecked = 0, l_iOff = 0;
        for (const auto& [l_strName, l_oVis] : l_mapGame) {
            if (!l_pCharge || !l_pMadeHand || !l_bHaveRef) break;
            const Clip l_oC = f_FitCharacter(*l_pCharge, l_strName, l_fnGameBox);
            const Node* l_pHand = f_FindNode(l_oC.m_oRoot, "hand");
            const float l_fShareMade = (l_pMadeHand->m_v3Pos.y - (l_oRef.m_fTop - l_oRef.m_fOrigH)) / (l_oRef.m_fBottom - l_oRef.m_fTop);
            const float l_fShare = (l_pHand->m_v3Pos.y - (l_oVis.m_fTop - l_oVis.m_fOrigH)) / (l_oVis.m_fBottom - l_oVis.m_fTop);
            const float l_fGapMade = l_pMadeHand->m_v3Pos.x - (l_oRef.m_fRight - l_oRef.m_fOrigW * 0.5f);
            const float l_fGap = l_pHand->m_v3Pos.x - (l_oVis.m_fRight - l_oVis.m_fOrigW * 0.5f);
            l_iChecked++;
            if (!f_Near(l_fShare, l_fShareMade, 1e-3f) || !f_Near(l_fGap, l_fGapMade, 1e-3f)) {
                std::printf("  %s: hand share %.3f (made %.3f), gap to the right edge %.2f (made %.2f)\n", l_strName.c_str(), l_fShare,
                            l_fShareMade, l_fGap, l_fGapMade);
                l_iOff++;
            }
        }
        CHECK(l_iChecked >= 11 && l_iOff == 0, "every character holds the bow at the same place on its body");
    } else {
        std::printf("FAIL: media/anim/archer_bow.anim or media/atlas/game.* not found (run in assets/)\n");
        ++g_iFail;
    }

    if (g_iFail) { std::printf("anim_character_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("anim_character_test: OK\n");
    return 0;
}

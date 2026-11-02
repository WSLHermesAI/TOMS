// atlas_core_test -- tests for tools/atlas/core (CTest: atlas.core).
//
//   packer      no overlaps, inside the page, padding kept, pinned rects exact, deterministic
//   build       trim, children (nested, trimmed parents), dedupe, bake, variants, errors
//   formats     every exporter read back (the .atlas through the game's own atlas_file.h) and the
//               pixels each sprite draws compared with its source image
//   golden      tests/data/*.pi (the old PI editor's output) -> project -> rebuilt .pi draws the
//               same pixels. ATLAS_PI_CORPUS=<folder> runs this over every .pi below that folder.
#include "atlas_build.h"
#include "atlas_export.h"
#include "atlas_store.h"
#include "atlas_xml.h"

#include <atlas_file.h>

#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>

using namespace atlas;
namespace fs = std::filesystem;

static int g_iFailed = 0, g_iChecks = 0;
#define CHECK(cond) do { g_iChecks++; if (!(cond)) { g_iFailed++; std::fprintf(stderr, "  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, msg) do { g_iChecks++; if (!(cond)) { g_iFailed++; std::fprintf(stderr, "  FAILED %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); } } while (0)

static void f_Run(const char* l_pszName, const std::function<void()>& l_fnF) {
    const int l_iBefore = g_iFailed;
    l_fnF();
    std::printf("%s %s\n", g_iFailed == l_iBefore ? "[ ok ]" : "[FAIL]", l_pszName);
}

static std::string f_TempDir(const std::string& l_strName) {
    const fs::path l_oD = fs::temp_directory_path() / ("atlas_test_" + l_strName);
    std::error_code l_oEc;
    fs::remove_all(l_oD, l_oEc);
    fs::create_directories(l_oD);
    return f_U8str(l_oD);
}

// A w x h image: transparent margin `m`, a colour pattern derived from `seed` inside.
static Image f_MakeImage(int l_iW, int l_iH, int l_iM, int l_iSeed) {
    Image l_oImg(l_iW, l_iH);
    for (int l_iY = l_iM; l_iY < l_iH - l_iM; l_iY++)
        for (int l_iX = l_iM; l_iX < l_iW - l_iM; l_iX++) {
            uint8_t* l_pP = l_oImg.f_At(l_iX, l_iY);
            l_pP[0] = (uint8_t)(l_iSeed * 37 + l_iX * 5);
            l_pP[1] = (uint8_t)(l_iSeed * 91 + l_iY * 7);
            l_pP[2] = (uint8_t)(l_iSeed * 13 + l_iX * l_iY);
            l_pP[3] = 255;
        }
    return l_oImg;
}

static bool f_SameImage(const Image& l_oA, const Image& l_oB) { return l_oA.m_iW == l_oB.m_iW && l_oA.m_iH == l_oB.m_iH && l_oA.m_vPx == l_oB.m_vPx; }

// What a game draws: fully transparent pixels count as equal whatever their RGB (old atlases keep
// colour under alpha 0; trimming drops it).
static bool f_SameVisible(const Image& l_oA, const Image& l_oB) {
    if (l_oA.m_iW != l_oB.m_iW || l_oA.m_iH != l_oB.m_iH) return false;
    for (size_t l_nI = 0; l_nI < l_oA.m_vPx.size(); l_nI += 4) {
        if (l_oA.m_vPx[l_nI + 3] == 0 && l_oB.m_vPx[l_nI + 3] == 0) continue;
        if (memcmp(&l_oA.m_vPx[l_nI], &l_oB.m_vPx[l_nI], 4) != 0) return false;
    }
    return true;
}

static std::string f_ReadAll(const std::string& l_strP) {
    std::ifstream l_oF(l_strP, std::ios::binary);
    std::stringstream l_oSs;
    l_oSs << l_oF.rdbuf();
    return l_oSs.str();
}

// ---- packer ------------------------------------------------------------------------------------

static void f_CheckPacking(const std::vector<PackItem>& l_vItems, const PackSettings& l_oS, const PackResult& l_oR) {
    CHECK(l_oR.m_vPlacements.size() == l_vItems.size());
    for (size_t l_nI = 0; l_nI < l_vItems.size(); l_nI++) {
        const PackPlacement& l_oA = l_oR.m_vPlacements[l_nI];
        CHECK_MSG(l_oA.m_iPage >= 0, "item " + std::to_string(l_nI) + " not placed");
        if (l_oA.m_iPage < 0) continue;
        const IRect& l_oPg = l_oR.m_vPageSizes[l_oA.m_iPage];
        CHECK(l_oA.m_iX >= l_oS.m_iBorder && l_oA.m_iY >= l_oS.m_iBorder);
        CHECK(l_oA.m_iX + l_vItems[l_nI].m_iW <= l_oPg.m_iW - l_oS.m_iBorder && l_oA.m_iY + l_vItems[l_nI].m_iH <= l_oPg.m_iH - l_oS.m_iBorder);
        if (l_oS.m_bPowerOfTwo) { CHECK((l_oPg.m_iW & (l_oPg.m_iW - 1)) == 0); CHECK((l_oPg.m_iH & (l_oPg.m_iH - 1)) == 0); }
        CHECK(l_oPg.m_iW <= l_oS.m_iMaxWidth && l_oPg.m_iH <= l_oS.m_iMaxHeight);
        for (size_t l_nJ = l_nI + 1; l_nJ < l_vItems.size(); l_nJ++) {
            const PackPlacement& l_oB = l_oR.m_vPlacements[l_nJ];
            if (l_oB.m_iPage != l_oA.m_iPage) continue;
            // Inflate both by padding on the right/bottom: they must still not overlap.
            const IRect l_oRa{l_oA.m_iX, l_oA.m_iY, l_vItems[l_nI].m_iW + l_oS.m_iPadding, l_vItems[l_nI].m_iH + l_oS.m_iPadding};
            const IRect l_oRb{l_oB.m_iX, l_oB.m_iY, l_vItems[l_nJ].m_iW + l_oS.m_iPadding, l_vItems[l_nJ].m_iH + l_oS.m_iPadding};
            CHECK_MSG(!l_oRa.f_Intersects(l_oRb), "items " + std::to_string(l_nI) + " and " + std::to_string(l_nJ) + " overlap");
        }
    }
}

static void f_TestPacker() {
    std::mt19937 l_oRng(1234);
    for (Heuristic l_eH : {Heuristic::Auto, Heuristic::BestShortSideFit, Heuristic::BestAreaFit, Heuristic::BottomLeft,
                        Heuristic::ContactPoint, Heuristic::Shelf}) {
        std::vector<PackItem> l_vItems;
        for (int l_iI = 0; l_iI < 120; l_iI++) l_vItems.push_back({(int)(l_oRng() % 60) + 1, (int)(l_oRng() % 60) + 1});
        PackSettings l_oS;
        l_oS.m_iMaxWidth = 256; l_oS.m_iMaxHeight = 256; l_oS.m_iPadding = 2; l_oS.m_iBorder = 1; l_oS.m_eHeuristic = l_eH; l_oS.m_bPowerOfTwo = true;
        const PackResult l_oR = f_PackPages(l_vItems, l_oS);
        CHECK(l_oR.m_vErrors.empty());
        CHECK(l_oR.m_vPageSizes.size() >= 2);   // 120 rects of ~30x30 average do not fit in one 256 page
        f_CheckPacking(l_vItems, l_oS, l_oR);
        const PackResult l_oAgain = f_PackPages(l_vItems, l_oS);
        bool l_bSame = l_oAgain.m_vPageSizes.size() == l_oR.m_vPageSizes.size();
        for (size_t l_nI = 0; l_bSame && l_nI < l_vItems.size(); l_nI++)
            l_bSame = l_oAgain.m_vPlacements[l_nI].m_iPage == l_oR.m_vPlacements[l_nI].m_iPage && l_oAgain.m_vPlacements[l_nI].m_iX == l_oR.m_vPlacements[l_nI].m_iX &&
                   l_oAgain.m_vPlacements[l_nI].m_iY == l_oR.m_vPlacements[l_nI].m_iY;
        CHECK_MSG(l_bSame, std::string("not deterministic: ") + f_HeuristicName(l_eH));
    }
    // Shrinks to a small page; pinned item lands exactly; too-big item is an error.
    std::vector<PackItem> l_vItems = {{10, 10}, {20, 5}, {5, 20}};
    PackItem l_oPin{8, 8, true, 0, 40, 40};
    l_vItems.push_back(l_oPin);
    PackSettings l_oS;
    l_oS.m_iMaxWidth = 512; l_oS.m_iMaxHeight = 512; l_oS.m_iMinWidth = 1; l_oS.m_iMinHeight = 1; l_oS.m_iPadding = 1;
    PackResult l_oR = f_PackPages(l_vItems, l_oS);
    f_CheckPacking(l_vItems, l_oS, l_oR);
    CHECK(l_oR.m_vPageSizes.size() == 1);
    CHECK(l_oR.m_vPlacements[3].m_iX == 40 && l_oR.m_vPlacements[3].m_iY == 40);
    // Shrunk to the used area: at least the pinned rect, at most the 64px bin it was packed in.
    CHECK(l_oR.m_vPageSizes[0].m_iW >= 48 && l_oR.m_vPageSizes[0].m_iW <= 64 && l_oR.m_vPageSizes[0].m_iH >= 48 && l_oR.m_vPageSizes[0].m_iH <= 64);
    l_vItems.push_back({600, 10});
    l_oR = f_PackPages(l_vItems, l_oS);
    CHECK(!l_oR.m_vErrors.empty());
    CHECK(l_oR.m_vPlacements[4].m_iPage == -1);
}

// ---- build -------------------------------------------------------------------------------------

struct Fixture {
    std::string m_strDir;
    Project m_oP;
};

// sprites/: floor 32x32 (opaque), coin 32x32 (4px margin), coin_copy (same pixels as coin),
//           frame 64x48 (opaque) with children; style/: floor at 64x64.
static Fixture f_MakeFixture(const std::string& l_strName) {
    Fixture l_oF;
    l_oF.m_strDir = f_TempDir(l_strName);
    fs::create_directories(l_oF.m_strDir + "/sprites/ui");
    fs::create_directories(l_oF.m_strDir + "/style/ui");
    f_SavePng(l_oF.m_strDir + "/sprites/floor.png", f_MakeImage(32, 32, 0, 1));
    f_SavePng(l_oF.m_strDir + "/sprites/coin.png", f_MakeImage(32, 32, 4, 2));
    f_SavePng(l_oF.m_strDir + "/sprites/coin_copy.png", f_MakeImage(32, 32, 4, 2));
    f_SavePng(l_oF.m_strDir + "/sprites/ui/frame.png", f_MakeImage(64, 48, 0, 3));
    f_SavePng(l_oF.m_strDir + "/sprites/ui/panel.png", f_MakeImage(40, 40, 10, 4));   // trimmed parent
    f_SavePng(l_oF.m_strDir + "/style/floor.png", f_MakeImage(64, 64, 0, 5));
    f_SavePng(l_oF.m_strDir + "/style/ui/frame.png", f_MakeImage(128, 96, 0, 6));

    Project& l_oP = l_oF.m_oP;
    l_oP.m_strFilePath = l_oF.m_strDir + "/game.atlasproj";
    l_oP.m_strName = "game";
    l_oP.m_oSettings.m_iPadding = 2;
    l_oP.m_oSettings.m_iExtrude = 1;
    l_oP.m_oSettings.m_iMinWidth = l_oP.m_oSettings.m_iMinHeight = 1;
    l_oP.m_vSources.push_back({"sprites", true, ""});
    l_oP.m_oOutput.m_strDir = "out";
    l_oP.m_oOutput.m_vFormats = {"atlas", "atlas-spine3", "plist", "tp-json", "pi", "rcss"};

    SpriteDef l_oFloor; l_oFloor.m_strName = "floor"; l_oFloor.m_vTags = {"map"}; l_oFloor.m_bHasPivot = true; l_oFloor.m_arrPivot[0] = 0.25f; l_oFloor.m_arrPivot[1] = 1.0f;
    l_oP.m_vSprites.push_back(l_oFloor);
    SpriteDef l_oFrame; l_oFrame.m_strName = "ui/frame"; l_oFrame.m_bHasSplit = true; l_oFrame.m_arrSplit[0] = 8; l_oFrame.m_arrSplit[1] = 8; l_oFrame.m_arrSplit[2] = 6; l_oFrame.m_arrSplit[3] = 6;
    l_oP.m_vSprites.push_back(l_oFrame);
    SpriteDef l_oTl; l_oTl.m_strName = "ui/frame_tl"; l_oTl.m_strParent = "ui/frame"; l_oTl.m_oRect = {0, 0, 16, 12}; l_oTl.m_vTags = {"ui"};
    l_oP.m_vSprites.push_back(l_oTl);
    SpriteDef l_oMid; l_oMid.m_strName = "ui/frame_mid"; l_oMid.m_strParent = "ui/frame"; l_oMid.m_oRect = {16, 12, 32, 24};
    l_oP.m_vSprites.push_back(l_oMid);
    SpriteDef l_oDot; l_oDot.m_strName = "ui/frame_dot"; l_oDot.m_strParent = "ui/frame_mid"; l_oDot.m_oRect = {4, 4, 8, 8};   // nested child
    l_oP.m_vSprites.push_back(l_oDot);
    SpriteDef l_oCorner; l_oCorner.m_strName = "ui/panel_corner"; l_oCorner.m_strParent = "ui/panel"; l_oCorner.m_oRect = {0, 0, 6, 6};   // inside the transparent margin
    l_oP.m_vSprites.push_back(l_oCorner);
    SpriteDef l_oBaked; l_oBaked.m_strName = "ui/frame_baked"; l_oBaked.m_strParent = "ui/frame"; l_oBaked.m_oRect = {48, 36, 16, 12}; l_oBaked.m_bBake = true;
    l_oP.m_vSprites.push_back(l_oBaked);
    Animation l_oA; l_oA.m_strName = "spin"; l_oA.m_vFrames = {{"coin", 0.1f}, {"coin_copy", 0.2f}};
    l_oP.m_vAnimations.push_back(l_oA);
    Variant l_oV; l_oV.m_strId = "hd"; l_oV.m_strOverrideDir = "style"; l_oV.m_oOutput = l_oP.m_oOutput; l_oV.m_oOutput.m_strDir = "out_hd";
    l_oP.m_vVariants.push_back(l_oV);
    return l_oF;
}

// What the sprite should look like: its source image, or for a child the rect of its root.
static Image f_Expected(const Fixture& l_oF, const std::string& l_strName, const std::string& l_strStyleDir = "") {
    auto l_fnLoad = [&](const std::string& l_strN) {
        Image l_oImg;
        if (!l_strStyleDir.empty() && f_LoadImage(l_oF.m_strDir + "/" + l_strStyleDir + "/" + l_strN + ".png", l_oImg)) return l_oImg;
        f_LoadImage(l_oF.m_strDir + "/sprites/" + l_strN + ".png", l_oImg);
        return l_oImg;
    };
    const SpriteDef* l_pD = l_oF.m_oP.f_FindSprite(l_strName);
    if (!l_pD || !l_pD->f_IsChild()) return l_fnLoad(l_strName);
    IRect l_oAbs = l_pD->m_oRect;
    const SpriteDef* l_pCur = l_pD;
    while (true) {
        const SpriteDef* l_pPar = l_oF.m_oP.f_FindSprite(l_pCur->m_strParent);
        if (l_pPar && l_pPar->f_IsChild()) { l_oAbs.m_iX += l_pPar->m_oRect.m_iX; l_oAbs.m_iY += l_pPar->m_oRect.m_iY; l_pCur = l_pPar; continue; }
        Image l_oRoot = l_fnLoad(l_pCur->m_strParent);
        Image l_oBase;
        f_LoadImage(l_oF.m_strDir + "/sprites/" + l_pCur->m_strParent + ".png", l_oBase);
        const double l_dSx = (double)l_oRoot.m_iW / l_oBase.m_iW, l_dSy = (double)l_oRoot.m_iH / l_oBase.m_iH;
        const int l_iX0 = (int)std::floor(l_oAbs.m_iX * l_dSx + 0.5), l_iY0 = (int)std::floor(l_oAbs.m_iY * l_dSy + 0.5);
        const IRect l_oR{l_iX0, l_iY0, (int)std::floor(l_oAbs.f_Right() * l_dSx + 0.5) - l_iX0, (int)std::floor(l_oAbs.f_Bottom() * l_dSy + 0.5) - l_iY0};
        return f_Crop(l_oRoot, l_oR);
    }
}

static void f_TestBuild() {
    Fixture l_oF = f_MakeFixture("build");
    BuildResult l_oB = f_Build(l_oF.m_oP);
    for (const Diagnostic& l_oD : l_oB.m_vDiagnostics) std::printf("    %s\n", f_FormatDiagnostic(l_oD).c_str());
    CHECK(l_oB.f_Ok());
    CHECK(l_oB.m_vPages.size() == 1);
    // floor, coin, coin_copy, ui/frame, ui/panel + 5 children
    CHECK(l_oB.m_vRegions.size() == 10);
    const Region* l_pCoin = l_oB.f_Find("coin");
    const Region* l_pCopy = l_oB.f_Find("coin_copy");
    CHECK(l_pCoin && l_pCopy);
    if (l_pCoin && l_pCopy) {
        CHECK(l_pCoin->m_oFrame == l_pCopy->m_oFrame);                 // dedupe: one place
        CHECK(l_pCopy->m_strAliasOf == "coin");
        CHECK(l_pCoin->m_oFrame.m_iW == 24 && l_pCoin->m_iOffsetX == 4);  // trimmed
        CHECK(l_pCoin->m_iOrigW == 32);
    }
    const Region* l_pFrame = l_oB.f_Find("ui/frame");
    const Region* l_pTl = l_oB.f_Find("ui/frame_tl");
    const Region* l_pDot = l_oB.f_Find("ui/frame_dot");
    CHECK(l_pFrame && l_pTl && l_pDot);
    if (l_pFrame && l_pTl && l_pDot) {
        CHECK(l_pTl->m_bChild && l_pTl->m_strParent == "ui/frame");
        CHECK(l_pTl->m_oFrame.m_iX == l_pFrame->m_oFrame.m_iX && l_pTl->m_oFrame.m_iY == l_pFrame->m_oFrame.m_iY && l_pTl->m_oFrame.m_iW == 16);
        CHECK(l_pDot->m_oFrame.m_iX == l_pFrame->m_oFrame.m_iX + 20 && l_pDot->m_oFrame.m_iY == l_pFrame->m_oFrame.m_iY + 16);
        CHECK(l_pDot->m_oLocal == (IRect{4, 4, 8, 8}));
        CHECK(l_pFrame->m_bHasSplit && l_pFrame->m_arrSplit[2] == 6);
    }
    // The panel is trimmed, but never past its child in the transparent corner.
    const Region* l_pPanel = l_oB.f_Find("ui/panel");
    CHECK(l_pPanel && l_pPanel->m_iOffsetX == 0 && l_pPanel->m_iOffsetY == 0 && l_pPanel->m_oFrame.m_iW == 30);
    const Region* l_pBaked = l_oB.f_Find("ui/frame_baked");
    CHECK(l_pBaked && l_pBaked->m_bChild && l_pFrame && !l_pFrame->m_oFrame.f_Intersects(l_pBaked->m_oFrame));
    // Every sprite draws exactly its source pixels.
    for (const Region& l_oR : l_oB.m_vRegions)
        CHECK_MSG(f_SameImage(f_RenderRegion(l_oB, l_oR), f_Expected(l_oF, l_oR.m_strName)), l_oR.m_strName);
    const Region* l_pFl = l_oB.f_Find("floor");
    CHECK(l_pFl && l_pFl->m_arrPivot[0] == 0.25f && l_pFl->m_vTags.size() == 1);

    // Extrude: the pixel left of the floor's frame repeats its first column.
    if (l_pFl) {
        const Image& l_oPg = l_oB.m_vPages[0].m_oImage;
        CHECK(memcmp(l_oPg.f_At(l_pFl->m_oFrame.m_iX - 1, l_pFl->m_oFrame.m_iY), l_oPg.f_At(l_pFl->m_oFrame.m_iX, l_pFl->m_oFrame.m_iY), 4) == 0);
    }

    // Variant: other sizes, same names; children scale with their root.
    BuildResult l_oHd = f_Build(l_oF.m_oP, BuildOptions{"hd", true});
    CHECK(l_oHd.f_Ok());
    CHECK(l_oHd.m_vRegions.size() == l_oB.m_vRegions.size());
    const Region* l_pHfl = l_oHd.f_Find("floor");
    CHECK(l_pHfl && l_pHfl->m_iOrigW == 64);
    const Region* l_pHtl = l_oHd.f_Find("ui/frame_tl");
    CHECK(l_pHtl && l_pHtl->m_oFrame.m_iW == 32 && l_pHtl->m_oFrame.m_iH == 24 && l_pHtl->m_oLocal == (IRect{0, 0, 32, 24}));
    for (const Region& l_oR : l_oHd.m_vRegions)
        CHECK_MSG(f_SameImage(f_RenderRegion(l_oHd, l_oR), f_Expected(l_oF, l_oR.m_strName, "style")), "hd " + l_oR.m_strName);

    // Deterministic.
    BuildResult l_oAgain = f_Build(l_oF.m_oP);
    CHECK(l_oAgain.m_vPages.size() == l_oB.m_vPages.size() && l_oAgain.m_vPages[0].m_oImage.m_vPx == l_oB.m_vPages[0].m_oImage.m_vPx);

    // Errors.
    Project l_oBad = l_oF.m_oP;
    SpriteDef l_oOut; l_oOut.m_strName = "outside"; l_oOut.m_strParent = "floor"; l_oOut.m_oRect = {30, 30, 8, 8};
    SpriteDef l_oOrphan; l_oOrphan.m_strName = "orphan"; l_oOrphan.m_strParent = "nope"; l_oOrphan.m_oRect = {0, 0, 1, 1};
    SpriteDef l_oLoopA; l_oLoopA.m_strName = "loopA"; l_oLoopA.m_strParent = "loopB"; l_oLoopA.m_oRect = {0, 0, 1, 1};
    SpriteDef l_oLoopB; l_oLoopB.m_strName = "loopB"; l_oLoopB.m_strParent = "loopA"; l_oLoopB.m_oRect = {0, 0, 1, 1};
    l_oBad.m_vSprites.insert(l_oBad.m_vSprites.end(), {l_oOut, l_oOrphan, l_oLoopA, l_oLoopB});
    BuildResult l_oE = f_Build(l_oBad);
    CHECK(!l_oE.f_Ok());
    int l_iFound = 0;
    for (const Diagnostic& l_oD : l_oE.m_vDiagnostics)
        if (l_oD.m_eLevel == Diagnostic::Error && (l_oD.m_strSprite == "outside" || l_oD.m_strSprite == "orphan" || l_oD.m_strSprite == "loopA" || l_oD.m_strSprite == "loopB")) l_iFound++;
    CHECK(l_iFound >= 4);

    // Project JSON round trip.
    Project l_oBack;
    std::string l_strErr;
    CHECK(f_ProjectFromJson(f_ProjectToJson(l_oF.m_oP), l_oBack, &l_strErr));
    CHECK(f_ProjectToJson(l_oBack) == f_ProjectToJson(l_oF.m_oP));

    // Pinned sprite stays where it was put.
    Project l_oPinned = l_oF.m_oP;
    SpriteDef* l_pPf = l_oPinned.f_FindSprite("floor");
    l_pPf->m_bPinned = true; l_pPf->m_iPinX = 100; l_pPf->m_iPinY = 3;
    BuildResult l_oPb = f_Build(l_oPinned);
    CHECK(l_oPb.f_Ok());
    const Region* l_pPr = l_oPb.f_Find("floor");
    CHECK(l_pPr && l_pPr->m_oFrame.m_iX == 100 && l_pPr->m_oFrame.m_iY == 3);
}

// ---- formats -----------------------------------------------------------------------------------

static void f_SameRegions(const BuildResult& l_oA, const BuildResult& l_oB, const std::string& l_strWhat, bool l_bChildren = true) {
    CHECK_MSG(l_oA.m_vRegions.size() == l_oB.m_vRegions.size(), l_strWhat);
    for (const Region& l_oR : l_oA.m_vRegions) {
        const Region* l_pO = l_oB.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pO, l_strWhat + ": missing " + l_oR.m_strName);
        if (!l_pO) continue;
        CHECK_MSG(l_pO->m_oFrame == l_oR.m_oFrame, l_strWhat + ": frame " + l_oR.m_strName);
        CHECK_MSG(l_pO->m_iOffsetX == l_oR.m_iOffsetX && l_pO->m_iOffsetY == l_oR.m_iOffsetY, l_strWhat + ": offset " + l_oR.m_strName);
        CHECK_MSG(l_pO->m_iOrigW == l_oR.m_iOrigW && l_pO->m_iOrigH == l_oR.m_iOrigH, l_strWhat + ": orig " + l_oR.m_strName);
        if (l_bChildren && !(l_oR.m_bBaked && l_strWhat == "pi")) {
            CHECK_MSG(l_pO->m_bChild == l_oR.m_bChild && l_pO->m_strParent == l_oR.m_strParent, l_strWhat + ": parent " + l_oR.m_strName);
            if (l_oR.m_bChild) CHECK_MSG(l_pO->m_oLocal.m_iX == l_oR.m_oLocal.m_iX && l_pO->m_oLocal.m_iY == l_oR.m_oLocal.m_iY, l_strWhat + ": local " + l_oR.m_strName);
        }
        CHECK_MSG(f_SameImage(f_RenderRegion(l_oA, l_oR), f_RenderRegion(l_oB, *l_pO)), l_strWhat + ": pixels " + l_oR.m_strName);
    }
}

static void f_TestFormats() {
    Fixture l_oF = f_MakeFixture("formats");
    BuildResult l_oB = f_Build(l_oF.m_oP);
    CHECK(l_oB.f_Ok());
    std::vector<ExportFile> l_vFiles;
    std::string l_strErr;
    CHECK(f_ExportFiles(l_oB, l_oF.m_oP.m_oOutput.m_vFormats, l_vFiles, &l_strErr));
    const std::string l_strOut = l_oF.m_oP.f_Resolve(l_oF.m_oP.m_oOutput.m_strDir);
    CHECK(f_WriteFiles(l_strOut, l_vFiles, &l_strErr));

    // .atlas through the game's reader.
    toms::AtlasFile l_oAf;
    CHECK(toms::f_ParseAtlas(f_ReadAll(l_strOut + "/game.atlas"), l_oAf, &l_strErr));
    CHECK(l_oAf.m_vPages.size() == 1 && l_oAf.m_vPages[0].m_iW == l_oB.m_vPages[0].m_oImage.m_iW);
    CHECK(l_oAf.m_vRegions.size() == l_oB.m_vRegions.size());
    for (const Region& l_oR : l_oB.m_vRegions) {
        const toms::AtlasRegion* l_pA = l_oAf.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pA, l_oR.m_strName);
        if (!l_pA) continue;
        CHECK_MSG(l_pA->m_iX == l_oR.m_oFrame.m_iX && l_pA->m_iY == l_oR.m_oFrame.m_iY && l_pA->m_iW == l_oR.m_oFrame.m_iW && l_pA->m_iH == l_oR.m_oFrame.m_iH, l_oR.m_strName);
        CHECK_MSG(l_pA->m_iOffX == l_oR.m_iOffsetX && l_pA->m_iOffY == l_oR.m_iOffsetY && l_pA->m_iOrigW == l_oR.m_iOrigW && l_pA->m_iOrigH == l_oR.m_iOrigH, l_oR.m_strName);
        CHECK_MSG(l_pA->m_bChild == l_oR.m_bChild && l_pA->m_strParent == l_oR.m_strParent, l_oR.m_strName);
        CHECK_MSG(l_pA->m_arrPivot[0] == l_oR.m_arrPivot[0] && l_pA->m_arrPivot[1] == l_oR.m_arrPivot[1], l_oR.m_strName);
        CHECK_MSG(l_pA->m_vTags == l_oR.m_vTags, l_oR.m_strName);
        CHECK_MSG(l_pA->m_bHasSplit == l_oR.m_bHasSplit, l_oR.m_strName);
        CHECK(std::abs(l_pA->m_arrUv[0] - (float)l_oR.m_oFrame.m_iX / l_oB.m_vPages[0].m_oImage.m_iW) < 1e-6f);
    }

    BuildResult l_oIn;
    CHECK(f_ImportLibgdx(l_strOut + "/game.atlas", l_oIn, &l_strErr));
    f_SameRegions(l_oB, l_oIn, "atlas");
    CHECK(f_ImportLibgdx(l_strOut + "/game.spine3.atlas", l_oIn, &l_strErr));
    f_SameRegions(l_oB, l_oIn, "spine3", false);
    CHECK(f_ImportTpJson(l_strOut + "/game.json", l_oIn, &l_strErr));
    f_SameRegions(l_oB, l_oIn, "tp-json");
    CHECK(l_oIn.m_vAnimations.size() == 1 && l_oIn.m_vAnimations[0].m_vFrames.size() == 2 && l_oIn.m_vAnimations[0].m_vFrames[1].m_fTime == 0.2f);
    CHECK(f_ImportPi(l_strOut + "/game.pi", l_oIn, &l_strErr));
    f_SameRegions(l_oB, l_oIn, "pi");
    CHECK(l_oIn.m_vAnimations.size() == 1 && l_oIn.m_vAnimations[0].m_vFrames[1].m_strSprite == "coin_copy");
    // Our own .plist back in: frames, trims, children, and the x_ fields (pivot, split, tags).
    CHECK_MSG(f_ImportPlist(l_strOut + "/game.plist", l_oIn, &l_strErr), l_strErr);
    f_SameRegions(l_oB, l_oIn, "plist");
    for (const Region& l_oR : l_oB.m_vRegions) {
        const Region* l_pO = l_oIn.f_Find(l_oR.m_strName);
        if (!l_pO) continue;
        CHECK_MSG(std::abs(l_pO->m_arrPivot[0] - l_oR.m_arrPivot[0]) < 1e-5f && std::abs(l_pO->m_arrPivot[1] - l_oR.m_arrPivot[1]) < 1e-5f, "plist pivot " + l_oR.m_strName);
        CHECK_MSG(l_pO->m_bHasSplit == l_oR.m_bHasSplit && l_pO->m_vTags == l_oR.m_vTags, "plist split/tags " + l_oR.m_strName);
    }
    BuildResult l_oByExt;
    CHECK(f_ImportAtlasFile(l_strOut + "/game.plist", l_oByExt, &l_strErr) && l_oByExt.m_vRegions.size() == l_oB.m_vRegions.size());

    // plist: the frames dict has every sprite, textureRect matches.
    auto l_upPlist = f_ParseXml(f_ReadAll(l_strOut + "/game.plist"), &l_strErr);
    CHECK(l_upPlist && l_upPlist->m_strName == "plist");
    if (l_upPlist) {
        const XmlNode& l_oDict = *l_upPlist->m_vChildren[0];
        const XmlNode& l_oFrames = *l_oDict.m_vChildren[1];
        CHECK(l_oFrames.m_strName == "dict" && l_oFrames.m_vChildren.size() == l_oB.m_vRegions.size() * 2);
        for (size_t l_nI = 0; l_nI + 1 < l_oFrames.m_vChildren.size(); l_nI += 2) {
            const Region* l_pR = l_oB.f_Find(l_oFrames.m_vChildren[l_nI]->m_strText);
            CHECK_MSG(l_pR, l_oFrames.m_vChildren[l_nI]->m_strText);
            if (!l_pR) continue;
            const XmlNode& l_oFd = *l_oFrames.m_vChildren[l_nI + 1];
            for (size_t l_nK = 0; l_nK + 1 < l_oFd.m_vChildren.size(); l_nK += 2)
                if (l_oFd.m_vChildren[l_nK]->m_strText == "textureRect")
                    CHECK(l_oFd.m_vChildren[l_nK + 1]->m_strText == "{{" + std::to_string(l_pR->m_oFrame.m_iX) + "," + std::to_string(l_pR->m_oFrame.m_iY) + "},{" +
                                                         std::to_string(l_pR->m_oFrame.m_iW) + "," + std::to_string(l_pR->m_oFrame.m_iH) + "}}");
        }
    }
    // rcss: one line per sprite, '/' turned into '-'.
    const std::string l_strRcss = f_ReadAll(l_strOut + "/game.rcss");
    CHECK(l_strRcss.find("@spritesheet game") != std::string::npos);
    CHECK(l_strRcss.find("ui-frame_tl: ") != std::string::npos);

    // Import into a project and rebuild: same pixels for every sprite.
    CHECK(f_ImportLibgdx(l_strOut + "/game.atlas", l_oIn, &l_strErr));
    Project l_oP2;
    const std::string l_strD2 = f_TempDir("formats_reimport");
    CHECK(f_ImportToProject(l_oIn, l_strD2 + "/re.atlasproj", l_strD2 + "/images", l_oP2, &l_strErr));
    l_oP2.m_oSettings.m_iMinWidth = l_oP2.m_oSettings.m_iMinHeight = 1;
    BuildResult l_oB2 = f_Build(l_oP2);
    CHECK(l_oB2.f_Ok());
    for (const Region& l_oR : l_oB.m_vRegions) {
        const Region* l_pO = l_oB2.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pO && f_SameImage(f_RenderRegion(l_oB, l_oR), f_RenderRegion(l_oB2, *l_pO)), "reimport " + l_oR.m_strName);
        if (l_pO) CHECK_MSG(l_pO->m_bChild == l_oR.m_bChild, "reimport child " + l_oR.m_strName);
    }

    // Writing unchanged files leaves them alone.
    const auto l_oT0 = fs::last_write_time(l_strOut + "/game.atlas");
    CHECK(f_WriteFiles(l_strOut, l_vFiles, &l_strErr));
    CHECK(fs::last_write_time(l_strOut + "/game.atlas") == l_oT0);
}

// ---- a Cocos / TexturePacker .plist written by another tool (format 2): rotated, trimmed, aliased ----------

static void f_TestPlistCocos() {
    const std::string l_strD = f_TempDir("plist_cocos");
    // An 8x4 page. "arrow" is 3x2 upright, stored turned clockwise as 2x3 at (0,0); "dot" is a 2x2 trim of a 6x6.
    Image l_oPage(8, 4);
    auto l_fnColour = [](int l_iX, int l_iY) { return std::array<uint8_t, 4>{uint8_t(40 + l_iX * 50), uint8_t(60 + l_iY * 90), 7, 255}; };
    const int l_iW = 3, l_iH = 2;
    for (int l_iSy = 0; l_iSy < l_iW; l_iSy++)
        for (int l_iSx = 0; l_iSx < l_iH; l_iSx++) {   // stored (sx, sy) holds upright (sy, H - 1 - sx)
            const auto l_arrC = l_fnColour(l_iSy, l_iH - 1 - l_iSx);
            memcpy(l_oPage.f_At(l_iSx, l_iSy), l_arrC.data(), 4);
        }
    for (int l_iY = 0; l_iY < 2; l_iY++)
        for (int l_iX = 0; l_iX < 2; l_iX++) { uint8_t* l_pP = l_oPage.f_At(4 + l_iX, l_iY); l_pP[0] = 200; l_pP[1] = 10; l_pP[2] = 10; l_pP[3] = 255; }
    std::string l_strErr;
    CHECK(f_SavePng(l_strD + "/sheet.png", l_oPage, &l_strErr));
    std::ofstream(f_U8path(l_strD + "/sheet.plist"), std::ios::binary) <<
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>\n"
        "<key>frames</key><dict>\n"
        "  <key>arrow.png</key><dict><key>frame</key><string>{{0,0},{3,2}}</string><key>offset</key><string>{0,0}</string>"
        "<key>rotated</key><true/><key>sourceColorRect</key><string>{{0,0},{3,2}}</string><key>sourceSize</key><string>{3,2}</string></dict>\n"
        "  <key>dot.png</key><dict><key>aliases</key><array><string>dot_again.png</string></array>"
        "<key>frame</key><string>{{4,0},{2,2}}</string><key>offset</key><string>{1,-1}</string>"
        "<key>rotated</key><false/><key>sourceColorRect</key><string>{{3,3},{2,2}}</string><key>sourceSize</key><string>{6,6}</string></dict>\n"
        "</dict>\n<key>metadata</key><dict><key>format</key><integer>2</integer>"
        "<key>textureFileName</key><string>sheet.png</string><key>size</key><string>{8,4}</string></dict>\n"
        "</dict></plist>\n";
    BuildResult l_oIn;
    CHECK_MSG(f_ImportAtlasFile(l_strD + "/sheet.plist", l_oIn, &l_strErr), l_strErr);
    CHECK(l_oIn.m_vRegions.size() == 3 && l_oIn.f_Find("arrow") && l_oIn.f_Find("dot") && l_oIn.f_Find("dot_again"));
    if (const Region* l_pA = l_oIn.f_Find("arrow")) {
        const Image l_oUp = f_RenderRegion(l_oIn, *l_pA);
        bool l_bSame = l_oUp.m_iW == l_iW && l_oUp.m_iH == l_iH;
        for (int l_iY = 0; l_bSame && l_iY < l_iH; l_iY++)
            for (int l_iX = 0; l_bSame && l_iX < l_iW; l_iX++) l_bSame = memcmp(l_oUp.f_At(l_iX, l_iY), l_fnColour(l_iX, l_iY).data(), 4) == 0;
        CHECK_MSG(l_bSame, "the rotated frame comes back upright");
    }
    if (const Region* l_pO = l_oIn.f_Find("dot")) {
        CHECK(l_pO->m_iOrigW == 6 && l_pO->m_iOrigH == 6 && l_pO->m_iOffsetX == 3 && l_pO->m_iOffsetY == 3);   // sourceColorRect
        const Region* l_pA = l_oIn.f_Find("dot_again");
        CHECK(l_pA && l_pA->m_strAliasOf == "dot" && f_SameImage(f_RenderRegion(l_oIn, *l_pO), f_RenderRegion(l_oIn, *l_pA)));
        const Image l_oFull = f_RenderRegion(l_oIn, *l_pO);
        CHECK(l_oFull.m_iW == 6 && l_oFull.m_iH == 6 && l_oFull.f_At(3, 3)[0] == 200 && l_oFull.f_At(0, 0)[3] == 0);   // trim put back
    }
    // ... and on into a project, like the editors' Import Atlas.
    Project l_oP;
    CHECK_MSG(f_ImportToProject(l_oIn, l_strD + "/sheet.atlasproj", "", l_oP, &l_strErr), l_strErr);
    CHECK(l_oP.m_mapImages.count("arrow") && l_oP.m_mapImages.count("dot") && l_oP.m_mapImages.at("arrow")->m_iW == l_iW);
}

// ---- embedded projects (images inside the packed atlas) ----------------------------------------

static bool f_Exists(const std::string& l_strP) { std::error_code l_oEc; return fs::is_regular_file(f_U8path(l_strP), l_oEc); }

static void f_TestEmbedded() {
    Fixture l_oF = f_MakeFixture("embedded");
    const BuildResult l_oRef = f_Build(l_oF.m_oP);
    const BuildResult l_oRefHd = f_Build(l_oF.m_oP, BuildOptions{"hd", true});
    CHECK(l_oRef.f_Ok() && l_oRefHd.f_Ok());

    // Folder project -> embedded: the PNGs move inside, the folders become references.
    Project l_oP = l_oF.m_oP;
    std::vector<Diagnostic> l_vDiags;
    CHECK(f_EmbedSources(l_oP, f_FileImageSource(), &l_vDiags) == 5);
    CHECK(l_vDiags.empty());
    CHECK(l_oP.m_bEmbedded && l_oP.m_vSources.empty() && l_oP.m_vReferences.size() == 1);
    CHECK(l_oP.m_vVariants[0].m_mapImages.size() == 2 && l_oP.m_vVariants[0].m_strOverrideDir.empty() && l_oP.m_vVariants[0].m_strReference == "style");

    // Saved somewhere else, with the packed atlas next to the project file.
    const std::string l_strDir = f_TempDir("embedded_saved");
    l_oP.m_oOutput.m_strDir = l_strDir;
    l_oP.m_vVariants[0].m_oOutput.m_strDir = l_strDir + "/hd";
    SaveResult l_oSr;
    std::string l_strErr;
    CHECK_MSG(f_SaveProjectAll(l_strDir + "/game.atlasproj", l_oP, {"plist"}, &l_oSr, &l_strErr), l_strErr);
    CHECK(l_oP.m_oOutput.m_strDir == "." && l_oP.m_vVariants[0].m_oOutput.m_strDir == "hd");
    CHECK(l_oP.m_vReferences[0].m_strPath != "sprites");   // still points at the fixture's folder
    for (const char* l_pszFile : {"/game.atlasproj", "/game.atlas", "/game.png", "/game.plist", "/game.pi", "/hd/game.atlas", "/hd/game.png"})
        CHECK_MSG(f_Exists(l_strDir + l_pszFile), l_pszFile);

    // The original PNGs are not needed any more.
    fs::remove_all(f_U8path(l_oF.m_strDir + "/sprites"));
    fs::remove_all(f_U8path(l_oF.m_strDir + "/style"));
    Project l_oQ;
    CHECK_MSG(f_OpenProject(l_strDir + "/game.atlasproj", l_oQ, &l_strErr), l_strErr);
    CHECK(l_oQ.m_bEmbedded && l_oQ.m_mapImages.size() == 5 && l_oQ.m_vVariants[0].m_mapImages.size() == 2);
    const BuildResult l_oB = f_Build(l_oQ);
    const BuildResult l_oBHd = f_Build(l_oQ, BuildOptions{"hd", true});
    CHECK(l_oB.f_Ok() && l_oBHd.f_Ok());
    CHECK(l_oB.m_vRegions.size() == l_oRef.m_vRegions.size() && l_oBHd.m_vRegions.size() == l_oRefHd.m_vRegions.size());
    for (const Region& l_oR : l_oRef.m_vRegions) {
        const Region* l_pO = l_oB.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pO && f_SameImage(f_RenderRegion(l_oRef, l_oR), f_RenderRegion(l_oB, *l_pO)), "embedded " + l_oR.m_strName);
        if (l_pO) CHECK_MSG(l_pO->m_bChild == l_oR.m_bChild && l_pO->m_strParent == l_oR.m_strParent && l_pO->m_arrPivot[0] == l_oR.m_arrPivot[0], "embedded meta " + l_oR.m_strName);
    }
    for (const Region& l_oR : l_oRefHd.m_vRegions) {
        const Region* l_pO = l_oBHd.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pO && f_SameImage(f_RenderRegion(l_oRefHd, l_oR), f_RenderRegion(l_oBHd, *l_pO)), "embedded hd " + l_oR.m_strName);
    }

    // Saving again without changes rewrites nothing but the project file.
    SaveResult l_oAgain;
    CHECK(f_SaveProjectAll(l_oQ.m_strFilePath, l_oQ, {"plist"}, &l_oAgain, &l_strErr));
    CHECK_MSG(l_oAgain.m_vWritten.size() == 1, std::to_string(l_oAgain.m_vWritten.size()) + " file(s) rewritten");

    // References: compared with what the project holds.
    fs::create_directories(f_U8path(l_oF.m_strDir + "/sprites"));
    f_SavePng(l_oF.m_strDir + "/sprites/floor.png", f_MakeImage(32, 32, 0, 1));   // same as inside
    f_SavePng(l_oF.m_strDir + "/sprites/coin.png", f_MakeImage(32, 32, 4, 9));    // changed
    f_SavePng(l_oF.m_strDir + "/sprites/gem.png", f_MakeImage(16, 16, 0, 7));     // new
    std::map<std::string, ReferenceImage::Status> l_mapSt;
    for (const ReferenceImage& l_oR : f_ScanReferences(l_oQ, f_FileImageSource())) l_mapSt[l_oR.m_strName] = l_oR.m_eStatus;
    CHECK(l_mapSt.size() == 3);
    CHECK(l_mapSt["floor"] == ReferenceImage::Same && l_mapSt["coin"] == ReferenceImage::Changed && l_mapSt["gem"] == ReferenceImage::New);
    Image l_oGem;
    CHECK(f_LoadImage(l_oF.m_strDir + "/sprites/gem.png", l_oGem));
    CHECK(f_SetImage(l_oQ, "gem", l_oGem, "", &l_strErr));
    CHECK(!f_SetImage(l_oQ, "ui/frame_tl", l_oGem, "", &l_strErr));          // a child has no image
    CHECK(!f_SetImage(l_oQ, "nothing", l_oGem, "hd", &l_strErr));            // variant art needs a base sprite
    CHECK(f_SaveProjectAll(l_oQ.m_strFilePath, l_oQ, {}, nullptr, &l_strErr));
    Project l_oQ2;
    CHECK(f_OpenProject(l_oQ.m_strFilePath, l_oQ2, &l_strErr) && l_oQ2.m_mapImages.count("gem") && f_SameImage(*l_oQ2.m_mapImages["gem"], l_oGem));

    // An image that cannot be packed is never silently dropped: the save is refused.
    Project l_oBig = l_oQ2;
    CHECK(f_SetImage(l_oBig, "huge", f_MakeImage(5000, 4, 0, 3), "", &l_strErr));   // opaque: trimming cannot shrink it
    CHECK(!f_SaveProjectAll(l_oBig.m_strFilePath, l_oBig, {}, nullptr, &l_strErr));
    CHECK(l_strErr.find("huge") != std::string::npos);
    Project l_oQ3;
    CHECK(f_OpenProject(l_oQ2.m_strFilePath, l_oQ3, &l_strErr) && !l_oQ3.m_mapImages.count("huge"));   // nothing was written

    // Back to separate files.
    std::vector<std::string> l_vWritten;
    const std::string l_strXdir = f_TempDir("embedded_extract");
    CHECK(f_ExtractImages(l_oQ3, l_strXdir, "", true, &l_vWritten, &l_strErr));
    CHECK(l_vWritten.size() == l_oQ3.m_mapImages.size() + 5);   // images + children
    Image l_oFrame;
    CHECK(f_LoadImage(l_strXdir + "/ui/frame.png", l_oFrame) && l_oFrame.m_iW == 64 && l_oFrame.m_iH == 48);

    // A .pi imported as an embedded project, saved as .pi again, draws the same pixels.
    BuildResult l_oOld;
    CHECK(f_ImportPi(std::string(ATLAS_TEST_DATA) + "/9Slicing_TalkingDialob.pi", l_oOld, &l_strErr));
    const std::string l_strPdir = f_TempDir("embedded_pi");
    Project l_oPp;
    CHECK(f_ImportToProject(l_oOld, l_strPdir + "/dialog.atlasproj", "", l_oPp, &l_strErr));
    CHECK(l_oPp.m_bEmbedded && l_oPp.m_mapImages.size() == 1 && l_oPp.m_vSprites.size() == 3);   // bg_01 + 3 children
    l_oPp.m_oOutput.m_vFormats = {"pi"};
    CHECK_MSG(f_SaveProjectAll(l_oPp.m_strFilePath, l_oPp, {}, nullptr, &l_strErr), l_strErr);
    BuildResult l_oBack;
    CHECK(f_ImportPi(l_strPdir + "/9Slicing_TalkingDialob.pi", l_oBack, &l_strErr));
    for (const Region& l_oR : l_oOld.m_vRegions) {
        const Region* l_pO = l_oBack.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pO && f_SameImage(f_RenderRegion(l_oOld, l_oR), f_RenderRegion(l_oBack, *l_pO)), "pi " + l_oR.m_strName);
    }
}

// ---- golden (.pi from the old editor) ----------------------------------------------------------

static void f_PiRoundTrip(const std::string& l_strPath, int& l_iCount) {
    BuildResult l_oOld;
    std::string l_strErr;
    if (!f_ImportPi(l_strPath, l_oOld, &l_strErr)) { std::printf("    skip %s: %s\n", l_strPath.c_str(), l_strErr.c_str()); return; }
    l_iCount++;
    const bool l_bTrace = std::getenv("ATLAS_TRACE") != nullptr;
    auto l_fnStep = [&](const char* l_pszWhat) { if (l_bTrace) { std::printf("      %s\n", l_pszWhat); std::fflush(stdout); } };
    l_fnStep("tempdir");
    const std::string l_strD = f_TempDir("golden_" + std::to_string(l_iCount));
    l_fnStep("importToProject");
    Project l_oP;
    CHECK_MSG(f_ImportToProject(l_oOld, l_strD + "/p.atlasproj", l_strD + "/img", l_oP, &l_strErr), l_strPath + ": " + l_strErr);
    l_oP.m_oSettings.m_iMaxWidth = l_oP.m_oSettings.m_iMaxHeight = 8192;
    l_oP.m_oSettings.m_iMinWidth = l_oP.m_oSettings.m_iMinHeight = 1;
    l_fnStep("build");
    BuildResult l_oNb = f_Build(l_oP);
    CHECK_MSG(l_oNb.f_Ok(), l_strPath);
    l_fnStep("export");
    std::vector<ExportFile> l_vFiles;
    CHECK(f_ExportFiles(l_oNb, {"pi"}, l_vFiles, &l_strErr));
    CHECK(f_WriteFiles(l_strD + "/out", l_vFiles, &l_strErr));
    BuildResult l_oBack;
    l_fnStep("reimport");
    CHECK_MSG(f_ImportPi(l_strD + "/out/" + l_oNb.m_strName + ".pi", l_oBack, &l_strErr), l_strPath + ": " + l_strErr);
    l_fnStep("compare");
    for (const Region& l_oR : l_oOld.m_vRegions) {
        const Region* l_pO = l_oBack.f_Find(l_oR.m_strName);
        CHECK_MSG(l_pO, l_strPath + ": missing " + l_oR.m_strName);
        if (!l_pO) continue;
        CHECK_MSG(l_pO->m_iOrigW == l_oR.m_iOrigW && l_pO->m_iOrigH == l_oR.m_iOrigH, l_strPath + ": orig " + l_oR.m_strName);
        CHECK_MSG(l_pO->m_bChild == l_oR.m_bChild, l_strPath + ": child " + l_oR.m_strName);
        CHECK_MSG(f_SameVisible(f_RenderRegion(l_oOld, l_oR), f_RenderRegion(l_oBack, *l_pO)), l_strPath + ": pixels " + l_oR.m_strName);
    }
}

static void f_TestGolden() {
    int l_iN = 0;
    f_PiRoundTrip(std::string(ATLAS_TEST_DATA) + "/9Slicing_TalkingDialob.pi", l_iN);
    CHECK(l_iN == 1);
    // The 9-slice sample: three PuzzleUnitChild units of bg_01.
    BuildResult l_oOld;
    std::string l_strErr;
    CHECK(f_ImportPi(std::string(ATLAS_TEST_DATA) + "/9Slicing_TalkingDialob.pi", l_oOld, &l_strErr));
    const Region* l_pC = l_oOld.f_Find("CentertUp");
    CHECK(l_pC && l_pC->m_bChild && l_pC->m_strParent == "bg_01" && l_pC->m_oLocal == (IRect{18, 0, 64, 18}));
    if (const char* l_pszCorpus = std::getenv("ATLAS_PI_CORPUS")) {
        int l_iM = 0;
        std::error_code l_oEc;
        for (fs::recursive_directory_iterator l_oIt(f_U8path(l_pszCorpus), l_oEc), l_oEnd; !l_oEc && l_oIt != l_oEnd; l_oIt.increment(l_oEc))
            if (l_oIt->path().extension() == ".pi") {
                const std::string l_strFile = f_U8str(l_oIt->path());
                if (std::getenv("ATLAS_TRACE")) { std::printf("    corpus file: %s\n", l_strFile.c_str()); std::fflush(stdout); }
                f_PiRoundTrip(l_strFile, l_iM);
            }
        std::printf("    corpus: %d .pi file(s)\n", l_iM);
    }
}

int main() {
    f_Run("packer", f_TestPacker);
    f_Run("build", f_TestBuild);
    f_Run("formats", f_TestFormats);
    f_Run("plist from Cocos", f_TestPlistCocos);
    f_Run("embedded", f_TestEmbedded);
    f_Run("golden .pi", f_TestGolden);
    std::printf("%d check(s), %d failed\n", g_iChecks, g_iFailed);
    return g_iFailed ? 1 : 0;
}

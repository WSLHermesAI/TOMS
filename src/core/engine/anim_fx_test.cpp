// anim_fx_test.cpp -- node effects and trails (anim_clip.h fx / trail, anim_player.h nodeLayerEffect / appendTrail;
// docs/engine/18_NODE_EFFECTS.md): the file (version 4 only when used, the errors), each pose's subtree range, a layer
// opened before a node's subtree and closed after it (nested ones inside), no layer when the effect shows nothing, the
// params at the node's time, and the afterimages. No GPU.
#include "anim_player.h"

#include <cmath>
#include <cstdio>

using namespace toms::anim;

namespace {
int g_iFail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s  (line %d)\n", msg, __LINE__); ++g_iFail; } } while (0)
bool f_Near(float l_fA, float l_fB, float l_fEps = 1e-3f) { return std::fabs(l_fA - l_fB) <= l_fEps; }

Node f_Sprite(const char* l_pszName, float l_fX = 0) {
    Node l_oN;
    l_oN.m_strName = l_pszName;
    l_oN.m_strSprite = "box";
    l_oN.m_v3Pos = {l_fX, 0, 0};
    return l_oN;
}

// root ( back(order -1), arm [fx] ( upper, hand [fx] ( fist ) ), front )
Clip f_Rig() {
    Clip l_oC;
    l_oC.m_strName = "rig";
    l_oC.m_oRoot.m_strName = "root";
    Node l_oBack = f_Sprite("back");
    l_oBack.m_iOrder = -1;
    Node l_oArm;
    l_oArm.m_strName = "arm";
    l_oArm.m_oFx.m_eShader = FxShader::MotionBlur;
    l_oArm.m_oFx.m_v4Params = {0, 0, 0, 0};
    Key<glm::vec4> l_o0, l_o1;
    l_o0.m_oV = {0, 0, 0, 0};
    l_o1.m_fT = 1;
    l_o1.m_oV = {40, 0, 0, 0};
    l_oArm.m_vFxKeys = {l_o0, l_o1};
    Node l_oHand;
    l_oHand.m_strName = "hand";
    l_oHand.m_oFx.m_eShader = FxShader::Glow;
    l_oHand.m_oFx.m_v4Params = {2, 10, 0.5f, 1};
    l_oHand.m_oFx.m_v4Color = {1, 0.5f, 0.2f, 1};
    l_oHand.m_vChildren.push_back(f_Sprite("fist", 20));
    l_oArm.m_vChildren.push_back(f_Sprite("upper", 10));
    l_oArm.m_vChildren.push_back(l_oHand);
    l_oC.m_oRoot.m_vChildren = {l_oBack, l_oArm, f_Sprite("front", 30)};
    return l_oC;
}

size_t f_PoseOf(const std::vector<NodePose>& l_vP, const char* l_pszName) {
    for (size_t l_nI = 0; l_nI < l_vP.size(); l_nI++)
        if (l_vP[l_nI].m_pNode->m_strName == l_pszName) return l_nI;
    return SIZE_MAX;
}

// The draw list as a string: q = a quad, [ = a layer begins (its kind), ] = it ends.
std::string f_Shape(const DrawList& l_oD) {
    std::string l_strS;
    for (const DrawList::Item& l_oIt : l_oD.m_vOrder) {
        if (l_oIt.m_eKind == DrawList::QuadItem) l_strS += 'q';
        else if (l_oIt.m_eKind == DrawList::MeshItemKind) l_strS += 'm';
        else if (l_oIt.m_eKind == DrawList::BeginLayer) l_strS += "[" + std::to_string(int(l_oD.m_vLayers[l_oIt.m_uIndex].m_eKind));
        else l_strS += ']';
    }
    return l_strS;
}
}  // namespace

int main() {
    // ---- the file ----
    AnimFile l_oF;
    l_oF.m_vClips.push_back(f_Rig());
    l_oF.m_vClips[0].m_oRoot.m_vChildren[2].m_oTrail.m_iCount = 3;
    l_oF.m_vClips[0].m_oRoot.m_vChildren[2].m_oTrail.m_fGap = 0.1f;
    l_oF.m_vClips[0].m_oRoot.m_vChildren[2].m_oTrail.m_bAdditive = true;
    Key<float> l_oTk;
    l_oTk.m_fT = 0.5f;
    l_oTk.m_oV = 0.25f;
    l_oF.m_vClips[0].m_oRoot.m_vChildren[2].m_vTrailKeys = {l_oTk};
    std::string l_strJson = f_AnimToJson(l_oF), l_strErr;
    CHECK(l_strJson.find("\"version\": 4") != std::string::npos && l_strJson.find("\"motionBlur\"") != std::string::npos &&
              l_strJson.find("\"trail\"") != std::string::npos, "effects and trails: version 4");
    AnimFile l_oBack;
    CHECK(f_ParseAnim(l_strJson, l_oBack, &l_strErr) && l_oBack == l_oF, "they read back the same");
    AnimFile l_oNone;
    l_oNone.m_vClips.push_back(Clip());
    CHECK(f_AnimToJson(l_oNone).find("\"version\": 2") != std::string::npos, "a file without them stays version 2");
    CHECK(!f_ParseAnim(R"({"version":4,"clips":[{"name":"c","root":{"name":"n","fx":{"shader":"sparkle"}}}]})", l_oBack, &l_strErr),
          "an unknown shader");
    CHECK(!f_ParseAnim(R"({"version":4,"clips":[{"name":"c","root":{"name":"n","trail":{"count":17}}}]})", l_oBack, &l_strErr),
          "a trail over 16 afterimages");
    CHECK(!f_ParseAnim(R"({"version":5,"clips":[]})", l_oBack, &l_strErr), "a newer version");

    // ---- subtree ranges ----
    const Clip l_oC = f_Rig();
    std::vector<NodePose> l_vPoses;
    f_Evaluate(l_oC, 0.5f, l_vPoses);
    const size_t l_nArm = f_PoseOf(l_vPoses, "arm"), l_nHand = f_PoseOf(l_vPoses, "hand"), l_nRoot = f_PoseOf(l_vPoses, "root");
    CHECK(l_vPoses.size() == 7 && l_vPoses[l_nRoot].m_uFirst == 0 && l_vPoses[l_nRoot].m_uEnd == 7, "the root's subtree: every pose");
    CHECK(l_vPoses[l_nArm].m_uEnd - l_vPoses[l_nArm].m_uFirst == 4 && l_vPoses[l_vPoses[l_nArm].m_uFirst].m_pNode->m_strName == "arm", "the arm: itself, upper, hand, fist");
    CHECK(l_vPoses[l_nHand].m_uFirst == l_nHand && l_vPoses[l_nHand].m_uEnd == l_nHand + 2, "the hand: itself and the fist");
    CHECK(f_Near(l_vPoses[l_nArm].m_fClipTime, 0.5f), "the clip's time in every pose");
    PoseCache l_oCache;
    l_oCache.f_Bind(&l_oC);
    l_oCache.f_Seek(0.5f);
    bool l_bSame = l_oCache.f_Poses().size() == l_vPoses.size();
    for (size_t l_nI = 0; l_bSame && l_nI < l_vPoses.size(); l_nI++)
        l_bSame = l_oCache.f_Poses()[l_nI].m_uFirst == l_vPoses[l_nI].m_uFirst && l_oCache.f_Poses()[l_nI].m_uEnd == l_vPoses[l_nI].m_uEnd;
    CHECK(l_bSame, "PoseCache gives the same ranges");

    // ---- layers around subtrees ----
    toms::AtlasFile l_oAtlas;
    toms::f_ParseAtlas("page.png\nsize: 64, 64\nbox\n  bounds: 0, 0, 16, 16\n", l_oAtlas, &l_strErr);
    AtlasSet l_oSet(l_oAtlas);
    DrawList l_oD;
    f_AppendDraws(l_oC, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    // back, [blur: arm's upper, [glow: hand's fist] ], front -- the arm and the hand have no sprites
    CHECK(f_Shape(l_oD) == "q[1q[2q]]q", "the blur holds the arm's subtree, the glow the hand's inside it, the rest outside");
    CHECK(l_oD.m_vLayers.size() == 2 && f_Near(l_oD.m_vLayers[0].m_arrParams[0], 20), "the blur vector at the node's time (half way: 20)");
    CHECK(f_Near(l_oD.m_vLayers[1].m_arrParams[0], 2) && f_Near(l_oD.m_vLayers[1].m_arrParams[1], 10) && f_Near(l_oD.m_vLayers[1].m_arrColor[1], 0.5f),
          "the glow's params and colour");
    f_Evaluate(l_oC, 0, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oC, l_vPoses, l_oSet, f_Placement(0, 0, 2.0f), nullptr, l_oD);
    CHECK(f_Shape(l_oD) == "qq[2q]q", "no blur at t = 0 (a zero vector): no layer, the glow still");
    CHECK(f_Near(l_oD.m_vLayers[0].m_arrParams[1], 20), "lengths scale with the placement (radius 10 at scale 2)");
    f_Evaluate(l_oC, 1, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oC, l_vPoses, l_oSet, f_Placement(0, 0, 1.0f, 90.0f), nullptr, l_oD);
    CHECK(f_Near(l_oD.m_vLayers[0].m_arrParams[0], 0) && f_Near(l_oD.m_vLayers[0].m_arrParams[1], 40), "the blur vector turns with the placement");
    Clip l_oHidden = f_Rig();
    l_oHidden.m_oRoot.m_vChildren[1].m_bVisible = false;
    f_Evaluate(l_oHidden, 1, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oHidden, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vLayers.empty(), "a hidden subtree: no layers");
    Clip l_oFlash = f_Rig();
    l_oFlash.m_oRoot.m_vChildren[1].m_oFx.m_eShader = FxShader::Flash;
    l_oFlash.m_oRoot.m_vChildren[1].m_vFxKeys.clear();
    f_Evaluate(l_oFlash, 1, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oFlash, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(f_Shape(l_oD) == "qq[2q]q", "a flash of amount 0 shows nothing: no layer");

    // ---- afterimages ----
    Clip l_oTrail = f_Rig();
    Node& l_oFront = l_oTrail.m_oRoot.m_vChildren[2];
    l_oFront.m_oTrail.m_iCount = 3;
    l_oFront.m_oTrail.m_fGap = 0.1f;
    l_oFront.m_oTrail.m_v4Color = {1, 1, 1, 0.6f};
    Key<glm::vec3> l_oP0, l_oP1;
    l_oP1.m_fT = 1;
    l_oP1.m_oV = {100, 0, 0};
    l_oFront.m_vPosKeys = {l_oP0, l_oP1};
    f_Evaluate(l_oTrail, 0.5f, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oTrail, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vQuads.size() == 7, "three afterimages of the front before it (4 sprites + 3)");
    if (l_oD.m_vQuads.size() == 7) {
        // the oldest (0.2 s) first, faintest; the newest (0.4 s) last, at the trail's alpha
        CHECK(f_Near(l_oD.m_vQuads[3].m_arrTint[3], 0.2f) && f_Near(l_oD.m_vQuads[5].m_arrTint[3], 0.6f), "they fade, oldest first");
        CHECK(f_Near(l_oD.m_vQuads[3].m_arrCorners[0], 20 - 8) && f_Near(l_oD.m_vQuads[5].m_arrCorners[0], 40 - 8), "each where the node was then");
    }
    f_Evaluate(l_oTrail, 0.15f, l_vPoses);
    l_oD.f_Clear();
    f_AppendDraws(l_oTrail, l_vPoses, l_oSet, glm::mat3(1.0f), nullptr, l_oD);
    CHECK(l_oD.m_vQuads.size() == 5, "none from before the clip began (only 0.05 s back)");

    if (g_iFail) { std::printf("anim_fx_test: %d failure(s)\n", g_iFail); return 1; }
    std::printf("anim_fx_test: OK\n");
    return 0;
}

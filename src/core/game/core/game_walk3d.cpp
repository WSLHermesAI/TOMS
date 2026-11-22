// game_walk3d.cpp -- walking a floor in 3D (docs/gameplay/24_STAGE_3D.md). The host makes the floor's model from its
// tile map when the floor is entered (its walls and ground: floor_mesh.h) and draws it with the battle stage's renderer
// and a camera tilted down on the hero; the game draws the map's objects as before, each in its own world group
// standing on its tile (the battle's fighters' way, docs/gameplay/21 §4.3), and the ground's marks (stairs, padlocks,
// the walk target) lying on the floor. One tile is one metre: x its column, z its row, y up. There is no 2D map: while
// the floor's model loads, the screen shows a turning circle (f_DrawLoadingCircle).
#include "game_internal.h"
#include "map_objects.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace toms::game_detail;

nlohmann::json Game::f_Walk3DStage() const {
    if (!m_jStage3D.is_object() || m_oSt.m_iWidth <= 0 || m_oSt.m_iHeight <= 0) return nullptr;
    // the look: the floor's theme where data/stage3d.json has it (its painted textures, its light), else the default
    const nlohmann::json l_jLooks = m_jStage3D.value("looks", nlohmann::json::object());
    const std::string l_strLook = l_jLooks.contains(m_oSt.m_strTheme) ? m_oSt.m_strTheme : m_jStage3D.value("defaultLook", std::string("castle"));
    nlohmann::json l_jRow = l_jLooks.value(l_strLook, nlohmann::json::object());
    if (!l_jRow.is_object()) l_jRow = nlohmann::json::object();
    // the walls and the stairs only (a door or a monster's tile is ground): a door opening does not make the floor again
    nlohmann::json l_jWalls = nlohmann::json::array();
    for (int l_iY = 0; l_iY < m_oSt.m_iHeight; l_iY++) {
        std::string l_strRow((size_t)m_oSt.m_iWidth, '.');
        for (int l_iX = 0; l_iX < m_oSt.m_iWidth; l_iX++) {
            const char l_cC = m_oSt.f_At(l_iX, l_iY);
            if (l_cC == '#' || l_cC == 'U' || l_cC == 'D') l_strRow[(size_t)l_iX] = l_cC;   // walls, and the stairs' steps
        }
        l_jWalls.push_back(l_strRow);
    }
    // the floor as a prop: its walls cast the sun's shadows (a "ground" casts none)
    nlohmann::json l_jFloor = {{"walls", l_jWalls}, {"look", l_strLook}, {"wallHeight", m_jStage3D.value("wallHeight", 1.0f)},
                               {"groundRepeat", m_jStage3D.value("groundRepeat", 2.0f)}};
    l_jRow["props"] = nlohmann::json::array({{{"floor", l_jFloor}, {"at", {0, 0, 0}}}});
    const std::string l_strShadow = m_jStage3D.value("shadow", std::string());
    if (!l_strShadow.empty() && m_jStage3D.value("sprites", nlohmann::json::object()).value("shadow", 0.3f) > 0.0f) l_jRow["shadow"] = l_strShadow;
    l_jRow["sprites"] = {{"glow", m_jStage3D.value("sprites", nlohmann::json::object()).value("glow", 0.5f)}};
    l_jRow["shadowBounds"] = {{-1, -1.2, -1}, {m_oSt.m_iWidth + 1, 2, m_oSt.m_iHeight + 1}};   // (down: the stairs' pits)
    return l_jRow;
}

void Game::f_CycleViewScale() {
    std::vector<float> l_vScales;
    if (m_jStage3D.is_object())
        for (const auto& l_jS : m_jStage3D.value("viewScales", nlohmann::json::array())) if (l_jS.is_number()) l_vScales.push_back(l_jS.get<float>());
    if (l_vScales.empty()) l_vScales = {1.0f, 1.5f, 2.0f, 3.0f};
    std::sort(l_vScales.begin(), l_vScales.end());
    float l_fNext = l_vScales.front();
    for (float l_fS : l_vScales) if (l_fS > m_oSettings.m_fViewScale + 0.001f) { l_fNext = l_fS; break; }
    m_oSettings.m_fViewScale = l_fNext;
    toms::f_EnsureSaveDir(toms::f_DefaultSaveDir());
    toms::f_SaveGameSettings(toms::f_SettingsPath(), m_oSettings);
}

void Game::f_SetSceneLoading(bool l_bLoading, int l_iDtMs) {
    if (!l_bLoading) { m_iSceneLoadingMs = -1; return; }
    m_iSceneLoadingMs = std::max(0, m_iSceneLoadingMs) + std::max(0, l_iDtMs);
}

// A scene that takes a while to come (the floor's or the battle's 3D stage loading): after a short moment (a quick load
// shows nothing), twelve dots in a circle at the middle of the screen, the bright one going round, fading in.
void Game::f_DrawLoadingCircle() {
    const int l_iDelayMs = m_jStage3D.is_object() ? m_jStage3D.value("loadingDelayMs", 250) : 250;
    if (m_iSceneLoadingMs < l_iDelayMs || !m_pRen) return;
    m_pRen->f_SetNode(NODE_STAGE);
    const float l_fFade = std::min(1.0f, (float)(m_iSceneLoadingMs - l_iDelayMs) / 300.0f);
    const float l_fCx = (float)m_pRen->f_Width() * 0.5f, l_fCy = (float)m_pRen->f_Height() * 0.5f;
    const float l_fR = 28.0f, l_fDot = 9.0f;
    const float l_fTurn = (float)std::fmod((double)m_iSceneLoadingMs / 1000.0, 1.0);   // one turn a second
    for (int l_iI = 0; l_iI < 12; l_iI++) {
        const float l_fA = (float)l_iI / 12.0f * 6.2831853f - 1.5707963f;
        // how far behind the bright dot this one is (0 = it): the tail fades
        const float l_fBehind = std::fmod(l_fTurn * 12.0f - (float)l_iI + 12.0f, 12.0f) / 12.0f;
        const float l_fLight = 0.25f + 0.75f * (1.0f - l_fBehind) * (1.0f - l_fBehind);
        Quad l_oQ;
        l_oQ.m_arrRect[0] = l_fCx + std::cos(l_fA) * l_fR - l_fDot * 0.5f;
        l_oQ.m_arrRect[1] = l_fCy + std::sin(l_fA) * l_fR - l_fDot * 0.5f;
        l_oQ.m_arrRect[2] = l_fDot;
        l_oQ.m_arrRect[3] = l_fDot;
        l_oQ.m_arrUv[0] = 0; l_oQ.m_arrUv[1] = 0; l_oQ.m_arrUv[2] = 1; l_oQ.m_arrUv[3] = 1;
        l_oQ.m_bSolid = true;
        l_oQ.m_arrTint[0] = 1.0f; l_oQ.m_arrTint[1] = 0.88f; l_oQ.m_arrTint[2] = 0.55f; l_oQ.m_arrTint[3] = l_fLight * l_fFade;
        m_pRen->f_DrawSprite(l_oQ);
    }
}

glm::vec2 Game::f_Walk3DToDesign(const glm::vec3& l_v3P, bool& l_bOk) const {
    l_bOk = false;
    if (!m_pRen || m_v2Walk3DDevice.x <= 0.0f || m_v4Walk3DDesign.z <= 0.0f || m_v4Walk3DDesign.w <= 0.0f) return glm::vec2(0.0f);
    const glm::vec4 l_v4C = m_mat4Walk3DViewProj * glm::vec4(l_v3P, 1.0f);
    if (l_v4C.w <= 1e-4f) return glm::vec2(0.0f);
    l_bOk = true;
    const glm::vec2 l_v2Dev((l_v4C.x / l_v4C.w * 0.5f + 0.5f) * m_v2Walk3DDevice.x, (0.5f - l_v4C.y / l_v4C.w * 0.5f) * m_v2Walk3DDevice.y);
    return glm::vec2((l_v2Dev.x - m_v4Walk3DDesign.x) / m_v4Walk3DDesign.z * (float)m_pRen->f_Width(),
                     (l_v2Dev.y - m_v4Walk3DDesign.y) / m_v4Walk3DDesign.w * (float)m_pRen->f_Height());
}

// W38 off the screen: a stair that just opened and is not in view gets a gold badge at the screen's edge, its arrow
// pointing the way to it (on the screen, over the 3D floor).
void Game::f_DrawWalk3DStairPointer() {
    const float l_fW = (float)m_pRen->f_Width(), l_fH = (float)m_pRen->f_Height(), l_fU = 3.0f, l_fM = 40.0f;
    auto l_fnSolid = [&](float l_fX, float l_fY, float l_fSw, float l_fSh, float l_fR, float l_fG, float l_fB, float l_fA) {
        Quad l_oQ; l_oQ.m_arrRect[0] = l_fX; l_oQ.m_arrRect[1] = l_fY; l_oQ.m_arrRect[2] = l_fSw; l_oQ.m_arrRect[3] = l_fSh;
        l_oQ.m_arrUv[0] = 0; l_oQ.m_arrUv[1] = 0; l_oQ.m_arrUv[2] = 1; l_oQ.m_arrUv[3] = 1; l_oQ.m_bSolid = true;
        l_oQ.m_arrTint[0] = l_fR; l_oQ.m_arrTint[1] = l_fG; l_oQ.m_arrTint[2] = l_fB; l_oQ.m_arrTint[3] = l_fA;
        m_pRen->f_DrawSprite(l_oQ);
    };
    for (const toms::StairLink& l_oS : m_oSt.m_vStairs) {
        if (!f_StairJustOpened(m_oSt.m_strId, l_oS) || l_oS.m_iX < 0) continue;
        bool l_bOk = false;
        const glm::vec2 l_v2P = f_Walk3DToDesign(glm::vec3(l_oS.m_iX + 0.5f, 0.0f, l_oS.m_iY + 0.5f), l_bOk);
        if (l_bOk && l_v2P.x >= 0.0f && l_v2P.x <= l_fW && l_v2P.y >= 0.0f && l_v2P.y <= l_fH) continue;   // in view: its own arrow
        const glm::vec2 l_v2C(l_fW * 0.5f, l_fH * 0.5f);
        const glm::vec2 l_v2D = l_bOk ? l_v2P - l_v2C : glm::vec2(0.0f, -1.0f);
        if (glm::length(l_v2D) < 1e-3f) continue;
        // the badge on the screen's edge (a margin in), toward the stair
        const float l_fK = std::min((l_fW * 0.5f - l_fM) / std::max(1e-3f, std::fabs(l_v2D.x)), (l_fH * 0.5f - l_fM) / std::max(1e-3f, std::fabs(l_v2D.y)));
        const glm::vec2 l_v2B = l_v2C + l_v2D * l_fK;
        const bool l_bHoriz = std::fabs(l_v2D.x) * l_fH >= std::fabs(l_v2D.y) * l_fW;
        const int l_iSign = l_bHoriz ? (l_v2D.x > 0 ? 1 : -1) : (l_v2D.y > 0 ? 1 : -1);
        const float l_fBx = l_v2B.x - 8 * l_fU, l_fBy = l_v2B.y - 8 * l_fU;
        const float l_fPulse = 0.75f + 0.25f * std::sin((float)std::fmod(m_dIdleClockMs, 800.0) / 800.0f * 6.2831853f);
        l_fnSolid(l_fBx + 2 * l_fU, l_fBy + 2 * l_fU, 12 * l_fU, 12 * l_fU, 0.20f, 0.16f, 0.08f, 0.9f);   // badge outline
        l_fnSolid(l_fBx + 3 * l_fU, l_fBy + 3 * l_fU, 10 * l_fU, 10 * l_fU, 1.0f, 0.85f, 0.3f, l_fPulse);   // badge
        for (int l_iR = 0; l_iR < 4; l_iR++) {   // the arrow head
            const float l_fLen = (7.0f - 2.0f * l_iR) * l_fU, l_fOff = (l_iSign > 0 ? 9.0f + l_iR : 6.0f - l_iR) * l_fU;
            if (l_bHoriz) l_fnSolid(l_fBx + l_fOff, l_fBy + 8 * l_fU - l_fLen * 0.5f, l_fU, l_fLen, 0.20f, 0.16f, 0.08f, 1.0f);
            else l_fnSolid(l_fBx + 8 * l_fU - l_fLen * 0.5f, l_fBy + l_fOff, l_fLen, l_fU, 0.20f, 0.16f, 0.08f, 1.0f);
        }
        if (l_bHoriz) l_fnSolid(l_fBx + (l_iSign > 0 ? 4 : 10) * l_fU, l_fBy + 7.5f * l_fU, 5 * l_fU, l_fU, 0.20f, 0.16f, 0.08f, 1.0f);   // the shaft
        else l_fnSolid(l_fBx + 7.5f * l_fU, l_fBy + (l_iSign > 0 ? 4 : 10) * l_fU, l_fU, 5 * l_fU, 0.20f, 0.16f, 0.08f, 1.0f);
    }
}

bool Game::f_WalkSceneShown() const {
    if (m_oTitle.f_IsOpen() || f_EndingActive() || f_StoreModal()) return false;
    if ((m_iHideMask & 1) == 0 && (m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0)) return false;
    if ((m_iHideMask & 2) == 0 && m_bInDialogue) return false;
    return true;
}

Game::Walk3DCamera Game::f_Walk3DCamera(float l_fDtSeconds) {
    const nlohmann::json l_jCam = m_jStage3D.is_object() ? m_jStage3D.value("camera", nlohmann::json::object()) : nlohmann::json::object();
    const glm::vec3 l_v3Target(m_oPl.m_iX + 0.5f, 0.6f, m_oPl.m_iY + 0.5f - l_jCam.value("lookAhead", 0.0f));
    if (m_strWalk3DFloor != m_oSt.m_strId) {   // a new floor: straight on the hero
        m_strWalk3DFloor = m_oSt.m_strId;
        m_v3Walk3DLook = l_v3Target;
    } else {
        const float l_fK = 1.0f - std::exp(-std::max(0.1f, l_jCam.value("follow", 7.0f)) * std::clamp(l_fDtSeconds, 0.0f, 0.25f));
        m_v3Walk3DLook += (l_v3Target - m_v3Walk3DLook) * l_fK;
    }
    const float l_fPitch = glm::radians(std::clamp(l_jCam.value("pitch", 55.0f), 10.0f, 89.0f));
    // Settings > View size: further back, more of the floor (x1.5 about 18 tiles across, x2 about 24, x3 about 36)
    const float l_fDist = std::max(2.0f, l_jCam.value("distance", 11.0f)) * std::clamp(f_ViewScale(), 0.5f, 4.0f);
    Walk3DCamera l_oC;
    l_oC.m_v3Look = m_v3Walk3DLook;
    l_oC.m_v3Eye = m_v3Walk3DLook + glm::vec3(0.0f, std::sin(l_fPitch) * l_fDist, std::cos(l_fPitch) * l_fDist);   // south of him, looking north
    l_oC.m_fFovYDeg = l_jCam.value("fov", 38.0f);
    return l_oC;
}

void Game::f_SetWalk3DView(bool l_bOn, const glm::mat4& l_mat4ViewProj, const glm::vec2& l_v2Device, const glm::vec4& l_v4DesignInDevice) {
    m_bWalk3DOn = l_bOn && m_pRen && m_pRen->f_SupportsWorld();
    if (!m_bWalk3DOn) return;
    m_mat4Walk3DViewProj = l_mat4ViewProj;
    m_v2Walk3DDevice = l_v2Device;
    m_v4Walk3DDesign = l_v4DesignInDevice;
}

std::vector<glm::vec4> Game::f_Walk3DShadows() const {
    std::vector<glm::vec4> l_vOut;
    const float l_fR = m_jStage3D.is_object() ? m_jStage3D.value("sprites", nlohmann::json::object()).value("shadow", 0.3f) : 0.0f;
    if (l_fR <= 0.0f) return l_vOut;
    l_vOut.push_back(glm::vec4(m_oPl.m_iX + 0.5f, 0.0f, m_oPl.m_iY + 0.55f, l_fR));
    for (const Entity& l_oE : m_oSt.m_vEntities) {
        if (l_oE.m_bConsumed) continue;
        const std::string l_strK = l_oE.m_strKind.substr(0, l_oE.m_strKind.find(':'));
        if (l_strK != "monster" && l_strK != "npc") continue;
        l_vOut.push_back(glm::vec4(l_oE.m_iX + l_oE.m_oFp.m_iW * 0.5f, 0.0f, l_oE.m_iY + l_oE.m_oFp.m_iH - 0.45f, l_fR * (float)l_oE.m_oFp.m_iW));
    }
    return l_vOut;
}

std::vector<Game::Battle3DLight> Game::f_Walk3DLights() const {
    std::vector<Battle3DLight> l_vOut;
    const nlohmann::json l_jL = m_jStage3D.is_object() ? m_jStage3D.value("stairLights", nlohmann::json::object()) : nlohmann::json::object();
    if (l_jL.value("off", false)) return l_vOut;
    auto l_fnColor = [&](const char* l_pszKey, const glm::vec3& l_v3Default) {
        const nlohmann::json l_jC = l_jL.value(l_pszKey, nlohmann::json());
        return l_jC.is_array() && l_jC.size() >= 3 ? glm::vec3(l_jC[0].get<float>(), l_jC[1].get<float>(), l_jC[2].get<float>()) : l_v3Default;
    };
    const float l_fWallH = m_jStage3D.value("wallHeight", 1.0f);
    for (const toms::StairLink& l_oS : m_oSt.m_vStairs) {
        if (l_oS.m_iX < 0) continue;
        Battle3DLight l_oL;
        l_oL.m_v3Pos = glm::vec3(l_oS.m_iX + 0.5f, l_oS.m_bUp ? l_fWallH + 0.7f : -0.3f, l_oS.m_iY + 0.5f);
        l_oL.m_v3Color = l_oS.m_bUp ? l_fnColor("up", {1.0f, 0.8f, 0.45f}) : l_fnColor("down", {0.45f, 0.65f, 1.0f});
        l_oL.m_fIntensity = l_jL.value("intensity", 6.0f) * (f_StairJustOpened(m_oSt.m_strId, l_oS) ? 1.6f : f_StairLocked(l_oS) ? 0.5f : 1.0f);
        l_oL.m_fRange = l_jL.value("range", 3.0f);
        l_vOut.push_back(l_oL);
    }
    const glm::vec3 l_v3Hero(m_oPl.m_iX + 0.5f, 0.0f, m_oPl.m_iY + 0.5f);
    std::sort(l_vOut.begin(), l_vOut.end(), [&](const Battle3DLight& l_oA, const Battle3DLight& l_oB) {
        return glm::length(glm::vec2(l_oA.m_v3Pos.x, l_oA.m_v3Pos.z) - glm::vec2(l_v3Hero.x, l_v3Hero.z)) <
               glm::length(glm::vec2(l_oB.m_v3Pos.x, l_oB.m_v3Pos.z) - glm::vec2(l_v3Hero.x, l_v3Hero.z));
    });
    return l_vOut;
}

float Game::f_Walk3DHeight(const std::string& l_strKind) const {
    const nlohmann::json l_jH = m_jStage3D.is_object() ? m_jStage3D.value("sprites", nlohmann::json::object()).value("heights", nlohmann::json::object())
                                                      : nlohmann::json::object();
    if (l_jH.contains(l_strKind) && l_jH[l_strKind].is_number()) return l_jH[l_strKind].get<float>();
    return l_jH.value("other", 1.0f);
}

glm::mat4 Game::f_Walk3DSpriteMatrix(const glm::vec2& l_v2Anchor, const glm::vec3& l_v3At, float l_fMetresPerPx) const {
    // the camera's right on the ground, the way it looks along the ground (away), the sprite's up leaning back that much
    glm::vec3 l_v3Right(m_mat4Walk3DViewProj[0][0], 0.0f, m_mat4Walk3DViewProj[2][0]);
    l_v3Right = glm::length(l_v3Right) > 1e-6f ? glm::normalize(l_v3Right) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 l_v3Away(l_v3Right.z, 0.0f, -l_v3Right.x);
    const float l_fLean = glm::radians(m_jStage3D.is_object() ? m_jStage3D.value("sprites", nlohmann::json::object()).value("lean", 20.0f) : 20.0f);
    const glm::vec3 l_v3Up = glm::vec3(0.0f, std::cos(l_fLean), 0.0f) + l_v3Away * std::sin(l_fLean);
    const glm::vec3 l_v3Normal = -l_v3Away * std::cos(l_fLean) + glm::vec3(0.0f, std::sin(l_fLean), 0.0f);   // toward the camera
    glm::mat4 l_mat4M(1.0f);
    l_mat4M[0] = glm::vec4(l_v3Right * l_fMetresPerPx, 0.0f);
    l_mat4M[1] = glm::vec4(-l_v3Up * l_fMetresPerPx, 0.0f);
    l_mat4M[2] = glm::vec4(l_v3Normal, 0.0f);
    l_mat4M[3] = glm::vec4(l_v3At - l_v3Right * (l_v2Anchor.x * l_fMetresPerPx) + l_v3Up * (l_v2Anchor.y * l_fMetresPerPx), 1.0f);
    return l_mat4M;
}

// An object of the map (MapFloor::f_Render): drawn as in 2D into its rect on a map of m_fWalk3DTile pixels a tile, that
// rect standing on its bottom row's middle, its sprite the kind's height (a one-tile sprite; a bigger one in proportion).
bool Game::f_Walk3DBeginObject(const MapObject& l_oO, const MapView& l_oV) {
    const MapRect l_oR = l_oO.f_Rect(l_oV);
    if (l_oR.m_fW <= 0.0f || l_oR.m_fH <= 0.0f || l_oV.m_fSSize <= 0.0f) return false;
    const Entity* l_pE = l_oO.f_Entity();
    const std::string l_strKind = l_pE ? l_pE->m_strKind.substr(0, l_pE->m_strKind.find(':')) : std::string("player");
    const float l_fMpp = f_Walk3DHeight(l_strKind) / l_oV.m_fSSize;
    const glm::vec2 l_v2Feet(l_oR.m_fX + l_oR.m_fW * 0.5f, l_oR.m_fY + l_oR.m_fH);
    const glm::vec3 l_v3At(l_v2Feet.x / l_oV.m_fTs, 0.0f, (l_v2Feet.y + l_oV.m_fInset) / l_oV.m_fTs - 0.45f);
    const glm::mat4 l_mat4M = f_Walk3DSpriteMatrix(l_v2Feet, l_v3At, l_fMpp);
    m_pRen->f_BeginWorld(&l_mat4M[0][0], true);
    return true;
}

void Game::f_Walk3DEndObject() { m_pRen->f_EndWorld(); }

void Game::f_DrawWalk3D() {
    MapView l_oV;
    l_oV.m_fTs = m_fWalk3DTile;
    l_oV.m_fInset = l_oV.m_fTs / 6.0f;
    l_oV.m_fSSize = l_oV.m_fTs - 2.0f * l_oV.m_fInset;
    // the marks lying on the ground, a little above it: a map pixel is 1 / tile metres, its x the world's x, its y the z
    glm::mat4 l_mat4Ground(1.0f);
    l_mat4Ground[0] = glm::vec4(1.0f / l_oV.m_fTs, 0.0f, 0.0f, 0.0f);
    l_mat4Ground[1] = glm::vec4(0.0f, 0.0f, 1.0f / l_oV.m_fTs, 0.0f);
    l_mat4Ground[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);   // facing up
    l_mat4Ground[3] = glm::vec4(0.0f, 0.015f, 0.0f, 1.0f);
    m_pRen->f_BeginWorld(&l_mat4Ground[0][0], true);
    f_DrawGroundMarks(l_oV);
    m_pRen->f_EndWorld();
    m_oFloor.f_Render(l_oV);   // each object in its own standing group (f_Walk3DBeginObject)
    // each stair's sign (a padlock, the gold arrow) standing over it: on the top step (up) or over the pit (down)
    const float l_fWallH = m_jStage3D.value("wallHeight", 1.0f);
    for (const toms::StairLink& l_oS : m_oSt.m_vStairs) {
        if (l_oS.m_iX < 0) continue;
        const float l_fBase = l_oS.m_bUp ? l_fWallH : 0.0f;
        const glm::mat4 l_mat4M = f_Walk3DSpriteMatrix(glm::vec2(l_oV.m_fTs * 0.5f, l_oV.m_fTs), glm::vec3(l_oS.m_iX + 0.5f, l_fBase, l_oS.m_iY + 0.5f),
                                                      0.9f / l_oV.m_fTs);
        m_pRen->f_BeginWorld(&l_mat4M[0][0], false);
        f_DrawStairSign(l_oS, 0.0f, 0.0f, l_oV.m_fTs);
        m_pRen->f_EndWorld();
    }
    f_DrawWalk3DStairPointer();
}

// A point of the screen (design pixels) -> the tile under it on the ground (the 3D floor's f_ScreenToTile).
bool Game::f_Walk3DScreenToTile(float l_fPx, float l_fPy, int& l_iTx, int& l_iTy) const {
    if (!m_pRen || m_v2Walk3DDevice.x <= 0.0f || m_v2Walk3DDevice.y <= 0.0f) return false;
    const glm::vec2 l_v2Dev(m_v4Walk3DDesign.x + l_fPx / (float)m_pRen->f_Width() * m_v4Walk3DDesign.z,
                            m_v4Walk3DDesign.y + l_fPy / (float)m_pRen->f_Height() * m_v4Walk3DDesign.w);
    const glm::vec2 l_v2Ndc(l_v2Dev.x / m_v2Walk3DDevice.x * 2.0f - 1.0f, 1.0f - l_v2Dev.y / m_v2Walk3DDevice.y * 2.0f);
    const glm::mat4 l_mat4Inv = glm::inverse(m_mat4Walk3DViewProj);
    auto l_fnAt = [&](float l_fZ) { const glm::vec4 l_v4P = l_mat4Inv * glm::vec4(l_v2Ndc, l_fZ, 1.0f); return glm::vec3(l_v4P) / l_v4P.w; };
    const glm::vec3 l_v3A = l_fnAt(0.2f), l_v3B = l_fnAt(0.8f);   // two points on the ray (inside either depth range)
    if (std::fabs(l_v3B.y - l_v3A.y) < 1e-6f) return false;
    const float l_fT = -l_v3A.y / (l_v3B.y - l_v3A.y);
    const glm::vec3 l_v3G = l_v3A + (l_v3B - l_v3A) * l_fT;
    l_iTx = (int)std::floor(l_v3G.x);
    l_iTy = (int)std::floor(l_v3G.z);
    return l_iTx >= 0 && l_iTy >= 0 && l_iTx < (int)m_oSt.m_iWidth && l_iTy < (int)m_oSt.m_iHeight;
}

bool Game::f_DrawWalk3DHero() {
    const nlohmann::json l_jSp = m_jStage3D.is_object() ? m_jStage3D.value("sprites", nlohmann::json::object()) : nlohmann::json::object();
    if (!l_jSp.value("paintedHero", false)) return false;
    auto l_itDef = m_mapEquipmentDefs.find(m_oEquipped.m_strWeaponId);
    if (l_itDef == m_mapEquipmentDefs.end()) return false;
    if (!m_upWalkHeroView || m_strWalkHeroWeapon != m_oEquipped.m_strWeaponId) {   // the weapon's look, once per weapon
        m_strWalkHeroWeapon = m_oEquipped.m_strWeaponId;
        toms::FighterLook l_oLook;
        l_oLook.m_strRender = l_itDef->second.m_strBattleRender;
        l_oLook.m_strCharacter = l_itDef->second.m_strBattleCharacterAnim;
        l_oLook.m_strHeld = l_itDef->second.m_strBattleHeldAnim;
        l_oLook.m_strModel = l_itDef->second.m_strBattleModel;
        m_upWalkHeroView = f_MakeFighterView(l_oLook, [this](const toms::anim::Clip& l_oC) { return f_FitClipToCharacter(l_oC, f_PlayerSprite()); },
                                             32.0f, false);
        if (m_upWalkHeroView && !m_upWalkHeroView->f_Play(l_itDef->second.f_BattleClip("idle"), true)) m_upWalkHeroView.reset();
        m_dWalkHeroClockMs = -1.0;
    }
    if (!m_upWalkHeroView) return false;
    if (m_dWalkHeroClockMs >= 0.0) m_upWalkHeroView->f_Update((int)std::clamp(m_dIdleClockMs - m_dWalkHeroClockMs, 0.0, 100.0));
    m_dWalkHeroClockMs = m_dIdleClockMs;
    if (m_iWalkHeroLastX >= 0 && m_oPl.m_iX != m_iWalkHeroLastX) m_iWalkHeroFacing = m_oPl.m_iX < m_iWalkHeroLastX ? -1 : 1;
    m_iWalkHeroLastX = m_oPl.m_iX;
    // the battle's box shape: its height is the hero's height (as on the 3D battle stage)
    glm::vec4 l_v4Box(0.0f, 0.0f, std::max(1.0f, m_v4UiPlayerBox.z), std::max(1.0f, m_v4UiPlayerBox.w));
    const glm::vec2 l_v2Feet(l_v4Box.z * 0.5f, l_v4Box.w);
    glm::mat4 l_mat4M = f_Walk3DSpriteMatrix(l_v2Feet, glm::vec3(m_oPl.m_iX + 0.5f, 0.0f, m_oPl.m_iY + 0.55f), l_jSp.value("paintedHeroHeight", 1.6f) / l_v4Box.w);
    if (m_iWalkHeroFacing < 0) {   // turned left: mirrored about his feet
        const glm::vec3 l_v3Feet = glm::vec3(l_mat4M * glm::vec4(l_v2Feet, 0.0f, 1.0f));
        l_mat4M[0] = -l_mat4M[0];
        l_mat4M[3] = glm::vec4(l_v3Feet - glm::vec3(l_mat4M * glm::vec4(l_v2Feet, 0.0f, 0.0f)), 1.0f);
    }
    m_pRen->f_BeginWorld(&l_mat4M[0][0], true);
    m_upWalkHeroView->f_Draw(m_pRen, l_v4Box);
    m_pRen->f_EndWorld();
    return true;
}

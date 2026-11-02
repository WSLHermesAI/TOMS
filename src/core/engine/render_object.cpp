// render_object.cpp -- see render_object.h.
#include "render_object.h"

#include "anim_clip.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {
bool f_IsWhite(const float l_arrT[4]) { return l_arrT[0] == 1 && l_arrT[1] == 1 && l_arrT[2] == 1 && l_arrT[3] == 1; }

// "file#name" -> (file, name); no '#': (s, "")
std::pair<std::string, std::string> f_SplitRef(const std::string& l_strS) {
    const size_t l_nH = l_strS.find('#');
    return l_nH == std::string::npos ? std::make_pair(l_strS, std::string()) : std::make_pair(l_strS.substr(0, l_nH), l_strS.substr(l_nH + 1));
}

std::string f_Str(const nlohmann::json& l_jJ, const char* l_pszKey) {
    auto l_oIt = l_jJ.find(l_pszKey);
    return l_oIt != l_jJ.end() && l_oIt->is_string() ? l_oIt->get<std::string>() : std::string();
}

bool f_ReadNumbers(const nlohmann::json& l_jJ, const char* l_pszKey, float* l_pOut, size_t l_nN) {
    auto l_oIt = l_jJ.find(l_pszKey);
    if (l_oIt == l_jJ.end() || !l_oIt->is_array() || l_oIt->size() < l_nN) return false;
    for (size_t l_nI = 0; l_nI < l_nN; l_nI++) {
        if (!(*l_oIt)[l_nI].is_number()) return false;
        l_pOut[l_nI] = (*l_oIt)[l_nI].get<float>();
    }
    return true;
}

nlohmann::json f_Numbers(const float* l_pV, size_t l_nN) {
    nlohmann::json l_jA = nlohmann::json::array();
    for (size_t l_nI = 0; l_nI < l_nN; l_nI++) l_jA.push_back(l_pV[l_nI]);
    return l_jA;
}

const char* f_AlignName(::BitmapFont::Align l_eA) {
    return l_eA == ::BitmapFont::Align::Center ? "center" : l_eA == ::BitmapFont::Align::Right ? "right" : "left";
}
}  // namespace

// ---- RenderObject ------------------------------------------------------------------------------------------------

void RenderObject::f_ApplyTransform() {
    f_SetLocalTransform(anim::f_Flatten(anim::f_Compose({m_v2Pos.x, m_v2Pos.y, 0}, {0, 0, m_fRotation}, {m_v2Scale.x, m_v2Scale.y, 1})));
}

void RenderObject::f_AddChild(std::shared_ptr<RenderObject> l_spChild) {
    if (!l_spChild) return;
    f_AddChildToLast(l_spChild.get(), false);
    m_vOwned.push_back(std::move(l_spChild));
}

void RenderObject::f_Update(float l_fDt) {
    for (const auto& l_spC : m_vOwned) l_spC->f_Update(l_fDt);
}

void RenderObject::f_SetView(const glm::mat3& l_mat3View) {
    m_mat3View = l_mat3View;
    for (const auto& l_spC : m_vOwned) l_spC->f_SetView(l_mat3View);
}

void RenderObject::f_Render(IRenderer* l_pRen) {
    if (l_pRen) f_Draw(l_pRen, m_mat3View * f_GetWorldTransform());
}

void RenderObject::f_CopyCommonTo(RenderObject& l_oO) const {
    l_oO.f_SetName(f_Name());
    std::copy(m_arrTint, m_arrTint + 4, l_oO.m_arrTint);
    l_oO.m_v2Pos = m_v2Pos;
    l_oO.m_v2Scale = m_v2Scale;
    l_oO.m_fRotation = m_fRotation;
    l_oO.f_ApplyTransform();
    l_oO.f_SetVisible(f_IsVisible());
    for (const auto& l_spC : m_vOwned) l_oO.f_AddChild(l_spC->f_Clone());
}

nlohmann::json RenderObject::f_CommonJson() const {
    nlohmann::json l_jJ = nlohmann::json::object();
    l_jJ["type"] = f_TypeName();
    if (!f_Name().empty()) l_jJ["name"] = f_Name();
    if (m_v2Pos != glm::vec2(0.0f)) l_jJ["pos"] = {m_v2Pos.x, m_v2Pos.y};
    if (m_v2Scale != glm::vec2(1.0f)) {
        if (m_v2Scale.x == m_v2Scale.y) l_jJ["scale"] = m_v2Scale.x;
        else l_jJ["scale"] = {m_v2Scale.x, m_v2Scale.y};
    }
    if (m_fRotation != 0) l_jJ["rotation"] = m_fRotation;
    if (!f_IsWhite(m_arrTint)) l_jJ["tint"] = f_Numbers(m_arrTint, 4);
    if (!f_IsVisible()) l_jJ["visible"] = false;
    if (!m_vOwned.empty()) {
        nlohmann::json l_jKids = nlohmann::json::array();
        for (const auto& l_spC : m_vOwned) l_jKids.push_back(l_spC->f_ToJson());
        l_jJ["children"] = std::move(l_jKids);
    }
    return l_jJ;
}

// ---- the factory ---------------------------------------------------------------------------------------------------

void RenderObjectFactory::f_Add(const std::string& l_strType, MakeFn l_fnMake) { m_mapMakers[l_strType] = l_fnMake; }

std::vector<std::string> RenderObjectFactory::f_Types() const {
    std::vector<std::string> l_vOut;
    for (const auto& [l_strT, l_fnF] : m_mapMakers) l_vOut.push_back(l_strT);
    return l_vOut;
}

std::shared_ptr<RenderObject> RenderObjectFactory::f_Build(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError) const {
    auto l_fnFail = [&](const std::string& l_strWhy) {
        l_strError = (l_oCtx.m_strWhere.empty() ? std::string() : l_oCtx.m_strWhere + ": ") + l_strWhy;
        return std::shared_ptr<RenderObject>();
    };
    if (!l_jJ.is_object()) return l_fnFail("not a JSON object");
    const std::string l_strType = f_Str(l_jJ, "type");
    if (l_strType.empty()) return l_fnFail("no \"type\"");
    auto l_itMaker = m_mapMakers.find(l_strType);
    if (l_itMaker == m_mapMakers.end()) return l_fnFail("unknown type '" + l_strType + "'");
    std::string l_strWhy;
    std::shared_ptr<RenderObject> l_spO = l_itMaker->second(l_jJ, l_oCtx, l_strWhy);
    if (!l_spO) return l_fnFail(l_strType + ": " + l_strWhy);
    // the fields every type shares
    if (const std::string l_strN = f_Str(l_jJ, "name"); !l_strN.empty()) l_spO->f_SetName(l_strN);
    float l_arrV[4];
    if (f_ReadNumbers(l_jJ, "pos", l_arrV, 2)) l_spO->m_v2Pos = {l_arrV[0], l_arrV[1]};
    if (auto l_oS = l_jJ.find("scale"); l_oS != l_jJ.end()) {
        if (l_oS->is_number()) l_spO->m_v2Scale = glm::vec2(l_oS->get<float>());
        else if (f_ReadNumbers(l_jJ, "scale", l_arrV, 2)) l_spO->m_v2Scale = {l_arrV[0], l_arrV[1]};
    }
    if (auto l_oR = l_jJ.find("rotation"); l_oR != l_jJ.end() && l_oR->is_number()) l_spO->m_fRotation = l_oR->get<float>();
    l_spO->f_ApplyTransform();
    f_ReadNumbers(l_jJ, "tint", l_spO->m_arrTint, 4);
    if (auto l_oVis = l_jJ.find("visible"); l_oVis != l_jJ.end() && l_oVis->is_boolean()) l_spO->f_SetVisible(l_oVis->get<bool>());
    if (auto l_oKids = l_jJ.find("children"); l_oKids != l_jJ.end()) {
        if (!l_oKids->is_array()) return l_fnFail("\"children\" is not a list");
        for (size_t l_nI = 0; l_nI < l_oKids->size(); l_nI++) {
            BuildContext l_oSub{l_oCtx.m_oRes, l_oCtx.m_oFactory, (l_oCtx.m_strWhere.empty() ? "" : l_oCtx.m_strWhere + ".") + "children[" + std::to_string(l_nI) + "]"};
            std::shared_ptr<RenderObject> l_spC = f_Build((*l_oKids)[l_nI], l_oSub, l_strError);
            if (!l_spC) return nullptr;   // error says where
            l_spO->f_AddChild(std::move(l_spC));
        }
    }
    return l_spO;
}

std::shared_ptr<RenderObject> RenderObjectFactory::f_Make(const nlohmann::json& l_jJ, res::ResourceManager& l_oRes, std::string* l_pError) const {
    BuildContext l_oCtx{l_oRes, *this, std::string()};
    std::string l_strErr;
    std::shared_ptr<RenderObject> l_spO = f_Build(l_jJ, l_oCtx, l_strErr);
    if (l_pError) *l_pError = l_spO ? std::string() : l_strErr;
    return l_spO;
}

std::shared_ptr<RenderObject> RenderObjectFactory::f_MakeOrMissing(const nlohmann::json& l_jJ, res::ResourceManager& l_oRes, std::string* l_pError) const {
    std::string l_strErr;
    if (std::shared_ptr<RenderObject> l_spO = f_Make(l_jJ, l_oRes, &l_strErr)) {
        if (l_pError) l_pError->clear();
        return l_spO;
    }
    auto l_spM = std::make_shared<MissingObject>();
    l_spM->m_strWhat = l_strErr;
    float l_arrV[2];
    if (l_jJ.is_object() && f_ReadNumbers(l_jJ, "pos", l_arrV, 2)) l_spM->f_SetPos(l_arrV[0], l_arrV[1]);
    if (l_pError) *l_pError = l_strErr;
    return l_spM;
}

std::shared_ptr<RenderObject> RenderObjectFactory::f_Instance(const nlohmann::json& l_jJ, res::ResourceManager& l_oRes, std::string* l_pError) {
    const std::string l_strKey = l_jJ.dump();
    auto l_itIt = m_mapPrototypes.find(l_strKey);
    if (l_itIt == m_mapPrototypes.end()) {
        std::shared_ptr<RenderObject> l_spProto = f_Make(l_jJ, l_oRes, l_pError);
        if (!l_spProto) return nullptr;
        l_itIt = m_mapPrototypes.emplace(l_strKey, std::move(l_spProto)).first;
    } else if (l_pError) {
        l_pError->clear();
    }
    return l_itIt->second->f_Clone();
}

void f_RegisterBuiltinRenderObjects(RenderObjectFactory& l_oF) {
    l_oF.f_Add<SpriteObject>();
    l_oF.f_Add<ClipObject>();
    l_oF.f_Add<ParticleObject>();
    l_oF.f_Add<TextObject>();
    l_oF.f_Add<GroupObject>();
    l_oF.f_Add<MissingObject>();
}

// ---- sprite --------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> SpriteObject::f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError) {
    auto l_spO = std::make_shared<SpriteObject>();
    l_spO->m_strAtlasPath = f_Str(l_jJ, "atlas");
    l_spO->m_strName = f_Str(l_jJ, "name");
    if (l_spO->m_strName.empty()) { l_strError = "no \"name\" (the sprite in the atlas)"; return nullptr; }
    l_spO->m_spAtlas = l_oCtx.m_oRes.f_Get<res::AtlasRes>(l_spO->m_strAtlasPath.empty() ? std::string(res::ResourceManager::m_pszSpriteAtlas) : l_spO->m_strAtlasPath);
    if (!l_spO->m_spAtlas->f_Ok()) { l_strError = "cannot load the atlas " + l_spO->m_spAtlas->f_Path() + " (" + l_spO->m_spAtlas->f_Error() + ")"; return nullptr; }
    if (!l_spO->m_spAtlas->f_Find(l_spO->m_strName)) { l_strError = "no sprite '" + l_spO->m_strName + "' in " + l_spO->m_spAtlas->f_Path(); return nullptr; }
    l_spO->m_bAdditive = f_Str(l_jJ, "blend") == "add";
    if (auto l_oS = l_jJ.find("solid"); l_oS != l_jJ.end() && l_oS->is_boolean()) l_spO->m_bSolid = l_oS->get<bool>();
    return l_spO;
}

std::shared_ptr<RenderObject> SpriteObject::f_Clone() const {
    auto l_spO = std::make_shared<SpriteObject>();
    l_spO->m_spAtlas = m_spAtlas;
    l_spO->m_strName = m_strName;
    l_spO->m_strAtlasPath = m_strAtlasPath;
    l_spO->m_bAdditive = m_bAdditive;
    l_spO->m_bSolid = m_bSolid;
    f_CopyCommonTo(*l_spO);
    return l_spO;
}

nlohmann::json SpriteObject::f_ToJson() const {
    nlohmann::json l_jJ = f_CommonJson();
    if (!m_strAtlasPath.empty()) l_jJ["atlas"] = m_strAtlasPath;
    l_jJ["name"] = m_strName;
    if (m_bAdditive) l_jJ["blend"] = "add";
    if (m_bSolid) l_jJ["solid"] = true;
    return l_jJ;
}

// As anim::makeQuad: the packed pixels inside the original image, around the region's pivot, through the transform.
void SpriteObject::f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) {
    const int l_iV = m_spAtlas->f_ContentVersion();
    if (l_iV != m_iQuadVersion || l_mat3World != m_mat3QuadWorld || !std::equal(m_arrTint, m_arrTint + 4, m_arrQuadTint)) {
        m_iQuadVersion = l_iV;
        m_mat3QuadWorld = l_mat3World;
        std::copy(m_arrTint, m_arrTint + 4, m_arrQuadTint);
        m_bHasQuad = false;
        if (const AtlasRegion* l_pR = m_spAtlas->f_Find(m_strName)) {
            const float l_fX0 = l_pR->m_iOffX - l_pR->m_arrPivot[0] * l_pR->m_iOrigW, l_fY0 = l_pR->m_iOffY - l_pR->m_arrPivot[1] * l_pR->m_iOrigH;
            const float l_fX1 = l_fX0 + l_pR->m_iW, l_fY1 = l_fY0 + l_pR->m_iH;
            const glm::vec3 l_arrC[4] = {l_mat3World * glm::vec3(l_fX0, l_fY0, 1), l_mat3World * glm::vec3(l_fX1, l_fY0, 1), l_mat3World * glm::vec3(l_fX1, l_fY1, 1),
                                    l_mat3World * glm::vec3(l_fX0, l_fY1, 1)};
            Quad l_oQ;
            l_oQ.m_bHasCorners = true;
            float l_fMinX = l_arrC[0].x, l_fMinY = l_arrC[0].y, l_fMaxX = l_arrC[0].x, l_fMaxY = l_arrC[0].y;
            for (int l_iI = 0; l_iI < 4; l_iI++) {
                l_oQ.m_arrCorners[l_iI * 2] = l_arrC[l_iI].x;
                l_oQ.m_arrCorners[l_iI * 2 + 1] = l_arrC[l_iI].y;
                l_fMinX = std::min(l_fMinX, l_arrC[l_iI].x); l_fMaxX = std::max(l_fMaxX, l_arrC[l_iI].x);
                l_fMinY = std::min(l_fMinY, l_arrC[l_iI].y); l_fMaxY = std::max(l_fMaxY, l_arrC[l_iI].y);
            }
            l_oQ.m_arrRect[0] = l_fMinX; l_oQ.m_arrRect[1] = l_fMinY; l_oQ.m_arrRect[2] = l_fMaxX - l_fMinX; l_oQ.m_arrRect[3] = l_fMaxY - l_fMinY;
            std::copy(l_pR->m_arrUv, l_pR->m_arrUv + 4, l_oQ.m_arrUv);
            std::copy(m_arrTint, m_arrTint + 4, l_oQ.m_arrTint);
            l_oQ.m_bAdditive = m_bAdditive;
            l_oQ.m_bSolid = m_bSolid;
            const std::vector<uint16_t>& l_vPages = m_spAtlas->f_PageTextures();
            l_oQ.m_u16Texture = l_pR->m_iPage >= 0 && l_pR->m_iPage < (int)l_vPages.size() ? l_vPages[(size_t)l_pR->m_iPage] : g_u16SpriteAtlasTexture;
            m_oQuad = l_oQ;
            m_bHasQuad = true;
        }
    }
    if (m_bHasQuad) l_pRen->f_DrawSprite(m_oQuad);
}

// ---- clip ----------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> ClipObject::f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError) {
    auto l_spO = std::make_shared<ClipObject>();
    auto [l_strFile, l_strClip] = f_SplitRef(f_Str(l_jJ, "anim"));
    if (const std::string l_strC = f_Str(l_jJ, "clip"); !l_strC.empty()) l_strClip = l_strC;
    if (l_strFile.empty()) { l_strError = "no \"anim\" (the .anim file)"; return nullptr; }
    if (l_strClip.empty()) { l_strError = "no \"clip\""; return nullptr; }
    l_spO->m_strAnimPath = l_strFile;
    l_spO->m_strClip = l_strClip;
    l_spO->m_spAnim = l_oCtx.m_oRes.f_Get<res::AnimRes>(l_strFile);
    if (!l_spO->m_spAnim->f_Ok()) { l_strError = "cannot load " + l_spO->m_spAnim->f_Path() + " (" + l_spO->m_spAnim->f_Error() + ")"; return nullptr; }
    if (!l_spO->m_spAnim->f_Find(l_strClip)) { l_strError = l_spO->m_spAnim->f_Path() + " has no clip '" + l_strClip + "'"; return nullptr; }
    if (auto l_oL = l_jJ.find("loop"); l_oL != l_jJ.end() && l_oL->is_boolean()) l_spO->m_bLoop = l_oL->get<bool>();
    if (auto l_oP = l_jJ.find("phase"); l_oP != l_jJ.end() && l_oP->is_number()) l_spO->m_fPhase = l_oP->get<float>();
    l_spO->f_Start();
    return l_spO;
}

void ClipObject::f_Start() {
    m_oPlayer.f_Play(m_spAnim->f_Find(m_strClip));
    m_iSeenVersion = m_spAnim->f_Version();
    m_iSeenContent = m_spAnim->f_ContentVersion();
    m_fCarryMs = 0;
    if (m_fPhase > 0) m_oPlayer.f_Update((int)std::lround(m_fPhase * 1000.0f));
    m_oPlayer.f_TakeEvents();
}

std::shared_ptr<RenderObject> ClipObject::f_Clone() const {
    auto l_spO = std::make_shared<ClipObject>();
    l_spO->m_spAnim = m_spAnim;
    l_spO->m_strClip = m_strClip;
    l_spO->m_strAnimPath = m_strAnimPath;
    l_spO->m_bLoop = m_bLoop;
    l_spO->m_fPhase = m_fPhase;
    f_CopyCommonTo(*l_spO);
    l_spO->f_Start();
    return l_spO;
}

nlohmann::json ClipObject::f_ToJson() const {
    nlohmann::json l_jJ = f_CommonJson();
    l_jJ["anim"] = m_strAnimPath;
    l_jJ["clip"] = m_strClip;
    if (m_bLoop) l_jJ["loop"] = true;
    if (m_fPhase != 0) l_jJ["phase"] = m_fPhase;
    return l_jJ;
}

void ClipObject::f_Update(float l_fDt) {
    if (m_spAnim->f_Version() != m_iSeenVersion) f_Start();   // the file was reloaded: the clip is a new one
    m_fCarryMs += l_fDt * 1000.0f;
    const int l_iMs = (int)m_fCarryMs;
    m_fCarryMs -= (float)l_iMs;
    if (l_iMs > 0) m_oPlayer.f_Update(l_iMs);
    m_oPlayer.f_TakeEvents();
    if (m_bLoop && m_oPlayer.f_Finished()) m_oPlayer.f_Play(m_spAnim->f_Find(m_strClip));
    RenderObject::f_Update(l_fDt);
}

void ClipObject::f_SetTime(float l_fSeconds) {
    if (m_spAnim->f_Version() != m_iSeenVersion) f_Start();
    m_oPlayer.f_Seek(l_fSeconds);
}

float ClipObject::f_Duration() const {
    const anim::Clip* l_pC = m_spAnim ? m_spAnim->f_Find(m_strClip) : nullptr;
    return l_pC ? l_pC->f_Duration() : 0.0f;
}

void ClipObject::f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) {
    if (m_spAnim->f_Version() != m_iSeenVersion) f_Start();
    if (m_spAnim->f_ContentVersion() != m_iSeenContent) {   // an atlas changed under the same set
        m_iSeenContent = m_spAnim->f_ContentVersion();
        m_oPlayer.f_RefreshQuads();
    }
    if (m_oPlayer.f_Showing()) m_oPlayer.f_Draw(l_pRen, m_spAnim->f_Atlases(), l_mat3World, f_IsWhite(m_arrTint) ? nullptr : m_arrTint);
}

// ---- particle ------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> ParticleObject::f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError) {
    auto l_spO = std::make_shared<ParticleObject>();
    auto [l_strFile, l_strEffect] = f_SplitRef(f_Str(l_jJ, "file"));
    if (const std::string l_strE = f_Str(l_jJ, "effect"); !l_strE.empty()) l_strEffect = l_strE;
    if (l_strFile.empty()) { l_strError = "no \"file\" (the .prt file)"; return nullptr; }
    if (l_strEffect.empty()) { l_strError = "no \"effect\""; return nullptr; }
    l_spO->m_strFilePath = l_strFile;
    l_spO->m_strEffect = l_strEffect;
    l_spO->m_spFile = l_oCtx.m_oRes.f_Get<res::ParticleRes>(l_strFile);
    if (!l_spO->m_spFile->f_Ok()) { l_strError = "cannot load " + l_spO->m_spFile->f_Path() + " (" + l_spO->m_spFile->f_Error() + ")"; return nullptr; }
    if (!l_spO->m_spFile->f_Find(l_strEffect)) { l_strError = l_spO->m_spFile->f_Path() + " has no effect '" + l_strEffect + "'"; return nullptr; }
    if (auto l_oL = l_jJ.find("loop"); l_oL != l_jJ.end() && l_oL->is_boolean()) l_spO->m_bLoop = l_oL->get<bool>();
    if (auto l_oS = l_jJ.find("seed"); l_oS != l_jJ.end() && l_oS->is_number_unsigned()) l_spO->m_uSeed = l_oS->get<uint32_t>();
    return l_spO;
}

// Started on the first draw: the effect's prewarm makes world-space particles where it is, so it needs its place.
void ParticleObject::f_Start(IRenderer* l_pRen, const glm::mat3& l_mat3World) {
    const fx::Effect* l_pE = m_spFile->f_Find(m_strEffect);
    m_iSeenVersion = m_spFile->f_Version();
    m_bPlaced = true;
    if (!l_pE) { m_oFx.f_Stop(true); return; }
    m_oFx.f_SetTransform(l_mat3World);
    if (m_iGpuThreshold > 0) m_oFx.f_SetGpuSimulation(l_pRen, m_iGpuThreshold);
    m_oFx.f_Play(l_pE, m_uSeed ? m_uSeed : (l_pE->m_uSeed ? l_pE->m_uSeed : 1u));
}

std::shared_ptr<RenderObject> ParticleObject::f_Clone() const {
    auto l_spO = std::make_shared<ParticleObject>();
    l_spO->m_spFile = m_spFile;
    l_spO->m_strEffect = m_strEffect;
    l_spO->m_strFilePath = m_strFilePath;
    l_spO->m_bLoop = m_bLoop;
    l_spO->m_uSeed = m_uSeed;
    l_spO->m_iGpuThreshold = m_iGpuThreshold;
    f_CopyCommonTo(*l_spO);
    return l_spO;
}

nlohmann::json ParticleObject::f_ToJson() const {
    nlohmann::json l_jJ = f_CommonJson();
    l_jJ["file"] = m_strFilePath;
    l_jJ["effect"] = m_strEffect;
    if (m_bLoop) l_jJ["loop"] = true;
    if (m_uSeed) l_jJ["seed"] = m_uSeed;
    return l_jJ;
}

void ParticleObject::f_Update(float l_fDt) {
    if (m_bPlaced && m_spFile->f_Version() != m_iSeenVersion) m_bPlaced = false;   // reloaded: start again on the next draw
    if (m_bPlaced && m_oFx.f_Effect()) {
        m_oFx.f_Update(l_fDt);
        if (m_bLoop && m_oFx.f_Finished()) m_oFx.f_Play(m_oFx.f_Effect(), m_oFx.f_Seed());
    }
    RenderObject::f_Update(l_fDt);
}

void ParticleObject::f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) {
    // the particles live under the view (on the map), and are drawn through it (the camera)
    const glm::mat3 l_mat3Placed = f_View() == glm::mat3(1.0f) ? l_mat3World : glm::inverse(f_View()) * l_mat3World;
    if (!m_bPlaced) f_Start(l_pRen, l_mat3Placed);
    if (!m_oFx.f_Effect()) return;
    m_oFx.f_SetTransform(l_mat3Placed);
    m_oFx.f_Draw(l_pRen, m_spFile->f_Atlases(), f_View(), f_IsWhite(m_arrTint) ? nullptr : m_arrTint);
}

// ---- text ----------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> TextObject::f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError) {
    auto l_spO = std::make_shared<TextObject>();
    l_spO->m_strFontPath = f_Str(l_jJ, "font");
    if (l_spO->m_strFontPath.empty()) { l_strError = "no \"font\" (the .fnt file)"; return nullptr; }
    l_spO->m_spFont = l_oCtx.m_oRes.f_Get<res::FontRes>(l_spO->m_strFontPath);
    if (!l_spO->m_spFont->f_Ok()) { l_strError = "cannot load " + l_spO->m_spFont->f_Path() + " (" + l_spO->m_spFont->f_Error() + ")"; return nullptr; }
    l_spO->m_strText = f_Str(l_jJ, "text");
    const std::string l_strA = f_Str(l_jJ, "align");
    l_spO->m_eAlign = l_strA == "center" ? ::BitmapFont::Align::Center : l_strA == "right" ? ::BitmapFont::Align::Right : ::BitmapFont::Align::Left;
    if (auto l_oS = l_jJ.find("size"); l_oS != l_jJ.end() && l_oS->is_number()) l_spO->m_fSize = l_oS->get<float>();
    return l_spO;
}

std::shared_ptr<RenderObject> TextObject::f_Clone() const {
    auto l_spO = std::make_shared<TextObject>();
    l_spO->m_spFont = m_spFont;
    l_spO->m_strFontPath = m_strFontPath;
    l_spO->m_strText = m_strText;
    l_spO->m_eAlign = m_eAlign;
    l_spO->m_fSize = m_fSize;
    f_CopyCommonTo(*l_spO);
    return l_spO;
}

nlohmann::json TextObject::f_ToJson() const {
    nlohmann::json l_jJ = f_CommonJson();
    l_jJ["font"] = m_strFontPath;
    l_jJ["text"] = m_strText;
    if (m_eAlign != ::BitmapFont::Align::Left) l_jJ["align"] = f_AlignName(m_eAlign);
    if (m_fSize != 1) l_jJ["size"] = m_fSize;
    return l_jJ;
}

// The anchor is the transform's origin; its x scale sizes the text (fonts are not rotated).
void TextObject::f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) {
    const float l_fScale = m_fSize * glm::length(glm::vec2(l_mat3World[0]));
    m_vQuads.clear();
    m_spFont->f_Font().f_AppendQuads(m_strText, l_mat3World[2].x, l_mat3World[2].y, l_fScale, m_eAlign, m_arrTint, m_spFont->f_Texture(), m_vQuads);
    for (const Quad& l_oQ : m_vQuads) l_pRen->f_DrawSprite(l_oQ);
}

// ---- group / missing -----------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> GroupObject::f_FromJson(const nlohmann::json&, BuildContext&, std::string&) {
    return std::make_shared<GroupObject>();
}

std::shared_ptr<RenderObject> GroupObject::f_Clone() const {
    auto l_spO = std::make_shared<GroupObject>();
    f_CopyCommonTo(*l_spO);
    return l_spO;
}

nlohmann::json GroupObject::f_ToJson() const { return f_CommonJson(); }

std::shared_ptr<RenderObject> MissingObject::f_FromJson(const nlohmann::json& l_jJ, BuildContext&, std::string&) {
    auto l_spO = std::make_shared<MissingObject>();
    l_spO->m_strWhat = f_Str(l_jJ, "what");
    return l_spO;
}

std::shared_ptr<RenderObject> MissingObject::f_Clone() const {
    auto l_spO = std::make_shared<MissingObject>();
    l_spO->m_strWhat = m_strWhat;
    f_CopyCommonTo(*l_spO);
    return l_spO;
}

nlohmann::json MissingObject::f_ToJson() const {
    nlohmann::json l_jJ = f_CommonJson();
    if (!m_strWhat.empty()) l_jJ["what"] = m_strWhat;
    return l_jJ;
}

void MissingObject::f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) {
    Quad l_oQ;
    l_oQ.m_bSolid = true;
    l_oQ.m_bHasCorners = true;
    const glm::vec3 l_arrC[4] = {l_mat3World * glm::vec3(-16, -16, 1), l_mat3World * glm::vec3(16, -16, 1), l_mat3World * glm::vec3(16, 16, 1),
                            l_mat3World * glm::vec3(-16, 16, 1)};
    float l_fMinX = l_arrC[0].x, l_fMinY = l_arrC[0].y, l_fMaxX = l_arrC[0].x, l_fMaxY = l_arrC[0].y;
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        l_oQ.m_arrCorners[l_iI * 2] = l_arrC[l_iI].x;
        l_oQ.m_arrCorners[l_iI * 2 + 1] = l_arrC[l_iI].y;
        l_fMinX = std::min(l_fMinX, l_arrC[l_iI].x); l_fMaxX = std::max(l_fMaxX, l_arrC[l_iI].x);
        l_fMinY = std::min(l_fMinY, l_arrC[l_iI].y); l_fMaxY = std::max(l_fMaxY, l_arrC[l_iI].y);
    }
    l_oQ.m_arrRect[0] = l_fMinX; l_oQ.m_arrRect[1] = l_fMinY; l_oQ.m_arrRect[2] = l_fMaxX - l_fMinX; l_oQ.m_arrRect[3] = l_fMaxY - l_fMinY;
    l_oQ.m_arrUv[0] = 0; l_oQ.m_arrUv[1] = 0; l_oQ.m_arrUv[2] = 1; l_oQ.m_arrUv[3] = 1;
    l_oQ.m_arrTint[0] = 1; l_oQ.m_arrTint[1] = 0.2f; l_oQ.m_arrTint[2] = 0.2f; l_oQ.m_arrTint[3] = 0.85f * m_arrTint[3];
    l_pRen->f_DrawSprite(l_oQ);
}

// ---- the stage editor's look ---------------------------------------------------------------------------------------

nlohmann::json f_LookToRenderJson(const nlohmann::json& l_jLook, const std::string& l_strMediaDir, const std::string& l_strSpriteAtlas) {
    if (!l_jLook.is_object()) return nlohmann::json();
    nlohmann::json l_jJ;
    const std::string l_strDir = l_strMediaDir.empty() || l_strMediaDir.back() == '/' ? l_strMediaDir : l_strMediaDir + "/";
    if (const auto [l_strFile, l_strClip] = f_SplitRef(f_Str(l_jLook, "anim")); !l_strFile.empty() && !l_strClip.empty()) {
        l_jJ = {{"type", "clip"}, {"anim", res::ResourceManager::f_Join(l_strDir, l_strFile)}, {"clip", l_strClip}, {"loop", true}};
    } else if (std::string l_strSprite = f_Str(l_jLook, "sprite"); !l_strSprite.empty()) {
        if (l_strSprite.size() > 4 && l_strSprite.compare(l_strSprite.size() - 4, 4, ".png") == 0) l_strSprite.resize(l_strSprite.size() - 4);
        l_jJ = {{"type", "sprite"}, {"atlas", l_strSpriteAtlas}, {"name", l_strSprite}};
    } else {
        return nlohmann::json();
    }
    if (auto l_oT = l_jLook.find("tint"); l_oT != l_jLook.end() && l_oT->is_array() && l_oT->size() >= 3) {
        nlohmann::json l_jTint = *l_oT;
        if (l_jTint.size() == 3) l_jTint.push_back(1.0);
        l_jJ["tint"] = l_jTint;
    }
    return l_jJ;
}

}  // namespace toms

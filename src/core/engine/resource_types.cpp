// resource_types.cpp -- see resource_types.h.
#include "resource_types.h"

#include "render_iface.h"
#include "vfs.h"

#include <stb_image.h>

#include <cstdio>

namespace toms::res {

// ---- TextureRes --------------------------------------------------------------------------------------------------

void TextureRes::f_ReleaseGpu() {
    if (m_spLink && m_spLink->m_pRen && m_u16Tex != g_u16SpriteAtlasTexture) m_spLink->m_pRen->f_ReleaseTexture(m_u16Tex);
    m_u16Tex = g_u16SpriteAtlasTexture;
}

bool TextureRes::f_Load(ResourceManager& l_oRm, std::string& l_strError) {
    std::string l_strPng;
    if (!toms::f_VfsReadAll(f_Path(), l_strPng)) { l_strError = "cannot read"; return false; }
    int l_iW = 0, l_iH = 0, l_iCh = 0;
    stbi_uc* l_pPx = stbi_load_from_memory((const stbi_uc*)l_strPng.data(), (int)l_strPng.size(), &l_iW, &l_iH, &l_iCh, 4);
    if (!l_pPx) { l_strError = "cannot decode the image"; return false; }
    uint16_t l_u16Tex = g_u16SpriteAtlasTexture;
    if (IRenderer* l_pRen = l_oRm.f_Renderer()) l_u16Tex = l_pRen->f_LoadTexture(std::vector<uint8_t>(l_pPx, l_pPx + (size_t)l_iW * l_iH * 4), (uint32_t)l_iW, (uint32_t)l_iH);
    stbi_image_free(l_pPx);
    f_ReleaseGpu();   // a reload: the old texture goes once the new one is up
    m_spLink = l_oRm.f_GpuLink();
    m_u16Tex = l_u16Tex;
    m_iW = l_iW;
    m_iH = l_iH;
    m_nBytes = (size_t)l_iW * l_iH * 4;
    return true;
}

// ---- AtlasRes ----------------------------------------------------------------------------------------------------

bool f_ReadMeshFile(const std::string& l_strPath, toms::SpriteMeshFile& l_oOut, std::string& l_strError) {
    l_oOut = toms::SpriteMeshFile();
    std::string l_strText;
    if (!toms::f_VfsExists(l_strPath) || !toms::f_VfsReadAll(l_strPath, l_strText)) return true;
    const std::string l_strDir = ResourceManager::f_DirOf(l_strPath);
    auto l_fnReadBuffer = [&](const std::string& l_strUri, std::string& l_strBytes) { return toms::f_VfsReadAll(ResourceManager::f_Join(l_strDir, l_strUri), l_strBytes); };
    if (toms::f_ParseSpriteMeshes(l_strText, l_fnReadBuffer, l_oOut, &l_strError)) return true;
    l_strError = l_strPath + ": " + l_strError;
    return false;
}

int AtlasRes::f_ContentVersion() const {
    int l_iV = f_Version();
    for (const auto& l_spP : m_vPages) l_iV += l_spP->f_Version();
    return l_iV;
}

bool AtlasRes::f_Load(ResourceManager& l_oRm, std::string& l_strError) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(f_Path(), l_strText)) { l_strError = "cannot read"; return false; }
    toms::AtlasFile l_oAtlas;
    if (!toms::f_ParseAtlas(l_strText, l_oAtlas, &l_strError)) return false;
    const std::string l_strDir = ResourceManager::f_DirOf(f_Path());
    std::vector<std::shared_ptr<TextureRes>> l_vPages;
    std::vector<uint16_t> l_vPageTex;
    for (toms::AtlasPage& l_oPg : l_oAtlas.m_vPages) {
        std::shared_ptr<TextureRes> l_spT = l_oRm.f_Get<TextureRes>(l_strDir + l_oPg.m_strFile);
        if (l_spT->f_Ok()) { l_oPg.m_iW = l_spT->f_Width(); l_oPg.m_iH = l_spT->f_Height(); }   // the image wins; UVs follow it
        if (IRenderer* l_pRen = l_oRm.f_Renderer(); l_pRen && l_oPg.m_strFilter.rfind("Linear", 0) == 0) l_pRen->f_SetTextureFilter(l_spT->f_Texture(), true);
        l_vPageTex.push_back(l_spT->f_Texture());
        l_vPages.push_back(std::move(l_spT));
    }
    l_oAtlas.f_ComputeUVs();
    toms::SpriteMeshFile l_oMeshes;   // its meshes (docs/engine/16_MESH_ANIMATION.md): skinned and deformed nodes bend them
    std::string l_strMeshError;
    if (!f_ReadMeshFile(toms::f_SpriteMeshPath(f_Path()), l_oMeshes, l_strMeshError)) std::fprintf(stderr, "[res] %s\n", l_strMeshError.c_str());
    m_oMeshes = std::move(l_oMeshes);
    m_oAtlas = std::move(l_oAtlas);
    m_vPages = std::move(l_vPages);
    m_vPageTex = std::move(l_vPageTex);
    m_nBytes = m_oAtlas.m_vRegions.size() * sizeof(toms::AtlasRegion);
    return true;
}

// ---- AtlasList ---------------------------------------------------------------------------------------------------

void AtlasList::f_Resolve(ResourceManager& l_oRm, const std::string& l_strFile, const std::vector<toms::anim::AtlasRef>& l_vRefs) {
    std::vector<Item> l_vItems;
    const std::string l_strDir = ResourceManager::f_DirOf(l_strFile);
    for (const toms::anim::AtlasRef& l_oRef : l_vRefs) {
        std::shared_ptr<AtlasRes> l_spA = l_oRm.f_Get<AtlasRes>(ResourceManager::f_Join(l_strDir, l_oRef.m_strPath));
        if (l_spA->f_Ok()) l_vItems.push_back({l_oRef.m_strId, std::move(l_spA)});
    }
    if (l_vRefs.empty())
        if (std::shared_ptr<AtlasRes> l_spGame = l_oRm.f_Find<AtlasRes>(ResourceManager::m_pszSpriteAtlas)) l_vItems.push_back({std::string(), std::move(l_spGame)});
    m_vItems = std::move(l_vItems);
    m_iBuiltAt = -1;
}

int AtlasList::f_ContentVersion() const {
    int l_iV = 0;
    for (const Item& l_oI : m_vItems) l_iV += l_oI.m_spAtlas->f_ContentVersion() + 1;   // +1: a list of atlases at version 0 is not "unbuilt"
    return l_iV;
}

const toms::anim::AtlasSet& AtlasList::f_Set() const {
    const int l_iV = f_ContentVersion();
    if (l_iV != m_iBuiltAt) {
        m_oSet = toms::anim::AtlasSet();
        for (const Item& l_oI : m_vItems) {
            m_oSet.f_Add(l_oI.m_spAtlas->f_Atlas(), l_oI.m_strId, l_oI.m_spAtlas->f_PageTextures());
            if (!l_oI.m_spAtlas->f_Meshes().m_mapMeshes.empty()) m_oSet.m_vEntries.back().m_pMeshes = &l_oI.m_spAtlas->f_Meshes();
        }
        m_iBuiltAt = l_iV;
    }
    return m_oSet;
}

// ---- AnimRes / ParticleRes ---------------------------------------------------------------------------------------

bool AnimRes::f_Load(ResourceManager& l_oRm, std::string& l_strError) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(f_Path(), l_strText)) { l_strError = "cannot read"; return false; }
    toms::anim::AnimFile l_oF;
    if (!toms::anim::f_ParseAnim(l_strText, l_oF, &l_strError)) return false;
    m_oFile = std::move(l_oF);
    m_oAtlases.f_Resolve(l_oRm, f_Path(), m_oFile.m_vAtlases);
    // A skinned sprite without a mesh still draws, as a flat quad: say so (docs/engine/16_MESH_ANIMATION.md).
    for (const toms::anim::Clip& l_oC : m_oFile.m_vClips)
        for (const toms::anim::MissingMesh& l_oM : toms::anim::f_SkinnedWithoutMesh(l_oC, m_oAtlases.f_Set()))
            std::fprintf(stderr, "[res] %s: clip '%s', node %s: the skinned sprite '%s' has no mesh, so it is drawn unbent "
                                 "(open and save the clip in the anim editor, or anim_editor --headless meshes)\n",
                         f_Path().c_str(), l_oC.m_strName.c_str(), l_oM.m_strNode.c_str(), l_oM.m_strSprite.c_str());
    m_nBytes = l_strText.size();   // about what the parsed clips take
    return true;
}

bool ParticleRes::f_Load(ResourceManager& l_oRm, std::string& l_strError) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(f_Path(), l_strText)) { l_strError = "cannot read"; return false; }
    toms::fx::ParticleFile l_oF;
    if (!toms::fx::f_ParseParticles(l_strText, l_oF, &l_strError)) return false;
    AtlasList l_oAtlases;
    l_oAtlases.f_Resolve(l_oRm, f_Path(), l_oF.m_vAtlases);
    m_oFile = std::move(l_oF);
    m_oAtlases = std::move(l_oAtlases);
    m_nBytes = l_strText.size();
    return true;
}

// ---- FontRes / JsonRes -------------------------------------------------------------------------------------------

bool FontRes::f_Load(ResourceManager& l_oRm, std::string& l_strError) {
    std::string l_strText;
    if (!toms::f_VfsReadAll(f_Path(), l_strText)) { l_strError = "cannot read"; return false; }
    ::BitmapFont l_oFont;
    if (!l_oFont.f_Parse(l_strText, &l_strError)) return false;
    if (l_oFont.m_vPages.empty() || l_oFont.m_vPages[0].empty()) { l_strError = "no page file"; return false; }
    std::shared_ptr<TextureRes> l_spPage = l_oRm.f_Get<TextureRes>(ResourceManager::f_DirOf(f_Path()) + l_oFont.m_vPages[0]);
    if (!l_spPage->f_Ok()) { l_strError = "cannot load its page " + l_spPage->f_Path(); return false; }
    if (l_oFont.m_iScaleW <= 0 || l_oFont.m_iScaleH <= 0) { l_oFont.m_iScaleW = l_spPage->f_Width(); l_oFont.m_iScaleH = l_spPage->f_Height(); }
    m_oFont = std::move(l_oFont);
    m_spPage = std::move(l_spPage);
    m_nBytes = m_oFont.m_vGlyphs.size() * sizeof(BitmapGlyph);
    return true;
}

bool JsonRes::f_Load(ResourceManager& l_oRm, std::string& l_strError) {
    (void)l_oRm;
    std::string l_strText;
    if (!toms::f_VfsReadAll(f_Path(), l_strText)) { l_strError = "cannot read"; return false; }
    nlohmann::json l_jJ = nlohmann::json::parse(l_strText, nullptr, false);
    if (l_jJ.is_discarded()) { l_strError = "not valid JSON"; return false; }
    m_jJson = std::move(l_jJ);
    m_nBytes = l_strText.size();
    return true;
}

}  // namespace toms::res

// ---- ResourceManager::preload (here: it needs the types) ---------------------------------------------------------

namespace toms::res {

int ResourceManager::f_Preload() {
    int l_iFailed = 0;
    for (const std::string& l_strK : f_Keys()) {
        const ManifestEntry& l_oE = m_mapManifest.at(l_strK);
        if (!l_oE.m_bPreload) continue;
        bool l_bOk = false;
        if (l_oE.m_strType == "texture") l_bOk = f_Get<TextureRes>(l_strK)->f_Ok();
        else if (l_oE.m_strType == "atlas") l_bOk = f_Get<AtlasRes>(l_strK)->f_Ok();
        else if (l_oE.m_strType == "anim") l_bOk = f_Get<AnimRes>(l_strK)->f_Ok();
        else if (l_oE.m_strType == "particle") l_bOk = f_Get<ParticleRes>(l_strK)->f_Ok();
        else if (l_oE.m_strType == "font") l_bOk = f_Get<FontRes>(l_strK)->f_Ok();
        else if (l_oE.m_strType == "json") l_bOk = f_Get<JsonRes>(l_strK)->f_Ok();
        else if (m_fnOnMissing) m_fnOnMissing(l_strK, f_Resolve(l_strK), "unknown type '" + l_oE.m_strType + "'");
        if (!l_bOk) l_iFailed++;
    }
    return l_iFailed;
}

}  // namespace toms::res

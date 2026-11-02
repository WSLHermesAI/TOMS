// resource_types.h -- the resources the engine loads (resource.h, docs/engine/12_RESOURCE_MANAGER.md):
//
//   TextureRes    a PNG on the GPU (IRenderer::loadTexture); freed with the object
//   AtlasRes      a .atlas file and its page textures (TextureRes, shared with every other atlas using the same PNG)
//   AnimRes       a .anim file and the atlases its sprites come from
//   ParticleRes   a .prt particle file and its atlases
//   FontRes       a picture font (.fnt) and its page texture
//   JsonRes       a parsed data file
//
// Each is a thin owner around a type the engine already has (AtlasFile, AnimFile, ParticleFile, BitmapFont): code that
// draws keeps using those, through the resource's accessors.
#pragma once
#include "resource.h"

#include "anim_player.h"
#include "atlas_file.h"
#include "bitmap_font.h"
#include "particle_fx.h"

#include <json.hpp>

namespace toms::res {

class TextureRes : public Resource {
    TOMS_OBJECT(TextureRes)
public:
    ~TextureRes() override { f_ReleaseGpu(); }
    // The Quad::texture to draw with: kSpriteAtlasTexture when nothing was uploaded (no renderer, or a failed load).
    uint16_t f_Texture() const { return m_u16Tex; }
    int f_Width() const { return m_iW; }
    int f_Height() const { return m_iH; }
    void f_ReleaseGpu();

protected:
    bool f_Load(ResourceManager& l_oRm, std::string& l_strError) override;

private:
    std::shared_ptr<GpuLink> m_spLink;
    uint16_t m_u16Tex = g_u16SpriteAtlasTexture;
    int m_iW = 0, m_iH = 0;
};

// A mesh file (`meshPath` = spriteMeshPath(<atlas>), and the .mesh.bin it may name), through the vfs. No file: true and
// `out` empty. A broken file: false and `error`.
bool f_ReadMeshFile(const std::string& l_strMeshPath, toms::SpriteMeshFile& l_oOut, std::string& l_strError);

class AtlasRes : public Resource {
    TOMS_OBJECT(AtlasRes)
public:
    const toms::AtlasFile& f_Atlas() const { return m_oAtlas; }
    const toms::AtlasRegion* f_Find(const std::string& l_strName) const { return m_oAtlas.f_Find(l_strName); }
    // Per page, the texture it is drawn with; empty = every page is the renderer's sprite atlas (the game's own).
    const std::vector<uint16_t>& f_PageTextures() const { return m_vPageTex; }
    int f_ContentVersion() const override;   // its own version + its pages'
    // Its sprites' meshes (<atlas>.mesh.json beside it, docs/engine/16_MESH_ANIMATION.md); empty when it has none.
    const toms::SpriteMeshFile& f_Meshes() const { return m_oMeshes; }
    // For provide(): the game's sprite atlas, drawn with the renderer's sprite texture.
    void f_SetProvided(const toms::AtlasFile& l_oAtlas, const toms::SpriteMeshFile& l_oMeshes = {}) {
        m_oAtlas = l_oAtlas; m_oMeshes = l_oMeshes; m_vPages.clear(); m_vPageTex.clear();
    }

protected:
    bool f_Load(ResourceManager& l_oRm, std::string& l_strError) override;

private:
    toms::AtlasFile m_oAtlas;
    toms::SpriteMeshFile m_oMeshes;
    std::vector<std::shared_ptr<TextureRes>> m_vPages;
    std::vector<uint16_t> m_vPageTex;
};

// The atlases a clip or particle file draws from, in lookup order, as an anim::AtlasSet. The set is rebuilt when an
// atlas's contents change (a reload), so a reference to it stays valid; compare contentVersion() to know.
class AtlasList {
public:
    struct Item {
        std::string m_strId;
        std::shared_ptr<AtlasRes> m_spAtlas;
    };
    // `file` is the .anim / .prt the refs are relative to. No refs: the game's sprite atlas, when it was provided.
    // Atlases that fail are left out (the manager has reported them).
    void f_Resolve(ResourceManager& l_oRm, const std::string& l_strFile, const std::vector<toms::anim::AtlasRef>& l_vRefs);
    const toms::anim::AtlasSet& f_Set() const;
    int f_ContentVersion() const;
    const std::vector<Item>& f_Items() const { return m_vItems; }

private:
    std::vector<Item> m_vItems;
    mutable toms::anim::AtlasSet m_oSet;
    mutable int m_iBuiltAt = -1;
};

class AnimRes : public Resource {
    TOMS_OBJECT(AnimRes)
public:
    const toms::anim::AnimFile& f_File() const { return m_oFile; }
    const toms::anim::Clip* f_Find(const std::string& l_strClip) const { return m_oFile.f_Find(l_strClip); }
    const toms::anim::AtlasSet& f_Atlases() const { return m_oAtlases.f_Set(); }
    int f_ContentVersion() const override { return f_Version() + m_oAtlases.f_ContentVersion(); }

protected:
    bool f_Load(ResourceManager& l_oRm, std::string& l_strError) override;

private:
    toms::anim::AnimFile m_oFile;
    AtlasList m_oAtlases;
};

class ParticleRes : public Resource {
    TOMS_OBJECT(ParticleRes)
public:
    const toms::fx::ParticleFile& f_File() const { return m_oFile; }
    const toms::fx::Effect* f_Find(const std::string& l_strEffect) const { return m_oFile.f_Find(l_strEffect); }
    const toms::anim::AtlasSet& f_Atlases() const { return m_oAtlases.f_Set(); }
    int f_ContentVersion() const override { return f_Version() + m_oAtlases.f_ContentVersion(); }

protected:
    bool f_Load(ResourceManager& l_oRm, std::string& l_strError) override;

private:
    toms::fx::ParticleFile m_oFile;
    AtlasList m_oAtlases;
};

class FontRes : public Resource {
    TOMS_OBJECT(FontRes)
public:
    const ::BitmapFont& f_Font() const { return m_oFont; }
    uint16_t f_Texture() const { return m_spPage ? m_spPage->f_Texture() : g_u16SpriteAtlasTexture; }
    int f_ContentVersion() const override { return f_Version() + (m_spPage ? m_spPage->f_Version() : 0); }

protected:
    bool f_Load(ResourceManager& l_oRm, std::string& l_strError) override;

private:
    ::BitmapFont m_oFont;
    std::shared_ptr<TextureRes> m_spPage;
};

class JsonRes : public Resource {
    TOMS_OBJECT(JsonRes)
public:
    const nlohmann::json& f_Json() const { return m_jJson; }

protected:
    bool f_Load(ResourceManager& l_oRm, std::string& l_strError) override;

private:
    nlohmann::json m_jJson;
};

}  // namespace toms::res

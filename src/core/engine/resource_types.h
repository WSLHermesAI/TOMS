// resource_types.h -- the resources the engine loads (resource.h, docs/25_RESOURCE_MANAGER.md):
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
    ~TextureRes() override { releaseGpu(); }
    // The Quad::texture to draw with: kSpriteAtlasTexture when nothing was uploaded (no renderer, or a failed load).
    uint16_t texture() const { return tex_; }
    int width() const { return w_; }
    int height() const { return h_; }
    void releaseGpu();

protected:
    bool load(ResourceManager& rm, std::string& error) override;

private:
    std::shared_ptr<GpuLink> link_;
    uint16_t tex_ = kSpriteAtlasTexture;
    int w_ = 0, h_ = 0;
};

// The meshes beside an atlas (<atlas>.mesh.json and the .mesh.bin it may name), through the vfs. No file: true and
// `out` empty. A broken file: false and `error`.
bool readSpriteMeshes(const std::string& atlasPath, toms::SpriteMeshFile& out, std::string& error);

class AtlasRes : public Resource {
    TOMS_OBJECT(AtlasRes)
public:
    const toms::AtlasFile& atlas() const { return atlas_; }
    const toms::AtlasRegion* find(const std::string& name) const { return atlas_.find(name); }
    // Per page, the texture it is drawn with; empty = every page is the renderer's sprite atlas (the game's own).
    const std::vector<uint16_t>& pageTextures() const { return pageTex_; }
    int contentVersion() const override;   // its own version + its pages'
    // Its sprites' meshes (<atlas>.mesh.json beside it, docs/34_MESH_ANIMATION.md); empty when it has none.
    const toms::SpriteMeshFile& meshes() const { return meshes_; }
    // For provide(): the game's sprite atlas, drawn with the renderer's sprite texture.
    void setProvided(const toms::AtlasFile& atlas, const toms::SpriteMeshFile& meshes = {}) {
        atlas_ = atlas; meshes_ = meshes; pages_.clear(); pageTex_.clear();
    }

protected:
    bool load(ResourceManager& rm, std::string& error) override;

private:
    toms::AtlasFile atlas_;
    toms::SpriteMeshFile meshes_;
    std::vector<std::shared_ptr<TextureRes>> pages_;
    std::vector<uint16_t> pageTex_;
};

// The atlases a clip or particle file draws from, in lookup order, as an anim::AtlasSet. The set is rebuilt when an
// atlas's contents change (a reload), so a reference to it stays valid; compare contentVersion() to know.
class AtlasList {
public:
    struct Item {
        std::string id;
        std::shared_ptr<AtlasRes> atlas;
    };
    // `file` is the .anim / .prt the refs are relative to. No refs: the game's sprite atlas, when it was provided.
    // Atlases that fail are left out (the manager has reported them).
    void resolve(ResourceManager& rm, const std::string& file, const std::vector<toms::anim::AtlasRef>& refs);
    const toms::anim::AtlasSet& set() const;
    int contentVersion() const;
    const std::vector<Item>& items() const { return items_; }

private:
    std::vector<Item> items_;
    mutable toms::anim::AtlasSet set_;
    mutable int builtAt_ = -1;
};

class AnimRes : public Resource {
    TOMS_OBJECT(AnimRes)
public:
    const toms::anim::AnimFile& file() const { return file_; }
    const toms::anim::Clip* find(const std::string& clip) const { return file_.find(clip); }
    const toms::anim::AtlasSet& atlases() const { return atlases_.set(); }
    int contentVersion() const override { return version() + atlases_.contentVersion(); }

protected:
    bool load(ResourceManager& rm, std::string& error) override;

private:
    toms::anim::AnimFile file_;
    AtlasList atlases_;
};

class ParticleRes : public Resource {
    TOMS_OBJECT(ParticleRes)
public:
    const toms::fx::ParticleFile& file() const { return file_; }
    const toms::fx::Effect* find(const std::string& effect) const { return file_.find(effect); }
    const toms::anim::AtlasSet& atlases() const { return atlases_.set(); }
    int contentVersion() const override { return version() + atlases_.contentVersion(); }

protected:
    bool load(ResourceManager& rm, std::string& error) override;

private:
    toms::fx::ParticleFile file_;
    AtlasList atlases_;
};

class FontRes : public Resource {
    TOMS_OBJECT(FontRes)
public:
    const ::BitmapFont& font() const { return font_; }
    uint16_t texture() const { return page_ ? page_->texture() : kSpriteAtlasTexture; }
    int contentVersion() const override { return version() + (page_ ? page_->version() : 0); }

protected:
    bool load(ResourceManager& rm, std::string& error) override;

private:
    ::BitmapFont font_;
    std::shared_ptr<TextureRes> page_;
};

class JsonRes : public Resource {
    TOMS_OBJECT(JsonRes)
public:
    const nlohmann::json& json() const { return json_; }

protected:
    bool load(ResourceManager& rm, std::string& error) override;

private:
    nlohmann::json json_;
};

}  // namespace toms::res

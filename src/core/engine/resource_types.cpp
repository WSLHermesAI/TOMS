// resource_types.cpp -- see resource_types.h.
#include "resource_types.h"

#include "render_iface.h"
#include "vfs.h"

#include <stb_image.h>

#include <cstdio>

namespace toms::res {

// ---- TextureRes --------------------------------------------------------------------------------------------------

void TextureRes::releaseGpu() {
    if (link_ && link_->ren && tex_ != kSpriteAtlasTexture) link_->ren->releaseTexture(tex_);
    tex_ = kSpriteAtlasTexture;
}

bool TextureRes::load(ResourceManager& rm, std::string& error) {
    std::string png;
    if (!toms::vfsReadAll(path(), png)) { error = "cannot read"; return false; }
    int w = 0, h = 0, ch = 0;
    stbi_uc* px = stbi_load_from_memory((const stbi_uc*)png.data(), (int)png.size(), &w, &h, &ch, 4);
    if (!px) { error = "cannot decode the image"; return false; }
    uint16_t tex = kSpriteAtlasTexture;
    if (IRenderer* ren = rm.renderer()) tex = ren->loadTexture(std::vector<uint8_t>(px, px + (size_t)w * h * 4), (uint32_t)w, (uint32_t)h);
    stbi_image_free(px);
    releaseGpu();   // a reload: the old texture goes once the new one is up
    link_ = rm.gpuLink();
    tex_ = tex;
    w_ = w;
    h_ = h;
    bytes_ = (size_t)w * h * 4;
    return true;
}

// ---- AtlasRes ----------------------------------------------------------------------------------------------------

bool readSpriteMeshes(const std::string& atlasPath, toms::SpriteMeshFile& out, std::string& error) {
    out = toms::SpriteMeshFile();
    const std::string path = toms::spriteMeshPath(atlasPath);
    std::string text;
    if (!toms::vfsExists(path) || !toms::vfsReadAll(path, text)) return true;
    const std::string dir = ResourceManager::dirOf(path);
    auto readBuffer = [&](const std::string& uri, std::string& bytes) { return toms::vfsReadAll(ResourceManager::join(dir, uri), bytes); };
    if (toms::parseSpriteMeshes(text, readBuffer, out, &error)) return true;
    error = path + ": " + error;
    return false;
}

int AtlasRes::contentVersion() const {
    int v = version();
    for (const auto& p : pages_) v += p->version();
    return v;
}

bool AtlasRes::load(ResourceManager& rm, std::string& error) {
    std::string text;
    if (!toms::vfsReadAll(path(), text)) { error = "cannot read"; return false; }
    toms::AtlasFile atlas;
    if (!toms::parseAtlas(text, atlas, &error)) return false;
    const std::string dir = ResourceManager::dirOf(path());
    std::vector<std::shared_ptr<TextureRes>> pages;
    std::vector<uint16_t> pageTex;
    for (toms::AtlasPage& pg : atlas.pages) {
        std::shared_ptr<TextureRes> t = rm.get<TextureRes>(dir + pg.file);
        if (t->ok()) { pg.w = t->width(); pg.h = t->height(); }   // the image wins; UVs follow it
        pageTex.push_back(t->texture());
        pages.push_back(std::move(t));
    }
    atlas.computeUVs();
    toms::SpriteMeshFile meshes;
    std::string meshError;
    if (!readSpriteMeshes(path(), meshes, meshError)) std::fprintf(stderr, "[res] %s\n", meshError.c_str());   // drawn as quads
    meshes_ = std::move(meshes);
    atlas_ = std::move(atlas);
    pages_ = std::move(pages);
    pageTex_ = std::move(pageTex);
    bytes_ = atlas_.regions.size() * sizeof(toms::AtlasRegion);
    return true;
}

// ---- AtlasList ---------------------------------------------------------------------------------------------------

void AtlasList::resolve(ResourceManager& rm, const std::string& file, const std::vector<toms::anim::AtlasRef>& refs) {
    std::vector<Item> items;
    const std::string dir = ResourceManager::dirOf(file);
    for (const toms::anim::AtlasRef& ref : refs) {
        std::shared_ptr<AtlasRes> a = rm.get<AtlasRes>(ResourceManager::join(dir, ref.path));
        if (a->ok()) items.push_back({ref.id, std::move(a)});
    }
    if (refs.empty())
        if (std::shared_ptr<AtlasRes> game = rm.find<AtlasRes>(ResourceManager::kSpriteAtlas)) items.push_back({std::string(), std::move(game)});
    items_ = std::move(items);
    builtAt_ = -1;
}

int AtlasList::contentVersion() const {
    int v = 0;
    for (const Item& i : items_) v += i.atlas->contentVersion() + 1;   // +1: a list of atlases at version 0 is not "unbuilt"
    return v;
}

const toms::anim::AtlasSet& AtlasList::set() const {
    const int v = contentVersion();
    if (v != builtAt_) {
        set_ = toms::anim::AtlasSet();
        for (const Item& i : items_) {
            set_.add(i.atlas->atlas(), i.id, i.atlas->pageTextures());
            if (!i.atlas->meshes().meshes.empty()) set_.entries.back().meshes = &i.atlas->meshes();
        }
        builtAt_ = v;
    }
    return set_;
}

// ---- AnimRes / ParticleRes ---------------------------------------------------------------------------------------

bool AnimRes::load(ResourceManager& rm, std::string& error) {
    std::string text;
    if (!toms::vfsReadAll(path(), text)) { error = "cannot read"; return false; }
    toms::anim::AnimFile f;
    if (!toms::anim::parseAnim(text, f, &error)) return false;
    AtlasList atlases;
    atlases.resolve(rm, path(), f.atlases);
    file_ = std::move(f);
    atlases_ = std::move(atlases);
    bytes_ = text.size();   // about what the parsed clips take
    return true;
}

bool ParticleRes::load(ResourceManager& rm, std::string& error) {
    std::string text;
    if (!toms::vfsReadAll(path(), text)) { error = "cannot read"; return false; }
    toms::fx::ParticleFile f;
    if (!toms::fx::parseParticles(text, f, &error)) return false;
    AtlasList atlases;
    atlases.resolve(rm, path(), f.atlases);
    file_ = std::move(f);
    atlases_ = std::move(atlases);
    bytes_ = text.size();
    return true;
}

// ---- FontRes / JsonRes -------------------------------------------------------------------------------------------

bool FontRes::load(ResourceManager& rm, std::string& error) {
    std::string text;
    if (!toms::vfsReadAll(path(), text)) { error = "cannot read"; return false; }
    ::BitmapFont font;
    if (!font.parse(text, &error)) return false;
    if (font.pages.empty() || font.pages[0].empty()) { error = "no page file"; return false; }
    std::shared_ptr<TextureRes> page = rm.get<TextureRes>(ResourceManager::dirOf(path()) + font.pages[0]);
    if (!page->ok()) { error = "cannot load its page " + page->path(); return false; }
    if (font.scaleW <= 0 || font.scaleH <= 0) { font.scaleW = page->width(); font.scaleH = page->height(); }
    font_ = std::move(font);
    page_ = std::move(page);
    bytes_ = font_.glyphs.size() * sizeof(BitmapGlyph);
    return true;
}

bool JsonRes::load(ResourceManager& rm, std::string& error) {
    (void)rm;
    std::string text;
    if (!toms::vfsReadAll(path(), text)) { error = "cannot read"; return false; }
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) { error = "not valid JSON"; return false; }
    json_ = std::move(j);
    bytes_ = text.size();
    return true;
}

}  // namespace toms::res

// ---- ResourceManager::preload (here: it needs the types) ---------------------------------------------------------

namespace toms::res {

int ResourceManager::preload() {
    int failed = 0;
    for (const std::string& k : keys()) {
        const ManifestEntry& e = manifest_.at(k);
        if (!e.preload) continue;
        bool ok = false;
        if (e.type == "texture") ok = get<TextureRes>(k)->ok();
        else if (e.type == "atlas") ok = get<AtlasRes>(k)->ok();
        else if (e.type == "anim") ok = get<AnimRes>(k)->ok();
        else if (e.type == "particle") ok = get<ParticleRes>(k)->ok();
        else if (e.type == "font") ok = get<FontRes>(k)->ok();
        else if (e.type == "json") ok = get<JsonRes>(k)->ok();
        else if (onMissing_) onMissing_(k, resolve(k), "unknown type '" + e.type + "'");
        if (!ok) failed++;
    }
    return failed;
}

}  // namespace toms::res

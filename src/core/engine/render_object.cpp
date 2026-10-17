// render_object.cpp -- see render_object.h.
#include "render_object.h"

#include "anim_clip.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {
bool isWhite(const float t[4]) { return t[0] == 1 && t[1] == 1 && t[2] == 1 && t[3] == 1; }

// "file#name" -> (file, name); no '#': (s, "")
std::pair<std::string, std::string> splitRef(const std::string& s) {
    const size_t h = s.find('#');
    return h == std::string::npos ? std::make_pair(s, std::string()) : std::make_pair(s.substr(0, h), s.substr(h + 1));
}

std::string str(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool readNumbers(const nlohmann::json& j, const char* key, float* out, size_t n) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < n) return false;
    for (size_t i = 0; i < n; i++) {
        if (!(*it)[i].is_number()) return false;
        out[i] = (*it)[i].get<float>();
    }
    return true;
}

nlohmann::json numbers(const float* v, size_t n) {
    nlohmann::json a = nlohmann::json::array();
    for (size_t i = 0; i < n; i++) a.push_back(v[i]);
    return a;
}

const char* alignName(::BitmapFont::Align a) {
    return a == ::BitmapFont::Align::Center ? "center" : a == ::BitmapFont::Align::Right ? "right" : "left";
}
}  // namespace

// ---- RenderObject ------------------------------------------------------------------------------------------------

void RenderObject::applyTransform() {
    SetLocalTransform(anim::flatten(anim::compose({pos_.x, pos_.y, 0}, {0, 0, rotation_}, {scale_.x, scale_.y, 1})));
}

void RenderObject::addChild(std::shared_ptr<RenderObject> child) {
    if (!child) return;
    AddChildToLast(child.get(), false);
    owned_.push_back(std::move(child));
}

void RenderObject::update(float dt) {
    for (const auto& c : owned_) c->update(dt);
}

void RenderObject::Render(IRenderer* ren) {
    if (ren) draw(ren, GetWorldTransform());
}

void RenderObject::copyCommonTo(RenderObject& o) const {
    o.SetName(Name());
    std::copy(tint, tint + 4, o.tint);
    o.pos_ = pos_;
    o.scale_ = scale_;
    o.rotation_ = rotation_;
    o.applyTransform();
    o.SetVisible(IsVisible());
    for (const auto& c : owned_) o.addChild(c->clone());
}

nlohmann::json RenderObject::commonJson() const {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = typeName();
    if (!Name().empty()) j["name"] = Name();
    if (pos_ != glm::vec2(0.0f)) j["pos"] = {pos_.x, pos_.y};
    if (scale_ != glm::vec2(1.0f)) {
        if (scale_.x == scale_.y) j["scale"] = scale_.x;
        else j["scale"] = {scale_.x, scale_.y};
    }
    if (rotation_ != 0) j["rotation"] = rotation_;
    if (!isWhite(tint)) j["tint"] = numbers(tint, 4);
    if (!IsVisible()) j["visible"] = false;
    if (!owned_.empty()) {
        nlohmann::json kids = nlohmann::json::array();
        for (const auto& c : owned_) kids.push_back(c->toJson());
        j["children"] = std::move(kids);
    }
    return j;
}

// ---- the factory ---------------------------------------------------------------------------------------------------

void RenderObjectFactory::add(const std::string& type, MakeFn make) { makers_[type] = make; }

std::vector<std::string> RenderObjectFactory::types() const {
    std::vector<std::string> out;
    for (const auto& [t, f] : makers_) out.push_back(t);
    return out;
}

std::shared_ptr<RenderObject> RenderObjectFactory::build(const nlohmann::json& j, BuildContext& ctx, std::string& error) const {
    auto fail = [&](const std::string& why) {
        error = (ctx.where.empty() ? std::string() : ctx.where + ": ") + why;
        return std::shared_ptr<RenderObject>();
    };
    if (!j.is_object()) return fail("not a JSON object");
    const std::string type = str(j, "type");
    if (type.empty()) return fail("no \"type\"");
    auto maker = makers_.find(type);
    if (maker == makers_.end()) return fail("unknown type '" + type + "'");
    std::string why;
    std::shared_ptr<RenderObject> o = maker->second(j, ctx, why);
    if (!o) return fail(type + ": " + why);
    // the fields every type shares
    if (const std::string n = str(j, "name"); !n.empty()) o->SetName(n);
    float v[4];
    if (readNumbers(j, "pos", v, 2)) o->pos_ = {v[0], v[1]};
    if (auto s = j.find("scale"); s != j.end()) {
        if (s->is_number()) o->scale_ = glm::vec2(s->get<float>());
        else if (readNumbers(j, "scale", v, 2)) o->scale_ = {v[0], v[1]};
    }
    if (auto r = j.find("rotation"); r != j.end() && r->is_number()) o->rotation_ = r->get<float>();
    o->applyTransform();
    readNumbers(j, "tint", o->tint, 4);
    if (auto vis = j.find("visible"); vis != j.end() && vis->is_boolean()) o->SetVisible(vis->get<bool>());
    if (auto kids = j.find("children"); kids != j.end()) {
        if (!kids->is_array()) return fail("\"children\" is not a list");
        for (size_t i = 0; i < kids->size(); i++) {
            BuildContext sub{ctx.res, ctx.factory, (ctx.where.empty() ? "" : ctx.where + ".") + "children[" + std::to_string(i) + "]"};
            std::shared_ptr<RenderObject> c = build((*kids)[i], sub, error);
            if (!c) return nullptr;   // error says where
            o->addChild(std::move(c));
        }
    }
    return o;
}

std::shared_ptr<RenderObject> RenderObjectFactory::make(const nlohmann::json& j, res::ResourceManager& res, std::string* error) const {
    BuildContext ctx{res, *this, std::string()};
    std::string err;
    std::shared_ptr<RenderObject> o = build(j, ctx, err);
    if (error) *error = o ? std::string() : err;
    return o;
}

std::shared_ptr<RenderObject> RenderObjectFactory::makeOrMissing(const nlohmann::json& j, res::ResourceManager& res, std::string* error) const {
    std::string err;
    if (std::shared_ptr<RenderObject> o = make(j, res, &err)) {
        if (error) error->clear();
        return o;
    }
    auto m = std::make_shared<MissingObject>();
    m->what = err;
    float v[2];
    if (j.is_object() && readNumbers(j, "pos", v, 2)) m->setPos(v[0], v[1]);
    if (error) *error = err;
    return m;
}

std::shared_ptr<RenderObject> RenderObjectFactory::instance(const nlohmann::json& j, res::ResourceManager& res, std::string* error) {
    const std::string key = j.dump();
    auto it = prototypes_.find(key);
    if (it == prototypes_.end()) {
        std::shared_ptr<RenderObject> proto = make(j, res, error);
        if (!proto) return nullptr;
        it = prototypes_.emplace(key, std::move(proto)).first;
    } else if (error) {
        error->clear();
    }
    return it->second->clone();
}

void registerBuiltinRenderObjects(RenderObjectFactory& f) {
    f.add<SpriteObject>();
    f.add<ClipObject>();
    f.add<ParticleObject>();
    f.add<TextObject>();
    f.add<GroupObject>();
    f.add<MissingObject>();
}

// ---- sprite --------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> SpriteObject::fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error) {
    auto o = std::make_shared<SpriteObject>();
    o->atlasPath_ = str(j, "atlas");
    o->name_ = str(j, "name");
    if (o->name_.empty()) { error = "no \"name\" (the sprite in the atlas)"; return nullptr; }
    o->atlas_ = ctx.res.get<res::AtlasRes>(o->atlasPath_.empty() ? std::string(res::ResourceManager::kSpriteAtlas) : o->atlasPath_);
    if (!o->atlas_->ok()) { error = "cannot load the atlas " + o->atlas_->path() + " (" + o->atlas_->error() + ")"; return nullptr; }
    if (!o->atlas_->find(o->name_)) { error = "no sprite '" + o->name_ + "' in " + o->atlas_->path(); return nullptr; }
    o->additive = str(j, "blend") == "add";
    return o;
}

std::shared_ptr<RenderObject> SpriteObject::clone() const {
    auto o = std::make_shared<SpriteObject>();
    o->atlas_ = atlas_;
    o->name_ = name_;
    o->atlasPath_ = atlasPath_;
    o->additive = additive;
    copyCommonTo(*o);
    return o;
}

nlohmann::json SpriteObject::toJson() const {
    nlohmann::json j = commonJson();
    if (!atlasPath_.empty()) j["atlas"] = atlasPath_;
    j["name"] = name_;
    if (additive) j["blend"] = "add";
    return j;
}

// As anim::makeQuad: the packed pixels inside the original image, around the region's pivot, through the transform.
void SpriteObject::draw(IRenderer* ren, const glm::mat3& world) {
    const int v = atlas_->contentVersion();
    if (v != quadVersion_ || world != quadWorld_ || !std::equal(tint, tint + 4, quadTint_)) {
        quadVersion_ = v;
        quadWorld_ = world;
        std::copy(tint, tint + 4, quadTint_);
        hasQuad_ = false;
        if (const AtlasRegion* r = atlas_->find(name_)) {
            const float x0 = r->offX - r->pivot[0] * r->origW, y0 = r->offY - r->pivot[1] * r->origH;
            const float x1 = x0 + r->w, y1 = y0 + r->h;
            const glm::vec3 c[4] = {world * glm::vec3(x0, y0, 1), world * glm::vec3(x1, y0, 1), world * glm::vec3(x1, y1, 1),
                                    world * glm::vec3(x0, y1, 1)};
            Quad q;
            q.hasCorners = true;
            float minX = c[0].x, minY = c[0].y, maxX = c[0].x, maxY = c[0].y;
            for (int i = 0; i < 4; i++) {
                q.corners[i * 2] = c[i].x;
                q.corners[i * 2 + 1] = c[i].y;
                minX = std::min(minX, c[i].x); maxX = std::max(maxX, c[i].x);
                minY = std::min(minY, c[i].y); maxY = std::max(maxY, c[i].y);
            }
            q.rect[0] = minX; q.rect[1] = minY; q.rect[2] = maxX - minX; q.rect[3] = maxY - minY;
            std::copy(r->uv, r->uv + 4, q.uv);
            std::copy(tint, tint + 4, q.tint);
            q.additive = additive;
            const std::vector<uint16_t>& pages = atlas_->pageTextures();
            q.texture = r->page >= 0 && r->page < (int)pages.size() ? pages[(size_t)r->page] : kSpriteAtlasTexture;
            quad_ = q;
            hasQuad_ = true;
        }
    }
    if (hasQuad_) ren->drawSprite(quad_);
}

// ---- clip ----------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> ClipObject::fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error) {
    auto o = std::make_shared<ClipObject>();
    auto [file, clip] = splitRef(str(j, "anim"));
    if (const std::string c = str(j, "clip"); !c.empty()) clip = c;
    if (file.empty()) { error = "no \"anim\" (the .anim file)"; return nullptr; }
    if (clip.empty()) { error = "no \"clip\""; return nullptr; }
    o->animPath_ = file;
    o->clip_ = clip;
    o->anim_ = ctx.res.get<res::AnimRes>(file);
    if (!o->anim_->ok()) { error = "cannot load " + o->anim_->path() + " (" + o->anim_->error() + ")"; return nullptr; }
    if (!o->anim_->find(clip)) { error = o->anim_->path() + " has no clip '" + clip + "'"; return nullptr; }
    if (auto l = j.find("loop"); l != j.end() && l->is_boolean()) o->loop = l->get<bool>();
    if (auto p = j.find("phase"); p != j.end() && p->is_number()) o->phase = p->get<float>();
    o->start();
    return o;
}

void ClipObject::start() {
    player_.play(anim_->find(clip_));
    seenVersion_ = anim_->version();
    seenContent_ = anim_->contentVersion();
    carryMs_ = 0;
    if (phase > 0) player_.update((int)std::lround(phase * 1000.0f));
    player_.takeEvents();
}

std::shared_ptr<RenderObject> ClipObject::clone() const {
    auto o = std::make_shared<ClipObject>();
    o->anim_ = anim_;
    o->clip_ = clip_;
    o->animPath_ = animPath_;
    o->loop = loop;
    o->phase = phase;
    copyCommonTo(*o);
    o->start();
    return o;
}

nlohmann::json ClipObject::toJson() const {
    nlohmann::json j = commonJson();
    j["anim"] = animPath_;
    j["clip"] = clip_;
    if (loop) j["loop"] = true;
    if (phase != 0) j["phase"] = phase;
    return j;
}

void ClipObject::update(float dt) {
    if (anim_->version() != seenVersion_) start();   // the file was reloaded: the clip is a new one
    carryMs_ += dt * 1000.0f;
    const int ms = (int)carryMs_;
    carryMs_ -= (float)ms;
    if (ms > 0) player_.update(ms);
    player_.takeEvents();
    if (loop && player_.finished()) player_.play(anim_->find(clip_));
    RenderObject::update(dt);
}

void ClipObject::draw(IRenderer* ren, const glm::mat3& world) {
    if (anim_->version() != seenVersion_) start();
    if (anim_->contentVersion() != seenContent_) {   // an atlas changed under the same set
        seenContent_ = anim_->contentVersion();
        player_.refreshQuads();
    }
    if (player_.showing()) player_.draw(ren, anim_->atlases(), world, isWhite(tint) ? nullptr : tint);
}

// ---- particle ------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> ParticleObject::fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error) {
    auto o = std::make_shared<ParticleObject>();
    auto [file, effect] = splitRef(str(j, "file"));
    if (const std::string e = str(j, "effect"); !e.empty()) effect = e;
    if (file.empty()) { error = "no \"file\" (the .prt file)"; return nullptr; }
    if (effect.empty()) { error = "no \"effect\""; return nullptr; }
    o->filePath_ = file;
    o->effect_ = effect;
    o->file_ = ctx.res.get<res::ParticleRes>(file);
    if (!o->file_->ok()) { error = "cannot load " + o->file_->path() + " (" + o->file_->error() + ")"; return nullptr; }
    if (!o->file_->find(effect)) { error = o->file_->path() + " has no effect '" + effect + "'"; return nullptr; }
    if (auto l = j.find("loop"); l != j.end() && l->is_boolean()) o->loop = l->get<bool>();
    if (auto s = j.find("seed"); s != j.end() && s->is_number_unsigned()) o->seed = s->get<uint32_t>();
    return o;
}

// Started on the first draw: the effect's prewarm makes world-space particles where it is, so it needs its place.
void ParticleObject::start(IRenderer* ren, const glm::mat3& world) {
    const fx::Effect* e = file_->find(effect_);
    seenVersion_ = file_->version();
    placed_ = true;
    if (!e) { fx_.stop(true); return; }
    fx_.setTransform(world);
    if (gpuThreshold > 0) fx_.setGpuSimulation(ren, gpuThreshold);
    fx_.play(e, seed ? seed : (e->seed ? e->seed : 1u));
}

std::shared_ptr<RenderObject> ParticleObject::clone() const {
    auto o = std::make_shared<ParticleObject>();
    o->file_ = file_;
    o->effect_ = effect_;
    o->filePath_ = filePath_;
    o->loop = loop;
    o->seed = seed;
    o->gpuThreshold = gpuThreshold;
    copyCommonTo(*o);
    return o;
}

nlohmann::json ParticleObject::toJson() const {
    nlohmann::json j = commonJson();
    j["file"] = filePath_;
    j["effect"] = effect_;
    if (loop) j["loop"] = true;
    if (seed) j["seed"] = seed;
    return j;
}

void ParticleObject::update(float dt) {
    if (placed_ && file_->version() != seenVersion_) placed_ = false;   // reloaded: start again on the next draw
    if (placed_ && fx_.effect()) {
        fx_.update(dt);
        if (loop && fx_.finished()) fx_.play(fx_.effect(), fx_.seed());
    }
    RenderObject::update(dt);
}

void ParticleObject::draw(IRenderer* ren, const glm::mat3& world) {
    if (!placed_) start(ren, world);
    if (!fx_.effect()) return;
    fx_.setTransform(world);
    fx_.draw(ren, file_->atlases(), glm::mat3(1.0f), isWhite(tint) ? nullptr : tint);
}

// ---- text ----------------------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> TextObject::fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error) {
    auto o = std::make_shared<TextObject>();
    o->fontPath_ = str(j, "font");
    if (o->fontPath_.empty()) { error = "no \"font\" (the .fnt file)"; return nullptr; }
    o->font_ = ctx.res.get<res::FontRes>(o->fontPath_);
    if (!o->font_->ok()) { error = "cannot load " + o->font_->path() + " (" + o->font_->error() + ")"; return nullptr; }
    o->text = str(j, "text");
    const std::string a = str(j, "align");
    o->align = a == "center" ? ::BitmapFont::Align::Center : a == "right" ? ::BitmapFont::Align::Right : ::BitmapFont::Align::Left;
    if (auto s = j.find("size"); s != j.end() && s->is_number()) o->size = s->get<float>();
    return o;
}

std::shared_ptr<RenderObject> TextObject::clone() const {
    auto o = std::make_shared<TextObject>();
    o->font_ = font_;
    o->fontPath_ = fontPath_;
    o->text = text;
    o->align = align;
    o->size = size;
    copyCommonTo(*o);
    return o;
}

nlohmann::json TextObject::toJson() const {
    nlohmann::json j = commonJson();
    j["font"] = fontPath_;
    j["text"] = text;
    if (align != ::BitmapFont::Align::Left) j["align"] = alignName(align);
    if (size != 1) j["size"] = size;
    return j;
}

// The anchor is the transform's origin; its x scale sizes the text (fonts are not rotated).
void TextObject::draw(IRenderer* ren, const glm::mat3& world) {
    const float scale = size * glm::length(glm::vec2(world[0]));
    quads_.clear();
    font_->font().appendQuads(text, world[2].x, world[2].y, scale, align, tint, font_->texture(), quads_);
    for (const Quad& q : quads_) ren->drawSprite(q);
}

// ---- group / missing -----------------------------------------------------------------------------------------------

std::shared_ptr<RenderObject> GroupObject::fromJson(const nlohmann::json&, BuildContext&, std::string&) {
    return std::make_shared<GroupObject>();
}

std::shared_ptr<RenderObject> GroupObject::clone() const {
    auto o = std::make_shared<GroupObject>();
    copyCommonTo(*o);
    return o;
}

nlohmann::json GroupObject::toJson() const { return commonJson(); }

std::shared_ptr<RenderObject> MissingObject::fromJson(const nlohmann::json& j, BuildContext&, std::string&) {
    auto o = std::make_shared<MissingObject>();
    o->what = str(j, "what");
    return o;
}

std::shared_ptr<RenderObject> MissingObject::clone() const {
    auto o = std::make_shared<MissingObject>();
    o->what = what;
    copyCommonTo(*o);
    return o;
}

nlohmann::json MissingObject::toJson() const {
    nlohmann::json j = commonJson();
    if (!what.empty()) j["what"] = what;
    return j;
}

void MissingObject::draw(IRenderer* ren, const glm::mat3& world) {
    Quad q;
    q.solid = true;
    q.hasCorners = true;
    const glm::vec3 c[4] = {world * glm::vec3(-16, -16, 1), world * glm::vec3(16, -16, 1), world * glm::vec3(16, 16, 1),
                            world * glm::vec3(-16, 16, 1)};
    float minX = c[0].x, minY = c[0].y, maxX = c[0].x, maxY = c[0].y;
    for (int i = 0; i < 4; i++) {
        q.corners[i * 2] = c[i].x;
        q.corners[i * 2 + 1] = c[i].y;
        minX = std::min(minX, c[i].x); maxX = std::max(maxX, c[i].x);
        minY = std::min(minY, c[i].y); maxY = std::max(maxY, c[i].y);
    }
    q.rect[0] = minX; q.rect[1] = minY; q.rect[2] = maxX - minX; q.rect[3] = maxY - minY;
    q.uv[0] = 0; q.uv[1] = 0; q.uv[2] = 1; q.uv[3] = 1;
    q.tint[0] = 1; q.tint[1] = 0.2f; q.tint[2] = 0.2f; q.tint[3] = 0.85f * tint[3];
    ren->drawSprite(q);
}

// ---- the stage editor's look ---------------------------------------------------------------------------------------

nlohmann::json lookToRenderJson(const nlohmann::json& look, const std::string& mediaDir, const std::string& spriteAtlas) {
    if (!look.is_object()) return nlohmann::json();
    nlohmann::json j;
    const std::string dir = mediaDir.empty() || mediaDir.back() == '/' ? mediaDir : mediaDir + "/";
    if (const auto [file, clip] = splitRef(str(look, "anim")); !file.empty() && !clip.empty()) {
        j = {{"type", "clip"}, {"anim", res::ResourceManager::join(dir, file)}, {"clip", clip}, {"loop", true}};
    } else if (std::string sprite = str(look, "sprite"); !sprite.empty()) {
        if (sprite.size() > 4 && sprite.compare(sprite.size() - 4, 4, ".png") == 0) sprite.resize(sprite.size() - 4);
        j = {{"type", "sprite"}, {"atlas", spriteAtlas}, {"name", sprite}};
    } else {
        return nlohmann::json();
    }
    if (auto t = look.find("tint"); t != look.end() && t->is_array() && t->size() >= 3) {
        nlohmann::json tint = *t;
        if (tint.size() == 3) tint.push_back(1.0);
        j["tint"] = tint;
    }
    return j;
}

}  // namespace toms

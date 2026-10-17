// render_object.h -- things that draw, made from JSON (docs/25_RESOURCE_MANAGER.md §4.7).
//
// FM79979's GetMe, done with a map: a JSON object names its class in "type", the factory finds that class's maker in
// one look-up, and the class reads the rest:
//
//   {"type": "sprite",   "atlas": "media/atlas/game.atlas", "name": "slime", "tint": [1, 0.8, 0.8, 1]}
//   {"type": "clip",     "anim": "media/anim/monster_idle.anim", "clip": "idle_slime", "loop": true}
//   {"type": "particle", "file": "media/fx/sparks.prt", "effect": "spark", "loop": true}
//   {"type": "text",     "font": "media/fonts/score.fnt", "text": "1200", "align": "right"}
//   {"type": "group",    "children": [ ... ]}
//
// Every type also takes "name", "pos": [x, y], "scale": s or [sx, sy], "rotation" (degrees), "tint", "visible" and
// "children". The caller only sees RenderObject; the real class does the work.
//
// Resources come from a res::ResourceManager and are held by the object (a sprite holds its atlas), so what an object
// draws with cannot go away under it. Positions are screen pixels, y down, like anim::placement: a node's world
// transform is where it draws (a sprite's pivot, a clip's origin, an effect's origin, the text's anchor).
//
// A description used many times (a monster type's look) is made once and clone()d for each use: a clone shares the
// resources and copies a few numbers, with no JSON and no look-ups. RenderObjectFactory::instance() does that.
#pragma once
#include "resource_types.h"
#include "scene.h"

#include <json.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace toms {

class RenderObjectFactory;

class RenderObject : public GameObject {
    TOMS_OBJECT(RenderObject)
public:
    float tint[4] = {1, 1, 1, 1};   // multiplies this object's colours (not its children's)

    // Its place relative to its parent (pixels, y down), its scale and its rotation (degrees): the node's local transform.
    void setPos(float x, float y) { pos_ = {x, y}; applyTransform(); }
    void setScale(float sx, float sy) { scale_ = {sx, sy}; applyTransform(); }
    void setRotation(float degrees) { rotation_ = degrees; applyTransform(); }
    glm::vec2 pos() const { return pos_; }
    glm::vec2 scale() const { return scale_; }
    float rotation() const { return rotation_; }

    // Children it owns: drawn after it, in order, placed by its transform. (Node's tree links them; the shared_ptr
    // keeps them alive.)
    void addChild(std::shared_ptr<RenderObject> child);
    const std::vector<std::shared_ptr<RenderObject>>& children() const { return owned_; }

    // Advances clips and effects (seconds), its children too.
    virtual void update(float dt);
    // Draws itself; RenderTree() draws it and its children, skipping a hidden one with everything under it.
    void Render(IRenderer* ren) override;

    // A copy that shares its resources, with its children cloned.
    virtual std::shared_ptr<RenderObject> clone() const = 0;
    // The JSON it was made from (its current state): factory.make(o->toJson()) makes the same object.
    virtual nlohmann::json toJson() const = 0;
    virtual const char* typeName() const = 0;   // its "type"

    // Its own resources were reloaded (contentVersion changed) -- they look things up again on their own; this is
    // for tests and tools.
    virtual bool ok() const { return true; }

protected:
    virtual void draw(IRenderer* ren, const glm::mat3& world) = 0;
    // For clone() / toJson(): the fields every type shares (transform, tint, visibility, name, children).
    void copyCommonTo(RenderObject& o) const;
    nlohmann::json commonJson() const;

private:
    friend class RenderObjectFactory;   // build() sets the common fields
    void applyTransform();
    std::vector<std::shared_ptr<RenderObject>> owned_;
    glm::vec2 pos_{0.0f, 0.0f}, scale_{1.0f, 1.0f};
    float rotation_ = 0;
};

// What a maker gets: the resources, the factory (for children) and where the object is in the JSON (for errors).
struct BuildContext {
    res::ResourceManager& res;
    const RenderObjectFactory& factory;
    std::string where;   // "children[2]" ... ("" at the top)
};

class RenderObjectFactory {
public:
    using MakeFn = std::shared_ptr<RenderObject> (*)(const nlohmann::json& j, BuildContext& ctx, std::string& error);

    // A class with `static constexpr const char* kTypeName` and `static std::shared_ptr<RenderObject> fromJson(const
    // nlohmann::json&, BuildContext&, std::string&)` -- checked when it compiles.
    template <class T> void add() { add(T::kTypeName, &T::fromJson); }
    void add(const std::string& type, MakeFn make);
    bool has(const std::string& type) const { return makers_.count(type) != 0; }
    std::vector<std::string> types() const;

    // The object a JSON description makes, with its common fields and children. Null when the type is unknown, a file
    // or a sprite is missing, or a field is wrong: `error` says what and where ("children[1]: no sprite 'x' in ...").
    std::shared_ptr<RenderObject> make(const nlohmann::json& j, res::ResourceManager& res, std::string* error = nullptr) const;
    // As make(), but never null: a mistake gives a MissingObject (a red box) that shows where it is.
    std::shared_ptr<RenderObject> makeOrMissing(const nlohmann::json& j, res::ResourceManager& res, std::string* error = nullptr) const;
    // A clone of the object this description made before (made now the first time). For descriptions used many times.
    std::shared_ptr<RenderObject> instance(const nlohmann::json& j, res::ResourceManager& res, std::string* error = nullptr);
    void clearPrototypes() { prototypes_.clear(); }

    std::shared_ptr<RenderObject> build(const nlohmann::json& j, BuildContext& ctx, std::string& error) const;

private:
    std::map<std::string, MakeFn> makers_;
    std::map<std::string, std::shared_ptr<RenderObject>> prototypes_;   // by the description's text
};

// sprite, clip, particle, text, group, missing.
void registerBuiltinRenderObjects(RenderObjectFactory& f);

// ---- the built-in types ----------------------------------------------------------------------------------------

class SpriteObject : public RenderObject {
    TOMS_OBJECT(SpriteObject)
public:
    static constexpr const char* kTypeName = "sprite";
    static std::shared_ptr<RenderObject> fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error);
    std::shared_ptr<RenderObject> clone() const override;
    nlohmann::json toJson() const override;
    const char* typeName() const override { return kTypeName; }

    const std::shared_ptr<res::AtlasRes>& atlas() const { return atlas_; }
    const std::string& spriteName() const { return name_; }
    bool additive = false;

protected:
    void draw(IRenderer* ren, const glm::mat3& world) override;

private:
    std::shared_ptr<res::AtlasRes> atlas_;
    std::string name_;
    std::string atlasPath_;   // as written in the JSON
    // the quad, kept until the transform, the tint or the atlas changes
    Quad quad_{};
    bool hasQuad_ = false;
    glm::mat3 quadWorld_{0.0f};
    float quadTint_[4] = {-1, -1, -1, -1};
    int quadVersion_ = -1;
};

class ClipObject : public RenderObject {
    TOMS_OBJECT(ClipObject)
public:
    static constexpr const char* kTypeName = "clip";
    static std::shared_ptr<RenderObject> fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error);
    std::shared_ptr<RenderObject> clone() const override;
    nlohmann::json toJson() const override;
    const char* typeName() const override { return kTypeName; }
    void update(float dt) override;

    const std::shared_ptr<res::AnimRes>& anim() const { return anim_; }
    const std::string& clipName() const { return clip_; }
    const anim::AnimPlayer& player() const { return player_; }
    bool loop = false;       // play again when it finishes (a clip with looping nodes keeps moving anyway)
    float phase = 0;         // seconds it starts in (a row of the same clip out of step)
    bool ok() const override { return anim_ && anim_->find(clip_) != nullptr; }

protected:
    void draw(IRenderer* ren, const glm::mat3& world) override;

private:
    void start();
    std::shared_ptr<res::AnimRes> anim_;
    std::string clip_, animPath_;
    anim::AnimPlayer player_;
    int seenVersion_ = -1, seenContent_ = -1;
    float carryMs_ = 0;
};

class ParticleObject : public RenderObject {
    TOMS_OBJECT(ParticleObject)
public:
    static constexpr const char* kTypeName = "particle";
    static std::shared_ptr<RenderObject> fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error);
    std::shared_ptr<RenderObject> clone() const override;
    nlohmann::json toJson() const override;
    const char* typeName() const override { return kTypeName; }
    void update(float dt) override;

    const std::shared_ptr<res::ParticleRes>& file() const { return file_; }
    const fx::EffectInstance& instance() const { return fx_; }
    bool loop = false;       // a one-shot effect plays again once it has finished
    uint32_t seed = 0;       // 0: the effect's own
    int gpuThreshold = 0;    // emitters above this many particles simulate on the GPU (0 = never; EffectInstance)
    bool ok() const override { return file_ && file_->find(effect_) != nullptr; }

protected:
    void draw(IRenderer* ren, const glm::mat3& world) override;

private:
    void start(IRenderer* ren, const glm::mat3& world);
    std::shared_ptr<res::ParticleRes> file_;
    std::string effect_, filePath_;
    fx::EffectInstance fx_;
    int seenVersion_ = -1;
    bool placed_ = false;
};

class TextObject : public RenderObject {
    TOMS_OBJECT(TextObject)
public:
    static constexpr const char* kTypeName = "text";
    static std::shared_ptr<RenderObject> fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error);
    std::shared_ptr<RenderObject> clone() const override;
    nlohmann::json toJson() const override;
    const char* typeName() const override { return kTypeName; }

    std::string text;        // UTF-8
    ::BitmapFont::Align align = ::BitmapFont::Align::Left;
    float size = 1;          // the font's scale

protected:
    void draw(IRenderer* ren, const glm::mat3& world) override;

private:
    std::shared_ptr<res::FontRes> font_;
    std::string fontPath_;
    std::vector<Quad> quads_;
};

class GroupObject : public RenderObject {
    TOMS_OBJECT(GroupObject)
public:
    static constexpr const char* kTypeName = "group";
    static std::shared_ptr<RenderObject> fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error);
    std::shared_ptr<RenderObject> clone() const override;
    nlohmann::json toJson() const override;
    const char* typeName() const override { return kTypeName; }

protected:
    void draw(IRenderer*, const glm::mat3&) override {}
};

// What a mistake in the data draws: a red box (32 x 32 around its origin), so it is seen, never a crash.
class MissingObject : public RenderObject {
    TOMS_OBJECT(MissingObject)
public:
    static constexpr const char* kTypeName = "missing";
    static std::shared_ptr<RenderObject> fromJson(const nlohmann::json& j, BuildContext& ctx, std::string& error);
    std::shared_ptr<RenderObject> clone() const override;
    nlohmann::json toJson() const override;
    const char* typeName() const override { return kTypeName; }
    std::string what;        // the error it stands for
    bool ok() const override { return false; }

protected:
    void draw(IRenderer* ren, const glm::mat3& world) override;
};

// The stage editor's object "look" ({"sprite": "slime.png", "anim": "anim/x.anim#clip", "tint": [...]}; anim wins over
// sprite) as a render description: a looping "clip", else a "sprite" from `spriteAtlas`. Paths in it are relative to
// `mediaDir` (the assets/media folder). Empty when it names neither.
nlohmann::json lookToRenderJson(const nlohmann::json& look, const std::string& mediaDir,
                                const std::string& spriteAtlas = res::ResourceManager::kSpriteAtlas);

}  // namespace toms

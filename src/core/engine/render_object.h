// render_object.h -- things that draw, made from JSON (docs/engine/12_RESOURCE_MANAGER.md §4.7).
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
    float m_arrTint[4] = {1, 1, 1, 1};   // multiplies this object's colours (not its children's)

    // Its place relative to its parent (pixels, y down), its scale and its rotation (degrees): the node's local transform.
    void f_SetPos(float l_fX, float l_fY) { m_v2Pos = {l_fX, l_fY}; f_ApplyTransform(); }
    void f_SetScale(float l_fSx, float l_fSy) { m_v2Scale = {l_fSx, l_fSy}; f_ApplyTransform(); }
    void f_SetRotation(float l_fDegrees) { m_fRotation = l_fDegrees; f_ApplyTransform(); }
    glm::vec2 f_Pos() const { return m_v2Pos; }
    glm::vec2 f_Scale() const { return m_v2Scale; }
    float f_Rotation() const { return m_fRotation; }

    // Children it owns: drawn after it, in order, placed by its transform. (Node's tree links them; the shared_ptr
    // keeps them alive.)
    void f_AddChild(std::shared_ptr<RenderObject> l_spChild);
    const std::vector<std::shared_ptr<RenderObject>>& f_Children() const { return m_vOwned; }

    // Advances clips and effects (seconds), its children too.
    virtual void f_Update(float l_fDt);
    // The camera it is seen through, for it and its children: it draws at view * its transform. An effect keeps its
    // particles under the view (where they were born on the map), so a scrolling camera carries them along.
    void f_SetView(const glm::mat3& l_mat3View);
    const glm::mat3& f_View() const { return m_mat3View; }
    // Draws itself; RenderTree() draws it and its children, skipping a hidden one with everything under it.
    void f_Render(IRenderer* l_pRen) override;

    // A copy that shares its resources, with its children cloned.
    virtual std::shared_ptr<RenderObject> f_Clone() const = 0;
    // The JSON it was made from (its current state): factory.make(o->toJson()) makes the same object.
    virtual nlohmann::json f_ToJson() const = 0;
    virtual const char* f_TypeName() const = 0;   // its "type"

    // Its own resources were reloaded (contentVersion changed) -- they look things up again on their own; this is
    // for tests and tools.
    virtual bool f_Ok() const { return true; }

protected:
    virtual void f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) = 0;
    // For clone() / toJson(): the fields every type shares (transform, tint, visibility, name, children).
    void f_CopyCommonTo(RenderObject& l_oO) const;
    nlohmann::json f_CommonJson() const;

private:
    friend class RenderObjectFactory;   // build() sets the common fields
    void f_ApplyTransform();
    std::vector<std::shared_ptr<RenderObject>> m_vOwned;
    glm::mat3 m_mat3View{1.0f};
    glm::vec2 m_v2Pos{0.0f, 0.0f}, m_v2Scale{1.0f, 1.0f};
    float m_fRotation = 0;
};

// What a maker gets: the resources, the factory (for children) and where the object is in the JSON (for errors).
struct BuildContext {
    res::ResourceManager& m_oRes;
    const RenderObjectFactory& m_oFactory;
    std::string m_strWhere;   // "children[2]" ... ("" at the top)
};

class RenderObjectFactory {
public:
    using MakeFn = std::shared_ptr<RenderObject> (*)(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);

    // A class with `static constexpr const char* m_pszTypeName` and `static std::shared_ptr<RenderObject> f_FromJson(const
    // nlohmann::json&, BuildContext&, std::string&)` -- checked when it compiles.
    template <class T> void f_Add() { f_Add(T::m_pszTypeName, &T::f_FromJson); }
    void f_Add(const std::string& l_strType, MakeFn l_fnMake);
    bool f_Has(const std::string& l_strType) const { return m_mapMakers.count(l_strType) != 0; }
    std::vector<std::string> f_Types() const;

    // The object a JSON description makes, with its common fields and children. Null when the type is unknown, a file
    // or a sprite is missing, or a field is wrong: `error` says what and where ("children[1]: no sprite 'x' in ...").
    std::shared_ptr<RenderObject> f_Make(const nlohmann::json& l_jJ, res::ResourceManager& l_oRes, std::string* l_pError = nullptr) const;
    // As make(), but never null: a mistake gives a MissingObject (a red box) that shows where it is.
    std::shared_ptr<RenderObject> f_MakeOrMissing(const nlohmann::json& l_jJ, res::ResourceManager& l_oRes, std::string* l_pError = nullptr) const;
    // A clone of the object this description made before (made now the first time). For descriptions used many times.
    std::shared_ptr<RenderObject> f_Instance(const nlohmann::json& l_jJ, res::ResourceManager& l_oRes, std::string* l_pError = nullptr);
    void f_ClearPrototypes() { m_mapPrototypes.clear(); }

    std::shared_ptr<RenderObject> f_Build(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError) const;

private:
    std::map<std::string, MakeFn> m_mapMakers;
    std::map<std::string, std::shared_ptr<RenderObject>> m_mapPrototypes;   // by the description's text
};

// sprite, clip, particle, text, group, missing.
void f_RegisterBuiltinRenderObjects(RenderObjectFactory& l_oF);

// ---- the built-in types ----------------------------------------------------------------------------------------

class SpriteObject : public RenderObject {
    TOMS_OBJECT(SpriteObject)
public:
    static constexpr const char* m_pszTypeName = "sprite";
    static std::shared_ptr<RenderObject> f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);
    std::shared_ptr<RenderObject> f_Clone() const override;
    nlohmann::json f_ToJson() const override;
    const char* f_TypeName() const override { return m_pszTypeName; }

    const std::shared_ptr<res::AtlasRes>& f_Atlas() const { return m_spAtlas; }
    const std::string& f_SpriteName() const { return m_strName; }
    bool m_bAdditive = false;
    bool m_bSolid = false;      // one flat colour (the tint) in the sprite's shape: a gate's key colour washed over it

protected:
    void f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) override;

private:
    std::shared_ptr<res::AtlasRes> m_spAtlas;
    std::string m_strName;
    std::string m_strAtlasPath;   // as written in the JSON
    // the quad, kept until the transform, the tint or the atlas changes
    Quad m_oQuad{};
    bool m_bHasQuad = false;
    glm::mat3 m_mat3QuadWorld{0.0f};
    float m_arrQuadTint[4] = {-1, -1, -1, -1};
    int m_iQuadVersion = -1;
};

class ClipObject : public RenderObject {
    TOMS_OBJECT(ClipObject)
public:
    static constexpr const char* m_pszTypeName = "clip";
    static std::shared_ptr<RenderObject> f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);
    std::shared_ptr<RenderObject> f_Clone() const override;
    nlohmann::json f_ToJson() const override;
    const char* f_TypeName() const override { return m_pszTypeName; }
    void f_Update(float l_fDt) override;

    const std::shared_ptr<res::AnimRes>& f_Anim() const { return m_spAnim; }
    const std::string& f_ClipName() const { return m_strClip; }
    const anim::AnimPlayer& f_Player() const { return m_oPlayer; }
    bool m_bLoop = false;       // play again when it finishes (a clip with looping nodes keeps moving anyway)
    float m_fPhase = 0;         // seconds it starts in (a row of the same clip out of step)
    bool f_Ok() const override { return m_spAnim && m_spAnim->f_Find(m_strClip) != nullptr; }
    // Driven by an outside clock instead of update(): shows the clip at this time (seconds since it started).
    void f_SetTime(float l_fSeconds);
    float f_Duration() const;  // of one play (0 when the clip is missing)

protected:
    void f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) override;

private:
    void f_Start();
    std::shared_ptr<res::AnimRes> m_spAnim;
    std::string m_strClip, m_strAnimPath;
    anim::AnimPlayer m_oPlayer;
    int m_iSeenVersion = -1, m_iSeenContent = -1;
    float m_fCarryMs = 0;
};

class ParticleObject : public RenderObject {
    TOMS_OBJECT(ParticleObject)
public:
    static constexpr const char* m_pszTypeName = "particle";
    static std::shared_ptr<RenderObject> f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);
    std::shared_ptr<RenderObject> f_Clone() const override;
    nlohmann::json f_ToJson() const override;
    const char* f_TypeName() const override { return m_pszTypeName; }
    void f_Update(float l_fDt) override;

    const std::shared_ptr<res::ParticleRes>& f_File() const { return m_spFile; }
    const fx::EffectInstance& f_Instance() const { return m_oFx; }
    bool m_bLoop = false;       // a one-shot effect plays again once it has finished
    uint32_t m_uSeed = 0;       // 0: the effect's own
    int m_iGpuThreshold = 0;    // emitters above this many particles simulate on the GPU (0 = never; EffectInstance)
    bool f_Ok() const override { return m_spFile && m_spFile->f_Find(m_strEffect) != nullptr; }

protected:
    void f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) override;

private:
    void f_Start(IRenderer* l_pRen, const glm::mat3& l_mat3World);
    std::shared_ptr<res::ParticleRes> m_spFile;
    std::string m_strEffect, m_strFilePath;
    fx::EffectInstance m_oFx;
    int m_iSeenVersion = -1;
    bool m_bPlaced = false;
};

class TextObject : public RenderObject {
    TOMS_OBJECT(TextObject)
public:
    static constexpr const char* m_pszTypeName = "text";
    static std::shared_ptr<RenderObject> f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);
    std::shared_ptr<RenderObject> f_Clone() const override;
    nlohmann::json f_ToJson() const override;
    const char* f_TypeName() const override { return m_pszTypeName; }

    std::string m_strText;        // UTF-8
    ::BitmapFont::Align m_eAlign = ::BitmapFont::Align::Left;
    float m_fSize = 1;          // the font's scale

protected:
    void f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) override;

private:
    std::shared_ptr<res::FontRes> m_spFont;
    std::string m_strFontPath;
    std::vector<Quad> m_vQuads;
};

class GroupObject : public RenderObject {
    TOMS_OBJECT(GroupObject)
public:
    static constexpr const char* m_pszTypeName = "group";
    static std::shared_ptr<RenderObject> f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);
    std::shared_ptr<RenderObject> f_Clone() const override;
    nlohmann::json f_ToJson() const override;
    const char* f_TypeName() const override { return m_pszTypeName; }

protected:
    void f_Draw(IRenderer*, const glm::mat3&) override {}
};

// What a mistake in the data draws: a red box (32 x 32 around its origin), so it is seen, never a crash.
class MissingObject : public RenderObject {
    TOMS_OBJECT(MissingObject)
public:
    static constexpr const char* m_pszTypeName = "missing";
    static std::shared_ptr<RenderObject> f_FromJson(const nlohmann::json& l_jJ, BuildContext& l_oCtx, std::string& l_strError);
    std::shared_ptr<RenderObject> f_Clone() const override;
    nlohmann::json f_ToJson() const override;
    const char* f_TypeName() const override { return m_pszTypeName; }
    std::string m_strWhat;        // the error it stands for
    bool f_Ok() const override { return false; }

protected:
    void f_Draw(IRenderer* l_pRen, const glm::mat3& l_mat3World) override;
};

// The stage editor's object "look" ({"sprite": "slime.png", "anim": "anim/x.anim#clip", "tint": [...]}; anim wins over
// sprite) as a render description: a looping "clip", else a "sprite" from `spriteAtlas`. Paths in it are relative to
// `mediaDir` (the assets/media folder). Empty when it names neither.
nlohmann::json f_LookToRenderJson(const nlohmann::json& l_jLook, const std::string& l_strMediaDir,
                                const std::string& l_strSpriteAtlas = res::ResourceManager::m_pszSpriteAtlas);

}  // namespace toms

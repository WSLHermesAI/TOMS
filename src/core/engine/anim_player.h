// anim_player.h -- plays an anim_clip.h clip and draws it from the packed sprite atlas(es).
//
//   toms::anim::AnimPlayer p;
//   p.play(file.find("slime_attack"));
//   each frame:  p.update(dtMs);
//                for (const std::string& e : p.takeEvents()) ...     // "hit", "sfx_swing", ...
//                p.draw(ren, atlases, toms::anim::placement(x, y, scale));
//
// A sprite is drawn around its pivot (the node's, else the atlas's x_pivot, else the centre) with
// the atlas's trim offsets, so it sits exactly where the untrimmed image would. The sprites come
// from one or more atlases (AtlasSet, the .anim's "atlases" in order); sprites from the same atlas
// page share a texture, so a whole clip -- rotated, scaled, any depth of nodes -- still draws in a
// few batches (a new draw call only where the texture or additive blending changes).
#pragma once
#include "anim_clip.h"
#include "atlas_file.h"
#include "render_iface.h"

#include <string>
#include <vector>

namespace toms::anim {

// Clip space -> screen: the clip's origin at (x, y), uniformly scaled (and optionally rotated).
glm::mat3 placement(float x, float y, float scale = 1.0f, float rotationDegrees = 0.0f);

// The atlases a clip draws from (the .anim's "atlases"), each with its id. A sprite reference
// "id:name" (anim_clip.h SpriteRef) is looked up in the atlas with that id; a bare "name" in each
// atlas in turn, the first that has it winning. Each atlas page has the Quad::texture it is drawn
// with -- kSpriteAtlasTexture for the game's own sprite atlas, else an IRenderer::loadTexture id.
struct AtlasSet {
    struct Entry {
        const toms::AtlasFile* atlas = nullptr;
        std::string id;
        std::vector<uint16_t> pageTextures;   // per page; empty = every page is the sprite atlas texture
    };
    std::vector<Entry> entries;

    AtlasSet() = default;
    AtlasSet(const toms::AtlasFile& single) { entries.push_back({&single, std::string(), {}}); }   // the common case
    void add(const toms::AtlasFile& atlas, std::string id = std::string(), std::vector<uint16_t> pageTextures = {}) {
        entries.push_back({&atlas, std::move(id), std::move(pageTextures)});
    }
    // The region a sprite reference names (nullptr: not found) and the texture to draw it with.
    // `entry` (optional) gets the index of the atlas it came from.
    const toms::AtlasRegion* find(const std::string& ref, uint16_t* texture = nullptr, int* entry = nullptr) const;
};

// The quads for one evaluated pose list (anim_clip.h evaluate), in draw order. tint multiplies
// every colour (nullptr = white). Sprites no atlas has are skipped and, with `missing`, reported
// once each.
void appendQuads(const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& placement,
                 const float tint[4], std::vector<Quad>& out, std::vector<std::string>* missing = nullptr);
// The quad of one pose; false = nothing to draw (hidden, transparent, no sprite or not found).
bool makeQuad(const NodePose& pose, const AtlasSet& atlases, const glm::mat3& placement, const glm::vec4& tint,
              Quad& out, const toms::AtlasRegion** region = nullptr);

class AnimPlayer {
public:
    void play(const Clip* clip);       // from the start; nullptr = stop
    void stop() { play(nullptr); }
    void update(int dtMs);             // advances; events passed go to takeEvents()
    std::vector<std::string> takeEvents();

    const Clip* clip() const { return clip_; }
    float time() const;                // clip time now (0..duration)
    bool finished() const;             // played playCount times (never, for a looping clip)
    bool showing() const;              // something to draw: running, or finished with stayAtLastFrame

    // Draws the clip at time(). The poses are a PoseCache (only the key tracks that have keys move)
    // and each quad is kept until its pose, the placement, the tint or the AtlasSet changes, so a
    // still part of a clip costs nothing but its drawSprite call.
    void draw(IRenderer* ren, const AtlasSet& atlases, const glm::mat3& placement,
              const float tint[4] = nullptr) const;
    // The atlases' contents changed under the same AtlasSet (a reload): rebuild every quad.
    void refreshQuads() { quadAtlases_ = nullptr; }
    const PoseCache& poses() const { return cache_; }

private:
    const Clip* clip_ = nullptr;
    double elapsed_ = 0;               // seconds since play()
    bool started_ = false;             // events at time 0 have been sent
    std::vector<std::string> events_;
    mutable PoseCache cache_;
    mutable std::vector<Quad> quads_;                // per pose
    mutable std::vector<unsigned char> hasQuad_;     // per pose: quads_[i] is drawn
    mutable const AtlasSet* quadAtlases_ = nullptr;  // what quads_ were made with
    mutable glm::mat3 quadPlace_{1.0f};
    mutable glm::vec4 quadTint_{1.0f};

    double total() const;              // playCount * duration; < 0 = forever
};

}  // namespace toms::anim

// anim_player.cpp -- see anim_player.h.
#include "anim_player.h"

#include <algorithm>
#include <cmath>

namespace toms::anim {

glm::mat3 placement(float x, float y, float scale, float rotationDegrees) {
    Node n;
    n.pos = {x, y};
    n.scale = {scale, scale};
    n.rot = rotationDegrees;
    return localTransform(n, 0);
}

const toms::AtlasRegion* AtlasSet::find(const std::string& ref, uint16_t* texture, int* entry) const {
    SpriteRef s = parseSpriteRef(ref);
    // An atlas id this set does not have (e.g. the game draws a clip with only its own atlas): look
    // the name up by order instead. The editor's check reports such references.
    if (!s.atlas.empty() &&
        std::none_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.id == s.atlas; }))
        s.atlas.clear();
    for (size_t i = 0; i < entries.size(); i++) {
        const Entry& e = entries[i];
        if (!e.atlas || (!s.atlas.empty() && e.id != s.atlas)) continue;
        const toms::AtlasRegion* r = e.atlas->find(s.name);
        if (!r) {
            if (!s.atlas.empty()) return nullptr;   // the named atlas does not have it: no fallback
            continue;
        }
        if (entry) *entry = (int)i;
        if (texture) {
            const bool own = r->page >= 0 && r->page < (int)e.pageTextures.size();
            *texture = own ? e.pageTextures[(size_t)r->page] : kSpriteAtlasTexture;
        }
        return r;
    }
    return nullptr;
}

bool makeQuad(const NodePose& p, const AtlasSet& atlases, const glm::mat3& place, const glm::vec4& t, Quad& q,
              const toms::AtlasRegion** region) {
    if (region) *region = nullptr;
    if (!p.visible || p.sprite.empty() || p.color.a <= 0.0f) return false;
    uint16_t texture = kSpriteAtlasTexture;
    const toms::AtlasRegion* r = atlases.find(p.sprite, &texture);
    if (region) *region = r;
    if (!r) return false;
    // The packed (trimmed) pixels, placed inside the original image, relative to the pivot.
    const glm::vec2 pivot = p.node->hasPivot ? p.node->pivot : glm::vec2(r->pivot[0], r->pivot[1]);
    const float x0 = r->offX - pivot.x * r->origW, y0 = r->offY - pivot.y * r->origH;
    const float x1 = x0 + r->w, y1 = y0 + r->h;
    const glm::mat3 m = place * p.world;
    const glm::vec3 c[4] = {m * glm::vec3(x0, y0, 1), m * glm::vec3(x1, y0, 1), m * glm::vec3(x1, y1, 1),
                            m * glm::vec3(x0, y1, 1)};
    q = Quad();
    q.hasCorners = true;
    float minX = c[0].x, minY = c[0].y, maxX = c[0].x, maxY = c[0].y;
    for (int i = 0; i < 4; i++) {
        q.corners[i * 2] = c[i].x;
        q.corners[i * 2 + 1] = c[i].y;
        minX = std::min(minX, c[i].x); maxX = std::max(maxX, c[i].x);
        minY = std::min(minY, c[i].y); maxY = std::max(maxY, c[i].y);
    }
    q.rect[0] = minX; q.rect[1] = minY; q.rect[2] = maxX - minX; q.rect[3] = maxY - minY;   // bounds
    std::copy(r->uv, r->uv + 4, q.uv);
    const glm::vec4 col = p.color * t;
    q.tint[0] = col.r; q.tint[1] = col.g; q.tint[2] = col.b; q.tint[3] = col.a;
    q.additive = p.node->blend == Blend::Add;
    q.texture = texture;
    return true;
}

void appendQuads(const std::vector<NodePose>& poses, const AtlasSet& atlases, const glm::mat3& place,
                 const float tint[4], std::vector<Quad>& out, std::vector<std::string>* missing) {
    const glm::vec4 t = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    Quad q;
    for (const NodePose& p : poses) {
        const toms::AtlasRegion* r = nullptr;
        if (makeQuad(p, atlases, place, t, q, &r)) {
            out.push_back(q);
        } else if (!r && missing && p.visible && !p.sprite.empty() && p.color.a > 0.0f &&
                   std::find(missing->begin(), missing->end(), p.sprite) == missing->end()) {
            missing->push_back(p.sprite);
        }
    }
}

// ---- AnimPlayer --------------------------------------------------------------------------------

void AnimPlayer::play(const Clip* clip) {
    clip_ = clip;
    cache_.bind(clip);
    quadAtlases_ = nullptr;
    elapsed_ = 0;
    started_ = false;
    events_.clear();
}

double AnimPlayer::total() const {
    if (!clip_ || clip_->playCount < 0) return -1;
    return (double)clip_->playCount * clip_->duration();
}

bool AnimPlayer::finished() const {
    const double tot = total();
    return clip_ && tot >= 0 && elapsed_ >= tot;
}

bool AnimPlayer::showing() const { return clip_ && (!finished() || clip_->stayAtLastFrame); }

float AnimPlayer::time() const {
    if (!clip_) return 0;
    const double d = clip_->duration();
    if (d <= 0) return 0;
    if (finished()) return (float)d;
    return (float)std::fmod(elapsed_, d);
}

void AnimPlayer::update(int dtMs) {
    if (!clip_) return;
    const double d = clip_->duration();
    const double tot = total();
    if (!started_) {   // keys at time 0 belong to the first update
        eventsBetween(*clip_, -1.0f, 0.0f, events_);
        started_ = true;
    }
    double a = elapsed_;
    elapsed_ += std::max(0, dtMs) / 1000.0;
    const double b = tot >= 0 ? std::min(elapsed_, tot) : elapsed_;
    if (d <= 0) return;
    // Walk the passed time one play at a time, so every event in it is sent once, in order.
    while (a < b) {
        double loopStart = std::floor(a / d) * d;
        if (loopStart + d <= a) loopStart += d;   // rounding put `a` on a boundary: that is the next play
        const double end = std::min(b, loopStart + d);
        const float t0 = a == loopStart && a > 0 ? -1.0f : (float)(a - loopStart);   // a new play: include time 0
        eventsBetween(*clip_, t0, (float)(end - loopStart), events_);
        a = end;
    }
}

std::vector<std::string> AnimPlayer::takeEvents() {
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

void AnimPlayer::draw(IRenderer* ren, const AtlasSet& atlases, const glm::mat3& place, const float tint[4]) const {
    if (!ren || !showing()) return;
    cache_.seek(time());
    const std::vector<NodePose>& poses = cache_.poses();
    const glm::vec4 t = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    const bool all = quads_.size() != poses.size() || quadAtlases_ != &atlases || quadPlace_ != place || quadTint_ != t;
    if (all) {
        quads_.assign(poses.size(), Quad());
        hasQuad_.assign(poses.size(), 0);
        quadAtlases_ = &atlases;
        quadPlace_ = place;
        quadTint_ = t;
    }
    for (size_t i = 0; i < poses.size(); i++) {
        if (all || cache_.changed(i)) hasQuad_[i] = makeQuad(poses[i], atlases, place, t, quads_[i]);
        if (hasQuad_[i]) ren->drawSprite(quads_[i]);
    }
}

}  // namespace toms::anim

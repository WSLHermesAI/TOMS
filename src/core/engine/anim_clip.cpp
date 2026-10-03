// anim_clip.cpp -- see anim_clip.h.
#include "anim_clip.h"

#include <tweeny/easing.h>
#include <json.hpp>

#include <algorithm>
#include <cmath>

using nlohmann::json;

namespace toms::anim {

// ---- easing ------------------------------------------------------------------------------------

namespace {

using EaseFn = float (*)(float);
// In the order of tweeny::easing::enumerated, so Ease::kind indexes both tables.
#define TOMS_EASE(name) [](float p) { return tweeny::easing::name.run(p, 0.0f, 1.0f); }
const EaseFn kEaseFns[] = {
    TOMS_EASE(linear),            // def: Tweeny's default is linear for floats
    TOMS_EASE(linear),
    [](float) { return 0.0f; },   // stepped: hold the key's value until the next key
    TOMS_EASE(quadraticIn),  TOMS_EASE(quadraticOut),  TOMS_EASE(quadraticInOut),
    TOMS_EASE(cubicIn),      TOMS_EASE(cubicOut),      TOMS_EASE(cubicInOut),
    TOMS_EASE(quarticIn),    TOMS_EASE(quarticOut),    TOMS_EASE(quarticInOut),
    TOMS_EASE(quinticIn),    TOMS_EASE(quinticOut),    TOMS_EASE(quinticInOut),
    TOMS_EASE(sinusoidalIn), TOMS_EASE(sinusoidalOut), TOMS_EASE(sinusoidalInOut),
    TOMS_EASE(exponentialIn), TOMS_EASE(exponentialOut), TOMS_EASE(exponentialInOut),
    TOMS_EASE(circularIn),   TOMS_EASE(circularOut),   TOMS_EASE(circularInOut),
    TOMS_EASE(bounceIn),     TOMS_EASE(bounceOut),     TOMS_EASE(bounceInOut),
    TOMS_EASE(elasticIn),    TOMS_EASE(elasticOut),    TOMS_EASE(elasticInOut),
    TOMS_EASE(backIn),       TOMS_EASE(backOut),       TOMS_EASE(backInOut),
};
#undef TOMS_EASE
constexpr int kEaseCount = (int)(sizeof(kEaseFns) / sizeof(kEaseFns[0]));
static_assert(kEaseCount == (int)tweeny::easing::enumerated::backInOut + 1, "easing table out of step with Tweeny");

// cubic-bezier(x1, y1, x2, y2) with P0 = (0,0), P3 = (1,1): find s with x(s) = p, return y(s).
float bezierEase(const float b[4], float p) {
    auto coord = [](float a1, float a2, float s) {   // one coordinate of the curve at s
        const float u = 1 - s;
        return 3 * u * u * s * a1 + 3 * u * s * s * a2 + s * s * s;
    };
    float lo = 0, hi = 1, s = p;
    for (int i = 0; i < 8; i++) {   // Newton, falling back to bisection when it stalls or leaves [0,1]
        const float x = coord(b[0], b[2], s) - p;
        if (std::fabs(x) < 1e-6f) return coord(b[1], b[3], s);
        const float u = 1 - s;
        const float dx = 3 * u * u * b[0] + 6 * u * s * (b[2] - b[0]) + 3 * s * s * (1 - b[2]);
        if (std::fabs(dx) < 1e-6f) break;
        const float next = s - x / dx;
        if (next < 0 || next > 1) break;
        s = next;
    }
    for (int i = 0; i < 40; i++) {
        s = (lo + hi) / 2;
        if (coord(b[0], b[2], s) < p) lo = s; else hi = s;
    }
    return coord(b[1], b[3], (lo + hi) / 2);
}

}  // namespace

const std::vector<std::string>& easeNames() {
    static const std::vector<std::string> names = {
        "def", "linear", "stepped",
        "quadraticIn", "quadraticOut", "quadraticInOut", "cubicIn", "cubicOut", "cubicInOut",
        "quarticIn", "quarticOut", "quarticInOut", "quinticIn", "quinticOut", "quinticInOut",
        "sinusoidalIn", "sinusoidalOut", "sinusoidalInOut", "exponentialIn", "exponentialOut", "exponentialInOut",
        "circularIn", "circularOut", "circularInOut", "bounceIn", "bounceOut", "bounceInOut",
        "elasticIn", "elasticOut", "elasticInOut", "backIn", "backOut", "backInOut"};
    return names;
}

float Ease::apply(float p) const {
    p = std::min(1.0f, std::max(0.0f, p));
    if (kind == kBezier) return bezierEase(bezier, p);
    if (kind < 0 || kind >= kEaseCount) return p;
    return kEaseFns[kind](p);
}

bool parseEase(const std::string& name, Ease& out) {
    const auto& n = easeNames();
    for (size_t i = 0; i < n.size(); i++)
        if (n[i] == name) { out.kind = (int)i; return true; }
    return false;
}

std::string easeName(const Ease& e) {
    if (e.kind == Ease::kBezier || e.kind < 0 || e.kind >= (int)easeNames().size()) return std::string();
    return easeNames()[(size_t)e.kind];
}

// ---- sampling ----------------------------------------------------------------------------------

namespace {

template <class T> T lerpValue(const T& a, const T& b, float f) { return a + (b - a) * f; }

// Interpolated channel: the key segment around t, eased by the key that starts it.
template <class T> T sample(const std::vector<Key<T>>& keys, float t, const T& rest) {
    if (keys.empty()) return rest;
    if (t <= keys.front().t) return keys.front().v;
    if (t >= keys.back().t) return keys.back().v;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Key<T>& k) { return x < k.t; });
    const Key<T>& b = *it;
    const Key<T>& a = *(it - 1);
    const float span = b.t - a.t;
    if (span <= 0 || a.ease.isStepped()) return a.v;
    return lerpValue(a.v, b.v, a.ease.apply((t - a.t) / span));
}

// Stepped channel: the last key at or before t (the first key before it starts).
template <class T> T sampleStep(const std::vector<Key<T>>& keys, float t, const T& rest) {
    if (keys.empty()) return rest;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Key<T>& k) { return x < k.t; });
    return it == keys.begin() ? keys.front().v : (it - 1)->v;
}

template <class T> float lastTime(const std::vector<Key<T>>& keys) { return keys.empty() ? 0.0f : keys.back().t; }

}  // namespace

glm::vec2 positionAt(const Node& n, float t) { return sample(n.posKeys, t, n.pos); }
float rotationAt(const Node& n, float t) { return sample(n.rotKeys, t, n.rot); }
glm::vec2 scaleAt(const Node& n, float t) { return sample(n.scaleKeys, t, n.scale); }
glm::vec4 colorAt(const Node& n, float t) { return glm::clamp(sample(n.colorKeys, t, n.color), 0.0f, 1.0f); }
std::string spriteAt(const Node& n, float t) { return sampleStep(n.spriteKeys, t, n.sprite); }
bool visibleAt(const Node& n, float t) { return sampleStep(n.visibleKeys, t, n.visible); }

float Node::lastKeyTime() const {
    float m = std::max({lastTime(posKeys), lastTime(scaleKeys), lastTime(rotKeys), lastTime(colorKeys),
                        lastTime(spriteKeys), lastTime(eventKeys), lastTime(visibleKeys)});
    for (const Node& c : children) m = std::max(m, c.lastKeyTime());
    return m;
}

float Clip::duration() const { return length > 0 ? length : root.lastKeyTime(); }

std::string defaultAtlasId(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (name.size() > 6 && name.compare(name.size() - 6, 6, ".atlas") == 0) name.resize(name.size() - 6);
    return name;
}

SpriteRef parseSpriteRef(const std::string& ref) {
    const size_t colon = ref.find(':');
    if (colon == std::string::npos) return {std::string(), ref};
    return {ref.substr(0, colon), ref.substr(colon + 1)};
}

std::string spriteRef(const std::string& atlasId, const std::string& name) {
    return atlasId.empty() ? name : atlasId + ":" + name;
}

const Clip* AnimFile::find(const std::string& n) const {
    for (const Clip& c : clips)
        if (c.name == n) return &c;
    return nullptr;
}

// ---- evaluation --------------------------------------------------------------------------------

namespace {

glm::mat3 compose(const glm::vec2& p, float rotDegrees, const glm::vec2& s) {
    const float r = glm::radians(rotDegrees);
    const float c = std::cos(r), sn = std::sin(r);
    // Column-major: T * R * S. With y down, a positive angle turns clockwise on screen.
    glm::mat3 m(1.0f);
    m[0] = glm::vec3(c * s.x, sn * s.x, 0);
    m[1] = glm::vec3(-sn * s.y, c * s.y, 0);
    m[2] = glm::vec3(p.x, p.y, 1);
    return m;
}

}  // namespace

glm::mat3 localTransform(const Node& n, float t) { return compose(positionAt(n, t), rotationAt(n, t), scaleAt(n, t)); }

namespace {

void walk(const Node& n, float t, const glm::mat3& parentWorld, const glm::vec4& parentColor, bool parentVisible,
          int depth, std::vector<NodePose>& out) {
    NodePose me;
    me.node = &n;
    me.depth = depth;
    me.world = parentWorld * localTransform(n, t);
    const glm::vec4 own = colorAt(n, t);
    me.color = n.inheritColor ? parentColor * own : own;
    me.visible = parentVisible && visibleAt(n, t);
    me.sprite = spriteAt(n, t);
    // Children in order (stable: equal orders keep their list order).
    std::vector<const Node*> kids;
    for (const Node& c : n.children) kids.push_back(&c);
    std::stable_sort(kids.begin(), kids.end(), [](const Node* a, const Node* b) { return a->order < b->order; });
    size_t k = 0;
    for (; k < kids.size() && kids[k]->order < 0; k++) walk(*kids[k], t, me.world, me.color, me.visible, depth + 1, out);
    out.push_back(me);
    for (; k < kids.size(); k++) walk(*kids[k], t, me.world, me.color, me.visible, depth + 1, out);
}

void collectEvents(const Node& n, float t0, float t1, std::vector<std::pair<float, std::string>>& out) {
    for (const Key<std::string>& k : n.eventKeys)
        if (k.t > t0 && k.t <= t1) out.emplace_back(k.t, k.v);
    for (const Node& c : n.children) collectEvents(c, t0, t1, out);
}

}  // namespace

void evaluate(const Clip& c, float t, std::vector<NodePose>& out) {
    out.clear();
    walk(c.root, t, glm::mat3(1.0f), glm::vec4(1.0f), true, 0, out);
}

// ---- PoseCache ---------------------------------------------------------------------------------

namespace {

// The last key at or before t (-1: before the first), starting from the previous answer: playback
// moves forward a little each frame, so it is nearly always the same key or the next one.
template <class T> int locate(const std::vector<Key<T>>& keys, float t, int hint) {
    const int n = (int)keys.size();
    auto fits = [&](int i) { return (i < 0 || keys[(size_t)i].t <= t) && (i + 1 >= n || t < keys[(size_t)i + 1].t); };
    if (hint >= -1 && hint < n && fits(hint)) return hint;
    if (hint >= -1 && hint + 1 < n && fits(hint + 1)) return hint + 1;
    return int(std::upper_bound(keys.begin(), keys.end(), t, [](float x, const Key<T>& k) { return x < k.t; }) -
               keys.begin()) - 1;
}

// Interpolated track: the key whose value it holds at t (before the first key, after the last, a
// stepped or empty segment), or -1 while it moves between two keys. Same cases as sample().
template <class T> int heldKey(const std::vector<Key<T>>& keys, float t, int i) {
    if (t <= keys.front().t) return 0;
    if (t >= keys.back().t) return (int)keys.size() - 1;
    const Key<T>& a = keys[(size_t)i];
    return a.ease.isStepped() || keys[(size_t)i + 1].t - a.t <= 0 ? i : -1;
}

// Moves an interpolated track to t. True when its value changed.
template <class T> bool advance(const std::vector<Key<T>>& keys, float t, int& cursor, int& held, T& value, bool force) {
    if (keys.empty()) return false;
    cursor = locate(keys, t, cursor);
    const int h = heldKey(keys, t, cursor);
    if (!force && h >= 0 && h == held) return false;   // still holding the same key: nothing to do
    held = h;
    T v;
    if (h >= 0) {
        v = keys[(size_t)h].v;
    } else {
        const Key<T>& a = keys[(size_t)cursor];
        const Key<T>& b = keys[(size_t)cursor + 1];
        v = lerpValue(a.v, b.v, a.ease.apply((t - a.t) / (b.t - a.t)));
    }
    if (!force && v == value) return false;
    value = v;
    return true;
}

// Moves a stepped track to t. True when its value changed.
template <class T> bool advanceStep(const std::vector<Key<T>>& keys, float t, int& cursor, int& held, T& value, bool force) {
    if (keys.empty()) return false;
    cursor = locate(keys, t, cursor);
    const int h = std::max(cursor, 0);
    if (!force && h == held) return false;
    held = h;
    if (!force && keys[(size_t)h].v == value) return false;
    value = keys[(size_t)h].v;
    return true;
}

}  // namespace

void PoseCache::bind(const Clip* clip) {
    clip_ = clip;
    items_.clear();
    poses_.clear();
    animated_ = 0;
    changedCount_ = 0;
    fresh_ = true;
    if (clip) addItems(clip->root, -1, 0);
    changed_.assign(poses_.size(), 1);
}

int PoseCache::addItems(const Node& n, int parent, int depth) {
    const int me = (int)items_.size();
    Item it;
    it.node = &n;
    it.parent = parent;
    it.animated = !(n.posKeys.empty() && n.rotKeys.empty() && n.scaleKeys.empty() && n.colorKeys.empty() &&
                    n.spriteKeys.empty() && n.visibleKeys.empty());
    for (int i = 0; i < kTracks; i++) {
        it.cursor[i] = -1;
        it.held[i] = -2;
    }
    it.pos = n.pos;
    it.rot = n.rot;
    it.scale = n.scale;
    it.color = n.color;
    it.visible = n.visible;
    animated_ += it.animated;
    items_.push_back(it);   // parents before their children: seek() updates in this order
    // Draw order as walk(): children with order < 0, the node itself, the rest.
    std::vector<const Node*> kids;
    for (const Node& c : n.children) kids.push_back(&c);
    std::stable_sort(kids.begin(), kids.end(), [](const Node* a, const Node* b) { return a->order < b->order; });
    size_t k = 0;
    for (; k < kids.size() && kids[k]->order < 0; k++) addItems(*kids[k], me, depth + 1);
    items_[(size_t)me].pose = poses_.size();
    NodePose p;
    p.node = &n;
    p.depth = depth;
    p.sprite = n.sprite;
    poses_.push_back(p);
    for (; k < kids.size(); k++) addItems(*kids[k], me, depth + 1);
    return me;
}

void PoseCache::seek(float t) {
    changedCount_ = 0;
    if (!clip_) return;
    const bool all = fresh_;
    fresh_ = false;
    for (Item& it : items_) {
        NodePose& p = poses_[it.pose];
        bool local = all, colour = all, vis = all, sprite = false;
        if (it.animated) {   // only the tracks that have keys; a node without keys skips all of this
            const Node& n = *it.node;
            local |= advance(n.posKeys, t, it.cursor[kPos], it.held[kPos], it.pos, all);
            local |= advance(n.rotKeys, t, it.cursor[kRot], it.held[kRot], it.rot, all);
            local |= advance(n.scaleKeys, t, it.cursor[kScale], it.held[kScale], it.scale, all);
            colour |= advance(n.colorKeys, t, it.cursor[kColor], it.held[kColor], it.color, all);
            vis |= advanceStep(n.visibleKeys, t, it.cursor[kVisible], it.held[kVisible], it.visible, all);
            sprite = advanceStep(n.spriteKeys, t, it.cursor[kSprite], it.held[kSprite], p.sprite, all);
        }
        const Item* parent = it.parent >= 0 ? &items_[(size_t)it.parent] : nullptr;
        const NodePose* pp = parent ? &poses_[parent->pose] : nullptr;
        if (local) it.local = compose(it.pos, it.rot, it.scale);
        it.worldChanged = local || (parent && parent->worldChanged);
        if (it.worldChanged) p.world = (pp ? pp->world : glm::mat3(1.0f)) * it.local;
        it.colorChanged = colour || (parent && parent->colorChanged && it.node->inheritColor);
        if (it.colorChanged) {
            const glm::vec4 own = glm::clamp(it.color, 0.0f, 1.0f);
            p.color = it.node->inheritColor ? (pp ? pp->color : glm::vec4(1.0f)) * own : own;
        }
        it.visibleChanged = vis || (parent && parent->visibleChanged);
        if (it.visibleChanged) p.visible = (pp ? pp->visible : true) && it.visible;
        const bool changed = it.worldChanged || it.colorChanged || it.visibleChanged || sprite;
        changed_[it.pose] = changed;
        changedCount_ += changed;
    }
}

void eventsBetween(const Clip& c, float t0, float t1, std::vector<std::string>& out) {
    std::vector<std::pair<float, std::string>> found;
    collectEvents(c.root, t0, t1, found);
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : found) out.push_back(std::move(f.second));
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

json vec(const glm::vec2& v) { return json::array({v.x, v.y}); }
json vec(const glm::vec4& v) { return json::array({v.x, v.y, v.z, v.w}); }
json easeToJson(const Ease& e) {
    if (e.kind == Ease::kBezier) return json::array({e.bezier[0], e.bezier[1], e.bezier[2], e.bezier[3]});
    return easeName(e);
}

template <class T, class F> json keysToJson(const std::vector<Key<T>>& keys, F value, bool eased) {
    json a = json::array();
    for (const Key<T>& k : keys) {
        json o{{"t", k.t}, {"v", value(k.v)}};
        if (eased && !k.ease.isLinear()) o["ease"] = easeToJson(k.ease);
        a.push_back(o);
    }
    return a;
}

json nodeToJson(const Node& n) {
    json j;
    j["name"] = n.name;
    if (!n.sprite.empty()) j["sprite"] = n.sprite;
    if (n.pos != glm::vec2(0)) j["pos"] = vec(n.pos);
    if (n.rot != 0) j["rot"] = n.rot;
    if (n.scale != glm::vec2(1)) j["scale"] = vec(n.scale);
    if (n.color != glm::vec4(1)) j["color"] = vec(n.color);
    if (!n.visible) j["visible"] = false;
    if (n.hasPivot) j["pivot"] = vec(n.pivot);
    if (n.order != 0) j["order"] = n.order;
    if (n.blend == Blend::Add) j["blend"] = "add";
    if (!n.inheritColor) j["inheritColor"] = false;
    json tr = json::object();
    auto v2 = [](const glm::vec2& v) { return vec(v); };
    auto v4 = [](const glm::vec4& v) { return vec(v); };
    auto f = [](float v) { return json(v); };
    auto s = [](const std::string& v) { return json(v); };
    auto b = [](bool v) { return json(v); };
    if (!n.posKeys.empty()) tr["pos"] = keysToJson(n.posKeys, v2, true);
    if (!n.rotKeys.empty()) tr["rot"] = keysToJson(n.rotKeys, f, true);
    if (!n.scaleKeys.empty()) tr["scale"] = keysToJson(n.scaleKeys, v2, true);
    if (!n.colorKeys.empty()) tr["color"] = keysToJson(n.colorKeys, v4, true);
    if (!n.spriteKeys.empty()) tr["sprite"] = keysToJson(n.spriteKeys, s, false);
    if (!n.visibleKeys.empty()) tr["visible"] = keysToJson(n.visibleKeys, b, false);
    if (!n.eventKeys.empty()) tr["event"] = keysToJson(n.eventKeys, s, false);
    if (!tr.empty()) j["tracks"] = tr;
    if (!n.children.empty()) {
        j["children"] = json::array();
        for (const Node& c : n.children) j["children"].push_back(nodeToJson(c));
    }
    return j;
}

glm::vec2 toVec2(const json& j) { return {j.at(0).get<float>(), j.at(1).get<float>()}; }
glm::vec4 toVec4(const json& j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()}; }

void easeFromJson(const json& j, Ease& e, const std::string& where) {
    if (j.is_string()) {
        if (!parseEase(j.get<std::string>(), e)) throw std::runtime_error(where + ": unknown ease '" + j.get<std::string>() + "'");
    } else if (j.is_array() && j.size() == 4) {
        e.kind = Ease::kBezier;
        for (int i = 0; i < 4; i++) e.bezier[i] = j[i].get<float>();
    } else {
        throw std::runtime_error(where + ": ease must be a name or [x1, y1, x2, y2]");
    }
}

template <class T, class F>
void keysFromJson(const json& tracks, const char* channel, std::vector<Key<T>>& out, F value, const std::string& where) {
    if (!tracks.contains(channel)) return;
    for (const json& k : tracks[channel]) {
        Key<T> key;
        key.t = k.at("t").get<float>();
        key.v = value(k.at("v"));
        if (k.contains("ease")) easeFromJson(k["ease"], key.ease, where + "." + channel);
        out.push_back(key);
    }
    std::stable_sort(out.begin(), out.end(), [](const Key<T>& a, const Key<T>& b) { return a.t < b.t; });
}

void nodeFromJson(const json& j, Node& n, const std::string& where) {
    n.name = j.value("name", std::string());
    const std::string here = where + "/" + n.name;
    n.sprite = j.value("sprite", std::string());
    if (j.contains("pos")) n.pos = toVec2(j["pos"]);
    n.rot = j.value("rot", 0.0f);
    if (j.contains("scale")) n.scale = toVec2(j["scale"]);
    if (j.contains("color")) n.color = toVec4(j["color"]);
    n.visible = j.value("visible", true);
    if (j.contains("pivot")) { n.hasPivot = true; n.pivot = toVec2(j["pivot"]); }
    n.order = j.value("order", 0);
    const std::string blend = j.value("blend", std::string("normal"));
    if (blend != "normal" && blend != "add") throw std::runtime_error(here + ": blend must be normal or add");
    n.blend = blend == "add" ? Blend::Add : Blend::Normal;
    n.inheritColor = j.value("inheritColor", true);
    if (j.contains("tracks")) {
        const json& tr = j["tracks"];
        keysFromJson(tr, "pos", n.posKeys, toVec2, here);
        keysFromJson(tr, "rot", n.rotKeys, [](const json& v) { return v.get<float>(); }, here);
        keysFromJson(tr, "scale", n.scaleKeys, toVec2, here);
        keysFromJson(tr, "color", n.colorKeys, toVec4, here);
        keysFromJson(tr, "sprite", n.spriteKeys, [](const json& v) { return v.get<std::string>(); }, here);
        keysFromJson(tr, "visible", n.visibleKeys, [](const json& v) { return v.get<bool>(); }, here);
        keysFromJson(tr, "event", n.eventKeys, [](const json& v) { return v.get<std::string>(); }, here);
    }
    for (const json& c : j.value("children", json::array())) {
        n.children.emplace_back();
        nodeFromJson(c, n.children.back(), here);
    }
}

}  // namespace

bool parseAnim(const std::string& text, AnimFile& out, std::string* err) {
    try {
        const json j = json::parse(text);
        AnimFile f;
        f.version = j.value("version", 1);
        if (j.contains("atlases")) {
            for (const json& a : j["atlases"]) {
                AtlasRef r;
                if (a.is_string()) r.path = a.get<std::string>();
                else { r.path = a.at("path").get<std::string>(); r.id = a.value("id", std::string()); }
                if (r.id.empty()) r.id = defaultAtlasId(r.path);
                for (const AtlasRef& o : f.atlases)
                    if (o.id == r.id) throw std::runtime_error("two atlases have the id '" + r.id + "': give one an \"id\"");
                f.atlases.push_back(r);
            }
        } else if (j.contains("atlas")) {   // older files: one atlas
            const std::string path = j["atlas"].get<std::string>();
            f.atlases.push_back({defaultAtlasId(path), path});
        }
        for (const json& c : j.value("clips", json::array())) {
            Clip clip;
            clip.name = c.value("name", std::string());
            clip.length = c.value("length", 0.0f);
            clip.playCount = c.value("playCount", 1);
            clip.stayAtLastFrame = c.value("stayAtLastFrame", true);
            if (c.contains("root")) nodeFromJson(c["root"], clip.root, clip.name);
            f.clips.push_back(std::move(clip));
        }
        out = std::move(f);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

std::string animToJson(const AnimFile& f) {
    json j;
    j["version"] = f.version;
    if (!f.atlases.empty()) {
        json a = json::array();
        for (const AtlasRef& r : f.atlases) {   // the short form when the id is the default one
            if (r.id == defaultAtlasId(r.path)) a.push_back(r.path);
            else a.push_back({{"id", r.id}, {"path", r.path}});
        }
        j["atlases"] = a;
    }
    j["clips"] = json::array();
    for (const Clip& c : f.clips) {
        json cj{{"name", c.name}};
        if (c.length > 0) cj["length"] = c.length;
        if (c.playCount != 1) cj["playCount"] = c.playCount;
        if (!c.stayAtLastFrame) cj["stayAtLastFrame"] = false;
        cj["root"] = nodeToJson(c.root);
        j["clips"].push_back(cj);
    }
    return j.dump(2) + "\n";
}

}  // namespace toms::anim

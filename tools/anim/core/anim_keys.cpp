// anim_keys.cpp -- see anim_keys.h.
#include "anim_keys.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace animed {

using toms::anim::Key;

namespace {

// Calls f(keys) with the channel's key vector (const or not, following NodeT).
template <class NodeT, class F> auto visit(NodeT& n, Channel c, F&& f) {
    switch (c) {
    case Channel::Pos: return f(n.posKeys);
    case Channel::Rot: return f(n.rotKeys);
    case Channel::Scale: return f(n.scaleKeys);
    case Channel::Color: return f(n.colorKeys);
    case Channel::Sprite: return f(n.spriteKeys);
    case Channel::Visible: return f(n.visibleKeys);
    case Channel::Event: break;
    }
    return f(n.eventKeys);
}

template <class T> struct Conv;
template <> struct Conv<glm::vec2> {
    static glm::vec2 from(const Value& v) { return {v.v.x, v.v.y}; }
    static Value to(const glm::vec2& x) { Value v; v.v = {x.x, x.y, 0, 0}; return v; }
};
template <> struct Conv<float> {
    static float from(const Value& v) { return v.v.x; }
    static Value to(float x) { Value v; v.v.x = x; return v; }
};
template <> struct Conv<glm::vec4> {
    static glm::vec4 from(const Value& v) { return v.v; }
    static Value to(const glm::vec4& x) { Value v; v.v = x; return v; }
};
template <> struct Conv<std::string> {
    static std::string from(const Value& v) { return v.s; }
    static Value to(const std::string& x) { Value v; v.s = x; return v; }
};
template <> struct Conv<bool> {
    static bool from(const Value& v) { return v.b; }
    static Value to(bool x) { Value v; v.b = x; return v; }
};

template <class T> int findKey(const std::vector<Key<T>>& keys, float t) {
    for (size_t i = 0; i < keys.size(); i++)
        if (sameTime(keys[i].t, t)) return (int)i;
    return -1;
}

template <class T> void sortKeys(std::vector<Key<T>>& keys) {
    std::stable_sort(keys.begin(), keys.end(), [](const Key<T>& a, const Key<T>& b) { return a.t < b.t; });
}

std::string num(float f) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", f);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// "1, 2" / "1 2" / "(1; 2)" -> numbers.
bool parseNumbers(const std::string& text, std::vector<float>& out) {
    std::string t;
    for (char ch : text) t += (ch == ',' || ch == ';' || ch == '(' || ch == ')' || ch == '[' || ch == ']') ? ' ' : ch;
    std::istringstream in(t);
    std::string tok;
    out.clear();
    while (in >> tok) {
        char* end = nullptr;
        const float f = std::strtof(tok.c_str(), &end);
        if (!end || *end != '\0' || !std::isfinite(f)) return false;
        out.push_back(f);
    }
    return true;
}

}  // namespace

bool isInterpolated(Channel c) { return c == Channel::Pos || c == Channel::Rot || c == Channel::Scale || c == Channel::Color; }

const char* channelName(Channel c) {
    switch (c) {
    case Channel::Pos: return "pos";
    case Channel::Rot: return "rot";
    case Channel::Scale: return "scale";
    case Channel::Color: return "color";
    case Channel::Sprite: return "sprite";
    case Channel::Visible: return "visible";
    case Channel::Event: break;
    }
    return "event";
}

int keyCount(const Node& n, Channel c) {
    return visit(n, c, [](const auto& keys) { return (int)keys.size(); });
}

std::vector<float> keyTimes(const Node& n, Channel c) {
    return visit(n, c, [](const auto& keys) {
        std::vector<float> out;
        for (const auto& k : keys) out.push_back(k.t);
        return out;
    });
}

std::vector<float> unionKeyTimes(const Node& n) {
    std::vector<float> all;
    for (Channel c : kAllChannels) {
        const std::vector<float> t = keyTimes(n, c);
        all.insert(all.end(), t.begin(), t.end());
    }
    std::sort(all.begin(), all.end());
    std::vector<float> out;
    for (float t : all)
        if (out.empty() || !sameTime(out.back(), t)) out.push_back(t);
    return out;
}

bool hasKeyAt(const Node& n, Channel c, float t) {
    return visit(n, c, [t](const auto& keys) { return findKey(keys, t) >= 0; });
}

bool keyAt(const Node& n, Channel c, float t, Value& out) {
    return visit(n, c, [&](const auto& keys) {
        using T = decltype(keys.front().v);
        const int i = findKey(keys, t);
        if (i < 0) return false;
        out = Conv<std::decay_t<T>>::to(keys[(size_t)i].v);
        return true;
    });
}

Value restValue(const Node& n, Channel c) {
    switch (c) {
    case Channel::Pos: return Conv<glm::vec2>::to(n.pos);
    case Channel::Rot: return Conv<float>::to(n.rot);
    case Channel::Scale: return Conv<glm::vec2>::to(n.scale);
    case Channel::Color: return Conv<glm::vec4>::to(n.color);
    case Channel::Sprite: return Conv<std::string>::to(n.sprite);
    case Channel::Visible: return Conv<bool>::to(n.visible);
    case Channel::Event: break;
    }
    return Value();
}

Value valueAt(const Node& n, Channel c, float t) {
    using namespace toms::anim;
    switch (c) {
    case Channel::Pos: return Conv<glm::vec2>::to(positionAt(n, t));
    case Channel::Rot: return Conv<float>::to(rotationAt(n, t));
    case Channel::Scale: return Conv<glm::vec2>::to(scaleAt(n, t));
    case Channel::Color: {
        // Unclamped would be the raw track; the runtime clamps, and so does what the user sees.
        return Conv<glm::vec4>::to(colorAt(n, t));
    }
    case Channel::Sprite: return Conv<std::string>::to(spriteAt(n, t));
    case Channel::Visible: return Conv<bool>::to(visibleAt(n, t));
    case Channel::Event: break;
    }
    Value v;
    keyAt(n, c, t, v);
    return v;
}

bool easeAt(const Node& n, Channel c, float t, Ease& out) {
    if (!isInterpolated(c)) return false;
    return visit(n, c, [&](const auto& keys) {
        const int i = findKey(keys, t);
        if (i < 0) return false;
        out = keys[(size_t)i].ease;
        return true;
    });
}

bool easeAtOrBefore(const Node& n, Channel c, float t, Ease& out, float* keyTime) {
    if (!isInterpolated(c)) return false;
    return visit(n, c, [&](const auto& keys) {
        int best = -1;
        for (size_t i = 0; i < keys.size(); i++)
            if (keys[i].t <= t + kTimeEps) best = (int)i;
        if (best < 0) return false;
        out = keys[(size_t)best].ease;
        if (keyTime) *keyTime = keys[(size_t)best].t;
        return true;
    });
}

int overlappingKeys(const Node& n, Channel c) {
    return visit(n, c, [](const auto& keys) {
        int count = 0;
        for (size_t i = 1; i < keys.size(); i++)
            if (sameTime(keys[i - 1].t, keys[i].t)) count++;
        return count;
    });
}

void setKey(Node& n, Channel c, float t, const Value& v) {
    visit(n, c, [&](auto& keys) {
        using T = std::decay_t<decltype(keys.front().v)>;
        const int i = findKey(keys, t);
        if (i >= 0) {
            keys[(size_t)i].v = Conv<T>::from(v);
            return 0;
        }
        Key<T> k;
        k.t = std::max(0.0f, t);
        k.v = Conv<T>::from(v);
        // A key dropped inside a segment keeps that segment's easing on both halves.
        for (const auto& other : keys)
            if (other.t < t) k.ease = other.ease;
        keys.push_back(k);
        sortKeys(keys);
        return 0;
    });
}

bool removeKey(Node& n, Channel c, float t) {
    return visit(n, c, [&](auto& keys) {
        const int i = findKey(keys, t);
        if (i < 0) return false;
        keys.erase(keys.begin() + i);
        return true;
    });
}

void setRest(Node& n, Channel c, const Value& v) {
    switch (c) {
    case Channel::Pos: n.pos = Conv<glm::vec2>::from(v); break;
    case Channel::Rot: n.rot = v.v.x; break;
    case Channel::Scale: n.scale = Conv<glm::vec2>::from(v); break;
    case Channel::Color: n.color = v.v; break;
    case Channel::Sprite: n.sprite = v.s; break;
    case Channel::Visible: n.visible = v.b; break;
    case Channel::Event: break;
    }
}

bool setEase(Node& n, Channel c, float t, const Ease& e) {
    if (!isInterpolated(c)) return false;
    return visit(n, c, [&](auto& keys) {
        const int i = findKey(keys, t);
        if (i < 0) return false;
        keys[(size_t)i].ease = e;
        return true;
    });
}

bool moveKeyTimes(Node& n, const std::vector<float>& from, const std::vector<float>& to, std::string* conflict) {
    if (from.size() != to.size()) return false;
    for (float t : to)
        if (t < -kTimeEps) {
            if (conflict) *conflict = "time " + num(t) + " is negative";
            return false;
        }
    Node work = n;
    for (Channel c : kAllChannels) {
        const bool ok = visit(work, c, [&](auto& keys) {
            std::vector<bool> moved(keys.size(), false);
            for (size_t r = 0; r < from.size(); r++) {
                const int i = findKey(keys, from[r]);
                if (i >= 0 && !moved[(size_t)i]) {
                    keys[(size_t)i].t = std::max(0.0f, to[r]);
                    moved[(size_t)i] = true;
                }
            }
            for (size_t i = 0; i < keys.size(); i++)
                for (size_t j = i + 1; j < keys.size(); j++)
                    if ((moved[i] || moved[j]) && sameTime(keys[i].t, keys[j].t)) {
                        if (conflict) *conflict = std::string(channelName(c)) + " already has a key at " + num(keys[i].t);
                        return false;
                    }
            sortKeys(keys);
            return true;
        });
        if (!ok) return false;
    }
    n = std::move(work);
    return true;
}

void scaleKeyTimes(Node& n, float factor, bool recursive) {
    for (Channel c : kAllChannels)
        visit(n, c, [&](auto& keys) {
            for (auto& k : keys) k.t *= factor;
            sortKeys(keys);
            return 0;
        });
    if (recursive)
        for (Node& ch : n.children) scaleKeyTimes(ch, factor, true);
}

std::string formatValue(Channel c, const Value& v) {
    switch (c) {
    case Channel::Pos:
    case Channel::Scale: return num(v.v.x) + ", " + num(v.v.y);
    case Channel::Rot: return num(v.v.x);
    case Channel::Color: return num(v.v.x) + ", " + num(v.v.y) + ", " + num(v.v.z) + ", " + num(v.v.w);
    case Channel::Sprite: return v.s.empty() ? std::string("(none)") : v.s;
    case Channel::Visible: return v.b ? "true" : "false";
    case Channel::Event: break;
    }
    return v.s;
}

bool parseValue(Channel c, const std::string& text, Value& out, std::string* err) {
    const std::string t = trim(text);
    out = Value();
    std::vector<float> f;
    auto fail = [&](const std::string& m) {
        if (err) *err = m;
        return false;
    };
    switch (c) {
    case Channel::Pos:
    case Channel::Scale:
        if (!parseNumbers(t, f) || (f.size() != 1 && f.size() != 2)) return fail("expected two numbers: x, y");
        out.v = {f[0], f.size() == 2 ? f[1] : f[0], 0, 0};   // one number = both (uniform scale)
        return true;
    case Channel::Rot:
        if (!parseNumbers(t, f) || f.size() != 1) return fail("expected one number (degrees)");
        out.v.x = f[0];
        return true;
    case Channel::Color:
        if (!parseNumbers(t, f) || (f.size() != 3 && f.size() != 4)) return fail("expected r, g, b[, a] (0..1)");
        out.v = {f[0], f[1], f[2], f.size() == 4 ? f[3] : 1.0f};
        return true;
    case Channel::Sprite:
        out.s = t == "(none)" ? std::string() : t;
        return true;
    case Channel::Visible:
        if (t == "true" || t == "1" || t == "yes" || t == "on") out.b = true;
        else if (t == "false" || t == "0" || t == "no" || t == "off") out.b = false;
        else return fail("expected true or false");
        return true;
    case Channel::Event: break;
    }
    if (t.empty()) return fail("an event needs a name");
    out.s = t;
    return true;
}

std::string formatEase(const Ease& e) {
    if (e.kind == Ease::kBezier)
        return "bezier(" + num(e.bezier[0]) + ", " + num(e.bezier[1]) + ", " + num(e.bezier[2]) + ", " + num(e.bezier[3]) + ")";
    return toms::anim::easeName(e);
}

bool parseEaseText(const std::string& text, Ease& out) {
    const std::string t = trim(text);
    if (toms::anim::parseEase(t, out)) return true;
    std::string body = t;
    if (body.rfind("bezier", 0) == 0) body = body.substr(6);
    std::vector<float> f;
    if (parseNumbers(body, f) && f.size() == 4) {
        out.kind = Ease::kBezier;
        for (int i = 0; i < 4; i++) out.bezier[i] = f[(size_t)i];
        out.bezier[0] = std::min(1.0f, std::max(0.0f, out.bezier[0]));   // x must stay in 0..1
        out.bezier[2] = std::min(1.0f, std::max(0.0f, out.bezier[2]));
        return true;
    }
    return false;
}

const Node* nodeAt(const Node& root, const std::vector<int>& path) {
    const Node* n = &root;
    for (int i : path) {
        if (i < 0 || i >= (int)n->children.size()) return nullptr;
        n = &n->children[(size_t)i];
    }
    return n;
}

Node* nodeAt(Node& root, const std::vector<int>& path) {
    return const_cast<Node*>(nodeAt(static_cast<const Node&>(root), path));
}

bool findPath(const Node& root, const Node* target, std::vector<int>& path) {
    if (&root == target) return true;
    for (size_t i = 0; i < root.children.size(); i++) {
        path.push_back((int)i);
        if (findPath(root.children[i], target, path)) return true;
        path.pop_back();
    }
    return false;
}

void collectNames(const Node& root, std::vector<std::string>& out) {
    out.push_back(root.name);
    for (const Node& c : root.children) collectNames(c, out);
}

std::string uniqueNodeName(const Node& root, const std::string& base) {
    std::vector<std::string> names;
    collectNames(root, names);
    const std::set<std::string> taken(names.begin(), names.end());
    const std::string b = base.empty() ? std::string("node") : base;
    if (!taken.count(b)) return b;
    for (int i = 2;; i++) {
        const std::string s = b + "_" + std::to_string(i);
        if (!taken.count(s)) return s;
    }
}

void collectSpriteRefs(const Node& root, std::vector<std::string>& out) {
    if (!root.sprite.empty()) out.push_back(root.sprite);
    for (const auto& k : root.spriteKeys)
        if (!k.v.empty()) out.push_back(k.v);
    for (const Node& c : root.children) collectSpriteRefs(c, out);
}

int rewriteSpriteRefs(Node& root, const std::function<std::string(const std::string&)>& f) {
    int changed = 0;
    auto one = [&](std::string& s) {
        if (s.empty()) return;
        std::string r = f(s);
        if (r != s) {
            s = std::move(r);
            changed++;
        }
    };
    one(root.sprite);
    for (auto& k : root.spriteKeys) one(k.v);
    for (Node& c : root.children) changed += rewriteSpriteRefs(c, f);
    return changed;
}

}  // namespace animed

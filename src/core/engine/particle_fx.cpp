// particle_fx.cpp -- see particle_fx.h.
#include "particle_fx.h"

#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <stdexcept>

using nlohmann::json;

namespace toms::fx {

// ---- curves ------------------------------------------------------------------------------------

template <class T> T Curve<T>::at(float t, const T& none) const {
    if (keys.empty()) return none;
    if (t <= keys.front().t) return keys.front().v;
    if (t >= keys.back().t) return keys.back().v;
    auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float x, const CurveKey<T>& k) { return x < k.t; });
    const CurveKey<T>& b = *it;
    const CurveKey<T>& a = *(it - 1);
    const float span = b.t - a.t;
    if (span <= 0 || a.ease.isStepped()) return a.v;
    return a.v + (b.v - a.v) * a.ease.apply((t - a.t) / span);
}
template struct Curve<float>;
template struct Curve<glm::vec4>;

// ---- names -------------------------------------------------------------------------------------

namespace {

const char* const kFactorNames[Blend::FactorCount] = {"zero",     "one",         "srcColor", "invSrcColor", "srcAlpha",
                                                       "invSrcAlpha", "dstColor", "invDstColor", "dstAlpha", "invDstAlpha"};
const char* const kShapeNames[] = {"point", "line", "box", "circle", "ring"};

}  // namespace

const char* Blend::factorName(Factor f) { return f < FactorCount ? kFactorNames[f] : "?"; }

bool Blend::factorFromName(const std::string& name, Factor& out) {
    for (int i = 0; i < FactorCount; i++)
        if (name == kFactorNames[i]) { out = Factor(i); return true; }
    return false;
}

std::string Blend::presetName() const {
    if (*this == normal()) return "normal";
    if (*this == add()) return "add";
    if (*this == multiply()) return "multiply";
    if (*this == screen()) return "screen";
    return std::string();
}

bool Blend::fromPreset(const std::string& name, Blend& out) {
    if (name == "normal") out = normal();
    else if (name == "add") out = add();
    else if (name == "multiply") out = multiply();
    else if (name == "screen") out = screen();
    else return false;
    return true;
}

const char* shapeName(Shape::Type t) { return kShapeNames[t]; }

const char* simulationName(Emitter::Simulation s) {
    return s == Emitter::Simulation::Gpu ? "gpu" : s == Emitter::Simulation::Cpu ? "cpu" : "auto";
}

const Effect* ParticleFile::find(const std::string& name) const {
    for (const Effect& e : effects)
        if (e.name == name) return &e;
    return nullptr;
}

// ---- JSON --------------------------------------------------------------------------------------

namespace {

[[noreturn]] void fail(const std::string& where, const std::string& what) { throw std::runtime_error(where + ": " + what); }

// Unknown fields are errors: a typo ("colour") would otherwise silently do nothing.
void onlyKnown(const json& j, std::initializer_list<const char*> known, const std::string& where) {
    if (!j.is_object()) fail(where, "must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool ok = false;
        for (const char* k : known) ok |= it.key() == k;
        if (!ok) fail(where, "unknown field '" + it.key() + "'");
    }
}

float num(const json& j, const std::string& where) {
    if (!j.is_number()) fail(where, "must be a number");
    return j.get<float>();
}

Range rangeFrom(const json& j, const std::string& where) {
    if (j.is_number()) return Range(j.get<float>());
    if (j.is_array() && j.size() == 2 && j[0].is_number() && j[1].is_number()) return Range(j[0].get<float>(), j[1].get<float>());
    fail(where, "must be a number or [min, max]");
}

glm::vec2 vec2From(const json& j, const std::string& where) {
    if (!j.is_array() || j.size() != 2) fail(where, "must be [x, y]");
    return {num(j[0], where), num(j[1], where)};
}

glm::vec4 colorFrom(const json& j, const std::string& where) {
    if (!j.is_array() || j.size() != 4) fail(where, "must be [r, g, b, a] (0..1)");
    return {num(j[0], where), num(j[1], where), num(j[2], where), num(j[3], where)};
}

void easeFrom(const json& j, Ease& e, const std::string& where) {
    if (j.is_string()) {
        if (!toms::anim::parseEase(j.get<std::string>(), e)) fail(where, "unknown ease '" + j.get<std::string>() + "'");
    } else if (j.is_array() && j.size() == 4) {
        e.kind = Ease::kBezier;
        for (int i = 0; i < 4; i++) e.bezier[i] = num(j[i], where);
    } else {
        fail(where, "ease must be a name or [x1, y1, x2, y2]");
    }
}

template <class T, class F> void curveFrom(const json& j, Curve<T>& c, F value, const std::string& where) {
    if (!j.is_array()) fail(where, "must be a list of {\"t\", \"v\"} keys");
    for (const json& k : j) {
        onlyKnown(k, {"t", "v", "ease"}, where);
        if (!k.contains("t") || !k.contains("v")) fail(where, "a key needs \"t\" and \"v\"");
        CurveKey<T> key;
        key.t = num(k["t"], where);
        key.v = value(k["v"], where);
        if (k.contains("ease")) easeFrom(k["ease"], key.ease, where);
        c.keys.push_back(key);
    }
    std::stable_sort(c.keys.begin(), c.keys.end(), [](const CurveKey<T>& a, const CurveKey<T>& b) { return a.t < b.t; });
}

void emitterFrom(const json& j, Emitter& e, const std::string& effectName) {
    onlyKnown(j, {"name", "offset", "start", "stop", "space", "maxParticles", "emission", "shape", "life", "direction",
                  "spread", "speed", "size", "rotation", "spin", "color", "sprite", "overLife", "forces", "flipbook",
                  "render", "simulation"},
              effectName + "/?");
    e.name = j.value("name", std::string());
    const std::string w = effectName + "/" + e.name;
    if (j.contains("offset")) e.offset = vec2From(j["offset"], w + ".offset");
    if (j.contains("start")) e.start = num(j["start"], w + ".start");
    if (j.contains("stop")) e.stop = num(j["stop"], w + ".stop");
    if (j.contains("space")) {
        const std::string s = j["space"].is_string() ? j["space"].get<std::string>() : std::string();
        if (s != "world" && s != "local") fail(w + ".space", "must be \"world\" or \"local\"");
        e.localSpace = s == "local";
    }
    if (j.contains("maxParticles")) e.maxParticles = (int)num(j["maxParticles"], w + ".maxParticles");
    if (j.contains("simulation")) {
        const std::string s = j["simulation"].is_string() ? j["simulation"].get<std::string>() : std::string();
        if (s != "auto" && s != "cpu" && s != "gpu") fail(w + ".simulation", "must be \"auto\", \"cpu\" or \"gpu\"");
        e.simulation = s == "gpu" ? Emitter::Simulation::Gpu : s == "cpu" ? Emitter::Simulation::Cpu : Emitter::Simulation::Auto;
    }
    if (j.contains("emission")) {
        const json& em = j["emission"];
        onlyKnown(em, {"rate", "bursts"}, w + ".emission");
        if (em.contains("rate")) e.rate = num(em["rate"], w + ".emission.rate");
        for (const json& b : em.value("bursts", json::array())) {
            onlyKnown(b, {"t", "count", "repeat", "interval"}, w + ".emission.bursts");
            Burst burst;
            if (b.contains("t")) burst.t = num(b["t"], w + ".bursts.t");
            if (b.contains("count")) burst.count = rangeFrom(b["count"], w + ".bursts.count");
            if (b.contains("repeat")) burst.repeat = (int)num(b["repeat"], w + ".bursts.repeat");
            if (b.contains("interval")) burst.interval = num(b["interval"], w + ".bursts.interval");
            e.bursts.push_back(burst);
        }
    }
    if (j.contains("shape")) {
        const json& s = j["shape"];
        const std::string sw = w + ".shape";
        onlyKnown(s, {"type", "length", "angle", "size", "radius", "thickness", "arc", "edge", "outward"}, sw);
        const std::string type = s.value("type", std::string("point"));
        int t = -1;
        for (int i = 0; i < 5; i++)
            if (type == kShapeNames[i]) t = i;
        if (t < 0) fail(sw, "type must be point, line, box, circle or ring");
        e.shape.type = Shape::Type(t);
        if (s.contains("length")) e.shape.length = num(s["length"], sw);
        if (s.contains("angle")) e.shape.angle = num(s["angle"], sw);
        if (s.contains("size")) { const glm::vec2 v = vec2From(s["size"], sw + ".size"); e.shape.width = v.x; e.shape.height = v.y; }
        if (s.contains("radius")) e.shape.radius = num(s["radius"], sw);
        if (s.contains("thickness")) e.shape.thickness = num(s["thickness"], sw);
        if (s.contains("arc")) { const glm::vec2 v = vec2From(s["arc"], sw + ".arc"); e.shape.arcFrom = v.x; e.shape.arcTo = v.y; }
        if (s.contains("edge")) e.shape.edge = s["edge"].get<bool>();
        if (s.contains("outward")) e.shape.outward = s["outward"].get<bool>();
    }
    if (j.contains("life")) e.life = rangeFrom(j["life"], w + ".life");
    if (j.contains("direction")) e.direction = num(j["direction"], w + ".direction");
    if (j.contains("spread")) e.spread = num(j["spread"], w + ".spread");
    if (j.contains("speed")) e.speed = rangeFrom(j["speed"], w + ".speed");
    if (j.contains("size")) e.size = rangeFrom(j["size"], w + ".size");
    if (j.contains("rotation")) e.rotation = rangeFrom(j["rotation"], w + ".rotation");
    if (j.contains("spin")) e.spin = rangeFrom(j["spin"], w + ".spin");
    if (j.contains("color")) {   // one colour, or two to pick between
        const json& c = j["color"];
        if (c.is_array() && c.size() == 2 && c[0].is_array()) {
            e.color = colorFrom(c[0], w + ".color");
            e.color2 = colorFrom(c[1], w + ".color");
        } else {
            e.color = e.color2 = colorFrom(c, w + ".color");
        }
    }
    if (j.contains("sprite")) e.sprite = j["sprite"].get<std::string>();
    if (j.contains("overLife")) {
        const json& o = j["overLife"];
        onlyKnown(o, {"color", "size", "speed", "spin"}, w + ".overLife");
        auto f = [](const json& v, const std::string& where) { return num(v, where); };
        if (o.contains("color")) curveFrom(o["color"], e.colorOverLife, colorFrom, w + ".overLife.color");
        if (o.contains("size")) curveFrom(o["size"], e.sizeOverLife, f, w + ".overLife.size");
        if (o.contains("speed")) curveFrom(o["speed"], e.speedOverLife, f, w + ".overLife.speed");
        if (o.contains("spin")) curveFrom(o["spin"], e.spinOverLife, f, w + ".overLife.spin");
    }
    if (j.contains("forces")) {
        const json& f = j["forces"];
        onlyKnown(f, {"gravity", "drag", "radial", "tangential"}, w + ".forces");
        if (f.contains("gravity")) e.gravity = vec2From(f["gravity"], w + ".forces.gravity");
        if (f.contains("drag")) e.drag = num(f["drag"], w + ".forces.drag");
        if (f.contains("radial")) e.radial = rangeFrom(f["radial"], w + ".forces.radial");
        if (f.contains("tangential")) e.tangential = rangeFrom(f["tangential"], w + ".forces.tangential");
    }
    if (j.contains("flipbook")) {
        const json& f = j["flipbook"];
        onlyKnown(f, {"frames", "mode", "fps", "randomStart"}, w + ".flipbook");
        for (const json& fr : f.value("frames", json::array())) e.frames.push_back(fr.get<std::string>());
        const std::string mode = f.value("mode", std::string("overLife"));
        if (mode != "overLife" && mode != "fps") fail(w + ".flipbook.mode", "must be \"overLife\" or \"fps\"");
        e.framesByFps = mode == "fps";
        if (f.contains("fps")) e.fps = num(f["fps"], w + ".flipbook.fps");
        e.randomFrame = f.value("randomStart", false);
    }
    if (j.contains("render")) {
        const json& r = j["render"];
        onlyKnown(r, {"blend", "order", "alignToVelocity", "oldestOnTop"}, w + ".render");
        if (r.contains("blend")) {
            const json& b = r["blend"];
            if (b.is_string()) {
                if (!Blend::fromPreset(b.get<std::string>(), e.blend))
                    fail(w + ".render.blend", "unknown preset '" + b.get<std::string>() + "' (normal, add, multiply, screen)");
            } else {
                onlyKnown(b, {"src", "dst"}, w + ".render.blend");
                if (!b.contains("src") || !b.contains("dst") ||
                    !Blend::factorFromName(b["src"].get<std::string>(), e.blend.src) ||
                    !Blend::factorFromName(b["dst"].get<std::string>(), e.blend.dst))
                    fail(w + ".render.blend", "needs \"src\" and \"dst\" factors (zero, one, srcColor, invSrcColor, srcAlpha, "
                                              "invSrcAlpha, dstColor, invDstColor, dstAlpha, invDstAlpha)");
            }
        }
        if (r.contains("order")) e.order = (int)num(r["order"], w + ".render.order");
        e.alignToVelocity = r.value("alignToVelocity", false);
        e.oldestOnTop = r.value("oldestOnTop", false);
    }
}

json rangeJson(const Range& r) { return r.fixed() ? json(r.min) : json::array({r.min, r.max}); }
json vecJson(const glm::vec2& v) { return json::array({v.x, v.y}); }
json vecJson(const glm::vec4& v) { return json::array({v.x, v.y, v.z, v.w}); }
json vecOrNum(float v) { return json(v); }
json vecOrNum(const glm::vec4& v) { return vecJson(v); }

template <class T> json curveJson(const Curve<T>& c) {
    json a = json::array();
    for (const CurveKey<T>& k : c.keys) {
        json o{{"t", k.t}, {"v", vecOrNum(k.v)}};
        if (!k.ease.isLinear()) {
            if (k.ease.kind == Ease::kBezier)
                o["ease"] = json::array({k.ease.bezier[0], k.ease.bezier[1], k.ease.bezier[2], k.ease.bezier[3]});
            else
                o["ease"] = toms::anim::easeName(k.ease);
        }
        a.push_back(o);
    }
    return a;
}

json emitterJson(const Emitter& e) {
    const Emitter d;   // defaults are left out
    json j;
    j["name"] = e.name;
    if (e.offset != d.offset) j["offset"] = vecJson(e.offset);
    if (e.start != d.start) j["start"] = e.start;
    if (e.stop != d.stop) j["stop"] = e.stop;
    if (e.localSpace) j["space"] = "local";
    if (e.maxParticles != d.maxParticles) j["maxParticles"] = e.maxParticles;
    if (e.simulation != d.simulation) j["simulation"] = simulationName(e.simulation);
    json em = json::object();
    if (e.rate != d.rate) em["rate"] = e.rate;
    if (!e.bursts.empty()) {
        em["bursts"] = json::array();
        for (const Burst& b : e.bursts) {
            json bj{{"t", b.t}, {"count", rangeJson(b.count)}};
            if (b.repeat != 0) { bj["repeat"] = b.repeat; bj["interval"] = b.interval; }
            em["bursts"].push_back(bj);
        }
    }
    if (!em.empty()) j["emission"] = em;
    if (e.shape.type != Shape::Point || e.shape.outward) {
        const Shape& s = e.shape;
        json sj{{"type", kShapeNames[s.type]}};
        if (s.type == Shape::Line) { sj["length"] = s.length; if (s.angle != 0) sj["angle"] = s.angle; }
        if (s.type == Shape::Box) sj["size"] = json::array({s.width, s.height});
        if (s.type == Shape::Circle || s.type == Shape::Ring) {
            sj["radius"] = s.radius;
            if (s.type == Shape::Ring) sj["thickness"] = s.thickness;
            if (s.arcFrom != 0 || s.arcTo != 360) sj["arc"] = json::array({s.arcFrom, s.arcTo});
        }
        if (s.edge) sj["edge"] = true;
        if (s.outward) sj["outward"] = true;
        j["shape"] = sj;
    }
    if (!(e.life == d.life)) j["life"] = rangeJson(e.life);
    if (e.direction != d.direction) j["direction"] = e.direction;
    if (e.spread != d.spread) j["spread"] = e.spread;
    if (!(e.speed == d.speed)) j["speed"] = rangeJson(e.speed);
    if (!(e.size == d.size)) j["size"] = rangeJson(e.size);
    if (!(e.rotation == d.rotation)) j["rotation"] = rangeJson(e.rotation);
    if (!(e.spin == d.spin)) j["spin"] = rangeJson(e.spin);
    if (e.color != e.color2) j["color"] = json::array({vecJson(e.color), vecJson(e.color2)});
    else if (e.color != d.color) j["color"] = vecJson(e.color);
    if (!e.sprite.empty()) j["sprite"] = e.sprite;
    json o = json::object();
    if (!e.colorOverLife.empty()) o["color"] = curveJson(e.colorOverLife);
    if (!e.sizeOverLife.empty()) o["size"] = curveJson(e.sizeOverLife);
    if (!e.speedOverLife.empty()) o["speed"] = curveJson(e.speedOverLife);
    if (!e.spinOverLife.empty()) o["spin"] = curveJson(e.spinOverLife);
    if (!o.empty()) j["overLife"] = o;
    json f = json::object();
    if (e.gravity != d.gravity) f["gravity"] = vecJson(e.gravity);
    if (e.drag != d.drag) f["drag"] = e.drag;
    if (!(e.radial == d.radial)) f["radial"] = rangeJson(e.radial);
    if (!(e.tangential == d.tangential)) f["tangential"] = rangeJson(e.tangential);
    if (!f.empty()) j["forces"] = f;
    if (!e.frames.empty()) {
        json fb{{"frames", e.frames}};
        if (e.framesByFps) { fb["mode"] = "fps"; fb["fps"] = e.fps; }
        if (e.randomFrame) fb["randomStart"] = true;
        j["flipbook"] = fb;
    }
    json r = json::object();
    if (e.blend != d.blend) {
        const std::string preset = e.blend.presetName();
        r["blend"] = preset.empty() ? json{{"src", Blend::factorName(e.blend.src)}, {"dst", Blend::factorName(e.blend.dst)}}
                                    : json(preset);
    }
    if (e.order != d.order) r["order"] = e.order;
    if (e.alignToVelocity) r["alignToVelocity"] = true;
    if (e.oldestOnTop) r["oldestOnTop"] = true;
    if (!r.empty()) j["render"] = r;
    return j;
}

}  // namespace

bool parseParticles(const std::string& text, ParticleFile& out, std::string* err) {
    try {
        const json j = json::parse(text);
        onlyKnown(j, {"version", "atlases", "effects"}, "file");
        ParticleFile f;
        f.version = j.value("version", 1);
        for (const json& a : j.value("atlases", json::array())) {
            toms::anim::AtlasRef r;
            if (a.is_string()) r.path = a.get<std::string>();
            else { r.path = a.at("path").get<std::string>(); r.id = a.value("id", std::string()); }
            if (r.id.empty()) r.id = toms::anim::defaultAtlasId(r.path);
            for (const toms::anim::AtlasRef& o : f.atlases)
                if (o.id == r.id) fail("atlases", "two atlases have the id '" + r.id + "': give one an \"id\"");
            f.atlases.push_back(r);
        }
        for (const json& ej : j.value("effects", json::array())) {
            onlyKnown(ej, {"name", "duration", "loop", "prewarm", "seed", "emitters"}, "effect");
            Effect e;
            e.name = ej.value("name", std::string());
            if (ej.contains("duration")) e.duration = num(ej["duration"], e.name + ".duration");
            e.loop = ej.value("loop", false);
            if (ej.contains("prewarm")) e.prewarm = num(ej["prewarm"], e.name + ".prewarm");
            e.seed = ej.value("seed", 0u);
            for (const json& em : ej.value("emitters", json::array())) {
                e.emitters.emplace_back();
                emitterFrom(em, e.emitters.back(), e.name);
            }
            f.effects.push_back(std::move(e));
        }
        out = std::move(f);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

std::string particlesToJson(const ParticleFile& f) {
    json j;
    j["version"] = f.version;
    if (!f.atlases.empty()) {
        json a = json::array();
        for (const toms::anim::AtlasRef& r : f.atlases) {
            if (r.id == toms::anim::defaultAtlasId(r.path)) a.push_back(r.path);
            else a.push_back({{"id", r.id}, {"path", r.path}});
        }
        j["atlases"] = a;
    }
    j["effects"] = json::array();
    for (const Effect& e : f.effects) {
        json ej{{"name", e.name}};
        if (e.duration != 0) ej["duration"] = e.duration;
        if (e.loop) ej["loop"] = true;
        if (e.prewarm != 0) ej["prewarm"] = e.prewarm;
        if (e.seed != 0) ej["seed"] = e.seed;
        ej["emitters"] = json::array();
        for (const Emitter& em : e.emitters) ej["emitters"].push_back(emitterJson(em));
        j["effects"].push_back(ej);
    }
    return j.dump(2) + "\n";
}

// ---- checks ------------------------------------------------------------------------------------

std::vector<Problem> checkParticles(const ParticleFile& f, const toms::anim::AtlasSet* atlases) {
    std::vector<Problem> out;
    auto err = [&](const std::string& w, const std::string& t) { out.push_back({w, t, false}); };
    auto warn = [&](const std::string& w, const std::string& t) { out.push_back({w, t, true}); };
    std::set<std::string> effectNames;
    for (const Effect& e : f.effects) {
        if (e.name.empty()) err("(effect)", "an effect needs a name");
        else if (!effectNames.insert(e.name).second) err(e.name, "two effects have this name");
        if (e.duration < 0) err(e.name, "duration must not be negative");
        if (e.loop && e.duration <= 0) warn(e.name, "loop has no effect without a duration");
        if (e.emitters.empty()) warn(e.name, "no emitters: draws nothing");
        std::set<std::string> emitterNames;
        for (const Emitter& m : e.emitters) {
            const std::string w = e.name + "/" + m.name;
            if (!emitterNames.insert(m.name).second) warn(w, "two emitters have this name");
            if (m.maxParticles <= 0) err(w, "maxParticles must be at least 1");
            if (m.stop > 0 && m.stop <= m.start) err(w, "stop must be after start");
            if (m.life.min <= 0) err(w, "life must be above 0");
            for (const auto& [name, r] : {std::pair<const char*, Range>{"life", m.life}, {"speed", m.speed}, {"size", m.size},
                                          {"rotation", m.rotation}, {"spin", m.spin}, {"radial", m.radial},
                                          {"tangential", m.tangential}})
                if (r.min > r.max) err(w, std::string(name) + ": min is above max");
            if (m.rate < 0) err(w, "emission.rate must not be negative");
            if (m.rate * m.life.max > m.maxParticles * 1.05f)
                warn(w, "rate x life (" + std::to_string((int)std::ceil(m.rate * m.life.max)) + ") is more than maxParticles (" +
                            std::to_string(m.maxParticles) + "): some particles will not be born");
            for (const Burst& b : m.bursts) {
                if (b.count.max > m.maxParticles) warn(w, "a burst is bigger than maxParticles");
                if (b.repeat != 0 && b.interval <= 0) err(w, "a repeating burst needs an interval above 0");
            }
            if (m.rate <= 0 && m.bursts.empty()) warn(w, "no rate and no bursts: emits nothing");
            auto keysIn01 = [&](const char* name, const std::vector<float>& ts) {
                for (size_t i = 0; i < ts.size(); i++) {
                    if (ts[i] < 0 || ts[i] > 1) err(w, std::string("overLife.") + name + ": key t must be 0..1");
                    if (i && ts[i] == ts[i - 1]) err(w, std::string("overLife.") + name + ": two keys at the same t");
                }
            };
            auto times = [](const auto& c) { std::vector<float> t; for (const auto& k : c.keys) t.push_back(k.t); return t; };
            keysIn01("color", times(m.colorOverLife));
            keysIn01("size", times(m.sizeOverLife));
            keysIn01("speed", times(m.speedOverLife));
            keysIn01("spin", times(m.spinOverLife));
            if (m.sprite.empty() && m.frames.empty()) err(w, "no sprite and no flipbook frames: draws nothing");
            if (m.framesByFps && m.fps <= 0) err(w, "flipbook fps must be above 0");
            for (Blend::Factor fac : {m.blend.src, m.blend.dst})
                if (fac == Blend::DstAlpha || fac == Blend::InvDstAlpha)
                    warn(w, "blend: the screen has no alpha, dstAlpha factors act like one / zero");
            if (atlases) {
                std::vector<std::string> refs = m.frames;
                if (!m.sprite.empty()) refs.push_back(m.sprite);
                for (const std::string& r : refs)
                    if (!atlases->find(r)) err(w, "sprite '" + r + "' is in no atlas");
            }
        }
    }
    return out;
}

// ---- EffectInstance ----------------------------------------------------------------------------

void EffectInstance::Pool::reserve(int n) {
    pos.resize((size_t)n); vel.resize((size_t)n);
    age.resize((size_t)n); life.resize((size_t)n); size.resize((size_t)n); rot.resize((size_t)n); spin.resize((size_t)n);
    radial.resize((size_t)n); tangential.resize((size_t)n);
    color.resize((size_t)n); frame.resize((size_t)n);
}

uint32_t EffectInstance::next() {   // splitmix32
    uint32_t z = (rng_ += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}

float EffectInstance::rand01() { return float(next() >> 8) * (1.0f / 16777216.0f); }

void EffectInstance::play(const Effect* effect, uint32_t seed) {
    effect_ = effect;
    pools_.clear();
    drawOrder_.clear();
    gpu_.clear();   // the previous play's GPU buffers go now (also for play(nullptr))
    time_ = 0;
    carry_ = 0;
    stopped_ = false;
    if (!effect) return;
    if (seed == 0) seed = effect->seed;
    if (seed == 0) {
        static uint32_t counter = 0;
        seed = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ (++counter * 0x9E3779B9u);
        if (seed == 0) seed = 1;
    }
    seed_ = seed;
    rng_ = seed;
    stepIndex_ = 0;
    pools_.resize(effect->emitters.size());
    gpu_.assign(effect->emitters.size(), nullptr);
    for (size_t i = 0; i < pools_.size(); i++) {
        const Emitter& em = effect->emitters[i];
        pools_[i].burstsDone.assign(em.bursts.size(), 0);
        drawOrder_.push_back(i);
        using Sim = Emitter::Simulation;
        const bool wantGpu = em.simulation == Sim::Gpu ||
                             (em.simulation == Sim::Auto && gpuThreshold_ > 0 && em.maxParticles > gpuThreshold_);
        if (wantGpu && gpuRen_ && gpuRen_->supportsGpuParticles()) startGpu(i);   // else (no compute) the CPU
        if (!gpu_[i]) pools_[i].reserve(std::max(1, em.maxParticles));   // a GPU emitter's particles live on the GPU
    }
    std::stable_sort(drawOrder_.begin(), drawOrder_.end(),
                     [&](size_t a, size_t b) { return effect->emitters[a].order < effect->emitters[b].order; });
    for (int n = (int)std::lround(effect->prewarm / kStep); n > 0; n--) step(kStep);
}

void EffectInstance::startGpu(size_t ei) {
    const Emitter& em = effect_->emitters[ei];
    auto g = std::make_shared<Gpu>();
    g->ren = gpuRen_;
    g->capacity = std::max(1, em.maxParticles);
    g->id = gpuRen_->createGpuParticles(uint32_t(g->capacity));
    if (!g->id) {   // no buffers: this emitter stays on the CPU
        g->ren = nullptr;
        gpu_[ei] = nullptr;
        return;
    }
    g->freeSlots.resize(size_t(g->capacity));
    for (int s = 0; s < g->capacity; s++) g->freeSlots[size_t(s)] = g->capacity - 1 - s;   // slot 0 first
    // The curves as tables over the life (the GPU interpolates between entries).
    constexpr uint32_t n = GpuParticleFrame::kLut;
    g->colorLut.resize(n * 4);
    g->scalarLut.resize(n * 4);
    for (uint32_t i = 0; i < n; i++) {
        const float u = float(i) / float(n - 1);
        const glm::vec4 col = em.colorOverLife.at(u, glm::vec4(1.0f));
        for (int k = 0; k < 4; k++) g->colorLut[i * 4 + k] = col[k];
        g->scalarLut[i * 4 + 0] = em.sizeOverLife.at(u, 1.0f);
        g->scalarLut[i * 4 + 1] = em.speedOverLife.at(u, 1.0f);
        g->scalarLut[i * 4 + 2] = em.spinOverLife.at(u, 1.0f);
        g->scalarLut[i * 4 + 3] = 0;
    }
    gpu_[ei] = g;
}

int EffectInstance::gpuEmitters() const {
    int n = 0;
    for (const auto& g : gpu_) n += g != nullptr;
    return n;
}

void EffectInstance::stop(bool clear) {
    stopped_ = true;
    if (clear) {
        for (Pool& p : pools_) p.count = 0;
        for (size_t i = 0; i < gpu_.size(); i++)
            if (gpu_[i]) startGpu(i);   // fresh, empty buffers
    }
}

void EffectInstance::update(float dt) {
    if (!effect_ || dt <= 0) return;
    carry_ += dt;
    int steps = int(carry_ / kStep + 1e-3f);   // 60 x (1/60) is 60 steps despite float error
    if (steps > kMaxSteps) {   // a long hitch: simulate a little, drop the rest
        steps = kMaxSteps;
        carry_ = 0;
    } else {
        carry_ -= steps * kStep;
    }
    while (steps-- > 0) step(kStep);
}

void EffectInstance::seek(float t) {
    const Effect* e = effect_;
    const uint32_t s = seed_;
    const glm::mat3 m = transform_;
    transform_ = m;
    play(e, s);   // starts at the prewarm time
    for (int n = (int)std::lround((t - time_) / kStep); n > 0; n--) step(kStep);
}

bool EffectInstance::emitting() const {
    if (!effect_ || stopped_) return false;
    if (effect_->duration <= 0 || effect_->loop) return true;
    return time_ < effect_->duration;
}

bool EffectInstance::finished() const { return !emitting() && liveCount() == 0; }

int EffectInstance::liveCount() const {
    int n = 0;
    for (size_t i = 0; i < pools_.size(); i++) n += liveCount(i);
    return n;
}

int EffectInstance::liveCount(size_t emitter) const {
    if (emitter >= pools_.size()) return 0;
    if (const Gpu* g = gpu_[emitter].get()) return g->capacity - int(g->freeSlots.size());
    return pools_[emitter].count;
}

namespace {

glm::vec2 rotate(const glm::vec2& v, float degrees) {
    const float r = glm::radians(degrees), c = std::cos(r), s = std::sin(r);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
glm::vec2 apply(const glm::mat3& m, const glm::vec2& p) { const glm::vec3 v = m * glm::vec3(p, 1); return {v.x, v.y}; }
glm::vec2 applyLinear(const glm::mat3& m, const glm::vec2& d) { const glm::vec3 v = m * glm::vec3(d, 0); return {v.x, v.y}; }

}  // namespace

void EffectInstance::spawn(size_t ei, int n) {
    const Emitter& em = effect_->emitters[ei];
    Pool& p = pools_[ei];
    Gpu* gpu = gpu_[ei].get();
    n = std::min(n, gpu ? int(gpu->freeSlots.size()) : std::max(1, em.maxParticles) - p.count);
    const Shape& sh = em.shape;
    for (; n > 0; n--) {
        // Where on the shape, and the direction it leaves in.
        glm::vec2 at{0, 0};
        float outAngle = 0;
        bool hasOut = false;
        switch (sh.type) {
        case Shape::Point:
            break;
        case Shape::Line:
            at = rotate({(rand01() - 0.5f) * sh.length, 0}, sh.angle);
            break;
        case Shape::Box:
            if (sh.edge) {
                const float per = 2 * (sh.width + sh.height);
                float d = rand01() * per;
                if (d < sh.width) at = {d - sh.width / 2, -sh.height / 2};
                else if ((d -= sh.width) < sh.height) at = {sh.width / 2, d - sh.height / 2};
                else if ((d -= sh.height) < sh.width) at = {sh.width / 2 - d, sh.height / 2};
                else at = {-sh.width / 2, sh.height / 2 - (d - sh.width)};
            } else {
                at = {(rand01() - 0.5f) * sh.width, (rand01() - 0.5f) * sh.height};
            }
            break;
        case Shape::Circle:
        case Shape::Ring: {
            const float a = sh.arcFrom + (sh.arcTo - sh.arcFrom) * rand01();
            const float r = sh.type == Shape::Ring ? sh.radius - sh.thickness * rand01()
                          : sh.edge             ? sh.radius
                                                : sh.radius * std::sqrt(rand01());
            at = rotate({r, 0}, a);
            outAngle = a;
            hasOut = true;
            break;
        }
        }
        float angle = em.direction;
        if (sh.outward) {
            if (hasOut) angle = outAngle;
            else if (at != glm::vec2(0)) angle = glm::degrees(std::atan2(at.y, at.x));
            else angle = rand01() * 360.0f;   // a point: every direction
        }
        if (em.spread != 0) angle += em.spread * (rand01() * 2 - 1);
        glm::vec2 vel = rotate({pick(em.speed), 0}, angle);
        glm::vec2 pos = em.offset + at;
        if (!em.localSpace) {   // born into the world where the effect is now
            pos = apply(transform_, pos);
            vel = applyLinear(transform_, vel);
        }
        // The same random values in the same order on both paths: a GPU emitter's particles are
        // the ones the CPU would have made.
        const float life = std::max(0.001f, pick(em.life));
        const float size = pick(em.size), rot = pick(em.rotation), spin = pick(em.spin);
        const float radial = pick(em.radial), tangential = pick(em.tangential);
        const glm::vec4 color = em.color == em.color2 ? em.color : em.color + (em.color2 - em.color) * rand01();
        const int frame = em.randomFrame && !em.frames.empty() ? int(next() % (uint32_t)em.frames.size()) : 0;
        if (gpu) {   // a record for the GPU, into a free slot
            const int slot = gpu->freeSlots.back();
            gpu->freeSlots.pop_back();
            // Born in this step, after the steps already waiting for the next draw: it skips those.
            const float age = -float(gpu->pendingSteps) * kStep;
            const float rec[20] = {pos.x, pos.y, vel.x, vel.y, age, life, size, rot, spin, radial, tangential, float(frame),
                                   color.r, color.g, color.b, color.a, float(slot), 0, 0, 0};
            gpu->spawn.insert(gpu->spawn.end(), rec, rec + 20);
            // It dies in the step where its age reaches its life: the k-th step of its life, k = ceil(life / dt).
            const long long k = std::max(1LL, (long long)std::ceil(life / kStep - 1e-4));
            gpu->frees.push({stepIndex_ + k - 1, slot});
            continue;
        }
        const int i = p.count++;
        p.pos[(size_t)i] = pos;
        p.vel[(size_t)i] = vel;
        p.age[(size_t)i] = 0;
        p.life[(size_t)i] = life;
        p.size[(size_t)i] = size;
        p.rot[(size_t)i] = rot;
        p.spin[(size_t)i] = spin;
        p.radial[(size_t)i] = radial;
        p.tangential[(size_t)i] = tangential;
        p.color[(size_t)i] = color;
        p.frame[(size_t)i] = frame;
    }
}

void EffectInstance::step(float dt) {
    const Effect& fx = *effect_;
    const float t0 = time_, t1 = time_ + dt;
    const float effectEnd = fx.duration > 0 ? fx.duration : 1e30f;
    for (size_t ei = 0; ei < fx.emitters.size(); ei++) {
        const Emitter& em = fx.emitters[ei];
        Pool& p = pools_[ei];
        Gpu* gpu = gpu_[ei].get();
        if (gpu)   // the slots of particles that die in this step take newborns again
            while (!gpu->frees.empty() && gpu->frees.top().first <= stepIndex_) {
                gpu->freeSlots.push_back(gpu->frees.top().second);
                gpu->frees.pop();
            }
        // Emission within the emitter's active window [start, end).
        if (!stopped_) {
            const float end = std::min(effectEnd, em.stop > 0 ? em.stop : 1e30f);
            const float a = std::max(t0, em.start), b = std::min(t1, end);
            if (b > a && em.rate > 0) {
                // The step's length, trimmed only at the window's edges (t1 - t0 drifts with the clock).
                double inside = dt;
                if (em.start > t0) inside -= em.start - t0;
                if (end < t1) inside -= t1 - end;
                p.spawnCarry += double(em.rate) * inside;
                const int n = int(p.spawnCarry + 1e-6);   // 60 x (10/60) must give 10, not 9.9999
                p.spawnCarry -= n;
                spawn(ei, n);
            }
            for (size_t bi = 0; bi < em.bursts.size(); bi++) {
                const Burst& bu = em.bursts[bi];
                const bool repeats = bu.repeat != 0 && bu.interval > 0;
                for (;;) {
                    const int k = p.burstsDone[bi];
                    if (k > 0 && (!repeats || (bu.repeat > 0 && k > bu.repeat))) break;
                    const float at = em.start + bu.t + (repeats ? k * bu.interval : 0.0f);
                    if (at >= t1 || at >= end) break;
                    p.burstsDone[bi]++;
                    if (at >= t0 || k == 0) spawn(ei, (int)std::lround(pick(bu.count)));
                }
            }
        }
        if (gpu) {   // the GPU simulates it, at the next draw
            gpu->pendingSteps++;
            continue;
        }
        // Simulation; dead particles leave, the rest keep their birth order.
        const glm::vec2 origin = em.localSpace ? em.offset : apply(transform_, em.offset);
        const float dragMul = em.drag > 0 ? std::exp(-em.drag * dt) : 1.0f;
        int w = 0;
        for (int i = 0; i < p.count; i++) {
            const size_t s = (size_t)i;
            const float age = p.age[s] + dt;
            if (age >= p.life[s]) continue;
            const float u = age / p.life[s];
            glm::vec2 v = p.vel[s] + em.gravity * dt;
            if (p.radial[s] != 0 || p.tangential[s] != 0) {
                const glm::vec2 d = p.pos[s] - origin;
                const float len = std::sqrt(d.x * d.x + d.y * d.y);
                if (len > 1e-4f) {
                    const glm::vec2 n = d / len;
                    v += (n * p.radial[s] + glm::vec2(-n.y, n.x) * p.tangential[s]) * dt;   // (-y, x): clockwise, y down
                }
            }
            v *= dragMul;
            const size_t o = (size_t)w++;
            p.vel[o] = v;
            p.pos[o] = p.pos[s] + v * em.speedOverLife.at(u, 1.0f) * dt;
            p.rot[o] = p.rot[s] + p.spin[s] * em.spinOverLife.at(u, 1.0f) * dt;
            p.age[o] = age;
            if (o != s) {
                p.life[o] = p.life[s]; p.size[o] = p.size[s]; p.spin[o] = p.spin[s];
                p.radial[o] = p.radial[s]; p.tangential[o] = p.tangential[s]; p.color[o] = p.color[s]; p.frame[o] = p.frame[s];
            }
        }
        p.count = w;
    }
    time_ = t1;
    stepIndex_++;
    if (fx.loop && fx.duration > 0 && time_ >= fx.duration) {   // start over: bursts fire again
        time_ -= fx.duration;
        for (Pool& p : pools_) std::fill(p.burstsDone.begin(), p.burstsDone.end(), 0);
    }
}

void EffectInstance::appendQuads(const toms::anim::AtlasSet& atlases, const glm::mat3& placement, const float tint[4],
                                 std::vector<Quad>& out, std::vector<std::string>* missing) const {
    if (!effect_) return;
    const glm::vec4 tintV = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    for (size_t ei : drawOrder_)
        if (!gpu_[ei]) appendEmitterQuads(ei, atlases, placement, tintV, out, missing);
}

void EffectInstance::appendEmitterQuads(size_t ei, const toms::anim::AtlasSet& atlases, const glm::mat3& placement,
                                        const glm::vec4& tintV, std::vector<Quad>& out, std::vector<std::string>* missing) const {
    auto report = [&](const std::string& ref) {
        if (missing && std::find(missing->begin(), missing->end(), ref) == missing->end()) missing->push_back(ref);
    };
    struct Look { const toms::AtlasRegion* r = nullptr; uint16_t tex = kSpriteAtlasTexture; };
    std::vector<Look> looks;
    {
        const Emitter& em = effect_->emitters[ei];
        const Pool& p = pools_[ei];
        if (p.count == 0) return;
        // The sprites of this emitter, looked up once per draw.
        const std::vector<std::string> single{em.sprite};
        const std::vector<std::string>& refs = em.frames.empty() ? single : em.frames;
        for (const std::string& ref : refs) {
            Look l;
            l.r = atlases.find(ref, &l.tex);
            if (!l.r) report(ref);
            looks.push_back(l);
        }
        const glm::mat3 base = em.localSpace ? placement * transform_ : placement;
        const bool additive = em.blend == Blend::add();
        for (int k = 0; k < p.count; k++) {
            const size_t i = (size_t)(em.oldestOnTop ? p.count - 1 - k : k);
            const float u = p.age[i] / p.life[i];
            size_t f = 0;
            if (looks.size() > 1) {
                f = em.framesByFps ? (size_t)(p.frame[i] + int(p.age[i] * em.fps)) % looks.size()
                                   : (size_t)(p.frame[i] + std::min(int(u * looks.size()), int(looks.size()) - 1)) % looks.size();
            }
            const Look& look = looks[f];
            if (!look.r || look.r->origW <= 0) continue;
            const glm::vec4 col = p.color[i] * em.colorOverLife.at(u, glm::vec4(1.0f)) * tintV;
            if (col.a <= 0.0f) continue;
            const float size = p.size[i] * em.sizeOverLife.at(u, 1.0f);
            if (size <= 0.0f) continue;
            const toms::AtlasRegion& r = *look.r;
            float angle = p.rot[i];
            if (em.alignToVelocity && (p.vel[i].x != 0 || p.vel[i].y != 0)) angle += glm::degrees(std::atan2(p.vel[i].y, p.vel[i].x));
            // The packed pixels inside the original image, around its centre, scaled to `size` wide.
            const float k2 = size / float(r.origW);
            const float x0 = (r.offX - 0.5f * r.origW) * k2, y0 = (r.offY - 0.5f * r.origH) * k2;
            const float x1 = x0 + r.w * k2, y1 = y0 + r.h * k2;
            const float rad = glm::radians(angle), c = std::cos(rad), s = std::sin(rad);
            glm::mat3 local(1.0f);
            local[0] = glm::vec3(c, s, 0);
            local[1] = glm::vec3(-s, c, 0);
            local[2] = glm::vec3(p.pos[i], 1);
            const glm::mat3 m = base * local;
            const glm::vec3 cs[4] = {m * glm::vec3(x0, y0, 1), m * glm::vec3(x1, y0, 1), m * glm::vec3(x1, y1, 1),
                                     m * glm::vec3(x0, y1, 1)};
            Quad q;
            q.hasCorners = true;
            float minX = cs[0].x, minY = cs[0].y, maxX = cs[0].x, maxY = cs[0].y;
            for (int v = 0; v < 4; v++) {
                q.corners[v * 2] = cs[v].x;
                q.corners[v * 2 + 1] = cs[v].y;
                minX = std::min(minX, cs[v].x); maxX = std::max(maxX, cs[v].x);
                minY = std::min(minY, cs[v].y); maxY = std::max(maxY, cs[v].y);
            }
            q.rect[0] = minX; q.rect[1] = minY; q.rect[2] = maxX - minX; q.rect[3] = maxY - minY;
            std::copy(r.uv, r.uv + 4, q.uv);
            q.tint[0] = col.r; q.tint[1] = col.g; q.tint[2] = col.b; q.tint[3] = col.a;
            q.additive = additive;   // other blend modes: phase 2b (docs/17_PARTICLES.md 3a)
            q.texture = look.tex;
            out.push_back(q);
        }
    }
}

void EffectInstance::draw(IRenderer* ren, const toms::anim::AtlasSet& atlases, const glm::mat3& placement, const float tint[4],
                          std::vector<std::string>* missing) {
    if (!effect_ || !ren) return;
    const glm::vec4 tintV = tint ? glm::vec4(tint[0], tint[1], tint[2], tint[3]) : glm::vec4(1.0f);
    std::vector<Quad> quads;
    for (size_t ei : drawOrder_) {
        Gpu* g = gpu_[ei].get();
        if (!g) {
            quads.clear();
            appendEmitterQuads(ei, atlases, placement, tintV, quads, missing);
            for (const Quad& q : quads) ren->drawSprite(q);
            continue;
        }
        const Emitter& em = effect_->emitters[ei];
        // The sprite / flipbook frames: their uv rects and, for size 1, their rects around the centre.
        const std::vector<std::string> single{em.sprite};
        const std::vector<std::string>& refs = em.frames.empty() ? single : em.frames;
        const uint32_t frames = std::min<uint32_t>((uint32_t)refs.size(), GpuParticleFrame::kMaxFrames);
        float uv[GpuParticleFrame::kMaxFrames * 4] = {}, rect[GpuParticleFrame::kMaxFrames * 4] = {};
        uint16_t texture = kSpriteAtlasTexture;
        for (uint32_t f = 0; f < frames; f++) {
            uint16_t tex = kSpriteAtlasTexture;
            const toms::AtlasRegion* r = atlases.find(refs[f], &tex);
            if (!r || r->origW <= 0) {   // nothing to draw for this frame (a zero rect)
                if (missing && std::find(missing->begin(), missing->end(), refs[f]) == missing->end()) missing->push_back(refs[f]);
                continue;
            }
            if (f == 0) texture = tex;   // one texture per draw: frames on other pages draw from the first's
            std::copy(r->uv, r->uv + 4, uv + f * 4);
            const float k = 1.0f / float(r->origW);
            rect[f * 4 + 0] = (r->offX - 0.5f * r->origW) * k;
            rect[f * 4 + 1] = (r->offY - 0.5f * r->origH) * k;
            rect[f * 4 + 2] = rect[f * 4 + 0] + r->w * k;
            rect[f * 4 + 3] = rect[f * 4 + 1] + r->h * k;
        }
        GpuParticleFrame fr;
        fr.emitter = g->id;
        fr.spawn = g->spawn.data();
        fr.spawnCount = uint32_t(g->spawn.size() / 20);
        fr.steps = g->pendingSteps;
        fr.dt = kStep;
        fr.gravity[0] = em.gravity.x;
        fr.gravity[1] = em.gravity.y;
        fr.dragPerStep = em.drag > 0 ? std::exp(-em.drag * kStep) : 1.0f;
        const glm::vec2 origin = em.localSpace ? em.offset : apply(transform_, em.offset);
        fr.origin[0] = origin.x;
        fr.origin[1] = origin.y;
        fr.alignToVelocity = em.alignToVelocity;
        fr.colorLut = g->colorLut.data();
        fr.scalarLut = g->scalarLut.data();
        fr.frameCount = std::max(1u, frames);
        fr.fps = em.framesByFps && frames > 1 ? em.fps : 0.0f;
        fr.frameUv = uv;
        fr.frameRect = rect;
        const glm::mat3 m = em.localSpace ? placement * transform_ : placement;
        fr.xform[0] = m[0][0]; fr.xform[1] = m[1][0]; fr.xform[2] = m[2][0];
        fr.xform[3] = m[0][1]; fr.xform[4] = m[1][1]; fr.xform[5] = m[2][1];
        for (int k = 0; k < 4; k++) fr.tint[k] = tintV[k];
        fr.texture = texture;
        fr.additive = em.blend == Blend::add();
        ren->drawGpuParticles(fr);
        g->spawn.clear();
        g->pendingSteps = 0;
    }
}
}  // namespace toms::fx

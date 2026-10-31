// skills.cpp -- see special_moves.h.
#include "special_moves.h"

#include "gesture.h"

#include <algorithm>
#include <cmath>

namespace toms {

namespace {

ShowBeat beatFromJson(const nlohmann::json& j) {
    ShowBeat b;
    b.atMs = std::max(0, j.value("at", 0));
    b.what = j.value("do", std::string());
    b.fx = j.value("fx", std::string());
    b.corner = j.value("corner", std::string());
    b.shape = j.value("shape", std::string());
    b.clip = j.value("clip", std::string());
    b.text = j.value("text", std::string());
    b.on = j.value("on", std::string());
    b.ms = std::max(0, j.value("ms", 0));
    b.count = std::max(0, j.value("count", 0));
    b.gapMs = std::max(0, j.value("gapMs", 100));
    b.value = j.value("value", 0.0f);
    b.zoom = std::max(1.0f, j.value("zoom", 1.0f));
    return b;
}

std::vector<ShowBeat> beatsFromJson(const nlohmann::json& j, const char* key) {
    std::vector<ShowBeat> v;
    if (j.contains(key) && j[key].is_array())
        for (const auto& b : j[key]) if (b.is_object()) v.push_back(beatFromJson(b));
    std::stable_sort(v.begin(), v.end(), [](const ShowBeat& a, const ShowBeat& b) { return a.atMs < b.atMs; });
    return v;
}

}  // namespace

const SpecialMove* SpecialMoveTable::find(const std::string& id) const {
    auto it = skills.find(id);
    return it == skills.end() ? nullptr : &it->second;
}

const SpecialMove* SpecialMoveTable::forWeapon(const std::string& weapon, const std::string& use) const {
    for (const auto& [id, s] : skills)
        if (s.weapon == weapon && s.use == use) return &s;
    return nullptr;
}

SpecialMoveTable specialMovesFromJson(const nlohmann::json& j) {
    SpecialMoveTable t;
    if (!j.is_object()) return t;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_object()) continue;
        const nlohmann::json& e = it.value();
        SpecialMove s;
        s.id = it.key();
        s.name = e.value("name", std::string());
        s.weapon = e.value("weapon", std::string());
        s.use = e.value("use", s.use);
        s.prompt = e.value("prompt", std::string());
        s.damage = std::max(0.0f, e.value("damage", s.damage));
        if (e.contains("play") && e["play"].is_object()) {
            const nlohmann::json& p = e["play"];
            s.play.kind = p.value("kind", s.play.kind);
            s.play.shape = p.value("shape", s.play.shape);
            s.play.points = std::max(1, p.value("points", s.play.points));
            s.play.timeMs = std::max(500, p.value("timeMs", s.play.timeMs));
            s.play.size = std::max(40.0f, p.value("size", s.play.size));
            s.play.cornerRadius = std::max(8.0f, p.value("cornerRadius", s.play.cornerRadius));
            if (p.contains("cues") && p["cues"].is_array())
                for (const auto& c : p["cues"]) if (c.is_string()) s.play.cues.push_back(c.get<std::string>());
        }
        if (s.play.kind == "shape") {   // a shape's steps: its corners after the first (a pentagram: 5)
            const int n = (int)shapeCorners(s.play.shape).size();
            if (n > 1) s.play.points = n - 1;
        }
        if (s.play.kind == "slice" && !s.play.cues.empty()) s.play.points = (int)s.play.cues.size();
        s.demo = beatsFromJson(e, "demo");
        s.step = beatsFromJson(e, "step");
        s.finish = beatsFromJson(e, "finish");
        t.skills[s.id] = std::move(s);
    }
    return t;
}

int showLength(const std::vector<ShowBeat>& beats) {
    int end = 0;
    for (const ShowBeat& b : beats) {
        int len = b.ms;
        if (b.what == "hits") len = std::max(len, b.count * b.gapMs);
        end = std::max(end, b.atMs + len);
    }
    return end;
}

std::vector<const ShowBeat*> beatsDue(const std::vector<ShowBeat>& beats, int fromMs, int toMs) {
    std::vector<const ShowBeat*> v;
    for (const ShowBeat& b : beats)
        if (b.atMs > fromMs && b.atMs <= toMs) v.push_back(&b);
    return v;
}

std::string namePart(const std::string& name, int done, int total) {
    std::vector<size_t> starts;   // each letter's first byte
    for (size_t i = 0; i < name.size(); i++)
        if (((unsigned char)name[i] & 0xC0) != 0x80) starts.push_back(i);
    if (total <= 0 || done >= total) return name;
    if (done <= 0) return std::string();
    const size_t letters = (size_t)std::lround((double)starts.size() * done / total);
    return letters >= starts.size() ? name : name.substr(0, starts[letters]);
}

} // namespace toms

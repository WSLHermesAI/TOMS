// battle_rules.cpp -- see battle_rules.h.
#include "battle_rules.h"

#include <algorithm>
#include <cmath>

namespace toms {

// ---- looks ----

const LookState* LooksTable::find(const std::string& look, const std::string& state, const std::string& fallback) const {
    std::string id = looks.count(look) ? look : fallback;
    for (int step = 0; step < 8 && !id.empty(); step++) {
        auto it = looks.find(id);
        if (it == looks.end()) return nullptr;
        auto s = it->second.states.find(state);
        if (s != it->second.states.end()) return &s->second;
        id = it->second.base;
    }
    return nullptr;
}

LooksTable looksFromJson(const nlohmann::json& j) {
    LooksTable t;
    if (!j.is_object()) return t;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_object()) continue;
        Look l;
        l.base = it.value().value("base", std::string());
        const nlohmann::json st = it.value().value("states", nlohmann::json::object());
        for (auto s = st.begin(); s != st.end(); ++s) {
            if (!s.value().is_object()) continue;
            LookState ls;
            ls.clip = s.value().value("clip", std::string());
            ls.sound = s.value().value("sound", std::string());
            ls.fx = s.value().value("fx", std::string());
            l.states[s.key()] = ls;
        }
        t.looks[it.key()] = l;
    }
    return t;
}

// ---- monster events ----

std::vector<MonsterEvent> monsterEventsFromJson(const nlohmann::json& j) {
    std::vector<MonsterEvent> out;
    if (!j.is_array()) return out;
    for (const auto& e : j) {
        if (!e.is_object()) continue;
        MonsterEvent m;
        m.on = e.value("on", std::string());
        m.value = e.value("value", 0.0f);
        m.once = e.value("once", m.on != "everyMs" && m.on != "perfectHit");
        if (e.contains("do") && e["do"].is_array()) m.actions = e["do"];
        if (!m.on.empty()) out.push_back(m);
    }
    return out;
}

std::vector<int> fireMonsterEvents(std::vector<MonsterEvent>& events, const EventCheck& c) {
    std::vector<int> out;
    for (size_t i = 0; i < events.size(); i++) {
        MonsterEvent& e = events[i];
        if (e.on != c.on || (e.once && e.fired > 0)) continue;
        bool fire = false;
        if (c.on == "hpBelow" || c.on == "playerHpBelow")   // crossing the line, not every hit below it
            fire = c.hpPctBefore >= e.value && c.hpPctAfter < e.value;
        else if (c.on == "attacks") {
            const int n = std::max(1, (int)std::lround(e.value));
            fire = e.once ? c.attacks == n : c.attacks > 0 && c.attacks % n == 0;
        } else if (c.on == "everyMs") {
            const int n = std::max(100, (int)std::lround(e.value));
            fire = c.msAfter / n > c.msBefore / n;
        } else
            fire = true;   // beforeBattle, battleStart, perfectHit, falls
        if (!fire) continue;
        e.fired++;
        out.push_back((int)i);
    }
    return out;
}

// ---- moves ----

const MoveDef& MovesTable::get(const std::string& id) const {
    static const MoveDef kStrike = [] { MoveDef m; m.id = "strike"; m.text = "battle.intent.strike"; return m; }();
    auto it = moves.find(id);
    return it != moves.end() ? it->second : kStrike;
}

MoveKind moveKindFromString(const std::string& s) {
    if (s == "heavy") return MoveKind::Heavy;
    if (s == "jabs") return MoveKind::Jabs;
    if (s == "spell") return MoveKind::Spell;
    if (s == "guard") return MoveKind::Guard;
    return MoveKind::Strike;
}

const char* moveKindName(MoveKind k) {
    switch (k) {
    case MoveKind::Heavy: return "heavy";
    case MoveKind::Jabs: return "jabs";
    case MoveKind::Spell: return "spell";
    case MoveKind::Guard: return "guard";
    default: return "strike";
    }
}

MovesTable movesFromJson(const nlohmann::json& j) {
    MovesTable t;
    if (!j.is_object()) return t;
    for (auto it = j.begin(); it != j.end(); ++it) {
        if (it.key().empty() || it.key()[0] == '_' || !it.value().is_object()) continue;
        const nlohmann::json& v = it.value();
        MoveDef m;
        m.id = it.key();
        m.kind = moveKindFromString(v.value("kind", it.key()));
        m.damage = std::max(0.0f, v.value("damage", 1.0f));
        m.hits = std::max(1, v.value("hits", 1));
        m.time = std::max(0.1f, v.value("time", 1.0f));
        m.breakHits = std::max(1, v.value("breakHits", 2));
        m.reduce = std::clamp(v.value("reduce", 0.3f), 0.0f, 1.0f);
        m.text = v.value("text", "battle.intent." + std::string(moveKindName(m.kind)));
        m.icon = v.value("icon", std::string());
        t.moves[m.id] = m;
    }
    return t;
}

std::vector<MoveChoice> moveChoicesFromJson(const nlohmann::json& j) {
    std::vector<MoveChoice> out;
    if (!j.is_array()) return out;
    for (const auto& m : j) {
        MoveChoice c;
        if (m.is_string()) c.id = m.get<std::string>();
        else if (m.is_object()) { c.id = m.value("id", std::string()); c.weight = std::max(0, m.value("weight", 1)); }
        if (!c.id.empty() && c.weight > 0) out.push_back(c);
    }
    return out;
}

std::string pickMove(const std::vector<MoveChoice>& choices, float roll) {
    int total = 0;
    for (const MoveChoice& c : choices) total += c.weight;
    if (total <= 0) return "strike";
    int at = std::clamp((int)std::floor(roll * total), 0, total - 1);
    for (const MoveChoice& c : choices) {
        if (at < c.weight) return c.id;
        at -= c.weight;
    }
    return choices.back().id;
}

// ---- the pace ----

Pace paceFromString(const std::string& s, Pace fallback) {
    if (s == "stamina") return Pace::Stamina;
    if (s == "cooldown") return Pace::Cooldown;
    return fallback;
}

const char* paceName(Pace p) { return p == Pace::Stamina ? "stamina" : "cooldown"; }

PaceSettings paceFromJson(const nlohmann::json& b) {
    PaceSettings p;
    if (!b.is_object()) return p;
    p.pace = paceFromString(b.value("pace", std::string()), Pace::Cooldown);
    const nlohmann::json s = b.value("stamina", nlohmann::json::object());
    p.staminaMax = std::max(1, s.value("max", p.staminaMax));
    p.staminaRegenPerS = std::max(0.0f, s.value("regenPerS", p.staminaRegenPerS));
    p.attackCost = std::max(0, s.value("attack", p.attackCost));
    p.guardCost = std::max(0, s.value("guard", p.guardCost));
    return p;
}

// ---- break ----

BreakSettings breakFromJson(const nlohmann::json& b) {
    BreakSettings s;
    if (!b.is_object()) return s;
    const nlohmann::json j = b.value("break", nlohmann::json::object());
    s.points = std::max(1, j.value("points", s.points));
    s.brokenMs = std::max(0, j.value("brokenMs", s.brokenMs));
    s.brokenDamage = std::max(1.0f, j.value("brokenDamage", s.brokenDamage));
    s.parryMs = std::max(0, j.value("parryMs", s.parryMs));
    s.parryDamage = std::max(1.0f, j.value("parryDamage", s.parryDamage));
    s.parryPower = std::clamp(j.value("parryPower", s.parryPower), 0, 100);
    s.parryBreak = std::max(0, j.value("parryBreak", s.parryBreak));
    s.guardBreak = std::max(0, j.value("guardBreak", s.guardBreak));
    return s;
}

bool addBreak(float& gauge, float add, int points) {
    gauge += std::max(0.0f, add);
    if (gauge + 1e-4f < (float)std::max(1, points)) return false;
    gauge = 0.0f;
    return true;
}

// ---- the revamp ----

int FeelSettings::stop(const std::string& m, bool reduced) const {
    auto it = stopMs.find(m);
    if (it == stopMs.end()) return 0;
    return std::max(0, (int)std::lround(it->second * strength * (reduced ? reducedScale : 1.0f)));
}

float FeelSettings::shake(const std::string& m, bool reduced) const {
    auto it = shakePx.find(m);
    if (it == shakePx.end()) return 0.0f;
    return std::max(0.0f, it->second * strength * (reduced ? reducedScale : 1.0f));
}

std::string FeelSettings::sound(const std::string& m) const {
    auto it = sounds.find(m);
    return it != sounds.end() ? it->second : std::string();
}

FeelSettings feelFromJson(const nlohmann::json& b) {
    FeelSettings f;
    f.stopMs = {{"hit", 25}, {"great", 50}, {"perfect", 80}, {"parry", 90}, {"break", 120}, {"hurt", 30},
                {"heavyHurt", 60}, {"ultimate", 120}};
    f.shakePx = {{"hit", 2}, {"great", 4}, {"perfect", 7}, {"parry", 4}, {"break", 9}, {"hurt", 4},
                 {"heavyHurt", 8}, {"ultimate", 10}, {"phase", 6}};
    if (!b.is_object() || !b.contains("feel") || !b["feel"].is_object()) return f;
    const nlohmann::json& j = b["feel"];
    f.strength = std::max(0.0f, j.value("strength", f.strength));
    for (const char* k : {"stopMs", "shakePx", "sounds"})
        if (j.contains(k) && j[k].is_object())
            for (auto it = j[k].begin(); it != j[k].end(); ++it) {
                if (std::string(k) == "stopMs" && it.value().is_number()) f.stopMs[it.key()] = it.value().get<int>();
                if (std::string(k) == "shakePx" && it.value().is_number()) f.shakePx[it.key()] = it.value().get<float>();
                if (std::string(k) == "sounds" && it.value().is_string()) f.sounds[it.key()] = it.value().get<std::string>();
            }
    f.shakeMs = std::max(0, j.value("shakeMs", f.shakeMs));
    f.slowMoMs = std::max(0, j.value("slowMoMs", f.slowMoMs));
    f.slowMoScale = std::clamp(j.value("slowMoScale", f.slowMoScale), 0.05f, 1.0f);
    f.reducedScale = std::clamp(j.value("reducedScale", f.reducedScale), 0.0f, 1.0f);
    f.chainStep = std::max(0.0f, j.value("chainStep", f.chainStep));
    f.chainMax = std::max(1, j.value("chainMax", f.chainMax));
    return f;
}

GradeSettings gradeFromJson(const nlohmann::json& b) {
    GradeSettings g;
    if (!b.is_object() || !b.contains("grade") || !b["grade"].is_object()) return g;
    const nlohmann::json& j = b["grade"];
    g.parSeconds = j.value("parSeconds", g.parSeconds);
    g.timePenalty = j.value("timePenalty", g.timePenalty);
    g.damagePenalty = j.value("damagePenalty", g.damagePenalty);
    g.perfectBonus = j.value("perfectBonus", g.perfectBonus);
    g.chainBonus = j.value("chainBonus", g.chainBonus);
    g.s = j.value("s", g.s);
    g.a = j.value("a", g.a);
    g.b = j.value("b", g.b);
    g.sGold = std::max(0.0f, j.value("sGold", g.sGold));
    g.sCard = std::clamp(j.value("sCard", g.sCard), 0.0f, 1.0f);
    return g;
}

std::string battleGrade(const GradeInput& in, const GradeSettings& g, int* score) {
    const float sc = 100.0f - std::max(0.0f, in.seconds - g.parSeconds) * g.timePenalty -
                     std::clamp(in.lostShare, 0.0f, 1.0f) * g.damagePenalty +
                     std::clamp(in.perfectRate, 0.0f, 1.0f) * g.perfectBonus + (float)std::max(0, in.bestChain) * g.chainBonus;
    if (score) *score = (int)std::lround(sc);
    return sc >= g.s ? "S" : sc >= g.a ? "A" : sc >= g.b ? "B" : "C";
}

GuardSettings guardFromJson(const nlohmann::json& b) {
    GuardSettings g;
    if (!b.is_object() || !b.contains("guard") || !b["guard"].is_object()) return g;
    const nlohmann::json& j = b["guard"];
    g.mode = j.value("mode", g.mode) == "bar" ? "bar" : "timing";
    g.parryMs = std::max(0, j.value("parryMs", g.parryMs));
    g.blockMs = std::max(g.parryMs, j.value("blockMs", g.blockMs));
    g.earlyMs = std::max(g.blockMs, j.value("earlyMs", g.earlyMs));
    g.blockPower = std::clamp(j.value("blockPower", g.blockPower), 0.0f, 100.0f);
    g.cooldownMs = std::max(0, j.value("cooldownMs", g.cooldownMs));
    return g;
}

GuardTiming guardTiming(int sinceMs, const GuardSettings& g) {
    if (sinceMs < 0) return GuardTiming::None;
    if (sinceMs <= g.parryMs) return GuardTiming::Parry;
    if (sinceMs <= g.blockMs) return GuardTiming::Block;
    if (sinceMs <= g.earlyMs) return GuardTiming::Early;
    return GuardTiming::None;
}

} // namespace toms

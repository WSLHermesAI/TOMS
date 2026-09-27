#include "equipment_actives.h"

#include <algorithm>
#include <fstream>

#include "json.hpp"

namespace toms {

std::map<std::string, ActiveDefinition> loadActiveDefinitions(const std::string& path) {
    std::map<std::string, ActiveDefinition> out;
    std::ifstream f(path);
    if (!f.good()) return out;                      // absent table = no actives, not a crash
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) return out;
        for (auto it = j.begin(); it != j.end(); ++it) {
            const nlohmann::json& v = it.value();
            ActiveDefinition d;
            d.id = v.value("id", it.key());
            d.kind = v.value("kind", std::string());
            d.damageMultiplier = v.value("damageMultiplier", 1.0f);
            d.usesPerBattle = v.value("usesPerBattle", 1);
            d.cooldownSeconds = v.value("cooldownSeconds", 0.0f);
            d.vfxFrames = v.value("vfxFrames", 0);
            out[d.id] = d;
        }
    } catch (const std::exception&) {
        return {};                                  // malformed table behaves like an absent one
    }
    return out;
}

std::map<std::string, std::vector<std::string>> loadEquipmentActives(const std::string& path) {
    std::map<std::string, std::vector<std::string>> out;
    std::ifstream f(path);
    if (!f.good()) return out;
    try {
        nlohmann::json j;
        f >> j;
        if (!j.is_object()) return out;
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::vector<std::string> ids;
            if (it.value().contains("actives") && it.value()["actives"].is_array())
                for (const auto& a : it.value()["actives"])
                    if (a.is_string()) ids.push_back(a.get<std::string>());
            out[it.key()] = ids;
        }
    } catch (const std::exception&) {
        return {};
    }
    return out;
}

std::vector<std::string> activesForSet(const std::string& weaponId,
                                      const std::string& armorId,
                                      const std::string& talentId,
                                      const std::map<std::string, std::vector<std::string>>& byItem) {
    std::vector<std::string> out;
    const std::string slots[3] = { weaponId, armorId, talentId };
    for (const std::string& itemId : slots) {
        if (itemId.empty()) continue;
        auto it = byItem.find(itemId);
        if (it == byItem.end()) continue;
        for (const std::string& activeId : it->second)
            if (std::find(out.begin(), out.end(), activeId) == out.end()) out.push_back(activeId);
    }
    return out;
}

std::map<std::string, ActiveRuntime> makeBattleRuntime(const std::map<std::string, ActiveDefinition>& defs) {
    std::map<std::string, ActiveRuntime> rt;
    for (const auto& kv : defs) {
        ActiveRuntime r;
        r.usesLeft = kv.second.usesPerBattle;
        r.cooldownLeft = 0.0f;
        rt[kv.first] = r;
    }
    return rt;
}

int tickActives(std::map<std::string, ActiveRuntime>& rt, float dt) {
    if (dt <= 0.0f) return 0;
    int ready = 0;
    for (auto& kv : rt) {
        ActiveRuntime& r = kv.second;
        if (r.cooldownLeft <= 0.0f) continue;
        r.cooldownLeft -= dt;
        if (r.cooldownLeft <= 0.0f) { r.cooldownLeft = 0.0f; ++ready; }
    }
    return ready;
}

bool canUseActive(const std::string& id,
                  const std::map<std::string, ActiveDefinition>& defs,
                  const std::map<std::string, ActiveRuntime>& rt,
                  const ActiveStatusEffects& st) {
    if (st.silence) return false;                       // st_silence blocks every active
    if (defs.find(id) == defs.end()) return false;      // not a real active
    auto it = rt.find(id);
    if (it == rt.end()) return false;                   // not granted this battle
    if (it->second.usesLeft <= 0) return false;         // once-per-battle spent
    if (it->second.cooldownLeft > 0.0f) return false;   // still cooling
    return true;
}

ActiveEffect useActive(const std::string& id,
                       const std::map<std::string, ActiveDefinition>& defs,
                       std::map<std::string, ActiveRuntime>& rt,
                       ActiveStatusEffects& st) {
    ActiveEffect eff;
    if (!canUseActive(id, defs, rt, st)) return eff;

    const ActiveDefinition& d = defs.at(id);
    ActiveRuntime& r = rt.at(id);
    r.usesLeft -= 1;
    // st_echo: "your next active does NOT go on cooldown" -- consumed by this use.
    if (st.echoQueued) st.echoQueued = false;
    else if (d.cooldownSeconds > 0.0f) r.cooldownLeft = d.cooldownSeconds;

    eff.fired = true;
    eff.id = id;
    eff.damageMultiplier = d.damageMultiplier;
    eff.guaranteedCrit = (d.kind == "guaranteedCrit");
    eff.surviveLethal = (d.kind == "surviveLethal");
    return eff;
}

}  // namespace toms

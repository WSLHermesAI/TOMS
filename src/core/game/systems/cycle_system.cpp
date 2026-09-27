#include "cycle_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

CyclesConfig loadCyclesConfig(const std::string& path) {
    CyclesConfig c;   // documented defaults if the file is missing/malformed
    nlohmann::json j = readJsonFileSafe(path);
    if (!j.is_object()) return c;
    c.maxCycles = j.value("maxCycles", c.maxCycles);
    if (j.contains("carry") && j["carry"].is_object()) {
        const auto& carry = j["carry"];
        if (carry.contains("skills") && carry["skills"].is_object())
            c.skillEffectScale = carry["skills"].value("effectScale", c.skillEffectScale);
        if (carry.contains("atkDefBonus") && carry["atkDefBonus"].is_object())
            c.statBonusScale = carry["atkDefBonus"].value("value", c.statBonusScale);
    }
    return c;
}

} // namespace toms

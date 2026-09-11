#include "equipment_system.h"

namespace toms {

const char* toString(EquipmentSlot s) {
    switch (s) {
        case EquipmentSlot::Weapon: return "weapon";
        case EquipmentSlot::Armor:  return "armor";
        case EquipmentSlot::Talent: return "talent";
    }
    return "weapon";
}

EquipmentSlot equipmentSlotFromString(const std::string& s) {
    if (s == "armor")  return EquipmentSlot::Armor;
    if (s == "talent") return EquipmentSlot::Talent;
    return EquipmentSlot::Weapon;
}

nlohmann::json toJson(const EquipmentDefinition& d) {
    nlohmann::json j;
    j["id"] = d.id; j["name"] = d.name; j["sprite"] = d.sprite; j["desc"] = d.desc;
    j["slot"] = toString(d.slot);
    j["stat"] = { {"atk", d.statAtk}, {"def", d.statDef} };
    j["bar"] = {
        {"v0", d.bar.v0}, {"vmax", d.bar.vmax}, {"rampTime", d.bar.rampTime},
        {"greenHalf", d.bar.greenHalf}, {"blueOuter", d.bar.blueOuter}, {"redOuter", d.bar.redOuter},
        {"maxMult", d.maxMult}
    };
    j["talent"] = d.talent;
    return j;
}

EquipmentDefinition equipmentFromJson(const nlohmann::json& j) {
    EquipmentDefinition d;
    if (!j.is_object()) return d;
    d.id = j.value("id", std::string());
    d.name = j.contains("name") ? j["name"] : nlohmann::json(d.id);
    d.sprite = j.value("sprite", std::string());
    d.desc = j.contains("desc") ? j["desc"] : nlohmann::json("");
    d.slot = equipmentSlotFromString(j.value("slot", std::string("weapon")));
    if (j.contains("stat") && j["stat"].is_object()) {
        d.statAtk = j["stat"].value("atk", 0);
        d.statDef = j["stat"].value("def", 0);
    }
    if (j.contains("bar") && j["bar"].is_object()) {
        auto& b = j["bar"];
        d.bar.v0 = b.value("v0", d.bar.v0);
        d.bar.vmax = b.value("vmax", d.bar.vmax);
        d.bar.rampTime = b.value("rampTime", d.bar.rampTime);
        d.bar.greenHalf = b.value("greenHalf", d.bar.greenHalf);
        d.bar.blueOuter = b.value("blueOuter", d.bar.blueOuter);
        d.bar.redOuter = b.value("redOuter", d.bar.redOuter);
        d.maxMult = b.value("maxMult", d.maxMult);
    }
    d.talent = j.value("talent", std::string());
    return d;
}

static const EquipmentDefinition* findEquipped(const std::string& id,
                                                const std::map<std::string, EquipmentDefinition>& defs) {
    if (id.empty()) return nullptr;
    auto it = defs.find(id);
    return it == defs.end() ? nullptr : &it->second;
}

void applyEquipmentStats(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs,
                          int& atk, int& def) {
    if (const auto* w = findEquipped(eq.weaponId, defs)) atk += w->statAtk;
    if (const auto* a = findEquipped(eq.armorId, defs))  def += a->statDef;
}

PowerBarParams effectiveAttackBar(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs) {
    PowerBarParams bar;   // baseline geometry if nothing equipped
    if (const auto* w = findEquipped(eq.weaponId, defs)) bar = w->bar;
    bar.rampTime *= talentRampTimeMultiplier(eq.talentId);
    return bar;
}

PowerBarParams effectiveDefenseBar(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs) {
    PowerBarParams bar;
    if (const auto* a = findEquipped(eq.armorId, defs)) bar = a->bar;
    bar.rampTime *= talentRampTimeMultiplier(eq.talentId);
    return bar;
}

float effectiveMaxMult(const EquippedSet& eq, const std::map<std::string, EquipmentDefinition>& defs) {
    float maxMult = 2.0f;
    if (const auto* w = findEquipped(eq.weaponId, defs)) maxMult = w->maxMult;
    return maxMult + talentMaxMultBonus(eq.talentId);
}

float talentRampTimeMultiplier(const std::string& talentId) {
    if (talentId == "focus") return 1.25f;
    return 1.0f;
}

float talentMaxMultBonus(const std::string& talentId) {
    if (talentId == "berserker") return 0.15f;
    return 0.0f;
}

bool talentZerosRedZoneAttacks(const std::string& talentId) {
    return talentId == "berserker";
}

float talentDefenseMitigationFloor(const std::string& talentId) {
    if (talentId == "guardian") return 0.10f;
    return 0.0f;
}

} // namespace toms

#pragma once
// Equipment actives (裝備主動技) -- the fourth of the four story-driven systems.
//
// Data lives in:
//   data/actives.json   -- the active definitions themselves (id, kind, multiplier, uses, cooldown)
//   data/equipment.json -- each item's "actives": ["a_..."] list, i.e. which actives it grants
//
// This file is pure logic + tests, exactly like skill_system/forge_system/hub_system: no Stage, no
// Game, no Renderer. The battle code asks it two questions -- "what can be fired right now?" and
// "what does firing it do?" -- and applies the returned effect itself. Keep it that way: the moment
// combat rules live in here, the headless tests stop being able to prove them.
//
// Semantics come from docs/design/ART_AND_ABILITY_DESIGN.md:
//   a_qingxiao_edge    -- once per battle, this attack crits and deals x1.8
//   a_sanctuary_echo   -- once per battle, lethal damage leaves you at 1 HP
//   st_echo            -- your next active does NOT go on cooldown (consumed by that use)
//   st_silence         -- no active may be used at all (the status owns the 6s duration)

#include <map>
#include <string>
#include <vector>

namespace toms {

// ---------------------------------------------------------------------------
// Definitions (data)
// ---------------------------------------------------------------------------

struct ActiveDefinition {
    std::string id;
    std::string kind;                 // "guaranteedCrit" | "surviveLethal"
    float damageMultiplier = 1.0f;
    int   usesPerBattle    = 1;
    float cooldownSeconds  = 0.0f;    // 0 = capped only by usesPerBattle
    int   vfxFrames        = 0;
};

// Reads data/actives.json. Unknown/missing file -> empty map (never throws).
std::map<std::string, ActiveDefinition> loadActiveDefinitions(const std::string& path);

// Reads the "actives" arrays out of data/equipment.json: item id -> active ids.
std::map<std::string, std::vector<std::string>> loadEquipmentActives(const std::string& path);

// The actives a currently-equipped set grants: weapon, then armor, then talent, each item's actives
// in authored order, duplicates removed. Empty slot ids are skipped.
std::vector<std::string> activesForSet(const std::string& weaponId,
                                      const std::string& armorId,
                                      const std::string& talentId,
                                      const std::map<std::string, std::vector<std::string>>& byItem);

// ---------------------------------------------------------------------------
// Runtime (per battle)
// ---------------------------------------------------------------------------

struct ActiveRuntime {
    int   usesLeft     = 0;
    float cooldownLeft = 0.0f;
};

// Status flags that gate actives. st_silence sets silence; st_echo sets echoQueued for one use.
struct ActiveStatusEffects {
    bool silence    = false;
    bool echoQueued = false;
};

// Per-second upkeep: advances cooldowns. Returns how many actives became ready (for a UI ping).
int tickActives(std::map<std::string, ActiveRuntime>& rt, float dt);

// Fresh state for a battle: every definition gets its full uses, no cooldown.
std::map<std::string, ActiveRuntime> makeBattleRuntime(const std::map<std::string, ActiveDefinition>& defs);

// What firing an active does. The caller (combat) owns applying it to damage/HP.
struct ActiveEffect {
    bool  fired            = false;
    bool  guaranteedCrit   = false;
    float damageMultiplier = 1.0f;
    bool  surviveLethal    = false;
    std::string id;
};

// Preconditions, in order: it is a real active, the battle gave it uses, uses remain, no silence,
// cooldown elapsed. Pure -- asks nothing of the caller and changes nothing.
bool canUseActive(const std::string& id,
                  const std::map<std::string, ActiveDefinition>& defs,
                  const std::map<std::string, ActiveRuntime>& rt,
                  const ActiveStatusEffects& st);

// Fires it: consumes one use, starts the cooldown (unless st_echo is queued, which is then consumed),
// and returns the effect for combat to apply. Returns fired=false if canUseActive() said no.
ActiveEffect useActive(const std::string& id,
                       const std::map<std::string, ActiveDefinition>& defs,
                       std::map<std::string, ActiveRuntime>& rt,
                       ActiveStatusEffects& st);

}  // namespace toms

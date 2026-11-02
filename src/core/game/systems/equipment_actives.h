#pragma once
// Equipment actives (裝備主動技) -- the fourth of the four story-driven systems.
//
// Data lives in:
//   data/actives.json   -- the active definitions themselves (id, kind, multiplier, uses, cooldown)
//   data/items.json -- each weapon/armor/talent's "actives": ["a_..."] list, i.e. which actives it grants
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
    std::string m_strId;
    std::string m_strKind;                 // "guaranteedCrit" | "surviveLethal"
    float m_fDamageMultiplier = 1.0f;
    int   m_iUsesPerBattle    = 1;
    float m_fCooldownSeconds  = 0.0f;    // 0 = capped only by usesPerBattle
    int   m_iVfxFrames        = 0;
};

// Reads data/actives.json. Unknown/missing file -> empty map (never throws).
std::map<std::string, ActiveDefinition> f_LoadActiveDefinitions(const std::string& l_strPath);

// Reads the "actives" arrays out of data/items.json: item id -> active ids.
std::map<std::string, std::vector<std::string>> f_LoadEquipmentActives(const std::string& l_strPath);

// The actives a currently-equipped set grants: weapon, then armor, then talent, each item's actives
// in authored order, duplicates removed. Empty slot ids are skipped.
std::vector<std::string> f_ActivesForSet(const std::string& l_strWeaponId,
                                      const std::string& l_strArmorId,
                                      const std::string& l_strTalentId,
                                      const std::map<std::string, std::vector<std::string>>& l_mapByItem);

// ---------------------------------------------------------------------------
// Runtime (per battle)
// ---------------------------------------------------------------------------

struct ActiveRuntime {
    int   m_iUsesLeft     = 0;
    float m_fCooldownLeft = 0.0f;
};

// Status flags that gate actives. st_silence sets silence; st_echo sets echoQueued for one use.
struct ActiveStatusEffects {
    bool m_bSilence    = false;
    bool m_bEchoQueued = false;
};

// Per-second upkeep: advances cooldowns. Returns how many actives became ready (for a UI ping).
int f_TickActives(std::map<std::string, ActiveRuntime>& l_mapRt, float l_fDt);

// Fresh state for a battle: every definition gets its full uses, no cooldown.
std::map<std::string, ActiveRuntime> f_MakeBattleRuntime(const std::map<std::string, ActiveDefinition>& l_mapDefs);

// What firing an active does. The caller (combat) owns applying it to damage/HP.
struct ActiveEffect {
    bool  m_bFired            = false;
    bool  m_bGuaranteedCrit   = false;
    float m_fDamageMultiplier = 1.0f;
    bool  m_bSurviveLethal    = false;
    std::string m_strId;
};

// Preconditions, in order: it is a real active, the battle gave it uses, uses remain, no silence,
// cooldown elapsed. Pure -- asks nothing of the caller and changes nothing.
bool f_CanUseActive(const std::string& l_strId,
                  const std::map<std::string, ActiveDefinition>& l_mapDefs,
                  const std::map<std::string, ActiveRuntime>& l_mapRt,
                  const ActiveStatusEffects& l_oSt);

// Fires it: consumes one use, starts the cooldown (unless st_echo is queued, which is then consumed),
// and returns the effect for combat to apply. Returns fired=false if canUseActive() said no.
ActiveEffect f_UseActive(const std::string& l_strId,
                       const std::map<std::string, ActiveDefinition>& l_mapDefs,
                       std::map<std::string, ActiveRuntime>& l_mapRt,
                       ActiveStatusEffects& l_oSt);

}  // namespace toms

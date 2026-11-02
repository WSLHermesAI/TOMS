// forge_system.h — S5: forging recipes (data/forge.json) and the pure crafting math, per
// STORY_BIBLE.md's 爐火/鍛造 lineage (F18's forge tutorial, ch_07's T3 forge fire) and
// docs/story/STORY_DATA_SCHEMA.md §1.2/§13's M5.
//
// A recipe's own "known" state is NOT stored here -- it lives on MetaSaveData::forgeRecipesKnown
// (permanent, cross-run: STORY_BIBLE.md §8's rebirth table explicitly keeps forge blueprints
// while resetting the crafted gear itself). This file is pure content + pure math, matching
// equipment_system.h/skill_system.h's exact shape -- one more instance of this project's
// "systems now, content later, content is data" pattern, not a new one.
#pragma once
#include <json.hpp>
#include <map>
#include <string>
#include <vector>

namespace toms {

struct ForgeRecipeDefinition {
    std::string m_strId;
    std::string m_strResultEquipmentId;   // an id into equipmentDefs_ (equipment_system.h) -- the
                                      // crafted result; forging is a second, materials-based path
                                      // to the same equipment the Store already sells for gold.
    int m_iCostGold = 0;
    std::map<std::string, int> m_mapMaterials;   // itemId -> count consumed from the player's inventory
    // Raw json (plain string or {code:text} multi-language map) -- resolved via Locale::field()
    // at display time, matching every other content file's name/desc convention.
    nlohmann::json m_jName, m_jDesc;
};

nlohmann::json f_ToJson(const ForgeRecipeDefinition& l_oD);
ForgeRecipeDefinition f_ForgeRecipeFromJson(const nlohmann::json& l_jJ);

// Loads data/forge.json's {id: ForgeRecipeDefinition} map. Missing/malformed file -> empty map
// (never throws), matching readJsonFileSafe's fail-soft convention used everywhere else.
std::map<std::string, ForgeRecipeDefinition> f_LoadForgeRecipes(const std::string& l_strPath);

// True if `recipeId` names a real, known recipe, the player can afford its gold cost, and every
// material it needs appears in `inv` at least as many times as required. Pure predicate -- the UI
// and the actual craft call share this one rule instead of re-deriving it.
bool f_CanCraft(const std::map<std::string, ForgeRecipeDefinition>& l_mapDefs,
              const std::vector<std::string>& l_vKnown, const std::vector<std::string>& l_vInv,
              int l_iGold, const std::string& l_strRecipeId);

} // namespace toms

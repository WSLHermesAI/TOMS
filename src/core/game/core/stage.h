// stage.h — stage data structures and JSON loader for Tower of the Sorcerer.
#pragma once
#include "../../engine/vfs.h"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <json.hpp>
#include "object.h"   // Stage derives Trackable so stage loads are leak-checked
#include "localization.h"   // toms::Locale::field() -- resolves name/story_note if multi-language
#include "footprint.h"      // toms::Footprint -- S1: 1/2/4-grid entities (doc section 1.7)
#include "stair_links.h"    // toms::StairLink -- the floor's stairs, some locked by an event (Q7)

struct Entity {
    int m_iX, m_iY;
    std::string m_strKind;   // from legend: monster:slime, npc:villager, item:gem_atk, stairs_up...
    std::string m_strId;     // resolved id (slime, villager, gem_atk, ...)
    std::string m_strRaw;    // single char
    bool m_bConsumed = false; // items/monsters removed after use
    // Milestone 4 (Encounter Resolution, see src/engine/encounter.h): optional per-tile override
    // of the encounter kind ("direct_battle"/"dialogue_gate"/"story_trigger"/"merchant"),
    // parsed from the stage JSON's optional top-level "encounter_overrides" map (keyed by the
    // tile's raw char). Empty = no override -> resolveEncounterKind() falls back to a default
    // derived from `kind`. No shipped stage sets this yet, so it's empty for every entity today.
    std::string m_strEncounterOverride;
    // ---- S1: 佔格 (footprint) ------------------------------------------------------------------
    // Parsed from the stage JSON's optional top-level "footprints" map (keyed by this tile's raw
    // char, same convention as encounter_overrides). Always 1x1 unless a stage says otherwise, so
    // every shipped stage file keeps parsing exactly as it did before S1 existed.
    toms::Footprint m_oFp;                 // 1x1 / 2x1 / 1x2 / 2x2 tiles
    bool m_bRoamer = false;                // wanders the floor and chases the player (see roamer.h)
    std::string m_strDisplayName;            // banner text for 4-grid entities; empty -> use `id`
    bool m_bFpExplicit = false;            // true when THIS stage file named the footprint for this
                                        // tile char (it then beats the character-level type table)
    // The stage editor's per-object settings (docs/gameplay/02_STAGE_EDITOR.md): the stage file's "objects", keyed "x,y". Its
    // look ({"sprite", "anim": "<file under media>#<clip>", "tint": [r, g, b, a]}; null = its type's look) and the
    // events linked to it, which fire when the player interacts with it (Game::fireLinkedEvents).
    nlohmann::json m_jLook;
    std::vector<std::string> m_vLinks;
    std::vector<std::string> m_vParty;     // the monsters fighting beside this one ("party": ids; docs/gameplay/19 §2.0): the map shows only a hint
    // Its stable id on this floor (objects."x,y".uid, given by the stage editor): what the save keys its status by,
    // so moving it in the editor keeps a defeated monster defeated (docs/gameplay/05_SAVE_SYSTEM.md). Empty in an old file.
    std::string m_strUid;
};

struct Stage : public Trackable {
    std::string m_strId, m_strName, m_strSubtitle;
    int m_iIndex = 0, m_iWidth = 0, m_iHeight = 0;
    std::vector<std::string> m_vTiles;        // rows of chars
    std::map<std::string,std::string> m_mapLegend;
    std::vector<Entity> m_vEntities;
    std::string m_strUp, m_strDown;                  // connected stage ids (the first stair up / down)
    std::vector<toms::StairLink> m_vStairs;   // every stair with its tile and lock (game_assets.cpp builds it)
    int m_iStartX = -1, m_iStartY = -1;          // the '@' tile (player_start), -1 when the grid has none
    std::string m_strStoryNote;
    // The floor's tile theme (the stage file's "theme", e.g. "ice"): walls, floor and stairs are drawn with
    // wall_<theme>, floor_<theme>, stairs_up_<theme>, stairs_down_<theme> where the atlas has them (docs/engine/14_AI_SPRITES.md).
    std::string m_strTheme;

    char f_At(int l_iX, int l_iY) const {
        if (l_iY < 0 || l_iY >= (int)m_vTiles.size()) return '#';
        const auto& l_strR = m_vTiles[l_iY];
        if (l_iX < 0 || l_iX >= (int)l_strR.size()) return '#';
        return l_strR[l_iX];
    }
    TOMS_OBJECT(Stage)
};

// Load a stage JSON file. legend maps char -> semantic; entities parsed from tiles. `locale`
// resolves `name`/`story_note` if they're multi-language {code:text} objects (plain strings
// still work as-is); called fresh on every stage load, always during live gameplay after the
// title phase has set the player's language, so this stays correctly reactive.
inline Stage f_ParseStage(const std::string& l_strPath, const toms::Locale& l_oLocale) {
    // Read the whole file into a string first, then json::parse. Using
    // operator>>(istream, json) directly is unreliable under Emscripten's libc++
    // (it can report "empty input" even though the file is present and non-empty).
    std::string l_strBuf;
    if (!toms::f_VfsReadAll(l_strPath, l_strBuf)) {
        fprintf(stderr, "[parseStage] FAILED to open %s\n", l_strPath.c_str());
        Stage l_oS; l_oS.m_strId = l_strPath; return l_oS;
    }
    if (l_strBuf.empty()) {
        fprintf(stderr, "[parseStage] EMPTY file %s\n", l_strPath.c_str());
        Stage l_oS; l_oS.m_strId = l_strPath; return l_oS;
    }
    nlohmann::json l_jJ;
    try { l_jJ = nlohmann::json::parse(l_strBuf); }
    catch (const std::exception& l_oE) {
        fprintf(stderr, "[parseStage] parse error in %s: %s\n", l_strPath.c_str(), l_oE.what());
        Stage l_oS; l_oS.m_strId = l_strPath; return l_oS;
    }
    Stage l_oS;
    l_oS.m_strId = l_jJ["id"]; l_oS.m_strName = l_oLocale.f_Field(l_jJ["name"]); l_oS.m_strSubtitle = l_jJ["subtitle"];
    l_oS.m_iIndex = l_jJ["index"]; l_oS.m_iWidth = l_jJ["width"]; l_oS.m_iHeight = l_jJ["height"];
    l_oS.m_vTiles = l_jJ["tiles"].get<std::vector<std::string>>();
    for (auto& [l_strK,l_strV] : l_jJ["legend"].get<std::map<std::string,std::string>>()) l_oS.m_mapLegend[l_strK] = l_strV;
    l_oS.m_strUp = l_jJ["connect"]["up"].is_null() ? "" : (std::string)l_jJ["connect"]["up"];
    l_oS.m_strDown = l_jJ["connect"]["down"].is_null() ? "" : (std::string)l_jJ["connect"]["down"];
    l_oS.m_strStoryNote = l_oLocale.f_Field(l_jJ["story_note"]);
    if (l_jJ.contains("theme") && l_jJ["theme"].is_string()) l_oS.m_strTheme = l_jJ["theme"].get<std::string>();
    // Milestone 4: optional per-tile encounter-kind overrides, keyed by raw tile char. Absent
    // from every shipped stage today -- guarded so existing files parse identically to before.
    const nlohmann::json* l_pEncounterOverrides = nullptr;
    if (l_jJ.contains("encounter_overrides") && l_jJ["encounter_overrides"].is_object())
        l_pEncounterOverrides = &l_jJ["encounter_overrides"];
    // S1: optional per-tile footprint overrides, keyed by raw tile char (see footprint.h for the
    // accepted forms). Absent from a file -> every entity stays 1x1.
    const nlohmann::json* l_pFootprints = nullptr;
    if (l_jJ.contains("footprints") && l_jJ["footprints"].is_object())
        l_pFootprints = &l_jJ["footprints"];
    const nlohmann::json* l_pObjects = nullptr;   // per-object look and linked events, keyed "x,y"
    if (l_jJ.contains("objects") && l_jJ["objects"].is_object())
        l_pObjects = &l_jJ["objects"];
    // parse entities
    for (int l_iY = 0; l_iY < (int)l_oS.m_vTiles.size(); l_iY++) {
        for (int l_iX = 0; l_iX < (int)l_oS.m_vTiles[l_iY].size(); l_iX++) {
            char l_cC = l_oS.m_vTiles[l_iY][l_iX];
            if (l_cC == '@') { l_oS.m_iStartX = l_iX; l_oS.m_iStartY = l_iY; continue; }   // where a new game / a fresh load starts
            if (l_cC == '#' || l_cC == '.') continue;
            auto l_itIt = l_oS.m_mapLegend.find(std::string(1, l_cC));
            if (l_itIt == l_oS.m_mapLegend.end()) continue;
            Entity l_oE; l_oE.m_iX = l_iX; l_oE.m_iY = l_iY; l_oE.m_strRaw = std::string(1,l_cC); l_oE.m_strKind = l_itIt->second;
            // resolve id: e.g. "monster:slime" -> "slime", "npc:villager" -> "villager", "item:gem_atk" -> "gem_atk"
            auto l_u64Pos = l_oE.m_strKind.find(':');
            l_oE.m_strId = (l_u64Pos == std::string::npos) ? l_oE.m_strKind : l_oE.m_strKind.substr(l_u64Pos+1);
            // The generated floors and the stage editors write a key as "key:<colour>"; the game handles keys as the
            // items key_<colour> (data/items.json: +1 key on contact). Without this a "key:yellow" tile was neither
            // drawn nor picked up, so its door could never open.
            if (l_oE.m_strKind.rfind("key:", 0) == 0) { l_oE.m_strId = "key_" + l_oE.m_strId; l_oE.m_strKind = "item:" + l_oE.m_strId; }
            if (l_pEncounterOverrides && l_pEncounterOverrides->contains(l_oE.m_strRaw) && (*l_pEncounterOverrides)[l_oE.m_strRaw].is_string())
                l_oE.m_strEncounterOverride = (*l_pEncounterOverrides)[l_oE.m_strRaw].get<std::string>();
            if (l_pFootprints && l_pFootprints->contains(l_oE.m_strRaw)) {
                toms::FootprintSpec l_oSpec = toms::f_FootprintSpecFromJson((*l_pFootprints)[l_oE.m_strRaw], l_oE.m_strRaw);
                l_oE.m_oFp = l_oSpec.m_oFp;
                l_oE.m_bRoamer = l_oSpec.m_bRoamer && l_oSpec.m_oFp.f_Big();   // a 1x1 "roamer" has nowhere to roam
                l_oE.m_strDisplayName = l_oSpec.m_strName;
                l_oE.m_bFpExplicit = true;
            }
            if (l_pObjects) {
                auto l_oO = l_pObjects->find(std::to_string(l_iX) + "," + std::to_string(l_iY));
                if (l_oO != l_pObjects->end() && l_oO->is_object()) {
                    if (l_oO->contains("look") && (*l_oO)["look"].is_object()) l_oE.m_jLook = (*l_oO)["look"];
                    if (l_oO->contains("uid") && (*l_oO)["uid"].is_string()) l_oE.m_strUid = (*l_oO)["uid"].get<std::string>();
                    if (l_oO->contains("party") && (*l_oO)["party"].is_array())
                        for (const auto& l_jId : (*l_oO)["party"]) if (l_jId.is_string()) l_oE.m_vParty.push_back(l_jId.get<std::string>());
                    if (l_oO->contains("events") && (*l_oO)["events"].is_array())
                        for (const auto& l_jId : (*l_oO)["events"]) if (l_jId.is_string()) l_oE.m_vLinks.push_back(l_jId.get<std::string>());
                }
            }
            l_oS.m_vEntities.push_back(l_oE);
        }
    }
    return l_oS;
}

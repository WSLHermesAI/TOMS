// encounter.h — Encounter Resolution: decides what happens when a stage tile with an enemy/NPC
// is entered (direct battle, dialogue-first, story-only, or a merchant), per
// docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §5.3 and docs/IMPLEMENTATION_ROADMAP.md
// Milestone 4.
//
// Scope note (owner decision, this session): `direct_battle` stays the default for every
// existing monster tile — no shipped stage data sets an override, so resolveEncounterKind()
// returns DirectBattle for all of them today, unchanged from current behavior.
// `dialogue_gate` is fully wired and tested, but opt-in only, via a stage's optional
// "encounter_overrides" map (see src/game/stage.h's Entity::encounterOverride).
#pragma once
#include <string>

namespace toms {

enum class EncounterKind {
    DirectBattle,   // tile enter -> straight into combat (today's only behavior)
    DialogueGate,   // tile enter -> dialogue first; a choice's action.enterBattle may start combat
    StoryTrigger,   // tile enter -> dialogue only, no combat path exists (today's NPC behavior)
    Merchant,       // tile enter -> opens the shop
};

const char* toString(EncounterKind k);
// Returns false (out left unchanged) if `s` doesn't match a known encounter-kind string.
bool tryParseEncounterKind(const std::string& s, EncounterKind& out);
// Convenience: unrecognized/empty input defaults to DirectBattle.
EncounterKind fromString(const std::string& s);

// Resolves the effective encounter kind for an entity on a stage tile:
//  - a recognized `overrideStr` (from the stage JSON's optional per-tile "encounter_overrides"
//    map) always wins;
//  - otherwise falls back to a default derived from `entityKind`'s "kind:" prefix
//    ("monster:" -> DirectBattle, "npc:" -> StoryTrigger, anything else -> DirectBattle).
// An unrecognized (e.g. misspelled) overrideStr is treated the same as "no override" — it
// falls through to the kind-derived default rather than silently becoming DirectBattle
// regardless of entity type.
EncounterKind resolveEncounterKind(const std::string& entityKind, const std::string& overrideStr);

} // namespace toms

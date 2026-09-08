// encounter_resolve_test.cpp — headless verification of Encounter Resolution (Milestone 4).
// Exits 0 on success, 1 on any CHECK failure. Run: ./encounter_resolve_test
#include "encounter.h"
#include <cstdio>

using namespace toms;

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", msg); g_fail++; } } while(0)

int main() {
    // 1. toString covers all 4 kinds with the documented string values.
    CHECK(std::string(toString(EncounterKind::DirectBattle)) == "direct_battle", "toString DirectBattle");
    CHECK(std::string(toString(EncounterKind::DialogueGate)) == "dialogue_gate", "toString DialogueGate");
    CHECK(std::string(toString(EncounterKind::StoryTrigger)) == "story_trigger", "toString StoryTrigger");
    CHECK(std::string(toString(EncounterKind::Merchant))     == "merchant",      "toString Merchant");

    // 2. tryParseEncounterKind round-trips for all 4, and rejects garbage.
    for (auto k : {EncounterKind::DirectBattle, EncounterKind::DialogueGate, EncounterKind::StoryTrigger, EncounterKind::Merchant}) {
        EncounterKind parsed;
        bool ok = tryParseEncounterKind(toString(k), parsed);
        CHECK(ok && parsed == k, "tryParseEncounterKind round-trips toString(k)");
    }
    EncounterKind dummy;
    CHECK(!tryParseEncounterKind("bogus_typo", dummy), "tryParseEncounterKind rejects an unrecognized string");
    CHECK(!tryParseEncounterKind("", dummy), "tryParseEncounterKind rejects an empty string");

    // 3. encounterKindFromString defaults unrecognized/empty input to DirectBattle.
    CHECK(encounterKindFromString("bogus_typo") == EncounterKind::DirectBattle, "encounterKindFromString defaults garbage to DirectBattle");
    CHECK(encounterKindFromString("") == EncounterKind::DirectBattle, "encounterKindFromString defaults empty to DirectBattle");
    CHECK(encounterKindFromString("merchant") == EncounterKind::Merchant, "encounterKindFromString parses a valid string correctly");

    // 4. resolveEncounterKind: kind-derived defaults with no override (this is the state of
    //    every shipped stage today — no data/stages/*.json file sets encounter_overrides).
    CHECK(resolveEncounterKind("monster:slime", "") == EncounterKind::DirectBattle,
          "monster with no override -> DirectBattle (today's actual default, unchanged)");
    CHECK(resolveEncounterKind("npc:villager", "") == EncounterKind::StoryTrigger,
          "npc with no override -> StoryTrigger (today's actual NPC behavior)");
    CHECK(resolveEncounterKind("item:gem_atk", "") == EncounterKind::DirectBattle,
          "a non-monster/non-npc kind falls back to a defined, harmless default");

    // 5. a recognized override always wins, for either entity kind.
    CHECK(resolveEncounterKind("monster:golem", "dialogue_gate") == EncounterKind::DialogueGate,
          "recognized override wins over the monster default");
    CHECK(resolveEncounterKind("npc:villager", "merchant") == EncounterKind::Merchant,
          "recognized override wins over the npc default, even to a different kind entirely");

    // 6. an unrecognized override string falls through to the kind-derived default, rather
    //    than silently becoming DirectBattle regardless of entity type.
    CHECK(resolveEncounterKind("npc:villager", "not_a_real_kind") == EncounterKind::StoryTrigger,
          "garbage override on an npc falls through to StoryTrigger, not DirectBattle");
    CHECK(resolveEncounterKind("monster:slime", "not_a_real_kind") == EncounterKind::DirectBattle,
          "garbage override on a monster falls through to its own default (DirectBattle)");

    if (g_fail == 0) { printf("encounter_resolve_test: ALL PASS (20 checks)\n"); return 0; }
    printf("encounter_resolve_test: %d CHECK(s) FAILED\n", g_fail);
    return 1;
}

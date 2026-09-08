#include "encounter.h"

namespace toms {

const char* toString(EncounterKind k) {
    switch (k) {
        case EncounterKind::DirectBattle: return "direct_battle";
        case EncounterKind::DialogueGate: return "dialogue_gate";
        case EncounterKind::StoryTrigger: return "story_trigger";
        case EncounterKind::Merchant:     return "merchant";
    }
    return "direct_battle";
}

bool tryParseEncounterKind(const std::string& s, EncounterKind& out) {
    if (s == "direct_battle") { out = EncounterKind::DirectBattle; return true; }
    if (s == "dialogue_gate") { out = EncounterKind::DialogueGate; return true; }
    if (s == "story_trigger") { out = EncounterKind::StoryTrigger; return true; }
    if (s == "merchant")      { out = EncounterKind::Merchant;     return true; }
    return false;
}

EncounterKind encounterKindFromString(const std::string& s) {
    EncounterKind k;
    return tryParseEncounterKind(s, k) ? k : EncounterKind::DirectBattle;
}

EncounterKind resolveEncounterKind(const std::string& entityKind, const std::string& overrideStr) {
    EncounterKind k;
    if (!overrideStr.empty() && tryParseEncounterKind(overrideStr, k)) return k;
    if (entityKind.rfind("monster:", 0) == 0) return EncounterKind::DirectBattle;
    if (entityKind.rfind("npc:", 0) == 0)     return EncounterKind::StoryTrigger;
    return EncounterKind::DirectBattle;
}

} // namespace toms

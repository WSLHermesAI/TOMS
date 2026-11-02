#include "encounter.h"

namespace toms {

const char* f_ToString(EncounterKind l_eK) {
    switch (l_eK) {
        case EncounterKind::DirectBattle: return "direct_battle";
        case EncounterKind::DialogueGate: return "dialogue_gate";
        case EncounterKind::StoryTrigger: return "story_trigger";
        case EncounterKind::Merchant:     return "merchant";
    }
    return "direct_battle";
}

bool f_TryParseEncounterKind(const std::string& l_strS, EncounterKind& l_eOut) {
    if (l_strS == "direct_battle") { l_eOut = EncounterKind::DirectBattle; return true; }
    if (l_strS == "dialogue_gate") { l_eOut = EncounterKind::DialogueGate; return true; }
    if (l_strS == "story_trigger") { l_eOut = EncounterKind::StoryTrigger; return true; }
    if (l_strS == "merchant")      { l_eOut = EncounterKind::Merchant;     return true; }
    return false;
}

EncounterKind f_EncounterKindFromString(const std::string& l_strS) {
    EncounterKind l_eK;
    return f_TryParseEncounterKind(l_strS, l_eK) ? l_eK : EncounterKind::DirectBattle;
}

EncounterKind f_ResolveEncounterKind(const std::string& l_strEntityKind, const std::string& l_strOverrideStr) {
    EncounterKind l_eK;
    if (!l_strOverrideStr.empty() && f_TryParseEncounterKind(l_strOverrideStr, l_eK)) return l_eK;
    if (l_strEntityKind.rfind("monster:", 0) == 0) return EncounterKind::DirectBattle;
    if (l_strEntityKind.rfind("npc:", 0) == 0)     return EncounterKind::StoryTrigger;
    return EncounterKind::DirectBattle;
}

} // namespace toms

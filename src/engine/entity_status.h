// entity_status.h — per-stage-tile runtime status for enemies/items/doors/NPCs, persisted via
// RunSaveData::entityStatus (src/game/save_system.h). See
// docs/GAME_LOGIC_AND_RENDERING_ARCHITECTURE.md §5.2 and docs/IMPLEMENTATION_ROADMAP.md
// Milestone 3.
//
// Stored as plain strings in the map (not a typed struct) so it round-trips through JSON via
// the exact map<string,string> shape save_system.h already persists — see toString/fromString.
#pragma once
#include <map>
#include <string>

namespace toms {

enum class EntityStatus {
    Untouched,   // default: renders normally, fully interactive
    Engaged,     // combat or dialogue currently open on this instance
    Defeated,    // enemy killed; tile permanently cleared
    Collected,   // item picked up; tile permanently cleared
    Opened,      // door consumed its key; stays open on revisit
    Hidden,      // not yet visible/interactive -- gated behind a story flag or mission state
};

const char* toString(EntityStatus s);
// Unrecognized/garbage strings map to Untouched -- fail-safe default, never fail-locked-out.
EntityStatus fromString(const std::string& s);

// key format: "<stageId>|<x>,<y>" -- matches the convention already used by save_test.cpp and
// architecture-doc §5.2.
std::string entityStatusKey(const std::string& stageId, int x, int y);

// Convenience accessors over the plain map RunSaveData::entityStatus already stores.
EntityStatus getEntityStatus(const std::map<std::string, std::string>& statusMap, const std::string& key);
void setEntityStatus(std::map<std::string, std::string>& statusMap, const std::string& key, EntityStatus s);

} // namespace toms

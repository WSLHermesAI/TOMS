#include "entity_status.h"

namespace toms {

const char* toString(EntityStatus s) {
    switch (s) {
        case EntityStatus::Untouched: return "Untouched";
        case EntityStatus::Engaged:   return "Engaged";
        case EntityStatus::Defeated:  return "Defeated";
        case EntityStatus::Collected: return "Collected";
        case EntityStatus::Opened:    return "Opened";
        case EntityStatus::Hidden:    return "Hidden";
    }
    return "Untouched";
}

EntityStatus fromString(const std::string& s) {
    if (s == "Engaged")   return EntityStatus::Engaged;
    if (s == "Defeated")  return EntityStatus::Defeated;
    if (s == "Collected") return EntityStatus::Collected;
    if (s == "Opened")    return EntityStatus::Opened;
    if (s == "Hidden")    return EntityStatus::Hidden;
    return EntityStatus::Untouched;
}

std::string entityStatusKey(const std::string& stageId, int x, int y) {
    return stageId + "|" + std::to_string(x) + "," + std::to_string(y);
}

EntityStatus getEntityStatus(const std::map<std::string, std::string>& statusMap, const std::string& key) {
    auto it = statusMap.find(key);
    return it == statusMap.end() ? EntityStatus::Untouched : fromString(it->second);
}

void setEntityStatus(std::map<std::string, std::string>& statusMap, const std::string& key, EntityStatus s) {
    statusMap[key] = toString(s);
}

} // namespace toms

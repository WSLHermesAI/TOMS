#include "entity_status.h"

namespace toms {

const char* f_ToString(EntityStatus l_eS) {
    switch (l_eS) {
        case EntityStatus::Untouched: return "Untouched";
        case EntityStatus::Engaged:   return "Engaged";
        case EntityStatus::Defeated:  return "Defeated";
        case EntityStatus::Collected: return "Collected";
        case EntityStatus::Opened:    return "Opened";
        case EntityStatus::Hidden:    return "Hidden";
    }
    return "Untouched";
}

EntityStatus f_FromString(const std::string& l_strS) {
    if (l_strS == "Engaged")   return EntityStatus::Engaged;
    if (l_strS == "Defeated")  return EntityStatus::Defeated;
    if (l_strS == "Collected") return EntityStatus::Collected;
    if (l_strS == "Opened")    return EntityStatus::Opened;
    if (l_strS == "Hidden")    return EntityStatus::Hidden;
    return EntityStatus::Untouched;
}

std::string f_EntityStatusKey(const std::string& l_strStageId, int l_iX, int l_iY) {
    return l_strStageId + "|" + std::to_string(l_iX) + "," + std::to_string(l_iY);
}

std::string f_EntityStatusKey(const std::string& l_strStageId, int l_iX, int l_iY, const std::string& l_strUid) {
    return l_strUid.empty() ? f_EntityStatusKey(l_strStageId, l_iX, l_iY) : l_strStageId + "#" + l_strUid;
}

EntityStatus f_GetEntityStatus(const std::map<std::string, std::string>& l_mapStatusMap, const std::string& l_strKey) {
    auto l_itIt = l_mapStatusMap.find(l_strKey);
    return l_itIt == l_mapStatusMap.end() ? EntityStatus::Untouched : f_FromString(l_itIt->second);
}

void f_SetEntityStatus(std::map<std::string, std::string>& l_mapStatusMap, const std::string& l_strKey, EntityStatus l_eS) {
    l_mapStatusMap[l_strKey] = f_ToString(l_eS);
}

} // namespace toms

// version_label.h -- what the title screen shows for assets/data/version.json ({"number": 12, "sha": "abc1234"}, written by
// tools\bump_version.ps1 at each web publish): "v12", or nothing when there is no valid number.
#pragma once
#include <json.hpp>

#include <string>

namespace toms {

inline std::string f_VersionLabel(const nlohmann::json& l_jVersion) {
    if (!l_jVersion.is_object() || !l_jVersion.contains("number") || !l_jVersion["number"].is_number_integer()) return "";
    const int l_iNumber = l_jVersion["number"].get<int>();
    return l_iNumber > 0 ? "v" + std::to_string(l_iNumber) : "";
}

}  // namespace toms

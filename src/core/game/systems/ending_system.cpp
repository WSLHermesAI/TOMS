#include "ending_system.h"
#include "save_system.h"   // toms::readJsonFileSafe

namespace toms {

nlohmann::json f_ToJson(const EndingDefinition& l_oD) {
    nlohmann::json l_jJ;
    l_jJ["id"] = l_oD.m_strId; l_jJ["name"] = l_oD.m_strNameKey; l_jJ["text"] = l_oD.m_strTextKey; l_jJ["tone"] = l_oD.m_strTone;
    l_jJ["unlocksHint"] = l_oD.m_strUnlocksHint; l_jJ["allowsRebirth"] = l_oD.m_bAllowsRebirth;
    l_jJ["requires"] = l_oD.m_jRequires;
    return l_jJ;
}

EndingDefinition f_EndingFromJson(const nlohmann::json& l_jJ) {
    EndingDefinition l_oD;
    if (!l_jJ.is_object()) return l_oD;
    l_oD.m_strId = l_jJ.value("id", std::string());
    l_oD.m_strNameKey = l_jJ.value("name", std::string());
    l_oD.m_strTextKey = l_jJ.value("text", std::string());
    l_oD.m_strTone = l_jJ.value("tone", std::string());
    l_oD.m_strUnlocksHint = l_jJ.value("unlocksHint", std::string());
    l_oD.m_bAllowsRebirth = l_jJ.value("allowsRebirth", false);
    l_oD.m_jRequires = l_jJ.contains("requires") ? l_jJ["requires"] : nlohmann::json();
    return l_oD;
}

EndingsTable f_LoadEndings(const std::string& l_strPath) {
    EndingsTable l_oT;
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (!l_jJ.is_object()) return l_oT;
    l_oT.m_strFallback = l_jJ.value("fallback", std::string());
    if (l_jJ.contains("endings") && l_jJ["endings"].is_array())
        for (auto& l_jE : l_jJ["endings"]) {
            EndingDefinition l_oD = f_EndingFromJson(l_jE);
            if (!l_oD.m_strId.empty()) l_oT.m_vEndings.push_back(l_oD);
        }
    return l_oT;
}

static const EndingDefinition* f_FindEnding(const EndingsTable& l_oTable, const std::string& l_strId) {
    for (auto& l_oE : l_oTable.m_vEndings) if (l_oE.m_strId == l_strId) return &l_oE;
    return nullptr;
}

const EndingDefinition* f_JudgeEnding(const EndingsTable& l_oTable, const ConditionContext& l_oCtx) {
    for (auto& l_oE : l_oTable.m_vEndings)
        if (f_Evaluate(l_oE.m_jRequires, l_oCtx)) return &l_oE;
    return f_FindEnding(l_oTable, l_oTable.m_strFallback);
}

const EndingDefinition* f_CheckNamedEndings(const EndingsTable& l_oTable, const std::vector<std::string>& l_vOrder,
                                           const ConditionContext& l_oCtx) {
    for (auto& l_strId : l_vOrder) {
        const EndingDefinition* l_pE = f_FindEnding(l_oTable, l_strId);
        if (l_pE && f_Evaluate(l_pE->m_jRequires, l_oCtx)) return l_pE;
    }
    return nullptr;
}

} // namespace toms

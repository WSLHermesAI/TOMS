#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_settings.h"
#include "save_system.h"

namespace toms {

nlohmann::json f_ToJson(const GameSettings& l_oS) {
    nlohmann::json l_jJ;
    l_jJ[toms::field::g_pszSchemaVersion] = l_oS.m_iSchemaVersion;
    l_jJ[toms::field::g_pszLanguage] = l_oS.m_strLanguage;
    l_jJ[toms::field::g_pszSlotCount] = l_oS.m_iSlotCount;
    l_jJ[toms::field::g_pszUiFontScale] = l_oS.m_fUiFontScale;
    l_jJ[toms::field::g_pszArtStyle] = l_oS.m_strArtStyle;
    if (!l_oS.m_strPlayerLook.empty()) l_jJ[toms::field::g_pszPlayerLook] = l_oS.m_strPlayerLook;
    l_jJ[toms::field::g_pszParticleGpuThreshold] = l_oS.m_iParticleGpuThreshold;
    l_jJ[toms::field::g_pszReduceEffects] = l_oS.m_bReduceEffects;
    l_jJ[toms::field::g_pszBattle3d] = l_oS.m_iBattle3D;
    l_jJ[toms::field::g_pszBattleSpeed] = l_oS.m_fBattleSpeed;
    l_jJ[toms::field::g_pszViewScale] = l_oS.m_fViewScale;
    l_jJ[toms::field::g_pszBowTutorialDone] = l_oS.m_bBowTutorialDone;
    return l_jJ;
}

GameSettings f_GameSettingsFromJson(const nlohmann::json& l_jJ) {
    GameSettings l_oS;
    if (!l_jJ.is_object()) return l_oS;
    l_oS.m_iSchemaVersion = l_jJ.value(toms::field::g_pszSchemaVersion, g_iGameSettingsSchemaVersion);
    std::string l_strLang = l_jJ.value(toms::field::g_pszLanguage, std::string("zh_TW"));
    if (!l_strLang.empty()) l_oS.m_strLanguage = l_strLang;
    if (l_oS.m_iSchemaVersion < 3) l_oS.m_strLanguage = "zh_TW";   // saved while English was the default: Traditional Chinese now (v3)
    int l_iSlots = l_jJ.value(toms::field::g_pszSlotCount, 3);
    if (l_iSlots < 1) l_iSlots = 1;
    if (l_iSlots > 8) l_iSlots = 8;      // the Continue page lays out at most 8 rows
    l_oS.m_iSlotCount = l_iSlots;
    float l_fFs = l_jJ.value(toms::field::g_pszUiFontScale, 1.5f);
    if (l_fFs < 0.5f || l_fFs > 4.0f) l_fFs = 1.5f;
    l_oS.m_fUiFontScale = l_fFs;
    l_oS.m_strArtStyle = l_jJ.value(toms::field::g_pszArtStyle, std::string());
    l_oS.m_strPlayerLook = l_jJ.value(toms::field::g_pszPlayerLook, std::string());
    l_oS.m_iParticleGpuThreshold = std::max(0, l_jJ.value(toms::field::g_pszParticleGpuThreshold, g_iDefaultParticleGpuThreshold));
    l_oS.m_bReduceEffects = l_jJ.value(toms::field::g_pszReduceEffects, false);
    l_oS.m_iBattle3D = std::clamp(l_jJ.value(toms::field::g_pszBattle3d, 0), 0, 2);
    const float l_fSpeed = l_jJ.value(toms::field::g_pszBattleSpeed, 1.0f);
    l_oS.m_fBattleSpeed = l_fSpeed >= 0.25f && l_fSpeed <= 3.0f ? l_fSpeed : 1.0f;
    const float l_fView = l_jJ.value(toms::field::g_pszViewScale, 1.5f);
    l_oS.m_fViewScale = l_fView >= 0.5f && l_fView <= 4.0f ? l_fView : 1.5f;
    l_oS.m_bBowTutorialDone = l_jJ.value(toms::field::g_pszBowTutorialDone, false);
    return l_oS;
}

GameSettings f_LoadGameSettings(const std::string& l_strPath, bool* l_pFound) {
    if (l_pFound) *l_pFound = false;
    nlohmann::json l_jJ = f_ReadJsonFileSafe(l_strPath);
    if (l_jJ.is_null()) return GameSettings{};
    if (l_pFound) *l_pFound = true;
    return f_GameSettingsFromJson(l_jJ);
}

bool f_SaveGameSettings(const std::string& l_strPath, const GameSettings& l_oS) {
    GameSettings l_oCopy = l_oS;
    l_oCopy.m_iSchemaVersion = g_iGameSettingsSchemaVersion;
    return f_WriteJsonAtomic(l_strPath, f_ToJson(l_oCopy));
}

} // namespace toms

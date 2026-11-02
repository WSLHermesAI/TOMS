#include "game_settings.h"
#include "save_system.h"

namespace toms {

nlohmann::json f_ToJson(const GameSettings& l_oS) {
    nlohmann::json l_jJ;
    l_jJ["schemaVersion"] = l_oS.m_iSchemaVersion;
    l_jJ["language"] = l_oS.m_strLanguage;
    l_jJ["slotCount"] = l_oS.m_iSlotCount;
    l_jJ["uiFontScale"] = l_oS.m_fUiFontScale;
    l_jJ["cameraMode"] = l_oS.m_iCameraMode;
    l_jJ["viewCols"] = l_oS.m_iViewCols;
    l_jJ["artStyle"] = l_oS.m_strArtStyle;
    if (!l_oS.m_strPlayerLook.empty()) l_jJ["playerLook"] = l_oS.m_strPlayerLook;
    l_jJ["particleGpuThreshold"] = l_oS.m_iParticleGpuThreshold;
    l_jJ["reduceEffects"] = l_oS.m_bReduceEffects;
    l_jJ["bowTutorialDone"] = l_oS.m_bBowTutorialDone;
    return l_jJ;
}

GameSettings f_GameSettingsFromJson(const nlohmann::json& l_jJ) {
    GameSettings l_oS;
    if (!l_jJ.is_object()) return l_oS;
    l_oS.m_iSchemaVersion = l_jJ.value("schemaVersion", g_iGameSettingsSchemaVersion);
    std::string l_strLang = l_jJ.value("language", std::string("zh_TW"));
    if (!l_strLang.empty()) l_oS.m_strLanguage = l_strLang;
    if (l_oS.m_iSchemaVersion < 3) l_oS.m_strLanguage = "zh_TW";   // saved while English was the default: Traditional Chinese now (v3)
    int l_iSlots = l_jJ.value("slotCount", 3);
    if (l_iSlots < 1) l_iSlots = 1;
    if (l_iSlots > 8) l_iSlots = 8;      // the Continue page lays out at most 8 rows
    l_oS.m_iSlotCount = l_iSlots;
    float l_fFs = l_jJ.value("uiFontScale", 1.5f);
    if (l_fFs < 0.5f || l_fFs > 4.0f) l_fFs = 1.5f;
    l_oS.m_fUiFontScale = l_fFs;
    int l_iCam = l_jJ.value("cameraMode", 0);
    l_oS.m_iCameraMode = (l_iCam == 1) ? 1 : 0;   // unrecognized/missing -> Follow
    int l_iVc = l_jJ.value("viewCols", 13);
    if (l_iVc < 4) l_iVc = 4;      // cameraViewportTiles() floors at 4 anyway; keep the setting sane
    if (l_iVc > 60) l_iVc = 60;    // past this a tile is a couple px, not a testing scenario
    l_oS.m_iViewCols = l_iVc;
    l_oS.m_strArtStyle = l_jJ.value("artStyle", std::string());
    l_oS.m_strPlayerLook = l_jJ.value("playerLook", std::string());
    l_oS.m_iParticleGpuThreshold = std::max(0, l_jJ.value("particleGpuThreshold", g_iDefaultParticleGpuThreshold));
    l_oS.m_bReduceEffects = l_jJ.value("reduceEffects", false);
    l_oS.m_bBowTutorialDone = l_jJ.value("bowTutorialDone", false);
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

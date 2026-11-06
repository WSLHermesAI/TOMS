// game_ui.cpp -- the UI contract (see ui/ui_state.h): buildUiState() describes every screen as
// plain data each frame, uiEvent() carries out what a button in assets/media/ui/*.rml asked for.
// Replaced the hand-drawn UI (drawTitleScreen, drawInventory, drawStoreUI, ...) on 2026-09-27;
// every rule still lives in the Game functions these call.
#include "game_internal.h"

#include <cmath>

using namespace toms::game_detail;

namespace {

std::string f_FmtPlayTime(int l_iSec) {
    if (l_iSec < 0) l_iSec = 0;
    char l_pszB[32];
    std::snprintf(l_pszB, sizeof(l_pszB), "%02d:%02d:%02d", l_iSec / 3600, (l_iSec / 60) % 60, l_iSec % 60);
    return l_pszB;
}

float f_Pct(float l_fNum, float l_fDen) {
    if (l_fDen <= 0.0f) return 0.0f;
    return std::max(0.0f, std::min(100.0f, 100.0f * l_fNum / l_fDen));
}

toms::UiPowerBar f_PowerBar(const toms::PowerBarParams& l_oP, const CombatState::AutoBar& l_oBar,
                          const std::string& l_strPrompt, const std::string& l_strLabel) {
    toms::UiPowerBar l_oOut;
    l_oOut.m_strPrompt = l_strPrompt;
    l_oOut.m_strButtonLabel = l_strLabel;
    l_oOut.m_bCooling = l_oBar.m_bCooling;
    const float l_fSpan = 2.0f * l_oP.m_fRedOuter;
    if (l_fSpan <= 0.0f) return l_oOut;
    l_oOut.m_fRedL = l_oOut.m_fRedR = f_Pct(l_oP.m_fRedOuter - l_oP.m_fBlueOuter, l_fSpan);
    l_oOut.m_fBlueL = l_oOut.m_fBlueR = f_Pct(l_oP.m_fBlueOuter - l_oP.m_fGreenHalf, l_fSpan);
    l_oOut.m_fGreen = f_Pct(2.0f * l_oP.m_fGreenHalf, l_fSpan);
    l_oOut.m_fMarker = f_Pct(l_oBar.m_fPos, l_fSpan);
    return l_oOut;
}

}  // namespace

// atlas sprite id (or "<id>.png") -> the sprite name the .rml files use (uiSpritesheet). The art
// style in use is the atlas loaded, so the UI matches the map without naming the style.
std::string Game::f_UiSprite(std::string l_strId) const {
    if (l_strId.empty()) l_strId = "coin";
    if (l_strId.size() > 4 && l_strId.compare(l_strId.size() - 4, 4, ".png") == 0) l_strId.resize(l_strId.size() - 4);
    return toms::f_RmlSpriteName(l_strId);
}

// The page images are named as seen from assets/media/ui/, where the .rml documents are.
std::string Game::f_UiSpritesheet() const {
    if (m_oSpriteAtlas.m_vPages.empty()) return std::string();
    const std::string l_strDir = m_iLoadedArtStyle > 0 ? "../styles/" + m_vArtStyles[m_iLoadedArtStyle].m_strId + "/atlas/" : "../atlas/";
    return toms::f_RmlSpritesheets(m_oSpriteAtlas, l_strDir);
}

std::vector<std::string> Game::f_MissingUiSprites() const {
    std::vector<std::string> l_vNames(g_arrSPRITEORDER, g_arrSPRITEORDER + g_iNSPRITES);
    for (const auto& l_pairKv : m_mapItemDefs) l_vNames.push_back(l_pairKv.second.value("sprite", std::string()));

    std::vector<std::string> l_vMissing;
    for (std::string l_strN : l_vNames) {
        if (l_strN.empty()) continue;
        if (l_strN.size() > 4 && l_strN.compare(l_strN.size() - 4, 4, ".png") == 0) l_strN.resize(l_strN.size() - 4);
        if (!m_oSpriteAtlas.f_Find(l_strN) && std::find(l_vMissing.begin(), l_vMissing.end(), l_strN) == l_vMissing.end()) l_vMissing.push_back(l_strN);
    }
    return l_vMissing;
}

std::string Game::f_ArtStyleName(int l_iIdx) const {
    if (l_iIdx <= 0 || l_iIdx >= (int)m_vArtStyles.size()) return m_oLocale.f_Tr("settings.style_original");
    return m_oLocale.f_Field(m_vArtStyles[l_iIdx].m_jName);
}

// The Settings pages' art style section (none when only the original art exists). `selected` is
// the highlighted style index (out of range = none). The chosen style is ticked; while it is not
// the one in use (chosen during a run), its row says it applies back on the title screen.
void Game::f_AppendArtStyleRows(std::vector<toms::UiRow>& l_vRows, int l_iSelected) const {
    if (m_vArtStyles.size() < 2) return;
    const int l_iChosen = f_ChosenArtStyle();
    for (int l_iI = 0; l_iI < (int)m_vArtStyles.size(); l_iI++) {
        toms::UiRow l_oR;
        if (l_iI == 0) l_oR.m_strSection = m_oLocale.f_Tr("settings.style_section");
        l_oR.m_strLabel = std::string(l_iI == l_iChosen ? "[x] " : "[ ] ") + f_ArtStyleName(l_iI);
        if (l_iI == l_iChosen && l_iChosen != m_iLoadedArtStyle) l_oR.m_strSub = m_oLocale.f_Tr("settings.style_pending");
        l_oR.m_bSelected = l_iI == l_iSelected;
        l_vRows.push_back(l_oR);
    }
}

std::string Game::f_ItemSprite(const std::string& l_strId) const {
    auto l_itIt = m_mapItemDefs.find(l_strId);
    return f_UiSprite(l_itIt == m_mapItemDefs.end() ? "coin" : l_itIt->second.value("sprite", std::string("coin.png")));
}

std::string Game::f_ItemEffectSummary(const std::string& l_strId) const {
    // "HP +40 • ATK +1 • STR +2": the item's stats (data/items.json), in a fixed order, then the
    // attributes of data/stats.json by their short names.
    const nlohmann::json& l_jEff = f_ItemStats(l_strId);
    std::vector<std::string> l_vParts;
    auto l_fnAdd = [&](const std::string& l_strKey, const std::string& l_strLabel) {
        if (l_jEff.contains(l_strKey) && l_jEff[l_strKey].is_number()) {
            int l_iV = l_jEff[l_strKey].get<int>();
            if (l_iV == 0 && f_IsGearType(f_ItemType(l_strId))) return;   // a worn piece's "atk: 0" says nothing
            l_vParts.push_back(l_strLabel + (l_iV >= 0 ? " +" : " ") + std::to_string(l_iV));
        }
    };
    l_fnAdd("hp", "HP"); l_fnAdd("maxhp", "Max HP"); l_fnAdd("atk", "ATK"); l_fnAdd("def", "DEF");
    l_fnAdd("exp", "EXP"); l_fnAdd("gold", m_oLocale.f_Tr("status.gold"));
    for (const char* l_pszK : {"key_yellow", "key_blue", "key_red"}) l_fnAdd(l_pszK, f_ItemName(l_pszK));
    for (const AttrDef& l_oA : m_vAttrDefs) l_fnAdd(l_oA.m_strId, l_oA.m_strShortName);
    if (l_jEff.contains("warp")) l_vParts.push_back("Warp");
    if (l_vParts.empty()) l_vParts.push_back(m_oLocale.f_Tr("inventory.no_effect"));
    std::string l_strOut;
    for (size_t l_nI = 0; l_nI < l_vParts.size(); ++l_nI) l_strOut += (l_nI ? " • " : "") + l_vParts[l_nI];
    return l_strOut;
}

void Game::f_BuildUiState(toms::UiState& l_oU) const {
    l_oU = toms::UiState{};
    const toms::Locale& l_oL = m_oLocale;

    // ---- title (the boot screen takes the whole screen) ----
    if (m_oTitle.f_IsOpen()) {
        toms::UiTitle& l_oT = l_oU.m_oTitle;
        l_oT.m_bVisible = true;
        l_oT.m_strTitle = l_oL.f_Tr("game.title");
        l_oT.m_strSubtitle = l_oL.f_Tr("game.subtitle");
        l_oT.m_strVersion = m_bShowVersion ? m_strVersionLabel : std::string();
        l_oT.m_iPage = (int)m_oTitle.f_Page();
        l_oT.m_strBackLabel = l_oL.f_Tr("menu.back");
        l_oT.m_strYesLabel = l_oL.f_Tr("menu.yes");
        l_oT.m_strNoLabel = l_oL.f_Tr("menu.no");
        switch (m_oTitle.f_Page()) {
            case toms::TitlePage::Menu: {
                static const char* l_arrKeys[3] = {"menu.new_game", "menu.continue", "menu.settings"};
                for (int l_iI = 0; l_iI < 3; l_iI++)
                    l_oT.m_vRows.push_back({l_oL.f_Tr(l_arrKeys[l_iI]), l_oL.f_Tr(std::string(l_arrKeys[l_iI]) + ".desc"), m_oTitle.f_MenuSelection() == l_iI, true});
                l_oT.m_strHint = l_oL.f_Tr("menu.hint");
                break;
            }
            case toms::TitlePage::Continue: {
                l_oT.m_strHeader = l_oL.f_Tr("continue.header");
                const auto& l_vSums = m_oTitle.f_Summaries();
                bool l_bAny = false;
                for (int l_iI = 0; l_iI < m_oTitle.f_SlotCount(); l_iI++) {
                    toms::UiRow l_oR;
                    l_oR.m_strLabel = l_oL.f_Tr("continue.slot") + " " + std::to_string(l_iI + 1);
                    if (l_iI < (int)l_vSums.size() && l_vSums[l_iI].m_bExists) {
                        const toms::SlotSummary& l_oS = l_vSums[l_iI];
                        l_bAny = true;
                        l_oR.m_strLabel += "   " + (l_oS.m_strStageName.empty() ? l_oS.m_strStageId : l_oS.m_strStageName) + "   " + l_oL.f_Tr("hud.level") +
                                   " " + std::to_string(l_oS.m_iLv) + "   HP " + std::to_string(l_oS.m_iHp) + "/" +
                                   std::to_string(l_oS.m_iMaxhp) + "   " + std::to_string(l_oS.m_iGold) + "G";
                        l_oR.m_strSub = l_oL.f_Tr("continue.saved_at") + " " + l_oS.m_strSavedAt + "    " + l_oL.f_Tr("continue.play_time") +
                                " " + f_FmtPlayTime(l_oS.m_iPlayTimeSec);
                        if (l_oS.m_bFromBackup) l_oR.m_strSub += "    [" + l_oL.f_Tr("continue.from_backup") + "]";
                        if (l_oS.m_bNewer) l_oR.m_strSub += "    [" + l_oL.f_Tr("continue.newer") + "]";
                    } else if (l_iI < (int)l_vSums.size() && l_vSums[l_iI].m_bDamaged) {
                        l_oR.m_strLabel += "   [" + l_oL.f_Tr("continue.damaged") + "]";
                    } else {
                        l_oR.m_strLabel += "   [" + l_oL.f_Tr("continue.empty") + "]";
                    }
                    l_oR.m_bSelected = m_oTitle.f_SlotSelection() == l_iI;
                    l_oT.m_vRows.push_back(l_oR);
                }
                l_oT.m_bShowBack = true;
                l_oT.m_bShowEmptyHint = !l_bAny;
                l_oT.m_strEmptyHint = l_oL.f_Tr("continue.hint");
                l_oT.m_strHint = l_oL.f_Tr("menu.hint");
                if (m_oTitle.f_NewGameConfirmOpen()) {
                    l_oT.m_bConfirmOpen = true;
                    l_oT.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("continue.new_game_confirm"), "slot",
                                                 std::to_string(m_oTitle.f_NewGameConfirmSlot()));
                    l_oT.m_strConfirmBody = l_oL.f_Tr("continue.new_game_confirm.body");
                    l_oT.m_bConfirmYes = m_oTitle.f_NewGameConfirmYesSelected();
                }
                break;
            }
            case toms::TitlePage::Settings: {
                l_oT.m_strHeader = l_oL.f_Tr("settings.header");
                const auto& l_vLangs = l_oL.f_Languages();
                for (int l_iI = 0; l_iI < (int)l_vLangs.size(); l_iI++)
                    l_oT.m_vRows.push_back({std::string(l_iI == l_oL.f_LanguageIndex() ? "[x] " : "[ ] ") + l_vLangs[l_iI].m_strName, "",
                                      m_oTitle.f_SettingsSelection() == l_iI, true});
                if (!l_oT.m_vRows.empty()) l_oT.m_vRows[0].m_strSection = l_oL.f_Tr("settings.language");   // section 1: language
                f_AppendArtStyleRows(l_oT.m_vRows, m_oTitle.f_SettingsSelection() - (int)l_vLangs.size());   // section 2: art style
                l_oT.m_bShowBack = true;
                l_oT.m_strHint = l_oL.f_Tr("settings.hint");
                if (m_oTitle.f_StyleConfirmOpen()) {
                    l_oT.m_bConfirmOpen = true;
                    l_oT.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("settings.style_confirm"), "style",
                                                 f_ArtStyleName(m_oTitle.f_StyleConfirmIndex()));
                    l_oT.m_strConfirmBody = l_oL.f_Tr("settings.style_apply_now");   // the title reloads the images at once
                    l_oT.m_bConfirmYes = m_oTitle.f_StyleConfirmYesSelected();
                }
                if (m_oTitle.f_LanguageConfirmOpen()) {
                    const int l_iIdx = m_oTitle.f_LanguageConfirmIndex();
                    l_oT.m_bConfirmOpen = true;
                    l_oT.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("settings.language_confirm"), "lang",
                                                 (l_iIdx >= 0 && l_iIdx < (int)l_vLangs.size()) ? l_vLangs[l_iIdx].m_strName : "");
                    l_oT.m_bConfirmYes = m_oTitle.f_LanguageConfirmYesSelected();
                }
                break;
            }
        }
        return;
    }

    // ---- ending (also takes the whole screen) ----
    if (f_EndingActive()) {
        toms::UiEnding& l_oE = l_oU.m_oEnding;
        l_oE.m_bVisible = true;
        const toms::EndingDefinition* l_pDef = nullptr;
        for (auto& l_oD : m_oEndingsTable.m_vEndings) if (l_oD.m_strId == m_strActiveEndingId) { l_pDef = &l_oD; break; }
        l_oE.m_strName = l_pDef ? l_oL.f_Tr(l_pDef->m_strNameKey) : m_strActiveEndingId;
        l_oE.m_strText = l_pDef ? l_oL.f_Tr(l_pDef->m_strTextKey) : "";
        l_oE.m_bRebirth = f_RebirthOffered();
        l_oE.m_strRebirthLabel = l_oL.f_Tr("ending.rebirth_button");
        l_oE.m_strTitleLabel = l_oL.f_Tr("ending.title_button");
        l_oE.m_strHint = l_oL.f_Tr("ending.dismiss_hint");
        return;
    }

    // Which scene is up -- the same precedence Game::draw() uses for the world.
    const bool l_bShowStore = f_StoreModal();
    const bool l_bShowBattle = !l_bShowStore && (m_iHideMask & 1) == 0 && (m_oCs.m_bActive || m_oCs.m_bWon || m_oCs.m_iResultPauseMs > 0);
    const bool l_bShowTalk = !l_bShowStore && !l_bShowBattle && (m_iHideMask & 2) == 0 && m_bInDialogue;
    const bool l_bShowWalk = !l_bShowStore && !l_bShowBattle && !l_bShowTalk;

    // ---- HUD (over the map) ----
    if (l_bShowWalk) {
        toms::UiHud& l_oH = l_oU.m_oHud;
        l_oH.m_bVisible = !m_bInGameMenuOpen;   // the player menu covers the screen
        l_oH.m_strTitleLine = l_oL.f_Tr("game.title") + " — " + m_oSt.m_strName + " (" + std::to_string(m_oSt.m_iIndex) + "/" +
                       std::to_string(m_iTotalStages) + ")";
        l_oH.m_fHpPct = f_Pct((float)m_oPl.m_iHp, (float)f_EffectiveMaxHp());
        l_oH.m_strHpText = "HP " + std::to_string(m_oPl.m_iHp) + "/" + std::to_string(f_EffectiveMaxHp());
        l_oH.m_strStatsLine = "ATK " + std::to_string(f_EffectiveAtk()) + "   DEF " + std::to_string(f_EffectiveDef()) + "   LV " + std::to_string(m_oPl.m_iLv);
        l_oH.m_strResLine = "GOLD " + std::to_string(m_oPl.m_iGold) + "   " + l_oL.f_Tr("hud.crystals") + " " + std::to_string(m_oPl.m_iCrystals) +
                     "   EXP " + std::to_string(m_oPl.m_iExp) + "   " + l_oL.f_Tr("hud.keys") +
                     " Y" + std::to_string(m_oPl.m_iKeyYellow) + " B" + std::to_string(m_oPl.m_iKeyBlue) + " R" +
                     std::to_string(m_oPl.m_iKeyRed);
        // The floor's story line: its intro, then its ambient lines as the player moves.
        l_oH.m_strFooter = m_oSt.m_strStoryNote;
        if (!m_strStoryIntroKey.empty()) {
            const int l_iIdx = f_StoryLineIndex();
            const std::string& l_strKey = (l_iIdx == 0 || m_vStoryAmbientKeys.empty())
                                     ? m_strStoryIntroKey : m_vStoryAmbientKeys[(size_t)(l_iIdx - 1) % m_vStoryAmbientKeys.size()];
            std::string l_strTr = l_oL.f_Tr(l_strKey);
            if (!l_strTr.empty() && l_strTr.rfind("story.", 0) != 0) l_oH.m_strFooter = l_strTr;
        }
        l_oH.m_strMenuLabel = l_oL.f_Tr("ingame_menu.title");
        l_oH.m_bMenuNews = m_bNewsStore || m_bNewsGear || m_bNewsEvents || m_oRun.f_SkillPoints() > 0;
        if (m_fChapterCardMs > 0.0f && !m_strChapterCardTitle.empty()) {
            l_oH.m_bChapterCard = true;
            l_oH.m_strChapterTitle = m_strChapterCardTitle;
            l_oH.m_strChapterSub = m_oSt.m_strName;
            l_oH.m_fChapterAlpha = std::min(1.0f, m_fChapterCardMs / 600.0f);
        }
        // Name plates over bosses and roamers, at the same place the map draws them.
        float l_fTs; int l_iCols, l_iRows; f_CameraViewportTiles(l_fTs, l_iCols, l_iRows);
        const float l_fOx = -m_oCam.f_X() * l_fTs, l_fOy = 60.0f - m_oCam.f_Y() * l_fTs, l_fInset = l_fTs / 6.0f;
        const float l_fScreenW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fScreenH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
        for (const auto& l_oE : m_oSt.m_vEntities) {
            if (l_oE.m_bConsumed || !(l_oE.m_oFp.f_BossTier() || l_oE.m_bRoamer)) continue;
            const float l_fX = l_fOx + l_oE.m_iX * l_fTs + l_fInset, l_fY = l_fOy + l_oE.m_iY * l_fTs + l_fInset, l_fW = l_oE.m_oFp.m_iW * l_fTs - 2.0f * l_fInset;
            if (l_fX + l_fW < 0 || l_fX > l_fScreenW || l_fY < 0 || l_fY > l_fScreenH) continue;
            l_oH.m_vBanners.push_back({l_oE.m_strDisplayName.empty() ? l_oE.m_strId : l_oE.m_strDisplayName, l_fX + l_fW * 0.5f, l_fY - 2.0f});
        }


        if (m_bStairsConfirmOpen) {
            l_oU.m_oStairs.m_bVisible = true;
            l_oU.m_oStairs.m_strQuestion = l_oL.f_Tr(m_bStairsConfirmIsUp ? "stairs.confirm_up" : "stairs.confirm_down");
            l_oU.m_oStairs.m_strYesLabel = l_oL.f_Tr("menu.yes") + " (Enter)";
            l_oU.m_oStairs.m_strNoLabel = l_oL.f_Tr("menu.no") + " (Esc)";
        }
        if (m_bInGameMenuOpen) f_BuildPlayerUi(l_oU);
        if (m_bStageSelectOpen) {
            toms::UiStageSelect& l_oS = l_oU.m_oStageSelect;
            l_oS.m_bVisible = true;
            l_oS.m_strTitle = l_oL.f_Tr("stageselect.title");
            l_oS.m_strHint = l_oL.f_Tr("stageselect.hint");
            l_oS.m_strCloseLabel = l_oL.f_Tr("inventory.close");
            for (size_t l_nI = 0; l_nI < m_vStageList.size(); l_nI++) {
                const StageInfo& l_oInfo = m_vStageList[l_nI];
                auto l_fnHas = [&](const std::string& l_strId) {
                    return std::find(m_oMeta.m_vUnlockedStages.begin(), m_oMeta.m_vUnlockedStages.end(), l_strId) != m_oMeta.m_vUnlockedStages.end();
                };
                const bool l_bReached = l_fnHas(l_oInfo.m_strId);
                const bool l_bLocked = l_oInfo.m_iIndex > 1 && l_nI > 0 && !l_fnHas(m_vStageList[l_nI - 1].m_strId);
                toms::UiRow l_oR;
                l_oR.m_strLabel = std::to_string(l_oInfo.m_iIndex) + ". " + l_oInfo.m_strName;
                if (l_bReached) l_oR.m_strLabel += "  " + l_oL.f_Tr("stageselect.reached_tag");
                else if (!l_bLocked) l_oR.m_strLabel += "  " + l_oL.f_Tr("stageselect.new_tag");
                l_oR.m_bEnabled = !l_bLocked;
                l_oR.m_strSub = l_bLocked ? f_TrParam(l_oL.f_Tr("stageselect.locked_hint"), "floor", std::to_string(l_oInfo.m_iIndex - 1)) : "";
                if (!l_oInfo.m_strPreview.empty()) l_oR.m_strSub += (l_oR.m_strSub.empty() ? "" : "   ") + l_oInfo.m_strPreview;
                l_oS.m_vRows.push_back(l_oR);
            }
        }
    }

    // ---- battle ----
    if (l_bShowBattle) {
        toms::UiBattle& l_oB = l_oU.m_oBattle;
        l_oB.m_bVisible = true;
        l_oB.m_bActive = m_oCs.m_bActive;
        // "Slime", or "Slime +2" with a party (each name is under its monster)
        l_oB.m_strTitle = l_oL.f_Tr("battle.title") + " " + (m_oCs.m_vFoes.empty() ? std::string() : m_oCs.m_vFoes[0].m_oEnemy.m_strName) +
                  (m_oCs.m_vFoes.size() > 1 ? " +" + std::to_string(m_oCs.m_vFoes.size() - 1) : std::string());
        l_oB.m_strPlayerSprite = f_UiSprite(f_PlayerSprite());
        l_oB.m_bPlayerAnim = f_BattleRigReady() && !m_strBattleRigState.empty();   // drawBattleRig draws the player instead
        for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) {   // the monsters (battle.rml .foe)
            const CombatState::Foe& l_oF = m_oCs.m_vFoes[l_nI];
            toms::UiFoe l_oUf;
            l_oUf.m_strSprite = f_UiSprite(f_MonsterSprite((int)l_nI));
            l_oUf.m_bAnim = l_nI < m_vFoeRigs.size() && m_vFoeRigs[l_nI].m_upView && !m_vFoeRigs[l_nI].m_strState.empty();   // drawBattleRig draws it
            l_oUf.m_fHpPct = f_Pct((float)std::max(0, l_oF.m_iHp), (float)l_oF.m_oEnemy.m_iHp);
            l_oUf.m_strName = l_oF.m_oEnemy.m_strName;
            l_oUf.m_strHpLine = "HP " + std::to_string(std::max(0, l_oF.m_iHp));
            l_oUf.m_strHpText = l_oUf.m_strName + " " + l_oUf.m_strHpLine;
            l_oUf.m_bTarget = m_oCs.m_bActive && m_oCs.m_vFoes.size() > 1 && (int)l_nI == m_oCs.m_iTarget && !l_oF.f_Down();
            l_oUf.m_bDown = l_oF.f_Down();
            if (m_oCs.m_bActive && !l_oF.f_Down()) {   // its intent (docs/gameplay/13 §6.1)
                const toms::MoveDef& l_oM = m_oMoves.f_Get(l_oF.m_strMove);
                if (l_oF.m_iStaggerMs > 0) {
                    l_oUf.m_strIntentKind = l_oF.m_bBroken ? "broken" : "stagger";
                    l_oUf.m_strIntent = l_oL.f_Tr(l_oF.m_bBroken ? "battle.intent.broken" : "battle.intent.stagger");
                    l_oUf.m_fIntentPct = 0.0f;
                } else {
                    l_oUf.m_strIntentKind = toms::f_MoveKindName(l_oM.m_eKind);
                    const int l_iLeft = std::max(0, l_oF.m_iMoveMs - std::max(0, l_oF.m_iClockMs));
                    char l_pszSecs[16];
                    std::snprintf(l_pszSecs, sizeof l_pszSecs, " %.1fs", l_iLeft / 1000.0);
                    l_oUf.m_strIntent = (l_oL.f_Has(l_oM.m_strText) ? l_oL.f_Tr(l_oM.m_strText) : l_oM.m_strText) + l_pszSecs;
                    l_oUf.m_fIntentPct = f_Pct((float)std::max(0, l_oF.m_iClockMs), (float)std::max(1, l_oF.m_iMoveMs));
                }
                l_oUf.m_fBreakPct = f_Pct(l_oF.m_fBreakGauge, (float)f_FoeBreakPoints((int)l_nI));
                l_oUf.m_bWarn = f_FoeWarning((int)l_nI) && !m_oCs.m_oSkill.m_bOn;   // the "!" before its hit (docs/gameplay/16 §4)
            }
            l_oB.m_vFoes.push_back(l_oUf);
        }
        l_oB.m_strEnemySprite = l_oB.m_vFoes.empty() ? std::string() : l_oB.m_vFoes[0].m_strSprite;
        l_oB.m_bEnemyAnim = !l_oB.m_vFoes.empty() && l_oB.m_vFoes[0].m_bAnim;
        l_oB.m_fPlayerHpPct = f_Pct((float)m_oCs.m_iPlayerHP, (float)f_EffectiveMaxHp());
        l_oB.m_fEnemyHpPct = l_oB.m_vFoes.empty() ? 0.0f : l_oB.m_vFoes[0].m_fHpPct;
        l_oB.m_strPlayerHpText = l_oL.f_Tr("battle.you") + " HP " + std::to_string(m_oCs.m_iPlayerHP);
        l_oB.m_strEnemyHpText = l_oB.m_vFoes.empty() ? std::string() : l_oB.m_vFoes[0].m_strHpText;
        l_oB.m_strLog = m_oCs.m_strLog;
        l_oB.m_strContinueHint = l_oL.f_Tr("battle.continue_hint");
        for (const auto& l_oP : m_oCs.m_vPopups) {   // over the fighter's box (battle.rcss #player / .foe .fbox: fighterBox)
            toms::UiPopup l_oQ;
            l_oQ.m_strText = l_oP.m_strText;
            l_oQ.m_strStyle = l_oP.m_strStyle;
            const float l_fR = l_oP.m_oRise.f_Value();            // 0 -> 1 as it rises
            const glm::vec4 l_v4Box = f_FighterBox(l_oP.m_iWho);
            l_oQ.m_fX = l_v4Box.x + l_v4Box.z * 0.5f - 150.0f;       // the 300 px box (a word is wider than a number), centred over it
            l_oQ.m_fY = l_v4Box.y + 26.0f - (float)l_oP.m_iLift - 70.0f * l_fR;
            l_oQ.m_fOpacity = l_fR < 0.6f ? 1.0f : std::max(0.0f, 1.0f - (l_fR - 0.6f) / 0.4f);
            l_oB.m_vPopups.push_back(l_oQ);
        }
        if (m_oCs.m_bActive) {
            l_oB.m_strClockLabel = l_oL.f_Tr("battle.enemy_clock");
            // the clock of the monster that attacks next (the nearest to its move); the stamina pace shows the stamina
            float l_fBest = 0.0f;
            for (const CombatState::Foe& l_oF : m_oCs.m_vFoes)
                if (!l_oF.f_Down()) l_fBest = std::max(l_fBest, f_Pct((float)std::max(0, l_oF.m_iClockMs), (float)std::max(1, l_oF.m_iMoveMs)));
            l_oB.m_fClockPct = l_fBest;
            l_oB.m_bStaminaPace = m_oCs.m_ePace == toms::Pace::Stamina;
            if (l_oB.m_bStaminaPace) {
                l_oB.m_strClockLabel = l_oL.f_Tr("battle.stamina");
                l_oB.m_fClockPct = f_Pct(m_oCs.m_fStamina, (float)m_oPaceSettings.m_iStaminaMax);
                l_oB.m_bAttackShort = !f_AttackRunning() && m_oCs.m_fStamina < (float)m_oPaceSettings.m_iAttackCost;
                l_oB.m_bGuardShort = m_oCs.m_fStamina < (float)m_oPaceSettings.m_iGuardCost;
            }
            {   // Battle v3 (docs/gameplay/07 phase 1): the charge, the track with the weapon level's points, the marker
                toms::UiCombo& l_oC = l_oB.m_oCombo;
                const std::string l_strKey = f_WeaponKey();
                const int l_iLv = f_WeaponLevel(l_strKey);
                const toms::WeaponPlay& l_oWp = f_WeaponPlayOf(l_strKey);
                l_oC.m_bRunning = m_oCs.m_oCombo.f_Running();
                l_oC.m_bCooling = m_oCs.m_oAtkBar.m_bCooling;
                l_oC.m_bWaiting = m_oCs.m_oCombo.f_Waiting();
                l_oC.m_fChargePct = l_oC.m_bRunning ? 100.0f * m_oCs.m_oCombo.f_Result().m_fCharge : 100.0f * f_ComboCharge();
                if (l_oC.m_bRunning) {
                    l_oC.m_fChargePct = 0;   // spent on this combo
                    l_oC.m_fMarker = 100.0f * m_oCs.m_oCombo.f_Marker();
                    const int l_iNear = m_oCs.m_oCombo.f_NearPoint();
                    for (size_t l_nI = 0; l_nI < m_oCs.m_oCombo.f_PointsAt().size(); l_nI++)
                        l_oC.m_vPoints.push_back({100.0f * m_oCs.m_oCombo.f_PointsAt()[l_nI], (bool)m_oCs.m_oCombo.f_PointsHit()[l_nI], (int)l_nI == l_iNear});
                } else {   // the points this combo will have, before it starts
                    const int l_iN = toms::f_WeaponLevelEntry(l_oWp, l_iLv).m_iPoints;
                    for (int l_iI = 0; l_iI < l_iN; l_iI++) l_oC.m_vPoints.push_back({100.0f * float(l_iI + 1) / float(l_iN + 1), false});
                }
                const std::string l_strName = m_mapEquipmentDefs.count(l_strKey) ? f_ItemName(l_strKey) : l_oL.f_Tr("battle.unarmed");
                const int l_iNext = toms::f_ExpToNextLevel(l_oWp, l_iLv);
                int l_iDone = 0;
                if (auto l_itU = m_mapWeaponExp.find(l_strKey); l_itU != m_mapWeaponExp.end()) {
                    l_iDone = l_itU->second - toms::f_WeaponExpForLevel(l_oWp, l_iLv);
                }
                l_oC.m_strLevelText = f_TrParam(f_TrParam(l_oL.f_Tr("battle.weapon_level"), "name", l_strName), "lv", std::to_string(l_iLv)) +
                               (l_iNext > 0 ? "  " + std::to_string(std::max(0, l_iDone)) + "/" + std::to_string(l_iNext) : std::string());
                l_oC.m_strPrompt = l_oL.f_Tr(l_oC.m_bRunning ? "battle.combo_prompt_run" : "battle.combo_prompt");
                l_oC.m_strButtonLabel = l_oL.f_Tr(l_oC.m_bCooling ? "battle.btn_charging" : l_oC.m_bRunning ? "battle.combo_tap" : "battle.combo_start");
            }
            l_oB.m_oDefense = f_PowerBar(toms::f_EffectiveDefenseBar(m_oEquipped, m_mapEquipmentDefs), m_oCs.m_oDefBar, l_oL.f_Tr("battle.defense_prompt"),
                                 l_oL.f_Tr(m_oCs.m_oDefBar.m_bCooling ? "battle.btn_charging" : "battle.btn_hold_defense"));
            l_oB.m_bShieldArmed = m_oCs.m_bShieldBanked;
            l_oB.m_strShieldText = m_oCs.m_bShieldBanked
                ? f_TrParam(l_oL.f_Tr("battle.shield_armed"), "pct", std::to_string((int)std::lround(m_oCs.m_fShieldPower)))
                : l_oL.f_Tr("battle.shield_none");
            l_oB.m_bCritArmed = m_oCs.m_bNextAttackGuaranteedCrit;
            l_oB.m_strCritHint = l_oL.f_Tr("battle.active_armed_hint");
            if (m_oCs.m_bCellsOn) {   // the six cells: where the hero stands, what is in danger and how soon (docs/gameplay/19 §2.1)
                l_oB.m_bCellsOn = true;
                l_oB.m_strCellHint = l_oL.f_Tr("battle.cell_hint");
                for (int l_iC = 0; l_iC < toms::g_iCellCols * toms::g_iCellRows; l_iC++) {
                    toms::UiBattleCell l_oCell;
                    l_oCell.m_fX = 152.0f * (float)(l_iC % toms::g_iCellCols);
                    l_oCell.m_fY = 64.0f * (float)(l_iC / toms::g_iCellCols);
                    l_oCell.m_bHere = l_iC == m_oCs.m_iHeroCell;
                    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes)
                        if (!l_oF.f_Down() && (l_oF.m_uDanger & (1u << l_iC))) {
                            l_oCell.m_bDanger = true;
                            l_oCell.m_fFill = std::max(l_oCell.m_fFill, std::clamp(100.0f * (1.0f - (float)(l_oF.m_iMoveMs - l_oF.m_iClockMs) / (float)m_oCellRules.m_iWarnMs), 0.0f, 100.0f));
                        }
                    l_oB.m_vCells.push_back(l_oCell);
                }
            }
            l_oB.m_strSuperLabel = l_oL.f_Tr("battle.super_label");
            for (int l_iI = 0; l_iI < m_oRun.f_SuperMax(); l_iI++) l_oB.m_vSuperDots.push_back(l_iI < m_oCs.m_iSuperCharge ? 1 : 0);
            l_oB.m_bSuperReady = m_oCs.m_iSuperCharge >= m_oRun.f_SuperMax() && !m_oCs.m_oCombo.f_Running() && !m_oCs.m_oSkill.m_bOn && !m_oCs.m_bWinPending;
            l_oB.m_strSuperButton = l_oL.f_Tr("battle.super_ready");
            // the revamp (docs/gameplay/15): the attack is the play, its panel only says how it is; one guard button
            l_oB.m_bShowClock = m_oCs.m_ePace == toms::Pace::Stamina;
            l_oB.m_bGuardTiming = m_oGuard.m_strMode != "bar";   // the one GUARD button ("timing" and "warn"); "bar": the five zones
            l_oB.m_bGuardLit = m_oCs.m_iGuardAtMs >= 0 && m_oCs.m_iClockMs - m_oCs.m_iGuardAtMs <= m_oGuard.m_iBlockMs;
            l_oB.m_bGuardCooling = m_oCs.m_iGuardCdMs > 0 || l_oB.m_bGuardShort;
            if (m_oGuard.m_strMode == "warn") {   // lit while an "!" shows: now (16 §4.2)
                l_oB.m_bGuardLit = false;
                for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) l_oB.m_bGuardLit = l_oB.m_bGuardLit || f_FoeWarning((int)l_nI);
                l_oB.m_bGuardCooling = m_oCs.m_iGuardLockMs > 0 || l_oB.m_bGuardShort || m_oCs.m_oSkill.m_bOn;
            }
            l_oB.m_strGuardLabel = l_oL.f_Tr("battle.guard_button");
            l_oB.m_strGuardHint = l_oL.f_Tr(m_oGuard.m_strMode == "warn" ? "battle.guard_hint_warn" : "battle.guard_hint");
            if (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase == CombatState::SkillRun::Phase::Play && m_oCs.m_oSkill.m_pDef) {   // a move's play (16 §6)
                const toms::SpecialMove& l_oD = *m_oCs.m_oSkill.m_pDef;
                l_oB.m_strSkillPrompt = l_oL.f_Has(l_oD.m_strPrompt) ? l_oL.f_Tr(l_oD.m_strPrompt) : l_oD.m_strPrompt;
                if (l_oD.m_oPlay.m_eType == toms::PlayType::eDRAW_SHAPE || l_oD.m_oPlay.m_eType == toms::PlayType::eMASH || toms::f_IsFreePlay(l_oD.m_oPlay.m_eType)) {
                    char l_pszT[16];
                    std::snprintf(l_pszT, sizeof l_pszT, "%.1f", m_oCs.m_oTouch.f_TimeLeft() * (float)l_oD.m_oPlay.m_iTimeMs / 1000.0f);
                    l_oB.m_strSkillTime = l_pszT;
                }
            }
            const toms::PlayType l_ePlayType = f_BattlePlayType();
            if (f_AttackRunning()) { l_oB.m_strAttackState = "running"; l_oB.m_strAttackLabel = l_oL.f_Tr("battle.attack_running"); }
            else if (m_oCs.m_ePace == toms::Pace::Cooldown && m_oCs.m_oAtkBar.m_bCooling) {
                l_oB.m_strAttackState = "recovering";
                l_oB.m_strAttackLabel = l_oL.f_Tr("battle.attack_recovering");
                l_oB.m_fAttackPct = 100.0f * (1.0f - (float)m_oCs.m_oAtkBar.m_iCooldownMs / (float)std::max(1, f_BarCooldownMs()));
            } else if (l_oB.m_bAttackShort) { l_oB.m_strAttackState = "short"; l_oB.m_strAttackLabel = l_oL.f_Tr("battle.no_stamina"); }
            else {
                l_oB.m_strAttackState = "ready";
                l_oB.m_strAttackLabel = l_oL.f_Tr("battle.attack_ready");
                l_oB.m_strAttackHint = l_oL.f_Tr(l_ePlayType == toms::PlayType::eTRACK ? "battle.attack_ready_hint"
                                     : std::string("battle.play_hint_") + toms::f_PlayTypeName(l_ePlayType));
            }
            if (l_ePlayType != toms::PlayType::eTRACK || m_oCs.m_oSkill.m_bOn) f_TouchPlayUi(l_oB);
            if (m_oCs.m_iChain > 1) l_oB.m_strChainText = f_TrParam(l_oL.f_Tr("battle.word_chain"), "n", std::to_string(m_oCs.m_iChain));
            auto l_vActiveIds = toms::f_EquippedActives(m_oEquipped, m_mapEquipmentDefs);
            if (!l_vActiveIds.empty() && toms::f_CanUseActive(l_vActiveIds[0], m_mapActiveDefs, m_oCs.m_mapActiveRuntime, m_oCs.m_oActiveStatus)) {
                l_oB.m_bActiveReady = true;   // (ActiveDefinition has no display name yet: the id is shown)
                l_oB.m_strActiveButton = f_TrParam(l_oL.f_Tr("battle.active_ready"), "name", l_vActiveIds[0]);
            }
        }
    }

    // ---- the battle's moments and its result (docs/gameplay/15 §3.4, §3.6) ----
    if (l_bShowBattle) {
        toms::UiBattle& l_oB = l_oU.m_oBattle;
        if (m_oCs.m_iBannerMs > 0 && !m_oCs.m_strBanner.empty()) {   // across the screen: slides in, holds, slides out
            const float l_fT = 1.0f - (float)m_oCs.m_iBannerMs / (float)m_oCs.m_iBannerMsMax;
            l_oB.m_strBanner = m_oCs.m_strBanner;
            l_oB.m_strBannerKind = m_oCs.m_strBannerKind;
            l_oB.m_fBannerOpacity = l_fT < 0.12f ? l_fT / 0.12f : l_fT > 0.85f ? (1.0f - l_fT) / 0.15f : 1.0f;
            l_oB.m_fBannerShift = l_fT < 0.12f ? (1.0f - l_fT / 0.12f) * 500.0f : l_fT > 0.85f ? -(l_fT - 0.85f) / 0.15f * 500.0f : 0.0f;
        }
        if (m_oCs.m_bWon && m_oCs.m_oResult.m_bValid) {
            const CombatState::Result& l_oR = m_oCs.m_oResult;
            l_oB.m_bResultShown = true;
            l_oB.m_strGrade = l_oR.m_strGrade;
            l_oB.m_bGradeS = l_oR.m_strGrade == "S";
            l_oB.m_strResultTitle = l_oL.f_Tr("battle.result_title");
            char l_pszSecs[16];
            std::snprintf(l_pszSecs, sizeof l_pszSecs, "%.1f", l_oR.m_fSeconds);
            l_oB.m_strResultStats = f_TrParam(f_TrParam(f_TrParam(l_oL.f_Tr("battle.result_stats"), "t", l_pszSecs), "c", std::to_string(m_oCs.m_iBestChain)),
                                     "p", std::to_string(m_oCs.m_iPerfects));
            const toms::WeaponPlay& l_oWp = f_WeaponPlayOf(l_oR.m_strWeapon);
            const std::string l_strName = m_mapEquipmentDefs.count(l_oR.m_strWeapon) ? f_ItemName(l_oR.m_strWeapon) : l_oL.f_Tr("battle.unarmed");
            l_oB.m_strResultWeapon = f_TrParam(f_TrParam(l_oL.f_Tr("battle.result_weapon"), "name", l_strName), "lv", std::to_string(l_oR.m_iLvAfter));
            const int l_iNeed = toms::f_ExpToNextLevel(l_oWp, l_oR.m_iLvAfter);
            const int l_iDone = l_oR.m_iWeaponExp - toms::f_WeaponExpForLevel(l_oWp, l_oR.m_iLvAfter);
            if (l_iNeed > 0) {   // the EXP to the next level, and what that level adds (07 pillar 2: the reason to go on)
                l_oB.m_fResultPct = f_Pct((float)std::max(0, l_iDone), (float)l_iNeed);
                l_oB.m_strResultProgress = f_TrParam(f_TrParam(f_TrParam(l_oL.f_Tr("battle.result_progress"), "n", std::to_string(std::max(0, l_iDone))), "m",
                                            std::to_string(l_iNeed)), "g", std::to_string(l_oR.m_iWeaponExpGained));
                const toms::WeaponLevel& l_oCur = toms::f_WeaponLevelEntry(l_oWp, l_oR.m_iLvAfter);
                const toms::WeaponLevel& l_oNxt = toms::f_WeaponLevelEntry(l_oWp, l_oR.m_iLvAfter + 1);
                std::string l_strWhat;
                if (l_oNxt.m_iPoints > l_oCur.m_iPoints) l_strWhat = f_TrParam(l_oL.f_Tr("battle.result_points"), "n", std::to_string(l_oNxt.m_iPoints));
                for (const auto& [l_strStat, l_iV] : l_oNxt.m_mapStats) {
                    const int l_iWas = l_oCur.m_mapStats.count(l_strStat) ? l_oCur.m_mapStats.at(l_strStat) : 0;
                    if (l_iV > l_iWas) {
                        std::string l_strUp = l_strStat;
                        for (char& l_cCh : l_strUp) l_cCh = (char)std::toupper((unsigned char)l_cCh);
                        l_strWhat += (l_strWhat.empty() ? "" : ", ") + l_strUp + " +" + std::to_string(l_iV - l_iWas);
                    }
                }
                if (!l_strWhat.empty()) l_oB.m_strResultNext = f_TrParam(l_oL.f_Tr("battle.result_next"), "what", l_strWhat);
            } else {
                l_oB.m_fResultPct = 100.0f;
                l_oB.m_strResultProgress = l_oL.f_Tr("battle.result_top");
            }
            if (l_oR.m_iLvAfter > l_oR.m_iLvBefore)
                l_oB.m_strResultLevelup = f_TrParam(f_TrParam(l_oL.f_Tr("battle.result_levelup"), "name", l_strName), "lv", std::to_string(l_oR.m_iLvAfter));
            l_oB.m_strResultGold = f_TrParam(f_TrParam(l_oL.f_Tr("battle.result_gold"), "gold", std::to_string(l_oR.m_iGold)), "exp", std::to_string(l_oR.m_iExp));
            l_oB.m_strResultCard = l_oR.m_strCard;
        }
    }

    // ---- a reward event (docs/gameplay/07 phase 6) ----
    if (m_oMiniGame.m_bOpen) {
        if (const toms::MiniGameDef* l_pDef = m_oMiniGames.f_Find(m_oMiniGame.m_strId)) {
            toms::UiMiniGame& l_oM = l_oU.m_oMinigame;
            l_oM.m_bVisible = true;
            l_oM.m_strTitle = l_oL.f_Field(l_pDef->m_jTitle);
            l_oM.m_strText = l_oL.f_Field(l_pDef->m_jText);
            l_oM.m_bRunning = m_oMiniGame.m_iPhase == 1;
            l_oM.m_bDone = m_oMiniGame.m_iPhase == 2;
            const std::string l_strUnit = l_oL.f_Tr(l_pDef->m_strCurrency == "crystals" ? "minigame.crystals" : "minigame.gold");
            l_oM.m_strPrompt = l_oM.m_bRunning ? l_oL.f_Tr("battle.combo_prompt_run")
                                 : f_TrParam(f_TrParam(f_TrParam(l_oL.f_Tr("minigame.rule"), "n", std::to_string(l_pDef->m_iPerHit)), "unit", l_strUnit),
                                           "bonus", std::to_string(l_pDef->m_iBonusAll));
            l_oM.m_strButtonLabel = l_oL.f_Tr(l_oM.m_bDone ? "minigame.close" : l_oM.m_bRunning ? "battle.combo_tap" : "minigame.start");
            l_oM.m_strCloseLabel = l_oL.f_Tr("minigame.close");
            if (l_oM.m_bDone)
                l_oM.m_strResult = m_oMiniGame.m_iReward > 0
                    ? f_TrParam(f_TrParam(l_oL.f_Tr("minigame.result"), "n", std::to_string(m_oMiniGame.m_iReward)), "unit", l_strUnit)
                    : l_oL.f_Tr("minigame.result_none");
            if (l_oM.m_bRunning) {
                l_oM.m_fMarker = 100.0f * m_oMiniGame.m_oPlay.f_Marker();
                for (size_t l_nI = 0; l_nI < m_oMiniGame.m_oPlay.f_PointsAt().size(); l_nI++)
                    l_oM.m_vPoints.push_back({100.0f * m_oMiniGame.m_oPlay.f_PointsAt()[l_nI], (bool)m_oMiniGame.m_oPlay.f_PointsHit()[l_nI]});
            } else {
                const int l_iN = l_pDef->m_oPlay.m_iPoints;
                for (int l_iI = 0; l_iI < l_iN; l_iI++)
                    l_oM.m_vPoints.push_back({100.0f * float(l_iI + 1) / float(l_iN + 1), l_oM.m_bDone && l_iI < m_oMiniGame.m_oPlay.f_Result().m_iHits});
            }
        }
    }

    // ---- dialogue ----
    if (l_bShowTalk) {
        toms::UiDialogue& l_oD = l_oU.m_oDialogue;
        l_oD.m_bVisible = true;
        l_oD.m_fScale = m_fUiScale;
        // The box spans the canvas width minus 40 px a side and scales about its bottom centre; scaled up (phones) it is
        // narrowed first, so it keeps that width on screen instead of running off both edges.
        const float l_fCw = m_pRen ? (float)m_pRen->f_Width() : 1024.0f;
        l_oD.m_fSide = (l_fCw - (l_fCw - 80.0f) / std::max(1.0f, m_fUiScale)) * 0.5f;
        l_oD.m_strSpeaker = m_strDlgSpeaker;
        if (m_jDlgData.contains("nodes") && m_jDlgData["nodes"].contains(m_strDlgNode) && m_jDlgData["nodes"][m_strDlgNode].contains("text"))
            l_oD.m_strText = l_oL.f_Field(m_jDlgData["nodes"][m_strDlgNode]["text"]);
        for (size_t l_nI = 0; l_nI < m_vDlgChoices.size(); l_nI++)
            l_oD.m_vChoices.push_back({m_vDlgChoices[l_nI].m_strLabel, "", (int)l_nI == m_iDlgSel, true});
    }

    // ---- store ----
    if (m_bStoreOpen) {
        toms::UiStore& l_oS = l_oU.m_oStore;
        l_oS.m_bVisible = true;
        l_oS.m_strTitle = l_oL.f_Field(m_jStoreTitle);
        l_oS.m_strHint = l_oL.f_Tr("store.controls_hint");
        l_oS.m_strGoldLabel = l_oL.f_Tr("store.gold_label");
        l_oS.m_strCloseLabel = l_oL.f_Tr("inventory.close");
        l_oS.m_strEffectLabel = l_oL.f_Tr("store.effect_label");
        l_oS.m_strPriceLabel = l_oL.f_Tr("store.price_label");
        l_oS.m_strBuyLabel = l_oL.f_Tr("store.buy_button");
        l_oS.m_iGold = m_oPl.m_iGold;
        static const char* l_arrTabKeys[4] = {"store.tab.potion", "store.tab.weapon", "store.tab.armor", "store.tab.talent"};
        for (int l_iI = 0; l_iI < 4; l_iI++) l_oS.m_vTabs.push_back({l_oL.f_Tr(l_arrTabKeys[l_iI]), l_iI == m_iStoreTab});
        const std::vector<int> l_vIdx = f_StoreTabIndices();
        for (size_t l_nI = 0; l_nI < l_vIdx.size(); l_nI++) {
            const StoreItemDef& l_oD = m_vStoreItems[l_vIdx[l_nI]];
            const bool l_bGear = f_IsGearType(f_ItemType(l_oD.m_strId));
            const bool l_bOwned = l_bGear && std::find(m_vGearOwned.begin(), m_vGearOwned.end(), l_oD.m_strId) != m_vGearOwned.end();
            toms::UiStoreItem l_oIt;
            l_oIt.m_strIcon = f_ItemSprite(l_oD.m_strId);
            l_oIt.m_strName = f_ItemName(l_oD.m_strId);
            l_oIt.m_strDesc = f_ItemDesc(l_oD.m_strId);
            l_oIt.m_strEffect = f_ItemEffectSummary(l_oD.m_strId);
            l_oIt.m_bEquipped = l_bGear && (l_oD.m_strId == m_oEquipped.m_strWeaponId || l_oD.m_strId == m_oEquipped.m_strArmorId || l_oD.m_strId == m_oEquipped.m_strTalentId);
            l_oIt.m_strStatus = l_bGear ? l_oL.f_Tr(l_oIt.m_bEquipped ? "store.equipped" : "store.tap_to_equip")
                             : l_oL.f_Tr("store.purchased_label") + " x" + std::to_string(l_oD.m_iPurchases);
            l_oIt.m_strPrice = l_bOwned ? "-" : std::to_string(l_oD.f_LiveCost());
            l_oIt.m_bSelected = (int)l_nI == m_iStoreSel;
            l_oS.m_vItems.push_back(l_oIt);
        }
    }
    if (!m_vNotices.empty()) {
        l_oU.m_oNotice.m_bVisible = true;
        l_oU.m_oNotice.m_strTitle = m_vNotices.front().m_strTitle;
        l_oU.m_oNotice.m_strBody = m_vNotices.front().m_strBody;
        l_oU.m_oNotice.m_strOk = l_oL.f_Tr("notice.ok");
    }
    if (m_bStoreUnlockDlg) {
        l_oU.m_oStore.m_bUnlockedDialog = true;
        l_oU.m_oStore.m_strUnlockedTitle = l_oL.f_Tr("store.unlocked_title");
        l_oU.m_oStore.m_strUnlockedBody = l_oL.f_Tr("store.unlocked_body");
        l_oU.m_oStore.m_strUnlockedOk = l_oL.f_Tr("store.confirm_hint");
    }

    // ---- always on top ----
    l_oU.m_oOverlay.m_bToast = m_iToastTimer > 0 && !m_strToastMsg.empty();
    l_oU.m_oOverlay.m_strToastText = m_strToastMsg;
    // The pick-up / level-up / event lines wait while the player menu is open: they would cover its
    // right side (they still time out).
    if (!m_bInGameMenuOpen)
        for (const auto& l_pairN : m_vNotifications) l_oU.m_oOverlay.m_vNotifications.push_back(l_pairN.first);
}

void Game::f_BuildMenuUi(toms::UiMenu& l_oM) const {
    const toms::Locale& l_oL = m_oLocale;
    l_oM.m_bVisible = true;
    l_oM.m_strYesLabel = l_oL.f_Tr("menu.yes");
    l_oM.m_strNoLabel = l_oL.f_Tr("menu.no");
    l_oM.m_strCloseLabel = l_oL.f_Tr("menu.back");
    auto l_fnSel = [&](size_t l_nI) { return (int)l_nI == m_iInGameMenuSel; };
    // The left list is always there; a page (Settings, Skills, Forge, Village) opens on the right,
    // and while it is open the keyboard moves on its rows and the row that opened it stays lit.
    {
        auto l_vOrder = f_MainMenuOrder();
        int l_iLit = m_iInGameMenuSel;
        if (m_eInGameMenuPage == InGameMenuPage::Settings) l_iLit = f_MainMenuRowIndex(MainMenuRow::Settings);
        if (m_eInGameMenuPage == InGameMenuPage::Skills)   l_iLit = f_MainMenuRowIndex(MainMenuRow::Skills);
        if (m_eInGameMenuPage == InGameMenuPage::Forge)    l_iLit = f_MainMenuRowIndex(MainMenuRow::Forge);
        if (m_eInGameMenuPage == InGameMenuPage::Hub)      l_iLit = f_MainMenuRowIndex(MainMenuRow::Village);
        for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
            toms::UiRow l_oR;
            switch (l_vOrder[l_nI]) {
                case MainMenuRow::Store:
                    l_oR.m_strLabel = l_oL.f_Tr("store.icon_label");
                    l_oR.m_bEnabled = m_bStoreUnlocked;
                    if (!m_bStoreUnlocked) l_oR.m_strSub = l_oL.f_Tr("system.locked");
                    else if (m_bNewsStore) l_oR.m_strSub = "•";
                    break;
                case MainMenuRow::Save:        l_oR.m_strLabel = l_oL.f_Tr("saveload.row"); break;
                case MainMenuRow::Settings:    l_oR.m_strLabel = l_oL.f_Tr("menu.settings"); break;
                case MainMenuRow::Skills:
                    l_oR.m_strLabel = l_oL.f_Tr("ingame_menu.skills");
                    if (m_oRun.f_SkillPoints() > 0) l_oR.m_strSub = f_TrParam(l_oL.f_Tr("skill.points_label"), "n", std::to_string(m_oRun.f_SkillPoints()));
                    break;
                case MainMenuRow::Village:     l_oR.m_strLabel = l_oL.f_Tr("ingame_menu.village"); break;
                case MainMenuRow::Forge:       l_oR.m_strLabel = l_oL.f_Tr("ingame_menu.forge"); break;
                case MainMenuRow::Fullscreen:
                    l_oR.m_strLabel = l_oL.f_Tr("system.fullscreen");
                    l_oR.m_strSub = l_oL.f_Tr(m_bFullscreenOn ? "system.on" : "system.off");
                    break;
                case MainMenuRow::BackToTitle: l_oR.m_strLabel = l_oL.f_Tr("ingame_menu.back_to_title"); break;
            }
            l_oR.m_bSelected = (int)l_nI == l_iLit;
            l_oM.m_vMainRows.push_back(l_oR);
        }
    }
    switch (m_eInGameMenuPage) {
        case InGameMenuPage::Main: {
            l_oM.m_iPage = 0;
            l_oM.m_strCloseLabel = l_oL.f_Tr("inventory.close");
            break;
        }
        case InGameMenuPage::Settings: {
            l_oM.m_iPage = 1;
            l_oM.m_strHeader = l_oL.f_Tr("settings.header");
            const auto& l_vLangs = l_oL.f_Languages();
            for (size_t l_nI = 0; l_nI < l_vLangs.size(); l_nI++)
                l_oM.m_vRows.push_back({std::string((int)l_nI == l_oL.f_LanguageIndex() ? "[x] " : "[ ] ") + l_vLangs[l_nI].m_strName, "", l_fnSel(l_nI), true});
            if (!l_oM.m_vRows.empty()) l_oM.m_vRows[0].m_strSection = l_oL.f_Tr("settings.language");   // section: language
            f_AppendArtStyleRows(l_oM.m_vRows, m_iInGameMenuSel - (int)l_vLangs.size());       // section: art style
            const size_t l_nCam = l_oM.m_vRows.size();                                      // section: other
            l_oM.m_vRows.push_back({l_oL.f_Tr(m_oCam.f_Mode() == toms::Camera::Mode::Rooms ? "settings.camera_rooms" : "settings.camera_follow"),
                              "", l_fnSel(l_nCam), true});
            l_oM.m_vRows[l_nCam].m_strSection = l_oL.f_Tr("settings.other_section");
            l_oM.m_vRows.push_back({l_oL.f_Tr(m_oSettings.m_bReduceEffects ? "settings.reduce_effects_on" : "settings.reduce_effects_off"),
                              "", l_fnSel(l_nCam + 1), true});   // docs/gameplay/15 §3.8
            {   // the battle speed (17 I10): activating the row takes the next step
                char l_pszSpeed[16];
                std::snprintf(l_pszSpeed, sizeof l_pszSpeed, "%g", m_oSettings.m_fBattleSpeed);
                l_oM.m_vRows.push_back({f_TrParam(l_oL.f_Tr("settings.battle_speed"), "x", l_pszSpeed), "", l_fnSel(l_nCam + 2), true});
            }
            break;
        }
        case InGameMenuPage::Skills: {
            l_oM.m_iPage = 2;
            l_oM.m_strHeader = l_oL.f_Tr("ingame_menu.skills");
            l_oM.m_strSubheader = f_TrParam(l_oL.f_Tr("skill.points_label"), "n", std::to_string(m_oRun.f_SkillPoints()));
            auto l_vOrder = f_SkillMenuOrder();
            for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
                const auto& l_oDef = m_mapSkillDefs.at(l_vOrder[l_nI]);
                const bool l_bOwned = m_oRun.f_HasSkill(l_vOrder[l_nI]);
                const bool l_bUnlockable = !l_bOwned && toms::f_CanUnlockSkill(m_mapSkillDefs, m_oRun.f_SkillsOwned(), m_oRun.f_SkillPoints(), l_vOrder[l_nI]);
                const std::string l_strMark = l_bOwned ? "[x] " : (l_bUnlockable ? "[ ] " : "[--] ");
                const std::string l_strName = l_oL.f_Field(l_oDef.m_jName);
                l_oM.m_vRows.push_back({l_bOwned ? l_strMark + l_strName
                                        : f_TrParam(l_strMark + l_strName + " (" + l_oL.f_Tr("skill.cost_suffix") + ")", "n", std::to_string(l_oDef.m_iCost)),
                                  "", l_fnSel(l_nI), l_bOwned || l_bUnlockable});
            }
            break;
        }
        case InGameMenuPage::Forge: {
            l_oM.m_iPage = 3;
            l_oM.m_strHeader = l_oL.f_Tr("ingame_menu.forge");
            l_oM.m_strSubheader = f_TrParam(l_oL.f_Tr("forge.gold_label"), "n", std::to_string(m_oPl.m_iGold));
            auto l_vOrder = f_ForgeMenuOrder();
            for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
                const auto& l_oDef = m_mapForgeDefs.at(l_vOrder[l_nI]);
                const bool l_bKnown = std::find(m_oMeta.m_vForgeRecipesKnown.begin(), m_oMeta.m_vForgeRecipesKnown.end(), l_vOrder[l_nI]) !=
                                   m_oMeta.m_vForgeRecipesKnown.end();
                const bool l_bCraftable = l_bKnown && toms::f_CanCraft(m_mapForgeDefs, m_oMeta.m_vForgeRecipesKnown, m_oPl.m_vInv, m_oPl.m_iGold, l_vOrder[l_nI]);
                std::string l_strLabel = std::string(!l_bKnown ? "[--] " : (l_bCraftable ? "[ ] " : "[!] ")) +
                                    (l_bKnown ? l_oL.f_Field(l_oDef.m_jName) : l_oL.f_Tr("forge.unknown_recipe"));
                if (l_bKnown) l_strLabel += " (" + std::to_string(l_oDef.m_iCostGold) + "g)";
                l_oM.m_vRows.push_back({l_strLabel, "", l_fnSel(l_nI), l_bCraftable});
            }
            break;
        }
        case InGameMenuPage::Hub: {
            l_oM.m_iPage = 4;
            l_oM.m_strHeader = l_oL.f_Tr("ingame_menu.village");
            auto l_vOrder = f_HubMenuOrder();
            auto l_fnHasFlag = [this](const std::string& l_strF) { return m_oRun.f_Flag(l_strF); };
            for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
                const bool l_bUnlocked = toms::f_HubLocationUnlocked(m_mapHubDefs, l_fnHasFlag, l_vOrder[l_nI]);
                l_oM.m_vRows.push_back({std::string(l_bUnlocked ? "[ ] " : "[--] ") +
                                  (l_bUnlocked ? l_oL.f_Field(m_mapHubDefs.at(l_vOrder[l_nI]).m_jName) : l_oL.f_Tr("hub.unknown_location")),
                                  "", l_fnSel(l_nI), l_bUnlocked});
            }
            break;
        }
    }
    if (m_bInGameLangConfirmOpen && m_bInGameConfirmIsStyle) {
        l_oM.m_bConfirmOpen = true;
        l_oM.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("settings.style_confirm"), "style", f_ArtStyleName(m_iInGameLangConfirmIdx));
        l_oM.m_strConfirmBody = l_oL.f_Tr("settings.style_apply_title");   // a run never changes look under the player
        l_oM.m_bConfirmYes = m_bInGameLangConfirmYes;
    } else if (m_bInGameLangConfirmOpen) {
        const auto& l_vLangs = l_oL.f_Languages();
        l_oM.m_bConfirmOpen = true;
        l_oM.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr("settings.language_confirm"), "lang",
                                     (m_iInGameLangConfirmIdx >= 0 && m_iInGameLangConfirmIdx < (int)l_vLangs.size())
                                     ? l_vLangs[m_iInGameLangConfirmIdx].m_strName : "");
        l_oM.m_bConfirmYes = m_bInGameLangConfirmYes;
    }
}

void Game::f_UiEvent(const std::string& l_strName, int l_iArg) {
    // ---- title ----
    if (l_strName == "title_row")     { if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_ClickRow(l_iArg)); return; }
    if (l_strName == "title_hover")   { if (m_oTitle.f_IsOpen()) m_oTitle.f_HoverRow(l_iArg); return; }
    if (l_strName == "title_back")    { if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_Cancel()); return; }
    if (l_strName == "title_confirm") { if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_AnswerConfirm(l_iArg != 0)); return; }
    // ---- ending ----
    if (l_strName == "ending_rebirth") { if (f_EndingActive()) f_Rebirth(); return; }
    if (l_strName == "ending_title")   { if (f_EndingActive()) f_DismissEndingScreen(); return; }
    if (l_strName == "ending_dismiss") { if (f_EndingActive() && !f_RebirthOffered()) f_DismissEndingScreen(); return; }
    // ---- HUD buttons ----
    if (l_strName == "hud_menu")      { if (!f_ModalActive()) { f_CancelWalk(); f_OpenPlayerMenu(); } return; }
    // ---- battle ----
    if (l_strName == "battle_attack")   { f_BattleTapAttack(); return; }
    if (l_strName == "minigame_tap")    { f_MiniGameTap(); return; }
    if (l_strName == "minigame_close")  { f_CloseMiniGame(); return; }
    if (l_strName == "battle_defend")   { f_BattleTapDefense(); return; }
    if (l_strName == "battle_super")    { f_BattleTapSuper(); return; }
    if (l_strName == "battle_active")   { f_BattleTapActive(); return; }
    if (l_strName == "battle_target")   { f_BattleSetTarget(l_iArg); return; }
    if (l_strName == "battle_cell")     { f_BattleMoveToCell(l_iArg); return; }
    if (l_strName == "battle_continue") { if (m_oCs.m_bWon) f_DismissVictory(); return; }
    // ---- dialogue ----
    if (l_strName == "dlg_choose")  { if (m_bInDialogue && l_iArg >= 0 && l_iArg < (int)m_vDlgChoices.size()) { m_iDlgSel = l_iArg; f_ChooseDialogue(l_iArg); } return; }
    if (l_strName == "dlg_hover")   { if (m_bInDialogue && l_iArg >= 0 && l_iArg < (int)m_vDlgChoices.size()) m_iDlgSel = l_iArg; return; }
    if (l_strName == "dlg_advance") { if (m_bInDialogue) f_ChooseDialogue(m_iDlgSel); return; }
    // ---- the player menu (game_player_menu.cpp) ----
    if (l_strName.rfind("pm_", 0) == 0) { f_PlayerMenuEvent(l_strName, l_iArg); return; }
    // ---- store ----
    if (l_strName == "store_tab") {
        if (m_bStoreOpen && l_iArg >= 0 && l_iArg < 4 && l_iArg != m_iStoreTab) { m_iStoreTab = l_iArg; m_iStoreSel = 0; m_oAudio.f_Play("confirm_click"); }
        return;
    }
    if (l_strName == "store_hover") { if (m_bStoreOpen && l_iArg >= 0 && l_iArg < (int)f_StoreTabIndices().size()) m_iStoreSel = l_iArg; return; }
    if (l_strName == "store_buy") {
        const std::vector<int> l_vIdx = f_StoreTabIndices();
        if (m_bStoreOpen && l_iArg >= 0 && l_iArg < (int)l_vIdx.size()) { m_iStoreSel = l_iArg; f_BuyStoreItem(l_vIdx[l_iArg]); }
        return;
    }
    if (l_strName == "store_close")       { if (m_bStoreOpen) f_CloseStore(); return; }
    if (l_strName == "store_unlocked_ok") { if (m_bStoreUnlockDlg) f_StoreKey(13); return; }
    // ---- in-game menu ----
    if (l_strName == "menu_row")     { if (m_bInGameMenuOpen && !m_bInGameLangConfirmOpen) { m_iInGameMenuSel = l_iArg; f_InGameMenuActivate(); } return; }
    if (l_strName == "menu_hover")   { if (m_bInGameMenuOpen && !m_bInGameLangConfirmOpen) m_iInGameMenuSel = l_iArg; return; }
    if (l_strName == "menu_close")   { if (m_bInGameMenuOpen) f_InGameMenuBack(); return; }
    if (l_strName == "menu_confirm") { if (m_bInGameLangConfirmOpen) { m_bInGameLangConfirmYes = l_iArg != 0; f_InGameMenuActivate(); } return; }
    if (l_strName == "notice_ok") { f_DismissNotice(); return; }
    // ---- stairs ----
    if (l_strName == "stairs") { if (m_bStairsConfirmOpen) { if (l_iArg) f_ConfirmStageTransition(); else f_CancelStageTransition(); } return; }
    // ---- stage select ----
    if (l_strName == "stage_pick") {
        if (!m_bStageSelectOpen || l_iArg < 0 || l_iArg >= (int)m_vStageList.size()) return;
        const StageInfo& l_oInfo = m_vStageList[l_iArg];
        const bool l_bLocked = l_oInfo.m_iIndex > 1 && l_iArg > 0 &&
            std::find(m_oMeta.m_vUnlockedStages.begin(), m_oMeta.m_vUnlockedStages.end(), m_vStageList[l_iArg - 1].m_strId) == m_oMeta.m_vUnlockedStages.end();
        if (l_bLocked) return;
        f_LoadStage(l_oInfo.m_strId);
        f_CloseStageSelect();
        return;
    }
    if (l_strName == "stage_close") { if (m_bStageSelectOpen) f_CloseStageSelect(); return; }
}

// Each weapon's own play and a move's play (touch_play.h; docs/gameplay/15 §3.3, 16 §3, §6), drawn over the fight as
// plain boxes (battle.rcss .tp): the bow's moving target, its pull, charge and crosshair (and the first-pull tutorial);
// the sword's prompt popping in (a way, or a shape) and its input window; the staff's circles counting, hiding, the
// "!" and the taps; a move's shape and its corners; and where each answer landed (a ring, or a cross), fading.
void Game::f_TouchPlayUi(toms::UiBattle& l_oB) const {
    const toms::TouchPlay& l_oP = m_oCs.m_oTouch;
    l_oB.m_strTouchKind = toms::f_PlayTypeName(m_oCs.m_oSkill.m_bOn ? l_oP.f_Type() : f_BattlePlayType());
    auto l_fnAdd = [&](const std::string& l_strCls, float l_fCx, float l_fCy, float l_fW, float l_fH, float l_fRot = 0.0f, float l_fRadius = 0.0f,
                   float l_fOpacity = 1.0f, std::string l_strText = {}) {
        l_oB.m_vTouch.push_back({l_strCls, l_fCx - l_fW * 0.5f, l_fCy - l_fH * 0.5f, l_fW, l_fH, l_fRot, l_fRadius, l_fOpacity, std::move(l_strText)});
    };
    auto l_fnLine = [&](const std::string& l_strCls, glm::vec2 l_v2A, glm::vec2 l_v2C, float l_fThick, float l_fOpacity = 1.0f) {
        const glm::vec2 l_v2D = l_v2C - l_v2A;
        const float l_fLen = std::sqrt(l_v2D.x * l_v2D.x + l_v2D.y * l_v2D.y);
        if (l_fLen >= 2.0f) l_fnAdd(l_strCls, (l_v2A.x + l_v2C.x) * 0.5f, (l_v2A.y + l_v2C.y) * 0.5f, l_fLen, l_fThick, std::atan2(l_v2D.y, l_v2D.x) * 57.29578f, l_fThick * 0.5f, l_fOpacity);
    };
    for (const toms::TouchPlay::Mark& l_oM : l_oP.f_Marks()) {
        if (l_oM.m_iAgeMs >= 500) continue;
        const float l_fK = (float)l_oM.m_iAgeMs / 500.0f, l_fS = 34.0f + 30.0f * l_fK;
        if (l_oM.m_bOk) l_fnAdd("mark ok", l_oM.m_fX, l_oM.m_fY, l_fS, l_fS, 0.0f, l_fS * 0.5f, 1.0f - l_fK);
        else { l_fnAdd("mark bad", l_oM.m_fX, l_oM.m_fY, 40, 7, 45.0f, 3.5f, 1.0f - l_fK); l_fnAdd("mark bad", l_oM.m_fX, l_oM.m_fY, 40, 7, -45.0f, 3.5f, 1.0f - l_fK); }
    }
    if (!l_oP.f_Running() || (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase != CombatState::SkillRun::Phase::Play)) return;
    const std::string l_strCount = std::to_string(std::min(l_oP.f_Index() + 1, l_oP.f_Points())) + "/" + std::to_string(l_oP.f_Points());
    switch (l_oP.f_Type()) {
    case toms::PlayType::ePULL_RELEASE: {
        const toms::TouchPlay::Target& l_oT = l_oP.f_Target();
        l_fnAdd(l_oP.f_Started() ? "target" : "target idle", l_oT.m_fX, l_oT.m_fY, 2 * l_oT.m_fR, 2 * l_oT.m_fR, 0.0f, l_oT.m_fR);
        l_fnAdd("bull", l_oT.m_fX, l_oT.m_fY, 0.8f * l_oT.m_fR, 0.8f * l_oT.m_fR, 0.0f, 0.4f * l_oT.m_fR);
        if (l_oP.f_Points() > 1) l_fnAdd("count", l_oT.m_fX, l_oT.m_fY - l_oT.m_fR - 18.0f, 80, 24, 0.0f, 0.0f, 1.0f, l_strCount);
        if (l_oP.f_Started()) l_fnAdd("time", l_oT.m_fX, l_oT.m_fY + l_oT.m_fR + 10.0f, 2 * l_oT.m_fR * l_oP.f_TimeLeft(), 5, 0.0f, 2.5f);
        if (l_oP.f_Aiming()) {
            const glm::vec2 l_v2C = l_oP.f_Crosshair(), l_v2A = l_oP.f_AimFrom();
            const float l_fCh = l_oP.f_Charge(), l_fS = 26.0f + 40.0f * l_fCh;
            l_fnLine("pull", l_v2A, l_oP.f_AimTo(), 5);
            l_fnLine("shot", l_v2A, l_v2C, 2);
            l_fnAdd("anchor", l_v2A.x, l_v2A.y, 14, 14, 0.0f, 7);
            l_fnAdd(l_fCh >= 1.0f ? "charge full" : "charge", l_v2A.x, l_v2A.y, l_fS, l_fS, 0.0f, l_fS * 0.5f);   // the pull's charge (16 §3.2)
            l_fnAdd("cross", l_v2C.x, l_v2C.y, 28, 28, 0.0f, 14);
        } else if (!m_oSettings.m_bBowTutorialDone && !l_oP.f_Started() && !m_oCs.m_oSkill.m_bOn) {
            // the first pull's tutorial: a ghost finger touches under the target, drags back and lets go (16 §3.2)
            const float l_fK = (float)(m_oCs.m_iClockMs % 1600) / 1600.0f;
            const float l_fDrag = std::clamp((l_fK - 0.25f) / 0.45f, 0.0f, 1.0f);
            const glm::vec2 l_v2From(l_oT.m_fX, l_oT.m_fY + 90.0f), l_v2To(l_oT.m_fX, l_oT.m_fY + 150.0f);
            const glm::vec2 l_v2F = l_v2From + (l_v2To - l_v2From) * l_fDrag;
            if (l_fK < 0.85f) {
                if (l_fDrag > 0.0f) l_fnLine("pull", l_v2From, l_v2F, 4, 0.6f);
                l_fnAdd("ghost", l_v2F.x, l_v2F.y, 34, 34, 0.0f, 17, l_fK < 0.15f ? l_fK / 0.15f : 1.0f);
            }
            l_fnAdd("tutorial", l_oT.m_fX, l_oT.m_fY + 190.0f, 360, 28, 0.0f, 0.0f, 1.0f, m_oLocale.f_Tr("battle.play_hint_pullRelease"));
        }
        break;
    }
    case toms::PlayType::eFREE_SWIPE:
    case toms::PlayType::eFREE_TAP:
    case toms::PlayType::eFREE_SHOT: {   // the ultimates (17 §5): the time left, how many so far, the swipe's trail
        const glm::vec4 l_v4A = f_TouchArea();
        const float l_fCx = l_v4A.x + l_v4A.z * 0.5f, l_fBase = l_v4A.y + l_v4A.w;
        l_fnAdd("count", l_fCx, l_fBase - 24.0f, 120, 28, 0.0f, 0.0f, 1.0f, std::to_string(l_oP.f_Hits()));
        l_fnAdd("time open", l_fCx, l_fBase + 6.0f, 320.0f * l_oP.f_TimeLeft(), 8, 0.0f, 4.0f);
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine("swipe", l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
        break;
    }
    case toms::PlayType::eSWIPE_WAY:
    case toms::PlayType::eSWIPE_GESTURE: {
        const toms::CueTrack& l_oCues = l_oP.f_Cues();
        const glm::vec2 l_v2At = l_oP.f_PromptAt();
        l_fnAdd("count", l_v2At.x, l_v2At.y - 56.0f, 80, 24, 0.0f, 0.0f, 1.0f, l_strCount);
        const int l_iAge = l_oCues.f_CueAgeMs();
        if (!l_oCues.f_Done() && l_iAge >= 0) {   // the prompt pops in with a little bounce (16 §3.1)
            const int l_iK = l_oCues.f_Index();
            const float l_fPop = l_iAge < 180 ? 0.5f + 0.7f * std::sin((float)l_iAge / 180.0f * 1.5708f) : l_iAge < 260 ? 1.2f - 0.2f * (float)(l_iAge - 180) / 80.0f : 1.0f;
            const std::string l_strCls = l_oCues.f_Open() ? "arrow now open" : "arrow now";
            if (l_oP.f_CueKind(l_iK) == "way") {
                const float l_fAng = (float)l_oP.f_CueDir(l_iK) * 45.0f, l_fRad = l_fAng / 57.29578f;
                const glm::vec2 l_v2Dir(std::cos(l_fRad), std::sin(l_fRad));
                l_fnAdd(l_strCls, l_v2At.x - l_v2Dir.x * 8.0f * l_fPop, l_v2At.y - l_v2Dir.y * 8.0f * l_fPop, 56 * l_fPop, 12 * l_fPop, l_fAng, 6 * l_fPop);
                l_fnAdd(l_strCls + " head", l_v2At.x + l_v2Dir.x * 16.0f * l_fPop, l_v2At.y + l_v2Dir.y * 16.0f * l_fPop, 26 * l_fPop, 26 * l_fPop, l_fAng + 45.0f);
            } else {   // a shape to draw: its outline
                const std::string& l_strCue = l_oP.f_CueKind(l_iK);
                std::vector<glm::vec2> l_vC = toms::f_ShapeCorners(l_strCue);
                if (l_strCue == "v" || l_strCue == "m") {   // a tick or an M can face any way (17 §4.2): each prompt turned its own way
                    const float l_fTurn = (float)l_oP.f_CueDir(l_iK) * 45.0f / 57.29578f, l_fCos = std::cos(l_fTurn), l_fSin = std::sin(l_fTurn);
                    for (glm::vec2& l_v2C : l_vC) {
                        const glm::vec2 l_v2D = l_v2C - glm::vec2(0.5f);
                        l_v2C = glm::vec2(0.5f) + glm::vec2(l_v2D.x * l_fCos - l_v2D.y * l_fSin, l_v2D.x * l_fSin + l_v2D.y * l_fCos);
                    }
                }
                for (size_t l_nI = 1; l_nI < l_vC.size(); l_nI++)
                    l_fnLine(l_strCls, l_v2At + (l_vC[l_nI - 1] - glm::vec2(0.5f)) * 70.0f * l_fPop, l_v2At + (l_vC[l_nI] - glm::vec2(0.5f)) * 70.0f * l_fPop, 7);
            }
            l_fnAdd(l_oCues.f_Open() ? "time open" : "time", l_v2At.x, l_v2At.y + 46.0f, 160.0f * l_oCues.f_TimeLeft(), 6, 0.0f, 3.0f);
        }
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine("swipe", l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
        break;
    }
    case toms::PlayType::eSEQUENCE_CLICK: {
        const auto& l_vCircles = l_oP.f_Circles();
        const float l_fR = 34.0f;
        const auto l_ePhase = l_oP.f_CirclePhase();
        for (size_t l_nK = 0; l_nK < l_vCircles.size(); l_nK++) {
            const toms::TouchPlay::Circle& l_oC = l_vCircles[l_nK];
            if (l_oC.m_iState != 0 || l_ePhase == toms::TouchPlay::CirclePhase::Hide) continue;
            if (l_ePhase == toms::TouchPlay::CirclePhase::Count) {   // lit in turn with its number (16 §3.3)
                const bool l_bLit = (int)l_nK < l_oP.f_CirclesLit();
                l_fnAdd(l_bLit ? "circle" : "circle idle", l_oC.m_fX, l_oC.m_fY, 2 * l_fR, 2 * l_fR, 0.0f, l_fR, l_bLit ? 1.0f : 0.35f, l_bLit ? std::to_string(l_nK + 1) : std::string());
                continue;
            }
            l_fnAdd("circle", l_oC.m_fX, l_oC.m_fY, 2 * l_fR, 2 * l_fR, 0.0f, l_fR);   // back together, without the numbers
            if (l_ePhase == toms::TouchPlay::CirclePhase::Warn) l_fnAdd("warn", l_oC.m_fX, l_oC.m_fY - l_fR - 18.0f, 30, 34, 0.0f, 0.0f, 1.0f, "!");
        }
        if (l_ePhase == toms::TouchPlay::CirclePhase::Tap) {
            const glm::vec4 l_v4A = f_TouchArea();
            l_fnAdd("time", l_v4A.x + l_v4A.z * 0.5f, l_v4A.y + l_v4A.w + 6.0f, 200.0f * l_oP.f_TimeLeft(), 6, 0.0f, 3.0f);
        }
        break;
    }
    case toms::PlayType::eDRAW_SHAPE: {   // a move's shape: its lines, the corners reached, the next one (16 §6.1)
        const toms::ShapeTrace& l_oTr = l_oP.f_Trace();
        const auto& l_vPts = l_oTr.f_Points();
        for (size_t l_nI = 1; l_nI < l_vPts.size(); l_nI++) l_fnLine((int)l_nI < l_oTr.f_Reached() ? "star done" : "star", l_vPts[l_nI - 1], l_vPts[l_nI], 6);
        const bool l_bClosed = l_vPts.size() > 1 && glm::length(l_vPts.front() - l_vPts.back()) < 0.5f;   // its end is its start
        for (size_t l_nI = 0; l_nI < l_vPts.size() - (l_bClosed ? 1 : 0); l_nI++) {
            const bool l_bDone = (int)l_nI < l_oTr.f_Reached(), l_bNext = (int)l_nI == l_oTr.f_Reached();
            l_fnAdd(l_bDone ? "corner done" : l_bNext ? "corner next" : "corner", l_vPts[l_nI].x, l_vPts[l_nI].y, 34, 34, 0.0f, 17);
        }
        if (l_oTr.f_Drawing()) l_fnAdd("cross", l_oTr.f_Last().x, l_oTr.f_Last().y, 24, 24, 0.0f, 12);
        break;
    }
    case toms::PlayType::eMASH: {
        const glm::vec4 l_v4A = f_TouchArea();
        const float l_fPulse = 1.0f + 0.08f * std::sin((float)l_oP.f_ClockMs() * 0.025f);
        l_fnAdd("circle", l_v4A.x + l_v4A.z * 0.5f, l_v4A.y + l_v4A.w * 0.5f, 110 * l_fPulse, 110 * l_fPulse, 0.0f, 55 * l_fPulse, 1.0f, l_strCount);
        break;
    }
    default:
        break;
    }
}

// game_ui.cpp -- the UI contract (see ui/ui_state.h): buildUiState() describes every screen as
// plain data each frame, uiEvent() carries out what a button in assets/media/ui/*.rml asked for.
// Replaced the hand-drawn UI (drawTitleScreen, drawInventory, drawStoreUI, ...) on 2026-09-27;
// every rule still lives in the Game functions these call.
#include "ui_names.h"     // toms::ui: the names the .rml / .rcss files use
#include "res_keys.h"
#include "text_keys.h"     // toms::text: every line of text by its key
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_names.h"   // toms::state, toms::battle_event, ... (no hard-coded name)
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
    for (const auto& l_pairKv : m_mapItemDefs) l_vNames.push_back(l_pairKv.second.value(toms::field::g_pszSprite, std::string()));

    std::vector<std::string> l_vMissing;
    for (std::string l_strN : l_vNames) {
        if (l_strN.empty()) continue;
        if (l_strN.size() > 4 && l_strN.compare(l_strN.size() - 4, 4, ".png") == 0) l_strN.resize(l_strN.size() - 4);
        if (!m_oSpriteAtlas.f_Find(l_strN) && std::find(l_vMissing.begin(), l_vMissing.end(), l_strN) == l_vMissing.end()) l_vMissing.push_back(l_strN);
    }
    return l_vMissing;
}

std::string Game::f_ArtStyleName(int l_iIdx) const {
    if (l_iIdx <= 0 || l_iIdx >= (int)m_vArtStyles.size()) return m_oLocale.f_Tr(toms::text::g_pszSettingsStyleOriginal);
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
        if (l_iI == 0) l_oR.m_strSection = m_oLocale.f_Tr(toms::text::g_pszSettingsStyleSection);
        l_oR.m_strLabel = std::string(l_iI == l_iChosen ? "[x] " : "[ ] ") + f_ArtStyleName(l_iI);
        if (l_iI == l_iChosen && l_iChosen != m_iLoadedArtStyle) l_oR.m_strSub = m_oLocale.f_Tr(toms::text::g_pszSettingsStylePending);
        l_oR.m_bSelected = l_iI == l_iSelected;
        l_vRows.push_back(l_oR);
    }
}

std::string Game::f_ItemSprite(const std::string& l_strId) const {
    auto l_itIt = m_mapItemDefs.find(l_strId);
    return f_UiSprite(l_itIt == m_mapItemDefs.end() ? "coin" : l_itIt->second.value(toms::field::g_pszSprite, std::string("coin.png")));
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
    l_fnAdd(toms::field::g_pszHp, "HP"); l_fnAdd(toms::field::g_pszMaxhp, "Max HP"); l_fnAdd(toms::field::g_pszAtk, "ATK"); l_fnAdd(toms::field::g_pszDef, "DEF");
    l_fnAdd(toms::field::g_pszExp, "EXP"); l_fnAdd(toms::field::g_pszGold, m_oLocale.f_Tr(toms::text::g_pszStatusGold));
    for (const char* l_pszK : {toms::field::g_pszKeyYellow, toms::field::g_pszKeyBlue, toms::field::g_pszKeyRed}) l_fnAdd(l_pszK, f_ItemName(l_pszK));
    for (const AttrDef& l_oA : m_vAttrDefs) l_fnAdd(l_oA.m_strId, l_oA.m_strShortName);
    if (l_jEff.contains(toms::field::g_pszWarp)) l_vParts.push_back("Warp");
    if (l_vParts.empty()) l_vParts.push_back(m_oLocale.f_Tr(toms::text::g_pszInventoryNoEffect));
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
        l_oT.m_strTitle = l_oL.f_Tr(toms::text::g_pszGameTitle);
        l_oT.m_strSubtitle = l_oL.f_Tr(toms::text::g_pszGameSubtitle);
        l_oT.m_strVersion = m_bShowVersion ? m_strVersionLabel : std::string();
        l_oT.m_iPage = (int)m_oTitle.f_Page();
        l_oT.m_strBackLabel = l_oL.f_Tr(toms::text::g_pszMenuBack);
        l_oT.m_strYesLabel = l_oL.f_Tr(toms::text::g_pszMenuYes);
        l_oT.m_strNoLabel = l_oL.f_Tr(toms::text::g_pszMenuNo);
        switch (m_oTitle.f_Page()) {
            case toms::TitlePage::Menu: {
                static const char* l_arrKeys[3] = {toms::text::g_pszMenuNewGame, toms::text::g_pszMenuContinue, toms::text::g_pszMenuSettings};
                for (int l_iI = 0; l_iI < 3; l_iI++)
                    l_oT.m_vRows.push_back({l_oL.f_Tr(l_arrKeys[l_iI]), l_oL.f_Tr(std::string(l_arrKeys[l_iI]) + ".desc"), m_oTitle.f_MenuSelection() == l_iI, true});
                l_oT.m_strHint = l_oL.f_Tr(toms::text::g_pszMenuHint);
                break;
            }
            case toms::TitlePage::Continue: {
                l_oT.m_strHeader = l_oL.f_Tr(toms::text::g_pszContinueHeader);
                const auto& l_vSums = m_oTitle.f_Summaries();
                bool l_bAny = false;
                for (int l_iI = 0; l_iI < m_oTitle.f_SlotCount(); l_iI++) {
                    toms::UiRow l_oR;
                    l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszContinueSlot) + " " + std::to_string(l_iI + 1);
                    if (l_iI < (int)l_vSums.size() && l_vSums[l_iI].m_bExists) {
                        const toms::SlotSummary& l_oS = l_vSums[l_iI];
                        l_bAny = true;
                        l_oR.m_strLabel += "   " + (l_oS.m_strStageName.empty() ? l_oS.m_strStageId : l_oS.m_strStageName) + "   " + l_oL.f_Tr(toms::text::g_pszHudLevel) +
                                   " " + std::to_string(l_oS.m_iLv) + "   HP " + std::to_string(l_oS.m_iHp) + "/" +
                                   std::to_string(l_oS.m_iMaxhp) + "   " + std::to_string(l_oS.m_iGold) + "G";
                        l_oR.m_strSub = l_oL.f_Tr(toms::text::g_pszContinueSavedAt) + " " + l_oS.m_strSavedAt + "    " + l_oL.f_Tr(toms::text::g_pszContinuePlayTime) +
                                " " + f_FmtPlayTime(l_oS.m_iPlayTimeSec);
                        if (l_oS.m_bFromBackup) l_oR.m_strSub += "    [" + l_oL.f_Tr(toms::text::g_pszContinueFromBackup) + "]";
                        if (l_oS.m_bNewer) l_oR.m_strSub += "    [" + l_oL.f_Tr(toms::text::g_pszContinueNewer) + "]";
                    } else if (l_iI < (int)l_vSums.size() && l_vSums[l_iI].m_bDamaged) {
                        l_oR.m_strLabel += "   [" + l_oL.f_Tr(toms::text::g_pszContinueDamaged) + "]";
                    } else {
                        l_oR.m_strLabel += "   [" + l_oL.f_Tr(toms::text::g_pszContinueEmpty) + "]";
                    }
                    l_oR.m_bSelected = m_oTitle.f_SlotSelection() == l_iI;
                    l_oT.m_vRows.push_back(l_oR);
                }
                l_oT.m_bShowBack = true;
                l_oT.m_bShowEmptyHint = !l_bAny;
                l_oT.m_strEmptyHint = l_oL.f_Tr(toms::text::g_pszContinueHint);
                l_oT.m_strHint = l_oL.f_Tr(toms::text::g_pszMenuHint);
                if (m_oTitle.f_NewGameConfirmOpen()) {
                    l_oT.m_bConfirmOpen = true;
                    l_oT.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr(toms::text::g_pszContinueNewGameConfirm), toms::field::g_pszSlot,
                                                 std::to_string(m_oTitle.f_NewGameConfirmSlot()));
                    l_oT.m_strConfirmBody = l_oL.f_Tr(toms::text::g_pszContinueNewGameConfirmBody);
                    l_oT.m_bConfirmYes = m_oTitle.f_NewGameConfirmYesSelected();
                }
                break;
            }
            case toms::TitlePage::Settings: {
                l_oT.m_strHeader = l_oL.f_Tr(toms::text::g_pszSettingsHeader);
                const auto& l_vLangs = l_oL.f_Languages();
                for (int l_iI = 0; l_iI < (int)l_vLangs.size(); l_iI++)
                    l_oT.m_vRows.push_back({std::string(l_iI == l_oL.f_LanguageIndex() ? "[x] " : "[ ] ") + l_vLangs[l_iI].m_strName, "",
                                      m_oTitle.f_SettingsSelection() == l_iI, true});
                if (!l_oT.m_vRows.empty()) l_oT.m_vRows[0].m_strSection = l_oL.f_Tr(toms::text::g_pszSettingsLanguage);   // section 1: language
                f_AppendArtStyleRows(l_oT.m_vRows, m_oTitle.f_SettingsSelection() - (int)l_vLangs.size());   // section 2: art style
                l_oT.m_bShowBack = true;
                l_oT.m_strHint = l_oL.f_Tr(toms::text::g_pszSettingsHint);
                if (m_oTitle.f_StyleConfirmOpen()) {
                    l_oT.m_bConfirmOpen = true;
                    l_oT.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr(toms::text::g_pszSettingsStyleConfirm), toms::field::g_pszStyle,
                                                 f_ArtStyleName(m_oTitle.f_StyleConfirmIndex()));
                    l_oT.m_strConfirmBody = l_oL.f_Tr(toms::text::g_pszSettingsStyleApplyNow);   // the title reloads the images at once
                    l_oT.m_bConfirmYes = m_oTitle.f_StyleConfirmYesSelected();
                }
                if (m_oTitle.f_LanguageConfirmOpen()) {
                    const int l_iIdx = m_oTitle.f_LanguageConfirmIndex();
                    l_oT.m_bConfirmOpen = true;
                    l_oT.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr(toms::text::g_pszSettingsLanguageConfirm), toms::field::g_pszLang,
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
        l_oE.m_strRebirthLabel = l_oL.f_Tr(toms::text::g_pszEndingRebirthButton);
        l_oE.m_strTitleLabel = l_oL.f_Tr(toms::text::g_pszEndingTitleButton);
        l_oE.m_strHint = l_oL.f_Tr(toms::text::g_pszEndingDismissHint);
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
        l_oH.m_strTitleLine = l_oL.f_Tr(toms::text::g_pszGameTitle) + " — " + m_oSt.m_strName + " (" + std::to_string(m_oSt.m_iIndex) + "/" +
                       std::to_string(m_iTotalStages) + ")";
        l_oH.m_fHpPct = f_Pct((float)m_oPl.m_iHp, (float)f_EffectiveMaxHp());
        l_oH.m_strHpText = "HP " + std::to_string(m_oPl.m_iHp) + "/" + std::to_string(f_EffectiveMaxHp());
        l_oH.m_strStatsLine = "ATK " + std::to_string(f_EffectiveAtk()) + "   DEF " + std::to_string(f_EffectiveDef()) + "   LV " + std::to_string(m_oPl.m_iLv);
        l_oH.m_strResLine = "GOLD " + std::to_string(m_oPl.m_iGold) + "   " + l_oL.f_Tr(toms::text::g_pszHudCrystals) + " " + std::to_string(m_oPl.m_iCrystals) +
                     "   EXP " + std::to_string(m_oPl.m_iExp) + "   " + l_oL.f_Tr(toms::text::g_pszHudKeys) +
                     " Y" + std::to_string(m_oPl.m_iKeyYellow) + " B" + std::to_string(m_oPl.m_iKeyBlue) + " R" +
                     std::to_string(m_oPl.m_iKeyRed);
        // The floor's story line: its intro, then its ambient lines as the player moves.
        l_oH.m_strFooter = m_oSt.m_strStoryNote;
        if (!m_strStoryIntroKey.empty()) {
            const int l_iIdx = f_StoryLineIndex();
            const std::string& l_strKey = (l_iIdx == 0 || m_vStoryAmbientKeys.empty())
                                     ? m_strStoryIntroKey : m_vStoryAmbientKeys[(size_t)(l_iIdx - 1) % m_vStoryAmbientKeys.size()];
            std::string l_strTr = l_oL.f_Tr(l_strKey);
            if (!l_strTr.empty() && l_strTr.rfind(toms::key_prefix::g_pszStory, 0) != 0) l_oH.m_strFooter = l_strTr;
        }
        l_oH.m_strMenuLabel = l_oL.f_Tr(toms::text::g_pszIngameMenuTitle);
        l_oH.m_bMenuNews = m_bNewsStore || m_bNewsGear || m_bNewsEvents || m_oRun.f_SkillPoints() > 0;
        if (m_fChapterCardMs > 0.0f && !m_strChapterCardTitle.empty()) {
            l_oH.m_bChapterCard = true;
            l_oH.m_strChapterTitle = m_strChapterCardTitle;
            l_oH.m_strChapterSub = m_oSt.m_strName;
            l_oH.m_fChapterAlpha = std::min(1.0f, m_fChapterCardMs / 600.0f);
        }
        // Name plates over bosses and roamers: over the head of its sprite standing on the 3D floor.
        const float l_fScreenW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fScreenH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
        for (const auto& l_oE : m_oSt.m_vEntities) {
            if (!m_bWalk3DOn || l_oE.m_bConsumed || !(l_oE.m_oFp.f_BossTier() || l_oE.m_bRoamer)) continue;
            const float l_fTall = f_Walk3DHeight("monster") * (float)l_oE.m_oFp.m_iH + 0.1f;
            bool l_bOk = false;
            const glm::vec2 l_v2P = f_Walk3DToDesign(glm::vec3(l_oE.m_iX + l_oE.m_oFp.m_iW * 0.5f, l_fTall, l_oE.m_iY + l_oE.m_oFp.m_iH - 0.45f), l_bOk);
            if (!l_bOk || l_v2P.x < 0 || l_v2P.x > l_fScreenW || l_v2P.y < 0 || l_v2P.y > l_fScreenH) continue;
            l_oH.m_vBanners.push_back({l_oE.m_strDisplayName.empty() ? l_oE.m_strId : l_oE.m_strDisplayName, l_v2P.x, l_v2P.y});
        }


        if (m_bStairsConfirmOpen) {
            l_oU.m_oStairs.m_bVisible = true;
            l_oU.m_oStairs.m_strQuestion = l_oL.f_Tr(m_bStairsConfirmIsUp ? toms::text::g_pszStairsConfirmUp : toms::text::g_pszStairsConfirmDown);
            l_oU.m_oStairs.m_strYesLabel = l_oL.f_Tr(toms::text::g_pszMenuYes) + " (Enter)";
            l_oU.m_oStairs.m_strNoLabel = l_oL.f_Tr(toms::text::g_pszMenuNo) + " (Esc)";
        }
        if (m_bInGameMenuOpen) f_BuildPlayerUi(l_oU);
        if (m_bStageSelectOpen) {
            toms::UiStageSelect& l_oS = l_oU.m_oStageSelect;
            l_oS.m_bVisible = true;
            l_oS.m_strTitle = l_oL.f_Tr(toms::text::g_pszStageselectTitle);
            l_oS.m_strHint = l_oL.f_Tr(m_bStageSelectTest ? toms::text::g_pszStageselectTestHint : toms::text::g_pszStageselectHint);
            l_oS.m_strCloseLabel = l_oL.f_Tr(toms::text::g_pszInventoryClose);
            for (size_t l_nI = 0; l_nI < m_vStageList.size(); l_nI++) {
                const StageInfo& l_oInfo = m_vStageList[l_nI];
                auto l_fnHas = [&](const std::string& l_strId) {
                    return std::find(m_oMeta.m_vUnlockedStages.begin(), m_oMeta.m_vUnlockedStages.end(), l_strId) != m_oMeta.m_vUnlockedStages.end();
                };
                const bool l_bReached = l_fnHas(l_oInfo.m_strId);
                const bool l_bLocked = !m_bStageSelectTest && l_oInfo.m_iIndex > 1 && l_nI > 0 && !l_fnHas(m_vStageList[l_nI - 1].m_strId);
                toms::UiRow l_oR;
                l_oR.m_strLabel = std::to_string(l_oInfo.m_iIndex) + ". " + l_oInfo.m_strName;
                if (l_bReached) l_oR.m_strLabel += "  " + l_oL.f_Tr(toms::text::g_pszStageselectReachedTag);
                else if (!l_bLocked) l_oR.m_strLabel += "  " + l_oL.f_Tr(toms::text::g_pszStageselectNewTag);
                l_oR.m_bEnabled = !l_bLocked;
                l_oR.m_strSub = l_bLocked ? f_TrParam(l_oL.f_Tr(toms::text::g_pszStageselectLockedHint), toms::field::g_pszFloor, std::to_string(l_oInfo.m_iIndex - 1)) : "";
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
        l_oB.m_strTitle = l_oL.f_Tr(toms::text::g_pszBattleTitle) + " " + (m_oCs.m_vFoes.empty() ? std::string() : m_oCs.m_vFoes[0].m_oEnemy.m_strName) +
                  (m_oCs.m_vFoes.size() > 1 ? " +" + std::to_string(m_oCs.m_vFoes.size() - 1) : std::string());
        l_oB.m_strPlayerSprite = f_UiSprite(f_PlayerSprite());
        // drawBattleRig draws the player instead of battle.rml's pixel picture. The picture is only a fallback for a weapon
        // whose clips could not load: while they are still to load (a battle's first frame) nothing shows, never the
        // old picture for a moment (it flashed before the animated hero and the 3D stage, the owner 2026-11-11).
        const bool l_bRigTried = m_strBattleRigWeapon == m_oEquipped.m_strWeaponId;
        l_oB.m_bPlayerAnim = !(l_bRigTried && !f_BattleRigReady());
        f_FillGauntletUi(l_oB);   // a boss's sequence: the ladder (battle.rml #gauntlet)
        for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) {   // the monsters (battle.rml .foe)
            const CombatState::Foe& l_oF = m_oCs.m_vFoes[l_nI];
            toms::UiFoe l_oUf;
            l_oUf.m_strSprite = f_UiSprite(f_MonsterSprite((int)l_nI));
            // drawBattleRig draws it; the pixel picture only when its clips were tried (a state) and could not load
            const bool l_bFoeTried = l_nI < m_vFoeRigs.size() && !m_vFoeRigs[l_nI].m_strState.empty();
            l_oUf.m_bAnim = !(l_bFoeTried && !m_vFoeRigs[l_nI].m_upView);
            l_oUf.m_fHpPct = f_Pct((float)std::max(0, l_oF.m_iHp), (float)l_oF.m_oEnemy.m_iHp);
            l_oUf.m_strName = l_oF.m_oEnemy.m_strName;
            l_oUf.m_strHpLine = "HP " + std::to_string(std::max(0, l_oF.m_iHp));
            l_oUf.m_strHpText = l_oUf.m_strName + " " + l_oUf.m_strHpLine;
            l_oUf.m_bTarget = m_oCs.m_bActive && m_oCs.m_vFoes.size() > 1 && (int)l_nI == m_oCs.m_iTarget && !l_oF.f_Down();
            l_oUf.m_bDown = l_oF.f_Down();
            {   // its head: where its name and HP go (kept on the screen, under the attack's band at the top)
                const glm::vec4 l_v4Box = f_FighterBox((int)l_nI);
                const float l_fW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f, l_fH = m_pRen ? (float)m_pRen->f_Height() : 768.0f;
                l_oUf.m_fHeadX = std::clamp(l_v4Box.x + l_v4Box.z * 0.5f, 90.0f, std::max(90.0f, l_fW - 90.0f));
                const float l_fMinY = 130.0f;   // its block (about 66 px high) stays under the title
                l_oUf.m_fHeadY = std::clamp(l_v4Box.y, l_fMinY, std::max(l_fMinY, l_fH - 120.0f));
            }
            if (m_oCs.m_bActive && !l_oF.f_Down()) {   // its intent (docs/gameplay/13 §6.1)
                const toms::MoveDef& l_oM = m_oMoves.f_Get(l_oF.m_strMove);
                if (l_oF.m_iStaggerMs > 0) {
                    l_oUf.m_strIntentKind = l_oF.m_bBroken ? toms::state::g_pszBroken : toms::battle_event::g_pszStagger;
                    l_oUf.m_strIntent = l_oL.f_Tr(l_oF.m_bBroken ? toms::text::g_pszBattleIntentBroken : toms::text::g_pszBattleIntentStagger);
                    l_oUf.m_fIntentPct = 0.0f;
                } else {
                    l_oUf.m_strIntentKind = toms::f_MoveKindName(l_oM.m_eKind);
                    const int l_iLeft = std::max(0, l_oF.m_iMoveMs - std::max(0, l_oF.m_iClockMs));
                    char l_pszSecs[16];
                    std::snprintf(l_pszSecs, sizeof l_pszSecs, " %.1fs", l_iLeft / 1000.0);
                    l_oUf.m_strIntent = (l_oL.f_Has(l_oM.m_strText) ? l_oL.f_Tr(l_oM.m_strText) : l_oM.m_strText) + l_pszSecs;
                    l_oUf.m_fIntentPct = f_Pct((float)std::max(0, l_oF.m_iClockMs), (float)std::max(1, l_oF.m_iMoveMs));
                }
                if (const toms::TutorialStep* l_pStep = f_TutorialStepNow(); l_pStep && l_pStep->m_bHold) { l_oUf.m_strIntent.clear(); l_oUf.m_fIntentPct = 0.0f; }   // it waits: no clock to read
                l_oUf.m_fBreakPct = f_Pct(l_oF.m_fBreakGauge, (float)f_FoeBreakPoints((int)l_nI));
                {   // its MP (the owner, 2026-11-25): shown when a move of its costs MP; bright when its dearest can come
                    int l_iDearest = 0;
                    for (const toms::MoveChoice& l_oC : l_oF.m_oEnemy.m_vMoves) l_iDearest = std::max(l_iDearest, f_FoeMoveMp((int)l_nI, l_oC.m_strId));
                    l_oUf.m_bMpOn = l_iDearest > 0 && l_oF.m_oMp.m_iMax > 0;
                    l_oUf.m_fMpPct = f_Pct(l_oF.m_fMp, (float)std::max(1, l_oF.m_oMp.m_iMax));
                    l_oUf.m_bMpReady = l_oUf.m_bMpOn && l_oF.m_fMp + 0.001f >= (float)l_iDearest;
                }
                l_oUf.m_bWarn = f_FoeWarning((int)l_nI) && !m_oCs.m_oSkill.m_bOn;   // the "!" before its hit (docs/gameplay/16 §4)
            }
            l_oB.m_vFoes.push_back(l_oUf);
        }
        l_oB.m_strEnemySprite = l_oB.m_vFoes.empty() ? std::string() : l_oB.m_vFoes[0].m_strSprite;
        l_oB.m_bEnemyAnim = !l_oB.m_vFoes.empty() && l_oB.m_vFoes[0].m_bAnim;
        l_oB.m_fPlayerHpPct = f_Pct((float)m_oCs.m_iPlayerHP, (float)f_EffectiveMaxHp());
        l_oB.m_fEnemyHpPct = l_oB.m_vFoes.empty() ? 0.0f : l_oB.m_vFoes[0].m_fHpPct;
        l_oB.m_strPlayerHpText = l_oL.f_Tr(toms::text::g_pszBattleYou) + " HP " + std::to_string(m_oCs.m_iPlayerHP);
        l_oB.m_strEnemyHpText = l_oB.m_vFoes.empty() ? std::string() : l_oB.m_vFoes[0].m_strHpText;
        l_oB.m_strLog = m_oCs.m_strLog;
        l_oB.m_strContinueHint = l_oL.f_Tr(toms::text::g_pszBattleContinueHint);
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
            l_oB.m_strClockLabel = l_oL.f_Tr(toms::text::g_pszBattleEnemyClock);
            // the clock of the monster that attacks next (the nearest to its move); the stamina pace shows the stamina
            float l_fBest = 0.0f;
            for (const CombatState::Foe& l_oF : m_oCs.m_vFoes)
                if (!l_oF.f_Down()) l_fBest = std::max(l_fBest, f_Pct((float)std::max(0, l_oF.m_iClockMs), (float)std::max(1, l_oF.m_iMoveMs)));
            l_oB.m_fClockPct = l_fBest;
            l_oB.m_bStaminaPace = m_oCs.m_ePace == toms::Pace::Stamina;
            if (l_oB.m_bStaminaPace) {
                l_oB.m_strClockLabel = l_oL.f_Tr(toms::text::g_pszBattleStamina);
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
                const std::string l_strName = m_mapEquipmentDefs.count(l_strKey) ? f_ItemName(l_strKey) : l_oL.f_Tr(toms::text::g_pszBattleUnarmed);
                const int l_iNext = toms::f_ExpToNextLevel(l_oWp, l_iLv);
                int l_iDone = 0;
                if (auto l_itU = m_mapWeaponExp.find(l_strKey); l_itU != m_mapWeaponExp.end()) {
                    l_iDone = l_itU->second - toms::f_WeaponExpForLevel(l_oWp, l_iLv);
                }
                l_oC.m_strLevelText = f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleWeaponLevel), toms::field::g_pszName, l_strName), toms::field::g_pszLv, std::to_string(l_iLv)) +
                               (l_iNext > 0 ? "  " + std::to_string(std::max(0, l_iDone)) + "/" + std::to_string(l_iNext) : std::string());
                l_oC.m_strPrompt = l_oL.f_Tr(l_oC.m_bRunning ? toms::text::g_pszBattleComboPromptRun : toms::text::g_pszBattleComboPrompt);
                l_oC.m_strButtonLabel = l_oL.f_Tr(l_oC.m_bCooling ? toms::text::g_pszBattleBtnCharging : l_oC.m_bRunning ? toms::text::g_pszBattleComboTap : toms::text::g_pszBattleComboStart);
            }
            l_oB.m_oDefense = f_PowerBar(toms::f_EffectiveDefenseBar(m_oEquipped, m_mapEquipmentDefs), m_oCs.m_oDefBar, l_oL.f_Tr(toms::text::g_pszBattleDefensePrompt),
                                 l_oL.f_Tr(m_oCs.m_oDefBar.m_bCooling ? toms::text::g_pszBattleBtnCharging : toms::text::g_pszBattleBtnHoldDefense));
            l_oB.m_bShieldArmed = m_oCs.m_bShieldBanked;
            l_oB.m_strShieldText = m_oCs.m_bShieldBanked
                ? f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleShieldArmed), toms::field::g_pszPct, std::to_string((int)std::lround(m_oCs.m_fShieldPower)))
                : l_oL.f_Tr(toms::text::g_pszBattleShieldNone);
            l_oB.m_bCritArmed = m_oCs.m_bNextAttackGuaranteedCrit;
            l_oB.m_strCritHint = l_oL.f_Tr(toms::text::g_pszBattleActiveArmedHint);
            l_oB.m_bFocus = f_CellIntroOn() && m_oCs.m_iTravelMs <= 0 && !m_oCs.m_bCellsShown;
            if (m_oCs.m_iBossIntroMs > 0 && !m_oCs.m_vFoes.empty()) {   // a boss's introduction: the banner slides in, the name types in
                const nlohmann::json l_jC = f_FeelFx().value(toms::field::g_pszBossIntro, nlohmann::json::object());
                const int l_iT = m_oCs.m_iBossIntroMsMax - m_oCs.m_iBossIntroMs;
                l_oB.m_bBossIntro = true;
                l_oB.m_strBossIntroLabel = l_oL.f_Tr(toms::text::g_pszBattleBossLabel);
                const float l_fIn = std::min(1.0f, (float)l_iT / 280.0f);
                l_oB.m_fBossIntroX = -1100.0f * (1.0f - l_fIn) * (1.0f - l_fIn);
                const std::string& l_strName = m_oCs.m_vFoes[0].m_oEnemy.m_strName;
                const int l_iChars = std::max(0, (l_iT - l_jC.value(toms::field::g_pszNameAtMs, 600)) / std::max(1, l_jC.value(toms::field::g_pszTypeMsPerChar, 110)) + 1);
                size_t l_nAt = 0;   // the first l_iChars letters (UTF-8: a letter is one to four bytes)
                for (int l_iK = 0; l_iK < l_iChars && l_nAt < l_strName.size(); l_iK++) {
                    const unsigned char l_u8C = (unsigned char)l_strName[l_nAt];
                    l_nAt += l_u8C < 0x80 ? 1 : l_u8C < 0xE0 ? 2 : l_u8C < 0xF0 ? 3 : 4;
                }
                l_oB.m_strBossIntroName = l_iT < l_jC.value(toms::field::g_pszNameAtMs, 600) ? std::string() : l_strName.substr(0, std::min(l_nAt, l_strName.size()));
            } else if (m_oCs.m_iHeroIntroMs > 0) {   // a special attack's introduction (22 §4.2): the hero's from the right, a monster's from the left
                const nlohmann::json l_jC = f_FeelFx().value(toms::field::g_pszHeroIntro, nlohmann::json::object());
                const int l_iT = m_oCs.m_iHeroIntroMsMax - m_oCs.m_iHeroIntroMs, l_iNameAt = l_jC.value(toms::field::g_pszNameAtMs, 350);
                l_oB.m_bBossIntro = true;
                l_oB.m_bHeroIntro = m_oCs.m_iIntroOwner < 0;
                l_oB.m_strBossIntroLabel = l_oL.f_Tr(m_oCs.m_strHeroIntroLabel);
                const float l_fIn = std::min(1.0f, (float)l_iT / 240.0f);
                l_oB.m_fBossIntroX = (m_oCs.m_iIntroOwner < 0 ? 1100.0f : -1100.0f) * (1.0f - l_fIn) * (1.0f - l_fIn);
                const std::string l_strName = m_oCs.m_strIntroName;
                const int l_iChars = std::max(0, (l_iT - l_iNameAt) / std::max(1, l_jC.value(toms::field::g_pszTypeMsPerChar, 90)) + 1);
                size_t l_nAt = 0;   // the first l_iChars letters (UTF-8)
                for (int l_iK = 0; l_iK < l_iChars && l_nAt < l_strName.size(); l_iK++) {
                    const unsigned char l_u8C = (unsigned char)l_strName[l_nAt];
                    l_nAt += l_u8C < 0x80 ? 1 : l_u8C < 0xE0 ? 2 : l_u8C < 0xF0 ? 3 : 4;
                }
                l_oB.m_strBossIntroName = l_iT < l_iNameAt ? std::string() : l_strName.substr(0, std::min(l_nAt, l_strName.size()));
            }
            const auto l_fnPop = [this](int l_iAgeMs) {   // grows past its size and settles (back-out)
                if (m_oSettings.m_bReduceEffects) return 1.0f;
                const float l_fK = std::clamp((float)l_iAgeMs / (float)m_oCellRules.m_iPopMs, 0.0f, 1.0f) - 1.0f;
                return std::max(0.01f, 1.0f + 2.70158f * l_fK * l_fK * l_fK + 1.70158f * l_fK * l_fK);
            };
            if (f_BattleCellsVisible()) {   // the six cells: where the hero stands, what is in danger and how soon (docs/gameplay/19 §2.1)
                l_oB.m_bCellsOn = true;
                l_oB.m_fCellsScale = l_fnPop(m_oCs.m_iCellsPopMs);
                const int l_iGoneMs = m_oCs.m_iCellsIdleMs - m_oCellRules.m_iHideAfterMs;   // no danger for a while: they shrink away
                if (l_iGoneMs > 0) {
                    const float l_fK = 1.0f - std::clamp((float)l_iGoneMs / (float)m_oCellRules.m_iPopMs, 0.0f, 1.0f);
                    l_oB.m_fCellsScale = m_oSettings.m_bReduceEffects ? 1.0f : std::max(0.01f, l_fK * l_fK);
                }
                if (m_oCs.m_iCellRows <= 2) l_oB.m_strCellHint = l_oL.f_Tr(toms::text::g_pszBattleCellHint);   // (a tall grid fills that place)
                // any grid: the width is shared out, the rows grow upward from the bottom of #cells and shrink past maxH
                const int l_iCols = m_oCs.m_iCellCols, l_iRows = m_oCs.m_iCellRows;
                const float l_fGap = m_oCellRules.m_fGap;
                const float l_fCw = (m_oCellRules.m_fAreaW - l_fGap * (float)(l_iCols - 1)) / (float)l_iCols;
                const float l_fCh = std::min(m_oCellRules.m_fRowH, (m_oCellRules.m_fMaxH - l_fGap * (float)(l_iRows - 1)) / (float)l_iRows);
                const float l_fTotal = l_fCh * (float)l_iRows + l_fGap * (float)(l_iRows - 1);
                const float l_fBase = m_oCellRules.m_fRowH * 2.0f + l_fGap - l_fTotal;   // (#cells is two default rows high)
                l_oB.m_fCellHintY = l_fBase - 24.0f;
                for (int l_iC = 0; l_iC < l_iCols * l_iRows; l_iC++) {
                    toms::UiBattleCell l_oCell;
                    // the floor in perspective: the near rows (toward the monster) narrower, each column leaning toward the middle
                    const int l_iCellRow = l_iC / l_iCols;
                    const float l_fStep = l_iRows > 1 ? m_oCellRules.m_fPerspective / (float)(l_iRows - 1) : 0.0f;
                    const float l_fRowK = 1.0f - l_fStep * (float)(l_iRows - 1 - l_iCellRow);
                    const float l_fFromMid = (float)(l_iC % l_iCols) - (float)(l_iCols - 1) * 0.5f;
                    const float l_fPitch = l_fCw + l_fGap;
                    l_oCell.m_fW = l_fCw * l_fRowK; l_oCell.m_fH = l_fCh;
                    l_oCell.m_fX = m_oCellRules.m_fAreaW * 0.5f + l_fFromMid * l_fPitch * l_fRowK - l_oCell.m_fW * 0.5f;
                    l_oCell.m_fY = l_fBase + (l_fCh + l_fGap) * (float)l_iCellRow;
                    l_oCell.m_fSkew = -std::atan2(l_fFromMid * l_fPitch * l_fStep, l_fCh + l_fGap) * 57.29578f;
                    l_oCell.m_bHere = l_iC == m_oCs.m_iHeroCell;
                    for (const CombatState::Foe& l_oF : m_oCs.m_vFoes)
                        if (!l_oF.f_Down() && (l_oF.m_uDanger & (1u << l_iC))) {
                            l_oCell.m_bDanger = true;
                            l_oCell.m_fFill = std::max(l_oCell.m_fFill, std::clamp(100.0f * (1.0f - (float)(l_oF.m_iMoveMs - l_oF.m_iClockMs) / (float)m_oCellRules.m_iWarnMs), 0.0f, 100.0f));
                        }
                    l_oB.m_vCells.push_back(l_oCell);
                }
            }
            l_oB.m_strSuperLabel = l_oL.f_Tr(toms::text::g_pszBattleSuperLabel);
            for (int l_iI = 0; l_iI < m_oRun.f_SuperMax(); l_iI++) l_oB.m_vSuperDots.push_back(l_iI < m_oCs.m_iSuperCharge ? 1 : 0);
            l_oB.m_bSuperReady = m_oCs.m_iSuperCharge >= m_oRun.f_SuperMax() && !m_oCs.m_oCombo.f_Running() && !m_oCs.m_oSkill.m_bOn && !m_oCs.m_bWinPending;
            l_oB.m_strSuperButton = l_oL.f_Tr(toms::text::g_pszBattleSuperReady);
            // the revamp (docs/gameplay/15): the attack is the play, its panel only says how it is; one guard button
            l_oB.m_bShowClock = m_oCs.m_ePace == toms::Pace::Stamina;
            l_oB.m_bGuardTiming = m_oGuard.m_strMode != toms::guard_mode::g_pszBar;   // the one GUARD button ("timing" and "warn"); "bar": the five zones
            l_oB.m_bGuardLit = m_oCs.m_iGuardAtMs >= 0 && m_oCs.m_iClockMs - m_oCs.m_iGuardAtMs <= m_oGuard.m_iBlockMs;
            l_oB.m_bGuardCooling = m_oCs.m_iGuardCdMs > 0 || l_oB.m_bGuardShort;
            if (m_oGuard.m_strMode == toms::guard_mode::g_pszWarn) {   // lit while an "!" shows: now (16 §4.2)
                l_oB.m_bGuardLit = false;
                for (size_t l_nI = 0; l_nI < m_oCs.m_vFoes.size(); l_nI++) l_oB.m_bGuardLit = l_oB.m_bGuardLit || f_FoeWarning((int)l_nI);
                l_oB.m_bGuardCooling = m_oCs.m_iGuardLockMs > 0 || l_oB.m_bGuardShort || m_oCs.m_oSkill.m_bOn;
            }
            l_oB.m_strGuardLabel = l_oL.f_Tr(toms::text::g_pszBattleGuardButton);
            l_oB.m_strGuardHint = l_oL.f_Tr(m_oGuard.m_strMode == toms::guard_mode::g_pszWarn ? toms::text::g_pszBattleGuardHintWarn : toms::text::g_pszBattleGuardHint);
            if (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase == CombatState::SkillRun::Phase::Play && m_oCs.m_oSkill.m_pDef) {   // a move's play (16 §6)
                const toms::SpecialMove& l_oD = *m_oCs.m_oSkill.m_pDef;
                l_oB.m_strSkillPrompt = l_oL.f_Has(l_oD.m_strPrompt) ? l_oL.f_Tr(l_oD.m_strPrompt) : l_oD.m_strPrompt;
                if (l_oD.m_oPlay.m_eType == toms::PlayType::eDRAW_SHAPE || l_oD.m_oPlay.m_eType == toms::PlayType::eMASH || l_oD.m_oPlay.m_eType == toms::PlayType::eSWIPE_CHAIN || l_oD.m_oPlay.m_eType == toms::PlayType::eSPIN_CIRCLE || toms::f_IsFreePlay(l_oD.m_oPlay.m_eType)) {
                    char l_pszT[16];
                    std::snprintf(l_pszT, sizeof l_pszT, "%.1f", m_oCs.m_oTouch.f_TimeLeft() * (float)l_oD.m_oPlay.m_iTimeMs / 1000.0f);
                    l_oB.m_strSkillTime = l_pszT;
                }
            }
            const toms::PlayType l_ePlayType = f_BattlePlayType();
            if (f_HeroDown()) {   // knocked down by a special attack: no attack until he is up
                l_oB.m_strAttackState = toms::ui::g_pszRecovering;
                l_oB.m_strAttackLabel = l_oL.f_Tr(toms::text::g_pszBattleAttackDown);
                const int l_iDownMs = std::max(1, f_FeelFx().value(toms::field::g_pszSpecialAttack, nlohmann::json::object()).value(toms::field::g_pszDownMs, 1600));
                l_oB.m_fAttackPct = 100.0f * (1.0f - (float)m_oCs.m_iHeroDownMs / (float)l_iDownMs);
            } else if (f_AttackRunning() || (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_iOwner < 0)) { l_oB.m_strAttackState = toms::ui::g_pszRunning; /* (the hero's move: its whole show) */ l_oB.m_strAttackLabel = l_oL.f_Tr(toms::text::g_pszBattleAttackRunning); }
            else if (m_oCs.m_ePace == toms::Pace::Cooldown && m_oCs.m_oAtkBar.m_bCooling) {
                l_oB.m_strAttackState = toms::ui::g_pszRecovering;
                l_oB.m_strAttackLabel = l_oL.f_Tr(toms::text::g_pszBattleAttackRecovering);
                l_oB.m_fAttackPct = 100.0f * (1.0f - (float)m_oCs.m_oAtkBar.m_iCooldownMs / (float)std::max(1, f_BarCooldownMs()));
            } else if (l_oB.m_bAttackShort) { l_oB.m_strAttackState = toms::ui::g_pszShort; l_oB.m_strAttackLabel = l_oL.f_Tr(toms::text::g_pszBattleNoStamina); }
            else {
                l_oB.m_strAttackState = toms::ui::g_pszReady;
                l_oB.m_strAttackLabel = l_oL.f_Tr(toms::text::g_pszBattleAttackReady);
                l_oB.m_strAttackHint = l_oL.f_Tr(l_ePlayType == toms::PlayType::eTRACK ? toms::text::g_pszBattleAttackReadyHint
                                     : std::string(toms::key_prefix::g_pszBattlePlayHint) + toms::f_PlayTypeName(l_ePlayType));
            }
            if (const toms::TutorialStep* l_pStep = f_TutorialStepNow()) {   // the new game's tutorial (docs/gameplay/19 B6)
                l_oB.m_strTutorial = f_TutorialText(*l_pStep);
                l_oB.m_strTutorialMark = l_pStep->m_strMark;
                l_oB.m_fTutorialScale = l_fnPop(m_oArena.m_iTutorialPopMs);
                // a step with hideMs: its box goes away that long after it popped up (the owner, 2026-11-13: the red cells'
                // hint "should auto disappear after 3 seconds"); its words stay set, so nothing else takes its place
                l_oB.m_bTutorialHidden = l_pStep->m_iHideMs > 0 && m_oArena.m_iTutorialPopMs >= l_pStep->m_iHideMs;
                if (l_pStep->m_strMark == toms::ui::g_pszCells && !f_BattleCellsVisible()) l_oB.m_strTutorial.clear();   // it comes with the cells
            }
            if (l_ePlayType != toms::PlayType::eTRACK || m_oCs.m_oSkill.m_bOn) f_TouchPlayUi(l_oB);
            if (m_oCs.m_iChain > 1) l_oB.m_strChainText = f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleWordChain), toms::field::g_pszN, std::to_string(m_oCs.m_iChain));
            auto l_vActiveIds = toms::f_EquippedActives(m_oEquipped, m_mapEquipmentDefs);
            if (!l_vActiveIds.empty() && toms::f_CanUseActive(l_vActiveIds[0], m_mapActiveDefs, m_oCs.m_mapActiveRuntime, m_oCs.m_oActiveStatus)) {
                l_oB.m_bActiveReady = true;   // (ActiveDefinition has no display name yet: the id is shown)
                l_oB.m_strActiveButton = f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleActiveReady), toms::field::g_pszName, l_vActiveIds[0]);
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
            l_oB.m_strResultTitle = l_oL.f_Tr(toms::text::g_pszBattleResultTitle);
            char l_pszSecs[16];
            std::snprintf(l_pszSecs, sizeof l_pszSecs, "%.1f", l_oR.m_fSeconds);
            l_oB.m_strResultStats = f_TrParam(f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultStats), toms::field::g_pszT, l_pszSecs), toms::field::g_pszC, std::to_string(m_oCs.m_iBestChain)),
                                     "p", std::to_string(m_oCs.m_iPerfects));
            const toms::WeaponPlay& l_oWp = f_WeaponPlayOf(l_oR.m_strWeapon);
            const std::string l_strName = m_mapEquipmentDefs.count(l_oR.m_strWeapon) ? f_ItemName(l_oR.m_strWeapon) : l_oL.f_Tr(toms::text::g_pszBattleUnarmed);
            l_oB.m_strResultWeapon = f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultWeapon), toms::field::g_pszName, l_strName), toms::field::g_pszLv, std::to_string(l_oR.m_iLvAfter));
            const int l_iNeed = toms::f_ExpToNextLevel(l_oWp, l_oR.m_iLvAfter);
            const int l_iDone = l_oR.m_iWeaponExp - toms::f_WeaponExpForLevel(l_oWp, l_oR.m_iLvAfter);
            if (l_iNeed > 0) {   // the EXP to the next level, and what that level adds (07 pillar 2: the reason to go on)
                l_oB.m_fResultPct = f_Pct((float)std::max(0, l_iDone), (float)l_iNeed);
                l_oB.m_strResultProgress = f_TrParam(f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultProgress), toms::field::g_pszN, std::to_string(std::max(0, l_iDone))), toms::field::g_pszM,
                                            std::to_string(l_iNeed)), "g", std::to_string(l_oR.m_iWeaponExpGained));
                const toms::WeaponLevel& l_oCur = toms::f_WeaponLevelEntry(l_oWp, l_oR.m_iLvAfter);
                const toms::WeaponLevel& l_oNxt = toms::f_WeaponLevelEntry(l_oWp, l_oR.m_iLvAfter + 1);
                std::string l_strWhat;
                if (l_oNxt.m_iPoints > l_oCur.m_iPoints) l_strWhat = f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultPoints), toms::field::g_pszN, std::to_string(l_oNxt.m_iPoints));
                for (const auto& [l_strStat, l_iV] : l_oNxt.m_mapStats) {
                    const int l_iWas = l_oCur.m_mapStats.count(l_strStat) ? l_oCur.m_mapStats.at(l_strStat) : 0;
                    if (l_iV > l_iWas) {
                        std::string l_strUp = l_strStat;
                        for (char& l_cCh : l_strUp) l_cCh = (char)std::toupper((unsigned char)l_cCh);
                        l_strWhat += (l_strWhat.empty() ? "" : ", ") + l_strUp + " +" + std::to_string(l_iV - l_iWas);
                    }
                }
                if (!l_strWhat.empty()) l_oB.m_strResultNext = f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultNext), toms::field::g_pszWhat, l_strWhat);
            } else {
                l_oB.m_fResultPct = 100.0f;
                l_oB.m_strResultProgress = l_oL.f_Tr(toms::text::g_pszBattleResultTop);
            }
            if (l_oR.m_iLvAfter > l_oR.m_iLvBefore)
                l_oB.m_strResultLevelup = f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultLevelup), toms::field::g_pszName, l_strName), toms::field::g_pszLv, std::to_string(l_oR.m_iLvAfter));
            l_oB.m_strResultGold = f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszBattleResultGold), toms::field::g_pszGold, std::to_string(l_oR.m_iGold)), toms::field::g_pszExp, std::to_string(l_oR.m_iExp));
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
            const std::string l_strUnit = l_oL.f_Tr(l_pDef->m_strCurrency == "crystals" ? toms::text::g_pszMinigameCrystals : toms::text::g_pszMinigameGold);
            l_oM.m_strPrompt = l_oM.m_bRunning ? l_oL.f_Tr(toms::text::g_pszBattleComboPromptRun)
                                 : f_TrParam(f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszMinigameRule), toms::field::g_pszN, std::to_string(l_pDef->m_iPerHit)), toms::field::g_pszUnit, l_strUnit),
                                           "bonus", std::to_string(l_pDef->m_iBonusAll));
            l_oM.m_strButtonLabel = l_oL.f_Tr(l_oM.m_bDone ? toms::text::g_pszMinigameClose : l_oM.m_bRunning ? toms::text::g_pszBattleComboTap : toms::text::g_pszMinigameStart);
            l_oM.m_strCloseLabel = l_oL.f_Tr(toms::text::g_pszMinigameClose);
            if (l_oM.m_bDone)
                l_oM.m_strResult = m_oMiniGame.m_iReward > 0
                    ? f_TrParam(f_TrParam(l_oL.f_Tr(toms::text::g_pszMinigameResult), toms::field::g_pszN, std::to_string(m_oMiniGame.m_iReward)), toms::field::g_pszUnit, l_strUnit)
                    : l_oL.f_Tr(toms::text::g_pszMinigameResultNone);
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
        if (m_jDlgData.contains(toms::field::g_pszNodes) && m_jDlgData[toms::field::g_pszNodes].contains(m_strDlgNode) && m_jDlgData[toms::field::g_pszNodes][m_strDlgNode].contains(toms::field::g_pszText))
            l_oD.m_strText = l_oL.f_Field(m_jDlgData[toms::field::g_pszNodes][m_strDlgNode][toms::field::g_pszText]);
        for (size_t l_nI = 0; l_nI < m_vDlgChoices.size(); l_nI++)
            l_oD.m_vChoices.push_back({m_vDlgChoices[l_nI].m_strLabel, "", (int)l_nI == m_iDlgSel, true});
    }

    // ---- store ----
    if (m_bStoreOpen) {
        toms::UiStore& l_oS = l_oU.m_oStore;
        l_oS.m_bVisible = true;
        l_oS.m_strTitle = l_oL.f_Field(m_jStoreTitle);
        l_oS.m_strHint = l_oL.f_Tr(toms::text::g_pszStoreControlsHint);
        l_oS.m_strGoldLabel = l_oL.f_Tr(toms::text::g_pszStoreGoldLabel);
        l_oS.m_strCloseLabel = l_oL.f_Tr(toms::text::g_pszInventoryClose);
        l_oS.m_strEffectLabel = l_oL.f_Tr(toms::text::g_pszStoreEffectLabel);
        l_oS.m_strPriceLabel = l_oL.f_Tr(toms::text::g_pszStorePriceLabel);
        l_oS.m_strBuyLabel = l_oL.f_Tr(toms::text::g_pszStoreBuyButton);
        l_oS.m_iGold = m_oPl.m_iGold;
        static const char* l_arrTabKeys[4] = {toms::text::g_pszStoreTabPotion, toms::text::g_pszStoreTabWeapon, toms::text::g_pszStoreTabArmor, toms::text::g_pszStoreTabTalent};
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
            l_oIt.m_strStatus = l_bGear ? l_oL.f_Tr(l_oIt.m_bEquipped ? toms::text::g_pszStoreEquipped : toms::text::g_pszStoreTapToEquip)
                             : l_oL.f_Tr(toms::text::g_pszStorePurchasedLabel) + " x" + std::to_string(l_oD.m_iPurchases);
            l_oIt.m_strPrice = l_bOwned ? "-" : std::to_string(l_oD.f_LiveCost());
            l_oIt.m_bSelected = (int)l_nI == m_iStoreSel;
            l_oS.m_vItems.push_back(l_oIt);
        }
    }
    if (!m_vNotices.empty()) {
        l_oU.m_oNotice.m_bVisible = true;
        l_oU.m_oNotice.m_strTitle = m_vNotices.front().m_strTitle;
        l_oU.m_oNotice.m_strBody = m_vNotices.front().m_strBody;
        l_oU.m_oNotice.m_strOk = l_oL.f_Tr(toms::text::g_pszNoticeOk);
    }
    if (m_bStoreUnlockDlg) {
        l_oU.m_oStore.m_bUnlockedDialog = true;
        l_oU.m_oStore.m_strUnlockedTitle = l_oL.f_Tr(toms::text::g_pszStoreUnlockedTitle);
        l_oU.m_oStore.m_strUnlockedBody = l_oL.f_Tr(toms::text::g_pszStoreUnlockedBody);
        l_oU.m_oStore.m_strUnlockedOk = l_oL.f_Tr(toms::text::g_pszStoreConfirmHint);
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
    l_oM.m_strYesLabel = l_oL.f_Tr(toms::text::g_pszMenuYes);
    l_oM.m_strNoLabel = l_oL.f_Tr(toms::text::g_pszMenuNo);
    l_oM.m_strCloseLabel = l_oL.f_Tr(toms::text::g_pszMenuBack);
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
                    l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszStoreIconLabel);
                    l_oR.m_bEnabled = m_bStoreUnlocked;
                    if (!m_bStoreUnlocked) l_oR.m_strSub = l_oL.f_Tr(toms::text::g_pszSystemLocked);
                    else if (m_bNewsStore) l_oR.m_strSub = "•";
                    break;
                case MainMenuRow::Save:        l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszSaveloadRow); break;
                case MainMenuRow::Settings:    l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszMenuSettings); break;
                case MainMenuRow::Skills:
                    l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszIngameMenuSkills);
                    if (m_oRun.f_SkillPoints() > 0) l_oR.m_strSub = f_TrParam(l_oL.f_Tr(toms::text::g_pszSkillPointsLabel), toms::field::g_pszN, std::to_string(m_oRun.f_SkillPoints()));
                    break;
                case MainMenuRow::Village:     l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszIngameMenuVillage); break;
                case MainMenuRow::Forge:       l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszIngameMenuForge); break;
                case MainMenuRow::Fullscreen:
                    l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszSystemFullscreen);
                    l_oR.m_strSub = l_oL.f_Tr(m_bFullscreenOn ? toms::text::g_pszSystemOn : toms::text::g_pszSystemOff);
                    break;
                case MainMenuRow::BackToTitle: l_oR.m_strLabel = l_oL.f_Tr(toms::text::g_pszIngameMenuBackToTitle); break;
            }
            l_oR.m_bSelected = (int)l_nI == l_iLit;
            l_oM.m_vMainRows.push_back(l_oR);
        }
    }
    switch (m_eInGameMenuPage) {
        case InGameMenuPage::Main: {
            l_oM.m_iPage = 0;
            l_oM.m_strCloseLabel = l_oL.f_Tr(toms::text::g_pszInventoryClose);
            break;
        }
        case InGameMenuPage::Settings: {
            l_oM.m_iPage = 1;
            l_oM.m_strHeader = l_oL.f_Tr(toms::text::g_pszSettingsHeader);
            const auto& l_vLangs = l_oL.f_Languages();
            for (size_t l_nI = 0; l_nI < l_vLangs.size(); l_nI++)
                l_oM.m_vRows.push_back({std::string((int)l_nI == l_oL.f_LanguageIndex() ? "[x] " : "[ ] ") + l_vLangs[l_nI].m_strName, "", l_fnSel(l_nI), true});
            if (!l_oM.m_vRows.empty()) l_oM.m_vRows[0].m_strSection = l_oL.f_Tr(toms::text::g_pszSettingsLanguage);   // section: language
            f_AppendArtStyleRows(l_oM.m_vRows, m_iInGameMenuSel - (int)l_vLangs.size());       // section: art style
            const size_t l_nCam = l_oM.m_vRows.size() - 1;                                  // section: other (from the next row)
            l_oM.m_vRows.push_back({l_oL.f_Tr(m_oSettings.m_bReduceEffects ? toms::text::g_pszSettingsReduceEffectsOn : toms::text::g_pszSettingsReduceEffectsOff),
                              "", l_fnSel(l_nCam + 1), true});   // docs/gameplay/15 §3.8
            l_oM.m_vRows[l_nCam + 1].m_strSection = l_oL.f_Tr(toms::text::g_pszSettingsOtherSection);
            {   // the battle speed (17 I10): activating the row takes the next step
                char l_pszSpeed[16];
                std::snprintf(l_pszSpeed, sizeof l_pszSpeed, "%g", m_oSettings.m_fBattleSpeed);
                l_oM.m_vRows.push_back({f_TrParam(l_oL.f_Tr(toms::text::g_pszSettingsBattleSpeed), toms::field::g_pszX, l_pszSpeed), "", l_fnSel(l_nCam + 2), true});
            }
            {   // the 3D battle (Q95): activating the row takes the next mode
                static const char* const l_arrKeys[3] = {toms::text::g_pszSettingsBattle3dFull, toms::text::g_pszSettingsBattle3dLight, toms::text::g_pszSettingsBattle3dOff};
                l_oM.m_vRows.push_back({l_oL.f_Tr(l_arrKeys[std::clamp(m_oSettings.m_iBattle3D, 0, 2)]), "", l_fnSel(l_nCam + 3), true});
            }
            {   // the view size (docs/gameplay/24): activating the row takes the next step
                char l_pszView[16];
                std::snprintf(l_pszView, sizeof l_pszView, "%g", m_oSettings.m_fViewScale);
                l_oM.m_vRows.push_back({f_TrParam(l_oL.f_Tr(toms::text::g_pszSettingsViewScale), toms::field::g_pszX, l_pszView), "", l_fnSel(l_nCam + 4), true});
            }
            l_oM.m_vRows.push_back({l_oL.f_Tr(toms::text::g_pszSettingsTestJump), "", l_fnSel(l_nCam + 5), true});   // test: any floor
            l_oM.m_vRows.push_back({l_oL.f_Tr(m_bPerfShown ? toms::text::g_pszSettingsPerfOn : toms::text::g_pszSettingsPerfOff), "",
                                    l_fnSel(l_nCam + 6), true});   // the performance checker (docs/engine/25)
            break;
        }
        case InGameMenuPage::Skills: {
            l_oM.m_iPage = 2;
            l_oM.m_strHeader = l_oL.f_Tr(toms::text::g_pszIngameMenuSkills);
            l_oM.m_strSubheader = f_TrParam(l_oL.f_Tr(toms::text::g_pszSkillPointsLabel), toms::field::g_pszN, std::to_string(m_oRun.f_SkillPoints()));
            auto l_vOrder = f_SkillMenuOrder();
            for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
                const auto& l_oDef = m_mapSkillDefs.at(l_vOrder[l_nI]);
                const bool l_bOwned = m_oRun.f_HasSkill(l_vOrder[l_nI]);
                const bool l_bUnlockable = !l_bOwned && toms::f_CanUnlockSkill(m_mapSkillDefs, m_oRun.f_SkillsOwned(), m_oRun.f_SkillPoints(), l_vOrder[l_nI]);
                const std::string l_strMark = l_bOwned ? "[x] " : (l_bUnlockable ? "[ ] " : "[--] ");
                const std::string l_strName = l_oL.f_Field(l_oDef.m_jName);
                l_oM.m_vRows.push_back({l_bOwned ? l_strMark + l_strName
                                        : f_TrParam(l_strMark + l_strName + " (" + l_oL.f_Tr(toms::text::g_pszSkillCostSuffix) + ")", toms::field::g_pszN, std::to_string(l_oDef.m_iCost)),
                                  "", l_fnSel(l_nI), l_bOwned || l_bUnlockable});
            }
            break;
        }
        case InGameMenuPage::Forge: {
            l_oM.m_iPage = 3;
            l_oM.m_strHeader = l_oL.f_Tr(toms::text::g_pszIngameMenuForge);
            l_oM.m_strSubheader = f_TrParam(l_oL.f_Tr(toms::text::g_pszForgeGoldLabel), toms::field::g_pszN, std::to_string(m_oPl.m_iGold));
            auto l_vOrder = f_ForgeMenuOrder();
            for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
                const auto& l_oDef = m_mapForgeDefs.at(l_vOrder[l_nI]);
                const bool l_bKnown = std::find(m_oMeta.m_vForgeRecipesKnown.begin(), m_oMeta.m_vForgeRecipesKnown.end(), l_vOrder[l_nI]) !=
                                   m_oMeta.m_vForgeRecipesKnown.end();
                const bool l_bCraftable = l_bKnown && toms::f_CanCraft(m_mapForgeDefs, m_oMeta.m_vForgeRecipesKnown, m_oPl.m_vInv, m_oPl.m_iGold, l_vOrder[l_nI]);
                std::string l_strLabel = std::string(!l_bKnown ? "[--] " : (l_bCraftable ? "[ ] " : "[!] ")) +
                                    (l_bKnown ? l_oL.f_Field(l_oDef.m_jName) : l_oL.f_Tr(toms::text::g_pszForgeUnknownRecipe));
                if (l_bKnown) l_strLabel += " (" + std::to_string(l_oDef.m_iCostGold) + "g)";
                l_oM.m_vRows.push_back({l_strLabel, "", l_fnSel(l_nI), l_bCraftable});
            }
            break;
        }
        case InGameMenuPage::Hub: {
            l_oM.m_iPage = 4;
            l_oM.m_strHeader = l_oL.f_Tr(toms::text::g_pszIngameMenuVillage);
            auto l_vOrder = f_HubMenuOrder();
            auto l_fnHasFlag = [this](const std::string& l_strF) { return m_oRun.f_Flag(l_strF); };
            for (size_t l_nI = 0; l_nI < l_vOrder.size(); l_nI++) {
                const bool l_bUnlocked = toms::f_HubLocationUnlocked(m_mapHubDefs, l_fnHasFlag, l_vOrder[l_nI]);
                l_oM.m_vRows.push_back({std::string(l_bUnlocked ? "[ ] " : "[--] ") +
                                  (l_bUnlocked ? l_oL.f_Field(m_mapHubDefs.at(l_vOrder[l_nI]).m_jName) : l_oL.f_Tr(toms::text::g_pszHubUnknownLocation)),
                                  "", l_fnSel(l_nI), l_bUnlocked});
            }
            break;
        }
    }
    if (m_bInGameLangConfirmOpen && m_bInGameConfirmIsStyle) {
        l_oM.m_bConfirmOpen = true;
        l_oM.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr(toms::text::g_pszSettingsStyleConfirm), toms::field::g_pszStyle, f_ArtStyleName(m_iInGameLangConfirmIdx));
        l_oM.m_strConfirmBody = l_oL.f_Tr(toms::text::g_pszSettingsStyleApplyTitle);   // a run never changes look under the player
        l_oM.m_bConfirmYes = m_bInGameLangConfirmYes;
    } else if (m_bInGameLangConfirmOpen) {
        const auto& l_vLangs = l_oL.f_Languages();
        l_oM.m_bConfirmOpen = true;
        l_oM.m_strConfirmQuestion = f_TrParam(l_oL.f_Tr(toms::text::g_pszSettingsLanguageConfirm), toms::field::g_pszLang,
                                     (m_iInGameLangConfirmIdx >= 0 && m_iInGameLangConfirmIdx < (int)l_vLangs.size())
                                     ? l_vLangs[m_iInGameLangConfirmIdx].m_strName : "");
        l_oM.m_bConfirmYes = m_bInGameLangConfirmYes;
    }
}

void Game::f_UiEvent(const std::string& l_strName, int l_iArg) {
    // ---- title ----
    if (l_strName == toms::ui::g_pszTitleRow)     { if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_ClickRow(l_iArg)); return; }
    if (l_strName == toms::ui::g_pszTitleHover)   { if (m_oTitle.f_IsOpen()) m_oTitle.f_HoverRow(l_iArg); return; }
    if (l_strName == toms::ui::g_pszTitleBack)    { if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_Cancel()); return; }
    if (l_strName == toms::ui::g_pszTitleConfirm) { if (m_oTitle.f_IsOpen()) f_HandleTitleAction(m_oTitle.f_AnswerConfirm(l_iArg != 0)); return; }
    // ---- ending ----
    if (l_strName == toms::ui::g_pszEndingRebirth) { if (f_EndingActive()) f_Rebirth(); return; }
    if (l_strName == toms::ui::g_pszEndingTitle)   { if (f_EndingActive()) f_DismissEndingScreen(); return; }
    if (l_strName == toms::ui::g_pszEndingDismiss) { if (f_EndingActive() && !f_RebirthOffered()) f_DismissEndingScreen(); return; }
    // ---- HUD buttons ----
    if (l_strName == toms::ui::g_pszHudMenu)      { if (!f_ModalActive()) { f_CancelWalk(); f_OpenPlayerMenu(); } return; }
    // ---- battle ----
    if (l_strName == toms::ui::g_pszBattleAttack)   { f_BattleTapAttack(); return; }
    if (l_strName == toms::ui::g_pszMinigameTap)    { f_MiniGameTap(); return; }
    if (l_strName == toms::ui::g_pszMinigameClose)  { f_CloseMiniGame(); return; }
    if (l_strName == toms::ui::g_pszBattleDefend)   { f_BattleTapDefense(); return; }
    if (l_strName == toms::ui::g_pszBattleSuper)    { f_BattleTapSuper(); return; }
    if (l_strName == toms::ui::g_pszBattleActive)   { f_BattleTapActive(); return; }
    if (l_strName == toms::ui::g_pszBattleTarget)   { f_BattleSetTarget(l_iArg); return; }
    if (l_strName == toms::ui::g_pszBattleCell)     { f_BattleMoveToCell(l_iArg); return; }
    if (l_strName == toms::ui::g_pszBattleContinue) { if (m_oCs.m_bWon) f_DismissVictory(); return; }
    // ---- dialogue ----
    if (l_strName == toms::ui::g_pszDlgChoose)  { if (m_bInDialogue && l_iArg >= 0 && l_iArg < (int)m_vDlgChoices.size()) { m_iDlgSel = l_iArg; f_ChooseDialogue(l_iArg); } return; }
    if (l_strName == toms::ui::g_pszDlgHover)   { if (m_bInDialogue && l_iArg >= 0 && l_iArg < (int)m_vDlgChoices.size()) m_iDlgSel = l_iArg; return; }
    if (l_strName == toms::ui::g_pszDlgAdvance) { if (m_bInDialogue) f_ChooseDialogue(m_iDlgSel); return; }
    // ---- the player menu (game_player_menu.cpp) ----
    if (l_strName.rfind(toms::ui::g_pszPm, 0) == 0) { f_PlayerMenuEvent(l_strName, l_iArg); return; }
    // ---- store ----
    if (l_strName == toms::ui::g_pszStoreTab) {
        if (m_bStoreOpen && l_iArg >= 0 && l_iArg < 4 && l_iArg != m_iStoreTab) { m_iStoreTab = l_iArg; m_iStoreSel = 0; m_oAudio.f_Play(toms::key::g_pszSfxConfirmClick); }
        return;
    }
    if (l_strName == toms::ui::g_pszStoreHover) { if (m_bStoreOpen && l_iArg >= 0 && l_iArg < (int)f_StoreTabIndices().size()) m_iStoreSel = l_iArg; return; }
    if (l_strName == toms::ui::g_pszStoreBuy) {
        const std::vector<int> l_vIdx = f_StoreTabIndices();
        if (m_bStoreOpen && l_iArg >= 0 && l_iArg < (int)l_vIdx.size()) { m_iStoreSel = l_iArg; f_BuyStoreItem(l_vIdx[l_iArg]); }
        return;
    }
    if (l_strName == toms::ui::g_pszStoreClose)       { if (m_bStoreOpen) f_CloseStore(); return; }
    if (l_strName == toms::ui::g_pszStoreUnlockedOk) { if (m_bStoreUnlockDlg) f_StoreKey(13); return; }
    // ---- in-game menu ----
    if (l_strName == toms::ui::g_pszMenuRow)     { if (m_bInGameMenuOpen && !m_bInGameLangConfirmOpen) { m_iInGameMenuSel = l_iArg; f_InGameMenuActivate(); } return; }
    if (l_strName == toms::ui::g_pszMenuHover)   { if (m_bInGameMenuOpen && !m_bInGameLangConfirmOpen) m_iInGameMenuSel = l_iArg; return; }
    if (l_strName == toms::ui::g_pszMenuClose)   { if (m_bInGameMenuOpen) f_InGameMenuBack(); return; }
    if (l_strName == toms::ui::g_pszMenuConfirm) { if (m_bInGameLangConfirmOpen) { m_bInGameLangConfirmYes = l_iArg != 0; f_InGameMenuActivate(); } return; }
    if (l_strName == toms::ui::g_pszNoticeOk) { f_DismissNotice(); return; }
    // ---- stairs ----
    if (l_strName == toms::ui::g_pszStairs) { if (m_bStairsConfirmOpen) { if (l_iArg) f_ConfirmStageTransition(); else f_CancelStageTransition(); } return; }
    // ---- stage select ----
    if (l_strName == toms::ui::g_pszStagePick) {
        if (!m_bStageSelectOpen || l_iArg < 0 || l_iArg >= (int)m_vStageList.size()) return;
        const StageInfo& l_oInfo = m_vStageList[l_iArg];
        const bool l_bLocked = !m_bStageSelectTest && l_oInfo.m_iIndex > 1 && l_iArg > 0 &&
            std::find(m_oMeta.m_vUnlockedStages.begin(), m_oMeta.m_vUnlockedStages.end(), m_vStageList[l_iArg - 1].m_strId) == m_oMeta.m_vUnlockedStages.end();
        if (l_bLocked) return;
        f_LoadStage(l_oInfo.m_strId);
        f_CloseStageSelect();
        return;
    }
    if (l_strName == toms::ui::g_pszStageClose) { if (m_bStageSelectOpen) f_CloseStageSelect(); return; }
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
        if (l_oM.m_bOk) l_fnAdd(toms::ui::g_pszMarkOk, l_oM.m_fX, l_oM.m_fY, l_fS, l_fS, 0.0f, l_fS * 0.5f, 1.0f - l_fK);
        else { l_fnAdd(toms::ui::g_pszMarkBad, l_oM.m_fX, l_oM.m_fY, 40, 7, 45.0f, 3.5f, 1.0f - l_fK); l_fnAdd(toms::ui::g_pszMarkBad, l_oM.m_fX, l_oM.m_fY, 40, 7, -45.0f, 3.5f, 1.0f - l_fK); }
    }
    if (!l_oP.f_Running() || (m_oCs.m_oSkill.m_bOn && m_oCs.m_oSkill.m_ePhase != CombatState::SkillRun::Phase::Play)) return;
    const std::string l_strCount = std::to_string(std::min(l_oP.f_Index() + 1, l_oP.f_Points())) + "/" + std::to_string(l_oP.f_Points());
    // one prompt, a way's arrow or a shape's outline, at `at`, `pop` times its size (the sword's, the recall's, the chain's)
    auto l_fnCue = [&](int l_iK, glm::vec2 l_v2At, float l_fPop, const std::string& l_strCls) {
        if (l_oP.f_CueKind(l_iK) == toms::gesture::g_pszWay) {
            const float l_fAng = (float)(l_oP.f_CueDir(l_iK) + (l_oP.f_CueReversed(l_iK) ? 4 : 0)) * 45.0f, l_fRad = l_fAng / 57.29578f;
            const glm::vec2 l_v2Dir(std::cos(l_fRad), std::sin(l_fRad));
            l_fnAdd(l_strCls, l_v2At.x - l_v2Dir.x * 8.0f * l_fPop, l_v2At.y - l_v2Dir.y * 8.0f * l_fPop, 56 * l_fPop, 12 * l_fPop, l_fAng, 6 * l_fPop);
            l_fnAdd(l_strCls + " head", l_v2At.x + l_v2Dir.x * 16.0f * l_fPop, l_v2At.y + l_v2Dir.y * 16.0f * l_fPop, 26 * l_fPop, 26 * l_fPop, l_fAng + 45.0f);
            return;
        }
        const std::string& l_strCue = l_oP.f_CueKind(l_iK);   // a shape to draw: its outline
        std::vector<glm::vec2> l_vC = toms::f_ShapeCorners(l_strCue);
        if (l_strCue == toms::gesture::g_pszV || l_strCue == toms::gesture::g_pszM) {   // a tick or an M can face any way (17 §4.2): each prompt turned its own way
            const float l_fTurn = (float)l_oP.f_CueDir(l_iK) * 45.0f / 57.29578f, l_fCos = std::cos(l_fTurn), l_fSin = std::sin(l_fTurn);
            for (glm::vec2& l_v2C : l_vC) {
                const glm::vec2 l_v2D = l_v2C - glm::vec2(0.5f);
                l_v2C = glm::vec2(0.5f) + glm::vec2(l_v2D.x * l_fCos - l_v2D.y * l_fSin, l_v2D.x * l_fSin + l_v2D.y * l_fCos);
            }
        }
        for (size_t l_nI = 1; l_nI < l_vC.size(); l_nI++)
            l_fnLine(l_strCls, l_v2At + (l_vC[l_nI - 1] - glm::vec2(0.5f)) * 70.0f * l_fPop, l_v2At + (l_vC[l_nI] - glm::vec2(0.5f)) * 70.0f * l_fPop, std::max(3.0f, 7.0f * l_fPop));
    };
    auto l_fnPopScale = [](int l_iAge) {   // a prompt pops in with a little bounce (16 §3.1)
        return l_iAge < 180 ? 0.5f + 0.7f * std::sin((float)l_iAge / 180.0f * 1.5708f) : l_iAge < 260 ? 1.2f - 0.2f * (float)(l_iAge - 180) / 80.0f : 1.0f;
    };
    switch (l_oP.f_Type()) {
    case toms::PlayType::ePULL_RELEASE: {
        const toms::TouchPlay::Target& l_oT = l_oP.f_Target();
        l_fnAdd(l_oP.f_Started() ? toms::ui::g_pszTarget : toms::ui::g_pszTargetIdle, l_oT.m_fX, l_oT.m_fY, 2 * l_oT.m_fR, 2 * l_oT.m_fR, 0.0f, l_oT.m_fR);
        l_fnAdd(toms::ui::g_pszBull, l_oT.m_fX, l_oT.m_fY, 0.8f * l_oT.m_fR, 0.8f * l_oT.m_fR, 0.0f, 0.4f * l_oT.m_fR);
        if (l_oP.f_Points() > 1) l_fnAdd(toms::ui::g_pszCount, l_oT.m_fX, l_oT.m_fY - l_oT.m_fR - 18.0f, 80, 24, 0.0f, 0.0f, 1.0f, l_strCount);
        if (l_oP.f_Started()) l_fnAdd(toms::ui::g_pszTime, l_oT.m_fX, l_oT.m_fY + l_oT.m_fR + 10.0f, 2 * l_oT.m_fR * l_oP.f_TimeLeft(), 5, 0.0f, 2.5f);
        if (l_oP.f_Aiming()) {
            const glm::vec2 l_v2C = l_oP.f_Crosshair(), l_v2A = l_oP.f_AimFrom();
            const float l_fCh = l_oP.f_Charge(), l_fS = 26.0f + 40.0f * l_fCh;
            l_fnLine(toms::ui::g_pszPull, l_v2A, l_oP.f_AimTo(), 5);
            l_fnLine(toms::ui::g_pszShot, l_v2A, l_v2C, 2);
            l_fnAdd(toms::ui::g_pszAnchor, l_v2A.x, l_v2A.y, 14, 14, 0.0f, 7);
            l_fnAdd(l_fCh >= 1.0f ? toms::ui::g_pszChargeFull : toms::ui::g_pszCharge, l_v2A.x, l_v2A.y, l_fS, l_fS, 0.0f, l_fS * 0.5f);   // the pull's charge (16 §3.2)
            l_fnAdd(toms::ui::g_pszCross, l_v2C.x, l_v2C.y, 28, 28, 0.0f, 14);
        } else if ((!m_oSettings.m_bBowTutorialDone || l_oB.m_strTutorialMark == toms::ui::g_pszAttack) && (l_oB.m_strTutorial.empty() || l_oB.m_strTutorialMark == toms::ui::g_pszAttack)
                   && !l_oP.f_Started() && !m_oCs.m_oSkill.m_bOn) {
            // the first pull's tutorial: a ghost finger touches under the target, drags back and lets go (16 §3.2)
            const float l_fK = (float)(m_oCs.m_iClockMs % 1600) / 1600.0f;
            const float l_fDrag = std::clamp((l_fK - 0.25f) / 0.45f, 0.0f, 1.0f);
            const glm::vec2 l_v2From(l_oT.m_fX, l_oT.m_fY + 90.0f), l_v2To(l_oT.m_fX, l_oT.m_fY + 150.0f);
            const glm::vec2 l_v2F = l_v2From + (l_v2To - l_v2From) * l_fDrag;
            if (l_fK < 0.85f) {
                if (l_fDrag > 0.0f) l_fnLine(toms::ui::g_pszPull, l_v2From, l_v2F, 4, 0.6f);
                l_fnAdd(toms::ui::g_pszGhost, l_v2F.x, l_v2F.y, 34, 34, 0.0f, 17, l_fK < 0.15f ? l_fK / 0.15f : 1.0f);
            }
            if (l_oB.m_strTutorial.empty()) {   // its words along the top of the attack's area, over the hero (not on his face)
                const glm::vec4 l_v4Band = f_TouchArea();
                l_fnAdd(toms::ui::g_pszTutorial, l_v4Band.x + l_v4Band.z * 0.5f, l_v4Band.y + 6.0f, 360, 28, 0.0f, 0.0f, 1.0f, m_oLocale.f_Tr(toms::text::g_pszBattlePlayHintPullRelease));
            }
        }
        break;
    }
    case toms::PlayType::eFREE_SWIPE:
    case toms::PlayType::eFREE_TAP:
    case toms::PlayType::eFREE_SHOT: {   // the ultimates (17 §5): the time left, how many so far, the swipe's trail
        const glm::vec4 l_v4A = f_TouchArea();
        const float l_fCx = l_v4A.x + l_v4A.z * 0.5f, l_fBase = l_v4A.y + l_v4A.w;
        l_fnAdd(toms::ui::g_pszCount, l_fCx, l_fBase - 24.0f, 120, 28, 0.0f, 0.0f, 1.0f, std::to_string(l_oP.f_Hits()));
        l_fnAdd(toms::ui::g_pszTimeOpen, l_fCx, l_fBase + 6.0f, 320.0f * l_oP.f_TimeLeft(), 8, 0.0f, 4.0f);
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine(toms::ui::g_pszSwipe, l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
        break;
    }
    case toms::PlayType::eSWIPE_WAY:
    case toms::PlayType::eSWIPE_GESTURE: {
        const toms::CueTrack& l_oCues = l_oP.f_Cues();
        const glm::vec2 l_v2At = l_oP.f_PromptAt();
        l_fnAdd(toms::ui::g_pszCount, l_v2At.x, l_v2At.y - 56.0f, 80, 24, 0.0f, 0.0f, 1.0f, l_strCount);
        const int l_iAge = l_oCues.f_CueAgeMs();
        if (!l_oCues.f_Done() && l_iAge >= 0) {   // the prompt pops in with a little bounce (16 §3.1)
            const int l_iK = l_oCues.f_Index();
            l_fnCue(l_iK, l_v2At, l_fnPopScale(l_iAge), l_oCues.f_Open() ? toms::ui::g_pszArrowNowOpen : toms::ui::g_pszArrowNow);
            l_fnAdd(l_oCues.f_Open() ? toms::ui::g_pszTimeOpen : toms::ui::g_pszTime, l_v2At.x, l_v2At.y + 46.0f, 160.0f * l_oCues.f_TimeLeft(), 6, 0.0f, 3.0f);
        }
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine(toms::ui::g_pszSwipe, l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
        break;
    }
    case toms::PlayType::eSWIPE_RECALL: {   // the memory blade (22 §2): shown one by one, "Go!", then swiped back from memory
        const glm::vec2 l_v2At = l_oP.f_PromptAt();
        const int l_iN = l_oP.f_Points();
        const auto l_ePhase = l_oP.f_RecallPhase();
        const int l_iShown = l_oP.f_RecallShowing();
        if (l_iShown >= 0) l_fnCue(l_iShown, l_v2At, l_fnPopScale(l_oP.f_RecallShownAgeMs()), l_oP.f_CueReversed(l_iShown) ? toms::ui::g_pszArrowNowRev : toms::ui::g_pszArrowNow);
        if (l_ePhase == toms::TouchPlay::RecallPhase::Ready) l_fnAdd(toms::ui::g_pszGo, l_v2At.x, l_v2At.y, 160, 44, 0.0f, 0.0f, 1.0f, m_oLocale.f_Tr(toms::text::g_pszBattleRecallGo));
        // a dot for each prompt: lit as each is shown; then done, the one due, those to come
        const float l_fGap = 22.0f, l_fX0 = l_v2At.x - l_fGap * (float)(l_iN - 1) * 0.5f;
        const int l_iSeen = l_ePhase == toms::TouchPlay::RecallPhase::Show ? std::max(l_iShown + 1, 0) : l_iN;
        for (int l_iK = 0; l_iK < l_iN; l_iK++) {
            std::string l_strCls = toms::ui::g_pszSlot;
            if (l_ePhase != toms::TouchPlay::RecallPhase::Answer) { if (l_iK < l_iSeen) l_strCls = toms::ui::g_pszSlotSeen; }
            else if (l_iK < l_oP.f_Index()) l_strCls = toms::ui::g_pszSlotDone;
            else if (l_iK == l_oP.f_Index()) l_strCls = toms::ui::g_pszSlotNow;
            l_fnAdd(l_strCls, l_fX0 + l_fGap * (float)l_iK, l_v2At.y - 52.0f, 14, 14, 0.0f, 7);
        }
        if (l_ePhase == toms::TouchPlay::RecallPhase::Answer) {
            l_fnAdd(toms::ui::g_pszCount, l_v2At.x, l_v2At.y, 80, 30, 0.0f, 0.0f, 1.0f, "?");
            l_fnAdd(toms::ui::g_pszTimeOpen, l_v2At.x, l_v2At.y + 46.0f, 200.0f * l_oP.f_TimeLeft(), 6, 0.0f, 3.0f);
        }
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine(toms::ui::g_pszSwipe, l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
        break;
    }
    case toms::PlayType::eSWIPE_CHAIN: {   // a skill's commands (22 §3): the row on a bar at the top centre, the one due big over the hero
        const float l_fW = m_pRen ? (float)m_pRen->f_Width() : 1024.0f;
        const int l_iN = l_oP.f_Points(), l_iNow = l_oP.f_Index();
        const float l_fCell = std::min(54.0f, (l_fW - 120.0f) / (float)std::max(1, l_iN)), l_fBarY = 150.0f;   // under the fight's title and the monsters' HP
        const float l_fX0 = l_fW * 0.5f - l_fCell * (float)(l_iN - 1) * 0.5f;
        l_fnAdd(toms::ui::g_pszChainbar, l_fW * 0.5f, l_fBarY, l_fCell * (float)l_iN + 16.0f, l_fCell + 12.0f, 0.0f, 10.0f);
        const auto& l_vMarks = l_oP.f_Marks();   // one mark per answer, in order: what each came to
        for (int l_iK = 0; l_iK < l_iN; l_iK++) {
            std::string l_strCls = toms::ui::g_pszArrow;
            if (l_iK < l_iNow) l_strCls = l_iK < (int)l_vMarks.size() && l_vMarks[(size_t)l_iK].m_bOk ? toms::ui::g_pszArrowCut : toms::ui::g_pszArrowMissed;
            else if (l_iK == l_iNow) l_strCls = toms::ui::g_pszArrowNowOpen;
            l_fnCue(l_iK, {l_fX0 + l_fCell * (float)l_iK, l_fBarY}, l_fCell / 90.0f * (l_iK == l_iNow ? 1.25f : 1.0f), l_strCls);
        }
        if (l_iNow < l_iN)   // the one due, big over the hero's head as every attack command is (the owner, 2026-11-13), popping in as it comes due
            l_fnCue(l_iNow, l_oP.f_PromptAt(), 1.3f * l_fnPopScale(l_vMarks.empty() ? l_oP.f_ClockMs() : l_vMarks.back().m_iAgeMs), toms::ui::g_pszArrowNowOpen);
        l_fnAdd(toms::ui::g_pszCount, l_fW * 0.5f + l_fCell * (float)l_iN * 0.5f + 48.0f, l_fBarY, 80, 28, 0.0f, 0.0f, 1.0f, std::to_string(std::min(l_iNow, l_iN)) + "/" + std::to_string(l_iN));
        l_fnAdd(toms::ui::g_pszTimeOpen, l_fW * 0.5f, l_fBarY + l_fCell * 0.5f + 16.0f, l_fCell * (float)l_iN * l_oP.f_TimeLeft(), 6, 0.0f, 3.0f);
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine(toms::ui::g_pszSwipe, l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
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
                l_fnAdd(l_bLit ? toms::ui::g_pszCircle : toms::ui::g_pszCircleIdle, l_oC.m_fX, l_oC.m_fY, 2 * l_fR, 2 * l_fR, 0.0f, l_fR, l_bLit ? 1.0f : 0.35f, l_bLit ? std::to_string(l_nK + 1) : std::string());
                continue;
            }
            if (l_ePhase == toms::TouchPlay::CirclePhase::Tap && (int)l_nK >= l_oP.f_CirclesShown()) continue;   // not back yet (20 §2)
            // back one by one with their numbers, which go once all are back (20 §2)
            l_fnAdd(toms::ui::g_pszCircle, l_oC.m_fX, l_oC.m_fY, 2 * l_fR, 2 * l_fR, 0.0f, l_fR, 1.0f, l_oP.f_CircleNumbersShown() ? std::to_string(l_nK + 1) : std::string());
            if (l_ePhase == toms::TouchPlay::CirclePhase::Warn) l_fnAdd(toms::ui::g_pszWarn, l_oC.m_fX, l_oC.m_fY - l_fR - 18.0f, 30, 34, 0.0f, 0.0f, 1.0f, "!");
        }
        if (l_ePhase == toms::TouchPlay::CirclePhase::Tap) {
            const glm::vec4 l_v4A = f_TouchArea();
            l_fnAdd(toms::ui::g_pszTime, l_v4A.x + l_v4A.z * 0.5f, l_v4A.y + l_v4A.w + 6.0f, 200.0f * l_oP.f_TimeLeft(), 6, 0.0f, 3.0f);
        }
        break;
    }
    case toms::PlayType::eDRAW_SHAPE: {   // a move's shape: its lines, the corners reached, the next one (16 §6.1)
        const toms::ShapeTrace& l_oTr = l_oP.f_Trace();
        const auto& l_vPts = l_oTr.f_Points();
        for (size_t l_nI = 1; l_nI < l_vPts.size(); l_nI++) l_fnLine((int)l_nI < l_oTr.f_Reached() ? toms::ui::g_pszStarDone : toms::ui::g_pszStar, l_vPts[l_nI - 1], l_vPts[l_nI], 6);
        const bool l_bClosed = l_vPts.size() > 1 && glm::length(l_vPts.front() - l_vPts.back()) < 0.5f;   // its end is its start
        for (size_t l_nI = 0; l_nI < l_vPts.size() - (l_bClosed ? 1 : 0); l_nI++) {
            const bool l_bDone = (int)l_nI < l_oTr.f_Reached(), l_bNext = (int)l_nI == l_oTr.f_Reached();
            l_fnAdd(l_bDone ? toms::ui::g_pszCornerDone : l_bNext ? toms::ui::g_pszCornerNext : toms::ui::g_pszCorner, l_vPts[l_nI].x, l_vPts[l_nI].y, 34, 34, 0.0f, 17);
        }
        if (l_oTr.f_Drawing()) l_fnAdd(toms::ui::g_pszCross, l_oTr.f_Last().x, l_oTr.f_Last().y, 24, 24, 0.0f, 12);
        break;
    }
    case toms::PlayType::eSPIN_CIRCLE: {   // the bow's volley (22 §4): circles round the ring, the way the label and the sparks go
        const glm::vec2 l_v2C = l_oP.f_SpinCentre();
        const float l_fR = l_oP.f_SpinRadius();
        const int l_iN = l_oP.f_Points(), l_iNow = std::min(l_oP.f_Index(), l_iN - 1);
        const bool l_bCw = l_oP.f_SpinClockwise(l_iNow);
        l_fnAdd(toms::ui::g_pszSpinring, l_v2C.x, l_v2C.y, 2.0f * l_fR, 2.0f * l_fR, 0.0f, l_fR);
        // the turn so far: ticks lit round the ring from the top, the way it goes
        const int l_iTicks = 24, l_iLit = (int)std::floor(l_oP.f_SpinProgress() * (float)l_iTicks + 0.001f);
        for (int l_iK = 0; l_iK < l_iTicks; l_iK++) {
            const float l_fRad = (-90.0f + (l_bCw ? 1.0f : -1.0f) * 360.0f * (float)l_iK / (float)l_iTicks) / 57.29578f;
            l_fnAdd(l_iK < l_iLit ? toms::ui::g_pszSpintickOn : toms::ui::g_pszSpintick, l_v2C.x + std::cos(l_fRad) * l_fR, l_v2C.y + std::sin(l_fRad) * l_fR, 10, 10, 0.0f, 5);
        }
        // the way and how many of it: "clockwise 2/3"; the turns of the same way in a row are counted together
        int l_iRunFrom = l_iNow, l_iRunTo = l_iNow;
        while (l_iRunFrom > 0 && l_oP.f_SpinClockwise(l_iRunFrom - 1) == l_bCw) l_iRunFrom--;
        while (l_iRunTo + 1 < l_iN && l_oP.f_SpinClockwise(l_iRunTo + 1) == l_bCw) l_iRunTo++;
        const std::string l_strWay = m_oLocale.f_Tr(l_bCw ? toms::text::g_pszBattleSpinCw : toms::text::g_pszBattleSpinCcw);
        l_fnAdd(l_bCw ? toms::ui::g_pszSpinlabel : toms::ui::g_pszSpinlabelCcw, l_v2C.x, l_v2C.y - 14.0f, 2.0f * l_fR, 30, 0.0f, 0.0f, 1.0f, l_strWay);
        l_fnAdd(toms::ui::g_pszCount, l_v2C.x, l_v2C.y + 18.0f, 90, 30, 0.0f, 0.0f, 1.0f,
                std::to_string(std::min(l_oP.f_Index(), l_iRunTo + 1) - l_iRunFrom + (l_oP.f_Index() <= l_iRunTo ? 1 : 0)) + "/" + std::to_string(l_iRunTo - l_iRunFrom + 1));
        // a dot per turn under the ring: done, missed, due, to come (the ones the other way in their own colour)
        const auto& l_vMarks = l_oP.f_Marks();
        const float l_fGap = 22.0f, l_fX0 = l_v2C.x - l_fGap * (float)(l_iN - 1) * 0.5f;
        for (int l_iK = 0; l_iK < l_iN; l_iK++) {
            std::string l_strCls = l_oP.f_SpinClockwise(l_iK) ? toms::ui::g_pszSlot : toms::ui::g_pszSlotSeen;
            if (l_iK < l_oP.f_Index()) l_strCls = l_iK < (int)l_vMarks.size() && l_vMarks[(size_t)l_iK].m_bOk ? toms::ui::g_pszSlotDone : toms::ui::g_pszSlotMissed;
            else if (l_iK == l_oP.f_Index()) l_strCls = toms::ui::g_pszSlotNow;
            l_fnAdd(l_strCls, l_fX0 + l_fGap * (float)l_iK, l_v2C.y + l_fR + 22.0f, 14, 14, 0.0f, 7);
        }
        l_fnAdd(toms::ui::g_pszTimeOpen, l_v2C.x, l_v2C.y + l_fR + 42.0f, 2.0f * l_fR * l_oP.f_TimeLeft(), 6, 0.0f, 3.0f);
        if (l_oP.f_Swiping())
            for (size_t l_nI = 1; l_nI < l_oP.f_Stroke().size(); l_nI++) l_fnLine(toms::ui::g_pszSwipe, l_oP.f_Stroke()[l_nI - 1], l_oP.f_Stroke()[l_nI], 8);
        break;
    }
    case toms::PlayType::eMASH: {
        const glm::vec4 l_v4A = f_TouchArea();
        const float l_fPulse = 1.0f + 0.08f * std::sin((float)l_oP.f_ClockMs() * 0.025f);
        l_fnAdd(toms::ui::g_pszCircle, l_v4A.x + l_v4A.z * 0.5f, l_v4A.y + l_v4A.w * 0.5f, 110 * l_fPulse, 110 * l_fPulse, 0.0f, 55 * l_fPulse, 1.0f, l_strCount);
        break;
    }
    default:
        break;
    }
}

// A tutorial step's words. "{play}": how this weapon attacks: the tutorial's own fuller words for it ("tutorial.play_" + the
// play; the owner, 2026-11-27: the staff explained), else the battle's short hint.
std::string Game::f_TutorialText(const toms::TutorialStep& l_oStep) const {
    const toms::Locale& l_oL = m_oLocale;
    const toms::PlayType l_ePlayType = f_BattlePlayType();
    const std::string l_strOwn = std::string(toms::key_prefix::g_pszTutorialPlay) + toms::f_PlayTypeName(l_ePlayType);
    const std::string l_strHow = l_oL.f_Has(l_strOwn) ? l_oL.f_Tr(l_strOwn)
        : l_oL.f_Tr(l_ePlayType == toms::PlayType::eTRACK ? toms::text::g_pszBattleAttackReadyHint
                                                          : std::string(toms::key_prefix::g_pszBattlePlayHint) + toms::f_PlayTypeName(l_ePlayType));
    return f_TrParam(l_oL.f_Tr(l_oStep.m_strText), toms::field::g_pszPlay, l_strHow);
}

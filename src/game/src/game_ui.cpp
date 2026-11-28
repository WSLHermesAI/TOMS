// game_ui.cpp -- see game_ui.h.
#include "perf.h"   // toms::perf: the performance checker (F4)
#include "ui_names.h"     // toms::ui: the names the .rml / .rcss files use
#include "field_keys.h"    // toms::field: every data field by its constant
#include "game_ui.h"

#include "game.h"
#include "rml_ui.h"

#include <RmlUi/Core.h>

#include <cstring>
#include <filesystem>

namespace toms::next {

namespace {

// Which document shows when, by its key in assets/media/resources.json (res_keys.h). Order = load order; the stacking
// itself is each document's `z-index` in its .rcss (the overlay document is always on top).
struct DocSpec { const char* m_pszKey; bool (*m_fnVisible)(const toms::UiState&); };
const DocSpec g_arrDocs[] = {
    {toms::key::g_pszUiHud,          [](const toms::UiState& l_oS) { return l_oS.m_oHud.m_bVisible; }},
    {toms::key::g_pszUiBattle,       [](const toms::UiState& l_oS) { return l_oS.m_oBattle.m_bVisible; }},
    {toms::key::g_pszUiDialogue,     [](const toms::UiState& l_oS) { return l_oS.m_oDialogue.m_bVisible; }},
    {toms::key::g_pszUiStore,        [](const toms::UiState& l_oS) { return l_oS.m_oStore.m_bVisible; }},
    {toms::key::g_pszUiStageSelect,  [](const toms::UiState& l_oS) { return l_oS.m_oStageSelect.m_bVisible; }},
    {toms::key::g_pszUiMain,         [](const toms::UiState& l_oS) { return l_oS.m_oMain.m_bVisible; }},
    {toms::key::g_pszUiPlayer,       [](const toms::UiState& l_oS) { return l_oS.m_oPlayer.m_bVisible; }},
    {toms::key::g_pszUiSaveload,     [](const toms::UiState& l_oS) { return l_oS.m_oSaveload.m_bVisible; }},
    {toms::key::g_pszUiDialogs,      [](const toms::UiState& l_oS) { return l_oS.m_oStairs.m_bVisible || l_oS.m_oStore.m_bUnlockedDialog || l_oS.m_oNotice.m_bVisible; }},
    {toms::key::g_pszUiTitle,        [](const toms::UiState& l_oS) { return l_oS.m_oTitle.m_bVisible; }},
    {toms::key::g_pszUiEnding,       [](const toms::UiState& l_oS) { return l_oS.m_oEnding.m_bVisible; }},
    {toms::key::g_pszUiMinigame,     [](const toms::UiState& l_oS) { return l_oS.m_oMinigame.m_bVisible; }},
    {toms::key::g_pszUiOverlay,      [](const toms::UiState&)   { return true; }},
};

// Registers every UiState type with the data model. Member names are the ones the .rml files use.
void f_RegisterTypes(Rml::DataModelConstructor& l_oC) {
    using namespace toms;
    l_oC.RegisterArray<std::vector<std::string>>();
    l_oC.RegisterArray<std::vector<int>>();
    if (auto l_oS = l_oC.RegisterStruct<UiRow>()) {
        l_oS.RegisterMember(toms::ui::g_pszLabel, &UiRow::m_strLabel);
        l_oS.RegisterMember(toms::ui::g_pszSub, &UiRow::m_strSub);
        l_oS.RegisterMember(toms::ui::g_pszSelected, &UiRow::m_bSelected);
        l_oS.RegisterMember(toms::ui::g_pszEnabled, &UiRow::m_bEnabled);
        l_oS.RegisterMember(toms::ui::g_pszSection, &UiRow::m_strSection);
        l_oS.RegisterMember(toms::ui::g_pszExtra, &UiRow::m_bExtra);
    }
    l_oC.RegisterArray<std::vector<UiRow>>();
    if (auto l_oS = l_oC.RegisterStruct<UiBanner>()) {
        l_oS.RegisterMember(toms::ui::g_pszText, &UiBanner::m_strText);
        l_oS.RegisterMember(toms::ui::g_pszX, &UiBanner::m_fX);
        l_oS.RegisterMember(toms::ui::g_pszY, &UiBanner::m_fY);
    }
    l_oC.RegisterArray<std::vector<UiBanner>>();
    if (auto l_oS = l_oC.RegisterStruct<UiTitle>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiTitle::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszScene, &UiTitle::m_bScene);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiTitle::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszSubtitle, &UiTitle::m_strSubtitle);
        l_oS.RegisterMember(toms::ui::g_pszVersion, &UiTitle::m_strVersion);
        l_oS.RegisterMember(toms::ui::g_pszHint, &UiTitle::m_strHint);
        l_oS.RegisterMember(toms::ui::g_pszPage, &UiTitle::m_iPage);
        l_oS.RegisterMember(toms::ui::g_pszHeader, &UiTitle::m_strHeader);
        l_oS.RegisterMember(toms::ui::g_pszSubheader, &UiTitle::m_strSubheader);
        l_oS.RegisterMember(toms::ui::g_pszRows, &UiTitle::m_vRows);
        l_oS.RegisterMember(toms::ui::g_pszShowBack, &UiTitle::m_bShowBack);
        l_oS.RegisterMember(toms::ui::g_pszBackLabel, &UiTitle::m_strBackLabel);
        l_oS.RegisterMember(toms::ui::g_pszShowEmptyHint, &UiTitle::m_bShowEmptyHint);
        l_oS.RegisterMember(toms::ui::g_pszEmptyHint, &UiTitle::m_strEmptyHint);
        l_oS.RegisterMember(toms::ui::g_pszConfirmOpen, &UiTitle::m_bConfirmOpen);
        l_oS.RegisterMember(toms::ui::g_pszConfirmQuestion, &UiTitle::m_strConfirmQuestion);
        l_oS.RegisterMember(toms::ui::g_pszConfirmBody, &UiTitle::m_strConfirmBody);
        l_oS.RegisterMember(toms::ui::g_pszYesLabel, &UiTitle::m_strYesLabel);
        l_oS.RegisterMember(toms::ui::g_pszNoLabel, &UiTitle::m_strNoLabel);
        l_oS.RegisterMember(toms::ui::g_pszConfirmYes, &UiTitle::m_bConfirmYes);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiHud>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiHud::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTitleLine, &UiHud::m_strTitleLine);
        l_oS.RegisterMember(toms::ui::g_pszHpPct, &UiHud::m_fHpPct);
        l_oS.RegisterMember(toms::ui::g_pszHpText, &UiHud::m_strHpText);
        l_oS.RegisterMember(toms::ui::g_pszStatsLine, &UiHud::m_strStatsLine);
        l_oS.RegisterMember(toms::ui::g_pszResLine, &UiHud::m_strResLine);
        l_oS.RegisterMember(toms::ui::g_pszFooter, &UiHud::m_strFooter);
        l_oS.RegisterMember(toms::ui::g_pszMenuLabel, &UiHud::m_strMenuLabel);
        l_oS.RegisterMember(toms::ui::g_pszMenuNews, &UiHud::m_bMenuNews);
        l_oS.RegisterMember(toms::ui::g_pszChapterCard, &UiHud::m_bChapterCard);
        l_oS.RegisterMember(toms::ui::g_pszChapterTitle, &UiHud::m_strChapterTitle);
        l_oS.RegisterMember(toms::ui::g_pszChapterSub, &UiHud::m_strChapterSub);
        l_oS.RegisterMember(toms::ui::g_pszChapterAlpha, &UiHud::m_fChapterAlpha);
        l_oS.RegisterMember(toms::ui::g_pszBanners, &UiHud::m_vBanners);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiPowerBar>()) {
        l_oS.RegisterMember(toms::ui::g_pszPrompt, &UiPowerBar::m_strPrompt);
        l_oS.RegisterMember(toms::ui::g_pszButtonLabel, &UiPowerBar::m_strButtonLabel);
        l_oS.RegisterMember(toms::ui::g_pszCooling, &UiPowerBar::m_bCooling);
        l_oS.RegisterMember(toms::ui::g_pszRedL, &UiPowerBar::m_fRedL);
        l_oS.RegisterMember(toms::ui::g_pszBlueL, &UiPowerBar::m_fBlueL);
        l_oS.RegisterMember(toms::ui::g_pszGreen, &UiPowerBar::m_fGreen);
        l_oS.RegisterMember(toms::ui::g_pszBlueR, &UiPowerBar::m_fBlueR);
        l_oS.RegisterMember(toms::ui::g_pszRedR, &UiPowerBar::m_fRedR);
        l_oS.RegisterMember(toms::ui::g_pszMarker, &UiPowerBar::m_fMarker);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiComboPoint>()) {
        l_oS.RegisterMember(toms::ui::g_pszPos, &UiComboPoint::m_fPos);
        l_oS.RegisterMember(toms::ui::g_pszHit, &UiComboPoint::m_bHit);
        l_oS.RegisterMember(toms::ui::g_pszNear, &UiComboPoint::m_bNear);
    }
    l_oC.RegisterArray<std::vector<UiComboPoint>>();
    if (auto l_oS = l_oC.RegisterStruct<UiCombo>()) {
        l_oS.RegisterMember(toms::ui::g_pszPrompt, &UiCombo::m_strPrompt);
        l_oS.RegisterMember(toms::ui::g_pszButtonLabel, &UiCombo::m_strButtonLabel);
        l_oS.RegisterMember(toms::ui::g_pszLevelText, &UiCombo::m_strLevelText);
        l_oS.RegisterMember(toms::ui::g_pszRunning, &UiCombo::m_bRunning);
        l_oS.RegisterMember(toms::ui::g_pszCooling, &UiCombo::m_bCooling);
        l_oS.RegisterMember(toms::ui::g_pszWaiting, &UiCombo::m_bWaiting);
        l_oS.RegisterMember(toms::ui::g_pszChargePct, &UiCombo::m_fChargePct);
        l_oS.RegisterMember(toms::ui::g_pszMarker, &UiCombo::m_fMarker);
        l_oS.RegisterMember(toms::ui::g_pszPoints, &UiCombo::m_vPoints);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiMiniGame>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiMiniGame::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiMiniGame::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszText, &UiMiniGame::m_strText);
        l_oS.RegisterMember(toms::ui::g_pszPrompt, &UiMiniGame::m_strPrompt);
        l_oS.RegisterMember(toms::ui::g_pszButtonLabel, &UiMiniGame::m_strButtonLabel);
        l_oS.RegisterMember(toms::ui::g_pszResult, &UiMiniGame::m_strResult);
        l_oS.RegisterMember(toms::ui::g_pszCloseLabel, &UiMiniGame::m_strCloseLabel);
        l_oS.RegisterMember(toms::ui::g_pszRunning, &UiMiniGame::m_bRunning);
        l_oS.RegisterMember(toms::ui::g_pszDone, &UiMiniGame::m_bDone);
        l_oS.RegisterMember(toms::ui::g_pszMarker, &UiMiniGame::m_fMarker);
        l_oS.RegisterMember(toms::ui::g_pszPoints, &UiMiniGame::m_vPoints);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiPopup>()) {
        l_oS.RegisterMember(toms::ui::g_pszText, &UiPopup::m_strText);
        l_oS.RegisterMember(toms::ui::g_pszX, &UiPopup::m_fX);
        l_oS.RegisterMember(toms::ui::g_pszY, &UiPopup::m_fY);
        l_oS.RegisterMember(toms::ui::g_pszOpacity, &UiPopup::m_fOpacity);
        l_oS.RegisterMember(toms::ui::g_pszStyle, &UiPopup::m_strStyle);
    }
    l_oC.RegisterArray<std::vector<UiPopup>>();
    if (auto l_oS = l_oC.RegisterStruct<UiGauntletStep>()) {
        l_oS.RegisterMember(toms::ui::g_pszSprite, &UiGauntletStep::m_strSprite);
        l_oS.RegisterMember(toms::ui::g_pszName, &UiGauntletStep::m_strName);
        l_oS.RegisterMember(toms::ui::g_pszState, &UiGauntletStep::m_strState);
        l_oS.RegisterMember(toms::ui::g_pszBoss, &UiGauntletStep::m_bBoss);
    }
    l_oC.RegisterArray<std::vector<UiGauntletStep>>();
    if (auto l_oS = l_oC.RegisterStruct<UiBattleCell>()) {
        l_oS.RegisterMember(toms::ui::g_pszX, &UiBattleCell::m_fX);
        l_oS.RegisterMember(toms::ui::g_pszY, &UiBattleCell::m_fY);
        l_oS.RegisterMember(toms::ui::g_pszW, &UiBattleCell::m_fW);
        l_oS.RegisterMember(toms::ui::g_pszH, &UiBattleCell::m_fH);
        l_oS.RegisterMember(toms::ui::g_pszSkew, &UiBattleCell::m_fSkew);
        l_oS.RegisterMember(toms::ui::g_pszHere, &UiBattleCell::m_bHere);
        l_oS.RegisterMember(toms::ui::g_pszDanger, &UiBattleCell::m_bDanger);
        l_oS.RegisterMember(toms::ui::g_pszFill, &UiBattleCell::m_fFill);
    }
    l_oC.RegisterArray<std::vector<UiBattleCell>>();
    if (auto l_oS = l_oC.RegisterStruct<UiTouchItem>()) {
        l_oS.RegisterMember(toms::ui::g_pszCls, &UiTouchItem::m_strCls);
        l_oS.RegisterMember(toms::ui::g_pszX, &UiTouchItem::m_fX);
        l_oS.RegisterMember(toms::ui::g_pszY, &UiTouchItem::m_fY);
        l_oS.RegisterMember(toms::ui::g_pszW, &UiTouchItem::m_fW);
        l_oS.RegisterMember(toms::ui::g_pszH, &UiTouchItem::m_fH);
        l_oS.RegisterMember(toms::ui::g_pszRot, &UiTouchItem::m_fRot);
        l_oS.RegisterMember(toms::ui::g_pszRadius, &UiTouchItem::m_fRadius);
        l_oS.RegisterMember(toms::ui::g_pszOpacity, &UiTouchItem::m_fOpacity);
        l_oS.RegisterMember(toms::ui::g_pszText, &UiTouchItem::m_strText);
    }
    l_oC.RegisterArray<std::vector<UiTouchItem>>();
    if (auto l_oS = l_oC.RegisterStruct<UiFoe>()) {
        l_oS.RegisterMember(toms::ui::g_pszSprite, &UiFoe::m_strSprite);
        l_oS.RegisterMember(toms::ui::g_pszAnim, &UiFoe::m_bAnim);
        l_oS.RegisterMember(toms::ui::g_pszHpPct, &UiFoe::m_fHpPct);
        l_oS.RegisterMember(toms::ui::g_pszHpText, &UiFoe::m_strHpText);
        l_oS.RegisterMember(toms::ui::g_pszName, &UiFoe::m_strName);
        l_oS.RegisterMember(toms::ui::g_pszHpLine, &UiFoe::m_strHpLine);
        l_oS.RegisterMember(toms::ui::g_pszTarget, &UiFoe::m_bTarget);
        l_oS.RegisterMember(toms::ui::g_pszDown, &UiFoe::m_bDown);
        l_oS.RegisterMember(toms::ui::g_pszIntent, &UiFoe::m_strIntent);
        l_oS.RegisterMember(toms::ui::g_pszIntentKind, &UiFoe::m_strIntentKind);
        l_oS.RegisterMember(toms::ui::g_pszIntentPct, &UiFoe::m_fIntentPct);
        l_oS.RegisterMember(toms::ui::g_pszBreakPct, &UiFoe::m_fBreakPct);
        l_oS.RegisterMember(toms::ui::g_pszMpOn, &UiFoe::m_bMpOn);
        l_oS.RegisterMember(toms::ui::g_pszMpPct, &UiFoe::m_fMpPct);
        l_oS.RegisterMember(toms::ui::g_pszMpReady, &UiFoe::m_bMpReady);
        l_oS.RegisterMember(toms::ui::g_pszWarn, &UiFoe::m_bWarn);
        l_oS.RegisterMember(toms::ui::g_pszHeadX, &UiFoe::m_fHeadX);
        l_oS.RegisterMember(toms::ui::g_pszHeadY, &UiFoe::m_fHeadY);
    }
    l_oC.RegisterArray<std::vector<UiFoe>>();
    if (auto l_oS = l_oC.RegisterStruct<UiBattle>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiBattle::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszActive, &UiBattle::m_bActive);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiBattle::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszPlayerSprite, &UiBattle::m_strPlayerSprite);
        l_oS.RegisterMember(toms::ui::g_pszPlayerAnim, &UiBattle::m_bPlayerAnim);
        l_oS.RegisterMember(toms::ui::g_pszEnemyAnim, &UiBattle::m_bEnemyAnim);
        l_oS.RegisterMember(toms::ui::g_pszEnemySprite, &UiBattle::m_strEnemySprite);
        l_oS.RegisterMember(toms::ui::g_pszPlayerHpPct, &UiBattle::m_fPlayerHpPct);
        l_oS.RegisterMember(toms::ui::g_pszEnemyHpPct, &UiBattle::m_fEnemyHpPct);
        l_oS.RegisterMember(toms::ui::g_pszPlayerHpText, &UiBattle::m_strPlayerHpText);
        l_oS.RegisterMember(toms::ui::g_pszEnemyHpText, &UiBattle::m_strEnemyHpText);
        l_oS.RegisterMember(toms::ui::g_pszLog, &UiBattle::m_strLog);
        l_oS.RegisterMember(toms::ui::g_pszContinueHint, &UiBattle::m_strContinueHint);
        l_oS.RegisterMember(toms::ui::g_pszClockLabel, &UiBattle::m_strClockLabel);
        l_oS.RegisterMember(toms::ui::g_pszClockPct, &UiBattle::m_fClockPct);
        l_oS.RegisterMember(toms::ui::g_pszAttack, &UiBattle::m_oAttack);
        l_oS.RegisterMember(toms::ui::g_pszDefense, &UiBattle::m_oDefense);
        l_oS.RegisterMember(toms::ui::g_pszCombo, &UiBattle::m_oCombo);
        l_oS.RegisterMember(toms::ui::g_pszShieldText, &UiBattle::m_strShieldText);
        l_oS.RegisterMember(toms::ui::g_pszShieldArmed, &UiBattle::m_bShieldArmed);
        l_oS.RegisterMember(toms::ui::g_pszCritArmed, &UiBattle::m_bCritArmed);
        l_oS.RegisterMember(toms::ui::g_pszCritHint, &UiBattle::m_strCritHint);
        l_oS.RegisterMember(toms::ui::g_pszSuperLabel, &UiBattle::m_strSuperLabel);
        l_oS.RegisterMember(toms::ui::g_pszSuperDots, &UiBattle::m_vSuperDots);
        l_oS.RegisterMember(toms::ui::g_pszSuperReady, &UiBattle::m_bSuperReady);
        l_oS.RegisterMember(toms::ui::g_pszSuperButton, &UiBattle::m_strSuperButton);
        l_oS.RegisterMember(toms::ui::g_pszActiveReady, &UiBattle::m_bActiveReady);
        l_oS.RegisterMember(toms::ui::g_pszActiveButton, &UiBattle::m_strActiveButton);
        l_oS.RegisterMember(toms::ui::g_pszPopups, &UiBattle::m_vPopups);
        l_oS.RegisterMember(toms::ui::g_pszCellsOn, &UiBattle::m_bCellsOn);
        l_oS.RegisterMember(toms::ui::g_pszCellHint, &UiBattle::m_strCellHint);
        l_oS.RegisterMember(toms::ui::g_pszTutorial, &UiBattle::m_strTutorial);
        l_oS.RegisterMember(toms::ui::g_pszTutorialMark, &UiBattle::m_strTutorialMark);
        l_oS.RegisterMember(toms::ui::g_pszTutorialHidden, &UiBattle::m_bTutorialHidden);
        l_oS.RegisterMember(toms::ui::g_pszBossIntro, &UiBattle::m_bBossIntro);
        l_oS.RegisterMember(toms::ui::g_pszHeroIntro, &UiBattle::m_bHeroIntro);
        l_oS.RegisterMember(toms::ui::g_pszBossIntroLabel, &UiBattle::m_strBossIntroLabel);
        l_oS.RegisterMember(toms::ui::g_pszBossIntroName, &UiBattle::m_strBossIntroName);
        l_oS.RegisterMember(toms::ui::g_pszBossIntroX, &UiBattle::m_fBossIntroX);
        l_oS.RegisterMember(toms::ui::g_pszTutorialScale, &UiBattle::m_fTutorialScale);
        l_oS.RegisterMember(toms::ui::g_pszCellsScale, &UiBattle::m_fCellsScale);
        l_oS.RegisterMember(toms::ui::g_pszCellHintY, &UiBattle::m_fCellHintY);
        l_oS.RegisterMember(toms::ui::g_pszFocus, &UiBattle::m_bFocus);
        l_oS.RegisterMember(toms::ui::g_pszCells, &UiBattle::m_vCells);
        l_oS.RegisterMember(toms::ui::g_pszSteps, &UiBattle::m_vSteps);
        l_oS.RegisterMember(toms::ui::g_pszTouchKind, &UiBattle::m_strTouchKind);
        l_oS.RegisterMember(toms::ui::g_pszTouch, &UiBattle::m_vTouch);
        l_oS.RegisterMember(toms::ui::g_pszSkillPrompt, &UiBattle::m_strSkillPrompt);
        l_oS.RegisterMember(toms::ui::g_pszSkillTime, &UiBattle::m_strSkillTime);
        l_oS.RegisterMember(toms::ui::g_pszFoes, &UiBattle::m_vFoes);
        l_oS.RegisterMember(toms::ui::g_pszStaminaPace, &UiBattle::m_bStaminaPace);
        for (auto [l_pszName, l_pMember] : std::initializer_list<std::pair<const char*, std::string UiBattle::*>>{
                 {toms::field::g_pszBanner, &UiBattle::m_strBanner}, {toms::field::g_pszBannerKind, &UiBattle::m_strBannerKind}, {toms::field::g_pszChainText, &UiBattle::m_strChainText},
                 {toms::field::g_pszGuardLabel, &UiBattle::m_strGuardLabel}, {toms::field::g_pszGuardHint, &UiBattle::m_strGuardHint},
                 {toms::field::g_pszAttackState, &UiBattle::m_strAttackState}, {toms::field::g_pszAttackLabel, &UiBattle::m_strAttackLabel},
                 {toms::field::g_pszAttackHint, &UiBattle::m_strAttackHint}, {toms::field::g_pszGrade, &UiBattle::m_strGrade}, {toms::field::g_pszResultTitle, &UiBattle::m_strResultTitle},
                 {toms::field::g_pszResultStats, &UiBattle::m_strResultStats}, {toms::field::g_pszResultWeapon, &UiBattle::m_strResultWeapon},
                 {toms::field::g_pszResultProgress, &UiBattle::m_strResultProgress}, {toms::field::g_pszResultNext, &UiBattle::m_strResultNext},
                 {toms::field::g_pszResultGold, &UiBattle::m_strResultGold}, {toms::field::g_pszResultCard, &UiBattle::m_strResultCard},
                 {toms::field::g_pszResultLevelup, &UiBattle::m_strResultLevelup}})
            l_oS.RegisterMember(l_pszName, l_pMember);
        l_oS.RegisterMember(toms::ui::g_pszBannerOpacity, &UiBattle::m_fBannerOpacity);
        l_oS.RegisterMember(toms::ui::g_pszBannerShift, &UiBattle::m_fBannerShift);
        l_oS.RegisterMember(toms::ui::g_pszAttackPct, &UiBattle::m_fAttackPct);
        l_oS.RegisterMember(toms::ui::g_pszResultPct, &UiBattle::m_fResultPct);
        l_oS.RegisterMember(toms::ui::g_pszGuardTiming, &UiBattle::m_bGuardTiming);
        l_oS.RegisterMember(toms::ui::g_pszGuardLit, &UiBattle::m_bGuardLit);
        l_oS.RegisterMember(toms::ui::g_pszGuardCooling, &UiBattle::m_bGuardCooling);
        l_oS.RegisterMember(toms::ui::g_pszShowClock, &UiBattle::m_bShowClock);
        l_oS.RegisterMember(toms::ui::g_pszResultShown, &UiBattle::m_bResultShown);
        l_oS.RegisterMember(toms::ui::g_pszGradeS, &UiBattle::m_bGradeS);
        l_oS.RegisterMember(toms::ui::g_pszAttackShort, &UiBattle::m_bAttackShort);
        l_oS.RegisterMember(toms::ui::g_pszGuardShort, &UiBattle::m_bGuardShort);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiDialogue>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiDialogue::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszSpeaker, &UiDialogue::m_strSpeaker);
        l_oS.RegisterMember(toms::ui::g_pszText, &UiDialogue::m_strText);
        l_oS.RegisterMember(toms::ui::g_pszChoices, &UiDialogue::m_vChoices);
        l_oS.RegisterMember(toms::ui::g_pszScale, &UiDialogue::m_fScale);
        l_oS.RegisterMember(toms::ui::g_pszSide, &UiDialogue::m_fSide);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiTab>()) {
        l_oS.RegisterMember(toms::ui::g_pszLabel, &UiTab::m_strLabel);
        l_oS.RegisterMember(toms::ui::g_pszActive, &UiTab::m_bActive);
        l_oS.RegisterMember(toms::ui::g_pszNews, &UiTab::m_bNews);
    }
    l_oC.RegisterArray<std::vector<UiTab>>();
    if (auto l_oS = l_oC.RegisterStruct<UiCell>()) {
        l_oS.RegisterMember(toms::ui::g_pszIcon, &UiCell::m_strIcon);
        l_oS.RegisterMember(toms::ui::g_pszName, &UiCell::m_strName);
        l_oS.RegisterMember(toms::ui::g_pszBadge, &UiCell::m_strBadge);
        l_oS.RegisterMember(toms::ui::g_pszSelected, &UiCell::m_bSelected);
        l_oS.RegisterMember(toms::ui::g_pszWorn, &UiCell::m_bWorn);
        l_oS.RegisterMember(toms::ui::g_pszLocked, &UiCell::m_bLocked);
    }
    l_oC.RegisterArray<std::vector<UiCell>>();
    if (auto l_oS = l_oC.RegisterStruct<UiLine>()) {
        l_oS.RegisterMember(toms::ui::g_pszLabel, &UiLine::m_strLabel);
        l_oS.RegisterMember(toms::ui::g_pszValue, &UiLine::m_strValue);
        l_oS.RegisterMember(toms::ui::g_pszExtra, &UiLine::m_strExtra);
        l_oS.RegisterMember(toms::ui::g_pszTone, &UiLine::m_iTone);
    }
    l_oC.RegisterArray<std::vector<UiLine>>();
    if (auto l_oS = l_oC.RegisterStruct<UiButton>()) {
        l_oS.RegisterMember(toms::ui::g_pszLabel, &UiButton::m_strLabel);
        l_oS.RegisterMember(toms::ui::g_pszEnabled, &UiButton::m_bEnabled);
    }
    l_oC.RegisterArray<std::vector<UiButton>>();
    if (auto l_oS = l_oC.RegisterStruct<UiPlayer>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiPlayer::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTab, &UiPlayer::m_iTab);
        l_oS.RegisterMember(toms::ui::g_pszTabs, &UiPlayer::m_vTabs);
        l_oS.RegisterMember(toms::ui::g_pszHint, &UiPlayer::m_strHint);
        l_oS.RegisterMember(toms::ui::g_pszCloseLabel, &UiPlayer::m_strCloseLabel);
        l_oS.RegisterMember(toms::ui::g_pszLevel, &UiPlayer::m_strLevel);
        l_oS.RegisterMember(toms::ui::g_pszHpText, &UiPlayer::m_strHpText);
        l_oS.RegisterMember(toms::ui::g_pszExpText, &UiPlayer::m_strExpText);
        l_oS.RegisterMember(toms::ui::g_pszHpPct, &UiPlayer::m_fHpPct);
        l_oS.RegisterMember(toms::ui::g_pszExpPct, &UiPlayer::m_fExpPct);
        l_oS.RegisterMember(toms::ui::g_pszCombatTitle, &UiPlayer::m_strCombatTitle);
        l_oS.RegisterMember(toms::ui::g_pszAttrTitle, &UiPlayer::m_strAttrTitle);
        l_oS.RegisterMember(toms::ui::g_pszAttrDesc, &UiPlayer::m_strAttrDesc);
        l_oS.RegisterMember(toms::ui::g_pszAttrPer, &UiPlayer::m_strAttrPer);
        l_oS.RegisterMember(toms::ui::g_pszAttrNow, &UiPlayer::m_strAttrNow);
        l_oS.RegisterMember(toms::ui::g_pszAttrPoints, &UiPlayer::m_strAttrPoints);
        l_oS.RegisterMember(toms::ui::g_pszAttrPointsHint, &UiPlayer::m_strAttrPointsHint);
        l_oS.RegisterMember(toms::ui::g_pszAttrCanAdd, &UiPlayer::m_bAttrCanAdd);
        l_oS.RegisterMember(toms::ui::g_pszAttrDraft, &UiPlayer::m_bAttrDraft);
        l_oS.RegisterMember(toms::ui::g_pszAttrConfirm, &UiPlayer::m_strAttrConfirm);
        l_oS.RegisterMember(toms::ui::g_pszAttrReset, &UiPlayer::m_strAttrReset);
        l_oS.RegisterMember(toms::ui::g_pszAttrConfirmSel, &UiPlayer::m_bAttrConfirmSel);
        l_oS.RegisterMember(toms::ui::g_pszAttrResetSel, &UiPlayer::m_bAttrResetSel);
        l_oS.RegisterMember(toms::ui::g_pszCombat, &UiPlayer::m_vCombat);
        l_oS.RegisterMember(toms::ui::g_pszMore, &UiPlayer::m_vMore);
        l_oS.RegisterMember(toms::ui::g_pszAttrs, &UiPlayer::m_vAttrs);
        l_oS.RegisterMember(toms::ui::g_pszFilters, &UiPlayer::m_vFilters);
        l_oS.RegisterMember(toms::ui::g_pszWornTitle, &UiPlayer::m_strWornTitle);
        l_oS.RegisterMember(toms::ui::g_pszWorn, &UiPlayer::m_vWorn);
        l_oS.RegisterMember(toms::ui::g_pszCells, &UiPlayer::m_vCells);
        l_oS.RegisterMember(toms::ui::g_pszEmptyText, &UiPlayer::m_strEmptyText);
        l_oS.RegisterMember(toms::ui::g_pszDetail, &UiPlayer::m_bDetail);
        l_oS.RegisterMember(toms::ui::g_pszSelectHint, &UiPlayer::m_strSelectHint);
        l_oS.RegisterMember(toms::ui::g_pszDIcon, &UiPlayer::m_strDIcon);
        l_oS.RegisterMember(toms::ui::g_pszDName, &UiPlayer::m_strDName);
        l_oS.RegisterMember(toms::ui::g_pszDDesc, &UiPlayer::m_strDDesc);
        l_oS.RegisterMember(toms::ui::g_pszDNote, &UiPlayer::m_strDNote);
        l_oS.RegisterMember(toms::ui::g_pszDLines, &UiPlayer::m_vDLines);
        l_oS.RegisterMember(toms::ui::g_pszDCompare, &UiPlayer::m_bDCompare);
        l_oS.RegisterMember(toms::ui::g_pszDNowLabel, &UiPlayer::m_strDNowLabel);
        l_oS.RegisterMember(toms::ui::g_pszDWithLabel, &UiPlayer::m_strDWithLabel);
        l_oS.RegisterMember(toms::ui::g_pszDCompareLines, &UiPlayer::m_vDCompareLines);
        l_oS.RegisterMember(toms::ui::g_pszDButtons, &UiPlayer::m_vDButtons);
        l_oS.RegisterMember(toms::ui::g_pszConfirmOpen, &UiPlayer::m_bConfirmOpen);
        l_oS.RegisterMember(toms::ui::g_pszConfirmQuestion, &UiPlayer::m_strConfirmQuestion);
        l_oS.RegisterMember(toms::ui::g_pszYesLabel, &UiPlayer::m_strYesLabel);
        l_oS.RegisterMember(toms::ui::g_pszNoLabel, &UiPlayer::m_strNoLabel);
        l_oS.RegisterMember(toms::ui::g_pszConfirmYes, &UiPlayer::m_bConfirmYes);
        l_oS.RegisterMember(toms::ui::g_pszEvFilters, &UiPlayer::m_vEvFilters);
        l_oS.RegisterMember(toms::ui::g_pszEvRows, &UiPlayer::m_vEvRows);
        l_oS.RegisterMember(toms::ui::g_pszEvDetail, &UiPlayer::m_bEvDetail);
        l_oS.RegisterMember(toms::ui::g_pszEvEmpty, &UiPlayer::m_strEvEmpty);
        l_oS.RegisterMember(toms::ui::g_pszEvHint, &UiPlayer::m_strEvHint);
        l_oS.RegisterMember(toms::ui::g_pszEvKind, &UiPlayer::m_strEvKind);
        l_oS.RegisterMember(toms::ui::g_pszEvTitle, &UiPlayer::m_strEvTitle);
        l_oS.RegisterMember(toms::ui::g_pszEvDesc, &UiPlayer::m_strEvDesc);
        l_oS.RegisterMember(toms::ui::g_pszEvConnectedTitle, &UiPlayer::m_strEvConnectedTitle);
        l_oS.RegisterMember(toms::ui::g_pszEvConnected, &UiPlayer::m_vEvConnected);
        l_oS.RegisterMember(toms::ui::g_pszSysHint, &UiPlayer::m_strSysHint);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiSlot>()) {
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiSlot::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszLine1, &UiSlot::m_strLine1);
        l_oS.RegisterMember(toms::ui::g_pszLine2, &UiSlot::m_strLine2);
        l_oS.RegisterMember(toms::ui::g_pszTag, &UiSlot::m_strTag);
        l_oS.RegisterMember(toms::ui::g_pszEmpty, &UiSlot::m_bEmpty);
        l_oS.RegisterMember(toms::ui::g_pszSelected, &UiSlot::m_bSelected);
        l_oS.RegisterMember(toms::ui::g_pszEnabled, &UiSlot::m_bEnabled);
    }
    l_oC.RegisterArray<std::vector<UiSlot>>();
    if (auto l_oS = l_oC.RegisterStruct<UiSaveLoad>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiSaveLoad::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiSaveLoad::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszHint, &UiSaveLoad::m_strHint);
        l_oS.RegisterMember(toms::ui::g_pszCloseLabel, &UiSaveLoad::m_strCloseLabel);
        l_oS.RegisterMember(toms::ui::g_pszModes, &UiSaveLoad::m_vModes);
        l_oS.RegisterMember(toms::ui::g_pszCards, &UiSaveLoad::m_vCards);
        l_oS.RegisterMember(toms::ui::g_pszConfirmOpen, &UiSaveLoad::m_bConfirmOpen);
        l_oS.RegisterMember(toms::ui::g_pszConfirmQuestion, &UiSaveLoad::m_strConfirmQuestion);
        l_oS.RegisterMember(toms::ui::g_pszConfirmBody, &UiSaveLoad::m_strConfirmBody);
        l_oS.RegisterMember(toms::ui::g_pszYesLabel, &UiSaveLoad::m_strYesLabel);
        l_oS.RegisterMember(toms::ui::g_pszNoLabel, &UiSaveLoad::m_strNoLabel);
        l_oS.RegisterMember(toms::ui::g_pszConfirmYes, &UiSaveLoad::m_bConfirmYes);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiStoreTab>()) {
        l_oS.RegisterMember(toms::ui::g_pszLabel, &UiStoreTab::m_strLabel);
        l_oS.RegisterMember(toms::ui::g_pszActive, &UiStoreTab::m_bActive);
    }
    l_oC.RegisterArray<std::vector<UiStoreTab>>();
    if (auto l_oS = l_oC.RegisterStruct<UiStoreItem>()) {
        l_oS.RegisterMember(toms::ui::g_pszIcon, &UiStoreItem::m_strIcon);
        l_oS.RegisterMember(toms::ui::g_pszName, &UiStoreItem::m_strName);
        l_oS.RegisterMember(toms::ui::g_pszDesc, &UiStoreItem::m_strDesc);
        l_oS.RegisterMember(toms::ui::g_pszEffect, &UiStoreItem::m_strEffect);
        l_oS.RegisterMember(toms::ui::g_pszStatus, &UiStoreItem::m_strStatus);
        l_oS.RegisterMember(toms::ui::g_pszPrice, &UiStoreItem::m_strPrice);
        l_oS.RegisterMember(toms::ui::g_pszSelected, &UiStoreItem::m_bSelected);
        l_oS.RegisterMember(toms::ui::g_pszEquipped, &UiStoreItem::m_bEquipped);
    }
    l_oC.RegisterArray<std::vector<UiStoreItem>>();
    if (auto l_oS = l_oC.RegisterStruct<UiStore>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiStore::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiStore::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszHint, &UiStore::m_strHint);
        l_oS.RegisterMember(toms::ui::g_pszGoldLabel, &UiStore::m_strGoldLabel);
        l_oS.RegisterMember(toms::ui::g_pszCloseLabel, &UiStore::m_strCloseLabel);
        l_oS.RegisterMember(toms::ui::g_pszEffectLabel, &UiStore::m_strEffectLabel);
        l_oS.RegisterMember(toms::ui::g_pszPriceLabel, &UiStore::m_strPriceLabel);
        l_oS.RegisterMember(toms::ui::g_pszBuyLabel, &UiStore::m_strBuyLabel);
        l_oS.RegisterMember(toms::ui::g_pszGold, &UiStore::m_iGold);
        l_oS.RegisterMember(toms::ui::g_pszTabs, &UiStore::m_vTabs);
        l_oS.RegisterMember(toms::ui::g_pszItems, &UiStore::m_vItems);
        l_oS.RegisterMember(toms::ui::g_pszUnlockedDialog, &UiStore::m_bUnlockedDialog);
        l_oS.RegisterMember(toms::ui::g_pszUnlockedTitle, &UiStore::m_strUnlockedTitle);
        l_oS.RegisterMember(toms::ui::g_pszUnlockedBody, &UiStore::m_strUnlockedBody);
        l_oS.RegisterMember(toms::ui::g_pszUnlockedOk, &UiStore::m_strUnlockedOk);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiMenu>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiMenu::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszPage, &UiMenu::m_iPage);
        l_oS.RegisterMember(toms::ui::g_pszMainRows, &UiMenu::m_vMainRows);
        l_oS.RegisterMember(toms::ui::g_pszHeader, &UiMenu::m_strHeader);
        l_oS.RegisterMember(toms::ui::g_pszSubheader, &UiMenu::m_strSubheader);
        l_oS.RegisterMember(toms::ui::g_pszRows, &UiMenu::m_vRows);
        l_oS.RegisterMember(toms::ui::g_pszCloseLabel, &UiMenu::m_strCloseLabel);
        l_oS.RegisterMember(toms::ui::g_pszConfirmOpen, &UiMenu::m_bConfirmOpen);
        l_oS.RegisterMember(toms::ui::g_pszConfirmQuestion, &UiMenu::m_strConfirmQuestion);
        l_oS.RegisterMember(toms::ui::g_pszConfirmBody, &UiMenu::m_strConfirmBody);
        l_oS.RegisterMember(toms::ui::g_pszYesLabel, &UiMenu::m_strYesLabel);
        l_oS.RegisterMember(toms::ui::g_pszNoLabel, &UiMenu::m_strNoLabel);
        l_oS.RegisterMember(toms::ui::g_pszConfirmYes, &UiMenu::m_bConfirmYes);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiNotice>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiNotice::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiNotice::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszBody, &UiNotice::m_strBody);
        l_oS.RegisterMember(toms::ui::g_pszOk, &UiNotice::m_strOk);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiStairs>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiStairs::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszQuestion, &UiStairs::m_strQuestion);
        l_oS.RegisterMember(toms::ui::g_pszYesLabel, &UiStairs::m_strYesLabel);
        l_oS.RegisterMember(toms::ui::g_pszNoLabel, &UiStairs::m_strNoLabel);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiStageSelect>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiStageSelect::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiStageSelect::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszHint, &UiStageSelect::m_strHint);
        l_oS.RegisterMember(toms::ui::g_pszCloseLabel, &UiStageSelect::m_strCloseLabel);
        l_oS.RegisterMember(toms::ui::g_pszRows, &UiStageSelect::m_vRows);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiTowerRow>()) {   // the main screen (docs/gameplay/27 §6)
        l_oS.RegisterMember(toms::ui::g_pszLabel, &UiTowerRow::m_strLabel);
        l_oS.RegisterMember(toms::ui::g_pszSub, &UiTowerRow::m_strSub);
        l_oS.RegisterMember(toms::ui::g_pszState, &UiTowerRow::m_strState);
        l_oS.RegisterMember(toms::ui::g_pszValue, &UiTowerRow::m_strValue);
        l_oS.RegisterMember(toms::ui::g_pszEnabled, &UiTowerRow::m_bEnabled);
    }
    l_oC.RegisterArray<std::vector<UiTowerRow>>();
    if (auto l_oS = l_oC.RegisterStruct<UiMain>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiMain::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszHome, &UiMain::m_bHome);
        l_oS.RegisterMember(toms::ui::g_pszPanel, &UiMain::m_iPanel);
        l_oS.RegisterMember(toms::ui::g_pszTitle, &UiMain::m_strTitle);
        l_oS.RegisterMember(toms::ui::g_pszSubtitle, &UiMain::m_strSubtitle);
        l_oS.RegisterMember(toms::ui::g_pszPanelTitle, &UiMain::m_strPanelTitle);
        l_oS.RegisterMember(toms::ui::g_pszHeroLevel, &UiMain::m_strHeroLevel);
        l_oS.RegisterMember(toms::ui::g_pszHeroHp, &UiMain::m_strHeroHp);
        l_oS.RegisterMember(toms::ui::g_pszHeroHpPct, &UiMain::m_fHeroHpPct);
        l_oS.RegisterMember(toms::ui::g_pszHeroAtk, &UiMain::m_strHeroAtk);
        l_oS.RegisterMember(toms::ui::g_pszHeroDef, &UiMain::m_strHeroDef);
        l_oS.RegisterMember(toms::ui::g_pszAtkLabel, &UiMain::m_strAtkLabel);
        l_oS.RegisterMember(toms::ui::g_pszDefLabel, &UiMain::m_strDefLabel);
        l_oS.RegisterMember(toms::ui::g_pszHpLabel, &UiMain::m_strHpLabel);
        l_oS.RegisterMember(toms::ui::g_pszGoldText, &UiMain::m_strGoldText);
        l_oS.RegisterMember(toms::ui::g_pszCrystalsText, &UiMain::m_strCrystalsText);
        l_oS.RegisterMember(toms::ui::g_pszStaminaText, &UiMain::m_strStaminaText);
        l_oS.RegisterMember(toms::ui::g_pszStaminaNext, &UiMain::m_strStaminaNext);
        l_oS.RegisterMember(toms::ui::g_pszClimbLabel, &UiMain::m_strClimbLabel);
        l_oS.RegisterMember(toms::ui::g_pszVillageLabel, &UiMain::m_strVillageLabel);
        l_oS.RegisterMember(toms::ui::g_pszBagLabel, &UiMain::m_strBagLabel);
        l_oS.RegisterMember(toms::ui::g_pszShopLabel, &UiMain::m_strShopLabel);
        l_oS.RegisterMember(toms::ui::g_pszQuestsLabel, &UiMain::m_strQuestsLabel);
        l_oS.RegisterMember(toms::ui::g_pszRankLabel, &UiMain::m_strRankLabel);
        l_oS.RegisterMember(toms::ui::g_pszHeroesLabel, &UiMain::m_strHeroesLabel);
        l_oS.RegisterMember(toms::ui::g_pszBackLabel, &UiMain::m_strBackLabel);
        l_oS.RegisterMember(toms::ui::g_pszTower, &UiMain::m_vTower);
        l_oS.RegisterMember(toms::ui::g_pszRows, &UiMain::m_vRows);
        l_oS.RegisterMember(toms::ui::g_pszTally, &UiMain::m_bTally);
        l_oS.RegisterMember(toms::ui::g_pszTallyCleared, &UiMain::m_bTallyCleared);
        l_oS.RegisterMember(toms::ui::g_pszTallyTitle, &UiMain::m_strTallyTitle);
        l_oS.RegisterMember(toms::ui::g_pszTallyRank, &UiMain::m_strTallyRank);
        l_oS.RegisterMember(toms::ui::g_pszTallyScore, &UiMain::m_strTallyScore);
        l_oS.RegisterMember(toms::ui::g_pszTallyNote, &UiMain::m_strTallyNote);
        l_oS.RegisterMember(toms::ui::g_pszTallyRows, &UiMain::m_vTallyRows);
        l_oS.RegisterMember(toms::ui::g_pszContinueLabel, &UiMain::m_strContinueLabel);
        l_oS.RegisterMember(toms::ui::g_pszHomeLabel, &UiMain::m_strHomeLabel);
        l_oS.RegisterMember(toms::ui::g_pszNoStamina, &UiMain::m_bNoStamina);
        l_oS.RegisterMember(toms::ui::g_pszNoStaminaText, &UiMain::m_strNoStaminaText);
        l_oS.RegisterMember(toms::ui::g_pszWaitLabel, &UiMain::m_strWaitLabel);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiEnding>()) {
        l_oS.RegisterMember(toms::ui::g_pszVisible, &UiEnding::m_bVisible);
        l_oS.RegisterMember(toms::ui::g_pszName, &UiEnding::m_strName);
        l_oS.RegisterMember(toms::ui::g_pszText, &UiEnding::m_strText);
        l_oS.RegisterMember(toms::ui::g_pszHint, &UiEnding::m_strHint);
        l_oS.RegisterMember(toms::ui::g_pszRebirth, &UiEnding::m_bRebirth);
        l_oS.RegisterMember(toms::ui::g_pszRebirthLabel, &UiEnding::m_strRebirthLabel);
        l_oS.RegisterMember(toms::ui::g_pszTitleLabel, &UiEnding::m_strTitleLabel);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiOverlay>()) {
        l_oS.RegisterMember(toms::ui::g_pszToast, &UiOverlay::m_bToast);
        l_oS.RegisterMember(toms::ui::g_pszToastText, &UiOverlay::m_strToastText);
        l_oS.RegisterMember(toms::ui::g_pszNotifications, &UiOverlay::m_vNotifications);
    }
}

}  // namespace

bool GameUi::f_Init(Rml::Context* l_pCtx, Game* l_pGame, const std::string& l_strUiDir, std::string& l_strError) {
    m_pCtx = l_pCtx;
    m_pGame = l_pGame;
    // generic_string: RmlUi joins relative URLs (images, stylesheets) on '/' only.
    m_strDir = std::filesystem::path(l_strUiDir).generic_string();

    Rml::DataModelConstructor l_oC = m_pCtx->CreateDataModel(toms::ui::g_pszGame);
    if (!l_oC) { l_strError = "RmlUi: could not create the \"game\" data model"; return false; }
    m_bModel = true;
    f_RegisterTypes(l_oC);
    l_oC.Bind(toms::ui::g_pszTitle, &m_oState.m_oTitle);
    l_oC.Bind(toms::ui::g_pszHud, &m_oState.m_oHud);
    l_oC.Bind(toms::ui::g_pszBattle, &m_oState.m_oBattle);
    l_oC.Bind(toms::ui::g_pszDialogue, &m_oState.m_oDialogue);
    l_oC.Bind(toms::ui::g_pszPlayer, &m_oState.m_oPlayer);
    l_oC.Bind(toms::ui::g_pszSaveload, &m_oState.m_oSaveload);
    l_oC.Bind(toms::ui::g_pszStore, &m_oState.m_oStore);
    l_oC.Bind(toms::ui::g_pszMenu, &m_oState.m_oMenu);
    l_oC.Bind(toms::ui::g_pszStairs, &m_oState.m_oStairs);
    l_oC.Bind(toms::ui::g_pszNotice, &m_oState.m_oNotice);
    l_oC.Bind(toms::ui::g_pszStageSelect, &m_oState.m_oStageSelect);
    l_oC.Bind(toms::ui::g_pszMain, &m_oState.m_oMain);
    l_oC.Bind(toms::ui::g_pszEnding, &m_oState.m_oEnding);
    l_oC.Bind(toms::ui::g_pszMinigame, &m_oState.m_oMinigame);
    l_oC.Bind(toms::ui::g_pszOverlay, &m_oState.m_oOverlay);
    // act('name') / act('name', n): the one event every button uses. The innermost handler wins
    // (a choice inside the dialogue box must not also count as a click on the box).
    l_oC.BindEventCallback(toms::ui::g_pszAct, [this](Rml::DataModelHandle, Rml::Event& l_oEv, const Rml::VariantList& l_vArgs) {
        l_oEv.StopPropagation();
        if (l_vArgs.empty()) return;
        m_pGame->f_UiEvent(l_vArgs[0].Get<Rml::String>(), l_vArgs.size() > 1 ? l_vArgs[1].Get<int>(0) : 0);
    });

    m_pGame->f_BuildUiState(m_oState);   // the documents bind to a filled model
    f_PublishSprites();
    if (!f_LoadDocuments(l_strError)) return false;
    f_Sync();                        // show the right documents before the first frame is drawn
    return true;
}

bool GameUi::f_LoadDocuments(std::string& l_strError) {
    m_vDocs.clear();
    for (const DocSpec& l_oSpec : g_arrDocs) {
        Doc l_oD;
        l_oD.m_strFile = m_pGame->f_PathOf(l_oSpec.m_pszKey);
        l_oD.m_pDoc = m_pCtx->LoadDocument(l_oD.m_strFile);
        if (!l_oD.m_pDoc) { l_strError = "RmlUi could not load " + l_oD.m_strFile + " (see the console log)"; return false; }
        if (m_strFamily != "toms") l_oD.m_pDoc->SetProperty("font-family", m_strFamily);
        m_vDocs.push_back(l_oD);
    }
    return true;
}

// Every document that shows sprites links "_atlas.rcss": the RmlUi @spritesheet for the atlas the
// game loaded (Game::uiSpritesheet). It is made in memory from that atlas, so the UI and the map
// always agree, and a .rml file only names sprites (<img data-attr-sprite="..."/>).
void GameUi::f_PublishSprites() {
    m_iSpriteRevision = m_pGame->f_SpriteAtlasRevision();
    const std::string l_strSheet = m_pGame->f_UiSpritesheet();
    if (l_strSheet.empty()) Rml::Log::Message(Rml::Log::LT_ERROR, "no prebuilt sprite atlas: the UI has no icons (assets/media/atlas)");
    for (const std::string& l_strName : m_pGame->f_MissingUiSprites())
        Rml::Log::Message(Rml::Log::LT_ERROR, "sprite '%s' is used by the game data but is not in the atlas", l_strName.c_str());
    RmlUi::f_SetVirtualFile("_atlas.rcss", l_strSheet);
}

void GameUi::f_SetFontFamily(const std::string& l_strFamily) {
    if (l_strFamily == m_strFamily) return;
    m_strFamily = l_strFamily;
    for (Doc& l_oD : m_vDocs)
        if (l_oD.m_pDoc) l_oD.m_pDoc->SetProperty("font-family", m_strFamily);
}

void GameUi::f_Shutdown() {
    for (Doc& l_oD : m_vDocs) if (l_oD.m_pDoc) l_oD.m_pDoc->Close();
    m_vDocs.clear();
    if (m_pCtx && m_bModel) m_pCtx->RemoveDataModel(toms::ui::g_pszGame);
    m_bModel = false;
    m_pCtx = nullptr;
}

void GameUi::f_Reload() {
    if (!m_pCtx) return;
    m_bDirtiedOnce = false;   // the documents come back fresh: every screen once more (f_Sync)
    for (Doc& l_oD : m_vDocs) if (l_oD.m_pDoc) l_oD.m_pDoc->Close();
    Rml::Factory::ClearStyleSheetCache();
    Rml::Factory::ClearTemplateCache();
    std::string l_strError;
    if (!f_LoadDocuments(l_strError)) Rml::Log::Message(Rml::Log::LT_ERROR, "%s", l_strError.c_str());
}

std::string GameUi::f_ReloadIfChanged() {
    namespace fs = std::filesystem;
    std::error_code l_oEc;
    std::string l_strChanged;
    std::map<std::string, fs::file_time_type> l_mapNow;
    for (const fs::directory_entry& l_oE : fs::directory_iterator(fs::u8path(m_strDir), l_oEc)) {
        const std::string l_strExt = l_oE.path().extension().string();
        if (l_strExt != ".rml" && l_strExt != ".rcss") continue;
        const fs::file_time_type l_oT = l_oE.last_write_time(l_oEc);
        if (l_oEc) continue;
        const std::string l_strName = l_oE.path().filename().u8string();
        l_mapNow[l_strName] = l_oT;
        if (m_bStamped && (!m_mapStamps.count(l_strName) || m_mapStamps[l_strName] != l_oT)) l_strChanged = l_strName;
    }
    if (m_bStamped && l_strChanged.empty() && l_mapNow.size() != m_mapStamps.size()) l_strChanged = "(a file removed)";
    m_mapStamps = std::move(l_mapNow);
    m_bStamped = true;
    if (!l_strChanged.empty()) f_Reload();
    return l_strChanged;
}

void GameUi::f_Sync() {
    if (!m_pCtx || !m_pGame) return;
    if (m_pGame->f_SpriteAtlasRevision() != m_iSpriteRevision) {   // another art style: new sheet, reload
        f_PublishSprites();
        f_Reload();
    }
    // A screen that is closing keeps its last values while it hides: its rows would otherwise
    // vanish under elements that are still bound to them (and flash empty for a frame).
    toms::UiState l_oNext;
    { toms::perf::Scope l_oP(toms::perf::section::g_pszUpdateUiBuild); m_pGame->f_BuildUiState(l_oNext); }
    toms::perf::Scope l_oPerfSync(toms::perf::section::g_pszUpdateUiSync);
    auto l_fnTake = [](auto& l_oCur, auto& l_oNxt) { if (l_oNxt.m_bVisible) l_oCur = std::move(l_oNxt); else l_oCur.m_bVisible = false; };
    l_fnTake(m_oState.m_oTitle, l_oNext.m_oTitle);
    m_oState.m_oTitle.m_bScene = m_bTitleScene;
    l_fnTake(m_oState.m_oHud, l_oNext.m_oHud);
    l_fnTake(m_oState.m_oBattle, l_oNext.m_oBattle);
    // Where the battle screen drew the attack track's points (last frame's layout), for the track's hint particles,
    // which the game draws over the UI (Game::comboHint).
    for (size_t l_nI = 0; l_nI < m_vDocs.size() && l_nI < std::size(g_arrDocs); l_nI++) {
        if (std::strcmp(g_arrDocs[l_nI].m_pszKey, toms::key::g_pszUiBattle) != 0 || !m_vDocs[l_nI].m_pDoc || !m_vDocs[l_nI].m_bShown) continue;
        Rml::ElementList l_vPts;
        m_vDocs[l_nI].m_pDoc->QuerySelectorAll(l_vPts, ".track .pt");
        std::vector<glm::vec2> l_vAt;
        for (Rml::Element* l_pE : l_vPts) {
            const Rml::Vector2f l_oO = l_pE->GetAbsoluteOffset(Rml::BoxArea::Border), l_oSz = l_pE->GetBox().GetSize(Rml::BoxArea::Border);
            l_vAt.push_back({l_oO.x + l_oSz.x * 0.5f, l_oO.y + l_oSz.y * 0.5f});
        }
        m_pGame->f_SetComboPointsOnScreen(std::move(l_vAt));
        auto l_fnBox = [](Rml::Element* l_pE) {   // the fighters' boxes (battle.rcss #player / .foe .fbox)
            if (!l_pE) return glm::vec4(0.0f);
            const Rml::Vector2f l_oO = l_pE->GetAbsoluteOffset(Rml::BoxArea::Border), l_oSz = l_pE->GetBox().GetSize(Rml::BoxArea::Border);
            return glm::vec4(l_oO.x, l_oO.y, l_oSz.x, l_oSz.y);
        };
        Rml::ElementList l_vFb;
        m_vDocs[l_nI].m_pDoc->QuerySelectorAll(l_vFb, ".foe .fbox");
        std::vector<glm::vec4> l_vFoes;
        for (Rml::Element* l_pE : l_vFb) l_vFoes.push_back(l_fnBox(l_pE));
        const glm::vec4 l_v4Pb = l_fnBox(m_vDocs[l_nI].m_pDoc->GetElementById(toms::ui::g_pszPlayer));
        if (l_v4Pb.z > 0 && !l_vFoes.empty() && l_vFoes[0].z > 0) m_pGame->f_SetBattleBoxes(l_v4Pb, l_vFoes);
    }
    l_fnTake(m_oState.m_oDialogue, l_oNext.m_oDialogue);
    l_fnTake(m_oState.m_oPlayer, l_oNext.m_oPlayer);
    l_fnTake(m_oState.m_oSaveload, l_oNext.m_oSaveload);
    l_fnTake(m_oState.m_oMenu, l_oNext.m_oMenu);
    l_fnTake(m_oState.m_oStairs, l_oNext.m_oStairs);
    l_fnTake(m_oState.m_oNotice, l_oNext.m_oNotice);
    l_fnTake(m_oState.m_oStageSelect, l_oNext.m_oStageSelect);
    l_fnTake(m_oState.m_oMain, l_oNext.m_oMain);
    l_fnTake(m_oState.m_oEnding, l_oNext.m_oEnding);
    l_fnTake(m_oState.m_oMinigame, l_oNext.m_oMinigame);   // a reward event (docs/gameplay/07 phase 6)
    if (l_oNext.m_oStore.m_bVisible || l_oNext.m_oStore.m_bUnlockedDialog) m_oState.m_oStore = std::move(l_oNext.m_oStore);
    else { m_oState.m_oStore.m_bVisible = false; m_oState.m_oStore.m_bUnlockedDialog = false; }
    m_oState.m_oOverlay = std::move(l_oNext.m_oOverlay);
    // The screens on show are re-evaluated each frame (RmlUi only touches elements whose value changed, and data-for
    // keeps its existing elements, so this never restarts a transition). A hidden screen is left alone: re-checking
    // every binding of every document each frame (DirtyAllVariables) was most of the UI's time (docs/engine/25 §4).
    // A screen is dirtied once more the frame it closes, so it hides.
    if (Rml::DataModelHandle l_oH = m_pCtx->GetDataModel(toms::ui::g_pszGame).GetModelHandle()) {
        const struct { const char* m_pszName; bool m_bShown; } l_arrScreens[] = {
            {toms::ui::g_pszTitle, m_oState.m_oTitle.m_bVisible},       {toms::ui::g_pszHud, m_oState.m_oHud.m_bVisible},
            {toms::ui::g_pszBattle, m_oState.m_oBattle.m_bVisible},     {toms::ui::g_pszDialogue, m_oState.m_oDialogue.m_bVisible},
            {toms::ui::g_pszPlayer, m_oState.m_oPlayer.m_bVisible},     {toms::ui::g_pszSaveload, m_oState.m_oSaveload.m_bVisible},
            {toms::ui::g_pszStore, m_oState.m_oStore.m_bVisible || m_oState.m_oStore.m_bUnlockedDialog},
            {toms::ui::g_pszMenu, m_oState.m_oMenu.m_bVisible},         {toms::ui::g_pszStairs, m_oState.m_oStairs.m_bVisible},
            {toms::ui::g_pszNotice, m_oState.m_oNotice.m_bVisible},     {toms::ui::g_pszStageSelect, m_oState.m_oStageSelect.m_bVisible},
            {toms::ui::g_pszMain, m_oState.m_oMain.m_bVisible},
            {toms::ui::g_pszEnding, m_oState.m_oEnding.m_bVisible},     {toms::ui::g_pszMinigame, m_oState.m_oMinigame.m_bVisible},
        };
        static_assert(std::size(l_arrScreens) <= m_nScreens, "GameUi::m_arrWasShown: one per screen");
        for (size_t l_nI = 0; l_nI < std::size(l_arrScreens); l_nI++) {
            if (l_arrScreens[l_nI].m_bShown || m_arrWasShown[l_nI] || !m_bDirtiedOnce) l_oH.DirtyVariable(l_arrScreens[l_nI].m_pszName);
            m_arrWasShown[l_nI] = l_arrScreens[l_nI].m_bShown;
        }
        l_oH.DirtyVariable(toms::ui::g_pszOverlay);   // the toasts and notices: over every screen
        m_bDirtiedOnce = true;
    }
    {   // which entry the player menu has selected: a change (keyboard or click) scrolls it into view
        std::string l_strKey = std::to_string(m_oState.m_oPlayer.m_iTab) + "|";
        auto l_fnAt = [&](const auto& l_oV) { for (size_t l_nI = 0; l_nI < l_oV.size(); l_nI++) if (l_oV[l_nI].m_bSelected) return (int)l_nI; return -1; };
        l_strKey += std::to_string(l_fnAt(m_oState.m_oPlayer.m_vCells)) + "|" + std::to_string(l_fnAt(m_oState.m_oPlayer.m_vEvRows)) + "|" +
               std::to_string(l_fnAt(m_oState.m_oPlayer.m_vAttrs)) + "|" + std::to_string(l_fnAt(m_oState.m_oMenu.m_vRows));
        if (l_strKey != m_strPlayerSelKey) { m_strPlayerSelKey = l_strKey; m_iScrollFrames = 2; }
    }
    for (size_t l_nI = 0; l_nI < m_vDocs.size(); l_nI++) {
        Doc& l_oD = m_vDocs[l_nI];
        if (!l_oD.m_pDoc) continue;
        const bool l_bWant = g_arrDocs[l_nI].m_fnVisible(m_oState);
        // The player menu's grids and lists scroll: an entry selected with the keyboard is scrolled
        // into view. The selection is laid out by the next context update, so this looks one frame later.
        if (l_bWant && l_oD.m_bShown && m_iScrollFrames > 0 && std::strcmp(g_arrDocs[l_nI].m_pszKey, toms::key::g_pszUiPlayer) == 0) {
            --m_iScrollFrames;
            for (Rml::Element* l_pSel : {l_oD.m_pDoc->QuerySelector(toms::ui::g_pszCellSelected), l_oD.m_pDoc->QuerySelector(toms::ui::g_pszEvrowSelected),
                                      l_oD.m_pDoc->QuerySelector(toms::ui::g_pszAttrSelected), l_oD.m_pDoc->QuerySelector(toms::ui::g_pszSysPageRowSelected)})
                if (l_pSel) l_pSel->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
                                                                       Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest));
        }
        if (l_bWant == l_oD.m_bShown) continue;
        if (l_bWant) l_oD.m_pDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        else l_oD.m_pDoc->Hide();
        l_oD.m_bShown = l_bWant;
    }
}

}  // namespace toms::next

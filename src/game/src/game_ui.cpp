// game_ui.cpp -- see game_ui.h.
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
        l_oS.RegisterMember("label", &UiRow::m_strLabel);
        l_oS.RegisterMember("sub", &UiRow::m_strSub);
        l_oS.RegisterMember("selected", &UiRow::m_bSelected);
        l_oS.RegisterMember("enabled", &UiRow::m_bEnabled);
        l_oS.RegisterMember("section", &UiRow::m_strSection);
        l_oS.RegisterMember("extra", &UiRow::m_bExtra);
    }
    l_oC.RegisterArray<std::vector<UiRow>>();
    if (auto l_oS = l_oC.RegisterStruct<UiBanner>()) {
        l_oS.RegisterMember("text", &UiBanner::m_strText);
        l_oS.RegisterMember("x", &UiBanner::m_fX);
        l_oS.RegisterMember("y", &UiBanner::m_fY);
    }
    l_oC.RegisterArray<std::vector<UiBanner>>();
    if (auto l_oS = l_oC.RegisterStruct<UiTitle>()) {
        l_oS.RegisterMember("visible", &UiTitle::m_bVisible);
        l_oS.RegisterMember("scene", &UiTitle::m_bScene);
        l_oS.RegisterMember("title", &UiTitle::m_strTitle);
        l_oS.RegisterMember("subtitle", &UiTitle::m_strSubtitle);
        l_oS.RegisterMember("version", &UiTitle::m_strVersion);
        l_oS.RegisterMember("hint", &UiTitle::m_strHint);
        l_oS.RegisterMember("page", &UiTitle::m_iPage);
        l_oS.RegisterMember("header", &UiTitle::m_strHeader);
        l_oS.RegisterMember("subheader", &UiTitle::m_strSubheader);
        l_oS.RegisterMember("rows", &UiTitle::m_vRows);
        l_oS.RegisterMember("show_back", &UiTitle::m_bShowBack);
        l_oS.RegisterMember("back_label", &UiTitle::m_strBackLabel);
        l_oS.RegisterMember("show_empty_hint", &UiTitle::m_bShowEmptyHint);
        l_oS.RegisterMember("empty_hint", &UiTitle::m_strEmptyHint);
        l_oS.RegisterMember("confirm_open", &UiTitle::m_bConfirmOpen);
        l_oS.RegisterMember("confirm_question", &UiTitle::m_strConfirmQuestion);
        l_oS.RegisterMember("confirm_body", &UiTitle::m_strConfirmBody);
        l_oS.RegisterMember("yes_label", &UiTitle::m_strYesLabel);
        l_oS.RegisterMember("no_label", &UiTitle::m_strNoLabel);
        l_oS.RegisterMember("confirm_yes", &UiTitle::m_bConfirmYes);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiHud>()) {
        l_oS.RegisterMember("visible", &UiHud::m_bVisible);
        l_oS.RegisterMember("title_line", &UiHud::m_strTitleLine);
        l_oS.RegisterMember("hp_pct", &UiHud::m_fHpPct);
        l_oS.RegisterMember("hp_text", &UiHud::m_strHpText);
        l_oS.RegisterMember("stats_line", &UiHud::m_strStatsLine);
        l_oS.RegisterMember("res_line", &UiHud::m_strResLine);
        l_oS.RegisterMember("footer", &UiHud::m_strFooter);
        l_oS.RegisterMember("menu_label", &UiHud::m_strMenuLabel);
        l_oS.RegisterMember("menu_news", &UiHud::m_bMenuNews);
        l_oS.RegisterMember("chapter_card", &UiHud::m_bChapterCard);
        l_oS.RegisterMember("chapter_title", &UiHud::m_strChapterTitle);
        l_oS.RegisterMember("chapter_sub", &UiHud::m_strChapterSub);
        l_oS.RegisterMember("chapter_alpha", &UiHud::m_fChapterAlpha);
        l_oS.RegisterMember("banners", &UiHud::m_vBanners);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiPowerBar>()) {
        l_oS.RegisterMember("prompt", &UiPowerBar::m_strPrompt);
        l_oS.RegisterMember("button_label", &UiPowerBar::m_strButtonLabel);
        l_oS.RegisterMember("cooling", &UiPowerBar::m_bCooling);
        l_oS.RegisterMember("red_l", &UiPowerBar::m_fRedL);
        l_oS.RegisterMember("blue_l", &UiPowerBar::m_fBlueL);
        l_oS.RegisterMember("green", &UiPowerBar::m_fGreen);
        l_oS.RegisterMember("blue_r", &UiPowerBar::m_fBlueR);
        l_oS.RegisterMember("red_r", &UiPowerBar::m_fRedR);
        l_oS.RegisterMember("marker", &UiPowerBar::m_fMarker);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiComboPoint>()) {
        l_oS.RegisterMember("pos", &UiComboPoint::m_fPos);
        l_oS.RegisterMember("hit", &UiComboPoint::m_bHit);
        l_oS.RegisterMember("near", &UiComboPoint::m_bNear);
    }
    l_oC.RegisterArray<std::vector<UiComboPoint>>();
    if (auto l_oS = l_oC.RegisterStruct<UiCombo>()) {
        l_oS.RegisterMember("prompt", &UiCombo::m_strPrompt);
        l_oS.RegisterMember("button_label", &UiCombo::m_strButtonLabel);
        l_oS.RegisterMember("level_text", &UiCombo::m_strLevelText);
        l_oS.RegisterMember("running", &UiCombo::m_bRunning);
        l_oS.RegisterMember("cooling", &UiCombo::m_bCooling);
        l_oS.RegisterMember("waiting", &UiCombo::m_bWaiting);
        l_oS.RegisterMember("charge_pct", &UiCombo::m_fChargePct);
        l_oS.RegisterMember("marker", &UiCombo::m_fMarker);
        l_oS.RegisterMember("points", &UiCombo::m_vPoints);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiMiniGame>()) {
        l_oS.RegisterMember("visible", &UiMiniGame::m_bVisible);
        l_oS.RegisterMember("title", &UiMiniGame::m_strTitle);
        l_oS.RegisterMember("text", &UiMiniGame::m_strText);
        l_oS.RegisterMember("prompt", &UiMiniGame::m_strPrompt);
        l_oS.RegisterMember("button_label", &UiMiniGame::m_strButtonLabel);
        l_oS.RegisterMember("result", &UiMiniGame::m_strResult);
        l_oS.RegisterMember("close_label", &UiMiniGame::m_strCloseLabel);
        l_oS.RegisterMember("running", &UiMiniGame::m_bRunning);
        l_oS.RegisterMember("done", &UiMiniGame::m_bDone);
        l_oS.RegisterMember("marker", &UiMiniGame::m_fMarker);
        l_oS.RegisterMember("points", &UiMiniGame::m_vPoints);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiPopup>()) {
        l_oS.RegisterMember("text", &UiPopup::m_strText);
        l_oS.RegisterMember("x", &UiPopup::m_fX);
        l_oS.RegisterMember("y", &UiPopup::m_fY);
        l_oS.RegisterMember("opacity", &UiPopup::m_fOpacity);
        l_oS.RegisterMember("style", &UiPopup::m_strStyle);
    }
    l_oC.RegisterArray<std::vector<UiPopup>>();
    if (auto l_oS = l_oC.RegisterStruct<UiBattleCell>()) {
        l_oS.RegisterMember("x", &UiBattleCell::m_fX);
        l_oS.RegisterMember("y", &UiBattleCell::m_fY);
        l_oS.RegisterMember("w", &UiBattleCell::m_fW);
        l_oS.RegisterMember("h", &UiBattleCell::m_fH);
        l_oS.RegisterMember("skew", &UiBattleCell::m_fSkew);
        l_oS.RegisterMember("here", &UiBattleCell::m_bHere);
        l_oS.RegisterMember("danger", &UiBattleCell::m_bDanger);
        l_oS.RegisterMember("fill", &UiBattleCell::m_fFill);
    }
    l_oC.RegisterArray<std::vector<UiBattleCell>>();
    if (auto l_oS = l_oC.RegisterStruct<UiTouchItem>()) {
        l_oS.RegisterMember("cls", &UiTouchItem::m_strCls);
        l_oS.RegisterMember("x", &UiTouchItem::m_fX);
        l_oS.RegisterMember("y", &UiTouchItem::m_fY);
        l_oS.RegisterMember("w", &UiTouchItem::m_fW);
        l_oS.RegisterMember("h", &UiTouchItem::m_fH);
        l_oS.RegisterMember("rot", &UiTouchItem::m_fRot);
        l_oS.RegisterMember("radius", &UiTouchItem::m_fRadius);
        l_oS.RegisterMember("opacity", &UiTouchItem::m_fOpacity);
        l_oS.RegisterMember("text", &UiTouchItem::m_strText);
    }
    l_oC.RegisterArray<std::vector<UiTouchItem>>();
    if (auto l_oS = l_oC.RegisterStruct<UiFoe>()) {
        l_oS.RegisterMember("sprite", &UiFoe::m_strSprite);
        l_oS.RegisterMember("anim", &UiFoe::m_bAnim);
        l_oS.RegisterMember("hp_pct", &UiFoe::m_fHpPct);
        l_oS.RegisterMember("hp_text", &UiFoe::m_strHpText);
        l_oS.RegisterMember("name", &UiFoe::m_strName);
        l_oS.RegisterMember("hp_line", &UiFoe::m_strHpLine);
        l_oS.RegisterMember("target", &UiFoe::m_bTarget);
        l_oS.RegisterMember("down", &UiFoe::m_bDown);
        l_oS.RegisterMember("intent", &UiFoe::m_strIntent);
        l_oS.RegisterMember("intent_kind", &UiFoe::m_strIntentKind);
        l_oS.RegisterMember("intent_pct", &UiFoe::m_fIntentPct);
        l_oS.RegisterMember("break_pct", &UiFoe::m_fBreakPct);
        l_oS.RegisterMember("warn", &UiFoe::m_bWarn);
    }
    l_oC.RegisterArray<std::vector<UiFoe>>();
    if (auto l_oS = l_oC.RegisterStruct<UiBattle>()) {
        l_oS.RegisterMember("visible", &UiBattle::m_bVisible);
        l_oS.RegisterMember("active", &UiBattle::m_bActive);
        l_oS.RegisterMember("title", &UiBattle::m_strTitle);
        l_oS.RegisterMember("player_sprite", &UiBattle::m_strPlayerSprite);
        l_oS.RegisterMember("player_anim", &UiBattle::m_bPlayerAnim);
        l_oS.RegisterMember("enemy_anim", &UiBattle::m_bEnemyAnim);
        l_oS.RegisterMember("enemy_sprite", &UiBattle::m_strEnemySprite);
        l_oS.RegisterMember("player_hp_pct", &UiBattle::m_fPlayerHpPct);
        l_oS.RegisterMember("enemy_hp_pct", &UiBattle::m_fEnemyHpPct);
        l_oS.RegisterMember("player_hp_text", &UiBattle::m_strPlayerHpText);
        l_oS.RegisterMember("enemy_hp_text", &UiBattle::m_strEnemyHpText);
        l_oS.RegisterMember("log", &UiBattle::m_strLog);
        l_oS.RegisterMember("continue_hint", &UiBattle::m_strContinueHint);
        l_oS.RegisterMember("clock_label", &UiBattle::m_strClockLabel);
        l_oS.RegisterMember("clock_pct", &UiBattle::m_fClockPct);
        l_oS.RegisterMember("attack", &UiBattle::m_oAttack);
        l_oS.RegisterMember("defense", &UiBattle::m_oDefense);
        l_oS.RegisterMember("combo", &UiBattle::m_oCombo);
        l_oS.RegisterMember("shield_text", &UiBattle::m_strShieldText);
        l_oS.RegisterMember("shield_armed", &UiBattle::m_bShieldArmed);
        l_oS.RegisterMember("crit_armed", &UiBattle::m_bCritArmed);
        l_oS.RegisterMember("crit_hint", &UiBattle::m_strCritHint);
        l_oS.RegisterMember("super_label", &UiBattle::m_strSuperLabel);
        l_oS.RegisterMember("super_dots", &UiBattle::m_vSuperDots);
        l_oS.RegisterMember("super_ready", &UiBattle::m_bSuperReady);
        l_oS.RegisterMember("super_button", &UiBattle::m_strSuperButton);
        l_oS.RegisterMember("active_ready", &UiBattle::m_bActiveReady);
        l_oS.RegisterMember("active_button", &UiBattle::m_strActiveButton);
        l_oS.RegisterMember("popups", &UiBattle::m_vPopups);
        l_oS.RegisterMember("cells_on", &UiBattle::m_bCellsOn);
        l_oS.RegisterMember("cell_hint", &UiBattle::m_strCellHint);
        l_oS.RegisterMember("tutorial", &UiBattle::m_strTutorial);
        l_oS.RegisterMember("tutorial_mark", &UiBattle::m_strTutorialMark);
        l_oS.RegisterMember("tutorial_scale", &UiBattle::m_fTutorialScale);
        l_oS.RegisterMember("cells_scale", &UiBattle::m_fCellsScale);
        l_oS.RegisterMember("cell_hint_y", &UiBattle::m_fCellHintY);
        l_oS.RegisterMember("focus", &UiBattle::m_bFocus);
        l_oS.RegisterMember("cells", &UiBattle::m_vCells);
        l_oS.RegisterMember("touch_kind", &UiBattle::m_strTouchKind);
        l_oS.RegisterMember("touch", &UiBattle::m_vTouch);
        l_oS.RegisterMember("skill_prompt", &UiBattle::m_strSkillPrompt);
        l_oS.RegisterMember("skill_time", &UiBattle::m_strSkillTime);
        l_oS.RegisterMember("foes", &UiBattle::m_vFoes);
        l_oS.RegisterMember("stamina_pace", &UiBattle::m_bStaminaPace);
        for (auto [l_pszName, l_pMember] : std::initializer_list<std::pair<const char*, std::string UiBattle::*>>{
                 {"banner", &UiBattle::m_strBanner}, {"banner_kind", &UiBattle::m_strBannerKind}, {"chain_text", &UiBattle::m_strChainText},
                 {"guard_label", &UiBattle::m_strGuardLabel}, {"guard_hint", &UiBattle::m_strGuardHint},
                 {"attack_state", &UiBattle::m_strAttackState}, {"attack_label", &UiBattle::m_strAttackLabel},
                 {"attack_hint", &UiBattle::m_strAttackHint}, {"grade", &UiBattle::m_strGrade}, {"result_title", &UiBattle::m_strResultTitle},
                 {"result_stats", &UiBattle::m_strResultStats}, {"result_weapon", &UiBattle::m_strResultWeapon},
                 {"result_progress", &UiBattle::m_strResultProgress}, {"result_next", &UiBattle::m_strResultNext},
                 {"result_gold", &UiBattle::m_strResultGold}, {"result_card", &UiBattle::m_strResultCard},
                 {"result_levelup", &UiBattle::m_strResultLevelup}})
            l_oS.RegisterMember(l_pszName, l_pMember);
        l_oS.RegisterMember("banner_opacity", &UiBattle::m_fBannerOpacity);
        l_oS.RegisterMember("banner_shift", &UiBattle::m_fBannerShift);
        l_oS.RegisterMember("attack_pct", &UiBattle::m_fAttackPct);
        l_oS.RegisterMember("result_pct", &UiBattle::m_fResultPct);
        l_oS.RegisterMember("guard_timing", &UiBattle::m_bGuardTiming);
        l_oS.RegisterMember("guard_lit", &UiBattle::m_bGuardLit);
        l_oS.RegisterMember("guard_cooling", &UiBattle::m_bGuardCooling);
        l_oS.RegisterMember("show_clock", &UiBattle::m_bShowClock);
        l_oS.RegisterMember("result_shown", &UiBattle::m_bResultShown);
        l_oS.RegisterMember("grade_s", &UiBattle::m_bGradeS);
        l_oS.RegisterMember("attack_short", &UiBattle::m_bAttackShort);
        l_oS.RegisterMember("guard_short", &UiBattle::m_bGuardShort);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiDialogue>()) {
        l_oS.RegisterMember("visible", &UiDialogue::m_bVisible);
        l_oS.RegisterMember("speaker", &UiDialogue::m_strSpeaker);
        l_oS.RegisterMember("text", &UiDialogue::m_strText);
        l_oS.RegisterMember("choices", &UiDialogue::m_vChoices);
        l_oS.RegisterMember("scale", &UiDialogue::m_fScale);
        l_oS.RegisterMember("side", &UiDialogue::m_fSide);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiTab>()) {
        l_oS.RegisterMember("label", &UiTab::m_strLabel);
        l_oS.RegisterMember("active", &UiTab::m_bActive);
        l_oS.RegisterMember("news", &UiTab::m_bNews);
    }
    l_oC.RegisterArray<std::vector<UiTab>>();
    if (auto l_oS = l_oC.RegisterStruct<UiCell>()) {
        l_oS.RegisterMember("icon", &UiCell::m_strIcon);
        l_oS.RegisterMember("name", &UiCell::m_strName);
        l_oS.RegisterMember("badge", &UiCell::m_strBadge);
        l_oS.RegisterMember("selected", &UiCell::m_bSelected);
        l_oS.RegisterMember("worn", &UiCell::m_bWorn);
        l_oS.RegisterMember("locked", &UiCell::m_bLocked);
    }
    l_oC.RegisterArray<std::vector<UiCell>>();
    if (auto l_oS = l_oC.RegisterStruct<UiLine>()) {
        l_oS.RegisterMember("label", &UiLine::m_strLabel);
        l_oS.RegisterMember("value", &UiLine::m_strValue);
        l_oS.RegisterMember("extra", &UiLine::m_strExtra);
        l_oS.RegisterMember("tone", &UiLine::m_iTone);
    }
    l_oC.RegisterArray<std::vector<UiLine>>();
    if (auto l_oS = l_oC.RegisterStruct<UiButton>()) {
        l_oS.RegisterMember("label", &UiButton::m_strLabel);
        l_oS.RegisterMember("enabled", &UiButton::m_bEnabled);
    }
    l_oC.RegisterArray<std::vector<UiButton>>();
    if (auto l_oS = l_oC.RegisterStruct<UiPlayer>()) {
        l_oS.RegisterMember("visible", &UiPlayer::m_bVisible);
        l_oS.RegisterMember("tab", &UiPlayer::m_iTab);
        l_oS.RegisterMember("tabs", &UiPlayer::m_vTabs);
        l_oS.RegisterMember("hint", &UiPlayer::m_strHint);
        l_oS.RegisterMember("close_label", &UiPlayer::m_strCloseLabel);
        l_oS.RegisterMember("level", &UiPlayer::m_strLevel);
        l_oS.RegisterMember("hp_text", &UiPlayer::m_strHpText);
        l_oS.RegisterMember("exp_text", &UiPlayer::m_strExpText);
        l_oS.RegisterMember("hp_pct", &UiPlayer::m_fHpPct);
        l_oS.RegisterMember("exp_pct", &UiPlayer::m_fExpPct);
        l_oS.RegisterMember("combat_title", &UiPlayer::m_strCombatTitle);
        l_oS.RegisterMember("attr_title", &UiPlayer::m_strAttrTitle);
        l_oS.RegisterMember("attr_desc", &UiPlayer::m_strAttrDesc);
        l_oS.RegisterMember("attr_per", &UiPlayer::m_strAttrPer);
        l_oS.RegisterMember("attr_now", &UiPlayer::m_strAttrNow);
        l_oS.RegisterMember("attr_points", &UiPlayer::m_strAttrPoints);
        l_oS.RegisterMember("attr_points_hint", &UiPlayer::m_strAttrPointsHint);
        l_oS.RegisterMember("attr_can_add", &UiPlayer::m_bAttrCanAdd);
        l_oS.RegisterMember("attr_draft", &UiPlayer::m_bAttrDraft);
        l_oS.RegisterMember("attr_confirm", &UiPlayer::m_strAttrConfirm);
        l_oS.RegisterMember("attr_reset", &UiPlayer::m_strAttrReset);
        l_oS.RegisterMember("attr_confirm_sel", &UiPlayer::m_bAttrConfirmSel);
        l_oS.RegisterMember("attr_reset_sel", &UiPlayer::m_bAttrResetSel);
        l_oS.RegisterMember("combat", &UiPlayer::m_vCombat);
        l_oS.RegisterMember("more", &UiPlayer::m_vMore);
        l_oS.RegisterMember("attrs", &UiPlayer::m_vAttrs);
        l_oS.RegisterMember("filters", &UiPlayer::m_vFilters);
        l_oS.RegisterMember("worn_title", &UiPlayer::m_strWornTitle);
        l_oS.RegisterMember("worn", &UiPlayer::m_vWorn);
        l_oS.RegisterMember("cells", &UiPlayer::m_vCells);
        l_oS.RegisterMember("empty_text", &UiPlayer::m_strEmptyText);
        l_oS.RegisterMember("detail", &UiPlayer::m_bDetail);
        l_oS.RegisterMember("select_hint", &UiPlayer::m_strSelectHint);
        l_oS.RegisterMember("d_icon", &UiPlayer::m_strDIcon);
        l_oS.RegisterMember("d_name", &UiPlayer::m_strDName);
        l_oS.RegisterMember("d_desc", &UiPlayer::m_strDDesc);
        l_oS.RegisterMember("d_note", &UiPlayer::m_strDNote);
        l_oS.RegisterMember("d_lines", &UiPlayer::m_vDLines);
        l_oS.RegisterMember("d_compare", &UiPlayer::m_bDCompare);
        l_oS.RegisterMember("d_now_label", &UiPlayer::m_strDNowLabel);
        l_oS.RegisterMember("d_with_label", &UiPlayer::m_strDWithLabel);
        l_oS.RegisterMember("d_compare_lines", &UiPlayer::m_vDCompareLines);
        l_oS.RegisterMember("d_buttons", &UiPlayer::m_vDButtons);
        l_oS.RegisterMember("confirm_open", &UiPlayer::m_bConfirmOpen);
        l_oS.RegisterMember("confirm_question", &UiPlayer::m_strConfirmQuestion);
        l_oS.RegisterMember("yes_label", &UiPlayer::m_strYesLabel);
        l_oS.RegisterMember("no_label", &UiPlayer::m_strNoLabel);
        l_oS.RegisterMember("confirm_yes", &UiPlayer::m_bConfirmYes);
        l_oS.RegisterMember("ev_filters", &UiPlayer::m_vEvFilters);
        l_oS.RegisterMember("ev_rows", &UiPlayer::m_vEvRows);
        l_oS.RegisterMember("ev_detail", &UiPlayer::m_bEvDetail);
        l_oS.RegisterMember("ev_empty", &UiPlayer::m_strEvEmpty);
        l_oS.RegisterMember("ev_hint", &UiPlayer::m_strEvHint);
        l_oS.RegisterMember("ev_kind", &UiPlayer::m_strEvKind);
        l_oS.RegisterMember("ev_title", &UiPlayer::m_strEvTitle);
        l_oS.RegisterMember("ev_desc", &UiPlayer::m_strEvDesc);
        l_oS.RegisterMember("ev_connected_title", &UiPlayer::m_strEvConnectedTitle);
        l_oS.RegisterMember("ev_connected", &UiPlayer::m_vEvConnected);
        l_oS.RegisterMember("sys_hint", &UiPlayer::m_strSysHint);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiSlot>()) {
        l_oS.RegisterMember("title", &UiSlot::m_strTitle);
        l_oS.RegisterMember("line1", &UiSlot::m_strLine1);
        l_oS.RegisterMember("line2", &UiSlot::m_strLine2);
        l_oS.RegisterMember("tag", &UiSlot::m_strTag);
        l_oS.RegisterMember("empty", &UiSlot::m_bEmpty);
        l_oS.RegisterMember("selected", &UiSlot::m_bSelected);
        l_oS.RegisterMember("enabled", &UiSlot::m_bEnabled);
    }
    l_oC.RegisterArray<std::vector<UiSlot>>();
    if (auto l_oS = l_oC.RegisterStruct<UiSaveLoad>()) {
        l_oS.RegisterMember("visible", &UiSaveLoad::m_bVisible);
        l_oS.RegisterMember("title", &UiSaveLoad::m_strTitle);
        l_oS.RegisterMember("hint", &UiSaveLoad::m_strHint);
        l_oS.RegisterMember("close_label", &UiSaveLoad::m_strCloseLabel);
        l_oS.RegisterMember("modes", &UiSaveLoad::m_vModes);
        l_oS.RegisterMember("cards", &UiSaveLoad::m_vCards);
        l_oS.RegisterMember("confirm_open", &UiSaveLoad::m_bConfirmOpen);
        l_oS.RegisterMember("confirm_question", &UiSaveLoad::m_strConfirmQuestion);
        l_oS.RegisterMember("confirm_body", &UiSaveLoad::m_strConfirmBody);
        l_oS.RegisterMember("yes_label", &UiSaveLoad::m_strYesLabel);
        l_oS.RegisterMember("no_label", &UiSaveLoad::m_strNoLabel);
        l_oS.RegisterMember("confirm_yes", &UiSaveLoad::m_bConfirmYes);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiStoreTab>()) {
        l_oS.RegisterMember("label", &UiStoreTab::m_strLabel);
        l_oS.RegisterMember("active", &UiStoreTab::m_bActive);
    }
    l_oC.RegisterArray<std::vector<UiStoreTab>>();
    if (auto l_oS = l_oC.RegisterStruct<UiStoreItem>()) {
        l_oS.RegisterMember("icon", &UiStoreItem::m_strIcon);
        l_oS.RegisterMember("name", &UiStoreItem::m_strName);
        l_oS.RegisterMember("desc", &UiStoreItem::m_strDesc);
        l_oS.RegisterMember("effect", &UiStoreItem::m_strEffect);
        l_oS.RegisterMember("status", &UiStoreItem::m_strStatus);
        l_oS.RegisterMember("price", &UiStoreItem::m_strPrice);
        l_oS.RegisterMember("selected", &UiStoreItem::m_bSelected);
        l_oS.RegisterMember("equipped", &UiStoreItem::m_bEquipped);
    }
    l_oC.RegisterArray<std::vector<UiStoreItem>>();
    if (auto l_oS = l_oC.RegisterStruct<UiStore>()) {
        l_oS.RegisterMember("visible", &UiStore::m_bVisible);
        l_oS.RegisterMember("title", &UiStore::m_strTitle);
        l_oS.RegisterMember("hint", &UiStore::m_strHint);
        l_oS.RegisterMember("gold_label", &UiStore::m_strGoldLabel);
        l_oS.RegisterMember("close_label", &UiStore::m_strCloseLabel);
        l_oS.RegisterMember("effect_label", &UiStore::m_strEffectLabel);
        l_oS.RegisterMember("price_label", &UiStore::m_strPriceLabel);
        l_oS.RegisterMember("buy_label", &UiStore::m_strBuyLabel);
        l_oS.RegisterMember("gold", &UiStore::m_iGold);
        l_oS.RegisterMember("tabs", &UiStore::m_vTabs);
        l_oS.RegisterMember("items", &UiStore::m_vItems);
        l_oS.RegisterMember("unlocked_dialog", &UiStore::m_bUnlockedDialog);
        l_oS.RegisterMember("unlocked_title", &UiStore::m_strUnlockedTitle);
        l_oS.RegisterMember("unlocked_body", &UiStore::m_strUnlockedBody);
        l_oS.RegisterMember("unlocked_ok", &UiStore::m_strUnlockedOk);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiMenu>()) {
        l_oS.RegisterMember("visible", &UiMenu::m_bVisible);
        l_oS.RegisterMember("page", &UiMenu::m_iPage);
        l_oS.RegisterMember("main_rows", &UiMenu::m_vMainRows);
        l_oS.RegisterMember("header", &UiMenu::m_strHeader);
        l_oS.RegisterMember("subheader", &UiMenu::m_strSubheader);
        l_oS.RegisterMember("rows", &UiMenu::m_vRows);
        l_oS.RegisterMember("close_label", &UiMenu::m_strCloseLabel);
        l_oS.RegisterMember("confirm_open", &UiMenu::m_bConfirmOpen);
        l_oS.RegisterMember("confirm_question", &UiMenu::m_strConfirmQuestion);
        l_oS.RegisterMember("confirm_body", &UiMenu::m_strConfirmBody);
        l_oS.RegisterMember("yes_label", &UiMenu::m_strYesLabel);
        l_oS.RegisterMember("no_label", &UiMenu::m_strNoLabel);
        l_oS.RegisterMember("confirm_yes", &UiMenu::m_bConfirmYes);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiNotice>()) {
        l_oS.RegisterMember("visible", &UiNotice::m_bVisible);
        l_oS.RegisterMember("title", &UiNotice::m_strTitle);
        l_oS.RegisterMember("body", &UiNotice::m_strBody);
        l_oS.RegisterMember("ok", &UiNotice::m_strOk);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiStairs>()) {
        l_oS.RegisterMember("visible", &UiStairs::m_bVisible);
        l_oS.RegisterMember("question", &UiStairs::m_strQuestion);
        l_oS.RegisterMember("yes_label", &UiStairs::m_strYesLabel);
        l_oS.RegisterMember("no_label", &UiStairs::m_strNoLabel);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiStageSelect>()) {
        l_oS.RegisterMember("visible", &UiStageSelect::m_bVisible);
        l_oS.RegisterMember("title", &UiStageSelect::m_strTitle);
        l_oS.RegisterMember("hint", &UiStageSelect::m_strHint);
        l_oS.RegisterMember("close_label", &UiStageSelect::m_strCloseLabel);
        l_oS.RegisterMember("rows", &UiStageSelect::m_vRows);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiEnding>()) {
        l_oS.RegisterMember("visible", &UiEnding::m_bVisible);
        l_oS.RegisterMember("name", &UiEnding::m_strName);
        l_oS.RegisterMember("text", &UiEnding::m_strText);
        l_oS.RegisterMember("hint", &UiEnding::m_strHint);
        l_oS.RegisterMember("rebirth", &UiEnding::m_bRebirth);
        l_oS.RegisterMember("rebirth_label", &UiEnding::m_strRebirthLabel);
        l_oS.RegisterMember("title_label", &UiEnding::m_strTitleLabel);
    }
    if (auto l_oS = l_oC.RegisterStruct<UiOverlay>()) {
        l_oS.RegisterMember("toast", &UiOverlay::m_bToast);
        l_oS.RegisterMember("toast_text", &UiOverlay::m_strToastText);
        l_oS.RegisterMember("notifications", &UiOverlay::m_vNotifications);
    }
}

}  // namespace

bool GameUi::f_Init(Rml::Context* l_pCtx, Game* l_pGame, const std::string& l_strUiDir, std::string& l_strError) {
    m_pCtx = l_pCtx;
    m_pGame = l_pGame;
    // generic_string: RmlUi joins relative URLs (images, stylesheets) on '/' only.
    m_strDir = std::filesystem::path(l_strUiDir).generic_string();

    Rml::DataModelConstructor l_oC = m_pCtx->CreateDataModel("game");
    if (!l_oC) { l_strError = "RmlUi: could not create the \"game\" data model"; return false; }
    m_bModel = true;
    f_RegisterTypes(l_oC);
    l_oC.Bind("title", &m_oState.m_oTitle);
    l_oC.Bind("hud", &m_oState.m_oHud);
    l_oC.Bind("battle", &m_oState.m_oBattle);
    l_oC.Bind("dialogue", &m_oState.m_oDialogue);
    l_oC.Bind("player", &m_oState.m_oPlayer);
    l_oC.Bind("saveload", &m_oState.m_oSaveload);
    l_oC.Bind("store", &m_oState.m_oStore);
    l_oC.Bind("menu", &m_oState.m_oMenu);
    l_oC.Bind("stairs", &m_oState.m_oStairs);
    l_oC.Bind("notice", &m_oState.m_oNotice);
    l_oC.Bind("stage_select", &m_oState.m_oStageSelect);
    l_oC.Bind("ending", &m_oState.m_oEnding);
    l_oC.Bind("minigame", &m_oState.m_oMinigame);
    l_oC.Bind("overlay", &m_oState.m_oOverlay);
    // act('name') / act('name', n): the one event every button uses. The innermost handler wins
    // (a choice inside the dialogue box must not also count as a click on the box).
    l_oC.BindEventCallback("act", [this](Rml::DataModelHandle, Rml::Event& l_oEv, const Rml::VariantList& l_vArgs) {
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
    if (m_pCtx && m_bModel) m_pCtx->RemoveDataModel("game");
    m_bModel = false;
    m_pCtx = nullptr;
}

void GameUi::f_Reload() {
    if (!m_pCtx) return;
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
    m_pGame->f_BuildUiState(l_oNext);
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
        const glm::vec4 l_v4Pb = l_fnBox(m_vDocs[l_nI].m_pDoc->GetElementById("player"));
        if (l_v4Pb.z > 0 && !l_vFoes.empty() && l_vFoes[0].z > 0) m_pGame->f_SetBattleBoxes(l_v4Pb, l_vFoes);
    }
    l_fnTake(m_oState.m_oDialogue, l_oNext.m_oDialogue);
    l_fnTake(m_oState.m_oPlayer, l_oNext.m_oPlayer);
    l_fnTake(m_oState.m_oSaveload, l_oNext.m_oSaveload);
    l_fnTake(m_oState.m_oMenu, l_oNext.m_oMenu);
    l_fnTake(m_oState.m_oStairs, l_oNext.m_oStairs);
    l_fnTake(m_oState.m_oNotice, l_oNext.m_oNotice);
    l_fnTake(m_oState.m_oStageSelect, l_oNext.m_oStageSelect);
    l_fnTake(m_oState.m_oEnding, l_oNext.m_oEnding);
    l_fnTake(m_oState.m_oMinigame, l_oNext.m_oMinigame);   // a reward event (docs/gameplay/07 phase 6)
    if (l_oNext.m_oStore.m_bVisible || l_oNext.m_oStore.m_bUnlockedDialog) m_oState.m_oStore = std::move(l_oNext.m_oStore);
    else { m_oState.m_oStore.m_bVisible = false; m_oState.m_oStore.m_bUnlockedDialog = false; }
    m_oState.m_oOverlay = std::move(l_oNext.m_oOverlay);
    // Everything is re-evaluated each frame: RmlUi only touches elements whose value changed, and
    // data-for keeps its existing elements, so this is cheap and never restarts a transition.
    if (Rml::DataModelHandle l_oH = m_pCtx->GetDataModel("game").GetModelHandle()) l_oH.DirtyAllVariables();
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
            for (Rml::Element* l_pSel : {l_oD.m_pDoc->QuerySelector(".cell.selected"), l_oD.m_pDoc->QuerySelector(".evrow.selected"),
                                      l_oD.m_pDoc->QuerySelector(".attr.selected"), l_oD.m_pDoc->QuerySelector("#sys-page .row.selected")})
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

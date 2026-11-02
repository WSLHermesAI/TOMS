#pragma once
// ui_state.h -- everything the game's UI shows, as plain data.
//
// Game::buildUiState() fills one UiState per frame from the game's own state (all text already
// translated), and the host binds it to the RmlUi documents in assets/media/ui/*.rml (data model
// "game", see src/game/src/game_ui.cpp). Buttons in those documents call Game::uiEvent(name, arg).
// So Game keeps every rule and every string, the .rml/.rcss files own every pixel, and this
// header is the contract between them. No RmlUi (or renderer) types here: the core code stays
// free of UI-library dependencies.
//
// Naming: members are snake_case because they are the names used inside the .rml files
// ({{hud.hp_text}}, data-if="battle.active", ...).
#include <string>
#include <vector>

namespace toms {

// One selectable row / button in a list (title pages, menus, dialogue choices, stage select).
struct UiRow {
    std::string m_strLabel;
    std::string m_strSub;          // second, dimmer line ("" = none)
    bool m_bSelected = false;    // keyboard cursor / hover
    bool m_bEnabled = true;      // false = shown dimmed (locked skill, unaffordable recipe, ...)
    std::string m_strSection;      // a heading drawn above this row ("" = none): Settings' Language / Art style
    bool m_bExtra = false;       // the row's second button shows (the Status tab: - takes a draft point back)
};

// A world-anchored name plate over a boss or roaming monster (design-space position).
struct UiBanner {
    std::string m_strText;
    float m_fX = 0, m_fY = 0;       // centre of the plate's bottom edge
};

struct UiTitle {
    bool m_bVisible = false;
    bool m_bScene = false;                // a 3D scene is drawn behind the title (the host's TitleScene): see-through body
    std::string m_strTitle, m_strSubtitle, m_strHint;
    int m_iPage = 0;                      // 0 menu, 1 continue (save slots), 2 settings (languages)
    std::string m_strHeader, m_strSubheader;     // page heading ("" on the menu page)
    std::vector<UiRow> m_vRows;           // the current page's rows
    bool m_bShowBack = false;
    std::string m_strBackLabel;
    bool m_bShowEmptyHint = false;      // continue page with no saves at all
    std::string m_strEmptyHint;
    // "Start a new game in this slot?" / "Switch to <language>?"
    bool m_bConfirmOpen = false;
    std::string m_strConfirmQuestion, m_strConfirmBody, m_strYesLabel, m_strNoLabel;
    bool m_bConfirmYes = true;           // which answer the keyboard cursor is on
};

struct UiHud {
    bool m_bVisible = false;              // the walking scene (map) is on screen
    std::string m_strTitleLine;            // "魔法塔 — <floor> (n/70)"
    float m_fHpPct = 0;                  // 0..100
    std::string m_strHpText, m_strStatsLine, m_strResLine, m_strFooter;
    // The walking scene's one button, ≡: it opens the player menu (docs/gameplay/03_PLAYER_MENU.md).
    std::string m_strMenuLabel;
    bool m_bMenuNews = false;            // a dot on it: something new inside (store, gear, events, skill points)
    bool m_bChapterCard = false;
    std::string m_strChapterTitle, m_strChapterSub;
    float m_fChapterAlpha = 0;           // 0..1, fades out
    std::vector<UiBanner> m_vBanners;
};

struct UiPowerBar {                    // a Battle v2 timing bar; zones and marker in percent
    std::string m_strPrompt, m_strButtonLabel;
    bool m_bCooling = false;
    float m_fRedL = 0, m_fBlueL = 0, m_fGreen = 0, m_fBlueR = 0, m_fRedR = 0;   // zone widths (sum 100)
    float m_fMarker = 0;                  // 0..100
};

struct UiComboPoint {                  // one point of the combo track
    float m_fPos = 0;                     // 0..100
    bool m_bHit = false;
    bool m_bNear = false;                 // the marker is in its window: a tap now hits it (the hint)
};

struct UiCombo {                       // Battle v3's point play (docs/gameplay/07 phase 1): the charge, the track, its points
    std::string m_strPrompt, m_strButtonLabel, m_strLevelText;
    bool m_bRunning = false, m_bCooling = false, m_bWaiting = false;
    float m_fChargePct = 0;              // 0..100
    float m_fMarker = 0;                  // 0..100
    std::vector<UiComboPoint> m_vPoints;
};

struct UiMiniGame {                    // a reward event (docs/gameplay/07 phase 6): its text, the point track, the result
    bool m_bVisible = false;
    std::string m_strTitle, m_strText, m_strPrompt, m_strButtonLabel, m_strResult, m_strCloseLabel;
    bool m_bRunning = false, m_bDone = false;
    float m_fMarker = 0;                  // 0..100
    std::vector<UiComboPoint> m_vPoints;
};

struct UiPopup {                       // a damage number rising over a fighter (docs/gameplay/07_GAMEPLAY_PLAN.md phase 0)
    std::string m_strText;
    float m_fX = 0, m_fY = 0;                // design px: its box's left / top
    float m_fOpacity = 1;
    std::string m_strStyle = "hit";         // battle.rcss .popup.<style>: hit great perfect miss guard hurt, and the words
                                       // parry block early chain move-<tier> (docs/gameplay/15 §3.8)
};

struct UiTouchItem {                   // one piece of a weapon's own play (touch_play.h): a target, the aim, an arrow, a circle
    std::string m_strCls;                   // battle.rcss .tp.<cls>
    float m_fX = 0, m_fY = 0, m_fW = 0, m_fH = 0;  // design px: its box (it turns about its middle)
    float m_fRot = 0;                     // degrees, clockwise
    float m_fRadius = 0;                  // px: round corners (a ring: half its size)
    float m_fOpacity = 1;
    std::string m_strText;
};

struct UiFoe {                         // one monster of the battle (docs/gameplay/13 §4.6)
    std::string m_strSprite;                // its still picture (_atlas.rcss), when the game does not draw it
    bool m_bAnim = false;                 // the game draws it (monster_battle.anim): no picture
    float m_fHpPct = 0;
    std::string m_strHpText;               // "Slime HP 24" (one monster: one line)
    std::string m_strName, m_strHpLine;         // "Slime" / "HP 24" (two or three: two lines, the slots are narrow)
    bool m_bTarget = false;               // the player's attacks go to it (shown only with two or more)
    bool m_bDown = false;
    // its next move (docs/gameplay/13 §6.1): the words and the time left, and how far its wind-up is
    std::string m_strIntent;
    std::string m_strIntentKind;           // strike, heavy, jabs, spell, guard; stagger / broken while stunned
    float m_fIntentPct = 0;
    float m_fBreakPct = 0;               // its break gauge (§6.4)
    bool m_bWarn = false;                 // the "!" over it: its hit comes, guard now (docs/gameplay/16 §4)
};

struct UiBattle {
    bool m_bVisible = false;
    bool m_bActive = false;               // fighting (false: won, showing "tap to continue")
    std::string m_strTitle, m_strPlayerSprite, m_strEnemySprite;   // sprite names (_atlas.rcss)
    bool m_bPlayerAnim = false;          // the game draws the player (a weapon's battle clips): no picture
    bool m_bEnemyAnim = false;           // the game draws the monster (monster_battle.anim): no picture
    float m_fPlayerHpPct = 0, m_fEnemyHpPct = 0;
    std::string m_strPlayerHpText, m_strEnemyHpText, m_strLog, m_strContinueHint;
    std::string m_strClockLabel;
    float m_fClockPct = 0;
    UiPowerBar m_oAttack, m_oDefense;            // (attack: Battle v2's bar, no longer shown; combo replaced it)
    UiCombo m_oCombo;
    std::string m_strShieldText;
    bool m_bShieldArmed = false;
    bool m_bCritArmed = false;
    std::string m_strCritHint;
    std::string m_strSuperLabel;
    std::vector<int> m_vSuperDots;       // 1 = charged
    bool m_bSuperReady = false;
    std::string m_strSuperButton;
    bool m_bActiveReady = false;
    std::string m_strActiveButton;
    std::vector<UiPopup> m_vPopups;
    std::string m_strTouchKind;            // the weapon's own play: bow, slice, circles ("" = the track; docs/gameplay/15 §3.3)
    std::vector<UiTouchItem> m_vTouch;    // what it draws over the monster
    std::string m_strSkillPrompt, m_strSkillTime;   // a move's play: "Draw the pentagram!", its seconds left (16 §6)
    std::vector<UiFoe> m_vFoes;           // 1..3; foes[0] is enemy_* above
    // ---- the revamp (docs/gameplay/15_BATTLE_REVAMP.md) ----
    std::string m_strBanner, m_strBannerKind;   // across the screen: break / line / ultimate
    float m_fBannerOpacity = 0, m_fBannerShift = 0;   // its slide in and out (px)
    std::string m_strChainText;            // "Chain ×3" (§3.4)
    bool m_bGuardTiming = true;          // one guard button (§3.2); false: the five-zone bar (an option)
    bool m_bGuardLit = false, m_bGuardCooling = false;
    std::string m_strGuardLabel, m_strGuardHint;
    std::string m_strAttackState;          // ready / running / recovering / short: the attack is the play, this only shows it
    std::string m_strAttackLabel, m_strAttackHint;
    float m_fAttackPct = 0;              // recovering: how far
    bool m_bShowClock = false;           // the stamina pace's bar (the global enemy clock is gone: §4)
    bool m_bResultShown = false, m_bGradeS = false;   // the result screen (§3.6)
    std::string m_strGrade, m_strResultTitle, m_strResultStats, m_strResultWeapon, m_strResultProgress, m_strResultNext, m_strResultGold, m_strResultCard,
                m_strResultLevelup;
    float m_fResultPct = 0;
    bool m_bStaminaPace = false;         // the stamina pace (13 §6.3): the clock row shows the stamina
    bool m_bAttackShort = false, m_bGuardShort = false;   // not enough stamina for it
};

struct UiDialogue {
    bool m_bVisible = false;
    std::string m_strSpeaker;               // the NPC's name (its NPC file); empty: no name line
    std::string m_strText;
    std::vector<UiRow> m_vChoices;
    float m_fScale = 1.0f;                // Game::setUiScale (phones)
    float m_fSide = 40.0f;                // the box's left/right margin in design px, so that scaled it still fits the screen
};

// ---- the player menu (player.rml): Status / Equipment / Items / Events / System ----
struct UiTab {                         // a tab, or a filter chip
    std::string m_strLabel;
    bool m_bActive = false;
    bool m_bNews = false;                 // a dot: something new on it
};

struct UiCell {                        // one cell of a 3-per-row grid (Items, Equipment)
    std::string m_strIcon, m_strName, m_strBadge;     // icon: a sprite name; badge: "x2" / ""
    bool m_bSelected = false, m_bWorn = false, m_bLocked = false;   // locked: an important item (cannot be dropped)
};

struct UiLine {                        // "label  value" (and, comparing gear, "-> extra")
    std::string m_strLabel, m_strValue, m_strExtra;
    int m_iTone = 0;                      // the comparison: 1 better, -1 worse, 0 the same / not compared
};

struct UiButton {
    std::string m_strLabel;
    bool m_bEnabled = true;
};

struct UiPlayer {
    bool m_bVisible = false;
    int m_iTab = 0;                       // 0 status, 1 equipment, 2 items, 3 events, 4 system
    std::vector<UiTab> m_vTabs;
    std::string m_strHint, m_strCloseLabel;
    // Status
    std::string m_strLevel, m_strHpText, m_strExpText;
    float m_fHpPct = 0, m_fExpPct = 0;
    std::string m_strCombatTitle, m_strAttrTitle, m_strAttrDesc;
    std::string m_strAttrPer, m_strAttrNow;    // the selected attribute's effect: per point / in total now ("" = none)
    std::string m_strAttrPoints;           // "Points to spend: 2" ("" = none to spend)
    std::string m_strAttrPointsHint;
    bool m_bAttrCanAdd = false;         // the rows show a + button
    bool m_bAttrDraft = false;           // points placed, not confirmed: Confirm / Reset show
    std::string m_strAttrConfirm, m_strAttrReset;
    bool m_bAttrConfirmSel = false, m_bAttrResetSel = false;   // the keyboard cursor is on Confirm / Reset
    std::vector<UiLine> m_vCombat;        // ATK, DEF, Gold, Keys, Floor, Chapter
    std::vector<UiLine> m_vMore;          // Weapon, Armor, Skills, Memory shards, State of mind
    std::vector<UiRow> m_vAttrs;          // label "STR  力量", sub = the value
    // Equipment and Items: filter chips, the grid, the selected entry
    std::vector<UiTab> m_vFilters;
    std::string m_strWornTitle;
    std::vector<UiLine> m_vWorn;          // Equipment: what each slot wears (click = that slot)
    std::vector<UiCell> m_vCells;
    std::string m_strEmptyText;            // the grid is empty
    bool m_bDetail = false;               // an entry is selected
    std::string m_strSelectHint;           // ... and when none is
    std::string m_strDIcon, m_strDName, m_strDDesc, m_strDNote;
    std::vector<UiLine> m_vDLines;       // Effect, Price, You have
    bool m_bDCompare = false;            // gear: compared with what is worn now
    std::string m_strDNowLabel, m_strDWithLabel;
    std::vector<UiLine> m_vDCompareLines;
    std::vector<UiButton> m_vDButtons;   // Use / Equip / Unequip / Drop
    // "Drop <item>?"
    bool m_bConfirmOpen = false;
    std::string m_strConfirmQuestion, m_strYesLabel, m_strNoLabel;
    bool m_bConfirmYes = false;
    // Events (its own lists: every tab's data is there every frame, so a hidden tab never reads a
    // list that another tab emptied)
    std::vector<UiTab> m_vEvFilters;     // Unfinished (n) / All (n)
    std::vector<UiRow> m_vEvRows;        // label: the mark and the title; sub: "done"; enabled=false: finished (greyed)
    bool m_bEvDetail = false;
    std::string m_strEvEmpty, m_strEvHint;
    std::string m_strEvKind, m_strEvTitle, m_strEvDesc, m_strEvConnectedTitle;
    std::vector<std::string> m_vEvConnected;
    // System: its rows and pages are menu.* (UiMenu)
    std::string m_strSysHint;
};

// ---- Save / Load (saveload.rml), opened from the player menu's System tab ----
struct UiSlot {
    std::string m_strTitle;                 // "Slot 1"
    std::string m_strLine1, m_strLine2;          // floor, level, HP, gold / saved at, play time ("" when empty)
    std::string m_strTag;                   // "This run" on the run's own slot
    bool m_bEmpty = true, m_bSelected = false, m_bEnabled = true;   // enabled=false: an empty slot on the Load tab
};

struct UiSaveLoad {
    bool m_bVisible = false;
    std::string m_strTitle, m_strHint, m_strCloseLabel;
    std::vector<UiTab> m_vModes;          // Save / Load
    std::vector<UiSlot> m_vCards;         // one per save slot (not "slots": a Qt macro in the editor build)
    // "Save to slot 2?" / "Overwrite slot 2?" / "Load slot 2?"
    bool m_bConfirmOpen = false;
    std::string m_strConfirmQuestion, m_strConfirmBody, m_strYesLabel, m_strNoLabel;
    bool m_bConfirmYes = false;
};

struct UiStoreTab { std::string m_strLabel; bool m_bActive = false; };
struct UiStoreItem {
    std::string m_strIcon, m_strName, m_strDesc, m_strEffect, m_strStatus, m_strPrice;   // icon: a sprite name (_atlas.rcss)
    bool m_bSelected = false, m_bEquipped = false;
};

struct UiStore {
    bool m_bVisible = false;
    std::string m_strTitle, m_strHint, m_strGoldLabel, m_strCloseLabel, m_strEffectLabel, m_strPriceLabel, m_strBuyLabel;
    int m_iGold = 0;
    std::vector<UiStoreTab> m_vTabs;
    std::vector<UiStoreItem> m_vItems;
    // "Store unlocked!" popup (shown once, when the store unlocks)
    bool m_bUnlockedDialog = false;
    std::string m_strUnlockedTitle, m_strUnlockedBody, m_strUnlockedOk;
};

struct UiMenu {                        // the player menu's System tab (the old in-game menu)
    bool m_bVisible = false;
    int m_iPage = 0;                      // 0 main, 1 settings, 2 skills, 3 forge, 4 village
    std::vector<UiRow> m_vMainRows;      // the left list: Store, Skills, ... Back to title (always)
    std::string m_strHeader, m_strSubheader;     // the open page (page != 0), shown on the right
    std::vector<UiRow> m_vRows;           // its rows
    std::string m_strCloseLabel;           // main page: Close; sub-pages: Back
    bool m_bConfirmOpen = false;         // "Switch to <language>?" / "Switch the art style to ...?"
    std::string m_strConfirmQuestion, m_strConfirmBody, m_strYesLabel, m_strNoLabel;
    bool m_bConfirmYes = true;
};

struct UiStairs {
    bool m_bVisible = false;
    std::string m_strQuestion, m_strYesLabel, m_strNoLabel;
};

// Q7: an OK popup ("Stairs unlocked" ...), dialogs.rml.
struct UiNotice {
    bool m_bVisible = false;
    std::string m_strTitle, m_strBody, m_strOk;
};

struct UiStageSelect {
    bool m_bVisible = false;
    std::string m_strTitle, m_strHint, m_strCloseLabel;
    std::vector<UiRow> m_vRows;           // enabled=false: locked (sub = why / preview text)
};

struct UiEnding {
    bool m_bVisible = false;
    std::string m_strName, m_strText, m_strHint;
    bool m_bRebirth = false;              // two buttons instead of "tap to continue"
    std::string m_strRebirthLabel, m_strTitleLabel;
};

struct UiOverlay {                     // always on top
    bool m_bToast = false;                // store purchase / "not enough gold" / "saved"
    std::string m_strToastText;
    std::vector<std::string> m_vNotifications;   // level up, mission available, floor events, ...
};

struct UiState {
    UiTitle m_oTitle;
    UiHud m_oHud;
    UiBattle m_oBattle;
    UiDialogue m_oDialogue;
    UiPlayer m_oPlayer;
    UiSaveLoad m_oSaveload;
    UiStore m_oStore;
    UiMenu m_oMenu;
    UiStairs m_oStairs;
    UiNotice m_oNotice;
    UiStageSelect m_oStageSelect;
    UiEnding m_oEnding;
    UiMiniGame m_oMinigame;
    UiOverlay m_oOverlay;
};

}  // namespace toms

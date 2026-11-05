// bed_window.h -- battle_editor: tune the battle beside the running game (docs/gameplay/13_BATTLE_EDITOR.md).
//   left:   the monsters (search; tick "+ Battle" for up to three; New, Duplicate, Delete)
//   middle: the battle bar (Battle / Restart, Pause, speed, the pace, Win / Lose now, the view's shape) over the game
//   right:  Monster | Weapon | Player (with the growth calculator) | Events | Log | Scenarios (every test scenario,
//           double-click to play it in the game view; docs/gameplay/08)
//   bottom: the test setups (tools/battle_editor/setups/<name>.json: editor files, not tests)
// Saving writes the game's own files: data/enemies.json, data/weapon_play.json, data/stats.json.
#pragma once
#include "bed_logic.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QTimer>

class BgfxViewport;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTreeWidget;
class AspectBox;

class BattleEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    BattleEditorWindow();

    // ---- for the self-test and the smoke test ----
    void f_SetQuiet(bool l_bQ) { m_bQuiet = l_bQ; }          // no message boxes
    QString f_DataDir() const { return m_oDataDir; }
    QString f_SetupsDir() const { return m_oSetupsDir; }
    int f_MonsterCount() const;
    bool f_SetInBattle(const QString& l_oId, bool l_bOn);  // the "+ Battle" tick (off: every copy of it leaves)
    bool f_AddToBattle(const QString& l_oId);           // "+ Again": one more of it (three bats), up to three in all
    QStringList f_InBattle() const { return m_oInBattle; }
    bool f_SelectMonster(const QString& l_oId);
    bool f_SelectWeapon(const QString& l_oKey);                                       // on the Weapon tab
    bool f_SetWeaponLevelCell(int l_iLv, const QString& l_oColumn, int l_iValue);     // "points" or an attribute id, as typed
    QString f_WeaponLevelsJson(const QString& l_oKey) const;                          // its "levels" as they stand, not saved
    bool f_GrabWeaponTab(const QString& l_oPng);                                      // a picture of the tab (no window needed)
    QString f_CurrentMonster() const { return m_oCurrent; }
    void f_SetMonsterNumber(const QString& l_oKey, int l_iValue);   // hp, atk, def, ... as typed in the form
    bed::Setup f_CurrentSetup() const;
    bool f_StartBattle();                             // ▶ Battle (and Restart)
    BgfxViewport* f_Viewport() const { return m_pView; }
    void f_RefreshLog();
    QString f_LogText() const;
    bool f_SaveSetupAs(const QString& l_oName);
    bool f_LoadSetup(const QString& l_oName);
    bool f_IsDirty() const { return m_bMonstersDirty || m_bWeaponsDirty; }
    static constexpr int m_iAutoRestartMs = 1500;   // the end of a battle stays on screen this long
    void f_SetPace(const QString& l_oPace);              // "", "cooldown", "stamina"
    // the growth calculator's table for (total, level, k), as shown
    std::vector<int> f_Calculate(int l_iTotal, int l_iLevel, double l_dK);
    // the Scenarios tab: start a scenario (a folder with scenario.json, or the file) in the game view; how many are listed
    bool f_PlayScenario(const QString& l_oPath);
    int f_ScenarioCount() const;

protected:
    void closeEvent(class QCloseEvent* l_pE) override;

private:
    // building the window
    QWidget* f_BuildMonsterList();
    QWidget* f_BuildCentre();
    QWidget* f_BuildMonsterTab();
    QWidget* f_BuildWeaponTab();
    QWidget* f_BuildPlayerTab();
    QWidget* f_BuildEventsTab();
    QWidget* f_BuildLogTab();
    QWidget* f_BuildScenariosTab();
    void f_FillScenarioList();
    QString f_ScenarioRoot() const;   // the repo's tests/scenarios
    QWidget* f_BuildSetupsBar();

    // the data files
    bool f_LoadData();
    bool f_SaveMonsters();
    bool f_SaveWeapons();
    bool f_SaveGrowthTable();
    void f_FillMonsterList();
    void f_ShowMonster(const QString& l_oId);
    void f_MonsterEdited();                // the form -> enemies_[current_], and the running battle
    void f_ApplyToRunningBattle(const QString& l_oId);
    void f_ShowWeapon(const QString& l_oKey);
    void f_ShowLevelExp();                  // the table's EXP column, from levelExp
    void f_WeaponEdited();
    void f_FillSetups();
    void f_ShowEvents();
    void f_ApplyEventsText();
    void f_UpdateInBattleLabel();
    void f_UpdateReadout();
    void f_Say(const QString& l_oText);       // the status line
    bool f_Ask(const QString& l_oTitle, const QString& l_oText);
    void f_Warn(const QString& l_oTitle, const QString& l_oText);

    QString m_oDataDir, m_oSetupsDir, m_oScratchDir, m_oFxFile;
    bed::ojson m_jEnemies, m_jWeaponPlay;
    QStringList m_oEffects;                // battle.particle's effect names (a setting naming another is shown red)
    QStringList m_oAttrIds;                // stats.json attributes
    QString m_oCurrent, m_oCurrentWeapon;
    QStringList m_oInBattle;
    bool m_bMonstersDirty = false, m_bWeaponsDirty = false;
    bool m_bFilling = false;               // a form being filled: its signals are not edits
    bool m_bQuiet = false;
    int m_iLogShown = -1;

    // widgets
    BgfxViewport* m_pView = nullptr;
    AspectBox* m_pViewBox = nullptr;
    QLineEdit* m_pSearch = nullptr;
    QListWidget* m_pMonsters = nullptr;
    QLabel* m_pInBattleLabel = nullptr;
    QTabWidget* m_pTabs = nullptr;
    QTreeWidget* m_pScenarioList = nullptr;
    QString m_oExtraScenarioDir;           // "Open scenario folder...": a folder of scenarios outside tests/
    QComboBox* m_pSpeed = nullptr;
    QComboBox* m_pPace = nullptr;
    QComboBox* m_pShape = nullptr;
    QCheckBox* m_pAutoRestart = nullptr;   // a battle that ends starts again (after kAutoRestartMs)
    bool m_bBattleRunning = false;         // seen running at the last check (refreshLog)
    bool m_bRestartPending = false;
    QPushButton* m_pPause = nullptr;
    // monster
    QLabel* m_pMId = nullptr;
    QLineEdit* m_pMNameEn = nullptr;
    QLineEdit* m_pMNameZh = nullptr;
    QLineEdit* m_pMSprite = nullptr;
    QLineEdit* m_pMLook = nullptr;
    QLineEdit* m_pMMoves = nullptr;
    QLineEdit* m_pMParty = nullptr;
    QCheckBox* m_pMBoss = nullptr;
    QMap<QString, QSpinBox*>* m_pMNumbers = nullptr;   // hp, atk, def, exp, gold, atk_interval_ms, break
    QLabel* m_pReadout = nullptr;
    // weapon
    QListWidget* m_pWeapons = nullptr;
    QMap<QString, QSpinBox*>* m_pWNumbers = nullptr;   // sweepMs, chargeMs, hitWindowMs, lateMs
    QComboBox* m_pWPace = nullptr;
    QTableWidget* m_pResults = nullptr;
    QLabel* m_pWLabel = nullptr;
    QTableWidget* m_pWLevels = nullptr;     // each level: its EXP to the next, points, attribute bonuses
    QSpinBox* m_pWExpFirst = nullptr;       // levelExp: Lv 1 to 2
    QDoubleSpinBox* m_pWExpScale = nullptr; // levelExp: each next level this many times the last
    // player
    QSpinBox* m_pPLv = nullptr;
    QSpinBox* m_pPHp = nullptr;
    QSpinBox* m_pPMaxHp = nullptr;
    QComboBox* m_pPWeapon = nullptr;
    QSpinBox* m_pPWeaponLevel = nullptr;
    QSpinBox* m_pPPoints = nullptr;
    QMap<QString, QSpinBox*>* m_pPAttrs = nullptr;
    QSpinBox* m_pCTotal = nullptr;
    QSpinBox* m_pCLevel = nullptr;
    QDoubleSpinBox* m_pCShape = nullptr;
    QTableWidget* m_pCTable = nullptr;
    QLabel* m_pCSum = nullptr;
    // events
    QLabel* m_pEvMonster = nullptr;
    QPlainTextEdit* m_pEvText = nullptr;
    QListWidget* m_pEvList = nullptr;
    // log
    QPlainTextEdit* m_pLog = nullptr;
    // setups
    QComboBox* m_pSetups = nullptr;
    QTimer m_oLogTimer;
};

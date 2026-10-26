// bed_window.h -- battle_editor: tune the battle beside the running game (docs/gameplay/13_BATTLE_EDITOR.md).
//   left:   the monsters (search; tick "+ Battle" for up to three; New, Duplicate, Delete)
//   middle: the battle bar (Battle / Restart, Pause, speed, the pace, Win / Lose now, the view's shape) over the game
//   right:  Monster | Weapon | Player (with the growth calculator) | Events | Log
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
class AspectBox;

class BattleEditorWindow : public QMainWindow {
    Q_OBJECT
public:
    BattleEditorWindow();

    // ---- for the self-test and the smoke test ----
    void setQuiet(bool q) { quiet_ = q; }          // no message boxes
    QString dataDir() const { return dataDir_; }
    QString setupsDir() const { return setupsDir_; }
    int monsterCount() const;
    bool setInBattle(const QString& id, bool on);  // the "+ Battle" tick (off: every copy of it leaves)
    bool addToBattle(const QString& id);           // "+ Again": one more of it (three bats), up to three in all
    QStringList inBattle() const { return inBattle_; }
    bool selectMonster(const QString& id);
    QString currentMonster() const { return current_; }
    void setMonsterNumber(const QString& key, int value);   // hp, atk, def, ... as typed in the form
    bed::Setup currentSetup() const;
    bool startBattle();                             // ▶ Battle (and Restart)
    BgfxViewport* viewport() const { return view_; }
    void refreshLog();
    QString logText() const;
    bool saveSetupAs(const QString& name);
    bool loadSetup(const QString& name);
    bool isDirty() const { return monstersDirty_ || weaponsDirty_; }
    static constexpr int kAutoRestartMs = 1500;   // the end of a battle stays on screen this long
    void setPace(const QString& pace);              // "", "cooldown", "stamina"
    // the growth calculator's table for (total, level, k), as shown
    std::vector<int> calculate(int total, int level, double k);

protected:
    void closeEvent(class QCloseEvent* e) override;

private:
    // building the window
    QWidget* buildMonsterList();
    QWidget* buildCentre();
    QWidget* buildMonsterTab();
    QWidget* buildWeaponTab();
    QWidget* buildPlayerTab();
    QWidget* buildEventsTab();
    QWidget* buildLogTab();
    QWidget* buildSetupsBar();

    // the data files
    bool loadData();
    bool saveMonsters();
    bool saveWeapons();
    bool saveGrowthTable();
    void fillMonsterList();
    void showMonster(const QString& id);
    void monsterEdited();                // the form -> enemies_[current_], and the running battle
    void applyToRunningBattle(const QString& id);
    void showWeapon(const QString& key);
    void weaponEdited();
    void fillSetups();
    void showEvents();
    void applyEventsText();
    void updateInBattleLabel();
    void updateReadout();
    void say(const QString& text);       // the status line
    bool ask(const QString& title, const QString& text);
    void warn(const QString& title, const QString& text);

    QString dataDir_, setupsDir_, scratchDir_, fxFile_;
    bed::ojson enemies_, weaponPlay_;
    QStringList effects_;                // battle.particle's effect names (a setting naming another is shown red)
    QStringList attrIds_;                // stats.json attributes
    QString current_, currentWeapon_;
    QStringList inBattle_;
    bool monstersDirty_ = false, weaponsDirty_ = false;
    bool filling_ = false;               // a form being filled: its signals are not edits
    bool quiet_ = false;
    int logShown_ = -1;

    // widgets
    BgfxViewport* view_ = nullptr;
    AspectBox* viewBox_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* monsters_ = nullptr;
    QLabel* inBattleLabel_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QComboBox* speed_ = nullptr;
    QComboBox* pace_ = nullptr;
    QComboBox* shape_ = nullptr;
    QCheckBox* autoRestart_ = nullptr;   // a battle that ends starts again (after kAutoRestartMs)
    bool battleRunning_ = false;         // seen running at the last check (refreshLog)
    bool restartPending_ = false;
    QPushButton* pause_ = nullptr;
    // monster
    QLabel* mId_ = nullptr;
    QLineEdit* mNameEn_ = nullptr;
    QLineEdit* mNameZh_ = nullptr;
    QLineEdit* mSprite_ = nullptr;
    QLineEdit* mLook_ = nullptr;
    QLineEdit* mMoves_ = nullptr;
    QLineEdit* mParty_ = nullptr;
    QCheckBox* mBoss_ = nullptr;
    QMap<QString, QSpinBox*>* mNumbers_ = nullptr;   // hp, atk, def, exp, gold, atk_interval_ms, break
    QLabel* readout_ = nullptr;
    // weapon
    QListWidget* weapons_ = nullptr;
    QMap<QString, QSpinBox*>* wNumbers_ = nullptr;   // sweepMs, chargeMs, hitWindowMs, lateMs
    QComboBox* wPace_ = nullptr;
    QTableWidget* results_ = nullptr;
    QLabel* wLabel_ = nullptr;
    // player
    QSpinBox* pLv_ = nullptr;
    QSpinBox* pHp_ = nullptr;
    QSpinBox* pMaxHp_ = nullptr;
    QComboBox* pWeapon_ = nullptr;
    QSpinBox* pWins_ = nullptr;
    QSpinBox* pPoints_ = nullptr;
    QMap<QString, QSpinBox*>* pAttrs_ = nullptr;
    QSpinBox* cTotal_ = nullptr;
    QSpinBox* cLevel_ = nullptr;
    QDoubleSpinBox* cShape_ = nullptr;
    QTableWidget* cTable_ = nullptr;
    QLabel* cSum_ = nullptr;
    // events
    QLabel* evMonster_ = nullptr;
    QPlainTextEdit* evText_ = nullptr;
    QListWidget* evList_ = nullptr;
    // log
    QPlainTextEdit* log_ = nullptr;
    // setups
    QComboBox* setups_ = nullptr;
    QTimer logTimer_;
};

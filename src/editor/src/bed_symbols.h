// bed_symbols.h -- the battle editor's Symbols tab: the staff's circles as symbols (data/circle_symbols.json; the owner,
// 2026-11-27: "I need an editor to make a curve or points, shown in order by time, not set by imagination";
// docs/gameplay/22 §5.1, docs/gameplay/13).
//
// A symbol is edited two ways, on a picture of the play's area (the middle of the screen, 560 x 340, the circles inside
// its edge): its circles (click to add, drag to move, right-click to delete; each with the time it lights up), or a
// curve (control points with a time each, smoothed by LOD, as the owner's FM79979 cCurveWithTime) that N circles are
// sampled from, evenly along it, each taking the curve's time there. ▶ plays it: the circles light at their times, a
// dot runs along the curve. The level rows (chance, letters, the symbols with weights) are edited below it. Save writes
// the file (and the running game takes it); Try shows this symbol on every staff attack in the test battle.
#pragma once
#include "bed_logic.h"

#include <QWidget>

#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace bed {

class SymbolCanvas;

class SymbolEditor : public QWidget {
    Q_OBJECT
public:
    explicit SymbolEditor(const QString& l_oDataDir, QWidget* l_pParent = nullptr);

    // the window's hooks: Save done (the file as written), Try (circles 0..1 and their ms; empty: stop trying)
    std::function<void(const ojson&)> m_fnSaved;
    std::function<void(const std::vector<SymPoint>&, const std::vector<int>&)> m_fnTry;
    std::function<void(const QString&)> m_fnSay;   // the status line

    // ---- what the canvas edits (the current symbol) ----
    enum class Mode { Circles, Curve };
    Mode f_Mode() const { return m_eMode; }
    std::vector<SymPoint>& f_Points();          // the circles or the curve's control points, by the mode
    std::vector<double>& f_Times();
    const std::vector<SymPoint>& f_Circles() const { return m_vCircles; }
    const std::vector<double>& f_CircleMs() const { return m_vCircleMs; }
    const std::vector<SymPoint>& f_Curve() const { return m_vCurve; }
    const std::vector<double>& f_CurveMs() const { return m_vCurveMs; }
    int f_Lod() const;
    double f_PlayMs() const { return m_dPlayMs; }   // < 0: not playing
    int f_Selected() const { return m_iSelected; }
    void f_SetSelected(int l_iIndex);
    void f_Edited();                             // the canvas changed the points: the table and the file follow
    double f_NextTime() const;                   // a new point's time: the last one's and a step on

    // for the self-test
    bool f_Load();
    int f_SymbolCount() const;
    bool f_SelectForTest(const QString& l_oId);   // that symbol shown (false: none such)

private:
    void f_BuildUi();
    void f_FillList();
    void f_SelectSymbol(const QString& l_oId);
    void f_Commit();                             // the current symbol's state into m_jFile
    void f_FillTable();
    void f_FillRows();
    void f_ReadRows();                           // the level rows table into m_jFile
    void f_Say(const QString& l_oText);
    bool f_Save();

    QString m_oPath;
    ojson m_jFile = ojson::object();
    QString m_oCurrent;
    Mode m_eMode = Mode::Circles;
    std::vector<SymPoint> m_vCircles, m_vCurve;
    std::vector<double> m_vCircleMs, m_vCurveMs;
    int m_iSelected = -1;
    double m_dPlayMs = -1.0;
    bool m_bFilling = false;

    QListWidget* m_pList = nullptr;
    QLineEdit* m_pId = nullptr;
    QComboBox* m_pMode = nullptr;
    SymbolCanvas* m_pCanvas = nullptr;
    QTableWidget* m_pTable = nullptr;
    QSpinBox* m_pLod = nullptr;
    QSpinBox* m_pTotalMs = nullptr;
    QSpinBox* m_pSampleN = nullptr;
    QCheckBox* m_pRotate = nullptr;
    QCheckBox* m_pReverse = nullptr;
    QCheckBox* m_pMirror = nullptr;
    QCheckBox* m_pLoop = nullptr;
    QPushButton* m_pPlay = nullptr;
    QTableWidget* m_pRows = nullptr;
    QLabel* m_pInfo = nullptr;
    QTimer* m_pTimer = nullptr;
};

}  // namespace bed

#pragma once

#include "AnimDocument.h"

#include <QDockWidget>

#include <functional>

class QCheckBox;
class QTableWidget;
class QToolBar;

// The MPDI-style key list of the selected node: one row per key time (the union over its
// channels), one column per channel showing the key's value there or "·". Selecting a row moves
// the playhead to it. Double-click edits a cell (an empty cell creates the key; clearing a cell
// removes it). Tools work on the selected rows: insert, delete, set time, even spacing, rescale,
// ramp (MPDI AverageAssign), fade in / out, sprite sequence, set ease. "Key all channels together"
// (MPDI rows): inserting or editing an interpolated cell keys every interpolated channel there.
class AnimKeyListDock : public QDockWidget
{
    Q_OBJECT

public:
    enum Column { ColIndex, ColTime, ColPos, ColRot, ColScale, ColColor, ColSprite, ColVisible, ColEvent, ColDeform, ColFx, ColTrail, ColEase, ColCount };

    explicit AnimKeyListDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

    // Where the sprite sequence comes from (the Sprites dock's selection).
    void f_SetSpriteSource(std::function<QStringList()> l_fnF) { m_fnSpriteSource = std::move(l_fnF); }

    QTableWidget* f_Table() const { return m_pTable; }
    std::vector<float> f_RowTimes() const { return m_vTimes; }
    std::vector<float> f_SelectedTimes() const;
    void f_SelectRows(const std::vector<int>& l_vRows);
    // The cell edit path (also what the inline editor commits).
    bool f_CommitCell(int l_iRow, int l_iColumn, const QString& l_oText);

public slots:
    void f_InsertKey();               // at the playhead
    void f_DeleteRows(bool l_bOnlyCurrentChannel);
    void f_SetTimeDialog();
    void f_EvenSpacing();
    void f_RescaleDialog();
    void f_RampDialog();
    void f_FadeIn() { f_Fade(true); }
    void f_FadeOut() { f_Fade(false); }
    void f_SpriteSequence();
    void f_SetEaseDialog();

private:
    void f_Rebuild();
    void f_MarkPlayhead();
    int f_CurrentChannel() const;     // channel of the current column, -1 if none
    void f_Fade(bool l_bIn);
    // Moves rows by delta (from = their times); on a clash offers to shift the keys after them.
    bool f_MoveWithShift(const std::vector<float>& l_vFrom, float l_fDelta, const QString& l_oText);
    void f_Warn(const QString& l_oText);

    AnimDocument* m_pDoc;
    QTableWidget* m_pTable;
    QToolBar* m_pToolBar;
    QCheckBox* m_pTogether;
    std::vector<float> m_vTimes;
    std::function<QStringList()> m_fnSpriteSource;
    bool m_bUpdating = false;
};

// Event keys of the selected node: time + name, add / remove / edit inline.
class AnimEventsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimEventsDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

private:
    void f_Rebuild();

    AnimDocument* m_pDoc;
    QTableWidget* m_pTable;
    std::vector<float> m_vTimes;
    bool m_bUpdating = false;
};

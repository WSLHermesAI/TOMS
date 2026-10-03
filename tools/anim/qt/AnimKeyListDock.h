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
    enum Column { ColIndex, ColTime, ColPos, ColRot, ColScale, ColColor, ColSprite, ColVisible, ColEvent, ColEase, ColCount };

    explicit AnimKeyListDock(AnimDocument* doc, QWidget* parent = nullptr);

    // Where the sprite sequence comes from (the Sprites dock's selection).
    void setSpriteSource(std::function<QStringList()> f) { m_spriteSource = std::move(f); }

    QTableWidget* table() const { return m_table; }
    std::vector<float> rowTimes() const { return m_times; }
    std::vector<float> selectedTimes() const;
    void selectRows(const std::vector<int>& rows);
    // The cell edit path (also what the inline editor commits).
    bool commitCell(int row, int column, const QString& text);

public slots:
    void insertKey();               // at the playhead
    void deleteRows(bool onlyCurrentChannel);
    void setTimeDialog();
    void evenSpacing();
    void rescaleDialog();
    void rampDialog();
    void fadeIn() { fade(true); }
    void fadeOut() { fade(false); }
    void spriteSequence();
    void setEaseDialog();

private:
    void rebuild();
    void markPlayhead();
    int currentChannel() const;     // channel of the current column, -1 if none
    void fade(bool in);
    // Moves rows by delta (from = their times); on a clash offers to shift the keys after them.
    bool moveWithShift(const std::vector<float>& from, float delta, const QString& text);
    void warn(const QString& text);

    AnimDocument* m_doc;
    QTableWidget* m_table;
    QToolBar* m_toolBar;
    QCheckBox* m_together;
    std::vector<float> m_times;
    std::function<QStringList()> m_spriteSource;
    bool m_updating = false;
};

// Event keys of the selected node: time + name, add / remove / edit inline.
class AnimEventsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimEventsDock(AnimDocument* doc, QWidget* parent = nullptr);

private:
    void rebuild();

    AnimDocument* m_doc;
    QTableWidget* m_table;
    std::vector<float> m_times;
    bool m_updating = false;
};

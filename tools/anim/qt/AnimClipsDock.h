#pragma once

#include "AnimDocument.h"

#include <QDockWidget>

class QCheckBox;
class QDoubleSpinBox;
class QListWidget;
class QSpinBox;

// The clips of the file (add, duplicate, rename, delete) and the current clip's length,
// play count and stayAtLastFrame.
class AnimClipsDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimClipsDock(AnimDocument* doc, QWidget* parent = nullptr);
    void beginRename();
    // Makes clip `row` the current one (double-click / Enter). When the current clip has changes,
    // asks first whether to discard them; `discard` answers instead (tests). False = stayed.
    bool openClip(int row, int discard = -1);

private:
    void rebuild();
    void refreshProps();

    AnimDocument* m_doc;
    QListWidget* m_list;
    QDoubleSpinBox* m_length;
    QSpinBox* m_playCount;
    QCheckBox* m_stay;
    bool m_updating = false;
};

// Shared by the Clips and Properties docks: the three clip settings as widgets bound to the doc.
struct ClipFields {
    QDoubleSpinBox* length = nullptr;
    QSpinBox* playCount = nullptr;
    QCheckBox* stay = nullptr;
    static ClipFields create(AnimDocument* doc, QWidget* parent, bool* updating);
    void refresh(const AnimDocument* doc) const;
};

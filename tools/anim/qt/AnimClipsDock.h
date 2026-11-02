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
    explicit AnimClipsDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);
    void f_BeginRename();
    // Makes clip `row` the current one (double-click / Enter). When the current clip has changes,
    // asks first whether to discard them; `discard` answers instead (tests). False = stayed.
    bool f_OpenClip(int l_iRow, int l_iDiscard = -1);

signals:
    // A clip was opened from the list (double-click / Enter): the editor plays it from the start.
    void f_ClipActivated(int l_iRow);

private:
    void f_Rebuild();
    void f_RefreshProps();

    AnimDocument* m_pDoc;
    QListWidget* m_pList;
    QDoubleSpinBox* m_pLength;
    QSpinBox* m_pPlayCount;
    QCheckBox* m_pStay;
    bool m_bUpdating = false;
};

// Shared by the Clips and Properties docks: the three clip settings as widgets bound to the doc.
struct ClipFields {
    QDoubleSpinBox* m_pLength = nullptr;
    QSpinBox* m_pPlayCount = nullptr;
    QCheckBox* m_pStay = nullptr;
    static ClipFields f_Create(AnimDocument* l_pDoc, QWidget* l_pParent, bool* l_pUpdating);
    void f_Refresh(const AnimDocument* l_pDoc) const;
};

#pragma once

#include <QDockWidget>

class ParticleDocument;
class ParticlePlayback;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

// The file's effects and their emitters. Select one to edit it in the Inspector; the checkbox
// hides an emitter in the preview only (the file is not changed); double-click / F2 renames.
// The toolbar adds effects and emitters, duplicates, deletes and moves emitters (list order =
// draw order among equal "order").
class EffectsDock : public QDockWidget
{
    Q_OBJECT

public:
    EffectsDock(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, QWidget* l_pParent = nullptr);
    QTreeWidget* f_Tree() const { return m_pTree; }
    QToolBar* f_ToolBar() const { return m_pBar; }

    void f_AddEffect();
    void f_AddEmitter();
    void f_DuplicateSelected();
    void f_DeleteSelected();

private:
    void f_Rebuild();
    void f_ScheduleRebuild();
    void f_SyncSelection();

    ParticleDocument* m_pDoc;
    ParticlePlayback* m_pPlay;
    QTreeWidget* m_pTree;
    QToolBar* m_pBar;
    bool m_bRebuilding = false;
    bool m_bPending = false;
};

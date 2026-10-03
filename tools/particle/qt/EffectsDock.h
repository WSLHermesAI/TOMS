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
    EffectsDock(ParticleDocument* doc, ParticlePlayback* playback, QWidget* parent = nullptr);
    QTreeWidget* tree() const { return m_tree; }
    QToolBar* toolBar() const { return m_bar; }

    void addEffect();
    void addEmitter();
    void duplicateSelected();
    void deleteSelected();

private:
    void rebuild();
    void scheduleRebuild();
    void syncSelection();

    ParticleDocument* m_doc;
    ParticlePlayback* m_play;
    QTreeWidget* m_tree;
    QToolBar* m_bar;
    bool m_rebuilding = false;
    bool m_pending = false;
};

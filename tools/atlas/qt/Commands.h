#pragma once

#include "atlas_project.h"

#include <QStringList>
#include <QUndoCommand>

class AtlasDocument;

// True when two project states are the same: the JSON and the images (compared by pointer --
// images are never changed in place, a changed image is a new one).
bool sameProjectState(const atlas::Project& a, const atlas::Project& b);

// The one undo command: the whole project before and after the edit, plus the selection on both
// sides. Copying a Project is cheap (images are shared pointers), so snapshots are simpler to get
// right than one hand-written inverse per kind of edit, and every edit -- images included -- is
// undoable by construction.
class ProjectCommand : public QUndoCommand
{
public:
    // mergeKey: non-empty to let the next edit with the same key (and selection) fold into this
    // command when it comes within kMergeWindowMs -- one undo step per spin box drag or typing burst.
    ProjectCommand(AtlasDocument* doc, const QString& text, atlas::Project before, atlas::Project after,
                   QStringList selectionBefore, QStringList selectionAfter, const QString& mergeKey);

    void undo() override;
    void redo() override;
    int id() const override { return m_id; }
    bool mergeWith(const QUndoCommand* other) override;

private:
    static constexpr qint64 kMergeWindowMs = 1500;

    AtlasDocument* m_doc;
    atlas::Project m_before, m_after;
    QStringList m_selectionBefore, m_selectionAfter;
    QString m_mergeKey;
    int m_id = -1;
    qint64 m_time = 0;   // when the last edit folded in
};

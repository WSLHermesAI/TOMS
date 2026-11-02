#pragma once

#include "atlas_project.h"

#include <QStringList>
#include <QUndoCommand>

class AtlasDocument;

// True when two project states are the same: the JSON and the images (compared by pointer --
// images are never changed in place, a changed image is a new one).
bool f_SameProjectState(const atlas::Project& l_oA, const atlas::Project& l_oB);

// The one undo command: the whole project before and after the edit, plus the selection on both
// sides. Copying a Project is cheap (images are shared pointers), so snapshots are simpler to get
// right than one hand-written inverse per kind of edit, and every edit -- images included -- is
// undoable by construction.
class ProjectCommand : public QUndoCommand
{
public:
    // mergeKey: non-empty to let the next edit with the same key (and selection) fold into this
    // command when it comes within kMergeWindowMs -- one undo step per spin box drag or typing burst.
    ProjectCommand(AtlasDocument* l_pDoc, const QString& l_oText, atlas::Project l_oBefore, atlas::Project l_oAfter,
                   QStringList l_oSelectionBefore, QStringList l_oSelectionAfter, const QString& l_oMergeKey);

    void undo() override;
    void redo() override;
    int id() const override { return m_iId; }
    bool mergeWith(const QUndoCommand* l_pOther) override;

private:
    static constexpr qint64 m_i64MergeWindowMs = 1500;

    AtlasDocument* m_pDoc;
    atlas::Project m_oBefore, m_oAfter;
    QStringList m_oSelectionBefore, m_oSelectionAfter;
    QString m_oMergeKey;
    int m_iId = -1;
    qint64 m_i64Time = 0;   // when the last edit folded in
};

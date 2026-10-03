#include "Commands.h"

#include "AtlasDocument.h"

#include <QDateTime>
#include <QHash>

#include <climits>

bool sameProjectState(const atlas::Project& a, const atlas::Project& b)
{
    if (a.images != b.images || a.variants.size() != b.variants.size()) return false;
    for (size_t i = 0; i < a.variants.size(); i++)
        if (a.variants[i].images != b.variants[i].images) return false;
    return atlas::projectToJson(a) == atlas::projectToJson(b);
}

ProjectCommand::ProjectCommand(AtlasDocument* doc, const QString& text, atlas::Project before, atlas::Project after,
                               QStringList selectionBefore, QStringList selectionAfter, const QString& mergeKey)
    : QUndoCommand(text)
    , m_doc(doc)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_selectionBefore(std::move(selectionBefore))
    , m_selectionAfter(std::move(selectionAfter))
    , m_mergeKey(mergeKey)
    , m_id(mergeKey.isEmpty() ? -1 : int(qHash(mergeKey) & INT_MAX))
    , m_time(QDateTime::currentMSecsSinceEpoch())
{
}

void ProjectCommand::undo() { m_doc->restoreState(m_before, m_selectionBefore); }

void ProjectCommand::redo() { m_doc->restoreState(m_after, m_selectionAfter); }

bool ProjectCommand::mergeWith(const QUndoCommand* other)
{
    const auto* o = static_cast<const ProjectCommand*>(other);   // same id() => same class
    // Two different keys can share a hash; the selection check keeps "padding of A" and
    // "padding of B" apart.
    if (o->m_mergeKey != m_mergeKey || o->m_selectionBefore != m_selectionAfter || o->m_time - m_time > kMergeWindowMs)
        return false;
    m_after = o->m_after;
    m_selectionAfter = o->m_selectionAfter;
    m_time = o->m_time;
    // Dragging a value back to where it started leaves nothing to undo.
    setObsolete(sameProjectState(m_after, m_before));
    return true;
}

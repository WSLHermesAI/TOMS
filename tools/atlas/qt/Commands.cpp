#include "Commands.h"

#include "AtlasDocument.h"

#include <QDateTime>
#include <QHash>

#include <climits>

bool f_SameProjectState(const atlas::Project& l_oA, const atlas::Project& l_oB)
{
    if (l_oA.m_mapImages != l_oB.m_mapImages || l_oA.m_vVariants.size() != l_oB.m_vVariants.size()) return false;
    for (size_t l_nI = 0; l_nI < l_oA.m_vVariants.size(); l_nI++)
        if (l_oA.m_vVariants[l_nI].m_mapImages != l_oB.m_vVariants[l_nI].m_mapImages) return false;
    return atlas::f_ProjectToJson(l_oA) == atlas::f_ProjectToJson(l_oB);
}

ProjectCommand::ProjectCommand(AtlasDocument* l_pDoc, const QString& l_oText, atlas::Project l_oBefore, atlas::Project l_oAfter,
                               QStringList l_oSelectionBefore, QStringList l_oSelectionAfter, const QString& l_oMergeKey)
    : QUndoCommand(l_oText)
    , m_pDoc(l_pDoc)
    , m_oBefore(std::move(l_oBefore))
    , m_oAfter(std::move(l_oAfter))
    , m_oSelectionBefore(std::move(l_oSelectionBefore))
    , m_oSelectionAfter(std::move(l_oSelectionAfter))
    , m_oMergeKey(l_oMergeKey)
    , m_iId(l_oMergeKey.isEmpty() ? -1 : int(qHash(l_oMergeKey) & INT_MAX))
    , m_i64Time(QDateTime::currentMSecsSinceEpoch())
{
}

void ProjectCommand::undo() { m_pDoc->f_RestoreState(m_oBefore, m_oSelectionBefore); }

void ProjectCommand::redo() { m_pDoc->f_RestoreState(m_oAfter, m_oSelectionAfter); }

bool ProjectCommand::mergeWith(const QUndoCommand* l_pOther)
{
    const auto* l_pO = static_cast<const ProjectCommand*>(l_pOther);   // same id() => same class
    // Two different keys can share a hash; the selection check keeps "padding of A" and
    // "padding of B" apart.
    if (l_pO->m_oMergeKey != m_oMergeKey || l_pO->m_oSelectionBefore != m_oSelectionAfter || l_pO->m_i64Time - m_i64Time > m_i64MergeWindowMs)
        return false;
    m_oAfter = l_pO->m_oAfter;
    m_oSelectionAfter = l_pO->m_oSelectionAfter;
    m_i64Time = l_pO->m_i64Time;
    // Dragging a value back to where it started leaves nothing to undo.
    setObsolete(f_SameProjectState(m_oAfter, m_oBefore));
    return true;
}

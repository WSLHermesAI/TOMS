#pragma once

#include "AnimDocument.h"

#include <QDockWidget>
#include <QSet>
#include <QTreeWidget>

class QToolBar;

// The tree of nodes of the current clip. Drag & drop reparents and reorders (list order is the
// render order among equal `order` values); the Sprite column shows the rest sprite as
// "name (atlas)"; the eye column toggles the rest `visible`; the Order column shows `order`. Sprites dragged from the Sprites dock onto a node become its children.
class AnimNodeTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit AnimNodeTree(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

protected:
    QStringList mimeTypes() const override;
    void dragEnterEvent(QDragEnterEvent* l_pE) override;
    void dragMoveEvent(QDragMoveEvent* l_pE) override;
    void dropEvent(QDropEvent* l_pE) override;
    bool edit(const QModelIndex& l_oIndex, EditTrigger l_eTrigger, QEvent* l_pEvent) override;   // the name only

private:
    AnimDocument* m_pDoc;
};

class AnimNodesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimNodesDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

    QToolBar* f_ToolBar() const { return m_pToolBar; }
    void f_BeginRename();
    // The right-click menu on a node (it selects the node first): these actions, nullptr = a separator.
    void f_SetContextActions(const QList<QAction*>& l_oActions) { m_oContextActions = l_oActions; }

signals:
    void f_AddSpriteNodeRequested();   // the editor knows which sprite is picked

private:
    void f_Rebuild();
    void f_SyncSelection();
    void f_PlaybackMenu(const NodePath& l_vPath, const QPoint& l_oGlobalPos);   // the Playback column's menu
    QTreeWidgetItem* f_ItemFor(const NodePath& l_vPath) const;

    AnimDocument* m_pDoc;
    AnimNodeTree* m_pTree;
    QToolBar* m_pToolBar;
    QSet<QString> m_oCollapsed;   // path strings
    bool m_bUpdating = false;
    QList<QAction*> m_oContextActions;
};

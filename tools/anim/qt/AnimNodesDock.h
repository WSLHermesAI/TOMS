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
    explicit AnimNodeTree(AnimDocument* doc, QWidget* parent = nullptr);

protected:
    QStringList mimeTypes() const override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool edit(const QModelIndex& index, EditTrigger trigger, QEvent* event) override;   // the name only

private:
    AnimDocument* m_doc;
};

class AnimNodesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimNodesDock(AnimDocument* doc, QWidget* parent = nullptr);

    QToolBar* toolBar() const { return m_toolBar; }
    void beginRename();

signals:
    void addSpriteNodeRequested();   // the editor knows which sprite is picked

private:
    void rebuild();
    void syncSelection();
    QTreeWidgetItem* itemFor(const NodePath& path) const;

    AnimDocument* m_doc;
    AnimNodeTree* m_tree;
    QToolBar* m_toolBar;
    QSet<QString> m_collapsed;   // path strings
    bool m_updating = false;
};

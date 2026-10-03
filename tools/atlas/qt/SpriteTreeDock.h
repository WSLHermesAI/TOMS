#pragma once

#include <QDockWidget>
#include <QSet>

class AtlasDocument;
class QLineEdit;
class QSortFilterProxyModel;
class QTreeView;
class SpriteTreeModel;

// The Sprites dock: filter box + tree of every sprite (folders, child sprites under their
// parent, thumbnails, child-count badges). Selection is two-way synced with the document.
// PNG files and folders dropped anywhere on the window are handled by MainWindow.
class SpriteTreeDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit SpriteTreeDock(AtlasDocument* doc, QWidget* parent = nullptr);

    // F2: inline rename of the current sprite.
    void beginRename();
    void focusFilter();

signals:
    void editSpriteRequested(const QString& name);
    void contextMenuRequested(const QPoint& globalPos);

private:
    void onTreeSelectionChanged();
    void onDocSelectionChanged();
    void onFilterChanged(const QString& text);
    void onRenameRequested(const QString& from, const QString& to);
    bool isFiltering() const;
    void saveExpansion();
    void restoreExpansion();

    AtlasDocument* m_doc;
    SpriteTreeModel* m_model;
    QSortFilterProxyModel* m_proxy;
    QLineEdit* m_filter;
    QTreeView* m_tree;
    bool m_syncing = false;
    QSet<QString> m_expanded;   // node keys
    QSet<QString> m_known;      // node keys seen before (new folders open expanded)
};

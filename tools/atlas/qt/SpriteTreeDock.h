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
    explicit SpriteTreeDock(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);

    // F2: inline rename of the current sprite.
    void f_BeginRename();
    void f_FocusFilter();

signals:
    void f_EditSpriteRequested(const QString& l_oName);
    void f_ContextMenuRequested(const QPoint& l_oGlobalPos);

private:
    void f_OnTreeSelectionChanged();
    void f_OnDocSelectionChanged();
    void f_OnFilterChanged(const QString& l_oText);
    void f_OnRenameRequested(const QString& l_oFrom, const QString& l_oTo);
    bool f_IsFiltering() const;
    void f_SaveExpansion();
    void f_RestoreExpansion();

    AtlasDocument* m_pDoc;
    SpriteTreeModel* m_pModel;
    QSortFilterProxyModel* m_pProxy;
    QLineEdit* m_pFilter;
    QTreeView* m_pTree;
    bool m_bSyncing = false;
    QSet<QString> m_oExpanded;   // node keys
    QSet<QString> m_oKnown;      // node keys seen before (new folders open expanded)
};

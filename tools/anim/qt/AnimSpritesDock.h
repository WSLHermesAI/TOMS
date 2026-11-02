#pragma once

#include "AnimDocument.h"

#include <QDockWidget>
#include <QListWidget>
#include <QVector>

class QLineEdit;
class QTabWidget;

// The sprites with thumbnails, one tab per atlas in lookup order (the tab shows the atlas id and
// its sprite count). Every row is one atlas's copy of a sprite and picking it stores that atlas:
// "id:name" (a name more than one atlas has carries a badge; each copy is usable). Drag one into
// the viewport (or onto a node) to create a node with it; double-click assigns it to the selected
// node (rest sprite, or a sprite key at the playhead with auto-key). The selection of the current
// tab (several sprites, in list order) feeds the key list's "Sprite sequence". The filter applies
// to every tab; each tab's title then shows how many of its sprites match.
class AnimSpriteList : public QListWidget
{
    Q_OBJECT
public:
    using QListWidget::QListWidget;
    static constexpr int m_iRefRole = Qt::UserRole + 3;   // sprite rows: "id:name"
    static constexpr const char* m_pszMime = "application/x-toms-sprite";   // the references, one per line
    static QStringList f_RefsFrom(const QMimeData* l_pM);   // empty: not a sprite drag

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& l_oItems) const override;   // the rows' references
};

class AnimSpritesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimSpritesDock(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);

    // Sprite references ("id:name") of the current tab's selected rows, in list order / of its current row.
    QStringList f_SelectedSprites() const;
    QString f_CurrentSprite() const;
    // Shows the tab of the first reference and selects the rows of these references (in that
    // tab); a bare name selects the copy the lookup picks.
    void f_SelectSprites(const QStringList& l_oRefs);
    void f_FocusFilter();
    AnimSpriteList* f_List() const;                    // the current tab's list (nullptr: no atlas)
    AnimSpriteList* f_ListFor(int l_iAtlasIndex) const;   // nullptr: no such tab
    QListWidgetItem* f_SpriteItem(int l_iAtlasIndex, const QString& l_oName) const;   // nullptr: not listed
    QTabWidget* f_Tabs() const { return m_pTabs; }

private:
    void f_Rebuild();
    void f_ApplyFilter();
    QString f_TabTitle(int l_iAtlasIndex, int l_iShown) const;

    AnimDocument* m_pDoc;
    QLineEdit* m_pFilter;
    QTabWidget* m_pTabs;
    QVector<AnimSpriteList*> m_oLists;   // one per atlas, in tab order (nullptr: the atlas did not load)
    QString m_oCurrentAtlas;             // path of the atlas whose tab is shown, kept across rebuilds
};

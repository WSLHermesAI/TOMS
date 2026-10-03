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
    static constexpr int kRefRole = Qt::UserRole + 3;   // sprite rows: "id:name"
    static constexpr const char* kMime = "application/x-toms-sprite";   // the references, one per line
    static QStringList refsFrom(const QMimeData* m);   // empty: not a sprite drag

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;   // the rows' references
};

class AnimSpritesDock : public QDockWidget
{
    Q_OBJECT

public:
    explicit AnimSpritesDock(AnimDocument* doc, QWidget* parent = nullptr);

    // Sprite references ("id:name") of the current tab's selected rows, in list order / of its current row.
    QStringList selectedSprites() const;
    QString currentSprite() const;
    // Shows the tab of the first reference and selects the rows of these references (in that
    // tab); a bare name selects the copy the lookup picks.
    void selectSprites(const QStringList& refs);
    void focusFilter();
    AnimSpriteList* list() const;                    // the current tab's list (nullptr: no atlas)
    AnimSpriteList* listFor(int atlasIndex) const;   // nullptr: no such tab
    QListWidgetItem* spriteItem(int atlasIndex, const QString& name) const;   // nullptr: not listed
    QTabWidget* tabs() const { return m_tabs; }

private:
    void rebuild();
    void applyFilter();
    QString tabTitle(int atlasIndex, int shown) const;

    AnimDocument* m_doc;
    QLineEdit* m_filter;
    QTabWidget* m_tabs;
    QVector<AnimSpriteList*> m_lists;   // one per atlas, in tab order (nullptr: the atlas did not load)
    QString m_currentAtlas;             // path of the atlas whose tab is shown, kept across rebuilds
};

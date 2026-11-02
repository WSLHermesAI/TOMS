#pragma once

#include <QDockWidget>
#include <QListWidget>

class ParticleDocument;
class QLineEdit;
class QTabWidget;

// A list whose drags carry sprite references ("id:name", one per line), the mime type the anim
// editor uses too ("application/x-toms-sprite").
class FxSpriteList : public QListWidget
{
    Q_OBJECT
public:
    using QListWidget::QListWidget;
    static constexpr int m_iRefRole = Qt::UserRole + 3;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& l_oItems) const override;
};

// The atlases' sprites with thumbnails, one tab per atlas (titled with its id), and a filter.
// Double-click: the selected emitter draws that sprite. Drag one or several (in list order) into
// the viewport for a new emitter there, or onto the Inspector's flipbook frames. The + / − buttons
// add or remove atlases.
class FxSpritesDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit FxSpritesDock(ParticleDocument* l_pDoc, QWidget* l_pParent = nullptr);
    FxSpriteList* f_ListFor(int l_iAtlas) const;
    QTabWidget* f_Tabs() const { return m_pTabs; }
    QStringList f_SelectedRefs() const;   // of the current tab, in list order

signals:
    void f_AddAtlasRequested();

private:
    void f_Rebuild();
    void f_ApplyFilter();

    ParticleDocument* m_pDoc;
    QLineEdit* m_pFilter;
    QTabWidget* m_pTabs;
    QVector<FxSpriteList*> m_oLists;
};

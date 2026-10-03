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
    static constexpr int kRefRole = Qt::UserRole + 3;
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

// The atlases' sprites with thumbnails, one tab per atlas (titled with its id), and a filter.
// Double-click: the selected emitter draws that sprite. Drag one or several (in list order) into
// the viewport for a new emitter there, or onto the Inspector's flipbook frames. The + / − buttons
// add or remove atlases.
class FxSpritesDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit FxSpritesDock(ParticleDocument* doc, QWidget* parent = nullptr);
    FxSpriteList* listFor(int atlas) const;
    QTabWidget* tabs() const { return m_tabs; }
    QStringList selectedRefs() const;   // of the current tab, in list order

signals:
    void addAtlasRequested();

private:
    void rebuild();
    void applyFilter();

    ParticleDocument* m_doc;
    QLineEdit* m_filter;
    QTabWidget* m_tabs;
    QVector<FxSpriteList*> m_lists;
};

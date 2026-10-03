#include "FxSpritesDock.h"

#include "Icons.h"
#include "ParticleDocument.h"

#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const char* kSpriteMime = "application/x-toms-sprite";
}

QStringList FxSpriteList::mimeTypes() const { return {QLatin1String(kSpriteMime), QStringLiteral("text/plain")}; }

QMimeData* FxSpriteList::mimeData(const QList<QListWidgetItem*>& items) const
{
    // In list order, not click order: a frame sequence drops as it is listed.
    QList<QListWidgetItem*> sorted = items;
    std::sort(sorted.begin(), sorted.end(), [this](QListWidgetItem* a, QListWidgetItem* b) { return row(a) < row(b); });
    QStringList refs;
    for (const QListWidgetItem* it : sorted) refs << it->data(kRefRole).toString();
    auto* m = new QMimeData;
    m->setData(QLatin1String(kSpriteMime), refs.join(QLatin1Char('\n')).toUtf8());
    m->setText(refs.join(QLatin1Char('\n')));
    return m;
}

FxSpritesDock::FxSpritesDock(ParticleDocument* doc, QWidget* parent)
    : QDockWidget(tr("Sprites"), parent)
    , m_doc(doc)
    , m_filter(new QLineEdit(this))
    , m_tabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("ParticleSpritesDock"));
    m_filter->setPlaceholderText(tr("Filter sprites…"));
    m_filter->setClearButtonEnabled(true);
    m_tabs->setDocumentMode(true);
    auto* add = new QToolButton(this);
    add->setIcon(Icons::icon(Icons::Id::Add));
    add->setToolTip(tr("Add an atlas"));
    auto* remove = new QToolButton(this);
    remove->setIcon(Icons::icon(Icons::Id::Remove));
    remove->setToolTip(tr("Remove the shown atlas from the file"));
    auto* top = new QHBoxLayout;
    top->addWidget(m_filter, 1);
    top->addWidget(add);
    top->addWidget(remove);
    auto* body = new QWidget(this);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(4, 4, 4, 4);
    l->addLayout(top);
    l->addWidget(m_tabs, 1);
    setWidget(body);
    connect(add, &QToolButton::clicked, this, &FxSpritesDock::addAtlasRequested);
    connect(remove, &QToolButton::clicked, this, [this] {
        const int i = m_tabs->currentIndex();
        if (i < 0 || i >= m_doc->atlasCount()) return;
        if (!qApp->property("toms.selftest").toBool() &&
            QMessageBox::question(this, tr("Remove Atlas"), tr("Remove the atlas '%1' from this file? Sprites from it stop drawing.")
                                                                .arg(m_doc->atlasId(i))) != QMessageBox::Yes)
            return;
        m_doc->removeAtlas(i);
    });
    connect(m_filter, &QLineEdit::textChanged, this, &FxSpritesDock::applyFilter);
    connect(doc, &ParticleDocument::atlasChanged, this, &FxSpritesDock::rebuild);
    rebuild();
}

FxSpriteList* FxSpritesDock::listFor(int atlas) const { return atlas >= 0 && atlas < m_lists.size() ? m_lists[atlas] : nullptr; }

QStringList FxSpritesDock::selectedRefs() const
{
    QStringList out;
    const FxSpriteList* l = listFor(m_tabs->currentIndex());
    if (!l) return out;
    for (int i = 0; i < l->count(); i++)
        if (l->item(i)->isSelected()) out << l->item(i)->data(FxSpriteList::kRefRole).toString();
    return out;
}

void FxSpritesDock::rebuild()
{
    const int keep = m_tabs->currentIndex();
    while (m_tabs->count()) {
        QWidget* w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
    m_lists.clear();
    for (int a = 0; a < m_doc->atlasCount(); a++) {
        const ParticleDocument::LoadedAtlas& atlas = m_doc->atlasAt(a);
        if (!atlas.ok) {
            auto* msg = new QLabel(tr("This atlas did not load:\n%1").arg(atlas.error));
            msg->setWordWrap(true);
            msg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            m_tabs->addTab(msg, Icons::icon(Icons::Id::Error), m_doc->atlasId(a));
            m_lists << nullptr;
            continue;
        }
        auto* list = new FxSpriteList;
        list->setViewMode(QListView::IconMode);
        list->setIconSize(QSize(40, 40));
        list->setGridSize(QSize(72, 64));
        list->setResizeMode(QListView::Adjust);
        list->setMovement(QListView::Static);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->setDragEnabled(true);
        list->setDragDropMode(QAbstractItemView::DragOnly);
        list->setToolTip(tr("Double-click: the selected emitter draws it. Drag into the viewport: a new emitter "
                            "(several: a flipbook). Drag onto the Inspector's Frames: flipbook frames."));
        for (const QString& name : atlas.spriteNames) {
            const QString ref = m_doc->atlasId(a) + QLatin1Char(':') + name;
            const QImage img = m_doc->spriteImage(ref);
            // On a dark tile: the fx sprites are white.
            QPixmap tile(40, 40);
            tile.fill(QColor(40, 40, 48));
            if (!img.isNull()) {
                QPainter p(&tile);
                const QImage s = img.scaled(38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                p.drawImage(QPoint((40 - s.width()) / 2, (40 - s.height()) / 2), s);
            }
            auto* it = new QListWidgetItem(QIcon(tile), name, list);
            it->setData(FxSpriteList::kRefRole, ref);
            it->setToolTip(QStringLiteral("%1  (%2×%3)").arg(ref).arg(img.width()).arg(img.height()));
        }
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
            const std::string ref = it->data(FxSpriteList::kRefRole).toString().toStdString();
            m_doc->editEmitter(tr("Sprite"), [&](toms::fx::Emitter& m) {
                m.sprite = ref;
                m.frames.clear();
            });
        });
        m_tabs->addTab(list, QStringLiteral("%1 (%2)").arg(m_doc->atlasId(a)).arg(atlas.spriteNames.size()));
        m_tabs->setTabToolTip(a, QDir::toNativeSeparators(atlas.path));
        m_lists << list;
    }
    if (m_doc->atlasCount() == 0) {
        auto* msg = new QLabel(tr("No atlas: add one with + (the TOMS particle sprites are assets/media/atlas/fx.atlas)."));
        msg->setWordWrap(true);
        msg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_tabs->addTab(msg, tr("(no atlas)"));
    }
    if (keep >= 0 && keep < m_tabs->count()) m_tabs->setCurrentIndex(keep);
    applyFilter();
}

void FxSpritesDock::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (FxSpriteList* l : m_lists)
        if (l)
            for (int i = 0; i < l->count(); i++) l->item(i)->setHidden(!f.isEmpty() && !l->item(i)->text().contains(f, Qt::CaseInsensitive));
}

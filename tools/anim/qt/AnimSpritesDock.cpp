#include "AnimSpritesDock.h"

#include "Icons.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

const char* kSpriteMime = AnimSpriteList::kMime;
constexpr int kAtlasRole = Qt::UserRole + 1;    // sprite rows: the atlas index
constexpr int kRefRole = AnimSpriteList::kRefRole;

bool isSprite(const QListWidgetItem* it) { return it->data(kAtlasRole).isValid(); }
QString refOf(const QListWidgetItem* it) { return it->data(kRefRole).toString(); }

// The thumbnail with a small badge in the corner: a name more than one atlas has.
QIcon badged(const QIcon& icon)
{
    QPixmap pm = icon.pixmap(64, 64);
    {
        QPainter p(&pm);
        Icons::icon(Icons::Id::Info).paint(&p, QRect(pm.width() - 26, pm.height() - 26, 26, 26));
    }
    return QIcon(pm);
}

}  // namespace

QStringList AnimSpriteList::refsFrom(const QMimeData* m)
{
    if (!m || !m->hasFormat(QLatin1String(kMime))) return {};
    return QString::fromUtf8(m->data(QLatin1String(kMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QStringList AnimSpriteList::mimeTypes() const { return {QLatin1String(kSpriteMime), QStringLiteral("text/plain")}; }

QMimeData* AnimSpriteList::mimeData(const QList<QListWidgetItem*>& items) const
{
    QStringList refs;
    for (const QListWidgetItem* it : items)
        if (isSprite(it)) refs << refOf(it);
    auto* m = new QMimeData;
    m->setData(QLatin1String(kSpriteMime), refs.join(QLatin1Char('\n')).toUtf8());
    m->setText(refs.join(QLatin1Char('\n')));
    return m;
}

AnimSpritesDock::AnimSpritesDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Sprites"), parent)
    , m_doc(doc)
{
    setObjectName(QStringLiteral("AnimSpritesDock"));
    auto* body = new QWidget(this);
    m_filter = new QLineEdit(body);
    m_filter->setPlaceholderText(tr("Filter sprites (all atlases)…"));
    m_filter->setClearButtonEnabled(true);
    m_tabs = new QTabWidget(body);
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setElideMode(Qt::ElideRight);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(4);
    l->addWidget(m_filter);
    l->addWidget(m_tabs, 1);
    setWidget(body);

    connect(doc, &AnimDocument::atlasChanged, this, &AnimSpritesDock::rebuild);
    connect(m_filter, &QLineEdit::textChanged, this, &AnimSpritesDock::applyFilter);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int i) {
        if (i >= 0 && i < m_doc->atlasCount()) m_currentAtlas = m_doc->atlasAt(i).path;
    });
    rebuild();
}

QString AnimSpritesDock::tabTitle(int a, int shown) const
{
    const AnimDocument::LoadedAtlas& atlas = m_doc->atlasAt(a);
    if (!atlas.ok) return m_doc->atlasId(a);
    const int total = atlas.spriteNames.size();
    return shown == total ? QStringLiteral("%1 (%2)").arg(m_doc->atlasId(a)).arg(total)
                          : QStringLiteral("%1 (%2/%3)").arg(m_doc->atlasId(a)).arg(shown).arg(total);
}

void AnimSpritesDock::rebuild()
{
    const QStringList selected = selectedSprites();
    const QString keep = m_currentAtlas;
    const QSignalBlocker block(m_tabs);
    while (m_tabs->count() > 0) {
        QWidget* w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
    m_lists.clear();

    // Which atlases have each name, in lookup order.
    QHash<QString, QList<int>> owners;
    for (int a = 0; a < m_doc->atlasCount(); a++)
        if (m_doc->atlasAt(a).ok)
            for (const QString& name : m_doc->atlasAt(a).spriteNames) owners[name] << a;

    int current = 0;
    for (int a = 0; a < m_doc->atlasCount(); a++) {
        const AnimDocument::LoadedAtlas& atlas = m_doc->atlasAt(a);
        const QString id = m_doc->atlasId(a);
        if (atlas.path == keep) current = a;
        QString tip = tr("Atlas id '%1': its sprites are stored as %1:name").arg(id) + QStringLiteral("\n") +
                      QDir::toNativeSeparators(atlas.path);
        if (!atlas.ok) {
            auto* msg = new QLabel(tr("This atlas did not load:\n%1").arg(atlas.error), m_tabs);
            msg->setWordWrap(true);
            msg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            msg->setMargin(8);
            m_tabs->addTab(msg, Icons::icon(Icons::Id::Error), tabTitle(a, 0));
            m_tabs->setTabToolTip(a, tip + QStringLiteral("\n") + atlas.error);
            m_lists << nullptr;
            continue;
        }
        auto* list = new AnimSpriteList(m_tabs);
        list->setIconSize(QSize(28, 28));
        list->setUniformItemSizes(true);
        list->setSelectionMode(QAbstractItemView::ExtendedSelection);
        list->setDragEnabled(true);
        list->setDragDropMode(QAbstractItemView::DragOnly);
        list->setToolTip(tr("Drag into the viewport to add a node; drag several onto the scrubber / Keys list / Timeline "
                            "to insert them as sprite keys; double-click to give the selected node this sprite"));
        connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
            if (isSprite(it)) m_doc->assignSprite(refOf(it));
        });
        for (const QString& name : atlas.spriteNames) {
            const QString ref = QString::fromStdString(m_doc->spriteRefFor(a, name));
            auto* it = new QListWidgetItem(name, list);
            it->setData(kAtlasRole, a);
            it->setData(kRefRole, ref);
            const QImage img = m_doc->spriteImage(a, name);
            QIcon icon = Icons::spriteIcon(SpriteImageCache::thumbnail(img, 64), false, false, false);
            QString itTip = QStringLiteral("%1  (%2×%3)\n").arg(ref).arg(img.width()).arg(img.height());
            const QList<int> in = owners.value(name);
            if (in.size() > 1) {
                QStringList others;
                for (int o : in)
                    if (o != a) others << m_doc->atlasId(o);
                icon = badged(icon);
                itTip += tr("Also in %1. Each copy is usable: picking this one stores %2, which always draws from '%3'.")
                             .arg(others.join(QStringLiteral(", ")), ref, id);
            } else {
                itTip += tr("Picking it stores %1.").arg(ref);
            }
            it->setIcon(icon);
            it->setToolTip(itTip);
        }
        m_tabs->addTab(list, tabTitle(a, atlas.spriteNames.size()));
        m_tabs->setTabToolTip(a, tip);
        m_lists << list;
    }
    if (m_doc->atlasCount() == 0) {
        auto* msg = new QLabel(tr("No atlas yet: add one under Properties > Atlases (or File > Add Atlas…)."), m_tabs);
        msg->setWordWrap(true);
        msg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        msg->setMargin(8);
        m_tabs->addTab(msg, tr("(no atlas)"));
    }
    m_tabs->setCurrentIndex(current);
    if (current < m_doc->atlasCount()) m_currentAtlas = m_doc->atlasAt(current).path;
    setWindowTitle(m_doc->spriteNames().isEmpty() ? tr("Sprites") : tr("Sprites (%1)").arg(m_doc->spriteNames().size()));
    applyFilter();
    if (!selected.isEmpty()) selectSprites(selected);
}

void AnimSpritesDock::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int a = 0; a < m_lists.size(); a++) {
        AnimSpriteList* list = m_lists[a];
        if (!list) continue;
        int shown = 0;
        for (int i = 0; i < list->count(); i++) {
            QListWidgetItem* it = list->item(i);
            const bool hide = !f.isEmpty() && !refOf(it).contains(f, Qt::CaseInsensitive);
            it->setHidden(hide);
            shown += !hide;
        }
        m_tabs->setTabText(a, tabTitle(a, shown));
    }
}

AnimSpriteList* AnimSpritesDock::list() const { return listFor(m_tabs->currentIndex()); }

AnimSpriteList* AnimSpritesDock::listFor(int atlasIndex) const
{
    return atlasIndex >= 0 && atlasIndex < m_lists.size() ? m_lists[atlasIndex] : nullptr;
}

QStringList AnimSpritesDock::selectedSprites() const
{
    QStringList out;
    const AnimSpriteList* l = list();
    if (!l) return out;
    for (int i = 0; i < l->count(); i++) {
        const QListWidgetItem* it = l->item(i);
        if (it->isSelected() && isSprite(it) && !out.contains(refOf(it))) out << refOf(it);
    }
    return out;
}

QString AnimSpritesDock::currentSprite() const
{
    const AnimSpriteList* l = list();
    const QListWidgetItem* it = l ? l->currentItem() : nullptr;
    return it && it->isSelected() && isSprite(it) ? refOf(it) : QString();
}

QListWidgetItem* AnimSpritesDock::spriteItem(int atlasIndex, const QString& name) const
{
    const AnimSpriteList* l = listFor(atlasIndex);
    if (!l) return nullptr;
    for (int i = 0; i < l->count(); i++) {
        QListWidgetItem* it = l->item(i);
        if (it->text() == name) return it;
    }
    return nullptr;
}

void AnimSpritesDock::selectSprites(const QStringList& refs)
{
    for (AnimSpriteList* l : m_lists)
        if (l) l->clearSelection();
    bool shown = false;
    for (const QString& r : refs) {
        // A bare name: the copy the lookup picks.
        const toms::anim::SpriteRef s = toms::anim::parseSpriteRef(r.toStdString());
        int atlas = s.atlas.empty() ? -1 : m_doc->atlasIndexOf(QString::fromStdString(s.atlas));
        if (s.atlas.empty()) m_doc->findSprite(s.name, &atlas);
        QListWidgetItem* it = spriteItem(atlas, QString::fromStdString(s.name));
        if (!it) continue;
        if (!shown) {   // the first reference's tab comes to the front
            m_tabs->setCurrentIndex(atlas);
            shown = true;
        }
        if (atlas == m_tabs->currentIndex()) {
            it->setSelected(true);
            it->listWidget()->scrollToItem(it);
        }
    }
}

void AnimSpritesDock::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

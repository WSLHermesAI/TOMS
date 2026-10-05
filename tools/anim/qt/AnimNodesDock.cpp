#include "AnimNodesDock.h"

#include "Icons.h"
#include "anim_keys.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCursor>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QToolBar>
#include <QVBoxLayout>

#include <functional>

using toms::anim::Node;

namespace {

const char* kSpriteMime = "application/x-toms-sprite";
constexpr int kPathRole = Qt::UserRole + 1;
enum Column { ColName, ColSprite, ColOrder, ColPlay, ColSolo, ColEye };

NodePath itemPath(const QTreeWidgetItem* it) { return it ? pathFromString(it->data(ColName, kPathRole).toString()) : NodePath(); }

}  // namespace

// ---- AnimNodeTree -------------------------------------------------------------------------------

AnimNodeTree::AnimNodeTree(AnimDocument* doc, QWidget* parent)
    : QTreeWidget(parent)
    , m_doc(doc)
{
    setColumnCount(6);
    setHeaderLabels({tr("Node"), tr("Sprite"), tr("Order"), QString(), QString(), QString()});
    headerItem()->setIcon(ColSolo, Icons::icon(Icons::Id::Solo));
    headerItem()->setToolTip(ColSolo, tr("Solo: the viewport draws only that node and its children (click a row's icon; again to end)"));
    headerItem()->setIcon(ColPlay, Icons::icon(Icons::Id::Loop));
    headerItem()->setToolTip(ColPlay, tr("Playback: once and stay / once then hide / loop (click a row's icon)"));
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    header()->setSectionResizeMode(ColSprite, QHeaderView::Stretch);
    header()->setSectionResizeMode(ColOrder, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(ColPlay, QHeaderView::Fixed);
    header()->resizeSection(ColPlay, 26);
    header()->setSectionResizeMode(ColSolo, QHeaderView::Fixed);
    header()->resizeSection(ColSolo, 26);
    header()->setSectionResizeMode(ColEye, QHeaderView::Fixed);
    header()->resizeSection(ColEye, 26);
    setUniformRowHeights(true);
    setIconSize(QSize(18, 18));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);
    setExpandsOnDoubleClick(false);
}

QStringList AnimNodeTree::mimeTypes() const
{
    QStringList t = QTreeWidget::mimeTypes();
    t << QLatin1String(kSpriteMime);
    return t;
}

void AnimNodeTree::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat(QLatin1String(kSpriteMime))) {
        e->acceptProposedAction();
        return;
    }
    QTreeWidget::dragEnterEvent(e);
}

void AnimNodeTree::dragMoveEvent(QDragMoveEvent* e)
{
    QTreeWidget::dragMoveEvent(e);   // keeps the drop indicator
    if (e->mimeData()->hasFormat(QLatin1String(kSpriteMime))) e->acceptProposedAction();
}

void AnimNodeTree::dropEvent(QDropEvent* e)
{
    QTreeWidgetItem* target = itemAt(e->position().toPoint());
    const DropIndicatorPosition where = dropIndicatorPosition();
    // Never let QTreeWidget move its own items: the tree is rebuilt from the document.
    e->setDropAction(Qt::IgnoreAction);
    e->accept();
    if (e->mimeData()->hasFormat(QLatin1String(kSpriteMime))) {
        const QStringList names =
            QString::fromUtf8(e->mimeData()->data(QLatin1String(kSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!names.isEmpty()) m_doc->addSpriteNode(target ? itemPath(target) : NodePath(), names.first(), glm::vec2(0, 0));
        return;
    }
    QTreeWidgetItem* source = currentItem();
    if (!source || source == target) return;
    const NodePath from = itemPath(source);
    if (!target || where == OnViewport) {
        m_doc->moveNode(from, NodePath(), 1 << 20);   // last child of the root
        return;
    }
    const NodePath to = itemPath(target);
    if (where == OnItem) {
        m_doc->moveNode(from, to, 1 << 20);
        return;
    }
    if (to.empty()) return;   // above / below the root: nowhere to go
    const NodePath parent(to.begin(), to.end() - 1);
    m_doc->moveNode(from, parent, to.back() + (where == BelowItem ? 1 : 0));
}

bool AnimNodeTree::edit(const QModelIndex& index, EditTrigger trigger, QEvent* event)
{
    return index.column() == ColName && QTreeWidget::edit(index, trigger, event);
}

// ---- AnimNodesDock ------------------------------------------------------------------------------

AnimNodesDock::AnimNodesDock(AnimDocument* doc, QWidget* parent)
    : QDockWidget(tr("Nodes"), parent)
    , m_doc(doc)
    , m_tree(new AnimNodeTree(doc, this))
    , m_toolBar(new QToolBar(this))
{
    setObjectName(QStringLiteral("AnimNodesDock"));
    m_toolBar->setIconSize(QSize(16, 16));
    auto* body = new QWidget(this);
    auto* l = new QVBoxLayout(body);
    l->setContentsMargins(4, 2, 4, 4);
    l->setSpacing(2);
    l->addWidget(m_toolBar);
    l->addWidget(m_tree, 1);
    setWidget(body);

    connect(doc, &AnimDocument::fileChanged, this, &AnimNodesDock::rebuild);
    connect(doc, &AnimDocument::clipChanged, this, &AnimNodesDock::rebuild);
    connect(doc, &AnimDocument::soloChanged, this, &AnimNodesDock::rebuild);
    connect(doc, &AnimDocument::atlasChanged, this, &AnimNodesDock::rebuild);   // "(auto: id)" follows the lookup
    connect(doc, &AnimDocument::selectionChanged, this, &AnimNodesDock::syncSelection);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (m_updating) return;
        const QList<QTreeWidgetItem*> sel = m_tree->selectedItems();
        if (!sel.isEmpty()) m_doc->selectNode(itemPath(sel.first()));
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it, int col) {
        const NodePath p = itemPath(it);
        if (col == ColEye) m_doc->editNode(p, tr("Toggle visible"), [](Node& n) { n.visible = !n.visible; });
        if (col == ColPlay) playbackMenu(p, QCursor::pos());
        if (col == ColSolo) m_doc->setSolo(m_doc->solo() == p ? std::nullopt : std::optional<NodePath>(p));
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (m_updating || col != ColName) return;
        const QString name = it->text(ColName).trimmed();
        if (!m_doc->renameNode(itemPath(it), name)) rebuild();   // refused: show the old name again
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* it) { m_collapsed.insert(it->data(ColName, kPathRole).toString()); });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* it) { m_collapsed.remove(it->data(ColName, kPathRole).toString()); });
    connect(doc, &AnimDocument::fileReset, this, [this] { m_collapsed.clear(); });
    rebuild();
}

void AnimNodesDock::beginRename()
{
    if (QTreeWidgetItem* it = itemFor(m_doc->selectedPath())) m_tree->editItem(it, ColName);
}

QTreeWidgetItem* AnimNodesDock::itemFor(const NodePath& path) const
{
    QTreeWidgetItem* it = m_tree->topLevelItem(0);
    for (int i : path) {
        if (!it || i >= it->childCount()) return nullptr;
        it = it->child(i);
    }
    return it;
}

void AnimNodesDock::playbackMenu(const NodePath& path, const QPoint& globalPos)
{
    const toms::anim::Clip* clip = m_doc->clip();
    const Node* node = clip ? animed::nodeAt(clip->root, path) : nullptr;
    if (!node) return;
    const int cur = node->loop ? 2 : node->stayAtLastFrame ? 0 : 1;
    QMenu menu(this);
    const QString names[3] = {tr("Once, stay at the last key"), tr("Once, then hide"), tr("Loop")};
    const Icons::Id icons[3] = {Icons::Id::Play, Icons::Id::Stop, Icons::Id::Loop};
    for (int i = 0; i < 3; i++) {
        QAction* a = menu.addAction(Icons::icon(icons[i]), names[i]);
        a->setCheckable(true);
        a->setChecked(i == cur);
        a->setData(i);
    }
    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;
    const int i = chosen->data().toInt();
    m_doc->editNode(path, tr("Playback"), [i](Node& n) {
        n.loop = i == 2;
        n.stayAtLastFrame = i != 1;
    });
}

void AnimNodesDock::rebuild()
{
    m_updating = true;
    m_tree->clear();
    if (const toms::anim::Clip* clip = m_doc->clip()) {
        std::function<void(QTreeWidgetItem*, const Node&, NodePath&)> add = [&](QTreeWidgetItem* it, const Node& n, NodePath& p) {
            it->setText(ColName, QString::fromStdString(n.name));
            it->setIcon(ColName, Icons::icon(n.sprite.empty() && n.spriteKeys.empty() ? Icons::Id::Node : Icons::Id::SpriteNode));
            it->setData(ColName, kPathRole, pathToString(p));
            QString tip = n.sprite.empty() ? tr("group") : tr("sprite: %1").arg(m_doc->spriteDisplay(n.sprite));
            if (!n.spriteKeys.empty()) {
                QStringList keyed;   // the sprites its keys switch to, in key order
                for (const auto& k : n.spriteKeys)
                    if (!keyed.contains(m_doc->spriteDisplay(k.v))) keyed << m_doc->spriteDisplay(k.v);
                tip += tr("\nsprite keys: %1").arg(keyed.join(QStringLiteral(", ")));
            }
            if (n.blend == toms::anim::Blend::Add) tip += tr(", additive");
            it->setToolTip(ColName, tip);
            if (!n.sprite.empty()) it->setText(ColSprite, m_doc->spriteDisplay(n.sprite));
            else if (!n.spriteKeys.empty()) it->setText(ColSprite, tr("(keys)"));
            it->setForeground(ColSprite, palette().color(QPalette::PlaceholderText));
            it->setToolTip(ColSprite, tip);
            it->setText(ColOrder, QString::number(n.order));
            it->setTextAlignment(ColOrder, Qt::AlignCenter);
            it->setToolTip(ColOrder, tr("order: < 0 draws before the parent's own sprite"));
            // Playback: a looping / hiding node shows it; a plain one has a faint play icon to click.
            const QIcon play = Icons::icon(n.loop ? Icons::Id::Loop : n.stayAtLastFrame ? Icons::Id::Play : Icons::Id::Stop);
            it->setIcon(ColPlay, n.loop || !n.stayAtLastFrame ? play : QIcon(play.pixmap(m_tree->iconSize(), QIcon::Disabled)));
            it->setToolTip(ColPlay, (n.loop ? tr("Loop: plays its keys again, forever (also after the clip ends)")
                                     : n.stayAtLastFrame ? tr("Once, stay at the last key")
                                                         : tr("Once, then hide (with its children)")) +
                                        tr("\nClick to change"));
            const bool solo = m_doc->solo() == p;
            const QIcon target = Icons::icon(Icons::Id::Solo);
            it->setIcon(ColSolo, solo ? target : QIcon(target.pixmap(m_tree->iconSize(), QIcon::Disabled)));
            it->setToolTip(ColSolo, solo ? tr("Solo: only this node and its children are drawn (click to end)")
                                         : tr("Solo: draw only this node and its children"));
            if (m_doc->solo() && !m_doc->soloShows(&n))   // not drawn now
                it->setForeground(ColName, palette().color(QPalette::PlaceholderText));
            it->setIcon(ColEye, Icons::icon(n.visible ? Icons::Id::Eye : Icons::Id::EyeOff));
            it->setToolTip(ColEye, tr("Rest visibility (click to toggle)"));
            Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDropEnabled;
            if (!p.empty()) f |= Qt::ItemIsDragEnabled;
            it->setFlags(f);
            if (!n.visible) it->setForeground(ColName, palette().color(QPalette::PlaceholderText));
            for (size_t i = 0; i < n.children.size(); i++) {
                p.push_back(int(i));
                add(new QTreeWidgetItem(it), n.children[i], p);
                p.pop_back();
            }
            it->setExpanded(!m_collapsed.contains(pathToString(p)));
        };
        NodePath p;
        auto* root = new QTreeWidgetItem(m_tree);
        add(root, clip->root, p);
    }
    m_updating = false;
    syncSelection();
}

void AnimNodesDock::syncSelection()
{
    m_updating = true;
    QTreeWidgetItem* it = itemFor(m_doc->selectedPath());
    if (it) {
        m_tree->setCurrentItem(it);
        m_tree->scrollToItem(it);
    } else {
        m_tree->clearSelection();
    }
    m_updating = false;
}

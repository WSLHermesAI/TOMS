#include "SpriteTreeDock.h"

#include "AtlasDocument.h"
#include "SpriteTreeModel.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>

namespace {

// Draws the child-count badge at the right end of sprite rows that have child sprites.
class SpriteItemDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const int count = index.data(SpriteTreeModel::ChildCountRole).toInt();
        QStyleOptionViewItem opt = option;
        QRect badge;
        if (count > 0) {
            const QString text = QString::number(count);
            const QFontMetrics fm(opt.font);
            const int w = std::max(fm.horizontalAdvance(text) + 10, fm.height() + 2);
            badge = QRect(opt.rect.right() - w - 4, opt.rect.center().y() - fm.height() / 2, w, fm.height());
            opt.rect.setRight(badge.left() - 4);   // keep the text clear of the badge
        }
        QStyledItemDelegate::paint(p, opt, index);
        if (count > 0) {
            p->save();
            p->setRenderHint(QPainter::Antialiasing);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(0xff, 0xa8, 0x3a, 200));
            p->drawRoundedRect(badge, badge.height() / 2.0, badge.height() / 2.0);
            p->setPen(QColor(0x20, 0x20, 0x20));
            p->drawText(badge, Qt::AlignCenter, QString::number(count));
            p->restore();
        }
    }
};

}  // namespace

SpriteTreeDock::SpriteTreeDock(AtlasDocument* doc, QWidget* parent)
    : QDockWidget(tr("Sprites"), parent)
    , m_doc(doc)
    , m_model(new SpriteTreeModel(doc, this))
    , m_proxy(new QSortFilterProxyModel(this))
{
    setObjectName(QStringLiteral("SpritesDock"));
    auto* body = new QWidget(this);
    m_filter = new QLineEdit(body);
    m_filter->setPlaceholderText(tr("Filter sprites…"));
    m_filter->setClearButtonEnabled(true);

    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterRole(SpriteTreeModel::FilterTextRole);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);   // a match keeps its folders/parents visible

    m_tree = new QTreeView(body);
    m_tree->setModel(m_proxy);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setIconSize(QSize(28, 28));
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);   // F2 goes through beginRename()
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setItemDelegate(new SpriteItemDelegate(m_tree));
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->header()->setStretchLastSection(true);

    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(m_filter);
    layout->addWidget(m_tree, 1);
    setWidget(body);

    connect(m_filter, &QLineEdit::textChanged, this, &SpriteTreeDock::onFilterChanged);
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SpriteTreeDock::onTreeSelectionChanged);
    connect(m_doc, &AtlasDocument::selectionChanged, this, &SpriteTreeDock::onDocSelectionChanged);
    connect(m_model, &SpriteTreeModel::renameRequested, this, &SpriteTreeDock::onRenameRequested);
    connect(m_model, &QAbstractItemModel::modelAboutToBeReset, this, &SpriteTreeDock::saveExpansion);
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] {
        restoreExpansion();
        onDocSelectionChanged();
    });
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex& idx) {
        const QString name = idx.data(SpriteTreeModel::SpriteNameRole).toString();
        if (name.isEmpty()) m_tree->setExpanded(idx, !m_tree->isExpanded(idx));
        else emit editSpriteRequested(name);
    });
    connect(m_tree, &QTreeView::customContextMenuRequested, this, [this](const QPoint& pos) {
        emit contextMenuRequested(m_tree->viewport()->mapToGlobal(pos));
    });
}

void SpriteTreeDock::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

void SpriteTreeDock::beginRename()
{
    const QString name = m_doc->currentSprite();
    if (name.isEmpty()) return;
    const QModelIndex idx = m_proxy->mapFromSource(m_model->indexForSprite(name));
    if (!idx.isValid()) return;
    m_tree->scrollTo(idx);
    m_tree->edit(idx);
}

void SpriteTreeDock::onRenameRequested(const QString& from, const QString& to)
{
    QString err;
    if (!m_doc->renameSprite(from, to, &err)) QMessageBox::warning(this, tr("Rename"), err);
}

void SpriteTreeDock::onTreeSelectionChanged()
{
    if (m_syncing) return;
    // Keep the document's order (the last one is the current sprite) and append new picks.
    QStringList picked;
    for (const QModelIndex& idx : m_tree->selectionModel()->selectedIndexes()) {
        const QString n = idx.data(SpriteTreeModel::SpriteNameRole).toString();
        if (!n.isEmpty()) picked << n;
    }
    QStringList sel;
    for (const QString& n : m_doc->selection())
        if (picked.contains(n)) sel << n;
    const QString current = m_tree->currentIndex().data(SpriteTreeModel::SpriteNameRole).toString();
    for (const QString& n : picked)
        if (!sel.contains(n) && n != current) sel << n;
    if (picked.contains(current)) {
        sel.removeAll(current);
        sel << current;
    }
    m_syncing = true;
    m_doc->setSelection(sel);
    m_syncing = false;
}

void SpriteTreeDock::onDocSelectionChanged()
{
    if (m_syncing) return;
    m_syncing = true;
    QItemSelection selection;
    QModelIndex current;
    for (const QString& n : m_doc->selection()) {
        const QModelIndex idx = m_proxy->mapFromSource(m_model->indexForSprite(n));
        if (!idx.isValid()) continue;
        selection.select(idx, idx);
        current = idx;
        for (QModelIndex p = idx.parent(); p.isValid(); p = p.parent()) m_tree->expand(p);
    }
    m_tree->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (current.isValid()) {
        m_tree->selectionModel()->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
        m_tree->scrollTo(current);
    }
    m_syncing = false;
}

void SpriteTreeDock::onFilterChanged(const QString& text)
{
    if (!isFiltering() && !text.isEmpty()) saveExpansion();   // remember the user's layout first
    m_proxy->setFilterFixedString(text);
    restoreExpansion();
}

bool SpriteTreeDock::isFiltering() const { return !m_proxy->filterRegularExpression().pattern().isEmpty(); }

void SpriteTreeDock::saveExpansion()
{
    if (isFiltering()) return;   // filtering expands everything temporarily
    m_expanded.clear();
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        for (int r = 0; r < m_proxy->rowCount(parent); r++) {
            const QModelIndex idx = m_proxy->index(r, 0, parent);
            const QString key = idx.data(SpriteTreeModel::NodeKeyRole).toString();
            m_known.insert(key);
            if (m_tree->isExpanded(idx)) m_expanded.insert(key);
            walk(idx);
        }
    };
    walk(QModelIndex());
}

void SpriteTreeDock::restoreExpansion()
{
    if (isFiltering()) {
        m_tree->expandAll();
        return;
    }
    std::function<void(const QModelIndex&)> walk = [&](const QModelIndex& parent) {
        for (int r = 0; r < m_proxy->rowCount(parent); r++) {
            const QModelIndex idx = m_proxy->index(r, 0, parent);
            const QString key = idx.data(SpriteTreeModel::NodeKeyRole).toString();
            // Folders open the first time they appear; sprites with children start closed.
            const bool folder = idx.data(SpriteTreeModel::SpriteNameRole).toString().isEmpty();
            const bool expand = m_known.contains(key) ? m_expanded.contains(key) : folder;
            m_tree->setExpanded(idx, expand);
            m_known.insert(key);
            walk(idx);
        }
    };
    walk(QModelIndex());
}

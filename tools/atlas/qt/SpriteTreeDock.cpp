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

    void paint(QPainter* l_pP, const QStyleOptionViewItem& l_oOption, const QModelIndex& l_oIndex) const override
    {
        const int l_iCount = l_oIndex.data(SpriteTreeModel::ChildCountRole).toInt();
        QStyleOptionViewItem l_oOpt = l_oOption;
        QRect l_oBadge;
        if (l_iCount > 0) {
            const QString l_oText = QString::number(l_iCount);
            const QFontMetrics l_oFm(l_oOpt.font);
            const int l_iW = std::max(l_oFm.horizontalAdvance(l_oText) + 10, l_oFm.height() + 2);
            l_oBadge = QRect(l_oOpt.rect.right() - l_iW - 4, l_oOpt.rect.center().y() - l_oFm.height() / 2, l_iW, l_oFm.height());
            l_oOpt.rect.setRight(l_oBadge.left() - 4);   // keep the text clear of the badge
        }
        QStyledItemDelegate::paint(l_pP, l_oOpt, l_oIndex);
        if (l_iCount > 0) {
            l_pP->save();
            l_pP->setRenderHint(QPainter::Antialiasing);
            l_pP->setPen(Qt::NoPen);
            l_pP->setBrush(QColor(0xff, 0xa8, 0x3a, 200));
            l_pP->drawRoundedRect(l_oBadge, l_oBadge.height() / 2.0, l_oBadge.height() / 2.0);
            l_pP->setPen(QColor(0x20, 0x20, 0x20));
            l_pP->drawText(l_oBadge, Qt::AlignCenter, QString::number(l_iCount));
            l_pP->restore();
        }
    }
};

}  // namespace

SpriteTreeDock::SpriteTreeDock(AtlasDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Sprites"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pModel(new SpriteTreeModel(l_pDoc, this))
    , m_pProxy(new QSortFilterProxyModel(this))
{
    setObjectName(QStringLiteral("SpritesDock"));
    auto* l_pBody = new QWidget(this);
    m_pFilter = new QLineEdit(l_pBody);
    m_pFilter->setPlaceholderText(tr("Filter sprites…"));
    m_pFilter->setClearButtonEnabled(true);

    m_pProxy->setSourceModel(m_pModel);
    m_pProxy->setFilterRole(SpriteTreeModel::FilterTextRole);
    m_pProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_pProxy->setRecursiveFilteringEnabled(true);   // a match keeps its folders/parents visible

    m_pTree = new QTreeView(l_pBody);
    m_pTree->setModel(m_pProxy);
    m_pTree->setHeaderHidden(true);
    m_pTree->setUniformRowHeights(true);
    m_pTree->setIconSize(QSize(28, 28));
    m_pTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_pTree->setEditTriggers(QAbstractItemView::NoEditTriggers);   // F2 goes through beginRename()
    m_pTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_pTree->setItemDelegate(new SpriteItemDelegate(m_pTree));
    m_pTree->setExpandsOnDoubleClick(false);
    m_pTree->header()->setStretchLastSection(true);

    auto* l_pLayout = new QVBoxLayout(l_pBody);
    l_pLayout->setContentsMargins(4, 4, 4, 4);
    l_pLayout->setSpacing(4);
    l_pLayout->addWidget(m_pFilter);
    l_pLayout->addWidget(m_pTree, 1);
    setWidget(l_pBody);

    connect(m_pFilter, &QLineEdit::textChanged, this, &SpriteTreeDock::f_OnFilterChanged);
    connect(m_pTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SpriteTreeDock::f_OnTreeSelectionChanged);
    connect(m_pDoc, &AtlasDocument::f_SelectionChanged, this, &SpriteTreeDock::f_OnDocSelectionChanged);
    connect(m_pModel, &SpriteTreeModel::f_RenameRequested, this, &SpriteTreeDock::f_OnRenameRequested);
    connect(m_pModel, &QAbstractItemModel::modelAboutToBeReset, this, &SpriteTreeDock::f_SaveExpansion);
    connect(m_pModel, &QAbstractItemModel::modelReset, this, [this] {
        f_RestoreExpansion();
        f_OnDocSelectionChanged();
    });
    connect(m_pTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& l_oIdx) {
        const QString l_oName = l_oIdx.data(SpriteTreeModel::SpriteNameRole).toString();
        if (l_oName.isEmpty()) m_pTree->setExpanded(l_oIdx, !m_pTree->isExpanded(l_oIdx));
        else emit f_EditSpriteRequested(l_oName);
    });
    connect(m_pTree, &QTreeView::customContextMenuRequested, this, [this](const QPoint& l_oPos) {
        emit f_ContextMenuRequested(m_pTree->viewport()->mapToGlobal(l_oPos));
    });
}

void SpriteTreeDock::f_FocusFilter()
{
    m_pFilter->setFocus();
    m_pFilter->selectAll();
}

void SpriteTreeDock::f_BeginRename()
{
    const QString l_oName = m_pDoc->f_CurrentSprite();
    if (l_oName.isEmpty()) return;
    const QModelIndex l_oIdx = m_pProxy->mapFromSource(m_pModel->f_IndexForSprite(l_oName));
    if (!l_oIdx.isValid()) return;
    m_pTree->scrollTo(l_oIdx);
    m_pTree->edit(l_oIdx);
}

void SpriteTreeDock::f_OnRenameRequested(const QString& l_oFrom, const QString& l_oTo)
{
    QString l_oErr;
    if (!m_pDoc->f_RenameSprite(l_oFrom, l_oTo, &l_oErr)) QMessageBox::warning(this, tr("Rename"), l_oErr);
}

void SpriteTreeDock::f_OnTreeSelectionChanged()
{
    if (m_bSyncing) return;
    // Keep the document's order (the last one is the current sprite) and append new picks.
    QStringList l_oPicked;
    for (const QModelIndex& l_oIdx : m_pTree->selectionModel()->selectedIndexes()) {
        const QString l_oN = l_oIdx.data(SpriteTreeModel::SpriteNameRole).toString();
        if (!l_oN.isEmpty()) l_oPicked << l_oN;
    }
    QStringList l_oSel;
    for (const QString& l_oN : m_pDoc->f_Selection())
        if (l_oPicked.contains(l_oN)) l_oSel << l_oN;
    const QString l_oCurrent = m_pTree->currentIndex().data(SpriteTreeModel::SpriteNameRole).toString();
    for (const QString& l_oN : l_oPicked)
        if (!l_oSel.contains(l_oN) && l_oN != l_oCurrent) l_oSel << l_oN;
    if (l_oPicked.contains(l_oCurrent)) {
        l_oSel.removeAll(l_oCurrent);
        l_oSel << l_oCurrent;
    }
    m_bSyncing = true;
    m_pDoc->f_SetSelection(l_oSel);
    m_bSyncing = false;
}

void SpriteTreeDock::f_OnDocSelectionChanged()
{
    if (m_bSyncing) return;
    m_bSyncing = true;
    QItemSelection l_oSelection;
    QModelIndex l_oCurrent;
    for (const QString& l_oN : m_pDoc->f_Selection()) {
        const QModelIndex l_oIdx = m_pProxy->mapFromSource(m_pModel->f_IndexForSprite(l_oN));
        if (!l_oIdx.isValid()) continue;
        l_oSelection.select(l_oIdx, l_oIdx);
        l_oCurrent = l_oIdx;
        for (QModelIndex l_oP = l_oIdx.parent(); l_oP.isValid(); l_oP = l_oP.parent()) m_pTree->expand(l_oP);
    }
    m_pTree->selectionModel()->select(l_oSelection, QItemSelectionModel::ClearAndSelect);
    if (l_oCurrent.isValid()) {
        m_pTree->selectionModel()->setCurrentIndex(l_oCurrent, QItemSelectionModel::NoUpdate);
        m_pTree->scrollTo(l_oCurrent);
    }
    m_bSyncing = false;
}

void SpriteTreeDock::f_OnFilterChanged(const QString& l_oText)
{
    if (!f_IsFiltering() && !l_oText.isEmpty()) f_SaveExpansion();   // remember the user's layout first
    m_pProxy->setFilterFixedString(l_oText);
    f_RestoreExpansion();
}

bool SpriteTreeDock::f_IsFiltering() const { return !m_pProxy->filterRegularExpression().pattern().isEmpty(); }

void SpriteTreeDock::f_SaveExpansion()
{
    if (f_IsFiltering()) return;   // filtering expands everything temporarily
    m_oExpanded.clear();
    std::function<void(const QModelIndex&)> l_fnWalk = [&](const QModelIndex& l_oParent) {
        for (int l_iR = 0; l_iR < m_pProxy->rowCount(l_oParent); l_iR++) {
            const QModelIndex l_oIdx = m_pProxy->index(l_iR, 0, l_oParent);
            const QString l_oKey = l_oIdx.data(SpriteTreeModel::NodeKeyRole).toString();
            m_oKnown.insert(l_oKey);
            if (m_pTree->isExpanded(l_oIdx)) m_oExpanded.insert(l_oKey);
            l_fnWalk(l_oIdx);
        }
    };
    l_fnWalk(QModelIndex());
}

void SpriteTreeDock::f_RestoreExpansion()
{
    if (f_IsFiltering()) {
        m_pTree->expandAll();
        return;
    }
    std::function<void(const QModelIndex&)> l_fnWalk = [&](const QModelIndex& l_oParent) {
        for (int l_iR = 0; l_iR < m_pProxy->rowCount(l_oParent); l_iR++) {
            const QModelIndex l_oIdx = m_pProxy->index(l_iR, 0, l_oParent);
            const QString l_oKey = l_oIdx.data(SpriteTreeModel::NodeKeyRole).toString();
            // Folders open the first time they appear; sprites with children start closed.
            const bool l_bFolder = l_oIdx.data(SpriteTreeModel::SpriteNameRole).toString().isEmpty();
            const bool l_bExpand = m_oKnown.contains(l_oKey) ? m_oExpanded.contains(l_oKey) : l_bFolder;
            m_pTree->setExpanded(l_oIdx, l_bExpand);
            m_oKnown.insert(l_oKey);
            l_fnWalk(l_oIdx);
        }
    };
    l_fnWalk(QModelIndex());
}

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

const char* g_pszSpriteMime = "application/x-toms-sprite";
constexpr int g_iPathRole = Qt::UserRole + 1;
enum Column { ColName, ColSprite, ColOrder, ColPlay, ColSolo, ColEye };

NodePath f_ItemPath(const QTreeWidgetItem* l_pIt) { return l_pIt ? f_PathFromString(l_pIt->data(ColName, g_iPathRole).toString()) : NodePath(); }

}  // namespace

// ---- AnimNodeTree -------------------------------------------------------------------------------

AnimNodeTree::AnimNodeTree(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QTreeWidget(l_pParent)
    , m_pDoc(l_pDoc)
{
    setColumnCount(6);
    setHeaderLabels({tr("Node"), tr("Sprite"), tr("Order"), QString(), QString(), QString()});
    headerItem()->setIcon(ColSolo, Icons::f_Icon(Icons::Id::Solo));
    headerItem()->setToolTip(ColSolo, tr("Solo: the viewport draws only that node and its children (click a row's icon; again to end)"));
    headerItem()->setIcon(ColPlay, Icons::f_Icon(Icons::Id::Loop));
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
    QStringList l_oT = QTreeWidget::mimeTypes();
    l_oT << QLatin1String(g_pszSpriteMime);
    return l_oT;
}

void AnimNodeTree::dragEnterEvent(QDragEnterEvent* l_pE)
{
    if (l_pE->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) {
        l_pE->acceptProposedAction();
        return;
    }
    QTreeWidget::dragEnterEvent(l_pE);
}

void AnimNodeTree::dragMoveEvent(QDragMoveEvent* l_pE)
{
    QTreeWidget::dragMoveEvent(l_pE);   // keeps the drop indicator
    if (l_pE->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) l_pE->acceptProposedAction();
}

void AnimNodeTree::dropEvent(QDropEvent* l_pE)
{
    QTreeWidgetItem* l_pTarget = itemAt(l_pE->position().toPoint());
    const DropIndicatorPosition l_eWhere = dropIndicatorPosition();
    // Never let QTreeWidget move its own items: the tree is rebuilt from the document.
    l_pE->setDropAction(Qt::IgnoreAction);
    l_pE->accept();
    if (l_pE->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) {
        const QStringList l_oNames =
            QString::fromUtf8(l_pE->mimeData()->data(QLatin1String(g_pszSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (!l_oNames.isEmpty()) m_pDoc->f_AddSpriteNode(l_pTarget ? f_ItemPath(l_pTarget) : NodePath(), l_oNames.first(), glm::vec2(0, 0));
        return;
    }
    QTreeWidgetItem* l_pSource = currentItem();
    if (!l_pSource || l_pSource == l_pTarget) return;
    const NodePath l_vFrom = f_ItemPath(l_pSource);
    if (!l_pTarget || l_eWhere == OnViewport) {
        m_pDoc->f_MoveNode(l_vFrom, NodePath(), 1 << 20);   // last child of the root
        return;
    }
    const NodePath l_vTo = f_ItemPath(l_pTarget);
    if (l_eWhere == OnItem) {
        m_pDoc->f_MoveNode(l_vFrom, l_vTo, 1 << 20);
        return;
    }
    if (l_vTo.empty()) return;   // above / below the root: nowhere to go
    const NodePath l_vParent(l_vTo.begin(), l_vTo.end() - 1);
    m_pDoc->f_MoveNode(l_vFrom, l_vParent, l_vTo.back() + (l_eWhere == BelowItem ? 1 : 0));
}

bool AnimNodeTree::edit(const QModelIndex& l_oIndex, EditTrigger l_eTrigger, QEvent* l_pEvent)
{
    return l_oIndex.column() == ColName && QTreeWidget::edit(l_oIndex, l_eTrigger, l_pEvent);
}

// ---- AnimNodesDock ------------------------------------------------------------------------------

AnimNodesDock::AnimNodesDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Nodes"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTree(new AnimNodeTree(l_pDoc, this))
    , m_pToolBar(new QToolBar(this))
{
    setObjectName(QStringLiteral("AnimNodesDock"));
    m_pToolBar->setIconSize(QSize(16, 16));
    auto* l_pBody = new QWidget(this);
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(4, 2, 4, 4);
    l_pL->setSpacing(2);
    l_pL->addWidget(m_pToolBar);
    l_pL->addWidget(m_pTree, 1);
    setWidget(l_pBody);

    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimNodesDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, &AnimNodesDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_SoloChanged, this, &AnimNodesDock::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimNodesDock::f_Rebuild);   // "(auto: id)" follows the lookup
    connect(l_pDoc, &AnimDocument::f_SelectionChanged, this, &AnimNodesDock::f_SyncSelection);
    m_pTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_pTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& l_oPos) {
        QTreeWidgetItem* l_pIt = m_pTree->itemAt(l_oPos);
        if (!l_pIt || m_oContextActions.isEmpty()) return;
        m_pTree->setCurrentItem(l_pIt);   // the node right-clicked is the one the actions work on
        QMenu l_oMenu(this);
        for (QAction* l_pA : m_oContextActions) {
            if (l_pA) l_oMenu.addAction(l_pA);
            else l_oMenu.addSeparator();
        }
        l_oMenu.exec(m_pTree->viewport()->mapToGlobal(l_oPos));
    });
    connect(m_pTree, &QTreeWidget::itemSelectionChanged, this, [this] {
        if (m_bUpdating) return;
        const QList<QTreeWidgetItem*> l_oSel = m_pTree->selectedItems();
        if (!l_oSel.isEmpty()) m_pDoc->f_SelectNode(f_ItemPath(l_oSel.first()));
    });
    connect(m_pTree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* l_pIt, int l_iCol) {
        const NodePath l_vP = f_ItemPath(l_pIt);
        if (l_iCol == ColEye) m_pDoc->f_EditNode(l_vP, tr("Toggle visible"), [](Node& l_oN) { l_oN.m_bVisible = !l_oN.m_bVisible; });
        if (l_iCol == ColPlay) f_PlaybackMenu(l_vP, QCursor::pos());
        if (l_iCol == ColSolo) m_pDoc->f_SetSolo(m_pDoc->f_Solo() == l_vP ? std::nullopt : std::optional<NodePath>(l_vP));
    });
    connect(m_pTree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* l_pIt, int l_iCol) {
        if (m_bUpdating || l_iCol != ColName) return;
        const QString l_oName = l_pIt->text(ColName).trimmed();
        if (!m_pDoc->f_RenameNode(f_ItemPath(l_pIt), l_oName)) f_Rebuild();   // refused: show the old name again
    });
    connect(m_pTree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* l_pIt) { m_oCollapsed.insert(l_pIt->data(ColName, g_iPathRole).toString()); });
    connect(m_pTree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* l_pIt) { m_oCollapsed.remove(l_pIt->data(ColName, g_iPathRole).toString()); });
    connect(l_pDoc, &AnimDocument::f_FileReset, this, [this] { m_oCollapsed.clear(); });
    f_Rebuild();
}

void AnimNodesDock::f_BeginRename()
{
    if (QTreeWidgetItem* l_pIt = f_ItemFor(m_pDoc->f_SelectedPath())) m_pTree->editItem(l_pIt, ColName);
}

QTreeWidgetItem* AnimNodesDock::f_ItemFor(const NodePath& l_vPath) const
{
    QTreeWidgetItem* l_pIt = m_pTree->topLevelItem(0);
    for (int l_iI : l_vPath) {
        if (!l_pIt || l_iI >= l_pIt->childCount()) return nullptr;
        l_pIt = l_pIt->child(l_iI);
    }
    return l_pIt;
}

void AnimNodesDock::f_PlaybackMenu(const NodePath& l_vPath, const QPoint& l_oGlobalPos)
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_Clip();
    const Node* l_pNode = l_pClip ? animed::f_NodeAt(l_pClip->m_oRoot, l_vPath) : nullptr;
    if (!l_pNode) return;
    const int l_iCur = l_pNode->m_bLoop ? 2 : l_pNode->m_bStayAtLastFrame ? 0 : 1;
    QMenu l_oMenu(this);
    const QString l_arrNames[3] = {tr("Once, stay at the last key"), tr("Once, then hide"), tr("Loop")};
    const Icons::Id l_arrIcons[3] = {Icons::Id::Play, Icons::Id::Stop, Icons::Id::Loop};
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        QAction* l_pA = l_oMenu.addAction(Icons::f_Icon(l_arrIcons[l_iI]), l_arrNames[l_iI]);
        l_pA->setCheckable(true);
        l_pA->setChecked(l_iI == l_iCur);
        l_pA->setData(l_iI);
    }
    QAction* l_pChosen = l_oMenu.exec(l_oGlobalPos);
    if (!l_pChosen) return;
    const int l_iI = l_pChosen->data().toInt();
    m_pDoc->f_EditNode(l_vPath, tr("Playback"), [l_iI](Node& l_oN) {
        l_oN.m_bLoop = l_iI == 2;
        l_oN.m_bStayAtLastFrame = l_iI != 1;
    });
}

void AnimNodesDock::f_Rebuild()
{
    m_bUpdating = true;
    m_pTree->clear();
    if (const toms::anim::Clip* l_pClip = m_pDoc->f_Clip()) {
        std::function<void(QTreeWidgetItem*, const Node&, NodePath&)> l_fnAdd = [&](QTreeWidgetItem* l_pIt, const Node& l_oN, NodePath& l_vP) {
            l_pIt->setText(ColName, QString::fromStdString(l_oN.m_strName));
            l_pIt->setIcon(ColName, Icons::f_Icon(l_oN.m_strSprite.empty() && l_oN.m_vSpriteKeys.empty() ? Icons::Id::Node : Icons::Id::SpriteNode));
            l_pIt->setData(ColName, g_iPathRole, f_PathToString(l_vP));
            QString l_oTip = l_oN.m_strSprite.empty() ? tr("group") : tr("sprite: %1").arg(m_pDoc->f_SpriteDisplay(l_oN.m_strSprite));
            if (!l_oN.m_vSpriteKeys.empty()) {
                QStringList l_oKeyed;   // the sprites its keys switch to, in key order
                for (const auto& l_oK : l_oN.m_vSpriteKeys)
                    if (!l_oKeyed.contains(m_pDoc->f_SpriteDisplay(l_oK.m_oV))) l_oKeyed << m_pDoc->f_SpriteDisplay(l_oK.m_oV);
                l_oTip += tr("\nsprite keys: %1").arg(l_oKeyed.join(QStringLiteral(", ")));
            }
            if (l_oN.m_eBlend == toms::anim::Blend::Add) l_oTip += tr(", additive");
            l_pIt->setToolTip(ColName, l_oTip);
            if (!l_oN.m_strSprite.empty()) l_pIt->setText(ColSprite, m_pDoc->f_SpriteDisplay(l_oN.m_strSprite));
            else if (!l_oN.m_vSpriteKeys.empty()) l_pIt->setText(ColSprite, tr("(keys)"));
            l_pIt->setForeground(ColSprite, palette().color(QPalette::PlaceholderText));
            l_pIt->setToolTip(ColSprite, l_oTip);
            l_pIt->setText(ColOrder, QString::number(l_oN.m_iOrder));
            l_pIt->setTextAlignment(ColOrder, Qt::AlignCenter);
            l_pIt->setToolTip(ColOrder, tr("order: < 0 draws before the parent's own sprite"));
            // Playback: a looping / hiding node shows it; a plain one has a faint play icon to click.
            const QIcon l_oPlay = Icons::f_Icon(l_oN.m_bLoop ? Icons::Id::Loop : l_oN.m_bStayAtLastFrame ? Icons::Id::Play : Icons::Id::Stop);
            l_pIt->setIcon(ColPlay, l_oN.m_bLoop || !l_oN.m_bStayAtLastFrame ? l_oPlay : QIcon(l_oPlay.pixmap(m_pTree->iconSize(), QIcon::Disabled)));
            l_pIt->setToolTip(ColPlay, (l_oN.m_bLoop ? tr("Loop: plays its keys again, forever (also after the clip ends)")
                                     : l_oN.m_bStayAtLastFrame ? tr("Once, stay at the last key")
                                                         : tr("Once, then hide (with its children)")) +
                                        tr("\nClick to change"));
            const bool l_bSolo = m_pDoc->f_Solo() == l_vP;
            const QIcon l_oTarget = Icons::f_Icon(Icons::Id::Solo);
            l_pIt->setIcon(ColSolo, l_bSolo ? l_oTarget : QIcon(l_oTarget.pixmap(m_pTree->iconSize(), QIcon::Disabled)));
            l_pIt->setToolTip(ColSolo, l_bSolo ? tr("Solo: only this node and its children are drawn (click to end)")
                                         : tr("Solo: draw only this node and its children"));
            if (m_pDoc->f_Solo() && !m_pDoc->f_SoloShows(&l_oN))   // not drawn now
                l_pIt->setForeground(ColName, palette().color(QPalette::PlaceholderText));
            l_pIt->setIcon(ColEye, Icons::f_Icon(l_oN.m_bVisible ? Icons::Id::Eye : Icons::Id::EyeOff));
            l_pIt->setToolTip(ColEye, tr("Rest visibility (click to toggle)"));
            Qt::ItemFlags l_oF = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDropEnabled;
            if (!l_vP.empty()) l_oF |= Qt::ItemIsDragEnabled;
            l_pIt->setFlags(l_oF);
            if (!l_oN.m_bVisible) l_pIt->setForeground(ColName, palette().color(QPalette::PlaceholderText));
            for (size_t l_nI = 0; l_nI < l_oN.m_vChildren.size(); l_nI++) {
                l_vP.push_back(int(l_nI));
                l_fnAdd(new QTreeWidgetItem(l_pIt), l_oN.m_vChildren[l_nI], l_vP);
                l_vP.pop_back();
            }
            l_pIt->setExpanded(!m_oCollapsed.contains(f_PathToString(l_vP)));
        };
        NodePath l_vP;
        auto* l_pRoot = new QTreeWidgetItem(m_pTree);
        l_fnAdd(l_pRoot, l_pClip->m_oRoot, l_vP);
    }
    m_bUpdating = false;
    f_SyncSelection();
}

void AnimNodesDock::f_SyncSelection()
{
    m_bUpdating = true;
    QTreeWidgetItem* l_pIt = f_ItemFor(m_pDoc->f_SelectedPath());
    if (l_pIt) {
        m_pTree->setCurrentItem(l_pIt);
        m_pTree->scrollToItem(l_pIt);
    } else {
        m_pTree->clearSelection();
    }
    m_bUpdating = false;
}

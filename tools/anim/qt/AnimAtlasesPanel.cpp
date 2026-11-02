#include "AnimAtlasesPanel.h"

#include "Icons.h"

#include <QApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QToolButton* f_ToolButton(QWidget* l_pParent, Icons::Id l_eIcon, const QString& l_oTip)
{
    auto* l_pB = new QToolButton(l_pParent);
    l_pB->setIcon(Icons::f_Icon(l_eIcon));
    l_pB->setToolTip(l_oTip);
    l_pB->setAutoRaise(true);
    l_pB->setIconSize(QSize(16, 16));
    return l_pB;
}

// --selftest runs without anyone to answer a dialog: prompts take their default.
bool f_SelftestRunning() { return qApp && qApp->property("toms.selftest").toBool(); }

}  // namespace

AnimAtlasesPanel::AnimAtlasesPanel(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pDoc(l_pDoc)
{
    // Three columns -- the id, the path (elided in the middle) and the sprite count -- so a long
    // relative path never needs a horizontal scroll bar.
    m_pList = new QTreeWidget(this);
    m_pList->setObjectName(QStringLiteral("AnimAtlasesList"));
    m_pList->setColumnCount(3);
    m_pList->setHeaderHidden(true);
    m_pList->setRootIsDecorated(false);
    m_pList->setUniformRowHeights(true);
    m_pList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pList->setEditTriggers(QAbstractItemView::NoEditTriggers);   // ids: double-click (below), F2
    m_pList->setTextElideMode(Qt::ElideMiddle);
    m_pList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pList->header()->setStretchLastSection(false);
    m_pList->header()->setSectionResizeMode(ColId, QHeaderView::ResizeToContents);
    m_pList->header()->setSectionResizeMode(ColPath, QHeaderView::Stretch);
    m_pList->header()->setSectionResizeMode(ColCount, QHeaderView::ResizeToContents);
    m_pList->setToolTip(tr("Sprite references name an atlas by its id (id:name); a bare name is looked up in each atlas in this "
                          "order, the first that has it wins"));
    m_pAdd = f_ToolButton(this, Icons::Id::Add, tr("Add an atlas (stored relative to the .anim)"));
    m_pRemove = f_ToolButton(this, Icons::Id::Remove, tr("Remove the selected atlas from the list"));
    m_pUp = f_ToolButton(this, Icons::Id::Up, tr("Look the selected atlas up earlier"));
    m_pDown = f_ToolButton(this, Icons::Id::Down, tr("Look the selected atlas up later"));
    m_pQualify = new QToolButton(this);
    m_pQualify->setText(tr("Qualify"));
    m_pQualify->setAutoRaise(true);
    m_pQualify->setToolTip(tr("Qualify sprite references: store the atlas with every bare sprite name (name -> id:name), "
                             "using the atlas the lookup picks now"));
    m_pReload = f_ToolButton(this, Icons::Id::Loop, tr("Reload every atlas from disk (Ctrl+Shift+R)"));

    auto* l_pButtons = new QHBoxLayout;
    l_pButtons->setContentsMargins(0, 0, 0, 0);
    l_pButtons->setSpacing(1);
    for (QToolButton* l_pB : {m_pAdd, m_pRemove, m_pUp, m_pDown, m_pQualify}) l_pButtons->addWidget(l_pB);
    l_pButtons->addStretch(1);
    l_pButtons->addWidget(m_pReload);
    auto* l_pL = new QVBoxLayout(this);
    l_pL->setContentsMargins(0, 0, 0, 0);
    l_pL->setSpacing(2);
    l_pL->addWidget(m_pList);
    l_pL->addLayout(l_pButtons);

    connect(m_pAdd, &QToolButton::clicked, this, &AnimAtlasesPanel::f_AddRequested);
    connect(m_pRemove, &QToolButton::clicked, this, [this] { f_RemoveAtlas(f_CurrentIndex()); });
    connect(m_pUp, &QToolButton::clicked, this, [this] {
        const int l_iI = f_CurrentIndex();
        if (m_pDoc->f_MoveAtlas(l_iI, -1)) f_SetCurrentIndex(l_iI - 1);
    });
    connect(m_pDown, &QToolButton::clicked, this, [this] {
        const int l_iI = f_CurrentIndex();
        if (m_pDoc->f_MoveAtlas(l_iI, 1)) f_SetCurrentIndex(l_iI + 1);
    });
    connect(m_pQualify, &QToolButton::clicked, m_pDoc, &AnimDocument::f_QualifySpriteReferences);
    connect(m_pReload, &QToolButton::clicked, m_pDoc, &AnimDocument::f_ReloadAtlases);
    connect(m_pList, &QTreeWidget::currentItemChanged, this, &AnimAtlasesPanel::f_UpdateButtons);
    connect(m_pList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* l_pIt, int) {
        if (l_pIt->flags() & Qt::ItemIsEditable) m_pList->editItem(l_pIt, ColId);
    });
    connect(m_pList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* l_pIt, int l_iCol) {
        if (m_bUpdating || l_iCol != ColId) return;
        const int l_iRow = m_pList->indexOfTopLevelItem(l_pIt);
        const QString l_oId = l_pIt->text(ColId).trimmed();
        // Rebuilding inside itemChanged would delete the item being committed: do it right after.
        QMetaObject::invokeMethod(this, [this, l_iRow, l_oId] {
            if (!f_RenameId(l_iRow, l_oId)) f_Rebuild();   // refused or unchanged: show the old id again
        }, Qt::QueuedConnection);
    });
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimAtlasesPanel::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_FilePathChanged, this, &AnimAtlasesPanel::f_Rebuild);
    connect(l_pDoc, &AnimDocument::f_FileChanged, this, &AnimAtlasesPanel::f_UpdateButtons);
    f_Rebuild();
}

int AnimAtlasesPanel::f_CurrentIndex() const
{
    const int l_iRow = m_pList->indexOfTopLevelItem(m_pList->currentItem());
    return l_iRow >= 0 && l_iRow < m_pDoc->f_AtlasCount() ? l_iRow : -1;
}

void AnimAtlasesPanel::f_SetCurrentIndex(int l_iIndex)
{
    if (l_iIndex >= 0 && l_iIndex < m_pDoc->f_AtlasCount()) m_pList->setCurrentItem(m_pList->topLevelItem(l_iIndex));
}

void AnimAtlasesPanel::f_Warn(const QString& l_oTitle, const QString& l_oText)
{
    emit m_pDoc->f_Message(l_oText);
    if (!f_SelftestRunning()) QMessageBox::warning(this, l_oTitle, l_oText);
}

bool AnimAtlasesPanel::f_RenameId(int l_iIndex, const QString& l_oId)
{
    if (l_iIndex < 0 || l_iIndex >= m_pDoc->f_AtlasCount() || l_oId == m_pDoc->f_AtlasId(l_iIndex)) return false;
    QString l_oErr;
    if (m_pDoc->f_RenameAtlasId(l_iIndex, l_oId, &l_oErr)) return true;
    if (!l_oErr.isEmpty()) f_Warn(tr("Atlas Id"), l_oErr);
    return false;
}

bool AnimAtlasesPanel::f_RemoveAtlas(int l_iIndex)
{
    if (l_iIndex < 0 || l_iIndex >= m_pDoc->f_AtlasCount()) return false;
    const int l_iRefs = m_pDoc->f_AtlasReferenceCount(l_iIndex);
    if (l_iRefs > 0 && !f_SelftestRunning() &&
        QMessageBox::question(this, tr("Remove Atlas"),
                              tr("%n sprite reference(s) use the atlas '%1' (%2).\n\nWithout it they become errors "
                                 "(see Problems). Remove it anyway?", nullptr, l_iRefs)
                                  .arg(m_pDoc->f_AtlasId(l_iIndex), m_pDoc->f_StoredAtlasPath(l_iIndex)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return false;
    const QString l_oId = m_pDoc->f_AtlasId(l_iIndex);
    if (!m_pDoc->f_RemoveAtlas(l_iIndex)) return false;
    if (l_iRefs > 0) emit m_pDoc->f_Message(tr("Removed the atlas '%1': %n sprite reference(s) no longer resolve.", nullptr, l_iRefs).arg(l_oId));
    return true;
}

void AnimAtlasesPanel::f_Rebuild()
{
    const int l_iKeep = f_CurrentIndex();
    m_bUpdating = true;
    m_pList->clear();
    const QColor l_oBad(236, 96, 96);
    QFont l_oIdFont = m_pList->font();
    l_oIdFont.setBold(true);
    for (int l_iI = 0; l_iI < m_pDoc->f_AtlasCount(); l_iI++) {
        const AnimDocument::LoadedAtlas& l_oA = m_pDoc->f_AtlasAt(l_iI);
        auto* l_pIt = new QTreeWidgetItem(m_pList);
        l_pIt->setFlags(l_pIt->flags() | Qt::ItemIsEditable);
        l_pIt->setText(ColId, m_pDoc->f_AtlasId(l_iI));
        l_pIt->setFont(ColId, l_oIdFont);
        l_pIt->setText(ColPath, m_pDoc->f_StoredAtlasPath(l_iI));
        QString l_oTip = QDir::toNativeSeparators(l_oA.m_oPath);
        if (l_oA.m_bOk) {
            l_pIt->setIcon(ColId, Icons::f_Icon(Icons::Id::Grid));
            l_pIt->setText(ColCount, l_oA.m_oSpriteNames.size() == 1 ? tr("1 sprite") : tr("%1 sprites").arg(l_oA.m_oSpriteNames.size()));
        } else {
            l_pIt->setIcon(ColId, Icons::f_Icon(Icons::Id::Error));
            l_pIt->setText(ColCount, tr("not loaded"));
            for (int l_iC = 0; l_iC < 3; l_iC++) l_pIt->setForeground(l_iC, l_oBad);
            l_oTip += QStringLiteral("\n") + l_oA.m_oError;
        }
        if (m_pDoc->f_AtlasPathIsAbsoluteInFile(l_iI)) l_oTip += tr("\nThe file names it with an absolute path: it is stored relative on save.");
        else if (m_pDoc->f_FilePath().isEmpty()) l_oTip += tr("\nUntitled file: the path becomes relative when the .anim is saved.");
        l_pIt->setToolTip(ColId, tr("Id '%1': sprites from this atlas are stored as %1:name. Double-click to rename it "
                                 "(every reference is rewritten).").arg(m_pDoc->f_AtlasId(l_iI)) +
                                  QStringLiteral("\n") + l_oTip);
        l_pIt->setToolTip(ColPath, l_oTip);
        l_pIt->setToolTip(ColCount, l_oTip);
        l_pIt->setTextAlignment(ColCount, Qt::AlignRight | Qt::AlignVCenter);
    }
    if (m_pDoc->f_AtlasCount() == 0) {
        auto* l_pIt = new QTreeWidgetItem(m_pList, {tr("(no atlas: + adds one)")});
        l_pIt->setFirstColumnSpanned(true);
        l_pIt->setFlags(Qt::NoItemFlags);
    }
    // Compact: as tall as its rows (2..5).
    const int l_iRows = std::clamp(m_pList->topLevelItemCount(), 2, 5);
    const int l_iRowH = std::max(m_pList->sizeHintForRow(0), std::max(m_pList->fontMetrics().height(), m_pList->iconSize().height()) + 6);
    m_pList->setFixedHeight(l_iRows * l_iRowH + 2 * m_pList->frameWidth());
    f_SetCurrentIndex(l_iKeep);
    m_bUpdating = false;
    f_UpdateButtons();
}

void AnimAtlasesPanel::f_UpdateButtons()
{
    const int l_iI = f_CurrentIndex();
    m_pRemove->setEnabled(l_iI >= 0);
    m_pUp->setEnabled(l_iI > 0);
    m_pDown->setEnabled(l_iI >= 0 && l_iI + 1 < m_pDoc->f_AtlasCount());
    m_pReload->setEnabled(m_pDoc->f_AtlasCount() > 0);
    m_pQualify->setEnabled(m_pDoc->f_BareSpriteReferenceCount() > 0);
}

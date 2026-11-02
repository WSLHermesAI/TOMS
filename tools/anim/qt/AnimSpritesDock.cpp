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

const char* g_pszSpriteMime = AnimSpriteList::m_pszMime;
constexpr int g_iAtlasRole = Qt::UserRole + 1;    // sprite rows: the atlas index
constexpr int g_iRefRole = AnimSpriteList::m_iRefRole;

bool f_IsSprite(const QListWidgetItem* l_pIt) { return l_pIt->data(g_iAtlasRole).isValid(); }
QString f_RefOf(const QListWidgetItem* l_pIt) { return l_pIt->data(g_iRefRole).toString(); }

// The thumbnail with a small badge in the corner: a name more than one atlas has.
QIcon f_Badged(const QIcon& l_oIcon)
{
    QPixmap l_oPm = l_oIcon.pixmap(64, 64);
    {
        QPainter l_oP(&l_oPm);
        Icons::f_Icon(Icons::Id::Info).paint(&l_oP, QRect(l_oPm.width() - 26, l_oPm.height() - 26, 26, 26));
    }
    return QIcon(l_oPm);
}

}  // namespace

QStringList AnimSpriteList::f_RefsFrom(const QMimeData* l_pM)
{
    if (!l_pM || !l_pM->hasFormat(QLatin1String(m_pszMime))) return {};
    return QString::fromUtf8(l_pM->data(QLatin1String(m_pszMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QStringList AnimSpriteList::mimeTypes() const { return {QLatin1String(g_pszSpriteMime), QStringLiteral("text/plain")}; }

QMimeData* AnimSpriteList::mimeData(const QList<QListWidgetItem*>& l_oItems) const
{
    QStringList l_oRefs;
    for (const QListWidgetItem* l_pIt : l_oItems)
        if (f_IsSprite(l_pIt)) l_oRefs << f_RefOf(l_pIt);
    auto* l_pM = new QMimeData;
    l_pM->setData(QLatin1String(g_pszSpriteMime), l_oRefs.join(QLatin1Char('\n')).toUtf8());
    l_pM->setText(l_oRefs.join(QLatin1Char('\n')));
    return l_pM;
}

AnimSpritesDock::AnimSpritesDock(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Sprites"), l_pParent)
    , m_pDoc(l_pDoc)
{
    setObjectName(QStringLiteral("AnimSpritesDock"));
    auto* l_pBody = new QWidget(this);
    m_pFilter = new QLineEdit(l_pBody);
    m_pFilter->setPlaceholderText(tr("Filter sprites (all atlases)…"));
    m_pFilter->setClearButtonEnabled(true);
    m_pTabs = new QTabWidget(l_pBody);
    m_pTabs->setDocumentMode(true);
    m_pTabs->setUsesScrollButtons(true);
    m_pTabs->tabBar()->setElideMode(Qt::ElideRight);
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(4, 4, 4, 4);
    l_pL->setSpacing(4);
    l_pL->addWidget(m_pFilter);
    l_pL->addWidget(m_pTabs, 1);
    setWidget(l_pBody);

    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimSpritesDock::f_Rebuild);
    connect(m_pFilter, &QLineEdit::textChanged, this, &AnimSpritesDock::f_ApplyFilter);
    connect(m_pTabs, &QTabWidget::currentChanged, this, [this](int l_iI) {
        if (l_iI >= 0 && l_iI < m_pDoc->f_AtlasCount()) m_oCurrentAtlas = m_pDoc->f_AtlasAt(l_iI).m_oPath;
    });
    f_Rebuild();
}

QString AnimSpritesDock::f_TabTitle(int l_iA, int l_iShown) const
{
    const AnimDocument::LoadedAtlas& l_oAtlas = m_pDoc->f_AtlasAt(l_iA);
    if (!l_oAtlas.m_bOk) return m_pDoc->f_AtlasId(l_iA);
    const int l_iTotal = l_oAtlas.m_oSpriteNames.size();
    return l_iShown == l_iTotal ? QStringLiteral("%1 (%2)").arg(m_pDoc->f_AtlasId(l_iA)).arg(l_iTotal)
                          : QStringLiteral("%1 (%2/%3)").arg(m_pDoc->f_AtlasId(l_iA)).arg(l_iShown).arg(l_iTotal);
}

void AnimSpritesDock::f_Rebuild()
{
    const QStringList l_oSelected = f_SelectedSprites();
    const QString l_oKeep = m_oCurrentAtlas;
    const QSignalBlocker l_oBlock(m_pTabs);
    while (m_pTabs->count() > 0) {
        QWidget* l_pW = m_pTabs->widget(0);
        m_pTabs->removeTab(0);
        delete l_pW;
    }
    m_oLists.clear();

    // Which atlases have each name, in lookup order.
    QHash<QString, QList<int>> l_oOwners;
    for (int l_iA = 0; l_iA < m_pDoc->f_AtlasCount(); l_iA++)
        if (m_pDoc->f_AtlasAt(l_iA).m_bOk)
            for (const QString& l_oName : m_pDoc->f_AtlasAt(l_iA).m_oSpriteNames) l_oOwners[l_oName] << l_iA;

    int l_iCurrent = 0;
    for (int l_iA = 0; l_iA < m_pDoc->f_AtlasCount(); l_iA++) {
        const AnimDocument::LoadedAtlas& l_oAtlas = m_pDoc->f_AtlasAt(l_iA);
        const QString l_oId = m_pDoc->f_AtlasId(l_iA);
        if (l_oAtlas.m_oPath == l_oKeep) l_iCurrent = l_iA;
        QString l_oTip = tr("Atlas id '%1': its sprites are stored as %1:name").arg(l_oId) + QStringLiteral("\n") +
                      QDir::toNativeSeparators(l_oAtlas.m_oPath);
        if (!l_oAtlas.m_bOk) {
            auto* l_pMsg = new QLabel(tr("This atlas did not load:\n%1").arg(l_oAtlas.m_oError), m_pTabs);
            l_pMsg->setWordWrap(true);
            l_pMsg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            l_pMsg->setMargin(8);
            m_pTabs->addTab(l_pMsg, Icons::f_Icon(Icons::Id::Error), f_TabTitle(l_iA, 0));
            m_pTabs->setTabToolTip(l_iA, l_oTip + QStringLiteral("\n") + l_oAtlas.m_oError);
            m_oLists << nullptr;
            continue;
        }
        auto* l_pList = new AnimSpriteList(m_pTabs);
        l_pList->setIconSize(QSize(28, 28));
        l_pList->setUniformItemSizes(true);
        l_pList->setSelectionMode(QAbstractItemView::ExtendedSelection);
        l_pList->setDragEnabled(true);
        l_pList->setDragDropMode(QAbstractItemView::DragOnly);
        l_pList->setToolTip(tr("Drag into the viewport to add a node; drag several onto the scrubber / Keys list / Timeline "
                            "to insert them as sprite keys; double-click to give the selected node this sprite"));
        connect(l_pList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* l_pIt) {
            if (f_IsSprite(l_pIt)) m_pDoc->f_AssignSprite(f_RefOf(l_pIt));
        });
        for (const QString& l_oName : l_oAtlas.m_oSpriteNames) {
            const QString l_oRef = QString::fromStdString(m_pDoc->f_SpriteRefFor(l_iA, l_oName));
            auto* l_pIt = new QListWidgetItem(l_oName, l_pList);
            l_pIt->setData(g_iAtlasRole, l_iA);
            l_pIt->setData(g_iRefRole, l_oRef);
            const QImage l_oImg = m_pDoc->f_SpriteImage(l_iA, l_oName);
            QIcon l_oIcon = Icons::f_SpriteIcon(SpriteImageCache::f_Thumbnail(l_oImg, 64), false, false, false);
            QString l_oItTip = QStringLiteral("%1  (%2×%3)\n").arg(l_oRef).arg(l_oImg.width()).arg(l_oImg.height());
            const QList<int> l_oIn = l_oOwners.value(l_oName);
            if (l_oIn.size() > 1) {
                QStringList l_oOthers;
                for (int l_iO : l_oIn)
                    if (l_iO != l_iA) l_oOthers << m_pDoc->f_AtlasId(l_iO);
                l_oIcon = f_Badged(l_oIcon);
                l_oItTip += tr("Also in %1. Each copy is usable: picking this one stores %2, which always draws from '%3'.")
                             .arg(l_oOthers.join(QStringLiteral(", ")), l_oRef, l_oId);
            } else {
                l_oItTip += tr("Picking it stores %1.").arg(l_oRef);
            }
            l_pIt->setIcon(l_oIcon);
            l_pIt->setToolTip(l_oItTip);
        }
        m_pTabs->addTab(l_pList, f_TabTitle(l_iA, l_oAtlas.m_oSpriteNames.size()));
        m_pTabs->setTabToolTip(l_iA, l_oTip);
        m_oLists << l_pList;
    }
    if (m_pDoc->f_AtlasCount() == 0) {
        auto* l_pMsg = new QLabel(tr("No atlas yet: add one under Properties > Atlases (or File > Add Atlas…)."), m_pTabs);
        l_pMsg->setWordWrap(true);
        l_pMsg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        l_pMsg->setMargin(8);
        m_pTabs->addTab(l_pMsg, tr("(no atlas)"));
    }
    m_pTabs->setCurrentIndex(l_iCurrent);
    if (l_iCurrent < m_pDoc->f_AtlasCount()) m_oCurrentAtlas = m_pDoc->f_AtlasAt(l_iCurrent).m_oPath;
    setWindowTitle(m_pDoc->f_SpriteNames().isEmpty() ? tr("Sprites") : tr("Sprites (%1)").arg(m_pDoc->f_SpriteNames().size()));
    f_ApplyFilter();
    if (!l_oSelected.isEmpty()) f_SelectSprites(l_oSelected);
}

void AnimSpritesDock::f_ApplyFilter()
{
    const QString l_oF = m_pFilter->text().trimmed();
    for (int l_iA = 0; l_iA < m_oLists.size(); l_iA++) {
        AnimSpriteList* l_pList = m_oLists[l_iA];
        if (!l_pList) continue;
        int l_iShown = 0;
        for (int l_iI = 0; l_iI < l_pList->count(); l_iI++) {
            QListWidgetItem* l_pIt = l_pList->item(l_iI);
            const bool l_bHide = !l_oF.isEmpty() && !f_RefOf(l_pIt).contains(l_oF, Qt::CaseInsensitive);
            l_pIt->setHidden(l_bHide);
            l_iShown += !l_bHide;
        }
        m_pTabs->setTabText(l_iA, f_TabTitle(l_iA, l_iShown));
    }
}

AnimSpriteList* AnimSpritesDock::f_List() const { return f_ListFor(m_pTabs->currentIndex()); }

AnimSpriteList* AnimSpritesDock::f_ListFor(int l_iAtlasIndex) const
{
    return l_iAtlasIndex >= 0 && l_iAtlasIndex < m_oLists.size() ? m_oLists[l_iAtlasIndex] : nullptr;
}

QStringList AnimSpritesDock::f_SelectedSprites() const
{
    QStringList l_oOut;
    const AnimSpriteList* l_pL = f_List();
    if (!l_pL) return l_oOut;
    for (int l_iI = 0; l_iI < l_pL->count(); l_iI++) {
        const QListWidgetItem* l_pIt = l_pL->item(l_iI);
        if (l_pIt->isSelected() && f_IsSprite(l_pIt) && !l_oOut.contains(f_RefOf(l_pIt))) l_oOut << f_RefOf(l_pIt);
    }
    return l_oOut;
}

QString AnimSpritesDock::f_CurrentSprite() const
{
    const AnimSpriteList* l_pL = f_List();
    const QListWidgetItem* l_pIt = l_pL ? l_pL->currentItem() : nullptr;
    return l_pIt && l_pIt->isSelected() && f_IsSprite(l_pIt) ? f_RefOf(l_pIt) : QString();
}

QListWidgetItem* AnimSpritesDock::f_SpriteItem(int l_iAtlasIndex, const QString& l_oName) const
{
    const AnimSpriteList* l_pL = f_ListFor(l_iAtlasIndex);
    if (!l_pL) return nullptr;
    for (int l_iI = 0; l_iI < l_pL->count(); l_iI++) {
        QListWidgetItem* l_pIt = l_pL->item(l_iI);
        if (l_pIt->text() == l_oName) return l_pIt;
    }
    return nullptr;
}

void AnimSpritesDock::f_SelectSprites(const QStringList& l_oRefs)
{
    for (AnimSpriteList* l_pL : m_oLists)
        if (l_pL) l_pL->clearSelection();
    bool l_bShown = false;
    for (const QString& l_oR : l_oRefs) {
        // A bare name: the copy the lookup picks.
        const toms::anim::SpriteRef l_oS = toms::anim::f_ParseSpriteRef(l_oR.toStdString());
        int l_iAtlas = l_oS.m_strAtlas.empty() ? -1 : m_pDoc->f_AtlasIndexOf(QString::fromStdString(l_oS.m_strAtlas));
        if (l_oS.m_strAtlas.empty()) m_pDoc->f_FindSprite(l_oS.m_strName, &l_iAtlas);
        QListWidgetItem* l_pIt = f_SpriteItem(l_iAtlas, QString::fromStdString(l_oS.m_strName));
        if (!l_pIt) continue;
        if (!l_bShown) {   // the first reference's tab comes to the front
            m_pTabs->setCurrentIndex(l_iAtlas);
            l_bShown = true;
        }
        if (l_iAtlas == m_pTabs->currentIndex()) {
            l_pIt->setSelected(true);
            l_pIt->listWidget()->scrollToItem(l_pIt);
        }
    }
}

void AnimSpritesDock::f_FocusFilter()
{
    m_pFilter->setFocus();
    m_pFilter->selectAll();
}

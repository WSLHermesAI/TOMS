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
const char* g_pszSpriteMime = "application/x-toms-sprite";
}

QStringList FxSpriteList::mimeTypes() const { return {QLatin1String(g_pszSpriteMime), QStringLiteral("text/plain")}; }

QMimeData* FxSpriteList::mimeData(const QList<QListWidgetItem*>& l_oItems) const
{
    // In list order, not click order: a frame sequence drops as it is listed.
    QList<QListWidgetItem*> l_oSorted = l_oItems;
    std::sort(l_oSorted.begin(), l_oSorted.end(), [this](QListWidgetItem* l_pA, QListWidgetItem* l_pB) { return row(l_pA) < row(l_pB); });
    QStringList l_oRefs;
    for (const QListWidgetItem* l_pIt : l_oSorted) l_oRefs << l_pIt->data(m_iRefRole).toString();
    auto* l_pM = new QMimeData;
    l_pM->setData(QLatin1String(g_pszSpriteMime), l_oRefs.join(QLatin1Char('\n')).toUtf8());
    l_pM->setText(l_oRefs.join(QLatin1Char('\n')));
    return l_pM;
}

FxSpritesDock::FxSpritesDock(ParticleDocument* l_pDoc, QWidget* l_pParent)
    : QDockWidget(tr("Sprites"), l_pParent)
    , m_pDoc(l_pDoc)
    , m_pFilter(new QLineEdit(this))
    , m_pTabs(new QTabWidget(this))
{
    setObjectName(QStringLiteral("ParticleSpritesDock"));
    m_pFilter->setPlaceholderText(tr("Filter sprites…"));
    m_pFilter->setClearButtonEnabled(true);
    m_pTabs->setDocumentMode(true);
    auto* l_pAdd = new QToolButton(this);
    l_pAdd->setIcon(Icons::f_Icon(Icons::Id::Add));
    l_pAdd->setToolTip(tr("Add an atlas"));
    auto* l_pRemove = new QToolButton(this);
    l_pRemove->setIcon(Icons::f_Icon(Icons::Id::Remove));
    l_pRemove->setToolTip(tr("Remove the shown atlas from the file"));
    auto* l_pTop = new QHBoxLayout;
    l_pTop->addWidget(m_pFilter, 1);
    l_pTop->addWidget(l_pAdd);
    l_pTop->addWidget(l_pRemove);
    auto* l_pBody = new QWidget(this);
    auto* l_pL = new QVBoxLayout(l_pBody);
    l_pL->setContentsMargins(4, 4, 4, 4);
    l_pL->addLayout(l_pTop);
    l_pL->addWidget(m_pTabs, 1);
    setWidget(l_pBody);
    connect(l_pAdd, &QToolButton::clicked, this, &FxSpritesDock::f_AddAtlasRequested);
    connect(l_pRemove, &QToolButton::clicked, this, [this] {
        const int l_iI = m_pTabs->currentIndex();
        if (l_iI < 0 || l_iI >= m_pDoc->f_AtlasCount()) return;
        if (!qApp->property("toms.selftest").toBool() &&
            QMessageBox::question(this, tr("Remove Atlas"), tr("Remove the atlas '%1' from this file? Sprites from it stop drawing.")
                                                                .arg(m_pDoc->f_AtlasId(l_iI))) != QMessageBox::Yes)
            return;
        m_pDoc->f_RemoveAtlas(l_iI);
    });
    connect(m_pFilter, &QLineEdit::textChanged, this, &FxSpritesDock::f_ApplyFilter);
    connect(l_pDoc, &ParticleDocument::f_AtlasChanged, this, &FxSpritesDock::f_Rebuild);
    f_Rebuild();
}

FxSpriteList* FxSpritesDock::f_ListFor(int l_iAtlas) const { return l_iAtlas >= 0 && l_iAtlas < m_oLists.size() ? m_oLists[l_iAtlas] : nullptr; }

QStringList FxSpritesDock::f_SelectedRefs() const
{
    QStringList l_oOut;
    const FxSpriteList* l_pL = f_ListFor(m_pTabs->currentIndex());
    if (!l_pL) return l_oOut;
    for (int l_iI = 0; l_iI < l_pL->count(); l_iI++)
        if (l_pL->item(l_iI)->isSelected()) l_oOut << l_pL->item(l_iI)->data(FxSpriteList::m_iRefRole).toString();
    return l_oOut;
}

void FxSpritesDock::f_Rebuild()
{
    const int l_iKeep = m_pTabs->currentIndex();
    while (m_pTabs->count()) {
        QWidget* l_pW = m_pTabs->widget(0);
        m_pTabs->removeTab(0);
        delete l_pW;
    }
    m_oLists.clear();
    for (int l_iA = 0; l_iA < m_pDoc->f_AtlasCount(); l_iA++) {
        const ParticleDocument::LoadedAtlas& l_oAtlas = m_pDoc->f_AtlasAt(l_iA);
        if (!l_oAtlas.m_bOk) {
            auto* l_pMsg = new QLabel(tr("This atlas did not load:\n%1").arg(l_oAtlas.m_oError));
            l_pMsg->setWordWrap(true);
            l_pMsg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            m_pTabs->addTab(l_pMsg, Icons::f_Icon(Icons::Id::Error), m_pDoc->f_AtlasId(l_iA));
            m_oLists << nullptr;
            continue;
        }
        auto* l_pList = new FxSpriteList;
        l_pList->setViewMode(QListView::IconMode);
        l_pList->setIconSize(QSize(40, 40));
        l_pList->setGridSize(QSize(72, 64));
        l_pList->setResizeMode(QListView::Adjust);
        l_pList->setMovement(QListView::Static);
        l_pList->setSelectionMode(QAbstractItemView::ExtendedSelection);
        l_pList->setDragEnabled(true);
        l_pList->setDragDropMode(QAbstractItemView::DragOnly);
        l_pList->setToolTip(tr("Double-click: the selected emitter draws it. Drag into the viewport: a new emitter "
                            "(several: a flipbook). Drag onto the Inspector's Frames: flipbook frames."));
        for (const QString& l_oName : l_oAtlas.m_oSpriteNames) {
            const QString l_oRef = m_pDoc->f_AtlasId(l_iA) + QLatin1Char(':') + l_oName;
            const QImage l_oImg = m_pDoc->f_SpriteImage(l_oRef);
            // On a dark tile: the fx sprites are white.
            QPixmap l_oTile(40, 40);
            l_oTile.fill(QColor(40, 40, 48));
            if (!l_oImg.isNull()) {
                QPainter l_oP(&l_oTile);
                const QImage l_oS = l_oImg.scaled(38, 38, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                l_oP.drawImage(QPoint((40 - l_oS.width()) / 2, (40 - l_oS.height()) / 2), l_oS);
            }
            auto* l_pIt = new QListWidgetItem(QIcon(l_oTile), l_oName, l_pList);
            l_pIt->setData(FxSpriteList::m_iRefRole, l_oRef);
            l_pIt->setToolTip(QStringLiteral("%1  (%2×%3)").arg(l_oRef).arg(l_oImg.width()).arg(l_oImg.height()));
        }
        connect(l_pList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* l_pIt) {
            const std::string l_strRef = l_pIt->data(FxSpriteList::m_iRefRole).toString().toStdString();
            m_pDoc->f_EditEmitter(tr("Sprite"), [&](toms::fx::Emitter& l_oM) {
                l_oM.m_strSprite = l_strRef;
                l_oM.m_vFrames.clear();
            });
        });
        m_pTabs->addTab(l_pList, QStringLiteral("%1 (%2)").arg(m_pDoc->f_AtlasId(l_iA)).arg(l_oAtlas.m_oSpriteNames.size()));
        m_pTabs->setTabToolTip(l_iA, QDir::toNativeSeparators(l_oAtlas.m_oPath));
        m_oLists << l_pList;
    }
    if (m_pDoc->f_AtlasCount() == 0) {
        auto* l_pMsg = new QLabel(tr("No atlas: add one with + (the TOMS particle sprites are assets/media/atlas/fx.atlas)."));
        l_pMsg->setWordWrap(true);
        l_pMsg->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        m_pTabs->addTab(l_pMsg, tr("(no atlas)"));
    }
    if (l_iKeep >= 0 && l_iKeep < m_pTabs->count()) m_pTabs->setCurrentIndex(l_iKeep);
    f_ApplyFilter();
}

void FxSpritesDock::f_ApplyFilter()
{
    const QString l_oF = m_pFilter->text().trimmed();
    for (FxSpriteList* l_pL : m_oLists)
        if (l_pL)
            for (int l_iI = 0; l_iI < l_pL->count(); l_iI++) l_pL->item(l_iI)->setHidden(!l_oF.isEmpty() && !l_pL->item(l_iI)->text().contains(l_oF, Qt::CaseInsensitive));
}

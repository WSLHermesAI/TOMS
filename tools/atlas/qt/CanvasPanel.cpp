#include "CanvasPanel.h"

#include "AtlasCanvas.h"
#include "AtlasDocument.h"
#include "Icons.h"
#include "SpriteEditCanvas.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

CanvasPanel::CanvasPanel(AtlasDocument* l_pDoc, QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTabs(new QTabBar(this))
    , m_pEditBar(new QWidget(this))
    , m_pBreadcrumb(new QLabel(m_pEditBar))
    , m_pStack(new QStackedWidget(this))
    , m_pAtlas(new AtlasCanvas(l_pDoc, m_pStack))
    , m_pEdit(new SpriteEditCanvas(l_pDoc, m_pStack))
{
    m_pTabs->setDocumentMode(true);
    m_pTabs->setExpanding(false);
    m_pTabs->setDrawBase(false);

    auto* l_pBack = new QToolButton(m_pEditBar);
    l_pBack->setIcon(Icons::f_Icon(Icons::Id::Back));
    l_pBack->setText(tr("Atlas"));
    l_pBack->setToolTip(tr("Back to the atlas (Esc)"));
    l_pBack->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    l_pBack->setAutoRaise(true);
    auto* l_pHint = new QLabel(tr("drag: new child · handles: resize · arrows: nudge · double-click: edit inside · Esc: up"), m_pEditBar);
    l_pHint->setForegroundRole(QPalette::PlaceholderText);
    auto* l_pBarLayout = new QHBoxLayout(m_pEditBar);
    l_pBarLayout->setContentsMargins(4, 2, 8, 2);
    l_pBarLayout->addWidget(l_pBack);
    l_pBarLayout->addWidget(m_pBreadcrumb, 1);
    l_pBarLayout->addWidget(l_pHint);
    m_pEditBar->hide();

    m_pStack->addWidget(m_pAtlas);
    m_pStack->addWidget(m_pEdit);

    auto* l_pLayout = new QVBoxLayout(this);
    l_pLayout->setContentsMargins(0, 0, 0, 0);
    l_pLayout->setSpacing(0);
    l_pLayout->addWidget(m_pTabs);
    l_pLayout->addWidget(m_pEditBar);
    l_pLayout->addWidget(m_pStack, 1);

    connect(l_pBack, &QToolButton::clicked, this, &CanvasPanel::f_ExitEditMode);
    connect(m_pTabs, &QTabBar::currentChanged, m_pAtlas, &AtlasCanvas::f_SetPage);
    connect(m_pAtlas, &AtlasCanvas::f_PageChanged, m_pTabs, &QTabBar::setCurrentIndex);
    connect(m_pAtlas, &AtlasCanvas::f_EditSpriteRequested, this, &CanvasPanel::f_EnterEditMode);
    connect(m_pEdit, &SpriteEditCanvas::f_ExitRequested, this, &CanvasPanel::f_ExitEditMode);
    connect(m_pEdit, &SpriteEditCanvas::f_TargetChanged, this, &CanvasPanel::f_RefreshBreadcrumb);
    connect(m_pAtlas, &CanvasView::f_ZoomChanged, this, [this](double l_dZ) { if (!f_IsEditMode()) emit f_ZoomChanged(l_dZ); });
    connect(m_pEdit, &CanvasView::f_ZoomChanged, this, [this](double l_dZ) { if (f_IsEditMode()) emit f_ZoomChanged(l_dZ); });
    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, &CanvasPanel::f_RefreshTabs);
    // A different project starts in the atlas view.
    connect(l_pDoc, &AtlasDocument::f_ProjectReset, this, &CanvasPanel::f_ExitEditMode);
}

CanvasView* CanvasPanel::f_CurrentView() const
{
    return f_IsEditMode() ? static_cast<CanvasView*>(m_pEdit) : static_cast<CanvasView*>(m_pAtlas);
}

bool CanvasPanel::f_IsEditMode() const { return m_pStack->currentWidget() == m_pEdit; }

void CanvasPanel::f_EnterEditMode(const QString& l_oSprite)
{
    if (l_oSprite.isEmpty()) return;
    // Child rects are in the base art's pixels; editing them over variant art would mislead.
    m_pDoc->f_SetVariant(QString());
    m_pDoc->f_SetSelection({l_oSprite});
    m_pEdit->f_SetTarget(l_oSprite);
    m_pStack->setCurrentWidget(m_pEdit);
    m_pTabs->hide();
    m_pEditBar->show();
    m_pEdit->setFocus();
    emit f_EditModeChanged(true);
    emit f_ZoomChanged(m_pEdit->f_Zoom());
}

void CanvasPanel::f_ExitEditMode()
{
    if (!f_IsEditMode()) return;
    m_pStack->setCurrentWidget(m_pAtlas);
    m_pEditBar->hide();
    m_pTabs->show();
    m_pAtlas->setFocus();
    emit f_EditModeChanged(false);
    emit f_ZoomChanged(m_pAtlas->f_Zoom());
}

void CanvasPanel::f_RefreshTabs()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const int l_iCount = l_spSnap ? int(l_spSnap->m_oPages.size()) : 0;
    const QSignalBlocker l_oBlock(m_pTabs);
    while (m_pTabs->count() > l_iCount) m_pTabs->removeTab(m_pTabs->count() - 1);
    for (int l_iI = 0; l_iI < l_iCount; l_iI++) {
        const QImage& l_oPg = l_spSnap->m_oPages[l_iI];
        const QString l_oText = tr("Page %1  ·  %2 × %3").arg(l_iI + 1).arg(l_oPg.width()).arg(l_oPg.height());
        if (l_iI < m_pTabs->count()) m_pTabs->setTabText(l_iI, l_oText);
        else m_pTabs->addTab(l_oText);
    }
    m_pTabs->setCurrentIndex(m_pAtlas->f_Page());
}

void CanvasPanel::f_RefreshBreadcrumb()
{
    // hero › hero_head › hero_eye: the parent chain of the sprite being edited.
    QStringList l_oChain;
    for (QString l_oN = m_pEdit->f_Target(); !l_oN.isEmpty() && l_oChain.size() < 32;) {
        l_oChain.prepend(l_oN.toHtmlEscaped());
        l_oN = QString::fromStdString(m_pDoc->f_SpriteDef(l_oN).m_strParent);
    }
    m_pBreadcrumb->setText(QStringLiteral("<b>%1</b>").arg(l_oChain.join(QStringLiteral("  ›  "))));
}

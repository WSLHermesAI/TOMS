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

CanvasPanel::CanvasPanel(AtlasDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_tabs(new QTabBar(this))
    , m_editBar(new QWidget(this))
    , m_breadcrumb(new QLabel(m_editBar))
    , m_stack(new QStackedWidget(this))
    , m_atlas(new AtlasCanvas(doc, m_stack))
    , m_edit(new SpriteEditCanvas(doc, m_stack))
{
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);

    auto* back = new QToolButton(m_editBar);
    back->setIcon(Icons::icon(Icons::Id::Back));
    back->setText(tr("Atlas"));
    back->setToolTip(tr("Back to the atlas (Esc)"));
    back->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    back->setAutoRaise(true);
    auto* hint = new QLabel(tr("drag: new child · handles: resize · arrows: nudge · double-click: edit inside · Esc: up"), m_editBar);
    hint->setForegroundRole(QPalette::PlaceholderText);
    auto* barLayout = new QHBoxLayout(m_editBar);
    barLayout->setContentsMargins(4, 2, 8, 2);
    barLayout->addWidget(back);
    barLayout->addWidget(m_breadcrumb, 1);
    barLayout->addWidget(hint);
    m_editBar->hide();

    m_stack->addWidget(m_atlas);
    m_stack->addWidget(m_edit);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabs);
    layout->addWidget(m_editBar);
    layout->addWidget(m_stack, 1);

    connect(back, &QToolButton::clicked, this, &CanvasPanel::exitEditMode);
    connect(m_tabs, &QTabBar::currentChanged, m_atlas, &AtlasCanvas::setPage);
    connect(m_atlas, &AtlasCanvas::pageChanged, m_tabs, &QTabBar::setCurrentIndex);
    connect(m_atlas, &AtlasCanvas::editSpriteRequested, this, &CanvasPanel::enterEditMode);
    connect(m_edit, &SpriteEditCanvas::exitRequested, this, &CanvasPanel::exitEditMode);
    connect(m_edit, &SpriteEditCanvas::targetChanged, this, &CanvasPanel::refreshBreadcrumb);
    connect(m_atlas, &CanvasView::zoomChanged, this, [this](double z) { if (!isEditMode()) emit zoomChanged(z); });
    connect(m_edit, &CanvasView::zoomChanged, this, [this](double z) { if (isEditMode()) emit zoomChanged(z); });
    connect(doc, &AtlasDocument::buildFinished, this, &CanvasPanel::refreshTabs);
    // A different project starts in the atlas view.
    connect(doc, &AtlasDocument::projectReset, this, &CanvasPanel::exitEditMode);
}

CanvasView* CanvasPanel::currentView() const
{
    return isEditMode() ? static_cast<CanvasView*>(m_edit) : static_cast<CanvasView*>(m_atlas);
}

bool CanvasPanel::isEditMode() const { return m_stack->currentWidget() == m_edit; }

void CanvasPanel::enterEditMode(const QString& sprite)
{
    if (sprite.isEmpty()) return;
    // Child rects are in the base art's pixels; editing them over variant art would mislead.
    m_doc->setVariant(QString());
    m_doc->setSelection({sprite});
    m_edit->setTarget(sprite);
    m_stack->setCurrentWidget(m_edit);
    m_tabs->hide();
    m_editBar->show();
    m_edit->setFocus();
    emit editModeChanged(true);
    emit zoomChanged(m_edit->zoom());
}

void CanvasPanel::exitEditMode()
{
    if (!isEditMode()) return;
    m_stack->setCurrentWidget(m_atlas);
    m_editBar->hide();
    m_tabs->show();
    m_atlas->setFocus();
    emit editModeChanged(false);
    emit zoomChanged(m_atlas->zoom());
}

void CanvasPanel::refreshTabs()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const int count = snap ? int(snap->pages.size()) : 0;
    const QSignalBlocker block(m_tabs);
    while (m_tabs->count() > count) m_tabs->removeTab(m_tabs->count() - 1);
    for (int i = 0; i < count; i++) {
        const QImage& pg = snap->pages[i];
        const QString text = tr("Page %1  ·  %2 × %3").arg(i + 1).arg(pg.width()).arg(pg.height());
        if (i < m_tabs->count()) m_tabs->setTabText(i, text);
        else m_tabs->addTab(text);
    }
    m_tabs->setCurrentIndex(m_atlas->page());
}

void CanvasPanel::refreshBreadcrumb()
{
    // hero › hero_head › hero_eye: the parent chain of the sprite being edited.
    QStringList chain;
    for (QString n = m_edit->target(); !n.isEmpty() && chain.size() < 32;) {
        chain.prepend(n.toHtmlEscaped());
        n = QString::fromStdString(m_doc->spriteDef(n).parent);
    }
    m_breadcrumb->setText(QStringLiteral("<b>%1</b>").arg(chain.join(QStringLiteral("  ›  "))));
}

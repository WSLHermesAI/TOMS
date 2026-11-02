#include "PageView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

PageView::PageView(QWidget* l_pParent) : QWidget(l_pParent) {
    setMinimumHeight(120);
    setCursor(Qt::PointingHandCursor);
}

void PageView::f_SetPage(const QImage& l_oPage, const QVector<QRect>& l_oRects) {
    m_oPage = l_oPage;
    m_oRects = l_oRects;
    if (m_iHighlight >= m_oRects.size()) m_iHighlight = -1;
    update();
}

void PageView::f_SetHighlight(int l_iGlyph) {
    m_iHighlight = l_iGlyph >= 0 && l_iGlyph < m_oRects.size() && !m_oRects[l_iGlyph].isEmpty() ? l_iGlyph : -1;
    update();
}

int PageView::f_GlyphAt(const QPoint& l_oPagePos) const {
    for (int l_iI = 0; l_iI < m_oRects.size(); l_iI++)
        if (!m_oRects[l_iI].isEmpty() && m_oRects[l_iI].contains(l_oPagePos)) return l_iI;
    return -1;
}

QRectF PageView::f_Target() const {
    if (m_oPage.isNull()) return {};
    const double l_dMargin = 8;
    const double l_dSx = (width() - 2 * l_dMargin) / m_oPage.width(), l_dSy = (height() - 2 * l_dMargin) / m_oPage.height();
    const double l_dS = std::max(0.01, std::min({l_dSx, l_dSy, 4.0}));   // fit, at most 4x
    const double l_dW = m_oPage.width() * l_dS, l_dH = m_oPage.height() * l_dS;
    return {l_dMargin, l_dMargin, l_dW, l_dH};
}

void PageView::paintEvent(QPaintEvent*) {
    QPainter l_oP(this);
    l_oP.fillRect(rect(), QColor(40, 40, 44));
    if (m_oPage.isNull()) {
        l_oP.setPen(QColor(150, 150, 150));
        l_oP.drawText(rect(), Qt::AlignCenter, tr("No page yet: add pictures and give them characters."));
        return;
    }
    const QRectF l_oT = f_Target();
    const double l_dS = l_oT.width() / m_oPage.width();
    // checker under the page, so transparent parts show
    l_oP.save();
    l_oP.setClipRect(l_oT);
    for (int l_iY = 0; l_iY < (int)l_oT.height() + 8; l_iY += 8)
        for (int l_iX = 0; l_iX < (int)l_oT.width() + 8; l_iX += 8)
            l_oP.fillRect(QRectF(l_oT.x() + l_iX, l_oT.y() + l_iY, 8, 8), ((l_iX / 8 + l_iY / 8) & 1) ? QColor(90, 90, 90) : QColor(64, 64, 64));
    l_oP.restore();
    l_oP.drawImage(l_oT, m_oPage);
    l_oP.setPen(QColor(120, 120, 120));
    l_oP.drawRect(l_oT.adjusted(-1, -1, 0, 0));

    auto l_fnToWidget = [&](const QRect& l_oR) {
        return QRectF(l_oT.x() + l_oR.x() * l_dS, l_oT.y() + l_oR.y() * l_dS, l_oR.width() * l_dS, l_oR.height() * l_dS);
    };
    // every glyph lightly; the selected one bright, the rest dimmed
    l_oP.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (const QRect& l_oR : m_oRects)
        if (!l_oR.isEmpty()) l_oP.drawRect(l_fnToWidget(l_oR));
    if (m_iHighlight >= 0) {
        const QRectF l_oH = l_fnToWidget(m_oRects[m_iHighlight]);
        QPainterPath l_oDim;
        l_oDim.addRect(l_oT);
        QPainterPath l_oHole;
        l_oHole.addRect(l_oH);
        l_oP.fillPath(l_oDim.subtracted(l_oHole), QColor(0, 0, 0, 110));
        l_oP.setPen(QPen(QColor(255, 210, 0), 2));
        l_oP.drawRect(l_oH.adjusted(-1, -1, 1, 1));
    }
}

void PageView::mousePressEvent(QMouseEvent* l_pE) {
    const QRectF l_oT = f_Target();
    if (l_oT.isEmpty()) return;
    const double l_dS = l_oT.width() / m_oPage.width();
    const QPointF l_oPos = l_pE->position();
    const QPoint l_oPagePos((int)((l_oPos.x() - l_oT.x()) / l_dS), (int)((l_oPos.y() - l_oT.y()) / l_dS));
    const int l_iG = f_GlyphAt(l_oPagePos);
    if (l_iG >= 0) {
        f_SetHighlight(l_iG);
        emit f_GlyphClicked(l_iG);
    }
}

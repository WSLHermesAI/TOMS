// EventWidgets.cpp -- see EventWidgets.h.
#include "EventWidgets.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>

#include <cmath>
#include <set>

CommitText::CommitText(const QString& l_oText, QWidget* l_pParent) : QPlainTextEdit(l_oText, l_pParent), m_oOrig(l_oText) {
    setTabChangesFocus(true);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { f_Fit(); if (m_fnOnEdit) m_fnOnEdit(toPlainText()); });
    f_Fit();
}

void CommitText::f_Fit() {
    const int l_iLines = std::max(1, int(toPlainText().count(QLatin1Char('\n'))) + 1);
    const int l_iH = fontMetrics().lineSpacing() * std::min(l_iLines, 12) + int(2 * document()->documentMargin()) + 2 * frameWidth() + 4;
    setFixedHeight(l_iH);
}

void CommitText::focusOutEvent(QFocusEvent* l_pE) {
    QPlainTextEdit::focusOutEvent(l_pE);
    const QString l_oNow = toPlainText();
    if (l_oNow != m_oOrig && m_fnOnCommit) { m_oOrig = l_oNow; m_fnOnCommit(l_oNow); }
}

CommitLine::CommitLine(const QString& l_oInitial, QWidget* l_pParent) : QLineEdit(l_oInitial, l_pParent), m_oOrig(l_oInitial) {
    connect(this, &QLineEdit::editingFinished, this, [this] {
        if (text() == m_oOrig || !m_fnOnCommit) return;
        m_oOrig = text();
        m_fnOnCommit(text());
    });
}

namespace EvUi {

QString f_Esc(const QString& l_oS) { return l_oS.toHtmlEscaped(); }
QString f_Q(const std::string& l_strS) { return QString::fromStdString(l_strS); }

QLabel* f_Rich(const QString& l_oHtml) {
    auto* l_pL = new QLabel(l_oHtml);
    l_pL->setTextFormat(Qt::RichText);
    l_pL->setWordWrap(true);
    l_pL->setMinimumWidth(40);
    l_pL->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    return l_pL;
}

QLabel* f_Muted(const QString& l_oText) {
    auto* l_pL = new QLabel(l_oText);
    l_pL->setWordWrap(true);
    l_pL->setStyleSheet(QStringLiteral("color:#8b929c"));
    return l_pL;
}

QLabel* f_Heading(const QString& l_oText) {
    auto* l_pL = new QLabel(l_oText);
    l_pL->setStyleSheet(QStringLiteral("color:#8b929c; font-weight:600"));
    return l_pL;
}

QToolButton* f_Chip(const QString& l_oText, const QColor& l_oBorder, const QString& l_oTip) {
    auto* l_pB = new QToolButton;
    l_pB->setText(l_oText);
    l_pB->setToolTip(l_oTip);
    l_pB->setStyleSheet(QStringLiteral("QToolButton { border:1px solid %1; border-radius:9px; padding:1px 7px; }").arg(l_oBorder.isValid() ? l_oBorder.name() : QStringLiteral("#3a3f47")));
    return l_pB;
}

QWidget* f_Banner(const QString& l_oHtml, bool l_bBad) {
    auto* l_pL = f_Rich(l_oHtml);
    l_pL->setStyleSheet(l_bBad ? QStringLiteral("background:#3a1b1b; border:1px solid #ff5d5d; border-radius:4px; padding:6px")
                         : QStringLiteral("background:#3a3214; border:1px solid #ff9f43; border-radius:4px; padding:6px"));
    return l_pL;
}

}  // namespace EvUi

// ---- the floor map ---------------------------------------------------------------------------------------------------

void FloorMapView::f_SetGraph(std::vector<Node> l_vNodes, std::vector<Edge> l_vEdges, std::vector<std::pair<QString, double>> l_vRows, const QString& l_oSel) {
    m_vNodes = std::move(l_vNodes);
    m_vEdges = std::move(l_vEdges);
    m_vRows = std::move(l_vRows);
    m_oSel = l_oSel;
    QRectF l_oAll;
    for (const Node& l_oN : m_vNodes) l_oAll |= l_oN.m_oR;
    m_oSize = QSize(int(l_oAll.right() + 40), int(l_oAll.bottom() + 20));
    setMinimumSize(m_oSize);
    update();
}

void FloorMapView::paintEvent(QPaintEvent*) {
    QPainter l_oG(this);
    l_oG.setRenderHint(QPainter::Antialiasing, true);
    std::map<QString, const Node*> l_mapPos;
    for (const Node& l_oN : m_vNodes) l_mapPos[l_oN.m_oId] = &l_oN;
    std::set<QString> l_setNear;
    if (!m_oSel.isEmpty()) {
        l_setNear.insert(m_oSel);
        for (const Edge& l_oE : m_vEdges) { if (l_oE.m_oFrom == m_oSel) l_setNear.insert(l_oE.m_oTo); if (l_oE.m_oTo == m_oSel) l_setNear.insert(l_oE.m_oFrom); }
    }
    l_oG.setPen(QColor("#8b929c"));
    for (const auto& [l_oLbl, l_dY] : m_vRows) l_oG.drawText(QPointF(6, l_dY + 16), l_oLbl);
    QFont l_oSmall = font();
    l_oSmall.setPointSizeF(std::max(7.0, l_oSmall.pointSizeF() - 1));
    for (const Edge& l_oE : m_vEdges) {
        auto l_itA = l_mapPos.find(l_oE.m_oFrom), l_itB = l_mapPos.find(l_oE.m_oTo);
        if (l_itA == l_mapPos.end() || l_itB == l_mapPos.end()) continue;
        const QRectF l_oRa = l_itA->second->m_oR, l_oRb = l_itB->second->m_oR;
        const bool l_bSameRow = std::abs(l_oRa.y() - l_oRb.y()) < 1, l_bFwd = l_oRb.x() > l_oRa.x();
        const QPointF l_oP1 = l_bSameRow ? QPointF(l_bFwd ? l_oRa.right() : l_oRa.left(), l_oRa.center().y()) : QPointF(l_oRa.center().x(), l_oRb.y() > l_oRa.y() ? l_oRa.bottom() : l_oRa.top());
        const QPointF l_oP2 = l_bSameRow ? QPointF(l_bFwd ? l_oRb.left() : l_oRb.right(), l_oRb.center().y()) : QPointF(l_oRb.center().x(), l_oRb.y() > l_oRa.y() ? l_oRb.top() : l_oRb.bottom());
        const double l_dBend = l_bSameRow && std::abs(l_oRb.x() - l_oRa.x()) > l_oRa.width() + 30 ? -22 : 0;
        QPainterPath l_oPath(l_oP1);
        if (l_bSameRow) l_oPath.quadTo(QPointF((l_oP1.x() + l_oP2.x()) / 2, l_oP1.y() + l_dBend), l_oP2);
        else l_oPath.cubicTo(QPointF(l_oP1.x(), (l_oP1.y() + l_oP2.y()) / 2), QPointF(l_oP2.x(), (l_oP1.y() + l_oP2.y()) / 2), l_oP2);
        const bool l_bHl = !m_oSel.isEmpty() && (l_oE.m_oFrom == m_oSel || l_oE.m_oTo == m_oSel), l_bFaded = !m_oSel.isEmpty() && !l_bHl;
        QColor l_oC = !l_oE.m_oLock.isEmpty() ? QColor("#e0b44c") : l_oE.m_bMiss ? QColor("#ff5d5d") : l_bHl ? QColor("#4c9eff") : QColor("#8b929c");
        if (l_bFaded) l_oC.setAlpha(60);
        QPen l_oPen(l_oC, l_bHl ? 2 : 1.4, !l_oE.m_oLock.isEmpty() || l_oE.m_bMiss ? Qt::DashLine : Qt::SolidLine);
        l_oG.setPen(l_oPen);
        l_oG.setBrush(Qt::NoBrush);
        l_oG.drawPath(l_oPath);
        // arrow head
        const QPointF l_oD = l_oP2 - l_oPath.pointAtPercent(0.9);
        const double l_dLen = std::hypot(l_oD.x(), l_oD.y());
        if (l_dLen > 0.1) {
            const QPointF l_oU = l_oD / l_dLen, l_oNrm(-l_oU.y(), l_oU.x());
            QPolygonF l_oHead({l_oP2, l_oP2 - l_oU * 7 + l_oNrm * 3.5, l_oP2 - l_oU * 7 - l_oNrm * 3.5});
            l_oG.setBrush(l_oC);
            l_oG.setPen(Qt::NoPen);
            l_oG.drawPolygon(l_oHead);
        }
        if (!l_oE.m_oLock.isEmpty() && !l_bFaded) {
            l_oG.setFont(l_oSmall);
            l_oG.setPen(QColor("#e0b44c"));
            l_oG.drawText(QPointF((l_oP1.x() + l_oP2.x()) / 2 + 4, (l_oP1.y() + l_oP2.y()) / 2 + l_dBend / 2 - 4), QStringLiteral("🔒 ") + l_oE.m_oLock);
            l_oG.setFont(font());
        }
    }
    for (const Node& l_oN : m_vNodes) {
        const bool l_bFaded = !m_oSel.isEmpty() && !l_setNear.count(l_oN.m_oId);
        QColor l_oFill = l_oN.m_bGhost ? QColor("#4a2222") : l_oN.m_bBoss ? QColor("#5a3a3a") : QColor("#2c3036");
        QColor l_oStroke = l_oN.m_bGhost ? QColor("#ff5d5d") : l_oN.m_bWarn ? QColor("#ff9f43") : l_oN.m_oId == m_oSel ? QColor("#ffd24c") : QColor("#7a828d");
        if (l_bFaded) { l_oFill.setAlpha(90); l_oStroke.setAlpha(90); }
        l_oG.setBrush(l_oFill);
        l_oG.setPen(QPen(l_oStroke, l_oN.m_oId == m_oSel ? 2 : 1, l_oN.m_bGhost || l_oN.m_bWarn ? Qt::DashLine : Qt::SolidLine));
        l_oG.drawRoundedRect(l_oN.m_oR, 4, 4);
        QColor l_oTc("#d7dbe0");
        if (l_bFaded) l_oTc.setAlpha(110);
        l_oG.setPen(l_oTc);
        l_oG.drawText(l_oN.m_oR.adjusted(6, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   (l_oN.m_bGhost ? QStringLiteral("⛔ ") : QString()) + l_oN.m_oId + (l_oN.m_bIsNew ? QStringLiteral(" NEW") : QString()) + (l_oN.m_bWarn ? QStringLiteral(" ⚠") : QString()));
    }
}

void FloorMapView::mousePressEvent(QMouseEvent* l_pE) {
    for (const Node& l_oN : m_vNodes)
        if (l_oN.m_oR.contains(l_pE->position())) { Q_EMIT f_Clicked(l_oN.m_oId); return; }
}

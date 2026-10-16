// EventWidgets.cpp -- see EventWidgets.h.
#include "EventWidgets.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>

#include <cmath>
#include <set>

CommitText::CommitText(const QString& text, QWidget* parent) : QPlainTextEdit(text, parent), m_orig(text) {
    setTabChangesFocus(true);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    connect(this, &QPlainTextEdit::textChanged, this, [this] { fit(); if (onEdit) onEdit(toPlainText()); });
    fit();
}

void CommitText::fit() {
    const int lines = std::max(1, int(toPlainText().count(QLatin1Char('\n'))) + 1);
    const int h = fontMetrics().lineSpacing() * std::min(lines, 12) + int(2 * document()->documentMargin()) + 2 * frameWidth() + 4;
    setFixedHeight(h);
}

void CommitText::focusOutEvent(QFocusEvent* e) {
    QPlainTextEdit::focusOutEvent(e);
    const QString now = toPlainText();
    if (now != m_orig && onCommit) { m_orig = now; onCommit(now); }
}

CommitLine::CommitLine(const QString& initial, QWidget* parent) : QLineEdit(initial, parent), m_orig(initial) {
    connect(this, &QLineEdit::editingFinished, this, [this] {
        if (text() == m_orig || !onCommit) return;
        m_orig = text();
        onCommit(text());
    });
}

namespace EvUi {

QString esc(const QString& s) { return s.toHtmlEscaped(); }
QString q(const std::string& s) { return QString::fromStdString(s); }

QLabel* rich(const QString& html) {
    auto* l = new QLabel(html);
    l->setTextFormat(Qt::RichText);
    l->setWordWrap(true);
    l->setMinimumWidth(40);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    return l;
}

QLabel* muted(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#8b929c"));
    return l;
}

QLabel* heading(const QString& text) {
    auto* l = new QLabel(text);
    l->setStyleSheet(QStringLiteral("color:#8b929c; font-weight:600"));
    return l;
}

QToolButton* chip(const QString& text, const QColor& border, const QString& tip) {
    auto* b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setStyleSheet(QStringLiteral("QToolButton { border:1px solid %1; border-radius:9px; padding:1px 7px; }").arg(border.isValid() ? border.name() : QStringLiteral("#3a3f47")));
    return b;
}

QWidget* banner(const QString& html, bool bad) {
    auto* l = rich(html);
    l->setStyleSheet(bad ? QStringLiteral("background:#3a1b1b; border:1px solid #ff5d5d; border-radius:4px; padding:6px")
                         : QStringLiteral("background:#3a3214; border:1px solid #ff9f43; border-radius:4px; padding:6px"));
    return l;
}

}  // namespace EvUi

// ---- the floor map ---------------------------------------------------------------------------------------------------

void FloorMapView::setGraph(std::vector<Node> nodes, std::vector<Edge> edges, std::vector<std::pair<QString, double>> rows, const QString& sel) {
    m_nodes = std::move(nodes);
    m_edges = std::move(edges);
    m_rows = std::move(rows);
    m_sel = sel;
    QRectF all;
    for (const Node& n : m_nodes) all |= n.r;
    m_size = QSize(int(all.right() + 40), int(all.bottom() + 20));
    setMinimumSize(m_size);
    update();
}

void FloorMapView::paintEvent(QPaintEvent*) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    std::map<QString, const Node*> pos;
    for (const Node& n : m_nodes) pos[n.id] = &n;
    std::set<QString> near;
    if (!m_sel.isEmpty()) {
        near.insert(m_sel);
        for (const Edge& e : m_edges) { if (e.from == m_sel) near.insert(e.to); if (e.to == m_sel) near.insert(e.from); }
    }
    g.setPen(QColor("#8b929c"));
    for (const auto& [lbl, y] : m_rows) g.drawText(QPointF(6, y + 16), lbl);
    QFont small = font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1));
    for (const Edge& e : m_edges) {
        auto a = pos.find(e.from), b = pos.find(e.to);
        if (a == pos.end() || b == pos.end()) continue;
        const QRectF ra = a->second->r, rb = b->second->r;
        const bool sameRow = std::abs(ra.y() - rb.y()) < 1, fwd = rb.x() > ra.x();
        const QPointF p1 = sameRow ? QPointF(fwd ? ra.right() : ra.left(), ra.center().y()) : QPointF(ra.center().x(), rb.y() > ra.y() ? ra.bottom() : ra.top());
        const QPointF p2 = sameRow ? QPointF(fwd ? rb.left() : rb.right(), rb.center().y()) : QPointF(rb.center().x(), rb.y() > ra.y() ? rb.top() : rb.bottom());
        const double bend = sameRow && std::abs(rb.x() - ra.x()) > ra.width() + 30 ? -22 : 0;
        QPainterPath path(p1);
        if (sameRow) path.quadTo(QPointF((p1.x() + p2.x()) / 2, p1.y() + bend), p2);
        else path.cubicTo(QPointF(p1.x(), (p1.y() + p2.y()) / 2), QPointF(p2.x(), (p1.y() + p2.y()) / 2), p2);
        const bool hl = !m_sel.isEmpty() && (e.from == m_sel || e.to == m_sel), faded = !m_sel.isEmpty() && !hl;
        QColor c = !e.lock.isEmpty() ? QColor("#e0b44c") : e.miss ? QColor("#ff5d5d") : hl ? QColor("#4c9eff") : QColor("#8b929c");
        if (faded) c.setAlpha(60);
        QPen pen(c, hl ? 2 : 1.4, !e.lock.isEmpty() || e.miss ? Qt::DashLine : Qt::SolidLine);
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        g.drawPath(path);
        // arrow head
        const QPointF d = p2 - path.pointAtPercent(0.9);
        const double len = std::hypot(d.x(), d.y());
        if (len > 0.1) {
            const QPointF u = d / len, nrm(-u.y(), u.x());
            QPolygonF head({p2, p2 - u * 7 + nrm * 3.5, p2 - u * 7 - nrm * 3.5});
            g.setBrush(c);
            g.setPen(Qt::NoPen);
            g.drawPolygon(head);
        }
        if (!e.lock.isEmpty() && !faded) {
            g.setFont(small);
            g.setPen(QColor("#e0b44c"));
            g.drawText(QPointF((p1.x() + p2.x()) / 2 + 4, (p1.y() + p2.y()) / 2 + bend / 2 - 4), QStringLiteral("🔒 ") + e.lock);
            g.setFont(font());
        }
    }
    for (const Node& n : m_nodes) {
        const bool faded = !m_sel.isEmpty() && !near.count(n.id);
        QColor fill = n.ghost ? QColor("#4a2222") : n.boss ? QColor("#5a3a3a") : QColor("#2c3036");
        QColor stroke = n.ghost ? QColor("#ff5d5d") : n.warn ? QColor("#ff9f43") : n.id == m_sel ? QColor("#ffd24c") : QColor("#7a828d");
        if (faded) { fill.setAlpha(90); stroke.setAlpha(90); }
        g.setBrush(fill);
        g.setPen(QPen(stroke, n.id == m_sel ? 2 : 1, n.ghost || n.warn ? Qt::DashLine : Qt::SolidLine));
        g.drawRoundedRect(n.r, 4, 4);
        QColor tc("#d7dbe0");
        if (faded) tc.setAlpha(110);
        g.setPen(tc);
        g.drawText(n.r.adjusted(6, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   (n.ghost ? QStringLiteral("⛔ ") : QString()) + n.id + (n.isNew ? QStringLiteral(" NEW") : QString()) + (n.warn ? QStringLiteral(" ⚠") : QString()));
    }
}

void FloorMapView::mousePressEvent(QMouseEvent* e) {
    for (const Node& n : m_nodes)
        if (n.r.contains(e->position())) { Q_EMIT clicked(n.id); return; }
}

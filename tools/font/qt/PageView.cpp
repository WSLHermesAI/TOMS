#include "PageView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

PageView::PageView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setCursor(Qt::PointingHandCursor);
}

void PageView::setPage(const QImage& page, const QVector<QRect>& rects) {
    page_ = page;
    rects_ = rects;
    if (highlight_ >= rects_.size()) highlight_ = -1;
    update();
}

void PageView::setHighlight(int glyph) {
    highlight_ = glyph >= 0 && glyph < rects_.size() && !rects_[glyph].isEmpty() ? glyph : -1;
    update();
}

int PageView::glyphAt(const QPoint& pagePos) const {
    for (int i = 0; i < rects_.size(); i++)
        if (!rects_[i].isEmpty() && rects_[i].contains(pagePos)) return i;
    return -1;
}

QRectF PageView::target() const {
    if (page_.isNull()) return {};
    const double margin = 8;
    const double sx = (width() - 2 * margin) / page_.width(), sy = (height() - 2 * margin) / page_.height();
    const double s = std::max(0.01, std::min({sx, sy, 4.0}));   // fit, at most 4x
    const double w = page_.width() * s, h = page_.height() * s;
    return {margin, margin, w, h};
}

void PageView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(40, 40, 44));
    if (page_.isNull()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No page yet: add pictures and give them characters."));
        return;
    }
    const QRectF t = target();
    const double s = t.width() / page_.width();
    // checker under the page, so transparent parts show
    p.save();
    p.setClipRect(t);
    for (int y = 0; y < (int)t.height() + 8; y += 8)
        for (int x = 0; x < (int)t.width() + 8; x += 8)
            p.fillRect(QRectF(t.x() + x, t.y() + y, 8, 8), ((x / 8 + y / 8) & 1) ? QColor(90, 90, 90) : QColor(64, 64, 64));
    p.restore();
    p.drawImage(t, page_);
    p.setPen(QColor(120, 120, 120));
    p.drawRect(t.adjusted(-1, -1, 0, 0));

    auto toWidget = [&](const QRect& r) {
        return QRectF(t.x() + r.x() * s, t.y() + r.y() * s, r.width() * s, r.height() * s);
    };
    // every glyph lightly; the selected one bright, the rest dimmed
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    for (const QRect& r : rects_)
        if (!r.isEmpty()) p.drawRect(toWidget(r));
    if (highlight_ >= 0) {
        const QRectF h = toWidget(rects_[highlight_]);
        QPainterPath dim;
        dim.addRect(t);
        QPainterPath hole;
        hole.addRect(h);
        p.fillPath(dim.subtracted(hole), QColor(0, 0, 0, 110));
        p.setPen(QPen(QColor(255, 210, 0), 2));
        p.drawRect(h.adjusted(-1, -1, 1, 1));
    }
}

void PageView::mousePressEvent(QMouseEvent* e) {
    const QRectF t = target();
    if (t.isEmpty()) return;
    const double s = t.width() / page_.width();
    const QPointF pos = e->position();
    const QPoint pagePos((int)((pos.x() - t.x()) / s), (int)((pos.y() - t.y()) / s));
    const int g = glyphAt(pagePos);
    if (g >= 0) {
        setHighlight(g);
        emit glyphClicked(g);
    }
}

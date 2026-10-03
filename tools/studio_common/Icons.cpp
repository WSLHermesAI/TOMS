#include "Icons.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <algorithm>
#include <cmath>

namespace Icons {

namespace {

// Icons are drawn on a 32x32 grid at twice the resolution, so they stay sharp on HiDPI screens.
constexpr int kSize = 64;
const QColor kInk(0x9a, 0xa3, 0xaf);
const QColor kBlue(0x4f, 0x9c, 0xff);
const QColor kOrange(0xff, 0xa8, 0x3a);
const QColor kGreen(0x4c, 0xc9, 0x6e);
const QColor kRed(0xf0, 0x4a, 0x5a);
const QColor kYellow(0xf2, 0xc1, 0x3d);

QPen ink(const QColor& c, qreal w = 2.5)
{
    return QPen(c, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void drawPin(QPainter& p, const QPointF& at, qreal s)
{
    // A map pin: round head, pointed tip at `at`.
    QPainterPath path;
    path.moveTo(at);
    path.cubicTo(at + QPointF(-s * 0.9, -s * 1.1), at + QPointF(-s * 0.9, -s * 2.2), at + QPointF(0, -s * 2.2));
    path.cubicTo(at + QPointF(s * 0.9, -s * 2.2), at + QPointF(s * 0.9, -s * 1.1), at);
    p.setPen(QPen(QColor(0, 0, 0, 120), s * 0.15));
    p.setBrush(kRed);
    p.drawPath(path);
    p.setBrush(Qt::white);
    p.setPen(Qt::NoPen);
    p.drawEllipse(at + QPointF(0, -s * 1.45), s * 0.32, s * 0.32);
}

void drawArrow(QPainter& p, QPointF from, QPointF to)
{
    p.drawLine(from, to);
    const QPointF d = (to - from) / std::max(1.0, std::hypot(to.x() - from.x(), to.y() - from.y()));
    const QPointF n(-d.y(), d.x());
    p.drawLine(to, to - d * 6 + n * 6);
    p.drawLine(to, to - d * 6 - n * 6);
}

void drawLevel(QPainter& p, Id id)
{
    const QColor c = id == Id::Error ? kRed : id == Id::Warning ? kYellow : kBlue;
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    if (id == Id::Warning) {
        QPolygonF tri({QPointF(16, 3), QPointF(30, 28), QPointF(2, 28)});
        p.drawPolygon(tri);
    } else {
        p.drawEllipse(QRectF(3, 3, 26, 26));
    }
    p.setPen(ink(id == Id::Warning ? QColor(0x20, 0x20, 0x20) : Qt::white, 3.2));
    if (id == Id::Error) {
        p.drawLine(QPointF(11, 11), QPointF(21, 21));
        p.drawLine(QPointF(21, 11), QPointF(11, 21));
    } else if (id == Id::Warning) {
        p.drawLine(QPointF(16, 11), QPointF(16, 19));
        p.drawPoint(QPointF(16, 24));
    } else {
        p.drawLine(QPointF(16, 14), QPointF(16, 22));
        p.drawPoint(QPointF(16, 9));
    }
}

QPixmap render(Id id)
{
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(kSize / 32.0, kSize / 32.0);
    p.setBrush(Qt::NoBrush);
    p.setPen(ink(kInk));
    switch (id) {
    case Id::New:
        p.drawPolygon(QPolygonF({QPointF(8, 4), QPointF(19, 4), QPointF(25, 10), QPointF(25, 28), QPointF(8, 28)}));
        p.drawPolyline(QPolygonF({QPointF(19, 4), QPointF(19, 10), QPointF(25, 10)}));
        p.setPen(ink(kGreen, 3));
        p.drawLine(QPointF(16, 15), QPointF(16, 23));
        p.drawLine(QPointF(12, 19), QPointF(20, 19));
        break;
    case Id::Open:
    case Id::Folder: {
        QPainterPath path;
        path.moveTo(3, 8);
        path.lineTo(12, 8);
        path.lineTo(15, 11);
        path.lineTo(29, 11);
        path.lineTo(29, 26);
        path.lineTo(3, 26);
        path.closeSubpath();
        p.setBrush(QColor(0xe0, 0xb0, 0x50, id == Id::Folder ? 230 : 160));
        p.setPen(ink(QColor(0xc0, 0x90, 0x30), 2));
        p.drawPath(path);
        break;
    }
    case Id::Save:
        p.drawRoundedRect(QRectF(5, 5, 22, 22), 3, 3);
        p.drawRect(QRectF(10, 5, 12, 7));
        p.setBrush(kBlue);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(9, 17, 14, 8));
        break;
    case Id::Import:
    case Id::Export:
        p.drawRoundedRect(QRectF(4, 14, 24, 14), 2, 2);
        p.setPen(ink(id == Id::Import ? kBlue : kGreen, 3));
        if (id == Id::Import) drawArrow(p, QPointF(16, 2), QPointF(16, 20));
        else drawArrow(p, QPointF(16, 21), QPointF(16, 3));
        break;
    case Id::EditMode:
        p.setPen(QPen(kOrange, 2, Qt::DashLine));
        p.drawRect(QRectF(4, 4, 18, 18));
        p.setPen(ink(kInk));
        p.setBrush(kInk);
        p.drawPolygon(QPolygonF({QPointF(16, 14), QPointF(29, 22), QPointF(23, 23), QPointF(26, 29), QPointF(23, 30),
                                 QPointF(20, 25), QPointF(16, 28)}));
        break;
    case Id::ZoomIn:
    case Id::ZoomOut:
        p.drawEllipse(QRectF(4, 4, 18, 18));
        p.setPen(ink(kInk, 4));
        p.drawLine(QPointF(20, 20), QPointF(28, 28));
        p.setPen(ink(kInk, 2.5));
        p.drawLine(QPointF(9, 13), QPointF(17, 13));
        if (id == Id::ZoomIn) p.drawLine(QPointF(13, 9), QPointF(13, 17));
        break;
    case Id::Fit:
        p.drawPolyline(QPolygonF({QPointF(4, 11), QPointF(4, 4), QPointF(11, 4)}));
        p.drawPolyline(QPolygonF({QPointF(21, 4), QPointF(28, 4), QPointF(28, 11)}));
        p.drawPolyline(QPolygonF({QPointF(28, 21), QPointF(28, 28), QPointF(21, 28)}));
        p.drawPolyline(QPolygonF({QPointF(11, 28), QPointF(4, 28), QPointF(4, 21)}));
        p.setBrush(QColor(kBlue.red(), kBlue.green(), kBlue.blue(), 120));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(10, 10, 12, 12));
        break;
    case Id::Image:
        p.drawRoundedRect(QRectF(4, 6, 24, 20), 2, 2);
        p.setBrush(kBlue);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(6, 24), QPointF(13, 14), QPointF(18, 20), QPointF(21, 17), QPointF(26, 24)}));
        p.setBrush(kYellow);
        p.drawEllipse(QRectF(19, 9, 5, 5));
        break;
    case Id::Child:
        p.setPen(QPen(kOrange, 2.5, Qt::DashLine));
        p.drawRect(QRectF(5, 5, 22, 22));
        p.setBrush(QColor(kOrange.red(), kOrange.green(), kOrange.blue(), 90));
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(10, 10, 12, 12));
        break;
    case Id::Pin:
        drawPin(p, QPointF(16, 29), 11);
        break;
    case Id::Grid:
        p.drawRect(QRectF(4, 4, 24, 24));
        p.setPen(QPen(kOrange, 2, Qt::DashLine));
        p.drawLine(QPointF(12, 5), QPointF(12, 27));
        p.drawLine(QPointF(20, 5), QPointF(20, 27));
        p.drawLine(QPointF(5, 12), QPointF(27, 12));
        p.drawLine(QPointF(5, 20), QPointF(27, 20));
        break;
    case Id::Bake:
        p.setPen(QPen(kOrange, 2.5, Qt::DashLine));
        p.drawRect(QRectF(3, 3, 16, 16));
        p.setPen(ink(kBlue));
        p.setBrush(QColor(kBlue.red(), kBlue.green(), kBlue.blue(), 90));
        p.drawRect(QRectF(13, 13, 16, 16));
        break;
    case Id::Exclude:
        p.setPen(ink(kRed, 3));
        p.drawEllipse(QRectF(5, 5, 22, 22));
        p.drawLine(QPointF(9, 23), QPointF(23, 9));
        break;
    case Id::Add:
        p.setPen(ink(kGreen, 3.5));
        p.drawLine(QPointF(16, 6), QPointF(16, 26));
        p.drawLine(QPointF(6, 16), QPointF(26, 16));
        break;
    case Id::Remove:
        p.setPen(ink(kRed, 3.5));
        p.drawLine(QPointF(6, 16), QPointF(26, 16));
        break;
    case Id::Up:
    case Id::Down:
        p.setPen(ink(kInk, 3));
        if (id == Id::Up) drawArrow(p, QPointF(16, 27), QPointF(16, 5));
        else drawArrow(p, QPointF(16, 5), QPointF(16, 27));
        break;
    case Id::Play:
        p.setBrush(kGreen);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(8, 5), QPointF(27, 16), QPointF(8, 27)}));
        break;
    case Id::Pause:
        p.setBrush(kInk);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(8, 6, 6, 20));
        p.drawRect(QRectF(18, 6, 6, 20));
        break;
    case Id::Back:
        p.setPen(ink(kInk, 3));
        drawArrow(p, QPointF(26, 16), QPointF(6, 16));
        break;
    case Id::Forward:
        p.setPen(ink(kInk, 3));
        drawArrow(p, QPointF(6, 16), QPointF(26, 16));
        break;
    case Id::Error:
    case Id::Warning:
    case Id::Info:
        drawLevel(p, id);
        break;
    case Id::Move:
        p.setPen(ink(kInk, 2.5));
        drawArrow(p, QPointF(16, 16), QPointF(16, 3));
        drawArrow(p, QPointF(16, 16), QPointF(16, 29));
        drawArrow(p, QPointF(16, 16), QPointF(3, 16));
        drawArrow(p, QPointF(16, 16), QPointF(29, 16));
        break;
    case Id::Rotate: {
        p.setPen(ink(kBlue, 3));
        p.drawArc(QRectF(5, 5, 22, 22), 100 * 16, 280 * 16);
        p.setBrush(kBlue);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(13, 1), QPointF(20, 6), QPointF(13, 11)}));
        break;
    }
    case Id::Scale:
        p.drawRect(QRectF(4, 12, 16, 16));
        p.setPen(ink(kOrange, 3));
        drawArrow(p, QPointF(14, 18), QPointF(28, 4));
        break;
    case Id::KeyOn:
    case Id::KeyOff: {
        const QPolygonF diamond({QPointF(16, 4), QPointF(28, 16), QPointF(16, 28), QPointF(4, 16)});
        p.setPen(ink(kYellow, 3));
        p.setBrush(id == Id::KeyOn ? QBrush(kYellow) : QBrush(Qt::NoBrush));
        p.drawPolygon(diamond);
        break;
    }
    case Id::KeyNone: {
        QColor c = kInk;
        c.setAlpha(110);
        p.setPen(QPen(c, 2, Qt::DotLine));
        p.drawPolygon(QPolygonF({QPointF(16, 6), QPointF(26, 16), QPointF(16, 26), QPointF(6, 16)}));
        break;
    }
    case Id::Eye:
    case Id::EyeOff: {
        QPainterPath eye;
        eye.moveTo(3, 16);
        eye.quadTo(16, 3, 29, 16);
        eye.quadTo(16, 29, 3, 16);
        QColor c = id == Id::Eye ? kInk : QColor(kInk.red(), kInk.green(), kInk.blue(), 90);
        p.setPen(ink(c, 2.5));
        p.drawPath(eye);
        p.setBrush(c);
        p.drawEllipse(QPointF(16, 16), 4, 4);
        if (id == Id::EyeOff) {
            p.setPen(ink(kRed, 3));
            p.drawLine(QPointF(6, 27), QPointF(26, 5));
        }
        break;
    }
    case Id::Stop:
        p.setBrush(kInk);
        p.setPen(Qt::NoPen);
        p.drawRect(QRectF(8, 8, 16, 16));
        break;
    case Id::Loop: {
        p.setPen(ink(kInk, 2.8));
        p.drawArc(QRectF(4, 7, 24, 18), 30 * 16, 300 * 16);
        p.setBrush(kInk);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(22, 3), QPointF(30, 9), QPointF(21, 12)}));
        break;
    }
    case Id::Node:
        p.drawEllipse(QRectF(11, 3, 10, 10));
        p.drawLine(QPointF(16, 13), QPointF(16, 18));
        p.drawLine(QPointF(7, 18), QPointF(25, 18));
        p.drawLine(QPointF(7, 18), QPointF(7, 22));
        p.drawLine(QPointF(25, 18), QPointF(25, 22));
        p.setBrush(kBlue);
        p.drawEllipse(QRectF(3, 22, 8, 8));
        p.drawEllipse(QRectF(21, 22, 8, 8));
        break;
    case Id::SpriteNode:
        p.drawRoundedRect(QRectF(4, 6, 24, 20), 2, 2);
        p.setBrush(kGreen);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(6, 24), QPointF(13, 14), QPointF(18, 20), QPointF(21, 17), QPointF(26, 24)}));
        break;
    case Id::Duplicate:
        p.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        p.setBrush(QColor(kBlue.red(), kBlue.green(), kBlue.blue(), 90));
        p.setPen(ink(kBlue));
        p.drawRoundedRect(QRectF(12, 12, 16, 16), 2, 2);
        break;
    case Id::Event:
        p.setPen(ink(kOrange, 3));
        p.drawLine(QPointF(8, 4), QPointF(8, 29));
        p.setBrush(kOrange);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF({QPointF(9, 4), QPointF(27, 10), QPointF(9, 16)}));
        break;
    case Id::Rename:
        p.drawRect(QRectF(3, 10, 26, 13));
        p.setPen(ink(kBlue, 2.5));
        p.drawLine(QPointF(20, 6), QPointF(20, 27));
        p.drawLine(QPointF(17, 6), QPointF(23, 6));
        p.drawLine(QPointF(17, 27), QPointF(23, 27));
        break;
    case Id::AutoKey:
        p.setBrush(kRed);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(4, 4, 24, 24));
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF({QPointF(16, 9), QPointF(23, 16), QPointF(16, 23), QPointF(9, 16)}));
        break;
    case Id::Snap:
        p.setPen(QPen(kInk, 1.5, Qt::DotLine));
        for (int i = 6; i <= 26; i += 10) {
            p.drawLine(QPointF(i, 3), QPointF(i, 29));
            p.drawLine(QPointF(3, i), QPointF(29, i));
        }
        p.setBrush(kOrange);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(16, 16), 4.5, 4.5);
        break;
    }
    return pm;
}

}  // namespace

QIcon icon(Id id)
{
    static QHash<int, QIcon> cache;
    auto it = cache.find(int(id));
    if (it == cache.end()) it = cache.insert(int(id), QIcon(render(id)));
    return *it;
}

QIcon spriteIcon(const QImage& thumb, bool child, bool pinned, bool excluded)
{
    constexpr int s = 64;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect inner = child ? QRect(8, 8, s - 16, s - 16) : QRect(2, 2, s - 4, s - 4);
    if (!thumb.isNull()) {
        // Pixel art must stay crisp: only smooth when shrinking.
        const QSize fit = thumb.size().scaled(inner.size(), Qt::KeepAspectRatio);
        const bool shrink = fit.width() < thumb.width();
        p.setRenderHint(QPainter::SmoothPixmapTransform, shrink);
        const QRect r(inner.center().x() - fit.width() / 2 + 1, inner.center().y() - fit.height() / 2 + 1, fit.width(), fit.height());
        if (excluded) p.setOpacity(0.35);
        p.drawImage(r, thumb);
        p.setOpacity(1.0);
    }
    if (child) {
        p.setPen(QPen(kOrange, 4, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(3, 3, s - 6, s - 6));
    }
    if (pinned) drawPin(p, QPointF(s - 12, s - 2), 11);
    if (excluded) {
        p.setPen(ink(kRed, 5));
        p.drawLine(QPointF(10, s - 10), QPointF(s - 10, 10));
    }
    return QIcon(pm);
}

}  // namespace Icons

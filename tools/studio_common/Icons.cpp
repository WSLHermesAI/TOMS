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
constexpr int g_iSize = 64;
const QColor g_oInk(0x9a, 0xa3, 0xaf);
const QColor g_oBlue(0x4f, 0x9c, 0xff);
const QColor g_oOrange(0xff, 0xa8, 0x3a);
const QColor g_oGreen(0x4c, 0xc9, 0x6e);
const QColor g_oRed(0xf0, 0x4a, 0x5a);
const QColor g_oYellow(0xf2, 0xc1, 0x3d);

QPen f_Ink(const QColor& l_oC, qreal l_dW = 2.5)
{
    return QPen(l_oC, l_dW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
}

void f_DrawPin(QPainter& l_oP, const QPointF& l_oAt, qreal l_dS)
{
    // A map pin: round head, pointed tip at `at`.
    QPainterPath l_oPath;
    l_oPath.moveTo(l_oAt);
    l_oPath.cubicTo(l_oAt + QPointF(-l_dS * 0.9, -l_dS * 1.1), l_oAt + QPointF(-l_dS * 0.9, -l_dS * 2.2), l_oAt + QPointF(0, -l_dS * 2.2));
    l_oPath.cubicTo(l_oAt + QPointF(l_dS * 0.9, -l_dS * 2.2), l_oAt + QPointF(l_dS * 0.9, -l_dS * 1.1), l_oAt);
    l_oP.setPen(QPen(QColor(0, 0, 0, 120), l_dS * 0.15));
    l_oP.setBrush(g_oRed);
    l_oP.drawPath(l_oPath);
    l_oP.setBrush(Qt::white);
    l_oP.setPen(Qt::NoPen);
    l_oP.drawEllipse(l_oAt + QPointF(0, -l_dS * 1.45), l_dS * 0.32, l_dS * 0.32);
}

void f_DrawArrow(QPainter& l_oP, QPointF l_oFrom, QPointF l_oTo)
{
    l_oP.drawLine(l_oFrom, l_oTo);
    const QPointF l_oD = (l_oTo - l_oFrom) / std::max(1.0, std::hypot(l_oTo.x() - l_oFrom.x(), l_oTo.y() - l_oFrom.y()));
    const QPointF l_oN(-l_oD.y(), l_oD.x());
    l_oP.drawLine(l_oTo, l_oTo - l_oD * 6 + l_oN * 6);
    l_oP.drawLine(l_oTo, l_oTo - l_oD * 6 - l_oN * 6);
}

void f_DrawLevel(QPainter& l_oP, Id l_eId)
{
    const QColor l_oC = l_eId == Id::Error ? g_oRed : l_eId == Id::Warning ? g_oYellow : g_oBlue;
    l_oP.setPen(Qt::NoPen);
    l_oP.setBrush(l_oC);
    if (l_eId == Id::Warning) {
        QPolygonF l_oTri({QPointF(16, 3), QPointF(30, 28), QPointF(2, 28)});
        l_oP.drawPolygon(l_oTri);
    } else {
        l_oP.drawEllipse(QRectF(3, 3, 26, 26));
    }
    l_oP.setPen(f_Ink(l_eId == Id::Warning ? QColor(0x20, 0x20, 0x20) : Qt::white, 3.2));
    if (l_eId == Id::Error) {
        l_oP.drawLine(QPointF(11, 11), QPointF(21, 21));
        l_oP.drawLine(QPointF(21, 11), QPointF(11, 21));
    } else if (l_eId == Id::Warning) {
        l_oP.drawLine(QPointF(16, 11), QPointF(16, 19));
        l_oP.drawPoint(QPointF(16, 24));
    } else {
        l_oP.drawLine(QPointF(16, 14), QPointF(16, 22));
        l_oP.drawPoint(QPointF(16, 9));
    }
}

QPixmap f_Render(Id l_eId)
{
    QPixmap l_oPm(g_iSize, g_iSize);
    l_oPm.fill(Qt::transparent);
    QPainter l_oP(&l_oPm);
    l_oP.setRenderHint(QPainter::Antialiasing);
    l_oP.scale(g_iSize / 32.0, g_iSize / 32.0);
    l_oP.setBrush(Qt::NoBrush);
    l_oP.setPen(f_Ink(g_oInk));
    switch (l_eId) {
    case Id::New:
        l_oP.drawPolygon(QPolygonF({QPointF(8, 4), QPointF(19, 4), QPointF(25, 10), QPointF(25, 28), QPointF(8, 28)}));
        l_oP.drawPolyline(QPolygonF({QPointF(19, 4), QPointF(19, 10), QPointF(25, 10)}));
        l_oP.setPen(f_Ink(g_oGreen, 3));
        l_oP.drawLine(QPointF(16, 15), QPointF(16, 23));
        l_oP.drawLine(QPointF(12, 19), QPointF(20, 19));
        break;
    case Id::Open:
    case Id::Folder: {
        QPainterPath l_oPath;
        l_oPath.moveTo(3, 8);
        l_oPath.lineTo(12, 8);
        l_oPath.lineTo(15, 11);
        l_oPath.lineTo(29, 11);
        l_oPath.lineTo(29, 26);
        l_oPath.lineTo(3, 26);
        l_oPath.closeSubpath();
        l_oP.setBrush(QColor(0xe0, 0xb0, 0x50, l_eId == Id::Folder ? 230 : 160));
        l_oP.setPen(f_Ink(QColor(0xc0, 0x90, 0x30), 2));
        l_oP.drawPath(l_oPath);
        break;
    }
    case Id::Save:
        l_oP.drawRoundedRect(QRectF(5, 5, 22, 22), 3, 3);
        l_oP.drawRect(QRectF(10, 5, 12, 7));
        l_oP.setBrush(g_oBlue);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawRect(QRectF(9, 17, 14, 8));
        break;
    case Id::Import:
    case Id::Export:
        l_oP.drawRoundedRect(QRectF(4, 14, 24, 14), 2, 2);
        l_oP.setPen(f_Ink(l_eId == Id::Import ? g_oBlue : g_oGreen, 3));
        if (l_eId == Id::Import) f_DrawArrow(l_oP, QPointF(16, 2), QPointF(16, 20));
        else f_DrawArrow(l_oP, QPointF(16, 21), QPointF(16, 3));
        break;
    case Id::EditMode:
        l_oP.setPen(QPen(g_oOrange, 2, Qt::DashLine));
        l_oP.drawRect(QRectF(4, 4, 18, 18));
        l_oP.setPen(f_Ink(g_oInk));
        l_oP.setBrush(g_oInk);
        l_oP.drawPolygon(QPolygonF({QPointF(16, 14), QPointF(29, 22), QPointF(23, 23), QPointF(26, 29), QPointF(23, 30),
                                 QPointF(20, 25), QPointF(16, 28)}));
        break;
    case Id::ZoomIn:
    case Id::ZoomOut:
        l_oP.drawEllipse(QRectF(4, 4, 18, 18));
        l_oP.setPen(f_Ink(g_oInk, 4));
        l_oP.drawLine(QPointF(20, 20), QPointF(28, 28));
        l_oP.setPen(f_Ink(g_oInk, 2.5));
        l_oP.drawLine(QPointF(9, 13), QPointF(17, 13));
        if (l_eId == Id::ZoomIn) l_oP.drawLine(QPointF(13, 9), QPointF(13, 17));
        break;
    case Id::Fit:
        l_oP.drawPolyline(QPolygonF({QPointF(4, 11), QPointF(4, 4), QPointF(11, 4)}));
        l_oP.drawPolyline(QPolygonF({QPointF(21, 4), QPointF(28, 4), QPointF(28, 11)}));
        l_oP.drawPolyline(QPolygonF({QPointF(28, 21), QPointF(28, 28), QPointF(21, 28)}));
        l_oP.drawPolyline(QPolygonF({QPointF(11, 28), QPointF(4, 28), QPointF(4, 21)}));
        l_oP.setBrush(QColor(g_oBlue.red(), g_oBlue.green(), g_oBlue.blue(), 120));
        l_oP.setPen(Qt::NoPen);
        l_oP.drawRect(QRectF(10, 10, 12, 12));
        break;
    case Id::Image:
        l_oP.drawRoundedRect(QRectF(4, 6, 24, 20), 2, 2);
        l_oP.setBrush(g_oBlue);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawPolygon(QPolygonF({QPointF(6, 24), QPointF(13, 14), QPointF(18, 20), QPointF(21, 17), QPointF(26, 24)}));
        l_oP.setBrush(g_oYellow);
        l_oP.drawEllipse(QRectF(19, 9, 5, 5));
        break;
    case Id::Child:
        l_oP.setPen(QPen(g_oOrange, 2.5, Qt::DashLine));
        l_oP.drawRect(QRectF(5, 5, 22, 22));
        l_oP.setBrush(QColor(g_oOrange.red(), g_oOrange.green(), g_oOrange.blue(), 90));
        l_oP.setPen(Qt::NoPen);
        l_oP.drawRect(QRectF(10, 10, 12, 12));
        break;
    case Id::Pin:
        f_DrawPin(l_oP, QPointF(16, 29), 11);
        break;
    case Id::Grid:
        l_oP.drawRect(QRectF(4, 4, 24, 24));
        l_oP.setPen(QPen(g_oOrange, 2, Qt::DashLine));
        l_oP.drawLine(QPointF(12, 5), QPointF(12, 27));
        l_oP.drawLine(QPointF(20, 5), QPointF(20, 27));
        l_oP.drawLine(QPointF(5, 12), QPointF(27, 12));
        l_oP.drawLine(QPointF(5, 20), QPointF(27, 20));
        break;
    case Id::Bake:
        l_oP.setPen(QPen(g_oOrange, 2.5, Qt::DashLine));
        l_oP.drawRect(QRectF(3, 3, 16, 16));
        l_oP.setPen(f_Ink(g_oBlue));
        l_oP.setBrush(QColor(g_oBlue.red(), g_oBlue.green(), g_oBlue.blue(), 90));
        l_oP.drawRect(QRectF(13, 13, 16, 16));
        break;
    case Id::Exclude:
        l_oP.setPen(f_Ink(g_oRed, 3));
        l_oP.drawEllipse(QRectF(5, 5, 22, 22));
        l_oP.drawLine(QPointF(9, 23), QPointF(23, 9));
        break;
    case Id::Add:
        l_oP.setPen(f_Ink(g_oGreen, 3.5));
        l_oP.drawLine(QPointF(16, 6), QPointF(16, 26));
        l_oP.drawLine(QPointF(6, 16), QPointF(26, 16));
        break;
    case Id::Remove:
        l_oP.setPen(f_Ink(g_oRed, 3.5));
        l_oP.drawLine(QPointF(6, 16), QPointF(26, 16));
        break;
    case Id::Up:
    case Id::Down:
        l_oP.setPen(f_Ink(g_oInk, 3));
        if (l_eId == Id::Up) f_DrawArrow(l_oP, QPointF(16, 27), QPointF(16, 5));
        else f_DrawArrow(l_oP, QPointF(16, 5), QPointF(16, 27));
        break;
    case Id::Play:
        l_oP.setBrush(g_oGreen);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawPolygon(QPolygonF({QPointF(8, 5), QPointF(27, 16), QPointF(8, 27)}));
        break;
    case Id::Pause:
        l_oP.setBrush(g_oInk);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawRect(QRectF(8, 6, 6, 20));
        l_oP.drawRect(QRectF(18, 6, 6, 20));
        break;
    case Id::Back:
        l_oP.setPen(f_Ink(g_oInk, 3));
        f_DrawArrow(l_oP, QPointF(26, 16), QPointF(6, 16));
        break;
    case Id::Forward:
        l_oP.setPen(f_Ink(g_oInk, 3));
        f_DrawArrow(l_oP, QPointF(6, 16), QPointF(26, 16));
        break;
    case Id::Brush: {   // a paint brush: weight painting
        l_oP.setPen(Qt::NoPen);
        l_oP.setBrush(g_oOrange);
        l_oP.drawEllipse(QPointF(9, 23), 6, 6);
        l_oP.drawPolygon(QPolygonF({QPointF(4, 25), QPointF(9, 29), QPointF(3, 30)}));
        l_oP.setPen(f_Ink(g_oInk, 4));
        l_oP.drawLine(QPointF(13, 19), QPointF(27, 5));
        break;
    }
    case Id::Solo:   // a target: show only this
        l_oP.setPen(f_Ink(g_oOrange, 2.5));
        l_oP.setBrush(Qt::NoBrush);
        l_oP.drawEllipse(QPointF(16, 16), 11, 11);
        l_oP.setPen(Qt::NoPen);
        l_oP.setBrush(g_oOrange);
        l_oP.drawEllipse(QPointF(16, 16), 5, 5);
        break;
    case Id::Error:
    case Id::Warning:
    case Id::Info:
        f_DrawLevel(l_oP, l_eId);
        break;
    case Id::Move:
        l_oP.setPen(f_Ink(g_oInk, 2.5));
        f_DrawArrow(l_oP, QPointF(16, 16), QPointF(16, 3));
        f_DrawArrow(l_oP, QPointF(16, 16), QPointF(16, 29));
        f_DrawArrow(l_oP, QPointF(16, 16), QPointF(3, 16));
        f_DrawArrow(l_oP, QPointF(16, 16), QPointF(29, 16));
        break;
    case Id::Rotate: {
        l_oP.setPen(f_Ink(g_oBlue, 3));
        l_oP.drawArc(QRectF(5, 5, 22, 22), 100 * 16, 280 * 16);
        l_oP.setBrush(g_oBlue);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawPolygon(QPolygonF({QPointF(13, 1), QPointF(20, 6), QPointF(13, 11)}));
        break;
    }
    case Id::Scale:
        l_oP.drawRect(QRectF(4, 12, 16, 16));
        l_oP.setPen(f_Ink(g_oOrange, 3));
        f_DrawArrow(l_oP, QPointF(14, 18), QPointF(28, 4));
        break;
    case Id::KeyOn:
    case Id::KeyOff: {
        const QPolygonF l_oDiamond({QPointF(16, 4), QPointF(28, 16), QPointF(16, 28), QPointF(4, 16)});
        l_oP.setPen(f_Ink(g_oYellow, 3));
        l_oP.setBrush(l_eId == Id::KeyOn ? QBrush(g_oYellow) : QBrush(Qt::NoBrush));
        l_oP.drawPolygon(l_oDiamond);
        break;
    }
    case Id::KeyNone: {
        QColor l_oC = g_oInk;
        l_oC.setAlpha(110);
        l_oP.setPen(QPen(l_oC, 2, Qt::DotLine));
        l_oP.drawPolygon(QPolygonF({QPointF(16, 6), QPointF(26, 16), QPointF(16, 26), QPointF(6, 16)}));
        break;
    }
    case Id::Eye:
    case Id::EyeOff: {
        QPainterPath l_oEye;
        l_oEye.moveTo(3, 16);
        l_oEye.quadTo(16, 3, 29, 16);
        l_oEye.quadTo(16, 29, 3, 16);
        QColor l_oC = l_eId == Id::Eye ? g_oInk : QColor(g_oInk.red(), g_oInk.green(), g_oInk.blue(), 90);
        l_oP.setPen(f_Ink(l_oC, 2.5));
        l_oP.drawPath(l_oEye);
        l_oP.setBrush(l_oC);
        l_oP.drawEllipse(QPointF(16, 16), 4, 4);
        if (l_eId == Id::EyeOff) {
            l_oP.setPen(f_Ink(g_oRed, 3));
            l_oP.drawLine(QPointF(6, 27), QPointF(26, 5));
        }
        break;
    }
    case Id::Stop:
        l_oP.setBrush(g_oInk);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawRect(QRectF(8, 8, 16, 16));
        break;
    case Id::Loop: {
        l_oP.setPen(f_Ink(g_oInk, 2.8));
        l_oP.drawArc(QRectF(4, 7, 24, 18), 30 * 16, 300 * 16);
        l_oP.setBrush(g_oInk);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawPolygon(QPolygonF({QPointF(22, 3), QPointF(30, 9), QPointF(21, 12)}));
        break;
    }
    case Id::Node:
        l_oP.drawEllipse(QRectF(11, 3, 10, 10));
        l_oP.drawLine(QPointF(16, 13), QPointF(16, 18));
        l_oP.drawLine(QPointF(7, 18), QPointF(25, 18));
        l_oP.drawLine(QPointF(7, 18), QPointF(7, 22));
        l_oP.drawLine(QPointF(25, 18), QPointF(25, 22));
        l_oP.setBrush(g_oBlue);
        l_oP.drawEllipse(QRectF(3, 22, 8, 8));
        l_oP.drawEllipse(QRectF(21, 22, 8, 8));
        break;
    case Id::SpriteNode:
        l_oP.drawRoundedRect(QRectF(4, 6, 24, 20), 2, 2);
        l_oP.setBrush(g_oGreen);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawPolygon(QPolygonF({QPointF(6, 24), QPointF(13, 14), QPointF(18, 20), QPointF(21, 17), QPointF(26, 24)}));
        break;
    case Id::Duplicate:
        l_oP.drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
        l_oP.setBrush(QColor(g_oBlue.red(), g_oBlue.green(), g_oBlue.blue(), 90));
        l_oP.setPen(f_Ink(g_oBlue));
        l_oP.drawRoundedRect(QRectF(12, 12, 16, 16), 2, 2);
        break;
    case Id::Event:
        l_oP.setPen(f_Ink(g_oOrange, 3));
        l_oP.drawLine(QPointF(8, 4), QPointF(8, 29));
        l_oP.setBrush(g_oOrange);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawPolygon(QPolygonF({QPointF(9, 4), QPointF(27, 10), QPointF(9, 16)}));
        break;
    case Id::Rename:
        l_oP.drawRect(QRectF(3, 10, 26, 13));
        l_oP.setPen(f_Ink(g_oBlue, 2.5));
        l_oP.drawLine(QPointF(20, 6), QPointF(20, 27));
        l_oP.drawLine(QPointF(17, 6), QPointF(23, 6));
        l_oP.drawLine(QPointF(17, 27), QPointF(23, 27));
        break;
    case Id::AutoKey:
        l_oP.setBrush(g_oRed);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawEllipse(QRectF(4, 4, 24, 24));
        l_oP.setBrush(Qt::white);
        l_oP.drawPolygon(QPolygonF({QPointF(16, 9), QPointF(23, 16), QPointF(16, 23), QPointF(9, 16)}));
        break;
    case Id::Snap:
        l_oP.setPen(QPen(g_oInk, 1.5, Qt::DotLine));
        for (int l_iI = 6; l_iI <= 26; l_iI += 10) {
            l_oP.drawLine(QPointF(l_iI, 3), QPointF(l_iI, 29));
            l_oP.drawLine(QPointF(3, l_iI), QPointF(29, l_iI));
        }
        l_oP.setBrush(g_oOrange);
        l_oP.setPen(Qt::NoPen);
        l_oP.drawEllipse(QPointF(16, 16), 4.5, 4.5);
        break;
    }
    return l_oPm;
}

}  // namespace

QIcon f_Icon(Id l_eId)
{
    static QHash<int, QIcon> l_oCache;
    auto l_oIt = l_oCache.find(int(l_eId));
    if (l_oIt == l_oCache.end()) l_oIt = l_oCache.insert(int(l_eId), QIcon(f_Render(l_eId)));
    return *l_oIt;
}

QIcon f_SpriteIcon(const QImage& l_oThumb, bool l_bChild, bool l_bPinned, bool l_bExcluded)
{
    constexpr int l_iS = 64;
    QPixmap l_oPm(l_iS, l_iS);
    l_oPm.fill(Qt::transparent);
    QPainter l_oP(&l_oPm);
    l_oP.setRenderHint(QPainter::Antialiasing);
    const QRect l_oInner = l_bChild ? QRect(8, 8, l_iS - 16, l_iS - 16) : QRect(2, 2, l_iS - 4, l_iS - 4);
    if (!l_oThumb.isNull()) {
        // Pixel art must stay crisp: only smooth when shrinking.
        const QSize l_oFit = l_oThumb.size().scaled(l_oInner.size(), Qt::KeepAspectRatio);
        const bool l_bShrink = l_oFit.width() < l_oThumb.width();
        l_oP.setRenderHint(QPainter::SmoothPixmapTransform, l_bShrink);
        const QRect l_oR(l_oInner.center().x() - l_oFit.width() / 2 + 1, l_oInner.center().y() - l_oFit.height() / 2 + 1, l_oFit.width(), l_oFit.height());
        if (l_bExcluded) l_oP.setOpacity(0.35);
        l_oP.drawImage(l_oR, l_oThumb);
        l_oP.setOpacity(1.0);
    }
    if (l_bChild) {
        l_oP.setPen(QPen(g_oOrange, 4, Qt::DashLine));
        l_oP.setBrush(Qt::NoBrush);
        l_oP.drawRect(QRectF(3, 3, l_iS - 6, l_iS - 6));
    }
    if (l_bPinned) f_DrawPin(l_oP, QPointF(l_iS - 12, l_iS - 2), 11);
    if (l_bExcluded) {
        l_oP.setPen(f_Ink(g_oRed, 5));
        l_oP.drawLine(QPointF(10, l_iS - 10), QPointF(l_iS - 10, 10));
    }
    return QIcon(l_oPm);
}

}  // namespace Icons

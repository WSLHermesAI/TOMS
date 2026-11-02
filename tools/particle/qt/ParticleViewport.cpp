#include "ParticleViewport.h"

#include "ParticleDocument.h"
#include "ParticlePlayback.h"
#include "Theme.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

using toms::fx::Emitter;
using toms::fx::Shape;

namespace {

const char* g_pszSpriteMime = "application/x-toms-sprite";
constexpr double g_dArrow = 70;      // widget pixels: direction arrow length
constexpr double g_dHandle = 6;      // widget pixels: handle radius

QStringList f_RefsFrom(const QMimeData* l_pM)
{
    if (!l_pM || !l_pM->hasFormat(QLatin1String(g_pszSpriteMime))) return {};
    return QString::fromUtf8(l_pM->data(QLatin1String(g_pszSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QPointF f_Polar(double l_dDegrees, double l_dLength)
{
    const double l_dR = l_dDegrees * M_PI / 180.0;
    return QPointF(std::cos(l_dR) * l_dLength, std::sin(l_dR) * l_dLength);
}

double f_AngleOf(const QPointF& l_oV) { return std::atan2(l_oV.y(), l_oV.x()) * 180.0 / M_PI; }

double f_Wrap180(double l_dA)
{
    while (l_dA > 180) l_dA -= 360;
    while (l_dA < -180) l_dA += 360;
    return l_dA;
}

}  // namespace

ParticleViewport::ParticleViewport(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, bool l_bGameRenderer, QWidget* l_pParent)
    : GameCanvasView(l_bGameRenderer, l_pParent)
    , m_pDoc(l_pDoc)
    , m_pPlay(l_pPlayback)
    , m_oPlayGuard(l_pPlayback)
{
    setAcceptDrops(true);
    connect(l_pDoc, &ParticleDocument::f_AtlasChanged, this, [this] { m_bAtlasDirty = true; });
    connect(l_pPlayback, &ParticlePlayback::f_Ticked, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &ParticleDocument::f_SelectionChanged, this, qOverload<>(&QWidget::update));
}

void ParticleViewport::f_SetBackground(Background l_eB)
{
    m_eBackground = l_eB;
    update();
}

bool ParticleViewport::f_SetBackgroundImage(const QString& l_oPath)
{
    QImage l_oImg(l_oPath);
    if (l_oImg.isNull()) return false;
    m_oBgImage = l_oImg.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_oBgPath = l_oPath;
    m_eBackground = Background::Image;
    update();
    return true;
}

int ParticleViewport::f_Batches(const std::vector<Quad>& l_vQuads)
{
    int l_iN = 0;
    for (size_t l_nI = 0; l_nI < l_vQuads.size(); l_nI++)
        if (l_nI == 0 || l_vQuads[l_nI].m_u16Texture != l_vQuads[l_nI - 1].m_u16Texture || l_vQuads[l_nI].m_bAdditive != l_vQuads[l_nI - 1].m_bAdditive) l_iN++;
    return l_iN;
}

void ParticleViewport::f_DrawQuads(QPainter& l_oP, const std::vector<Quad>& l_vQuads, const ParticleDocument& l_oDoc, const QTransform& l_oView)
{
    l_oP.save();
    l_oP.setRenderHint(QPainter::SmoothPixmapTransform, true);   // particles are soft: filter like the fx atlas (Linear)
    for (const Quad& l_oQ : l_vQuads) {
        int l_iPage = 0;
        const SpriteImageCache* l_pImages = l_oDoc.f_PageImages(l_oQ.m_u16Texture, &l_iPage);
        if (!l_pImages || l_iPage < 0 || l_iPage >= l_pImages->f_Pages().size()) continue;
        const QSize l_oPs = l_pImages->f_Pages()[l_iPage].size();
        const int l_iX0 = int(std::lround(l_oQ.m_arrUv[0] * l_oPs.width())), l_iY0 = int(std::lround(l_oQ.m_arrUv[1] * l_oPs.height()));
        const int l_iX1 = int(std::lround(l_oQ.m_arrUv[2] * l_oPs.width())), l_iY1 = int(std::lround(l_oQ.m_arrUv[3] * l_oPs.height()));
        const QRect l_oSrc(l_iX0, l_iY0, l_iX1 - l_iX0, l_iY1 - l_iY0);
        if (l_oSrc.isEmpty()) continue;
        const QImage l_oImg = l_pImages->f_Tinted(l_iPage, l_oSrc, l_oQ.m_arrTint);
        if (l_oImg.isNull()) continue;
        // Corners 0, 1, 3 span the parallelogram the sprite's pixel rect maps onto.
        const double l_dSw = l_oSrc.width(), l_dSh = l_oSrc.height();
        const QPointF l_oC0(l_oQ.m_arrCorners[0], l_oQ.m_arrCorners[1]), l_oC1(l_oQ.m_arrCorners[2], l_oQ.m_arrCorners[3]), l_oC3(l_oQ.m_arrCorners[6], l_oQ.m_arrCorners[7]);
        const QTransform l_oQuad((l_oC1.x() - l_oC0.x()) / l_dSw, (l_oC1.y() - l_oC0.y()) / l_dSw, (l_oC3.x() - l_oC0.x()) / l_dSh, (l_oC3.y() - l_oC0.y()) / l_dSh, l_oC0.x(), l_oC0.y());
        l_oP.setTransform(l_oQuad * l_oView);
        // Additive = the game's SRC_ALPHA, ONE: the premultiplied tinted sprite plus the screen.
        l_oP.setCompositionMode(l_oQ.m_bAdditive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
        l_oP.drawImage(QPointF(0, 0), l_oImg);
    }
    l_oP.restore();
}

QPointF ParticleViewport::f_EmitterPos() const
{
    const Emitter* l_pE = m_pDoc->f_Emitter();
    return m_pPlay->f_Origin() + (l_pE ? QPointF(l_pE->m_v2Offset.x, l_pE->m_v2Offset.y) : QPointF());
}

QPointF ParticleViewport::f_HandlePos(Handle l_eH) const
{
    const Emitter* l_pE = m_pDoc->f_Emitter();
    if (!l_pE || !m_bGizmo) return QPointF(qQNaN(), qQNaN());
    const QPointF l_oC = f_ToWidget(f_EmitterPos());
    const Shape& l_oS = l_pE->m_oShape;
    switch (l_eH) {
    case Handle::Move: return l_oC;
    case Handle::Direction: return l_oS.m_bOutward ? QPointF(qQNaN(), qQNaN()) : l_oC + f_Polar(l_pE->m_fDirection, g_dArrow);
    case Handle::SpreadA: return l_oS.m_bOutward ? QPointF(qQNaN(), qQNaN()) : l_oC + f_Polar(l_pE->m_fDirection - l_pE->m_fSpread, g_dArrow * 0.7);
    case Handle::SpreadB: return l_oS.m_bOutward ? QPointF(qQNaN(), qQNaN()) : l_oC + f_Polar(l_pE->m_fDirection + l_pE->m_fSpread, g_dArrow * 0.7);
    case Handle::Shape:
        switch (l_oS.m_eType) {
        case Shape::Point: return QPointF(qQNaN(), qQNaN());
        case Shape::Line: return f_ToWidget(f_EmitterPos() + f_Polar(l_oS.m_fAngle, l_oS.m_fLength / 2));
        case Shape::Box: return f_ToWidget(f_EmitterPos() + QPointF(l_oS.m_fWidth / 2, l_oS.m_fHeight / 2));
        case Shape::Circle:
        case Shape::Ring: return f_ToWidget(f_EmitterPos() + QPointF(l_oS.m_fRadius, 0));
        }
        break;
    case Handle::None: break;
    }
    return QPointF(qQNaN(), qQNaN());
}

ParticleViewport::Handle ParticleViewport::f_HitHandle(const QPointF& l_oW) const
{
    // Small handles first: they sit on top of the move square.
    for (Handle l_eH : {Handle::Shape, Handle::SpreadA, Handle::SpreadB, Handle::Direction, Handle::Move}) {
        const QPointF l_oP = f_HandlePos(l_eH);
        if (!std::isnan(l_oP.x()) && QLineF(l_oP, l_oW).length() <= g_dHandle + 3) return l_eH;
    }
    return Handle::None;
}

void ParticleViewport::f_PaintContent(QPainter& l_oP)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    const QTransform l_oView = f_ViewTransform();
    if (f_OverGameScene()) {   // the game renderer drew the background, grid, origin and effect
        l_oP.setRenderHint(QPainter::Antialiasing, true);
        if (m_bGizmo) f_PaintGizmo(l_oP);
        f_DrawLabel(l_oP, QPointF(8, 8), f_StatusText());
        f_DrawLabel(l_oP, QPointF(8, height() - 26),
                  tr("Click: fire here   Ctrl+drag: move it   Double-click: back to 0,0   Right-drag: pan   Wheel: zoom"));
        return;
    }
    // Background over the checkerboard the base drew.
    if (m_eBackground == Background::Dark) l_oP.fillRect(f_ToWidget(f_ContentRect()), QColor(24, 24, 30));
    else if (m_eBackground == Background::Light) l_oP.fillRect(f_ToWidget(f_ContentRect()), QColor(225, 225, 230));
    else if (m_eBackground == Background::Image && !m_oBgImage.isNull()) {
        l_oP.save();
        l_oP.setTransform(l_oView);
        l_oP.drawImage(QPointF(-m_oBgImage.width() / 2.0, -m_oBgImage.height() / 2.0), m_oBgImage);
        l_oP.restore();
    }
    if (m_bGrid && f_Zoom() * 32 >= 8) {
        const QPointF l_oA = f_ToContent(QPointF(0, 0)), l_oB = f_ToContent(QPointF(width(), height()));
        l_oP.setPen(QPen(l_oTc.m_oGrid, 1));
        for (double l_dX = std::floor(l_oA.x() / 32) * 32; l_dX <= l_oB.x(); l_dX += 32) {
            const double l_dWx = std::round(f_ToWidget(QPointF(l_dX, 0)).x()) + 0.5;
            l_oP.drawLine(QPointF(l_dWx, 0), QPointF(l_dWx, height()));
        }
        for (double l_dY = std::floor(l_oA.y() / 32) * 32; l_dY <= l_oB.y(); l_dY += 32) {
            const double l_dWy = std::round(f_ToWidget(QPointF(0, l_dY)).y()) + 0.5;
            l_oP.drawLine(QPointF(0, l_dWy), QPointF(width(), l_dWy));
        }
    }
    {   // the effect's origin
        const QPointF l_oO = f_ToWidget(m_pPlay->f_Origin());
        l_oP.setPen(QPen(l_oTc.m_oGridAxis, 1));
        l_oP.drawLine(l_oO + QPointF(-10, 0), l_oO + QPointF(10, 0));
        l_oP.drawLine(l_oO + QPointF(0, -10), l_oO + QPointF(0, 10));
    }

    m_vQuads.clear();
    m_pPlay->f_Instance().f_AppendQuads(m_pDoc->f_AtlasSet(), glm::mat3(1.0f), nullptr, m_vQuads);
    f_DrawQuads(l_oP, m_vQuads, *m_pDoc, l_oView);

    l_oP.setRenderHint(QPainter::Antialiasing, true);
    if (m_bGizmo) f_PaintGizmo(l_oP);

    // Status over the view.
    f_DrawLabel(l_oP, QPointF(8, 8), f_StatusText());
    f_DrawLabel(l_oP, QPointF(8, height() - 26),
              tr("Click: fire here   Ctrl+drag: move it   Double-click: back to 0,0   Right-drag: pan   Wheel: zoom"));
}

QString ParticleViewport::f_StatusText() const
{
    const toms::fx::EffectInstance& l_oFx = m_pPlay->f_Instance();
    if (f_UsingGameRenderer())
        return QStringLiteral("%1   FPS %2   t %3 s   live %4   GPU-simulated emitters %5 (above %6)   seed %7%8")
            .arg(f_RendererName())
            .arg(m_dFps, 0, 'f', 1)
            .arg(l_oFx.f_Time(), 0, 'f', 2)
            .arg(l_oFx.f_LiveCount())
            .arg(l_oFx.f_GpuEmitters())
            .arg(m_pPlay->f_GpuThreshold())
            .arg(m_pPlay->f_Seed())
            .arg(!m_pPlay->f_Playing() ? tr("   (paused)") : l_oFx.f_Finished() ? tr("   (finished)") : QString());
    QString l_oStatus = QStringLiteral("t %1 s   live %2   quads %3   batches %4   %5 ms   seed %6")
                         .arg(l_oFx.f_Time(), 0, 'f', 2)
                         .arg(l_oFx.f_LiveCount())
                         .arg(m_vQuads.size())
                         .arg(f_Batches(m_vQuads))
                         .arg(m_pPlay->f_LastStepMs(), 0, 'f', 2)
                         .arg(m_pPlay->f_Seed());
    if (!m_pPlay->f_Playing()) l_oStatus += tr("   (paused)");
    else if (l_oFx.f_Finished()) l_oStatus += tr("   (finished)");
    return l_oStatus;
}

void ParticleViewport::f_PaintGizmo(QPainter& l_oP)
{
    const Emitter* l_pE = m_pDoc->f_Emitter();
    if (!l_pE) return;
    const Theme::Colors& l_oTc = Theme::f_Colors();
    const QPointF l_oC = f_ToWidget(f_EmitterPos());
    const Shape& l_oS = l_pE->m_oShape;
    // The shape outline (content size, so it zooms).
    l_oP.setPen(QPen(l_oTc.m_oSelection, 1, Qt::DashLine));
    l_oP.setBrush(Qt::NoBrush);
    const double l_dZ = f_Zoom();
    switch (l_oS.m_eType) {
    case Shape::Point: break;
    case Shape::Line: {
        const QPointF l_oH = f_Polar(l_oS.m_fAngle, l_oS.m_fLength / 2 * l_dZ);
        l_oP.drawLine(l_oC - l_oH, l_oC + l_oH);
        break;
    }
    case Shape::Box: l_oP.drawRect(QRectF(l_oC - QPointF(l_oS.m_fWidth / 2 * l_dZ, l_oS.m_fHeight / 2 * l_dZ), QSizeF(l_oS.m_fWidth * l_dZ, l_oS.m_fHeight * l_dZ))); break;
    case Shape::Circle:
    case Shape::Ring: {
        const double l_dR = l_oS.m_fRadius * l_dZ;
        if (l_oS.m_fArcTo - l_oS.m_fArcFrom >= 360) l_oP.drawEllipse(l_oC, l_dR, l_dR);
        else l_oP.drawArc(QRectF(l_oC.x() - l_dR, l_oC.y() - l_dR, 2 * l_dR, 2 * l_dR), int(-l_oS.m_fArcFrom * 16), int(-(l_oS.m_fArcTo - l_oS.m_fArcFrom) * 16));
        if (l_oS.m_eType == Shape::Ring && l_oS.m_fThickness > 0) {
            const double l_dR2 = std::max(0.0, (l_oS.m_fRadius - l_oS.m_fThickness) * l_dZ);
            l_oP.drawEllipse(l_oC, l_dR2, l_dR2);
        }
        break;
    }
    }
    // Direction and spread (not for "outward" shapes: they fly away from the centre).
    if (!l_oS.m_bOutward) {
        {
            QPainterPath l_oWedge;
            l_oWedge.moveTo(l_oC);
            l_oWedge.arcTo(QRectF(l_oC.x() - g_dArrow * 0.7, l_oC.y() - g_dArrow * 0.7, g_dArrow * 1.4, g_dArrow * 1.4), -(l_pE->m_fDirection - l_pE->m_fSpread),
                        -2 * l_pE->m_fSpread);
            l_oWedge.closeSubpath();
            QColor l_oFill = l_oTc.m_oSelection;
            l_oFill.setAlpha(40);
            l_oP.setPen(Qt::NoPen);
            l_oP.setBrush(l_oFill);
            l_oP.drawPath(l_oWedge);
            l_oP.setPen(QPen(l_oTc.m_oGizmoX, 2));
            const QPointF l_oTip = l_oC + f_Polar(l_pE->m_fDirection, g_dArrow);
            l_oP.drawLine(l_oC, l_oTip);
            l_oP.drawLine(l_oTip, l_oTip + f_Polar(l_pE->m_fDirection + 150, 10));
            l_oP.drawLine(l_oTip, l_oTip + f_Polar(l_pE->m_fDirection - 150, 10));
        }
    }
    auto l_fnHandle = [&](Handle l_eH, const QColor& l_oCol, bool l_bSquare) {
        const QPointF l_oHp = f_HandlePos(l_eH);
        if (std::isnan(l_oHp.x())) return;
        l_oP.setPen(QPen(m_eHover == l_eH || m_eDrag == l_eH ? Qt::white : Qt::black, 1));
        l_oP.setBrush(l_oCol);
        if (l_bSquare) l_oP.drawRect(QRectF(l_oHp - QPointF(g_dHandle, g_dHandle), QSizeF(2 * g_dHandle, 2 * g_dHandle)));
        else l_oP.drawEllipse(l_oHp, g_dHandle, g_dHandle);
    };
    l_fnHandle(Handle::Shape, l_oTc.m_oGizmoRotate, false);
    l_fnHandle(Handle::SpreadA, l_oTc.m_oSelection, false);
    l_fnHandle(Handle::SpreadB, l_oTc.m_oSelection, false);
    l_fnHandle(Handle::Direction, l_oTc.m_oGizmoX, false);
    l_fnHandle(Handle::Move, l_oTc.m_oGizmoY, true);
    // What is being dragged.
    if (m_eDrag == Handle::Direction) f_DrawLabel(l_oP, l_oC + QPointF(12, 12), tr("direction %1°").arg(l_pE->m_fDirection, 0, 'f', 0));
    else if (m_eDrag == Handle::SpreadA || m_eDrag == Handle::SpreadB) f_DrawLabel(l_oP, l_oC + QPointF(12, 12), tr("spread ±%1°").arg(l_pE->m_fSpread, 0, 'f', 0));
    else if (m_eDrag == Handle::Move) f_DrawLabel(l_oP, l_oC + QPointF(12, 12), tr("offset %1, %2").arg(l_pE->m_v2Offset.x, 0, 'f', 0).arg(l_pE->m_v2Offset.y, 0, 'f', 0));
}

void ParticleViewport::mousePressEvent(QMouseEvent* l_pE)
{
    if (f_PanPress(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton) return;
    m_oPressWidget = l_pE->position();
    m_oPressContent = f_ToContent(l_pE->position());
    m_oStartOrigin = m_pPlay->f_Origin();
    m_iGesture++;
    if (l_pE->modifiers() & Qt::ControlModifier) {
        m_bMovingOrigin = true;
        return;
    }
    m_eDrag = f_HitHandle(l_pE->position());
    if (const Emitter* l_pEm = m_pDoc->f_Emitter()) m_oStartOffset = QPointF(l_pEm->m_v2Offset.x, l_pEm->m_v2Offset.y);
    m_bPressedEmpty = m_eDrag == Handle::None;
    update();
}

void ParticleViewport::f_DragTo(const QPointF& l_oCp, Qt::KeyboardModifiers l_oMods)
{
    const QString l_oKey = QStringLiteral("gizmo%1").arg(m_iGesture);
    const QPointF l_oEp = f_EmitterPos();
    switch (m_eDrag) {
    case Handle::Move: {
        QPointF l_oOff = m_oStartOffset + (l_oCp - m_oPressContent);
        if (l_oMods & Qt::ShiftModifier) l_oOff = QPointF(std::round(l_oOff.x() / 8) * 8, std::round(l_oOff.y() / 8) * 8);
        m_pDoc->f_EditEmitter(tr("Move emitter"), [&](Emitter& l_oM) { l_oM.m_v2Offset = glm::vec2(float(std::round(l_oOff.x())), float(std::round(l_oOff.y()))); }, l_oKey);
        break;
    }
    case Handle::Direction: {
        double l_dA = f_AngleOf(l_oCp - l_oEp);
        l_dA = (l_oMods & Qt::ShiftModifier) ? std::round(l_dA / 15) * 15 : std::round(l_dA);
        m_pDoc->f_EditEmitter(tr("Direction"), [&](Emitter& l_oM) { l_oM.m_fDirection = float(l_dA); }, l_oKey);
        break;
    }
    case Handle::SpreadA:
    case Handle::SpreadB: {
        const Emitter* l_pEm = m_pDoc->f_Emitter();
        double l_dSp = std::fabs(f_Wrap180(f_AngleOf(l_oCp - l_oEp) - l_pEm->m_fDirection));
        l_dSp = (l_oMods & Qt::ShiftModifier) ? std::round(l_dSp / 15) * 15 : std::round(l_dSp);
        m_pDoc->f_EditEmitter(tr("Spread"), [&](Emitter& l_oM) { l_oM.m_fSpread = float(std::min(180.0, l_dSp)); }, l_oKey);
        break;
    }
    case Handle::Shape: {
        const QPointF l_oD = l_oCp - l_oEp;
        m_pDoc->f_EditEmitter(tr("Shape size"), [&](Emitter& l_oM) {
            switch (l_oM.m_oShape.m_eType) {
            case Shape::Line:
                l_oM.m_oShape.m_fLength = float(std::round(2 * std::hypot(l_oD.x(), l_oD.y())));
                l_oM.m_oShape.m_fAngle = float(std::round(f_AngleOf(l_oD)));
                break;
            case Shape::Box:
                l_oM.m_oShape.m_fWidth = float(std::round(2 * std::fabs(l_oD.x())));
                l_oM.m_oShape.m_fHeight = float(std::round(2 * std::fabs(l_oD.y())));
                break;
            case Shape::Circle:
            case Shape::Ring: l_oM.m_oShape.m_fRadius = float(std::round(std::hypot(l_oD.x(), l_oD.y()))); break;
            case Shape::Point: break;
            }
        }, l_oKey);
        break;
    }
    case Handle::None: break;
    }
}

void ParticleViewport::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (f_PanMove(l_pE)) return;
    const QPointF l_oCp = f_ToContent(l_pE->position());
    if (m_bMovingOrigin) {
        const QPointF l_oO = m_oStartOrigin + (l_oCp - m_oPressContent);
        m_pPlay->f_SetOrigin(QPointF(std::round(l_oO.x()), std::round(l_oO.y())), false);
        return;
    }
    if (m_eDrag != Handle::None && (l_pE->buttons() & Qt::LeftButton)) {
        f_DragTo(l_oCp, l_pE->modifiers());
        return;
    }
    const Handle l_eH = f_HitHandle(l_pE->position());
    if (l_eH != m_eHover) {
        m_eHover = l_eH;
        setCursor(l_eH == Handle::None ? Qt::ArrowCursor : l_eH == Handle::Move ? Qt::SizeAllCursor : Qt::PointingHandCursor);
        update();
    }
}

void ParticleViewport::mouseReleaseEvent(QMouseEvent* l_pE)
{
    if (f_PanRelease(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton) return;
    const bool l_bClick = (l_pE->position() - m_oPressWidget).manhattanLength() < 4;
    if (m_bPressedEmpty && l_bClick && !m_bMovingOrigin) {   // fire the effect here
        const QPointF l_oCp = f_ToContent(l_pE->position());
        m_pPlay->f_SetOrigin(QPointF(std::round(l_oCp.x()), std::round(l_oCp.y())), true);
    }
    m_eDrag = Handle::None;
    m_bMovingOrigin = false;
    m_bPressedEmpty = false;
    update();
}

void ParticleViewport::mouseDoubleClickEvent(QMouseEvent* l_pE)
{
    if (l_pE->button() == Qt::LeftButton && f_HitHandle(l_pE->position()) == Handle::None) m_pPlay->f_SetOrigin(QPointF(0, 0), true);
}

void ParticleViewport::dragEnterEvent(QDragEnterEvent* l_pE)
{
    if (f_RefsFrom(l_pE->mimeData()).isEmpty()) return;
    l_pE->setDropAction(Qt::CopyAction);
    l_pE->accept();
}

void ParticleViewport::dragMoveEvent(QDragMoveEvent* l_pE)
{
    if (f_RefsFrom(l_pE->mimeData()).isEmpty()) return;
    l_pE->setDropAction(Qt::CopyAction);
    l_pE->accept();
}

void ParticleViewport::dropEvent(QDropEvent* l_pE)
{
    const QStringList l_oRefs = f_RefsFrom(l_pE->mimeData());
    if (l_oRefs.isEmpty()) return;
    l_pE->setDropAction(Qt::CopyAction);
    l_pE->accept();
    emit f_SpritesDropped(l_oRefs, f_ToContent(l_pE->position()) - m_pPlay->f_Origin());
}

#include "SpriteEditCanvas.h"

#include "AtlasDocument.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr double g_dHandleRadius = 5.0;   // screen pixels
constexpr double g_dGrabDistance = 6.0;

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }
QRect f_ToQRect(const atlas::IRect& l_oR) { return QRect(l_oR.m_iX, l_oR.m_iY, l_oR.m_iW, l_oR.m_iH); }
atlas::IRect f_ToIRect(const QRect& l_oR) { return atlas::IRect{l_oR.x(), l_oR.y(), l_oR.width(), l_oR.height()}; }

}  // namespace

SpriteEditCanvas::SpriteEditCanvas(AtlasDocument* l_pDoc, QWidget* l_pParent)
#if TOMS_ATLAS_GAME_RENDERER
    : GameCanvasView(GameCanvasView::f_GameRendererWanted(QStringLiteral("AtlasEditor")), l_pParent)
#else
    : CanvasView(parent)
#endif
    , m_pDoc(l_pDoc)
{
    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, &SpriteEditCanvas::f_Reload);
    connect(l_pDoc, &AtlasDocument::f_ProjectChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AtlasDocument::f_SelectionChanged, this, qOverload<>(&QWidget::update));
}

void SpriteEditCanvas::f_SetTarget(const QString& l_oName)
{
    m_oTarget = l_oName;
    m_eDrag = Drag::None;
    f_Reload();
    f_RequestFit();
    emit f_TargetChanged(m_oTarget);
}

void SpriteEditCanvas::f_Reload()
{
    if (m_oTarget.isEmpty()) return;
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    if (l_spSnap && !l_spSnap->f_Region(m_oTarget) && !m_pDoc->f_SpriteExists(m_oTarget)) {
        // Deleted (or renamed) while being edited.
        m_oTarget.clear();
        emit f_ExitRequested();
        return;
    }
    const QImage l_oImg = m_pDoc->f_SpriteImage(m_oTarget);
    if (!l_oImg.isNull()) m_oImage = l_oImg;   // keep the old pixels while the sprite fails to build
    update();
}

QVector<SpriteEditCanvas::Child> SpriteEditCanvas::f_Children() const
{
    QVector<Child> l_oOut;
    const std::string l_strT = m_oTarget.toStdString();
    for (const atlas::SpriteDef& l_oD : m_pDoc->f_Project().m_vSprites)
        if (l_oD.m_strParent == l_strT) l_oOut.push_back({f_Qs(l_oD.m_strName), f_ToQRect(l_oD.m_oRect)});
    return l_oOut;
}

QString SpriteEditCanvas::f_Focus() const
{
    const QString l_oCur = m_pDoc->f_CurrentSprite();
    for (const Child& l_oC : f_Children())
        if (l_oC.m_oName == l_oCur) return l_oCur;
    return m_oTarget;
}

QRect SpriteEditCanvas::f_FocusRect() const
{
    const QString l_oF = f_Focus();
    if (l_oF == m_oTarget) return QRect(QPoint(0, 0), m_oImage.size());
    return f_ToQRect(m_pDoc->f_SpriteDef(l_oF).m_oRect);
}

QPointF SpriteEditCanvas::f_PivotPoint(const QString& l_oName, const QRect& l_oR) const
{
    const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_oName);
    const float* l_pPv = l_oD.m_bHasPivot ? l_oD.m_arrPivot : m_pDoc->f_Project().m_oSettings.m_arrDefaultPivot;
    return QPointF(l_oR.x() + l_pPv[0] * l_oR.width(), l_oR.y() + l_pPv[1] * l_oR.height());
}

int SpriteEditCanvas::f_ChildAt(const QVector<Child>& l_oKids, const QPointF& l_oC) const
{
    int l_iBest = -1;
    for (int l_iI = 0; l_iI < l_oKids.size(); l_iI++) {
        if (!QRectF(l_oKids[l_iI].m_oRect).contains(l_oC)) continue;
        if (l_iBest < 0 || l_oKids[l_iI].m_oRect.width() * l_oKids[l_iI].m_oRect.height() < l_oKids[l_iBest].m_oRect.width() * l_oKids[l_iBest].m_oRect.height()) l_iBest = l_iI;
    }
    return l_iBest;
}

QPointF SpriteEditCanvas::f_HandlePos(const QRect& l_oR, int l_iHandle) const
{
    const QRectF l_oW = f_ToWidget(QRectF(l_oR));
    switch (l_iHandle) {
    case 0: return l_oW.topLeft();
    case 1: return QPointF(l_oW.center().x(), l_oW.top());
    case 2: return l_oW.topRight();
    case 3: return QPointF(l_oW.right(), l_oW.center().y());
    case 4: return l_oW.bottomRight();
    case 5: return QPointF(l_oW.center().x(), l_oW.bottom());
    case 6: return l_oW.bottomLeft();
    default: return QPointF(l_oW.left(), l_oW.center().y());
    }
}

int SpriteEditCanvas::f_HandleAt(const QRect& l_oR, const QPointF& l_oPos) const
{
    for (int l_iH = 0; l_iH < 8; l_iH++)
        if ((f_HandlePos(l_oR, l_iH) - l_oPos).manhattanLength() <= g_dGrabDistance * 1.5) return l_iH;
    return -1;
}

int SpriteEditCanvas::f_SplitAt(const QPointF& l_oPos) const
{
    const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(f_Focus());
    if (!l_oD.m_bHasSplit) return -1;
    const QRectF l_oFr = f_ToWidget(QRectF(f_FocusRect()));
    if (!l_oFr.adjusted(-g_dGrabDistance, -g_dGrabDistance, g_dGrabDistance, g_dGrabDistance).contains(l_oPos)) return -1;
    const double l_dZ = f_Zoom();
    const double l_arrXs[2] = {l_oFr.left() + l_oD.m_arrSplit[0] * l_dZ, l_oFr.right() - l_oD.m_arrSplit[1] * l_dZ};
    const double l_arrYs[2] = {l_oFr.top() + l_oD.m_arrSplit[2] * l_dZ, l_oFr.bottom() - l_oD.m_arrSplit[3] * l_dZ};
    for (int l_iI = 0; l_iI < 2; l_iI++)
        if (std::abs(l_oPos.x() - l_arrXs[l_iI]) <= g_dGrabDistance) return l_iI;
    for (int l_iI = 0; l_iI < 2; l_iI++)
        if (std::abs(l_oPos.y() - l_arrYs[l_iI]) <= g_dGrabDistance) return 2 + l_iI;
    return -1;
}

QRect SpriteEditCanvas::f_ClampToImage(QRect l_oR) const
{
    l_oR = l_oR.normalized();
    l_oR = l_oR.intersected(QRect(QPoint(0, 0), m_oImage.size()));
    return l_oR;
}

// ---- painting -----------------------------------------------------------------------------------

#if TOMS_ATLAS_GAME_RENDERER
SpriteEditCanvas::~SpriteEditCanvas() { f_ReleaseGameRenderer(); }

void SpriteEditCanvas::f_PaintGameScene(const GameScene& l_oS)
{
    if (m_oImage.isNull()) return;
    l_oS.f_Image(m_oTex.f_Cached(QStringLiteral("sprite"), m_oImage), f_ToWidget(QRectF(QPointF(0, 0), QSizeF(m_oImage.size()))));
}
#else
SpriteEditCanvas::~SpriteEditCanvas() = default;
static bool overGameScene() { return false; }
#endif

void SpriteEditCanvas::f_PaintContent(QPainter& l_oP)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    if (m_oImage.isNull()) {
        l_oP.setPen(palette().color(QPalette::PlaceholderText));
        l_oP.drawText(rect(), Qt::AlignCenter, m_oTarget.isEmpty() ? tr("Select a sprite to edit") : tr("Building…"));
        return;
    }
    const QRectF l_oImgRect = f_ToWidget(QRectF(QPointF(0, 0), QSizeF(m_oImage.size())));
    l_oP.setRenderHint(QPainter::SmoothPixmapTransform, f_Zoom() < 1.0);
    if (!f_OverGameScene()) l_oP.drawImage(l_oImgRect, m_oImage);   // else the game renderer drew it

    // Pixel grid once pixels are big enough to aim at.
    if (f_Zoom() >= 8) {
        QColor l_oGrid = l_oTc.m_oLabel;
        l_oGrid.setAlpha(28);
        l_oP.setPen(QPen(l_oGrid, 1));
        const QRectF l_oVis = l_oImgRect.intersected(QRectF(rect()));
        const QPointF l_oC0 = f_ToContent(l_oVis.topLeft()), l_oC1 = f_ToContent(l_oVis.bottomRight());
        for (int l_iX = int(std::ceil(l_oC0.x())); l_iX <= int(l_oC1.x()); l_iX++) l_oP.drawLine(QLineF(f_ToWidget(QPointF(l_iX, l_oC0.y())), f_ToWidget(QPointF(l_iX, l_oC1.y()))));
        for (int l_iY = int(std::ceil(l_oC0.y())); l_iY <= int(l_oC1.y()); l_iY++) l_oP.drawLine(QLineF(f_ToWidget(QPointF(l_oC0.x(), l_iY)), f_ToWidget(QPointF(l_oC1.x(), l_iY))));
    }

    const QStringList& l_oSelection = m_pDoc->f_Selection();
    const QString l_oCur = f_Focus();
    const QVector<Child> l_oKids = f_Children();
    const QFontMetrics l_oFm(font());
    for (const Child& l_oC : l_oKids) {
        QRect l_oR = l_oC.m_oRect;
        if ((m_eDrag == Drag::Move || m_eDrag == Drag::Resize) && l_oC.m_oName == m_oDragName) l_oR = m_oLiveRect;
        const QRectF l_oW = f_ToWidget(QRectF(l_oR));
        const bool l_bSel = l_oSelection.contains(l_oC.m_oName);
        l_oP.setBrush(Qt::NoBrush);
        if (l_bSel) {
            QColor l_oFill = l_oTc.m_oSelection;
            l_oFill.setAlpha(35);
            l_oP.fillRect(l_oW, l_oFill);
            l_oP.setPen(QPen(l_oTc.m_oSelection, 2));
        } else {
            l_oP.setPen(QPen(l_oTc.m_oChildOutline, 1, Qt::DashLine));
        }
        l_oP.drawRect(l_oW);
        // Name inside the rect when it fits.
        const QString l_oLabel = l_oC.m_oName.section(QLatin1Char('/'), -1);
        if (l_oW.width() > l_oFm.horizontalAdvance(l_oLabel) + 8 && l_oW.height() > l_oFm.height() + 4) {
            l_oP.setPen(l_bSel ? l_oTc.m_oSelection : l_oTc.m_oChildOutline);
            l_oP.drawText(l_oW.adjusted(4, 2, -2, -2), Qt::AlignLeft | Qt::AlignTop, l_oLabel);
        }
    }

    // 9-slice guides of the focus sprite.
    const atlas::SpriteDef l_oFd = m_pDoc->f_SpriteDef(l_oCur);
    const QRect l_oFr = (m_eDrag == Drag::Move || m_eDrag == Drag::Resize) && m_oDragName == l_oCur ? m_oLiveRect : f_FocusRect();
    if (l_oFd.m_bHasSplit) {
        int l_arrS[4];
        for (int l_iI = 0; l_iI < 4; l_iI++) l_arrS[l_iI] = m_eDrag == Drag::Split ? m_arrLiveSplit[l_iI] : l_oFd.m_arrSplit[l_iI];
        const QRectF l_oW = f_ToWidget(QRectF(l_oFr));
        const double l_dZ = f_Zoom();
        l_oP.setPen(QPen(l_oTc.m_oSlice, 1, Qt::DashLine));
        const double l_dXl = l_oW.left() + l_arrS[0] * l_dZ, l_dXr = l_oW.right() - l_arrS[1] * l_dZ;
        const double l_dYt = l_oW.top() + l_arrS[2] * l_dZ, l_dYb = l_oW.bottom() - l_arrS[3] * l_dZ;
        l_oP.drawLine(QLineF(l_dXl, l_oW.top(), l_dXl, l_oW.bottom()));
        l_oP.drawLine(QLineF(l_dXr, l_oW.top(), l_dXr, l_oW.bottom()));
        l_oP.drawLine(QLineF(l_oW.left(), l_dYt, l_oW.right(), l_dYt));
        l_oP.drawLine(QLineF(l_oW.left(), l_dYb, l_oW.right(), l_dYb));
        if (m_eDrag == Drag::Split)
            f_DrawLabel(l_oP, l_oW.bottomLeft() + QPointF(0, 6), tr("9-slice  L %1  R %2  T %3  B %4").arg(l_arrS[0]).arg(l_arrS[1]).arg(l_arrS[2]).arg(l_arrS[3]));
    }

    // Handles of the current child.
    if (l_oCur != m_oTarget) {
        l_oP.setPen(QPen(l_oTc.m_oLabelBackground, 1));
        l_oP.setBrush(l_oTc.m_oSelection);
        l_oP.setRenderHint(QPainter::Antialiasing);
        for (int l_iH = 0; l_iH < 8; l_iH++) {
            const QPointF l_oC = f_HandlePos(l_oFr, l_iH);
            l_oP.drawRect(QRectF(l_oC.x() - g_dHandleRadius, l_oC.y() - g_dHandleRadius, g_dHandleRadius * 2, g_dHandleRadius * 2));
        }
        l_oP.setRenderHint(QPainter::Antialiasing, false);
    }

    // Pivot of the focus sprite.
    {
        QPointF l_oPv = f_PivotPoint(l_oCur, l_oFr);
        if (m_eDrag == Drag::Pivot) l_oPv = QPointF(l_oFr.x() + m_oLivePivot.x() * l_oFr.width(), l_oFr.y() + m_oLivePivot.y() * l_oFr.height());
        const QPointF l_oW = f_ToWidget(l_oPv);
        l_oP.setRenderHint(QPainter::Antialiasing);
        l_oP.setPen(QPen(QColor(0, 0, 0, 160), 3));
        l_oP.setBrush(Qt::NoBrush);
        l_oP.drawEllipse(l_oW, 6, 6);
        l_oP.setPen(QPen(l_oTc.m_oPivot, 1.5));
        l_oP.drawEllipse(l_oW, 6, 6);
        l_oP.drawLine(QLineF(l_oW.x() - 10, l_oW.y(), l_oW.x() + 10, l_oW.y()));
        l_oP.drawLine(QLineF(l_oW.x(), l_oW.y() - 10, l_oW.x(), l_oW.y() + 10));
        l_oP.setRenderHint(QPainter::Antialiasing, false);
        if (m_eDrag == Drag::Pivot)
            f_DrawLabel(l_oP, l_oW + QPointF(12, 8), tr("pivot %1, %2").arg(m_oLivePivot.x(), 0, 'f', 3).arg(m_oLivePivot.y(), 0, 'f', 3));
    }

    // The rect being drawn or changed, with its numbers.
    if (m_eDrag == Drag::Create || m_eDrag == Drag::Move || m_eDrag == Drag::Resize) {
        const QRectF l_oW = f_ToWidget(QRectF(m_oLiveRect));
        if (m_eDrag == Drag::Create) {
            l_oP.setPen(QPen(l_oTc.m_oSelection, 1, Qt::DashLine));
            l_oP.setBrush(Qt::NoBrush);
            l_oP.drawRect(l_oW);
        }
        f_DrawLabel(l_oP, l_oW.bottomLeft() + QPointF(0, 6),
                  QStringLiteral("%1, %2   %3 × %4").arg(m_oLiveRect.x()).arg(m_oLiveRect.y()).arg(m_oLiveRect.width()).arg(m_oLiveRect.height()));
    }

    // What is being edited, top-left.
    const QRect l_oCr = f_FocusRect();
    QString l_oInfo = QStringLiteral("%1   %2 × %3").arg(m_oTarget).arg(m_oImage.width()).arg(m_oImage.height());
    if (l_oCur != m_oTarget) l_oInfo += QStringLiteral("   ▸ %1  %2, %3  %4 × %5").arg(l_oCur).arg(l_oCr.x()).arg(l_oCr.y()).arg(l_oCr.width()).arg(l_oCr.height());
    l_oInfo += QStringLiteral("   %1%").arg(qRound(f_Zoom() * 100));
    f_DrawLabel(l_oP, QPointF(8, 8), l_oInfo);
}

// ---- mouse --------------------------------------------------------------------------------------

void SpriteEditCanvas::f_UpdateCursor(const QPointF& l_oPos)
{
    if (f_IsPanning()) return;
    const QString l_oCur = f_Focus();
    const QRect l_oFr = f_FocusRect();
    if ((f_ToWidget(f_PivotPoint(l_oCur, l_oFr)) - l_oPos).manhattanLength() <= g_dGrabDistance * 1.5) { setCursor(Qt::SizeAllCursor); return; }
    if (const int l_iS = f_SplitAt(l_oPos); l_iS >= 0) { setCursor(l_iS < 2 ? Qt::SplitHCursor : Qt::SplitVCursor); return; }
    if (l_oCur != m_oTarget) {
        static const Qt::CursorShape l_arrShapes[8] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                                                  Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
        if (const int l_iH = f_HandleAt(l_oFr, l_oPos); l_iH >= 0) { setCursor(l_arrShapes[l_iH]); return; }
    }
    if (f_ChildAt(f_Children(), f_ToContent(l_oPos)) >= 0) setCursor(Qt::PointingHandCursor);
    else setCursor(Qt::CrossCursor);
}

void SpriteEditCanvas::mousePressEvent(QMouseEvent* l_pE)
{
    if (f_PanPress(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton || m_oImage.isNull()) return;
    const QPointF l_oPos = l_pE->position();
    m_oPressContent = f_ToContent(l_oPos);
    const QString l_oCur = f_Focus();
    const QRect l_oFr = f_FocusRect();

    if ((f_ToWidget(f_PivotPoint(l_oCur, l_oFr)) - l_oPos).manhattanLength() <= g_dGrabDistance * 1.5) {
        m_eDrag = Drag::Pivot;
        m_oDragName = l_oCur;
        const QPointF l_oPv = f_PivotPoint(l_oCur, l_oFr);
        m_oLivePivot = QPointF((l_oPv.x() - l_oFr.x()) / std::max(1, l_oFr.width()), (l_oPv.y() - l_oFr.y()) / std::max(1, l_oFr.height()));
        return;
    }
    if (const int l_iS = f_SplitAt(l_oPos); l_iS >= 0) {
        m_eDrag = Drag::Split;
        m_oDragName = l_oCur;
        m_iSplitSide = l_iS;
        const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(l_oCur);
        std::copy(l_oD.m_arrSplit, l_oD.m_arrSplit + 4, m_arrLiveSplit);
        return;
    }
    if (l_oCur != m_oTarget) {
        if (const int l_iH = f_HandleAt(l_oFr, l_oPos); l_iH >= 0) {
            m_eDrag = Drag::Resize;
            m_oDragName = l_oCur;
            m_iHandle = l_iH;
            m_oStartRect = m_oLiveRect = l_oFr;
            return;
        }
    }
    const QVector<Child> l_oKids = f_Children();
    const int l_iHit = f_ChildAt(l_oKids, m_oPressContent);
    if (l_iHit >= 0) {
        const QString l_oName = l_oKids[l_iHit].m_oName;
        QStringList l_oSel = m_pDoc->f_Selection();
        if (l_pE->modifiers() & Qt::ControlModifier) {
            if (l_oSel.contains(l_oName)) l_oSel.removeAll(l_oName);
            else l_oSel << l_oName;
            m_pDoc->f_SetSelection(l_oSel);
            return;
        }
        m_pDoc->f_SetSelection({l_oName});
        m_eDrag = Drag::Move;
        m_oDragName = l_oName;
        m_oStartRect = m_oLiveRect = l_oKids[l_iHit].m_oRect;
        return;
    }
    // Empty pixels: start a new child rect (and leave the target as the current sprite).
    m_pDoc->f_SetSelection({m_oTarget});
    m_eDrag = Drag::Create;
    const QPoint l_oP0(int(std::lround(m_oPressContent.x())), int(std::lround(m_oPressContent.y())));
    m_oStartRect = QRect(l_oP0, QSize(0, 0));
    m_oLiveRect = QRect();
}

void SpriteEditCanvas::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (f_PanMove(l_pE)) return;
    const QPointF l_oC = f_ToContent(l_pE->position());
    const QPoint l_oG(int(std::lround(l_oC.x())), int(std::lround(l_oC.y())));   // nearest pixel edge
    const QSize l_oImg = m_oImage.size();
    switch (m_eDrag) {
    case Drag::None:
        f_UpdateCursor(l_pE->position());
        return;
    case Drag::Create: {
        const QPoint l_oA = m_oStartRect.topLeft();
        m_oLiveRect = f_ClampToImage(QRect(QPoint(std::min(l_oA.x(), l_oG.x()), std::min(l_oA.y(), l_oG.y())),
                                        QPoint(std::max(l_oA.x(), l_oG.x()) - 1, std::max(l_oA.y(), l_oG.y()) - 1)));
        break;
    }
    case Drag::Move: {
        const int l_iDx = std::clamp(int(std::lround(l_oC.x() - m_oPressContent.x())), -m_oStartRect.x(), l_oImg.width() - m_oStartRect.right() - 1);
        const int l_iDy = std::clamp(int(std::lround(l_oC.y() - m_oPressContent.y())), -m_oStartRect.y(), l_oImg.height() - m_oStartRect.bottom() - 1);
        m_oLiveRect = m_oStartRect.translated(l_iDx, l_iDy);
        break;
    }
    case Drag::Resize: {
        // Edges as exclusive coordinates, so a rect can never collapse below one pixel.
        int l_iL = m_oStartRect.x(), l_iT = m_oStartRect.y(), l_iR = m_oStartRect.x() + m_oStartRect.width(), l_iB = m_oStartRect.y() + m_oStartRect.height();
        const int l_iGx = std::clamp(l_oG.x(), 0, l_oImg.width()), l_iGy = std::clamp(l_oG.y(), 0, l_oImg.height());
        if (m_iHandle == 0 || m_iHandle == 6 || m_iHandle == 7) l_iL = std::min(l_iGx, l_iR - 1);
        if (m_iHandle == 2 || m_iHandle == 3 || m_iHandle == 4) l_iR = std::max(l_iGx, l_iL + 1);
        if (m_iHandle == 0 || m_iHandle == 1 || m_iHandle == 2) l_iT = std::min(l_iGy, l_iB - 1);
        if (m_iHandle == 4 || m_iHandle == 5 || m_iHandle == 6) l_iB = std::max(l_iGy, l_iT + 1);
        m_oLiveRect = QRect(l_iL, l_iT, l_iR - l_iL, l_iB - l_iT);
        break;
    }
    case Drag::Pivot: {
        const QRect l_oFr = f_FocusRect();
        // Half-pixel steps: the centre of an odd-sized sprite stays reachable.
        const double l_dPx = std::round(l_oC.x() * 2) / 2, l_dPy = std::round(l_oC.y() * 2) / 2;
        m_oLivePivot = QPointF((l_dPx - l_oFr.x()) / std::max(1, l_oFr.width()), (l_dPy - l_oFr.y()) / std::max(1, l_oFr.height()));
        break;
    }
    case Drag::Split: {
        const QRect l_oFr = f_FocusRect();
        int* l_pS = m_arrLiveSplit;
        switch (m_iSplitSide) {
        case 0: l_pS[0] = std::clamp(l_oG.x() - l_oFr.x(), 0, l_oFr.width() - l_pS[1]); break;
        case 1: l_pS[1] = std::clamp(l_oFr.x() + l_oFr.width() - l_oG.x(), 0, l_oFr.width() - l_pS[0]); break;
        case 2: l_pS[2] = std::clamp(l_oG.y() - l_oFr.y(), 0, l_oFr.height() - l_pS[3]); break;
        default: l_pS[3] = std::clamp(l_oFr.y() + l_oFr.height() - l_oG.y(), 0, l_oFr.height() - l_pS[2]); break;
        }
        break;
    }
    }
    update();
}

void SpriteEditCanvas::mouseReleaseEvent(QMouseEvent* l_pE)
{
    if (f_PanRelease(l_pE)) {
        f_UpdateCursor(l_pE->position());
        return;
    }
    if (l_pE->button() != Qt::LeftButton) return;
    f_CommitDrag();
    m_eDrag = Drag::None;
    update();
}

void SpriteEditCanvas::f_CommitDrag()
{
    switch (m_eDrag) {
    case Drag::None:
        break;
    case Drag::Create: {
        if (m_oLiveRect.width() < 1 || m_oLiveRect.height() < 1) break;
        const QRect l_oR = m_oLiveRect;
        m_eDrag = Drag::None;   // the dialog below runs an event loop: no live drag meanwhile
        QString l_oBase = m_oTarget + QStringLiteral("_%1").arg(f_Children().size());
        bool l_bOk = false;
        const QString l_oName = QInputDialog::getText(this, tr("New Child Sprite"),
                                                   tr("Name of the child sprite (%1, %2  %3 × %4):").arg(l_oR.x()).arg(l_oR.y()).arg(l_oR.width()).arg(l_oR.height()),
                                                   QLineEdit::Normal, m_pDoc->f_UniqueName(l_oBase), &l_bOk).trimmed();
        if (!l_bOk || l_oName.isEmpty()) break;
        if (m_pDoc->f_SpriteExists(l_oName)) {
            QMessageBox::warning(this, tr("New Child Sprite"), tr("There already is a sprite called '%1'.").arg(l_oName));
            break;
        }
        m_pDoc->f_AddChildren(m_oTarget, {{l_oName, f_ToIRect(l_oR)}}, tr("New child %1").arg(l_oName));
        break;
    }
    case Drag::Move:
    case Drag::Resize:
        if (m_oLiveRect != m_oStartRect) {
            const atlas::IRect l_oR = f_ToIRect(m_oLiveRect);
            m_pDoc->f_EditSprites({m_oDragName}, m_eDrag == Drag::Move ? tr("Move %1").arg(m_oDragName) : tr("Resize %1").arg(m_oDragName),
                               [&](atlas::SpriteDef& l_oD) { l_oD.m_oRect = l_oR; });
        }
        break;
    case Drag::Pivot: {
        const QPointF l_oPv = m_oLivePivot;
        m_pDoc->f_EditSprites({m_oDragName}, tr("Move pivot of %1").arg(m_oDragName), [&](atlas::SpriteDef& l_oD) {
            l_oD.m_bHasPivot = true;
            l_oD.m_arrPivot[0] = float(l_oPv.x());
            l_oD.m_arrPivot[1] = float(l_oPv.y());
        });
        break;
    }
    case Drag::Split: {
        const int* l_pS = m_arrLiveSplit;
        m_pDoc->f_EditSprites({m_oDragName}, tr("9-slice of %1").arg(m_oDragName),
                           [&](atlas::SpriteDef& l_oD) { std::copy(l_pS, l_pS + 4, l_oD.m_arrSplit); });
        break;
    }
    }
}

void SpriteEditCanvas::mouseDoubleClickEvent(QMouseEvent* l_pE)
{
    if (l_pE->button() != Qt::LeftButton) return;
    const QVector<Child> l_oKids = f_Children();
    const int l_iHit = f_ChildAt(l_oKids, f_ToContent(l_pE->position()));
    if (l_iHit >= 0) f_SetTarget(l_oKids[l_iHit].m_oName);   // edit inside the child
}

void SpriteEditCanvas::f_Nudge(int l_iDx, int l_iDy)
{
    const QString l_oCur = f_Focus();
    if (l_oCur == m_oTarget) return;
    const QRect l_oR = f_FocusRect();
    const QRect l_oMoved = l_oR.translated(std::clamp(l_iDx, -l_oR.x(), m_oImage.width() - l_oR.right() - 1),
                                     std::clamp(l_iDy, -l_oR.y(), m_oImage.height() - l_oR.bottom() - 1));
    if (l_oMoved == l_oR) return;
    m_pDoc->f_EditSprites({l_oCur}, tr("Move %1").arg(l_oCur), [&](atlas::SpriteDef& l_oD) { l_oD.m_oRect = f_ToIRect(l_oMoved); },
                       QStringLiteral("nudge"));
}

void SpriteEditCanvas::keyPressEvent(QKeyEvent* l_pE)
{
    const int l_iStep = (l_pE->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (l_pE->key()) {
    case Qt::Key_Escape:
        if (m_eDrag != Drag::None) {
            m_eDrag = Drag::None;
            update();
        } else if (const atlas::SpriteDef l_oD = m_pDoc->f_SpriteDef(m_oTarget); l_oD.f_IsChild()) {
            const QString l_oChild = m_oTarget;
            f_SetTarget(f_Qs(l_oD.m_strParent));
            m_pDoc->f_SetSelection({l_oChild});
        } else {
            emit f_ExitRequested();
        }
        return;
    case Qt::Key_Left: f_Nudge(-l_iStep, 0); return;
    case Qt::Key_Right: f_Nudge(l_iStep, 0); return;
    case Qt::Key_Up: f_Nudge(0, -l_iStep); return;
    case Qt::Key_Down: f_Nudge(0, l_iStep); return;
    default:
        CanvasView::keyPressEvent(l_pE);
    }
}

void SpriteEditCanvas::contextMenuEvent(QContextMenuEvent* l_pE)
{
    const QVector<Child> l_oKids = f_Children();
    const int l_iHit = f_ChildAt(l_oKids, f_ToContent(l_pE->pos()));
    if (l_iHit >= 0 && !m_pDoc->f_Selection().contains(l_oKids[l_iHit].m_oName)) m_pDoc->f_SetSelection({l_oKids[l_iHit].m_oName});
    else if (l_iHit < 0) m_pDoc->f_SetSelection({m_oTarget});
    emit f_ContextMenuRequested(l_pE->globalPos());
}

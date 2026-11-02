#include "AtlasCanvas.h"

#include "AtlasDocument.h"
#include "Icons.h"
#include "Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {

QRectF f_FrameRect(const atlas::Region& l_oR) { return QRectF(l_oR.m_oFrame.m_iX, l_oR.m_oFrame.m_iY, l_oR.m_oFrame.m_iW, l_oR.m_oFrame.m_iH); }

QString f_Qs(const std::string& l_strS) { return QString::fromStdString(l_strS); }

const std::vector<atlas::Region> g_vNoRegions;   // iterated when there is no build yet

}  // namespace

AtlasCanvas::AtlasCanvas(AtlasDocument* l_pDoc, QWidget* l_pParent)
#if TOMS_ATLAS_GAME_RENDERER
    : GameCanvasView(GameCanvasView::f_GameRendererWanted(QStringLiteral("AtlasEditor")), l_pParent)
#else
    : CanvasView(parent)
#endif
    , m_pDoc(l_pDoc)
{
    connect(l_pDoc, &AtlasDocument::f_BuildFinished, this, &AtlasCanvas::f_OnBuildFinished);
    connect(l_pDoc, &AtlasDocument::f_SelectionChanged, this, &AtlasCanvas::f_OnSelectionChanged);
    connect(l_pDoc, &AtlasDocument::f_ProjectChanged, this, &AtlasCanvas::f_RefreshPinned);
    // A new project (or variant) shows its first page, fitted.
    connect(l_pDoc, &AtlasDocument::f_ProjectReset, this, [this] { m_iPage = 0; f_RequestFit(); });
}

void AtlasCanvas::f_SetPage(int l_iPage)
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const int l_iCount = l_spSnap ? int(l_spSnap->m_oPages.size()) : 0;
    l_iPage = std::clamp(l_iPage, 0, std::max(0, l_iCount - 1));
    if (l_iPage == m_iPage) return;
    m_iPage = l_iPage;
    m_oHover.clear();
    update();
    emit f_PageChanged(m_iPage);
}

void AtlasCanvas::f_SetShowOutlines(bool l_bShow)
{
    m_bShowOutlines = l_bShow;
    update();
}

void AtlasCanvas::f_SetShowChildren(bool l_bShow)
{
    m_bShowChildren = l_bShow;
    update();
}

QSize AtlasCanvas::f_ContentSize() const
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    if (!l_spSnap || m_iPage >= l_spSnap->m_oPages.size()) return QSize();
    return l_spSnap->m_oPages[m_iPage].size();
}

void AtlasCanvas::f_OnBuildFinished()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const int l_iCount = int(l_spSnap->m_oPages.size());
    if (m_iPage >= l_iCount) f_SetPage(l_iCount - 1);
    // An in-progress drag refers to frames of the old layout; drop it rather than jump.
    if (m_eDrag == Drag::Move) {
        m_eDrag = Drag::None;
        m_oDragNames.clear();
    }
    update();
}

void AtlasCanvas::f_OnSelectionChanged()
{
    // Follow a selection made elsewhere (tree, problems) to its page and into view.
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const atlas::Region* l_pR = l_spSnap ? l_spSnap->f_Region(m_pDoc->f_CurrentSprite()) : nullptr;
    if (l_pR && isVisibleTo(window())) {
        f_SetPage(l_pR->m_iPage);
        f_CenterOn(f_FrameRect(*l_pR));
    }
    update();
}

void AtlasCanvas::f_RefreshPinned()
{
    m_oPinned.clear();
    for (const atlas::SpriteDef& l_oD : m_pDoc->f_Project().m_vSprites)
        if (l_oD.m_bPinned) m_oPinned.insert(f_Qs(l_oD.m_strName));
    update();
}

bool AtlasCanvas::f_IsVisible(const atlas::Region& l_oR) const
{
    return l_oR.m_iPage == m_iPage && l_oR.m_strAliasOf.empty() && (m_bShowChildren || !l_oR.m_bChild || l_oR.m_bBaked);
}

bool AtlasCanvas::f_IsMovable(const atlas::Region& l_oR) const { return l_oR.m_strAliasOf.empty() && (!l_oR.m_bChild || l_oR.m_bBaked); }

QString AtlasCanvas::f_HitTest(const QPointF& l_oContent) const
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    if (!l_spSnap) return QString();
    const atlas::Region* l_pBest = nullptr;
    for (const atlas::Region& l_oR : l_spSnap->m_oResult.m_vRegions) {
        if (!f_IsVisible(l_oR) || !f_FrameRect(l_oR).contains(l_oContent)) continue;
        if (!l_pBest || l_oR.m_oFrame.m_iW * l_oR.m_oFrame.m_iH < l_pBest->m_oFrame.m_iW * l_pBest->m_oFrame.m_iH) l_pBest = &l_oR;
    }
    return l_pBest ? f_Qs(l_pBest->m_strName) : QString();
}

QString AtlasCanvas::f_TooltipFor(const QString& l_oName) const
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const atlas::Region* l_pR = l_spSnap ? l_spSnap->f_Region(l_oName) : nullptr;
    if (!l_pR) return QString();
    QString l_oT = QStringLiteral("<b>%1</b><br>").arg(l_oName.toHtmlEscaped());
    l_oT += tr("frame %1, %2  %3×%4").arg(l_pR->m_oFrame.m_iX).arg(l_pR->m_oFrame.m_iY).arg(l_pR->m_oFrame.m_iW).arg(l_pR->m_oFrame.m_iH);
    l_oT += QStringLiteral("<br>") + tr("original %1×%2").arg(l_pR->m_iOrigW).arg(l_pR->m_iOrigH);
    if (l_pR->f_Trimmed()) l_oT += QStringLiteral("<br>") + tr("offset %1, %2").arg(l_pR->m_iOffsetX).arg(l_pR->m_iOffsetY);
    if (l_pR->m_bChild)
        l_oT += QStringLiteral("<br>") + tr("child of %1 at %2, %3%4").arg(f_Qs(l_pR->m_strParent).toHtmlEscaped()).arg(l_pR->m_oLocal.m_iX)
                                         .arg(l_pR->m_oLocal.m_iY).arg(l_pR->m_bBaked ? tr(" (baked)") : QString());
    QStringList l_oAliases;
    for (const atlas::Region& l_oA : l_spSnap->m_oResult.m_vRegions)
        if (f_Qs(l_oA.m_strAliasOf) == l_oName) l_oAliases << f_Qs(l_oA.m_strName).toHtmlEscaped();
    if (!l_oAliases.isEmpty()) l_oT += QStringLiteral("<br>") + tr("same pixels as: %1").arg(l_oAliases.join(QStringLiteral(", ")));
    if (m_oPinned.contains(l_oName)) l_oT += QStringLiteral("<br>") + tr("pinned");
    if (const int l_iEx = m_pDoc->f_Project().m_oSettings.m_iExtrude; l_iEx > 0 && (!l_pR->m_bChild || l_pR->m_bBaked))
        l_oT += QStringLiteral("<br>") + tr("extrude %1 px around the frame (dimmed: copies of the edge pixels)").arg(l_iEx);
    return l_oT;
}

#if TOMS_ATLAS_GAME_RENDERER
AtlasCanvas::~AtlasCanvas() { f_ReleaseGameRenderer(); }

void AtlasCanvas::f_PaintGameScene(const GameScene& l_oS)
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    if (!l_spSnap || l_spSnap->m_oPages.isEmpty()) return;
    const QImage& l_oPage = l_spSnap->m_oPages[std::min<int>(m_iPage, int(l_spSnap->m_oPages.size()) - 1)];
    l_oS.f_Image(m_oTex.f_Cached(QStringLiteral("page"), l_oPage), f_ToWidget(QRectF(QPointF(0, 0), QSizeF(l_oPage.size()))));
}
#else
AtlasCanvas::~AtlasCanvas() = default;
static bool overGameScene() { return false; }
#endif

void AtlasCanvas::f_PaintContent(QPainter& l_oP)
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const Theme::Colors& l_oTc = Theme::f_Colors();
    if (!l_spSnap || l_spSnap->m_oPages.isEmpty()) {
        l_oP.setPen(palette().color(QPalette::PlaceholderText));
        l_oP.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   m_pDoc->f_IsBuilding() ? tr("Building…")
                                       : tr("No sprites yet.\nDrop PNG files or folders on the Sprites panel,\n"
                                            "or use File › Add Source Folder…"));
        return;
    }
    const QImage& l_oPage = l_spSnap->m_oPages[std::min<int>(m_iPage, int(l_spSnap->m_oPages.size()) - 1)];
    const QRectF l_oPageRect = f_ToWidget(QRectF(QPointF(0, 0), QSizeF(l_oPage.size())));
    l_oP.setRenderHint(QPainter::SmoothPixmapTransform, f_Zoom() < 1.0);
    if (!f_OverGameScene()) l_oP.drawImage(l_oPageRect, l_oPage);   // else the game renderer drew it

    // Aliases share their owner's frame: count them for a badge on the owner.
    QHash<QString, int> l_oAliasCount;
    for (const atlas::Region& l_oR : l_spSnap->m_oResult.m_vRegions)
        if (!l_oR.m_strAliasOf.empty() && l_oR.m_iPage == m_iPage) l_oAliasCount[f_Qs(l_oR.m_strAliasOf)]++;

    const QSet<QString> l_oSelected(m_pDoc->f_Selection().begin(), m_pDoc->f_Selection().end());
    const QPointF l_oDelta(m_oMoveDelta);
    l_oP.setRenderHint(QPainter::Antialiasing, false);

    // Extrude: the build copies each sprite's edge pixels this far outward on the page. They are not part
    // of the sprite (its frame is the outline), so they are dimmed and hatched: otherwise the sprite seems
    // to stick out of its frame by that much.
    if (const int l_iEx = m_pDoc->f_Project().m_oSettings.m_iExtrude; l_iEx > 0) {
        QColor l_oDim = l_oTc.m_oCanvasBackground;
        l_oDim.setAlpha(170);
        QColor l_oHatch = l_oTc.m_oLabel;
        l_oHatch.setAlpha(70);
        for (const atlas::Region& l_oR : l_spSnap->m_oResult.m_vRegions) {
            if (l_oR.m_iPage != m_iPage || !l_oR.m_strAliasOf.empty() || (l_oR.m_bChild && !l_oR.m_bBaked)) continue;   // own pixels only
            const QRectF l_oF = f_FrameRect(l_oR);
            const QRectF l_oG = l_oF.adjusted(-l_iEx, -l_iEx, l_iEx, l_iEx);
            const QRectF l_arrRing[4] = {
                {l_oG.left(), l_oG.top(), l_oG.width(), double(l_iEx)},                          // above
                {l_oG.left(), l_oF.bottom(), l_oG.width(), double(l_iEx)},                       // below
                {l_oG.left(), l_oF.top(), double(l_iEx), l_oF.height()},                         // left
                {l_oF.right(), l_oF.top(), double(l_iEx), l_oF.height()},                        // right
            };
            for (const QRectF& l_oPart : l_arrRing) {
                const QRectF l_oW = f_ToWidget(l_oPart);
                l_oP.fillRect(l_oW, l_oDim);
                if (f_Zoom() >= 4) l_oP.fillRect(l_oW, QBrush(l_oHatch, Qt::BDiagPattern));
            }
        }
    }

    for (const atlas::Region& l_oR : l_spSnap->m_oResult.m_vRegions) {
        if (!f_IsVisible(l_oR)) continue;
        const QString l_oName = f_Qs(l_oR.m_strName);
        const QRectF l_oW = f_ToWidget(f_FrameRect(l_oR)).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool l_bChild = l_oR.m_bChild && !l_oR.m_bBaked;
        if (l_oName == m_oHover) l_oP.fillRect(l_oW, l_oTc.m_oHover);
        if (m_bShowOutlines) {
            QPen l_oPen(l_oR.m_bChild ? l_oTc.m_oChildOutline : l_oTc.m_oImageOutline, 1, l_bChild ? Qt::DashLine : Qt::SolidLine);
            l_oP.setPen(l_oPen);
            l_oP.setBrush(Qt::NoBrush);
            l_oP.drawRect(l_oW);
        }
        if (l_oAliasCount.contains(l_oName) && m_bShowOutlines) {
            const QString l_oBadge = QStringLiteral("=%1").arg(l_oAliasCount[l_oName]);
            const QFontMetrics l_oFm(font());
            const QRectF l_oB(l_oW.right() - l_oFm.horizontalAdvance(l_oBadge) - 6, l_oW.top(), l_oFm.horizontalAdvance(l_oBadge) + 6, l_oFm.height());
            if (l_oB.width() < l_oW.width() && l_oB.height() < l_oW.height()) {
                l_oP.fillRect(l_oB, l_oTc.m_oAliasOutline);
                l_oP.setPen(Qt::white);
                l_oP.drawText(l_oB, Qt::AlignCenter, l_oBadge);
            }
        }
        if (l_oSelected.contains(l_oName)) {
            QColor l_oFill = l_oTc.m_oSelection;
            l_oFill.setAlpha(40);
            l_oP.fillRect(l_oW, l_oFill);
            l_oP.setPen(QPen(l_oTc.m_oSelection, 2));
            l_oP.drawRect(l_oW.adjusted(-1, -1, 1, 1));
        }
        if (m_oPinned.contains(l_oName)) {
            const int l_iS = std::clamp(int(std::min(l_oW.width(), l_oW.height()) * 0.6), 10, 18);
            Icons::f_Icon(Icons::Id::Pin).paint(&l_oP, QRect(int(l_oW.left()) - l_iS / 3, int(l_oW.top()) - l_iS / 3, l_iS, l_iS));
        }
    }

    // Sprites being dragged: their pixels at the new place, the old place dashed.
    if (m_eDrag == Drag::Move) {
        for (const QString& l_oN : m_oDragNames) {
            const atlas::Region* l_pR = l_spSnap->f_Region(l_oN);
            if (!l_pR) continue;
            const QRectF l_oFrom = f_ToWidget(f_FrameRect(*l_pR));
            const QRectF l_oTo = f_ToWidget(f_FrameRect(*l_pR).translated(l_oDelta));
            l_oP.setPen(QPen(l_oTc.m_oSelection, 1, Qt::DashLine));
            l_oP.setBrush(Qt::NoBrush);
            l_oP.drawRect(l_oFrom);
            l_oP.setOpacity(0.85);
            l_oP.drawImage(l_oTo, l_oPage, QRectF(f_FrameRect(*l_pR)));
            l_oP.setOpacity(1.0);
            l_oP.setPen(QPen(l_oTc.m_oSelection, 2));
            l_oP.drawRect(l_oTo);
        }
        if (const atlas::Region* l_pR = l_spSnap->f_Region(m_oDragNames.value(0))) {
            const QRectF l_oTo = f_ToWidget(f_FrameRect(*l_pR).translated(l_oDelta));
            f_DrawLabel(l_oP, l_oTo.bottomLeft() + QPointF(0, 4),
                      tr("pin at %1, %2").arg(l_pR->m_oFrame.m_iX + m_oMoveDelta.x()).arg(l_pR->m_oFrame.m_iY + m_oMoveDelta.y()));
        }
    }
    if (m_eDrag == Drag::Rubber) {
        QColor l_oFill = l_oTc.m_oSelection;
        l_oFill.setAlpha(30);
        l_oP.setPen(QPen(l_oTc.m_oSelection, 1, Qt::DashLine));
        l_oP.setBrush(l_oFill);
        l_oP.drawRect(f_ToWidget(m_oRubber.normalized()));
    }
}

bool AtlasCanvas::event(QEvent* l_pE)
{
    if (l_pE->type() == QEvent::ToolTip) {
        auto* l_pHe = static_cast<QHelpEvent*>(l_pE);
        const QString l_oName = m_eDrag == Drag::None ? f_HitTest(f_ToContent(l_pHe->pos())) : QString();
        if (l_oName.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(l_pHe->globalPos(), f_TooltipFor(l_oName), this);
        return true;
    }
    return CanvasView::event(l_pE);
}

void AtlasCanvas::mousePressEvent(QMouseEvent* l_pE)
{
    if (f_PanPress(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton) return;
    m_oPressWidget = l_pE->position();
    m_oPressContent = f_ToContent(l_pE->position());
    m_oMoveDelta = QPoint();
    m_oSelectionAtPress = m_pDoc->f_Selection();
    const QString l_oHit = f_HitTest(m_oPressContent);
    const bool l_bToggle = l_pE->modifiers() & Qt::ControlModifier, l_bAdd = l_pE->modifiers() & Qt::ShiftModifier;
    if (l_oHit.isEmpty()) {
        m_eDrag = Drag::Rubber;
        m_oRubber = QRectF(m_oPressContent, m_oPressContent);
        if (!l_bToggle && !l_bAdd) m_pDoc->f_SetSelection({});
        return;
    }
    QStringList l_oSel = m_pDoc->f_Selection();
    if (l_bToggle) {
        if (l_oSel.contains(l_oHit)) l_oSel.removeAll(l_oHit);
        else l_oSel << l_oHit;
        m_pDoc->f_SetSelection(l_oSel);
        m_eDrag = Drag::None;
        return;
    }
    if (!l_oSel.contains(l_oHit) && !l_bAdd) l_oSel.clear();
    l_oSel.removeAll(l_oHit);   // (re)appended: the clicked sprite becomes the current one
    l_oSel << l_oHit;
    m_pDoc->f_SetSelection(l_oSel);
    // Everything selected on this page that owns a place in the atlas moves together.
    m_oDragNames.clear();
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    for (const QString& l_oN : l_oSel)
        if (const atlas::Region* l_pR = l_spSnap->f_Region(l_oN); l_pR && l_pR->m_iPage == m_iPage && f_IsMovable(*l_pR)) m_oDragNames << l_oN;
    m_eDrag = m_oDragNames.contains(l_oHit) ? Drag::Pending : Drag::None;
}

void AtlasCanvas::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (f_PanMove(l_pE)) return;
    const QPointF l_oC = f_ToContent(l_pE->position());
    switch (m_eDrag) {
    case Drag::Pending:
        if ((l_pE->position() - m_oPressWidget).manhattanLength() < QApplication::startDragDistance()) break;
        m_eDrag = Drag::Move;
        [[fallthrough]];
    case Drag::Move: {
        // Whole pixels, kept inside the page for the sprite under the cursor.
        QPoint l_oD(int(std::lround(l_oC.x() - m_oPressContent.x())), int(std::lround(l_oC.y() - m_oPressContent.y())));
        const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
        const QSize l_oPage = f_ContentSize();
        for (const QString& l_oN : m_oDragNames) {
            const atlas::Region* l_pR = l_spSnap->f_Region(l_oN);
            l_oD.setX(std::clamp(l_oD.x(), -l_pR->m_oFrame.m_iX, l_oPage.width() - l_pR->m_oFrame.f_Right()));
            l_oD.setY(std::clamp(l_oD.y(), -l_pR->m_oFrame.m_iY, l_oPage.height() - l_pR->m_oFrame.f_Bottom()));
        }
        m_oMoveDelta = l_oD;
        update();
        break;
    }
    case Drag::Rubber: {
        m_oRubber.setBottomRight(l_oC);
        const QRectF l_oArea = m_oRubber.normalized();
        QStringList l_oSel = (l_pE->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) ? m_oSelectionAtPress : QStringList();
        const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
        for (const atlas::Region& l_oR : l_spSnap ? l_spSnap->m_oResult.m_vRegions : g_vNoRegions) {
            if (!f_IsVisible(l_oR)) continue;
            // Children only when wholly inside: dragging across a sheet should pick the sheets.
            const bool l_bTake = l_oR.m_bChild && !l_oR.m_bBaked ? l_oArea.contains(f_FrameRect(l_oR)) : l_oArea.intersects(f_FrameRect(l_oR));
            if (l_bTake && !l_oSel.contains(f_Qs(l_oR.m_strName))) l_oSel << f_Qs(l_oR.m_strName);
        }
        m_pDoc->f_SetSelection(l_oSel);
        update();
        break;
    }
    case Drag::None: {
        const QString l_oHover = f_HitTest(l_oC);
        if (l_oHover != m_oHover) {
            m_oHover = l_oHover;
            update();
        }
        break;
    }
    }
}

void AtlasCanvas::mouseReleaseEvent(QMouseEvent* l_pE)
{
    if (f_PanRelease(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton) return;
    if (m_eDrag == Drag::Move && !m_oMoveDelta.isNull()) f_CommitMove();
    m_eDrag = Drag::None;
    m_oMoveDelta = QPoint();
    update();
}

void AtlasCanvas::f_CommitMove()
{
    const BuildSnapshotPtr l_spSnap = m_pDoc->f_Snapshot();
    const QPoint l_oD = m_oMoveDelta;
    const int l_iPage = m_iPage;
    m_pDoc->f_EditSprites(m_oDragNames, m_oDragNames.size() == 1 ? tr("Move %1").arg(m_oDragNames.first()) : tr("Move sprites"),
                       [&](atlas::SpriteDef& l_oDef) {
                           const atlas::Region* l_pR = l_spSnap->f_Region(QString::fromStdString(l_oDef.m_strName));
                           if (!l_pR) return;
                           l_oDef.m_bPinned = true;
                           l_oDef.m_iPinPage = l_iPage;
                           l_oDef.m_iPinX = l_pR->m_oFrame.m_iX + l_oD.x();
                           l_oDef.m_iPinY = l_pR->m_oFrame.m_iY + l_oD.y();
                       });
}

void AtlasCanvas::mouseDoubleClickEvent(QMouseEvent* l_pE)
{
    if (l_pE->button() != Qt::LeftButton) return;
    const QString l_oHit = f_HitTest(f_ToContent(l_pE->position()));
    if (!l_oHit.isEmpty()) emit f_EditSpriteRequested(l_oHit);
}

void AtlasCanvas::contextMenuEvent(QContextMenuEvent* l_pE)
{
    const QString l_oHit = f_HitTest(f_ToContent(l_pE->pos()));
    if (!l_oHit.isEmpty() && !m_pDoc->f_Selection().contains(l_oHit)) m_pDoc->f_SetSelection({l_oHit});
    emit f_ContextMenuRequested(l_pE->globalPos());
}

void AtlasCanvas::leaveEvent(QEvent* l_pE)
{
    m_oHover.clear();
    update();
    CanvasView::leaveEvent(l_pE);
}

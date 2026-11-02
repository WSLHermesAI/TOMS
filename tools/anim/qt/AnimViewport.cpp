#include "AnimViewport.h"

#include "bgfx_renderer.h"   // GameScene::ren (the game renderer)

#include "Theme.h"
#include "anim_player.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace animed;
using toms::anim::Node;
using toms::anim::NodePose;

namespace {

const char* g_pszSpriteMime = "application/x-toms-sprite";
constexpr double g_dPi = 3.14159265358979323846;
constexpr double g_dArrowLen = 70, g_dRotateRadius = 58, g_dScaleLen = 62, g_dHandle = 6;

QPointF f_Unit(const QPointF& l_oV)
{
    const double l_dL = std::hypot(l_oV.x(), l_oV.y());
    return l_dL > 1e-9 ? l_oV / l_dL : QPointF(1, 0);
}

double f_Dot(const QPointF& l_oA, const QPointF& l_oB) { return l_oA.x() * l_oB.x() + l_oA.y() * l_oB.y(); }

double f_DistToSegment(const QPointF& l_oP, const QPointF& l_oA, const QPointF& l_oB)
{
    const QPointF l_oAb = l_oB - l_oA;
    const double l_dLen2 = f_Dot(l_oAb, l_oAb);
    const double l_dT = l_dLen2 > 0 ? std::clamp(f_Dot(l_oP - l_oA, l_oAb) / l_dLen2, 0.0, 1.0) : 0.0;
    const QPointF l_oC = l_oA + l_oAb * l_dT;
    return std::hypot(l_oP.x() - l_oC.x(), l_oP.y() - l_oC.y());
}

QTransform f_ToQt(const glm::mat3& l_mat3M) { return QTransform(l_mat3M[0][0], l_mat3M[0][1], l_mat3M[1][0], l_mat3M[1][1], l_mat3M[2][0], l_mat3M[2][1]); }
// A node's 3D world as the editor's orthographic screen camera shows it (what the game draws).
QTransform f_ToQt(const glm::mat4& l_mat4M) { return f_ToQt(toms::anim::f_Flatten(l_mat4M)); }

QPolygonF f_Corners(const Quad& l_oQ)
{
    return QPolygonF({QPointF(l_oQ.m_arrCorners[0], l_oQ.m_arrCorners[1]), QPointF(l_oQ.m_arrCorners[2], l_oQ.m_arrCorners[3]),
                      QPointF(l_oQ.m_arrCorners[4], l_oQ.m_arrCorners[5]), QPointF(l_oQ.m_arrCorners[6], l_oQ.m_arrCorners[7])});
}

// Point in a convex quad, either winding (mirrored sprites wind the other way).
bool f_InQuad(const QPolygonF& l_oC, const QPointF& l_oP)
{
    int l_iSign = 0;
    for (int l_iI = 0; l_iI < 4; l_iI++) {
        const QPointF l_oA = l_oC[l_iI], l_oB = l_oC[(l_iI + 1) % 4];
        const double l_dCross = (l_oB.x() - l_oA.x()) * (l_oP.y() - l_oA.y()) - (l_oB.y() - l_oA.y()) * (l_oP.x() - l_oA.x());
        if (std::fabs(l_dCross) < 1e-9) continue;
        const int l_iS = l_dCross > 0 ? 1 : -1;
        if (l_iSign == 0) l_iSign = l_iS;
        else if (l_iS != l_iSign) return false;
    }
    return l_iSign != 0;
}

void f_DrawArrowHead(QPainter& l_oP, const QPointF& l_oTip, const QPointF& l_oDir, double l_dSize)
{
    const QPointF l_oN(-l_oDir.y(), l_oDir.x());
    l_oP.drawPolygon(QPolygonF({l_oTip, l_oTip - l_oDir * l_dSize + l_oN * l_dSize * 0.5, l_oTip - l_oDir * l_dSize - l_oN * l_dSize * 0.5}));
}

}  // namespace

AnimViewport::AnimViewport(AnimDocument* l_pDoc, bool l_bGameRenderer, QWidget* l_pParent)
    : GameCanvasView(l_bGameRenderer, l_pParent)
    , m_pDoc(l_pDoc)
{
    setAcceptDrops(true);
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, [this] { m_bAtlasDirty = true; });
    connect(l_pDoc, &AnimDocument::f_FileChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_SelectionChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_TimeChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_PreviewTimeChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_SoloChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_FileReset, this, &AnimViewport::f_FrameClip);
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, &AnimViewport::f_FrameClip);
    connect(l_pDoc, &AnimDocument::f_AtlasChanged, this, &AnimViewport::f_FrameClip);
}

void AnimViewport::f_SetTool(Tool l_eT)
{
    if (l_eT == m_eTool) return;
    m_eTool = l_eT;
    update();
    emit f_ToolChanged(l_eT);
}

QSize AnimViewport::f_ContentSize() const { return m_oStage.size().toSize(); }

QTransform AnimViewport::f_QuadTransform(const Quad& l_oQ, const QSizeF& l_oSrc)
{
    const double l_dSw = std::max(1e-9, l_oSrc.width()), l_dSh = std::max(1e-9, l_oSrc.height());
    const QPointF l_oC0(l_oQ.m_arrCorners[0], l_oQ.m_arrCorners[1]), l_oC1(l_oQ.m_arrCorners[2], l_oQ.m_arrCorners[3]), l_oC3(l_oQ.m_arrCorners[6], l_oQ.m_arrCorners[7]);
    return QTransform((l_oC1.x() - l_oC0.x()) / l_dSw, (l_oC1.y() - l_oC0.y()) / l_dSw, (l_oC3.x() - l_oC0.x()) / l_dSh, (l_oC3.y() - l_oC0.y()) / l_dSh, l_oC0.x(), l_oC0.y());
}

void AnimViewport::f_BuildFrame(float l_fT, std::vector<NodePose>& l_vPoses, std::vector<DrawItem>& l_vItems,
                              std::vector<std::string>* l_pMissing) const
{
    l_vPoses.clear();
    l_vItems.clear();
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();   // as a previewed character, when one is chosen
    if (!l_pClip) return;
    toms::anim::f_Evaluate(*l_pClip, l_fT, l_vPoses);
    const glm::mat3 l_mat3Place = toms::anim::f_Placement(0, 0);
    // The game's lookup (AtlasSet, first atlas that has the name wins); each quad's texture is a
    // per-(atlas, page) id of the document's, which maps back to the page image it is cut from.
    const toms::anim::AtlasSet& l_oAtlases = m_pDoc->f_AtlasSet();
    std::vector<NodePose> l_vOne(1);
    std::vector<Quad> l_vQuads;
    for (const NodePose& l_oPose : l_vPoses) {
        if (!m_pDoc->f_SoloShows(l_oPose.m_pNode)) continue;   // solo: only that node's subtree
        l_vOne[0] = l_oPose;
        l_vQuads.clear();
        toms::anim::f_AppendQuads(l_vOne, l_oAtlases, l_mat3Place, nullptr, l_vQuads, l_pMissing);
        for (const Quad& l_oQ : l_vQuads) {
            DrawItem l_oIt;
            l_oIt.m_oQuad = l_oQ;
            f_FindPath(l_pClip->m_oRoot, l_oPose.m_pNode, l_oIt.m_vPath);
            l_oIt.m_pImages = m_pDoc->f_PageImages(l_oQ.m_u16Texture, &l_oIt.m_iPage);
            if (l_oIt.m_pImages && l_oIt.m_iPage >= 0 && l_oIt.m_iPage < l_oIt.m_pImages->f_Pages().size()) {
                const QSize l_oPs = l_oIt.m_pImages->f_Pages()[l_oIt.m_iPage].size();
                const int l_iX0 = int(std::lround(l_oQ.m_arrUv[0] * l_oPs.width())), l_iY0 = int(std::lround(l_oQ.m_arrUv[1] * l_oPs.height()));
                const int l_iX1 = int(std::lround(l_oQ.m_arrUv[2] * l_oPs.width())), l_iY1 = int(std::lround(l_oQ.m_arrUv[3] * l_oPs.height()));
                l_oIt.m_oSrc = QRect(l_iX0, l_iY0, l_iX1 - l_iX0, l_iY1 - l_iY0);
            }
            l_vItems.push_back(l_oIt);
        }
    }
}

void AnimViewport::f_Rebuild()
{
    std::vector<std::string> l_vMissing;
    f_BuildFrame(m_pDoc->f_PreviewTime(), m_vPoses, m_vItems, &l_vMissing);
    m_oMissing.clear();
    for (const std::string& l_strS : l_vMissing) m_oMissing << QString::fromStdString(l_strS);
}

void AnimViewport::f_UpdateStage()
{
    QRectF l_oBounds(-64, -64, 128, 128);
    const float l_fD = m_pDoc->f_Duration();
    std::vector<NodePose> l_vPoses;
    std::vector<DrawItem> l_vItems;
    for (int l_iI = 0; l_iI <= 16; l_iI++) {
        f_BuildFrame(l_fD * float(l_iI) / 16.0f, l_vPoses, l_vItems, nullptr);
        for (const DrawItem& l_oIt : l_vItems) l_oBounds |= f_Corners(l_oIt.m_oQuad).boundingRect();
        if (l_fD <= 0) break;
    }
    const double l_dG = 16;
    l_oBounds = QRectF(QPointF(std::floor(l_oBounds.left() / l_dG) * l_dG - l_dG, std::floor(l_oBounds.top() / l_dG) * l_dG - l_dG),
                    QPointF(std::ceil(l_oBounds.right() / l_dG) * l_dG + l_dG, std::ceil(l_oBounds.bottom() / l_dG) * l_dG + l_dG));
    m_oStage = l_oBounds;
}

void AnimViewport::f_FrameClip()
{
    f_UpdateStage();
    f_Rebuild();
    f_RequestFit();
    f_FitToView();
    update();
}

// ---- painting -----------------------------------------------------------------------------------

AnimViewport::~AnimViewport() { f_ReleaseGameRenderer(); }

void AnimViewport::f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen)
{
    m_oTex.f_Attach(l_pRen);
    m_bAtlasDirty = true;
}

QImage AnimViewport::f_RenderClipFrame(const toms::anim::Clip& l_oClip, const toms::anim::Clip* l_pHeld, float l_fT, int l_iW, int l_iH,
                                     const glm::mat3& l_mat3Place, const QColor& l_oBackground)
{
    return f_RenderOffscreen(l_iW, l_iH, [&](const GameScene& l_oS) {
        f_PrepareAtlases();
        if (l_oBackground.alpha() > 0) l_oS.f_Rect(QRectF(0, 0, l_iW, l_iH), l_oBackground);
        std::vector<NodePose> l_vPoses;
        toms::anim::f_Evaluate(l_oClip, l_fT, l_vPoses);
        toms::anim::DrawList l_oDraws;
        toms::anim::f_AppendDraws(l_oClip, l_vPoses, m_oTex.f_AtlasSet(), l_mat3Place, nullptr, l_oDraws);
        glm::mat3 l_mat3Hand;
        if (l_pHeld && toms::anim::f_AttachPlacement(l_vPoses, "hand", l_mat3Place, l_mat3Hand)) {
            std::vector<NodePose> l_vHeldPoses;
            toms::anim::f_Evaluate(*l_pHeld, std::min(l_fT, l_pHeld->f_Duration()), l_vHeldPoses);
            toms::anim::f_AppendDraws(*l_pHeld, l_vHeldPoses, m_oTex.f_AtlasSet(), l_mat3Hand, nullptr, l_oDraws);
        }
        l_oDraws.f_Submit(&l_oS.m_oRen);
    });
}

void AnimViewport::f_PrepareAtlases()
{
    if (m_bAtlasDirty) {
        m_bAtlasDirty = false;
        std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>> l_vAtlases;
        for (int l_iI = 0; l_iI < m_pDoc->f_AtlasCount(); l_iI++)
            if (m_pDoc->f_AtlasAt(l_iI).m_bOk)
                l_vAtlases.emplace_back(m_pDoc->f_AtlasId(l_iI).toStdString(), m_pDoc->f_AtlasAt(l_iI).m_oPath.toStdString(), &m_pDoc->f_AtlasAt(l_iI).m_oAtlas);
        m_oTex.f_SetAtlases(l_vAtlases);
    }
    for (int l_iI = 0; l_iI < m_pDoc->f_AtlasCount(); l_iI++)   // skinned and deformed nodes bend these (docs/engine/16_MESH_ANIMATION.md)
        if (m_pDoc->f_AtlasAt(l_iI).m_bOk)
            m_oTex.f_SetMeshes(m_pDoc->f_AtlasId(l_iI).toStdString(), m_pDoc->f_AtlasMeshes(l_iI).m_mapMeshes.empty() ? nullptr : &m_pDoc->f_AtlasMeshes(l_iI));
}

void AnimViewport::f_PaintGameScene(const GameScene& l_oS)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    f_PrepareAtlases();
    const QPointF l_oO = f_ToWidget(QPointF(0, 0));   // origin cross: where the game places the clip
    l_oS.f_Line(QPointF(std::round(l_oO.x()) + 0.5, 0), QPointF(std::round(l_oO.x()) + 0.5, height()), 1, l_oTc.m_oGridAxis);
    l_oS.f_Line(QPointF(0, std::round(l_oO.y()) + 0.5), QPointF(width(), std::round(l_oO.y()) + 0.5), 1, l_oTc.m_oGridAxis);
    // The clip, exactly as the game draws it (Game::drawPreviewAnim): evaluate + appendDraws (skinned nodes bent).
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();
    if (!l_pClip) return;
    std::vector<NodePose> l_vPoses;
    toms::anim::f_Evaluate(*l_pClip, m_pDoc->f_PreviewTime(), l_vPoses);
    if (m_pDoc->f_Solo())   // only the solo node's subtree
        l_vPoses.erase(std::remove_if(l_vPoses.begin(), l_vPoses.end(), [this](const NodePose& l_oP) { return !m_pDoc->f_SoloShows(l_oP.m_pNode); }),
                    l_vPoses.end());
    toms::anim::DrawList l_oDraws;
    const glm::mat3 l_mat3Place = GameScene::f_Placement(f_ViewTransform());
    toms::anim::f_AppendDraws(*l_pClip, l_vPoses, m_oTex.f_AtlasSet(), l_mat3Place, nullptr, l_oDraws);
    // View > Held Clip: the weapon's clip of the same name at the `hand` node, in front (as CharacterRig draws it)
    glm::mat3 l_mat3Hand;
    if (const toms::anim::Clip* l_pHeld = m_pDoc->f_HeldClip(); l_pHeld && toms::anim::f_AttachPlacement(l_vPoses, "hand", l_mat3Place, l_mat3Hand)) {
        std::vector<NodePose> l_vHeldPoses;
        toms::anim::f_Evaluate(*l_pHeld, std::min(m_pDoc->f_PreviewTime(), l_pHeld->f_Duration()), l_vHeldPoses);
        toms::anim::f_AppendDraws(*l_pHeld, l_vHeldPoses, m_oTex.f_AtlasSet(), l_mat3Hand, nullptr, l_oDraws);
    }
    l_oDraws.f_Submit(&l_oS.m_oRen);
}

void AnimViewport::f_PaintContent(QPainter& l_oP)
{
    f_Rebuild();
    const Theme::Colors& l_oTc = Theme::f_Colors();
    const QTransform l_oView = f_ViewTransform();
    const bool l_bOver = f_OverGameScene();   // the game renderer drew the origin and sprites

    if (!l_bOver) {   // origin cross: where the game places the clip
        const QPointF l_oO = f_ToWidget(QPointF(0, 0));
        l_oP.setPen(QPen(l_oTc.m_oGridAxis, 1));
        l_oP.drawLine(QPointF(std::round(l_oO.x()) + 0.5, 0), QPointF(std::round(l_oO.x()) + 0.5, height()));
        l_oP.drawLine(QPointF(0, std::round(l_oO.y()) + 0.5), QPointF(width(), std::round(l_oO.y()) + 0.5));
    }

    // The sprites: each quad is an affine image of its pixel rect, like the game's vertices.
    l_oP.save();
    l_oP.setRenderHint(QPainter::SmoothPixmapTransform, false);
    l_oP.setRenderHint(QPainter::Antialiasing, false);
    for (const DrawItem& l_oIt : m_vItems) {
        if (l_bOver || l_oIt.m_oSrc.isEmpty() || !l_oIt.m_pImages) continue;
        const QImage l_oImg = l_oIt.m_pImages->f_Tinted(l_oIt.m_iPage, l_oIt.m_oSrc, l_oIt.m_oQuad.m_arrTint);
        if (l_oImg.isNull()) continue;
        l_oP.setTransform(f_QuadTransform(l_oIt.m_oQuad, l_oIt.m_oSrc.size()) * l_oView);
        // Additive = the game's SRC_ALPHA, ONE: premultiplied source plus destination.
        l_oP.setCompositionMode(l_oIt.m_oQuad.m_bAdditive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
        l_oP.drawImage(QPointF(0, 0), l_oImg);
    }
    l_oP.restore();

    l_oP.setRenderHint(QPainter::Antialiasing, true);
    const NodePath& l_vSel = m_pDoc->f_SelectedPath();
    if (m_iHover >= 0 && m_iHover < int(m_vItems.size()) && m_vItems[size_t(m_iHover)].m_vPath != l_vSel && m_eDrag == Drag::None) {
        l_oP.setPen(QPen(l_oTc.m_oHover.lighter(300), 1, Qt::DashLine));
        l_oP.setBrush(Qt::NoBrush);
        l_oP.drawPolygon(l_oView.map(f_Corners(m_vItems[size_t(m_iHover)].m_oQuad)));
    }
    for (const DrawItem& l_oIt : m_vItems)
        if (l_oIt.m_vPath == l_vSel) {
            l_oP.setPen(QPen(l_oTc.m_oSelection, 1.5));
            l_oP.setBrush(Qt::NoBrush);
            l_oP.drawPolygon(l_oView.map(f_Corners(l_oIt.m_oQuad)));
        }
    f_DrawSkins(l_oP, l_oView);
    f_DrawLattice(l_oP, l_oView);
    if (m_eMode != EditMode::Weights) f_DrawGizmo(l_oP, f_Gizmo());   // Weights mode: the bones are painted, not moved
    f_DrawBrush(l_oP);

    // Status text over the view.
    l_oP.setRenderHint(QPainter::Antialiasing, false);
    const toms::anim::Clip* l_pClip = m_pDoc->f_Clip();
    QString l_oInfo = l_pClip ? QStringLiteral("%1   t = %2 s / %3 s").arg(QString::fromStdString(l_pClip->m_strName))
                                  .arg(m_pDoc->f_Time(), 0, 'f', 3).arg(l_pClip->f_Duration(), 0, 'f', 3)
                        : tr("no clip");
    if (l_pClip && m_pDoc->f_Solo())
        if (const Node* l_pSn = f_NodeAt(l_pClip->m_oRoot, *m_pDoc->f_Solo()))
            l_oInfo += tr("   SOLO: %1").arg(QString::fromStdString(l_pSn->m_strName));
    if (l_pClip && m_pDoc->f_PreviewTime() > m_pDoc->f_Time())
        l_oInfo += tr("   (ended: looping nodes play on, +%1 s)").arg(m_pDoc->f_PreviewTime() - m_pDoc->f_Time(), 0, 'f', 1);
    if (m_eMode == EditMode::Bones) l_oInfo += tr("   BONES: click a bone (Auto-Key on: pose it; off: place it in the sprite)");
    if (m_eMode == EditMode::Lattice) l_oInfo += tr("   LATTICE: drag a point to key the deform at the playhead");
    if (m_eMode == EditMode::Weights) {
        const Node* l_pSel = m_pDoc->f_SelectedNode();
        l_oInfo += l_pSel ? tr("   WEIGHTS of %1: drag to add, Ctrl subtract, Shift smooth; [ ] brush size; click a joint for another bone")
                          .arg(QString::fromStdString(l_pSel->m_strName))
                    : tr("   WEIGHTS: click a bone's joint to paint its weights");
    }
    if (!m_pDoc->f_PreviewCharacter().isEmpty()) l_oInfo += tr("   AS: %1").arg(m_pDoc->f_PreviewCharacter());
    if (m_pDoc->f_AtlasCount() == 0) l_oInfo += tr("   (no atlas: add one under Properties > Atlases)");
    else if (!m_pDoc->f_AtlasLoaded()) l_oInfo += tr("   (an atlas did not load: see Problems)");
    f_DrawLabel(l_oP, QPointF(8, 8), l_oInfo);
    if (!m_oMissing.isEmpty()) f_DrawLabel(l_oP, QPointF(8, 32), tr("missing sprites: %1").arg(m_oMissing.join(QStringLiteral(", "))));
    if (m_eMode == EditMode::Weights) {
        const QString l_oUnder = f_WeightsUnderCursor();
        if (!l_oUnder.isEmpty()) f_DrawLabel(l_oP, QPointF(8, m_oMissing.isEmpty() ? 32 : 56), l_oUnder);
    }
}

void AnimViewport::f_SetEditMode(EditMode l_eM)
{
    if (l_eM == m_eMode) return;
    m_eMode = l_eM;
    m_iHover = -1;
    m_oHoverBone.clear();
    m_iHoverPoint = -1;
    m_bPainting = false;
    if (l_eM != EditMode::Weights) unsetCursor();
    update();
}

bool AnimViewport::f_SelectedLattice(toms::anim::LatticeView& l_oOut) const
{
    const Node* l_pSel = m_pDoc->f_DrawNode(m_pDoc->f_SelectedNode());
    if (!l_pSel || l_pSel->m_strSprite.empty()) return false;
    for (size_t l_nI = 0; l_nI < m_vPoses.size(); l_nI++)
        if (m_vPoses[l_nI].m_pNode == l_pSel)
            return m_vPoses[l_nI].m_bVisible && toms::anim::f_LatticeView(m_vPoses, l_nI, m_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), l_oOut);
    return false;
}

int AnimViewport::f_LatticePointAt(const QPointF& l_oW) const
{
    toms::anim::LatticeView l_oLv;
    if (m_eMode != EditMode::Lattice || !f_SelectedLattice(l_oLv)) return -1;
    const QTransform l_oView = f_ViewTransform();
    int l_iBest = -1;
    double l_dBestD = 9.0;
    for (size_t l_nI = 0; l_nI < l_oLv.m_vPoints.size(); l_nI++) {
        const QPointF l_oPt = l_oView.map(QPointF(l_oLv.m_vPoints[l_nI].x, l_oLv.m_vPoints[l_nI].y));
        const double l_dD = std::hypot(l_oW.x() - l_oPt.x(), l_oW.y() - l_oPt.y());
        if (l_dD < l_dBestD) { l_dBestD = l_dD; l_iBest = int(l_nI); }
    }
    return l_iBest;
}

void AnimViewport::f_DrawLattice(QPainter& l_oP, const QTransform& l_oView) const
{
    toms::anim::LatticeView l_oLv;
    if (m_eMode != EditMode::Lattice || !f_SelectedLattice(l_oLv)) return;
    auto l_fnAt = [&](int l_iC, int l_iR) { const glm::vec2& l_v2V = l_oLv.m_vPoints[size_t(l_iR * l_oLv.m_iCols + l_iC)]; return l_oView.map(QPointF(l_v2V.x, l_v2V.y)); };
    l_oP.setPen(QPen(QColor(255, 210, 90, 170), 1));
    for (int l_iR = 0; l_iR < l_oLv.m_iRows; l_iR++)
        for (int l_iC = 0; l_iC < l_oLv.m_iCols; l_iC++) {
            if (l_iC + 1 < l_oLv.m_iCols) l_oP.drawLine(l_fnAt(l_iC, l_iR), l_fnAt(l_iC + 1, l_iR));
            if (l_iR + 1 < l_oLv.m_iRows) l_oP.drawLine(l_fnAt(l_iC, l_iR), l_fnAt(l_iC, l_iR + 1));
        }
    for (int l_iR = 0; l_iR < l_oLv.m_iRows; l_iR++)
        for (int l_iC = 0; l_iC < l_oLv.m_iCols; l_iC++) {
            const int l_iI = l_iR * l_oLv.m_iCols + l_iC;
            const bool l_bMoved = l_oLv.m_vOffsets[size_t(l_iI)] != glm::vec2(0.0f);
            const bool l_bHot = l_iI == m_iHoverPoint || l_iI == m_iDragPoint;
            l_oP.setPen(QPen(l_bHot ? Qt::white : QColor(0, 0, 0, 200), l_bHot ? 2 : 1));
            l_oP.setBrush(l_bMoved ? QColor(255, 140, 60) : QColor(255, 220, 110));
            l_oP.drawRect(QRectF(l_fnAt(l_iC, l_iR) - QPointF(4, 4), QSizeF(8, 8)));
        }
}

QString AnimViewport::f_BoneAt(const QPointF& l_oW, bool l_bJointsOnly) const
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();
    if (!l_pClip) return QString();
    const glm::mat3 l_mat3Place = toms::anim::f_Placement(0, 0);
    const QTransform l_oView = f_ViewTransform();
    QString l_oBest;
    double l_dBestD = 9.0;   // widget pixels: a joint or a segment within this
    for (size_t l_nI = 0; l_nI < m_vPoses.size(); l_nI++) {
        const NodePose& l_oPose = m_vPoses[l_nI];
        if (!l_oPose.m_pNode || !l_oPose.m_pNode->f_Skinned() || !l_oPose.m_bVisible || !m_pDoc->f_SoloShows(l_oPose.m_pNode)) continue;
        toms::anim::SkinView l_oSv;
        if (toms::anim::f_SkinView(*l_pClip, m_vPoses, l_nI, m_pDoc->f_AtlasSet(), l_mat3Place, &l_oSv) != toms::anim::SkinState::Ok) continue;
        for (size_t l_nB = 0; l_nB < l_oSv.m_vBones.size(); l_nB++) {
            if (!l_oSv.m_vBoneFound[l_nB]) continue;
            const QPointF l_oS = l_oView.map(QPointF(l_oSv.m_vBoneStart[l_nB].x, l_oSv.m_vBoneStart[l_nB].y)), l_oE = l_oView.map(QPointF(l_oSv.m_vBoneEnd[l_nB].x, l_oSv.m_vBoneEnd[l_nB].y));
            // The joint wins over a segment passing by: it counts 3 pixels nearer.
            const double l_dJoint = std::hypot(l_oW.x() - l_oS.x(), l_oW.y() - l_oS.y()) - 3.0;
            const QPointF l_oSe = l_oE - l_oS;
            const double l_dLen2 = l_oSe.x() * l_oSe.x() + l_oSe.y() * l_oSe.y();
            const double l_dT = l_dLen2 > 1e-9 ? std::clamp(((l_oW.x() - l_oS.x()) * l_oSe.x() + (l_oW.y() - l_oS.y()) * l_oSe.y()) / l_dLen2, 0.0, 1.0) : 0.0;
            const double l_dSeg = std::hypot(l_oW.x() - (l_oS.x() + l_dT * l_oSe.x()), l_oW.y() - (l_oS.y() + l_dT * l_oSe.y()));
            const double l_dD = l_bJointsOnly ? l_dJoint : std::min(l_dJoint, l_dSeg);
            if (l_dD < l_dBestD) {
                l_dBestD = l_dD;
                l_oBest = QString::fromStdString(l_oSv.m_vBones[l_nB]);
            }
        }
    }
    return l_oBest;
}

QColor AnimViewport::f_BoneColor(int l_iB)
{
    static const QColor l_arrPalette[] = {QColor(0xff, 0x5c, 0x5c), QColor(0x4f, 0xc3, 0xf7), QColor(0xff, 0xd5, 0x4f),
                                      QColor(0x81, 0xc7, 0x84), QColor(0xba, 0x68, 0xc8), QColor(0xff, 0x8a, 0x65),
                                      QColor(0x4d, 0xb6, 0xac), QColor(0xf0, 0x62, 0x92)};
    return l_arrPalette[size_t(l_iB < 0 ? 0 : l_iB) % (sizeof(l_arrPalette) / sizeof(l_arrPalette[0]))];
}

void AnimViewport::f_DrawSkins(QPainter& l_oP, const QTransform& l_oView) const
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();
    if ((!m_bShowSkin && m_eMode != EditMode::Bones && m_eMode != EditMode::Weights) || !l_pClip) return;
    const bool l_bHeat = m_eMode == EditMode::Weights;   // the selected bone's weights as colours (blue 0 .. red 1)
    const Node* l_pSel = m_pDoc->f_SelectedNode();
    const glm::mat3 l_mat3Place = toms::anim::f_Placement(0, 0);   // content = clip space, as buildFrame
    auto l_fnAt = [&](const glm::vec2& l_v2C) { return l_oView.map(QPointF(l_v2C.x, l_v2C.y)); };
    for (size_t l_nI = 0; l_nI < m_vPoses.size(); l_nI++) {
        const NodePose& l_oPose = m_vPoses[l_nI];
        // Hidden (itself, a parent, a visible key) or left out by solo: no mesh is drawn, so no overlay either.
        if (!l_oPose.m_pNode || !l_oPose.m_pNode->f_Skinned() || !l_oPose.m_bVisible || !m_pDoc->f_SoloShows(l_oPose.m_pNode)) continue;
        toms::anim::SkinView l_oSv;
        if (toms::anim::f_SkinView(*l_pClip, m_vPoses, l_nI, m_pDoc->f_AtlasSet(), l_mat3Place, &l_oSv) != toms::anim::SkinState::Ok) continue;
        const int l_iBones = int(l_oSv.m_vBones.size());
        int l_iSelBone = -1;   // the selected node is one of this skin's bones: shade by its weight
        for (int l_iB = 0; l_iB < l_iBones && l_pSel; l_iB++)
            if (l_oSv.m_vBoneFound[size_t(l_iB)] && l_oSv.m_vBones[size_t(l_iB)] == l_pSel->m_strName) l_iSelBone = l_iB;

        if (l_bHeat && l_iSelBone < 0) continue;   // Weights mode: only the skins the selected bone is in
        if (l_bHeat) {   // every triangle filled with its points' mean weight for the selected bone
            l_oP.setPen(Qt::NoPen);
            for (size_t l_nT = 0; l_nT + 2 < l_oSv.m_vTriangles.size(); l_nT += 3) {
                double l_dW = 0;
                for (int l_iC = 0; l_iC < 3; l_iC++) l_dW += animed::f_BoneWeight(l_oSv.m_vWeights, l_oSv.m_vTriangles[l_nT + size_t(l_iC)], l_iSelBone);
                QColor l_oCol = f_WeightColor(l_dW / 3);
                l_oCol.setAlpha(150);
                l_oP.setBrush(l_oCol);
                l_oP.drawPolygon(QPolygonF({l_fnAt(l_oSv.m_vVertices[l_oSv.m_vTriangles[l_nT]]), l_fnAt(l_oSv.m_vVertices[l_oSv.m_vTriangles[l_nT + 1]]),
                                         l_fnAt(l_oSv.m_vVertices[l_oSv.m_vTriangles[l_nT + 2]])}));
            }
        }
        // The mesh: every edge once (two triangles share most of them), in one call, without antialiasing -- a mesh has
        // hundreds of edges, and thin lines drawn one by one, smoothed, made each repaint of a skinned clip cost tens of ms.
        {
            std::vector<uint32_t> l_vSeen;
            l_vSeen.reserve(l_oSv.m_vTriangles.size());
            QVector<QLineF> l_oLines;
            l_oLines.reserve(int(l_oSv.m_vTriangles.size()));
            std::vector<QPointF> l_vPts(l_oSv.m_vVertices.size());
            for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) l_vPts[l_nV] = l_fnAt(l_oSv.m_vVertices[l_nV]);
            for (size_t l_nT = 0; l_nT + 2 < l_oSv.m_vTriangles.size(); l_nT += 3)
                for (int l_iK = 0; l_iK < 3; l_iK++) {
                    const uint16_t l_u16A = l_oSv.m_vTriangles[l_nT + size_t(l_iK)], l_u16B = l_oSv.m_vTriangles[l_nT + size_t((l_iK + 1) % 3)];
                    l_vSeen.push_back(uint32_t(std::min(l_u16A, l_u16B)) << 16 | std::max(l_u16A, l_u16B));
                }
            std::sort(l_vSeen.begin(), l_vSeen.end());
            l_vSeen.erase(std::unique(l_vSeen.begin(), l_vSeen.end()), l_vSeen.end());
            for (uint32_t l_uE : l_vSeen) l_oLines.push_back(QLineF(l_vPts[l_uE >> 16], l_vPts[l_uE & 0xffff]));
            l_oP.save();
            l_oP.setRenderHint(QPainter::Antialiasing, false);
            l_oP.setPen(QPen(QColor(255, 255, 255, 80), 1));
            l_oP.drawLines(l_oLines);
            l_oP.restore();
        }
        // The points: the colours of the bones they follow, mixed by weight; or how much they follow the selected bone.
        for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) {
            double l_dR = 0, l_dG = 0, l_dBl = 0, l_dSum = 0, l_dMine = 0;
            for (int l_iK = 0; l_iK < toms::anim::g_iSkinInfluences; l_iK++) {
                const float l_fW = l_oSv.m_vWeights[l_nV * 2 * toms::anim::g_iSkinInfluences + 2 * size_t(l_iK) + 1];
                const int l_iB = int(l_oSv.m_vWeights[l_nV * 2 * toms::anim::g_iSkinInfluences + 2 * size_t(l_iK)]);
                if (l_fW <= 0 || l_iB < 0 || l_iB >= l_iBones) continue;
                const QColor l_oC = f_BoneColor(l_iB);
                l_dR += l_fW * l_oC.redF(); l_dG += l_fW * l_oC.greenF(); l_dBl += l_fW * l_oC.blueF(); l_dSum += l_fW;
                if (l_iB == l_iSelBone) l_dMine += l_fW;
            }
            QColor l_oCol;
            if (l_bHeat) {
                l_oCol = f_WeightColor(l_dSum > 0 ? l_dMine / l_dSum : 0);
            } else if (l_iSelBone >= 0) {
                const double l_dF = l_dSum > 0 ? l_dMine / l_dSum : 0, l_dDim = 0.18;
                const QColor l_oC = f_BoneColor(l_iSelBone);
                l_oCol = QColor::fromRgbF(float(l_dDim + (l_oC.redF() - l_dDim) * l_dF), float(l_dDim + (l_oC.greenF() - l_dDim) * l_dF), float(l_dDim + (l_oC.blueF() - l_dDim) * l_dF));
            } else {
                l_oCol = l_dSum > 0 ? QColor::fromRgbF(float(l_dR / l_dSum), float(l_dG / l_dSum), float(l_dBl / l_dSum)) : QColor(128, 128, 128);
            }
            const QPixmap& l_oDot = f_VertexDot(l_oCol, l_bHeat ? 2.5 : 3.5, l_oP.device() ? l_oP.device()->devicePixelRatioF() : 1.0);
            const QPointF l_oC = l_fnAt(l_oSv.m_vVertices[l_nV]);
            l_oP.drawPixmap(l_oC - QPointF(l_oDot.width(), l_oDot.height()) / (2.0 * l_oDot.devicePixelRatio()), l_oDot);
        }
        // The bones: the segment each one's weights are measured against, where the bone has carried it now.
        for (int l_iB = 0; l_iB < l_iBones; l_iB++) {
            const QPointF l_oS = l_fnAt(l_oSv.m_vBoneStart[size_t(l_iB)]), l_oE = l_fnAt(l_oSv.m_vBoneEnd[size_t(l_iB)]);
            const bool l_bPicked = l_iB == l_iSelBone;
            const bool l_bHovered = (m_eMode == EditMode::Bones || m_eMode == EditMode::Weights) && !l_bPicked && l_oSv.m_vBoneFound[size_t(l_iB)] &&
                                 QString::fromStdString(l_oSv.m_vBones[size_t(l_iB)]) == m_oHoverBone;
            const QColor l_oC = l_bHovered ? f_BoneColor(l_iB).lighter(135) : f_BoneColor(l_iB);
            if (l_bHovered) {
                l_oP.setPen(QPen(QColor(255, 255, 255, 140), 6, Qt::SolidLine, Qt::RoundCap));
                l_oP.drawLine(l_oS, l_oE);
            }
            if (l_bPicked) {
                l_oP.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap));
                l_oP.drawLine(l_oS, l_oE);
            }
            l_oP.setPen(QPen(l_oSv.m_vBoneFound[size_t(l_iB)] ? l_oC : QColor(255, 60, 60), l_bPicked ? 4 : 3,
                          l_oSv.m_vBoneFound[size_t(l_iB)] ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap));
            l_oP.drawLine(l_oS, l_oE);
            l_oP.setBrush(l_oC);
            l_oP.setPen(QPen(l_bPicked ? Qt::white : QColor(0, 0, 0, 200), l_bPicked ? 2 : 1));
            l_oP.drawEllipse(l_oS, 5.5, 5.5);
            f_DrawLabel(l_oP, l_oS + QPointF(8, -22), l_oSv.m_vBoneFound[size_t(l_iB)] ? QString::fromStdString(l_oSv.m_vBones[size_t(l_iB)])
                                                                      : tr("%1 (no such node)").arg(QString::fromStdString(l_oSv.m_vBones[size_t(l_iB)])));
        }
    }
}

// A point of a skin's mesh: a dot in `col` with a dark rim, made once per colour (to 5 bits a channel) and size, then
// stamped -- hundreds of points a repaint, each an antialiased ellipse with its own pen and brush, were most of its cost.
const QPixmap& AnimViewport::f_VertexDot(const QColor& l_oCol, double l_dRadius, double l_dDpr) const
{
    const QColor l_oQ((l_oCol.red() & 0xf8) | 4, (l_oCol.green() & 0xf8) | 4, (l_oCol.blue() & 0xf8) | 4);
    const quint64 l_u64Key = quint64(l_oQ.rgb() & 0xffffff) | quint64(l_dRadius * 2) << 24 | quint64(l_dDpr * 4) << 32;
    auto l_oIt = m_oDots.find(l_u64Key);
    if (l_oIt != m_oDots.end()) return l_oIt.value();
    const double l_dSize = 2 * l_dRadius + 2;
    QPixmap l_oPm(int(std::ceil(l_dSize * l_dDpr)), int(std::ceil(l_dSize * l_dDpr)));
    l_oPm.setDevicePixelRatio(l_dDpr);
    l_oPm.fill(Qt::transparent);
    QPainter l_oD(&l_oPm);
    l_oD.setRenderHint(QPainter::Antialiasing, true);
    l_oD.setPen(QPen(QColor(0, 0, 0, 200), 1));
    l_oD.setBrush(l_oQ);
    l_oD.drawEllipse(QPointF(l_dSize / 2, l_dSize / 2), l_dRadius, l_dRadius);
    l_oD.end();
    if (m_oDots.size() > 4096) m_oDots.clear();
    return m_oDots.insert(l_u64Key, l_oPm).value();
}

QColor AnimViewport::f_WeightColor(double l_dW)
{
    // blue -> cyan -> green -> yellow -> red, as most weight painters show it
    static const QColor l_arrStops[] = {QColor(30, 50, 170), QColor(0, 190, 230), QColor(40, 200, 70), QColor(250, 220, 40),
                                    QColor(235, 50, 40)};
    const double l_dX = std::clamp(l_dW, 0.0, 1.0) * 4.0;
    const int l_iI = std::min(3, int(l_dX));
    const double l_dF = l_dX - l_iI;
    const QColor &l_oA = l_arrStops[l_iI], &l_oB = l_arrStops[l_iI + 1];
    return QColor::fromRgbF(float(l_oA.redF() + (l_oB.redF() - l_oA.redF()) * l_dF), float(l_oA.greenF() + (l_oB.greenF() - l_oA.greenF()) * l_dF),
                            float(l_oA.blueF() + (l_oB.blueF() - l_oA.blueF()) * l_dF));
}

void AnimViewport::f_SetBrushRadius(double l_dR)
{
    l_dR = std::clamp(l_dR, 4.0, 400.0);
    if (l_dR == m_dBrushRadius) return;
    m_dBrushRadius = l_dR;
    update();
    emit f_BrushChanged();
}

void AnimViewport::f_SetBrushStrength(double l_dS)
{
    l_dS = std::clamp(l_dS, 0.01, 1.0);
    if (l_dS == m_dBrushStrength) return;
    m_dBrushStrength = l_dS;
    update();
    emit f_BrushChanged();
}

void AnimViewport::f_DrawBrush(QPainter& l_oP) const
{
    if (m_eMode != EditMode::Weights || m_oCursor.x() < -1e8 || !rect().contains(m_oCursor.toPoint())) return;
    const QColor l_oC = m_ePaintMode == animed::WeightBrush::Subtract ? QColor(120, 170, 255)
                     : m_ePaintMode == animed::WeightBrush::Smooth ? QColor(200, 200, 200) : QColor(255, 140, 90);
    l_oP.setBrush(Qt::NoBrush);
    l_oP.setPen(QPen(QColor(0, 0, 0, 160), 3));
    l_oP.drawEllipse(m_oCursor, m_dBrushRadius, m_dBrushRadius);
    l_oP.setPen(QPen(l_oC, 1.5));
    l_oP.drawEllipse(m_oCursor, m_dBrushRadius, m_dBrushRadius);
    // the strength: an inner ring
    l_oP.setPen(QPen(l_oC, 1, Qt::DotLine));
    l_oP.drawEllipse(m_oCursor, m_dBrushRadius * m_dBrushStrength, m_dBrushRadius * m_dBrushStrength);
}

QString AnimViewport::f_WeightsUnderCursor() const
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();
    if (!l_pClip || m_oCursor.x() < -1e8) return QString();
    const glm::mat3 l_mat3Place = toms::anim::f_Placement(0, 0);
    const QTransform l_oView = f_ViewTransform();
    const Node* l_pSel = m_pDoc->f_SelectedNode();
    QString l_oBest;
    double l_dBestD = 10.0;   // widget pixels
    for (size_t l_nI = 0; l_nI < m_vPoses.size(); l_nI++) {
        const NodePose& l_oPose = m_vPoses[l_nI];
        if (!l_oPose.m_pNode || !l_oPose.m_pNode->f_Skinned() || !l_oPose.m_bVisible || !m_pDoc->f_SoloShows(l_oPose.m_pNode)) continue;
        toms::anim::SkinView l_oSv;
        if (toms::anim::f_SkinView(*l_pClip, m_vPoses, l_nI, m_pDoc->f_AtlasSet(), l_mat3Place, &l_oSv) != toms::anim::SkinState::Ok) continue;
        if (!l_pSel || std::find(l_oSv.m_vBones.begin(), l_oSv.m_vBones.end(), l_pSel->m_strName) == l_oSv.m_vBones.end()) continue;
        for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) {
            const QPointF l_oPt = l_oView.map(QPointF(l_oSv.m_vVertices[l_nV].x, l_oSv.m_vVertices[l_nV].y));
            const double l_dD = std::hypot(m_oCursor.x() - l_oPt.x(), m_oCursor.y() - l_oPt.y());
            if (l_dD >= l_dBestD) continue;
            l_dBestD = l_dD;
            QStringList l_oParts;
            for (int l_iK = 0; l_iK < toms::anim::g_iSkinInfluences; l_iK++) {
                const float l_fW = l_oSv.m_vWeights[l_nV * 2 * toms::anim::g_iSkinInfluences + 2 * size_t(l_iK) + 1];
                const int l_iB = int(l_oSv.m_vWeights[l_nV * 2 * toms::anim::g_iSkinInfluences + 2 * size_t(l_iK)]);
                if (l_fW > 0 && l_iB >= 0 && l_iB < int(l_oSv.m_vBones.size()))
                    l_oParts << QStringLiteral("%1 %2").arg(QString::fromStdString(l_oSv.m_vBones[size_t(l_iB)])).arg(l_fW, 0, 'f', 2);
            }
            l_oBest = tr("%1 point %2: %3%4").arg(QString::fromStdString(l_oPose.m_pNode->m_strName)).arg(l_nV).arg(l_oParts.join(QStringLiteral(", ")))
                       .arg(l_oSv.m_bAutoWeights ? tr("  (automatic weights)") : QString());
        }
    }
    return l_oBest;
}

bool AnimViewport::f_PaintWeightsAt(const QPointF& l_oW, animed::WeightBrush l_eMode, int l_iStroke)
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();
    const Node* l_pSel = m_pDoc->f_SelectedNode();
    if (!l_pClip || !l_pSel) return false;
    f_Rebuild();
    const glm::mat3 l_mat3Place = toms::anim::f_Placement(0, 0);
    const QTransform l_oView = f_ViewTransform();
    const double l_dScale = std::max(1e-9, std::sqrt(std::fabs(l_oView.determinant())));   // widget pixels per clip pixel
    const QPointF l_oC = l_oView.inverted().map(l_oW);
    const float l_fRadius = float(m_dBrushRadius / l_dScale);
    struct Paint { NodePath m_vPath; std::vector<float> m_vWeights; };
    std::vector<Paint> l_vPaints;
    for (size_t l_nI = 0; l_nI < m_vPoses.size(); l_nI++) {
        const NodePose& l_oPose = m_vPoses[l_nI];
        if (!l_oPose.m_pNode || !l_oPose.m_pNode->f_Skinned() || !l_oPose.m_bVisible || !m_pDoc->f_SoloShows(l_oPose.m_pNode)) continue;
        toms::anim::SkinView l_oSv;
        if (toms::anim::f_SkinView(*l_pClip, m_vPoses, l_nI, m_pDoc->f_AtlasSet(), l_mat3Place, &l_oSv) != toms::anim::SkinState::Ok) continue;
        int l_iBone = -1;
        for (size_t l_nB = 0; l_nB < l_oSv.m_vBones.size(); l_nB++)
            if (l_oSv.m_vBoneFound[l_nB] && l_oSv.m_vBones[l_nB] == l_pSel->m_strName) l_iBone = int(l_nB);
        if (l_iBone < 0) continue;
        std::vector<float> l_vAmount(l_oSv.m_vVertices.size(), 0.0f);
        bool l_bAny = false;
        for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) {
            const float l_fD = std::hypot(l_oSv.m_vVertices[l_nV].x - float(l_oC.x()), l_oSv.m_vVertices[l_nV].y - float(l_oC.y()));
            l_vAmount[l_nV] = float(m_dBrushStrength) * animed::f_BrushFalloff(l_fD, l_fRadius);
            l_bAny = l_bAny || l_vAmount[l_nV] > 0;
        }
        if (!l_bAny) continue;
        // Subtract on a point only this bone moves: the nearest other bone (its segment, as drawn now) takes the share.
        std::vector<float> l_vFallback(l_oSv.m_vVertices.size() * 2 * toms::anim::g_iSkinInfluences, 0.0f);
        for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) {
            int l_iNearest = -1;
            double l_dNearD = 1e30;
            for (size_t l_nB = 0; l_nB < l_oSv.m_vBones.size(); l_nB++) {
                if (int(l_nB) == l_iBone || !l_oSv.m_vBoneFound[l_nB]) continue;
                const double l_dD = f_DistToSegment(QPointF(l_oSv.m_vVertices[l_nV].x, l_oSv.m_vVertices[l_nV].y), QPointF(l_oSv.m_vBoneStart[l_nB].x, l_oSv.m_vBoneStart[l_nB].y),
                                               QPointF(l_oSv.m_vBoneEnd[l_nB].x, l_oSv.m_vBoneEnd[l_nB].y));
                if (l_dD < l_dNearD) { l_dNearD = l_dD; l_iNearest = int(l_nB); }
            }
            l_vFallback[l_nV * 2 * toms::anim::g_iSkinInfluences] = float(std::max(0, l_iNearest));
            l_vFallback[l_nV * 2 * toms::anim::g_iSkinInfluences + 1] = l_iNearest >= 0 ? 1.0f : 0.0f;
        }
        Paint l_oPt;
        l_oPt.m_vWeights = l_oSv.m_vWeights;   // automatic until the first stroke: painting starts from them
        animed::f_PaintWeights(l_oPt.m_vWeights, int(l_oSv.m_vBones.size()), l_iBone, l_eMode, l_vAmount, l_oSv.m_vTriangles, &l_vFallback);
        if (l_oPt.m_vWeights == l_oSv.m_vWeights) continue;   // nothing moved (Subtract on a point no other bone can take)
        if (!f_FindPath(l_pClip->m_oRoot, l_oPose.m_pNode, l_oPt.m_vPath)) continue;
        l_vPaints.push_back(std::move(l_oPt));
    }
    bool l_bChanged = false;
    const QString l_oKey = QStringLiteral("#weights%1").arg(l_iStroke);   // '#': the whole stroke is one undo step
    for (const Paint& l_oPt : l_vPaints)
        l_bChanged |= m_pDoc->f_EditNode(l_oPt.m_vPath, tr("Paint Weights"), [&l_oPt](Node& l_oN) { l_oN.m_vSkinWeights = l_oPt.m_vWeights; }, l_oKey);
    return l_bChanged;
}

AnimViewport::Gizmo AnimViewport::f_Gizmo() const
{
    Gizmo l_oG;
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();
    const Node* l_pSel = m_pDoc->f_DrawNode(m_pDoc->f_SelectedNode());
    if (!l_pClip || !l_pSel) return l_oG;
    const NodePose* l_pMe = nullptr;
    const NodePose* l_pParent = nullptr;
    const NodePath& l_vPath = m_pDoc->f_SelectedPath();
    const Node* l_pParentNode = l_vPath.empty() ? nullptr : f_NodeAt(l_pClip->m_oRoot, NodePath(l_vPath.begin(), l_vPath.end() - 1));
    for (const NodePose& l_oPose : m_vPoses) {
        if (l_oPose.m_pNode == l_pSel) l_pMe = &l_oPose;
        if (l_pParentNode && l_oPose.m_pNode == l_pParentNode) l_pParent = &l_oPose;
    }
    if (!l_pMe) return l_oG;
    const glm::mat3 l_mat3Pw = l_pParent ? toms::anim::f_Flatten(l_pParent->m_mat4World) : glm::mat3(1.0f);
    const glm::mat3 l_mat3Mw = toms::anim::f_Flatten(l_pMe->m_mat4World);
    l_oG.m_bValid = true;
    l_oG.m_oOrigin = f_ToWidget(QPointF(l_mat3Mw[2][0], l_mat3Mw[2][1]));
    l_oG.m_oParentX = f_Unit(QPointF(l_mat3Pw[0][0], l_mat3Pw[0][1]));
    l_oG.m_oParentY = f_Unit(QPointF(l_mat3Pw[1][0], l_mat3Pw[1][1]));
    l_oG.m_oNodeX = f_Unit(QPointF(l_mat3Mw[0][0], l_mat3Mw[0][1]));
    l_oG.m_oNodeY = f_Unit(QPointF(l_mat3Mw[1][0], l_mat3Mw[1][1]));
    l_oG.m_dParentDet = double(l_mat3Pw[0][0]) * l_mat3Pw[1][1] - double(l_mat3Pw[1][0]) * l_mat3Pw[0][1];
    l_oG.m_oParentToContent = f_ToQt(l_mat3Pw);
    return l_oG;
}

void AnimViewport::f_DrawGizmo(QPainter& l_oP, const Gizmo& l_oG) const
{
    if (!l_oG.m_bValid) return;
    const Theme::Colors& l_oTc = Theme::f_Colors();
    const QPointF l_oO = l_oG.m_oOrigin;
    // The pivot: the node's origin, which its sprite is drawn around.
    l_oP.setPen(QPen(l_oTc.m_oPivot, 2));
    l_oP.setBrush(Qt::NoBrush);
    l_oP.drawEllipse(l_oO, 4.5, 4.5);
    l_oP.drawLine(l_oO - QPointF(8, 0), l_oO + QPointF(8, 0));
    l_oP.drawLine(l_oO - QPointF(0, 8), l_oO + QPointF(0, 8));
    const bool l_bDragging = m_eDrag != Drag::None;
    auto l_fnPen = [&](const QColor& l_oC, bool l_bActive) { return QPen(l_bActive ? l_oC.lighter(140) : l_oC, l_bActive ? 3 : 2); };
    switch (m_eTool) {
    case Tool::Move: {
        const QPointF l_oEx = l_oO + l_oG.m_oParentX * g_dArrowLen, l_oEy = l_oO + l_oG.m_oParentY * g_dArrowLen;
        l_oP.setPen(l_fnPen(l_oTc.m_oGizmoX, m_eDrag == Drag::MoveX));
        l_oP.setBrush(l_oTc.m_oGizmoX);
        l_oP.drawLine(l_oO + l_oG.m_oParentX * 12, l_oEx);
        f_DrawArrowHead(l_oP, l_oEx + l_oG.m_oParentX * 4, l_oG.m_oParentX, 12);
        l_oP.setPen(l_fnPen(l_oTc.m_oGizmoY, m_eDrag == Drag::MoveY));
        l_oP.setBrush(l_oTc.m_oGizmoY);
        l_oP.drawLine(l_oO + l_oG.m_oParentY * 12, l_oEy);
        f_DrawArrowHead(l_oP, l_oEy + l_oG.m_oParentY * 4, l_oG.m_oParentY, 12);
        QColor l_oFill = l_oTc.m_oSelection;
        l_oFill.setAlpha(m_eDrag == Drag::MoveFree ? 160 : 70);
        l_oP.setPen(QPen(l_oTc.m_oSelection, 1.5));
        l_oP.setBrush(l_oFill);
        l_oP.drawRect(QRectF(l_oO - QPointF(7, 7), QSizeF(14, 14)));
        break;
    }
    case Tool::Rotate:
        l_oP.setPen(l_fnPen(l_oTc.m_oGizmoRotate, m_eDrag == Drag::Rotate));
        l_oP.setBrush(Qt::NoBrush);
        l_oP.drawEllipse(l_oO, g_dRotateRadius, g_dRotateRadius);
        l_oP.drawLine(l_oO, l_oO + l_oG.m_oNodeX * g_dRotateRadius);
        if (l_bDragging) f_DrawLabel(l_oP, l_oO + QPointF(g_dRotateRadius + 6, -10), QStringLiteral("%1°").arg(m_dAngleSum, 0, 'f', 1));
        break;
    case Tool::Scale: {
        const QPointF l_oEx = l_oO + l_oG.m_oNodeX * g_dScaleLen, l_oEy = l_oO + l_oG.m_oNodeY * g_dScaleLen;
        l_oP.setPen(l_fnPen(l_oTc.m_oGizmoX, m_eDrag == Drag::ScaleX));
        l_oP.setBrush(l_oTc.m_oGizmoX);
        l_oP.drawLine(l_oO, l_oEx);
        l_oP.drawRect(QRectF(l_oEx - QPointF(g_dHandle, g_dHandle), QSizeF(2 * g_dHandle, 2 * g_dHandle)));
        l_oP.setPen(l_fnPen(l_oTc.m_oGizmoY, m_eDrag == Drag::ScaleY));
        l_oP.setBrush(l_oTc.m_oGizmoY);
        l_oP.drawLine(l_oO, l_oEy);
        l_oP.drawRect(QRectF(l_oEy - QPointF(g_dHandle, g_dHandle), QSizeF(2 * g_dHandle, 2 * g_dHandle)));
        QColor l_oFill = l_oTc.m_oSelection;
        l_oFill.setAlpha(m_eDrag == Drag::ScaleUniform ? 160 : 70);
        l_oP.setPen(QPen(l_oTc.m_oSelection, 1.5));
        l_oP.setBrush(l_oFill);
        l_oP.drawRect(QRectF(l_oO - QPointF(7, 7), QSizeF(14, 14)));
        break;
    }
    }
}

// ---- interaction --------------------------------------------------------------------------------

AnimViewport::Drag AnimViewport::f_HandleAt(const QPointF& l_oW, const Gizmo& l_oG) const
{
    if (!l_oG.m_bValid) return Drag::None;
    const QPointF l_oO = l_oG.m_oOrigin;
    const auto l_fnWithin = [&](const QPointF& l_oC, double l_dR) { return std::hypot(l_oW.x() - l_oC.x(), l_oW.y() - l_oC.y()) <= l_dR; };
    switch (m_eTool) {
    case Tool::Move:
        if (l_fnWithin(l_oO, 9)) return Drag::MoveFree;
        if (f_DistToSegment(l_oW, l_oO + l_oG.m_oParentX * 12, l_oO + l_oG.m_oParentX * (g_dArrowLen + 4)) < 6) return Drag::MoveX;
        if (f_DistToSegment(l_oW, l_oO + l_oG.m_oParentY * 12, l_oO + l_oG.m_oParentY * (g_dArrowLen + 4)) < 6) return Drag::MoveY;
        break;
    case Tool::Rotate: {
        const double l_dD = std::hypot(l_oW.x() - l_oO.x(), l_oW.y() - l_oO.y());
        if (std::fabs(l_dD - g_dRotateRadius) < 7) return Drag::Rotate;
        break;
    }
    case Tool::Scale:
        if (l_fnWithin(l_oO + l_oG.m_oNodeX * g_dScaleLen, g_dHandle + 3)) return Drag::ScaleX;
        if (l_fnWithin(l_oO + l_oG.m_oNodeY * g_dScaleLen, g_dHandle + 3)) return Drag::ScaleY;
        if (l_fnWithin(l_oO, 9)) return Drag::ScaleUniform;
        break;
    }
    return Drag::None;
}

int AnimViewport::f_ItemAt(const QPointF& l_oW) const
{
    const QPointF l_oC = f_ToContent(l_oW);
    for (int l_iI = int(m_vItems.size()) - 1; l_iI >= 0; l_iI--)
        if (f_InQuad(f_Corners(m_vItems[size_t(l_iI)].m_oQuad), l_oC)) return l_iI;
    return -1;
}

QPointF AnimViewport::f_ParentPointAt(const NodePath& l_vParent, const QPointF& l_oContentPos) const
{
    const toms::anim::Clip* l_pClip = m_pDoc->f_DrawClip();   // m_poses are the drawn copy's
    if (!l_pClip) return l_oContentPos;
    const Node* l_pPn = f_NodeAt(l_pClip->m_oRoot, l_vParent);
    for (const NodePose& l_oPose : m_vPoses)
        if (l_oPose.m_pNode == l_pPn) {
            bool l_bOk = false;
            const QTransform l_oInv = f_ToQt(l_oPose.m_mat4World).inverted(&l_bOk);
            return l_bOk ? l_oInv.map(l_oContentPos) : l_oContentPos;
        }
    return l_oContentPos;
}

void AnimViewport::mousePressEvent(QMouseEvent* l_pE)
{
    if (f_PanPress(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton) return;
    setFocus();
    f_Rebuild();
    if (m_eMode == EditMode::Weights) {   // a joint picks its bone; anywhere else paints the selected bone
        const QString l_oJoint = f_BoneAt(l_pE->position(), true);
        if (!l_oJoint.isEmpty()) {
            m_pDoc->f_SelectNodeByName(l_oJoint);
            return;
        }
        m_ePaintMode = (l_pE->modifiers() & Qt::ShiftModifier) ? animed::WeightBrush::Smooth
                      : (l_pE->modifiers() & Qt::ControlModifier) ? animed::WeightBrush::Subtract : animed::WeightBrush::Add;
        m_bPainting = true;
        m_iDragSerial++;
        emit f_DragStarted();
        m_oLastDab = l_pE->position();
        f_PaintWeightsAt(l_pE->position(), m_ePaintMode, m_iDragSerial);
        update();
        return;
    }
    Gizmo l_oG = f_Gizmo();
    Drag l_eD = f_HandleAt(l_pE->position(), l_oG);
    if (l_eD == Drag::None && m_eMode == EditMode::Lattice) {   // Lattice mode: a point of the selected node's lattice
        const int l_iPoint = f_LatticePointAt(l_pE->position());
        toms::anim::LatticeView l_oLv;
        if (l_iPoint >= 0 && f_SelectedLattice(l_oLv)) {
            m_iDragPoint = l_iPoint;
            m_oDragStart = l_pE->position();
            m_mapDragStartDeform.clear();
            for (size_t l_nI = 0; l_nI < l_oLv.m_vOffsets.size(); l_nI++)
                if (l_oLv.m_vOffsets[l_nI] != glm::vec2(0.0f)) m_mapDragStartDeform[int(l_nI)] = l_oLv.m_vOffsets[l_nI];
            // content -> sprite pixels: the inverse of the lattice's own direction and scale on screen
            const glm::mat2 l_mat2ToContent(glm::vec2(l_oLv.m_mat3SpriteToPlaced[0]), glm::vec2(l_oLv.m_mat3SpriteToPlaced[1]));
            m_mat2DragToSprite = std::fabs(glm::determinant(l_mat2ToContent)) > 1e-9f ? glm::inverse(l_mat2ToContent) : glm::mat2(1.0f);
            m_iDragSerial++;
            emit f_DragStarted();   // playback pauses: the key goes to the frame on screen
            update();
            return;
        }
    }
    if (l_eD == Drag::None && m_eMode == EditMode::Bones) {   // Bones mode: only bones are picked, never sprites
        const QString l_oBone = f_BoneAt(l_pE->position());
        if (l_oBone.isEmpty() || !m_pDoc->f_SelectNodeByName(l_oBone)) return;
        if (m_eTool != Tool::Move) return;
        f_Rebuild();
        l_oG = f_Gizmo();
        l_eD = Drag::MoveFree;   // drag a bone to move it
    }
    if (l_eD == Drag::None) {
        const int l_iHit = f_ItemAt(l_pE->position());
        if (l_iHit < 0) return;
        m_pDoc->f_SelectNode(m_vItems[size_t(l_iHit)].m_vPath);
        if (m_eTool != Tool::Move) return;
        f_Rebuild();
        l_oG = f_Gizmo();
        l_eD = Drag::MoveFree;   // drag a sprite to move it
    }
    const Node* l_pSel = m_pDoc->f_SelectedNode();
    if (!l_pSel || !l_oG.m_bValid) return;
    emit f_DragStarted();   // playback pauses first: the drag edits the frame on screen
    m_eDrag = l_eD;
    m_oDragStart = l_pE->position();
    m_oDragGizmo = l_oG;
    const float l_fT = m_pDoc->f_Time();
    m_oStartPos = f_ValueAt(*l_pSel, Channel::Pos, l_fT);
    m_oStartRot = f_ValueAt(*l_pSel, Channel::Rot, l_fT);
    m_oStartScale = f_ValueAt(*l_pSel, Channel::Scale, l_fT);
    m_dLastAngle = std::atan2(m_oDragStart.y() - l_oG.m_oOrigin.y(), m_oDragStart.x() - l_oG.m_oOrigin.x()) * 180.0 / g_dPi;
    m_dAngleSum = 0;
    m_iDragSerial++;
    update();
}

void AnimViewport::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (f_PanMove(l_pE)) return;
    m_oCursor = l_pE->position();
    if (m_eMode == EditMode::Weights) {
        if (m_bPainting) {   // a dab every fifth of the brush along the way
            const double l_dStep = std::max(2.0, m_dBrushRadius * 0.2);
            const QPointF l_oD = l_pE->position() - m_oLastDab;
            if (std::hypot(l_oD.x(), l_oD.y()) >= l_dStep) {
                m_oLastDab = l_pE->position();
                f_PaintWeightsAt(l_pE->position(), m_ePaintMode, m_iDragSerial);
            }
        } else {
            m_ePaintMode = (l_pE->modifiers() & Qt::ShiftModifier) ? animed::WeightBrush::Smooth
                          : (l_pE->modifiers() & Qt::ControlModifier) ? animed::WeightBrush::Subtract : animed::WeightBrush::Add;
            f_Rebuild();
            const QString l_oJoint = f_BoneAt(l_pE->position(), true);
            m_oHoverBone = l_oJoint;
            setCursor(l_oJoint.isEmpty() ? Qt::CrossCursor : Qt::PointingHandCursor);
        }
        update();
        return;
    }
    if (m_iDragPoint >= 0) {   // Lattice mode: the point follows the mouse; a key at the playhead
        const QTransform l_oInv = f_ViewTransform().inverted();
        const QPointF l_oC0 = l_oInv.map(m_oDragStart), l_oC1 = l_oInv.map(l_pE->position());
        glm::vec2 l_v2Move = m_mat2DragToSprite * glm::vec2(float(l_oC1.x() - l_oC0.x()), float(l_oC1.y() - l_oC0.y()));
        if (m_bSnap) l_v2Move = glm::round(l_v2Move);
        animed::Value l_oV;
        l_oV.m_mapD = m_mapDragStartDeform;
        l_oV.m_mapD[m_iDragPoint] = (m_mapDragStartDeform.count(m_iDragPoint) ? m_mapDragStartDeform[m_iDragPoint] : glm::vec2(0.0f)) + l_v2Move;
        if (l_oV.m_mapD[m_iDragPoint] == glm::vec2(0.0f)) l_oV.m_mapD.erase(m_iDragPoint);
        m_pDoc->f_SetChannel(Channel::Deform, l_oV, QStringLiteral("lattice.%1").arg(m_iDragSerial));
        return;
    }
    if (m_eDrag != Drag::None) {
        f_ApplyDrag(l_pE->position(), l_pE->modifiers());
        return;
    }
    f_Rebuild();
    if (m_eMode == EditMode::Lattice) {
        const int l_iPoint = f_LatticePointAt(l_pE->position());
        if (l_iPoint != m_iHoverPoint) {
            m_iHoverPoint = l_iPoint;
            update();
        }
        if (l_iPoint >= 0) { setCursor(Qt::SizeAllCursor); return; }
    }
    if (m_eMode == EditMode::Bones) {
        const QString l_oBone = f_BoneAt(l_pE->position());
        const Drag l_eH = f_HandleAt(l_pE->position(), f_Gizmo());
        setCursor(l_eH != Drag::None ? Qt::PointingHandCursor : !l_oBone.isEmpty() ? (m_eTool == Tool::Move ? Qt::SizeAllCursor : Qt::PointingHandCursor)
                                                                             : Qt::ArrowCursor);
        if (l_oBone != m_oHoverBone || m_iHover != -1) {
            m_oHoverBone = l_oBone;
            m_iHover = -1;
            update();
        }
        return;
    }
    const int l_iHover = f_ItemAt(l_pE->position());
    const Drag l_eH = f_HandleAt(l_pE->position(), f_Gizmo());
    setCursor(l_eH != Drag::None ? Qt::PointingHandCursor : l_iHover >= 0 && m_eTool == Tool::Move ? Qt::SizeAllCursor : Qt::ArrowCursor);
    if (l_iHover != m_iHover) {
        m_iHover = l_iHover;
        update();
    }
}

void AnimViewport::mouseReleaseEvent(QMouseEvent* l_pE)
{
    if (f_PanRelease(l_pE)) return;
    if (l_pE->button() == Qt::LeftButton && m_bPainting) {
        m_bPainting = false;
        update();
        return;
    }
    if (l_pE->button() == Qt::LeftButton && m_iDragPoint >= 0) {
        m_iDragPoint = -1;
        update();
        return;
    }
    if (l_pE->button() == Qt::LeftButton && m_eDrag != Drag::None) {
        m_eDrag = Drag::None;
        update();
    }
}

void AnimViewport::leaveEvent(QEvent* l_pE)
{
    m_oCursor = QPointF(-1e9, -1e9);   // the brush and the point readout go with the cursor
    if (m_eMode == EditMode::Weights) update();
    GameCanvasView::leaveEvent(l_pE);
}

void AnimViewport::f_ApplyDrag(const QPointF& l_oW, Qt::KeyboardModifiers l_oMods)
{
    const Gizmo& l_oG = m_oDragGizmo;
    const QString l_oKey = QStringLiteral("#drag%1").arg(m_iDragSerial);
    switch (m_eDrag) {
    case Drag::MoveFree:
    case Drag::MoveX:
    case Drag::MoveY: {
        bool l_bOk = false;
        const QTransform l_oInv = l_oG.m_oParentToContent.inverted(&l_bOk);
        if (!l_bOk) return;
        QPointF l_oDelta = l_oInv.map(f_ToContent(l_oW)) - l_oInv.map(f_ToContent(m_oDragStart));
        if (m_eDrag == Drag::MoveX) l_oDelta.setY(0);
        if (m_eDrag == Drag::MoveY) l_oDelta.setX(0);
        Value l_oV = m_oStartPos;
        l_oV.m_v4V.x += float(l_oDelta.x());
        l_oV.m_v4V.y += float(l_oDelta.y());
        if (m_bSnap || (l_oMods & Qt::ControlModifier)) {
            l_oV.m_v4V.x = std::round(l_oV.m_v4V.x);
            l_oV.m_v4V.y = std::round(l_oV.m_v4V.y);
        }
        m_pDoc->f_SetChannel(Channel::Pos, l_oV, l_oKey);
        break;
    }
    case Drag::Rotate: {
        const double l_dA = std::atan2(l_oW.y() - l_oG.m_oOrigin.y(), l_oW.x() - l_oG.m_oOrigin.x()) * 180.0 / g_dPi;
        double l_dStep = l_dA - m_dLastAngle;
        while (l_dStep > 180) l_dStep -= 360;
        while (l_dStep < -180) l_dStep += 360;
        m_dAngleSum += l_dStep;
        m_dLastAngle = l_dA;
        double l_dDelta = l_oG.m_dParentDet < 0 ? -m_dAngleSum : m_dAngleSum;   // a mirroring parent turns the other way
        if (l_oMods & Qt::ShiftModifier) l_dDelta = std::round(l_dDelta / 15) * 15;
        else if (m_bSnap) l_dDelta = std::round(l_dDelta);
        Value l_oV = m_oStartRot;
        l_oV.m_v4V.z += float(l_dDelta);   // the screen-plane turn (rot z); x / y tilts are typed in Properties
        m_pDoc->f_SetChannel(Channel::Rot, l_oV, l_oKey);
        update();
        break;
    }
    case Drag::ScaleX:
    case Drag::ScaleY:
    case Drag::ScaleUniform: {
        Value l_oV = m_oStartScale;
        if (m_eDrag == Drag::ScaleUniform) {
            const double l_dF = std::exp(((l_oW.x() - m_oDragStart.x()) - (l_oW.y() - m_oDragStart.y())) / 150.0);
            l_oV.m_v4V.x *= float(l_dF);
            l_oV.m_v4V.y *= float(l_dF);
            l_oV.m_v4V.z *= float(l_dF);   // uniform: all three
        } else {
            const QPointF l_oU = m_eDrag == Drag::ScaleX ? l_oG.m_oNodeX : l_oG.m_oNodeY;
            double l_dP0 = f_Dot(m_oDragStart - l_oG.m_oOrigin, l_oU);
            if (std::fabs(l_dP0) < 4) l_dP0 = l_dP0 < 0 ? -4 : 4;
            const float l_fF = float(f_Dot(l_oW - l_oG.m_oOrigin, l_oU) / l_dP0);
            if (m_eDrag == Drag::ScaleX) l_oV.m_v4V.x *= l_fF;
            else l_oV.m_v4V.y *= l_fF;
            if (l_oMods & Qt::ShiftModifier) {   // Shift: keep the proportions
                if (m_eDrag == Drag::ScaleX) l_oV.m_v4V.y = m_oStartScale.m_v4V.y * l_fF;
                else l_oV.m_v4V.x = m_oStartScale.m_v4V.x * l_fF;
            }
        }
        if (m_bSnap) {
            l_oV.m_v4V.x = std::round(l_oV.m_v4V.x * 20) / 20;
            l_oV.m_v4V.y = std::round(l_oV.m_v4V.y * 20) / 20;
        }
        m_pDoc->f_SetChannel(Channel::Scale, l_oV, l_oKey);
        break;
    }
    case Drag::None: break;
    }
}

void AnimViewport::dragEnterEvent(QDragEnterEvent* l_pE)
{
    if (l_pE->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) l_pE->acceptProposedAction();
}

void AnimViewport::dragMoveEvent(QDragMoveEvent* l_pE)
{
    if (l_pE->mimeData()->hasFormat(QLatin1String(g_pszSpriteMime))) l_pE->acceptProposedAction();
}

void AnimViewport::dropEvent(QDropEvent* l_pE)
{
    const QStringList l_oNames = QString::fromUtf8(l_pE->mimeData()->data(QLatin1String(g_pszSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (l_oNames.isEmpty() || !m_pDoc->f_Clip()) return;
    f_Rebuild();
    // Under the selected node, at the drop point in that node's space.
    const NodePath l_vParent = m_pDoc->f_SelectedNode() ? m_pDoc->f_SelectedPath() : NodePath();
    QPointF l_oLocal = f_ParentPointAt(l_vParent, f_ToContent(l_pE->position()));
    if (m_bSnap) l_oLocal = QPointF(std::round(l_oLocal.x()), std::round(l_oLocal.y()));
    m_pDoc->f_AddSpriteNode(l_vParent, l_oNames.first(), glm::vec2(float(l_oLocal.x()), float(l_oLocal.y())));
    l_pE->acceptProposedAction();
    setFocus();
}

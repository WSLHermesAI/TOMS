// StageCanvas.cpp -- see StageCanvas.h.
#include "StageCanvas.h"

#include "SpriteBank.h"
#include "StageDraw.h"
#include "StageEditor.h"
#include "Theme.h"
#include "stage_checks.h"

#include "anim_clip.h"
#include "anim_player.h"
#include "bgfx_renderer.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

using stage::Cell;
using stage::Json;

namespace {
constexpr int g_iTS = 32;   // the game's tile size (manifest "tile")
const float g_arrWhite[4] = {1, 1, 1, 1};
bool f_IsWhite(const float l_arrT[4]) { return l_arrT[0] == 1 && l_arrT[1] == 1 && l_arrT[2] == 1 && l_arrT[3] == 1; }
}  // namespace

StageCanvas::StageCanvas(StageEditor* l_pEditor, QWidget* l_pParent)
    : GameCanvasView(GameCanvasView::f_GameRendererWanted(QStringLiteral("StageEventEditor")), l_pParent), m_pEd(l_pEditor) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 160);
    m_oClock.start();
    // animations: repaint while clips are on screen (the game renderer only; QPainter draws still sprites)
    m_oTick.setInterval(33);
    connect(&m_oTick, &QTimer::timeout, this, [this] { if (m_iAnimated && isVisible() && f_UsingGameRenderer()) update(); });
    m_oTick.start();
}

StageCanvas::~StageCanvas() { f_ReleaseGameRenderer(); }

QSize StageCanvas::f_ContentSize() const {
    const stage::StageDoc* l_pD = m_pEd->f_CurDoc();
    return l_pD ? QSize(l_pD->f_Width() * g_iTS, l_pD->f_Height() * g_iTS) : QSize(g_iTS * 8, g_iTS * 8);
}

QRectF StageCanvas::f_CellRect(int l_iX, int l_iY, int l_iW, int l_iH) const { return f_ToWidget(QRectF(l_iX * g_iTS, l_iY * g_iTS, l_iW * g_iTS, l_iH * g_iTS)); }

std::optional<Cell> StageCanvas::f_CellAt(const QPointF& l_oWp) const {
    const stage::StageDoc* l_pD = m_pEd->f_CurDoc();
    if (!l_pD) return std::nullopt;
    const QPointF l_oC = f_ToContent(l_oWp);
    const int l_iX = (int)std::floor(l_oC.x() / g_iTS), l_iY = (int)std::floor(l_oC.y() / g_iTS);
    if (!l_pD->f_Inside(l_iX, l_iY)) return std::nullopt;
    return Cell{l_iX, l_iY};
}

void StageCanvas::f_ShowCell(const Cell& l_oC) { f_CenterOn(QRectF(l_oC.m_iX * g_iTS - g_iTS * 3, l_oC.m_iY * g_iTS - g_iTS * 3, g_iTS * 7, g_iTS * 7)); update(); }

void StageCanvas::f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) {
    m_pRen = l_pRen;
    m_mapAnimTex.clear();
    if (!l_pRen) { m_upTex.reset(); return; }
    m_upTex = std::make_unique<GameTextures>();
    m_upTex->f_Attach(l_pRen);
    m_bAtlasDirty = true;
}

// ---- the game renderer -----------------------------------------------------------------------------------------------

void StageCanvas::f_DrawSprite(const GameScene& l_oS, const QString& l_oSprite, const QRectF& l_oR, const float l_arrTint[4]) {
    if (!m_upTex) return;
    uint16_t l_u16Tex = g_u16SpriteAtlasTexture;
    const toms::AtlasRegion* l_pReg = m_upTex->f_AtlasSet().f_Find(l_oSprite.toStdString(), &l_u16Tex);
    if (!l_pReg) return;
    Quad l_oQ;
    l_oQ.m_arrRect[0] = float(l_oR.x()); l_oQ.m_arrRect[1] = float(l_oR.y()); l_oQ.m_arrRect[2] = float(l_oR.width()); l_oQ.m_arrRect[3] = float(l_oR.height());
    for (int l_iI = 0; l_iI < 4; l_iI++) { l_oQ.m_arrUv[l_iI] = l_pReg->m_arrUv[l_iI]; l_oQ.m_arrTint[l_iI] = l_arrTint[l_iI]; }
    l_oQ.m_u16Texture = l_u16Tex;
    l_oS.m_oRen.f_DrawSprite(l_oQ);
}

bool StageCanvas::f_DrawClip(const GameScene& l_oS, const std::string& l_strRef, const QRectF& l_oR, int l_iTx, int l_iTy, const float l_arrTint[4]) {
    const SpriteBank::Anim* l_pOwner = nullptr;
    const toms::anim::Clip* l_pClip = m_pEd->f_Bank().f_Clip(l_strRef, &l_pOwner);
    if (!l_pClip || !l_pOwner) return false;
    std::unique_ptr<GameTextures>& l_upTex = m_mapAnimTex[l_pOwner->m_strPath];
    if (!l_upTex) {
        l_upTex = std::make_unique<GameTextures>();
        l_upTex->f_Attach(m_pRen);
        std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>> l_vList;
        for (const auto& [l_strId, l_strPath, l_spAtlas] : l_pOwner->m_vAtlases) l_vList.emplace_back(l_strId, l_strPath, l_spAtlas.get());
        l_upTex->f_SetAtlases(l_vList);
    }
    // as Game::drawObjectAnim: each tile its own phase, so a row of slimes does not move in step
    const float l_fLen = l_pClip->f_Duration();
    const double l_dPhase = (double)(((unsigned)l_iTx * 73856093u ^ (unsigned)l_iTy * 19349663u) % 997u) / 997.0 * l_fLen;
    const float l_fT = l_fLen > 0 ? (float)std::fmod(m_oClock.elapsed() / 1000.0 + l_dPhase, (double)l_fLen) : 0.0f;
    std::vector<toms::anim::NodePose> l_vPoses;
    toms::anim::f_Evaluate(*l_pClip, l_fT, l_vPoses);
    // authored around a 32x32 sprite with its pivot at the bottom centre: placed on the rect's bottom centre, scaled to it
    const glm::mat3 l_mat3Place = toms::anim::f_Flatten(toms::anim::f_Compose(
        {float(l_oR.x() + l_oR.width() * 0.5), float(l_oR.y() + l_oR.height()), 0}, glm::vec3(0), {float(l_oR.width() / g_iTS), float(l_oR.height() / g_iTS), 1}));
    std::vector<Quad> l_vQuads;
    toms::anim::f_AppendQuads(l_vPoses, l_upTex->f_AtlasSet(), l_mat3Place, f_IsWhite(l_arrTint) ? nullptr : l_arrTint, l_vQuads);
    for (const Quad& l_oQ : l_vQuads) l_oS.m_oRen.f_DrawSprite(l_oQ);
    if (!l_vQuads.empty()) m_iAnimated++;
    return !l_vQuads.empty();
}

namespace {
const Json* f_OwnLook(const Json& l_jO) { auto l_oIt = l_jO.find("look"); return l_oIt == l_jO.end() ? nullptr : &*l_oIt; }

// One frame's look-ups: each legend char's meaning and each type's look are found once, not for every cell (a JSON
// look-up per cell made a big floor slow to draw, more so in a Debug build).
struct Looks {
    const stage::Project& m_oP;
    const stage::StageDoc& m_oD;
    std::array<std::string, 256> m_arrSems;
    std::array<bool, 256> m_arrHave{};
    std::map<std::string, Json> m_mapTypes;
    Json m_jMerged;

    Looks(const stage::Project& l_oPr, const stage::StageDoc& l_oDoc) : m_oP(l_oPr), m_oD(l_oDoc) {}
    const std::string& f_SemAt(int l_iX, int l_iY) {
        const unsigned char l_u8C = (unsigned char)m_oD.f_At(l_iX, l_iY);
        if (!m_arrHave[l_u8C]) { m_arrSems[l_u8C] = m_oP.f_SemAt(m_oD, l_iX, l_iY); m_arrHave[l_u8C] = true; }
        return m_arrSems[l_u8C];
    }
    const Json& f_TypeLook(const std::string& l_strSem) {
        auto l_itIt = m_mapTypes.find(l_strSem);
        if (l_itIt == m_mapTypes.end()) l_itIt = m_mapTypes.emplace(l_strSem, m_oP.f_TypeLook(l_strSem)).first;
        return l_itIt->second;
    }
    // as Project::lookAt: the type's look with the object's own look over it
    const Json& f_LookAt(int l_iX, int l_iY, const std::string& l_strSem, bool* l_pOwnSprite = nullptr) {
        const Json& l_jBase = f_TypeLook(l_strSem);
        if (l_pOwnSprite) *l_pOwnSprite = false;
        const Json* l_pO = stage::Project::f_ObjOf(m_oD, l_iX, l_iY);
        const Json* l_pLk = l_pO ? f_OwnLook(*l_pO) : nullptr;
        if (!l_pLk || !l_pLk->is_object() || l_pLk->empty()) return l_jBase;
        if (l_pOwnSprite) *l_pOwnSprite = l_pLk->contains("sprite");
        m_jMerged = l_jBase;
        for (auto& [l_strK, l_jV] : l_pLk->items()) m_jMerged[l_strK] = l_jV;
        return m_jMerged;
    }
};
}  // namespace

StageCanvas::Ghost StageCanvas::f_Ghost() const {
    Ghost l_oGh;
    stage::StageDoc* l_pD = m_pEd->f_CurDoc();
    const StageEditor::View& l_oV = m_pEd->f_View();
    if (!l_pD || !l_oV.m_optHover || f_IsPanning()) return l_oGh;
    const stage::Project& l_oP = m_pEd->f_Project();
    if (l_oV.m_optMoveFrom) {
        if (*l_oV.m_optMoveFrom == *l_oV.m_optHover) return l_oGh;
        l_oGh.m_strSem = l_oP.f_SemAt(*l_pD, l_oV.m_optMoveFrom->m_iX, l_oV.m_optMoveFrom->m_iY);
        l_oGh.m_oFrom = *l_oV.m_optMoveFrom;
        l_oGh.m_jLook = l_oP.f_LookAt(*l_pD, l_oV.m_optMoveFrom->m_iX, l_oV.m_optMoveFrom->m_iY, l_oGh.m_strSem);   // its own look: sprite, tint, animation
        const Json* l_pO = stage::Project::f_ObjOf(*l_pD, l_oV.m_optMoveFrom->m_iX, l_oV.m_optMoveFrom->m_iY);
        const Json* l_pLk = l_pO ? f_OwnLook(*l_pO) : nullptr;
        l_oGh.m_bOwnSprite = l_pLk && l_pLk->is_object() && l_pLk->contains("sprite");
    } else if ((l_oV.m_eTool == StageEditor::Paint || (l_oV.m_eTool == StageEditor::Rect && !l_oV.m_optRectA)) && stage::Project::f_IsEntity(l_oV.m_strBrush)) {
        l_oGh.m_strSem = l_oV.m_strBrush;
        l_oGh.m_oFrom = *l_oV.m_optHover;
        l_oGh.m_jLook = l_oP.f_TypeLook(l_oGh.m_strSem);
    }
    if (l_oGh.m_strSem.empty()) return l_oGh;
    const Cell l_oH = *l_oV.m_optHover;
    l_oGh.m_oChk = l_oP.f_PlaceCheck(*l_pD, l_oH.m_iX, l_oH.m_iY, l_oGh.m_strSem, l_oV.m_optMoveFrom ? &*l_oV.m_optMoveFrom : nullptr);
    int l_iFw = 1, l_iFh = 1;
    for (const Cell& l_oC : l_oGh.m_oChk.m_vCells) { l_iFw = std::max(l_iFw, l_oC.m_iX - l_oH.m_iX + 1); l_iFh = std::max(l_iFh, l_oC.m_iY - l_oH.m_iY + 1); }
    l_oGh.m_oRect = f_CellRect(l_oH.m_iX, l_oH.m_iY, l_iFw, l_iFh);
    l_oGh.m_bShow = true;
    return l_oGh;
}

void StageCanvas::f_DrawEntity(const GameScene& l_oS, const std::string& l_strSem, const Json& l_jLook, bool l_bOwnSprite, const QRectF& l_oR, int l_iTx, int l_iTy,
                             float l_fAlpha) {
    if (l_strSem.rfind("event:", 0) == 0) {   // the story plate (game_scene_draw.cpp)
        l_oS.f_Rect(l_oR, QColor(219, 184, 92, int(235 * l_fAlpha)));
        const double l_dI = std::min(l_oR.width(), l_oR.height()) * 0.22;
        l_oS.f_Rect(l_oR.adjusted(l_dI, l_dI, -l_dI, -l_dI), QColor(71, 87, 117, int(242 * l_fAlpha)));
        return;
    }
    float l_arrTint[4];
    StageDraw::f_LookTint(l_jLook, l_arrTint);
    l_arrTint[3] *= l_fAlpha;
    if (l_jLook.contains("anim") && l_jLook["anim"].is_string() && f_DrawClip(l_oS, l_jLook["anim"].get<std::string>(), l_oR, l_iTx, l_iTy, l_arrTint)) return;
    if (l_strSem.rfind("monster:", 0) == 0 && !l_bOwnSprite && f_DrawClip(l_oS, "anim/monster_idle.anim#idle_" + l_strSem.substr(8), l_oR, l_iTx, l_iTy, l_arrTint)) return;
    f_DrawSprite(l_oS, StageDraw::f_LookSprite(m_pEd->f_Project(), m_pEd->f_Bank(), l_strSem, l_jLook), l_oR, l_arrTint);
    float l_arrWash[4];
    if (StageDraw::f_GateWash(m_pEd->f_Project(), l_strSem, l_arrWash)) l_oS.f_Rect(l_oR, QColor::fromRgbF(l_arrWash[0], l_arrWash[1], l_arrWash[2], l_arrWash[3] * l_fAlpha));
}

void StageCanvas::f_PaintGameScene(const GameScene& l_oS) {
    m_iAnimated = 0;
    stage::StageDoc* l_pD = m_pEd->f_CurDoc();
    if (!l_pD || !m_upTex) return;
    stage::Project& l_oP = m_pEd->f_Project();
    if (m_bAtlasDirty) {
        m_bAtlasDirty = false;
        m_upTex->f_SetAtlases({{"game", m_pEd->f_Bank().f_AtlasPath().toStdString(), &m_pEd->f_Bank().f_Atlas()}});
    }
    Looks l_oL(l_oP, *l_pD);
    const QColor l_oBg = QColor(0x12, 0x13, 0x16);
    l_oS.f_Rect(QRectF(0, 0, width(), height()), l_oBg);
    // ground first, then objects in row order (a 2-wide monster spills to the right, like the game)
    const QString l_oWall = QString::fromStdString(l_oP.f_SpriteOf("wall")), l_oFloor = QString::fromStdString(l_oP.f_SpriteOf("floor"));   // themed
    for (int l_iY = 0; l_iY < l_pD->f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_pD->f_Width(); l_iX++)
            f_DrawSprite(l_oS, l_oL.f_SemAt(l_iX, l_iY) == "wall" ? l_oWall : l_oFloor, f_CellRect(l_iX, l_iY), g_arrWhite);
    for (int l_iY = 0; l_iY < l_pD->f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_pD->f_Width(); l_iX++) {
            const std::string& l_strSem = l_oL.f_SemAt(l_iX, l_iY);
            if (!stage::Project::f_IsEntity(l_strSem)) continue;
            const stage::Footprint l_oFp = l_strSem.rfind("monster:", 0) == 0 ? l_oP.f_FootprintOf(*l_pD, l_pD->f_At(l_iX, l_iY), l_strSem) : stage::Footprint{};
            bool l_bOwnSprite = false;
            const Json& l_jLook = l_oL.f_LookAt(l_iX, l_iY, l_strSem, &l_bOwnSprite);
            f_DrawEntity(l_oS, l_strSem, l_jLook, l_bOwnSprite, f_CellRect(l_iX, l_iY, l_oFp.m_iW, l_oFp.m_iH), l_iX, l_iY);
        }
    // the ghost: drawn by the game renderer too, so a moved object keeps its own sprite and its animation plays
    const Ghost l_oGh = f_Ghost();
    if (l_oGh.m_bShow && !l_oGh.m_oChk.m_bSame) f_DrawEntity(l_oS, l_oGh.m_strSem, l_oGh.m_jLook, l_oGh.m_bOwnSprite, l_oGh.m_oRect, l_oGh.m_oFrom.m_iX, l_oGh.m_oFrom.m_iY, 0.6f);
}

// ---- QPainter: the map without a GPU, and the editor's marks ---------------------------------------------------------

void StageCanvas::f_PaintSceneQt(QPainter& l_oG) {
    stage::StageDoc* l_pD = m_pEd->f_CurDoc();
    l_oG.fillRect(rect(), QColor(0x12, 0x13, 0x16));
    if (!l_pD) return;
    stage::Project& l_oP = m_pEd->f_Project();
    Looks l_oL(l_oP, *l_pD);
    for (int l_iY = 0; l_iY < l_pD->f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_pD->f_Width(); l_iX++)
            StageDraw::f_DrawSem(l_oG, l_oP, m_pEd->f_Bank(), l_oL.f_SemAt(l_iX, l_iY) == "wall" ? "wall" : "floor", f_CellRect(l_iX, l_iY));
    for (int l_iY = 0; l_iY < l_pD->f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_pD->f_Width(); l_iX++) {
            const std::string& l_strSem = l_oL.f_SemAt(l_iX, l_iY);
            if (!stage::Project::f_IsEntity(l_strSem)) continue;
            const stage::Footprint l_oFp = l_strSem.rfind("monster:", 0) == 0 ? l_oP.f_FootprintOf(*l_pD, l_pD->f_At(l_iX, l_iY), l_strSem) : stage::Footprint{};
            StageDraw::f_DrawSem(l_oG, l_oP, m_pEd->f_Bank(), l_strSem, f_CellRect(l_iX, l_iY, l_oFp.m_iW, l_oFp.m_iH), l_oL.f_LookAt(l_iX, l_iY, l_strSem));
        }
}

void StageCanvas::f_PaintContent(QPainter& l_oG) {
    if (!f_OverGameScene()) f_PaintSceneQt(l_oG);
    if (m_pEd->f_CurDoc()) f_PaintMarks(l_oG);
}

void StageCanvas::f_PaintMarks(QPainter& l_oG) {
    stage::StageDoc& l_oD = *m_pEd->f_CurDoc();
    stage::Project& l_oP = m_pEd->f_Project();
    const StageEditor::View& l_oV = m_pEd->f_View();
    const double l_dS = g_iTS * f_Zoom();
    l_oG.setRenderHint(QPainter::Antialiasing, false);
    const bool l_bMarks = !l_oV.m_bGameView;
    const std::vector<char> l_vReach = l_bMarks ? stage::f_ReachableCells(l_oP, l_oD) : std::vector<char>();
    Looks l_oL(l_oP, l_oD);
    for (int l_iY = 0; l_iY < l_oD.f_Height(); l_iY++)
        for (int l_iX = 0; l_iX < l_oD.f_Width(); l_iX++) {
            const std::string& l_strSem = l_oL.f_SemAt(l_iX, l_iY);
            const QRectF l_oR = f_CellRect(l_iX, l_iY);
            if (!stage::Project::f_IsEntity(l_strSem)) {
                if (l_strSem.empty() && l_bMarks) l_oG.fillRect(l_oR, QColor(255, 93, 93, 140));   // a char with no legend entry
                continue;
            }
            if (!l_bMarks) continue;
            // ✎ its own look, ▶ an animation, ⚡n linked events
            const Json* l_pO = stage::Project::f_ObjOf(l_oD, l_iX, l_iY);
            const Json& l_jLook = l_oL.f_LookAt(l_iX, l_iY, l_strSem);
            std::vector<std::pair<QString, QColor>> l_vBadges;
            if (l_pO && l_pO->contains("look") && (*l_pO)["look"].is_object() && !(*l_pO)["look"].empty()) l_vBadges.push_back({QStringLiteral("✎"), QColor("#4c9eff")});
            if (l_jLook.contains("anim")) l_vBadges.push_back({QStringLiteral("▶"), QColor("#4cd18a")});
            if (l_pO && l_pO->contains("events") && (*l_pO)["events"].is_array() && !(*l_pO)["events"].empty())
                l_vBadges.push_back({QStringLiteral("⚡%1").arg((*l_pO)["events"].size()), QColor("#ffd24c")});
            if (!l_vBadges.empty()) {
                QFont l_oF = font();
                l_oF.setBold(true);
                const int l_iFs = std::max(9, int(std::lround(l_dS * 0.22)));
                l_oF.setPixelSize(l_iFs);
                l_oG.setFont(l_oF);
                for (size_t l_nI = 0; l_nI < l_vBadges.size(); l_nI++) {
                    const double l_dTw = l_oG.fontMetrics().horizontalAdvance(l_vBadges[l_nI].first) + 4;
                    const QRectF l_oB(l_oR.x() + 1, l_oR.bottom() - (double)(l_nI + 1) * (l_iFs + 3), l_dTw, l_iFs + 2);
                    l_oG.fillRect(l_oB, QColor(0, 0, 0, 166));
                    l_oG.setPen(l_vBadges[l_nI].second);
                    l_oG.drawText(l_oB.adjusted(2, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, l_vBadges[l_nI].first);
                }
            }
            if (l_strSem.rfind("event:", 0) == 0) {   // the event's kind colour in the corner; red when no pool defines it
                const stage::Event* l_pE = l_oP.f_Event(l_strSem.substr(6));
                l_oG.setRenderHint(QPainter::Antialiasing, true);
                l_oG.setPen(Qt::NoPen);
                l_oG.setBrush(l_pE ? StageDraw::f_KindColor(l_oP, l_pE->m_strKind) : QColor("#ff5d5d"));
                l_oG.drawEllipse(QPointF(l_oR.right() - 6, l_oR.top() + 6), 4, 4);
                l_oG.setBrush(Qt::NoBrush);
                l_oG.setRenderHint(QPainter::Antialiasing, false);
            }
            if (!l_vReach.empty() && !l_vReach[(size_t)l_iY * l_oD.f_Width() + l_iX] && l_strSem != "player_start") {
                QPen l_oPen(QColor(255, 159, 67, 230), 2, Qt::DashLine);
                l_oG.setPen(l_oPen);
                l_oG.drawRect(l_oR.adjusted(2, 2, -2, -2));
            }
        }
    if ((l_oV.m_bShowSafe || l_oV.m_optPlaceMode) && l_bMarks) {
        for (const Cell& l_oC : stage::f_SafeCells(l_oP, m_pEd->f_CurId())) {
            const QRectF l_oR = f_CellRect(l_oC.m_iX, l_oC.m_iY).adjusted(3, 3, -3, -3);
            l_oG.fillRect(l_oR, QColor(76, 209, 138, 56));
            l_oG.setPen(QPen(QColor(76, 209, 138, 140), 1));
            l_oG.drawRect(l_oR);
        }
    }
    if (l_oV.m_bShowGrid && l_bMarks) {
        l_oG.setPen(QPen(QColor(255, 255, 255, 20), 1));
        const QRectF l_oAll = f_CellRect(0, 0, l_oD.f_Width(), l_oD.f_Height());
        for (int l_iX = 0; l_iX <= l_oD.f_Width(); l_iX++) l_oG.drawLine(QPointF(std::round(l_oAll.left() + l_iX * l_dS) + 0.5, l_oAll.top()), QPointF(std::round(l_oAll.left() + l_iX * l_dS) + 0.5, l_oAll.bottom()));
        for (int l_iY = 0; l_iY <= l_oD.f_Height(); l_iY++) l_oG.drawLine(QPointF(l_oAll.left(), std::round(l_oAll.top() + l_iY * l_dS) + 0.5), QPointF(l_oAll.right(), std::round(l_oAll.top() + l_iY * l_dS) + 0.5));
    }
    if (l_oV.m_optRectA) {
        const int l_iX0 = std::min(l_oV.m_optRectA->m_iX, l_oV.m_optRectB->m_iX), l_iY0 = std::min(l_oV.m_optRectA->m_iY, l_oV.m_optRectB->m_iY);
        const int l_iX1 = std::max(l_oV.m_optRectA->m_iX, l_oV.m_optRectB->m_iX), l_iY1 = std::max(l_oV.m_optRectA->m_iY, l_oV.m_optRectB->m_iY);
        const QRectF l_oR = f_CellRect(l_iX0, l_iY0, l_iX1 - l_iX0 + 1, l_iY1 - l_iY0 + 1);
        l_oG.fillRect(l_oR, QColor(76, 158, 255, 56));
        l_oG.setPen(QPen(QColor("#4c9eff"), 2));
        l_oG.drawRect(l_oR.adjusted(1, 1, -1, -1));
    }
    for (const Cell& l_oC : l_oV.m_vFlash) {
        l_oG.setPen(QPen(QColor("#ff5d5d"), 3));
        l_oG.drawRect(f_CellRect(l_oC.m_iX, l_oC.m_iY).adjusted(1.5, 1.5, -1.5, -1.5));
    }
    // the brush's ghost under the mouse: green where it can go, red (and why) where it cannot. With the game renderer
    // the ghost itself is drawn in paintGameScene (animated); only the frame and the red cells are painted here.
    const Ghost l_oGh = f_Ghost();
    if (l_oGh.m_bShow) {
        const stage::PlaceCheck& l_oChk = l_oGh.m_oChk;
        if (!l_oChk.m_bSame && !f_OverGameScene()) StageDraw::f_DrawSem(l_oG, l_oP, m_pEd->f_Bank(), l_oGh.m_strSem, l_oGh.m_oRect, l_oGh.m_jLook, 0.6);
        if (!l_oChk.m_bOk) for (const Cell& l_oC : l_oChk.m_vCells) l_oG.fillRect(f_CellRect(l_oC.m_iX, l_oC.m_iY), QColor(255, 60, 60, 115));
        l_oG.setPen(QPen(l_oChk.m_bOk ? QColor("#4cd18a") : QColor("#ff3c3c"), 3));
        l_oG.drawRect(l_oGh.m_oRect.adjusted(1.5, 1.5, -1.5, -1.5));
        setCursor(l_oChk.m_bOk ? Qt::CrossCursor : Qt::ForbiddenCursor);
        m_pEd->f_PlaceHint(l_oChk);
    } else {
        setCursor(l_oV.m_eTool == StageEditor::Select || l_oV.m_eTool == StageEditor::Edit ? Qt::ArrowCursor : Qt::CrossCursor);
        if (l_oV.m_optHover && l_bMarks) {
            l_oG.setPen(QPen(QColor(255, 255, 255, 153), 1));
            l_oG.drawRect(f_CellRect(l_oV.m_optHover->m_iX, l_oV.m_optHover->m_iY).adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
    if (l_oV.m_optSel && l_bMarks) {
        l_oG.setPen(QPen(QColor("#ffd24c"), 2.5));
        l_oG.drawRect(f_CellRect(l_oV.m_optSel->m_iX, l_oV.m_optSel->m_iY).adjusted(1.5, 1.5, -1.5, -1.5));
    }
}

// ---- mouse -------------------------------------------------------------------------------------------------------

void StageCanvas::mousePressEvent(QMouseEvent* l_pE) {
    setFocus();
    if (f_PanPress(l_pE)) return;
    if (l_pE->button() != Qt::LeftButton) return;
    if (const auto l_optC = f_CellAt(l_pE->position())) m_pEd->f_CanvasPress(*l_optC);
}

void StageCanvas::mouseMoveEvent(QMouseEvent* l_pE) {
    if (f_PanMove(l_pE)) return;
    m_pEd->f_CanvasHover(f_CellAt(l_pE->position()), (l_pE->buttons() & Qt::LeftButton) != 0);
}

void StageCanvas::mouseReleaseEvent(QMouseEvent* l_pE) {
    if (f_PanRelease(l_pE)) return;
    if (l_pE->button() == Qt::LeftButton) m_pEd->f_CanvasRelease();
}

void StageCanvas::leaveEvent(QEvent* l_pE) {
    GameCanvasView::leaveEvent(l_pE);
    m_pEd->f_CanvasHover(std::nullopt, false);
}

// a right click that did not pan: the eyedropper (the cell's object becomes the brush)
void StageCanvas::contextMenuEvent(QContextMenuEvent* l_pE) {
    if (const auto l_optC = f_CellAt(l_pE->pos())) m_pEd->f_Eyedropper(*l_optC);
}

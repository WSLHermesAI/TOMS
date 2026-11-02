// anim_editor -- the Qt editor for .anim node animations (docs/engine/05_ANIMATION.md):
//
//   anim_editor                                   the editor
//   anim_editor file.anim                         the editor with that file open
//   anim_editor --headless check file.anim        parse + check (sprites vs atlas, keys); exit 0 / 2
//   anim_editor --selftest file.anim outdir       automated check (-platform offscreen); outdir must be
//                                                 on the atlas's drive (atlas paths are stored relative)
//   anim_editor --render file.anim clip outdir t1,t2,... [held.anim]
//                                                 the clip at those times through the game renderer, PNGs
//                                                 (with a held file: its clip of the same name at `hand`)
//   anim_editor --export file.anim outdir [gif|apng|png] [clip,clip,...] [held.anim]
//                                                 clips as animated pictures (File > Export Clips as Animation)
#include "AnimAtlasesPanel.h"
#include "AnimDocument.h"
#include "AnimClipsDock.h"
#include "AutoBackup.h"
#include "AnimEditor.h"
#include "AnimExport.h"
#include "AnimKeyListDock.h"
#include "AnimMainWindow.h"
#include "AnimNodesDock.h"
#include "AnimProblemsDock.h"
#include "AnimPropertiesDock.h"
#include "AnimSpritesDock.h"
#include "AnimTransport.h"
#include "AnimViewport.h"
#include "BezierDialog.h"
#include "Console.h"
#include "Icons.h"
#include "Language.h"
#include "Theme.h"
#include "anim_check.h"
#include "anim_player.h"
#include "bgfx_host.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QScrollBar>
#include <QScrollArea>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>

#include <cmath>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif
#include <memory>

using namespace animed;
using toms::anim::Node;

namespace {

// The image a window presents (the game renderer's swap chain included), also behind other windows: PrintWindow
// with PW_RENDERFULLCONTENT reads it from the compositor. A null image where that is not available.
QImage f_PresentedImage(QWidget* l_pW)
{
#ifdef _WIN32
    // PrintWindow asks the window to paint, and a new frame would hide what was on the screen before it.
    auto* l_pCanvas = qobject_cast<GameCanvasView*>(l_pW);
    if (l_pCanvas) l_pCanvas->f_HoldFrames(true);
    struct Release { GameCanvasView* m_pC; ~Release() { if (m_pC) m_pC->f_HoldFrames(false); } } l_oRelease{l_pCanvas};
    const HWND l_pHwnd = reinterpret_cast<HWND>(l_pW->winId());
    RECT l_oRc{};
    if (!GetClientRect(l_pHwnd, &l_oRc) || l_oRc.right <= 0 || l_oRc.bottom <= 0) return QImage();
    const int l_iWidth = l_oRc.right, l_iHeight = l_oRc.bottom;
    HDC l_pScreenDc = GetDC(nullptr);
    HDC l_pDc = CreateCompatibleDC(l_pScreenDc);
    HBITMAP l_pBmp = CreateCompatibleBitmap(l_pScreenDc, l_iWidth, l_iHeight);
    HGDIOBJ l_pOld = SelectObject(l_pDc, l_pBmp);
    const bool l_bPrinted = PrintWindow(l_pHwnd, l_pDc, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != 0;
    QImage l_oImg(l_iWidth, l_iHeight, QImage::Format_RGB32);
    BITMAPINFO l_oBi{};
    l_oBi.bmiHeader.biSize = sizeof(l_oBi.bmiHeader);
    l_oBi.bmiHeader.biWidth = l_iWidth;
    l_oBi.bmiHeader.biHeight = -l_iHeight;   // top-down
    l_oBi.bmiHeader.biPlanes = 1;
    l_oBi.bmiHeader.biBitCount = 32;
    l_oBi.bmiHeader.biCompression = BI_RGB;
    SelectObject(l_pDc, l_pOld);
    const bool l_bRead = GetDIBits(l_pDc, l_pBmp, 0, UINT(l_iHeight), l_oImg.bits(), &l_oBi, DIB_RGB_COLORS) == l_iHeight;
    DeleteObject(l_pBmp);
    DeleteDC(l_pDc);
    ReleaseDC(nullptr, l_pScreenDc);
    return l_bPrinted && l_bRead ? l_oImg : QImage();
#else
    (void)l_pW;
    return QImage();
#endif
}

// The game atlas's coin as packed (trimmed: smaller than its 32 x 32 image).
QSize f_GameCoinSize(const AnimDocument* l_pDoc)
{
    const toms::AtlasRegion* l_pR = l_pDoc->f_AtlasCount() > 0 ? l_pDoc->f_AtlasAt(0).m_oAtlas.f_Find("coin") : nullptr;
    return l_pR ? QSize(l_pR->m_iW, l_pR->m_iH) : QSize();
}

void f_ProcessEvents()
{
    for (int l_iI = 0; l_iI < 3; l_iI++) QCoreApplication::processEvents();
}

bool f_Check(bool l_bOk, const char* l_pszWhat)
{
    std::printf("selftest: %-58s %s\n", l_pszWhat, l_bOk ? "ok" : "FAILED");
    return l_bOk;
}

int f_RowOf(const AnimKeyListDock* l_pKeys, float l_fT)
{
    const std::vector<float> l_vTimes = l_pKeys->f_RowTimes();
    for (size_t l_nI = 0; l_nI < l_vTimes.size(); l_nI++)
        if (f_SameTime(l_vTimes[l_nI], l_fT)) return int(l_nI);
    return -1;
}

// The viewport's quads vs one appendQuads() call over the same poses, and the QPainter transform
// of each quad vs its corners: the editor draws exactly what the game would.
bool f_CheckDrawing(AnimEditor* l_pEd, float l_fT)
{
    AnimDocument* l_pDoc = l_pEd->f_Document();
    std::vector<toms::anim::NodePose> l_vPoses;
    std::vector<AnimViewport::DrawItem> l_vItems;
    l_pEd->f_Viewport()->f_BuildFrame(l_fT, l_vPoses, l_vItems, nullptr);
    std::vector<toms::anim::NodePose> l_vRef;
    toms::anim::f_Evaluate(*l_pDoc->f_Clip(), l_fT, l_vRef);
    std::vector<Quad> l_vQuads;
    toms::anim::f_AppendQuads(l_vRef, l_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), nullptr, l_vQuads);
    bool l_bOk = l_vItems.size() == l_vQuads.size() && !l_vQuads.empty();
    double l_dMaxCorner = 0, l_dMaxMap = 0;
    for (size_t l_nI = 0; l_bOk && l_nI < l_vItems.size(); l_nI++) {
        const Quad& l_oA = l_vItems[l_nI].m_oQuad;
        const Quad& l_oB = l_vQuads[l_nI];
        for (int l_iK = 0; l_iK < 8; l_iK++) l_dMaxCorner = std::max(l_dMaxCorner, double(std::fabs(l_oA.m_arrCorners[l_iK] - l_oB.m_arrCorners[l_iK])));
        for (int l_iK = 0; l_iK < 4; l_iK++) l_dMaxCorner = std::max(l_dMaxCorner, double(std::fabs(l_oA.m_arrTint[l_iK] - l_oB.m_arrTint[l_iK]) + std::fabs(l_oA.m_arrUv[l_iK] - l_oB.m_arrUv[l_iK])));
        l_bOk = l_bOk && l_oA.m_bAdditive == l_oB.m_bAdditive && l_oA.m_u16Texture == l_oB.m_u16Texture;
        // The page image the viewport cuts the sprite from is the one the quad's texture names.
        int l_iPage = -1;
        l_bOk = l_bOk && l_vItems[l_nI].m_pImages && l_vItems[l_nI].m_pImages == l_pDoc->f_PageImages(l_oA.m_u16Texture, &l_iPage) && l_vItems[l_nI].m_iPage == l_iPage;
        // The transform QPainter draws the sprite's pixel rect with must land on the 4 corners.
        const QTransform l_oTr = AnimViewport::f_QuadTransform(l_oA, l_vItems[l_nI].m_oSrc.size());
        const QSizeF l_oS = l_vItems[l_nI].m_oSrc.size();
        const QPointF l_arrSrc[4] = {QPointF(0, 0), QPointF(l_oS.width(), 0), QPointF(l_oS.width(), l_oS.height()), QPointF(0, l_oS.height())};
        for (int l_iK = 0; l_iK < 4; l_iK++) {
            const QPointF l_oM = l_oTr.map(l_arrSrc[l_iK]);
            l_dMaxMap = std::max(l_dMaxMap, std::hypot(l_oM.x() - l_oA.m_arrCorners[l_iK * 2], l_oM.y() - l_oA.m_arrCorners[l_iK * 2 + 1]));
        }
        const toms::anim::Clip* l_pClip = l_pDoc->f_Clip();
        const Node* l_pN = f_NodeAt(l_pClip->m_oRoot, l_vItems[l_nI].m_vPath);
        std::printf("selftest:   t=%.2f %-8s %s corners (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) (%.2f,%.2f) tint a=%.2f\n", l_fT,
                    l_pN ? l_pN->m_strName.c_str() : "?", l_oA.m_bAdditive ? "add " : "norm", l_oA.m_arrCorners[0], l_oA.m_arrCorners[1], l_oA.m_arrCorners[2], l_oA.m_arrCorners[3],
                    l_oA.m_arrCorners[4], l_oA.m_arrCorners[5], l_oA.m_arrCorners[6], l_oA.m_arrCorners[7], l_oA.m_arrTint[3]);
    }
    std::printf("selftest:   %zu quads, max corner/uv/tint difference %.6f, max transform error %.6f px\n", l_vItems.size(), l_dMaxCorner, l_dMaxMap);
    return l_bOk && l_dMaxCorner < 1e-4 && l_dMaxMap < 1e-3;
}

// The page and pixels the viewport draws the node `name` with at time t (nullptr: not drawn).
const SpriteImageCache* f_DrawnFrom(AnimEditor* l_pEd, const char* l_pszName, float l_fT, QRect* l_pSrc)
{
    AnimDocument* l_pDoc = l_pEd->f_Document();
    std::vector<toms::anim::NodePose> l_vPoses;
    std::vector<AnimViewport::DrawItem> l_vItems;
    l_pEd->f_Viewport()->f_BuildFrame(l_fT, l_vPoses, l_vItems, nullptr);
    for (const AnimViewport::DrawItem& l_oIt : l_vItems)
        if (const Node* l_pN = f_NodeAt(l_pDoc->f_Clip()->m_oRoot, l_oIt.m_vPath); l_pN && l_pN->m_strName == l_pszName) {
            if (l_pSrc) *l_pSrc = l_oIt.m_oSrc;
            return l_oIt.m_pImages;
        }
    return nullptr;
}

bool f_HasProblemIn(AnimDocument* l_pDoc, Problem::Level l_eLevel, const QString& l_oA, const QString& l_oB)
{
    for (const Problem& l_oP : f_CheckAnim(l_pDoc->f_File(), l_pDoc->f_CheckAtlases())) {
        const QString l_oM = QString::fromStdString(l_oP.m_strMessage);
        if (l_oP.m_eLevel == l_eLevel && l_oM.contains(l_oA) && l_oM.contains(l_oB)) return true;
    }
    return false;
}

// (d) Sprite references with atlas ids (the file has game + extra; extra has spark and its own
// coin): every pick stores "id:name" -- the exact copy picked, also the duplicate coin of the
// later atlas -- the viewport draws that atlas's page, sprite keys can switch atlases, ids can be
// renamed (rewriting the references), bare names qualified, broken references are errors.
bool f_AtlasReferences(AnimEditor* l_pEd, const QString& l_oWorkDir, const QString& l_oOutDir, AnimMainWindow& l_oW)
{
    AnimDocument* l_pDoc = l_pEd->f_Document();
    AnimSpritesDock* l_pSprites = l_pEd->f_SpritesDock();
    AnimKeyListDock* l_pKeys = l_pEd->f_KeyListDock();
    bool l_bOk = true;
    const QString l_oStart = l_pDoc->f_ToJson();
    const SpriteImageCache* l_pGamePage = &l_pDoc->f_AtlasAt(0).m_oImages;
    const SpriteImageCache* l_pExtraPage = &l_pDoc->f_AtlasAt(1).m_oImages;
    const int l_iBase = l_pDoc->f_UndoStack()->index();
    auto l_fnSteps = [&] { return l_pDoc->f_UndoStack()->index() - l_iBase; };
    auto l_fnUndoAll = [&] {
        while (l_pDoc->f_UndoStack()->index() > l_iBase) l_pDoc->f_UndoStack()->undo();
        f_ProcessEvents();
        return l_pDoc->f_ToJson() == l_oStart;
    };
    const Node* l_pGem = nullptr;
    auto l_fnSelectGem = [&] {
        l_pDoc->f_SelectNodeByName(QStringLiteral("gem"));
        f_ProcessEvents();
        l_pGem = l_pDoc->f_SelectedNode();
        return l_pGem && l_pGem->m_strName == "gem";
    };
    QRect l_oSrc;

    // The dock: both coins are listed and pickable; the later copy is not greyed.
    QListWidgetItem* l_pGameCoin = l_pSprites->f_SpriteItem(0, QStringLiteral("coin"));
    QListWidgetItem* l_pExtraCoin = l_pSprites->f_SpriteItem(1, QStringLiteral("coin"));
    QListWidgetItem* l_pExtraSpark = l_pSprites->f_SpriteItem(1, QStringLiteral("spark"));
    l_bOk &= f_Check(l_pGameCoin && l_pExtraCoin && l_pExtraSpark && (l_pExtraCoin->flags() & Qt::ItemIsEnabled) &&
                    l_pExtraCoin->foreground() == l_pGameCoin->foreground() && l_pExtraCoin->toolTip().contains(QStringLiteral("usable")),
                "(d) Sprites dock: extra's duplicate coin is listed, enabled, not greyed");
    if (!l_pGameCoin || !l_pExtraCoin || !l_pExtraSpark || !l_fnSelectGem()) return false;
    std::unique_ptr<QMimeData> l_upMime(l_pSprites->f_ListFor(1)->mimeData({l_pExtraCoin}));
    l_bOk &= f_Check(l_upMime && l_upMime->text() == QLatin1String("extra:coin"), "(d) dragging extra's coin carries \"extra:coin\"");

    // 1. Double-click extra's coin (auto-key off: the rest sprite).
    l_pDoc->f_SetAutoKey(false);
    emit l_pSprites->f_ListFor(1)->itemDoubleClicked(l_pExtraCoin);
    f_ProcessEvents();
    l_fnSelectGem();
    l_bOk &= f_Check(l_pGem->m_strSprite == "extra:coin" && l_fnSteps() == 1, "(d) double-click extra's coin: gem.sprite = \"extra:coin\" (1 step)");
    l_bOk &= f_Check(f_DrawnFrom(l_pEd, "gem", 0.0f, &l_oSrc) == l_pExtraPage && l_oSrc == QRect(16, 0, 16, 16),
                "(d) viewport: gem drawn from extra's page (16,0 16x16)");
    l_bOk &= f_Check(f_CheckDrawing(l_pEd, 0.0f), "(d) viewport quads == appendQuads");
    l_bOk &= f_Check(l_pDoc->f_SpriteDisplay(l_pGem->m_strSprite) == QLatin1String("coin (extra)") &&
                    l_pDoc->f_SpriteDisplay("coin") == QLatin1String("coin (auto: game)") && l_pDoc->f_SpriteDisplay("") == QLatin1String("(none)"),
                "(d) shown as 'coin (extra)'; a bare name as 'coin (auto: game)'");
    // The Nodes dock's Sprite column.
    bool l_bNodesShow = false;
    if (auto* l_pTree = l_pEd->f_NodesDock()->findChild<QTreeWidget*>())
        for (QTreeWidgetItemIterator l_oIt(l_pTree); *l_oIt; ++l_oIt)
            if ((*l_oIt)->text(0) == QLatin1String("gem")) l_bNodesShow = (*l_oIt)->text(1) == QLatin1String("coin (extra)");
    l_bOk &= f_Check(l_bNodesShow, "(d) Nodes dock: gem's sprite column shows 'coin (extra)'");
    l_bOk &= f_Check(l_pDoc->f_AddSpriteNode(NodePath(), l_upMime->text(), glm::vec2(40, 0)) && l_pDoc->f_SelectedNode()->m_strSprite == "extra:coin",
                "(d) dropping it adds a node with \"extra:coin\"");
    l_bOk &= f_Check(l_fnUndoAll(), "(d) undone");

    // 1b. Several sprites dropped on the scrubber (MPDI-style sequence): one sprite key each,
    // 0.1 s apart from the time under the cursor, in one undo step; the same on the key list.
    if (!l_fnSelectGem()) return false;
    {
        auto l_upSeq = std::make_unique<QMimeData>();
        l_upSeq->setData(QLatin1String(AnimSpriteList::m_pszMime), QByteArrayLiteral("game:coin\nextra:spark\nextra:coin"));
        AnimScrubber* l_pScrubber = l_pEd->f_Transport()->f_Scrubber();
        const QPointF l_oAt(8, l_pScrubber->height() / 2);   // the track's left end: t = 0
        QDragEnterEvent l_oEnter(l_oAt.toPoint(), Qt::CopyAction, l_upSeq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(l_pScrubber, &l_oEnter);
        QDropEvent l_oDrop(l_oAt, Qt::CopyAction, l_upSeq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(l_pScrubber, &l_oDrop);
        f_ProcessEvents();   // the drop is handled after the drag's event loop
        l_pGem = l_pDoc->f_SelectedNode();
        auto l_fnKeyAt = [&](size_t l_nI, float l_fT, const char* l_pszV) {
            return l_pGem->m_vSpriteKeys.size() > l_nI && std::fabs(l_pGem->m_vSpriteKeys[l_nI].m_fT - l_fT) < 1e-4f && l_pGem->m_vSpriteKeys[l_nI].m_oV == l_pszV;
        };
        l_bOk &= f_Check(l_oEnter.isAccepted() && l_oDrop.isAccepted() && l_pGem->m_vSpriteKeys.size() == 3 && l_fnKeyAt(0, 0.0f, "game:coin") &&
                        l_fnKeyAt(1, 0.1f, "extra:spark") && l_fnKeyAt(2, 0.2f, "extra:coin") && l_fnSteps() == 1,
                    "(d) 3 sprites dropped on the scrubber: keys at 0 / 0.1 / 0.2 (1 step)");
        l_bOk &= f_Check(f_DrawnFrom(l_pEd, "gem", 0.15f, &l_oSrc) == l_pExtraPage && l_oSrc == QRect(0, 0, 16, 16), "(d) t=0.15: extra's spark");
        // On the key list: the row under the cursor gives the start time (the 0.1 s row).
        QTableWidget* l_pTable = l_pKeys->f_Table();
        f_ProcessEvents();
        const int l_iR = f_RowOf(l_pKeys, 0.1f);
        if (l_iR < 0) return f_Check(false, "(d) key list has a 0.1 s row");
        l_pTable->scrollToItem(l_pTable->item(l_iR, 0));
        const QRect l_oRowRect = l_pTable->visualItemRect(l_pTable->item(l_iR, 0));
        QDragEnterEvent l_oEnter2(l_oRowRect.center(), Qt::CopyAction, l_upSeq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(l_pTable->viewport(), &l_oEnter2);   // a drop goes to the widget that took the enter
        QDropEvent l_oDrop2(QPointF(l_oRowRect.center()), Qt::CopyAction, l_upSeq.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(l_pTable->viewport(), &l_oDrop2);
        f_ProcessEvents();
        l_pGem = l_pDoc->f_SelectedNode();
        std::printf("selftest:   key list drop at row %d (%d,%d %dx%d), accepted %d, steps %d, sprite keys:", l_iR, l_oRowRect.x(),
                    l_oRowRect.y(), l_oRowRect.width(), l_oRowRect.height(), int(l_oDrop2.isAccepted()), l_fnSteps());
        for (const auto& l_oK : l_pGem->m_vSpriteKeys) std::printf(" %.3f=%s", l_oK.m_fT, l_oK.m_oV.c_str());
        std::printf("\n");
        l_bOk &= f_Check(l_oDrop2.isAccepted() && l_pGem->m_vSpriteKeys.size() == 4 && l_fnKeyAt(1, 0.1f, "game:coin") &&
                        l_fnKeyAt(2, 0.2f, "extra:spark") && l_fnKeyAt(3, 0.3f, "extra:coin") && l_fnSteps() == 2,
                    "(d) dropped on key list row 0.1: keys replaced from 0.1, one more at 0.3 (1 step)");
        // Other drags are not taken.
        QMimeData l_oPlain;
        l_oPlain.setText(QStringLiteral("coin"));
        QDragEnterEvent l_oOther(l_oAt.toPoint(), Qt::CopyAction, &l_oPlain, Qt::LeftButton, Qt::NoModifier);
        l_oOther.ignore();
        QApplication::sendEvent(l_pScrubber, &l_oOther);
        l_bOk &= f_Check(!l_oOther.isAccepted(), "(d) a plain text drag is not taken by the scrubber");
    }
    l_bOk &= f_Check(l_fnUndoAll(), "(d) undone");

    // 2. The Properties sprite picker: "coin (extra)" from the list; a typed bare name gets the
    // atlas the lookup picks; a bare legacy name shown as "(auto: ...)" is left alone untouched.
    l_fnSelectGem();
    QComboBox* l_pCombo = l_pEd->f_PropertiesDock()->f_SpriteCombo();
    const int l_iIdx = l_pCombo->findText(QStringLiteral("coin (extra)"));
    l_bOk &= f_Check(l_iIdx > 0 && l_pCombo->findText(QStringLiteral("coin (game)")) > 0, "(d) Properties: both coins in the sprite list");
    l_pCombo->setCurrentIndex(l_iIdx);
    emit l_pCombo->activated(l_iIdx);
    f_ProcessEvents();
    l_fnSelectGem();
    l_bOk &= f_Check(l_pGem->m_strSprite == "extra:coin" && l_pCombo->currentText() == QLatin1String("coin (extra)"),
                "(d) Properties: picking 'coin (extra)' stores \"extra:coin\"");
    l_pCombo->setEditText(QStringLiteral("spark"));
    emit l_pCombo->lineEdit()->editingFinished();
    f_ProcessEvents();
    l_fnSelectGem();
    l_bOk &= f_Check(l_pGem->m_strSprite == "extra:spark", "(d) Properties: typing 'spark' stores \"extra:spark\"");
    l_bOk &= f_Check(l_fnUndoAll(), "(d) undone");
    l_pDoc->f_SelectNodeByName(QStringLiteral("coin"));
    f_ProcessEvents();
    const int l_iBefore = l_pDoc->f_UndoStack()->index();
    emit l_pCombo->lineEdit()->editingFinished();   // untouched "coin (auto: game)": nothing written
    l_bOk &= f_Check(l_pCombo->currentText() == QLatin1String("coin (auto: game)") && l_pDoc->f_UndoStack()->index() == l_iBefore,
                "(d) Properties: a bare name shows 'coin (auto: game)', left alone");

    // 3. Sprite keys alternating atlases (auto-key on: a key at the playhead per double-click).
    l_fnSelectGem();
    l_pDoc->f_SetAutoKey(true);
    const float l_arrTimes[3] = {0.0f, 0.2f, 0.4f};
    QListWidgetItem* l_arrPicks[3] = {l_pGameCoin, l_pExtraSpark, l_pExtraCoin};
    for (int l_iI = 0; l_iI < 3; l_iI++) {
        l_pDoc->f_SetTime(l_arrTimes[l_iI]);
        emit l_arrPicks[l_iI]->listWidget()->itemDoubleClicked(l_arrPicks[l_iI]);
        f_ProcessEvents();
    }
    l_fnSelectGem();
    l_bOk &= f_Check(l_pGem->m_vSpriteKeys.size() == 3 && l_pGem->m_vSpriteKeys[0].m_oV == "game:coin" && l_pGem->m_vSpriteKeys[1].m_oV == "extra:spark" &&
                    l_pGem->m_vSpriteKeys[2].m_oV == "extra:coin" && l_fnSteps() == 3,
                "(d) sprite keys 0 game:coin, 0.2 extra:spark, 0.4 extra:coin");
    l_bOk &= f_Check(f_DrawnFrom(l_pEd, "gem", 0.1f, &l_oSrc) == l_pGamePage && l_oSrc.size() == f_GameCoinSize(l_pDoc), "(d) t=0.1: game's coin (its packed pixels)");
    l_bOk &= f_Check(f_DrawnFrom(l_pEd, "gem", 0.3f, &l_oSrc) == l_pExtraPage && l_oSrc == QRect(0, 0, 16, 16), "(d) t=0.3: extra's spark");
    l_bOk &= f_Check(f_DrawnFrom(l_pEd, "gem", 0.5f, &l_oSrc) == l_pExtraPage && l_oSrc == QRect(16, 0, 16, 16), "(d) t=0.5: extra's coin");
    for (float l_fT : {0.1f, 0.3f, 0.5f}) l_bOk &= f_Check(f_CheckDrawing(l_pEd, l_fT), "(d) viewport quads == appendQuads");
    const int l_iR1 = f_RowOf(l_pKeys, 0.2f), l_iR2 = f_RowOf(l_pKeys, 0.4f);
    l_bOk &= f_Check(l_iR1 >= 0 && l_iR2 >= 0 && l_pKeys->f_Table()->item(l_iR1, AnimKeyListDock::ColSprite)->text() == QLatin1String("spark (extra)") &&
                    l_pKeys->f_Table()->item(l_iR2, AnimKeyListDock::ColSprite)->text() == QLatin1String("coin (extra)"),
                "(d) key list shows 'spark (extra)', 'coin (extra)'");
    l_bOk &= f_Check(l_pKeys->f_CommitCell(l_iR2, AnimKeyListDock::ColSprite, QStringLiteral("coin (game)")) && l_pDoc->f_SelectedNode()->m_vSpriteKeys[2].m_oV == "game:coin" &&
                    l_pKeys->f_CommitCell(l_iR2, AnimKeyListDock::ColSprite, QStringLiteral("spark")) && l_pDoc->f_SelectedNode()->m_vSpriteKeys[2].m_oV == "extra:spark",
                "(d) key list cell: 'coin (game)' -> game:coin, typed 'spark' -> extra:spark");
    l_pDoc->f_UndoStack()->undo();
    l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();
    l_fnSelectGem();
    l_pKeys->raise();
    l_pKeys->f_SelectRows({l_iR1});
    f_ProcessEvents();
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("keylist_atlases.png"))), "keylist_atlases.png");
    l_bOk &= f_Check(l_pKeys->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("keylist_sprites.png"))), "keylist_sprites.png");

    // 4. Rename extra's id through the Atlases list's cell: every "extra:" reference follows, one step.
    AnimAtlasesPanel* l_pPanel = l_pEd->f_PropertiesDock()->f_AtlasesPanel();
    const int l_iBeforeRename = l_pDoc->f_UndoStack()->index();
    if (QTreeWidgetItem* l_pIt = l_pPanel->f_List()->topLevelItem(1)) l_pIt->setText(AnimAtlasesPanel::ColId, QStringLiteral("fx"));
    f_ProcessEvents();
    l_fnSelectGem();
    l_bOk &= f_Check(l_pDoc->f_AtlasId(1) == QLatin1String("fx") && l_pGem->m_vSpriteKeys[1].m_oV == "fx:spark" && l_pGem->m_vSpriteKeys[2].m_oV == "fx:coin" &&
                    l_pGem->m_vSpriteKeys[0].m_oV == "game:coin" && l_pDoc->f_UndoStack()->index() == l_iBeforeRename + 1 &&
                    f_DrawnFrom(l_pEd, "gem", 0.5f, &l_oSrc) == l_pExtraPage &&
                    f_CountLevel(f_CheckAnim(l_pDoc->f_File(), l_pDoc->f_CheckAtlases()), Problem::Error) == 0,
                "(d) rename id extra -> fx: references rewritten (1 step), still drawn, no errors");
    QString l_oWhy;
    l_bOk &= f_Check(!l_pDoc->f_RenameAtlasId(1, QStringLiteral("a:b"), &l_oWhy) && l_oWhy.contains(QLatin1Char(':')) &&
                    !l_pDoc->f_RenameAtlasId(1, QStringLiteral("game"), &l_oWhy) && l_oWhy.contains(QStringLiteral("unique")) &&
                    !l_pDoc->f_RenameAtlasId(1, QString(), &l_oWhy) && l_pDoc->f_AtlasId(1) == QLatin1String("fx"),
                "(d) ids: ':' / taken / empty refused");
    l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();
    l_fnSelectGem();
    l_bOk &= f_Check(l_pDoc->f_AtlasId(1) == QLatin1String("extra") && l_pGem->m_vSpriteKeys[1].m_oV == "extra:spark" &&
                    l_pPanel->f_List()->topLevelItem(1)->text(AnimAtlasesPanel::ColId) == QLatin1String("extra"),
                "(d) rename undone");

    // 5. Removing extra: its references would break -- counted, and errors after.
    const int l_iRefs = l_pDoc->f_AtlasReferenceCount(1);
    l_bOk &= f_Check(l_iRefs == 3, "(d) extra is used by 3 references (spark node, 2 gem keys)");
    std::printf("selftest:   atlasReferenceCount(extra) = %d\n", l_iRefs);
    l_bOk &= f_Check(l_pPanel->f_RemoveAtlas(1) && f_HasProblemIn(l_pDoc, Problem::Error, QStringLiteral("'extra:coin'"), QStringLiteral("no atlas has the id")),
                "(d) remove extra: 'extra:coin' is an error");
    l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();

    // 6. Broken references are errors: an unknown id, a name the atlas lacks.
    l_fnSelectGem();
    l_pDoc->f_EditSelected(QStringLiteral("test"), [](Node& l_oN) { l_oN.m_strSprite = "nope:coin"; });
    l_pDoc->f_EditSelected(QStringLiteral("test"), [](Node& l_oN) { l_oN.m_vSpriteKeys[0].m_oV = "extra:slime"; });
    l_bOk &= f_Check(f_HasProblemIn(l_pDoc, Problem::Error, QStringLiteral("'nope:coin'"), QStringLiteral("no atlas has the id 'nope'")) &&
                    f_HasProblemIn(l_pDoc, Problem::Error, QStringLiteral("'extra:slime'"), QStringLiteral("has no sprite 'slime'")),
                "(d) check: unknown id and missing name are errors");
    for (const Problem& l_oP : f_CheckAnim(l_pDoc->f_File(), l_pDoc->f_CheckAtlases()))
        if (l_oP.m_eLevel == Problem::Error) std::printf("selftest:   error: %s: %s\n", l_oP.m_strWhere.c_str(), l_oP.m_strMessage.c_str());
    l_pDoc->f_UndoStack()->undo();
    l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();

    // 7. Qualify: every bare name gets the atlas the lookup picks now (coin -> game:coin), one step.
    const int l_iBare = l_pDoc->f_BareSpriteReferenceCount();
    const int l_iBeforeQualify = l_pDoc->f_UndoStack()->index();
    l_bOk &= f_Check(l_iBare > 0 && f_HasProblemIn(l_pDoc, Problem::Warning, QStringLiteral("'coin'"), QStringLiteral("does not name one")),
                "(d) bare names in the file; bare 'coin' warns");
    const int l_iQualified = l_pDoc->f_QualifySpriteReferences();
    std::vector<std::string> l_vAll;
    for (const toms::anim::Clip& l_oC : l_pDoc->f_File().m_vClips) f_CollectSpriteRefs(l_oC.m_oRoot, l_vAll);
    bool l_bAllQualified = !l_vAll.empty();
    for (const std::string& l_strR : l_vAll) l_bAllQualified = l_bAllQualified && !toms::anim::f_ParseSpriteRef(l_strR).m_strAtlas.empty();
    l_pDoc->f_SelectNodeByName(QStringLiteral("coin"));
    l_bOk &= f_Check(l_iQualified == l_iBare && l_bAllQualified && l_pDoc->f_SelectedNode()->m_strSprite == "game:coin" &&
                    l_pDoc->f_UndoStack()->index() == l_iBeforeQualify + 1 && l_pDoc->f_BareSpriteReferenceCount() == 0 &&
                    !f_HasProblemIn(l_pDoc, Problem::Warning, QStringLiteral("coin"), QStringLiteral("several atlases")),
                "(d) qualify: every reference names its atlas, coin -> game:coin, warning gone (1 step)");
    std::printf("selftest:   qualified %d bare reference(s)\n", l_iQualified);
    l_pDoc->f_UndoStack()->undo();
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_BareSpriteReferenceCount() == l_iBare, "(d) qualify undone");

    // 8. A second game.atlas (a style's): its default id is taken, so it gets the style folder's.
    {
        const QString l_oStyleDir = QDir(l_oWorkDir).filePath(QStringLiteral("styles/dark16/atlas"));
        QDir().mkpath(l_oStyleDir);
        QFile::remove(QDir(l_oStyleDir).filePath(QStringLiteral("game.atlas")));
        QFile::copy(QDir(l_oWorkDir).filePath(QStringLiteral("extra/extra.png")), QDir(l_oStyleDir).filePath(QStringLiteral("extra.png")));
        QFile::copy(QDir(l_oWorkDir).filePath(QStringLiteral("extra/extra.atlas")), QDir(l_oStyleDir).filePath(QStringLiteral("game.atlas")));
        const QString l_oStyle = QDir(l_oStyleDir).filePath(QStringLiteral("game.atlas"));
        l_bOk &= f_Check(l_pDoc->f_SuggestAtlasId(l_oStyle) == QLatin1String("dark16") && l_pEd->f_AddAtlasFile(l_oStyle) && l_pDoc->f_AtlasCount() == 3 &&
                        l_pDoc->f_AtlasId(2) == QLatin1String("dark16"),
                    "(d) a second game.atlas gets the id 'dark16'");
        QString l_oErr;
        l_bOk &= f_Check(!l_pDoc->f_AddAtlas(l_oStyle, QStringLiteral("x"), &l_oErr), "(d) the same file twice is refused");
        const toms::anim::AnimFile l_oF = l_pDoc->f_File();
        l_bOk &= f_Check(l_oF.m_vAtlases.size() == 3 && l_oF.m_vAtlases[2].m_strId == "dark16" &&
                        l_pDoc->f_ToJson().contains(QStringLiteral("\"id\": \"dark16\"")),
                    "(d) stored as {\"id\": \"dark16\", \"path\": ...}");
        l_pDoc->f_UndoStack()->undo();
        f_ProcessEvents();
    }

    // 9. Save with the alternating keys, reopen: the same file.
    const QString l_oSaved = l_pDoc->f_FilePath();
    const QString l_oJson = l_pDoc->f_ToJson();
    l_bOk &= f_Check(l_pEd->f_SaveTo(l_oSaved), "(d) save");
    std::string l_strText;
    l_bOk &= f_Check(f_ReadTextFile(l_oSaved.toStdString(), l_strText) && l_strText.find("\"extra:coin\"") != std::string::npos &&
                    l_strText.find("\"extra:spark\"") != std::string::npos,
                "(d) the file stores \"extra:coin\", \"extra:spark\"");
    l_bOk &= f_Check(l_pEd->f_OpenFile(l_oSaved, false) && l_pDoc->f_ToJson() == l_oJson && l_pDoc->f_AtlasId(1) == QLatin1String("extra"),
                "(d) reopen: animToJson equal");
    l_bOk &= f_Check(l_fnSelectGem() && f_DrawnFrom(l_pEd, "gem", 0.5f, &l_oSrc) == &l_pDoc->f_AtlasAt(1).m_oImages && l_oSrc == QRect(16, 0, 16, 16),
                "(d) reopened: t=0.5 draws extra's coin");
    std::fflush(stdout);
    l_bOk &= f_Check(f_HeadlessMain({"check", l_oSaved.toStdString()}) == 0, "(d) --headless check on it: exit 0");
    return l_bOk;
}

int f_RunSelfTest(AnimMainWindow& l_oW, const QString& l_oFile, const QString& l_oOutDir)
{
    qApp->setProperty("toms.selftest", true);
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // a crash still leaves the lines before it
    AnimEditor* l_pEd = l_oW.f_Editor();
    AnimDocument* l_pDoc = l_pEd->f_Document();
    QDir().mkpath(l_oOutDir);
    bool l_bOk = true;
    if (!l_pEd->f_OpenFile(l_oFile, false)) {
        std::fprintf(stderr, "selftest: cannot open %s\n", l_oFile.toUtf8().constData());
        return 2;
    }
    // Work on a copy in outDir (it must be on the atlases' drive: atlas paths are stored relative).
    const QString l_oWorkDir = QDir(l_oOutDir).filePath(QStringLiteral("work"));
    QDir(l_oWorkDir).removeRecursively();
    QDir().mkpath(l_oWorkDir);
    const QString l_oWork = QDir(l_oWorkDir).filePath(QFileInfo(l_oFile).fileName());
    if (!l_pEd->f_SaveTo(l_oWork) || !l_pEd->f_OpenFile(l_oWork, false)) {
        std::fprintf(stderr, "selftest: cannot copy %s to %s (outdir must be on the same drive as the atlas)\n",
                     l_oFile.toUtf8().constData(), l_oWork.toUtf8().constData());
        return 2;
    }
    l_pDoc->f_SetAutoKey(true);
    l_pDoc->f_SetKeyTogether(true);
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_AtlasLoaded() && l_pDoc->f_AtlasCount() == 1, "atlas loaded (the scratch copy finds it)");
    l_bOk &= f_Check(!l_pDoc->f_SpriteNames().isEmpty(), "sprites listed");
    const QString l_oOriginal = l_pDoc->f_ToJson();

    // 1. The viewport draws what the game draws.
    l_bOk &= f_Check(f_CheckDrawing(l_pEd, 0.76f), "viewport quads == appendQuads at t=0.76");
    l_bOk &= f_Check(f_CheckDrawing(l_pEd, 0.0f), "viewport quads == appendQuads at t=0");

    // 2. Select a node, add keys through the UI's own paths, undo them.
    l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("slime")), "select node 'slime'");
    f_ProcessEvents();
    AnimKeyListDock* l_pKeys = l_pEd->f_KeyListDock();
    const int l_iRowsBefore = l_pKeys->f_Table()->rowCount();
    std::vector<int> l_vTracksBefore;   // key count per interpolated channel
    for (Channel l_eC : g_arrInterpolated) l_vTracksBefore.push_back(f_KeyCount(*l_pDoc->f_SelectedNode(), l_eC));
    l_pDoc->f_SetTime(0.25f);
    l_pKeys->f_InsertKey();   // the Insert button / K
    f_ProcessEvents();
    const Node* l_pSlime = l_pDoc->f_SelectedNode();
    bool l_bOnlyAnimated = l_pSlime != nullptr;
    for (size_t l_nI = 0; l_pSlime && l_nI < 4; l_nI++) {
        const Channel l_eC = g_arrInterpolated[l_nI];
        const bool l_bAnimated = l_vTracksBefore[l_nI] > 0;
        l_bOnlyAnimated &= l_bAnimated ? f_HasKeyAt(*l_pSlime, l_eC, 0.25f) && f_KeyCount(*l_pSlime, l_eC) == l_vTracksBefore[l_nI] + 1
                                 : f_KeyCount(*l_pSlime, l_eC) == 0;
    }
    l_bOk &= f_Check(l_bOnlyAnimated && l_pKeys->f_Table()->rowCount() == l_iRowsBefore + 1,
                "insert key at 0.25: keys slime's animated channels only (no new tracks)");
    const int l_iRow = f_RowOf(l_pKeys, 0.25f);
    l_bOk &= f_Check(l_iRow >= 0 && l_pKeys->f_CommitCell(l_iRow, AnimKeyListDock::ColPos, QStringLiteral("5, -20")) &&
                    toms::anim::f_PositionAt(*l_pDoc->f_SelectedNode(), 0.25f) == glm::vec3(5, -20, 0),
                "edit a key list cell (pos = 5, -20)");
    l_pEd->f_PropertiesDock()->f_PosXSpin()->setValue(7.0);   // a Properties edit with auto-key
    f_ProcessEvents();
    l_bOk &= f_Check(toms::anim::f_PositionAt(*l_pDoc->f_SelectedNode(), 0.25f).x == 7.0f, "properties edit writes the key (pos.x = 7)");
    int l_iUndos = 0;
    while (l_pDoc->f_UndoStack()->canUndo() && l_pDoc->f_ToJson() != l_oOriginal && l_iUndos < 10) {
        l_pDoc->f_UndoStack()->undo();
        l_iUndos++;
    }
    f_ProcessEvents();
    l_bOk &= f_Check(l_pDoc->f_ToJson() == l_oOriginal && !l_pDoc->f_IsDirty() && l_iUndos == 3, "undo restores the file (3 steps)");

    // A viewport drag (Move tool, auto-key): click the gem, drag it in three steps = one undo step.
    {
        AnimViewport* l_pVp = l_pEd->f_Viewport();
        l_pDoc->f_SetTime(0.5f);
        f_ProcessEvents();
        std::vector<toms::anim::NodePose> l_vPoses;
        std::vector<AnimViewport::DrawItem> l_vItems;
        l_pVp->f_BuildFrame(0.5f, l_vPoses, l_vItems, nullptr);
        QPointF l_oGem;
        for (const AnimViewport::DrawItem& l_oIt : l_vItems)
            if (const Node* l_pN = f_NodeAt(l_pDoc->f_Clip()->m_oRoot, l_oIt.m_vPath); l_pN && l_pN->m_strName == "gem")
                l_oGem = QPointF((l_oIt.m_oQuad.m_arrCorners[0] + l_oIt.m_oQuad.m_arrCorners[4]) / 2, (l_oIt.m_oQuad.m_arrCorners[1] + l_oIt.m_oQuad.m_arrCorners[5]) / 2);
        const QPointF l_oAt = l_pVp->f_WidgetPos(l_oGem);
        const int l_iStackBefore = l_pDoc->f_UndoStack()->index();
        auto l_fnSend = [&](QEvent::Type l_eType, const QPointF& l_oP, Qt::MouseButtons l_oButtons) {
            QMouseEvent l_oE(l_eType, l_oP, l_pVp->mapToGlobal(l_oP), Qt::LeftButton, l_oButtons, Qt::NoModifier);
            QCoreApplication::sendEvent(l_pVp, &l_oE);
        };
        l_fnSend(QEvent::MouseButtonPress, l_oAt, Qt::LeftButton);
        for (int l_iI = 1; l_iI <= 3; l_iI++) l_fnSend(QEvent::MouseMove, l_oAt + QPointF(10 * l_iI, 0), Qt::LeftButton);
        l_fnSend(QEvent::MouseButtonRelease, l_oAt + QPointF(30, 0), Qt::NoButton);
        const Node* l_pG = l_pDoc->f_SelectedNode();
        l_bOk &= f_Check(l_pG && l_pG->m_strName == "gem" && f_HasKeyAt(*l_pG, Channel::Pos, 0.5f) && l_pDoc->f_UndoStack()->index() == l_iStackBefore + 1,
                    "viewport: click selects gem, drag keys pos (1 undo step)");
        if (l_pG) std::printf("selftest:   gem pos key at 0.5: %.3f, %.3f\n", toms::anim::f_PositionAt(*l_pG, 0.5f).x, toms::anim::f_PositionAt(*l_pG, 0.5f).y);
        l_pDoc->f_UndoStack()->undo();
        l_bOk &= f_Check(l_pDoc->f_ToJson() == l_oOriginal, "drag undone");
        l_pDoc->f_SelectNodeByName(QStringLiteral("slime"));
        f_ProcessEvents();
    }

    // 3. Key list tools, each undone.
    l_pKeys->f_SelectRows({0, 1, 2});
    l_pKeys->f_FadeIn();
    l_pSlime = l_pDoc->f_SelectedNode();
    l_bOk &= f_Check(l_pSlime && l_pSlime->m_vColorKeys.size() == 3 && l_pSlime->m_vColorKeys[0].m_oV.a == 0.0f && std::fabs(l_pSlime->m_vColorKeys[1].m_oV.a - 0.5f) < 1e-6f &&
                    l_pSlime->m_vColorKeys[2].m_oV.a == 1.0f,
                "fade in over 3 rows (alpha 0, 0.5, 1)");
    l_pDoc->f_UndoStack()->undo();
    l_pEd->f_SpritesDock()->f_SelectSprites({QStringLiteral("bat"), QStringLiteral("slime")});
    l_pKeys->f_SelectRows({0, 1, 2});
    l_pKeys->f_SpriteSequence();
    l_pSlime = l_pDoc->f_SelectedNode();
    l_bOk &= f_Check(l_pSlime && l_pSlime->m_vSpriteKeys.size() == 3 && l_pSlime->m_vSpriteKeys[0].m_oV == "game:bat" && l_pSlime->m_vSpriteKeys[1].m_oV == "game:slime",
                "sprite sequence (bat, slime, bat), stored with the atlas");
    l_pDoc->f_UndoStack()->undo();
    QString l_oConflict;
    l_bOk &= f_Check(!l_pDoc->f_MoveRows({0.5f}, {1.0f}, &l_oConflict) && !l_oConflict.isEmpty(), "set time refuses a clash");
    l_bOk &= f_Check(l_pDoc->f_MoveRows({0.5f}, {0.4f}, &l_oConflict), "set time 0.5 -> 0.4");
    l_pDoc->f_UndoStack()->undo();
    l_bOk &= f_Check(l_pDoc->f_ToJson() == l_oOriginal, "tools undone");

    // 4. Play from 0 to 0.5 (AnimPlayer semantics; the 'hit' event at 0.5 fires).
    QStringList l_oFired;
    QObject::connect(l_pEd->f_Transport(), &AnimTransport::f_EventsFired, l_pEd, [&](const QStringList& l_oN, float) { l_oFired << l_oN; });
    l_pDoc->f_SetTime(0);
    l_pEd->f_Transport()->f_Play();
    l_pEd->f_Transport()->f_Advance(500);
    l_pEd->f_Transport()->f_Pause();
    f_ProcessEvents();
    l_bOk &= f_Check(std::fabs(l_pDoc->f_Time() - 0.5f) < 1e-4f && l_oFired.contains(QStringLiteral("hit")), "play to t=0.5 fires 'hit'");

    // 4b. A looping node keeps playing after the clip's end: the playhead stays at the end (edits go
    // there), the viewport draws a later time; pausing goes back to the playhead.
    {
        const QString l_oBefore = l_pDoc->f_ToJson();
        l_pDoc->f_EditClip(QStringLiteral("loop root"), [](toms::anim::Clip& l_oC) { l_oC.m_oRoot.m_bLoop = true; l_oC.m_iPlayCount = 1; });
        const bool l_bClipLoop = l_pEd->f_Transport()->f_LoopAction()->isChecked();
        l_pEd->f_Transport()->f_LoopAction()->setChecked(false);   // the clip plays once, its node loops
        const float l_fD = l_pDoc->f_Duration();
        l_pDoc->f_SetTime(0);
        l_pEd->f_Transport()->f_Play();
        l_pEd->f_Transport()->f_Advance(int(l_fD * 1000) + 700);
        f_ProcessEvents();
        l_bOk &= f_Check(l_pEd->f_Transport()->f_IsPlaying() && std::fabs(l_pDoc->f_Time() - l_fD) < 1e-4f && l_pDoc->f_PreviewTime() > l_fD + 0.6f,
                    "loop node: playback runs on past the end (playhead at the end, preview later)");
        l_pEd->f_Transport()->f_Pause();
        l_bOk &= f_Check(l_pDoc->f_PreviewTime() == l_pDoc->f_Time(), "pause: the preview is the playhead again");
        l_pDoc->f_UndoStack()->undo();
        l_bOk &= f_Check(l_pDoc->f_ToJson() == l_oBefore, "loop undone");
        l_pEd->f_Transport()->f_LoopAction()->setChecked(l_bClipLoop);
    }

    // 4c. Clips dock: double-click opens a clip; a changed clip asks to discard its changes first.
    {
        AnimClipsDock* l_pClips = l_pEd->f_ClipsDock();
        const int l_iStart = l_pDoc->f_UndoStack()->index();
        const int l_iHome = l_pDoc->f_ClipIndex();
        const size_t l_nClipCount = l_pDoc->f_File().m_vClips.size();
        l_pDoc->f_AddClip(QStringLiteral("other"));
        const int l_iOther = l_pDoc->f_ClipIndex();
        l_bOk &= f_Check(!l_pDoc->f_ClipModified() && l_pClips->f_OpenClip(l_iHome) && l_pDoc->f_ClipIndex() == l_iHome,
                    "open a clip: an unchanged clip switches without asking");
        l_pDoc->f_EditClip(QStringLiteral("len"), [](toms::anim::Clip& l_oC) { l_oC.m_fLength = 2.5f; });
        l_bOk &= f_Check(l_pDoc->f_ClipModified(), "an edit marks the clip changed");
        l_bOk &= f_Check(!l_pClips->f_OpenClip(l_iOther, 0) && l_pDoc->f_ClipIndex() == l_iHome && l_pDoc->f_ClipModified(),
                    "No: stays on the changed clip, changes kept");
        l_bOk &= f_Check(l_pClips->f_OpenClip(l_iOther, 1) && l_pDoc->f_ClipIndex() == l_iOther, "Yes: opens the other clip");
        l_bOk &= f_Check(l_pDoc->f_File().m_vClips[size_t(l_iHome)].m_fLength != 2.5f, "Yes: the changes to the first clip are discarded");
        l_pDoc->f_UndoStack()->undo();
        l_bOk &= f_Check(l_pDoc->f_File().m_vClips[size_t(l_iHome)].m_fLength == 2.5f, "undo brings the discarded changes back");
        l_pDoc->f_SetClipIndex(l_iHome);
        l_pDoc->f_DeleteClip(l_iOther);
        l_bOk &= f_Check(l_pDoc->f_ClipIndex() == l_iHome, "deleting another clip keeps the open one");
        l_pDoc->f_UndoStack()->setIndex(l_iStart);
        l_pDoc->f_SetClipIndex(l_iHome);
        l_bOk &= f_Check(l_pDoc->f_File().m_vClips.size() == l_nClipCount && l_pDoc->f_ClipIndex() == l_iHome, "clips test undone");
    }

    // 4d. Automatic backups: after `edits` edits a copy goes to the backups folder; only `keep` stay;
    // the copy opens (absolute atlas paths). The user's settings are put back afterwards.
    {
        const AutoBackup::Settings l_oSaved = AutoBackup::f_Settings();
        AutoBackup::f_SetSettings({true, 60, 3, 2});
        const QString l_oDir = QDir(AutoBackup::f_Folder()).filePath(QFileInfo(l_pDoc->f_FilePath()).completeBaseName());
        QDir(l_oDir).removeRecursively();
        auto l_fnCount = [&] { return int(QDir(l_oDir).entryList({QStringLiteral("*.anim")}, QDir::Files).size()); };
        const int l_iStart = l_pDoc->f_UndoStack()->index();
        for (int l_iI = 1; l_iI <= 2; l_iI++) l_pDoc->f_EditClip(QStringLiteral("b"), [l_iI](toms::anim::Clip& l_oC) { l_oC.m_fLength = 1.0f + 0.1f * float(l_iI); });
        l_bOk &= f_Check(l_fnCount() == 0, "backup: not before 3 edits");
        l_pDoc->f_EditClip(QStringLiteral("b"), [](toms::anim::Clip& l_oC) { l_oC.m_fLength = 1.3f; });
        l_bOk &= f_Check(l_fnCount() == 1, "backup: written after 3 edits");
        for (int l_iI = 4; l_iI <= 9; l_iI++) l_pDoc->f_EditClip(QStringLiteral("b"), [l_iI](toms::anim::Clip& l_oC) { l_oC.m_fLength = 1.0f + 0.1f * float(l_iI); });
        l_bOk &= f_Check(l_fnCount() == 2, "backup: only the newest 2 are kept");
        const QStringList l_oFiles = QDir(l_oDir).entryList({QStringLiteral("*.anim")}, QDir::Files, QDir::Name);
        toms::anim::AnimFile l_oBack;
        std::string l_strErr;
        QFile l_oF(QDir(l_oDir).filePath(l_oFiles.isEmpty() ? QString() : l_oFiles.last()));
        l_bOk &= f_Check(l_oF.open(QIODevice::ReadOnly) && toms::anim::f_ParseAnim(l_oF.readAll().toStdString(), l_oBack, &l_strErr) &&
                        !l_oBack.m_vAtlases.empty() && QDir::isAbsolutePath(QString::fromStdString(l_oBack.m_vAtlases[0].m_strPath)) &&
                        std::fabs(l_oBack.m_vClips[size_t(l_pDoc->f_ClipIndex())].m_fLength - 1.9f) < 1e-4f,
                    "backup: the newest copy has the last edit and absolute atlas paths");
        l_oF.close();
        l_pDoc->f_UndoStack()->setIndex(l_iStart);
        QDir(l_oDir).removeRecursively();
        AutoBackup::f_SetSettings(l_oSaved);
    }

    // 5. Screenshots.
    f_ProcessEvents();
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("main.png"))), "main.png");
    l_pKeys->raise();
    l_pKeys->f_SelectRows({1});
    f_ProcessEvents();
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("keylist.png"))), "keylist.png");
    if (auto* l_pProblems = l_oW.findChild<QDockWidget*>(QStringLiteral("AnimProblemsDock"))) {
        l_pProblems->raise();
        f_ProcessEvents();
        l_bOk &= f_Check(l_pProblems->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("problems.png"))), "problems.png");
        l_pKeys->raise();
    }
    {
        toms::anim::Ease l_oE;
        l_oE.m_iKind = toms::anim::Ease::m_iBezier;
        const float l_arrB[4] = {0.34f, 1.56f, 0.64f, 1.0f};
        std::copy(l_arrB, l_arrB + 4, l_oE.m_arrBezier);
        BezierDialog l_oDlg(l_oE, &l_oW);
        l_oDlg.show();
        f_ProcessEvents();
        l_bOk &= f_Check(l_oDlg.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("bezier.png"))), "bezier.png");
    }
    l_oW.f_SetDarkTheme(false);
    f_ProcessEvents();
    l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("light.png"))), "light.png");
    l_oW.f_SetDarkTheme(true);

    // 6. Save As into outDir and reopen: the same file (the atlas paths are rebased).
    const QString l_oSaveDir = QDir(l_oOutDir).filePath(QStringLiteral("saveas"));
    QDir(l_oSaveDir).removeRecursively();
    QDir().mkpath(l_oSaveDir);
    const QString l_oSaved = QDir(l_oSaveDir).filePath(QFileInfo(l_oFile).fileName());
    l_bOk &= f_Check(l_pEd->f_SaveTo(l_oSaved) && !l_pDoc->f_IsDirty(), "save as");
    const QString l_oBefore = l_pDoc->f_ToJson();
    const QStringList l_oAtlasesBefore = l_pDoc->f_AtlasPaths();
    l_bOk &= f_Check(l_pEd->f_OpenFile(l_oSaved, false) && l_pDoc->f_ToJson() == l_oBefore && l_pDoc->f_AtlasLoaded() && l_pDoc->f_AtlasPaths() == l_oAtlasesBefore,
                "reopen: animToJson equal, atlas found");
    std::string l_strText;
    toms::anim::AnimFile l_oReparsed;
    l_bOk &= f_Check(f_ReadTextFile(l_oSaved.toStdString(), l_strText) && toms::anim::f_ParseAnim(l_strText, l_oReparsed) &&
                    QString::fromStdString(toms::anim::f_AnimToJson(l_oReparsed)) == l_oBefore,
                "saved file parses back to the same JSON");
    std::printf("selftest: saved %s (atlases %s)\n", QDir::toNativeSeparators(l_oSaved).toUtf8().constData(),
                l_oReparsed.m_vAtlases.empty() ? "none" : l_oReparsed.m_vAtlases.front().m_strPath.c_str());

    // The file on disk: an "atlases" array of `expected` relative paths that exist, no "atlas".
    auto l_fnStoredRelative = [](const QString& l_oPath, int l_iExpected) {
        QFile l_oF(l_oPath);
        if (!l_oF.open(QIODevice::ReadOnly)) return false;
        const QJsonObject l_oO = QJsonDocument::fromJson(l_oF.readAll()).object();
        const QJsonArray l_oA = l_oO.value(QStringLiteral("atlases")).toArray();
        if (l_oO.contains(QStringLiteral("atlas")) || l_oA.size() != l_iExpected) return false;
        for (const QJsonValue& l_oV : l_oA) {
            const QString l_oP = l_oV.toString();
            if (l_oP.isEmpty() || QDir::isAbsolutePath(l_oP) || f_IsAbsoluteAtlasPath(l_oP.toStdString()) ||
                !QFileInfo::exists(QFileInfo(l_oPath).absoluteDir().filePath(l_oP)))
                return false;
        }
        return true;
    };
    auto l_fnWriteFile = [](const QString& l_oPath, const QByteArray& l_oBytes) {
        QFile l_oF(l_oPath);
        return l_oF.open(QIODevice::WriteOnly | QIODevice::Truncate) && l_oF.write(l_oBytes) == l_oBytes.size();
    };
    auto l_fnHasProblem = [&](Problem::Level l_eLevel, const QString& l_oA, const QString& l_oB) {
        for (const Problem& l_oP : f_CheckAnim(l_pDoc->f_File(), l_pDoc->f_CheckAtlases())) {
            const QString l_oM = QString::fromStdString(l_oP.m_strMessage);
            if (l_oP.m_eLevel == l_eLevel && l_oM.contains(l_oA) && l_oM.contains(l_oB)) return true;
        }
        return false;
    };
    const QString l_oGameAtlas = l_pDoc->f_AtlasPaths().value(0);
    l_bOk &= f_Check(l_fnStoredRelative(l_oSaved, 1), "(a) saved file: \"atlases\" with relative paths only");

    // (b) Save As onto another drive than the atlas: refused, nothing written.
    {
        const QString l_oOther = QDir(QDir::tempPath()).filePath(QStringLiteral("toms_anim_selftest_other_drive.anim"));
        QFile::remove(l_oOther);
        if (QDir::isAbsolutePath(QDir(QDir::tempPath()).relativeFilePath(l_oGameAtlas))) {
            QString l_oErr;
            l_bOk &= f_Check(!l_pDoc->f_Save(l_oOther, &l_oErr) && !QFileInfo::exists(l_oOther) && l_pDoc->f_FilePath() == QFileInfo(l_oSaved).absoluteFilePath() &&
                            l_oErr.contains(QStringLiteral("game.atlas")),
                        "(b) Save As to another drive is refused");
            std::printf("selftest:   \"%s\"\n", l_oErr.section(QLatin1Char('\n'), 0, 0).toUtf8().constData());
            l_bOk &= f_Check(!l_pEd->f_SaveTo(l_oOther) && !QFileInfo::exists(l_oOther), "(b) ... through the editor too");
        } else {
            std::printf("selftest: (b) skipped: the temp folder is on the atlas's drive\n");
        }
    }

    // An older file's single "atlas" opens and saves as "atlases"; an absolute path is warned
    // about and stored relative on save.
    {
        QFile l_oIn(l_oSaved);
        l_oIn.open(QIODevice::ReadOnly);
        QJsonObject l_oO = QJsonDocument::fromJson(l_oIn.readAll()).object();
        l_oIn.close();
        const QString l_oRel = l_oO.value(QStringLiteral("atlases")).toArray().at(0).toString();
        l_oO.remove(QStringLiteral("atlases"));
        l_oO.insert(QStringLiteral("atlas"), l_oRel);
        const QString l_oLegacy = QDir(l_oWorkDir).filePath(QStringLiteral("legacy.anim"));
        l_fnWriteFile(l_oLegacy, QJsonDocument(l_oO).toJson());
        l_bOk &= f_Check(l_pEd->f_OpenFile(l_oLegacy, false) && l_pDoc->f_AtlasCount() == 1 && l_pDoc->f_AtlasLoaded(), "old file (\"atlas\": \"...\") opens");
        l_bOk &= f_Check(l_pEd->f_SaveTo(l_oLegacy) && l_fnStoredRelative(l_oLegacy, 1), "... and saves \"atlases\"");
        l_oO.remove(QStringLiteral("atlas"));
        l_oO.insert(QStringLiteral("atlases"), QJsonArray{l_oGameAtlas});
        const QString l_oAbsFile = QDir(l_oWorkDir).filePath(QStringLiteral("absolute.anim"));
        l_fnWriteFile(l_oAbsFile, QJsonDocument(l_oO).toJson());
        l_bOk &= f_Check(l_pEd->f_OpenFile(l_oAbsFile, false) && l_pDoc->f_AtlasLoaded() && l_pDoc->f_AtlasPathIsAbsoluteInFile(0),
                    "absolute atlas path: the file opens, the atlas loads");
        l_bOk &= f_Check(l_fnHasProblem(Problem::Warning, QStringLiteral("absolute atlas path"), QStringLiteral("relative")),
                    "... Problems: 'absolute atlas path' warning");
        l_bOk &= f_Check(l_pEd->f_SaveTo(l_oAbsFile) && l_fnStoredRelative(l_oAbsFile, 1) && !l_pDoc->f_AtlasPathIsAbsoluteInFile(0) &&
                        !l_fnHasProblem(Problem::Warning, QStringLiteral("absolute atlas path"), QString()),
                    "... stored relative on save, warning gone");
    }

    // (c) Two atlases: a small second one with a sprite game.atlas lacks (spark) and a name it
    // has too (coin).
    {
        l_bOk &= f_Check(l_pEd->f_OpenFile(l_oWork, false) && l_pDoc->f_AtlasCount() == 1, "reopen the scratch copy");
        const QString l_oExtraDir = QDir(l_oWorkDir).filePath(QStringLiteral("extra"));
        QDir().mkpath(l_oExtraDir);
        QImage l_oPage(32, 16, QImage::Format_ARGB32);
        l_oPage.fill(Qt::transparent);
        {
            QPainter l_oP(&l_oPage);
            l_oP.setRenderHint(QPainter::Antialiasing);
            l_oP.setBrush(QColor(90, 210, 255));
            l_oP.setPen(Qt::NoPen);
            l_oP.drawPolygon(QPolygonF({QPointF(8, 0), QPointF(10, 6), QPointF(16, 8), QPointF(10, 10), QPointF(8, 16), QPointF(6, 10),
                                     QPointF(0, 8), QPointF(6, 6)}));
            l_oP.fillRect(QRect(18, 2, 12, 12), QColor(255, 60, 200));
        }
        const QString l_oExtra = QDir(l_oExtraDir).filePath(QStringLiteral("extra.atlas"));
        l_bOk &= f_Check(l_oPage.save(QDir(l_oExtraDir).filePath(QStringLiteral("extra.png"))) &&
                        l_fnWriteFile(l_oExtra, "extra.png\nsize: 32, 16\nformat: RGBA8888\nfilter: Nearest, Nearest\nrepeat: none\n"
                                         "spark\n  bounds: 0, 0, 16, 16\ncoin\n  bounds: 16, 0, 16, 16\n"),
                    "(c) write extra.atlas + extra.png");
        const int l_iStack = l_pDoc->f_UndoStack()->index();
        l_bOk &= f_Check(l_pEd->f_AddAtlasFile(l_oExtra) && l_pDoc->f_AtlasCount() == 2 && l_pDoc->f_AtlasLoaded() && l_pDoc->f_UndoStack()->index() == l_iStack + 1,
                    "(c) add it as the second atlas (1 undo step)");
        int l_iAi = -1, l_iBi = -1;
        l_bOk &= f_Check(l_pDoc->f_FindSprite("coin", &l_iAi) && l_iAi == 0 && l_pDoc->f_FindSprite("spark", &l_iBi) && l_iBi == 1,
                    "(c) lookup order: coin from game.atlas, spark from extra.atlas");
        l_bOk &= f_Check(l_pDoc->f_SpriteNames().contains(QStringLiteral("spark")) && l_pDoc->f_SpriteNames().count(QStringLiteral("coin")) == 1,
                    "(c) Sprites: names of both atlases");
        l_bOk &= f_Check(l_pDoc->f_AtlasId(0) == QLatin1String("game") && l_pDoc->f_AtlasId(1) == QLatin1String("extra"), "(c) atlas ids: game, extra");
        l_bOk &= f_Check(l_pDoc->f_AddSpriteNode(NodePath(), QStringLiteral("extra:spark"), glm::vec2(0, 72)) && l_pDoc->f_SelectedNode() &&
                        l_pDoc->f_SelectedNode()->m_strName == "spark",
                    "(c) add a node with 'extra:spark' (named spark)");
        std::vector<toms::anim::NodePose> l_vPoses;
        std::vector<AnimViewport::DrawItem> l_vItems;
        l_pEd->f_Viewport()->f_BuildFrame(0.0f, l_vPoses, l_vItems, nullptr);
        bool l_bSpark = false, l_bCoin = false;
        for (const AnimViewport::DrawItem& l_oIt : l_vItems) {
            const Node* l_pN = f_NodeAt(l_pDoc->f_Clip()->m_oRoot, l_oIt.m_vPath);
            if (l_pN && l_pN->m_strName == "spark") l_bSpark = l_oIt.m_pImages == &l_pDoc->f_AtlasAt(1).m_oImages && l_oIt.m_oSrc == QRect(0, 0, 16, 16);
            if (l_pN && l_pN->m_strName == "coin") l_bCoin = l_oIt.m_pImages == &l_pDoc->f_AtlasAt(0).m_oImages && l_oIt.m_oSrc.size() == f_GameCoinSize(l_pDoc);
        }
        l_bOk &= f_Check(l_bSpark && l_bCoin, "(c) viewport: spark from extra.atlas's page, coin from game.atlas's");
        l_bOk &= f_Check(f_CheckDrawing(l_pEd, 0.0f), "(c) viewport quads == appendQuads over both atlases");
        l_bOk &= f_Check(l_fnHasProblem(Problem::Warning, QStringLiteral("'coin'"), QStringLiteral("several atlases")) &&
                        f_CountLevel(f_CheckAnim(l_pDoc->f_File(), l_pDoc->f_CheckAtlases()), Problem::Error) == 0,
                    "(c) check: 'coin' in two atlases is a warning, no errors");
        l_bOk &= f_Check(l_pEd->f_SaveTo(l_oWork) && l_fnStoredRelative(l_oWork, 2), "(c) saved: two relative atlas paths");
        std::fflush(stdout);
        l_bOk &= f_Check(f_HeadlessMain({"check", l_oWork.toStdString()}) == 0, "(c) --headless check on it: exit 0 (warnings only)");
        l_bOk &= f_Check(l_pDoc->f_MoveAtlas(1, -1) && l_pDoc->f_FindSprite("coin", &l_iAi) && l_iAi == 0 &&
                        QFileInfo(l_pDoc->f_AtlasAt(0).m_oPath).fileName() == QLatin1String("extra.atlas"),
                    "(c) move extra.atlas up: coin now comes from it");
        l_pDoc->f_UndoStack()->undo();
        l_bOk &= f_Check(QFileInfo(l_pDoc->f_AtlasAt(0).m_oPath).fileName() == QLatin1String("game.atlas") && l_pDoc->f_AtlasAt(1).m_bOk, "(c) undo the move");

        l_bOk &= f_Check(f_AtlasReferences(l_pEd, l_oWorkDir, l_oOutDir, l_oW), "(d) sprite references store their atlas");

        // Screenshots: the Atlases list, the Sprites dock grouped by atlas.
        l_pEd->f_PropertiesDock()->raise();
        l_pEd->f_PropertiesDock()->f_AtlasesPanel()->f_SetCurrentIndex(1);
        l_pEd->f_SpritesDock()->f_SelectSprites({QStringLiteral("spark")});
        if (QListWidget* l_pList = l_pEd->f_SpritesDock()->f_List()) l_pList->scrollToBottom();
        f_ProcessEvents();
        l_bOk &= f_Check(l_oW.grab().save(QDir(l_oOutDir).filePath(QStringLiteral("atlases.png"))), "atlases.png");
        l_bOk &= f_Check(l_pEd->f_PropertiesDock()->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("properties.png"))), "properties.png");
        l_bOk &= f_Check(l_pEd->f_SpritesDock()->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("sprites.png"))), "sprites.png");
        if (auto* l_pFilter = l_pEd->f_SpritesDock()->findChild<QLineEdit*>()) {
            l_pFilter->setText(QStringLiteral("coin"));
            if (QListWidget* l_pList = l_pEd->f_SpritesDock()->f_List()) l_pList->scrollToTop();
            f_ProcessEvents();
            // Floating and taller, so both coins (game's and extra's) show with their badges.
            QDockWidget* l_pDock = l_pEd->f_SpritesDock();
            l_pDock->setFloating(true);
            l_pDock->resize(340, 240);
            f_ProcessEvents();
            l_bOk &= f_Check(l_pDock->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("sprites_dup.png"))), "sprites_dup.png");
            l_pDock->setFloating(false);
            f_ProcessEvents();
            l_pFilter->clear();
        }
        {   // One tab per atlas: the second tab (extra) in front.
            AnimSpritesDock* l_pSd = l_pEd->f_SpritesDock();
            l_bOk &= f_Check(l_pSd->f_Tabs()->count() == l_pDoc->f_AtlasCount() && l_pSd->f_Tabs()->tabText(0).startsWith(l_pDoc->f_AtlasId(0)) &&
                            l_pSd->f_Tabs()->tabText(1).startsWith(l_pDoc->f_AtlasId(1)),
                        "(c) Sprites dock: one tab per atlas, titled with its id");
            l_pSd->f_Tabs()->setCurrentIndex(1);
            f_ProcessEvents();
            l_bOk &= f_Check(l_pSd->f_List() == l_pSd->f_ListFor(1) && l_pSd->f_List()->count() == 2, "(c) the extra tab lists its 2 sprites");
            l_bOk &= f_Check(l_pSd->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("sprites_tabs.png"))), "sprites_tabs.png");
            l_pSd->f_Tabs()->setCurrentIndex(0);
        }
        if (auto* l_pProblems = l_oW.findChild<QDockWidget*>(QStringLiteral("AnimProblemsDock"))) {
            l_pProblems->raise();
            f_ProcessEvents();
            l_bOk &= f_Check(l_pProblems->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("problems_atlases.png"))), "problems_atlases.png");
        }

        l_bOk &= f_Check(l_pDoc->f_RemoveAtlas(1) && l_pDoc->f_AtlasCount() == 1 && !l_pDoc->f_FindSprite("spark") &&
                        l_fnHasProblem(Problem::Error, QStringLiteral("'extra:spark'"), QStringLiteral("no atlas has the id 'extra'")),
                    "(c) remove it: 'extra:spark' names an unknown atlas (error)");
        l_pDoc->f_UndoStack()->undo();
        l_bOk &= f_Check(l_pDoc->f_AtlasCount() == 2 && l_pDoc->f_FindSprite("spark"), "(c) undo the remove");

        // An untitled file may hold atlases only in memory: adding one asks to save first.
        l_pDoc->f_NewFile();
        f_ProcessEvents();
        l_bOk &= f_Check(l_pDoc->f_FilePath().isEmpty() && l_pDoc->f_AtlasCount() == 2 && !l_pEd->f_AddAtlasFile(l_oExtra) &&
                        l_pEd->f_ProblemsDock()->f_LogText().contains(QStringLiteral("save the animation first")),
                    "untitled: adding an atlas asks to save the .anim first");
    }
    {   // The mouse wheel changes a number only in a field that has focus (Theme.cpp WheelNeedsFocus).
        QDoubleSpinBox* l_pBox = nullptr;
        for (QDoubleSpinBox* l_pB : l_pEd->f_PropertiesDock()->findChildren<QDoubleSpinBox*>())
            if (l_pB->isEnabled() && l_pB->isVisibleTo(l_pEd->f_PropertiesDock()) && l_pB->value() < l_pB->maximum() - 1) { l_pBox = l_pB; break; }
        if (!l_pBox) {
            l_bOk &= f_Check(false, "wheel: a number field in Properties");
        } else {
            auto l_fnWheel = [&](int l_iDelta) {
                const QPointF l_oAt(l_pBox->width() / 2.0, l_pBox->height() / 2.0);
                QWheelEvent l_oE(l_oAt, l_pBox->mapToGlobal(l_oAt), QPoint(), QPoint(0, l_iDelta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(l_pBox, &l_oE);
                f_ProcessEvents();
            };
            l_bOk &= f_Check(l_pBox->focusPolicy() == Qt::StrongFocus, "wheel: number fields take focus by click or Tab, not by the wheel");
            l_oW.setFocus();
            f_ProcessEvents();
            // A short Properties panel, so it has something to scroll.
            QScrollArea* l_pArea = l_pEd->f_PropertiesDock()->findChild<QScrollArea*>();
            if (l_pArea) { l_pArea->setMaximumHeight(160); f_ProcessEvents(); l_pArea->verticalScrollBar()->setValue(0); f_ProcessEvents(); }
            const double l_dBefore = l_pBox->value();
            const int l_iScrolledFrom = l_pArea ? l_pArea->verticalScrollBar()->value() : 0;
            l_fnWheel(-120);
            l_bOk &= f_Check(l_pBox->value() == l_dBefore && !l_pBox->hasFocus(), "wheel: over an unfocused field the value stays");
            l_bOk &= f_Check(l_pArea && l_pArea->verticalScrollBar()->maximum() > 0 && l_pArea->verticalScrollBar()->value() > l_iScrolledFrom,
                        "wheel: over an unfocused field the Properties panel scrolls");
            if (l_pArea) { l_pArea->setMaximumHeight(QWIDGETSIZE_MAX); f_ProcessEvents(); }
            l_pBox->setFocus();
            f_ProcessEvents();
            if (l_pBox->hasFocus()) {   // the offscreen platform may not focus a window
                l_fnWheel(120);
                l_bOk &= f_Check(l_pBox->value() != l_dBefore, "wheel: in a focused field it changes the value");
                l_pDoc->f_UndoStack()->undo();
            }
        }
    }
    {   // The skin view (docs/engine/16_MESH_ANIMATION.md) on tests/smoke/anim_skin.anim, next to the file under test.
        const QString l_oSkinFile = QFileInfo(l_oFile).dir().filePath(QStringLiteral("anim_skin.anim"));
        AnimViewport* l_pView = l_pEd->f_Viewport();
        if (!QFileInfo::exists(l_oSkinFile)) {
            l_bOk &= f_Check(false, "skin: tests/smoke/anim_skin.anim is next to the file under test");
        } else if (l_pEd->f_OpenFile(l_oSkinFile, false)) {
            l_pView->resize(640, 480);
            f_ProcessEvents();
            l_pEd->f_Viewport()->f_FrameClip();
            l_pDoc->f_SetTime(0.25f);   // the chest and head turned their furthest
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("golem")), "skin: select the skinned golem");
            f_ProcessEvents();
            const QString l_oInfo = l_pEd->f_PropertiesDock()->f_SkinInfoText();
            l_bOk &= f_Check(l_oInfo.contains(QStringLiteral("3 bone(s)")) && l_oInfo.contains(QStringLiteral("automatic")),
                        "skin: Properties says its mesh, 3 bones, automatic weights");
            // Bone 2's colour (head, yellow; the hip's red sits under the Move arrow): none with Show Skin off, its segment and joint with it on.
            auto l_fnBoneColoured = [&](int l_iBone) {
                const QImage l_oImg = l_pView->f_UsingGameRenderer() ? f_PresentedImage(l_pView) : l_pView->grab().toImage();
                const QColor l_oC = AnimViewport::f_BoneColor(l_iBone);
                int l_iN = 0;
                for (int l_iY = 0; l_iY < l_oImg.height(); l_iY++)
                    for (int l_iX = 0; l_iX < l_oImg.width(); l_iX++) {
                        const QColor l_oP = l_oImg.pixelColor(l_iX, l_iY);
                        if (std::abs(l_oP.red() - l_oC.red()) < 6 && std::abs(l_oP.green() - l_oC.green()) < 6 && std::abs(l_oP.blue() - l_oC.blue()) < 6) l_iN++;
                    }
                return l_iN;
            };
            l_pEd->f_ShowSkinAction()->setChecked(false);
            f_ProcessEvents();
            const int l_iOff = l_fnBoneColoured(2);
            l_pEd->f_ShowSkinAction()->setChecked(true);
            f_ProcessEvents();
            const int l_iOn = l_fnBoneColoured(2);
            l_bOk &= f_Check(l_pView->f_ShowSkin() && l_iOn > l_iOff + 50, "skin: View > Show Skin draws the bones over the sprite");
            l_bOk &= f_Check(l_pView->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("skin_view.png"))), "skin_view.png");
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("chest")), "skin: select the chest bone");
            f_ProcessEvents();
            l_bOk &= f_Check(l_pView->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("skin_bone.png"))), "skin_bone.png (shaded by the chest's weights)");
            l_bOk &= f_Check(l_pEd->f_PropertiesDock()->f_SkinInfoText().isEmpty(), "skin: a bone has no skin note");
            l_pEd->f_ShowSkinAction()->setChecked(false);

            // Copy / paste nodes (Edit menu): the whole rig, pasted into the same clip, becomes a second rig.
            const QList<QAction*> l_oClip = l_pEd->f_NodeClipboardActions();   // cut, copy, paste, paste as sibling
            const toms::anim::Clip* l_pC = l_pDoc->f_Clip();
            const int l_iUndoAt = l_pDoc->f_UndoStack()->index();
            const size_t l_nChildren = l_pC->m_oRoot.m_vChildren.size();
            l_pDoc->f_SelectNode({});   // the root, "stage"
            l_oClip[1]->trigger();
            l_bOk &= f_Check(QGuiApplication::clipboard()->text().contains(QStringLiteral("\"anim-node\"")),
                        "copy: the node is on the clipboard as text");
            l_oClip[2]->trigger();   // under the root
            l_pC = l_pDoc->f_Clip();
            const Node* l_pPasted = l_pC->m_oRoot.m_vChildren.size() == l_nChildren + 1 ? &l_pC->m_oRoot.m_vChildren.back() : nullptr;
            l_bOk &= f_Check(l_pPasted && l_pPasted->m_strName == "stage_2" && l_pDoc->f_SelectedNode() == l_pPasted,
                        "paste: under the selected node, renamed stage_2, selected");
            const Node* l_pGolem2 = l_pPasted ? animed::f_NodeAt(*l_pPasted, {0}) : nullptr;
            l_bOk &= f_Check(l_pGolem2 && l_pGolem2->m_strName == "golem_2" &&
                            l_pGolem2->m_vSkinBones == std::vector<std::string>({"hip_2", "chest_2", "head_2"}),
                        "paste: the pasted skin follows the pasted bones (hip_2, chest_2, head_2)");
            const Node* l_pChest2 = l_pGolem2 ? animed::f_NodeAt(*l_pGolem2, {0, 0}) : nullptr;   // golem_2 > hip_2 > chest_2
            l_bOk &= f_Check(l_pChest2 && l_pChest2->m_strName == "chest_2" && l_pChest2->m_vRotKeys.size() == 4, "paste: children and keys come along");
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("hip_2")), "select hip_2");
            l_oClip[0]->trigger();   // cut
            l_bOk &= f_Check(!l_pDoc->f_SelectNodeByName(QStringLiteral("hip_2")) && !l_pDoc->f_SelectNodeByName(QStringLiteral("chest_2")),
                        "cut: the node and its children are gone");
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("golem_2")), "select golem_2");
            l_oClip[3]->trigger();   // paste as sibling: right after golem_2
            l_pC = l_pDoc->f_Clip();
            l_pPasted = &l_pC->m_oRoot.m_vChildren.back();
            l_bOk &= f_Check(l_pPasted->m_vChildren.size() == 2 && l_pPasted->m_vChildren[1].m_strName == "hip_2",
                        "paste as sibling: after the selected node, the name free again (hip_2)");
            QGuiApplication::clipboard()->setText(QStringLiteral("just some text"));
            const int l_iBefore = l_pDoc->f_UndoStack()->index();
            l_oClip[2]->trigger();
            l_bOk &= f_Check(l_pDoc->f_UndoStack()->index() == l_iBefore, "paste: other text on the clipboard changes nothing");
            while (l_pDoc->f_UndoStack()->index() > l_iUndoAt) l_pDoc->f_UndoStack()->undo();
            l_bOk &= f_Check(l_pDoc->f_Clip()->m_oRoot.m_vChildren.size() == l_nChildren, "undo: the clip is as it was");

            // Bones mode: a click picks a bone (here the chest's joint), never the sprite under it.
            l_pEd->f_BoneModeAction()->setChecked(true);
            f_ProcessEvents();
            l_bOk &= f_Check(l_pView->f_EditMode() == AnimViewport::EditMode::Bones, "bones mode: B / the toolbar switch");
            l_pDoc->f_SelectNodeByName(QStringLiteral("golem"));
            f_ProcessEvents();
            auto l_fnBonePoint = [&](const char* l_pszName) {   // where a bone's joint is drawn, in widget pixels
                std::vector<toms::anim::NodePose> l_vPoses;
                toms::anim::f_Evaluate(*l_pDoc->f_Clip(), l_pDoc->f_PreviewTime(), l_vPoses);
                for (size_t l_nI = 0; l_nI < l_vPoses.size(); l_nI++) {
                    toms::anim::SkinView l_oSv;
                    if (toms::anim::f_SkinView(*l_pDoc->f_Clip(), l_vPoses, l_nI, l_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), &l_oSv) != toms::anim::SkinState::Ok)
                        continue;
                    for (size_t l_nB = 0; l_nB < l_oSv.m_vBones.size(); l_nB++)
                        if (l_oSv.m_vBones[l_nB] == l_pszName) return l_pView->f_WidgetPos(QPointF(l_oSv.m_vBoneStart[l_nB].x, l_oSv.m_vBoneStart[l_nB].y));
                }
                return QPointF(-1, -1);
            };
            auto l_fnClick = [&](const QPointF& l_oAt) {
                for (QEvent::Type l_eType : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                    QMouseEvent l_oE(l_eType, l_oAt, l_pView->mapToGlobal(l_oAt), Qt::LeftButton,
                                  l_eType == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(l_pView, &l_oE);
                }
                f_ProcessEvents();
            };
            const QPointF l_oChestAt = l_fnBonePoint("chest");
            l_bOk &= f_Check(l_pView->f_BoneAt(l_oChestAt) == QStringLiteral("chest"), "bones mode: the chest's joint is found under its point");
            l_fnClick(l_oChestAt);
            l_bOk &= f_Check(l_pDoc->f_SelectedNode() && l_pDoc->f_SelectedNode()->m_strName == "chest", "bones mode: a click on the joint selects the chest");
            {   // Editing while the clip plays pauses it: a drag (Auto-Key on) keys the frame on screen once.
                auto l_fnPump = [](int l_iMs) {   // let the playback timer run
                    QElapsedTimer l_oT;
                    l_oT.start();
                    while (l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                };
                const bool l_bAutoKey = l_pDoc->f_AutoKey();
                l_pDoc->f_SetAutoKey(true);
                const int l_iPlayAt = l_pDoc->f_UndoStack()->index();
                const size_t l_nKeysBefore = l_pDoc->f_SelectedNode()->m_vPosKeys.size();
                l_pEd->f_Transport()->f_Play();
                l_fnPump(150);
                l_bOk &= f_Check(l_pEd->f_Transport()->f_IsPlaying(), "playing");
                const QPointF l_oFrom = l_fnBonePoint("chest");
                QMouseEvent l_oPress(QEvent::MouseButtonPress, l_oFrom, l_pView->mapToGlobal(l_oFrom), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(l_pView, &l_oPress);
                l_bOk &= f_Check(!l_pEd->f_Transport()->f_IsPlaying(), "a drag starting pauses playback");
                for (int l_iI = 1; l_iI <= 6; l_iI++) {   // the drag goes on over several frames
                    const QPointF l_oTo = l_oFrom + QPointF(3.0 * l_iI, 0);
                    QMouseEvent l_oMove(QEvent::MouseMove, l_oTo, l_pView->mapToGlobal(l_oTo), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(l_pView, &l_oMove);
                    l_fnPump(40);
                }
                QMouseEvent l_oRelease(QEvent::MouseButtonRelease, l_oFrom + QPointF(18, 0), l_pView->mapToGlobal(l_oFrom + QPointF(18, 0)),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(l_pView, &l_oRelease);
                f_ProcessEvents();
                l_bOk &= f_Check(l_pDoc->f_SelectedNode()->m_vPosKeys.size() == l_nKeysBefore + 1, "the whole drag added one key, not one per frame");
                l_pEd->f_Transport()->f_Play();
                l_fnPump(150);
                l_pDoc->f_SetChannel(animed::Channel::Pos, animed::Value{glm::vec4(1, -12, 0, 0)});
                l_bOk &= f_Check(!l_pEd->f_Transport()->f_IsPlaying(), "a typed value pauses playback too");
                while (l_pDoc->f_UndoStack()->index() > l_iPlayAt) l_pDoc->f_UndoStack()->undo();
                l_pDoc->f_SetAutoKey(l_bAutoKey);
            }
            // Hidden: the golem off, its bones are neither drawn nor picked.
            l_pDoc->f_EditNode(NodePath{0}, QStringLiteral("hide"), [](Node& l_oN) { l_oN.m_bVisible = false; });
            f_ProcessEvents();
            l_bOk &= f_Check(l_pView->f_BoneAt(l_oChestAt).isEmpty(), "a hidden skinned node: no bones to pick (and no overlay)");
            l_pDoc->f_UndoStack()->undo();
            l_pEd->f_ObjectModeAction()->setChecked(true);
            f_ProcessEvents();
            l_bOk &= f_Check(l_pView->f_EditMode() == AnimViewport::EditMode::Object, "object mode: O");

            // Bones outside the sprite: the warning, and Move Bones Under This Sprite (nothing moves on screen).
            const int l_iFixAt = l_pDoc->f_UndoStack()->index();
            l_pDoc->f_EditClip(QStringLiteral("rig"), [](toms::anim::Clip& l_oC) {
                Node l_oG;
                l_oG.m_strName = "lamp";
                l_oG.m_strSprite = "golem";
                l_oG.m_v3Pos = {40, 0, 0};
                l_oG.m_vSkinBones = {"lampbone"};
                Node l_oB;
                l_oB.m_strName = "lampbone";
                l_oB.m_v3Pos = {45, -8, 0};
                toms::anim::Key<glm::vec3> l_oK;
                l_oK.m_fT = 0.5f;
                l_oK.m_oV = {47, -8, 0};
                l_oB.m_vPosKeys.push_back(l_oK);
                l_oC.m_oRoot.m_vChildren.push_back(l_oG);
                l_oC.m_oRoot.m_vChildren.push_back(l_oB);
            });
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("lamp")), "select a sprite whose bone is its sibling");
            f_ProcessEvents();
            l_bOk &= f_Check(l_pEd->f_PropertiesDock()->f_BonesOutsideShown(), "bones outside: Properties warns");
            l_bOk &= f_Check(l_pDoc->f_ParentBonesUnder(l_pDoc->f_SelectedPath()), "Move Bones Under This Sprite");
            const Node* l_pLamp = l_pDoc->f_SelectedNode();
            l_bOk &= f_Check(l_pLamp && l_pLamp->m_strName == "lamp" && l_pLamp->m_vChildren.size() == 1 && l_pLamp->m_vChildren[0].m_strName == "lampbone" &&
                            l_pLamp->m_vChildren[0].m_v3Pos == glm::vec3(5, -8, 0) && l_pLamp->m_vChildren[0].m_vPosKeys[0].m_oV == glm::vec3(7, -8, 0),
                        "the bone is under the sprite, its rest and keys shifted: it stays where it was");
            f_ProcessEvents();
            l_bOk &= f_Check(!l_pEd->f_PropertiesDock()->f_BonesOutsideShown(), "bones inside: no warning");
            while (l_pDoc->f_UndoStack()->index() > l_iFixAt) l_pDoc->f_UndoStack()->undo();

            // Mesh grid: the golem's node asks for 8 x 8 cells; its mesh is cut again at once (one undo step).
            l_pDoc->f_SelectNodeByName(QStringLiteral("golem"));
            f_ProcessEvents();
            int l_iGc = 0, l_iGr = 0;
            l_bOk &= f_Check(l_pEd->f_PropertiesDock()->f_MeshGridBox()->isVisibleTo(l_pEd->f_PropertiesDock()) &&
                            l_pDoc->f_MeshGridOf(*l_pDoc->f_SelectedNode(), l_iGc, l_iGr) && l_iGc == 4 && l_iGr == 4,
                        "mesh grid: shown for the skinned golem, 4 x 4 (8-pixel cells of a 32-pixel sprite)");
            l_bOk &= f_Check(l_pDoc->f_AtlasMeshes(0).f_Find(l_pDoc->f_AtlasSet().f_MeshKeyFor("golem", *l_pDoc->f_SelectedNode())) != nullptr,
                        "mesh grid: the golem's mesh is in the atlas's mesh file (key \"golem\")");
            const int l_iGridAt = l_pDoc->f_UndoStack()->index();
            l_bOk &= f_Check(l_pDoc->f_SetMeshGrid(8, 8) && l_pDoc->f_MeshGridOf(*l_pDoc->f_SelectedNode(), l_iGc, l_iGr) && l_iGc == 8 && l_iGr == 8 &&
                            l_pDoc->f_SelectedNode()->m_arrMeshGrid[0] == 8 && l_pDoc->f_UndoStack()->index() == l_iGridAt + 1,
                        "mesh grid: 8 x 8, kept in the node, one undo step");
            l_bOk &= f_Check(l_pDoc->f_AtlasSet().f_MeshKeyFor("golem", *l_pDoc->f_SelectedNode()) == "golem@8x8" &&
                            l_pDoc->f_AtlasMeshes(0).f_Find("golem@8x8") != nullptr && l_pDoc->f_AtlasMeshes(0).f_Find("golem") != nullptr,
                        "mesh grid: the 8 x 8 mesh is added beside the 8-pixel one (other clips may use that)");
            f_ProcessEvents();
            l_bOk &= f_Check(l_pEd->f_PropertiesDock()->f_SkinInfoText().contains(QStringLiteral("Mesh: ")) &&
                            !l_pEd->f_PropertiesDock()->f_SkinInfoText().contains(QStringLiteral(" 25 points")),
                        "mesh grid: the skin uses the new mesh at once");
            l_pEd->f_ShowSkinAction()->setChecked(true);
            f_ProcessEvents();
            l_bOk &= f_Check(l_pView->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("skin_grid8.png"))), "skin_grid8.png");
            l_pEd->f_ShowSkinAction()->setChecked(false);
            while (l_pDoc->f_UndoStack()->index() > l_iGridAt) l_pDoc->f_UndoStack()->undo();

            // Motion recipes (docs/engine/06 section 7): on the golem, one undo step each; a bone recipe needs a skin.
            l_pDoc->f_SelectNodeByName(QStringLiteral("golem"));
            const int l_iRecipeAt = l_pDoc->f_UndoStack()->index();
            l_bOk &= f_Check(l_pDoc->f_ApplyMotionRecipe("breathe", 0.0f, 1.0f, 1.0f) && l_pDoc->f_SelectedNode()->m_vDeformKeys.size() >= 3 &&
                            l_pDoc->f_UndoStack()->index() == l_iRecipeAt + 1,
                        "recipe: breathe keys the golem's lattice, one undo step");
            const toms::anim::Node* l_pHeadBefore = toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "head");
            const size_t l_nHeadKeys = l_pHeadBefore ? l_pHeadBefore->m_vRotKeys.size() : 0;
            l_bOk &= f_Check(l_pDoc->f_ApplyMotionRecipe("nod", 2.0f, 0.8f, 1.0f) && l_pDoc->f_UndoStack()->index() == l_iRecipeAt + 2 &&
                            toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "head")->m_vRotKeys.size() > l_nHeadKeys,
                        "recipe: nod keys the skin's last bone");
            l_pDoc->f_SelectNodeByName(QStringLiteral("hip"));
            l_bOk &= f_Check(!l_pDoc->f_ApplyMotionRecipe("swing", 0.0f, 0.0f, 1.0f) && l_pDoc->f_UndoStack()->index() == l_iRecipeAt + 2,
                        "recipe: a bone recipe on a node without a skin is refused");
            while (l_pDoc->f_UndoStack()->index() > l_iRecipeAt) l_pDoc->f_UndoStack()->undo();

            // Weights mode (docs/engine/16 step 5 part 3): a click on a joint picks the bone; a stroke paints its weights (one
            // undo step); Ctrl+stroke takes them off; Reset to Automatic Weights drops them.
            l_pEd->f_WeightModeAction()->setChecked(true);
            f_ProcessEvents();
            l_bOk &= f_Check(l_pView->f_EditMode() == AnimViewport::EditMode::Weights, "weights mode: P / the toolbar switch");
            l_pDoc->f_SelectNodeByName(QStringLiteral("golem"));
            f_ProcessEvents();
            l_fnClick(l_fnBonePoint("chest"));
            l_bOk &= f_Check(l_pDoc->f_SelectedNode() && l_pDoc->f_SelectedNode()->m_strName == "chest", "weights mode: a click on a joint picks that bone");
            // The chest's weight on the point nearest widget point w, and every point's weights summing to 1.
            auto l_fnChestWeight = [&](const QPointF& l_oW, bool* l_pSumsOk) {
                std::vector<toms::anim::NodePose> l_vPoses;
                toms::anim::f_Evaluate(*l_pDoc->f_Clip(), l_pDoc->f_PreviewTime(), l_vPoses);
                for (size_t l_nI = 0; l_nI < l_vPoses.size(); l_nI++) {
                    toms::anim::SkinView l_oSv;
                    if (toms::anim::f_SkinView(*l_pDoc->f_Clip(), l_vPoses, l_nI, l_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), &l_oSv) != toms::anim::SkinState::Ok)
                        continue;
                    const int l_iChest = int(std::find(l_oSv.m_vBones.begin(), l_oSv.m_vBones.end(), "chest") - l_oSv.m_vBones.begin());
                    size_t l_nBest = 0;
                    double l_dBestD = 1e30;
                    for (size_t l_nV = 0; l_nV < l_oSv.m_vVertices.size(); l_nV++) {
                        const QPointF l_oP = l_pView->f_WidgetPos(QPointF(l_oSv.m_vVertices[l_nV].x, l_oSv.m_vVertices[l_nV].y));
                        const double l_dD = std::hypot(l_oP.x() - l_oW.x(), l_oP.y() - l_oW.y());
                        if (l_dD < l_dBestD) { l_dBestD = l_dD; l_nBest = l_nV; }
                        float l_fSum = 0;
                        for (int l_iK = 0; l_iK < toms::anim::g_iSkinInfluences; l_iK++) l_fSum += l_oSv.m_vWeights[l_nV * 2 * toms::anim::g_iSkinInfluences + 2 * size_t(l_iK) + 1];
                        if (l_pSumsOk && std::fabs(l_fSum - 1.0f) > 1e-3f) *l_pSumsOk = false;
                    }
                    return animed::f_BoneWeight(l_oSv.m_vWeights, l_nBest, l_iChest);
                }
                return -1.0f;
            };
            auto l_fnStroke = [&](const QPointF& l_oFrom, Qt::KeyboardModifiers l_oMods) {   // press, three moves, release
                QMouseEvent l_oPress(QEvent::MouseButtonPress, l_oFrom, l_pView->mapToGlobal(l_oFrom), Qt::LeftButton, Qt::LeftButton, l_oMods);
                QApplication::sendEvent(l_pView, &l_oPress);
                for (int l_iI = 1; l_iI <= 3; l_iI++) {
                    const QPointF l_oTo = l_oFrom + QPointF(0, 4.0 * l_iI);
                    QMouseEvent l_oMove(QEvent::MouseMove, l_oTo, l_pView->mapToGlobal(l_oTo), Qt::NoButton, Qt::LeftButton, l_oMods);
                    QApplication::sendEvent(l_pView, &l_oMove);
                }
                const QPointF l_oEnd = l_oFrom + QPointF(0, 12);
                QMouseEvent l_oRelease(QEvent::MouseButtonRelease, l_oEnd, l_pView->mapToGlobal(l_oEnd), Qt::LeftButton, Qt::NoButton, l_oMods);
                QApplication::sendEvent(l_pView, &l_oRelease);
                f_ProcessEvents();
            };
            // Paint beside the hip, away from every joint: the chest's share there grows.
            const QPointF l_oPaintAt = l_fnBonePoint("hip") + QPointF(14, 0);
            const float l_fChestBefore = l_fnChestWeight(l_oPaintAt, nullptr);
            const int l_iWeightsAt = l_pDoc->f_UndoStack()->index();
            l_pView->f_SetBrushStrength(0.8);
            l_fnStroke(l_oPaintAt, Qt::NoModifier);
            const Node* l_pGolem = toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "golem");
            bool l_bSums = true;
            const float l_fChestAfter = l_fnChestWeight(l_oPaintAt, &l_bSums);
            l_bOk &= f_Check(l_pGolem && !l_pGolem->m_vSkinWeights.empty() && l_pGolem->m_vSkinWeights.size() % (2 * toms::anim::g_iSkinInfluences) == 0,
                        "weights: the stroke wrote the golem's weights (no longer automatic)");
            l_bOk &= f_Check(l_fChestAfter > l_fChestBefore + 0.1f, "weights: the chest's share grew under the brush");
            l_bOk &= f_Check(l_bSums, "weights: every point still sums to 1");
            l_bOk &= f_Check(l_pDoc->f_UndoStack()->index() == l_iWeightsAt + 1, "weights: the whole stroke is one undo step");
            l_bOk &= f_Check(l_pView->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("skin_weights.png"))), "skin_weights.png (the chest's weights)");
            l_fnStroke(l_oPaintAt, Qt::ControlModifier);
            l_bOk &= f_Check(l_fnChestWeight(l_oPaintAt, nullptr) < l_fChestAfter - 0.1f && l_pDoc->f_UndoStack()->index() == l_iWeightsAt + 2,
                        "weights: Ctrl+stroke takes the chest's share off");
            l_pEd->f_AutoWeightsAction()->trigger();   // the chest is selected: the skin it is in
            l_pGolem = toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "golem");
            l_bOk &= f_Check(l_pGolem && l_pGolem->m_vSkinWeights.empty() && l_pDoc->f_UndoStack()->index() == l_iWeightsAt + 3,
                        "weights: Reset to Automatic Weights drops them (one undo step)");
            while (l_pDoc->f_UndoStack()->index() > l_iWeightsAt) l_pDoc->f_UndoStack()->undo();
            l_pView->f_SetBrushStrength(0.5);
            l_pEd->f_ObjectModeAction()->setChecked(true);
            f_ProcessEvents();
        } else {
            l_bOk &= f_Check(false, "skin: open tests/smoke/anim_skin.anim");
        }
    }
    {   // Help > Skinning Tutorial (docs/engine/17): the tutorial and its two files are found from the editor's folder and open.
        l_bOk &= f_Check(!AnimEditor::f_RepoFile(QStringLiteral("docs/engine/17_SKINNING_TUTORIAL.md")).isEmpty() &&
                        !AnimEditor::f_RepoFile(QStringLiteral("docs/engine/zh_TW/17_SKINNING_TUTORIAL.md")).isEmpty(),
                    "tutorial: docs/engine/17 (both languages) found from the editor's folder");
        l_pEd->f_TutorialActions()[1]->trigger();   // the start file: the golem only
        f_ProcessEvents();
        const Node* l_pG = l_pDoc->f_Clip() ? toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "golem") : nullptr;
        l_bOk &= f_Check(l_pDoc->f_FilePath().endsWith(QStringLiteral("golem_start.anim")) && l_pG && !l_pG->f_Skinned() && l_pG->m_vChildren.empty(),
                    "tutorial: Open Tutorial Start File opens the golem with no bones");
        l_pEd->f_TutorialActions()[2]->trigger();   // the finished file: bones, painted weights, keys, lattice
        f_ProcessEvents();
        l_pG = l_pDoc->f_Clip() ? toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "golem") : nullptr;
        bool l_bPainted = false;
        if (l_pG && l_pDoc->f_Clip()) {
            std::vector<toms::anim::NodePose> l_vPoses;
            toms::anim::f_Evaluate(*l_pDoc->f_Clip(), 0.0f, l_vPoses);
            for (size_t l_nI = 0; l_nI < l_vPoses.size(); l_nI++) {
                toms::anim::SkinView l_oSv;
                if (l_vPoses[l_nI].m_pNode == l_pG &&
                    toms::anim::f_SkinView(*l_pDoc->f_Clip(), l_vPoses, l_nI, l_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), &l_oSv) == toms::anim::SkinState::Ok)
                    l_bPainted = !l_oSv.m_bAutoWeights;
            }
        }
        l_bOk &= f_Check(l_pDoc->f_FilePath().endsWith(QStringLiteral("golem_done.anim")) && l_pG && l_pG->m_vSkinBones.size() == 3 && l_bPainted &&
                        !l_pG->m_vDeformKeys.empty() && toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "chest")->m_vRotKeys.size() == 3,
                    "tutorial: Open Tutorial Finished File: 3 bones, painted weights that fit the mesh, a breath, a sway");
    }
    {   // Preview as Character (docs/engine/05 "Characters"): assets/media/anim/archer_bow.anim shown as other characters.
        const QString l_oArcherFile = QFileInfo(l_oFile).dir().filePath(QStringLiteral("../../assets/media/anim/archer_bow.anim"));
        if (!QFileInfo::exists(l_oArcherFile)) {
            l_bOk &= f_Check(false, "character: assets/media/anim/archer_bow.anim");
        } else if (l_pEd->f_OpenFile(l_oArcherFile, false)) {
            f_ProcessEvents();
            const QStringList l_oWho = l_pDoc->f_CharacterChoices();
            l_bOk &= f_Check(l_oWho.contains(QStringLiteral("player")) && l_oWho.contains(QStringLiteral("player_knight_girl")) && l_oWho.size() >= 11,
                        "character: every player_* sprite is offered");
            l_bOk &= f_Check(l_pDoc->f_DrawClip() == l_pDoc->f_Clip(), "character: as made, the view draws the clip itself");
            const glm::vec3 l_v3HandMade = toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "hand")->m_v3Pos;
            l_pDoc->f_SetPreviewCharacter(QStringLiteral("player_knight_girl"));
            f_ProcessEvents();
            const toms::anim::Clip* l_pShown = l_pDoc->f_DrawClip();
            const toms::anim::Node* l_pBody = l_pShown ? toms::anim::f_FindNode(l_pShown->m_oRoot, "body") : nullptr;
            const toms::anim::Node* l_pHand = l_pShown ? toms::anim::f_FindNode(l_pShown->m_oRoot, "hand") : nullptr;
            l_bOk &= f_Check(l_pShown != l_pDoc->f_Clip() && l_pBody && l_pBody->m_strSprite == "game:player_knight_girl" && l_pHand && l_pHand->m_v3Pos.x > l_v3HandMade.x,
                        "character: the view draws the knight girl, the bow further out (a wider body)");
            l_bOk &= f_Check(l_pDoc->f_DrawClip() == l_pShown, "character: the fitted copy is kept (poses stay valid)");
            l_bOk &= f_Check(toms::anim::f_FindNode(l_pDoc->f_Clip()->m_oRoot, "body")->m_strSprite == "game:player", "character: the clip itself is unchanged");
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("hand")) && l_pDoc->f_DrawNode(l_pDoc->f_SelectedNode()) == l_pHand,
                        "character: the selected node is found in the drawn copy (gizmo, outline)");
            l_bOk &= f_Check(l_pEd->f_Viewport()->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("character_knight.png"))), "character_knight.png");
            l_pDoc->f_SetPreviewCharacter(QString());
            f_ProcessEvents();
            l_bOk &= f_Check(l_pDoc->f_DrawClip() == l_pDoc->f_Clip(), "character: back to as made");
        } else {
            l_bOk &= f_Check(false, "character: open archer_bow.anim");
        }
    }
    {   // Lattice mode (docs/engine/16 step 4): drag a lattice point of a sprite with no deform yet -> a deform key at the playhead.
        const QString l_oDeformFile = QFileInfo(l_oFile).dir().filePath(QStringLiteral("anim_deform.anim"));
        AnimViewport* l_pView = l_pEd->f_Viewport();
        if (!QFileInfo::exists(l_oDeformFile) || !l_pEd->f_OpenFile(l_oDeformFile, false)) {
            l_bOk &= f_Check(false, "lattice: open tests/smoke/anim_deform.anim");
        } else {
            l_pView->resize(640, 480);
            f_ProcessEvents();
            l_pView->f_FrameClip();
            l_pDoc->f_SetTime(0.25f);
            l_pEd->f_LatticeModeAction()->setChecked(true);
            l_bOk &= f_Check(l_pView->f_EditMode() == AnimViewport::EditMode::Lattice && l_pDoc->f_SelectNodeByName(QStringLiteral("plain")),
                        "lattice: L mode, the plain slime selected");
            f_ProcessEvents();
            const toms::anim::Node* l_pPlain = l_pDoc->f_SelectedNode();
            l_bOk &= f_Check(l_pPlain && l_pPlain->m_vDeformKeys.empty() && l_pPlain->f_LatticeCols() == 4, "lattice: no deform yet, 4 x 4");
            // where point 5 (second row, second column) is drawn
            std::vector<toms::anim::NodePose> l_vPoses;
            toms::anim::f_Evaluate(*l_pDoc->f_Clip(), l_pDoc->f_PreviewTime(), l_vPoses);
            toms::anim::LatticeView l_oLv;
            QPointF l_oAt(-1, -1);
            for (size_t l_nI = 0; l_nI < l_vPoses.size(); l_nI++)
                if (l_vPoses[l_nI].m_pNode == l_pPlain && toms::anim::f_LatticeView(l_vPoses, l_nI, l_pDoc->f_AtlasSet(), toms::anim::f_Placement(0, 0), l_oLv))
                    l_oAt = l_pView->f_WidgetPos(QPointF(l_oLv.m_vPoints[5].x, l_oLv.m_vPoints[5].y));
            l_bOk &= f_Check(l_pView->f_LatticePointAt(l_oAt) == 5, "lattice: point 5 is found under its square");
            const int l_iUndoAt = l_pDoc->f_UndoStack()->index();
            auto l_fnSend = [&](QEvent::Type l_eType, const QPointF& l_oPt, Qt::MouseButtons l_oHeld) {
                QMouseEvent l_oE(l_eType, l_oPt, l_pView->mapToGlobal(l_oPt), l_eType == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, l_oHeld, Qt::NoModifier);
                QApplication::sendEvent(l_pView, &l_oE);
                f_ProcessEvents();
            };
            l_fnSend(QEvent::MouseButtonPress, l_oAt, Qt::LeftButton);
            for (int l_iI = 1; l_iI <= 4; l_iI++) l_fnSend(QEvent::MouseMove, l_oAt + QPointF(0, 5.0 * l_iI), Qt::LeftButton);
            l_fnSend(QEvent::MouseButtonRelease, l_oAt + QPointF(0, 20), Qt::NoButton);
            l_pPlain = l_pDoc->f_SelectedNode();
            l_bOk &= f_Check(l_pPlain && l_pPlain->m_vDeformKeys.size() == 1 && std::fabs(l_pPlain->m_vDeformKeys[0].m_fT - 0.25f) < 1e-3f &&
                            l_pPlain->m_vDeformKeys[0].m_oV.count(5) && l_pPlain->m_vDeformKeys[0].m_oV.at(5).y > 0.5f && l_pPlain->m_vDeformKeys[0].m_oV.size() == 1,
                        "lattice: one deform key at the playhead, point 5 moved down");
            l_bOk &= f_Check(l_pDoc->f_UndoStack()->index() == l_iUndoAt + 1, "lattice: the whole drag is one undo step");
            l_bOk &= f_Check(l_pView->grab().save(QDir(l_oOutDir).filePath(QStringLiteral("lattice_drag.png"))), "lattice_drag.png");
            // a smaller lattice keeps the key: point 5 (col 1, row 1 of 4 x 4) -> point 4 (col 1, row 1 of 3 x 3)
            l_bOk &= f_Check(l_pDoc->f_SetLatticeSize(3, 3) && l_pDoc->f_SelectedNode()->f_LatticeCols() == 3 &&
                            l_pDoc->f_SelectedNode()->m_vDeformKeys[0].m_oV.count(4) == 1,
                        "lattice: resized to 3 x 3, the moved point remapped");
            while (l_pDoc->f_UndoStack()->index() > l_iUndoAt) l_pDoc->f_UndoStack()->undo();
            l_bOk &= f_Check(l_pDoc->f_SelectedNode() && l_pDoc->f_SelectedNode()->m_vDeformKeys.empty(), "lattice: undo");
            // A key row shows its time: the slime's empty deform key at t = 1 draws it at rest, also clicked during
            // playback, and also when the playhead already is at 1 with a preview run on past it (looping nodes).
            l_bOk &= f_Check(l_pDoc->f_SelectNodeByName(QStringLiteral("slime")), "lattice: the deformed slime selected");
            QTableWidget* l_pKeys = l_pEd->f_KeyListDock()->f_Table();
            auto l_fnWait = [&](int l_iMs) { QElapsedTimer l_oW; l_oW.start(); while (l_oW.elapsed() < l_iMs) f_ProcessEvents(); };
            auto l_fnRowAt = [&](const QString& l_oT) {
                for (int l_iR = 0; l_iR < l_pKeys->rowCount(); l_iR++)
                    if (l_pKeys->item(l_iR, 1) && l_pKeys->item(l_iR, 1)->text() == l_oT) return l_iR;
                return -1;
            };
            auto l_fnClick = [&](int l_iRow) {
                const QRect l_oRc = l_pKeys->visualRect(l_pKeys->model()->index(l_iRow, 1));
                for (QEvent::Type l_eType : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
                    QMouseEvent l_oE(l_eType, QPointF(l_oRc.center()), l_pKeys->viewport()->mapToGlobal(QPointF(l_oRc.center())), Qt::LeftButton,
                                  l_eType == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(l_pKeys->viewport(), &l_oE);
                }
            };
            auto l_fnMostShown = [&] {   // the largest offset of the slime's lattice on screen
                std::vector<toms::anim::NodePose> l_vPs;
                toms::anim::f_Evaluate(*l_pDoc->f_Clip(), l_pDoc->f_PreviewTime(), l_vPs);
                float l_fMost = 0;
                for (const auto& l_oP : l_vPs)
                    if (l_oP.m_pNode == l_pDoc->f_SelectedNode())
                        for (const glm::vec2& l_v2O : toms::anim::f_DeformAt(*l_oP.m_pNode, l_oP.m_fTime)) l_fMost = std::max(l_fMost, glm::length(l_v2O));
                return l_fMost;
            };
            const int l_iHalfRow = l_fnRowAt(QStringLiteral("0.500")), l_iLastRow = l_fnRowAt(QStringLiteral("1.000"));
            l_pEd->f_Transport()->f_Play();
            l_fnWait(200);
            if (l_iHalfRow >= 0) l_fnClick(l_iHalfRow);
            l_fnWait(100);
            l_bOk &= f_Check(l_iHalfRow >= 0 && !l_pEd->f_Transport()->f_IsPlaying() && l_pDoc->f_PreviewTime() == 0.5f && l_fnMostShown() > 5,
                        "lattice: the t = 0.5 row clicked during playback: paused there, deformed");
            if (l_iLastRow >= 0) l_fnClick(l_iLastRow);
            l_fnWait(100);
            l_bOk &= f_Check(l_iLastRow >= 0 && l_pDoc->f_PreviewTime() == 1.0f && l_fnMostShown() < 1e-4f,
                        "lattice: the t = 1 row (an empty deform key): the slime at rest");
            // What is on the screen (not the widget's own grab: the game renderer presents to the window): one click
            // = one repaint, and that frame must be the one shown, not the frame before it (bgfx presents a frame at
            // the next bgfx::frame() unless flip-after-render is on).
            if (l_pView->f_UsingGameRenderer()) {
                // One repaint, captured before anything else can paint again: a click can bring two (the press
                // moves the row, the release clicks it), which would hide a frame shown one late.
                auto l_fnSettle = [&] { for (int l_iI = 0; l_iI < 3; l_iI++) { l_pView->repaint(); l_fnWait(50); } };
                l_pDoc->f_SetTime(0.5f);
                l_fnSettle();
                const QImage l_oHalf = f_PresentedImage(l_pView);
                l_pDoc->f_SetTime(1.0f);
                l_pView->repaint();
                const QImage l_oOnce = f_PresentedImage(l_pView);
                l_fnSettle();
                const QImage l_oSettled = f_PresentedImage(l_pView);
                l_bOk &= f_Check(!l_oOnce.isNull() && l_oOnce != l_oHalf && l_oOnce == l_oSettled,
                            "lattice: one repaint puts the new time on the screen (not the frame before)");
            }
            l_pDoc->f_SetPreviewTime(1.5f);   // as playback leaves it when looping nodes run on past the end
            if (l_iLastRow >= 0) l_fnClick(l_iLastRow);
            l_bOk &= f_Check(l_pDoc->f_PreviewTime() == 1.0f && l_fnMostShown() < 1e-4f, "lattice: clicking the playhead's own row ends a run-on preview");
            while (l_pDoc->f_UndoStack()->index() > l_iUndoAt) l_pDoc->f_UndoStack()->undo();
            l_pEd->f_ObjectModeAction()->setChecked(true);
        }
    }
    std::printf("selftest: %s\n", l_bOk ? "ok" : "FAILED");
    return l_bOk ? 0 : 1;
}

// Right-drag on a canvas: it must pan (the content moves with the mouse), as middle-drag does.
static bool f_RightDragPans(CanvasView* l_pView)
{
    const QPointF l_oBefore = l_pView->f_ContentToWidget(QPointF(0, 0));
    const QPointF l_oA(l_pView->width() / 2.0, l_pView->height() / 2.0), l_oB = l_oA + QPointF(60, 25);
    auto l_fnSend = [&](QEvent::Type l_eType, const QPointF& l_oAt, Qt::MouseButton l_eButton, Qt::MouseButtons l_oButtons) {
        QMouseEvent l_oE(l_eType, l_oAt, l_pView->mapToGlobal(l_oAt), l_eButton, l_oButtons, Qt::NoModifier);
        QApplication::sendEvent(l_pView, &l_oE);
    };
    l_fnSend(QEvent::MouseButtonPress, l_oA, Qt::RightButton, Qt::RightButton);
    for (int l_iI = 1; l_iI <= 4; l_iI++) l_fnSend(QEvent::MouseMove, l_oA + (l_oB - l_oA) * (l_iI / 4.0), Qt::NoButton, Qt::RightButton);
    l_fnSend(QEvent::MouseButtonRelease, l_oB, Qt::RightButton, Qt::NoButton);
    QCoreApplication::processEvents();
    const QPointF l_oMoved = l_pView->f_ContentToWidget(QPointF(0, 0)) - l_oBefore;
    return std::fabs(l_oMoved.x() - 60) < 1 && std::fabs(l_oMoved.y() - 25) < 1;
}

// The viewport with the game renderer (on screen): it starts, draws, and a bgfx screenshot of the
// clip at a fixed time is saved for comparing with toms_game --anim.
int f_RunGpuSelfTest(AnimMainWindow& l_oW, const QString& l_oFile, const QString& l_oOutDir)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(l_oOutDir);
    AnimEditor* l_pEd = l_oW.f_Editor();
    auto l_fnPump = [](int l_iMs) {
        QElapsedTimer l_oT;
        l_oT.start();
        while (l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    bool l_bOk = f_Check(l_pEd->f_OpenFile(l_oFile, false), "open the clip");
    l_fnPump(500);
    AnimViewport* l_pView = l_pEd->f_Viewport();
    l_bOk &= f_Check(l_pView->f_UsingGameRenderer(), "the viewport runs on the game renderer");
    std::printf("selftest:   %s\n", l_pView->f_RendererName().toUtf8().constData());
    if (!l_pView->f_UsingGameRenderer()) return 1;
    l_pEd->f_Document()->f_SetTime(0.5f);
    const int l_iF0 = l_pView->f_FramesDrawn();
    l_fnPump(600);
    l_bOk &= f_Check(l_pView->f_FramesDrawn() > l_iF0, "frames are drawn");
    l_bOk &= f_Check(f_RightDragPans(l_pView), "right-drag pans the viewport (game renderer window)");
    const int l_iBefore = toms::next::f_BgfxHostScreenshotsWritten();
    l_pView->f_SaveGameRendererShot(QDir(l_oOutDir).filePath(QStringLiteral("anim_gpu.png")));
    QElapsedTimer l_oT;
    l_oT.start();
    while (toms::next::f_BgfxHostScreenshotsWritten() == l_iBefore && l_oT.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    l_bOk &= f_Check(toms::next::f_BgfxHostScreenshotsWritten() > l_iBefore, "anim_gpu.png (bgfx screenshot)");
    auto l_fnShot = [&](const char* l_pszName) {
        const int l_iN = toms::next::f_BgfxHostScreenshotsWritten();
        l_pView->f_SaveGameRendererShot(QDir(l_oOutDir).filePath(QString::fromLatin1(l_pszName)));
        QElapsedTimer l_oSt;
        l_oSt.start();
        while (toms::next::f_BgfxHostScreenshotsWritten() == l_iN && l_oSt.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return toms::next::f_BgfxHostScreenshotsWritten() > l_iN;
    };
    // Double-click another clip in the Clips dock (real mouse events): the viewport shows that clip.
    if (l_pEd->f_Document()->f_File().m_vClips.size() > 1) {
        auto* l_pList = l_pEd->f_ClipsDock()->findChild<QListWidget*>();
        l_pEd->f_ClipsDock()->show();
        l_pEd->f_ClipsDock()->raise();
        l_fnPump(200);
        const QPoint l_oAt = l_pList->visualItemRect(l_pList->item(1)).center();
        for (QEvent::Type l_eType : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease, QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease}) {
            QMouseEvent l_oE(l_eType, QPointF(l_oAt), l_pList->viewport()->mapToGlobal(QPointF(l_oAt)), Qt::LeftButton,
                          l_eType == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(l_pList->viewport(), &l_oE);
        }
        const int l_iF1 = l_pView->f_FramesDrawn();
        l_fnPump(600);
        l_bOk &= f_Check(l_pEd->f_Document()->f_ClipIndex() == 1, "double-click opens clip 2");
        l_bOk &= f_Check(l_pView->f_FramesDrawn() > l_iF1, "the viewport redraws after the switch");
        l_bOk &= f_Check(l_fnShot("anim_clip2.png"), "anim_clip2.png (the second clip)");
    }
    // Solo the first child of the root's first child (in anim_child_timing's second clip: the coin
    // and the gem it carries): the player is not drawn, the coin and gem are, in their place.
    {
        AnimDocument* l_pDoc = l_pEd->f_Document();
        const toms::anim::Clip* l_pC = l_pDoc->f_Clip();
        if (l_pC && !l_pC->m_oRoot.m_vChildren.empty() && !l_pC->m_oRoot.m_vChildren[0].m_vChildren.empty()) {
            const NodePath l_vSolo{0, 0};
            l_pDoc->f_SetSolo(l_vSolo);
            const toms::anim::Node& l_oParent = l_pC->m_oRoot.m_vChildren[0];
            const toms::anim::Node& l_oKid = l_oParent.m_vChildren[0];
            l_bOk &= f_Check(l_pDoc->f_SoloShows(&l_oKid) && !l_pDoc->f_SoloShows(&l_oParent) && !l_pDoc->f_SoloShows(&l_pC->m_oRoot) &&
                            (l_oKid.m_vChildren.empty() || l_pDoc->f_SoloShows(&l_oKid.m_vChildren[0])),
                        "solo: the node and its children are drawn, its parents not");
            l_fnPump(300);
            l_bOk &= f_Check(l_fnShot("anim_solo.png"), "anim_solo.png (solo)");
            l_pDoc->f_SetClipIndex(0);
            l_bOk &= f_Check(!l_pDoc->f_Solo(), "another clip ends the solo");
        }
    }
    std::printf("selftest: %s\n", l_bOk ? "ok" : "FAILED");
    return l_bOk ? 0 : 1;
}

// anim_editor --render file.anim clip outdir t1,t2,... [held.anim]: the clip at those times through the game renderer,
// framed once for the whole clip, as outdir/<clip>_<ms>.png (a held file: its clip of the same name at `hand`).
int f_RunRender(AnimMainWindow& l_oW, const QStringList& l_oA)
{
    qApp->setProperty("toms.selftest", true);
    QDir().mkpath(l_oA[2]);
    AnimEditor* l_pEd = l_oW.f_Editor();
    auto l_fnPump = [](int l_iMs) {
        QElapsedTimer l_oT;
        l_oT.start();
        while (l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    if (!l_pEd->f_OpenFile(l_oA[0], false)) { std::fprintf(stderr, "render: cannot open %s\n", qPrintable(l_oA[0])); return 2; }
    AnimDocument* l_pDoc = l_pEd->f_Document();
    int l_iIndex = -1;
    for (size_t l_nI = 0; l_nI < l_pDoc->f_File().m_vClips.size(); l_nI++)
        if (QString::fromStdString(l_pDoc->f_File().m_vClips[l_nI].m_strName) == l_oA[1]) l_iIndex = int(l_nI);
    if (l_iIndex < 0) { std::fprintf(stderr, "render: no clip %s\n", qPrintable(l_oA[1])); return 2; }
    l_pDoc->f_SetClipIndex(l_iIndex);
    QString l_oErr;
    if (l_oA.size() > 4 && !l_pDoc->f_SetHeldFile(l_oA[4], &l_oErr)) { std::fprintf(stderr, "render: %s\n", qPrintable(l_oErr)); return 2; }
    for (QDockWidget* l_pD : l_oW.findChildren<QDockWidget*>()) l_pD->hide();   // the viewport alone
    l_fnPump(500);
    AnimViewport* l_pView = l_pEd->f_Viewport();
    if (!l_pView->f_UsingGameRenderer()) { std::fprintf(stderr, "render: no game renderer\n"); return 1; }
    l_pView->f_FrameClip();
    // TOMS_RENDER_ZOOM: times the framed zoom, around the view's centre (a clip with a screen-sized node frames far out)
    if (const double l_dZ = qEnvironmentVariable("TOMS_RENDER_ZOOM").toDouble(); l_dZ > 0) l_pView->f_SetZoom(l_pView->f_Zoom() * l_dZ);
    int l_iFailed = 0;
    for (const QString& l_oTs : l_oA[3].split(QLatin1Char(','))) {
        const float l_fT = l_oTs.toFloat();
        l_pDoc->f_SetTime(l_fT);
        l_fnPump(250);
        const int l_iN = toms::next::f_BgfxHostScreenshotsWritten();
        const QString l_oPng = QDir(l_oA[2]).filePath(QStringLiteral("%1_%2.png").arg(l_oA[1]).arg(int(std::lround(l_fT * 1000)), 5, 10, QLatin1Char('0')));
        l_pView->f_SaveGameRendererShot(l_oPng);
        QElapsedTimer l_oSt;
        l_oSt.start();
        while (toms::next::f_BgfxHostScreenshotsWritten() == l_iN && l_oSt.elapsed() < 3000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (toms::next::f_BgfxHostScreenshotsWritten() == l_iN) l_iFailed++;
        else std::printf("%s\n", qPrintable(l_oPng));
    }
    return l_iFailed ? 1 : 0;
}

// anim_editor --export file.anim outdir [gif|apng|png] [clip,clip,...] [held.anim]: those clips (default: all) as
// animated pictures through the game renderer (File > Export Clips as Animation), 30 fps, half size, transparent. Each
// GIF is read back with Qt's GIF reader: it must decode with one picture per frame.
int f_RunExport(AnimMainWindow& l_oW, const QStringList& l_oA)
{
    qApp->setProperty("toms.selftest", true);
    AnimEditor* l_pEd = l_oW.f_Editor();
    auto l_fnPump = [](int l_iMs) {
        QElapsedTimer l_oT;
        l_oT.start();
        while (l_oT.elapsed() < l_iMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    };
    if (!l_pEd->f_OpenFile(l_oA[0], false)) { std::fprintf(stderr, "export: cannot open %s\n", qPrintable(l_oA[0])); return 2; }
    AnimDocument* l_pDoc = l_pEd->f_Document();
    animexport::Options l_oOpt;
    const QString l_oFormat = l_oA.size() > 2 ? l_oA[2] : QStringLiteral("gif");
    l_oOpt.m_eFormat = l_oFormat == QLatin1String("apng") ? animexport::Format::Apng
               : l_oFormat == QLatin1String("png")  ? animexport::Format::PngFrames : animexport::Format::Gif;
    QString l_oErr;
    if (l_oA.size() > 4 && !l_pDoc->f_SetHeldFile(l_oA[4], &l_oErr)) { std::fprintf(stderr, "export: %s\n", qPrintable(l_oErr)); return 2; }
    l_fnPump(500);
    AnimViewport* l_pView = l_pEd->f_Viewport();
    if (!l_pView->f_UsingGameRenderer()) { std::fprintf(stderr, "export: no game renderer\n"); return 1; }
    const QStringList l_oOnly = l_oA.size() > 3 && !l_oA[3].isEmpty() ? l_oA[3].split(QLatin1Char(',')) : QStringList();
    int l_iFailed = 0;
    for (const toms::anim::Clip& l_oC : l_pDoc->f_File().m_vClips) {
        if (!l_oOnly.isEmpty() && !l_oOnly.contains(QString::fromStdString(l_oC.m_strName))) continue;
        const QString l_oP = animexport::f_ExportClip(l_pView, *l_pDoc, l_oC, l_oOpt, l_oA[1], &l_oErr);
        if (l_oP.isEmpty()) { std::fprintf(stderr, "export: %s: %s\n", l_oC.m_strName.c_str(), qPrintable(l_oErr)); l_iFailed++; continue; }
        if (l_oOpt.m_eFormat == animexport::Format::Gif) {   // it must read back: as many pictures as frames
            QImageReader l_oR(l_oP);
            int l_iFrames = 0;
            while (l_oR.canRead() && !l_oR.read().isNull()) l_iFrames++;
            const int l_iWant = int(std::lround(l_oC.f_Duration() * l_oOpt.m_iFps)) + (l_oC.m_iPlayCount >= 0 ? 1 : 0);
            if (l_iFrames != std::max(1, l_iWant)) {
                std::fprintf(stderr, "export: %s: %d picture(s) read back, %d expected\n", qPrintable(l_oP), l_iFrames, l_iWant);
                l_iFailed++;
                continue;
            }
        }
        std::printf("%s\n", qPrintable(QDir::toNativeSeparators(l_oP)));
    }
    return l_iFailed ? 1 : 0;
}

}  // namespace

int main(int l_iArgc, char** l_pArgv)
{
    const std::vector<std::string> l_vArgs = Console::f_Utf8Args(l_iArgc, l_pArgv);
    if (!l_vArgs.empty() && l_vArgs[0] == "--headless") {
        Console::f_AttachParent();
        const int l_iCode = animed::f_HeadlessMain(std::vector<std::string>(l_vArgs.begin() + 1, l_vArgs.end()));
        std::fflush(stdout);
        std::fflush(stderr);
        return l_iCode;
    }

    QApplication l_oApp(l_iArgc, l_pArgv);
    QApplication::setOrganizationName(QStringLiteral("TOMS"));
    QApplication::setApplicationName(QStringLiteral("AnimEditor"));
    QApplication::setApplicationDisplayName(QStringLiteral("Anim Editor"));
    QApplication::setWindowIcon(Icons::f_Icon(Icons::Id::Rotate));
    Theme::f_Apply(Theme::Mode::Dark);
    Language::f_Init();   // English / 中文 (tools/studio_common/Language.h), before any window

    const QStringList l_oQargs = QCoreApplication::arguments().mid(1);
    if (l_oQargs.size() == 3 && l_oQargs[0] == QLatin1String("--selftest")) {
        Console::f_AttachParent();
        AnimMainWindow l_oW;
        l_oW.f_SetDarkTheme(true);
        l_oW.resize(1440, 900);
        l_oW.show();
        const int l_iCode = f_RunSelfTest(l_oW, l_oQargs[1], l_oQargs[2]);
        std::fflush(stdout);
        return l_iCode;
    }
    if (l_oQargs.size() == 3 && l_oQargs[0] == QLatin1String("--selftest-gpu")) {   // on screen: the game renderer
        Console::f_AttachParent();
        qApp->setProperty("toms.selftest", true);
        AnimMainWindow l_oW;
        l_oW.f_SetDarkTheme(true);
        l_oW.resize(1440, 900);
        l_oW.show();
        const int l_iCode = f_RunGpuSelfTest(l_oW, l_oQargs[1], l_oQargs[2]);
        std::fflush(stdout);
        return l_iCode;
    }
    if (l_oQargs.size() >= 3 && l_oQargs[0] == QLatin1String("--export")) {   // on screen: the game renderer
        Console::f_AttachParent();
        qApp->setProperty("toms.selftest", true);
        AnimMainWindow l_oW;
        l_oW.f_SetDarkTheme(true);
        l_oW.resize(900, 700);
        l_oW.show();
        const int l_iCode = f_RunExport(l_oW, l_oQargs.mid(1));
        std::fflush(stdout);
        return l_iCode;
    }
    if (l_oQargs.size() >= 5 && l_oQargs[0] == QLatin1String("--render")) {   // on screen: the game renderer
        Console::f_AttachParent();
        qApp->setProperty("toms.selftest", true);
        AnimMainWindow l_oW;
        l_oW.f_SetDarkTheme(true);
        l_oW.resize(1100, 1000);
        l_oW.show();
        const int l_iCode = f_RunRender(l_oW, l_oQargs.mid(1));
        std::fflush(stdout);
        return l_iCode;
    }
    AnimMainWindow l_oW;
    l_oW.show();
    if (!l_oQargs.isEmpty()) l_oW.f_Editor()->f_OpenFile(l_oQargs.first());
    return l_oApp.exec();
}

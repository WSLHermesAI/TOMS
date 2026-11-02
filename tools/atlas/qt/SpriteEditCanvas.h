#pragma once

// Inside the TOMS build the canvases draw their image with the game's renderer (GameCanvasView,
// tools/studio_common's studio_bgfx); a standalone build of the atlas tool has no game engine and
// paints with QPainter (CanvasView).
#if TOMS_ATLAS_GAME_RENDERER
#include "GameCanvasView.h"
using AtlasCanvasBase = GameCanvasView;
#else
#include "CanvasView.h"
using AtlasCanvasBase = CanvasView;
#endif

#include <QImage>
#include <QRect>
#include <QVector>

class AtlasDocument;

// Sprite edit mode: one sprite alone at its original size, with its child sprites.
//   - drag on empty pixels: draw a new child rect (asks for the name)
//   - click a child: select it; drag it to move, drag one of its 8 handles to resize
//   - drag the pivot marker; drag the 9-slice guides (when the sprite has 9-slice borders)
//   - arrow keys nudge the selected child (Shift: 10 px); double-click a child to edit inside it;
//     Esc goes back up to the parent, and from an image sprite back to the atlas
// Every coordinate snaps to whole pixels (the pivot to half pixels) and is committed on release
// as one undo step.
class SpriteEditCanvas : public AtlasCanvasBase
{
    Q_OBJECT

public:
    // Drawn with the game's renderer (GameCanvasView) unless the setting "preview/gameRenderer" is off.
    explicit SpriteEditCanvas(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);
    ~SpriteEditCanvas() override;

    QString f_Target() const { return m_oTarget; }
    void f_SetTarget(const QString& l_oName);

signals:
    void f_TargetChanged(const QString& l_oName);
    void f_ExitRequested();
    void f_ContextMenuRequested(const QPoint& l_oGlobalPos);

protected:
    QSize f_ContentSize() const override { return m_oImage.size(); }
    void f_PaintContent(QPainter& l_oP) override;
#if TOMS_ATLAS_GAME_RENDERER
    void f_PaintGameScene(const GameScene& l_oS) override;   // the image, as the game samples it
    void f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) override { m_oTex.f_Attach(l_pRen); }
#endif
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void mouseDoubleClickEvent(QMouseEvent* l_pE) override;
    void keyPressEvent(QKeyEvent* l_pE) override;
    void contextMenuEvent(QContextMenuEvent* l_pE) override;

private:
    struct Child {
        QString m_oName;
        QRect m_oRect;   // in the target's pixels
    };

    void f_Reload();
    QVector<Child> f_Children() const;
    // The sprite whose pivot and 9-slice are shown: the current child, or the target itself.
    QString f_Focus() const;
    QRect f_FocusRect() const;
    QPointF f_PivotPoint(const QString& l_oName, const QRect& l_oR) const;   // content coordinates
    int f_ChildAt(const QVector<Child>& l_oKids, const QPointF& l_oC) const;
    int f_HandleAt(const QRect& l_oR, const QPointF& l_oWidgetPos) const;     // 0..7 clockwise from top-left, -1
    int f_SplitAt(const QPointF& l_oWidgetPos) const;                       // 0 left, 1 right, 2 top, 3 bottom, -1
    QPointF f_HandlePos(const QRect& l_oR, int l_iHandle) const;              // widget coordinates
    QRect f_ClampToImage(QRect l_oR) const;
    void f_UpdateCursor(const QPointF& l_oWidgetPos);
    void f_CommitDrag();
    void f_Nudge(int l_iDx, int l_iDy);

    enum class Drag { None, Create, Move, Resize, Pivot, Split };

    AtlasDocument* m_pDoc;
    QString m_oTarget;
    QImage m_oImage;

    Drag m_eDrag = Drag::None;
    QString m_oDragName;
    QPointF m_oPressContent;
    QRect m_oStartRect, m_oLiveRect;
    int m_iHandle = -1;
    int m_iSplitSide = -1;
    int m_arrLiveSplit[4] = {0, 0, 0, 0};
    QPointF m_oLivePivot;   // 0..1 of the focus rect
#if TOMS_ATLAS_GAME_RENDERER
    GameTextures m_oTex;   // the image as a game-renderer texture
#endif
};

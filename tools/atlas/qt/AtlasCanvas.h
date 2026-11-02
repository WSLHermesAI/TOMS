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

#include <QSet>
#include <QStringList>

class AtlasDocument;
namespace atlas { struct Region; }

// One atlas page as the last build laid it out: page pixels on a checkerboard, image sprites
// outlined solid, child sprites dashed, dedupe aliases badged, pinned sprites marked.
// Click/rubber-band selects (kept in sync with the Sprites dock through the document); dragging
// an image sprite moves it and pins it there; double-click opens it in sprite edit mode.
class AtlasCanvas : public AtlasCanvasBase
{
    Q_OBJECT

public:
    // Drawn with the game's renderer (GameCanvasView) unless the setting "preview/gameRenderer" is off.
    explicit AtlasCanvas(AtlasDocument* l_pDoc, QWidget* l_pParent = nullptr);
    ~AtlasCanvas() override;

    int f_Page() const { return m_iPage; }
    void f_SetPage(int l_iPage);
    void f_SetShowOutlines(bool l_bShow);
    void f_SetShowChildren(bool l_bShow);

signals:
    void f_PageChanged(int l_iPage);
    void f_EditSpriteRequested(const QString& l_oName);
    void f_ContextMenuRequested(const QPoint& l_oGlobalPos);

protected:
    QSize f_ContentSize() const override;
    void f_PaintContent(QPainter& l_oP) override;
#if TOMS_ATLAS_GAME_RENDERER
    void f_PaintGameScene(const GameScene& l_oS) override;   // the image, as the game samples it
    void f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) override { m_oTex.f_Attach(l_pRen); }
#endif
    bool event(QEvent* l_pE) override;   // tooltips
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void mouseDoubleClickEvent(QMouseEvent* l_pE) override;
    void contextMenuEvent(QContextMenuEvent* l_pE) override;
    void leaveEvent(QEvent* l_pE) override;

private:
    void f_OnBuildFinished();
    void f_OnSelectionChanged();
    void f_RefreshPinned();
    // The sprite under a content point: aliases resolve to the region that owns the pixels, and
    // the smallest rect wins so children can be picked inside their parent.
    QString f_HitTest(const QPointF& l_oContent) const;
    bool f_IsVisible(const atlas::Region& l_oR) const;
    bool f_IsMovable(const atlas::Region& l_oR) const;   // has its own place in the atlas
    QString f_TooltipFor(const QString& l_oName) const;
    void f_CommitMove();

    enum class Drag { None, Pending, Move, Rubber };

    AtlasDocument* m_pDoc;
    int m_iPage = 0;
    bool m_bShowOutlines = true;
    bool m_bShowChildren = true;
    QSet<QString> m_oPinned;
    QString m_oHover;

    Drag m_eDrag = Drag::None;
    QPointF m_oPressWidget;          // where the button went down (widget)
    QPointF m_oPressContent;
    QPoint m_oMoveDelta;             // whole pixels
    QStringList m_oDragNames;
    QRectF m_oRubber;                // content
    QStringList m_oSelectionAtPress;
#if TOMS_ATLAS_GAME_RENDERER
    GameTextures m_oTex;   // the image as a game-renderer texture
#endif
};

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
    explicit AtlasCanvas(AtlasDocument* doc, QWidget* parent = nullptr);
    ~AtlasCanvas() override;

    int page() const { return m_page; }
    void setPage(int page);
    void setShowOutlines(bool show);
    void setShowChildren(bool show);

signals:
    void pageChanged(int page);
    void editSpriteRequested(const QString& name);
    void contextMenuRequested(const QPoint& globalPos);

protected:
    QSize contentSize() const override;
    void paintContent(QPainter& p) override;
#if TOMS_ATLAS_GAME_RENDERER
    void paintGameScene(const GameScene& s) override;   // the image, as the game samples it
    void gameRendererChanged(toms::next::BgfxRenderer* ren) override { m_tex.attach(ren); }
#endif
    bool event(QEvent* e) override;   // tooltips
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void onBuildFinished();
    void onSelectionChanged();
    void refreshPinned();
    // The sprite under a content point: aliases resolve to the region that owns the pixels, and
    // the smallest rect wins so children can be picked inside their parent.
    QString hitTest(const QPointF& content) const;
    bool isVisible(const atlas::Region& r) const;
    bool isMovable(const atlas::Region& r) const;   // has its own place in the atlas
    QString tooltipFor(const QString& name) const;
    void commitMove();

    enum class Drag { None, Pending, Move, Rubber };

    AtlasDocument* m_doc;
    int m_page = 0;
    bool m_showOutlines = true;
    bool m_showChildren = true;
    QSet<QString> m_pinned;
    QString m_hover;

    Drag m_drag = Drag::None;
    QPointF m_pressWidget;          // where the button went down (widget)
    QPointF m_pressContent;
    QPoint m_moveDelta;             // whole pixels
    QStringList m_dragNames;
    QRectF m_rubber;                // content
    QStringList m_selectionAtPress;
#if TOMS_ATLAS_GAME_RENDERER
    GameTextures m_tex;   // the image as a game-renderer texture
#endif
};

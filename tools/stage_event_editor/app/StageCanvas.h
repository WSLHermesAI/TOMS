// StageCanvas.h -- the floor's map, drawn by the game's own renderer (GameCanvasView: toms_game's BgfxRenderer).
//
// The ground, then every object with its look in row order (a 2-wide monster spills right and down, as in the game):
// a look's animation ("anim/<file>.anim#<clip>") and the monsters' idle clips play, as Game::drawObjectAnim /
// drawIdleAnim do; a tint multiplies the sprite; a gate gets its key's colour washed over it; events are the game's
// story plates. The editor's marks (grid, ✎ ▶ ⚡ badges, unreachable objects, safe cells, the brush's ghost, the
// selection) are painted over it with QPainter. 🎮 Game view leaves the marks out. Without a GPU the whole map is
// painted with QPainter (still sprites).
//
// Mouse: left = the tool (StageEditor), right / middle drag or Space + left = pan, a right click = eyedropper, wheel =
// zoom around the cursor.
#pragma once

#include "GameCanvasView.h"
#include "stage_project.h"

#include <QElapsedTimer>
#include <QTimer>

#include <map>
#include <memory>
#include <optional>
#include <string>

class StageEditor;

class StageCanvas : public GameCanvasView {
    Q_OBJECT
public:
    explicit StageCanvas(StageEditor* editor, QWidget* parent = nullptr);
    ~StageCanvas() override;

    std::optional<stage::Cell> cellAt(const QPointF& widgetPos) const;
    void showCell(const stage::Cell& c);   // scrolls it into view
    void floorChanged() { requestFit(); }
    int animatedObjects() const { return m_animated; }

protected:
    QSize contentSize() const override;
    bool drawCheckerboard() const override { return false; }
    void paintGameScene(const GameScene& s) override;
    void gameRendererChanged(toms::next::BgfxRenderer* ren) override;
    void paintContent(QPainter& p) override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    struct AnimTex;
    void paintSceneQt(QPainter& p);   // no GPU: the map with QPainter
    void paintMarks(QPainter& p);
    QRectF cellRect(int x, int y, int w = 1, int h = 1) const;
    // (GPU) a clip in a widget rectangle, as Game::drawClipInRect; false when it draws nothing
    bool drawClip(const GameScene& s, const std::string& ref, const QRectF& r, int tx, int ty, const float tint[4]);
    void drawSprite(const GameScene& s, const QString& sprite, const QRectF& r, const float tint[4]);

    StageEditor* m_ed;
    toms::next::BgfxRenderer* m_ren = nullptr;
    std::unique_ptr<GameTextures> m_tex;                     // the game atlas
    std::map<std::string, std::unique_ptr<GameTextures>> m_animTex;   // per .anim file: its atlases
    bool m_atlasDirty = true;
    QElapsedTimer m_clock;
    QTimer m_tick;
    int m_animated = 0;                                      // clips drawn in the last frame
};

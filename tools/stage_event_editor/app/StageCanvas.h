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

#include <array>

#include <map>
#include <memory>
#include <optional>
#include <string>

class StageEditor;
using stage::Json;

class StageCanvas : public GameCanvasView {
    Q_OBJECT
public:
    explicit StageCanvas(StageEditor* l_pEditor, QWidget* l_pParent = nullptr);
    ~StageCanvas() override;

    std::optional<stage::Cell> f_CellAt(const QPointF& l_oWidgetPos) const;
    void f_ShowCell(const stage::Cell& l_oC);   // scrolls it into view
    void f_FloorChanged() { f_RequestFit(); }
    int f_AnimatedObjects() const { return m_iAnimated; }

protected:
    QSize f_ContentSize() const override;
    bool f_DrawCheckerboard() const override { return false; }
    void f_PaintGameScene(const GameScene& l_oS) override;
    void f_GameRendererChanged(toms::next::BgfxRenderer* l_pRen) override;
    void f_PaintContent(QPainter& l_oP) override;

    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void leaveEvent(QEvent* l_pE) override;
    void contextMenuEvent(QContextMenuEvent* l_pE) override;

private:
    struct AnimTex;
    // the object under the mouse while moving it (its own look) or the brush's (its type's look), where it would go
    struct Ghost {
        bool m_bShow = false;
        std::string m_strSem;
        stage::Json m_jLook;
        bool m_bOwnSprite = false;
        stage::Cell m_oFrom;           // the moved object's cell (its animation phase), else the hover cell
        QRectF m_oRect;
        stage::PlaceCheck m_oChk;
    };
    Ghost f_Ghost() const;
    // (GPU) one object: its animation, else its sprite, tinted; alpha < 1 for the ghost
    void f_DrawEntity(const GameScene& l_oS, const std::string& l_strSem, const stage::Json& l_jLook, bool l_bOwnSprite, const QRectF& l_oR, int l_iTx, int l_iTy,
                    float l_fAlpha = 1.0f);
    void f_PaintSceneQt(QPainter& l_oP);   // no GPU: the map with QPainter
    void f_PaintMarks(QPainter& l_oP);
    QRectF f_CellRect(int l_iX, int l_iY, int l_iW = 1, int l_iH = 1) const;
    // (GPU) a clip in a widget rectangle, as Game::drawClipInRect; false when it draws nothing
    bool f_DrawClip(const GameScene& l_oS, const std::string& l_strRef, const QRectF& l_oR, int l_iTx, int l_iTy, const float l_arrTint[4]);
    void f_DrawSprite(const GameScene& l_oS, const QString& l_oSprite, const QRectF& l_oR, const float l_arrTint[4]);

    StageEditor* m_pEd;
    toms::next::BgfxRenderer* m_pRen = nullptr;
    std::unique_ptr<GameTextures> m_upTex;                     // the game atlas
    std::map<std::string, std::unique_ptr<GameTextures>> m_mapAnimTex;   // per .anim file: its atlases
    bool m_bAtlasDirty = true;
    QElapsedTimer m_oClock;
    QTimer m_oTick;
    int m_iAnimated = 0;                                      // clips drawn in the last frame
};

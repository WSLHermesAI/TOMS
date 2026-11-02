// bgfx_viewport.h -- a QWidget that bgfx renders into, running the real game (GameSession).
//
// How the embedding works:
//   * WA_NativeWindow gives the widget its own HWND; WA_PaintOnScreen + a null paintEngine()
//     stop Qt from painting over it. bgfx gets winId() as its native window handle.
//   * bgfx is initialized single-threaded on the first showEvent (a real HWND exists by then).
//   * A QTimer drives frames: GameSession::frame() then bgfx::frame().
//   * resizeEvent -> bgfx::reset with the size in device pixels (devicePixelRatio aware).
// Keep this widget where its native parent does not change (the central widget or a fixed tab):
// re-parenting recreates the HWND and would need a bgfx reset with the new handle.
#pragma once
#include "game_session.h"

#include <QElapsedTimer>
#include <QString>
#include <QTimer>
#include <QWidget>

class BgfxViewport : public QWidget {
    Q_OBJECT
public:
    explicit BgfxViewport(QWidget* l_pParent = nullptr);
    ~BgfxViewport() override;

    QPaintEngine* paintEngine() const override { return nullptr; }

    bool f_RestartGame(const QString& l_oStage = QStringLiteral("stage01"));
    void f_LoadStage(const QString& l_oId);
    // A test scenario (docs/gameplay/08_TEST_SCENARIOS.md): a fresh game, straight in the scenario's scene. When the view is not
    // up yet the scenario waits and starts with the game. False (with the reason) when the scenario cannot start.
    bool f_StartScenario(const QString& l_oPath, QString& l_oError);
    void f_SetShowStats(bool l_bOn);
    // Smoke test: after `frames` frames, save the viewport (bgfx screenshot) to `png` and emit smokeTestDone.
    void f_RunSmokeTest(int l_iFrames, const QString& l_oPng);
    void f_SetDebugOverlay(bool l_bOn);
    // The game's speed (the battle editor): 0 pauses it (it still draws), 2 runs it twice as fast.
    void f_SetTimeScale(double l_dS) { m_dTimeScale = l_dS < 0.0 ? 0.0 : l_dS; }
    // Inspect (the UI editor): a left click is not passed to the game; inspectClicked() says where it was.
    void f_SetInspectMode(bool l_bOn) { m_bInspectMode = l_bOn; }
    bool f_InspectMode() const { return m_bInspectMode; }
    int f_FrameCount() const { return m_iFrameCount; }   // frames drawn since the window opened (smoke tests)
    QString f_RendererName() const;
    QString f_AssetDir() const;
    toms::next::GameSession& f_Session() { return m_oSession; }

signals:
    void f_StatusMessage(const QString& l_oText);
    void f_FrameInfo(double l_dFps, int l_iQuads, int l_iDrawCalls);
    void f_StartFailed(const QString& l_oTitle, const QString& l_oText);
    void f_SmokeTestDone();
    void f_InspectClicked();   // inspect mode: a left click; the UI's hover element is the one under it

protected:
    void showEvent(QShowEvent* l_pE) override;
    void resizeEvent(QResizeEvent* l_pE) override;
    void paintEvent(QPaintEvent*) override {}
    void keyPressEvent(QKeyEvent* l_pE) override;
    void keyReleaseEvent(QKeyEvent* l_pE) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void wheelEvent(QWheelEvent* l_pE) override;
    void focusOutEvent(QFocusEvent* l_pE) override;
    void leaveEvent(QEvent* l_pE) override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab belongs to the game (stage select)

private:
    bool f_EnsureBgfx();
    void f_Tick();
    void f_SetKey(int l_iQtKey, bool l_bDown);
    void f_UpdateMouse(const QPointF& l_oLogicalPos, Qt::MouseButtons l_oButtons);
    uint32_t f_PixelWidth() const;
    uint32_t f_PixelHeight() const;

    QTimer m_oTimer;
    QElapsedTimer m_oClock;
    toms::next::GameSession m_oSession;
    toms::next::InputState m_oInput;
    QString m_oPendingStage = QStringLiteral("stage01");
    QString m_oPendingScenario;   // startScenario before bgfx was up: started by showEvent
    bool m_bBgfxReady = false;
    bool m_bInspectMode = false;
    double m_dTimeScale = 1.0;
    double m_dTimeCarry = 0.0;   // the part of a millisecond a scaled frame left over
    bool m_bFailed = false;
    int m_iFramesSinceReport = 0;
    qint64 m_i64MsSinceReport = 0;
    int m_iFrameCount = 0;
    int m_iSmokeFrames = 0;
    QString m_oSmokePng;
};

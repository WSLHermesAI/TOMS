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
    explicit BgfxViewport(QWidget* parent = nullptr);
    ~BgfxViewport() override;

    QPaintEngine* paintEngine() const override { return nullptr; }

    bool restartGame(const QString& stage = QStringLiteral("stage01"));
    void loadStage(const QString& id);
    void setShowStats(bool on);
    // Smoke test: after `frames` frames, save the viewport (bgfx screenshot) to `png` and emit smokeTestDone.
    void runSmokeTest(int frames, const QString& png);
    void setDebugOverlay(bool on);
    QString rendererName() const;
    QString assetDir() const;
    toms::next::GameSession& session() { return session_; }

signals:
    void statusMessage(const QString& text);
    void frameInfo(double fps, int quads, int drawCalls);
    void startFailed(const QString& title, const QString& text);
    void smokeTestDone();

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void paintEvent(QPaintEvent*) override {}
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    void leaveEvent(QEvent* e) override;
    bool focusNextPrevChild(bool) override { return false; }   // Tab belongs to the game (stage select)

private:
    bool ensureBgfx();
    void tick();
    void setKey(int qtKey, bool down);
    void updateMouse(const QPointF& logicalPos, Qt::MouseButtons buttons);
    uint32_t pixelWidth() const;
    uint32_t pixelHeight() const;

    QTimer timer_;
    QElapsedTimer clock_;
    toms::next::GameSession session_;
    toms::next::InputState input_;
    QString pendingStage_ = QStringLiteral("stage01");
    bool bgfxReady_ = false;
    bool failed_ = false;
    int framesSinceReport_ = 0;
    qint64 msSinceReport_ = 0;
    int frameCount_ = 0;
    int smokeFrames_ = 0;
    QString smokePng_;
};

// bgfx_viewport.cpp -- see bgfx_viewport.h.
#include "bgfx_viewport.h"

#include "bgfx_host.h"
#include "bgfx_renderer.h"
#include "game.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <cmath>
#include <cstdlib>

using namespace toms::next;

BgfxViewport::BgfxViewport(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    connect(&timer_, &QTimer::timeout, this, &BgfxViewport::tick);
}

BgfxViewport::~BgfxViewport() {
    timer_.stop();
    session_.stop();          // GPU handles first...
    bgfxHostShutdown();       // ...then bgfx, while this widget's HWND still exists
}

uint32_t BgfxViewport::pixelWidth() const  { return (uint32_t)std::lround(width()  * devicePixelRatioF()); }
uint32_t BgfxViewport::pixelHeight() const { return (uint32_t)std::lround(height() * devicePixelRatioF()); }

bool BgfxViewport::ensureBgfx() {
    if (bgfxReady_) return true;
    if (failed_) return false;
    BgfxHostConfig cfg;
    cfg.nativeWindow = reinterpret_cast<void*>(winId());
    cfg.width = pixelWidth();
    cfg.height = pixelHeight();
    if (const char* r = std::getenv("TOMS_RENDERER")) cfg.renderer = r;
    std::string err;
    if (!bgfxHostInit(cfg, err)) {
        failed_ = true;
        Q_EMIT startFailed(tr("Graphics could not start"), QString::fromStdString(err));
        return false;
    }
    bgfxReady_ = true;
    return true;
}

bool BgfxViewport::restartGame(const QString& stage) {
    pendingStage_ = stage;
    if (!ensureBgfx()) return false;
    SessionOptions opts;
    opts.assetDir = GameSession::defaultAssetDir();
    opts.startStage = stage.toStdString();
    const std::string fontNote = GameSession::applyFontFallback(opts.assetDir);
    if (!fontNote.empty()) Q_EMIT statusMessage(QString::fromStdString(fontNote).section('\n', 0, 0));
    std::string err;
    if (!session_.start(opts, err)) {
        timer_.stop();
        Q_EMIT startFailed(tr("The game could not start"), QString::fromStdString(err));
        return false;
    }
    input_ = InputState{};
    clock_.start();
    timer_.start(0);   // as fast as vsync allows; bgfx::frame() blocks on present
    Q_EMIT statusMessage(tr("Playing %1 on %2").arg(stage, rendererName()));
    return true;
}

void BgfxViewport::loadStage(const QString& id) {
    if (!session_.running()) { restartGame(id); return; }
    session_.game()->loadStage(id.toStdString());
    Q_EMIT statusMessage(tr("Loaded %1").arg(id));
    setFocus();
}

void BgfxViewport::setShowStats(bool on) { bgfxHostSetDebugText(on); }

void BgfxViewport::runSmokeTest(int frames, const QString& png) {
    smokeFrames_ = frames;
    smokePng_ = png;
}
void BgfxViewport::setDebugOverlay(bool on) { session_.showDebugOverlay = on; }

QString BgfxViewport::rendererName() const {
    return bgfxReady_ ? QString::fromStdString(bgfxHostRendererName()) : tr("(not started)");
}

QString BgfxViewport::assetDir() const { return QString::fromStdString(GameSession::defaultAssetDir()); }

void BgfxViewport::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!bgfxReady_ && !failed_) restartGame(pendingStage_);
}

void BgfxViewport::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (bgfxReady_) bgfxHostReset(pixelWidth(), pixelHeight());
}

void BgfxViewport::tick() {
    if (!bgfxReady_ || !session_.running() || !isVisible()) return;
    const qint64 dt = clock_.restart();
    const bool keep = session_.frame((int)dt, input_, pixelWidth(), pixelHeight());
    input_.wheel = 0;
    ++frameCount_;
    if (smokeFrames_ > 0 && frameCount_ == smokeFrames_ && session_.renderer())
        session_.renderer()->savePNG(smokePng_.toStdString());
    bgfxHostFrame();
    if (smokeFrames_ > 0 && frameCount_ == smokeFrames_ + 3) Q_EMIT smokeTestDone();
    if (!keep) {   // Escape on the title menu quits the game; in the editor just start again
        Q_EMIT statusMessage(tr("The game asked to quit -- restarted"));
        restartGame(pendingStage_);
        return;
    }
    ++framesSinceReport_;
    msSinceReport_ += dt;
    if (msSinceReport_ >= 500) {
        const double fps = framesSinceReport_ * 1000.0 / (double)msSinceReport_;
        BgfxRenderer* r = session_.renderer();
        Q_EMIT frameInfo(fps, r ? (int)r->lastQuadCount() : 0, r ? (int)r->lastDrawCalls() : 0);
        framesSinceReport_ = 0;
        msSinceReport_ = 0;
    }
}

void BgfxViewport::setKey(int qtKey, bool down) {
    auto set = [&](Key k) { input_.down[(size_t)k] = down; };
    switch (qtKey) {
    case Qt::Key_Up: set(Key::Up); break;
    case Qt::Key_Down: set(Key::Down); break;
    case Qt::Key_Left: set(Key::Left); break;
    case Qt::Key_Right: set(Key::Right); break;
    case Qt::Key_W: set(Key::W); break;
    case Qt::Key_A: set(Key::A); break;
    case Qt::Key_S: set(Key::S); break;
    case Qt::Key_D: set(Key::D); break;
    case Qt::Key_Return: case Qt::Key_Enter: set(Key::Enter); break;
    case Qt::Key_Space: set(Key::Space); break;
    case Qt::Key_Escape: set(Key::Escape); break;
    case Qt::Key_Tab: set(Key::Tab); break;
    case Qt::Key_F1: set(Key::F1); break;
    case Qt::Key_F2: set(Key::F2); break;
    case Qt::Key_F3: set(Key::F3); break;
    case Qt::Key_F: set(Key::F); break;
    case Qt::Key_G: set(Key::G); break;
    case Qt::Key_H: set(Key::H); break;
    case Qt::Key_I: set(Key::I); break;
    case Qt::Key_B: set(Key::B); break;
    default:
        if (qtKey >= Qt::Key_1 && qtKey <= Qt::Key_9) input_.down[(size_t)Key::Num1 + (qtKey - Qt::Key_1)] = down;
        break;
    }
}

void BgfxViewport::keyPressEvent(QKeyEvent* e) { setKey(e->key(), true); e->accept(); }

void BgfxViewport::keyReleaseEvent(QKeyEvent* e) {
    if (e->isAutoRepeat()) { e->accept(); return; }   // held keys stay down between repeats
    setKey(e->key(), false);
    e->accept();
}

void BgfxViewport::updateMouse(const QPointF& p, Qt::MouseButtons buttons) {
    const double dpr = devicePixelRatioF();
    input_.mouseX = (float)(p.x() * dpr);
    input_.mouseY = (float)(p.y() * dpr);
    input_.mouseLeft = buttons & Qt::LeftButton;
    input_.mouseRight = buttons & Qt::RightButton;
    input_.mouseMiddle = buttons & Qt::MiddleButton;
    input_.hasMouse = true;
}

void BgfxViewport::mousePressEvent(QMouseEvent* e)   { setFocus(); updateMouse(e->position(), e->buttons()); }
void BgfxViewport::mouseReleaseEvent(QMouseEvent* e) { updateMouse(e->position(), e->buttons()); }
void BgfxViewport::mouseMoveEvent(QMouseEvent* e)    { updateMouse(e->position(), e->buttons()); }
void BgfxViewport::wheelEvent(QWheelEvent* e)        { input_.wheel += e->angleDelta().y() / 120.0f; }

void BgfxViewport::focusOutEvent(QFocusEvent* e) {
    QWidget::focusOutEvent(e);
    input_.down.fill(false);   // no stuck keys after Alt+Tab
}

void BgfxViewport::leaveEvent(QEvent* e) {
    QWidget::leaveEvent(e);
    input_.hasMouse = false;
}

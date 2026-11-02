// bgfx_viewport.cpp -- see bgfx_viewport.h.
#include "bgfx_viewport.h"

#include <QFileInfo>

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

BgfxViewport::BgfxViewport(QWidget* l_pParent) : QWidget(l_pParent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(320, 240);
    connect(&m_oTimer, &QTimer::timeout, this, &BgfxViewport::f_Tick);
}

BgfxViewport::~BgfxViewport() {
    m_oTimer.stop();
    m_oSession.f_Stop();          // GPU handles first...
    f_BgfxHostShutdown();       // ...then bgfx, while this widget's HWND still exists
}

uint32_t BgfxViewport::f_PixelWidth() const  { return (uint32_t)std::lround(width()  * devicePixelRatioF()); }
uint32_t BgfxViewport::f_PixelHeight() const { return (uint32_t)std::lround(height() * devicePixelRatioF()); }

bool BgfxViewport::f_EnsureBgfx() {
    if (m_bBgfxReady) return true;
    if (m_bFailed) return false;
    BgfxHostConfig l_oCfg;
    l_oCfg.m_pNativeWindow = reinterpret_cast<void*>(winId());
    l_oCfg.m_uWidth = f_PixelWidth();
    l_oCfg.m_uHeight = f_PixelHeight();
    if (const char* l_pszR = std::getenv("TOMS_RENDERER")) l_oCfg.m_strRenderer = l_pszR;
    std::string l_strErr;
    if (!f_BgfxHostInit(l_oCfg, l_strErr)) {
        m_bFailed = true;
        Q_EMIT f_StartFailed(tr("Graphics could not start"), QString::fromStdString(l_strErr));
        return false;
    }
    m_bBgfxReady = true;
    return true;
}

bool BgfxViewport::f_RestartGame(const QString& l_oStage) {
    m_oPendingStage = l_oStage;
    if (!f_EnsureBgfx()) return false;
    SessionOptions l_oOpts;
    l_oOpts.m_strAssetDir = GameSession::f_DefaultAssetDir();
    l_oOpts.m_strStartStage = l_oStage.toStdString();
    l_oOpts.m_bHotReload = true;   // art, clips and effects saved in the tools show up in the running game
    std::string l_strErr;
    if (!m_oSession.f_Start(l_oOpts, l_strErr)) {
        m_oTimer.stop();
        Q_EMIT f_StartFailed(tr("The game could not start"), QString::fromStdString(l_strErr));
        return false;
    }
    m_oInput = InputState{};
    m_oClock.start();
    m_oTimer.start(0);   // as fast as vsync allows; bgfx::frame() blocks on present
    Q_EMIT f_StatusMessage(tr("Playing %1 on %2").arg(l_oStage, f_RendererName()));
    return true;
}

void BgfxViewport::f_LoadStage(const QString& l_oId) {
    if (!m_oSession.f_Running()) { f_RestartGame(l_oId); return; }
    m_oSession.f_Game()->f_LoadStage(l_oId.toStdString());
    Q_EMIT f_StatusMessage(tr("Loaded %1").arg(l_oId));
    setFocus();
}

bool BgfxViewport::f_StartScenario(const QString& l_oPath, QString& l_oError) {
    if (!m_bBgfxReady) {   // the Play tab is not shown yet: start it with the game
        m_oPendingScenario = l_oPath;
        return true;
    }
    if (!f_RestartGame()) { l_oError = tr("The game could not start."); return false; }
    std::string l_strErr;
    if (!m_oSession.f_Game()->f_StartScenario(l_oPath.toStdString(), l_strErr)) {
        l_oError = QString::fromStdString(l_strErr);
        return false;
    }
    Q_EMIT f_StatusMessage(tr("Scenario %1").arg(QFileInfo(l_oPath).fileName()));
    setFocus();
    return true;
}

void BgfxViewport::f_SetShowStats(bool l_bOn) { f_BgfxHostSetDebugText(l_bOn); }

void BgfxViewport::f_RunSmokeTest(int l_iFrames, const QString& l_oPng) {
    m_iSmokeFrames = l_iFrames;
    m_oSmokePng = l_oPng;
}
void BgfxViewport::f_SetDebugOverlay(bool l_bOn) { m_oSession.m_bShowDebugOverlay = l_bOn; }

QString BgfxViewport::f_RendererName() const {
    return m_bBgfxReady ? QString::fromStdString(f_BgfxHostRendererName()) : tr("(not started)");
}

QString BgfxViewport::f_AssetDir() const { return QString::fromStdString(GameSession::f_DefaultAssetDir()); }

void BgfxViewport::showEvent(QShowEvent* l_pE) {
    QWidget::showEvent(l_pE);
    if (!m_bBgfxReady && !m_bFailed && f_RestartGame(m_oPendingStage) && !m_oPendingScenario.isEmpty()) {
        const QString l_oPath = m_oPendingScenario;
        m_oPendingScenario.clear();
        QString l_oErr;
        if (!f_StartScenario(l_oPath, l_oErr)) Q_EMIT f_StartFailed(tr("The scenario could not start"), l_oErr);
    }
}

void BgfxViewport::resizeEvent(QResizeEvent* l_pE) {
    QWidget::resizeEvent(l_pE);
    if (m_bBgfxReady) f_BgfxHostReset(f_PixelWidth(), f_PixelHeight());
}

void BgfxViewport::f_Tick() {
    if (!m_bBgfxReady || !m_oSession.f_Running() || !isVisible()) return;
    const qint64 l_i64Dt = m_oClock.restart();
    const double l_dScaled = l_i64Dt * m_dTimeScale + m_dTimeCarry;   // setTimeScale (1: as is)
    const int l_iGameDt = (int)l_dScaled;
    m_dTimeCarry = l_dScaled - l_iGameDt;
    const bool l_bKeep = m_oSession.f_Frame(l_iGameDt, m_oInput, f_PixelWidth(), f_PixelHeight());
    m_oInput.m_fWheel = 0;
    ++m_iFrameCount;
    if (m_iSmokeFrames > 0 && m_iFrameCount == m_iSmokeFrames && m_oSession.f_Renderer())
        m_oSession.f_Renderer()->f_SavePNG(m_oSmokePng.toStdString());
    f_BgfxHostFrame();
    if (m_iSmokeFrames > 0 && m_iFrameCount == m_iSmokeFrames + 3) Q_EMIT f_SmokeTestDone();
    if (!l_bKeep) {   // Escape on the title menu quits the game; in the editor just start again
        Q_EMIT f_StatusMessage(tr("The game asked to quit -- restarted"));
        f_RestartGame(m_oPendingStage);
        return;
    }
    ++m_iFramesSinceReport;
    m_i64MsSinceReport += l_i64Dt;
    if (m_i64MsSinceReport >= 500) {
        const double l_dFps = m_iFramesSinceReport * 1000.0 / (double)m_i64MsSinceReport;
        BgfxRenderer* l_pR = m_oSession.f_Renderer();
        Q_EMIT f_FrameInfo(l_dFps, l_pR ? (int)l_pR->f_LastQuadCount() : 0, l_pR ? (int)l_pR->f_LastDrawCalls() : 0);
        m_iFramesSinceReport = 0;
        m_i64MsSinceReport = 0;
    }
}

void BgfxViewport::f_SetKey(int l_iQtKey, bool l_bDown) {
    auto l_fnSet = [&](Key l_eK) { m_oInput.m_arrDown[(size_t)l_eK] = l_bDown; };
    switch (l_iQtKey) {
    case Qt::Key_Up: l_fnSet(Key::Up); break;
    case Qt::Key_Down: l_fnSet(Key::Down); break;
    case Qt::Key_Left: l_fnSet(Key::Left); break;
    case Qt::Key_Right: l_fnSet(Key::Right); break;
    case Qt::Key_W: l_fnSet(Key::W); break;
    case Qt::Key_A: l_fnSet(Key::A); break;
    case Qt::Key_S: l_fnSet(Key::S); break;
    case Qt::Key_D: l_fnSet(Key::D); break;
    case Qt::Key_Return: case Qt::Key_Enter: l_fnSet(Key::Enter); break;
    case Qt::Key_Space: l_fnSet(Key::Space); break;
    case Qt::Key_Escape: l_fnSet(Key::Escape); break;
    case Qt::Key_Tab: l_fnSet(Key::Tab); break;
    case Qt::Key_F1: l_fnSet(Key::F1); break;
    case Qt::Key_F2: l_fnSet(Key::F2); break;
    case Qt::Key_F3: l_fnSet(Key::F3); break;
    case Qt::Key_F5: l_fnSet(Key::F5); break;
    case Qt::Key_F8: l_fnSet(Key::F8); break;
    case Qt::Key_F: l_fnSet(Key::F); break;
    case Qt::Key_G: l_fnSet(Key::G); break;
    case Qt::Key_H: l_fnSet(Key::H); break;
    case Qt::Key_I: l_fnSet(Key::I); break;
    case Qt::Key_B: l_fnSet(Key::B); break;
    default:
        if (l_iQtKey >= Qt::Key_1 && l_iQtKey <= Qt::Key_9) m_oInput.m_arrDown[(size_t)Key::Num1 + (l_iQtKey - Qt::Key_1)] = l_bDown;
        break;
    }
}

void BgfxViewport::keyPressEvent(QKeyEvent* l_pE) { f_SetKey(l_pE->key(), true); l_pE->accept(); }

void BgfxViewport::keyReleaseEvent(QKeyEvent* l_pE) {
    if (l_pE->isAutoRepeat()) { l_pE->accept(); return; }   // held keys stay down between repeats
    f_SetKey(l_pE->key(), false);
    l_pE->accept();
}

void BgfxViewport::f_UpdateMouse(const QPointF& l_oP, Qt::MouseButtons l_oButtons) {
    const double l_dDpr = devicePixelRatioF();
    m_oInput.m_fMouseX = (float)(l_oP.x() * l_dDpr);
    m_oInput.m_fMouseY = (float)(l_oP.y() * l_dDpr);
    m_oInput.m_bMouseLeft = l_oButtons & Qt::LeftButton;
    m_oInput.m_bMouseRight = l_oButtons & Qt::RightButton;
    m_oInput.m_bMouseMiddle = l_oButtons & Qt::MiddleButton;
    m_oInput.m_bHasMouse = true;
}

void BgfxViewport::mousePressEvent(QMouseEvent* l_pE) {
    setFocus();
    if (m_bInspectMode && l_pE->button() == Qt::LeftButton) {   // the UI editor picks the element; the game sees no click
        f_UpdateMouse(l_pE->position(), l_pE->buttons() & ~Qt::LeftButton);
        Q_EMIT f_InspectClicked();
        return;
    }
    f_UpdateMouse(l_pE->position(), l_pE->buttons());
}
void BgfxViewport::mouseReleaseEvent(QMouseEvent* l_pE) {
    f_UpdateMouse(l_pE->position(), m_bInspectMode ? (l_pE->buttons() & ~Qt::LeftButton) : l_pE->buttons());
}
void BgfxViewport::mouseMoveEvent(QMouseEvent* l_pE) {
    f_UpdateMouse(l_pE->position(), m_bInspectMode ? (l_pE->buttons() & ~Qt::LeftButton) : l_pE->buttons());
}
void BgfxViewport::wheelEvent(QWheelEvent* l_pE)        { m_oInput.m_fWheel += l_pE->angleDelta().y() / 120.0f; }

void BgfxViewport::focusOutEvent(QFocusEvent* l_pE) {
    QWidget::focusOutEvent(l_pE);
    m_oInput.m_arrDown.fill(false);   // no stuck keys after Alt+Tab
}

void BgfxViewport::leaveEvent(QEvent* l_pE) {
    QWidget::leaveEvent(l_pE);
    m_oInput.m_bHasMouse = false;
}

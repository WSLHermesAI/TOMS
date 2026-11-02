#pragma once

#include "particle_fx.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QSet>

class ParticleDocument;
class QTimer;

// The preview: the current effect played with the game's EffectInstance, driven by a timer.
// After every edit it plays again with the same seed up to the same time, so changing a value
// shows its result at the same moment instead of starting over. Hidden emitters (the eye in the
// Effects dock) emit nothing in the preview; the file is not changed.
class ParticlePlayback : public QObject
{
    Q_OBJECT

public:
    explicit ParticlePlayback(ParticleDocument* l_pDoc, QObject* l_pParent = nullptr);

    const toms::fx::EffectInstance& f_Instance() const { return m_oFx; }
    bool f_Playing() const { return m_bPlaying; }
    float f_Time() const { return m_oFx.f_Time(); }
    float f_Speed() const { return m_fSpeed; }
    uint32_t f_Seed() const { return m_uSeed; }
    QPointF f_Origin() const { return m_oOrigin; }
    bool f_AutoRestart() const { return m_bAutoRestart; }
    double f_LastStepMs() const { return m_dLastStepMs; }

    void f_Play();
    void f_Pause();
    void f_TogglePlay();
    void f_Restart();               // time 0, playing
    void f_StepFrame();             // one 1/60 s step, paused
    void f_Seek(float l_fT);           // paused at effect time t (a fresh play with the same seed)
    void f_SetSpeed(float l_fS) { m_fSpeed = l_fS; }
    void f_SetSeed(uint32_t l_uS);
    void f_RandomizeSeed();
    void f_SetAutoRestart(bool l_bOn) { m_bAutoRestart = l_bOn; }
    // Where the effect is in the viewport. restart: play again from there (a burst "fired" at
    // the click); otherwise it moves while playing (world-space trails stay behind).
    void f_SetOrigin(const QPointF& l_oP, bool l_bRestart);
    void f_Advance(float l_fSeconds);  // as timer ticks would (the selftest drives this directly)

    // The game renderer the preview draws with (ParticleViewport's game-renderer mode): emitters set
    // to "gpu", or "auto" ones above `threshold`, are then simulated on the GPU as in the game.
    // nullptr: everything on the CPU (the QPainter preview).
    void f_SetGpuRenderer(IRenderer* l_pRen, int l_iThreshold);
    IRenderer* f_GpuRenderer() const { return m_pGpuRen; }
    int f_GpuThreshold() const { return m_iGpuThreshold; }
    toms::fx::EffectInstance& f_InstanceForDraw() { return m_oFx; }   // EffectInstance::draw is not const

    bool f_IsHidden(int l_iEmitter) const { return m_oHidden.contains(l_iEmitter); }
    void f_SetHidden(int l_iEmitter, bool l_bHidden);

signals:
    void f_Ticked();                // a new frame to draw
    void f_StateChanged();          // playing / paused / seed / speed

private:
    void f_Rebuild(bool l_bKeepTime);
    void f_Tick();

    ParticleDocument* m_pDoc;
    toms::fx::Effect m_oPreview;       // the current effect, hidden emitters muted
    toms::fx::EffectInstance m_oFx;
    QTimer* m_pTimer;
    QElapsedTimer m_oClock;
    QSet<int> m_oHidden;
    QPointF m_oOrigin{0, 0};
    float m_fSpeed = 1;
    float m_fIdle = 0;                 // seconds since a one-shot finished
    uint32_t m_uSeed = 1;
    int m_iEffect = -1;
    bool m_bPlaying = true;
    bool m_bAutoRestart = true;
    double m_dLastStepMs = 0;
    IRenderer* m_pGpuRen = nullptr;
    int m_iGpuThreshold = 5000;   // the desktop default of GameSettings::particleGpuThreshold
};

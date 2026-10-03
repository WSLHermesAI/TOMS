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
    explicit ParticlePlayback(ParticleDocument* doc, QObject* parent = nullptr);

    const toms::fx::EffectInstance& instance() const { return m_fx; }
    bool playing() const { return m_playing; }
    float time() const { return m_fx.time(); }
    float speed() const { return m_speed; }
    uint32_t seed() const { return m_seed; }
    QPointF origin() const { return m_origin; }
    bool autoRestart() const { return m_autoRestart; }
    double lastStepMs() const { return m_lastStepMs; }

    void play();
    void pause();
    void togglePlay();
    void restart();               // time 0, playing
    void stepFrame();             // one 1/60 s step, paused
    void seek(float t);           // paused at effect time t (a fresh play with the same seed)
    void setSpeed(float s) { m_speed = s; }
    void setSeed(uint32_t s);
    void randomizeSeed();
    void setAutoRestart(bool on) { m_autoRestart = on; }
    // Where the effect is in the viewport. restart: play again from there (a burst "fired" at
    // the click); otherwise it moves while playing (world-space trails stay behind).
    void setOrigin(const QPointF& p, bool restart);
    void advance(float seconds);  // as timer ticks would (the selftest drives this directly)

    // The game renderer the preview draws with (ParticleViewport's game-renderer mode): emitters set
    // to "gpu", or "auto" ones above `threshold`, are then simulated on the GPU as in the game.
    // nullptr: everything on the CPU (the QPainter preview).
    void setGpuRenderer(IRenderer* ren, int threshold);
    IRenderer* gpuRenderer() const { return m_gpuRen; }
    int gpuThreshold() const { return m_gpuThreshold; }
    toms::fx::EffectInstance& instanceForDraw() { return m_fx; }   // EffectInstance::draw is not const

    bool isHidden(int emitter) const { return m_hidden.contains(emitter); }
    void setHidden(int emitter, bool hidden);

signals:
    void ticked();                // a new frame to draw
    void stateChanged();          // playing / paused / seed / speed

private:
    void rebuild(bool keepTime);
    void tick();

    ParticleDocument* m_doc;
    toms::fx::Effect m_preview;       // the current effect, hidden emitters muted
    toms::fx::EffectInstance m_fx;
    QTimer* m_timer;
    QElapsedTimer m_clock;
    QSet<int> m_hidden;
    QPointF m_origin{0, 0};
    float m_speed = 1;
    float m_idle = 0;                 // seconds since a one-shot finished
    uint32_t m_seed = 1;
    int m_effect = -1;
    bool m_playing = true;
    bool m_autoRestart = true;
    double m_lastStepMs = 0;
    IRenderer* m_gpuRen = nullptr;
    int m_gpuThreshold = 5000;   // the desktop default of GameSettings::particleGpuThreshold
};

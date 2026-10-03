#include "ParticlePlayback.h"

#include "ParticleDocument.h"

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <chrono>

namespace {
// After an edit an endless effect is replayed at most this far: it looks the same by then, and
// replaying minutes of simulation on every key press would make editing slow.
constexpr float kMaxReplay = 6.0f;
}

ParticlePlayback::ParticlePlayback(ParticleDocument* doc, QObject* parent)
    : QObject(parent)
    , m_doc(doc)
    , m_timer(new QTimer(this))
{
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(16);
    connect(m_timer, &QTimer::timeout, this, &ParticlePlayback::tick);
    connect(doc, &ParticleDocument::fileChanged, this, [this] { rebuild(true); });
    connect(doc, &ParticleDocument::atlasChanged, this, [this] { emit ticked(); });
    connect(doc, &ParticleDocument::selectionChanged, this, [this] {
        if (m_doc->effectIndex() != m_effect) {
            m_hidden.clear();
            rebuild(false);
        }
    });
    rebuild(false);
    m_clock.start();
    m_timer->start();
}

void ParticlePlayback::rebuild(bool keepTime)
{
    const float t = keepTime && m_effect == m_doc->effectIndex() ? m_fx.time() : 0.0f;
    m_effect = m_doc->effectIndex();
    const toms::fx::Effect* e = m_doc->effect();
    if (!e) {
        m_fx.play(nullptr);
        emit ticked();
        return;
    }
    m_preview = *e;
    for (int i = 0; i < int(m_preview.emitters.size()); i++)
        if (m_hidden.contains(i)) {
            m_preview.emitters[size_t(i)].rate = 0;
            m_preview.emitters[size_t(i)].bursts.clear();
        }
    m_fx.setTransform(toms::anim::placement(float(m_origin.x()), float(m_origin.y())));
    m_fx.setGpuSimulation(m_gpuRen, m_gpuThreshold);
    m_fx.play(&m_preview, m_seed);
    if (t > 0) m_fx.seek(m_preview.duration > 0 ? t : std::min(t, kMaxReplay));
    m_idle = 0;
    emit ticked();
}

void ParticlePlayback::play()
{
    if (m_playing) return;
    if (m_fx.finished()) rebuild(false);
    m_playing = true;
    m_clock.restart();
    emit stateChanged();
}

void ParticlePlayback::pause()
{
    if (!m_playing) return;
    m_playing = false;
    emit stateChanged();
}

void ParticlePlayback::togglePlay() { m_playing ? pause() : play(); }

void ParticlePlayback::restart()
{
    rebuild(false);
    m_playing = true;
    m_clock.restart();
    emit stateChanged();
}

void ParticlePlayback::stepFrame()
{
    pause();
    advance(toms::fx::EffectInstance::kStep);
}

void ParticlePlayback::seek(float t)
{
    pause();
    rebuild(false);
    if (t > 0) m_fx.seek(t);
    emit ticked();
}

void ParticlePlayback::setSeed(uint32_t s)
{
    m_seed = s ? s : 1;
    rebuild(true);
    emit stateChanged();
}

void ParticlePlayback::randomizeSeed() { setSeed(QRandomGenerator::global()->bounded(1u, 1000000u)); }

void ParticlePlayback::setOrigin(const QPointF& p, bool restart)
{
    m_origin = p;
    m_fx.setTransform(toms::anim::placement(float(p.x()), float(p.y())));
    if (restart) this->restart();
    else emit ticked();
}

void ParticlePlayback::setGpuRenderer(IRenderer* ren, int threshold)
{
    if (ren == m_gpuRen && threshold == m_gpuThreshold) return;
    m_gpuRen = ren;
    m_gpuThreshold = threshold;
    rebuild(true);   // the same moment, now with its GPU emitters (or without them)
}

void ParticlePlayback::setHidden(int emitter, bool hidden)
{
    if (hidden) m_hidden.insert(emitter);
    else m_hidden.remove(emitter);
    rebuild(true);
}

void ParticlePlayback::advance(float seconds)
{
    if (!m_fx.effect()) return;
    const auto t0 = std::chrono::steady_clock::now();
    m_fx.update(seconds);
    m_lastStepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (m_autoRestart && m_fx.finished() && (m_idle += seconds) >= 0.5f) rebuild(false);   // a one-shot plays again
    emit ticked();
}

void ParticlePlayback::tick()
{
    const double dt = m_clock.restart() / 1000.0;
    if (!m_playing) return;
    advance(float(std::min(dt, 0.1) * m_speed));
}

#include "ParticlePlayback.h"

#include "ParticleDocument.h"

#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <chrono>

namespace {
// After an edit an endless effect is replayed at most this far: it looks the same by then, and
// replaying minutes of simulation on every key press would make editing slow.
constexpr float g_fMaxReplay = 6.0f;
}

ParticlePlayback::ParticlePlayback(ParticleDocument* l_pDoc, QObject* l_pParent)
    : QObject(l_pParent)
    , m_pDoc(l_pDoc)
    , m_pTimer(new QTimer(this))
{
    m_pTimer->setTimerType(Qt::PreciseTimer);
    m_pTimer->setInterval(16);
    connect(m_pTimer, &QTimer::timeout, this, &ParticlePlayback::f_Tick);
    connect(l_pDoc, &ParticleDocument::f_FileChanged, this, [this] { f_Rebuild(true); });
    connect(l_pDoc, &ParticleDocument::f_AtlasChanged, this, [this] { emit f_Ticked(); });
    connect(l_pDoc, &ParticleDocument::f_SelectionChanged, this, [this] {
        if (m_pDoc->f_EffectIndex() != m_iEffect) {
            m_oHidden.clear();
            f_Rebuild(false);
        }
    });
    f_Rebuild(false);
    m_oClock.start();
    m_pTimer->start();
}

void ParticlePlayback::f_Rebuild(bool l_bKeepTime)
{
    const float l_fT = l_bKeepTime && m_iEffect == m_pDoc->f_EffectIndex() ? m_oFx.f_Time() : 0.0f;
    m_iEffect = m_pDoc->f_EffectIndex();
    const toms::fx::Effect* l_pE = m_pDoc->f_Effect();
    if (!l_pE) {
        m_oFx.f_Play(nullptr);
        emit f_Ticked();
        return;
    }
    m_oPreview = *l_pE;
    for (int l_iI = 0; l_iI < int(m_oPreview.m_vEmitters.size()); l_iI++)
        if (m_oHidden.contains(l_iI)) {
            m_oPreview.m_vEmitters[size_t(l_iI)].m_fRate = 0;
            m_oPreview.m_vEmitters[size_t(l_iI)].m_vBursts.clear();
        }
    m_oFx.f_SetTransform(toms::anim::f_Placement(float(m_oOrigin.x()), float(m_oOrigin.y())));
    m_oFx.f_SetGpuSimulation(m_pGpuRen, m_iGpuThreshold);
    m_oFx.f_Play(&m_oPreview, m_uSeed);
    if (l_fT > 0) m_oFx.f_Seek(m_oPreview.m_fDuration > 0 ? l_fT : std::min(l_fT, g_fMaxReplay));
    m_fIdle = 0;
    emit f_Ticked();
}

void ParticlePlayback::f_Play()
{
    if (m_bPlaying) return;
    if (m_oFx.f_Finished()) f_Rebuild(false);
    m_bPlaying = true;
    m_oClock.restart();
    emit f_StateChanged();
}

void ParticlePlayback::f_Pause()
{
    if (!m_bPlaying) return;
    m_bPlaying = false;
    emit f_StateChanged();
}

void ParticlePlayback::f_TogglePlay() { m_bPlaying ? f_Pause() : f_Play(); }

void ParticlePlayback::f_Restart()
{
    f_Rebuild(false);
    m_bPlaying = true;
    m_oClock.restart();
    emit f_StateChanged();
}

void ParticlePlayback::f_StepFrame()
{
    f_Pause();
    f_Advance(toms::fx::EffectInstance::m_fStep);
}

void ParticlePlayback::f_Seek(float l_fT)
{
    f_Pause();
    f_Rebuild(false);
    if (l_fT > 0) m_oFx.f_Seek(l_fT);
    emit f_Ticked();
}

void ParticlePlayback::f_SetSeed(uint32_t l_uS)
{
    m_uSeed = l_uS ? l_uS : 1;
    f_Rebuild(true);
    emit f_StateChanged();
}

void ParticlePlayback::f_RandomizeSeed() { f_SetSeed(QRandomGenerator::global()->bounded(1u, 1000000u)); }

void ParticlePlayback::f_SetOrigin(const QPointF& l_oP, bool l_bRestart)
{
    m_oOrigin = l_oP;
    m_oFx.f_SetTransform(toms::anim::f_Placement(float(l_oP.x()), float(l_oP.y())));
    if (l_bRestart) this->f_Restart();
    else emit f_Ticked();
}

void ParticlePlayback::f_SetGpuRenderer(IRenderer* l_pRen, int l_iThreshold)
{
    if (l_pRen == m_pGpuRen && l_iThreshold == m_iGpuThreshold) return;
    m_pGpuRen = l_pRen;
    m_iGpuThreshold = l_iThreshold;
    f_Rebuild(true);   // the same moment, now with its GPU emitters (or without them)
}

void ParticlePlayback::f_SetHidden(int l_iEmitter, bool l_bHidden)
{
    if (l_bHidden) m_oHidden.insert(l_iEmitter);
    else m_oHidden.remove(l_iEmitter);
    f_Rebuild(true);
}

void ParticlePlayback::f_Advance(float l_fSeconds)
{
    if (!m_oFx.f_Effect()) return;
    const auto l_oT0 = std::chrono::steady_clock::now();
    m_oFx.f_Update(l_fSeconds);
    m_dLastStepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - l_oT0).count();
    if (m_bAutoRestart && m_oFx.f_Finished() && (m_fIdle += l_fSeconds) >= 0.5f) f_Rebuild(false);   // a one-shot plays again
    emit f_Ticked();
}

void ParticlePlayback::f_Tick()
{
    const double l_dDt = m_oClock.restart() / 1000.0;
    if (!m_bPlaying) return;
    f_Advance(float(std::min(l_dDt, 0.1) * m_fSpeed));
}

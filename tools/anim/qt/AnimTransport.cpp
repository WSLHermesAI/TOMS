#include "AnimTransport.h"

#include "Icons.h"
#include "Theme.h"

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolBar>

#include <algorithm>
#include <cmath>

using namespace animed;

// ---- AnimScrubber -------------------------------------------------------------------------------

AnimScrubber::AnimScrubber(AnimDocument* l_pDoc, QWidget* l_pParent)
    : QWidget(l_pParent)
    , m_pDoc(l_pDoc)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(tr("Playhead: click or drag (snaps to the selected node's keys)"));
    connect(l_pDoc, &AnimDocument::f_TimeChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_FileChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_SelectionChanged, this, qOverload<>(&QWidget::update));
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, qOverload<>(&QWidget::update));
}

float AnimScrubber::f_Span() const { return std::max({m_pDoc->f_Duration(), m_pDoc->f_Time(), 0.001f}); }

QRectF AnimScrubber::f_Track() const { return QRectF(8, 4, width() - 16, height() - 8); }

void AnimScrubber::paintEvent(QPaintEvent*)
{
    const Theme::Colors& l_oTc = Theme::f_Colors();
    QPainter l_oP(this);
    l_oP.setRenderHint(QPainter::Antialiasing);
    const QRectF l_oTr = f_Track();
    l_oP.setPen(Qt::NoPen);
    l_oP.setBrush(palette().color(QPalette::Base));
    l_oP.drawRoundedRect(l_oTr, 3, 3);
    const float l_fS = f_Span(), l_fD = m_pDoc->f_Duration();
    auto l_fnX = [&](double l_dT) { return l_oTr.left() + l_dT / l_fS * l_oTr.width(); };
    if (l_fD > 0 && l_fD < l_fS) {   // past the clip's end
        l_oP.setBrush(QColor(0x80, 0x80, 0x80, 50));
        l_oP.drawRect(QRectF(QPointF(l_fnX(l_fD), l_oTr.top()), l_oTr.bottomRight()));
    }
    // Second / tenth ticks.
    l_oP.setPen(QPen(palette().color(QPalette::PlaceholderText), 1));
    const double l_dStep = l_fS > 20 ? 1.0 : l_fS > 2 ? 0.5 : 0.1;
    for (double l_dT = 0; l_dT <= l_fS + 1e-6; l_dT += l_dStep) l_oP.drawLine(QPointF(l_fnX(l_dT), l_oTr.bottom() - 4), QPointF(l_fnX(l_dT), l_oTr.bottom()));
    // The selected node's keys.
    if (const toms::anim::Node* l_pN = m_pDoc->f_SelectedNode()) {
        l_oP.setPen(QPen(l_oTc.m_oKey.darker(160), 1));
        l_oP.setBrush(l_oTc.m_oKey);
        for (float l_fT : f_UnionKeyTimes(*l_pN)) {
            const QPointF l_oC(l_fnX(l_fT), l_oTr.center().y());
            l_oP.drawPolygon(QPolygonF({l_oC + QPointF(0, -5), l_oC + QPointF(5, 0), l_oC + QPointF(0, 5), l_oC + QPointF(-5, 0)}));
        }
    }
    // Playhead.
    const double l_dPx = l_fnX(m_pDoc->f_Time());
    l_oP.setPen(QPen(l_oTc.m_oSelection, 2));
    l_oP.drawLine(QPointF(l_dPx, 1), QPointF(l_dPx, height() - 1));
}

float AnimScrubber::f_TimeAt(double l_dPx) const
{
    const QRectF l_oTr = f_Track();
    float l_fT = float(std::clamp((l_dPx - l_oTr.left()) / l_oTr.width(), 0.0, 1.0) * f_Span());
    if (const toms::anim::Node* l_pN = m_pDoc->f_SelectedNode())
        for (float l_fK : f_UnionKeyTimes(*l_pN))
            if (std::fabs((l_fK - l_fT) / f_Span() * l_oTr.width()) < 5) l_fT = l_fK;
    return l_fT;
}

void AnimScrubber::f_Seek(double l_dPx)
{
    m_pDoc->f_SetTime(f_TimeAt(l_dPx));
    emit f_Scrubbed();
}

void AnimScrubber::mousePressEvent(QMouseEvent* l_pE)
{
    if (l_pE->button() == Qt::LeftButton) f_Seek(l_pE->position().x());
}

void AnimScrubber::mouseMoveEvent(QMouseEvent* l_pE)
{
    if (l_pE->buttons() & Qt::LeftButton) f_Seek(l_pE->position().x());
}

// ---- AnimTransport ------------------------------------------------------------------------------

AnimTransport::AnimTransport(AnimDocument* l_pDoc, QWidget* l_pParentWidget)
    : QObject(l_pParentWidget)
    , m_pDoc(l_pDoc)
    , m_pToolBar(new QToolBar(tr("Transport"), l_pParentWidget))
    , m_pTimer(new QTimer(this))
{
    m_pToolBar->setObjectName(QStringLiteral("AnimTransportBar"));
    m_pToolBar->setIconSize(QSize(18, 18));
    m_pToolBar->setMovable(false);
    m_pPlay = m_pToolBar->addAction(Icons::f_Icon(Icons::Id::Play), tr("Play / Pause"));
    m_pPlay->setShortcut(QKeySequence(Qt::Key_Space));
    m_pPlay->setToolTip(tr("Play / pause from the playhead (Space)"));
    m_pStop = m_pToolBar->addAction(Icons::f_Icon(Icons::Id::Stop), tr("Stop"));
    m_pStop->setToolTip(tr("Stop and go back to 0"));
    m_pLoop = m_pToolBar->addAction(Icons::f_Icon(Icons::Id::Loop), tr("Loop Preview"));
    m_pLoop->setCheckable(true);
    m_pLoop->setToolTip(tr("Preview as a loop, whatever the clip's play count"));
    m_pToolBar->addSeparator();
    m_pSpeed = new QComboBox(m_pToolBar);
    for (double l_dS : {0.1, 0.25, 0.5, 1.0, 2.0}) m_pSpeed->addItem(QStringLiteral("%1×").arg(l_dS), l_dS);
    m_pSpeed->setCurrentIndex(3);
    m_pSpeed->setToolTip(tr("Playback speed"));
    m_pToolBar->addWidget(m_pSpeed);
    m_pTimeSpin = new QDoubleSpinBox(m_pToolBar);
    m_pTimeSpin->setRange(0, 3600);
    m_pTimeSpin->setDecimals(3);
    m_pTimeSpin->setSingleStep(1.0 / 60);
    m_pTimeSpin->setSuffix(QStringLiteral(" s"));
    m_pTimeSpin->setKeyboardTracking(false);
    m_pTimeSpin->setToolTip(tr("Playhead time"));
    m_pTimeSpin->setMinimumWidth(90);
    m_pToolBar->addWidget(m_pTimeSpin);
    m_pScrubber = new AnimScrubber(l_pDoc, m_pToolBar);
    m_pToolBar->addWidget(m_pScrubber);
    m_pDuration = new QLabel(m_pToolBar);
    m_pDuration->setContentsMargins(6, 0, 6, 0);
    m_pDuration->setMinimumWidth(80);
    m_pToolBar->addWidget(m_pDuration);

    connect(m_pPlay, &QAction::triggered, this, &AnimTransport::f_TogglePlay);
    connect(m_pStop, &QAction::triggered, this, &AnimTransport::f_Stop);
    connect(m_pLoop, &QAction::toggled, this, [this] {
        if (m_bPlaying) f_Restart(m_pDoc->f_Time());
    });
    connect(m_pTimeSpin, &QDoubleSpinBox::valueChanged, this, [this](double l_dV) {
        f_Pause();
        m_pDoc->f_SetTime(float(l_dV));
    });
    connect(m_pScrubber, &AnimScrubber::f_Scrubbed, this, &AnimTransport::f_Pause);
    connect(m_pTimer, &QTimer::timeout, this, &AnimTransport::f_Tick);
    connect(l_pDoc, &AnimDocument::f_TimeChanged, this, [this] {
        if (m_bPlaying && !m_bSettingTime) f_Pause();   // someone moved the playhead
        f_UpdateWidgets();
    });
    connect(l_pDoc, &AnimDocument::f_FileChanged, this, [this] {
        if (m_bPlaying) f_Restart(m_pDoc->f_Time());   // play the edited clip from here on
        f_UpdateWidgets();
    });
    connect(l_pDoc, &AnimDocument::f_ClipChanged, this, [this] {
        f_Pause();
        f_UpdateWidgets();
    });
    connect(l_pDoc, &AnimDocument::f_FileReset, this, &AnimTransport::f_Pause);
    f_UpdateWidgets();
}

void AnimTransport::f_UpdateWidgets()
{
    const QSignalBlocker l_oB(m_pTimeSpin);
    m_pTimeSpin->setValue(m_pDoc->f_Time());
    const toms::anim::Clip* l_pC = m_pDoc->f_Clip();
    m_pDuration->setText(l_pC ? tr("/ %1 s%2").arg(l_pC->f_Duration(), 0, 'f', 3).arg(l_pC->m_iPlayCount < 0 ? tr("  loop") : QString()) : QString());
    m_pPlay->setIcon(Icons::f_Icon(m_bPlaying ? Icons::Id::Pause : Icons::Id::Play));
}

void AnimTransport::f_Restart(float l_fAtTime)
{
    const toms::anim::Clip* l_pC = m_pDoc->f_Clip();
    if (!l_pC) return;
    m_oClip = *l_pC;
    if (m_pLoop->isChecked()) m_oClip.m_iPlayCount = -1;
    m_oPlayer.f_Play(&m_oClip);
    // Skip to the playhead; the events in between are not "fired". From 0 nothing is skipped:
    // the time-0 events go out with the first update, like in the game.
    if (l_fAtTime > 0) {
        m_oPlayer.f_Update(int(std::lround(l_fAtTime * 1000.0)));
        m_oPlayer.f_TakeEvents();
    }
    m_dCarryMs = 0;
}

void AnimTransport::f_Play()
{
    const toms::anim::Clip* l_pC = m_pDoc->f_Clip();
    if (!l_pC || m_bPlaying) return;
    float l_fT = m_pDoc->f_Time();
    if (l_fT >= l_pC->f_Duration() - 1e-4f) l_fT = 0;
    f_Restart(l_fT);
    m_bPlaying = true;
    m_oClock.start();
    m_pTimer->start(15);
    f_UpdateWidgets();
    emit f_PlayingChanged(true);
}

void AnimTransport::f_Pause()
{
    if (!m_bPlaying) return;
    m_bPlaying = false;
    m_pDoc->f_SetPreviewTime(-1);   // back to the playhead (edits happen there)
    m_pTimer->stop();
    f_UpdateWidgets();
    emit f_PlayingChanged(false);
}

void AnimTransport::f_TogglePlay()
{
    if (m_bPlaying) f_Pause();
    else f_Play();
}

void AnimTransport::f_Stop()
{
    f_Pause();
    m_pDoc->f_SetTime(0);
}

void AnimTransport::f_Tick()
{
    const double l_dSpeed = m_pSpeed->currentData().toDouble();
    m_dCarryMs += double(m_oClock.restart()) * l_dSpeed;
    const int l_iWhole = int(m_dCarryMs);
    m_dCarryMs -= l_iWhole;
    if (l_iWhole > 0) f_Advance(l_iWhole);
}

void AnimTransport::f_Advance(int l_iMs)
{
    if (!m_bPlaying) {
        f_Play();
        if (!m_bPlaying) return;
    }
    m_oPlayer.f_Update(l_iMs);
    const std::vector<std::string> l_vEvents = m_oPlayer.f_TakeEvents();
    m_bSettingTime = true;
    m_pDoc->f_SetTime(m_oPlayer.f_Time());
    m_bSettingTime = false;
    if (!l_vEvents.empty()) {
        QStringList l_oNames;
        for (const std::string& l_strE : l_vEvents) l_oNames << QString::fromStdString(l_strE);
        emit f_EventsFired(l_oNames, m_oPlayer.f_Time());
    }
    if (m_oPlayer.f_Finished()) {
        if (m_oPlayer.f_Showing() && toms::anim::f_HasLoopingNodes(m_oClip)) {
            // As in the game: the clip's timeline is over, its looping nodes keep playing (pause stops).
            m_pDoc->f_SetPreviewTime(m_oPlayer.f_PoseTime());
            return;
        }
        f_Pause();
        if (!m_oPlayer.f_Showing()) emit m_pDoc->f_Message(tr("Clip finished (stayAtLastFrame is off: the game hides it now)."));
    }
}

void AnimTransport::f_StepKey(int l_iDirection)
{
    const toms::anim::Node* l_pN = m_pDoc->f_SelectedNode();
    if (!l_pN) return;
    f_Pause();
    const float l_fT = m_pDoc->f_Time();
    const std::vector<float> l_vKeys = f_UnionKeyTimes(*l_pN);
    if (l_iDirection > 0) {
        for (float l_fK : l_vKeys)
            if (l_fK > l_fT + g_fTimeEps) return m_pDoc->f_SetTime(l_fK);
    } else {
        for (auto l_itIt = l_vKeys.rbegin(); l_itIt != l_vKeys.rend(); ++l_itIt)
            if (*l_itIt < l_fT - g_fTimeEps) return m_pDoc->f_SetTime(*l_itIt);
    }
}

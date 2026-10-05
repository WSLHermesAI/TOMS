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

AnimScrubber::AnimScrubber(AnimDocument* doc, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip(tr("Playhead: click or drag (snaps to the selected node's keys)"));
    connect(doc, &AnimDocument::timeChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::fileChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::clipChanged, this, qOverload<>(&QWidget::update));
}

float AnimScrubber::span() const { return std::max({m_doc->duration(), m_doc->time(), 0.001f}); }

QRectF AnimScrubber::track() const { return QRectF(8, 4, width() - 16, height() - 8); }

void AnimScrubber::paintEvent(QPaintEvent*)
{
    const Theme::Colors& tc = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF tr_ = track();
    p.setPen(Qt::NoPen);
    p.setBrush(palette().color(QPalette::Base));
    p.drawRoundedRect(tr_, 3, 3);
    const float s = span(), d = m_doc->duration();
    auto x = [&](double t) { return tr_.left() + t / s * tr_.width(); };
    if (d > 0 && d < s) {   // past the clip's end
        p.setBrush(QColor(0x80, 0x80, 0x80, 50));
        p.drawRect(QRectF(QPointF(x(d), tr_.top()), tr_.bottomRight()));
    }
    // Second / tenth ticks.
    p.setPen(QPen(palette().color(QPalette::PlaceholderText), 1));
    const double step = s > 20 ? 1.0 : s > 2 ? 0.5 : 0.1;
    for (double t = 0; t <= s + 1e-6; t += step) p.drawLine(QPointF(x(t), tr_.bottom() - 4), QPointF(x(t), tr_.bottom()));
    // The selected node's keys.
    if (const toms::anim::Node* n = m_doc->selectedNode()) {
        p.setPen(QPen(tc.key.darker(160), 1));
        p.setBrush(tc.key);
        for (float t : unionKeyTimes(*n)) {
            const QPointF c(x(t), tr_.center().y());
            p.drawPolygon(QPolygonF({c + QPointF(0, -5), c + QPointF(5, 0), c + QPointF(0, 5), c + QPointF(-5, 0)}));
        }
    }
    // Playhead.
    const double px = x(m_doc->time());
    p.setPen(QPen(tc.selection, 2));
    p.drawLine(QPointF(px, 1), QPointF(px, height() - 1));
}

float AnimScrubber::timeAt(double px) const
{
    const QRectF tr_ = track();
    float t = float(std::clamp((px - tr_.left()) / tr_.width(), 0.0, 1.0) * span());
    if (const toms::anim::Node* n = m_doc->selectedNode())
        for (float k : unionKeyTimes(*n))
            if (std::fabs((k - t) / span() * tr_.width()) < 5) t = k;
    return t;
}

void AnimScrubber::seek(double px)
{
    m_doc->setTime(timeAt(px));
    emit scrubbed();
}

void AnimScrubber::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) seek(e->position().x());
}

void AnimScrubber::mouseMoveEvent(QMouseEvent* e)
{
    if (e->buttons() & Qt::LeftButton) seek(e->position().x());
}

// ---- AnimTransport ------------------------------------------------------------------------------

AnimTransport::AnimTransport(AnimDocument* doc, QWidget* parentWidget)
    : QObject(parentWidget)
    , m_doc(doc)
    , m_toolBar(new QToolBar(tr("Transport"), parentWidget))
    , m_timer(new QTimer(this))
{
    m_toolBar->setObjectName(QStringLiteral("AnimTransportBar"));
    m_toolBar->setIconSize(QSize(18, 18));
    m_toolBar->setMovable(false);
    m_play = m_toolBar->addAction(Icons::icon(Icons::Id::Play), tr("Play / Pause"));
    m_play->setShortcut(QKeySequence(Qt::Key_Space));
    m_play->setToolTip(tr("Play / pause from the playhead (Space)"));
    m_stop = m_toolBar->addAction(Icons::icon(Icons::Id::Stop), tr("Stop"));
    m_stop->setToolTip(tr("Stop and go back to 0"));
    m_loop = m_toolBar->addAction(Icons::icon(Icons::Id::Loop), tr("Loop Preview"));
    m_loop->setCheckable(true);
    m_loop->setToolTip(tr("Preview as a loop, whatever the clip's play count"));
    m_toolBar->addSeparator();
    m_speed = new QComboBox(m_toolBar);
    for (double s : {0.1, 0.25, 0.5, 1.0, 2.0}) m_speed->addItem(QStringLiteral("%1×").arg(s), s);
    m_speed->setCurrentIndex(3);
    m_speed->setToolTip(tr("Playback speed"));
    m_toolBar->addWidget(m_speed);
    m_timeSpin = new QDoubleSpinBox(m_toolBar);
    m_timeSpin->setRange(0, 3600);
    m_timeSpin->setDecimals(3);
    m_timeSpin->setSingleStep(1.0 / 60);
    m_timeSpin->setSuffix(QStringLiteral(" s"));
    m_timeSpin->setKeyboardTracking(false);
    m_timeSpin->setToolTip(tr("Playhead time"));
    m_timeSpin->setMinimumWidth(90);
    m_toolBar->addWidget(m_timeSpin);
    m_scrubber = new AnimScrubber(doc, m_toolBar);
    m_toolBar->addWidget(m_scrubber);
    m_duration = new QLabel(m_toolBar);
    m_duration->setContentsMargins(6, 0, 6, 0);
    m_duration->setMinimumWidth(80);
    m_toolBar->addWidget(m_duration);

    connect(m_play, &QAction::triggered, this, &AnimTransport::togglePlay);
    connect(m_stop, &QAction::triggered, this, &AnimTransport::stop);
    connect(m_loop, &QAction::toggled, this, [this] {
        if (m_playing) restart(m_doc->time());
    });
    connect(m_timeSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        pause();
        m_doc->setTime(float(v));
    });
    connect(m_scrubber, &AnimScrubber::scrubbed, this, &AnimTransport::pause);
    connect(m_timer, &QTimer::timeout, this, &AnimTransport::tick);
    connect(doc, &AnimDocument::timeChanged, this, [this] {
        if (m_playing && !m_settingTime) pause();   // someone moved the playhead
        updateWidgets();
    });
    connect(doc, &AnimDocument::fileChanged, this, [this] {
        if (m_playing) restart(m_doc->time());   // play the edited clip from here on
        updateWidgets();
    });
    connect(doc, &AnimDocument::clipChanged, this, [this] {
        pause();
        updateWidgets();
    });
    connect(doc, &AnimDocument::fileReset, this, &AnimTransport::pause);
    updateWidgets();
}

void AnimTransport::updateWidgets()
{
    const QSignalBlocker b(m_timeSpin);
    m_timeSpin->setValue(m_doc->time());
    const toms::anim::Clip* c = m_doc->clip();
    m_duration->setText(c ? tr("/ %1 s%2").arg(c->duration(), 0, 'f', 3).arg(c->playCount < 0 ? tr("  loop") : QString()) : QString());
    m_play->setIcon(Icons::icon(m_playing ? Icons::Id::Pause : Icons::Id::Play));
}

void AnimTransport::restart(float atTime)
{
    const toms::anim::Clip* c = m_doc->clip();
    if (!c) return;
    m_clip = *c;
    if (m_loop->isChecked()) m_clip.playCount = -1;
    m_player.play(&m_clip);
    // Skip to the playhead; the events in between are not "fired". From 0 nothing is skipped:
    // the time-0 events go out with the first update, like in the game.
    if (atTime > 0) {
        m_player.update(int(std::lround(atTime * 1000.0)));
        m_player.takeEvents();
    }
    m_carryMs = 0;
}

void AnimTransport::play()
{
    const toms::anim::Clip* c = m_doc->clip();
    if (!c || m_playing) return;
    float t = m_doc->time();
    if (t >= c->duration() - 1e-4f) t = 0;
    restart(t);
    m_playing = true;
    m_clock.start();
    m_timer->start(15);
    updateWidgets();
    emit playingChanged(true);
}

void AnimTransport::pause()
{
    if (!m_playing) return;
    m_playing = false;
    m_doc->setPreviewTime(-1);   // back to the playhead (edits happen there)
    m_timer->stop();
    updateWidgets();
    emit playingChanged(false);
}

void AnimTransport::togglePlay()
{
    if (m_playing) pause();
    else play();
}

void AnimTransport::stop()
{
    pause();
    m_doc->setTime(0);
}

void AnimTransport::tick()
{
    const double speed = m_speed->currentData().toDouble();
    m_carryMs += double(m_clock.restart()) * speed;
    const int whole = int(m_carryMs);
    m_carryMs -= whole;
    if (whole > 0) advance(whole);
}

void AnimTransport::advance(int ms)
{
    if (!m_playing) {
        play();
        if (!m_playing) return;
    }
    m_player.update(ms);
    const std::vector<std::string> events = m_player.takeEvents();
    m_settingTime = true;
    m_doc->setTime(m_player.time());
    m_settingTime = false;
    if (!events.empty()) {
        QStringList names;
        for (const std::string& e : events) names << QString::fromStdString(e);
        emit eventsFired(names, m_player.time());
    }
    if (m_player.finished()) {
        if (m_player.showing() && toms::anim::hasLoopingNodes(m_clip)) {
            // As in the game: the clip's timeline is over, its looping nodes keep playing (pause stops).
            m_doc->setPreviewTime(m_player.poseTime());
            return;
        }
        pause();
        if (!m_player.showing()) emit m_doc->message(tr("Clip finished (stayAtLastFrame is off: the game hides it now)."));
    }
}

void AnimTransport::stepKey(int direction)
{
    const toms::anim::Node* n = m_doc->selectedNode();
    if (!n) return;
    pause();
    const float t = m_doc->time();
    const std::vector<float> keys = unionKeyTimes(*n);
    if (direction > 0) {
        for (float k : keys)
            if (k > t + kTimeEps) return m_doc->setTime(k);
    } else {
        for (auto it = keys.rbegin(); it != keys.rend(); ++it)
            if (*it < t - kTimeEps) return m_doc->setTime(*it);
    }
}

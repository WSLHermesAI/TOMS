#include "TimelineDock.h"

#include "ParticleDocument.h"
#include "ParticlePlayback.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>

#include <cmath>

using toms::fx::Effect;
using toms::fx::Emitter;

namespace {
constexpr double kNames = 110;   // the lane names column
constexpr double kRuler = 22;
constexpr double kLane = 22;
}

TimelineView::TimelineView(ParticleDocument* doc, ParticlePlayback* playback, QWidget* parent)
    : QWidget(parent)
    , m_doc(doc)
    , m_play(playback)
{
    setMouseTracking(true);
    setMinimumHeight(int(kRuler + kLane * 2));
    connect(playback, &ParticlePlayback::ticked, this, qOverload<>(&QWidget::update));
    connect(doc, &ParticleDocument::fileChanged, this, [this] {
        updateGeometry();
        update();
    });
    connect(doc, &ParticleDocument::selectionChanged, this, [this] {
        updateGeometry();
        update();
    });
}

QSize TimelineView::sizeHint() const
{
    const Effect* e = m_doc->effect();
    return QSize(500, int(kRuler + kLane * (e ? std::max<size_t>(1, e->emitters.size()) : 1) + 6));
}

float TimelineView::span() const
{
    const Effect* e = m_doc->effect();
    if (!e) return 4;
    if (e->duration > 0) return e->duration;
    float s = 4;
    for (const Emitter& m : e->emitters) {
        s = std::max({s, m.stop + 1, m.start + 2});
        for (const auto& b : m.bursts) s = std::max(s, m.start + b.t + 1);
    }
    return std::ceil(s);
}

double TimelineView::xOf(float t) const { return kNames + t / span() * (width() - kNames - 10); }

float TimelineView::timeAt(double x) const
{
    const float t = float((x - kNames) / (width() - kNames - 10) * span());
    return std::clamp(std::round(t * 100) / 100, 0.0f, span());
}

QRectF TimelineView::laneRect(int i) const { return QRectF(0, kRuler + i * kLane, width(), kLane); }

QRectF TimelineView::barRect(int i) const
{
    const Effect* e = m_doc->effect();
    if (!e || i < 0 || i >= int(e->emitters.size())) return QRectF();
    const Emitter& m = e->emitters[size_t(i)];
    const float end = m.stop > 0 ? m.stop : span();
    const QRectF lane = laneRect(i);
    return QRectF(QPointF(xOf(m.start), lane.top() + 4), QPointF(xOf(end), lane.bottom() - 4));
}

void TimelineView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const Theme::Colors& tc = Theme::colors();
    p.fillRect(rect(), palette().color(QPalette::Base));
    const Effect* e = m_doc->effect();
    // Ruler.
    p.fillRect(QRectF(0, 0, width(), kRuler), palette().color(QPalette::AlternateBase));
    const float s = span();
    const float step = s <= 2 ? 0.25f : s <= 6 ? 0.5f : s <= 20 ? 1.0f : 5.0f;
    p.setPen(palette().color(QPalette::Text));
    for (float t = 0; t <= s + 1e-4f; t += step) {
        const double x = xOf(t);
        const bool major = std::fabs(std::fmod(t + 1e-4f, step * 2)) < 1e-3f;
        p.drawLine(QPointF(x, kRuler - (major ? 9 : 5)), QPointF(x, kRuler));
        if (major) p.drawText(QRectF(x + 2, 1, 50, 12), Qt::AlignLeft | Qt::AlignTop, QString::number(t, 'g', 3));
    }
    if (!e) return;
    if (e->duration <= 0) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(QRectF(4, 2, kNames - 6, kRuler - 4), Qt::AlignLeft | Qt::AlignVCenter, tr("endless"));
    }
    // Lanes.
    for (int i = 0; i < int(e->emitters.size()); i++) {
        const Emitter& m = e->emitters[size_t(i)];
        const QRectF lane = laneRect(i);
        if (i == m_doc->emitterIndex()) p.fillRect(lane, QColor(tc.selection.red(), tc.selection.green(), tc.selection.blue(), 45));
        p.setPen(palette().color(QPalette::Text));
        p.drawText(lane.adjusted(6, 0, -(width() - kNames), 0), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(QString::fromStdString(m.name), Qt::ElideRight, int(kNames - 10)));
        const QRectF bar = barRect(i);
        if (m.rate > 0) {
            QColor c = m.blend == toms::fx::Blend::add() ? QColor(230, 150, 60) : QColor(110, 150, 220);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(bar, 3, 3);
            if (m.stop <= 0) {   // open-ended: until the effect ends
                p.setPen(QPen(Qt::white, 1));
                p.drawText(bar.adjusted(0, 0, -4, 0), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("→"));
            }
        } else {
            p.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(bar, 3, 3);
        }
        // Bursts.
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(tc.key);
        const float end = std::min(span(), m.stop > 0 ? m.stop : span());
        for (const toms::fx::Burst& b : m.bursts) {
            const int times = b.repeat < 0 || b.interval <= 0 ? (b.interval > 0 ? 1000 : 1) : b.repeat + 1;
            for (int k = 0; k < times; k++) {
                const float t = m.start + b.t + (b.interval > 0 ? k * b.interval : 0);
                if (t > end + 1e-4f) break;
                const QPointF c(xOf(t), lane.center().y());
                QPolygonF d;
                d << c + QPointF(0, -6) << c + QPointF(5, 0) << c + QPointF(0, 6) << c + QPointF(-5, 0);
                p.drawPolygon(d);
            }
        }
    }
    // Playhead.
    const double x = xOf(std::min(m_play->time(), s));
    p.setPen(QPen(QColor(230, 60, 60), 2));
    p.drawLine(QPointF(x, 0), QPointF(x, height()));
}

void TimelineView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    m_gesture++;
    const QPointF pos = e->position();
    if (pos.y() < kRuler) {
        m_drag = Drag::Seek;
        m_play->seek(timeAt(pos.x()));
        return;
    }
    const Effect* ef = m_doc->effect();
    const int lane = int((pos.y() - kRuler) / kLane);
    if (!ef || lane < 0 || lane >= int(ef->emitters.size())) return;
    m_doc->select(m_doc->effectIndex(), lane);
    const QRectF bar = barRect(lane);
    const Emitter& m = ef->emitters[size_t(lane)];
    m_lane = lane;
    m_pressT = timeAt(pos.x());
    m_startStart = m.start;
    m_startStop = m.stop;
    if (std::fabs(pos.x() - bar.left()) <= 5) m_drag = Drag::Start;
    else if (std::fabs(pos.x() - bar.right()) <= 5) m_drag = Drag::Stop;
    else if (bar.contains(pos)) m_drag = Drag::Move;
    else m_drag = Drag::None;
}

void TimelineView::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = e->position();
    if (m_drag == Drag::None) {
        const Effect* ef = m_doc->effect();
        const int lane = int((pos.y() - kRuler) / kLane);
        Qt::CursorShape cur = Qt::ArrowCursor;
        if (ef && pos.y() >= kRuler && lane >= 0 && lane < int(ef->emitters.size())) {
            const QRectF bar = barRect(lane);
            if (std::fabs(pos.x() - bar.left()) <= 5 || std::fabs(pos.x() - bar.right()) <= 5) cur = Qt::SizeHorCursor;
            else if (bar.contains(pos)) cur = Qt::OpenHandCursor;
        }
        setCursor(cur);
        return;
    }
    const float t = timeAt(pos.x());
    const QString key = QStringLiteral("timeline%1").arg(m_gesture);
    switch (m_drag) {
    case Drag::Seek: m_play->seek(t); break;
    case Drag::Start:
        m_doc->editEmitter(tr("Emitter start"), [&](Emitter& m) {
            m.start = m.stop > 0 ? std::min(t, m.stop - 0.01f) : t;
        }, key);
        break;
    case Drag::Stop:
        m_doc->editEmitter(tr("Emitter stop"), [&](Emitter& m) {
            m.stop = t >= span() - 1e-3f ? 0.0f : std::max(t, m.start + 0.01f);   // to the very end = open-ended
        }, key);
        break;
    case Drag::Move: {
        const float d = t - m_pressT;
        m_doc->editEmitter(tr("Move emitter in time"), [&](Emitter& m) {
            const float shift = std::max(d, -m_startStart);
            m.start = m_startStart + shift;
            if (m_startStop > 0) m.stop = m_startStop + shift;
        }, key);
        break;
    }
    case Drag::None: break;
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) { m_drag = Drag::None; }

TimelineDock::TimelineDock(ParticleDocument* doc, ParticlePlayback* playback, QWidget* parent)
    : QDockWidget(tr("Timeline"), parent)
    , m_view(new TimelineView(doc, playback, this))
{
    setObjectName(QStringLiteral("ParticleTimelineDock"));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(m_view);
    setWidget(scroll);
}

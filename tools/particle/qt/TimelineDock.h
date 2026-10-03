#pragma once

#include <QDockWidget>
#include <QWidget>

class ParticleDocument;
class ParticlePlayback;

// The current effect over time: a ruler (click / drag = seek, paused there: the preview replays
// with the same seed, so scrubbing is exact) and one lane per emitter with its active window
// (start..stop; open-ended = until the effect ends) and its bursts as ticks. Drag a bar's ends to
// change start / stop, its middle to move both; click a lane to select the emitter.
class TimelineView : public QWidget
{
    Q_OBJECT
public:
    TimelineView(ParticleDocument* doc, ParticlePlayback* playback, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    float span() const;                       // seconds shown
    double xOf(float t) const;
    float timeAt(double x) const;
    QRectF laneRect(int emitter) const;
    QRectF barRect(int emitter) const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    enum class Drag { None, Seek, Start, Stop, Move };
    ParticleDocument* m_doc;
    ParticlePlayback* m_play;
    Drag m_drag = Drag::None;
    int m_lane = -1;
    float m_pressT = 0, m_startStart = 0, m_startStop = 0;
    int m_gesture = 0;
};

class TimelineDock : public QDockWidget
{
    Q_OBJECT
public:
    TimelineDock(ParticleDocument* doc, ParticlePlayback* playback, QWidget* parent = nullptr);
    TimelineView* view() const { return m_view; }
private:
    TimelineView* m_view;
};

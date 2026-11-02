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
    TimelineView(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, QWidget* l_pParent = nullptr);
    QSize sizeHint() const override;
    float f_Span() const;                       // seconds shown
    double f_XOf(float l_fT) const;
    float f_TimeAt(double l_dX) const;
    QRectF f_LaneRect(int l_iEmitter) const;
    QRectF f_BarRect(int l_iEmitter) const;

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;
    void mouseReleaseEvent(QMouseEvent* l_pE) override;

private:
    enum class Drag { None, Seek, Start, Stop, Move };
    ParticleDocument* m_pDoc;
    ParticlePlayback* m_pPlay;
    Drag m_eDrag = Drag::None;
    int m_iLane = -1;
    float m_fPressT = 0, m_fStartStart = 0, m_fStartStop = 0;
    int m_iGesture = 0;
};

class TimelineDock : public QDockWidget
{
    Q_OBJECT
public:
    TimelineDock(ParticleDocument* l_pDoc, ParticlePlayback* l_pPlayback, QWidget* l_pParent = nullptr);
    TimelineView* f_View() const { return m_pView; }
private:
    TimelineView* m_pView;
};

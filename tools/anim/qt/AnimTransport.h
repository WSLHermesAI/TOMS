#pragma once

#include "AnimDocument.h"
#include "anim_player.h"

#include <QElapsedTimer>
#include <QObject>
#include <QWidget>

class QAction;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTimer;
class QToolBar;

// A slider over the clip's duration with the selected node's key times as ticks. Click / drag
// moves the playhead (snapping to a tick within a few pixels). The multi-track timeline is Phase 4.
class AnimScrubber : public QWidget
{
    Q_OBJECT
public:
    explicit AnimScrubber(AnimDocument* doc, QWidget* parent = nullptr);
    QSize sizeHint() const override { return QSize(400, 28); }
    QSize minimumSizeHint() const override { return QSize(120, 24); }
    // The time at x (widget pixels), snapped to a key of the selected node within a few pixels.
    float timeAt(double x) const;

signals:
    void scrubbed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    float span() const;
    QRectF track() const;
    void seek(double x);

    AnimDocument* m_doc;
};

// Playback with AnimPlayer semantics (playCount, stayAtLastFrame, events once each, also across
// loop wraps), driven by a timer; the Loop toggle previews the clip as if playCount were -1.
// Fired events go to eventsFired (status bar + log).
class AnimTransport : public QObject
{
    Q_OBJECT

public:
    AnimTransport(AnimDocument* doc, QWidget* parentWidget);

    QToolBar* toolBar() const { return m_toolBar; }
    QAction* playAction() const { return m_play; }
    QAction* stopAction() const { return m_stop; }
    QAction* loopAction() const { return m_loop; }
    AnimScrubber* scrubber() const { return m_scrubber; }
    bool isPlaying() const { return m_playing; }

    void play();     // from the playhead (from the start when it is at the end)
    void pause();
    void togglePlay();
    void stop();     // pause and back to 0
    // Advances playback by ms as a timer tick would (the selftest drives this directly).
    void advance(int ms);
    void stepKey(int direction);   // previous / next key time of the selected node

signals:
    void eventsFired(const QStringList& names, float time);
    void playingChanged(bool playing);

private:
    void restart(float atTime);
    void tick();
    void updateWidgets();

    AnimDocument* m_doc;
    QToolBar* m_toolBar;
    QAction *m_play, *m_stop, *m_loop;
    QComboBox* m_speed;
    QDoubleSpinBox* m_timeSpin;
    QLabel* m_duration;
    AnimScrubber* m_scrubber;
    QTimer* m_timer;
    QElapsedTimer m_clock;
    double m_carryMs = 0;
    bool m_playing = false;
    bool m_settingTime = false;
    toms::anim::Clip m_clip;       // the clip being played (a copy: edits restart it)
    toms::anim::AnimPlayer m_player;
};

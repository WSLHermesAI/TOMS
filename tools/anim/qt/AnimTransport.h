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
    explicit AnimScrubber(AnimDocument* l_pDoc, QWidget* l_pParent = nullptr);
    QSize sizeHint() const override { return QSize(400, 28); }
    QSize minimumSizeHint() const override { return QSize(120, 24); }
    // The time at x (widget pixels), snapped to a key of the selected node within a few pixels.
    float f_TimeAt(double l_dX) const;

signals:
    void f_Scrubbed();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* l_pE) override;
    void mouseMoveEvent(QMouseEvent* l_pE) override;

private:
    float f_Span() const;
    QRectF f_Track() const;
    void f_Seek(double l_dX);

    AnimDocument* m_pDoc;
};

// Playback with AnimPlayer semantics (playCount, stayAtLastFrame, events once each, also across
// loop wraps), driven by a timer; the Loop toggle previews the clip as if playCount were -1.
// Fired events go to eventsFired (status bar + log).
class AnimTransport : public QObject
{
    Q_OBJECT

public:
    AnimTransport(AnimDocument* l_pDoc, QWidget* l_pParentWidget);

    QToolBar* f_ToolBar() const { return m_pToolBar; }
    QAction* f_PlayAction() const { return m_pPlay; }
    QAction* f_StopAction() const { return m_pStop; }
    QAction* f_LoopAction() const { return m_pLoop; }
    AnimScrubber* f_Scrubber() const { return m_pScrubber; }
    bool f_IsPlaying() const { return m_bPlaying; }

    void f_Play();     // from the playhead (from the start when it is at the end)
    void f_Pause();
    void f_TogglePlay();
    void f_Stop();     // pause and back to 0
    // Advances playback by ms as a timer tick would (the selftest drives this directly).
    void f_Advance(int l_iMs);
    void f_StepKey(int l_iDirection);   // previous / next key time of the selected node

signals:
    void f_EventsFired(const QStringList& l_oNames, float l_fTime);
    void f_PlayingChanged(bool l_bPlaying);

private:
    void f_Restart(float l_fAtTime);
    void f_Tick();
    void f_UpdateWidgets();

    AnimDocument* m_pDoc;
    QToolBar* m_pToolBar;
    QAction *m_pPlay, *m_pStop, *m_pLoop;
    QComboBox* m_pSpeed;
    QDoubleSpinBox* m_pTimeSpin;
    QLabel* m_pDuration;
    AnimScrubber* m_pScrubber;
    QTimer* m_pTimer;
    QElapsedTimer m_oClock;
    double m_dCarryMs = 0;
    bool m_bPlaying = false;
    bool m_bSettingTime = false;
    toms::anim::Clip m_oClip;       // the clip being played (a copy: edits restart it)
    toms::anim::AnimPlayer m_oPlayer;
};

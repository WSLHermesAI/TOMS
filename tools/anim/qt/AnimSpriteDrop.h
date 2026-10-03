#pragma once

#include <QObject>
#include <QPoint>
#include <QStringList>

#include <functional>

class QWidget;

// Lets a widget take sprites dragged from the Sprites dock without subclassing it (an event
// filter): a drop sends the references, in list order, and the time under the cursor. The
// scrubber, the key list and the Timeline tab use it to insert a sprite sequence (MPDI style).
class AnimSpriteDrop : public QObject
{
    Q_OBJECT
public:
    using TimeAt = std::function<float(const QPoint&)>;   // widget position -> clip time
    AnimSpriteDrop(QWidget* target, TimeAt timeAt);

signals:
    void dropped(const QStringList& refs, float time);

protected:
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    QWidget* m_target;
    TimeAt m_timeAt;
};

#include "AnimSpriteDrop.h"

#include "AnimSpritesDock.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QWidget>

AnimSpriteDrop::AnimSpriteDrop(QWidget* target, TimeAt timeAt)
    : QObject(target)
    , m_target(target)
    , m_timeAt(std::move(timeAt))
{
    target->setAcceptDrops(true);
    target->installEventFilter(this);
}

bool AnimSpriteDrop::eventFilter(QObject* watched, QEvent* e)
{
    if (watched != m_target) return false;
    switch (e->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* d = static_cast<QDragMoveEvent*>(e);
        if (AnimSpriteList::refsFrom(d->mimeData()).isEmpty()) return false;
        d->setDropAction(Qt::CopyAction);
        d->accept();
        return true;
    }
    case QEvent::Drop: {
        auto* d = static_cast<QDropEvent*>(e);
        const QStringList refs = AnimSpriteList::refsFrom(d->mimeData());
        if (refs.isEmpty()) return false;
        d->setDropAction(Qt::CopyAction);
        d->accept();
        const float t = m_timeAt(d->position().toPoint());
        // After the drag's own event loop has finished: the receiver may open a dialog.
        QMetaObject::invokeMethod(this, [this, refs, t] { emit dropped(refs, t); }, Qt::QueuedConnection);
        return true;
    }
    default:
        return false;
    }
}

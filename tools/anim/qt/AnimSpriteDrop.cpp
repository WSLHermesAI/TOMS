#include "AnimSpriteDrop.h"

#include "AnimSpritesDock.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QWidget>

AnimSpriteDrop::AnimSpriteDrop(QWidget* l_pTarget, TimeAt l_fnTimeAt)
    : QObject(l_pTarget)
    , m_pTarget(l_pTarget)
    , m_fnTimeAt(std::move(l_fnTimeAt))
{
    l_pTarget->setAcceptDrops(true);
    l_pTarget->installEventFilter(this);
}

bool AnimSpriteDrop::eventFilter(QObject* l_pWatched, QEvent* l_pE)
{
    if (l_pWatched != m_pTarget) return false;
    switch (l_pE->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* l_pD = static_cast<QDragMoveEvent*>(l_pE);
        if (AnimSpriteList::f_RefsFrom(l_pD->mimeData()).isEmpty()) return false;
        l_pD->setDropAction(Qt::CopyAction);
        l_pD->accept();
        return true;
    }
    case QEvent::Drop: {
        auto* l_pD = static_cast<QDropEvent*>(l_pE);
        const QStringList l_oRefs = AnimSpriteList::f_RefsFrom(l_pD->mimeData());
        if (l_oRefs.isEmpty()) return false;
        l_pD->setDropAction(Qt::CopyAction);
        l_pD->accept();
        const float l_fT = m_fnTimeAt(l_pD->position().toPoint());
        // After the drag's own event loop has finished: the receiver may open a dialog.
        QMetaObject::invokeMethod(this, [this, l_oRefs, l_fT] { emit f_Dropped(l_oRefs, l_fT); }, Qt::QueuedConnection);
        return true;
    }
    default:
        return false;
    }
}

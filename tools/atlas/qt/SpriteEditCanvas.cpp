#include "SpriteEditCanvas.h"

#include "AtlasDocument.h"
#include "Theme.h"

#include <QContextMenuEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace {

constexpr double kHandleRadius = 5.0;   // screen pixels
constexpr double kGrabDistance = 6.0;

QString qs(const std::string& s) { return QString::fromStdString(s); }
QRect toQRect(const atlas::IRect& r) { return QRect(r.x, r.y, r.w, r.h); }
atlas::IRect toIRect(const QRect& r) { return atlas::IRect{r.x(), r.y(), r.width(), r.height()}; }

}  // namespace

SpriteEditCanvas::SpriteEditCanvas(AtlasDocument* doc, QWidget* parent)
    : CanvasView(parent)
    , m_doc(doc)
{
    connect(doc, &AtlasDocument::buildFinished, this, &SpriteEditCanvas::reload);
    connect(doc, &AtlasDocument::projectChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AtlasDocument::selectionChanged, this, qOverload<>(&QWidget::update));
}

void SpriteEditCanvas::setTarget(const QString& name)
{
    m_target = name;
    m_drag = Drag::None;
    reload();
    requestFit();
    emit targetChanged(m_target);
}

void SpriteEditCanvas::reload()
{
    if (m_target.isEmpty()) return;
    const BuildSnapshotPtr snap = m_doc->snapshot();
    if (snap && !snap->region(m_target) && !m_doc->spriteExists(m_target)) {
        // Deleted (or renamed) while being edited.
        m_target.clear();
        emit exitRequested();
        return;
    }
    const QImage img = m_doc->spriteImage(m_target);
    if (!img.isNull()) m_image = img;   // keep the old pixels while the sprite fails to build
    update();
}

QVector<SpriteEditCanvas::Child> SpriteEditCanvas::children() const
{
    QVector<Child> out;
    const std::string t = m_target.toStdString();
    for (const atlas::SpriteDef& d : m_doc->project().sprites)
        if (d.parent == t) out.push_back({qs(d.name), toQRect(d.rect)});
    return out;
}

QString SpriteEditCanvas::focus() const
{
    const QString cur = m_doc->currentSprite();
    for (const Child& c : children())
        if (c.name == cur) return cur;
    return m_target;
}

QRect SpriteEditCanvas::focusRect() const
{
    const QString f = focus();
    if (f == m_target) return QRect(QPoint(0, 0), m_image.size());
    return toQRect(m_doc->spriteDef(f).rect);
}

QPointF SpriteEditCanvas::pivotPoint(const QString& name, const QRect& r) const
{
    const atlas::SpriteDef d = m_doc->spriteDef(name);
    const float* pv = d.hasPivot ? d.pivot : m_doc->project().settings.defaultPivot;
    return QPointF(r.x() + pv[0] * r.width(), r.y() + pv[1] * r.height());
}

int SpriteEditCanvas::childAt(const QVector<Child>& kids, const QPointF& c) const
{
    int best = -1;
    for (int i = 0; i < kids.size(); i++) {
        if (!QRectF(kids[i].rect).contains(c)) continue;
        if (best < 0 || kids[i].rect.width() * kids[i].rect.height() < kids[best].rect.width() * kids[best].rect.height()) best = i;
    }
    return best;
}

QPointF SpriteEditCanvas::handlePos(const QRect& r, int handle) const
{
    const QRectF w = toWidget(QRectF(r));
    switch (handle) {
    case 0: return w.topLeft();
    case 1: return QPointF(w.center().x(), w.top());
    case 2: return w.topRight();
    case 3: return QPointF(w.right(), w.center().y());
    case 4: return w.bottomRight();
    case 5: return QPointF(w.center().x(), w.bottom());
    case 6: return w.bottomLeft();
    default: return QPointF(w.left(), w.center().y());
    }
}

int SpriteEditCanvas::handleAt(const QRect& r, const QPointF& pos) const
{
    for (int h = 0; h < 8; h++)
        if ((handlePos(r, h) - pos).manhattanLength() <= kGrabDistance * 1.5) return h;
    return -1;
}

int SpriteEditCanvas::splitAt(const QPointF& pos) const
{
    const atlas::SpriteDef d = m_doc->spriteDef(focus());
    if (!d.hasSplit) return -1;
    const QRectF fr = toWidget(QRectF(focusRect()));
    if (!fr.adjusted(-kGrabDistance, -kGrabDistance, kGrabDistance, kGrabDistance).contains(pos)) return -1;
    const double z = zoom();
    const double xs[2] = {fr.left() + d.split[0] * z, fr.right() - d.split[1] * z};
    const double ys[2] = {fr.top() + d.split[2] * z, fr.bottom() - d.split[3] * z};
    for (int i = 0; i < 2; i++)
        if (std::abs(pos.x() - xs[i]) <= kGrabDistance) return i;
    for (int i = 0; i < 2; i++)
        if (std::abs(pos.y() - ys[i]) <= kGrabDistance) return 2 + i;
    return -1;
}

QRect SpriteEditCanvas::clampToImage(QRect r) const
{
    r = r.normalized();
    r = r.intersected(QRect(QPoint(0, 0), m_image.size()));
    return r;
}

// ---- painting -----------------------------------------------------------------------------------

void SpriteEditCanvas::paintContent(QPainter& p)
{
    const Theme::Colors& tc = Theme::colors();
    if (m_image.isNull()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect(), Qt::AlignCenter, m_target.isEmpty() ? tr("Select a sprite to edit") : tr("Building…"));
        return;
    }
    const QRectF imgRect = toWidget(QRectF(QPointF(0, 0), QSizeF(m_image.size())));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom() < 1.0);
    p.drawImage(imgRect, m_image);

    // Pixel grid once pixels are big enough to aim at.
    if (zoom() >= 8) {
        QColor grid = tc.label;
        grid.setAlpha(28);
        p.setPen(QPen(grid, 1));
        const QRectF vis = imgRect.intersected(QRectF(rect()));
        const QPointF c0 = toContent(vis.topLeft()), c1 = toContent(vis.bottomRight());
        for (int x = int(std::ceil(c0.x())); x <= int(c1.x()); x++) p.drawLine(QLineF(toWidget(QPointF(x, c0.y())), toWidget(QPointF(x, c1.y()))));
        for (int y = int(std::ceil(c0.y())); y <= int(c1.y()); y++) p.drawLine(QLineF(toWidget(QPointF(c0.x(), y)), toWidget(QPointF(c1.x(), y))));
    }

    const QStringList& selection = m_doc->selection();
    const QString cur = focus();
    const QVector<Child> kids = children();
    const QFontMetrics fm(font());
    for (const Child& c : kids) {
        QRect r = c.rect;
        if ((m_drag == Drag::Move || m_drag == Drag::Resize) && c.name == m_dragName) r = m_liveRect;
        const QRectF w = toWidget(QRectF(r));
        const bool sel = selection.contains(c.name);
        p.setBrush(Qt::NoBrush);
        if (sel) {
            QColor fill = tc.selection;
            fill.setAlpha(35);
            p.fillRect(w, fill);
            p.setPen(QPen(tc.selection, 2));
        } else {
            p.setPen(QPen(tc.childOutline, 1, Qt::DashLine));
        }
        p.drawRect(w);
        // Name inside the rect when it fits.
        const QString label = c.name.section(QLatin1Char('/'), -1);
        if (w.width() > fm.horizontalAdvance(label) + 8 && w.height() > fm.height() + 4) {
            p.setPen(sel ? tc.selection : tc.childOutline);
            p.drawText(w.adjusted(4, 2, -2, -2), Qt::AlignLeft | Qt::AlignTop, label);
        }
    }

    // 9-slice guides of the focus sprite.
    const atlas::SpriteDef fd = m_doc->spriteDef(cur);
    const QRect fr = (m_drag == Drag::Move || m_drag == Drag::Resize) && m_dragName == cur ? m_liveRect : focusRect();
    if (fd.hasSplit) {
        int s[4];
        for (int i = 0; i < 4; i++) s[i] = m_drag == Drag::Split ? m_liveSplit[i] : fd.split[i];
        const QRectF w = toWidget(QRectF(fr));
        const double z = zoom();
        p.setPen(QPen(tc.slice, 1, Qt::DashLine));
        const double xl = w.left() + s[0] * z, xr = w.right() - s[1] * z;
        const double yt = w.top() + s[2] * z, yb = w.bottom() - s[3] * z;
        p.drawLine(QLineF(xl, w.top(), xl, w.bottom()));
        p.drawLine(QLineF(xr, w.top(), xr, w.bottom()));
        p.drawLine(QLineF(w.left(), yt, w.right(), yt));
        p.drawLine(QLineF(w.left(), yb, w.right(), yb));
        if (m_drag == Drag::Split)
            drawLabel(p, w.bottomLeft() + QPointF(0, 6), tr("9-slice  L %1  R %2  T %3  B %4").arg(s[0]).arg(s[1]).arg(s[2]).arg(s[3]));
    }

    // Handles of the current child.
    if (cur != m_target) {
        p.setPen(QPen(tc.labelBackground, 1));
        p.setBrush(tc.selection);
        p.setRenderHint(QPainter::Antialiasing);
        for (int h = 0; h < 8; h++) {
            const QPointF c = handlePos(fr, h);
            p.drawRect(QRectF(c.x() - kHandleRadius, c.y() - kHandleRadius, kHandleRadius * 2, kHandleRadius * 2));
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // Pivot of the focus sprite.
    {
        QPointF pv = pivotPoint(cur, fr);
        if (m_drag == Drag::Pivot) pv = QPointF(fr.x() + m_livePivot.x() * fr.width(), fr.y() + m_livePivot.y() * fr.height());
        const QPointF w = toWidget(pv);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0, 0, 0, 160), 3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(w, 6, 6);
        p.setPen(QPen(tc.pivot, 1.5));
        p.drawEllipse(w, 6, 6);
        p.drawLine(QLineF(w.x() - 10, w.y(), w.x() + 10, w.y()));
        p.drawLine(QLineF(w.x(), w.y() - 10, w.x(), w.y() + 10));
        p.setRenderHint(QPainter::Antialiasing, false);
        if (m_drag == Drag::Pivot)
            drawLabel(p, w + QPointF(12, 8), tr("pivot %1, %2").arg(m_livePivot.x(), 0, 'f', 3).arg(m_livePivot.y(), 0, 'f', 3));
    }

    // The rect being drawn or changed, with its numbers.
    if (m_drag == Drag::Create || m_drag == Drag::Move || m_drag == Drag::Resize) {
        const QRectF w = toWidget(QRectF(m_liveRect));
        if (m_drag == Drag::Create) {
            p.setPen(QPen(tc.selection, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(w);
        }
        drawLabel(p, w.bottomLeft() + QPointF(0, 6),
                  QStringLiteral("%1, %2   %3 × %4").arg(m_liveRect.x()).arg(m_liveRect.y()).arg(m_liveRect.width()).arg(m_liveRect.height()));
    }

    // What is being edited, top-left.
    const QRect cr = focusRect();
    QString info = QStringLiteral("%1   %2 × %3").arg(m_target).arg(m_image.width()).arg(m_image.height());
    if (cur != m_target) info += QStringLiteral("   ▸ %1  %2, %3  %4 × %5").arg(cur).arg(cr.x()).arg(cr.y()).arg(cr.width()).arg(cr.height());
    info += QStringLiteral("   %1%").arg(qRound(zoom() * 100));
    drawLabel(p, QPointF(8, 8), info);
}

// ---- mouse --------------------------------------------------------------------------------------

void SpriteEditCanvas::updateCursor(const QPointF& pos)
{
    if (isPanning()) return;
    const QString cur = focus();
    const QRect fr = focusRect();
    if ((toWidget(pivotPoint(cur, fr)) - pos).manhattanLength() <= kGrabDistance * 1.5) { setCursor(Qt::SizeAllCursor); return; }
    if (const int s = splitAt(pos); s >= 0) { setCursor(s < 2 ? Qt::SplitHCursor : Qt::SplitVCursor); return; }
    if (cur != m_target) {
        static const Qt::CursorShape shapes[8] = {Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor,
                                                  Qt::SizeFDiagCursor, Qt::SizeVerCursor, Qt::SizeBDiagCursor, Qt::SizeHorCursor};
        if (const int h = handleAt(fr, pos); h >= 0) { setCursor(shapes[h]); return; }
    }
    if (childAt(children(), toContent(pos)) >= 0) setCursor(Qt::PointingHandCursor);
    else setCursor(Qt::CrossCursor);
}

void SpriteEditCanvas::mousePressEvent(QMouseEvent* e)
{
    if (panPress(e)) return;
    if (e->button() != Qt::LeftButton || m_image.isNull()) return;
    const QPointF pos = e->position();
    m_pressContent = toContent(pos);
    const QString cur = focus();
    const QRect fr = focusRect();

    if ((toWidget(pivotPoint(cur, fr)) - pos).manhattanLength() <= kGrabDistance * 1.5) {
        m_drag = Drag::Pivot;
        m_dragName = cur;
        const QPointF pv = pivotPoint(cur, fr);
        m_livePivot = QPointF((pv.x() - fr.x()) / std::max(1, fr.width()), (pv.y() - fr.y()) / std::max(1, fr.height()));
        return;
    }
    if (const int s = splitAt(pos); s >= 0) {
        m_drag = Drag::Split;
        m_dragName = cur;
        m_splitSide = s;
        const atlas::SpriteDef d = m_doc->spriteDef(cur);
        std::copy(d.split, d.split + 4, m_liveSplit);
        return;
    }
    if (cur != m_target) {
        if (const int h = handleAt(fr, pos); h >= 0) {
            m_drag = Drag::Resize;
            m_dragName = cur;
            m_handle = h;
            m_startRect = m_liveRect = fr;
            return;
        }
    }
    const QVector<Child> kids = children();
    const int hit = childAt(kids, m_pressContent);
    if (hit >= 0) {
        const QString name = kids[hit].name;
        QStringList sel = m_doc->selection();
        if (e->modifiers() & Qt::ControlModifier) {
            if (sel.contains(name)) sel.removeAll(name);
            else sel << name;
            m_doc->setSelection(sel);
            return;
        }
        m_doc->setSelection({name});
        m_drag = Drag::Move;
        m_dragName = name;
        m_startRect = m_liveRect = kids[hit].rect;
        return;
    }
    // Empty pixels: start a new child rect (and leave the target as the current sprite).
    m_doc->setSelection({m_target});
    m_drag = Drag::Create;
    const QPoint p0(int(std::lround(m_pressContent.x())), int(std::lround(m_pressContent.y())));
    m_startRect = QRect(p0, QSize(0, 0));
    m_liveRect = QRect();
}

void SpriteEditCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (panMove(e)) return;
    const QPointF c = toContent(e->position());
    const QPoint g(int(std::lround(c.x())), int(std::lround(c.y())));   // nearest pixel edge
    const QSize img = m_image.size();
    switch (m_drag) {
    case Drag::None:
        updateCursor(e->position());
        return;
    case Drag::Create: {
        const QPoint a = m_startRect.topLeft();
        m_liveRect = clampToImage(QRect(QPoint(std::min(a.x(), g.x()), std::min(a.y(), g.y())),
                                        QPoint(std::max(a.x(), g.x()) - 1, std::max(a.y(), g.y()) - 1)));
        break;
    }
    case Drag::Move: {
        const int dx = std::clamp(int(std::lround(c.x() - m_pressContent.x())), -m_startRect.x(), img.width() - m_startRect.right() - 1);
        const int dy = std::clamp(int(std::lround(c.y() - m_pressContent.y())), -m_startRect.y(), img.height() - m_startRect.bottom() - 1);
        m_liveRect = m_startRect.translated(dx, dy);
        break;
    }
    case Drag::Resize: {
        // Edges as exclusive coordinates, so a rect can never collapse below one pixel.
        int l = m_startRect.x(), t = m_startRect.y(), r = m_startRect.x() + m_startRect.width(), b = m_startRect.y() + m_startRect.height();
        const int gx = std::clamp(g.x(), 0, img.width()), gy = std::clamp(g.y(), 0, img.height());
        if (m_handle == 0 || m_handle == 6 || m_handle == 7) l = std::min(gx, r - 1);
        if (m_handle == 2 || m_handle == 3 || m_handle == 4) r = std::max(gx, l + 1);
        if (m_handle == 0 || m_handle == 1 || m_handle == 2) t = std::min(gy, b - 1);
        if (m_handle == 4 || m_handle == 5 || m_handle == 6) b = std::max(gy, t + 1);
        m_liveRect = QRect(l, t, r - l, b - t);
        break;
    }
    case Drag::Pivot: {
        const QRect fr = focusRect();
        // Half-pixel steps: the centre of an odd-sized sprite stays reachable.
        const double px = std::round(c.x() * 2) / 2, py = std::round(c.y() * 2) / 2;
        m_livePivot = QPointF((px - fr.x()) / std::max(1, fr.width()), (py - fr.y()) / std::max(1, fr.height()));
        break;
    }
    case Drag::Split: {
        const QRect fr = focusRect();
        int* s = m_liveSplit;
        switch (m_splitSide) {
        case 0: s[0] = std::clamp(g.x() - fr.x(), 0, fr.width() - s[1]); break;
        case 1: s[1] = std::clamp(fr.x() + fr.width() - g.x(), 0, fr.width() - s[0]); break;
        case 2: s[2] = std::clamp(g.y() - fr.y(), 0, fr.height() - s[3]); break;
        default: s[3] = std::clamp(fr.y() + fr.height() - g.y(), 0, fr.height() - s[2]); break;
        }
        break;
    }
    }
    update();
}

void SpriteEditCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (panRelease(e)) {
        updateCursor(e->position());
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    commitDrag();
    m_drag = Drag::None;
    update();
}

void SpriteEditCanvas::commitDrag()
{
    switch (m_drag) {
    case Drag::None:
        break;
    case Drag::Create: {
        if (m_liveRect.width() < 1 || m_liveRect.height() < 1) break;
        const QRect r = m_liveRect;
        m_drag = Drag::None;   // the dialog below runs an event loop: no live drag meanwhile
        QString base = m_target + QStringLiteral("_%1").arg(children().size());
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New Child Sprite"),
                                                   tr("Name of the child sprite (%1, %2  %3 × %4):").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()),
                                                   QLineEdit::Normal, m_doc->uniqueName(base), &ok).trimmed();
        if (!ok || name.isEmpty()) break;
        if (m_doc->spriteExists(name)) {
            QMessageBox::warning(this, tr("New Child Sprite"), tr("There already is a sprite called '%1'.").arg(name));
            break;
        }
        m_doc->addChildren(m_target, {{name, toIRect(r)}}, tr("New child %1").arg(name));
        break;
    }
    case Drag::Move:
    case Drag::Resize:
        if (m_liveRect != m_startRect) {
            const atlas::IRect r = toIRect(m_liveRect);
            m_doc->editSprites({m_dragName}, m_drag == Drag::Move ? tr("Move %1").arg(m_dragName) : tr("Resize %1").arg(m_dragName),
                               [&](atlas::SpriteDef& d) { d.rect = r; });
        }
        break;
    case Drag::Pivot: {
        const QPointF pv = m_livePivot;
        m_doc->editSprites({m_dragName}, tr("Move pivot of %1").arg(m_dragName), [&](atlas::SpriteDef& d) {
            d.hasPivot = true;
            d.pivot[0] = float(pv.x());
            d.pivot[1] = float(pv.y());
        });
        break;
    }
    case Drag::Split: {
        const int* s = m_liveSplit;
        m_doc->editSprites({m_dragName}, tr("9-slice of %1").arg(m_dragName),
                           [&](atlas::SpriteDef& d) { std::copy(s, s + 4, d.split); });
        break;
    }
    }
}

void SpriteEditCanvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const QVector<Child> kids = children();
    const int hit = childAt(kids, toContent(e->position()));
    if (hit >= 0) setTarget(kids[hit].name);   // edit inside the child
}

void SpriteEditCanvas::nudge(int dx, int dy)
{
    const QString cur = focus();
    if (cur == m_target) return;
    const QRect r = focusRect();
    const QRect moved = r.translated(std::clamp(dx, -r.x(), m_image.width() - r.right() - 1),
                                     std::clamp(dy, -r.y(), m_image.height() - r.bottom() - 1));
    if (moved == r) return;
    m_doc->editSprites({cur}, tr("Move %1").arg(cur), [&](atlas::SpriteDef& d) { d.rect = toIRect(moved); },
                       QStringLiteral("nudge"));
}

void SpriteEditCanvas::keyPressEvent(QKeyEvent* e)
{
    const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (e->key()) {
    case Qt::Key_Escape:
        if (m_drag != Drag::None) {
            m_drag = Drag::None;
            update();
        } else if (const atlas::SpriteDef d = m_doc->spriteDef(m_target); d.isChild()) {
            const QString child = m_target;
            setTarget(qs(d.parent));
            m_doc->setSelection({child});
        } else {
            emit exitRequested();
        }
        return;
    case Qt::Key_Left: nudge(-step, 0); return;
    case Qt::Key_Right: nudge(step, 0); return;
    case Qt::Key_Up: nudge(0, -step); return;
    case Qt::Key_Down: nudge(0, step); return;
    default:
        CanvasView::keyPressEvent(e);
    }
}

void SpriteEditCanvas::contextMenuEvent(QContextMenuEvent* e)
{
    const QVector<Child> kids = children();
    const int hit = childAt(kids, toContent(e->pos()));
    if (hit >= 0 && !m_doc->selection().contains(kids[hit].name)) m_doc->setSelection({kids[hit].name});
    else if (hit < 0) m_doc->setSelection({m_target});
    emit contextMenuRequested(e->globalPos());
}

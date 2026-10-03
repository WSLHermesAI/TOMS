#include "AtlasCanvas.h"

#include "AtlasDocument.h"
#include "Icons.h"
#include "Theme.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace {

QRectF frameRect(const atlas::Region& r) { return QRectF(r.frame.x, r.frame.y, r.frame.w, r.frame.h); }

QString qs(const std::string& s) { return QString::fromStdString(s); }

const std::vector<atlas::Region> kNoRegions;   // iterated when there is no build yet

}  // namespace

AtlasCanvas::AtlasCanvas(AtlasDocument* doc, QWidget* parent)
#if TOMS_ATLAS_GAME_RENDERER
    : GameCanvasView(GameCanvasView::gameRendererWanted(QStringLiteral("AtlasEditor")), parent)
#else
    : CanvasView(parent)
#endif
    , m_doc(doc)
{
    connect(doc, &AtlasDocument::buildFinished, this, &AtlasCanvas::onBuildFinished);
    connect(doc, &AtlasDocument::selectionChanged, this, &AtlasCanvas::onSelectionChanged);
    connect(doc, &AtlasDocument::projectChanged, this, &AtlasCanvas::refreshPinned);
    // A new project (or variant) shows its first page, fitted.
    connect(doc, &AtlasDocument::projectReset, this, [this] { m_page = 0; requestFit(); });
}

void AtlasCanvas::setPage(int page)
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const int count = snap ? int(snap->pages.size()) : 0;
    page = std::clamp(page, 0, std::max(0, count - 1));
    if (page == m_page) return;
    m_page = page;
    m_hover.clear();
    update();
    emit pageChanged(m_page);
}

void AtlasCanvas::setShowOutlines(bool show)
{
    m_showOutlines = show;
    update();
}

void AtlasCanvas::setShowChildren(bool show)
{
    m_showChildren = show;
    update();
}

QSize AtlasCanvas::contentSize() const
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    if (!snap || m_page >= snap->pages.size()) return QSize();
    return snap->pages[m_page].size();
}

void AtlasCanvas::onBuildFinished()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const int count = int(snap->pages.size());
    if (m_page >= count) setPage(count - 1);
    // An in-progress drag refers to frames of the old layout; drop it rather than jump.
    if (m_drag == Drag::Move) {
        m_drag = Drag::None;
        m_dragNames.clear();
    }
    update();
}

void AtlasCanvas::onSelectionChanged()
{
    // Follow a selection made elsewhere (tree, problems) to its page and into view.
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const atlas::Region* r = snap ? snap->region(m_doc->currentSprite()) : nullptr;
    if (r && isVisibleTo(window())) {
        setPage(r->page);
        centerOn(frameRect(*r));
    }
    update();
}

void AtlasCanvas::refreshPinned()
{
    m_pinned.clear();
    for (const atlas::SpriteDef& d : m_doc->project().sprites)
        if (d.pinned) m_pinned.insert(qs(d.name));
    update();
}

bool AtlasCanvas::isVisible(const atlas::Region& r) const
{
    return r.page == m_page && r.aliasOf.empty() && (m_showChildren || !r.child || r.baked);
}

bool AtlasCanvas::isMovable(const atlas::Region& r) const { return r.aliasOf.empty() && (!r.child || r.baked); }

QString AtlasCanvas::hitTest(const QPointF& content) const
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    if (!snap) return QString();
    const atlas::Region* best = nullptr;
    for (const atlas::Region& r : snap->result.regions) {
        if (!isVisible(r) || !frameRect(r).contains(content)) continue;
        if (!best || r.frame.w * r.frame.h < best->frame.w * best->frame.h) best = &r;
    }
    return best ? qs(best->name) : QString();
}

QString AtlasCanvas::tooltipFor(const QString& name) const
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const atlas::Region* r = snap ? snap->region(name) : nullptr;
    if (!r) return QString();
    QString t = QStringLiteral("<b>%1</b><br>").arg(name.toHtmlEscaped());
    t += tr("frame %1, %2  %3×%4").arg(r->frame.x).arg(r->frame.y).arg(r->frame.w).arg(r->frame.h);
    t += QStringLiteral("<br>") + tr("original %1×%2").arg(r->origW).arg(r->origH);
    if (r->trimmed()) t += QStringLiteral("<br>") + tr("offset %1, %2").arg(r->offsetX).arg(r->offsetY);
    if (r->child)
        t += QStringLiteral("<br>") + tr("child of %1 at %2, %3%4").arg(qs(r->parent).toHtmlEscaped()).arg(r->local.x)
                                         .arg(r->local.y).arg(r->baked ? tr(" (baked)") : QString());
    QStringList aliases;
    for (const atlas::Region& a : snap->result.regions)
        if (qs(a.aliasOf) == name) aliases << qs(a.name).toHtmlEscaped();
    if (!aliases.isEmpty()) t += QStringLiteral("<br>") + tr("same pixels as: %1").arg(aliases.join(QStringLiteral(", ")));
    if (m_pinned.contains(name)) t += QStringLiteral("<br>") + tr("pinned");
    return t;
}

#if TOMS_ATLAS_GAME_RENDERER
AtlasCanvas::~AtlasCanvas() { releaseGameRenderer(); }

void AtlasCanvas::paintGameScene(const GameScene& s)
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    if (!snap || snap->pages.isEmpty()) return;
    const QImage& page = snap->pages[std::min<int>(m_page, int(snap->pages.size()) - 1)];
    s.image(m_tex.cached(QStringLiteral("page"), page), toWidget(QRectF(QPointF(0, 0), QSizeF(page.size()))));
}
#else
AtlasCanvas::~AtlasCanvas() = default;
static bool overGameScene() { return false; }
#endif

void AtlasCanvas::paintContent(QPainter& p)
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const Theme::Colors& tc = Theme::colors();
    if (!snap || snap->pages.isEmpty()) {
        p.setPen(palette().color(QPalette::PlaceholderText));
        p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
                   m_doc->isBuilding() ? tr("Building…")
                                       : tr("No sprites yet.\nDrop PNG files or folders on the Sprites panel,\n"
                                            "or use File › Add Source Folder…"));
        return;
    }
    const QImage& page = snap->pages[std::min<int>(m_page, int(snap->pages.size()) - 1)];
    const QRectF pageRect = toWidget(QRectF(QPointF(0, 0), QSizeF(page.size())));
    p.setRenderHint(QPainter::SmoothPixmapTransform, zoom() < 1.0);
    if (!overGameScene()) p.drawImage(pageRect, page);   // else the game renderer drew it

    // Aliases share their owner's frame: count them for a badge on the owner.
    QHash<QString, int> aliasCount;
    for (const atlas::Region& r : snap->result.regions)
        if (!r.aliasOf.empty() && r.page == m_page) aliasCount[qs(r.aliasOf)]++;

    const QSet<QString> selected(m_doc->selection().begin(), m_doc->selection().end());
    const QPointF delta(m_moveDelta);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const atlas::Region& r : snap->result.regions) {
        if (!isVisible(r)) continue;
        const QString name = qs(r.name);
        const QRectF w = toWidget(frameRect(r)).adjusted(0.5, 0.5, -0.5, -0.5);
        const bool child = r.child && !r.baked;
        if (name == m_hover) p.fillRect(w, tc.hover);
        if (m_showOutlines) {
            QPen pen(r.child ? tc.childOutline : tc.imageOutline, 1, child ? Qt::DashLine : Qt::SolidLine);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(w);
        }
        if (aliasCount.contains(name) && m_showOutlines) {
            const QString badge = QStringLiteral("=%1").arg(aliasCount[name]);
            const QFontMetrics fm(font());
            const QRectF b(w.right() - fm.horizontalAdvance(badge) - 6, w.top(), fm.horizontalAdvance(badge) + 6, fm.height());
            if (b.width() < w.width() && b.height() < w.height()) {
                p.fillRect(b, tc.aliasOutline);
                p.setPen(Qt::white);
                p.drawText(b, Qt::AlignCenter, badge);
            }
        }
        if (selected.contains(name)) {
            QColor fill = tc.selection;
            fill.setAlpha(40);
            p.fillRect(w, fill);
            p.setPen(QPen(tc.selection, 2));
            p.drawRect(w.adjusted(-1, -1, 1, 1));
        }
        if (m_pinned.contains(name)) {
            const int s = std::clamp(int(std::min(w.width(), w.height()) * 0.6), 10, 18);
            Icons::icon(Icons::Id::Pin).paint(&p, QRect(int(w.left()) - s / 3, int(w.top()) - s / 3, s, s));
        }
    }

    // Sprites being dragged: their pixels at the new place, the old place dashed.
    if (m_drag == Drag::Move) {
        for (const QString& n : m_dragNames) {
            const atlas::Region* r = snap->region(n);
            if (!r) continue;
            const QRectF from = toWidget(frameRect(*r));
            const QRectF to = toWidget(frameRect(*r).translated(delta));
            p.setPen(QPen(tc.selection, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(from);
            p.setOpacity(0.85);
            p.drawImage(to, page, QRectF(frameRect(*r)));
            p.setOpacity(1.0);
            p.setPen(QPen(tc.selection, 2));
            p.drawRect(to);
        }
        if (const atlas::Region* r = snap->region(m_dragNames.value(0))) {
            const QRectF to = toWidget(frameRect(*r).translated(delta));
            drawLabel(p, to.bottomLeft() + QPointF(0, 4),
                      tr("pin at %1, %2").arg(r->frame.x + m_moveDelta.x()).arg(r->frame.y + m_moveDelta.y()));
        }
    }
    if (m_drag == Drag::Rubber) {
        QColor fill = tc.selection;
        fill.setAlpha(30);
        p.setPen(QPen(tc.selection, 1, Qt::DashLine));
        p.setBrush(fill);
        p.drawRect(toWidget(m_rubber.normalized()));
    }
}

bool AtlasCanvas::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip) {
        auto* he = static_cast<QHelpEvent*>(e);
        const QString name = m_drag == Drag::None ? hitTest(toContent(he->pos())) : QString();
        if (name.isEmpty()) QToolTip::hideText();
        else QToolTip::showText(he->globalPos(), tooltipFor(name), this);
        return true;
    }
    return CanvasView::event(e);
}

void AtlasCanvas::mousePressEvent(QMouseEvent* e)
{
    if (panPress(e)) return;
    if (e->button() != Qt::LeftButton) return;
    m_pressWidget = e->position();
    m_pressContent = toContent(e->position());
    m_moveDelta = QPoint();
    m_selectionAtPress = m_doc->selection();
    const QString hit = hitTest(m_pressContent);
    const bool toggle = e->modifiers() & Qt::ControlModifier, add = e->modifiers() & Qt::ShiftModifier;
    if (hit.isEmpty()) {
        m_drag = Drag::Rubber;
        m_rubber = QRectF(m_pressContent, m_pressContent);
        if (!toggle && !add) m_doc->setSelection({});
        return;
    }
    QStringList sel = m_doc->selection();
    if (toggle) {
        if (sel.contains(hit)) sel.removeAll(hit);
        else sel << hit;
        m_doc->setSelection(sel);
        m_drag = Drag::None;
        return;
    }
    if (!sel.contains(hit) && !add) sel.clear();
    sel.removeAll(hit);   // (re)appended: the clicked sprite becomes the current one
    sel << hit;
    m_doc->setSelection(sel);
    // Everything selected on this page that owns a place in the atlas moves together.
    m_dragNames.clear();
    const BuildSnapshotPtr snap = m_doc->snapshot();
    for (const QString& n : sel)
        if (const atlas::Region* r = snap->region(n); r && r->page == m_page && isMovable(*r)) m_dragNames << n;
    m_drag = m_dragNames.contains(hit) ? Drag::Pending : Drag::None;
}

void AtlasCanvas::mouseMoveEvent(QMouseEvent* e)
{
    if (panMove(e)) return;
    const QPointF c = toContent(e->position());
    switch (m_drag) {
    case Drag::Pending:
        if ((e->position() - m_pressWidget).manhattanLength() < QApplication::startDragDistance()) break;
        m_drag = Drag::Move;
        [[fallthrough]];
    case Drag::Move: {
        // Whole pixels, kept inside the page for the sprite under the cursor.
        QPoint d(int(std::lround(c.x() - m_pressContent.x())), int(std::lround(c.y() - m_pressContent.y())));
        const BuildSnapshotPtr snap = m_doc->snapshot();
        const QSize page = contentSize();
        for (const QString& n : m_dragNames) {
            const atlas::Region* r = snap->region(n);
            d.setX(std::clamp(d.x(), -r->frame.x, page.width() - r->frame.right()));
            d.setY(std::clamp(d.y(), -r->frame.y, page.height() - r->frame.bottom()));
        }
        m_moveDelta = d;
        update();
        break;
    }
    case Drag::Rubber: {
        m_rubber.setBottomRight(c);
        const QRectF area = m_rubber.normalized();
        QStringList sel = (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) ? m_selectionAtPress : QStringList();
        const BuildSnapshotPtr snap = m_doc->snapshot();
        for (const atlas::Region& r : snap ? snap->result.regions : kNoRegions) {
            if (!isVisible(r)) continue;
            // Children only when wholly inside: dragging across a sheet should pick the sheets.
            const bool take = r.child && !r.baked ? area.contains(frameRect(r)) : area.intersects(frameRect(r));
            if (take && !sel.contains(qs(r.name))) sel << qs(r.name);
        }
        m_doc->setSelection(sel);
        update();
        break;
    }
    case Drag::None: {
        const QString hover = hitTest(c);
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
        break;
    }
    }
}

void AtlasCanvas::mouseReleaseEvent(QMouseEvent* e)
{
    if (panRelease(e)) return;
    if (e->button() != Qt::LeftButton) return;
    if (m_drag == Drag::Move && !m_moveDelta.isNull()) commitMove();
    m_drag = Drag::None;
    m_moveDelta = QPoint();
    update();
}

void AtlasCanvas::commitMove()
{
    const BuildSnapshotPtr snap = m_doc->snapshot();
    const QPoint d = m_moveDelta;
    const int page = m_page;
    m_doc->editSprites(m_dragNames, m_dragNames.size() == 1 ? tr("Move %1").arg(m_dragNames.first()) : tr("Move sprites"),
                       [&](atlas::SpriteDef& def) {
                           const atlas::Region* r = snap->region(QString::fromStdString(def.name));
                           if (!r) return;
                           def.pinned = true;
                           def.pinPage = page;
                           def.pinX = r->frame.x + d.x();
                           def.pinY = r->frame.y + d.y();
                       });
}

void AtlasCanvas::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton) return;
    const QString hit = hitTest(toContent(e->position()));
    if (!hit.isEmpty()) emit editSpriteRequested(hit);
}

void AtlasCanvas::contextMenuEvent(QContextMenuEvent* e)
{
    const QString hit = hitTest(toContent(e->pos()));
    if (!hit.isEmpty() && !m_doc->selection().contains(hit)) m_doc->setSelection({hit});
    emit contextMenuRequested(e->globalPos());
}

void AtlasCanvas::leaveEvent(QEvent* e)
{
    m_hover.clear();
    update();
    CanvasView::leaveEvent(e);
}

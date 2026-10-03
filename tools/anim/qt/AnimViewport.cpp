#include "AnimViewport.h"

#include "Theme.h"
#include "anim_player.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

using namespace animed;
using toms::anim::Node;
using toms::anim::NodePose;

namespace {

const char* kSpriteMime = "application/x-toms-sprite";
constexpr double kPi = 3.14159265358979323846;
constexpr double kArrowLen = 70, kRotateRadius = 58, kScaleLen = 62, kHandle = 6;

QPointF unit(const QPointF& v)
{
    const double l = std::hypot(v.x(), v.y());
    return l > 1e-9 ? v / l : QPointF(1, 0);
}

double dot(const QPointF& a, const QPointF& b) { return a.x() * b.x() + a.y() * b.y(); }

double distToSegment(const QPointF& p, const QPointF& a, const QPointF& b)
{
    const QPointF ab = b - a;
    const double len2 = dot(ab, ab);
    const double t = len2 > 0 ? std::clamp(dot(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    const QPointF c = a + ab * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

QTransform toQt(const glm::mat3& m) { return QTransform(m[0][0], m[0][1], m[1][0], m[1][1], m[2][0], m[2][1]); }

QPolygonF corners(const Quad& q)
{
    return QPolygonF({QPointF(q.corners[0], q.corners[1]), QPointF(q.corners[2], q.corners[3]),
                      QPointF(q.corners[4], q.corners[5]), QPointF(q.corners[6], q.corners[7])});
}

// Point in a convex quad, either winding (mirrored sprites wind the other way).
bool inQuad(const QPolygonF& c, const QPointF& p)
{
    int sign = 0;
    for (int i = 0; i < 4; i++) {
        const QPointF a = c[i], b = c[(i + 1) % 4];
        const double cross = (b.x() - a.x()) * (p.y() - a.y()) - (b.y() - a.y()) * (p.x() - a.x());
        if (std::fabs(cross) < 1e-9) continue;
        const int s = cross > 0 ? 1 : -1;
        if (sign == 0) sign = s;
        else if (s != sign) return false;
    }
    return sign != 0;
}

void drawArrowHead(QPainter& p, const QPointF& tip, const QPointF& dir, double size)
{
    const QPointF n(-dir.y(), dir.x());
    p.drawPolygon(QPolygonF({tip, tip - dir * size + n * size * 0.5, tip - dir * size - n * size * 0.5}));
}

}  // namespace

AnimViewport::AnimViewport(AnimDocument* doc, QWidget* parent)
    : CanvasView(parent)
    , m_doc(doc)
{
    setAcceptDrops(true);
    connect(doc, &AnimDocument::fileChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::timeChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::fileReset, this, &AnimViewport::frameClip);
    connect(doc, &AnimDocument::clipChanged, this, &AnimViewport::frameClip);
    connect(doc, &AnimDocument::atlasChanged, this, &AnimViewport::frameClip);
}

void AnimViewport::setTool(Tool t)
{
    if (t == m_tool) return;
    m_tool = t;
    update();
    emit toolChanged(t);
}

void AnimViewport::setShowGrid(bool on)
{
    m_grid = on;
    update();
}

QSize AnimViewport::contentSize() const { return m_stage.size().toSize(); }

QTransform AnimViewport::quadTransform(const Quad& q, const QSizeF& src)
{
    const double sw = std::max(1e-9, src.width()), sh = std::max(1e-9, src.height());
    const QPointF c0(q.corners[0], q.corners[1]), c1(q.corners[2], q.corners[3]), c3(q.corners[6], q.corners[7]);
    return QTransform((c1.x() - c0.x()) / sw, (c1.y() - c0.y()) / sw, (c3.x() - c0.x()) / sh, (c3.y() - c0.y()) / sh, c0.x(), c0.y());
}

void AnimViewport::buildFrame(float t, std::vector<NodePose>& poses, std::vector<DrawItem>& items,
                              std::vector<std::string>* missing) const
{
    poses.clear();
    items.clear();
    const toms::anim::Clip* clip = m_doc->clip();
    if (!clip) return;
    toms::anim::evaluate(*clip, t, poses);
    const glm::mat3 place = toms::anim::placement(0, 0);
    // The game's lookup (AtlasSet, first atlas that has the name wins); each quad's texture is a
    // per-(atlas, page) id of the document's, which maps back to the page image it is cut from.
    const toms::anim::AtlasSet& atlases = m_doc->atlasSet();
    std::vector<NodePose> one(1);
    std::vector<Quad> quads;
    for (const NodePose& pose : poses) {
        one[0] = pose;
        quads.clear();
        toms::anim::appendQuads(one, atlases, place, nullptr, quads, missing);
        for (const Quad& q : quads) {
            DrawItem it;
            it.quad = q;
            findPath(clip->root, pose.node, it.path);
            it.images = m_doc->pageImages(q.texture, &it.page);
            if (it.images && it.page >= 0 && it.page < it.images->pages().size()) {
                const QSize ps = it.images->pages()[it.page].size();
                const int x0 = int(std::lround(q.uv[0] * ps.width())), y0 = int(std::lround(q.uv[1] * ps.height()));
                const int x1 = int(std::lround(q.uv[2] * ps.width())), y1 = int(std::lround(q.uv[3] * ps.height()));
                it.src = QRect(x0, y0, x1 - x0, y1 - y0);
            }
            items.push_back(it);
        }
    }
}

void AnimViewport::rebuild()
{
    std::vector<std::string> missing;
    buildFrame(m_doc->time(), m_poses, m_items, &missing);
    m_missing.clear();
    for (const std::string& s : missing) m_missing << QString::fromStdString(s);
}

void AnimViewport::updateStage()
{
    QRectF bounds(-64, -64, 128, 128);
    const float d = m_doc->duration();
    std::vector<NodePose> poses;
    std::vector<DrawItem> items;
    for (int i = 0; i <= 16; i++) {
        buildFrame(d * float(i) / 16.0f, poses, items, nullptr);
        for (const DrawItem& it : items) bounds |= corners(it.quad).boundingRect();
        if (d <= 0) break;
    }
    const double g = 16;
    bounds = QRectF(QPointF(std::floor(bounds.left() / g) * g - g, std::floor(bounds.top() / g) * g - g),
                    QPointF(std::ceil(bounds.right() / g) * g + g, std::ceil(bounds.bottom() / g) * g + g));
    m_stage = bounds;
}

void AnimViewport::frameClip()
{
    updateStage();
    requestFit();
    fitToView();
}

// ---- painting -----------------------------------------------------------------------------------

void AnimViewport::paintContent(QPainter& p)
{
    rebuild();
    const Theme::Colors& tc = Theme::colors();
    const QTransform view = viewTransform();

    if (m_grid && zoom() * 16 >= 6) {
        const QPointF a = toContent(QPointF(0, 0)), b = toContent(QPointF(width(), height()));
        p.setPen(QPen(tc.grid, 1));
        for (double x = std::floor(a.x() / 16) * 16; x <= b.x(); x += 16) {
            const double wx = std::round(toWidget(QPointF(x, 0)).x()) + 0.5;
            p.drawLine(QPointF(wx, 0), QPointF(wx, height()));
        }
        for (double y = std::floor(a.y() / 16) * 16; y <= b.y(); y += 16) {
            const double wy = std::round(toWidget(QPointF(0, y)).y()) + 0.5;
            p.drawLine(QPointF(0, wy), QPointF(width(), wy));
        }
    }
    {   // origin cross: where the game places the clip
        const QPointF o = toWidget(QPointF(0, 0));
        p.setPen(QPen(tc.gridAxis, 1));
        p.drawLine(QPointF(std::round(o.x()) + 0.5, 0), QPointF(std::round(o.x()) + 0.5, height()));
        p.drawLine(QPointF(0, std::round(o.y()) + 0.5), QPointF(width(), std::round(o.y()) + 0.5));
    }

    // The sprites: each quad is an affine image of its pixel rect, like the game's vertices.
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.setRenderHint(QPainter::Antialiasing, false);
    for (const DrawItem& it : m_items) {
        if (it.src.isEmpty() || !it.images) continue;
        const QImage img = it.images->tinted(it.page, it.src, it.quad.tint);
        if (img.isNull()) continue;
        p.setTransform(quadTransform(it.quad, it.src.size()) * view);
        // Additive = the game's SRC_ALPHA, ONE: premultiplied source plus destination.
        p.setCompositionMode(it.quad.additive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
        p.drawImage(QPointF(0, 0), img);
    }
    p.restore();

    p.setRenderHint(QPainter::Antialiasing, true);
    const NodePath& sel = m_doc->selectedPath();
    if (m_hover >= 0 && m_hover < int(m_items.size()) && m_items[size_t(m_hover)].path != sel && m_drag == Drag::None) {
        p.setPen(QPen(tc.hover.lighter(300), 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(view.map(corners(m_items[size_t(m_hover)].quad)));
    }
    for (const DrawItem& it : m_items)
        if (it.path == sel) {
            p.setPen(QPen(tc.selection, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(view.map(corners(it.quad)));
        }
    drawGizmo(p, gizmo());

    // Status text over the view.
    p.setRenderHint(QPainter::Antialiasing, false);
    const toms::anim::Clip* clip = m_doc->clip();
    QString info = clip ? QStringLiteral("%1   t = %2 s / %3 s").arg(QString::fromStdString(clip->name))
                                  .arg(m_doc->time(), 0, 'f', 3).arg(clip->duration(), 0, 'f', 3)
                        : tr("no clip");
    if (m_doc->atlasCount() == 0) info += tr("   (no atlas: add one under Properties > Atlases)");
    else if (!m_doc->atlasLoaded()) info += tr("   (an atlas did not load: see Problems)");
    drawLabel(p, QPointF(8, 8), info);
    if (!m_missing.isEmpty()) drawLabel(p, QPointF(8, 32), tr("missing sprites: %1").arg(m_missing.join(QStringLiteral(", "))));
}

AnimViewport::Gizmo AnimViewport::gizmo() const
{
    Gizmo g;
    const toms::anim::Clip* clip = m_doc->clip();
    const Node* sel = m_doc->selectedNode();
    if (!clip || !sel) return g;
    const NodePose* me = nullptr;
    const NodePose* parent = nullptr;
    const NodePath& path = m_doc->selectedPath();
    const Node* parentNode = path.empty() ? nullptr : nodeAt(clip->root, NodePath(path.begin(), path.end() - 1));
    for (const NodePose& pose : m_poses) {
        if (pose.node == sel) me = &pose;
        if (parentNode && pose.node == parentNode) parent = &pose;
    }
    if (!me) return g;
    const glm::mat3 pw = parent ? parent->world : glm::mat3(1.0f);
    g.valid = true;
    g.origin = toWidget(QPointF(me->world[2][0], me->world[2][1]));
    g.parentX = unit(QPointF(pw[0][0], pw[0][1]));
    g.parentY = unit(QPointF(pw[1][0], pw[1][1]));
    g.nodeX = unit(QPointF(me->world[0][0], me->world[0][1]));
    g.nodeY = unit(QPointF(me->world[1][0], me->world[1][1]));
    g.parentDet = double(pw[0][0]) * pw[1][1] - double(pw[1][0]) * pw[0][1];
    g.parentToContent = toQt(pw);
    return g;
}

void AnimViewport::drawGizmo(QPainter& p, const Gizmo& g) const
{
    if (!g.valid) return;
    const Theme::Colors& tc = Theme::colors();
    const QPointF o = g.origin;
    // The pivot: the node's origin, which its sprite is drawn around.
    p.setPen(QPen(tc.pivot, 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(o, 4.5, 4.5);
    p.drawLine(o - QPointF(8, 0), o + QPointF(8, 0));
    p.drawLine(o - QPointF(0, 8), o + QPointF(0, 8));
    const bool dragging = m_drag != Drag::None;
    auto pen = [&](const QColor& c, bool active) { return QPen(active ? c.lighter(140) : c, active ? 3 : 2); };
    switch (m_tool) {
    case Tool::Move: {
        const QPointF ex = o + g.parentX * kArrowLen, ey = o + g.parentY * kArrowLen;
        p.setPen(pen(tc.gizmoX, m_drag == Drag::MoveX));
        p.setBrush(tc.gizmoX);
        p.drawLine(o + g.parentX * 12, ex);
        drawArrowHead(p, ex + g.parentX * 4, g.parentX, 12);
        p.setPen(pen(tc.gizmoY, m_drag == Drag::MoveY));
        p.setBrush(tc.gizmoY);
        p.drawLine(o + g.parentY * 12, ey);
        drawArrowHead(p, ey + g.parentY * 4, g.parentY, 12);
        QColor fill = tc.selection;
        fill.setAlpha(m_drag == Drag::MoveFree ? 160 : 70);
        p.setPen(QPen(tc.selection, 1.5));
        p.setBrush(fill);
        p.drawRect(QRectF(o - QPointF(7, 7), QSizeF(14, 14)));
        break;
    }
    case Tool::Rotate:
        p.setPen(pen(tc.gizmoRotate, m_drag == Drag::Rotate));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(o, kRotateRadius, kRotateRadius);
        p.drawLine(o, o + g.nodeX * kRotateRadius);
        if (dragging) drawLabel(p, o + QPointF(kRotateRadius + 6, -10), QStringLiteral("%1°").arg(m_angleSum, 0, 'f', 1));
        break;
    case Tool::Scale: {
        const QPointF ex = o + g.nodeX * kScaleLen, ey = o + g.nodeY * kScaleLen;
        p.setPen(pen(tc.gizmoX, m_drag == Drag::ScaleX));
        p.setBrush(tc.gizmoX);
        p.drawLine(o, ex);
        p.drawRect(QRectF(ex - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle)));
        p.setPen(pen(tc.gizmoY, m_drag == Drag::ScaleY));
        p.setBrush(tc.gizmoY);
        p.drawLine(o, ey);
        p.drawRect(QRectF(ey - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle)));
        QColor fill = tc.selection;
        fill.setAlpha(m_drag == Drag::ScaleUniform ? 160 : 70);
        p.setPen(QPen(tc.selection, 1.5));
        p.setBrush(fill);
        p.drawRect(QRectF(o - QPointF(7, 7), QSizeF(14, 14)));
        break;
    }
    }
}

// ---- interaction --------------------------------------------------------------------------------

AnimViewport::Drag AnimViewport::handleAt(const QPointF& w, const Gizmo& g) const
{
    if (!g.valid) return Drag::None;
    const QPointF o = g.origin;
    const auto within = [&](const QPointF& c, double r) { return std::hypot(w.x() - c.x(), w.y() - c.y()) <= r; };
    switch (m_tool) {
    case Tool::Move:
        if (within(o, 9)) return Drag::MoveFree;
        if (distToSegment(w, o + g.parentX * 12, o + g.parentX * (kArrowLen + 4)) < 6) return Drag::MoveX;
        if (distToSegment(w, o + g.parentY * 12, o + g.parentY * (kArrowLen + 4)) < 6) return Drag::MoveY;
        break;
    case Tool::Rotate: {
        const double d = std::hypot(w.x() - o.x(), w.y() - o.y());
        if (std::fabs(d - kRotateRadius) < 7) return Drag::Rotate;
        break;
    }
    case Tool::Scale:
        if (within(o + g.nodeX * kScaleLen, kHandle + 3)) return Drag::ScaleX;
        if (within(o + g.nodeY * kScaleLen, kHandle + 3)) return Drag::ScaleY;
        if (within(o, 9)) return Drag::ScaleUniform;
        break;
    }
    return Drag::None;
}

int AnimViewport::itemAt(const QPointF& w) const
{
    const QPointF c = toContent(w);
    for (int i = int(m_items.size()) - 1; i >= 0; i--)
        if (inQuad(corners(m_items[size_t(i)].quad), c)) return i;
    return -1;
}

QPointF AnimViewport::parentPointAt(const NodePath& parent, const QPointF& contentPos) const
{
    const toms::anim::Clip* clip = m_doc->clip();
    if (!clip) return contentPos;
    const Node* pn = nodeAt(clip->root, parent);
    for (const NodePose& pose : m_poses)
        if (pose.node == pn) {
            bool ok = false;
            const QTransform inv = toQt(pose.world).inverted(&ok);
            return ok ? inv.map(contentPos) : contentPos;
        }
    return contentPos;
}

void AnimViewport::mousePressEvent(QMouseEvent* e)
{
    if (panPress(e)) return;
    if (e->button() != Qt::LeftButton) return;
    setFocus();
    rebuild();
    Gizmo g = gizmo();
    Drag d = handleAt(e->position(), g);
    if (d == Drag::None) {
        const int hit = itemAt(e->position());
        if (hit < 0) return;
        m_doc->selectNode(m_items[size_t(hit)].path);
        if (m_tool != Tool::Move) return;
        rebuild();
        g = gizmo();
        d = Drag::MoveFree;   // drag a sprite to move it
    }
    const Node* sel = m_doc->selectedNode();
    if (!sel || !g.valid) return;
    m_drag = d;
    m_dragStart = e->position();
    m_dragGizmo = g;
    const float t = m_doc->time();
    m_startPos = valueAt(*sel, Channel::Pos, t);
    m_startRot = valueAt(*sel, Channel::Rot, t);
    m_startScale = valueAt(*sel, Channel::Scale, t);
    m_lastAngle = std::atan2(m_dragStart.y() - g.origin.y(), m_dragStart.x() - g.origin.x()) * 180.0 / kPi;
    m_angleSum = 0;
    m_dragSerial++;
    update();
}

void AnimViewport::mouseMoveEvent(QMouseEvent* e)
{
    if (panMove(e)) return;
    if (m_drag != Drag::None) {
        applyDrag(e->position(), e->modifiers());
        return;
    }
    rebuild();
    const int hover = itemAt(e->position());
    const Drag h = handleAt(e->position(), gizmo());
    setCursor(h != Drag::None ? Qt::PointingHandCursor : hover >= 0 && m_tool == Tool::Move ? Qt::SizeAllCursor : Qt::ArrowCursor);
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void AnimViewport::mouseReleaseEvent(QMouseEvent* e)
{
    if (panRelease(e)) return;
    if (e->button() == Qt::LeftButton && m_drag != Drag::None) {
        m_drag = Drag::None;
        update();
    }
}

void AnimViewport::applyDrag(const QPointF& w, Qt::KeyboardModifiers mods)
{
    const Gizmo& g = m_dragGizmo;
    const QString key = QStringLiteral("#drag%1").arg(m_dragSerial);
    switch (m_drag) {
    case Drag::MoveFree:
    case Drag::MoveX:
    case Drag::MoveY: {
        bool ok = false;
        const QTransform inv = g.parentToContent.inverted(&ok);
        if (!ok) return;
        QPointF delta = inv.map(toContent(w)) - inv.map(toContent(m_dragStart));
        if (m_drag == Drag::MoveX) delta.setY(0);
        if (m_drag == Drag::MoveY) delta.setX(0);
        Value v = m_startPos;
        v.v.x += float(delta.x());
        v.v.y += float(delta.y());
        if (m_snap || (mods & Qt::ControlModifier)) {
            v.v.x = std::round(v.v.x);
            v.v.y = std::round(v.v.y);
        }
        m_doc->setChannel(Channel::Pos, v, key);
        break;
    }
    case Drag::Rotate: {
        const double a = std::atan2(w.y() - g.origin.y(), w.x() - g.origin.x()) * 180.0 / kPi;
        double step = a - m_lastAngle;
        while (step > 180) step -= 360;
        while (step < -180) step += 360;
        m_angleSum += step;
        m_lastAngle = a;
        double delta = g.parentDet < 0 ? -m_angleSum : m_angleSum;   // a mirroring parent turns the other way
        if (mods & Qt::ShiftModifier) delta = std::round(delta / 15) * 15;
        else if (m_snap) delta = std::round(delta);
        Value v = m_startRot;
        v.v.x += float(delta);
        m_doc->setChannel(Channel::Rot, v, key);
        update();
        break;
    }
    case Drag::ScaleX:
    case Drag::ScaleY:
    case Drag::ScaleUniform: {
        Value v = m_startScale;
        if (m_drag == Drag::ScaleUniform) {
            const double f = std::exp(((w.x() - m_dragStart.x()) - (w.y() - m_dragStart.y())) / 150.0);
            v.v.x *= float(f);
            v.v.y *= float(f);
        } else {
            const QPointF u = m_drag == Drag::ScaleX ? g.nodeX : g.nodeY;
            double p0 = dot(m_dragStart - g.origin, u);
            if (std::fabs(p0) < 4) p0 = p0 < 0 ? -4 : 4;
            const float f = float(dot(w - g.origin, u) / p0);
            if (m_drag == Drag::ScaleX) v.v.x *= f;
            else v.v.y *= f;
            if (mods & Qt::ShiftModifier) {   // Shift: keep the proportions
                if (m_drag == Drag::ScaleX) v.v.y = m_startScale.v.y * f;
                else v.v.x = m_startScale.v.x * f;
            }
        }
        if (m_snap) {
            v.v.x = std::round(v.v.x * 20) / 20;
            v.v.y = std::round(v.v.y * 20) / 20;
        }
        m_doc->setChannel(Channel::Scale, v, key);
        break;
    }
    case Drag::None: break;
    }
}

void AnimViewport::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasFormat(QLatin1String(kSpriteMime))) e->acceptProposedAction();
}

void AnimViewport::dragMoveEvent(QDragMoveEvent* e)
{
    if (e->mimeData()->hasFormat(QLatin1String(kSpriteMime))) e->acceptProposedAction();
}

void AnimViewport::dropEvent(QDropEvent* e)
{
    const QStringList names = QString::fromUtf8(e->mimeData()->data(QLatin1String(kSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (names.isEmpty() || !m_doc->clip()) return;
    rebuild();
    // Under the selected node, at the drop point in that node's space.
    const NodePath parent = m_doc->selectedNode() ? m_doc->selectedPath() : NodePath();
    QPointF local = parentPointAt(parent, toContent(e->position()));
    if (m_snap) local = QPointF(std::round(local.x()), std::round(local.y()));
    m_doc->addSpriteNode(parent, names.first(), glm::vec2(float(local.x()), float(local.y())));
    e->acceptProposedAction();
    setFocus();
}

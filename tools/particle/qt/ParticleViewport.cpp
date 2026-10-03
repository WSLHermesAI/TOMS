#include "ParticleViewport.h"

#include "ParticleDocument.h"
#include "ParticlePlayback.h"
#include "Theme.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

using toms::fx::Emitter;
using toms::fx::Shape;

namespace {

const char* kSpriteMime = "application/x-toms-sprite";
constexpr double kArrow = 70;      // widget pixels: direction arrow length
constexpr double kHandle = 6;      // widget pixels: handle radius

QStringList refsFrom(const QMimeData* m)
{
    if (!m || !m->hasFormat(QLatin1String(kSpriteMime))) return {};
    return QString::fromUtf8(m->data(QLatin1String(kSpriteMime))).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QPointF polar(double degrees, double length)
{
    const double r = degrees * M_PI / 180.0;
    return QPointF(std::cos(r) * length, std::sin(r) * length);
}

double angleOf(const QPointF& v) { return std::atan2(v.y(), v.x()) * 180.0 / M_PI; }

double wrap180(double a)
{
    while (a > 180) a -= 360;
    while (a < -180) a += 360;
    return a;
}

}  // namespace

ParticleViewport::ParticleViewport(ParticleDocument* doc, ParticlePlayback* playback, bool gameRenderer, QWidget* parent)
    : GameCanvasView(gameRenderer, parent)
    , m_doc(doc)
    , m_play(playback)
    , m_playGuard(playback)
{
    setAcceptDrops(true);
    connect(doc, &ParticleDocument::atlasChanged, this, [this] { m_atlasDirty = true; });
    connect(playback, &ParticlePlayback::ticked, this, qOverload<>(&QWidget::update));
    connect(doc, &ParticleDocument::selectionChanged, this, qOverload<>(&QWidget::update));
}

void ParticleViewport::setBackground(Background b)
{
    m_background = b;
    update();
}

bool ParticleViewport::setBackgroundImage(const QString& path)
{
    QImage img(path);
    if (img.isNull()) return false;
    m_bgImage = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    m_bgPath = path;
    m_background = Background::Image;
    update();
    return true;
}

int ParticleViewport::batches(const std::vector<Quad>& quads)
{
    int n = 0;
    for (size_t i = 0; i < quads.size(); i++)
        if (i == 0 || quads[i].texture != quads[i - 1].texture || quads[i].additive != quads[i - 1].additive) n++;
    return n;
}

void ParticleViewport::drawQuads(QPainter& p, const std::vector<Quad>& quads, const ParticleDocument& doc, const QTransform& view)
{
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);   // particles are soft: filter like the fx atlas (Linear)
    for (const Quad& q : quads) {
        int page = 0;
        const SpriteImageCache* images = doc.pageImages(q.texture, &page);
        if (!images || page < 0 || page >= images->pages().size()) continue;
        const QSize ps = images->pages()[page].size();
        const int x0 = int(std::lround(q.uv[0] * ps.width())), y0 = int(std::lround(q.uv[1] * ps.height()));
        const int x1 = int(std::lround(q.uv[2] * ps.width())), y1 = int(std::lround(q.uv[3] * ps.height()));
        const QRect src(x0, y0, x1 - x0, y1 - y0);
        if (src.isEmpty()) continue;
        const QImage img = images->tinted(page, src, q.tint);
        if (img.isNull()) continue;
        // Corners 0, 1, 3 span the parallelogram the sprite's pixel rect maps onto.
        const double sw = src.width(), sh = src.height();
        const QPointF c0(q.corners[0], q.corners[1]), c1(q.corners[2], q.corners[3]), c3(q.corners[6], q.corners[7]);
        const QTransform quad((c1.x() - c0.x()) / sw, (c1.y() - c0.y()) / sw, (c3.x() - c0.x()) / sh, (c3.y() - c0.y()) / sh, c0.x(), c0.y());
        p.setTransform(quad * view);
        // Additive = the game's SRC_ALPHA, ONE: the premultiplied tinted sprite plus the screen.
        p.setCompositionMode(q.additive ? QPainter::CompositionMode_Plus : QPainter::CompositionMode_SourceOver);
        p.drawImage(QPointF(0, 0), img);
    }
    p.restore();
}

QPointF ParticleViewport::emitterPos() const
{
    const Emitter* e = m_doc->emitter();
    return m_play->origin() + (e ? QPointF(e->offset.x, e->offset.y) : QPointF());
}

QPointF ParticleViewport::handlePos(Handle h) const
{
    const Emitter* e = m_doc->emitter();
    if (!e || !m_gizmo) return QPointF(qQNaN(), qQNaN());
    const QPointF c = toWidget(emitterPos());
    const Shape& s = e->shape;
    switch (h) {
    case Handle::Move: return c;
    case Handle::Direction: return s.outward ? QPointF(qQNaN(), qQNaN()) : c + polar(e->direction, kArrow);
    case Handle::SpreadA: return s.outward ? QPointF(qQNaN(), qQNaN()) : c + polar(e->direction - e->spread, kArrow * 0.7);
    case Handle::SpreadB: return s.outward ? QPointF(qQNaN(), qQNaN()) : c + polar(e->direction + e->spread, kArrow * 0.7);
    case Handle::Shape:
        switch (s.type) {
        case Shape::Point: return QPointF(qQNaN(), qQNaN());
        case Shape::Line: return toWidget(emitterPos() + polar(s.angle, s.length / 2));
        case Shape::Box: return toWidget(emitterPos() + QPointF(s.width / 2, s.height / 2));
        case Shape::Circle:
        case Shape::Ring: return toWidget(emitterPos() + QPointF(s.radius, 0));
        }
        break;
    case Handle::None: break;
    }
    return QPointF(qQNaN(), qQNaN());
}

ParticleViewport::Handle ParticleViewport::hitHandle(const QPointF& w) const
{
    // Small handles first: they sit on top of the move square.
    for (Handle h : {Handle::Shape, Handle::SpreadA, Handle::SpreadB, Handle::Direction, Handle::Move}) {
        const QPointF p = handlePos(h);
        if (!std::isnan(p.x()) && QLineF(p, w).length() <= kHandle + 3) return h;
    }
    return Handle::None;
}

void ParticleViewport::paintContent(QPainter& p)
{
    const Theme::Colors& tc = Theme::colors();
    const QTransform view = viewTransform();
    if (overGameScene()) {   // the game renderer drew the background, grid, origin and effect
        p.setRenderHint(QPainter::Antialiasing, true);
        if (m_gizmo) paintGizmo(p);
        drawLabel(p, QPointF(8, 8), statusText());
        drawLabel(p, QPointF(8, height() - 26),
                  tr("Click: fire here   Ctrl+drag: move it   Double-click: back to 0,0   Right-drag: pan   Wheel: zoom"));
        return;
    }
    // Background over the checkerboard the base drew.
    if (m_background == Background::Dark) p.fillRect(toWidget(contentRect()), QColor(24, 24, 30));
    else if (m_background == Background::Light) p.fillRect(toWidget(contentRect()), QColor(225, 225, 230));
    else if (m_background == Background::Image && !m_bgImage.isNull()) {
        p.save();
        p.setTransform(view);
        p.drawImage(QPointF(-m_bgImage.width() / 2.0, -m_bgImage.height() / 2.0), m_bgImage);
        p.restore();
    }
    if (m_grid && zoom() * 32 >= 8) {
        const QPointF a = toContent(QPointF(0, 0)), b = toContent(QPointF(width(), height()));
        p.setPen(QPen(tc.grid, 1));
        for (double x = std::floor(a.x() / 32) * 32; x <= b.x(); x += 32) {
            const double wx = std::round(toWidget(QPointF(x, 0)).x()) + 0.5;
            p.drawLine(QPointF(wx, 0), QPointF(wx, height()));
        }
        for (double y = std::floor(a.y() / 32) * 32; y <= b.y(); y += 32) {
            const double wy = std::round(toWidget(QPointF(0, y)).y()) + 0.5;
            p.drawLine(QPointF(0, wy), QPointF(width(), wy));
        }
    }
    {   // the effect's origin
        const QPointF o = toWidget(m_play->origin());
        p.setPen(QPen(tc.gridAxis, 1));
        p.drawLine(o + QPointF(-10, 0), o + QPointF(10, 0));
        p.drawLine(o + QPointF(0, -10), o + QPointF(0, 10));
    }

    m_quads.clear();
    m_play->instance().appendQuads(m_doc->atlasSet(), glm::mat3(1.0f), nullptr, m_quads);
    drawQuads(p, m_quads, *m_doc, view);

    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_gizmo) paintGizmo(p);

    // Status over the view.
    drawLabel(p, QPointF(8, 8), statusText());
    drawLabel(p, QPointF(8, height() - 26),
              tr("Click: fire here   Ctrl+drag: move it   Double-click: back to 0,0   Right-drag: pan   Wheel: zoom"));
}

QString ParticleViewport::statusText() const
{
    const toms::fx::EffectInstance& fx = m_play->instance();
    if (usingGameRenderer())
        return QStringLiteral("%1   FPS %2   t %3 s   live %4   GPU-simulated emitters %5 (above %6)   seed %7%8")
            .arg(rendererName())
            .arg(m_fps, 0, 'f', 1)
            .arg(fx.time(), 0, 'f', 2)
            .arg(fx.liveCount())
            .arg(fx.gpuEmitters())
            .arg(m_play->gpuThreshold())
            .arg(m_play->seed())
            .arg(!m_play->playing() ? tr("   (paused)") : fx.finished() ? tr("   (finished)") : QString());
    QString status = QStringLiteral("t %1 s   live %2   quads %3   batches %4   %5 ms   seed %6")
                         .arg(fx.time(), 0, 'f', 2)
                         .arg(fx.liveCount())
                         .arg(m_quads.size())
                         .arg(batches(m_quads))
                         .arg(m_play->lastStepMs(), 0, 'f', 2)
                         .arg(m_play->seed());
    if (!m_play->playing()) status += tr("   (paused)");
    else if (fx.finished()) status += tr("   (finished)");
    return status;
}

void ParticleViewport::paintGizmo(QPainter& p)
{
    const Emitter* e = m_doc->emitter();
    if (!e) return;
    const Theme::Colors& tc = Theme::colors();
    const QPointF c = toWidget(emitterPos());
    const Shape& s = e->shape;
    // The shape outline (content size, so it zooms).
    p.setPen(QPen(tc.selection, 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    const double z = zoom();
    switch (s.type) {
    case Shape::Point: break;
    case Shape::Line: {
        const QPointF h = polar(s.angle, s.length / 2 * z);
        p.drawLine(c - h, c + h);
        break;
    }
    case Shape::Box: p.drawRect(QRectF(c - QPointF(s.width / 2 * z, s.height / 2 * z), QSizeF(s.width * z, s.height * z))); break;
    case Shape::Circle:
    case Shape::Ring: {
        const double r = s.radius * z;
        if (s.arcTo - s.arcFrom >= 360) p.drawEllipse(c, r, r);
        else p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), int(-s.arcFrom * 16), int(-(s.arcTo - s.arcFrom) * 16));
        if (s.type == Shape::Ring && s.thickness > 0) {
            const double r2 = std::max(0.0, (s.radius - s.thickness) * z);
            p.drawEllipse(c, r2, r2);
        }
        break;
    }
    }
    // Direction and spread (not for "outward" shapes: they fly away from the centre).
    if (!s.outward) {
        {
            QPainterPath wedge;
            wedge.moveTo(c);
            wedge.arcTo(QRectF(c.x() - kArrow * 0.7, c.y() - kArrow * 0.7, kArrow * 1.4, kArrow * 1.4), -(e->direction - e->spread),
                        -2 * e->spread);
            wedge.closeSubpath();
            QColor fill = tc.selection;
            fill.setAlpha(40);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPath(wedge);
            p.setPen(QPen(tc.gizmoX, 2));
            const QPointF tip = c + polar(e->direction, kArrow);
            p.drawLine(c, tip);
            p.drawLine(tip, tip + polar(e->direction + 150, 10));
            p.drawLine(tip, tip + polar(e->direction - 150, 10));
        }
    }
    auto handle = [&](Handle h, const QColor& col, bool square) {
        const QPointF hp = handlePos(h);
        if (std::isnan(hp.x())) return;
        p.setPen(QPen(m_hover == h || m_drag == h ? Qt::white : Qt::black, 1));
        p.setBrush(col);
        if (square) p.drawRect(QRectF(hp - QPointF(kHandle, kHandle), QSizeF(2 * kHandle, 2 * kHandle)));
        else p.drawEllipse(hp, kHandle, kHandle);
    };
    handle(Handle::Shape, tc.gizmoRotate, false);
    handle(Handle::SpreadA, tc.selection, false);
    handle(Handle::SpreadB, tc.selection, false);
    handle(Handle::Direction, tc.gizmoX, false);
    handle(Handle::Move, tc.gizmoY, true);
    // What is being dragged.
    if (m_drag == Handle::Direction) drawLabel(p, c + QPointF(12, 12), tr("direction %1°").arg(e->direction, 0, 'f', 0));
    else if (m_drag == Handle::SpreadA || m_drag == Handle::SpreadB) drawLabel(p, c + QPointF(12, 12), tr("spread ±%1°").arg(e->spread, 0, 'f', 0));
    else if (m_drag == Handle::Move) drawLabel(p, c + QPointF(12, 12), tr("offset %1, %2").arg(e->offset.x, 0, 'f', 0).arg(e->offset.y, 0, 'f', 0));
}

void ParticleViewport::mousePressEvent(QMouseEvent* e)
{
    if (panPress(e)) return;
    if (e->button() != Qt::LeftButton) return;
    m_pressWidget = e->position();
    m_pressContent = toContent(e->position());
    m_startOrigin = m_play->origin();
    m_gesture++;
    if (e->modifiers() & Qt::ControlModifier) {
        m_movingOrigin = true;
        return;
    }
    m_drag = hitHandle(e->position());
    if (const Emitter* em = m_doc->emitter()) m_startOffset = QPointF(em->offset.x, em->offset.y);
    m_pressedEmpty = m_drag == Handle::None;
    update();
}

void ParticleViewport::dragTo(const QPointF& cp, Qt::KeyboardModifiers mods)
{
    const QString key = QStringLiteral("gizmo%1").arg(m_gesture);
    const QPointF ep = emitterPos();
    switch (m_drag) {
    case Handle::Move: {
        QPointF off = m_startOffset + (cp - m_pressContent);
        if (mods & Qt::ShiftModifier) off = QPointF(std::round(off.x() / 8) * 8, std::round(off.y() / 8) * 8);
        m_doc->editEmitter(tr("Move emitter"), [&](Emitter& m) { m.offset = glm::vec2(float(std::round(off.x())), float(std::round(off.y()))); }, key);
        break;
    }
    case Handle::Direction: {
        double a = angleOf(cp - ep);
        a = (mods & Qt::ShiftModifier) ? std::round(a / 15) * 15 : std::round(a);
        m_doc->editEmitter(tr("Direction"), [&](Emitter& m) { m.direction = float(a); }, key);
        break;
    }
    case Handle::SpreadA:
    case Handle::SpreadB: {
        const Emitter* em = m_doc->emitter();
        double sp = std::fabs(wrap180(angleOf(cp - ep) - em->direction));
        sp = (mods & Qt::ShiftModifier) ? std::round(sp / 15) * 15 : std::round(sp);
        m_doc->editEmitter(tr("Spread"), [&](Emitter& m) { m.spread = float(std::min(180.0, sp)); }, key);
        break;
    }
    case Handle::Shape: {
        const QPointF d = cp - ep;
        m_doc->editEmitter(tr("Shape size"), [&](Emitter& m) {
            switch (m.shape.type) {
            case Shape::Line:
                m.shape.length = float(std::round(2 * std::hypot(d.x(), d.y())));
                m.shape.angle = float(std::round(angleOf(d)));
                break;
            case Shape::Box:
                m.shape.width = float(std::round(2 * std::fabs(d.x())));
                m.shape.height = float(std::round(2 * std::fabs(d.y())));
                break;
            case Shape::Circle:
            case Shape::Ring: m.shape.radius = float(std::round(std::hypot(d.x(), d.y()))); break;
            case Shape::Point: break;
            }
        }, key);
        break;
    }
    case Handle::None: break;
    }
}

void ParticleViewport::mouseMoveEvent(QMouseEvent* e)
{
    if (panMove(e)) return;
    const QPointF cp = toContent(e->position());
    if (m_movingOrigin) {
        const QPointF o = m_startOrigin + (cp - m_pressContent);
        m_play->setOrigin(QPointF(std::round(o.x()), std::round(o.y())), false);
        return;
    }
    if (m_drag != Handle::None && (e->buttons() & Qt::LeftButton)) {
        dragTo(cp, e->modifiers());
        return;
    }
    const Handle h = hitHandle(e->position());
    if (h != m_hover) {
        m_hover = h;
        setCursor(h == Handle::None ? Qt::ArrowCursor : h == Handle::Move ? Qt::SizeAllCursor : Qt::PointingHandCursor);
        update();
    }
}

void ParticleViewport::mouseReleaseEvent(QMouseEvent* e)
{
    if (panRelease(e)) return;
    if (e->button() != Qt::LeftButton) return;
    const bool click = (e->position() - m_pressWidget).manhattanLength() < 4;
    if (m_pressedEmpty && click && !m_movingOrigin) {   // fire the effect here
        const QPointF cp = toContent(e->position());
        m_play->setOrigin(QPointF(std::round(cp.x()), std::round(cp.y())), true);
    }
    m_drag = Handle::None;
    m_movingOrigin = false;
    m_pressedEmpty = false;
    update();
}

void ParticleViewport::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && hitHandle(e->position()) == Handle::None) m_play->setOrigin(QPointF(0, 0), true);
}

void ParticleViewport::dragEnterEvent(QDragEnterEvent* e)
{
    if (refsFrom(e->mimeData()).isEmpty()) return;
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

void ParticleViewport::dragMoveEvent(QDragMoveEvent* e)
{
    if (refsFrom(e->mimeData()).isEmpty()) return;
    e->setDropAction(Qt::CopyAction);
    e->accept();
}

void ParticleViewport::dropEvent(QDropEvent* e)
{
    const QStringList refs = refsFrom(e->mimeData());
    if (refs.isEmpty()) return;
    e->setDropAction(Qt::CopyAction);
    e->accept();
    emit spritesDropped(refs, toContent(e->position()) - m_play->origin());
}

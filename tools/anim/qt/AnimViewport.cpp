#include "AnimViewport.h"

#include "bgfx_renderer.h"   // GameScene::ren (the game renderer)

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
// A node's 3D world as the editor's orthographic screen camera shows it (what the game draws).
QTransform toQt(const glm::mat4& m) { return toQt(toms::anim::flatten(m)); }

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

AnimViewport::AnimViewport(AnimDocument* doc, bool gameRenderer, QWidget* parent)
    : GameCanvasView(gameRenderer, parent)
    , m_doc(doc)
{
    setAcceptDrops(true);
    connect(doc, &AnimDocument::atlasChanged, this, [this] { m_atlasDirty = true; });
    connect(doc, &AnimDocument::fileChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::selectionChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::timeChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::previewTimeChanged, this, qOverload<>(&QWidget::update));
    connect(doc, &AnimDocument::soloChanged, this, qOverload<>(&QWidget::update));
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
    const toms::anim::Clip* clip = m_doc->drawClip();   // as a previewed character, when one is chosen
    if (!clip) return;
    toms::anim::evaluate(*clip, t, poses);
    const glm::mat3 place = toms::anim::placement(0, 0);
    // The game's lookup (AtlasSet, first atlas that has the name wins); each quad's texture is a
    // per-(atlas, page) id of the document's, which maps back to the page image it is cut from.
    const toms::anim::AtlasSet& atlases = m_doc->atlasSet();
    std::vector<NodePose> one(1);
    std::vector<Quad> quads;
    for (const NodePose& pose : poses) {
        if (!m_doc->soloShows(pose.node)) continue;   // solo: only that node's subtree
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
    buildFrame(m_doc->previewTime(), m_poses, m_items, &missing);
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
    rebuild();
    requestFit();
    fitToView();
    update();
}

// ---- painting -----------------------------------------------------------------------------------

AnimViewport::~AnimViewport() { releaseGameRenderer(); }

void AnimViewport::gameRendererChanged(toms::next::BgfxRenderer* ren)
{
    m_tex.attach(ren);
    m_atlasDirty = true;
}

QImage AnimViewport::renderClipFrame(const toms::anim::Clip& clip, const toms::anim::Clip* held, float t, int w, int h,
                                     const glm::mat3& place, const QColor& background)
{
    return renderOffscreen(w, h, [&](const GameScene& s) {
        prepareAtlases();
        if (background.alpha() > 0) s.rect(QRectF(0, 0, w, h), background);
        std::vector<NodePose> poses;
        toms::anim::evaluate(clip, t, poses);
        toms::anim::DrawList draws;
        toms::anim::appendDraws(clip, poses, m_tex.atlasSet(), place, nullptr, draws);
        glm::mat3 hand;
        if (held && toms::anim::attachPlacement(poses, "hand", place, hand)) {
            std::vector<NodePose> heldPoses;
            toms::anim::evaluate(*held, std::min(t, held->duration()), heldPoses);
            toms::anim::appendDraws(*held, heldPoses, m_tex.atlasSet(), hand, nullptr, draws);
        }
        draws.submit(&s.ren);
    });
}

void AnimViewport::prepareAtlases()
{
    if (m_atlasDirty) {
        m_atlasDirty = false;
        std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>> atlases;
        for (int i = 0; i < m_doc->atlasCount(); i++)
            if (m_doc->atlasAt(i).ok)
                atlases.emplace_back(m_doc->atlasId(i).toStdString(), m_doc->atlasAt(i).path.toStdString(), &m_doc->atlasAt(i).atlas);
        m_tex.setAtlases(atlases);
    }
    for (int i = 0; i < m_doc->atlasCount(); i++)   // skinned and deformed nodes bend these (docs/engine/16_MESH_ANIMATION.md)
        if (m_doc->atlasAt(i).ok)
            m_tex.setMeshes(m_doc->atlasId(i).toStdString(), m_doc->atlasMeshes(i).meshes.empty() ? nullptr : &m_doc->atlasMeshes(i));
}

void AnimViewport::paintGameScene(const GameScene& s)
{
    const Theme::Colors& tc = Theme::colors();
    prepareAtlases();
    const QPointF o = toWidget(QPointF(0, 0));   // origin cross: where the game places the clip
    s.line(QPointF(std::round(o.x()) + 0.5, 0), QPointF(std::round(o.x()) + 0.5, height()), 1, tc.gridAxis);
    s.line(QPointF(0, std::round(o.y()) + 0.5), QPointF(width(), std::round(o.y()) + 0.5), 1, tc.gridAxis);
    // The clip, exactly as the game draws it (Game::drawPreviewAnim): evaluate + appendDraws (skinned nodes bent).
    const toms::anim::Clip* clip = m_doc->drawClip();
    if (!clip) return;
    std::vector<NodePose> poses;
    toms::anim::evaluate(*clip, m_doc->previewTime(), poses);
    if (m_doc->solo())   // only the solo node's subtree
        poses.erase(std::remove_if(poses.begin(), poses.end(), [this](const NodePose& p) { return !m_doc->soloShows(p.node); }),
                    poses.end());
    toms::anim::DrawList draws;
    const glm::mat3 place = GameScene::placement(viewTransform());
    toms::anim::appendDraws(*clip, poses, m_tex.atlasSet(), place, nullptr, draws);
    // View > Held Clip: the weapon's clip of the same name at the `hand` node, in front (as CharacterRig draws it)
    glm::mat3 hand;
    if (const toms::anim::Clip* held = m_doc->heldClip(); held && toms::anim::attachPlacement(poses, "hand", place, hand)) {
        std::vector<NodePose> heldPoses;
        toms::anim::evaluate(*held, std::min(m_doc->previewTime(), held->duration()), heldPoses);
        toms::anim::appendDraws(*held, heldPoses, m_tex.atlasSet(), hand, nullptr, draws);
    }
    draws.submit(&s.ren);
}

void AnimViewport::paintContent(QPainter& p)
{
    rebuild();
    const Theme::Colors& tc = Theme::colors();
    const QTransform view = viewTransform();
    const bool over = overGameScene();   // the game renderer drew the origin and sprites

    if (!over) {   // origin cross: where the game places the clip
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
        if (over || it.src.isEmpty() || !it.images) continue;
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
    drawSkins(p, view);
    drawLattice(p, view);
    if (m_mode != EditMode::Weights) drawGizmo(p, gizmo());   // Weights mode: the bones are painted, not moved
    drawBrush(p);

    // Status text over the view.
    p.setRenderHint(QPainter::Antialiasing, false);
    const toms::anim::Clip* clip = m_doc->clip();
    QString info = clip ? QStringLiteral("%1   t = %2 s / %3 s").arg(QString::fromStdString(clip->name))
                                  .arg(m_doc->time(), 0, 'f', 3).arg(clip->duration(), 0, 'f', 3)
                        : tr("no clip");
    if (clip && m_doc->solo())
        if (const Node* sn = nodeAt(clip->root, *m_doc->solo()))
            info += tr("   SOLO: %1").arg(QString::fromStdString(sn->name));
    if (clip && m_doc->previewTime() > m_doc->time())
        info += tr("   (ended: looping nodes play on, +%1 s)").arg(m_doc->previewTime() - m_doc->time(), 0, 'f', 1);
    if (m_mode == EditMode::Bones) info += tr("   BONES: click a bone (Auto-Key on: pose it; off: place it in the sprite)");
    if (m_mode == EditMode::Lattice) info += tr("   LATTICE: drag a point to key the deform at the playhead");
    if (m_mode == EditMode::Weights) {
        const Node* sel = m_doc->selectedNode();
        info += sel ? tr("   WEIGHTS of %1: drag to add, Ctrl subtract, Shift smooth; [ ] brush size; click a joint for another bone")
                          .arg(QString::fromStdString(sel->name))
                    : tr("   WEIGHTS: click a bone's joint to paint its weights");
    }
    if (!m_doc->previewCharacter().isEmpty()) info += tr("   AS: %1").arg(m_doc->previewCharacter());
    if (m_doc->atlasCount() == 0) info += tr("   (no atlas: add one under Properties > Atlases)");
    else if (!m_doc->atlasLoaded()) info += tr("   (an atlas did not load: see Problems)");
    drawLabel(p, QPointF(8, 8), info);
    if (!m_missing.isEmpty()) drawLabel(p, QPointF(8, 32), tr("missing sprites: %1").arg(m_missing.join(QStringLiteral(", "))));
    if (m_mode == EditMode::Weights) {
        const QString under = weightsUnderCursor();
        if (!under.isEmpty()) drawLabel(p, QPointF(8, m_missing.isEmpty() ? 32 : 56), under);
    }
}

void AnimViewport::setEditMode(EditMode m)
{
    if (m == m_mode) return;
    m_mode = m;
    m_hover = -1;
    m_hoverBone.clear();
    m_hoverPoint = -1;
    m_painting = false;
    if (m != EditMode::Weights) unsetCursor();
    update();
}

bool AnimViewport::selectedLattice(toms::anim::LatticeView& out) const
{
    const Node* sel = m_doc->drawNode(m_doc->selectedNode());
    if (!sel || sel->sprite.empty()) return false;
    for (size_t i = 0; i < m_poses.size(); i++)
        if (m_poses[i].node == sel)
            return m_poses[i].visible && toms::anim::latticeView(m_poses, i, m_doc->atlasSet(), toms::anim::placement(0, 0), out);
    return false;
}

int AnimViewport::latticePointAt(const QPointF& w) const
{
    toms::anim::LatticeView lv;
    if (m_mode != EditMode::Lattice || !selectedLattice(lv)) return -1;
    const QTransform view = viewTransform();
    int best = -1;
    double bestD = 9.0;
    for (size_t i = 0; i < lv.points.size(); i++) {
        const QPointF pt = view.map(QPointF(lv.points[i].x, lv.points[i].y));
        const double d = std::hypot(w.x() - pt.x(), w.y() - pt.y());
        if (d < bestD) { bestD = d; best = int(i); }
    }
    return best;
}

void AnimViewport::drawLattice(QPainter& p, const QTransform& view) const
{
    toms::anim::LatticeView lv;
    if (m_mode != EditMode::Lattice || !selectedLattice(lv)) return;
    auto at = [&](int c, int r) { const glm::vec2& v = lv.points[size_t(r * lv.cols + c)]; return view.map(QPointF(v.x, v.y)); };
    p.setPen(QPen(QColor(255, 210, 90, 170), 1));
    for (int r = 0; r < lv.rows; r++)
        for (int c = 0; c < lv.cols; c++) {
            if (c + 1 < lv.cols) p.drawLine(at(c, r), at(c + 1, r));
            if (r + 1 < lv.rows) p.drawLine(at(c, r), at(c, r + 1));
        }
    for (int r = 0; r < lv.rows; r++)
        for (int c = 0; c < lv.cols; c++) {
            const int i = r * lv.cols + c;
            const bool moved = lv.offsets[size_t(i)] != glm::vec2(0.0f);
            const bool hot = i == m_hoverPoint || i == m_dragPoint;
            p.setPen(QPen(hot ? Qt::white : QColor(0, 0, 0, 200), hot ? 2 : 1));
            p.setBrush(moved ? QColor(255, 140, 60) : QColor(255, 220, 110));
            p.drawRect(QRectF(at(c, r) - QPointF(4, 4), QSizeF(8, 8)));
        }
}

QString AnimViewport::boneAt(const QPointF& w, bool jointsOnly) const
{
    const toms::anim::Clip* clip = m_doc->drawClip();
    if (!clip) return QString();
    const glm::mat3 place = toms::anim::placement(0, 0);
    const QTransform view = viewTransform();
    QString best;
    double bestD = 9.0;   // widget pixels: a joint or a segment within this
    for (size_t i = 0; i < m_poses.size(); i++) {
        const NodePose& pose = m_poses[i];
        if (!pose.node || !pose.node->skinned() || !pose.visible || !m_doc->soloShows(pose.node)) continue;
        toms::anim::SkinView sv;
        if (toms::anim::skinView(*clip, m_poses, i, m_doc->atlasSet(), place, &sv) != toms::anim::SkinState::Ok) continue;
        for (size_t b = 0; b < sv.bones.size(); b++) {
            if (!sv.boneFound[b]) continue;
            const QPointF s = view.map(QPointF(sv.boneStart[b].x, sv.boneStart[b].y)), e = view.map(QPointF(sv.boneEnd[b].x, sv.boneEnd[b].y));
            // The joint wins over a segment passing by: it counts 3 pixels nearer.
            const double joint = std::hypot(w.x() - s.x(), w.y() - s.y()) - 3.0;
            const QPointF se = e - s;
            const double len2 = se.x() * se.x() + se.y() * se.y();
            const double t = len2 > 1e-9 ? std::clamp(((w.x() - s.x()) * se.x() + (w.y() - s.y()) * se.y()) / len2, 0.0, 1.0) : 0.0;
            const double seg = std::hypot(w.x() - (s.x() + t * se.x()), w.y() - (s.y() + t * se.y()));
            const double d = jointsOnly ? joint : std::min(joint, seg);
            if (d < bestD) {
                bestD = d;
                best = QString::fromStdString(sv.bones[b]);
            }
        }
    }
    return best;
}

QColor AnimViewport::boneColor(int b)
{
    static const QColor kPalette[] = {QColor(0xff, 0x5c, 0x5c), QColor(0x4f, 0xc3, 0xf7), QColor(0xff, 0xd5, 0x4f),
                                      QColor(0x81, 0xc7, 0x84), QColor(0xba, 0x68, 0xc8), QColor(0xff, 0x8a, 0x65),
                                      QColor(0x4d, 0xb6, 0xac), QColor(0xf0, 0x62, 0x92)};
    return kPalette[size_t(b < 0 ? 0 : b) % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

void AnimViewport::drawSkins(QPainter& p, const QTransform& view) const
{
    const toms::anim::Clip* clip = m_doc->drawClip();
    if ((!m_showSkin && m_mode != EditMode::Bones && m_mode != EditMode::Weights) || !clip) return;
    const bool heat = m_mode == EditMode::Weights;   // the selected bone's weights as colours (blue 0 .. red 1)
    const Node* sel = m_doc->selectedNode();
    const glm::mat3 place = toms::anim::placement(0, 0);   // content = clip space, as buildFrame
    auto at = [&](const glm::vec2& c) { return view.map(QPointF(c.x, c.y)); };
    for (size_t i = 0; i < m_poses.size(); i++) {
        const NodePose& pose = m_poses[i];
        // Hidden (itself, a parent, a visible key) or left out by solo: no mesh is drawn, so no overlay either.
        if (!pose.node || !pose.node->skinned() || !pose.visible || !m_doc->soloShows(pose.node)) continue;
        toms::anim::SkinView sv;
        if (toms::anim::skinView(*clip, m_poses, i, m_doc->atlasSet(), place, &sv) != toms::anim::SkinState::Ok) continue;
        const int bones = int(sv.bones.size());
        int selBone = -1;   // the selected node is one of this skin's bones: shade by its weight
        for (int b = 0; b < bones && sel; b++)
            if (sv.boneFound[size_t(b)] && sv.bones[size_t(b)] == sel->name) selBone = b;

        if (heat && selBone < 0) continue;   // Weights mode: only the skins the selected bone is in
        if (heat) {   // every triangle filled with its points' mean weight for the selected bone
            p.setPen(Qt::NoPen);
            for (size_t t = 0; t + 2 < sv.triangles.size(); t += 3) {
                double w = 0;
                for (int c = 0; c < 3; c++) w += animed::boneWeight(sv.weights, sv.triangles[t + size_t(c)], selBone);
                QColor col = weightColor(w / 3);
                col.setAlpha(150);
                p.setBrush(col);
                p.drawPolygon(QPolygonF({at(sv.vertices[sv.triangles[t]]), at(sv.vertices[sv.triangles[t + 1]]),
                                         at(sv.vertices[sv.triangles[t + 2]])}));
            }
        }
        // The mesh: every edge once (two triangles share most of them), in one call, without antialiasing -- a mesh has
        // hundreds of edges, and thin lines drawn one by one, smoothed, made each repaint of a skinned clip cost tens of ms.
        {
            std::vector<uint32_t> seen;
            seen.reserve(sv.triangles.size());
            QVector<QLineF> lines;
            lines.reserve(int(sv.triangles.size()));
            std::vector<QPointF> pts(sv.vertices.size());
            for (size_t v = 0; v < sv.vertices.size(); v++) pts[v] = at(sv.vertices[v]);
            for (size_t t = 0; t + 2 < sv.triangles.size(); t += 3)
                for (int k = 0; k < 3; k++) {
                    const uint16_t a = sv.triangles[t + size_t(k)], b = sv.triangles[t + size_t((k + 1) % 3)];
                    seen.push_back(uint32_t(std::min(a, b)) << 16 | std::max(a, b));
                }
            std::sort(seen.begin(), seen.end());
            seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
            for (uint32_t e : seen) lines.push_back(QLineF(pts[e >> 16], pts[e & 0xffff]));
            p.save();
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setPen(QPen(QColor(255, 255, 255, 80), 1));
            p.drawLines(lines);
            p.restore();
        }
        // The points: the colours of the bones they follow, mixed by weight; or how much they follow the selected bone.
        for (size_t v = 0; v < sv.vertices.size(); v++) {
            double r = 0, g = 0, bl = 0, sum = 0, mine = 0;
            for (int k = 0; k < toms::anim::kSkinInfluences; k++) {
                const float w = sv.weights[v * 2 * toms::anim::kSkinInfluences + 2 * size_t(k) + 1];
                const int b = int(sv.weights[v * 2 * toms::anim::kSkinInfluences + 2 * size_t(k)]);
                if (w <= 0 || b < 0 || b >= bones) continue;
                const QColor c = boneColor(b);
                r += w * c.redF(); g += w * c.greenF(); bl += w * c.blueF(); sum += w;
                if (b == selBone) mine += w;
            }
            QColor col;
            if (heat) {
                col = weightColor(sum > 0 ? mine / sum : 0);
            } else if (selBone >= 0) {
                const double f = sum > 0 ? mine / sum : 0, dim = 0.18;
                const QColor c = boneColor(selBone);
                col = QColor::fromRgbF(float(dim + (c.redF() - dim) * f), float(dim + (c.greenF() - dim) * f), float(dim + (c.blueF() - dim) * f));
            } else {
                col = sum > 0 ? QColor::fromRgbF(float(r / sum), float(g / sum), float(bl / sum)) : QColor(128, 128, 128);
            }
            const QPixmap& dot = vertexDot(col, heat ? 2.5 : 3.5, p.device() ? p.device()->devicePixelRatioF() : 1.0);
            const QPointF c = at(sv.vertices[v]);
            p.drawPixmap(c - QPointF(dot.width(), dot.height()) / (2.0 * dot.devicePixelRatio()), dot);
        }
        // The bones: the segment each one's weights are measured against, where the bone has carried it now.
        for (int b = 0; b < bones; b++) {
            const QPointF s = at(sv.boneStart[size_t(b)]), e = at(sv.boneEnd[size_t(b)]);
            const bool picked = b == selBone;
            const bool hovered = (m_mode == EditMode::Bones || m_mode == EditMode::Weights) && !picked && sv.boneFound[size_t(b)] &&
                                 QString::fromStdString(sv.bones[size_t(b)]) == m_hoverBone;
            const QColor c = hovered ? boneColor(b).lighter(135) : boneColor(b);
            if (hovered) {
                p.setPen(QPen(QColor(255, 255, 255, 140), 6, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(s, e);
            }
            if (picked) {
                p.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(s, e);
            }
            p.setPen(QPen(sv.boneFound[size_t(b)] ? c : QColor(255, 60, 60), picked ? 4 : 3,
                          sv.boneFound[size_t(b)] ? Qt::SolidLine : Qt::DashLine, Qt::RoundCap));
            p.drawLine(s, e);
            p.setBrush(c);
            p.setPen(QPen(picked ? Qt::white : QColor(0, 0, 0, 200), picked ? 2 : 1));
            p.drawEllipse(s, 5.5, 5.5);
            drawLabel(p, s + QPointF(8, -22), sv.boneFound[size_t(b)] ? QString::fromStdString(sv.bones[size_t(b)])
                                                                      : tr("%1 (no such node)").arg(QString::fromStdString(sv.bones[size_t(b)])));
        }
    }
}

// A point of a skin's mesh: a dot in `col` with a dark rim, made once per colour (to 5 bits a channel) and size, then
// stamped -- hundreds of points a repaint, each an antialiased ellipse with its own pen and brush, were most of its cost.
const QPixmap& AnimViewport::vertexDot(const QColor& col, double radius, double dpr) const
{
    const QColor q((col.red() & 0xf8) | 4, (col.green() & 0xf8) | 4, (col.blue() & 0xf8) | 4);
    const quint64 key = quint64(q.rgb() & 0xffffff) | quint64(radius * 2) << 24 | quint64(dpr * 4) << 32;
    auto it = m_dots.find(key);
    if (it != m_dots.end()) return it.value();
    const double size = 2 * radius + 2;
    QPixmap pm(int(std::ceil(size * dpr)), int(std::ceil(size * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter d(&pm);
    d.setRenderHint(QPainter::Antialiasing, true);
    d.setPen(QPen(QColor(0, 0, 0, 200), 1));
    d.setBrush(q);
    d.drawEllipse(QPointF(size / 2, size / 2), radius, radius);
    d.end();
    if (m_dots.size() > 4096) m_dots.clear();
    return m_dots.insert(key, pm).value();
}

QColor AnimViewport::weightColor(double w)
{
    // blue -> cyan -> green -> yellow -> red, as most weight painters show it
    static const QColor kStops[] = {QColor(30, 50, 170), QColor(0, 190, 230), QColor(40, 200, 70), QColor(250, 220, 40),
                                    QColor(235, 50, 40)};
    const double x = std::clamp(w, 0.0, 1.0) * 4.0;
    const int i = std::min(3, int(x));
    const double f = x - i;
    const QColor &a = kStops[i], &b = kStops[i + 1];
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f));
}

void AnimViewport::setBrushRadius(double r)
{
    r = std::clamp(r, 4.0, 400.0);
    if (r == m_brushRadius) return;
    m_brushRadius = r;
    update();
    emit brushChanged();
}

void AnimViewport::setBrushStrength(double s)
{
    s = std::clamp(s, 0.01, 1.0);
    if (s == m_brushStrength) return;
    m_brushStrength = s;
    update();
    emit brushChanged();
}

void AnimViewport::drawBrush(QPainter& p) const
{
    if (m_mode != EditMode::Weights || m_cursor.x() < -1e8 || !rect().contains(m_cursor.toPoint())) return;
    const QColor c = m_paintMode == animed::WeightBrush::Subtract ? QColor(120, 170, 255)
                     : m_paintMode == animed::WeightBrush::Smooth ? QColor(200, 200, 200) : QColor(255, 140, 90);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0, 0, 0, 160), 3));
    p.drawEllipse(m_cursor, m_brushRadius, m_brushRadius);
    p.setPen(QPen(c, 1.5));
    p.drawEllipse(m_cursor, m_brushRadius, m_brushRadius);
    // the strength: an inner ring
    p.setPen(QPen(c, 1, Qt::DotLine));
    p.drawEllipse(m_cursor, m_brushRadius * m_brushStrength, m_brushRadius * m_brushStrength);
}

QString AnimViewport::weightsUnderCursor() const
{
    const toms::anim::Clip* clip = m_doc->drawClip();
    if (!clip || m_cursor.x() < -1e8) return QString();
    const glm::mat3 place = toms::anim::placement(0, 0);
    const QTransform view = viewTransform();
    const Node* sel = m_doc->selectedNode();
    QString best;
    double bestD = 10.0;   // widget pixels
    for (size_t i = 0; i < m_poses.size(); i++) {
        const NodePose& pose = m_poses[i];
        if (!pose.node || !pose.node->skinned() || !pose.visible || !m_doc->soloShows(pose.node)) continue;
        toms::anim::SkinView sv;
        if (toms::anim::skinView(*clip, m_poses, i, m_doc->atlasSet(), place, &sv) != toms::anim::SkinState::Ok) continue;
        if (!sel || std::find(sv.bones.begin(), sv.bones.end(), sel->name) == sv.bones.end()) continue;
        for (size_t v = 0; v < sv.vertices.size(); v++) {
            const QPointF pt = view.map(QPointF(sv.vertices[v].x, sv.vertices[v].y));
            const double d = std::hypot(m_cursor.x() - pt.x(), m_cursor.y() - pt.y());
            if (d >= bestD) continue;
            bestD = d;
            QStringList parts;
            for (int k = 0; k < toms::anim::kSkinInfluences; k++) {
                const float w = sv.weights[v * 2 * toms::anim::kSkinInfluences + 2 * size_t(k) + 1];
                const int b = int(sv.weights[v * 2 * toms::anim::kSkinInfluences + 2 * size_t(k)]);
                if (w > 0 && b >= 0 && b < int(sv.bones.size()))
                    parts << QStringLiteral("%1 %2").arg(QString::fromStdString(sv.bones[size_t(b)])).arg(w, 0, 'f', 2);
            }
            best = tr("%1 point %2: %3%4").arg(QString::fromStdString(pose.node->name)).arg(v).arg(parts.join(QStringLiteral(", ")))
                       .arg(sv.autoWeights ? tr("  (automatic weights)") : QString());
        }
    }
    return best;
}

bool AnimViewport::paintWeightsAt(const QPointF& w, animed::WeightBrush mode, int stroke)
{
    const toms::anim::Clip* clip = m_doc->drawClip();
    const Node* sel = m_doc->selectedNode();
    if (!clip || !sel) return false;
    rebuild();
    const glm::mat3 place = toms::anim::placement(0, 0);
    const QTransform view = viewTransform();
    const double scale = std::max(1e-9, std::sqrt(std::fabs(view.determinant())));   // widget pixels per clip pixel
    const QPointF c = view.inverted().map(w);
    const float radius = float(m_brushRadius / scale);
    struct Paint { NodePath path; std::vector<float> weights; };
    std::vector<Paint> paints;
    for (size_t i = 0; i < m_poses.size(); i++) {
        const NodePose& pose = m_poses[i];
        if (!pose.node || !pose.node->skinned() || !pose.visible || !m_doc->soloShows(pose.node)) continue;
        toms::anim::SkinView sv;
        if (toms::anim::skinView(*clip, m_poses, i, m_doc->atlasSet(), place, &sv) != toms::anim::SkinState::Ok) continue;
        int bone = -1;
        for (size_t b = 0; b < sv.bones.size(); b++)
            if (sv.boneFound[b] && sv.bones[b] == sel->name) bone = int(b);
        if (bone < 0) continue;
        std::vector<float> amount(sv.vertices.size(), 0.0f);
        bool any = false;
        for (size_t v = 0; v < sv.vertices.size(); v++) {
            const float d = std::hypot(sv.vertices[v].x - float(c.x()), sv.vertices[v].y - float(c.y()));
            amount[v] = float(m_brushStrength) * animed::brushFalloff(d, radius);
            any = any || amount[v] > 0;
        }
        if (!any) continue;
        // Subtract on a point only this bone moves: the nearest other bone (its segment, as drawn now) takes the share.
        std::vector<float> fallback(sv.vertices.size() * 2 * toms::anim::kSkinInfluences, 0.0f);
        for (size_t v = 0; v < sv.vertices.size(); v++) {
            int nearest = -1;
            double nearD = 1e30;
            for (size_t b = 0; b < sv.bones.size(); b++) {
                if (int(b) == bone || !sv.boneFound[b]) continue;
                const double d = distToSegment(QPointF(sv.vertices[v].x, sv.vertices[v].y), QPointF(sv.boneStart[b].x, sv.boneStart[b].y),
                                               QPointF(sv.boneEnd[b].x, sv.boneEnd[b].y));
                if (d < nearD) { nearD = d; nearest = int(b); }
            }
            fallback[v * 2 * toms::anim::kSkinInfluences] = float(std::max(0, nearest));
            fallback[v * 2 * toms::anim::kSkinInfluences + 1] = nearest >= 0 ? 1.0f : 0.0f;
        }
        Paint pt;
        pt.weights = sv.weights;   // automatic until the first stroke: painting starts from them
        animed::paintWeights(pt.weights, int(sv.bones.size()), bone, mode, amount, sv.triangles, &fallback);
        if (pt.weights == sv.weights) continue;   // nothing moved (Subtract on a point no other bone can take)
        if (!findPath(clip->root, pose.node, pt.path)) continue;
        paints.push_back(std::move(pt));
    }
    bool changed = false;
    const QString key = QStringLiteral("#weights%1").arg(stroke);   // '#': the whole stroke is one undo step
    for (const Paint& pt : paints)
        changed |= m_doc->editNode(pt.path, tr("Paint Weights"), [&pt](Node& n) { n.skinWeights = pt.weights; }, key);
    return changed;
}

AnimViewport::Gizmo AnimViewport::gizmo() const
{
    Gizmo g;
    const toms::anim::Clip* clip = m_doc->drawClip();
    const Node* sel = m_doc->drawNode(m_doc->selectedNode());
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
    const glm::mat3 pw = parent ? toms::anim::flatten(parent->world) : glm::mat3(1.0f);
    const glm::mat3 mw = toms::anim::flatten(me->world);
    g.valid = true;
    g.origin = toWidget(QPointF(mw[2][0], mw[2][1]));
    g.parentX = unit(QPointF(pw[0][0], pw[0][1]));
    g.parentY = unit(QPointF(pw[1][0], pw[1][1]));
    g.nodeX = unit(QPointF(mw[0][0], mw[0][1]));
    g.nodeY = unit(QPointF(mw[1][0], mw[1][1]));
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
    const toms::anim::Clip* clip = m_doc->drawClip();   // m_poses are the drawn copy's
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
    if (m_mode == EditMode::Weights) {   // a joint picks its bone; anywhere else paints the selected bone
        const QString joint = boneAt(e->position(), true);
        if (!joint.isEmpty()) {
            m_doc->selectNodeByName(joint);
            return;
        }
        m_paintMode = (e->modifiers() & Qt::ShiftModifier) ? animed::WeightBrush::Smooth
                      : (e->modifiers() & Qt::ControlModifier) ? animed::WeightBrush::Subtract : animed::WeightBrush::Add;
        m_painting = true;
        m_dragSerial++;
        emit dragStarted();
        m_lastDab = e->position();
        paintWeightsAt(e->position(), m_paintMode, m_dragSerial);
        update();
        return;
    }
    Gizmo g = gizmo();
    Drag d = handleAt(e->position(), g);
    if (d == Drag::None && m_mode == EditMode::Lattice) {   // Lattice mode: a point of the selected node's lattice
        const int point = latticePointAt(e->position());
        toms::anim::LatticeView lv;
        if (point >= 0 && selectedLattice(lv)) {
            m_dragPoint = point;
            m_dragStart = e->position();
            m_dragStartDeform.clear();
            for (size_t i = 0; i < lv.offsets.size(); i++)
                if (lv.offsets[i] != glm::vec2(0.0f)) m_dragStartDeform[int(i)] = lv.offsets[i];
            // content -> sprite pixels: the inverse of the lattice's own direction and scale on screen
            const glm::mat2 toContent(glm::vec2(lv.spriteToPlaced[0]), glm::vec2(lv.spriteToPlaced[1]));
            m_dragToSprite = std::fabs(glm::determinant(toContent)) > 1e-9f ? glm::inverse(toContent) : glm::mat2(1.0f);
            m_dragSerial++;
            emit dragStarted();   // playback pauses: the key goes to the frame on screen
            update();
            return;
        }
    }
    if (d == Drag::None && m_mode == EditMode::Bones) {   // Bones mode: only bones are picked, never sprites
        const QString bone = boneAt(e->position());
        if (bone.isEmpty() || !m_doc->selectNodeByName(bone)) return;
        if (m_tool != Tool::Move) return;
        rebuild();
        g = gizmo();
        d = Drag::MoveFree;   // drag a bone to move it
    }
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
    emit dragStarted();   // playback pauses first: the drag edits the frame on screen
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
    m_cursor = e->position();
    if (m_mode == EditMode::Weights) {
        if (m_painting) {   // a dab every fifth of the brush along the way
            const double step = std::max(2.0, m_brushRadius * 0.2);
            const QPointF d = e->position() - m_lastDab;
            if (std::hypot(d.x(), d.y()) >= step) {
                m_lastDab = e->position();
                paintWeightsAt(e->position(), m_paintMode, m_dragSerial);
            }
        } else {
            m_paintMode = (e->modifiers() & Qt::ShiftModifier) ? animed::WeightBrush::Smooth
                          : (e->modifiers() & Qt::ControlModifier) ? animed::WeightBrush::Subtract : animed::WeightBrush::Add;
            rebuild();
            const QString joint = boneAt(e->position(), true);
            m_hoverBone = joint;
            setCursor(joint.isEmpty() ? Qt::CrossCursor : Qt::PointingHandCursor);
        }
        update();
        return;
    }
    if (m_dragPoint >= 0) {   // Lattice mode: the point follows the mouse; a key at the playhead
        const QTransform inv = viewTransform().inverted();
        const QPointF c0 = inv.map(m_dragStart), c1 = inv.map(e->position());
        glm::vec2 move = m_dragToSprite * glm::vec2(float(c1.x() - c0.x()), float(c1.y() - c0.y()));
        if (m_snap) move = glm::round(move);
        animed::Value v;
        v.d = m_dragStartDeform;
        v.d[m_dragPoint] = (m_dragStartDeform.count(m_dragPoint) ? m_dragStartDeform[m_dragPoint] : glm::vec2(0.0f)) + move;
        if (v.d[m_dragPoint] == glm::vec2(0.0f)) v.d.erase(m_dragPoint);
        m_doc->setChannel(Channel::Deform, v, QStringLiteral("lattice.%1").arg(m_dragSerial));
        return;
    }
    if (m_drag != Drag::None) {
        applyDrag(e->position(), e->modifiers());
        return;
    }
    rebuild();
    if (m_mode == EditMode::Lattice) {
        const int point = latticePointAt(e->position());
        if (point != m_hoverPoint) {
            m_hoverPoint = point;
            update();
        }
        if (point >= 0) { setCursor(Qt::SizeAllCursor); return; }
    }
    if (m_mode == EditMode::Bones) {
        const QString bone = boneAt(e->position());
        const Drag h = handleAt(e->position(), gizmo());
        setCursor(h != Drag::None ? Qt::PointingHandCursor : !bone.isEmpty() ? (m_tool == Tool::Move ? Qt::SizeAllCursor : Qt::PointingHandCursor)
                                                                             : Qt::ArrowCursor);
        if (bone != m_hoverBone || m_hover != -1) {
            m_hoverBone = bone;
            m_hover = -1;
            update();
        }
        return;
    }
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
    if (e->button() == Qt::LeftButton && m_painting) {
        m_painting = false;
        update();
        return;
    }
    if (e->button() == Qt::LeftButton && m_dragPoint >= 0) {
        m_dragPoint = -1;
        update();
        return;
    }
    if (e->button() == Qt::LeftButton && m_drag != Drag::None) {
        m_drag = Drag::None;
        update();
    }
}

void AnimViewport::leaveEvent(QEvent* e)
{
    m_cursor = QPointF(-1e9, -1e9);   // the brush and the point readout go with the cursor
    if (m_mode == EditMode::Weights) update();
    GameCanvasView::leaveEvent(e);
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
        v.v.z += float(delta);   // the screen-plane turn (rot z); x / y tilts are typed in Properties
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
            v.v.z *= float(f);   // uniform: all three
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

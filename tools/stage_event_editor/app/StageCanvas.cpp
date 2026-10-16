// StageCanvas.cpp -- see StageCanvas.h.
#include "StageCanvas.h"

#include "SpriteBank.h"
#include "StageDraw.h"
#include "StageEditor.h"
#include "Theme.h"
#include "stage_checks.h"

#include "anim_clip.h"
#include "anim_player.h"
#include "bgfx_renderer.h"

#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

using stage::Cell;
using stage::Json;

namespace {
constexpr int TS = 32;   // the game's tile size (manifest "tile")
const float kWhite[4] = {1, 1, 1, 1};
bool isWhite(const float t[4]) { return t[0] == 1 && t[1] == 1 && t[2] == 1 && t[3] == 1; }
}  // namespace

StageCanvas::StageCanvas(StageEditor* editor, QWidget* parent)
    : GameCanvasView(GameCanvasView::gameRendererWanted(QStringLiteral("StageEventEditor")), parent), m_ed(editor) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 160);
    m_clock.start();
    // animations: repaint while clips are on screen (the game renderer only; QPainter draws still sprites)
    m_tick.setInterval(33);
    connect(&m_tick, &QTimer::timeout, this, [this] { if (m_animated && isVisible() && usingGameRenderer()) update(); });
    m_tick.start();
}

StageCanvas::~StageCanvas() { releaseGameRenderer(); }

QSize StageCanvas::contentSize() const {
    const stage::StageDoc* d = m_ed->curDoc();
    return d ? QSize(d->width() * TS, d->height() * TS) : QSize(TS * 8, TS * 8);
}

QRectF StageCanvas::cellRect(int x, int y, int w, int h) const { return toWidget(QRectF(x * TS, y * TS, w * TS, h * TS)); }

std::optional<Cell> StageCanvas::cellAt(const QPointF& wp) const {
    const stage::StageDoc* d = m_ed->curDoc();
    if (!d) return std::nullopt;
    const QPointF c = toContent(wp);
    const int x = (int)std::floor(c.x() / TS), y = (int)std::floor(c.y() / TS);
    if (!d->inside(x, y)) return std::nullopt;
    return Cell{x, y};
}

void StageCanvas::showCell(const Cell& c) { centerOn(QRectF(c.x * TS - TS * 3, c.y * TS - TS * 3, TS * 7, TS * 7)); update(); }

void StageCanvas::gameRendererChanged(toms::next::BgfxRenderer* ren) {
    m_ren = ren;
    m_animTex.clear();
    if (!ren) { m_tex.reset(); return; }
    m_tex = std::make_unique<GameTextures>();
    m_tex->attach(ren);
    m_atlasDirty = true;
}

// ---- the game renderer -----------------------------------------------------------------------------------------------

void StageCanvas::drawSprite(const GameScene& s, const QString& sprite, const QRectF& r, const float tint[4]) {
    if (!m_tex) return;
    uint16_t tex = kSpriteAtlasTexture;
    const toms::AtlasRegion* reg = m_tex->atlasSet().find(sprite.toStdString(), &tex);
    if (!reg) return;
    Quad q;
    q.rect[0] = float(r.x()); q.rect[1] = float(r.y()); q.rect[2] = float(r.width()); q.rect[3] = float(r.height());
    for (int i = 0; i < 4; i++) { q.uv[i] = reg->uv[i]; q.tint[i] = tint[i]; }
    q.texture = tex;
    s.ren.drawSprite(q);
}

bool StageCanvas::drawClip(const GameScene& s, const std::string& ref, const QRectF& r, int tx, int ty, const float tint[4]) {
    const SpriteBank::Anim* owner = nullptr;
    const toms::anim::Clip* clip = m_ed->bank().clip(ref, &owner);
    if (!clip || !owner) return false;
    std::unique_ptr<GameTextures>& tex = m_animTex[owner->path];
    if (!tex) {
        tex = std::make_unique<GameTextures>();
        tex->attach(m_ren);
        std::vector<std::tuple<std::string, std::string, const toms::AtlasFile*>> list;
        for (const auto& [id, path, atlas] : owner->atlases) list.emplace_back(id, path, atlas.get());
        tex->setAtlases(list);
    }
    // as Game::drawObjectAnim: each tile its own phase, so a row of slimes does not move in step
    const float len = clip->duration();
    const double phase = (double)(((unsigned)tx * 73856093u ^ (unsigned)ty * 19349663u) % 997u) / 997.0 * len;
    const float t = len > 0 ? (float)std::fmod(m_clock.elapsed() / 1000.0 + phase, (double)len) : 0.0f;
    std::vector<toms::anim::NodePose> poses;
    toms::anim::evaluate(*clip, t, poses);
    // authored around a 32x32 sprite with its pivot at the bottom centre: placed on the rect's bottom centre, scaled to it
    const glm::mat3 place = toms::anim::flatten(toms::anim::compose(
        {float(r.x() + r.width() * 0.5), float(r.y() + r.height()), 0}, glm::vec3(0), {float(r.width() / TS), float(r.height() / TS), 1}));
    std::vector<Quad> quads;
    toms::anim::appendQuads(poses, tex->atlasSet(), place, isWhite(tint) ? nullptr : tint, quads);
    for (const Quad& q : quads) s.ren.drawSprite(q);
    if (!quads.empty()) m_animated++;
    return !quads.empty();
}

void StageCanvas::paintGameScene(const GameScene& s) {
    m_animated = 0;
    stage::StageDoc* d = m_ed->curDoc();
    if (!d || !m_tex) return;
    stage::Project& p = m_ed->project();
    if (m_atlasDirty) {
        m_atlasDirty = false;
        m_tex->setAtlases({{"game", m_ed->bank().atlasPath().toStdString(), &m_ed->bank().atlas()}});
    }
    const QColor bg = QColor(0x12, 0x13, 0x16);
    s.rect(QRectF(0, 0, width(), height()), bg);
    // ground first, then objects in row order (a 2-wide monster spills to the right, like the game)
    for (int y = 0; y < d->height(); y++)
        for (int x = 0; x < d->width(); x++)
            drawSprite(s, p.semAt(*d, x, y) == "wall" ? QStringLiteral("wall") : QStringLiteral("floor"), cellRect(x, y), kWhite);
    for (int y = 0; y < d->height(); y++)
        for (int x = 0; x < d->width(); x++) {
            const std::string sem = p.semAt(*d, x, y);
            if (!stage::Project::isEntity(sem)) continue;
            const stage::Footprint fp = sem.rfind("monster:", 0) == 0 ? p.footprintOf(*d, d->at(x, y), sem) : stage::Footprint{};
            const QRectF r = cellRect(x, y, fp.w, fp.h);
            if (sem.rfind("event:", 0) == 0) {   // the story plate (game_scene_draw.cpp)
                s.rect(r, QColor(219, 184, 92, 235));
                const double i = std::min(r.width(), r.height()) * 0.22;
                s.rect(r.adjusted(i, i, -i, -i), QColor(71, 87, 117, 242));
                continue;
            }
            const Json look = p.lookAt(*d, x, y, sem);
            float tint[4];
            StageDraw::lookTint(look, tint);
            const Json* own = stage::Project::objOf(*d, x, y);
            const bool ownSprite = own && own->contains("look") && (*own)["look"].is_object() && (*own)["look"].contains("sprite");
            if (look.contains("anim") && look["anim"].is_string() && drawClip(s, look["anim"].get<std::string>(), r, x, y, tint)) continue;
            if (sem.rfind("monster:", 0) == 0 && !ownSprite && drawClip(s, "anim/monster_idle.anim#idle_" + sem.substr(8), r, x, y, tint)) continue;
            drawSprite(s, StageDraw::lookSprite(p, m_ed->bank(), sem, look), r, tint);
            float wash[4];
            if (StageDraw::gateWash(p, sem, wash)) s.rect(r, QColor::fromRgbF(wash[0], wash[1], wash[2], wash[3]));
        }
}

// ---- QPainter: the map without a GPU, and the editor's marks ---------------------------------------------------------

void StageCanvas::paintSceneQt(QPainter& g) {
    stage::StageDoc* d = m_ed->curDoc();
    g.fillRect(rect(), QColor(0x12, 0x13, 0x16));
    if (!d) return;
    stage::Project& p = m_ed->project();
    for (int y = 0; y < d->height(); y++)
        for (int x = 0; x < d->width(); x++)
            StageDraw::drawSem(g, p, m_ed->bank(), p.semAt(*d, x, y) == "wall" ? "wall" : "floor", cellRect(x, y));
    for (int y = 0; y < d->height(); y++)
        for (int x = 0; x < d->width(); x++) {
            const std::string sem = p.semAt(*d, x, y);
            if (!stage::Project::isEntity(sem)) continue;
            const stage::Footprint fp = sem.rfind("monster:", 0) == 0 ? p.footprintOf(*d, d->at(x, y), sem) : stage::Footprint{};
            StageDraw::drawSem(g, p, m_ed->bank(), sem, cellRect(x, y, fp.w, fp.h), p.lookAt(*d, x, y, sem));
        }
}

void StageCanvas::paintContent(QPainter& g) {
    if (!overGameScene()) paintSceneQt(g);
    if (m_ed->curDoc()) paintMarks(g);
}

void StageCanvas::paintMarks(QPainter& g) {
    stage::StageDoc& d = *m_ed->curDoc();
    stage::Project& p = m_ed->project();
    const StageEditor::View& v = m_ed->view();
    const double s = TS * zoom();
    g.setRenderHint(QPainter::Antialiasing, false);
    const bool marks = !v.gameView;
    const std::vector<char> reach = marks ? stage::reachableCells(p, d) : std::vector<char>();
    for (int y = 0; y < d.height(); y++)
        for (int x = 0; x < d.width(); x++) {
            const std::string sem = p.semAt(d, x, y);
            const QRectF r = cellRect(x, y);
            if (!stage::Project::isEntity(sem)) {
                if (sem.empty() && marks) g.fillRect(r, QColor(255, 93, 93, 140));   // a char with no legend entry
                continue;
            }
            if (!marks) continue;
            // ✎ its own look, ▶ an animation, ⚡n linked events
            const Json* o = stage::Project::objOf(d, x, y);
            const Json look = p.lookAt(d, x, y, sem);
            std::vector<std::pair<QString, QColor>> badges;
            if (o && o->contains("look") && (*o)["look"].is_object() && !(*o)["look"].empty()) badges.push_back({QStringLiteral("✎"), QColor("#4c9eff")});
            if (look.contains("anim")) badges.push_back({QStringLiteral("▶"), QColor("#4cd18a")});
            if (o && o->contains("events") && (*o)["events"].is_array() && !(*o)["events"].empty())
                badges.push_back({QStringLiteral("⚡%1").arg((*o)["events"].size()), QColor("#ffd24c")});
            if (!badges.empty()) {
                QFont f = font();
                f.setBold(true);
                const int fs = std::max(9, int(std::lround(s * 0.22)));
                f.setPixelSize(fs);
                g.setFont(f);
                for (size_t i = 0; i < badges.size(); i++) {
                    const double tw = g.fontMetrics().horizontalAdvance(badges[i].first) + 4;
                    const QRectF b(r.x() + 1, r.bottom() - (double)(i + 1) * (fs + 3), tw, fs + 2);
                    g.fillRect(b, QColor(0, 0, 0, 166));
                    g.setPen(badges[i].second);
                    g.drawText(b.adjusted(2, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, badges[i].first);
                }
            }
            if (sem.rfind("event:", 0) == 0) {   // the event's kind colour in the corner; red when no pool defines it
                const stage::Event* e = p.event(sem.substr(6));
                g.setRenderHint(QPainter::Antialiasing, true);
                g.setPen(Qt::NoPen);
                g.setBrush(e ? StageDraw::kindColor(p, e->kind) : QColor("#ff5d5d"));
                g.drawEllipse(QPointF(r.right() - 6, r.top() + 6), 4, 4);
                g.setBrush(Qt::NoBrush);
                g.setRenderHint(QPainter::Antialiasing, false);
            }
            if (!reach.empty() && !reach[(size_t)y * d.width() + x] && sem != "player_start") {
                QPen pen(QColor(255, 159, 67, 230), 2, Qt::DashLine);
                g.setPen(pen);
                g.drawRect(r.adjusted(2, 2, -2, -2));
            }
        }
    if ((v.showSafe || v.placeMode) && marks) {
        for (const Cell& c : stage::safeCells(p, m_ed->curId())) {
            const QRectF r = cellRect(c.x, c.y).adjusted(3, 3, -3, -3);
            g.fillRect(r, QColor(76, 209, 138, 56));
            g.setPen(QPen(QColor(76, 209, 138, 140), 1));
            g.drawRect(r);
        }
    }
    if (v.showGrid && marks) {
        g.setPen(QPen(QColor(255, 255, 255, 20), 1));
        const QRectF all = cellRect(0, 0, d.width(), d.height());
        for (int x = 0; x <= d.width(); x++) g.drawLine(QPointF(std::round(all.left() + x * s) + 0.5, all.top()), QPointF(std::round(all.left() + x * s) + 0.5, all.bottom()));
        for (int y = 0; y <= d.height(); y++) g.drawLine(QPointF(all.left(), std::round(all.top() + y * s) + 0.5), QPointF(all.right(), std::round(all.top() + y * s) + 0.5));
    }
    if (v.rectA) {
        const int x0 = std::min(v.rectA->x, v.rectB->x), y0 = std::min(v.rectA->y, v.rectB->y);
        const int x1 = std::max(v.rectA->x, v.rectB->x), y1 = std::max(v.rectA->y, v.rectB->y);
        const QRectF r = cellRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
        g.fillRect(r, QColor(76, 158, 255, 56));
        g.setPen(QPen(QColor("#4c9eff"), 2));
        g.drawRect(r.adjusted(1, 1, -1, -1));
    }
    for (const Cell& c : v.flash) {
        g.setPen(QPen(QColor("#ff5d5d"), 3));
        g.drawRect(cellRect(c.x, c.y).adjusted(1.5, 1.5, -1.5, -1.5));
    }
    // the brush's ghost under the mouse: green where it can go, red (and why) where it cannot
    const std::string ghost = v.moveFrom ? p.semAt(d, v.moveFrom->x, v.moveFrom->y)
        : ((v.tool == StageEditor::Paint || (v.tool == StageEditor::Rect && !v.rectA)) && stage::Project::isEntity(v.brush)) ? v.brush : std::string();
    if (v.hover && !ghost.empty() && !isPanning() && !(v.moveFrom && *v.moveFrom == *v.hover)) {
        const Cell h = *v.hover;
        const stage::PlaceCheck chk = p.placeCheck(d, h.x, h.y, ghost, v.moveFrom ? &*v.moveFrom : nullptr);
        int fw = 1, fh = 1;
        for (const Cell& c : chk.cells) { fw = std::max(fw, c.x - h.x + 1); fh = std::max(fh, c.y - h.y + 1); }
        if (!chk.same) StageDraw::drawSem(g, p, m_ed->bank(), ghost, cellRect(h.x, h.y, fw, fh), p.typeLook(ghost), 0.6);
        if (!chk.ok) for (const Cell& c : chk.cells) g.fillRect(cellRect(c.x, c.y), QColor(255, 60, 60, 115));
        g.setPen(QPen(chk.ok ? QColor("#4cd18a") : QColor("#ff3c3c"), 3));
        g.drawRect(cellRect(h.x, h.y, fw, fh).adjusted(1.5, 1.5, -1.5, -1.5));
        setCursor(chk.ok ? Qt::CrossCursor : Qt::ForbiddenCursor);
        m_ed->placeHint(chk);
    } else {
        setCursor(v.tool == StageEditor::Select || v.tool == StageEditor::Edit ? Qt::ArrowCursor : Qt::CrossCursor);
        if (v.hover && marks) {
            g.setPen(QPen(QColor(255, 255, 255, 153), 1));
            g.drawRect(cellRect(v.hover->x, v.hover->y).adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }
    if (v.sel && marks) {
        g.setPen(QPen(QColor("#ffd24c"), 2.5));
        g.drawRect(cellRect(v.sel->x, v.sel->y).adjusted(1.5, 1.5, -1.5, -1.5));
    }
}

// ---- mouse -------------------------------------------------------------------------------------------------------

void StageCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    if (panPress(e)) return;
    if (e->button() != Qt::LeftButton) return;
    if (const auto c = cellAt(e->position())) m_ed->canvasPress(*c);
}

void StageCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panMove(e)) return;
    m_ed->canvasHover(cellAt(e->position()), (e->buttons() & Qt::LeftButton) != 0);
}

void StageCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (panRelease(e)) return;
    if (e->button() == Qt::LeftButton) m_ed->canvasRelease();
}

void StageCanvas::leaveEvent(QEvent* e) {
    GameCanvasView::leaveEvent(e);
    m_ed->canvasHover(std::nullopt, false);
}

// a right click that did not pan: the eyedropper (the cell's object becomes the brush)
void StageCanvas::contextMenuEvent(QContextMenuEvent* e) {
    if (const auto c = cellAt(e->pos())) m_ed->eyedropper(*c);
}

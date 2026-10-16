// StageDraw.cpp -- see StageDraw.h.
#include "StageDraw.h"

#include "SpriteBank.h"

#include <QPainter>

#include <algorithm>

using stage::Json;

namespace StageDraw {

QString spriteName(const Json& v) {
    if (!v.is_string()) return QString();
    QString s = QString::fromStdString(v.get<std::string>());
    if (s.endsWith(QStringLiteral(".png"))) s.chop(4);
    return s;
}

QString lookSprite(const stage::Project& p, const SpriteBank& bank, const std::string& sem, const Json& look) {
    const QString own = look.is_object() && look.contains("sprite") ? spriteName(look["sprite"]) : QString();
    if (!own.isEmpty() && bank.has(own)) return own;
    return QString::fromStdString(p.spriteOf(sem));
}

bool lookTint(const Json& look, float rgba[4]) {
    rgba[0] = rgba[1] = rgba[2] = rgba[3] = 1;
    if (!look.is_object() || !look.contains("tint") || !look["tint"].is_array()) return false;
    const Json& t = look["tint"];
    for (size_t i = 0; i < 4 && i < t.size(); i++)
        if (t[i].is_number()) rgba[i] = std::clamp(t[i].get<float>(), 0.0f, 1.0f);
    return true;
}

bool gateWash(const stage::Project& p, const std::string& sem, float rgba[4]) {
    if (sem.rfind("gate:", 0) != 0) return false;
    rgba[0] = 0.6f; rgba[1] = 0.3f; rgba[2] = 0.9f; rgba[3] = 0.6f;
    if (const Json* it = p.typeRec("item:" + sem.substr(5)); it && it->contains("tint") && (*it)["tint"].is_array())
        for (size_t i = 0; i < 3 && i < (*it)["tint"].size(); i++) rgba[i] = (*it)["tint"][i].get<float>();
    return true;
}

QColor kindColor(const stage::Project& p, const std::string& kind) {
    if (p.kinds.contains(kind) && p.kinds[kind].is_object() && p.kinds[kind].contains("color") && p.kinds[kind]["color"].is_string())
        return QColor(QString::fromStdString(p.kinds[kind]["color"].get<std::string>()));
    static const std::map<std::string, const char*> css = {{"relic", "#e0b44c"}, {"whisper", "#a98bff"}, {"cache", "#3fc2b5"},
        {"trap", "#ff6b6b"}, {"rescue", "#7bd86a"}, {"merchant_echo", "#ff8fc7"}, {"memory_shard", "#74c6ff"}};
    auto it = css.find(kind);
    return QColor(it != css.end() ? it->second : "#888888");
}

void drawSem(QPainter& g, const stage::Project& p, const SpriteBank& bank, const std::string& sem, const QRectF& r,
             const Json& look, qreal opacity) {
    if (sem.empty()) return;
    g.save();
    g.setOpacity(g.opacity() * opacity);
    if (sem.rfind("event:", 0) == 0) {   // the game's story plate: gold with a slate inset (game_scene_draw.cpp)
        g.fillRect(r, QColor(219, 184, 92, 235));
        const qreal i = std::min(r.width(), r.height()) * 0.22;
        g.fillRect(r.adjusted(i, i, -i, -i), QColor(71, 87, 117, 242));
        g.restore();
        return;
    }
    const QString sp = lookSprite(p, bank, sem, look);
    float tint[4];
    const bool tinted = lookTint(look, tint);
    const QImage img = sp.isEmpty() ? QImage() : tinted ? bank.tinted(sp, tint) : bank.image(sp);
    if (!img.isNull()) {
        g.drawImage(r, img);
        float wash[4];
        if (gateWash(p, sem, wash)) g.fillRect(r, QColor::fromRgbF(wash[0], wash[1], wash[2], wash[3]));
        g.restore();
        return;
    }
    static const std::map<std::string, const char*> fallback = {{"wall", "#5b5b66"}, {"floor", "#2b2b33"}, {"stairs", "#e0a83a"},
        {"door", "#c98b2b"}, {"key", "#d8c84a"}, {"gate", "#a070e0"}, {"player", "#3aa0ff"}, {"npc", "#46c46a"},
        {"monster", "#e0473a"}, {"item", "#b46bff"}};
    const size_t colon = sem.find(':');
    const std::string cat = colon != std::string::npos ? sem.substr(0, colon) : sem.rfind("stairs", 0) == 0 ? "stairs"
                          : sem == "player_start" ? "player" : sem;
    auto f = fallback.find(cat);
    g.fillRect(r.adjusted(2, 2, -2, -2), QColor(f != fallback.end() ? f->second : "#888888"));
    QFont font(QStringLiteral("Consolas"));
    font.setPixelSize(std::max(6, int(r.height() * 0.42)));
    g.setFont(font);
    g.setPen(Qt::white);
    const std::string name = colon != std::string::npos ? sem.substr(colon + 1) : sem;
    g.drawText(r, Qt::AlignCenter, QString::fromStdString(name.substr(0, 1)).toUpper());
    g.restore();
}

QImage icon(const stage::Project& p, const SpriteBank& bank, const std::string& sem, int size) {
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter g(&img);
    const QRectF r(0, 0, size, size);
    if (stage::Project::isEntity(sem) && sem.rfind("event:", 0) != 0) drawSem(g, p, bank, "floor", r);
    drawSem(g, p, bank, sem, r, p.typeLook(sem));
    return img;
}

}  // namespace StageDraw
